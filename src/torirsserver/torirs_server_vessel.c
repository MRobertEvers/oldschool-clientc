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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Monotonic hull identity, never recycled (docs/SAILING_PLAN.md S2.1).
 *
 * Process-wide rather than per-world for the same reason the map-instance pool
 * is: a serial only has to be unequal, and one counter cannot hand the same
 * number to two live hulls however many worlds exist.
 */
static int g_vessel_serial;

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
    /* Truncation toward zero, NOT lround: this table must be byte-identical
     * to the client's (3rd/toridraw shared_tables.c builds its sin table
     * with a plain cast), because the deck→root projection below must land
     * on the same tile the client's Wev_ParentFromDeck computes. With
     * lround here and truncation there, the two ends disagreed by 1-2 fine
     * units at the twelve non-cardinal headings — enough to flap the
     * rider's projected shadow across a tile boundary. */
    for( i = 0; i < TORIRSSERVER_VESSEL_ANGLE_UNITS; i++ )
        s_sin16[i] =
            (int32_t)(sin((double)i * k_two_pi / TORIRSSERVER_VESSEL_ANGLE_UNITS) * 65536.0);
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
 *  direction (0, -1) lands on (-sin, -cos), agreeing with the mover.
 *  Floor shift, no rounding term: the client's Wev_ParentFromDeck floors,
 *  and the observer projection must land on the tile the client draws. */
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

    *out_rx = (int)(((int64_t)lx * c + (int64_t)lz * s) >> 16);
    *out_rz = (int)(((int64_t)lz * c - (int64_t)lx * s) >> 16);
}

/** The transposed rotation — vessel_rotate_forward's inverse (exact up to
 *  the 16.16 floor). Floor shift like the forward, matching the client's
 *  Wev_DeckFromParent so both ends map a root point to the same deck tile. */
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

    *out_lx = (int)(((int64_t)rx * c - (int64_t)rz * s) >> 16);
    *out_lz = (int)(((int64_t)rz * c + (int64_t)rx * s) >> 16);
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

static int
vessel_debug_enabled(void);

