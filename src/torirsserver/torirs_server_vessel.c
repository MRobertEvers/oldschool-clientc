/*
 * Vessels — spawn, steer, move (docs/SAILING_PLAN.md S1).
 *
 * The mover here is the server half of OSRS sailing's world-entity model: the
 * deck is a stationary map instance, and what "moves" is a transform — a fine
 * position, a yaw and a hull rectangle — that this file advances once per world
 * tick. Nothing here touches the wire; S2's WORLDENTITY_INFO reads the state
 * this file maintains.
 *
 * Three decisions worth restating where the code can enforce them:
 *
 * - **Water is COLL_TYPE_BLOCKED's yes.** A tile is sailable exactly when a
 *   moverestrict=blocked npc could stand on it: it carries COLL_FLAG_FLOOR
 *   (water/lava — the flag whose *presence* blocks walkers) and none of the
 *   loc/antimacro bits. That is one `collision_can_move` call against the same
 *   CollisionMap players route on, so a dock built over the water blocks the
 *   hull the same way it blocks a fishing spot.
 *
 * - **Steps are quantized, headings are not sacred.** Every per-tick step is a
 *   multiple of 32 fine units per axis (the quarter-tile quantum the client's
 *   interpolation expects), but the trig displacement it approximates is
 *   carried exactly in per-axis residuals, so a long diagonal sail lands where
 *   the commanded heading says rather than where the rounding drifted.
 *
 * - **A blocked step stops the boat.** No sliding along the coast, no partial
 *   step: the hull either takes the whole quantized step onto water or parks
 *   (state -> IDLE) with its position untouched. Boat-vs-boat collision does
 *   not exist, faithfully to the deob.
 */

#include "torirs_server_vessel.h"

#include "engine/world_builder/collision_map.h"
#include "torirs_server.h"
#include "torirs_server_mapinstance.h"
#include "torirs_server_scene.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixed-point trig                                                    */
/* ------------------------------------------------------------------ */

/*
 * 65536-scaled sine over the 2048-unit circle, built lazily from libm (already
 * linked; torirs_server_combat.c uses math.h the same way). Server-side only, so
 * 64-bit intermediates are fine — the no-64-bit rule covers 3rd/toridraw.
 */
static int32_t s_sin16[TORIRSSERVER_VESSEL_ANGLE_UNITS];
static int s_sin16_ready;

static void
vessel_trig_init(void)
{
    static const double k_two_pi = 6.283185307179586476925286766559;
    int i;

    if( s_sin16_ready )
        return;
    for( i = 0; i < TORIRSSERVER_VESSEL_ANGLE_UNITS; i++ )
        s_sin16[i] =
            (int32_t)lround(sin((double)i * k_two_pi / TORIRSSERVER_VESSEL_ANGLE_UNITS) * 65536.0);
    s_sin16_ready = 1;
}

static int32_t
vessel_sin(int angle)
{
    vessel_trig_init();
    return s_sin16[angle & TORIRSSERVER_VESSEL_ANGLE_MASK];
}

static int32_t
vessel_cos(int angle)
{
    vessel_trig_init();
    return s_sin16[(angle + 512) & TORIRSSERVER_VESSEL_ANGLE_MASK];
}

/** Rotate a deck-local fine offset by yaw into a root-space offset. The bow
 *  direction (0, -1) lands on (-sin, -cos), agreeing with the mover. */
static void
vessel_rotate_forward(
    int angle,
    int lx,
    int lz,
    int* out_rx,
    int* out_rz)
{
    int64_t c = vessel_cos(angle);
    int64_t s = vessel_sin(angle);

    *out_rx = (int)(((int64_t)lx * c + (int64_t)lz * s + 32768) >> 16);
    *out_rz = (int)(((int64_t)lz * c - (int64_t)lx * s + 32768) >> 16);
}

/** The transposed rotation — vessel_rotate_forward's inverse (exact up to the
 *  16.16 rounding). */
static void
vessel_rotate_inverse(
    int angle,
    int rx,
    int rz,
    int* out_lx,
    int* out_lz)
{
    int64_t c = vessel_cos(angle);
    int64_t s = vessel_sin(angle);

    *out_lx = (int)(((int64_t)rx * c - (int64_t)rz * s + 32768) >> 16);
    *out_lz = (int)(((int64_t)rz * c + (int64_t)rx * s + 32768) >> 16);
}