void
ToriRSServer_VesselWaterRestampBound(struct ToriRSServer* srv)
{
    int base_x;
    int base_z;

    assert(srv);
    base_x = ToriRSServer_SceneBaseX();
    base_z = ToriRSServer_SceneBaseZ();
    /* Cacheless builds leave the bound window unbuilt — a documented fallback,
     * not a caller bug. Nothing to stamp into. */
    if( base_x < 0 )
        return;

    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; i++ )
    {
        struct ToriRSServerVessel* vessel = &srv->vessels[i];
        struct CollisionMap* cm;
        int tile_x;
        int tile_z;
        int r;

        if( !vessel->in_use || vessel->water_stamp <= 0 )
            continue;
        cm = ToriRSServer_SceneCollision(vessel->level);
        if( !cm )
            continue;
        tile_x = vessel->fine_x >> 7;
        tile_z = vessel->fine_z >> 7;
        r = vessel->water_stamp;
        if( vessel_debug_enabled() )
            fprintf(stderr,
                    "vessel: restamp hull %d r=%d at tile %d,%d into window base %d,%d\n",
                    vessel->index, r, tile_x, tile_z, base_x, base_z);
        for( int dx = -r; dx <= r; dx++ )
            for( int dz = -r; dz <= r; dz++ )
            {
                int x = tile_x + dx;
                int z = tile_z + dz;

                if( x < base_x || x >= base_x + TORIRSSERVER_SCENE_TILES ||
                    z < base_z || z >= base_z + TORIRSSERVER_SCENE_TILES )
                    continue;
                /* Never stamp over a map-instance reservation: those squares
                 * are somebody's deck, house or raid room, and water under a
                 * rider's feet reads as "walk anywhere". */
                if( ToriRSServer_MapInstanceFind(x, z) != 0 )
                    continue;
                collision_map_set_water(cm, x - base_x, z - base_z);
            }
    }
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
    /* 13 zones is the client's hard grid stride (REBUILD_WORLDENTITY decodes
     * onto the 13x13 instance array, gameproto_parse.c) — and 14+ would also
     * index the encoder's own zones[4][13][13] out of bounds. The instance
     * pool accepts 16, so refuse here, where the size is still a request. */
    if( zone_w > 13 || zone_h > 13 )
        return 0;
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
    /* Never reused, unlike the slot and the view id. See the field's comment:
     * this is the only thing that tells a wire encoder "the hull under view 1
     * is not the hull you were told about", when a free and a spawn land in
     * the same tick and both recycle the same numbers. */
    vessel->serial = ++g_vessel_serial;
    vessel->seq_id = -1;
    /* Lowest free world-view id. 0 when all 15 are taken: the hull still sails,
     * it just has no name the wire can say — see the field's comment. */
    for( int view = 1; view <= TORIRSSERVER_WEV_VIEW_MAX; view++ )
    {
        int taken = 0;

        for( int i = 0; i < TORIRSSERVER_VESSEL_MAX && !taken; i++ )
            if( srv->vessels[i].in_use && srv->vessels[i].view_id == view )
                taken = 1;
        if( !taken )
        {
            vessel->view_id = view;
            break;
        }
    }
    vessel->config_id = config_id;
    vessel->size_x_tiles = size_x_tiles;
    vessel->size_z_tiles = size_z_tiles;
    /* Full hull, scaled by footprint: the bar reads as the boat's bulk and a
     * bigger hull has more of it. Values are arbitrary until hull damage
     * exists; the SHAPE (hp == hp_max at spawn, both nonzero) is what the
     * sidepanel's 0/0 placeholder needed. */
    vessel->hp_max = 50 * (size_x_tiles > 0 ? size_x_tiles : 1) *
                     (size_z_tiles > 0 ? size_z_tiles : 1);
    vessel->hp = vessel->hp_max;
    /* Deterministic per slot, small enough for the shortest option table:
     * the composed name is stable across respawns of the same slot, and two
     * hulls afloat at once read differently. */
    {
        int slot = (int)(vessel - srv->vessels);

        vessel->name_descriptor = 1 + (slot * 7) % 20;
        vessel->name_noun = 1 + (slot * 13 + 5) % 20;
    }
    vessel->deck_src_x = -1;
    vessel->deck_src_z = -1;
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

    /* Lowest free deck-window slot (see the field's comment). 0 when all are
     * taken: the hull still sails, riders of THIS hull just lose deck
     * collision once their own window follows the hull away from the pool. */
    for( int wi = 0; wi < TORIRSSERVER_SCENE_VESSEL_WINDOW_MAX; wi++ )
    {
        int pool_index = TORIRSSERVER_SCENE_VESSEL_WINDOW_BASE + wi;
        int taken = 0;

        for( int i = 0; i < TORIRSSERVER_VESSEL_MAX && !taken; i++ )
            if( srv->vessels[i].in_use && srv->vessels[i].deck_window == pool_index )
                taken = 1;
        if( !taken )
        {
            vessel->deck_window = pool_index;
            break;
        }
    }
    if( vessel->deck_window == 0 )
        fprintf(stderr,
                "torirsserver: vessel %d spawned with no free deck window — "
                "riders cannot walk this deck while it sails\n",
                vessel->index);

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

    /*
     * Nobody is left standing on a deck that is about to stop existing
     * (docs/sailing_coverage.csv SAIL-54).
     *
     * A deck tile is a pool square — hundreds of squares off the real map,
     * reachable by no route, and once the vessel is gone `obs_*` collapses back
     * onto that raw tile so no other client can see them either. A rider left
     * behind is not misplaced, they are deleted from the game while still
     * logged in.
     *
     * They are put down where they LOOKED like they were standing: their own
     * deck tile projected through the hull's final transform, which is the last
     * place every other client saw them. The disembark then reads as the hull
     * vanishing from under them rather than as a teleport across the map.
     * Whether that root tile is open water is content's business — a scuttling
     * script that wants a dock moves them first; the engine's job is only to
     * refuse to strand them in the pool.
     *
     * Before the instance release, because both the ownership test and the
     * projection belong to the vessel and neither survives it.
     */
    {
        struct ToriRSServerPlayer* was_active = srv->active_player;

        for( int i = 0; i < srv->player_count; i++ )
        {
            struct ToriRSServerPlayer* player = &srv->players[i];
            int fine_x = 0;
            int fine_z = 0;

            if( !player->active )
                continue;
            if( ToriRSServer_VesselAtTile(srv, player->x, player->z) != vessel )
                continue;
            ToriRSServer_VesselDeckTileToRoot(vessel, player->x, player->z, &fine_x, &fine_z);
            /* WorldTeleport acts on the bound player, so bind each in turn and
             * put the previous binding back — the same save/restore every
             * world-scoped helper called from outside a tick does. */
            ToriRSServer_WorldSetActive(srv, player);
            ToriRSServer_WorldTeleport(srv, vessel->level, fine_x >> 7, fine_z >> 7);
        }
        ToriRSServer_WorldSetActive(srv, was_active);
    }

    /* The deck's pinned collision window goes with the deck. */
    if( vessel->deck_window != 0 )
        ToriRSServer_SceneWindowRelease(
            ToriRSServer_SceneWindowByIndex(vessel->deck_window));

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

int
ToriRSServer_VesselHeadingToward(
    int dx,
    int dz)
{
    return vessel_heading_toward(dx, dz);
}

/** Round a fine displacement to the nearest 32-unit quantum (half rounds up;
 *  the arithmetic shift makes that hold for negatives too). */
static int
vessel_quantize(int fine)
{
    return ((fine + TORIRSSERVER_VESSEL_FINE_QUANTUM / 2) >> 5) << 5;
}

/**
 * Per-tick mover trace, off unless TORIRS_VESSEL_DEBUG is set.
 *
 * The mover's failure modes are all silent: a blocked step parks the hull and
 * every later tick returns at the IDLE guard, so "sailed once and stopped" and
 * "never ticked at all" look identical from outside. This names which.
 */
static int
vessel_debug_enabled(void)
{
    static int cached = -1;

    if( cached < 0 )
        cached = getenv("TORIRS_VESSEL_DEBUG") ? 1 : 0;
    return cached;
}

/** Re-walk the footprint samples of a refused step and name each tile's flag
 *  word. "Blocked" alone cannot distinguish a hull that left the stamped water
 *  patch from one still inside it that a loc bit refuses. */
static void
vessel_debug_dump_footprint(
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
            int tx;
            int tz;

            vessel_rotate_forward(angle, lx, lz, &rx, &rz);
            tx = (fine_x + rx) >> 7;
            tz = (fine_z + rz) >> 7;
            fprintf(
                stderr,
                "vessel:   sample local %d,%d -> tile %d,%d flags 0x%x %s\n",
                lx,
                lz,
                tx,
                tz,
                (unsigned)ToriRSServer_SceneTileFlags(vessel->level, tx, tz),
                ToriRSServer_VesselTileSailable(vessel->level, tx, tz) ? "ok"
                                                                       : "REFUSED");
        }
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

    if( vessel_debug_enabled() )
        fprintf(
            stderr,
            "vessel: tick state %d angle %d heading %d tier %d fine %d,%d tile %d,%d level %d\n",
            (int)vessel->state,
            vessel->angle,
            vessel->heading,
            vessel->speed_tier,
            vessel->fine_x,
            vessel->fine_z,
            vessel->fine_x >> 7,
            vessel->fine_z >> 7,
            vessel->level);

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

    /*
     * The launch controls (docs/SAILING.md §7): with the sails un-set a
     * HEADING command still TURNS the hull — the arc above has already run —
     * but it does not translate; setting the sails is the instant "go".
     * Reversing is the wiki's stationary nudge: backward at the base half
     * tile per tick, only ever with the sails down. A TARGET sail is a
     * scripted move and ignores the sails entirely.
     */
    if( vessel->state == TORIRSSERVER_VESSEL_HEADING && !vessel->sails_set )
    {
        if( !vessel->reversing )
            return;
        speed = -64;
    }

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
    {
        if( vessel_debug_enabled() )
            fprintf(
                stderr,
                "vessel: no step (ideal %d,%d residual %d,%d)\n",
                ideal_x,
                ideal_z,
                vessel->residual_x,
                vessel->residual_z);
        return;
    }

    next_x = vessel->fine_x + step_x;
    next_z = vessel->fine_z + step_z;
    if( !vessel_footprint_sailable(vessel, next_x, next_z, vessel->angle) )
    {
        if( vessel_debug_enabled() )
            fprintf(
                stderr,
                "vessel: BLOCKED step %d,%d from tile %d,%d to tile %d,%d level %d -> parked\n",
                step_x,
                step_z,
                vessel->fine_x >> 7,
                vessel->fine_z >> 7,
                next_x >> 7,
                next_z >> 7,
                vessel->level);
        if( vessel_debug_enabled() )
            vessel_debug_dump_footprint(vessel, next_x, next_z, vessel->angle);
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

/**
 * The config's pivot (archive-72 opcodes 4/5), mirrored here because the
 * server does not decode archive 72 and the projection must recenter with
 * EXACTLY the client's terms — the client's descent places deck point d at
 * hull + R(d − zone_tiles·64 − pivot), so a projection missing the pivot
 * (or using the raw tile size where the client uses the zone-rounded one)
 * draws every rider offset from where the server thinks they stand.
 * Values from cache.osrs239 (wev_test TORIRS_WEV_DUMP=1). Unknown ids: 0,0.
 */
static void
vessel_config_pivot(
    int config_id,
    int* out_px,
    int* out_pz)
{
    static const int k_px[15] = { 0, -64, 0, -64, 0, -64, -64, -64, -64, -64, 0, 0, -64, -64, -64 };
    static const int k_pz[15] = { 0, -64, -64, 0, 0, 512, 512, 512, 512, 192, -64, -64, 0, 0, 0 };

    if( config_id >= 1 && config_id <= 14 )
    {
        *out_px = k_px[config_id];
        *out_pz = k_pz[config_id];
        return;
    }
    *out_px = 0;
    *out_pz = 0;
}

/**
 * The config's deck plane (archive-72 opcode 2), mirrored like the pivots:
 * the plane a rider STANDS on. Every real boat in cache.osrs239 authors its
 * walkable planking at plane 1 (the hull at plane 0 is the loc-built shell —
 * solid collision, which is why boarding at level 0 could not walk a single
 * tile); config 4 is the plane-0 oddity. Unknown ids: 0.
 */
int
ToriRSServer_VesselDeckPlane(const struct ToriRSServerVessel* vessel)
{
    static const int k_plane[15] = { 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

    assert(vessel);
    if( vessel->config_id >= 1 && vessel->config_id <= 14 )
        return k_plane[vessel->config_id];
    return 0;
}

/** The recenter the CLIENT applies: half the ZONE-ROUNDED deck box (the wire
 *  publishes zone counts, so the client's view is zones×8 tiles regardless of
 *  the hull's own size) plus the config pivot. One helper so DeckToRoot and
 *  RootToDeck cannot drift apart. */
static void
vessel_recenter_fine(
    const struct ToriRSServerVessel* vessel,
    int* out_cx,
    int* out_cz)
{
    int px;
    int pz;

    vessel_config_pivot(vessel->config_id, &px, &pz);
    *out_cx = ((vessel->size_x_tiles + 7) / 8) * 8 * (TORIRSSERVER_VESSEL_FINE_PER_TILE / 2) +
              px;
    *out_cz = ((vessel->size_z_tiles + 7) / 8) * 8 * (TORIRSSERVER_VESSEL_FINE_PER_TILE / 2) +
              pz;
}

void
ToriRSServer_VesselDeckToRoot(
    const struct ToriRSServerVessel* vessel,
    int deck_fine_x,
    int deck_fine_z,
    int* out_fine_x,
    int* out_fine_z)
{
    int cx;
    int cz;
    int lx;
    int lz;
    int rx;
    int rz;

    assert(vessel);
    assert(vessel->size_x_tiles > 0);
    assert(vessel->size_z_tiles > 0);
    assert(out_fine_x);
    assert(out_fine_z);

    vessel_recenter_fine(vessel, &cx, &cz);
    lx = deck_fine_x - cx;
    lz = deck_fine_z - cz;
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
    int cx;
    int cz;
    int lx;
    int lz;

    assert(vessel);
    assert(vessel->size_x_tiles > 0);
    assert(vessel->size_z_tiles > 0);
    assert(out_deck_fine_x);
    assert(out_deck_fine_z);

    vessel_recenter_fine(vessel, &cx, &cz);
    vessel_rotate_inverse(
        vessel->angle, root_fine_x - vessel->fine_x, root_fine_z - vessel->fine_z, &lx, &lz);
    *out_deck_fine_x = lx + cx;
    *out_deck_fine_z = lz + cz;
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

/* ------------------------------------------------------------------ */
/* Wire-facing lookups (docs/SAILING_PLAN.md S2)                       */
/* ------------------------------------------------------------------ */

struct ToriRSServerVessel*
ToriRSServer_VesselByView(
    struct ToriRSServer* srv,
    int view_id)
{
    assert(srv);

    /* View 0 is the root world and names no vessel; an id past the registry is
     * a value off the wire, not a caller bug. */
    if( view_id <= 0 || view_id > TORIRSSERVER_WEV_VIEW_MAX )
        return NULL;
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; i++ )
        if( srv->vessels[i].in_use && srv->vessels[i].view_id == view_id )
            return &srv->vessels[i];
    return NULL;
}

struct ToriRSServerVessel*
ToriRSServer_VesselAtTile(
    struct ToriRSServer* srv,
    int tile_x,
    int tile_z)
{
    int instance;

    assert(srv);

    /* The pool already answers "which reservation is this tile in"; the vessel
     * is the one hull that owns that reservation. Cheaper than re-deriving
     * bounds here, and it agrees with the pool by construction. */
    instance = ToriRSServer_MapInstanceFind(tile_x, tile_z);
    if( instance == 0 )
        return NULL;
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; i++ )
        if( srv->vessels[i].in_use && srv->vessels[i].instance == instance )
            return &srv->vessels[i];
    return NULL;
}

void
ToriRSServer_VesselDeckZones(
    const struct ToriRSServerVessel* vessel,
    int* out_zones_x,
    int* out_zones_z)
{
    assert(vessel);
    assert(vessel->in_use);
    assert(out_zones_x);
    assert(out_zones_z);

    /* The same rounding VesselSpawn reserved the instance with — and the wire's
     * size nibbles are zone counts, so this is what goes on the wire too. */
    *out_zones_x = (vessel->size_x_tiles + 7) / 8;
    *out_zones_z = (vessel->size_z_tiles + 7) / 8;
}