/* ------------------------------------------------------------------ */
/* Water                                                               */
/* ------------------------------------------------------------------ */

int
ToriRSServer_VesselTileSailable(
    int level,
    int tile_x,
    int tile_z)
{
    int flags;

    assert(level >= 0);
    assert(level < TORIRSSERVER_MAPINSTANCE_LEVELS);

    /* Outside every built scene window this reads 0: FLOOR unset, so the
     * BLOCKED test below says no. Unknown map is not sailable — the same safe
     * direction ToriRSServer_SceneWalkBlocked chose for walkers. */
    flags = ToriRSServer_SceneTileFlags(level, tile_x, tile_z);
    return collision_can_move(COLL_TYPE_BLOCKED, flags, COLL_FLAG_WALK_BLOCKED);
}

/**
 * Every tile the hull would cover at (fine_x, fine_z, angle) is sailable?
 *
 * Sampled rather than rasterized: deck-space sample points sit on a 64-unit
 * (half-tile) grid inset 32 units from the hull edge, so every tile of the
 * unrotated footprint carries two samples per axis and a rotation cannot swing
 * an interior tile out from between them. Half-open enough that a hull flush
 * against the shoreline tile boundary does not read the land tile.
 */
static int
vessel_footprint_sailable(
    const struct ToriRSServerVessel* vessel,
    int fine_x,
    int fine_z,
    int angle)
{
    int half_x = vessel->size_x_tiles * (TORIRSSERVER_VESSEL_FINE_PER_TILE / 2);
    int half_z = vessel->size_z_tiles * (TORIRSSERVER_VESSEL_FINE_PER_TILE / 2);
    int lx;
    int lz;
    int rx;
    int rz;

    for( lz = -half_z + 32; lz <= half_z - 32; lz += 64 )
        for( lx = -half_x + 32; lx <= half_x - 32; lx += 64 )
        {
            vessel_rotate_forward(angle, lx, lz, &rx, &rz);
            if( !ToriRSServer_VesselTileSailable(
                    vessel->level, (fine_x + rx) >> 7, (fine_z + rz) >> 7) )
                return 0;
        }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

int
ToriRSServer_VesselSpawn(
    struct ToriRSServer* srv,
    int config_id,
    int size_x_tiles,
    int size_z_tiles,
    int level,
    int tile_x,
    int tile_z,
    int angle)
{
    struct ToriRSServerVessel* vessel;
    int slot;
    int zone_w;
    int zone_h;
    int instance;

    assert(srv);
    assert(config_id >= 0);
    assert(size_x_tiles > 0);
    assert(size_z_tiles > 0);
    assert(level >= 0);
    assert(level < TORIRSSERVER_MAPINSTANCE_LEVELS);
    assert(tile_x >= 0);
    assert(tile_z >= 0);
    assert(angle >= 0);
    assert(angle < TORIRSSERVER_VESSEL_ANGLE_UNITS);

    for( slot = 0; slot < TORIRSSERVER_VESSEL_MAX; slot++ )
        if( !srv->vessels[slot].in_use )
            break;
    /* Pool exhaustion is a capacity bug, not a state content can reach: 32
     * hulls outruns the 8-instance deck pool four times over. */
    assert(slot < TORIRSSERVER_VESSEL_MAX);

    /* Deck reservation, rounded up to whole zones (8 tiles each). The
     * instance pool being exhausted IS a state content can reach, and 0 is the
     * same "check your handle" answer map_instance_alloc gives. */
    zone_w = (size_x_tiles + 7) / 8;
    zone_h = (size_z_tiles + 7) / 8;
    instance = ToriRSServer_MapInstanceAlloc(ToriRSServer_WorldCacheDir(), zone_w, zone_h);
    if( instance == 0 )
        return 0;
    /* The vessel owns the deck's lifetime: an empty deck is a boat nobody
     * boarded, not an abandoned instance for the linger clock to reap. */
    ToriRSServer_MapInstanceSetLinger(instance, 0);

    vessel = &srv->vessels[slot];
    memset(vessel, 0, sizeof(*vessel));
    vessel->in_use = 1;
    vessel->index = slot + 1;
    vessel->config_id = config_id;
    vessel->size_x_tiles = size_x_tiles;
    vessel->size_z_tiles = size_z_tiles;
    vessel->instance = instance;
    vessel->level = level;
    vessel->fine_x = tile_x * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
    vessel->fine_z = tile_z * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
    vessel->angle = angle;
    vessel->state = TORIRSSERVER_VESSEL_IDLE;
    /* Nearest compass point to the spawn yaw, so a bare "set speed and go"
     * sails the way the hull already faces. */
    vessel->heading = ((angle + TORIRSSERVER_VESSEL_HEADING_STEP / 2) /
                      TORIRSSERVER_VESSEL_HEADING_STEP) &
                      15;
    vessel->speed_tier = TORIRSSERVER_VESSEL_SPEED_TIER_MIN;
    vessel->turn_rate = TORIRSSERVER_VESSEL_TURN_RATE_DEFAULT;

    srv->vessel_count++;
    return vessel->index;
}

int
ToriRSServer_VesselFree(
    struct ToriRSServer* srv,
    int handle)
{
    struct ToriRSServerVessel* vessel;

    assert(srv);
    vessel = ToriRSServer_VesselGet(srv, handle);
    if( !vessel )
        return 0;

    /* The world-level release, not the bare registry one: the deck may hold
     * npcs, floor objects and loc changes, and the pool re-issues its squares
     * immediately (see ToriRSServer_WorldMapInstanceFree). */
    ToriRSServer_WorldMapInstanceFree(srv, vessel->instance);
    memset(vessel, 0, sizeof(*vessel));
    srv->vessel_count--;
    return 1;
}

struct ToriRSServerVessel*
ToriRSServer_VesselGet(
    struct ToriRSServer* srv,
    int handle)
{
    struct ToriRSServerVessel* vessel;

    assert(srv);
    if( handle < 1 || handle > TORIRSSERVER_VESSEL_MAX )
        return NULL;
    vessel = &srv->vessels[handle - 1];
    return vessel->in_use ? vessel : NULL;
}

int
ToriRSServer_VesselLiveCount(struct ToriRSServer* srv)
{
    assert(srv);
    return srv->vessel_count;
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

void
ToriRSServer_VesselSetTarget(
    struct ToriRSServerVessel* vessel,
    int tile_x,
    int tile_z)
{
    assert(vessel);
    assert(vessel->in_use);
    assert(tile_x >= 0);
    assert(tile_z >= 0);

    /* Tile centers (tile*128 + 64) are already 32-unit-quantum aligned, so
     * arrival can snap to the target without breaking the step invariant. */
    vessel->target_fine_x = tile_x * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
    vessel->target_fine_z = tile_z * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64;
    vessel->state = TORIRSSERVER_VESSEL_TARGET;
    vessel->residual_x = 0;
    vessel->residual_z = 0;
}

void
ToriRSServer_VesselSetHeading(
    struct ToriRSServerVessel* vessel,
    int heading)
{
    assert(vessel);
    assert(vessel->in_use);
    assert(heading >= 0);
    assert(heading < 16);

    vessel->heading = heading;
    vessel->state = TORIRSSERVER_VESSEL_HEADING;
    vessel->residual_x = 0;
    vessel->residual_z = 0;
}

void
ToriRSServer_VesselSetSpeed(
    struct ToriRSServerVessel* vessel,
    int speed_tier)
{
    assert(vessel);
    assert(vessel->in_use);
    assert(speed_tier >= TORIRSSERVER_VESSEL_SPEED_TIER_MIN);
    assert(speed_tier <= TORIRSSERVER_VESSEL_SPEED_TIER_MAX);

    vessel->speed_tier = speed_tier;
}

void
ToriRSServer_VesselStop(struct ToriRSServerVessel* vessel)
{
    assert(vessel);
    assert(vessel->in_use);

    vessel->state = TORIRSSERVER_VESSEL_IDLE;
    vessel->residual_x = 0;
    vessel->residual_z = 0;
}

/* ------------------------------------------------------------------ */
/* Mover                                                               */
/* ------------------------------------------------------------------ */

/** The 16-point compass heading whose sail direction (-sin, -cos) best points
 *  along (dx, dz). Solving -sin t = dx, -cos t = dz gives t = atan2(-dx, -dz),
 *  rounded to the nearest multiple of 128. */
static int
vessel_heading_toward(
    int dx,
    int dz)
{
    static const double k_two_pi = 6.283185307179586476925286766559;
    int angle;

    angle = (int)lround(
                atan2((double)-dx, (double)-dz) * (TORIRSSERVER_VESSEL_ANGLE_UNITS / k_two_pi)) &
            TORIRSSERVER_VESSEL_ANGLE_MASK;
    return ((angle + TORIRSSERVER_VESSEL_HEADING_STEP / 2) / TORIRSSERVER_VESSEL_HEADING_STEP) & 15;
}

/** Round a fine displacement to the nearest 32-unit quantum (half rounds up;
 *  the arithmetic shift makes that hold for negatives too). */
static int
vessel_quantize(int fine)
{
    return ((fine + TORIRSSERVER_VESSEL_FINE_QUANTUM / 2) >> 5) << 5;
}

static void
vessel_tick(struct ToriRSServerVessel* vessel)
{
    int desired;
    int arc;
    int speed;
    int64_t s;
    int64_t c;
    int ideal_x;
    int ideal_z;
    int want_x;
    int want_z;
    int step_x;
    int step_z;
    int next_x;
    int next_z;

    if( vessel->state == TORIRSSERVER_VESSEL_IDLE )
        return;

    if( vessel->state == TORIRSSERVER_VESSEL_TARGET )
    {
        int dx = vessel->target_fine_x - vessel->fine_x;
        int dz = vessel->target_fine_z - vessel->fine_z;

        if( dx == 0 && dz == 0 )
        {
            ToriRSServer_VesselStop(vessel);
            return;
        }
        /* Re-derived every tick, so a mid-course retarget curves in. */
        vessel->heading = vessel_heading_toward(dx, dz);
    }

    /* Turn: shortest arc toward the commanded heading, capped. */
    desired = vessel->heading * TORIRSSERVER_VESSEL_HEADING_STEP;
    arc = (desired - vessel->angle) & TORIRSSERVER_VESSEL_ANGLE_MASK;
    if( arc > TORIRSSERVER_VESSEL_ANGLE_UNITS / 2 )
        arc -= TORIRSSERVER_VESSEL_ANGLE_UNITS;
    if( arc > vessel->turn_rate )
        arc = vessel->turn_rate;
    else if( arc < -vessel->turn_rate )
        arc = -vessel->turn_rate;
    vessel->angle = (vessel->angle + arc) & TORIRSSERVER_VESSEL_ANGLE_MASK;

    speed = vessel->speed_tier * 64;

    if( vessel->state == TORIRSSERVER_VESSEL_TARGET )
    {
        /* Arrival: within one tick's travel (Chebyshev, matching the per-axis
         * steps), snap to the quantum-aligned target and park. */
        int dx = vessel->target_fine_x - vessel->fine_x;
        int dz = vessel->target_fine_z - vessel->fine_z;
        int adx = dx < 0 ? -dx : dx;
        int adz = dz < 0 ? -dz : dz;

        if( (adx > adz ? adx : adz) <= speed )
        {
            if( vessel_footprint_sailable(
                    vessel, vessel->target_fine_x, vessel->target_fine_z, vessel->angle) )
            {
                vessel->fine_x = vessel->target_fine_x;
                vessel->fine_z = vessel->target_fine_z;
            }
            /* Arrived or blocked at the doorstep — parked either way. */
            ToriRSServer_VesselStop(vessel);
            return;
        }
    }

    /* Advance on the post-turn yaw: dx = -sin, dz = -cos, scaled by speed,
     * quantized to 32-unit steps with the trig remainder carried per axis. */
    s = vessel_sin(vessel->angle);
    c = vessel_cos(vessel->angle);
    ideal_x = (int)((-s * speed + 32768) >> 16);
    ideal_z = (int)((-c * speed + 32768) >> 16);

    want_x = ideal_x + vessel->residual_x;
    want_z = ideal_z + vessel->residual_z;
    step_x = vessel_quantize(want_x);
    step_z = vessel_quantize(want_z);
    vessel->residual_x = want_x - step_x;
    vessel->residual_z = want_z - step_z;

    if( step_x == 0 && step_z == 0 )
        return;

    next_x = vessel->fine_x + step_x;
    next_z = vessel->fine_z + step_z;
    if( !vessel_footprint_sailable(vessel, next_x, next_z, vessel->angle) )
    {
        /* A blocked step stops the boat: whole step or none, no sliding. */
        ToriRSServer_VesselStop(vessel);
        return;
    }

    vessel->fine_x = next_x;
    vessel->fine_z = next_z;
}

void
ToriRSServer_VesselTickAll(struct ToriRSServer* srv)
{
    int i;

    assert(srv);
    if( srv->vessel_count == 0 )
        return;
    for( i = 0; i < TORIRSSERVER_VESSEL_MAX; i++ )
        if( srv->vessels[i].in_use )
            vessel_tick(&srv->vessels[i]);
}

/* ------------------------------------------------------------------ */
/* Deck <-> root projection                                            */
/* ------------------------------------------------------------------ */

void
ToriRSServer_VesselDeckToRoot(
    const struct ToriRSServerVessel* vessel,
    int deck_fine_x,
    int deck_fine_z,
    int* out_fine_x,
    int* out_fine_z)
{
    int lx;
    int lz;
    int rx;
    int rz;

    assert(vessel);
    assert(vessel->size_x_tiles > 0);
    assert(vessel->size_z_tiles > 0);
    assert(out_fine_x);
    assert(out_fine_z);

    /* Pivot at the hull center; a config-supplied pivot is S3's concern. */
    lx = deck_fine_x - vessel->size_x_tiles * (TORIRSSERVER_VESSEL_FINE_PER_TILE / 2);
    lz = deck_fine_z - vessel->size_z_tiles * (TORIRSSERVER_VESSEL_FINE_PER_TILE / 2);
    vessel_rotate_forward(vessel->angle, lx, lz, &rx, &rz);
    *out_fine_x = vessel->fine_x + rx;
    *out_fine_z = vessel->fine_z + rz;
}

void
ToriRSServer_VesselRootToDeck(
    const struct ToriRSServerVessel* vessel,
    int root_fine_x,
    int root_fine_z,
    int* out_deck_fine_x,
    int* out_deck_fine_z)
{
    int lx;
    int lz;

    assert(vessel);
    assert(vessel->size_x_tiles > 0);
    assert(vessel->size_z_tiles > 0);
    assert(out_deck_fine_x);
    assert(out_deck_fine_z);

    vessel_rotate_inverse(
        vessel->angle, root_fine_x - vessel->fine_x, root_fine_z - vessel->fine_z, &lx, &lz);
    *out_deck_fine_x = lx + vessel->size_x_tiles * (TORIRSSERVER_VESSEL_FINE_PER_TILE / 2);
    *out_deck_fine_z = lz + vessel->size_z_tiles * (TORIRSSERVER_VESSEL_FINE_PER_TILE / 2);
}

void
ToriRSServer_VesselDeckTileToRoot(
    const struct ToriRSServerVessel* vessel,
    int deck_tile_x,
    int deck_tile_z,
    int* out_fine_x,
    int* out_fine_z)
{
    int base_x = 0;
    int base_z = 0;
    int ok;

    assert(vessel);
    assert(vessel->in_use);
    assert(vessel->instance > 0);
    assert(out_fine_x);
    assert(out_fine_z);

    ok = ToriRSServer_MapInstanceBase(vessel->instance, &base_x, &base_z);
    /* The vessel owns its reservation for its whole life; a dead handle here
     * means somebody freed the instance out from under it. */
    assert(ok);
    (void)ok;

    ToriRSServer_VesselDeckToRoot(
        vessel,
        (deck_tile_x - base_x) * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64,
        (deck_tile_z - base_z) * TORIRSSERVER_VESSEL_FINE_PER_TILE + 64,
        out_fine_x,
        out_fine_z);
}
