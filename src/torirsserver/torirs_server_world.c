/*
 * The mock's game state: the tick, the player, the npcs, the containers.
 *
 * Modelled on how xrsps-typescript's server is put together (server/src/game):
 * a fixed 600 ms tick drives everything, actors hold a queue of pending tiles
 * rather than a position delta, and idle npcs re-roll a roam every 15-30 ticks
 * within a radius of where they spawned. Nothing here predicts: a click
 * produces a queue, the queue produces one step per tick, and the step only
 * becomes visible once PLAYER_INFO says so.
 *
 * Coordinates are absolute tiles throughout. The conversion to the scene-local
 * form the info streams carry happens in torirs_server_encode.c, which is the only
 * place that needs to know about the origin zone.
 *
 * `--selftest` used to live at the bottom of this file and is now
 * torirs_server_world_selftest.c; what it reaches in here is declared in
 * torirs_server_world_internal.h and nowhere else.
 */
#include "torirs_server.h"
#include "torirs_server_gwd_manifest.gen.h"
#include "torirs_server_music_regions.gen.h"

#include "torirs_server_container.h"
#include "torirs_server_shop.h"
#include "torirs_server_content.h"
#include "torirs_server_db.h"
#include "torirs_server_equipment.h"
#include "torirs_server_friends.h"
#include "torirs_server_ids.h"
#include "torirs_server_runenergy.h"
#include "torirs_server_save.h"
#include "torirs_server_session.h"
#include "torirs_server_scene.h"
#include "torirs_server_world_internal.h"
#include "mock239_facing.h"
#include "mock239_interface_inbound.h"
#include "engine/world_builder/collision_map.h"
/* `torirs_server_scene.h` only forward-declares `struct ToriRS_FeatureTable`, so
 * reading a field off `ToriRSServer_SceneFeatures()` needs the definition. */
#include "features/features.h"
#include "ss_trigger.h"
#include "ss_meta.h"
#include "ssvm.h"
#include "ssvm_provider.h"

#include "net/jbase37.h"
#include "net/rev/pktnames.h"
#include "net/wordpack.h"

#include <rsareabuf.h>

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* The tile a session logs in on. See ToriRSServer_WorldSetHome. */
int g_home_x = 3222;
int g_home_z = 3218;

static void
ToriRSServer_WorldBuildEntities(struct ToriRSServer* srv);

/* Declared up here because the scene rebuild — the one place a window moves —
 * is a thousand lines above where the roster lives. */
static void
world_static_npcs_sync(struct ToriRSServer* srv);

static void
world_static_npcs_reset(void);

/* And the loc replay, for the same reason: the window build helper sits with
 * the rebuild machinery, the ZoneMap replay sits with the loc mutations. */
static void
world_locs_reapply_window(
    struct ToriRSServer* srv,
    struct ToriRSServerSceneWindow* window);

static void
ToriRSServer_WorldLoginFinish(struct ToriRSServerPlayer* player);

/* Where the scene reads its map squares from. Set once beside the other cache
 * loaders; kept here rather than threaded through every rebuild path because
 * the mock opens exactly one cache. */
static char g_cache_dir[512] = TORIRSSERVER_CACHE_DIR_DEFAULT;

void
ToriRSServer_WorldSetCacheDir(const char* dir)
{
    snprintf(g_cache_dir, sizeof(g_cache_dir), "%s", dir);
}

const char*
ToriRSServer_WorldCacheDir(void)
{
    return g_cache_dir;
}


/* xorshift32: a session replays identically for a given seed, which matters
 * when a screenshot test has to land on the same frame twice. */
static uint32_t
next_random(struct ToriRSServer* srv)
{
    uint32_t x = srv->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    srv->rng = x;
    return x;
}

static int
random_range(
    struct ToriRSServer* srv,
    int lo,
    int hi)
{
    if( hi <= lo )
        return lo;
    return lo + (int)(next_random(srv) % (uint32_t)(hi - lo + 1));
}

int
ToriRSServer_Random(
    struct ToriRSServer* srv,
    int lo,
    int hi)
{
    return random_range(srv, lo, hi);
}

/*
 * When an npc that has just appeared may first consider roaming.
 *
 * Spread over a window rather than set to a fixed delay so a room full of npcs
 * spawned on one tick does not step in unison. Exported because a respawn is the
 * same event as a spawn from the roamer's point of view, and torirs_server_combat.c
 * was answering it with `TORIRSSERVER_ATTACK_SPEED` — a different quantity that
 * happened to be a plausible number of ticks.
 */
void
ToriRSServer_WorldNpcRoamStagger(
    struct ToriRSServer* srv,
    struct ToriRSServerNpc* npc)
{
    npc->next_roam_tick = srv->tick + random_range(srv, 5, 30);
}

static void
say(
    struct ToriRSServer* srv,
    const char* fmt,
    ...)
{
    char text[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    ToriRSServer_SendMessage(srv->active_player, text);
}

/* ------------------------------------------------------------------ */
/* Containers                                                          */
/* ------------------------------------------------------------------ */

/*
 * Write one backpack cell.
 *
 * `count <= 0` empties it rather than parking an obj there at zero: the
 * backpack's occupancy test is `obj_id >= 0` everywhere that counts free slots
 * (`ToriRSServer_ContainerAdd`, `inv_freespace`, `inv_free_slots`), so a cell
 * holding zero of something is full and empty at once — and for an unstackable
 * it is dead for good, since the add loop only writes cells whose obj_id is
 * negative. torirs_server_bank.c's `inv_write` is the same writer with the same rule;
 * see SS_OP_INV_SETSLOT's header (torirs_server_scripts.c) for the whole shape.
 *
 * The two containers that do legitimately hold an obj at zero — bank
 * placeholders, shop baseline lines — are not the backpack and do not come
 * through here.
 */
void
inv_set(
    struct ToriRSServerPlayer* player,
    int slot,
    int obj_id,
    int count)
{
    if( slot < 0 || slot >= TORIRSSERVER_INV_SLOTS )
        return;
    if( obj_id < 0 || count <= 0 )
    {
        player->inv[slot].obj_id = -1;
        player->inv[slot].count = 0;
        for( int v = 0; v < TORIRSSERVER_ITEM_VAR_MAX; v++ )
        {
            player->inv[slot].var_key[v] = -1;
            player->inv[slot].var_val[v] = 0;
        }
    }
    else
    {
        if( player->inv[slot].obj_id != obj_id )
        {
            for( int v = 0; v < TORIRSSERVER_ITEM_VAR_MAX; v++ )
            {
                player->inv[slot].var_key[v] = -1;
                player->inv[slot].var_val[v] = 0;
            }
        }
        player->inv[slot].obj_id = obj_id;
        player->inv[slot].count = count;
    }
    player->inv_dirty |= 1u << slot;
}

void
worn_set(
    struct ToriRSServerPlayer* player,
    int slot,
    int obj_id,
    int count)
{
    if( slot < 0 || slot >= TORIRSSERVER_WORN_SLOTS )
        return;
    if( obj_id < 0 )
    {
        player->worn[slot].obj_id = -1;
        player->worn[slot].count = 0;
        for( int v = 0; v < TORIRSSERVER_ITEM_VAR_MAX; v++ )
        {
            player->worn[slot].var_key[v] = -1;
            player->worn[slot].var_val[v] = 0;
        }
    }
    else
    {
        if( player->worn[slot].obj_id != obj_id )
        {
            for( int v = 0; v < TORIRSSERVER_ITEM_VAR_MAX; v++ )
            {
                player->worn[slot].var_key[v] = -1;
                player->worn[slot].var_val[v] = 0;
            }
        }
        player->worn[slot].obj_id = obj_id;
        player->worn[slot].count = count;
    }
    player->worn_dirty |= 1u << slot;
    player->masks |= TORIRSSERVER_PMASK_APPEARANCE;
}

int
inv_first_free(const struct ToriRSServerPlayer* player)
{
    for( int i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        if( player->inv[i].obj_id < 0 )
            return i;
    return -1;
}

/* `inv_stack_slot` was here — "the slot already holding a stack of this obj".
 * `interaction_engine_obj` was its last caller and went with the `opobj`
 * fallback; the same rule now lives once, in `ToriRSServer_ContainerAdd`
 * (torirs_server_container.c), which is `Inventory.add` and is what `obj_takeitem`
 * and `inv_add` both go through. Two copies of a stacking rule is how the two
 * came to disagree in the first place. */

/* ------------------------------------------------------------------ */
/* Equipment                                                           */
/* ------------------------------------------------------------------ */

/* Take the item off worn slot `slot` and put it back in the backpack. */
void
unequip_slot(
    struct ToriRSServer* srv,
    int slot)
{
    int32_t arg;

    /*
     * Content's `[proc,unequip]` owns the move, the message, ~update_bas and
     * ~combat_weapon_category_sync. The worn-tab click and the selftest both
     * land here; naming the proc is the same debt as ToriRSServer_Say — the worn
     * path is still engine-entered until every client click is an IF_BUTTON.
     */
    if( slot < 0 || slot >= TORIRSSERVER_WORN_SLOTS )
        return;
    arg = (int32_t)slot;
    if( !ToriRSServer_ScriptsRunProc(srv, "[proc,unequip]", &arg, 1) )
    {
        /* No content pack: keep the physical move so headless boots still work. */
        struct ToriRSServerPlayer* player = srv->active_player;
        int obj_id = player->worn[slot].obj_id;
        int dest;

        if( obj_id < 0 )
            return;
        dest = inv_first_free(player);
        if( dest < 0 )
            return;
        inv_set(player, dest, obj_id, player->worn[slot].count);
        worn_set(player, slot, -1, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Movement                                                            */
/* ------------------------------------------------------------------ */

/* Drop whatever route the player was walking. Exported because arriving at a
 * combat target ends the walk, and that decision is combat's
 * (`ToriRSServer_CombatPlayerApproach`), not this file's. */
void
ToriRSServer_WorldStepsClear(struct ToriRSServerPlayer* player)
{
    player->waypoint_index = -1;
}

void
steps_clear(struct ToriRSServerPlayer* player)
{
    ToriRSServer_WorldStepsClear(player);
}

/*
 * The part of player_lock that is about the actor, not the packet stream.
 *
 * Keep this separate from clear_pending_action: that function closes modals
 * and can release the active script, while the script which called
 * player_lock must keep executing (and its queued successors must keep
 * draining). A time-stopped player gives up only the route/interaction they
 * could otherwise continue without sending another packet.
 */
static void
player_cancel_locked_actions(struct ToriRSServerPlayer* player)
{
    int had_route;

    assert(player);
    had_route = player->waypoint_index >= 0 || player->dest_x >= 0 || player->dest_z >= 0;
    steps_clear(player);
    player->dest_x = -1;
    player->dest_z = -1;
    player->face_target_x = -1;
    player->face_target_z = -1;
    if( had_route )
        player->clear_map_flag = 1;
    ToriRSServer_CombatStopPlayerAt(player);
}

/*
 * What a stun cancels the moment it lands.
 *
 * The same route/facing teardown a lock does, plus the latched interaction —
 * `player_cancel_locked_actions` leaves that to the lock's own gate, which a
 * stun does not go through. Without the interaction clear, a stunned player
 * with a pending "Attack" resumed it the instant the stun expired, having
 * queued it during the stun.
 */
void
ToriRSServer_WorldStunInterrupt(struct ToriRSServerPlayer* player)
{
    assert(player);
    player_cancel_locked_actions(player);
    player->interaction.kind = TORIRSSERVER_INTERACT_NONE;
    player->interaction.npc_slot = -1;
}

void
ToriRSServer_WorldPlayerLock(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    player = srv->active_player;
    assert(player);
    player->action_locked = 1;
    player_cancel_locked_actions(player);
}

void
ToriRSServer_WorldPlayerUnlock(struct ToriRSServer* srv)
{
    assert(srv);
    assert(srv->active_player);
    srv->active_player->action_locked = 0;
}

static int
player_has_waypoints(struct ToriRSServerPlayer const* player)
{
    return player->waypoint_index >= 0;
}

/*
 * `Player.blockWalkFlag()` — `BLOCK_NPC_AND_PLAYERS`, and nothing else.
 *
 * Not NPC_OCC: a player walks *through* an ordinary npc and is stopped only by
 * the ones that hard-block, which is `blockwalk=all` (`npc_occupancy_mask`) and
 * the default is `blockwalk=npc`. NPC_OCC is the npcs' own affair — it is what
 * keeps them from standing on each other.
 *
 * Reading NPC_OCC here was invisible for as long as a scene rebuild silently
 * threw every occupancy bit away; re-stamping them (world_occupancy_restamp)
 * turned it into a player frozen behind the first npc on its route.
 */
int
player_travel_extra(void)
{
    return COLL_FLAG_BLOCK_NPC_AND_PLAYERS;
}

/*
 * `Npc.blockWalkFlag()`. The hard blocks always apply; the two opt-outs are
 * orthogonal, and `moverestrict=blocked` opts out of all of it — an npc that
 * lives where the map blocks (lava, water) respects no entity collision at all.
 *
 * `nomove` returns NULL there, meaning "clear the waypoints"; here the callers
 * answer that before stepping, so it falls through to the ordinary flags.
 */
int
npc_travel_extra(struct ToriRSServerNpc const* npc)
{
    int flag;

    assert(npc);
    if( npc->def && npc->def->moverestrict == 1 /* blocked */ )
        return COLL_FLAG_OPEN;

    flag = COLL_FLAG_BLOCK_NPC_AND_PLAYERS;
    /* blockwalk=none: do not respect npc-occupancy. */
    if( npc->blockwalk != 0 )
        flag |= COLL_FLAG_NPC_OCC;
    /* passthru: do not respect player-occupancy. */
    if( !npc->def || npc->def->moverestrict != 6 )
        flag |= COLL_FLAG_PLAYER_OCC;
    return flag;
}

static int
npc_occupancy_mask(struct ToriRSServerNpc const* npc)
{
    int mask = 0;

    assert(npc);
    switch( npc->blockwalk )
    {
    case 1: /* NPC */
        mask = COLL_FLAG_NPC_OCC;
        break;
    case 2: /* ALL */
        mask = COLL_FLAG_NPC_OCC | COLL_FLAG_BLOCK_NPC_AND_PLAYERS;
        break;
    case 3: /* PLAYER */
        mask = COLL_FLAG_PLAYER_OCC;
        break;
    default: /* NONE */
        break;
    }
    if( npc->blocksight )
        mask |= COLL_FLAG_PROJ_BLOCK_ENTITY;
    return mask;
}

void
npc_set_occupancy(struct ToriRSServerNpc const* npc, int add)
{
    int mask;

    assert(npc);
    mask = npc_occupancy_mask(npc);
    if( mask == 0 )
        return;
    ToriRSServer_SceneChangeOccupancy(npc->level, npc->x, npc->z, npc->size, mask, add);
}

void
ToriRSServer_WorldNpcOccupancy(
    struct ToriRSServerNpc* npc,
    int add)
{
    npc_set_occupancy(npc, add);
}

void
player_set_occupancy(struct ToriRSServerPlayer const* player, int add)
{
    assert(player);
    ToriRSServer_SceneChangeOccupancy(
        player->level, player->x, player->z, 1, COLL_FLAG_PLAYER_OCC, add);
}

/*
 * Which move restriction an npc walks under — LostCity
 * `PathingEntity.getCollisionStrategy`, whose MoveRestrict ids the content tree
 * parses into `moverestrict` (fields/npc.ini).
 *
 * `nomove` has no collision type: it means the npc does not step at all, which
 * every caller answers before asking this (npc_walk_to / the wander roll). It
 * maps to NORMAL here so a stray call cannot silently make a stationary npc
 * walk through walls.
 */
static int
npc_collision_type(struct ToriRSServerNpc const* npc)
{
    assert(npc);
    if( !npc->def )
        return COLL_TYPE_NORMAL;
    switch( npc->def->moverestrict )
    {
    case 1: /* blocked */
        return COLL_TYPE_BLOCKED;
    case 2: /* blocked_normal */
        return COLL_TYPE_LINE_OF_SIGHT;
    case 3: /* indoors */
        return COLL_TYPE_INDOORS;
    case 4: /* outdoors */
        return COLL_TYPE_OUTDOORS;
    default: /* normal, nomove, passthru */
        return COLL_TYPE_NORMAL;
    }
}

/*
 * Re-stamp every entity's occupancy onto a freshly built collision map.
 *
 * `ToriRSServer_SceneBuild` re-reads the map squares, so the flags it hands back
 * describe the terrain and the locs and nothing else — every NPC_OCC /
 * PLAYER_OCC bit written since the last build is on the array it just freed.
 * Without this, the first tick after a scene re-centre lets npcs and players
 * walk through each other until each of them happens to move (a step clears the
 * old tile and sets the new one, so occupancy heals itself only where somebody
 * walks).
 *
 * Called by every caller of ToriRSServer_SceneBuild that has entities to re-stamp.
 * `ToriRSServer_WorldInit` does not: it builds before anything is placed.
 */
void
world_occupancy_restamp(struct ToriRSServer* srv)
{
    assert(srv);
    for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        struct ToriRSServerNpc const* npc = &srv->npcs[i];

        if( !npc->active || npc->death_tick >= 0 )
            continue;
        npc_set_occupancy(npc, 1);
    }
    for( int i = 0; i < srv->player_count; i++ )
    {
        struct ToriRSServerPlayer const* player = &srv->players[i];

        if( !player->active )
            continue;
        player_set_occupancy(player, 1);
    }
}

void
npc_queue_waypoint(struct ToriRSServerNpc* npc, int x, int z)
{
    assert(npc);
    npc->waypoints[0].x = (int16_t)x;
    npc->waypoints[0].z = (int16_t)z;
    npc->waypoint_index = 0;
}

void
npc_clear_waypoints(struct ToriRSServerNpc* npc)
{
    assert(npc);
    npc->waypoint_index = -1;
}

/*
 * True when the greedy stepper cannot leave the current tile toward the active
 * waypoint. Used by interaction_continue_or_give_up to tell a real post-move
 * stall from a fresh walk_to_approach (steps_taken still 0, first step still
 * legal) — truncating that route to one tile is what broke run on op approaches.
 */
static int
player_waypoint_step_blocked(struct ToriRSServerPlayer const* player)
{
    int wp_x;
    int wp_z;
    int dx;
    int dz;
    int dir;
    int index;
    int extra = player_travel_extra();

    if( player->waypoint_index < 0 )
        return 1;

    index = player->waypoint_index;
    wp_x = player->waypoints[index].x;
    wp_z = player->waypoints[index].z;
    if( wp_x == player->x && wp_z == player->z )
    {
        index--;
        if( index < 0 )
            return 1;
        wp_x = player->waypoints[index].x;
        wp_z = player->waypoints[index].z;
    }

    dx = wp_x > player->x ? 1 : (wp_x < player->x ? -1 : 0);
    dz = wp_z > player->z ? 1 : (wp_z < player->z ? -1 : 0);
    if( dx == 0 && dz == 0 )
        return 1;

    dir = ToriRSServer_StepDirection(dx, dz);
    if( dir >= 0 &&
        ToriRSServer_SceneCanStepExtra(player->level, player->x, player->z, dir, extra) )
        return 0;
    if( dx != 0 )
    {
        dir = ToriRSServer_StepDirection(dx, 0);
        if( dir >= 0 &&
            ToriRSServer_SceneCanStepExtra(player->level, player->x, player->z, dir, extra) )
            return 0;
    }
    if( dz != 0 )
    {
        dir = ToriRSServer_StepDirection(0, dz);
        if( dir >= 0 &&
            ToriRSServer_SceneCanStepExtra(player->level, player->x, player->z, dir, extra) )
            return 0;
    }
    return 1;
}

/*
 * Install waypoints from a walk-order path (path[0] = first tile from the player).
 * Mirrors PathingEntity.queueWaypoints: the array is stored dest-first, and
 * waypoint_index counts down from the tile nearest the player toward the
 * destination.
 *
 * Only corner tiles are stored — the last tile of each straight run, matching
 * LostCity PathFinder's backtrace and collision_route_backtrace. The greedy
 * stepper fills the gaps; aiming at a run-start instead would hand it a
 * mixed-axis delta past the corner and let it cut an unvalidated diagonal.
 *
 * Cap at TORIRSSERVER_WAYPOINT_MAX by dropping destination-end corners (the
 * reference pop()), so an over-long route stops short rather than beelining
 * through walls to the raw destination.
 */
static void
queue_path_as_waypoints(
    struct ToriRSServerPlayer* player,
    int const* path_x,
    int const* path_z,
    int steps)
{
    int turn_x[TORIRSSERVER_WAYPOINT_MAX];
    int turn_z[TORIRSSERVER_WAYPOINT_MAX];
    int turns = 0;

    player->waypoint_index = -1;
    if( steps <= 0 )
        return;

    for( int i = 0; i < steps; i++ )
    {
        int from_x = i == 0 ? player->x : path_x[i - 1];
        int from_z = i == 0 ? player->z : path_z[i - 1];
        int dx = path_x[i] - from_x;
        int dz = path_z[i] - from_z;
        int is_corner;

        if( i == steps - 1 )
            is_corner = 1;
        else
        {
            int next_from_x = path_x[i];
            int next_from_z = path_z[i];
            int next_dx = path_x[i + 1] - next_from_x;
            int next_dz = path_z[i + 1] - next_from_z;

            is_corner = (next_dx != dx || next_dz != dz);
        }

        if( !is_corner )
            continue;
        if( turns >= TORIRSSERVER_WAYPOINT_MAX )
            break;
        turn_x[turns] = path_x[i];
        turn_z[turns] = path_z[i];
        turns++;
    }

    /* Reverse into dest-first storage: waypoints[0] = destination,
     * waypoints[turns-1] = first tile to walk toward. */
    for( int i = 0; i < turns; i++ )
    {
        player->waypoints[i].x = (int16_t)turn_x[turns - 1 - i];
        player->waypoints[i].z = (int16_t)turn_z[turns - 1 - i];
    }
    player->waypoint_index = turns - 1;
}

/*
 * Fill waypoints from a BFS to (x, z). Used for ground clicks and as the body
 * of walk_to_approach when the approach is EXACT.
 */
static int
waypoints_walk_to(
    struct ToriRSServerPlayer* player,
    int x,
    int z)
{
    int path_x[TORIRSSERVER_STEP_MAX];
    int path_z[TORIRSSERVER_STEP_MAX];
    int steps = ToriRSServer_SceneRoute(player->level, player->x, player->z, x, z, path_x, path_z,
                                    TORIRSSERVER_STEP_MAX);

    if( steps < 0 )
    {
        /*
         * Route failed, so do not move.
         *
         * The reference has no straight-line fallback: an unreachable click is
         * a click that does nothing. Leaving the waypoint queue empty is the
         * correct answer.
         */
        player->waypoint_index = -1;
        player->dest_x = -1;
        player->dest_z = -1;
        return 0;
    }
    queue_path_as_waypoints(player, path_x, path_z, steps);

    /* collision_map_route_tiles returns walk order, so its final tile is the
     * actual arrival.  That differs from (x,z) when the official move-near
     * fallback answers an unreachable ground click, such as Inferno lava.
     * Keeping the raw click here discarded the result of Statics.method5592:
     * movement used the nearest path, but destination state and SET_MAP_FLAG
     * still named the blocked tile. */
    if( steps > 0 )
    {
        player->dest_x = path_x[steps - 1];
        player->dest_z = path_z[steps - 1];
    }
    else
    {
        player->dest_x = player->x;
        player->dest_z = player->z;
    }
    return 1;
}

static void
waypoints_walk_to_approach(
    struct ToriRSServerPlayer* player,
    int x,
    int z,
    struct CollisionApproach const* approach)
{
    int path_x[TORIRSSERVER_STEP_MAX];
    int path_z[TORIRSSERVER_STEP_MAX];
    int arrive_x = x;
    int arrive_z = z;
    int steps;

    assert(approach);
    steps = ToriRSServer_SceneRouteOp(player->level, player->x, player->z, x, z, approach, path_x,
                                   path_z, TORIRSSERVER_STEP_MAX, &arrive_x, &arrive_z);
    if( steps < 0 )
    {
        player->waypoint_index = -1;
        return;
    }
    queue_path_as_waypoints(player, path_x, path_z, steps);
    player->dest_x = arrive_x;
    player->dest_z = arrive_z;
}

void
ToriRSServer_WorldWalkTo(
    struct ToriRSServer* srv,
    int x,
    int z)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int base_x;
    int base_z;

    steps_clear(player);
    player->dest_x = x;
    player->dest_z = z;
    player->steps_taken = 0;
    if( !waypoints_walk_to(player, x, z) )
        return;

    /* Server owns the yellow cross: scene-local routed destination.  For an
     * unreachable click this is the move-near arrival, not the raw click. */
    base_x = ToriRSServer_SceneBaseX();
    base_z = ToriRSServer_SceneBaseZ();
    if( base_x >= 0 && player->waypoint_index >= 0 )
        ToriRSServer_SendSetMapFlag(
            player, player->dest_x - base_x, player->dest_z - base_z);
}

void
ToriRSServer_WorldWalkToApproach(
    struct ToriRSServer* srv,
    int x,
    int z,
    struct CollisionApproach const* approach)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int base_x;
    int base_z;

    assert(approach);
    steps_clear(player);
    player->dest_x = x;
    player->dest_z = z;
    player->steps_taken = 0;
    waypoints_walk_to_approach(player, x, z, approach);

    base_x = ToriRSServer_SceneBaseX();
    base_z = ToriRSServer_SceneBaseZ();
    if( base_x >= 0 && player->waypoint_index >= 0 )
        ToriRSServer_SendSetMapFlag(
            player, player->dest_x - base_x, player->dest_z - base_z);
}

/* ------------------------------------------------------------------ */
/* Interactions                                                        */
/* ------------------------------------------------------------------ */

/*
 * Chebyshev distance from a point to a rectangle, 0 when inside.
 *
 * Rectangle rather than point because targets have footprints: a 3x3 npc is
 * "next to you" from a tile that is three away from its south-west corner, and
 * testing against the corner alone makes large npcs unreachable from two of
 * their four sides.
 */
int
distance_to_rect(
    int from_x,
    int from_z,
    int rect_x,
    int rect_z,
    int size_x,
    int size_z)
{
    int dx = 0;
    int dz = 0;

    if( from_x < rect_x )
        dx = rect_x - from_x;
    else if( from_x > rect_x + size_x - 1 )
        dx = from_x - (rect_x + size_x - 1);
    if( from_z < rect_z )
        dz = rect_z - from_z;
    else if( from_z > rect_z + size_z - 1 )
        dz = from_z - (rect_z + size_z - 1);

    return dx > dz ? dx : dz;
}

void
ToriRSServer_WorldInteractionClearAt(struct ToriRSServerPlayer* player)
{
    assert(player);
    memset(&player->interaction, 0, sizeof(player->interaction));
    player->interaction.kind = TORIRSSERVER_INTERACT_NONE;
    player->interaction.npc_slot = -1;
    player->interaction.ap_range = TORIRSSERVER_AP_RANGE_DEFAULT;
    player->interaction_serial++;
}

void
ToriRSServer_WorldInteractionClear(struct ToriRSServer* srv)
{
    ToriRSServer_WorldInteractionClearAt(srv->active_player);
}

void
ToriRSServer_WorldInteractionSet(
    struct ToriRSServer* srv,
    enum ToriRSServerInteractionKind kind,
    int op,
    int npc_slot,
    int target_id,
    int tile_x,
    int tile_z,
    int level,
    int size_x,
    int size_z)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct ToriRSServerInteraction* interaction = &player->interaction;
    int sx = size_x > 0 ? size_x : 1;
    int sz = size_z > 0 ? size_z : 1;

    player->interaction_serial++;
    interaction->kind = kind;
    interaction->op = op;
    interaction->npc_slot = npc_slot;
    interaction->target_id = target_id;
    interaction->target_generation = 0;
    if( kind == TORIRSSERVER_INTERACT_NPC && npc_slot >= 0 && npc_slot < TORIRSSERVER_NPC_MAX )
        interaction->target_generation = srv->npcs[npc_slot].generation;
    else if( kind == TORIRSSERVER_INTERACT_PLAYER && npc_slot >= 0 &&
             npc_slot < srv->player_count )
        interaction->target_generation = srv->players[npc_slot].login_generation;
    else if( kind == TORIRSSERVER_INTERACT_OBJ )
    {
        for( int i = 0; i < TORIRSSERVER_GROUND_MAX; i++ )
        {
            const struct ToriRSServerGroundObj* obj = &srv->ground[i];

            if( ToriRSServer_WorldGroundVisibleTo(srv, i, srv->active_player->pid) &&
                obj->obj_id == target_id && obj->x == tile_x &&
                obj->z == tile_z && obj->level == level )
            {
                interaction->target_generation = (uint32_t)obj->generation;
                break;
            }
        }
    }
    interaction->x = tile_x;
    interaction->z = tile_z;
    interaction->level = level;
    interaction->size_x = sx;
    interaction->size_z = sz;
    interaction->ap_tried = 0;
    /* `PathingEntity.setInteraction` resets both, and it has to: an ap range a
     * previous interaction narrowed would otherwise decide how close this one
     * has to get. */
    interaction->ap_range = TORIRSSERVER_AP_RANGE_DEFAULT;
    interaction->ap_range_called = 0;
    /* Cleared here for the same reason `ap_tried` is: this call establishes a
     * *new* interaction, and the two form flags select which trigger family it
     * resolves to. Both callers that want a form set it immediately after this
     * returns, so clearing costs them nothing — and an op interaction that
     * inherited a previous cast's `spell` would run `[apnpct]` for a click. */
    interaction->use_on = 0;
    interaction->spell = 0;

    /* LostCity PathingEntity.setInteraction: only NonPathingEntity (loc/obj)
     * records targetX/Z for reorient() to consume after movement. Pathing
     * targets use FACE_ENTITY; leave any prior face_target stash alone. */
    if( kind == TORIRSSERVER_INTERACT_LOC || kind == TORIRSSERVER_INTERACT_OBJ )
    {
        player->face_target_x = ToriRSServer_CoordFine(tile_x, sx);
        player->face_target_z = ToriRSServer_CoordFine(tile_z, sz);
    }
}

/**
 * The loc an interaction is about, by id first and position second.
 *
 * A tile routinely carries more than one loc — 3226,3223 in Lumbridge holds the
 * castle door *and* a wall decoration — so "whatever is at this tile" is not a
 * safe way to re-find one. The id is tried first; only when it is gone does the
 * tile-only form apply, which is the case that matters for a door somebody else
 * opened while the player was walking over. That door is still the thing they
 * clicked, and its id having changed is not staleness.
 */
static int
find_interaction_loc(
    int tile_x,
    int tile_z,
    int level,
    int loc_id)
{
    int slot = ToriRSServer_SceneFindLoc(tile_x, tile_z, level, loc_id);

    if( slot >= 0 )
        return slot;
    return ToriRSServer_SceneFindLoc(tile_x, tile_z, level, -1);
}

/**
 * Is the target still there, and where?
 *
 * Returns 0 when the interaction is stale, which is the common case rather than
 * the exotic one: npcs die and respawn into the same slot, doors are opened by
 * something else, and a ground obj is taken by whoever got there first. An npc
 * is re-read every tick because it *moves* — walking to where it was is not the
 * same as walking to where it is.
 */
static int
interaction_target(
    struct ToriRSServer* srv,
    int* out_x,
    int* out_z,
    int* out_size_x,
    int* out_size_z)
{
    struct ToriRSServerInteraction* interaction = &srv->active_player->interaction;

    switch( interaction->kind )
    {
    case TORIRSSERVER_INTERACT_NPC:
    {
        struct ToriRSServerNpc* npc;

        if( interaction->npc_slot < 0 || interaction->npc_slot >= TORIRSSERVER_NPC_MAX )
            return 0;
        npc = &srv->npcs[interaction->npc_slot];
        /*
         * Slot reuse: same index, a different npc. Acting on it would attack
         * whatever respawned there — and `generation` is the whole of that
         * test. Every spawn into a slot bumps it (`ToriRSServer_WorldNpcSpawn`),
         * so (slot, generation) names one npc for its entire life, which is
         * exactly what the reference gets for free by holding the object.
         *
         * The TYPE deliberately takes no part, and this is a considered
         * divergence: `Player.validateTarget` compares `targetSubject.type`
         * against `target.type` with the comment "this is effectively checking
         * if the Npc or Loc did a changetype", so upstream a transformation
         * ends the interaction. Here it must not. `npc_changetype` is the same
         * npc in a new form — Verzik between phases, Xarpus between his
         * feeding and combat types — and losing the interaction loses the
         * fight: the interaction is cleared, and with it the `p_opnpc(2)`
         * re-issue at the end of `[label,player_melee_attack]` that is the
         * only thing re-arming the swing. The player stands there, still
         * walking after a boss that no longer takes hits.
         *
         * So the live type is ADOPTED rather than compared. Everything
         * downstream keys off `target_id` — `interaction_category`, the ap
         * rung, the `[opnpc<n>]` dispatch — and adopting it is what makes them
         * resolve against what the npc is NOW, which is the reference's own
         * `NpcType.get(target.type)` read at dispatch time. A phase that binds
         * its own `[apnpc2,<new type>]` therefore gets it on the next swing.
         *
         * The footprint travels with it, and only with it. A transform that
         * grows the npc has already moved its collision rectangle
         * (`npc_changetype_rehydrate` releases and retakes the occupancy), and
         * an approach still measuring the 3x3 of Xarpus' feeding form would
         * walk the player under the 5x5 he is now. But the refresh is gated on
         * the type having actually changed rather than run every tick,
         * because the interaction's footprint is its opener's to state: the
         * under-a-size-5-npc stanza below stands a 1x1 goblin in for a boss by
         * describing it as 5x5 in the interaction, and a size re-read on every
         * tick quietly puts that back to 1 and tests nothing.
         */
        if( !npc->active || npc->generation != interaction->target_generation )
            return 0;
        if( npc->level != srv->active_player->level )
            return 0;
        if( npc->death_tick >= 0 )
            return 0;
        if( npc->type != interaction->target_id )
        {
            int size = npc->size > 0 ? npc->size : 1;

            interaction->target_id = npc->type;
            interaction->size_x = size;
            interaction->size_z = size;
        }
        *out_x = npc->x;
        *out_z = npc->z;
        *out_size_x = interaction->size_x;
        *out_size_z = interaction->size_z;
        return 1;
    }

    case TORIRSSERVER_INTERACT_PLAYER:
    {
        struct ToriRSServerPlayer* target;

        if( interaction->npc_slot < 0 || interaction->npc_slot >= srv->player_count )
            return 0;
        target = &srv->players[interaction->npc_slot];
        /* Logged out, or the slot now holds somebody else. */
        if( !target->active || target->pid != interaction->target_id ||
            target->login_generation != interaction->target_generation )
            return 0;
        if( target == srv->active_player )
            return 0;
        if( target->level != srv->active_player->level )
            return 0;
        *out_x = target->x;
        *out_z = target->z;
        *out_size_x = 1;
        *out_size_z = 1;
        return 1;
    }

    case TORIRSSERVER_INTERACT_LOC:
    {
        int slot = find_interaction_loc(interaction->x, interaction->z, interaction->level,
                                        interaction->target_id);
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(slot);

        /* A tile with nothing on it at all is genuinely stale. */
        if( !loc )
            return 0;
        *out_x = loc->x;
        *out_z = loc->z;
        *out_size_x = loc->size_x > 0 ? loc->size_x : 1;
        *out_size_z = loc->size_z > 0 ? loc->size_z : 1;
        return 1;
    }

    case TORIRSSERVER_INTERACT_OBJ:
        for( int i = 0; i < TORIRSSERVER_GROUND_MAX; i++ )
        {
            struct ToriRSServerGroundObj* obj = &srv->ground[i];

            if( !ToriRSServer_WorldGroundVisibleTo(srv, i, srv->active_player->pid) ||
                obj->obj_id != interaction->target_id )
                continue;
            if( obj->x != interaction->x || obj->z != interaction->z )
                continue;
            if( obj->level != interaction->level )
                continue;
            if( (uint32_t)obj->generation != interaction->target_generation )
                continue;
            *out_x = obj->x;
            *out_z = obj->z;
            *out_size_x = 1;
            *out_size_z = 1;
            return 1;
        }
        return 0;

    case TORIRSSERVER_INTERACT_NONE:
    default:
        return 0;
    }
}

/*
 * The category rung of the trigger lookup, per subject kind.
 *
 * `Player.getOpTrigger` passes `type.category` for npc, loc and obj alike, and
 * all three answer here as of 2026-08-02:
 *
 * - **obj** — config opcode 94, a number the cache states and
 *   `pack/category.pack` names. This is the rung `[opheld1,_bones]` binds
 *   through.
 * - **npc** — config opcode 18, the same arrangement one namespace over. The
 *   comment that used to sit here said an osrs239 npc record "carries no
 *   category at all… absent, which is why `struct ToriRSServerNpcInfo` has no field
 *   for it". That was wrong, and wrong in the direction that costs the most:
 *   the cache states a category on **9,149 of its 16,292 npc records**, the
 *   decoder has read it into `RSCache_Dat2ConfigNpc.category` all along, and
 *   `cachepack` round-trips it. It was unread, not absent. Names for the ids
 *   come from the crawl in `pack/category.pack` (triage §7.6b, §9 step 3b).
 * - **loc** — config opcode 61, and this arm returned a hardcoded -1 until
 *   2026-08-02 for a reason that had two halves. The first was real: the linked
 *   decoder read opcode 61 and threw the value away, so no loc in this tree had a
 *   category to pass. The second was that `ToriRSServerLocDef.category` existed and
 *   was a *private two-valued door enum* whose values (1 and 2) would have
 *   aliased onto real ids in `pack/category.pack` — passing it would have bound
 *   every door in the game to `weapon_staff`'s scripts, quietly. Both are gone:
 *   `dat2_config_loc.c` keeps the field, the def's is a pack id, and
 *   `ToriRSServer_LocCategory` merges the two in that order.
 */
int
interaction_category(const struct ToriRSServerInteraction* interaction)
{
    switch( interaction->kind )
    {
    case TORIRSSERVER_INTERACT_OBJ:
    {
        const struct ToriRSServerObjInfo* info = ToriRSServer_ObjInfo(interaction->target_id);

        return info->category > 0 ? info->category : -1;
    }
    case TORIRSSERVER_INTERACT_NPC:
        return ToriRSServer_NpcCategory(interaction->target_id);
    case TORIRSSERVER_INTERACT_LOC:
        return ToriRSServer_LocCategory(interaction->target_id);
    default:
        return -1;
    }
}

/**
 * Fire one trigger for this interaction, with whatever entity it is *about*
 * bound as the script's active one.
 *
 * The npc has ridden along since the family was written; the loc had not, and
 * that omission is what the `oploc` fallback row was really waiting on — see
 * `ToriRSServer_ScriptsRunTriggerOnLoc`. Stated in one function because the ap
 * rung and the op dispatch both need it and a second copy is how the two come to
 * disagree about a script's execution context.
 *
 * `find_interaction_loc` rather than the interaction's own slot, for the reason
 * that function's own comment gives: between the click and the dispatch the loc
 * can have been changed by somebody else, and that door is still the thing the
 * player clicked.
 */
static int
run_interaction_trigger(
    struct ToriRSServer* srv,
    const struct ToriRSServerInteraction* interaction,
    int trigger)
{
    /*
     * A cast is keyed by the spell, not by what it is aimed at — the reference
     * writes one `[apnpct,magic_spellbook:wind_strike]` per spell and matches no
     * npc — and its subject is a *component uid*, which does not fit the
     * compiled lookup key. So it needs its own dispatcher, and it needs it for
     * every target kind including LOC, whose by-name rung is about the loc.
     * See ToriRSServer_ScriptsRunSpellTrigger.
     *
     * The target is still bound as the script's ACTIVE entity, exactly as the
     * op arms below bind theirs. Being keyed by the spell decides which script
     * runs; it does not decide what that script can see. Without this an
     * `[aploct,<component>]` ran with no active loc and its first `loc_coord`
     * aborted with "the active loc is gone" — the same defect the `[oploc<n>]`
     * family had, in the one family that had no reason to be exempt. A familiar
     * special cast at a tree is exactly that shape: the script's whole subject
     * is the loc it was aimed at.
     */
    if( interaction->spell )
    {
        int loc_slot = -1;
        int result;

        if( interaction->kind == TORIRSSERVER_INTERACT_LOC )
        {
            loc_slot = find_interaction_loc(interaction->x, interaction->z, interaction->level,
                                            interaction->target_id);
        }
        else if( interaction->kind == TORIRSSERVER_INTERACT_OBJ )
        {
            int obj_slot = ToriRSServer_WorldGroundFind(srv, interaction->x, interaction->z,
                                                     interaction->level, interaction->target_id);

            srv->pending_active_obj =
                obj_slot >= 0 ? ToriRSServer_WorldObjHandle(srv, obj_slot) : 0;
        }
        result = ToriRSServer_ScriptsRunSpellTrigger(
            srv, trigger, interaction->spell,
            interaction->kind == TORIRSSERVER_INTERACT_NPC ? interaction->npc_slot : -1,
            interaction->kind == TORIRSSERVER_INTERACT_PLAYER ? interaction->npc_slot : -1, loc_slot);
        srv->pending_active_obj = 0;
        return result;
    }

    if( interaction->kind == TORIRSSERVER_INTERACT_LOC )
    {
        int slot = find_interaction_loc(interaction->x, interaction->z, interaction->level,
                                        interaction->target_id);
        int type = ToriRSServer_LocResolveTransform(srv->active_player, interaction->target_id);
        int category;

        if( type < 0 )
            type = interaction->target_id;
        category = ToriRSServer_LocCategory(type);
        return ToriRSServer_ScriptsRunTriggerOnLoc(srv, trigger, type, category, slot);
    }
    {
        int category = interaction_category(interaction);

        return ToriRSServer_ScriptsRunTrigger(srv, trigger, interaction->target_id, category,
                                           interaction->npc_slot);
    }
}

/**
 * The `[ap*]` trigger this interaction resolves to. Its `[op*]` twin is +7.
 *
 * +7 is not a coincidence and not a table: `ss_trigger.h` lays every family out
 * as `ap1..ap5, apU, apT, op1..op5, opU, opT`, so `APLOC1 59 -> OPLOC1 66` and
 * `APLOCU 64 -> OPLOCU 71` are the same offset. The reference stores the ap id
 * and adds 7 (`Player.getOpTrigger`), which is why it is stated once here.
 *
 * A use-on carries no op number — "use this on that" is one verb — so the `u`
 * form ignores `op` entirely. Separated into a function rather than grown inside
 * `ToriRSServer_WorldProcessInteraction` because that function holds three of the
 * seven `enum ToriRSServerFallback` call sites, and Phase 3 has to be able to delete
 * them out of arms nothing else has rewritten.
 */
static int
interaction_ap_trigger(
    enum ToriRSServerInteractionKind kind,
    int op,
    int use_on,
    int spell)
{
    /* The `t` form outranks the `u` form because nothing sets both: a cast is
     * started by `p_opnpct` and a use-on by the use-on handler, and neither
     * clears the other's field on an interaction it did not create. Testing
     * `spell` first means a leftover `use_on` cannot steer a cast. */
    switch( kind )
    {
    case TORIRSSERVER_INTERACT_NPC:
        if( spell )
            return SS_TRIGGER_APNPCT;
        return use_on ? SS_TRIGGER_APNPCU : SS_TRIGGER_APNPC1 + (op - 1);
    case TORIRSSERVER_INTERACT_LOC:
        if( spell )
            return SS_TRIGGER_APLOCT;
        return use_on ? SS_TRIGGER_APLOCU : SS_TRIGGER_APLOC1 + (op - 1);
    case TORIRSSERVER_INTERACT_OBJ:
        if( spell )
            return SS_TRIGGER_APOBJT;
        return use_on ? SS_TRIGGER_APOBJU : SS_TRIGGER_APOBJ1 + (op - 1);
    case TORIRSSERVER_INTERACT_PLAYER:
        if( spell )
            return SS_TRIGGER_APPLAYERT;
        return use_on ? SS_TRIGGER_APPLAYERU : SS_TRIGGER_APPLAYER1 + (op - 1);
    default:
        return -1;
    }
}

/* The engine's own behaviour for an op nothing was bound to. Defined below;
 * these are what used to run inline inside the packet handlers. */
static void
interaction_engine_npc(
    struct ToriRSServer* srv,
    int slot,
    int op_num);

/* ------------------------------------------------------------------ */
/* The verbs the engine answers itself                                 */
/* ------------------------------------------------------------------ */

/*
 * The cache menu verbs this engine implements, named once.
 *
 * Each of these is read off the record's own op list rather than an id list:
 * OldSchool has dozens of npcs that say "Attack" and a list would be a second
 * copy kept by hand and wrong for whichever one nobody added.
 *
 * **The loc verbs left on 2026-08-02** — "Bank", "Use-quickly", "Climb-up",
 * "Climb-down" and a bare "Climb" — with `interaction_engine_loc`, the row that
 * declared it and everything the row reached. The argument above is why a *list*
 * was wrong and it stays true; what changed is that the answer is no longer to
 * read the verb in C at all. A script cannot read a menu verb, so the grouping
 * moved to content as data derived from the cache and checked against it:
 * `tools/bank_import.py` writes the 78 records that say "Bank" as name bindings,
 * `tools/ladder_import.py` writes the 1,445 that say a climb verb as four
 * allocated categories plus 17 names. "Use-quickly" matched zero records in this
 * cache and moved nowhere.
 *
 * They are gathered here because a *second* reader needs them —
 * `ToriRSServer_ScriptsReportShadowedOps`, which asks at load which of these
 * verbs content has bound a trigger over. Two copies of the list is two
 * chances for the report to say the opposite of what the runtime does, and a
 * report that disagrees with the behaviour it describes is worse than none.
 *
 * That these are string literals in C is the standing violation
 * PORTING_GUIDE §2.4 item 2 names, and it is not fixed here: the verb is the
 * cache's own word and the comparison is how the engine reads it. What is
 * fixed is that there is now one occurrence of each instead of seven.
 */
#define TORIRSSERVER_VERB_ATTACK "Attack"
static const char* const k_engine_npc_verbs[] = { TORIRSSERVER_VERB_ATTACK };

static const char*
claimed_in(
    const char* verb,
    const char* const* claimed,
    size_t count)
{
    assert(verb);
    for( size_t i = 0; i < count; i++ )
        if( strcmp(verb, claimed[i]) == 0 )
            return claimed[i];
    return NULL;
}

const char*
ToriRSServer_WorldEngineClaimedVerb(
    int trigger,
    int32_t subject)
{
    if( trigger >= SS_TRIGGER_OPNPC1 && trigger <= SS_TRIGGER_OPNPC5 )
    {
        const struct ToriRSServerNpcInfo* info = ToriRSServer_NpcInfo((int)subject);
        int op_num = trigger - SS_TRIGGER_OPNPC1 + 1;

        if( !info || !info->ops[op_num - 1] )
            return NULL;
        return claimed_in(info->ops[op_num - 1], k_engine_npc_verbs,
                          sizeof(k_engine_npc_verbs) / sizeof(k_engine_npc_verbs[0]));
    }
    /*
     * `[oploc<n>]`, `[opheld<n>]` and `[opobj<n>]` were all here and none is now.
     * With `interaction_engine_loc`, the held tail of `handle_opheld` and
     * `interaction_engine_obj` gone, the engine answers no loc, held or ground
     * verb at all — so there is nothing for a door, a ladder, a bank booth, a
     * wielded weapon or a pile on the floor to shadow, and the report correctly
     * says nothing about them. Their verb macros went in the same commits:
     * leaving them would make a later `[oploc2,<loc>]` or `[opheld2,<obj>]`
     * report as shadowing a verb nothing answers, which is worse than silence.
     */
    return NULL;
}

/*
 * Approach geometry for the latched interaction — same shape the walk used, so
 * reach and route cannot disagree.
 */
static void
interaction_build_approach(
    struct ToriRSServer* srv,
    int size_x,
    struct CollisionApproach* approach)
{
    struct ToriRSServerInteraction const* interaction = &srv->active_player->interaction;

    assert(approach);
    if( interaction->kind == TORIRSSERVER_INTERACT_LOC )
    {
        int loc_slot = find_interaction_loc(interaction->x, interaction->z, interaction->level,
                                            interaction->target_id);
        ToriRSServer_SceneLocApproach(loc_slot, approach);
    }
    else if( interaction->kind == TORIRSSERVER_INTERACT_NPC )
        ToriRSServer_SceneNpcApproach(size_x, approach);
    else if( interaction->kind == TORIRSSERVER_INTERACT_PLAYER )
        ToriRSServer_SceneNpcApproach(1, approach);
    else
        ToriRSServer_SceneObjApproach(0, approach);
}

/*
 * LostCity PathingEntity.randomWalk — one cardinal step off a stacked target.
 * Deterministic via ToriRSServer_Random (selftests need a stable chase).
 *
 * The reference guards this branch with `moveStrategy === NAIVE`, so it is what
 * an *npc* does when it ends up under its target; rsmod spells the same thing
 * out as `NpcInteractionProcessor.stepAwayFromTarget`. Running it for the
 * player is the pre-`under_target_routes_out` behaviour and is kept only for
 * eras that state it — see interaction_path_to_pathing_target.
 */
static void
interaction_random_walk(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int x = player->x;
    int z = player->z;

    if( ToriRSServer_Random(srv, 0, 1) == 0 )
        x += ToriRSServer_Random(srv, 0, 1) == 0 ? -1 : 1;
    else
        z += ToriRSServer_Random(srv, 0, 1) == 0 ? -1 : 1;
    player->waypoints[0].x = (int16_t)x;
    player->waypoints[0].z = (int16_t)z;
    player->waypoint_index = 0;
    player->dest_x = x;
    player->dest_z = z;
}

/*
 * LostCity Player.tryInteract — AP then OP against live target coords. No path
 * mutation except clear-on-success.
 *
 * allow_op_scenery mirrors the reference's allowOpScenery: locs/objs only OP
 * when true (post-move stall, or the packet-handler immediate try). Pathing
 * entities (npc/player) OP whenever operable, including the tick they arrive.
 *
 * Returns 1 when the interaction ran and was cleared.
 */
static int
interaction_try(
    struct ToriRSServer* srv,
    int allow_op_scenery)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct ToriRSServerInteraction* interaction = &player->interaction;
    struct CollisionApproach approach;
    int target_x;
    int target_z;
    int size_x;
    int size_z;
    int distance;
    int trigger;
    int category;
    int reached;
    int under_pathing;

    if( interaction->kind == TORIRSSERVER_INTERACT_NONE )
        return 0;

    if( !interaction_target(srv, &target_x, &target_z, &size_x, &size_z) )
    {
        /* The target is gone — an npc that just died is the common case. A
         * route queued to approach it (melee adjacency, or the "get to ap
         * range" walk on the way in) is aimed at a tile that no longer means
         * anything, and nothing else on this path would stop it: with the
         * interaction cleared, next tick's gate on `interaction.kind` is
         * false and advance_player keeps draining the queue regardless,
         * walking the player onto the corpse over the following ticks. */
        steps_clear(player);
        ToriRSServer_WorldInteractionClear(srv);
        return 1;
    }

    interaction->x = target_x;
    interaction->z = target_z;
    interaction_build_approach(srv, size_x, &approach);

    distance = distance_to_rect(player->x, player->z, target_x, target_z, size_x, size_z);
    under_pathing = (interaction->kind == TORIRSSERVER_INTERACT_NPC ||
                     interaction->kind == TORIRSSERVER_INTERACT_PLAYER) &&
                    distance == 0;

    /*
     * At range: content's chance to handle this without closing the distance.
     * Standing under a pathing entity is not approachable (LostCity
     * inApproachDistance). Only content can — the engine has no ranged
     * behaviour of its own — so a miss here just falls through to the walk.
     *
     * The range is the interaction's, not the constant: `p_aprange` narrows it
     * (see `ToriRSServerInteraction.ap_range`), and re-arms `ap_tried` so the
     * trigger fires again once the walk has closed to the range the script
     * asked for. That is the whole of the reference's ap loop — `tryInteract`
     * returns false when `apRangeCalled` is set, restores the waypoints and
     * leaves the target standing — and it is what every combat script is built
     * on: `[apnpc1,_]` opens with an `p_aprange` for the weapon's reach.
     */
    if( !under_pathing && !interaction->ap_tried && distance <= interaction->ap_range )
    {
        int ap_ok = 1;

        /* AP also requires approached() LoS. Player→npc for OPNPC/APNPC;
         * cast backwards when the mover is the npc (handled in npc_run_mode).
         * Player→player is the one pairing that went symmetric in 2019, and
         * it asks through its own predicate so PvM cannot inherit it. */
        if( interaction->kind == TORIRSSERVER_INTERACT_NPC )
        {
            ap_ok = ToriRSServer_SceneApproached(
                player->level, player->x, player->z, target_x, target_z, 1, 1, size_x, size_z);
        }
        else if( interaction->kind == TORIRSSERVER_INTERACT_PLAYER )
        {
            ap_ok = ToriRSServer_SceneApproachedPvp(
                player->level, player->x, player->z, target_x, target_z, 1, 1, 1, 1);
        }
        if( ap_ok )
        {
            interaction->ap_tried = 1;
            /* Cleared before the script rather than after, because it is the
             * script that sets it — `PathingEntity` resets it at the top of
             * every tick and `tryInteract` again before dispatching. */
            interaction->ap_range_called = 0;
            trigger = interaction_ap_trigger(interaction->kind, interaction->op,
                                             interaction->use_on, interaction->spell);
            /* A miss here has no fallback and needs none: "nothing bound at range"
             * is the ordinary case for almost every interaction in the game, and
             * what it means is "keep walking". It still reports once per
             * interaction under TORIRSSERVER_VERBOSE — once, because `ap_tried` latches
             * above and this runs on one tick of the walk rather than all of them. */
            if( trigger >= 0 )
            {
                unsigned serial = player->interaction_serial;
                struct ToriRSServerInteraction snapshot = *interaction;
                int ran = run_interaction_trigger(srv, &snapshot, trigger) !=
                          TORIRSSERVER_TRIGGER_NONE;
                /* The script may have replaced the interaction — `p_opnpc(2)` is
                 * how every combat script re-arms itself for the next swing, and
                 * a ranged one reaches it through THIS branch rather than the op
                 * one below. The clear used to be unconditional and ran after the
                 * script, so the re-arm was created and then thrown away in the
                 * same tick: a bow fired exactly once and the fight went quiet.
                 * (The op branch never had this because it clears *before*
                 * dispatching, which is the reference's order for both.)
                 *
                 * The reference is `Player.tryInteract`: it stashes whatever the
                 * script left in `target` as `nextTarget`, clears the waypoints,
                 * and restores it at the end of `processInteraction`. Here the
                 * new interaction is already in place, so keeping it is the whole
                 * of the restore — only the walk has to go. */
                if( player->interaction_serial != serial )
                {
                    steps_clear(player);
                    return 1;
                }
                if( ran && !interaction->ap_range_called )
                {
                    steps_clear(player);
                    ToriRSServer_WorldInteractionClear(srv);
                    return 1;
                }
            }
        }
    }

    reached =
        ToriRSServer_SceneReached(player->level, player->x, player->z, target_x, target_z, &approach);
    /* Ground obj EXACT: standing on the tile. Retry 1x1 adjacent only when
     * we have finished trying to walk (no step this tick, no waypoints left)
     * — and never mutate `approach` itself: the re-route below must keep
     * routing EXACT, or a mid-walk adjacent check permanently redirects the
     * destination to a neighbour tile. */
    if( !reached && interaction->kind == TORIRSSERVER_INTERACT_OBJ && !player_has_waypoints(player) &&
        player->steps_taken == 0 )
    {
        struct CollisionApproach adjacent;

        ToriRSServer_SceneObjApproach(1, &adjacent);
        reached = ToriRSServer_SceneReached(player->level, player->x, player->z, target_x, target_z,
                                        &adjacent);
    }

    if( !reached )
        return 0;

    /* Locs/objs wait for allowOpScenery; pathing entities act on arrival. */
    if( (interaction->kind == TORIRSSERVER_INTERACT_LOC ||
         interaction->kind == TORIRSSERVER_INTERACT_OBJ) &&
        !allow_op_scenery )
        return 0;

    {
        int op_num = interaction->op;
        int slot = interaction->npc_slot;
        enum ToriRSServerInteractionKind kind = interaction->kind;
        int target_id = interaction->target_id;
        int loc_x = interaction->x;
        int loc_z = interaction->z;
        int loc_level = interaction->level;
        int use_on = interaction->use_on;
        int spell = interaction->spell;
        int loc_trigger_type = target_id;
        /* The cast arm below dispatches through `run_interaction_trigger`, which
         * reads the interaction — and the clear a few lines down is what this
         * copy survives. Same reason the ap rung above snapshots. */
        struct ToriRSServerInteraction snapshot = *interaction;

        /* Multiloc: scene entity / find stays BASE; trigger type+category use
         * the varbit-resolved child (LostCity OpLocHandler + getOpTrigger gap
         * filled for osrs239 child-bound scripts like [oploc1,ernest_doorajar]). */
        if( kind == TORIRSSERVER_INTERACT_LOC )
        {
            loc_trigger_type = ToriRSServer_LocResolveTransform(player, target_id);
            if( loc_trigger_type < 0 )
                loc_trigger_type = target_id;
            category = ToriRSServer_LocCategory(loc_trigger_type);
        }
        else
        {
            /* Read before the clear below, with everything else the dispatch needs:
             * the interaction it is derived from does not survive to the switch. */
            category = interaction_category(interaction);
        }

        /*
         * Clear *before* running, not after. A script is allowed to start a new
         * interaction (`p_opnpc`), and clearing afterwards would throw away the
         * one it just asked for.
         */
        ToriRSServer_WorldInteractionClear(srv);
        steps_clear(player);
        player->dest_x = -1;
        player->dest_z = -1;

        /*
         * A use-on takes its own arm and leaves before the switch below, rather
         * than growing a fourth case inside it.
         *
         * The reason is the miss: there is **no engine use-on behaviour** to fall
         * back to. `Player.defaultOp` answers an unbound `*u` with the message
         * and nothing else, and routing it into TORIRSSERVER_FALLBACK_OPNPC/OPOBJ
         * would hand "use a bucket on the goblin" to the greeting and "use a
         * bucket on the pile" to the pickup. (It named `TORIRSSERVER_FALLBACK_OPLOC`
         * first, with "would hand use-a-bucket-on-the-door to the door handler
         * and open it" — which is what the door handler did until it was
         * deleted; the loc arm below reaches the same message this one does
         * now.) The message is content's — `[proc,nothing_interesting_message]`
         * — reached through the same `ToriRSServer_Say` the other four literals moved
         * behind.
         *
         * The subject is the **target**, never the used item. That is
         * `Player.getOpTrigger` reading `type.id`/`type.category` off the loc,
         * npc or obj the interaction is against; the item reaches content only
         * as `last_useitem`/`last_useslot`.
         */
        /*
         * A cast takes the same shape of arm as a use-on and for the same
         * reason — there is no engine behaviour to fall back to — but the
         * trigger type is the *spell*, not the target. `[opnpct]` is what runs
         * when the caster is already adjacent; the reference notes its own
         * osrs-era `opnpct` triggers are unused and puts every spell on
         * `[apnpct]`, which the ap rung above has already tried by the time a
         * walk reaches here, so this is the melee-range case and the miss is the
         * common one.
         *
         * Through `run_interaction_trigger`, not a bare keyed lookup, and that
         * is a fix rather than a tidy-up: the keyed lookup can only find a
         * subject below `ssc_compile.c`'s `1 << 21` ceiling, and every spell
         * component in the tree is above it (a spellbook's is 14 million; the
         * Summoning familiar overlay's is 63 million). So the by-name rung is
         * the ONLY one that can match here, the ap rung has used it since it was
         * written, and this arm never did — an `[opnpct,<component>]` was
         * unreachable, and what the player got for walking all the way in was
         * "Nothing interesting happens".
         */
        if( spell )
        {
            int ap = interaction_ap_trigger(kind, op_num, 0, spell);

            if( ap < 0 || run_interaction_trigger(srv, &snapshot, ap + 7) == TORIRSSERVER_TRIGGER_NONE )
                ToriRSServer_Say(srv, "nothing_interesting_message", NULL);
            return 1;
        }

        if( use_on )
        {
            int ap = interaction_ap_trigger(kind, op_num, 1, 0);
            int trigger_type = kind == TORIRSSERVER_INTERACT_LOC ? loc_trigger_type : target_id;
            int ran = TORIRSSERVER_TRIGGER_NONE;

            /*
             * A loc use-on goes through `run_trigger_on_loc`, exactly as the
             * `[oploc<n>]` arm below does, and for the same reason: only that
             * entry point hands the script its active loc. This arm used to
             * call `ToriRSServer_ScriptsRunTrigger`, whose last parameter is the
             * *npc* slot — which is -1 for a loc — so every `[oplocu]` script
             * tree-wide ran with no active loc and the first `loc_param`,
             * `loc_coord`, `loc_angle`, `loc_shape`, `loc_change` or `loc_del`
             * in it aborted with "requires an active entity".
             *
             * The comment in torirs_server_scripts.c's dispatch records this exact
             * class of bug being fixed for `[oploc<n>]`/`[aploc<n>]` on
             * 2026-08-02; the use-on arm was missed, and stayed broken because
             * the only symptom is an abort on the *second* line of a script —
             * the trigger binds and matches, so it reads as a content bug.
             * `[aplocu]` was never affected: it resolves through
             * `run_interaction_trigger`, which already branches on LOC.
             */
            if( ap >= 0 )
            {
                if( kind == TORIRSSERVER_INTERACT_LOC )
                    ran = ToriRSServer_ScriptsRunTriggerOnLoc(
                        srv, ap + 7, trigger_type, category,
                        find_interaction_loc(loc_x, loc_z, loc_level, target_id));
                else
                    ran = ToriRSServer_ScriptsRunTrigger(srv, ap + 7, trigger_type, category, slot);
            }
            if( ran == TORIRSSERVER_TRIGGER_NONE )
                ToriRSServer_Say(srv, "nothing_interesting_message", NULL);
            return 1;
        }

        /*
         * Content's first refusal on the interaction, before any trigger.
         *
         * There is no way to write "ask me about every npc first" as a trigger
         * — a name binding is exclusive and `[opnpc1,_]` shadows every specific
         * handler in the game. Treasure Trails needs it (a cryptic clue can
         * point at any of 218 npcs, 139 of which already own an `[opnpc1,…]`),
         * and so will the Slayer and achievement-diary task hooks.
         *
         * Optional by construction: a tree that defines neither proc pays one
         * failed name lookup and behaves exactly as it did. See
         * `ToriRSServer_ScriptsRunClaim`.
         */
        {
            int32_t claimed = 0;
            int32_t claim_args[2] = { (int32_t)target_id, (int32_t)op_num };

            if( kind == TORIRSSERVER_INTERACT_NPC &&
                ToriRSServer_ScriptsRunClaim(srv, "[proc,interact_npc_claim]", slot, -1,
                                          claim_args, 2, &claimed) &&
                claimed )
                return 1;
            if( kind == TORIRSSERVER_INTERACT_LOC )
            {
                claim_args[0] = (int32_t)target_id;
                if( ToriRSServer_ScriptsRunClaim(
                        srv, "[proc,interact_loc_claim]", -1,
                        find_interaction_loc(loc_x, loc_z, loc_level, target_id), claim_args, 2,
                        &claimed) &&
                    claimed )
                    return 1;
            }
        }

        switch( kind )
        {
        case TORIRSSERVER_INTERACT_NPC:
            if( ToriRSServer_ScriptsFallback(
                    srv, TORIRSSERVER_FALLBACK_OPNPC,
                    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1 + (op_num - 1),
                                                target_id, category, slot)) )
                interaction_engine_npc(srv, slot, op_num);
            break;
        case TORIRSSERVER_INTERACT_LOC:
            /* No fallback row since 2026-08-02: doors, ladders and bank booths
             * are all content, so a loc op that binds nothing gets the
             * reference's own answer for one — `Player.defaultOp`, which is the
             * message and nothing else. Same shape as the use-on arm above, and
             * `FAILED` deliberately says nothing: a script that aborted has
             * already had its turn. */
            if( ToriRSServer_ScriptsRunTriggerOnLoc(srv, SS_TRIGGER_OPLOC1 + (op_num - 1),
                                                   loc_trigger_type, category,
                                                   find_interaction_loc(loc_x, loc_z, loc_level,
                                                                        target_id)) ==
                TORIRSSERVER_TRIGGER_NONE )
                ToriRSServer_Say(srv, "nothing_interesting_message", NULL);
            break;
        case TORIRSSERVER_INTERACT_OBJ:
        {
            /* The pile the click named, handed to the script as its active obj
             * — `Player.getOpTrigger` sets `state.activeObj` for the obj arm the
             * same way it sets `activeNpc` for the npc one. Without it every
             * `obj_*` opcode aborts on the VM's require-an-active-obj check and
             * `[opobj<n>]` can only be written blind. */
            int obj_slot =
                ToriRSServer_WorldGroundFind(srv, loc_x, loc_z, loc_level, target_id);
            int result;

            srv->pending_active_obj =
                obj_slot >= 0 ? ToriRSServer_WorldObjHandle(srv, obj_slot) : 0;
            result = ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPOBJ1 + (op_num - 1),
                                                 target_id, category, -1);
            srv->pending_active_obj = 0;
            /*
             * No fallback, and this is the only one of the three arms that has
             * none — `TORIRSSERVER_FALLBACK_OPOBJ` was deleted here. A miss is
             * `Player.defaultOp` exactly: the message and nothing else. The
             * walk still happened (this runs on arrival), which is also the
             * reference's shape.
             *
             * `TORIRSSERVER_TRIGGER_FAILED` deliberately does NOT speak. A script
             * that aborted is a content bug, and printing "nothing interesting
             * happens" over it is the indistinguishability §3.18 exists to
             * remove — the abort already reports itself.
             */
            if( result == TORIRSSERVER_TRIGGER_NONE )
                ToriRSServer_Say(srv, "nothing_interesting_message", NULL);
            break;
        }
        case TORIRSSERVER_INTERACT_PLAYER:
            /* No engine verb and no fallback: everything a player can do to
             * another player is content's (`Player.defaultOp` for the miss).
             * The subject is -1 — an `[opplayer<n>]` binds on the trigger alone,
             * there being no player "type" to key on. */
            if( ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPPLAYER1 + (op_num - 1), -1,
                                            category, -1) == TORIRSSERVER_TRIGGER_NONE )
                ToriRSServer_Say(srv, "nothing_interesting_message", NULL);
            break;
        default:
            break;
        }
    }
    return 1;
}

/*
 * LostCity Player.pathToPathingTarget — SMART repath at the last waypoint only
 * (Tests 29/30). Full walk_to_approach, not one adjacent tile: a one-tile aim
 * forced walk-speed chase after the first arrival and stacked the player on a
 * mover that stepped onto the stale dest (same deadlock combat already fixed).
 */
void
interaction_path_to_pathing_target(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct ToriRSServerInteraction* interaction = &player->interaction;
    struct CollisionApproach approach;
    int target_x;
    int target_z;
    int size_x;
    int size_z;

    if( interaction->kind != TORIRSSERVER_INTERACT_NPC &&
        interaction->kind != TORIRSSERVER_INTERACT_PLAYER )
        return;
    if( player->waypoint_index > 0 )
        return;
    if( !interaction_target(srv, &target_x, &target_z, &size_x, &size_z) )
    {
        /* See the matching comment in interaction_try: a stale target must
         * drop any route queued toward it, or advance_player keeps walking
         * the player there with nothing left to supervise the queue. */
        steps_clear(player);
        ToriRSServer_WorldInteractionClear(srv);
        return;
    }

    interaction->x = target_x;
    interaction->z = target_z;
    /*
     * Standing inside the target's footprint.
     *
     * Under `under_target_routes_out` this is not a case at all: the approach
     * the line below builds is the exclusive rectangle (shape -2), whose
     * `reached` refuses every overlapping tile, so the ordinary BFS floods
     * across the footprint — a plain npc writes NPC_OCC and the player mover
     * never reads it — and terminates on the first perimeter tile it pops.
     * Expansion order W/E/S/N makes that the nearest face, and a tie resolves
     * west before east before south before north. One straight run, so it
     * compresses to a single corner waypoint and a running player clears it two
     * tiles per tick.
     *
     * Falling through rather than routing here is deliberate: the exit and the
     * approach are the same call, which is exactly the reference's claim that
     * there is no under-target branch on the player side.
     */
    if( !ToriRSServer_SceneFeatures()->under_target_routes_out &&
        distance_to_rect(player->x, player->z, target_x, target_z, size_x, size_z) == 0 )
    {
        interaction_random_walk(srv);
        return;
    }
    interaction_build_approach(srv, size_x, &approach);
    ToriRSServer_WorldWalkToApproach(srv, target_x, target_z, &approach);
}

/*
 * Keep closing distance after a failed try: re-flood when empty or blocked, or
 * give up with cannot_reach_message. Movers are re-aimed by
 * interaction_path_to_pathing_target before the step; this is stall recovery.
 */
static void
interaction_continue_or_give_up(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct ToriRSServerInteraction* interaction = &player->interaction;
    struct CollisionApproach approach;
    int target_x;
    int target_z;
    int size_x;
    int size_z;
    int reached;

    if( interaction->kind == TORIRSSERVER_INTERACT_NONE )
        return;
    if( !interaction_target(srv, &target_x, &target_z, &size_x, &size_z) )
    {
        /* See the matching comment in interaction_try: a stale target must
         * drop any route queued toward it, or advance_player keeps walking
         * the player there with nothing left to supervise the queue. */
        steps_clear(player);
        ToriRSServer_WorldInteractionClear(srv);
        return;
    }

    interaction->x = target_x;
    interaction->z = target_z;
    interaction_build_approach(srv, size_x, &approach);
    reached =
        ToriRSServer_SceneReached(player->level, player->x, player->z, target_x, target_z, &approach);
    if( !reached && interaction->kind == TORIRSSERVER_INTERACT_OBJ && !player_has_waypoints(player) &&
        player->steps_taken == 0 )
    {
        struct CollisionApproach adjacent;

        ToriRSServer_SceneObjApproach(1, &adjacent);
        reached = ToriRSServer_SceneReached(player->level, player->x, player->z, target_x, target_z,
                                        &adjacent);
    }
    if( reached )
        return;

    /*
     * Re-flood when empty or truly blocked — not merely steps_taken==0 on a
     * fresh walk_to_approach (that truncate forced walk-only op approaches).
     * Full corner path so run can take two tiles/tick.
     */
    if( !player_has_waypoints(player) ||
        (player->steps_taken == 0 && player_waypoint_step_blocked(player)) )
    {
        int path_x[TORIRSSERVER_STEP_MAX];
        int path_z[TORIRSSERVER_STEP_MAX];
        int arrive_x = target_x;
        int arrive_z = target_z;
        int n = ToriRSServer_SceneRouteOp(player->level, player->x, player->z, target_x, target_z,
                                       &approach, path_x, path_z, TORIRSSERVER_STEP_MAX, &arrive_x,
                                       &arrive_z);
        if( n > 0 )
        {
            queue_path_as_waypoints(player, path_x, path_z, n);
            player->dest_x = arrive_x;
            player->dest_z = arrive_z;
            return;
        }
        if( player->steps_taken == 0 )
        {
            ToriRSServer_Say(srv, "cannot_reach_message", NULL);
            ToriRSServer_WorldInteractionClear(srv);
            player->clear_map_flag = 1;
            player->dest_x = -1;
            player->dest_z = -1;
            player->waypoint_index = -1;
        }
    }
}

/**
 * Resolve the pending interaction if it is already in range, else leave the
 * walk alone (recover a stalled route if needed).
 *
 * Packet handlers call this so a click on something you are standing next to
 * acts immediately. Per-tick chase is phase_player: try → pathToPathingTarget →
 * move → try (LostCity processInteraction).
 */
void
ToriRSServer_WorldProcessInteraction(struct ToriRSServer* srv)
{
    if( interaction_try(srv, 1) )
        return;
    interaction_continue_or_give_up(srv);
}

/* ------------------------------------------------------------------ */
/* Run energy                                                          */
/* ------------------------------------------------------------------ */

/*
 * Everything the player is carrying, in grams, worn items included.
 *
 * The weights come out of the obj records themselves (config opcode 75), which
 * is why there is no table here: a cape weighs what OldSchool says a cape
 * weighs. Weight-reducing items are negative in the cache and stay negative,
 * so the total can go below zero; the drain formula clamps.
 */
int
player_weight_grams(const struct ToriRSServerPlayer* player)
{
    int total = 0;

    /* Stackable objs are weightless and a non-stackable stack weighs its count.
     * Reference: Player.ts:640-645 — `if (!type || type.stackable) continue;`
     * then `this.runweight += type.weight * item.count`. Ignoring both made a
     * thousand coins weigh a thousand times a coin, and a stack of anything
     * weigh one. */
    for( int i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
    {
        const struct ToriRSServerObjInfo* info;

        if( player->inv[i].obj_id < 0 )
            continue;
        info = ToriRSServer_ObjInfo(player->inv[i].obj_id);
        if( info->stackable )
            continue;
        total += info->weight * player->inv[i].count;
    }
    /* Worn equipment is one per slot, and worn stackables (ammo) are the same
     * weightless case. */
    for( int i = 0; i < TORIRSSERVER_WORN_SLOTS; i++ )
    {
        const struct ToriRSServerObjInfo* info;

        if( player->worn[i].obj_id < 0 )
            continue;
        info = ToriRSServer_ObjInfo(player->worn[i].obj_id);
        if( info->stackable )
            continue;
        total += info->weight;
    }
    return total;
}

/*
 * One tick of energy. WHICH arithmetic is an era decision, and it lives in the
 * feature table: `run_energy_model`, enum ToriRS_RunEnergyModel, implemented in
 * torirs_server_runenergy.c and overridable per boot with TORIRSSERVER_RUN_ENERGY.
 *
 *              drain per running tick               restore per idle tick
 *   classic    67 + 67*kg/64  (kg 0..64)            agility/6 + 8
 *   osrs2025   (60 + 67*kg/64) * (1 - agility/300)  agility/10 + 15
 *
 * Under classic an unencumbered player gets a hair over 149 running ticks from
 * full and a fully-laden one about half that; under osrs2025 the same player at
 * level 99 gets around 250, which is the whole point of the 2025 rework and the
 * reason Agility is worth training outside its own courses.
 *
 * ⚠️ THE REFERENCES DISAGREE about the classic pair, and it is LostCity's that
 * is implemented. Recorded because it decided the shape of the classic line:
 *
 *   LostCity   67 + 67*kg/64  (kg 0..64)         agility/6 + 8      <- classic
 *   OpenRune   64 + min(g,6400)/10000            computes a value, then discards
 *              (RunEnergy.kt:41-42)              it and adds a flat 500 (:51-55)
 *
 * LostCity's drain is strongly weight-dependent (67..134, a 2x span); OpenRune's
 * is not (64..64.64), which makes carried weight almost free. The 67 + 67*w/64
 * form is the widely-attested 2004 one, so it is the one kept. OpenRune's
 * restore is not usable as an authority at all — `recovery` is computed and
 * never read, so its effective rate is a flat 500/tick regardless of Agility.
 *
 * Everything else here is shared by both models, because none of it changed.
 *
 * Energy is spent ONCE PER TICK, not per step. The reference charges the loss
 * in the `else` of `stepsTaken < 2` (Player.ts:705-713), so covering two tiles
 * *is* the base cost — multiplying by the step count charged twice for the
 * only case that can ever reach the branch, and emptied the bar at about
 * 1.98 %/tick instead of 0.99 %.
 *
 * The branch is `move_count < 2`, not `move_count == 0`: a player who runs but
 * has only one tile left to cover regenerates, because a one-tile tick is a
 * walk however the toggle is set.
 *
 * Reaching zero clears the toggle rather than merely refusing to run, which is
 * what makes the orb go dark instead of the player silently walking with a lit
 * orb.
 */
static void
run_energy_tick(
    struct ToriRSServer* srv,
    int run_steps)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int model = ToriRSServer_SceneFeatures()->run_energy_model;
    /* Base level, not boosted: an agility potion neither speeds recovery nor
     * slows the burn. Reference: Player.ts:707 uses `baseLevels[AGILITY]`. */
    int agility = player->stat_level[TORIRSSERVER_STAT_AGILITY];

    /* A delayed player neither burns nor recovers. Reference: Player.ts:703,
     * `if (this.delayed) return;`. */
    if( player->delayed_until > srv->tick )
        return;

    if( run_steps >= 2 )
    {
        /* Clamped inside the model; a weight-reducing set can total negative. */
        int drain =
            ToriRSServer_RunEnergyDrain(model, player_weight_grams(player) / 1000, agility);

        /*
         * Stamina potion: wiki says drain is cut 70% while `%stamina_active`
         * is armed. Content owns the duration countdown
         * (player/scripts/consumption/inferno_potions.rs2's
         * `[timer,stamina_expire]`) and only ever writes 0 or 1 here; this is
         * the one piece of the effect a script cannot express, since
         * run-energy drain itself has no content-facing op.
         *
         * `stamina_active` is a **varbit** (25, a bit of
         * `inferno_temp_noprotect_transmit`), which content's own `%` prefix
         * hides — the name resolves in both namespaces from a script and only
         * one of them from here. This read was
         * `player->varps[ToriRSServer_WorldVarp("stamina_active")]`, and
         * `ToriRSServer_WorldVarp` is a varp lookup: it answered -1 for a varbit
         * name and the drain was decided by `varps[-1]`, the int before the
         * array. It read nonzero, so every player in the world ran at the
         * potion rate — 40 instead of 134 — and no potion was involved.
         */
        if( ToriRSServer_VarbitGet(player, ToriRSServer_Ids()->varbit_stamina_active) )
            drain = drain * 3 / 10;

        player->run_energy -= drain;
        if( player->run_energy <= 0 )
        {
            player->run_energy = 0;
            player->run_toggle = 0;
            player->running = 0;
            ToriRSServer_WorldSetVarp(srv, ToriRSServer_WorldVarp("option_run"), 0);
        }
        return;
    }

    if( player->run_energy < TORIRSSERVER_RUN_ENERGY_MAX )
    {
        player->run_energy += ToriRSServer_RunEnergyRestore(model, agility);
        if( player->run_energy > TORIRSSERVER_RUN_ENERGY_MAX )
            player->run_energy = TORIRSSERVER_RUN_ENERGY_MAX;
    }
}

/* Put the orb's two numbers on the wire, but only when one of them moved: at
 * one packet a tick each they would be a third of everything the mock sends. */
static void
run_energy_flush(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int percent = player->run_energy * 100 / TORIRSSERVER_RUN_ENERGY_MAX;
    /* Kilograms, not grams. UPDATE_RUNWEIGHT's value is read back by CS2's
     * RUNWEIGHT_VISIBLE, and the gameframe prints that with "kg" after it —
     * sending grams put "31892 kg" beside a player carrying 32. */
    int weight = player_weight_grams(player) / 1000;

    if( percent != player->run_energy_sent )
    {
        player->run_energy_sent = percent;
        ToriRSServer_SendRunEnergy(srv->active_player, percent);
    }
    if( weight != player->run_weight_sent )
    {
        player->run_weight_sent = weight;
        ToriRSServer_SendRunWeight(srv->active_player, weight);
    }
}

/* Resolve the exact next tile toward the current waypoint without occupying
 * it. Controller-style content needs this seam before each tile: the route's
 * destination is not necessarily the next step, and a running tick can cross
 * two independently gated tiles. */
static int
player_plan_step(
    struct ToriRSServerPlayer* player,
    int* next_x,
    int* next_z)
{
    int wp_x;
    int wp_z;
    int dx;
    int dz;
    int dir;
    int try_dx;
    int try_dz;
    int extra = player_travel_extra();

    assert(player);
    if( player->waypoint_index < 0 )
        return -1;

    wp_x = player->waypoints[player->waypoint_index].x;
    wp_z = player->waypoints[player->waypoint_index].z;
    if( wp_x == player->x && wp_z == player->z )
    {
        player->waypoint_index--;
        if( player->waypoint_index < 0 )
            return -1;
        wp_x = player->waypoints[player->waypoint_index].x;
        wp_z = player->waypoints[player->waypoint_index].z;
    }

    dx = wp_x > player->x ? 1 : (wp_x < player->x ? -1 : 0);
    dz = wp_z > player->z ? 1 : (wp_z < player->z ? -1 : 0);
    if( dx == 0 && dz == 0 )
        return -1;

    /* Prefer the diagonal when both axes differ. */
    try_dx = dx;
    try_dz = dz;
    dir = ToriRSServer_StepDirection(try_dx, try_dz);
    if( dir >= 0 &&
        ToriRSServer_SceneCanStepExtra(player->level, player->x, player->z, dir, extra) )
        goto planned;

    /* Cardinally: E/W first, then N/S — PathingEntity.takeStep order. */
    if( dx != 0 )
    {
        try_dx = dx;
        try_dz = 0;
        dir = ToriRSServer_StepDirection(try_dx, try_dz);
        if( dir >= 0 &&
            ToriRSServer_SceneCanStepExtra(player->level, player->x, player->z, dir, extra) )
            goto planned;
    }
    if( dz != 0 )
    {
        try_dx = 0;
        try_dz = dz;
        dir = ToriRSServer_StepDirection(try_dx, try_dz);
        if( dir >= 0 &&
            ToriRSServer_SceneCanStepExtra(player->level, player->x, player->z, dir, extra) )
            goto planned;
    }

    return -1;

planned:
    *next_x = player->x + try_dx;
    *next_z = player->z + try_dz;
    return dir;
}

/* Consume one planned step, recording its direction so PLAYER_INFO can spell
 * it out. Port of PathingEntity.validateAndAdvanceStep / takeStep: prefer the
 * diagonal, then E/W, then N/S, and retain a blocked route because another
 * actor may only occupy the tile temporarily. */
static int
player_take_step(struct ToriRSServerPlayer* player)
{
    int next_x;
    int next_z;
    int dir;
    int prev_x;
    int prev_z;

    assert(player);
    prev_x = player->x;
    prev_z = player->z;
    dir = player_plan_step(player, &next_x, &next_z);
    if( dir < 0 )
    {
        player->last_step_x = prev_x;
        player->last_step_z = prev_z;
        return -1;
    }

    player_set_occupancy(player, 0);
    player->x = next_x;
    player->z = next_z;
    player_set_occupancy(player, 1);
    player->last_step_x = prev_x;
    player->last_step_z = prev_z;
    if( player->waypoint_index >= 0 &&
        player->x == player->waypoints[player->waypoint_index].x &&
        player->z == player->waypoints[player->waypoint_index].z )
        player->waypoint_index--;
    return dir;
}

void
advance_player(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int max_tiles;

    assert(player);
    /* Publish the confrontation snapshot before this tick's movement —
     * `Player.processInteraction`'s first two lines. It is the tile a *player*
     * using the "Follow" op (OPPLAYER3/APPLAYER3) walks to, which is the one
     * place the reference reads it. NPC modes deliberately do not: see
     * `npc_walk_to_player`. */
    player->follow_x = player->last_step_x;
    player->follow_z = player->last_step_z;

    /* Running is a request, not a state: the toggle says the player wants to,
     * the energy says whether they can. Deciding it here rather than at the
     * move packet is what makes energy run out mid-walk. */
    player->running = player->run_toggle && player->run_energy > 0;
    max_tiles = player->running ? 2 : 1;

    player->move_count = 0;
    player->steps_taken = 0;
    {
        int from_x = player->x;
        int from_z = player->z;

        for( int i = 0; i < max_tiles; i++ )
        {
            int next_x;
            int next_z;
            int planned = player_plan_step(player, &next_x, &next_z);

            if( planned < 0 )
            {
                /* Keep player_take_step as the one place that records a
                 * collision stall's last_step snapshot. */
                player_take_step(player);
                break;
            }

            /* LostCity's walktrigger is one-shot and content re-arms it when
             * a policy remains active. Run it for each concrete tile, before
             * occupancy changes. A script vetoes with p_walk(coord), exactly
             * as the existing freeze scripts do. Re-plan afterwards because a
             * non-vetoing hook is also allowed to replace the route. */
            if( player->walktrigger >= 0 )
            {
                player->walkstep_coord =
                    ToriRSServer_CoordPack(player->level, next_x, next_z);
                ToriRSServer_ScriptsProcessWalktrigger(srv);
                player->walkstep_coord = 0;
                if( !player_has_waypoints(player) )
                    break;
            }

            int dir = player_take_step(player);
            if( dir < 0 )
                break;
            player->move_dirs[player->move_count++] = dir;
            player->steps_taken++;
        }

        /*
         * TORIRSSERVER_MOVE_TRACE=1 — the tiles this tick, and how the wire will
         * describe them.
         *
         * A running two-step turn can have a one-tile diagonal net displacement,
         * so v239 carries WALK geometry plus RUN traversal. RuneLite uses that
         * traversal to pathfind locally (method2600) and retain the corner tile;
         * the classic stream carries both directions directly.
         */
        if( getenv("TORIRSSERVER_MOVE_TRACE") && (player->steps_taken || from_x != player->x ||
                                             from_z != player->z) )
        {
            int net_x = player->x - from_x;
            int net_z = player->z - from_z;

            fprintf(stderr,
                    "move: %s from=%d,%d to=%d,%d steps=%d net=(%d,%d) %s wire=%s\n",
                    player->display_name, from_x, from_z, player->x, player->z,
                    player->steps_taken, net_x, net_z,
                    player->running ? "run" : "walk",
                    (net_x >= -1 && net_x <= 1 && net_z >= -1 && net_z <= 1)
                        ? ((net_x && net_z) ? "WALK(diagonal)" : "WALK")
                        : "RUN");
        }
    }

    if( !player_has_waypoints(player) && player->dest_x >= 0 )
    {
        /* Arrived (or never had a route that reached). Clear the map flag when
         * we actually walked somewhere this interaction; unreachable handling
         * is process_interaction's job. */
        if( player->steps_taken > 0 ||
            (player->x == player->dest_x && player->z == player->dest_z) )
        {
            player->clear_map_flag = 1;
            player->dest_x = -1;
            player->dest_z = -1;
        }
    }
}

/*
 * Fire the first floor-loc script bound to the tile on which movement ended.
 *
 * This intentionally runs once, after the whole walk/run update. A running
 * turn has two concrete steps, but an underfoot hazard is only entered when the
 * player finishes on it; firing between the two steps would make a one-tile
 * trap impossible to run across. Loc footprints can overlap, so walk the
 * scene's stable slot order and stop after the first script actually binds.
 */
static void
player_process_locstep(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    if( !player || player->steps_taken <= 0 )
        return;

    for( int slot = 0;; slot++ )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(slot);
        int type;
        int result;

        if( !loc )
            break;
        if( !loc->active || loc->level != player->level || player->x < loc->x ||
            player->x >= loc->x + loc->size_x || player->z < loc->z ||
            player->z >= loc->z + loc->size_z )
            continue;

        type = ToriRSServer_LocResolveTransform(player, loc->loc_id);
        if( type < 0 )
            type = loc->loc_id;
        result = ToriRSServer_ScriptsRunTriggerOnLoc(
            srv, SS_TRIGGER_LOCSTEP, type, ToriRSServer_LocCategory(type), slot);
        if( result != TORIRSSERVER_TRIGGER_NONE )
            return;
    }
}

/*
 * Build one scene window at one zone origin, and replay everything durable
 * onto it.
 *
 * The instance check is here, in one place, rather than at each of the callers:
 * whether a scene is instanced is a property of *where* it is, and both the
 * login build and every re-centre want the same answer. Reading it from the
 * coordinate — rather than from a flag someone has to remember to set — is what
 * keeps the server's collision and the client's REBUILD_REGION descriptors from
 * disagreeing about which map the scene is.
 */
/*
 * Which reservation a window centred at this zone is a copy of, and which BUILD
 * of it.
 *
 * The handle alone is not enough: a reservation is reused in place — same slot,
 * same base — so re-entering an activity keeps the handle and the coordinates
 * while the zones underneath are refilled from a different square. Nothing in
 * the window moves, so `maybe_rebuild` sees no reason to re-centre and the
 * server's collision copy stays the PREVIOUS occupant's.
 *
 * Measured on the Theatre: `::tob 2` refilled reservation 1 from m51_69 and the
 * server's scene remained m49_68 — Xarpus's square — for the rest of the run,
 * so every `loc_find` in the Bloat room answered about the wrong room and its
 * central tank read as absent.
 *
 * The generation is bumped by every `ToriRSServer_MapInstanceBuild`, so
 * comparing it against the one the window was built from catches exactly that.
 */
static int
zone_centre_generation(
    int zone_x,
    int zone_z)
{
    int handle =
        ToriRSServer_MapInstanceFind(ToriRSServer_SceneOrigin(zone_x) + TORIRSSERVER_SCENE_TILES / 2,
                                     ToriRSServer_SceneOrigin(zone_z) + TORIRSSERVER_SCENE_TILES / 2);

    return handle ? ToriRSServer_MapInstanceGeneration(handle) : 0;
}

static void
world_window_scene_build(
    struct ToriRSServer* srv,
    struct ToriRSServerSceneWindow* window,
    int zone_x,
    int zone_z)
{
    struct ToriRSServerSceneWindow* bound = ToriRSServer_SceneBoundWindow();
    struct ToriRSServerMapInstanceWindow instance_window;
    int centre_x = ToriRSServer_SceneOrigin(zone_x) + TORIRSSERVER_SCENE_TILES / 2;
    int centre_z = ToriRSServer_SceneOrigin(zone_z) + TORIRSSERVER_SCENE_TILES / 2;

    ToriRSServer_SceneBindWindow(window);
    if( ToriRSServer_MapInstanceFind(centre_x, centre_z) == 0 )
    {
        ToriRSServer_SceneBuild(ToriRSServer_WorldCacheDir(), zone_x, zone_z);
    }
    else
    {
        ToriRSServer_MapInstanceWindow(zone_x, zone_z, &instance_window);
        ToriRSServer_SceneBuildInstance(ToriRSServer_WorldCacheDir(), zone_x, zone_z,
                                        &instance_window);
    }
    /* A build reads the cache, so the fresh window has forgotten every runtime
     * loc change. The durable record is the ZoneMap's; replay it onto the
     * window just built — the other windows still carry theirs. */
    world_locs_reapply_window(srv, window);
    ToriRSServer_SceneBindWindow(bound);

    /* The entities' own collision, which the rebuilt map does not carry.
     * Occupancy broadcasts into every built window and adds are ORs, so
     * windows that already carry it are restamped idempotently. */
    world_occupancy_restamp(srv);
    /*
     * The roster follows the windows, and every build comes through here —
     * login, per-player re-centre, instance — which is why the sync hangs off
     * this rather than off each of their call sites.
     *
     * After the scene, not before: a spawn is placed against collision
     * (`npc_spawn` stamps occupancy), and standing one up against the scene it
     * is leaving would file it in the wrong zones.
     */
    world_static_npcs_sync(srv);
}

/* Build (or re-centre) one PLAYER's window on where they stand, moving their
 * wire origin with it. */
static void
world_player_scene_build(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    player->zone_x = player->x >> 3;
    player->zone_z = player->z >> 3;
    /* Attempted, not necessarily built: cacheless the window stays unbuilt,
     * and maybe_rebuild's skip test must key off the attempt or it re-builds
     * every tick. See scene_build_attempted in torirs_server.h. */
    player->scene_build_attempted = 1;
    world_window_scene_build(srv, ToriRSServer_PlayerSceneWindow(player), player->zone_x,
                             player->zone_z);
    player->scene_built_generation = zone_centre_generation(player->zone_x, player->zone_z);
}

void
ToriRSServer_WorldSceneRebuild(struct ToriRSServer* srv)
{
    srv->scene_built_generation = zone_centre_generation(srv->zone_x, srv->zone_z);
    world_window_scene_build(srv, ToriRSServer_SceneWindowRoot(), srv->zone_x, srv->zone_z);
}

/*
 * An instance has just been assembled: show it to whoever is standing in it.
 *
 * Nothing happens for the ordinary case, which is content building a house
 * *before* teleporting anyone into it — the teleport rebuilds the scene anyway.
 * This is for the other case, which construction needs: adding a room to a house
 * the player is already inside. The zones changed under them, so their collision
 * and their client's scene are both stale, and only a rebuild fixes it.
 */
void
ToriRSServer_WorldMapInstanceBuilt(
    struct ToriRSServer* srv,
    int handle)
{
    for( int i = 0; i < srv->player_count; i++ )
    {
        struct ToriRSServerPlayer* player = &srv->players[i];

        if( !player->active )
            continue;
        if( ToriRSServer_MapInstanceFind(player->x, player->z) != handle )
            continue;
        /* Each occupant's own window, not a shared one: the zones changed
         * under them, so their collision and their client's scene are both
         * stale. */
        world_player_scene_build(srv, player);
        player->rebuild_pending = 1;
        player->place_dirty = 1;
        /* Same reason as maybe_rebuild: the client's scene is being replaced, so
         * it holds no zones and phase 10 re-states each one. */
        ToriRSServer_ZonePlayerReset(player);
        player->move_count = 0;
    }
    /* A first build may have upgraded the active player from the root binding
     * to their own window — refresh the binding. */
    ToriRSServer_WorldSetActive(srv, srv->active_player);
}

int
ToriRSServer_WorldMapInstanceFree(
    struct ToriRSServer* srv,
    int handle)
{
    int x;
    int z;
    int width;
    int height;

    if( !ToriRSServer_MapInstanceBounds(handle, &x, &z, &width, &height) )
        return 0;

    ToriRSServer_ZoneLocsClearRect(srv, x, z, width, height);
    /* A timed revert is durable state too. If one survives the release it can
     * mutate the next tenant's freshly built scene several ticks later. */
    for( int i = 0; i < TORIRSSERVER_LOC_REVERT_MAX; i++ )
    {
        struct ToriRSServerLocRevert* entry = &srv->loc_reverts[i];

        if( entry->active && entry->x >= x && entry->x < x + width && entry->z >= z &&
            entry->z < z + height )
            entry->active = 0;
    }
    /*
     * A released square has no owner that can collect its floor objects, and
     * the pool can immediately hand those coordinates to another encounter.
     * Keep this in the world-level release rather than the script opcode so
     * the disconnect fallback and every future caller get the same teardown.
     * Map spawns are not cloned into instance-pool coordinates, but clearing
     * their respawn latch as well makes an accidental one unable to reappear
     * in the next tenant's room.
     */
    for( int i = 0; i < TORIRSSERVER_GROUND_MAX; i++ )
    {
        struct ToriRSServerGroundObj* obj = &srv->ground[i];

        if( !obj->active || obj->x < x || obj->x >= x + width || obj->z < z ||
            obj->z >= z + height )
            continue;
        ToriRSServer_WorldGroundTake(srv, i);
        obj->respawn_tick = -1;
    }
    /*
     * And its npcs, for the same reason and with the bigger blast radius.
     *
     * Everything above releases something the next tenant would inherit
     * silently; a surviving npc is one it inherits VISIBLY. The pool re-issues
     * these coordinates immediately, so the boss of the room somebody just
     * left is standing in — and attacking inside — the room somebody just
     * built. Measured: `::toa 5` then `::toa 9` put Kephri in Ba-Ba's arena,
     * and `::toa 7` then `::toa 10` put Akkha in the Wardens'.
     *
     * The Theatre never showed it because every ToB boss `npc_del`s itself as
     * it dies, so its rooms are empty by the time they are freed. That is a
     * property of how that raid ends its rooms, not a guarantee the pool can
     * rely on: a room left early, a wipe, or a debugproc frees a room with its
     * boss alive. So the release owns it.
     *
     * All four levels, deliberately — an instance is a whole square-column and
     * three ToA rooms live on plane 1.
     */
    for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        struct ToriRSServerNpc* npc = &srv->npcs[i];

        if( !npc->active || npc->x < x || npc->x >= x + width || npc->z < z ||
            npc->z >= z + height )
            continue;
        ToriRSServer_WorldNpcFree(srv, i);
    }
    return ToriRSServer_MapInstanceFree(handle);
}

/*
 * Re-centre a player's scene window when they near its edge.
 *
 * The client holds a 104x104 scene based at (zone - 6) * 8. Once a player is
 * within 16 tiles of an edge their scene has to be rebuilt around them. The
 * client shifts every kept entity/camera by the base-tile delta
 * (App_WorldRebuildShift / deob method3310) — this is not a teleport. LostCity
 * BuildArea.rebuildNormal sends REBUILD_NORMAL without setting player.tele;
 * place_dirty stays off so PLAYER_INFO can keep walk/run bits and mid-tile
 * interpolation survives.
 *
 * **Per player, per window.** The scene used to be the world's one window and
 * re-centring it re-centred it for everybody, which made the one build area a
 * genuine multiplayer limit: two players more than ~70 tiles apart pulled the
 * origin back and forth and rebuilt each other's scene every tick. Every
 * player now owns a window (`ToriRSServer_PlayerSceneWindow`) with its own
 * origin (`player->zone_x`), only the player who crossed their own margin is
 * rebuilt, and nobody else's wire origin moves — so nobody else is told
 * anything. The world's root window (`srv->zone_x`) stays anchored at the home
 * zone for world logic that acts with no player in hand.
 */
void
maybe_rebuild(struct ToriRSServer* srv)
{
    int edge = TORIRSSERVER_SCENE_TILES - TORIRSSERVER_REBUILD_MARGIN;
    int rebuilt = 0;

    /*
     * A reservation rebuilt UNDER the root window is a new map at the same
     * address. Nothing moves the root, so only its contents can go stale;
     * rebuild in place (the rebuild re-stamps srv->scene_built_generation).
     */
    if( zone_centre_generation(srv->zone_x, srv->zone_z) != srv->scene_built_generation )
        ToriRSServer_WorldSceneRebuild(srv);

    for( int i = 0; i < srv->player_count; i++ )
    {
        struct ToriRSServerPlayer* player = &srv->players[i];
        int local_x;
        int local_z;

        if( !player->active )
            continue;
        local_x = player->x - ToriRSServer_SceneOrigin(player->zone_x);
        local_z = player->z - ToriRSServer_SceneOrigin(player->zone_z);
        /* The attempt flag, NOT the window's built bit: cacheless builds are a
         * documented fallback (no collision, window stays unbuilt), so keying
         * the skip off "built" re-attempted the build — and reset every
         * player's zones — every tick, forever. The margins alone decide a
         * re-centre once a placement exists. */
        {
            /*
             * A DIFFERENT INSTANCE IS A DIFFERENT MAP, even at the same
             * window.
             *
             * The window is 104 tiles and an instance square is 64, so a
             * teleport from one reservation to a neighbouring one can land
             * well inside the margins and move the window not at all. The
             * window then stays centred on the square the player LEFT — which
             * content has usually just released — and the next build at that
             * stale centre answers "not instanced" and builds the pool's empty
             * land instead: a scene with no locs at all, and a `loc_find` in
             * the new room that reports the room is missing.
             *
             * Measured on the Theatre, walking room 3 -> room 4: the Nylocas
             * square and Sotetseg's are both inside one window, so the
             * Sotetseg chamber built as "scene built at zone 803,14 — 0 locs"
             * and its exit passage could not be found on any plane. The
             * generation check in phase_client_out sees the change but
             * rebuilds at the stale centre, so it cannot repair this on its
             * own.
             *
             * Zones are 8 tiles and squares are 64, both aligned, so the
             * re-centre below puts the window's centre tile back inside the
             * player's own square and the condition cannot re-trigger against
             * itself.
             *
             * The generation half of the test catches the other stale case: a
             * reservation rebuilt IN PLACE under an unmoved window — same
             * handle, same base, new zones (see zone_centre_generation).
             */
            int window_handle = ToriRSServer_MapInstanceFind(
                ToriRSServer_SceneOrigin(player->zone_x) + TORIRSSERVER_SCENE_TILES / 2,
                ToriRSServer_SceneOrigin(player->zone_z) + TORIRSSERVER_SCENE_TILES / 2);

            if( player->scene_build_attempted &&
                local_x >= TORIRSSERVER_REBUILD_MARGIN && local_x < edge &&
                local_z >= TORIRSSERVER_REBUILD_MARGIN && local_z < edge &&
                ToriRSServer_MapInstanceFind(player->x, player->z) == window_handle &&
                zone_centre_generation(player->zone_x, player->zone_z) ==
                    player->scene_built_generation )
                continue;
        }

        /*
         * The server's collision window moves with this client's scene, and a
         * build re-reads the cache — the durable loc record and the entity
         * occupancy are replayed inside world_window_scene_build, which is
         * what puts collision back where the clients believe it is.
         */
        world_player_scene_build(srv, player);
        player->rebuild_pending = 1;
        /* No place_dirty: edge rebuild is not a teleport. Login / P_TELEPORT /
         * plane change / instance room-add still set it where absolute place
         * is required. */
        /* REBUILD_NORMAL resets the client's scene to the cache's version, which
         * un-opens every door it had been told about and drops every obj. So the
         * client is treated as holding no zones at all and phase 10 re-sends
         * each one in full. Cleared here, where the rebuild is *decided*, rather
         * than where it is sent: phase 10 is where the zones go out, so clearing
         * at send time would be a tick late. */
        ToriRSServer_ZonePlayerReset(player);
        /* Tracked npcs and players deliberately survive: the client shifts
         * every kept entity by the base-tile delta when it rebuilds
         * (App_WorldRebuildShift), so their slots stay valid. Dropping and
         * re-adding them would instead re-spawn into slots the client still
         * holds. */
        rebuilt = 1;
    }

    /* A first build upgrades the active player from the root binding to their
     * own window — refresh the binding. */
    if( rebuilt )
        ToriRSServer_WorldSetActive(srv, srv->active_player);
}

/*
 * A player-scoped map view which does not move the player.
 *
 * REBUILD_NORMAL normally derives its centre from this player's own scene
 * window origin. Scrying is different: only the viewer loads a remote
 * normal-world scene, while their entity, collision (their own window,
 * unmoved), zone triggers and every other player remain in the POH instance. The arbitrary-centre rebuild therefore goes
 * straight to this client and the authoritative world is left untouched.
 *
 * Scene-local output is withheld in phase_client_out for the lifetime of the
 * view. The deadline is an engine fail-safe, not gameplay policy: content still
 * calls REMOTE_VIEW_END at its chosen end, while a cancelled script or missed
 * modal close cannot strand the client in the remote scene forever.
 */
void
ToriRSServer_WorldRemoteViewStart(
    struct ToriRSServerPlayer* player,
    int x,
    int z,
    int level,
    int ticks)
{
    struct ToriRSServer* srv;

    if( !player || !player->active )
        return;
    srv = player->world;
    if( !srv )
        return;

    if( player->remote_view_active )
        ToriRSServer_WorldRemoteViewEnd(player);

    player->remote_view_active = 1;
    player->remote_view_until = srv->tick + (ticks > 0 ? ticks : 1);
    player->remote_view_saved_action_locked = player->action_locked;
    player->action_locked = 1;
    ToriRSServer_WorldInteractionClearAt(player);
    player->waypoint_index = -1;
    player->dest_x = -1;
    player->dest_z = -1;

    ToriRSServer_SendRebuildNormalAt(player, x >> 3, z >> 3);
    if( srv->verbose )
        fprintf(stderr,
                "torirsserver: remote view start at %d,%d,%d until tick %d (player remains %d,%d,%d)\n",
                x, z, level, player->remote_view_until, player->x, player->z,
                player->level);
}

void
ToriRSServer_WorldRemoteViewEnd(struct ToriRSServerPlayer* player)
{
    struct ToriRSServer* srv;

    if( !player || !player->remote_view_active )
        return;
    srv = player->world;

    player->remote_view_active = 0;
    player->remote_view_until = 0;
    player->action_locked = player->remote_view_saved_action_locked;
    player->remote_view_saved_action_locked = 0;

    /* The next client-out phase restores the player's real normal/instance
     * scene and places the local actor absolutely inside it. Resetting the
     * zone window makes every dynamic POH loc replay after that rebuild. */
    player->rebuild_pending = 1;
    player->place_dirty = 1;
    ToriRSServer_ZonePlayerReset(player);
    ToriRSServer_SendCamReset(player);

    if( srv && srv->verbose )
        fprintf(stderr, "torirsserver: remote view end; restoring player scene at %d,%d,%d\n",
                player->x, player->z, player->level);
}

/* ------------------------------------------------------------------ */
/* NPCs                                                                */
/* ------------------------------------------------------------------ */

void
ToriRSServer_WorldNpcTeleport(
    struct ToriRSServerNpc* npc,
    int x,
    int z,
    int level)
{
    assert(npc);

    npc_set_occupancy(npc, 0);
    npc->x = x;
    npc->z = z;
    npc->level = level;
    npc_set_occupancy(npc, 1);
    /* The reference seeds the same pair, so the npc faces away from the tile it
     * would have stepped from rather than holding the heading it arrived with. */
    npc->last_step_x = npc->x - 1;
    npc->last_step_z = npc->z;
    npc_clear_waypoints(npc);
    npc->step_dir = -1;
    npc->run_dir = -1;
    npc->tele = 1;
}

void
ToriRSServer_WorldNpcQueueWaypoint(
    struct ToriRSServerNpc* npc,
    int x,
    int z)
{
    assert(npc);
    npc_queue_waypoint(npc, x, z);
}

/*
 * What this npc does when nothing is being done to it — see the header for why
 * it is one function and not three copies of the same three lines.
 *
 * Reads the npc rather than the def because `wander_radius` is per-npc: a
 * `nomove` record zeroes it at spawn, and an npc whose radius was taken away
 * must not come back a wanderer.
 */
int
ToriRSServer_WorldNpcDefaultMode(const struct ToriRSServerNpc* npc)
{
    const struct ToriRSServerNpcDef* def;
    int mode;

    assert(npc);
    def = npc->def ? npc->def : ToriRSServer_ContentNpcDefault();
    /*
     * Wander is the *default*, not the absence of a mode — see the field's
     * comment. An npc with no radius starts on `none`.
     *
     * A content `defaultmode=` outranks both, and patrol is why it exists:
     * Hans walks a fixed ring round the castle grounds, which is a route, not a
     * radius. Expressing him as a wanderer (which is what this tree did) puts
     * him somewhere random in a hundred tiles and makes the greeter outside the
     * castle the hardest npc in Lumbridge to find.
     */
    mode = npc->wander_radius > 0 ? TORIRSSERVER_NPCMODE_WANDER : TORIRSSERVER_NPCMODE_NONE;
    /*
     * `defaultmode_stated`, not `defaultmode != NONE`. `TORIRSSERVER_NPCMODE_NONE`
     * is zero, so the old test could never be true for `defaultmode=none` and
     * every one of those lines in the content tree was a no-op — see the
     * field's comment for what that cost.
     */
    if( def->defaultmode_stated )
        mode = def->defaultmode;
    if( def->patrol_count > 0 )
        mode = TORIRSSERVER_NPCMODE_PATROL;
    return mode;
}

int
npc_spawn(
    struct ToriRSServer* srv,
    int type,
    int x,
    int z,
    int level)
{
    const struct ToriRSServerNpcDef* def;

    /*
     * NPC_INFO's nearby-entity index is not the cache id namespace. Revision
     * 239 uses a 16-bit per-client index and a 14-bit initial type. A type
     * above 0x3fff is installed through the same packet's mask-0x1 p2Alt3
     * transformation update. Reject only the reserved 0xffff value that the
     * transformation block spells as "none".
     */
    if( type < 0 || type > TORIRSSERVER_NPC_CONFIG_MAX )
    {
        fprintf(
            stderr,
            "torirsserver: npc type %d exceeds the NPC config id wire range; not spawned\n",
            type);
        return -1;
    }

    /* Content first, engine defaults second. A spawn is allowed to name an npc
     * no config block describes — it gets 10 hitpoints and unarmed animations,
     * which is enough to be a target rather than a crash. */
    def = ToriRSServer_ContentNpc(type);
    if( !def )
        def = ToriRSServer_ContentNpcDefault();

    for( int slot = 0; slot < TORIRSSERVER_NPC_MAX; slot++ )
    {
        struct ToriRSServerNpc* npc = &srv->npcs[slot];
        if( npc->active || npc->pending_free )
            continue;
        /*
         * Unfile the previous occupant before taking its slot — torirs_server_zone.h
         * asks the pool allocators to do exactly this, and this is the pool
         * allocator.
         *
         * A slot freed and handed out again inside one tick used to be a live
         * hazard here: the ZoneMap would still hold the slot number under the
         * old zone, and a client subscribed to that zone would be holding the
         * *new* npc under the old one's position — and separately, NPC_INFO
         * could splice the new npc's data (including a same-tick hit) onto a
         * client's already-tracked entity for that slot, since nothing on the
         * wire lets a client tell "different npc now" from "same npc,
         * continuing" (confirmed against the real client's own NPC_INFO
         * decoder — see docs/torirs_server_npc_slot_reap.md).
         *
         * The loop's `npc->pending_free` check above is what actually closes
         * that: a slot freed via `ToriRSServer_WorldNpcFree` cannot reach this
         * point until `ToriRSServer_WorldNpcReap` has run, which happens once
         * per tick, after every player's NPC_INFO already went out — so by
         * the time a slot is reused, no client can still be resolving it as
         * the old npc. The refile call below is now defense in depth rather
         * than the fix, kept because it is cheap and because every other free
         * site not routed through `ToriRSServer_WorldNpcFree` (there should be
         * none — see the doc) would otherwise leave the ZoneMap stale too.
         */
        ToriRSServer_ZoneNpcRefile(srv, slot);
        uint16_t generation = (uint16_t)(npc->generation + 1u);

        if( generation == 0 )
            generation = 1;
        memset(npc, 0, sizeof(*npc));
        npc->generation = generation;
        npc->active = 1;
        npc->type = type;
        /* The form every timed `npc_changetype` unwinds to. Set here and
         * nowhere else — see the field. */
        npc->spawn_type = type;
        npc->x = x;
        npc->z = z;
        npc->level = level;
        npc->spawn_x = x;
        npc->spawn_z = z;
        npc->spawn_level = level;
        /* Projected now, not left to the memset.
         * `ToriRSServer_WorldRefreshObservation` overwrites these at the top of
         * every tick's info phase, but an encoder reached before the first of
         * those — which is every selftest that builds a world and encodes
         * without ticking — would otherwise read 0,0 and place the npc that
         * far from whoever is watching. Content can and does spawn straight
         * onto a deck, so this is the real projection rather than a copy of
         * x/z. */
        ToriRSServer_WorldNpcRefreshObservation(srv, npc);
        npc->face_dir = def->facing >= 0 ? def->facing : TORIRSSERVER_FACE_SOUTH;
        npc->def = def;
        npc->wander_radius = def->nomove ? 0 : def->wanderrange;
        {
            /* The ungated row: a nameless multinpc instance still has a
             * footprint and still has to obey its turnspeed. */
            const struct ToriRSServerNpcInfo* info = ToriRSServer_NpcInfoRecord(type);
            npc->size = (info && info->size > 0) ? info->size : 1;
            static int dbg_zuk_size = -1;
            if( dbg_zuk_size < 0 )
                dbg_zuk_size = getenv("DBG_ZUK_SIZE") != NULL;
            if( dbg_zuk_size )
                fprintf(stderr, "DBG spawn type=%d x=%d z=%d info=%p info_size=%d npc_size=%d\n",
                        type, x, z, (void*)info, info ? info->size : -1, npc->size);
            /*
             * NPC_INFO measures view range to the footprint, and the ZoneMap's
             * zone pre-reject pads by TORIRSSERVER_NPC_SIZE_MAX to match. An npc
             * past that pad can be dropped from the candidate set while its
             * body is in plain view — the same class of disappearance the
             * footprint measure exists to fix, so it says so rather than
             * waiting to be rediscovered from a screenshot.
             */
            if( npc->size > TORIRSSERVER_NPC_SIZE_MAX )
                fprintf(stderr,
                        "torirsserver: npc type %d is size %d, above TORIRSSERVER_NPC_SIZE_MAX (%d) — "
                        "it can leave view early on its east/north sides; raise the "
                        "constant\n",
                        type, npc->size, TORIRSSERVER_NPC_SIZE_MAX);
            /* The server overlay wins when it states one, because the cache
             * the server booted from is not necessarily the cache the client
             * did. Unstated (-1) defers to the record. */
            npc->turnspeed = def->turnspeed >= 0 ? def->turnspeed
                                                 : (info ? info->turnspeed : 32);
        }
        npc->blockwalk = def->blockwalk;
        npc->blocksight = def->blocksight;
        /* `timer=<ticks>` arms the type's `[ai_timer]` at spawn. The hook is
         * resolved by type when it fires, so a changetype picks up the new
         * type's — which is exactly what Troll Stronghold's sleeping prison
         * guards need, each pair swapping between the awake and asleep record
         * from inside its own timer. */
        npc->timer_interval = def->timer > 0 ? def->timer : 0;
        npc->timer_clock = 0;
        npc->last_step_x = x - 1;
        npc->last_step_z = z;
        npc->follow_x = npc->last_step_x;
        npc->follow_z = npc->last_step_z;
        npc->waypoint_index = -1;
        npc->stuck_counter = 0;
        ToriRSServer_NpcResetDefaults(npc);
        npc->patrol_index = 0;
        npc->patrol_pause = 0;
        ToriRSServer_WorldNpcRoamStagger(srv, npc);
        npc->step_dir = -1;
        npc->run_dir = -1;
        npc->face_entity = -1;
        /* Explicit, because the memset above makes it 0 and 0 is a *sequence
         * id*: a fresh npc would arrive holding an incumbent animation the
         * priority gate has to beat. */
        npc->anim_id = -1;

        npc->base_hitpoints = def->hitpoints > 0 ? def->hitpoints : 1;
        npc->hitpoints = npc->base_hitpoints;
        npc->max_hitpoints = npc->base_hitpoints;
        /* Explicit, because the memset above makes it 0 and 0 is
         * TORIRSSERVER_HUNT_NONE: every aggressive npc in the world would spawn
         * passive. See the field. */
        npc->huntmode = def->huntmode;
        npc->combat_target = -1;
        /* Explicit for the same reason as `combat_target` beside it: the memset
         * above makes it 0, and 0 is npc slot zero. */
        npc->combat_target_npc = -1;
        npc->death_tick = -1;
        npc->respawn_tick = -1;
        /* Explicit, because the memset above makes it 0 and 0 is a *tick*:
         * every npc in the world would vanish on the first pass. */
        npc->despawn_tick = -1;
        /* Explicit, because the memset above makes it 0 and 0 is a valid
         * roster index: every script-created npc would claim spawn 0 and be
         * retired the first time the window moved past it. The roster sync
         * overwrites this immediately after the call that is its own. */
        npc->static_spawn = -1;
        /* Phase 3 runs `[ai_spawn]` on the next tick — see the field. */
        npc->spawn_pending = 1;

        /*
         * Animations are content's, all three, with no engine fallback.
         *
         * There was one: a convention that built a sequence name out of the
         * npc's *display name* (`"Goblin"` -> `goblin_attack`) and asked the
         * cache for it. It is gone. The engine naming content is the thing this
         * server is organised against, and this particular instance was also
         * wrong often — the cache has no `man_attack`, `woman_attack`,
         * `guard_attack` or `duck_attack`, so the convention answered -1 for
         * every human and every duck while looking authoritative for goblins.
         *
         * An npc nothing describes inherits the `[default]` block instead
         * (general/configs/npc_default.npc), which is a content file an author
         * can read and change.
         */
        npc->attack_seq = def->attack_anim;
        npc->block_seq = def->defend_anim;
        npc->death_seq = def->death_anim;
        /* Beside the animation each one belongs to, from the same record and on
         * the same line, because they are one event. */
        npc->attack_sound = def->attack_sound;
        npc->block_sound = def->defend_sound;
        npc->death_sound = def->death_sound;

        npc_set_occupancy(npc, 1);

        /* The per-tick phases walk to this rather than to TORIRSSERVER_NPC_MAX,
         * which is a memory ceiling now and not the roster. */
        if( slot >= srv->npc_slot_max )
            srv->npc_slot_max = slot + 1;
        return slot;
    }

    {
        int active = 0;

        for( int slot = 0; slot < TORIRSSERVER_NPC_MAX; slot++ )
        {
            if( srv->npcs[slot].active )
                active++;
        }
        fprintf(stderr,
                "torirsserver: no free npc slot for type %d at %d,%d (active=%d/%d slot_max=%d)\n",
                type, x, z, active, TORIRSSERVER_NPC_MAX, srv->npc_slot_max);
    }
    return -1;
}

/*
 * The despawn choke point. See docs/torirs_server_npc_slot_reap.md and the note
 * ahead of the `pending_free` check in `npc_spawn` above for why this exists
 * rather than every despawn site just writing `npc->active = 0` itself.
 *
 * `active` still clears immediately — deliberately. Same-tick game logic
 * (`npc_find`, `huntall`, `npc_hastarget`, combat target resolution, ...)
 * has to keep seeing this npc as gone the instant it dies; that half of the
 * contract is unchanged. What's deferred is only the slot's ELIGIBILITY for
 * `npc_spawn`'s free-slot scan: freeing it is recorded as a queued command
 * instead, and the slot cannot be handed to a new npc until
 * `ToriRSServer_WorldNpcReap` drains that queue — once per tick, after every
 * player's NPC_INFO for this tick has already gone out.
 */
void
ToriRSServer_WorldNpcFree(
    struct ToriRSServer* srv,
    int slot)
{
    struct ToriRSServerNpc* npc;

    assert(srv);
    if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active )
        return; /* already dead (or already queued) — do not double-queue */

    /*
     * A CORPSE THAT WAS NEVER SEEN TO DIE.
     *
     * The invariant behind every "it just disappeared", stated at the one
     * place every removal passes through. An npc that reached zero hitpoints
     * — the engine's death, a script's `npc_statsub`, anything — and is now
     * being taken off every client having never had its own `death_seq` put
     * on the wire is a death the player watched as a vanishing. Counting
     * animations ISSUED proves some played; only this counts the ones missed.
     *
     * Not an assert, and not conditional on HOW it died: an npc with no death
     * animation (`death_seq < 0`) and a room teardown removing living npcs
     * (`hitpoints > 0`) are both excluded rather than excused.
     */
    static int anim_lost = -1;
    if( anim_lost < 0 )
        anim_lost = getenv("TORIRSSERVER_ANIM_LOST") != NULL;
    if( anim_lost )
        fprintf(stderr,
                "torirsserver: npc_free type=%d slot=%d tick=%d hp=%d death_tick=%d "
                "stage=%d death_seq=%d sent=%d\n",
                npc->type, slot, srv->tick, npc->hitpoints, npc->death_tick,
                npc->death_stage, npc->death_seq, npc->death_seq_sent);
    /*
     * NOT WHILE ITS ANIMATION IS STILL QUEUED.
     *
     * See `ToriRSServerNpc.free_deferred_for_anim`. The encoders decide keep vs
     * remove from `active`, which the line below clears, so a removal issued
     * in the npc phase on the tick an animation was played takes the animation
     * off the wire with it. Held for one phase instead: NPC_INFO ships the
     * animation in phase 10, phase cleanup completes the free, and the client
     * sees the remove on the next tick. Once only — the flag stops the
     * deferred call from deferring again.
     */
    /*
     * AND NOT BEFORE THE CORPSE HAS OUTLIVED ITS OWN DEATH ANIMATION.
     *
     * `death_delay` is the engine's answer to "how long does a corpse lie
     * there" and it is tuned to the death sequence's length (2 ticks = 60
     * client cycles against `elemental_death`'s 59). The engine's own death
     * path honours it; a SCRIPT that plays the death itself and removes the
     * npc on its own clock does not, and its arithmetic was measured wrong —
     * a Nylocas Matomenos absorbed at the Maiden was removed on the very tick
     * its animation was played, so the client had one tick of a two-tick
     * animation, or none.
     *
     * Held here rather than corrected in each script: "a death animation is
     * seen in full" is a property of the engine, and the next encounter to
     * schedule its own death will not remember. The npc is already at zero
     * hitpoints and out of the fight; the only thing this extends is how long
     * the corpse is drawn.
     */
    /* `death_seq_tick < 0` means the stamp (phase cleanup) has not run yet,
     * which is to say the animation was played on THIS tick — the worst case
     * and the one that produced a removal in the same tick as the seq. */
    /*
     * NOT held between "death armed" and "death animation played", though it
     * is tempting. A removal landing in that window (a room teardown sweeping
     * the arena) does lose the animation — but holding it delays the teardown,
     * and the room lifecycle is load-bearing elsewhere: Sotetseg's maze form,
     * the room-empty deadline and instance-plane assertions all moved when it
     * was tried, six selftest failures' worth. A room being destroyed taking
     * its dying npcs with it is acceptable; the player is leaving it anyway.
     */

    if( npc->death_seq_sent &&
        (npc->death_seq_tick < 0 ||
         srv->tick <
             npc->death_seq_tick +
                 (npc->def ? npc->def : ToriRSServer_ContentNpcDefault())->death_delay) )
    {
        if( getenv("TORIRSSERVER_ANIM_LOST") )
            fprintf(stderr,
                    "torirsserver: npc_free HELD for the death animation: type=%d slot=%d "
                    "tick=%d (seq %d played at %d, needs %d tick(s))\n",
                    npc->type, slot, srv->tick, npc->death_seq, npc->death_seq_tick,
                    (npc->def ? npc->def : ToriRSServer_ContentNpcDefault())->death_delay);
        npc->free_wanted = 1;
        return;
    }

    if( (npc->masks & TORIRSSERVER_NMASK_ANIM) && !npc->free_deferred_for_anim )
    {
        npc->free_deferred_for_anim = 1;
        npc->free_wanted = 1;
        if( getenv("TORIRSSERVER_ANIM_LOST") )
            fprintf(stderr,
                    "torirsserver: npc_free HELD one phase: type=%d slot=%d tick=%d has "
                    "seq %d queued and would have been removed before it shipped\n",
                    npc->type, slot, srv->tick, npc->anim_id);
        return;
    }

    if( npc->hitpoints == 0 && npc->death_seq >= 0 && !npc->death_seq_sent )
    {
        srv->silent_death_removals++;
        fprintf(stderr,
                "torirsserver: SILENT DEATH: npc type %d (slot %d) removed at 0 hp "
                "without its death animation (seq %d) ever being sent — the player "
                "saw it vanish\n",
                npc->type, slot, npc->death_seq);
    }

    /*
     * LIFT THE COLLISION STAMP, and do it here rather than at each caller.
     *
     * `active = 0` retires the npc from every list that walks the pool, but the
     * footprint it wrote into the scene with `npc_set_occupancy` is not part of
     * the pool — it is a bitmask on the collision map, and nothing else ever
     * clears it. So every npc that has ever died leaves a permanent, invisible
     * NPC_OCC block on the tiles it stopped on, for the rest of the run.
     *
     * Two selftest stanzas already knew ("do not measure this npc's ghost") and
     * cleared it by hand before setting `active = 0`; the real game had nobody
     * doing that. It is worst in an arena that kills things where the fight
     * happens: Verzik's room collapses six pillars during phase one and then
     * spawns exploding nylocas into the same floor, and the crabs walked into
     * ghosts they could not see and stalled — `npc_take_step` refused west,
     * north and south with the map reading perfectly clear, because
     * `map_blocked` answers on the map's own bits and occupancy is not one of
     * them.
     *
     * Idempotent: `collision_map_remove` is `&= ~mask`, so the handful of
     * callers that already cleared before calling in are unaffected.
     *
     * Below the deferred-free returns above on purpose — those come back to
     * this function later, and an npc still being drawn through its death
     * animation should still occupy its tile.
     */
    npc_set_occupancy(npc, 0);

    npc->active = 0;
    npc->pending_free = 1;

    if( srv->npc_free_queue_count < TORIRSSERVER_NPC_MAX )
    {
        struct ToriRSServerNpcFreeCmd* cmd = &srv->npc_free_queue[srv->npc_free_queue_count++];
        cmd->slot = slot;
        cmd->generation = npc->generation;
    }
    /* No overflow branch: the queue is sized to TORIRSSERVER_NPC_MAX and at most
     * one command per currently-active npc can ever be pending between
     * reaps, so `npc_free_queue_count` can never reach it. */
}

/*
 * Once per tick, from phase_cleanup, after every player's NPC_INFO for this
 * tick has already gone out (see ToriRSServer_WorldTick's phase order). This is
 * the only place a slot becomes eligible for `npc_spawn`'s scan again.
 */
void
ToriRSServer_WorldNpcReap(struct ToriRSServer* srv)
{
    assert(srv);
    for( int i = 0; i < srv->npc_free_queue_count; i++ )
    {
        struct ToriRSServerNpcFreeCmd* cmd = &srv->npc_free_queue[i];
        struct ToriRSServerNpc* npc = &srv->npcs[cmd->slot];

        /* Generation guard: defensive only (see the field's comment) — a
         * queued slot cannot legitimately be reused before this runs, since
         * `pending_free` is what blocks npc_spawn's scan from touching it. */
        if( npc->generation == cmd->generation )
            npc->pending_free = 0;
    }
    srv->npc_free_queue_count = 0;
}

/*
 * `npc_add`'s entry point. Same spawn, but content gets the slot back so the
 * script can act on the npc it just created — the reference leaves it active,
 * and without the slot there would be no way to say which of six identical
 * goblins was meant.
 */
int
ToriRSServer_WorldNpcSpawn(
    struct ToriRSServer* srv,
    int type,
    int x,
    int z,
    int level)
{
    return npc_spawn(srv, type, x, z, level);
}

struct ToriRSServerPlayer*
ToriRSServer_WorldNpcOwner(
    struct ToriRSServer* srv,
    const struct ToriRSServerNpc* npc)
{
    struct ToriRSServerPlayer* player;

    if( !srv || !npc || npc->owner_gen == 0 || npc->owner_pid < 0 ||
        npc->owner_pid >= TORIRSSERVER_PLAYER_MAX )
        return NULL;
    player = &srv->players[npc->owner_pid];
    if( !player->active || player->login_generation != npc->owner_gen )
        return NULL;
    return player;
}

int
ToriRSServer_WorldNpcVisibleTo(
    struct ToriRSServer* srv,
    const struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player)
{
    if( !npc )
        return 0;
    if( npc->owner_gen == 0 )
        return 1;
    if( !srv || !player || !player->active )
        return 0;
    return ToriRSServer_WorldNpcOwner(srv, npc) == player;
}

void
ToriRSServer_WorldNpcSetFollower(
    struct ToriRSServerPlayer* player,
    const struct ToriRSServerNpc* npc,
    int slot)
{
    assert(player);
    if( !npc || slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
    {
        player->follower_slot = -1;
        player->follower_gen = 0;
        return;
    }
    player->follower_slot = slot;
    player->follower_gen = npc->generation;
}

struct ToriRSServerNpc*
ToriRSServer_WorldNpcFollower(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int* out_slot)
{
    struct ToriRSServerNpc* npc;

    if( out_slot )
        *out_slot = -1;
    if( !srv || !player || player->follower_slot < 0 ||
        player->follower_slot >= TORIRSSERVER_NPC_MAX || player->follower_gen == 0 )
        return NULL;
    npc = &srv->npcs[player->follower_slot];
    /* The generation is the whole point: a dead follower whose slot has been
     * reused must resolve to nothing, not to its replacement. */
    if( !npc->active || npc->generation != player->follower_gen )
        return NULL;
    if( out_slot )
        *out_slot = player->follower_slot;
    return npc;
}

void
ToriRSServer_WorldNpcSetOwner(
    struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player)
{
    assert(npc);
    if( !player || !player->active )
    {
        npc->owner_pid = 0;
        npc->owner_gen = 0;
        return;
    }
    npc->owner_pid = player->pid;
    npc->owner_gen = player->login_generation;
}

/*
 * PathingEntity.takeStep against the current waypoint. Returns 1 when the npc
 * moved. Never uses ToriRSServer_SceneRoute — occupancy gates every attempt.
 */
static int
npc_take_step(struct ToriRSServerNpc* npc)
{
    int wp_x;
    int wp_z;
    int dx;
    int dz;
    int try_dx;
    int try_dz;
    int prev_x;
    int prev_z;
    int extra;
    int size;
    int coll_type;
    int moved = 0;

    assert(npc);
    if( npc->waypoint_index < 0 )
        return 0;
    /*
     * Frozen: the route is kept, the step is not taken. Keeping the waypoints
     * is deliberate — an npc that was walking somewhere when the freeze landed
     * resumes to the same place when it melts, which is what the live game
     * does and what dropping the route would silently change.
     */
    if( npc->frozen_ticks > 0 )
        return 0;

    wp_x = npc->waypoints[npc->waypoint_index].x;
    wp_z = npc->waypoints[npc->waypoint_index].z;
    if( wp_x == npc->x && wp_z == npc->z )
    {
        npc->waypoint_index--;
        if( npc->waypoint_index < 0 )
            return 0;
        wp_x = npc->waypoints[npc->waypoint_index].x;
        wp_z = npc->waypoints[npc->waypoint_index].z;
    }

    dx = wp_x > npc->x ? 1 : (wp_x < npc->x ? -1 : 0);
    dz = wp_z > npc->z ? 1 : (wp_z < npc->z ? -1 : 0);
    if( dx == 0 && dz == 0 )
        return 0;

    prev_x = npc->x;
    prev_z = npc->z;
    extra = npc_travel_extra(npc);
    size = npc->size > 0 ? npc->size : 1;
    /* The npc's own move restriction, not everyone's: a swimmer walks where the
     * map blocks, an indoors npc will not leave the roof. */
    coll_type = npc_collision_type(npc);
    try_dx = 0;
    try_dz = 0;

    /*
     * Diagonal first, at every size.
     *
     * This used to be gated on `size == 1`, which is not what the reference
     * does: `PathingEntity.takeStep` tries the diagonal, then the X leg, then
     * the Z leg, and leaves the footprint entirely to `canTravel`. Ours already
     * had the footprint half — `collision_map_can_travel_typed` carries the
     * size-2 three-tile diagonal check and a size>=3 corners-plus-mid-edge loop,
     * both written against StepValidator — so the gate was refusing a step the
     * collision map was ready to answer.
     *
     * What it cost: every npc bigger than 1x1 walked in an L. A Nylocas
     * Matomenos is size 2, and a crab that should cut the corner of the Maiden's
     * arena on the diagonal instead walked six tiles west and then six tiles
     * south — the same total distance, a visibly wrong path, and one that runs
     * along the rows the other crabs are queued on instead of away from them.
     * "The nylocas do not path correctly towards Maiden" is this line.
     */
    if( dx != 0 && dz != 0 &&
        ToriRSServer_SceneCanTravelTyped(npc->level, npc->x, npc->z, dx, dz, size, extra,
                                       coll_type) )
    {
        try_dx = dx;
        try_dz = dz;
    }
    else if( dx != 0 && ToriRSServer_SceneCanTravelTyped(npc->level, npc->x, npc->z, dx, 0, size,
                                                       extra, coll_type) )
    {
        try_dx = dx;
        try_dz = 0;
    }
    else if( dz != 0 && ToriRSServer_SceneCanTravelTyped(npc->level, npc->x, npc->z, 0, dz, size,
                                                       extra, coll_type) )
    {
        try_dx = 0;
        try_dz = dz;
    }
    /* else stall — keep waypoint */

    npc->last_step_x = prev_x;
    npc->last_step_z = prev_z;

    if( try_dx != 0 || try_dz != 0 )
    {
        int dir = ToriRSServer_StepDirection(try_dx, try_dz);

        npc_set_occupancy(npc, 0);
        npc->x += try_dx;
        npc->z += try_dz;
        npc_set_occupancy(npc, 1);
        /*
         * First step of the tick fills `step_dir`, a second fills `run_dir`.
         *
         * `step_dir` is cleared in phase 11, after every observer's NPC_INFO
         * has been written, so "already set" means "already stepped this tick"
         * and needs no separate counter — the same thing `walkDir !== -1`
         * means in `PathingEntity.processMovement`.
         */
        if( npc->step_dir < 0 )
            npc->step_dir = dir;
        else
            npc->run_dir = dir;
        /* A step is also a turn, and the facing outlives the step. */
        if( dir >= 0 )
            npc->face_dir = dir;
        moved = 1;
        if( npc->x == wp_x && npc->z == wp_z )
            npc->waypoint_index--;
    }
    return moved;
}

/*
 * One step of an npc's walk toward a tile via the naive pathfinder.
 *
 * NPCs never flood — collision_map_naive_path (through the scene helper) queues
 * a single waypoint, then takeStep advances one tile. When footprints already
 * overlap, naive_path itself picks a random cardinal (randomWalk).
 *
 * Returns 1 when the npc moved this tick.
 */
int
ToriRSServer_WorldNpcWalkTo(
    struct ToriRSServerNpc* npc,
    int target_x,
    int target_z)
{
    const struct ToriRSServerNpcDef* def;
    int out_x;
    int out_z;
    int size;
    unsigned* rng = NULL;
    unsigned local_rng;

    assert(npc);
    def = npc->def ? npc->def : ToriRSServer_ContentNpcDefault();
    if( def->nomove || def->moverestrict == 5 )
        return 0;

    size = npc->size > 0 ? npc->size : 1;
    /*
     * Standing on the destination is only "nowhere to go" for a 1x1 npc.
     *
     * `npc->x/npc->z` is the south-west anchor, so for anything bigger this
     * compares one corner of the footprint against the target and answers for
     * the whole square. A player walks through npcs (`player_travel_extra`
     * reads no NPC_OCC), so ending up on a cow's anchor tile is ordinary — and
     * this then refused to move the cow off, forever. The cow overlapped the
     * player, an overlap is not attack range, and the fight it had been dragged
     * into could not start: it stood on the player's head until one of them
     * walked away.
     *
     * `collision_map_naive_path` already has the answer for the general case —
     * intersecting footprints pick a random cardinal, `randomWalk` in the
     * reference — so all this has to do is stop short-circuiting past it.
     */
    if( size == 1 && npc->x == target_x && npc->z == target_z )
        return 0;

    /* Prefer the world's RNG when we can reach it through a live player. */
    if( npc->def )
    {
        /* walk_to has no srv; seed from tick-ish last_step as a stable stand-in
         * when the caller did not pass a server. Occupancy randomWalk still
         * advances whatever we give it. */
    }
    local_rng = (unsigned)(npc->x * 73856093u ^ (unsigned)npc->z * 19349663u ^
                           (unsigned)npc->stuck_counter);
    rng = &local_rng;

    if( !ToriRSServer_SceneNaivePath(
            npc->level, npc->x, npc->z, target_x, target_z, size, size, 1, 1,
            npc_travel_extra(npc), rng, &out_x, &out_z) )
    {
        npc_clear_waypoints(npc);
        return 0;
    }

    /*
     * LostCity's findNaivePath can return the source tile when already aligned
     * cardinally with a 1x1 dest (the perimeter walk lands on src, then the
     * `currX !== destX && currZ !== destZ` loop never runs). They discard that
     * result rather than queueing it; queuing it here would make take_step clear
     * the waypoint and look permanently stuck. Patrol does not use this path —
     * it queues the absolute waypoint and takeSteps (Npc.patrolMode).
     */
    if( out_x == npc->x && out_z == npc->z )
    {
        npc->stuck_counter++;
        return 0;
    }

    npc_queue_waypoint(npc, out_x, out_z);
    if( npc_take_step(npc) )
    {
        npc->stuck_counter = 0;
        return 1;
    }
    npc->stuck_counter++;
    return 0;
}

/*
 * One step away from a tile — `playerescape`. Queues a DIAGONAL away waypoint
 * and takeSteps; stuck_counter is the caller's.
 *
 * The quadrant comes from `>=` comparisons, never from a sign, and that is the
 * whole of the port's correctness here. `Npc.playerEscapeMode`
 * (LostCity engine/src/engine/entity/Npc.ts:788) picks one of SOUTH_WEST /
 * NORTH_WEST / SOUTH_EAST / NORTH_EAST by asking `target.x >= this.x` and
 * `target.z >= this.z`, so a target on the same row or column still yields a
 * diagonal — there is no cardinal escape direction in the reference at all.
 *
 * This used to take `-sign_of(delta)` per axis, which is zero when the two
 * share a row. That made the waypoint a pure cardinal, and a cardinal has
 * exactly one candidate: `npc_take_step` tries the diagonal, then the X leg,
 * then the Z leg, so a zeroed axis deletes the third option. Hans stands due
 * west of the player on the same z, his one westward candidate is blocked, and
 * he stalls forever — with a southward step available and never considered.
 */
static int
npc_step_away(
    struct ToriRSServerNpc* npc,
    int target_x,
    int target_z)
{
    int dx;
    int dz;

    assert(npc);
    dx = target_x >= npc->x ? -1 : 1;
    dz = target_z >= npc->z ? -1 : 1;
    npc_queue_waypoint(npc, npc->x + dx, npc->z + dz);
    return npc_take_step(npc);
}

/** Chebyshev tiles between an npc and the player. */
int
npc_player_range(
    const struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player)
{
    int dx = npc->x - player->x;
    int dz = npc->z - player->z;

    if( dx < 0 )
        dx = -dx;
    if( dz < 0 )
        dz = -dz;
    return dx > dz ? dx : dz;
}

/*
 * Edge-to-edge Chebyshev, the reference's `CoordGrid.distanceTo` — the gap
 * between the two FOOTPRINTS, not between their south-west corners.
 *
 * `npc_player_range` above measures corners, which for the size-1 npcs that
 * dominate the roster is the same number. It is not the same number for
 * anything bigger: a 3x3 npc standing shoulder to shoulder with the player is
 * one tile away by this measure and three by the other. Modes that ask "is the
 * player *beside* me" have to use this one or every large npc answers no while
 * touching them.
 */
static int
npc_player_distance(
    const struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player)
{
    int size = npc->size > 0 ? npc->size : 1;
    int dx = 0;
    int dz = 0;

    if( npc->x > player->x )
        dx = npc->x - player->x;
    else if( player->x > npc->x + size - 1 )
        dx = player->x - (npc->x + size - 1);
    if( npc->z > player->z )
        dz = npc->z - player->z;
    else if( player->z > npc->z + size - 1 )
        dz = player->z - (npc->z + size - 1);
    return dx > dz ? dx : dz;
}

/*
 * One step toward the player, for every player-facing npc mode including
 * `playerfollow`. Naive destination handles the perimeter, so the npc ends up
 * beside the player rather than on them.
 *
 * The tile is the player's CURRENT one, not their published `follow_x/follow_z`.
 * That is the reference: `Npc.playerFollowMode` -> `Npc.pathToTarget` ->
 * `PathingEntity.naivePathToTarget`, which calls `findNaivePath` with
 * `this.target.x, this.target.z`. `followX/followZ` is read in exactly one
 * place in the whole reference engine — `Player.pathToPathingTarget`, for a
 * player using the "Follow" op on another player (OPPLAYER3/APPLAYER3) — and
 * nowhere in the npc mode machine. The follow *dance*
 * (docs/OSRS_PATHING_LOS.md section 5) is a player-on-player mechanic; an npc
 * follower simply walks at whoever it is following.
 *
 * This used to path to `player->follow_x/follow_z`, and that was stale twice
 * over. `follow_x` is published at the top of `advance_player`, so it is
 * already the tile before the player's *last* step; the npc phase runs before
 * the player phase, so by the time an npc reads it the player has taken
 * another step on top of that. A familiar aimed two tiles behind its owner
 * never caught up while the owner was moving — it held a four-tile gap over an
 * eight-tile walk and only closed once they stopped.
 */
static int
npc_walk_to_player(
    struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player)
{
    assert(npc && player);
    return ToriRSServer_WorldNpcWalkTo(npc, player->x, player->z);
}

int
ToriRSServer_WorldNpcWalkToApproach(
    struct ToriRSServerNpc* npc,
    int target_x,
    int target_z,
    struct CollisionApproach const* approach)
{
    const struct ToriRSServerNpcDef* def;

    assert(npc);
    assert(approach);
    def = npc->def ? npc->def : ToriRSServer_ContentNpcDefault();
    if( ToriRSServer_SceneReached(npc->level, npc->x, npc->z, target_x, target_z, approach) )
        return 0;
    if( def->nomove || def->moverestrict == 5 )
        return 0;

    /* Naive destination already walks the perimeter of a larger footprint when
     * dest width/height are passed; approach rects are 1x1 here for NPCs. */
    return ToriRSServer_WorldNpcWalkTo(npc, target_x, target_z);
}

/*
 * The player a standing mode is being held against, or NULL.
 *
 * Three answers in priority order, and the order is the point:
 *
 *  1. An owned npc's owner. A familiar's modes are about its owner and nobody
 *     else, and a stale owner fails closed rather than transferring.
 *  2. The player `npc_setmode` named — the reference's `PathingEntity.target`.
 *  3. `srv->active_player`, for a mode that arrived without a target: combat
 *     sets `applayer2` directly, and the world spawner sets `playerface` on a
 *     familiar before there is a script to name anybody.
 *
 * (3) was until now the *only* answer, which is how "the npc I am talking to
 * wanders off" survived: phase 4 belongs to no player, so `active_player` is
 * whoever happened to be served last, and every range test in the mode machine
 * was measured against them.
 */
static struct ToriRSServerPlayer*
npc_mode_player(
    struct ToriRSServer* srv,
    const struct ToriRSServerNpc* npc)
{
    if( npc->owner_gen != 0 )
        return ToriRSServer_WorldNpcOwner(srv, npc);
    if( npc->mode_target_gen != 0 )
    {
        struct ToriRSServerPlayer* player;

        if( npc->mode_target_pid < 0 || npc->mode_target_pid >= TORIRSSERVER_PLAYER_MAX )
            return NULL;
        player = &srv->players[npc->mode_target_pid];
        if( !player->active || player->login_generation != npc->mode_target_gen )
            return NULL;
        return player;
    }
    return srv->active_player;
}

/*
 * `Npc.validateTarget()` — is the player this standing mode is about still a
 * player this standing mode can be about?
 *
 * The reference runs it before every targeted mode and calls `resetDefaults()`
 * when it fails, which is the whole of "except under certain conditions": a
 * held npc goes back to wandering when the player changes floor, when they
 * walk past the record's `maxrange` leash from where the npc spawned, or when
 * they stop existing. Nothing else releases it — a conversation does not end
 * the mode, walking away does.
 *
 * Scoped to the standing modes (`playerescape` .. `playerfaceclose`). The
 * `opplayer<n>`/`applayer<n>` errand modes clear themselves the tick they fire
 * and the pursuit that uses them is already leashed by
 * `torirs_server_combat.c:target_within_maxrange`; putting a second, subtly different
 * leash in front of them would be two rules for one thing.
 */
static int
npc_mode_target_valid(
    const struct ToriRSServerNpc* npc,
    const struct ToriRSServerPlayer* player)
{
    const struct ToriRSServerNpcDef* def;
    int maxrange;
    int dx;
    int dz;
    int from_spawn;

    assert(npc && player);
    if( npc->mode < TORIRSSERVER_NPCMODE_PLAYERESCAPE || npc->mode > TORIRSSERVER_NPCMODE_PLAYERFACECLOSE )
        return 1;
    if( npc->level != player->level )
        return 0;
    /* `targetWithinMaxRange` returns true for `playerfollow` before it looks at
     * anything: a familiar has to be able to follow its owner off the map
     * square it was summoned on. */
    if( npc->mode == TORIRSSERVER_NPCMODE_PLAYERFOLLOW )
        return 1;

    def = npc->def ? npc->def : ToriRSServer_ContentNpcDefault();
    maxrange = def->maxrange;
    dx = player->x - npc->spawn_x;
    dz = player->z - npc->spawn_z;
    if( dx < 0 )
        dx = -dx;
    if( dz < 0 )
        dz = -dz;
    from_spawn = dx > dz ? dx : dz;

    /* Escape has its own clause in the reference and it is not the general one:
     * an npc fleeing is *supposed* to end up far from its spawn, so the mode
     * survives until BOTH it and the player it is fleeing are past the leash.
     * Using the general test here would cancel the flight the moment it worked. */
    if( npc->mode == TORIRSSERVER_NPCMODE_PLAYERESCAPE )
    {
        int npc_dx = npc->x - npc->spawn_x;
        int npc_dz = npc->z - npc->spawn_z;
        int npc_from_spawn;

        if( npc_dx < 0 )
            npc_dx = -npc_dx;
        if( npc_dz < 0 )
            npc_dz = -npc_dz;
        npc_from_spawn = npc_dx > npc_dz ? npc_dx : npc_dz;
        return !(from_spawn > maxrange && npc_from_spawn > maxrange);
    }
    return from_spawn <= maxrange + 1;
}

/*
 * `npc_setmode`, once per tick per npc.
 *
 * 253 of the LostCity tree's `npc_setmode` calls name eleven modes and 162 of
 * them are `none`/`null` — "stop what you were doing" — so the machine is much
 * smaller than the numbering suggests. What it covers, and what it does not,
 * is stated rather than silently approximated: an unhandled mode warns once and
 * falls back to `none`, because an npc quietly standing still is the failure
 * that reads as "the script did not run".
 *
 * Returns 1 when the mode took the npc's movement for this tick, so the roam
 * below leaves it alone.
 */
int
npc_run_mode(
    struct ToriRSServer* srv,
    struct ToriRSServerNpc* npc,
    int slot)
{
    struct ToriRSServerPlayer* player;
    int range;

    /* The three targetless modes first, in the reference's own order, and
     * before any player is resolved — `processMovementInteraction` dispatches
     * none/wander/patrol without ever looking at a target. Requiring a player
     * up here is why patrol used to stop dead in a world nobody is logged into,
     * and why an npc holding a stale standing mode fell through to the roam
     * instead of being reset by the gate below. */
    if( npc->mode == TORIRSSERVER_NPCMODE_NONE || npc->mode == TORIRSSERVER_NPCMODE_NULL )
        return 0;
    if( npc->mode == TORIRSSERVER_NPCMODE_WANDER )
        return 0; /* The roam below is what wander means. */

    if( npc->mode == TORIRSSERVER_NPCMODE_PATROL )
    {
        player = NULL;
        range = 0;
    }
    else
    {
        player = npc_mode_player(srv, npc);
        /* No target, or one that no longer qualifies: back to the default mode,
         * and no movement this tick — `resetDefaults(); return;` is where the
         * reference's targeted dispatch ends. */
        if( !player || !npc_mode_target_valid(npc, player) )
        {
            ToriRSServer_NpcResetDefaults(npc);
            return 0;
        }
        range = npc_player_range(npc, player);
    }

    /* Every player-facing mode faces the player; only some of them move.
     *
     * By pid — `player` is right here and used for the range and the steps, so
     * naming it in the face id too costs nothing and makes the id mean the same
     * person on every observer's stream.
     *
     * Which player it is is now `npc_mode_player` — the owner, else the player
     * `npc_setmode` named, else the phase's leftover `active_player`. Only the
     * last of those three is a guess, and it is only reached by a mode nobody
     * named a target for. */
    if( player && npc->mode >= TORIRSSERVER_NPCMODE_PLAYERESCAPE )
        ToriRSServer_NpcFacePlayer(npc, player->pid);

    /*
     * `npc->follow_x/follow_z` is this npc's OWN published previous tile — the
     * reference's `PathingEntity.followX`, what anything following *it* would
     * aim at. The npc phase publishes it once per turn off `npc->last_step_*`,
     * and so do spawn and respawn.
     *
     * A block here used to overwrite it with the *player's* previous tile on
     * every turn an npc spent in a player-facing mode, which is a different
     * entity's snapshot in this npc's field. Nothing reads it yet, so it was
     * silent — but it is exactly the value a follower-of-a-follower would path
     * to, and it disagreed with the three other sites that set it.
     */

    switch( npc->mode )
    {
    case TORIRSSERVER_NPCMODE_PLAYERESCAPE:
        if( range < 8 )
        {
            if( !npc_step_away(npc, player->x, player->z) )
                npc->stuck_counter++;
            else
                npc->stuck_counter = 0;
        }
        if( npc->stuck_counter >= 5 )
        {
            ToriRSServer_NpcResetDefaults(npc);
            npc->stuck_counter = 0;
        }
        return 1;

    case TORIRSSERVER_NPCMODE_PLAYERFOLLOW:
        /*
         * Up to TWO tiles a tick, which is what makes this mode different from
         * every other mover here.
         *
         * A follower that walks cannot follow an owner who runs: the owner
         * covers two tiles a tick and the follower one, so the gap grows by a
         * tile every tick for as long as they keep going. Measured over an
         * eight-tile run the familiar was five tiles back and still losing
         * ground, and nothing in `playerfollow` leashes it, so a run across
         * Lumbridge left it wherever it ran out of ticks. That is the shape of
         * "my familiar does not follow me".
         *
         * Re-pathing rather than draining a queued route is what naive
         * following is — there is no route, `ToriRSServer_WorldNpcWalkTo` picks
         * the next tile fresh each time — so the second tile is a second call,
         * and `npc_take_step` files its direction in `run_dir` for NPC_INFO's
         * two-step op.
         *
         * The second step is gated on still being more than a tile away, so a
         * follower already at the owner's shoulder walks. That is the observed
         * behaviour: a familiar keeping station strolls, and one that got left
         * behind sprints until it catches up.
         *
         * Combat pursuit (`torirs_server_combat.c`) deliberately does NOT do this.
         * Outrunning a monster is a mechanic; outrunning your own familiar is
         * not.
         */
        if( range > 1 )
        {
            npc_walk_to_player(npc, player);
            if( npc_player_range(npc, player) > 1 )
                npc_walk_to_player(npc, player);
        }
        return 1;

    case TORIRSSERVER_NPCMODE_PLAYERFACECLOSE:
        /*
         * Face, and HOLD STILL. This is the mode `~chatnpc` puts a speaking npc
         * into, and holding still is its entire job: a wanderer you start a
         * conversation with must not stroll off to the next tile of its radius
         * while the player is reading, and the npc you asked to bank must not
         * drift out of the bank booth mid-transaction.
         *
         * `Npc.playerFaceCloseMode` calls no mover at all. This walked toward
         * the player instead, which is a *different* mode — `playerfollow` —
         * and had the npc shouldering onto the player's tile whenever the
         * conversation started at range.
         *
         * More than a tile away and the mode is over: `resetDefaults()`, back
         * to wandering or patrolling. That is how you leave — walking away from
         * an npc releases it, and nothing else does. The distance is
         * footprint-to-footprint, so a 3x3 npc counts as beside the player when
         * it is touching them rather than three tiles out and instantly free.
         */
        if( npc_player_distance(npc, player) > 1 )
            ToriRSServer_NpcResetDefaults(npc);
        return 1;

    case TORIRSSERVER_NPCMODE_PLAYERFACE:
        /*
         * Face only, and no distance clause — the difference from
         * `playerfaceclose`, and the reason `~chatnpcrange` uses it: a
         * conversation held across a fence or a counter would end on its first
         * line under the one-tile rule.
         *
         * What ends it is `npc_mode_target_valid` above — the record's
         * `maxrange` leash measured from the npc's spawn tile, a floor change,
         * or the player going away.
         */
        return 1;

    default:
        break;
    }

    /*
     * `opplayer<n>` walks the npc to the player and runs `[ai_opplayer<n>]` on
     * arrival; `applayer<n>` runs `[ai_applayer<n>]` as soon as it is in range
     * without closing. Both then drop back to `none` — the mode describes an
     * errand, not a standing state, and an npc left in it would re-fire its
     * trigger every tick.
     */
    if( npc->mode >= TORIRSSERVER_NPCMODE_OPPLAYER1 && npc->mode <= TORIRSSERVER_NPCMODE_OPPLAYER5 )
    {
        int op = npc->mode - TORIRSSERVER_NPCMODE_OPPLAYER1;

        if( range > 1 )
        {
            npc_walk_to_player(npc, player);
            return 1;
        }
        /* Cleared *before* the trigger runs, so a script that sets a new mode
         * keeps it — the same ordering the player's interaction resolver uses
         * and for the same reason. Continuous combat does not use this path:
         * combat_npc_tick fires AI_OPPLAYER2 on the attack clock instead. */
        npc->mode = TORIRSSERVER_NPCMODE_NONE;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_OPPLAYER1 + op, npc->type, -1, slot);
        return 1;
    }
    if( npc->mode >= TORIRSSERVER_NPCMODE_APPLAYER1 && npc->mode <= TORIRSSERVER_NPCMODE_APPLAYER5 )
    {
        int op = npc->mode - TORIRSSERVER_NPCMODE_APPLAYER1;
        int size = npc->size > 0 ? npc->size : 1;
        /* The npc's own `param=attackrange` widens (or narrows) the AP reach.
         * The default is the reference's flat 10 (`PathingEntity.apRange`),
         * which is wrong in both directions for anything that states a reach:
         * Jal-MejRah's four tiles fired from ten, and the Inferno's rangers —
         * whose reach the wiki puts past the player's own maximum of ten —
         * walked to ten before firing. `> 1` and not `> 0` because 1 is the
         * unauthored default and means melee, which AP modes are not for. */
        int ap_range = TORIRSSERVER_AP_RANGE_DEFAULT;

        if( npc->def && npc->def->attackrange > 1 )
            ap_range = npc->def->attackrange;
        if( range > ap_range )
        {
            npc_walk_to_player(npc, player);
            return 1;
        }
        /* AP also requires approached LoS — cast backwards (player → npc). */
        if( !ToriRSServer_SceneApproached(
                npc->level, player->x, player->z, npc->x, npc->z, 1, 1, size, size) )
        {
            npc_walk_to_player(npc, player);
            return 1;
        }
        npc->mode = TORIRSSERVER_NPCMODE_NONE; /* see above */
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_APPLAYER1 + op, npc->type, -1, slot);
        return 1;
    }

    /*
     * Patrol — LostCity Npc.patrolMode: queue the absolute waypoint and
     * takeStep toward it. Do not re-run naive_path each tick; that finder can
     * return the source tile on a pure cardinal approach, which left Hans one
     * tile short of every waypoint until the stuck-teleport fired.
     */
    if( npc->mode == TORIRSSERVER_NPCMODE_PATROL )
    {
        const struct ToriRSServerNpcDef* def = npc->def;
        int dest_x;
        int dest_z;

        if( !def || def->patrol_count <= 0 )
        {
            npc->mode = TORIRSSERVER_NPCMODE_NONE;
            return 0;
        }
        if( npc->patrol_index < 0 || npc->patrol_index >= def->patrol_count )
            npc->patrol_index = 0;

        dest_x = def->patrol[npc->patrol_index].x;
        dest_z = def->patrol[npc->patrol_index].z;

        if( npc->patrol_pause > 0 )
        {
            npc->patrol_pause--;
            return 1;
        }
        if( npc->x == dest_x && npc->z == dest_z )
        {
            npc->patrol_pause = def->patrol[npc->patrol_index].pause;
            npc->patrol_index = (npc->patrol_index + 1) % def->patrol_count;
            npc->stuck_counter = 0;
            dest_x = def->patrol[npc->patrol_index].x;
            dest_z = def->patrol[npc->patrol_index].z;
            if( npc->patrol_pause > 0 )
                return 1;
            /* pause was 0 — queue the next leg this tick, like LostCity. */
        }
        if( npc->waypoint_index < 0 ||
            npc->waypoints[npc->waypoint_index].x != dest_x ||
            npc->waypoints[npc->waypoint_index].z != dest_z )
            npc_queue_waypoint(npc, dest_x, dest_z);

        if( npc_take_step(npc) )
            npc->stuck_counter = 0;
        else
            npc->stuck_counter++;

        if( npc->stuck_counter >= 32 || npc->level != def->patrol[npc->patrol_index].level )
        {
            ToriRSServer_WorldNpcTeleport(
                npc, dest_x, dest_z, def->patrol[npc->patrol_index].level);
            npc->stuck_counter = 0;
        }
        return 1;
    }

    /* Anything else — the loc/obj/npc-targeted modes, which carry a
     * target this mode field has nowhere to put. Reported once per npc type so
     * a tree using one is not silently ignored. */
    {
        static unsigned char warned[1 << TORIRSSERVER_NPC_TYPE_BITS];

        if( npc->type >= 0 && npc->type < (int)(sizeof(warned) / sizeof(warned[0])) &&
            !warned[npc->type] )
        {
            warned[npc->type] = 1;
            fprintf(stderr, "torirsserver: npc mode %d is not implemented (npc %d); using none\n",
                    npc->mode, npc->type);
        }
    }
    npc->mode = TORIRSSERVER_NPCMODE_NONE;
    return 0;
}

/* One tile of idle roaming, on the xrsps/RSMod timer: an idle npc re-rolls a
 * roam every 15-30 ticks and stays inside its wander radius. */
void
advance_npcs(struct ToriRSServer* srv)
{
    for( int slot = 0; slot < srv->npc_slot_max; slot++ )
    {
        struct ToriRSServerNpc* npc = &srv->npcs[slot];

        /*
         * An `npc_add` with a duration expires here, before anything else in
         * the phase looks at it. `ToriRSServer_WorldNpcFree` is the ordinary
         * NPC_INFO remove path — the same one a death uses — so the client
         * is told the way it is told about everything else, and the slot
         * cannot be reused before that telling has happened (see
         * docs/torirs_server_npc_slot_reap.md).
         */
        if( npc->active && npc->despawn_tick >= 0 && srv->tick >= npc->despawn_tick )
        {
            npc_set_occupancy(npc, 0);
            ToriRSServer_WorldNpcFree(srv, slot);
            continue;
        }

        /* Entity poison is a global 30-tick timer, not an AI queue. Run it
         * before npc_delay's turn gate: a delayed/frozen NPC still receives
         * poison in the reference, while death clears it in the helper. */
        ToriRSServer_CombatNpcPoisonTick(srv, slot);
        if( !npc->active || npc->death_tick >= 0 )
            continue;

        /*
         * A timed `npc_changetype` expiring: back to `spawn_type`.
         *
         * ABOVE the `npc_delay` gate, unlike the timers and queues below it,
         * because this is not the npc taking a turn — it is a clock on what the
         * npc *is*, and it has to keep running while the npc is busy. The gate
         * skips every tick an npc spends delayed, and the npcs that transform
         * are the ones that then fight: a rock crab swings on `npc_delay`, so
         * behind the gate its 1000-tick reversion would only count the ticks
         * nobody was hitting it. That is the same starvation the queue drain's
         * comment below describes, and it is worse here — the reversion is what
         * ENDS the transformation, so it would never arrive at all for anything
         * kept permanently busy.
         *
         * `^max_32bit_int` is content's "never", and a countdown holds it: it
         * decrements toward a value it cannot reach in any session, with no
         * `tick + duration` to overflow. The loc revert table learned that one
         * the hard way (docs and `loc_reverts`).
         */
        if( npc->changetype_delay > 0 && --npc->changetype_delay == 0 )
            ToriRSServer_NpcChangeType(npc, npc->spawn_type, 0);

        /* `npc_delay` makes the reference NPC invalid for the remainder of
         * its turn (`Npc.isValid()` returns false while delayed). Its parked
         * script was offered a resume by phase_npcs immediately before this
         * loop; if the deadline is still in the future, timers, queues, hunts,
         * and modes must all wait. Letting a one-tick AI timer run here made
         * Inferno adds attack on every server tick during their four-tick
         * attack delay, eventually filling the glyph and player queues. */
        if( npc->active && srv->tick < npc->delayed_until )
            continue;

        /*
         * `npc_settimer` and `npc_queue`, in phase 4's own order: **timers
         * before queues**, matching `Npc.processNpc`, which calls
         * `processTimers()` and then `processQueue()`. This ran them the other
         * way round, under a comment claiming it matched the reference.
         *
         * Both dispatch by npc *type*, so an npc that changed type between
         * queueing and firing runs the new type's script — which is what
         * `npc_changetype` is for and why the script id is not stored.
         */
        if( npc->active && npc->timer_interval > 0 )
        {
            if( ++npc->timer_clock >= npc->timer_interval )
            {
                npc->timer_clock = 0;
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_TIMER, npc->type, -1, slot);
            }
        }
        /*
         * The freeze melts a tick at a time, and it is decremented here rather
         * than in the movement phase because an npc with no route still has to
         * thaw. Above the queue drain on purpose: a freeze stops movement only,
         * so a frozen npc keeps running its queues and keeps fighting.
         */
        if( npc->active && npc->frozen_ticks > 0 )
            npc->frozen_ticks--;
        /*
         * The queue drain is gated on the npc not being delayed — the reference
         * only decrements while `!this.delayed` — and the comparison is against
         * the value *after* the decrement, so an npc's delay 0 and delay 1 both
         * fire on the next npc phase. A player's queue compares the value before
         * its decrement; the two conventions really do differ by one, which is
         * why it is written out here rather than shared.
         *
         * **The delay is the one this turn started with, not the one the timer
         * above just set.** `Npc.processNpc` asks once, at the top of the turn,
         * and then runs `processTimers()` and `processQueue()` unconditionally;
         * re-reading `delayed_until` here asks a second time, after an
         * `[ai_timer]` that ended in `npc_delay` has already moved it.
         *
         * That is not a rounding difference, it is starvation: every Inferno
         * monster attacks on `npc_delay(4)`, so the tick its delay expires is
         * also the tick its timer re-arms a fresh one — and the queue was never
         * drained on any other tick, because the top gate skipped those. The
         * player's melee swing ends in `npc_queue(2, $damage, 0)`
         * (combat_stats.rs2), so the queue is where *every* hit on the adds
         * lives: the entries piled up, none ever fired, and Jal-Xil took no
         * damage, showed no hitsplat and never flinched however long you hit it.
         */
        if( npc->active )
        {
            for( int i = 0; i < TORIRSSERVER_NPC_QUEUE_MAX; i++ )
            {
                if( !npc->queue[i].active )
                    continue;
                npc->queue[i].delay--;
                if( npc->queue[i].delay > 0 )
                    continue;
                npc->queue[i].active = 0;
                /* The queued value IS the script's `last_int` — `npc_queue(2,
                 * $damage, $delay)` is how one npc damages another, and
                 * `[ai_queue2]` reads it back with `last_int`. Dispatching
                 * without it dropped the argument, so every npc-to-npc hit in
                 * the tree landed for zero. Reference: `Npc.ts` sets
                 * `state.lastInt = request.lastInt` on the queued script. */
                ToriRSServer_ScriptsRunTriggerLastint(
                    srv, SS_TRIGGER_AI_QUEUE1 + (npc->queue[i].queue - 1), npc->type, -1, slot,
                    npc->queue[i].arg);
            }
        }
        if( !npc->active )
            continue;

        /* Publish own confrontation snapshot each turn. */
        npc->follow_x = npc->last_step_x;
        npc->follow_z = npc->last_step_z;

        /* TORIRSSERVER_FAMILIAR_DEBUG=1: one line per tick per owned npc. The
         * familiar's pursuit is a three-way handshake between a script timer,
         * a stored mode and a queued waypoint, and none of the three is
         * visible from the client — "my familiar just follows me" is the same
         * observation whether the mode never left playerfollow, the waypoint
         * never got queued, or the timer never ran. */
        if( npc->owner_gen != 0 && ToriRSServer_FamiliarDebug() )
        {
            fprintf(
                stderr,
                "familiar_dbg tick=%d slot=%d type=%d mode=%d wp=%d ct=%d "
                "ctn=%d at=%d,%d face=%d\n",
                srv->tick, slot, npc->type, npc->mode, npc->waypoint_index,
                npc->combat_target, npc->combat_target_npc, npc->x, npc->z,
                npc->face_entity);
        }

        /* Combat and death own the npc's movement. Roaming used to clear
         * step_dir here, which also wiped the step the combat mover had just
         * produced — phase 11 does that clear, once, at the right time.
         *
         * Either kind of combat target: a fight with another npc closes and
         * paces exactly like a fight with a player, so the mode machine and the
         * roam must keep out of it the same way. */
        if( npc->combat_target >= 0 || npc->combat_target_npc >= 0 ||
            npc->death_tick >= 0 )
            continue;
        if( npc_run_mode(srv, npc, slot) )
            continue;
        if( npc->mode != TORIRSSERVER_NPCMODE_WANDER )
        {
            /*
             * Reference `Npc.noMode()` is `this.updateMovement()`, and
             * `updateMovement` steps whenever `waypointIndex !== -1` — a
             * targetless npc still walks a waypoint a script queued with
             * `npc_walk`. Without this the opcode stores a destination nothing
             * ever reads, which is indistinguishable from the opcode being
             * missing: the Inferno's Ancestral Glyph stood in the lava and
             * TzKal-Zuk never became ready, because "ready" is the glyph
             * reaching the end of its row.
             */
            if( npc->waypoint_index >= 0 )
            {
                if( npc_take_step(npc) )
                {
                    npc->stuck_counter = 0;
                    /*
                     * And a SECOND tile, when content asked for one.
                     *
                     * `Npc.defaultMoveSpeed()` is WALK for every npc in the
                     * reference, so this branch stepped once and there was no
                     * way to say otherwise. The Pestilent Bloat needs to run
                     * between 40% and 60% health, which is not decoration:
                     * whether a team can stay behind a pillar is a function of
                     * how fast it comes round. `npc_setmovespeed` is the
                     * switch and this is the only mover that reads it.
                     *
                     * `npc_take_step` files the second direction in `run_dir`
                     * itself, which is what NPC_INFO's two-step op encodes, so
                     * the client draws a run rather than a stutter. The guard
                     * is the route still existing: an npc that reached its
                     * waypoint on the first tile stops there instead of
                     * overshooting it.
                     */
                    if( npc->move_speed > 0 && npc->waypoint_index >= 0 )
                        npc_take_step(npc);
                }
                else
                    npc->stuck_counter++;
            }
            continue;
        }
        if( npc->wander_radius <= 0 )
            continue;

        /*
         * Outside its radius, wandering means *going home* via naive pathing.
         */
        if( npc->x - npc->spawn_x > npc->wander_radius ||
            npc->spawn_x - npc->x > npc->wander_radius ||
            npc->z - npc->spawn_z > npc->wander_radius ||
            npc->spawn_z - npc->z > npc->wander_radius )
        {
            ToriRSServer_WorldNpcWalkTo(npc, npc->spawn_x, npc->spawn_z);
        }
        /*
         * The roam clock, and until now it was write-only.
         *
         * `next_roam_tick` is set in four places — a fresh spawn, a respawn,
         * and both OPNPC handlers ("idle roaming resumes only after the
         * response has had time to show") — and was read in none, so each of
         * those was a comment rather than a behaviour. The cost shows one test
         * down: a goblin that respawns and wanders off its spawn tile on the
         * same tick, so `ToriRSServer_WorldNpcRoamStagger`'s 5..30 ticks of
         * settling never happened.
         *
         * It gates only the choice of a NEW roam. Going home when outside the
         * radius stays ungated above, and a walk already in progress still
         * advances below — an earlier attempt that gated the whole block let an
         * npc drift 50 tiles from its spawn, because the radius clamp IS the
         * go-home branch.
         */
        else if( srv->tick >= npc->next_roam_tick && next_random(srv) % 8u == 0u )
        {
            int dest_x =
                npc->spawn_x + random_range(srv, -npc->wander_radius, npc->wander_radius);
            int dest_z =
                npc->spawn_z + random_range(srv, -npc->wander_radius, npc->wander_radius);
            if( dest_x != npc->x || dest_z != npc->z )
                ToriRSServer_WorldNpcWalkTo(npc, dest_x, dest_z);
            else if( npc->waypoint_index >= 0 )
            {
                if( npc_take_step(npc) )
                    npc->stuck_counter = 0;
                else
                    npc->stuck_counter++;
            }
            else
                npc->stuck_counter++;
        }
        else if( npc->waypoint_index >= 0 )
        {
            if( npc_take_step(npc) )
                npc->stuck_counter = 0;
            else
                npc->stuck_counter++;
        }
        else
            npc->stuck_counter++;

        /* stuck_counter > 500 → teleport to spawn (LostCity wanderMode). */
        if( npc->stuck_counter > 500 )
        {
            if( npc->x != npc->spawn_x || npc->z != npc->spawn_z ||
                npc->level != npc->spawn_level )
            {
                ToriRSServer_WorldNpcTeleport(
                    npc, npc->spawn_x, npc->spawn_z, npc->spawn_level);
            }
            npc->stuck_counter = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Ground objs                                                         */
/* ------------------------------------------------------------------ */

/*
 * Objs on the floor.
 *
 * Two kinds, and the distinction is LostCity's: a *spawn* comes from a map
 * square's `==== OBJ ====` section and comes back on a timer after it is taken;
 * a *drop* is left by a kill and expires. Collapsing the two would mean either
 * a world that empties permanently or loot that never goes away.
 *
 * The client is told about one only while it is within the scene, and told
 * again after a rebuild — REBUILD_NORMAL resets its zones, so `sent` is
 * cleared there rather than being trusted across it.
 */

static int
world_obj_add(
    struct ToriRSServer* srv,
    int obj_id,
    int count,
    int x,
    int z,
    int level,
    int duration,
    int receiver_pid,
    int private_ticks)
{
    if( obj_id < 0 || count <= 0 )
        return -1;

    /* Stack onto an existing pile of the same obj. A stackable obj has to
     * merge — three separate coin piles on one tile is not a thing the client
     * can draw — and merging a non-stackable one is wrong, so the count is
     * only combined when the cache says it stacks. */
    if( receiver_pid < 0 && ToriRSServer_ObjInfo(obj_id)->stackable )
    {
        for( int i = 0; i < TORIRSSERVER_GROUND_MAX; i++ )
        {
            struct ToriRSServerGroundObj* obj = &srv->ground[i];

            if( !obj->active || obj->obj_id != obj_id || obj->x != x || obj->z != z ||
                obj->level != level )
                continue;
            {
                int old_count = obj->count;

                obj->count += count;
                /* OBJ_COUNT says old->new in one packet, which is what the
                 * client's stack merge wants. This used to forget the obj on
                 * every client so phase 8 would re-add it, which was a whole
                 * OBJ_ADD to say a number changed. */
                ToriRSServer_ZoneObjCounted(srv, i, old_count, obj->count);
            }
            return i;
        }
    }

    for( int i = 0; i < TORIRSSERVER_GROUND_MAX; i++ )
    {
        struct ToriRSServerGroundObj* obj = &srv->ground[i];
        int generation;

        if( obj->active )
            continue;
        /* Before the memset, while the slot is still inactive and still carries
         * the zone it was last filed under: a slot freed and reused inside one
         * tick would otherwise stay in the old zone's list forever. */
        ToriRSServer_ZoneObjRefile(srv, i);
        /* Survives the memset, and must: it is what tells a script holding this
         * slot that the obj it was holding is not the one here now. */
        generation = obj->generation + 1;
        memset(obj, 0, sizeof(*obj));
        obj->generation = generation;
        obj->active = 1;
        obj->obj_id = obj_id;
        obj->count = count;
        obj->x = x;
        obj->z = z;
        obj->level = level;
        obj->despawn_tick = duration > 0 ? srv->tick + duration : -1;
        obj->respawn_tick = -1;
        obj->is_spawn = duration < 0;
        obj->receiver_pid = receiver_pid;
        obj->public_tick = receiver_pid >= 0 && private_ticks > 0
                         ? srv->tick + private_ticks : -1;
        ToriRSServer_ZoneObjRefile(srv, i);
        ToriRSServer_ZoneObjAdded(srv, i);
        return i;
    }
    return -1;
}

int
ToriRSServer_WorldObjAdd(
    struct ToriRSServer* srv,
    int obj_id,
    int count,
    int x,
    int z,
    int level,
    int duration)
{
    return world_obj_add(srv, obj_id, count, x, z, level, duration, -1, 0);
}

int
ToriRSServer_WorldObjAddPrivate(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* owner,
    int obj_id,
    int count,
    int x,
    int z,
    int level,
    int duration,
    int private_ticks)
{
    if( private_ticks <= 0 || !owner )
        return ToriRSServer_WorldObjAdd(srv, obj_id, count, x, z, level, duration);
    return world_obj_add(srv, obj_id, count, x, z, level, duration,
                         owner->pid, private_ticks);
}

int
ToriRSServer_WorldGroundVisibleTo(
    const struct ToriRSServer* srv,
    int slot,
    int pid)
{
    const struct ToriRSServerGroundObj* obj;

    /* A slot outside the table is a caller bug, not a hidden pile. Returning 0
     * for one reads as "you cannot see it", which is what every caller here
     * does with a false: skip the obj, refuse the take, drop it out of the zone
     * flush. The bad index would have shown up as an obj that silently never
     * appears. `torirs_server_zone.c` already indexes `srv->ground[zone->objs[i]]`
     * one line above its call, so the guard was not protecting that path
     * either -- it was only hiding it here. */
    assert(srv);
    assert(slot >= 0);
    assert(slot < TORIRSSERVER_GROUND_MAX);
    obj = &srv->ground[slot];
    return obj->active && (obj->receiver_pid < 0 || obj->receiver_pid == pid);
}

/*
 * Take a ground obj out of the world and tell the zone it is gone.
 *
 * This used to be `ground_withdraw`, which walked the player pool and sent an
 * OBJ_DEL to everyone whose `ground_sent[slot]` flag was set. It is one queued
 * event now: the zone knows who is standing in it and the flush in phase 10
 * decides who hears about it, which is the same answer without the server
 * keeping a bit per obj per client.
 */
void
ground_clear(
    struct ToriRSServer* srv,
    int slot)
{
    struct ToriRSServerGroundObj* obj = &srv->ground[slot];

    if( !obj->active )
        return;
    /* Queue the event while the obj is still filed in its zone — the event is
     * addressed to the zone the obj is *in*, and unfiling first would address
     * it to nowhere. */
    ToriRSServer_ZoneObjRemoved(srv, slot);
    obj->active = 0;
    ToriRSServer_ZoneObjRefile(srv, slot);
}

/*
 * `flush_ground` was here.
 *
 * It ran per player, over all 256 ground slots, every tick, comparing each obj
 * against `ToriRSServer_SceneContains` and a per-player `ground_sent[]` bitmap.
 * Everything it did is now a property of zones: whether a client holds a zone
 * is one set membership test, the objs in that zone are a list, and a client
 * that has just been handed one is caught up by `write_state` in
 * torirs_server_zone.c. See torirs_server_zone.h.
 */

/** The world half: expire drops and bring taken spawns back. Once a tick, for
 *  everybody, before phase 10 flushes the zones. */
static void
ground_tick(struct ToriRSServer* srv)
{
    for( int i = 0; i < TORIRSSERVER_GROUND_MAX; i++ )
    {
        struct ToriRSServerGroundObj* obj = &srv->ground[i];

        if( !obj->active )
        {
            /* A taken spawn comes back where it was. */
            if( obj->respawn_tick >= 0 && srv->tick >= obj->respawn_tick )
            {
                obj->active = 1;
                obj->respawn_tick = -1;
                /* A new entity, not the old one resuming: the reference
                 * constructs a fresh `Obj`, and a script still holding the
                 * taken one must not silently start acting on this. */
                obj->generation++;
                ToriRSServer_ZoneObjRefile(srv, i);
                ToriRSServer_ZoneObjAdded(srv, i);
            }
            continue;
        }
        if( obj->receiver_pid >= 0 && obj->public_tick >= 0 && srv->tick >= obj->public_tick )
        {
            /* The owner has a private pile on the client already. Remove that
             * view first, then publish a normal zone add to everyone so it
             * cannot become a duplicate stack for the owner. */
            ToriRSServer_ZoneObjRemoved(srv, i);
            obj->receiver_pid = -1;
            obj->public_tick = -1;
            ToriRSServer_ZoneObjAdded(srv, i);
        }
        if( obj->despawn_tick >= 0 && srv->tick >= obj->despawn_tick )
            ground_clear(srv, i);
    }
}

/*
 * Remove a ground obj, telling every client that has it and arming its respawn
 * if it was a map spawn.
 *
 * This is `World.removeObj(obj, duration)` with one substitution, and the
 * substitution predates this function's export: the reference's duration is
 * `ObjType.respawnrate`, a *per-obj* server-band field that this tree has no
 * decoder and no `fields/obj.ini` row for. `^lootdrop_duration` — content's
 * own constant, resolved through `ToriRSServer_Ids()` — stands in for all of them.
 * Both of the reference's callers (`OBJ_DEL`, and `OBJ_TAKEITEM`'s removal
 * half) reduce to exactly this here, because `removeObj` ignores its duration
 * entirely unless the obj is a RESPAWN one.
 *
 * Exported so the `obj_*` opcodes remove a pile the same way the engine does.
 * The `ground_clear`-first ordering is load-bearing and is stated there.
 */
void
ToriRSServer_WorldGroundTake(
    struct ToriRSServer* srv,
    int slot)
{
    struct ToriRSServerGroundObj* obj;

    if( slot < 0 || slot >= TORIRSSERVER_GROUND_MAX )
        return;
    obj = &srv->ground[slot];
    ground_clear(srv, slot);
    obj->respawn_tick = obj->is_spawn ? srv->tick + ToriRSServer_Ids()->lootdrop_duration : -1;
}

int
ToriRSServer_WorldGroundFind(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int obj_id)
{
    for( int i = 0; i < TORIRSSERVER_GROUND_MAX; i++ )
    {
        const struct ToriRSServerGroundObj* obj = &srv->ground[i];

        if( !ToriRSServer_WorldGroundVisibleTo(srv, i, srv->active_player->pid) ||
            obj->obj_id != obj_id )
            continue;
        if( obj->x != x || obj->z != z || obj->level != level )
            continue;
        return i;
    }
    return -1;
}

/*
 * The handle, and why it is not just the slot.
 *
 * TORIRSSERVER_GROUND_MAX is 4096, so thirteen bits carry `slot + 1` and everything
 * above is the slot's generation. The generation is what makes the handle name
 * an *obj* rather than an *index*: a script that suspends between `obj_find`
 * and `obj_takeitem` resumes into a world where its pile may have been taken
 * and the slot handed to somebody else's drop, and an index would resolve to
 * that drop with nothing failing.
 *
 * This was 9 bits when the pool was 256, and the assertion below is what said
 * so when the pool grew to hold the world's 2,256 obj spawns. Widening the slot
 * field narrows the generation, and the narrow case is the one that matters:
 * `intptr_t` is 32 bits under emscripten, which leaves 19 bits — half a million
 * take-and-respawn cycles on a single slot before a handle can repeat, against
 * a spawn that respawns on a timer measured in ticks.
 */
#define TORIRSSERVER_OBJ_HANDLE_SLOT_BITS 13

/* `slot + 1` has to fit under the generation, or two different objs can share a
 * handle and the identity check silently stops checking. */
typedef char ToriRSServer_ObjHandleSlotFits
    [(TORIRSSERVER_GROUND_MAX + 1) <= (1 << TORIRSSERVER_OBJ_HANDLE_SLOT_BITS) ? 1 : -1];

intptr_t
ToriRSServer_WorldObjHandle(
    struct ToriRSServer* srv,
    int slot)
{
    if( slot < 0 || slot >= TORIRSSERVER_GROUND_MAX )
        return 0;
    return (intptr_t)(((intptr_t)srv->ground[slot].generation
                       << TORIRSSERVER_OBJ_HANDLE_SLOT_BITS) |
                      (intptr_t)(slot + 1));
}

int
ToriRSServer_WorldGroundSlot(
    struct ToriRSServer* srv,
    intptr_t handle)
{
    int slot = (int)(handle & ((1 << TORIRSSERVER_OBJ_HANDLE_SLOT_BITS) - 1)) - 1;

    if( slot < 0 || slot >= TORIRSSERVER_GROUND_MAX )
        return -1;
    if( !srv->ground[slot].active )
        return -1;
    if( handle != ToriRSServer_WorldObjHandle(srv, slot) )
        return -1;
    return slot;
}

/* ------------------------------------------------------------------ */
/* Client packet handlers                                              */
/* ------------------------------------------------------------------ */

/*
 * MOVE_GAMECLICK / MOVE_MINIMAPCLICK.
 *
 * osrs230 MOVE_GAMECLICK is a fixed 5-byte destination body
 * (keyCombination, x, z) with no waypoints — the server owns the route.
 * Minimap still uses the classic start + signed (dx,dz) pairs plus a
 * 14-byte anti-cheat trailer (subtracted before counting waypoints).
 * If a LostCity-era client still sends intermediate waypoints, the last
 * one is the destination and we re-path from the player.
 */
static void
handle_move(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len,
    int trailer_len)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct RSAreaBuf buf;
    int ctrl;
    int start_x;
    int start_z;
    int waypoints;
    int dest_x;
    int dest_z;
    int dx_sw;
    int dz_sw;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    ctrl = rsab_g1(&buf);
    start_x = rsab_g2(&buf);
    start_z = rsab_g2(&buf);
    if( !rsab_ok(&buf) )
        return;

    /*
     * Ctrl held on a move request turns run mode ON and leaves it on, the way
     * a ctrl-click does in OldSchool. It is deliberately not a mirror of the
     * flag: the client sends 0 on every ordinary click, so assigning it would
     * mean the next plain click silently switched running back off.
     *
     * `running` itself is decided per tick in advance_player, because the
     * energy can run out between here and the step that spends it.
     */
    if( ctrl && !player->run_toggle )
    {
        player->run_toggle = 1;
        ToriRSServer_WorldSetVarp(srv, ToriRSServer_WorldVarp("option_run"), 1);
    }

    /* Reference MoveClickHandler: reject clicks whose first waypoint is more
     * than a scene away from the player. */
    dx_sw = start_x - player->x;
    dz_sw = start_z - player->z;
    if( dx_sw < 0 )
        dx_sw = -dx_sw;
    if( dz_sw < 0 )
        dz_sw = -dz_sw;
    if( dx_sw > 104 || dz_sw > 104 )
    {
        player->clear_map_flag = 1;
        steps_clear(player);
        player->dest_x = -1;
        player->dest_z = -1;
        return;
    }

    /* Walking somewhere is a new interaction, and a new interaction ends the
     * old one. Without this the player keeps swinging at whatever they were
     * fighting the moment they get back in range, and — more visibly — keeps
     * *facing* it the whole way there. The same argument retires a pending
     * op: clicking the ground half way to a door means you changed your mind,
     * and a door that opens when you happen to walk past it later is worse than
     * one that does not open at all.
     *
     * An open dialogue goes with them, which is the same rule seen from the
     * other side: the conversation *is* the interaction, so walking off while
     * an npc is mid-sentence has to end it. Leaving it up wedged the player's
     * one script slot as well as the screen — every later trigger that wanted
     * to park hit "dropping a script that suspended while another waits", so a
     * single unfinished chat quietly disabled every dialogue after it. */
    ToriRSServer_WorldClearPendingAction(srv);

    dest_x = start_x;
    dest_z = start_z;
    waypoints = (len - 5 - trailer_len) / 2;
    if( waypoints < 0 )
        waypoints = 0;
    for( int i = 0; i < waypoints; i++ )
    {
        int dx = rsab_g1s(&buf);
        int dz = rsab_g1s(&buf);
        if( !rsab_ok(&buf) )
            break;
        dest_x = start_x + dx;
        dest_z = start_z + dz;
    }

    /* Same-tile click: clear the route (reference sets allowRepath NONE). */
    if( waypoints == 0 && start_x == player->x && start_z == player->z )
    {
        steps_clear(player);
        player->dest_x = -1;
        player->dest_z = -1;
        player->clear_map_flag = 1;
        return;
    }

    ToriRSServer_WorldWalkTo(srv, dest_x, dest_z);

    if( srv->verbose )
        fprintf(
            stderr,
            "torirsserver: <- MOVE ctrl=%d start=%d,%d waypoints=%d wp_idx=%d dest=%d,%d\n",
            ctrl,
            start_x,
            start_z,
            waypoints,
            player->waypoint_index,
            player->dest_x,
            player->dest_z);
}

/** Is this component one of the bank's two item grids? Those are the only
 *  components whose `slot` does not index the backpack. */
static int
bank_component(int component)
{
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();

    return component == ids->com_bankmain_items || component == ids->com_bankside_items;
}

/* OPHELD<n>: p2 objId, p2 slot, p4 componentId. `n` is the 1-based index into
 * the item's inventory ops, so the server can tell "Wear" from "Drop" instead
 * of guessing from the item type.
 *
 * `component` is the packed (interface << 16) | child uid of the container the
 * cell belongs to — 149:0 for the backpack, 387:15..25 for a worn slot — and
 * `slot` the sub id inside it (rsprot If3Button.combinedId / .sub). The
 * backpack's sub id IS the inventory slot; the worn tab's is not, so that side
 * goes through ToriRSServer_EquipmentWornSlot. */
void
handle_worn_inv_button(
    struct ToriRSServer* srv,
    int component,
    int op_num)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int worn = ToriRSServer_EquipmentWornSlot(component);
    const char* com_name;
    int result;
    int trigger;

    if( worn < 0 || op_num < 1 || op_num > 5 )
        return;

    /*
     * IF_BUTTONX is the only rev-239 callback for a worn-slot action.  The
     * golden RuneLite client writes its object field as 0xffff for these
     * static IF3 leaves, so object presence cannot select the old OPHELD path.
     * The component is the enduring address: content binds
     * [inv_buttonN,wornitems:slotN] and its enum resolves that component back
     * to the worn container slot.
     */
    player->last_slot = worn;
    player->last_com = component;
    player->last_verb = op_num;
    trigger = SS_TRIGGER_INV_BUTTON1 + (op_num - 1);
    result = ToriRSServer_ScriptsRunTrigger(srv, trigger, component, -1, -1);
    if( result == TORIRSSERVER_TRIGGER_NONE &&
        (com_name = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_COMPONENT, component)) )
    {
        char name[192];
        const struct SSVM_Script* script;

        snprintf(name, sizeof(name), "[inv_button%d,%s]", op_num, com_name);
        script = srv->scripts_ok ? SSVM_ProviderGetByName(srv->scripts, name) : NULL;
        if( script )
            result = ToriRSServer_ScriptsRunHook(srv, script, NULL, 0)
                         ? TORIRSSERVER_TRIGGER_RAN
                         : TORIRSSERVER_TRIGGER_FAILED;
    }
    /*
     * Nothing bound the component itself — fall through to the same
     * obj-based [opheldN,name] dispatch the backpack path uses (above), so
     * content only has to bind an item's Check/Revert/etc once and it fires
     * whether the item is clicked from the backpack or the worn tab. Op1 is
     * excluded: the worn tab's slot-0 op is always Remove regardless of what
     * the objtype's own op1 label says (see the unequip_slot fallback
     * below), so re-dispatching it by obj id could hijack that with an
     * unrelated `[opheld1,obj]` binding meant for backpack context.
     *
     * Before this, `[opheld3,dodgy_necklace]`, `[opheld4,ibanstaff]`, every
     * crystal item's `[opheld3,...]` Check, and any other worn charged
     * item's Check/Revert/Dismantle bindings were unreachable while worn —
     * `handle_worn_inv_button` never tried anything past the per-slot
     * component binding, so clicking Check on an equipped item silently hit
     * the engine's "nothing bound this" fallback every time. docs/
     * ITEM_CHARGES_PLAN.md's charged items are typically worn when checked,
     * which is exactly the case this was breaking.
     */
    if( result == TORIRSSERVER_TRIGGER_NONE && op_num >= 2 )
    {
        int worn_obj_id = player->worn[worn].obj_id;
        if( worn_obj_id > 0 )
        {
            const struct ToriRSServerObjInfo* worn_info = ToriRSServer_ObjInfo(worn_obj_id);
            player->last_item = worn_obj_id;
            result = ToriRSServer_ScriptsRunTrigger(
                srv, SS_TRIGGER_OPHELD1 + (op_num - 1), worn_obj_id,
                worn_info->category > 0 ? worn_info->category : -1, -1);
        }
    }
    if( result == TORIRSSERVER_TRIGGER_NONE && op_num == 1 )
        unequip_slot(srv, worn);
}

static void
handle_opheld(
    struct ToriRSServer* srv,
    int op_num,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct RSAreaBuf buf;
    int obj_id;
    int slot;
    int component;
    const struct ToriRSServerObjInfo* info;
    const char* verb;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    obj_id = rsab_g2(&buf);
    slot = rsab_g2(&buf);
    component = rsab_g4(&buf);
    if( !rsab_ok(&buf) )
        return;

    /* Worn tab actions are INV_BUTTON triggers, not ObjType actions. Do this
     * before reading obj metadata: rev239's IF_BUTTONX route may carry the
     * no-item sentinel for a static worn-slot leaf. */
    if( ToriRSServer_EquipmentWornSlot(component) >= 0 )
    {
        handle_worn_inv_button(srv, component, op_num);
        return;
    }

    info = ToriRSServer_ObjInfo(obj_id);
    verb = (op_num >= 1 && op_num <= 5) ? info->if_ops[op_num - 1] : NULL;
    if( srv->verbose )
        fprintf(
            stderr,
            "torirsserver: <- OPHELD%d obj=%d (%s) slot=%d com=%d|%d verb=%s\n",
            op_num,
            obj_id,
            info->name,
            slot,
            (component >> 16) & 0xffff,
            component & 0xffff,
            verb ? verb : "-");

    /*
     * The bank's two grids answer on the same packet, and they are not the
     * backpack: `slot` there indexes container 95 (or the side panel's copy of
     * the backpack), and `component` is what says which. Content gets first
     * refusal through the [inv_button<n>] trigger; the bank's own router is a
     * declared engine fallback (TORIRSSERVER_FALLBACK_INV_BUTTON) rather than the
     * unconditional else it used to be.
     */
    if( bank_component(component) )
    {
        player->last_slot = slot;
        player->last_com = component;
        if( ToriRSServer_ScriptsFallback(
                srv, TORIRSSERVER_FALLBACK_INV_BUTTON,
                ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_INV_BUTTON1 + (op_num - 1),
                                            component, -1, -1)) )
            ToriRSServer_BankHandleButton(srv, component, slot, obj_id, op_num);
        return;
    }

    if( slot < 0 || slot >= TORIRSSERVER_INV_SLOTS || player->inv[slot].obj_id != obj_id )
        return;

    /*
     * Content first, exactly as OPNPC and OPLOC do it.
     *
     * The obj record's own `category` is the second key, which is what lets
     * `[opheld1,_bones]` cover all 38 bones in the cache from one script the
     * way the reference's does — an obj category is the only grouping an
     * OldSchool cache states, and restating it as a list in content would be a
     * second copy of it kept by hand.
     *
     * `last_item` / `last_slot` / `last_verb` are set before the dispatch
     * because a bound script reads them to learn *which* item was clicked: the
     * script is addressed by obj or category, so nothing else carries the slot.
     */
    player->last_item = obj_id;
    player->last_slot = slot;
    player->last_com = component;
    player->last_verb = op_num;
    /*
     * And that is the whole of it. `TORIRSSERVER_FALLBACK_OPHELD` was here until
     * 2026-08-02 — a verb ladder matching the cache strings "Wear", "Wield" and
     * "Drop" and then `equip_from_slot` — and it is `[opheld2,_] ~equip(last_slot)`
     * and `[opheld5,_] ~dropslot(last_slot)` now, the reference's own two lines,
     * in `player/scripts/equip.rs2` and `player/scripts/drop.rs2`.
     *
     * A miss answers the way `[opobj<n>]`'s does and for the same reason: the
     * reference's engine says `Player.defaultOp` and nothing else. That is a
     * behaviour change for the ops the ladder used to sweep up — it acted on a
     * *verb string* wherever the cache put it, and it equipped anything wearable
     * on any op index that carried no verb at all. Both of those are addressed
     * by content now, and an op nothing binds says so.
     *
     * FAILED is deliberately silent: a script that aborted has already reported
     * itself, and speaking over it re-creates exactly the indistinguishability
     * the inverted fallback exists to remove (§3.18).
     */
    if( op_num >= 1 && op_num <= 5 &&
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPHELD1 + (op_num - 1), obj_id,
                                    info->category > 0 ? info->category : -1,
                                    -1) == TORIRSSERVER_TRIGGER_NONE )
        ToriRSServer_Say(srv, "nothing_interesting_message", NULL);
}

/*
 * Portal Nexus configuration is deliberately client-staged.  Interface 19's
 * original CS2 mutates the *_temp varbits while an entry is dragged, then the
 * server validates and commits that staging area when Confirm is clicked.
 * IF_BUTTOND carries enough information to mirror those mutations: the source
 * and destination dynamic-child ids are the destination-list ordinal and slot.
 *
 * https://oldschool.runescape.wiki/w/Portal_nexus#Customisation_and_usage
 */
static int
ToriRSServer_TeleNexusSlotVarbit(int slot, int temporary)
{
    char name[64];

    if( slot < 1 || slot > 45 )
        return -1;
    snprintf(name, sizeof(name), "poh_nexus_tele_%d%s", slot,
             temporary ? "_temp" : "");
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
}

static int
ToriRSServer_TeleNexusSlotGet(struct ToriRSServerPlayer* player, int slot, int temporary)
{
    int varbit = ToriRSServer_TeleNexusSlotVarbit(slot, temporary);

    return varbit >= 0 ? ToriRSServer_VarbitGet(player, varbit) : 0;
}

static void
ToriRSServer_TeleNexusSlotSet(
    struct ToriRSServer* srv,
    int slot,
    int temporary,
    int value)
{
    int varbit = ToriRSServer_TeleNexusSlotVarbit(slot, temporary);

    if( varbit >= 0 )
        ToriRSServer_VarbitSet(srv, varbit, value);
}

static int
ToriRSServer_TeleNexusCapacity(struct ToriRSServerPlayer* player)
{
    int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "poh_nexus_id");
    int tier = id >= 0 ? ToriRSServer_VarbitGet(player, id) : 0;

    if( tier == 1 )
        return 4;
    if( tier == 2 )
        return 8;
    if( tier == 3 )
        return 41;
    return 0;
}

static int
ToriRSServer_TeleNexusStageDrag(
    struct ToriRSServer* srv,
    int src_com,
    int src_slot,
    int dst_com,
    int dst_slot)
{
    /* enum_1375's exact 41-entry ordering in the rev-239 cache. */
    static const int destination_by_row[41] = {
        1,  2,  3,  4,  11, 5,  31, 6,  8,  7,  9,  18, 14, 17,
        10, 12, 13, 16, 15, 19, 20, 21, 22, 23, 24, 25, 26, 27,
        28, 29, 30, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
    };
    struct ToriRSServerPlayer* player = srv->active_player;
    int available = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_COMPONENT, "telenexus:non_slotted_list");
    int slotted = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_COMPONENT, "telenexus:slotted_list");
    int left_click = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_COMPONENT, "telenexus:click_layer");
    int capacity = ToriRSServer_TeleNexusCapacity(player);

    if( capacity == 0 || available < 0 || slotted < 0 || left_click < 0 )
        return 0;

    if( src_com == available && dst_com == slotted )
    {
        int destination;

        if( src_slot < 1 || src_slot > 41 )
            return 1;
        destination = destination_by_row[src_slot - 1];
        for( int slot = 1; slot <= capacity; slot++ )
            if( ToriRSServer_TeleNexusSlotGet(player, slot, 1) == destination )
                return 1;
        for( int slot = 1; slot <= capacity; slot++ )
        {
            if( ToriRSServer_TeleNexusSlotGet(player, slot, 1) == 0 )
            {
                ToriRSServer_TeleNexusSlotSet(srv, slot, 1, destination);
                break;
            }
        }
        return 1;
    }

    if( src_com != slotted || src_slot < 1 || src_slot > capacity )
        return 0;
    if( dst_com == slotted )
    {
        int source;
        int target;

        if( dst_slot < 1 || dst_slot > capacity || dst_slot == src_slot )
            return 1;
        source = ToriRSServer_TeleNexusSlotGet(player, src_slot, 1);
        target = ToriRSServer_TeleNexusSlotGet(player, dst_slot, 1);
        ToriRSServer_TeleNexusSlotSet(srv, src_slot, 1, target);
        ToriRSServer_TeleNexusSlotSet(srv, dst_slot, 1, source);
        return 1;
    }
    if( dst_com == available )
    {
        ToriRSServer_TeleNexusSlotSet(srv, src_slot, 1, 0);
        return 1;
    }
    if( dst_com == left_click )
    {
        int varbit = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_VARBIT, "poh_nexus_left_click_temp");
        int destination = ToriRSServer_TeleNexusSlotGet(player, src_slot, 1);

        if( varbit >= 0 && destination > 0 )
            ToriRSServer_VarbitSet(srv, varbit, destination);
        return 1;
    }
    return 0;
}

static void
ToriRSServer_TeleNexusStageClick(
    struct ToriRSServer* srv,
    int component,
    int sub,
    int op_num)
{
    int clear = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_COMPONENT, "telenexus:click_text");
    int options = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_COMPONENT, "telenexus:radio_button_options");
    int mode_options = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_COMPONENT, "telenexus_teleport:options_layer");
    int varbit = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_VARBIT, "poh_nexus_left_click_temp");
    int value;

    if( op_num != 1 )
        return;
    if( component == mode_options && (sub == 2 || sub == 3) )
    {
        int mode = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_VARBIT, "poh_nexus_tele_scry_mode");

        if( mode >= 0 )
            ToriRSServer_VarbitSet(srv, mode, sub == 3);
        return;
    }
    if( varbit < 0 )
        return;
    if( component == clear )
    {
        ToriRSServer_VarbitSet(srv, varbit, 0);
        return;
    }
    if( component != options )
        return;

    value = ToriRSServer_VarbitGet(srv->active_player, varbit);
    /* The alternate teleport paired by param_679 is encoded as base + 150,
     * exactly as clientscript telenexus_radio_click does it. */
    if( sub == 2 && value > 150 )
        ToriRSServer_VarbitSet(srv, varbit, value - 150);
    else if( sub == 3 && value > 0 && value < 150 )
        ToriRSServer_VarbitSet(srv, varbit, value + 150);
}

/* INV_BUTTOND / IfButtonD: rev-230 dual-endpoint frame (16 bytes):
 *   srcCom p4 LE, srcObj p2 LE, srcSlot p2 LE+128,
 *   dstCom p4 BE, dstObj p2 BE+128, dstSlot p2 BE+128.
 * The endpoint object fields are the item ids painted on the two widgets.
 * Rev239's inventory script paints a client-only null object on empty cells,
 * so an empty destination does not necessarily send -1. The server's own
 * container is authoritative about whether the slot is empty. The CS2 drag
 * hook optimistically repaints the two dynamic widgets but does not mutate the
 * logical inventory; this packet is what actually moves the items, and the
 * following transmit reconciles that paint with server state.
 *
 * Same-component moves inside the backpack and bankmain items are accepted.
 * Bank item → bankmain:tabs assigns the stack to that tab (updates tab_size).
 * Other cross-container drags are refused — the mock has no equip-by-drag. */
static void
handle_inv_buttond(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct RSAreaBuf buf;
    int src_com;
    int src_obj;
    int src_slot;
    int dst_com;
    int dst_obj;
    int dst_slot;
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    src_com = rsab_g4_alt1(&buf);
    src_obj = rsab_g2_alt1(&buf);
    src_slot = rsab_g2_alt3(&buf);
    dst_com = rsab_g4(&buf);
    dst_obj = rsab_g2_alt2(&buf);
    dst_slot = rsab_g2_alt2(&buf);
    if( !rsab_ok(&buf) )
        return;

    /* Sign-extend the 16-bit item ids: empty slots arrive as 0xffff (-1). */
    if( src_obj >= 0x8000 )
        src_obj -= 0x10000;
    if( dst_obj >= 0x8000 )
        dst_obj -= 0x10000;

    if( srv->verbose )
        fprintf(
            stderr,
            "torirsserver: <- INV_BUTTOND %d|%d#%d (obj %d) -> %d|%d#%d (obj %d)\n",
            (src_com >> 16) & 0xffff,
            src_com & 0xffff,
            src_slot,
            src_obj,
            (dst_com >> 16) & 0xffff,
            dst_com & 0xffff,
            dst_slot,
            dst_obj);

    if( ToriRSServer_TeleNexusStageDrag(
            srv, src_com, src_slot, dst_com, dst_slot) )
        return;

    if( src_com == ids->com_inventory_items || src_com == ids->com_bankside_items )
    {
        struct ToriRSServerItem swap;

        /* The normal gameframe and the bank side panel are two named views of
         * the backpack. A drag remains within the surface it started on; the
         * component selects the view while the slots index the same container. */
        if( src_com != dst_com )
            return;
        if( src_slot == dst_slot )
            return;
        if( src_slot < 0 || src_slot >= TORIRSSERVER_INV_SLOTS || dst_slot < 0 ||
            dst_slot >= TORIRSSERVER_INV_SLOTS )
            return;
        /* Stale source: a drag can only begin on the item the server still has
         * in that slot. The destination item is likewise checked when filled.
         * When the authoritative destination is empty, ignore its cosmetic
         * widget item: rev239 deliberately paints a non-empty null object in
         * empty inventory cells and method3759 transmits that field verbatim. */
        if( player->inv[src_slot].obj_id != src_obj )
            return;
        if( player->inv[dst_slot].obj_id >= 0 &&
            player->inv[dst_slot].obj_id != dst_obj )
            return;

        swap = player->inv[src_slot];
        inv_set(player, src_slot, player->inv[dst_slot].obj_id, player->inv[dst_slot].count);
        inv_set(player, dst_slot, swap.obj_id, swap.count);
        return;
    }

    if( src_com == ids->com_bankmain_items )
    {
        struct ToriRSServerBank* bank = &player->bank;

        if( src_slot < 0 || src_slot >= bank->size )
            return;
        if( src_obj >= 0 && bank->slots[src_slot].obj_id != src_obj )
            return;

        /* Drop onto the tab strip: child 0..9 backgrounds, 10..19 icons
         * (script_505). Kronos maps the same way (toSlot - 10). */
        if( dst_com == ids->com_bankmain_tabs )
        {
            int dest_tab = dst_slot;

            if( dest_tab >= 10 && dest_tab <= 19 )
                dest_tab -= 10;
            else if( dest_tab < 0 || dest_tab > 9 )
                return;
            ToriRSServer_BankMoveToTab(srv, src_slot, dest_tab);
            return;
        }

        if( src_com != dst_com )
            return;
        if( dst_slot < 0 || dst_slot >= bank->size )
            return;
        if( src_slot == dst_slot )
            return;
        if( dst_obj >= 0 && bank->slots[dst_slot].obj_id != dst_obj )
            return;
        if( dst_obj < 0 && bank->slots[dst_slot].obj_id >= 0 )
            return;

        ToriRSServer_BankMoveSlot(srv, src_slot, dst_slot);
        return;
    }
}

/*
 * What an npc op does when no content claimed it.
 *
 * "Attack" is engine behaviour, not content. The verb comes from the npc's own
 * cache record — the same five ops the client built its right-click menu from —
 * so anything OldSchool made attackable is attackable here, with no per-npc
 * script line and no separate list to keep in step with the client's. Which op
 * index carries it varies (a goblin's Attack is op 2, a guard's is op 1), which
 * is exactly why the index is not hardcoded.
 */
static void
interaction_engine_npc(
    struct ToriRSServer* srv,
    int slot,
    int op_num)
{
    struct ToriRSServerNpc* npc;
    const struct ToriRSServerNpcInfo* info;
    const char* verb;

    if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX || !srv->npcs[slot].active )
        return;
    npc = &srv->npcs[slot];

    info = ToriRSServer_NpcInfo(npc->type);
    verb = (op_num >= 1 && op_num <= 5) ? info->ops[op_num - 1] : NULL;
    if( verb && strcmp(verb, TORIRSSERVER_VERB_ATTACK) == 0 )
    {
        ToriRSServer_CombatEngage(srv, slot);
        return;
    }

    /*
     * Everything else is a greeting, and the words are content's:
     * `[proc,npc_default_chat]` (player/messages.rs2), run with this npc active
     * so it can use `npc_say` (overhead SAY on NPC_INFO).
     */
    /* The clicker, by pid: `active_player` is whoever's packet is being
     * dispatched, which for an op handler is exactly the right answer. Two
     * clients greeting the same npc each see it turn to the one who clicked. */
    if( srv->active_player )
        ToriRSServer_NpcFacePlayer(npc, srv->active_player->pid);
    ToriRSServer_ScriptsRunProcOnNpc(srv, "[proc,npc_default_chat]", slot);
}

/*
 * OPNPC<n>: p2 npc slot.
 *
 * Latches the interaction and starts the walk; the acting happens in
 * ToriRSServer_WorldProcessInteraction, either on this tick (already in range) or
 * on whichever tick the player arrives.
 */
static void
handle_opnpc(
    struct ToriRSServer* srv,
    int op_num,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int slot;
    struct ToriRSServerNpc* npc;
    const struct ToriRSServerNpcInfo* info;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    /*
     * The client names the npc by what WE called it (ToriRSServerPlayerSlotMap), so
     * this is a translation and not a cast. Using the wire value as a pool
     * index is how every npc became unclickable: the name resolved to a
     * different npc, the walk went to that one's tile, and the player was told
     * "I can't reach that" while standing on top of the one they clicked.
     */
    slot = ToriRSServer_SlotMapWorld(srv->active_player, rsab_g2(&buf));
    if( !rsab_ok(&buf) || slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active )
        return;

    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- OPNPC%d slot=%d type=%d\n", op_num, slot, npc->type);

    /* A new interaction ends the old one — including the facing, and including
     * a dialogue still on screen from the last one. Combat is re-established by
     * the engine handler if this op is "Attack".
     *
     * Clicking the *same* npc again is the case that makes this necessary
     * rather than tidy: the [opnpc] about to run wants to park a fresh script,
     * and it cannot while the previous conversation still holds the slot. */
    ToriRSServer_WorldClearPendingAction(srv);

    info = ToriRSServer_NpcInfo(npc->type);
    ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_NPC, op_num, slot, npc->type, npc->x,
                                  npc->z, npc->level, info->size, info->size);
    {
        struct CollisionApproach approach;
        ToriRSServer_SceneNpcApproach(info->size, &approach);
        ToriRSServer_WorldWalkToApproach(srv, npc->x, npc->z, &approach);
    }

    /* Idle roaming resumes only after the response has had time to show. */
    npc->next_roam_tick = srv->tick + 8;

    ToriRSServer_WorldProcessInteraction(srv);
}

/*
 * The player's plane changed — drop entity/zone tracking for the old plane.
 *
 * Split out of `climb` (below) 2026-08-02 so that `p_teleport` can call it too.
 * That is not tidying: `SS_OP_P_TELEPORT` moved x/z/level and set `place_dirty`
 * and nothing else, so a script that teleported a player up a floor left the
 * client holding a level's worth of npcs, players and zone state that it had no
 * way to learn about. `maybe_rebuild` does not cover it — it fires on the scene
 * *window* moving, and a ladder does not move the window at all.
 *
 * Do **not** set `rebuild_pending`. The wire's LOC_ADD_CHANGE has no plane —
 * the client applies it to `minusedlevel` (the player's current plane) — so
 * content that must edit another plane (Kronos Inferno: tele to z1, spawn
 * flanks, tele back) relies on those locs surviving the return trip. A
 * REBUILD_* reloads scenery from the cache and would wipe them; LostCity's
 * `PathingEntity.teleport` only sets jump/INSTANT on a plane change for the
 * same reason. Entity lists and zone catch-up still reset so the new plane's
 * NPCs and ZoneMap state are re-sent without touching other levels' scenery.
 *
 * Not the caller's steps or destination: `ToriRSServer_WorldProcessInteraction`
 * has already cleared both before any `[oploc]` script runs, and a teleport that
 * is not a plane change has no business dropping a queued walk.
 */
void
ToriRSServer_WorldPlayerLevelChanged(struct ToriRSServerPlayer* player)
{
    player->place_dirty = 1;
    memset(player->npc_tracked, 0, sizeof(player->npc_tracked));
    player->tracked_count = 0;
    /* The zones on the new level are different zones — a zone key carries the
     * level — so this is not only "forget what you were told", it is what makes
     * the next flush compute a new active window. */
    ToriRSServer_ZonePlayerReset(player);
    memset(player->player_tracked, 0, sizeof(player->player_tracked));
    player->tracked_player_count = 0;
}

/*
 * `climb` was here — "move the player a level, which is all a staircase or a
 * ladder does". It is `[proc,climb]` in
 * `ladders_stairs/scripts/ladders.rs2` now, four lines of `p_teleport
 * (movecoord(coord, 0, $delta, 0))` behind a plane-range guard, which is where
 * LostCity puts it (`content/scripts/ladders+stairs/`, 607 lines, none of it in
 * that tree's engine). What stayed here is the half that is owed to every
 * teleport across a plane and not only to a ladder:
 * `ToriRSServer_WorldPlayerLevelChanged` above, called from `SS_OP_P_TELEPORT`.
 *
 * OPOBJ<n>: p2 x, p2 z, p2 objId — picking something up off the floor.
 *
 * The walk and the take are one action here rather than a queued interaction:
 * the mock has no interaction model, so the player arrives instantly in game
 * terms and the client sees the walk happen underneath. Getting that wrong in
 * the other direction (take first, walk after) would let a player vacuum up
 * Lumbridge from the castle roof.
 * OPOBJ<n>: p2 x, p2 z, p2 objId — the click on a pile on the floor.
 *
 * This handler latches an interaction and walks; the act happens on arrival, in
 * `ToriRSServer_WorldProcessInteraction`. Getting that wrong in the other
 * direction (take first, walk after) would let a player vacuum up Lumbridge
 * from the castle roof.
 *
 * There is no engine behaviour behind it any more. `interaction_engine_obj`
 * lived here and was ~39 lines that took the pile whatever op number arrived,
 * because it never read one; `player/scripts/pickup.rs2` binds `[opobj3,_]`
 * instead, which is the op the client synthesises Take on and the op the
 * reference binds (43 `[opobj3,*]` scripts in LostCity's content, plus
 * `[opobj1,yommiseeds]` and `[opobj4,_category_22]` — Light on a pile of logs —
 * which are exactly the ops the deleted C answered by picking the logs up).
 */
static void
handle_opobj(
    struct ToriRSServer* srv,
    int op_num,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int tile_x;
    int tile_z;
    int obj_id;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    tile_x = rsab_g2(&buf);
    tile_z = rsab_g2(&buf);
    obj_id = rsab_g2(&buf);
    if( !rsab_ok(&buf) )
        return;

    ToriRSServer_WorldClearPendingAction(srv);
    ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_OBJ, op_num, -1, obj_id, tile_x,
                                  tile_z, srv->active_player->level, 1, 1);
    /* Onto the tile, not beside it: a pile is picked up from on top. Exact
     * arrival first; the reach test retries 1x1 adjacent if needed. */
    {
        struct CollisionApproach exact;
        ToriRSServer_SceneObjApproach(0, &exact);
        ToriRSServer_WorldWalkToApproach(srv, tile_x, tile_z, &exact);
        if( !player_has_waypoints(srv->active_player) &&
            !ToriRSServer_SceneReached(srv->active_player->level, srv->active_player->x,
                                   srv->active_player->z, tile_x, tile_z, &exact) )
        {
            struct CollisionApproach adj;
            ToriRSServer_SceneObjApproach(1, &adj);
            ToriRSServer_WorldWalkToApproach(srv, tile_x, tile_z, &adj);
        }
    }

    ToriRSServer_WorldProcessInteraction(srv);
}

/*
 * `interaction_engine_loc` was here — 84 lines, deleted 2026-08-02 with the
 * `TORIRSSERVER_FALLBACK_OPLOC` row it was the whole of. It did two things and both
 * are content now, which is where LostCity has always had them:
 *
 * - **the door swap.** `doors/scripts/doors.rs2` — `[oploc1,_door_closed]`,
 *   `[oploc1,_door_opened]`, `[oploc2,_door_opened]`, keyed on the category and
 *   reading the pairing out of `loc_param(next_loc_stage)`, the reference's own
 *   file verbatim. The reference's door SWINGS (`loc_del` then `loc_add` on the
 *   adjacent tile, angle turned a quarter) where this C swapped in place.
 * - **the verb ladder.** A `strcmp` against the loc's cache menu verb: "Bank"
 *   and "Use-quickly" opened the bank, "Climb-up"/"Climb-down"/"Climb" moved
 *   the player a plane. Content cannot read a menu verb, so each verb became
 *   the list of records carrying it, derived from the cache and checked against
 *   it on every `test-port`: `tools/bank_import.py` -> 78 name bindings in
 *   `interface_bank/scripts/bank_booths.rs2`, `tools/ladder_import.py` -> four
 *   allocated categories over 1,428 records plus 17 names.
 *   "Use-quickly" matched **zero** records in this cache and moved nowhere.
 *
 * Two things went with it deliberately rather than being reproduced.
 *
 * **The op number.** This function never read one — it swapped a door for any
 * op the client could send — so it also answered `Pick-lock` on a locked house
 * door, `Repair` on a damaged pest gate, `Force`, `Remove`, `Attack`, `Search`
 * and `Quick-open` by opening the thing. Measured across the whole cache that is
 * 54 (record, op) pairs, on 26 records. Content binds the op the pairing is
 * about and nothing else, so those 54 now get `Player.defaultOp` — the message —
 * which is exactly what the reference gives them, since it binds none of them
 * either. That is a wrong answer removed, not a route lost.
 *
 * **The bank booths nothing had bound.** 78 records say "Bank"; content bound
 * one. All 78 are bound now, which is what made the deletion possible and is why
 * `bank_import.py` is a generator with a `--check` rather than a hand list.
 */
/*
 * OPLOC<n>: p2 x, p2 z, p2 locId — doors, gates, stairs and ladders.
 *
 * Latches the interaction and walks to a tile beside the loc; the acting is
 * whatever content bound to [oploc<n>], and the message if nothing did.
 */
static void
handle_oploc(
    struct ToriRSServer* srv,
    int op_num,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int tile_x;
    int tile_z;
    int loc_id;
    int slot;
    struct ToriRSServerSceneLoc* loc;
    int size_x = 1;
    int size_z = 1;
    int resolved;
    const char* op;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    tile_x = rsab_g2(&buf);
    tile_z = rsab_g2(&buf);
    loc_id = rsab_g2(&buf);
    if( !rsab_ok(&buf) )
        return;

    ToriRSServer_WorldClearPendingAction(srv);

    /* The footprint decides what counts as "beside it": a two-tile gate is
     * reachable from tiles a one-tile door is not. The slot is also what the op
     * is validated against below, so it has to be found first. */
    slot = ToriRSServer_SceneFindLoc(tile_x, tile_z, srv->active_player->level, loc_id);
    loc = ToriRSServer_SceneLoc(slot);

    /*
     * LostCity OpLocHandler: validate ops against the multiloc-resolved child
     * before latching. Hidden / missing ops are a lagging client — drop.
     *
     * Judged against the PLACEMENT and not just the type, because
     * LOC_ADD_CHANGE_V2 lets one placement hide an op its loctype declares and
     * offer one it does not. A door that swung open and renamed its op to
     * "Close" sends a perfectly legitimate click that the type-only check
     * rejects, silently, as a lagging client.
     */
    resolved = ToriRSServer_LocResolveTransform(srv->active_player, loc_id);
    if( resolved < 0 )
        return;
    op = ToriRSServer_SceneLocPlacementOp(slot, resolved, op_num);
    if( !op || strcmp(op, "hidden") == 0 )
        return;
    if( loc )
    {
        tile_x = loc->x;
        tile_z = loc->z;
        size_x = loc->size_x > 0 ? loc->size_x : 1;
        size_z = loc->size_z > 0 ? loc->size_z : 1;
    }

    ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_LOC, op_num, -1, loc_id, tile_x,
                                  tile_z, srv->active_player->level, size_x, size_z);
    {
        struct CollisionApproach approach;
        ToriRSServer_SceneLocApproach(slot, &approach);
        ToriRSServer_WorldWalkToApproach(srv, tile_x, tile_z, &approach);
    }

    ToriRSServer_WorldProcessInteraction(srv);
}

/* ------------------------------------------------------------------ */
/* Use-on: the `*u` family                                             */
/* ------------------------------------------------------------------ */

/*
 * "Use A on B" — four packets the client has always sent and this server had no
 * route for, so they were dropped as unknown.
 *
 * The trap the whole family turns on: a use-on click carries **two** ids and
 * only one of them is the trigger's subject. The subject is the thing clicked
 * *on* — the loc, the npc, the ground obj — and the item is carried separately
 * in `last_useitem`/`last_useslot`. `Player.getOpTrigger`/`getApTrigger` read
 * `type.id` and `type.category` off `this.target`, which is the target entity;
 * the used obj appears nowhere in the lookup. Getting that backwards compiles,
 * runs, and works for any item that only ever has one target — which is why the
 * selftest uses two ids that are nowhere near each other and asserts *both*
 * directions.
 *
 * `opheldu` is the exception in every respect: both ends are items, there is
 * nothing to walk to, and its lookup is a bespoke four-rung chain that swaps the
 * two as it goes (`ToriRSServer_ScriptsRunOpheldu`).
 */

/**
 * Read the "used item" tail every `*u` packet ends with, and check it is real.
 *
 * `useObj`, `useSlot`, `useCom`. The reference resolves `useCom` to an inventory
 * through the player's own listener list and then asks that inventory whether
 * `useSlot` holds `useObj`; this server has no listener model, so the check is
 * against the backpack, which is the only container a use-on can come from here
 * and the same check `handle_opheld` already makes for its own slot.
 *
 * Returns 0 when the click describes something the player is not holding, which
 * is a lagged or lying client and not an error worth answering.
 */
static int
useon_tail(
    struct ToriRSServer* srv,
    struct RSAreaBuf* buf,
    int component_bytes,
    int* out_obj,
    int* out_slot)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int use_obj = rsab_g2(buf);
    int use_slot = rsab_g2(buf);
    int use_com = component_bytes == 4 ? rsab_g4(buf) : rsab_g2(buf);

    (void)use_com;
    if( !rsab_ok(buf) )
        return 0;
    if( use_slot < 0 || use_slot >= TORIRSSERVER_INV_SLOTS )
        return 0;
    if( player->inv[use_slot].obj_id != use_obj )
        return 0;
    *out_obj = use_obj;
    *out_slot = use_slot;
    return 1;
}

/*
 * OPHELDU: obj, slot, com, useObj, useSlot, useCom — "use item A on item B".
 *
 * Answered on the spot rather than queued as an interaction, because there is
 * nothing to walk to. `OpHeldUHandler` does the same: it validates, latches, and
 * runs the script inside the handler.
 *
 * The component is 4 bytes here and 2 on the other three, which is the client's
 * own asymmetry (`net_out_opheldu` writes `out_p_com`, `net_out_oplocu` writes
 * `p2`) and is why the decode is per-packet rather than shared.
 */
static void
handle_opheldu(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct RSAreaBuf buf;
    int obj_id;
    int slot;
    int component;
    int use_obj;
    int use_slot;
    const struct ToriRSServerObjInfo* info;
    const struct ToriRSServerObjInfo* use_info;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    obj_id = rsab_g2(&buf);
    slot = rsab_g2(&buf);
    component = rsab_g4(&buf);
    if( !rsab_ok(&buf) )
        return;
    if( slot < 0 || slot >= TORIRSSERVER_INV_SLOTS || player->inv[slot].obj_id != obj_id )
        return;
    if( !useon_tail(srv, &buf, 4, &use_obj, &use_slot) )
        return;

    info = ToriRSServer_ObjInfo(obj_id);
    use_info = ToriRSServer_ObjInfo(use_obj);
    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- OPHELDU obj=%d (%s) slot=%d com=%d|%d use=%d (%s) slot=%d\n",
                obj_id, info->name, slot, (component >> 16) & 0xffff, component & 0xffff, use_obj,
                use_info->name, use_slot);

    ToriRSServer_WorldClearPendingAction(srv);

    /* Latched before the dispatch, and in this order, because the dispatch
     * *swaps* them: `last_item` is whichever of the two the bound script is
     * named after by the time it runs. */
    player->last_item = obj_id;
    player->last_slot = slot;
    player->last_com = component;
    player->last_useitem = use_obj;
    player->last_useslot = use_slot;

    if( ToriRSServer_ScriptsRunOpheldu(srv, obj_id, info->category > 0 ? info->category : -1, use_obj,
                                    use_info->category > 0 ? use_info->category : -1) ==
        TORIRSSERVER_TRIGGER_NONE )
        ToriRSServer_Say(srv, "nothing_interesting_message", NULL);
}

/**
 * The shared tail of the three use-on packets that *are* interactions.
 *
 * They differ only in what they name as a target, so the latch, the walk and the
 * "act on arrival" are one function. `use_on` is set on the interaction rather
 * than passed to `ToriRSServer_WorldInteractionSet`, so the three existing op
 * handlers' call sites stay untouched — Phase 3 has to delete fallback calls out
 * of this file and a re-flowed signature would collide with that.
 */
static void
useon_interact(
    struct ToriRSServer* srv,
    enum ToriRSServerInteractionKind kind,
    int npc_slot,
    int target_id,
    int tile_x,
    int tile_z,
    int level,
    int size_x,
    int size_z,
    int use_obj,
    int use_slot)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    ToriRSServer_WorldClearPendingAction(srv);

    player->last_useitem = use_obj;
    player->last_useslot = use_slot;

    /* op 1 is a placeholder: `interaction_ap_trigger` ignores it once `use_on`
     * is set, because a use-on has no op number — "use this on that" is the
     * whole verb. */
    ToriRSServer_WorldInteractionSet(srv, kind, 1, npc_slot, target_id, tile_x, tile_z, level, size_x,
                                  size_z);
    player->interaction.use_on = 1;

    if( kind == TORIRSSERVER_INTERACT_OBJ )
    {
        struct CollisionApproach exact;
        ToriRSServer_SceneObjApproach(0, &exact);
        ToriRSServer_WorldWalkToApproach(srv, tile_x, tile_z, &exact);
        if( !player_has_waypoints(player) &&
            !ToriRSServer_SceneReached(player->level, player->x, player->z, tile_x, tile_z, &exact) )
        {
            struct CollisionApproach adj;
            ToriRSServer_SceneObjApproach(1, &adj);
            ToriRSServer_WorldWalkToApproach(srv, tile_x, tile_z, &adj);
        }
    }
    else if( kind == TORIRSSERVER_INTERACT_NPC )
    {
        struct CollisionApproach approach;
        ToriRSServer_SceneNpcApproach(size_x, &approach);
        ToriRSServer_WorldWalkToApproach(srv, tile_x, tile_z, &approach);
    }
    else
    {
        int loc_slot = ToriRSServer_SceneFindLoc(tile_x, tile_z, level, target_id);
        struct CollisionApproach approach;
        ToriRSServer_SceneLocApproach(loc_slot, &approach);
        ToriRSServer_WorldWalkToApproach(srv, tile_x, tile_z, &approach);
    }

    ToriRSServer_WorldProcessInteraction(srv);
}

/*
 * The cast twin of `useon_interact`: latch the target, walk to it, and let the
 * arrival resolve to the `t` trigger family.
 *
 * Everything about the walk is identical — a cast is an interaction like any
 * other — so the only difference is which field the interaction carries.
 * `use_on` names an item and leaves the trigger keyed by the target;
 * `spell` names the spell's component and makes the SPELL the trigger's
 * subject, which is what `[apnpct,magic_spellbook:wind_strike]` matches on.
 * See ToriRSServerInteraction.spell and interaction_ap_trigger, which already knew
 * how to route this and had no caller from the wire.
 *
 * No check that the target advertises anything: an npc does not have to offer
 * an option to be castable at, and the reference tests only that the spell
 * component is non-null (the same rule `p_opnpct` follows).
 */
static void
spell_interact(
    struct ToriRSServer* srv,
    enum ToriRSServerInteractionKind kind,
    int npc_slot,
    int target_id,
    int tile_x,
    int tile_z,
    int level,
    int size_x,
    int size_z,
    int spell_component)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    if( spell_component <= 0 )
        return;

    ToriRSServer_WorldClearPendingAction(srv);

    /* op 1 is a placeholder, as in useon_interact: once `spell` is set
     * `interaction_ap_trigger` ignores the op number entirely. */
    ToriRSServer_WorldInteractionSet(srv, kind, 1, npc_slot, target_id, tile_x, tile_z, level, size_x,
                                  size_z);
    player->interaction.spell = spell_component;

    if( kind == TORIRSSERVER_INTERACT_OBJ )
    {
        struct CollisionApproach exact;
        ToriRSServer_SceneObjApproach(0, &exact);
        ToriRSServer_WorldWalkToApproach(srv, tile_x, tile_z, &exact);
        if( !player_has_waypoints(player) &&
            !ToriRSServer_SceneReached(player->level, player->x, player->z, tile_x, tile_z, &exact) )
        {
            struct CollisionApproach adj;
            ToriRSServer_SceneObjApproach(1, &adj);
            ToriRSServer_WorldWalkToApproach(srv, tile_x, tile_z, &adj);
        }
    }
    else if( kind == TORIRSSERVER_INTERACT_NPC || kind == TORIRSSERVER_INTERACT_PLAYER )
    {
        struct CollisionApproach approach;
        ToriRSServer_SceneNpcApproach(size_x, &approach);
        ToriRSServer_WorldWalkToApproach(srv, tile_x, tile_z, &approach);
    }
    else
    {
        int loc_slot = ToriRSServer_SceneFindLoc(tile_x, tile_z, level, target_id);
        struct CollisionApproach approach;
        ToriRSServer_SceneLocApproach(loc_slot, &approach);
        ToriRSServer_WorldWalkToApproach(srv, tile_x, tile_z, &approach);
    }

    ToriRSServer_WorldProcessInteraction(srv);
}

/* The `p4 spellComponent` tail every targeted-cast body ends with. Four bytes,
 * unlike the use-on tail's two, because the component IS the trigger subject
 * here — see put_spell_tail in mock239_inbound.c. */
static int
spell_tail(
    struct RSAreaBuf* buf,
    int* out_spell)
{
    int spell = rsab_g4(buf);
    if( !rsab_ok(buf) || spell <= 0 )
        return 0;
    *out_spell = spell;
    return 1;
}

/* OPLOCT: p2 x, p2 z, p2 locId, p4 spellComponent. */
static void
handle_oploct(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int tile_x;
    int tile_z;
    int loc_id;
    int spell;
    int slot;
    struct ToriRSServerSceneLoc* loc;
    int size_x = 1;
    int size_z = 1;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    tile_x = rsab_g2(&buf);
    tile_z = rsab_g2(&buf);
    loc_id = rsab_g2(&buf);
    if( !rsab_ok(&buf) || !spell_tail(&buf, &spell) )
        return;

    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- OPLOCT loc=%d at %d,%d spell=%d|%d\n", loc_id, tile_x, tile_z,
                (spell >> 16) & 0xffff, spell & 0xffff);

    slot = ToriRSServer_SceneFindLoc(tile_x, tile_z, srv->active_player->level, loc_id);
    loc = ToriRSServer_SceneLoc(slot);
    if( loc )
    {
        tile_x = loc->x;
        tile_z = loc->z;
        size_x = loc->size_x > 0 ? loc->size_x : 1;
        size_z = loc->size_z > 0 ? loc->size_z : 1;
    }
    spell_interact(srv, TORIRSSERVER_INTERACT_LOC, -1, loc_id, tile_x, tile_z,
                   srv->active_player->level, size_x, size_z, spell);
}

/* OPNPCT: p2 npcSlot, p4 spellComponent. */
static void
handle_opnpct(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int slot;
    int spell;
    struct ToriRSServerNpc* npc;
    const struct ToriRSServerNpcInfo* info;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    /*
     * The client names the npc by what WE called it (ToriRSServerPlayerSlotMap), so
     * this is a translation and not a cast. Using the wire value as a pool
     * index is how every npc became unclickable: the name resolved to a
     * different npc, the walk went to that one's tile, and the player was told
     * "I can't reach that" while standing on top of the one they clicked.
     */
    slot = ToriRSServer_SlotMapWorld(srv->active_player, rsab_g2(&buf));
    if( !rsab_ok(&buf) || slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return;
    if( !spell_tail(&buf, &spell) )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active )
        return;

    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- OPNPCT slot=%d type=%d spell=%d|%d\n", slot, npc->type,
                (spell >> 16) & 0xffff, spell & 0xffff);

    info = ToriRSServer_NpcInfo(npc->type);
    spell_interact(srv, TORIRSSERVER_INTERACT_NPC, slot, npc->type, npc->x, npc->z, npc->level,
                   info->size, info->size, spell);

    /* Same reason as OPNPCU: idle roaming stays parked until the response has
     * had time to show, and the npc may not have survived the dispatch. */
    if( npc->active )
        npc->next_roam_tick = srv->tick + 8;
}

/* OPOBJT: p2 x, p2 z, p2 objId, p4 spellComponent. */
static void
handle_opobjt(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int tile_x;
    int tile_z;
    int obj_id;
    int spell;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    tile_x = rsab_g2(&buf);
    tile_z = rsab_g2(&buf);
    obj_id = rsab_g2(&buf);
    if( !rsab_ok(&buf) || !spell_tail(&buf, &spell) )
        return;

    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- OPOBJT obj=%d at %d,%d spell=%d|%d\n", obj_id, tile_x, tile_z,
                (spell >> 16) & 0xffff, spell & 0xffff);

    spell_interact(srv, TORIRSSERVER_INTERACT_OBJ, -1, obj_id, tile_x, tile_z,
                   srv->active_player->level, 1, 1, spell);
}

/* OPPLAYERT: p2 player pid, p4 spellComponent.  Revision 239's inbound
 * adapter has decoded this shape since the targeted-cast family landed, but
 * the world route used to omit it entirely. */
static void
handle_opplayert(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int pid;
    int spell;
    int slot = -1;
    struct ToriRSServerPlayer* target;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    pid = rsab_g2(&buf);
    if( !rsab_ok(&buf) || !spell_tail(&buf, &spell) )
        return;

    for( int i = 0; i < srv->player_count; i++ )
    {
        if( srv->players[i].active && srv->players[i].pid == pid )
        {
            slot = i;
            break;
        }
    }
    if( slot < 0 || &srv->players[slot] == srv->active_player )
        return;
    target = &srv->players[slot];

    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- OPPLAYERT pid=%d spell=%d|%d\n", pid,
                (spell >> 16) & 0xffff, spell & 0xffff);

    spell_interact(srv, TORIRSSERVER_INTERACT_PLAYER, slot, target->pid, target->x,
                   target->z, target->level, 1, 1, spell);
}

/*
 * OPHELDT: p2 obj, p2 slot, p4 itemComponent, p4 spellComponent — "cast this
 * spell on that inventory item" (High Alchemy, Superheat, the enchants).
 *
 * Answered on the spot rather than queued, exactly as OPHELDU is and for the
 * same reason: the item is already in the player's hand, so there is nothing to
 * walk to. The trigger's subject is the SPELL — `[opheldt,magic_spellbook:
 * high_alchemy]`, one script per spell — and the item travels in `last_item`,
 * which is how the content reads it (`@magic_spell_high_alch(^highlvl_alchemy,
 * last_item)`).
 */
static void
handle_opheldt(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct RSAreaBuf buf;
    int obj_id;
    int slot;
    int component;
    int spell;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    obj_id = rsab_g2(&buf);
    slot = rsab_g2(&buf);
    component = rsab_g4(&buf);
    if( !rsab_ok(&buf) || !spell_tail(&buf, &spell) )
        return;
    if( slot < 0 || slot >= TORIRSSERVER_INV_SLOTS || player->inv[slot].obj_id != obj_id )
        return;

    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- OPHELDT obj=%d (%s) slot=%d com=%d|%d spell=%d|%d\n", obj_id,
                ToriRSServer_ObjInfo(obj_id)->name, slot, (component >> 16) & 0xffff,
                component & 0xffff, (spell >> 16) & 0xffff, spell & 0xffff);

    ToriRSServer_WorldClearPendingAction(srv);

    player->last_item = obj_id;
    player->last_slot = slot;
    player->last_com = component;

    if( ToriRSServer_ScriptsRunSpellTrigger(srv, SS_TRIGGER_OPHELDT, spell, -1, -1, -1) ==
        TORIRSSERVER_TRIGGER_NONE )
        ToriRSServer_Say(srv, "nothing_interesting_message", NULL);
}

/* OPLOCU: p2 x, p2 z, p2 locId, then the used-item tail. */
static void
handle_oplocu(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int tile_x;
    int tile_z;
    int loc_id;
    int use_obj;
    int use_slot;
    int slot;
    struct ToriRSServerSceneLoc* loc;
    int size_x = 1;
    int size_z = 1;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    tile_x = rsab_g2(&buf);
    tile_z = rsab_g2(&buf);
    loc_id = rsab_g2(&buf);
    if( !rsab_ok(&buf) )
        return;
    if( !useon_tail(srv, &buf, 2, &use_obj, &use_slot) )
        return;

    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- OPLOCU loc=%d at %d,%d use=%d slot=%d\n", loc_id, tile_x,
                tile_z, use_obj, use_slot);

    /* The footprint decides what counts as "beside it", exactly as OPLOC<n>. */
    slot = ToriRSServer_SceneFindLoc(tile_x, tile_z, srv->active_player->level, loc_id);
    loc = ToriRSServer_SceneLoc(slot);
    if( loc )
    {
        tile_x = loc->x;
        tile_z = loc->z;
        size_x = loc->size_x > 0 ? loc->size_x : 1;
        size_z = loc->size_z > 0 ? loc->size_z : 1;
    }

    useon_interact(srv, TORIRSSERVER_INTERACT_LOC, -1, loc_id, tile_x, tile_z,
                   srv->active_player->level, size_x, size_z, use_obj, use_slot);
}

/* OPNPCU: p2 npcSlot, then the used-item tail. */
static void
handle_opnpcu(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int slot;
    int use_obj;
    int use_slot;
    struct ToriRSServerNpc* npc;
    const struct ToriRSServerNpcInfo* info;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    /*
     * The client names the npc by what WE called it (ToriRSServerPlayerSlotMap), so
     * this is a translation and not a cast. Using the wire value as a pool
     * index is how every npc became unclickable: the name resolved to a
     * different npc, the walk went to that one's tile, and the player was told
     * "I can't reach that" while standing on top of the one they clicked.
     */
    slot = ToriRSServer_SlotMapWorld(srv->active_player, rsab_g2(&buf));
    if( !rsab_ok(&buf) || slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return;
    if( !useon_tail(srv, &buf, 2, &use_obj, &use_slot) )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active )
        return;

    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- OPNPCU slot=%d type=%d use=%d slot=%d\n", slot, npc->type,
                use_obj, use_slot);

    info = ToriRSServer_NpcInfo(npc->type);
    useon_interact(srv, TORIRSSERVER_INTERACT_NPC, slot, npc->type, npc->x, npc->z, npc->level,
                   info->size, info->size, use_obj, use_slot);

    /* Idle roaming resumes only after the response has had time to show, as it
     * does for OPNPC<n>. Set after the dispatch because the script may have
     * despawned the npc. */
    if( npc->active )
        npc->next_roam_tick = srv->tick + 8;
}

/* OPOBJU: p2 x, p2 z, p2 objId, then the used-item tail. */
static void
handle_opobju(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int tile_x;
    int tile_z;
    int obj_id;
    int use_obj;
    int use_slot;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    tile_x = rsab_g2(&buf);
    tile_z = rsab_g2(&buf);
    obj_id = rsab_g2(&buf);
    if( !rsab_ok(&buf) )
        return;
    if( !useon_tail(srv, &buf, 2, &use_obj, &use_slot) )
        return;

    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- OPOBJU obj=%d at %d,%d use=%d slot=%d\n", obj_id, tile_x,
                tile_z, use_obj, use_slot);

    useon_interact(srv, TORIRSSERVER_INTERACT_OBJ, -1, obj_id, tile_x, tile_z,
                   srv->active_player->level, 1, 1, use_obj, use_slot);
}

/*
 * `opplayeru` / `applayeru` are deliberately absent, and this is the reason.
 *
 * rev-230 assigns no OPPLAYERU wire opcode — `src/net/rev/osrs230/packetout.h`
 * has rows for OPHELDU, OPNPCU, OPLOCU and OPOBJU and none for the player form,
 * so `net_out_opplayeru` cannot encode anything this revision could carry. There
 * is no packet to route. Five of the reference's 546 uses are the two player
 * forms; the other 541 are here.
 *
 * It also means the one place the reference overrides the trigger's subject with
 * the *used* obj rather than the target — `setInteraction(..., APPLAYERU, useObj)`
 * feeding `targetSubject.com`, which is why `[applayeru,rotten_tomato]` names the
 * thrown item — has no call site here. That asymmetry must not be generalised to
 * the other four: for locs, npcs and ground objs the subject is the target.
 */

/* ------------------------------------------------------------------ */
/* `::give` / `::spawn` — an id from the spelling a human has          */
/* ------------------------------------------------------------------ */

/*
 * A display name in the shape a command line can carry: lowercase, one `_` per
 * run of anything that is not a letter or a digit, no leading or trailing one.
 *
 * "Scythe of Vitur" -> `scythe_of_vitur`, "Ring of dueling(8)" ->
 * `ring_of_dueling_8`. That is deliberately the same shape the cache's own
 * gamevals use, which is what lets one spelling reach an obj through either
 * table: the pack when the cache names it, this when it does not.
 */
static void
obj_name_underscore(
    char* out,
    size_t size,
    const char* in)
{
    size_t written = 0;

    if( size == 0 )
        return;
    assert(out);
    for( ; in && *in && written + 1 < size; in++ )
    {
        unsigned char c = (unsigned char)*in;

        if( isalnum(c) )
            out[written++] = (char)tolower(c);
        else if( written > 0 && out[written - 1] != '_' )
            out[written++] = '_';
    }
    while( written > 0 && out[written - 1] == '_' )
        written--;
    out[written] = '\0';
}

/*
 * The display name a cheat argument is matched against, or NULL when the record
 * is not something a human would be naming.
 *
 * One per namespace, because the "this row is not a real record" test is not the
 * same question in both: an obj answers it through `known` plus the `null` every
 * note record carries, an npc through the name gate `ToriRSServer_NpcInfo` already
 * applies (`ToriRSServer_NpcInfoKnown`) plus the same `null`.
 */
static const char*
cheat_obj_display_name(int id)
{
    const struct ToriRSServerObjInfo* info = ToriRSServer_ObjInfo(id);

    /* Every note record is named `null` and every placeholder is unnamed;
     * neither is what a human typing an item name is asking for. */
    if( !info->known || !info->name || strcmp(info->name, "null") == 0 )
        return NULL;
    return info->name;
}

static const char*
cheat_npc_display_name(int id)
{
    const struct ToriRSServerNpcInfo* info;

    /* The multinpc instances are nameless and `ToriRSServer_NpcInfo` would hand back
     * its "Someone" placeholder for every one of them — which would make one
     * spelling match thousands of ids. Ask the ungated question first. */
    if( !ToriRSServer_NpcInfoKnown(id) )
        return NULL;
    info = ToriRSServer_NpcInfo(id);
    if( !info->name || strcmp(info->name, "null") == 0 )
        return NULL;
    return info->name;
}

/*
 * The id a `::give`/`::spawn` argument means in one namespace, or -1.
 *
 * Four ways in, most specific first, because a cheat that guesses is worse than
 * one that refuses:
 *
 *   1. a plain number — `::give 995 1000` still has to work, and `::item` is
 *      the only other way to name an id;
 *   2. the cache's own gameval (`configs/all.obj.compack`), which is already
 *      underscored — `scythe_of_vitur` is 22325 there;
 *   3. the *display* name underscored, so the command still resolves against a
 *      cache whose gamevals were never packed;
 *   4. a unique substring of a gameval. Unique is the whole rule: two matches
 *      is an ambiguous request, and the caller gets the candidates to choose
 *      from rather than whichever one the scan reached first.
 *
 * `suggest` is filled only on the ambiguous and empty cases (2..N candidates,
 * or none), so a miss can say *why* it missed. NULL when the caller does not
 * want one.
 *
 * Parameterised over the namespace rather than written twice: `::spawn` asks
 * exactly this question about npcs, and the interesting part is the ladder, not
 * which table it walks. `record_count` is the exclusive bound on rung 3's scan
 * and `display_name` its accessor.
 */
static int
cheat_id_from_name(
    enum ToriRSServerPackKind kind,
    int record_count,
    const char* (*display_name)(int id),
    const char* arg,
    char* suggest,
    size_t suggest_size)
{
    char wanted[128];
    int pack_count;
    int match = -1;
    int matches = 0;
    char* end = NULL;
    long numeric;

    if( suggest && suggest_size )
        suggest[0] = '\0';
    if( !arg || !arg[0] )
        return -1;

    numeric = strtol(arg, &end, 10);
    if( end && end != arg && *end == '\0' )
        return (int)numeric;

    obj_name_underscore(wanted, sizeof(wanted), arg);
    if( !wanted[0] )
        return -1;

    match = ToriRSServer_ContentSymbol(kind, wanted);
    if( match >= 0 )
        return match;

    for( int id = 0; id < record_count; id++ )
    {
        const char* name = display_name(id);
        char have[128];

        if( !name )
            continue;
        obj_name_underscore(have, sizeof(have), name);
        if( strcmp(have, wanted) == 0 )
            return id;
    }

    /* Nothing exact. Substring over the gamevals, and the answer is only an
     * answer when exactly one name carries it. */
    pack_count = ToriRSServer_ContentSymbolWalk(kind, -1, NULL, NULL);
    for( int i = 0; i < pack_count; i++ )
    {
        int id = -1;
        const char* name = NULL;

        if( !ToriRSServer_ContentSymbolWalk(kind, i, &id, &name) || !name )
            continue;
        if( !strstr(name, wanted) )
            continue;
        matches++;
        if( match < 0 )
            match = id;
        if( suggest && suggest_size && matches <= 6 )
        {
            size_t used = strlen(suggest);

            snprintf(suggest + used, suggest_size - used, "%s%s",
                     used ? ", " : "", name);
        }
    }
    /* Say how many were left out. A list that stops at six reads as the whole
     * answer, and "which of these six" is a different question from "which of
     * these two hundred" — the second means narrow the search, not choose. */
    if( matches > 6 && suggest && suggest_size )
    {
        size_t used = strlen(suggest);

        snprintf(suggest + used, suggest_size - used, " (+%d more)", matches - 6);
    }
    if( matches == 1 )
    {
        if( suggest && suggest_size )
            suggest[0] = '\0';
        return match;
    }
    return -1;
}

/** The obj a `::give` argument means, or -1. */
int
cheat_obj_from_name(
    const char* arg,
    char* suggest,
    size_t suggest_size)
{
    return cheat_id_from_name(TORIRSSERVER_PACK_OBJ, ToriRSServer_ObjInfoCount(),
                              cheat_obj_display_name, arg, suggest, suggest_size);
}

/** How many npcs one `::spawn` may place. A cheat's argument is typed, so it is
 *  also mistyped, and filling the npc pool looks like a world where nothing
 *  spawns rather than like a typo. */
#define TORIRSSERVER_CHEAT_SPAWN_MAX 20

/** The npc type a `::spawn` argument means, or -1. */
int
cheat_npc_from_name(
    const char* arg,
    char* suggest,
    size_t suggest_size)
{
    return cheat_id_from_name(TORIRSSERVER_PACK_NPC, ToriRSServer_NpcInfoCount(),
                              cheat_npc_display_name, arg, suggest, suggest_size);
}

/*
 * The sailing boat template lives in map square m60_99 -- the off-map staging
 * region this cache authors hulls in, at tiles x 3840..3903, z 6336..6399.
 * A whole three-deck ship (`boatkit_deck_straight01`, `boatkit_shiphull_*`,
 * `boatkit_mast_*`, `boatkit_helm01`, cannons, ladders) occupies zones
 * (5..6, 5..7) on planes 0..2 -- the 16x24 tiles starting here.
 *
 * Found by decoding every `.jl2` in the content tree against the 400
 * `sailing_boat_*` loc ids: of the 696 map squares that mention sailing at
 * all, only m60_99 and m60_100 carry hulls, and only m60_99 carries an
 * assembled ship rather than a rack of bare hull models. This is why the deck
 * no longer comes from Lumbridge castle: there IS a boat-shaped map template,
 * it is simply not on the playable grid.
 */
#define VESSEL_DECK_TEMPLATE_X 3880
#define VESSEL_DECK_TEMPLATE_Z 6376

/**
 * Point every zone of a vessel's deck at the footprint starting at
 * `src_x`,`src_z` -- one source zone per deck zone, on every plane.
 *
 * Per-zone and not one repeated chunk, because a ship is not a tiling: the bow
 * sits in one source zone and the stern in another, and aiming all six deck
 * zones at the same source builds six identical midships slabs. All four
 * planes because the entity's own world is where the hull (plane 0), the main
 * deck (plane 1) and the quarterdeck (plane 2) are authored, and which of them
 * a client draws is `WevConfig.plane`'s answer, not this function's.
 */
static void
vessel_deck_fill_from(
    struct ToriRSServerVessel* vessel,
    int src_x,
    int src_z)
{
    int zones_x = 0;
    int zones_z = 0;

    assert(vessel);
    assert(src_x >= 0);
    assert(src_z >= 0);

    ToriRSServer_VesselDeckZones(vessel, &zones_x, &zones_z);
    for( int level = 0; level < TORIRSSERVER_MAPINSTANCE_LEVELS; level++ )
        for( int zx = 0; zx < zones_x; zx++ )
            for( int zz = 0; zz < zones_z; zz++ )
                ToriRSServer_MapInstanceSetchunk(
                    vessel->instance,
                    level,
                    zx,
                    zz,
                    src_x + zx * 8,
                    src_z + zz * 8,
                    level,
                    0);
}

/*
 * Server commands, so a session can be steered without a UI.
 *
 * `::~name` is the cache-independent spelling. The pristine revision-239
 * client strips the leading `::`, offers `~name` to its local typed-emote
 * parser (which cannot mistake it for `cry`, `run`, etc.), then sends `~name`
 * through CLIENT_CHEAT. Strip that marker once here, before both the content
 * debugproc namespace and the C diagnostic ladder. Plain `::name` remains an
 * alias for clients whose cache has exact local-command matching.
 *
 * Full reason this boundary exists: docs/CRYSTAL_SET_COMMAND.md.
 */
void
handle_cheat(
    struct ToriRSServer* srv,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct RSAreaBuf buf;
    char text[128];
    int obj_id = 0;
    int count = 1;
    int tile_x = 0;
    int tile_z = 0;
    int npc_type = 0;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    /* net_out_client_cheat writes the body newline-terminated, without the
     * leading "::". */
    rsab_gjstr(&buf, text, sizeof(text), RSAB_JSTR_NEWLINE);

    /* `::~foo` arrives as `~foo`: the client owns/removes `::`, while `~` is
     * the explicit server-side namespace escape. memmove includes the NUL. */
    if( text[0] == '~' )
        memmove(text, text + 1, strlen(text));

    /*
     * Content first, exactly as `[if_button]` is dispatched: a `[debugproc,
     * <name>]` in the tree claims the line before any branch below sees it.
     *
     * That order is what makes a cheat writable without touching the engine,
     * and it is the reference's own — LostCity has no C-side cheat that a
     * debugproc could not replace, and everything it ships as content is one.
     * `::pray` used to be a branch here; it is
     * skill_prayer/scripts/cheat_prayer.rs2 now, and it toggles a prayer
     * through the same proc the prayer book's button does.
     */
    /*
     * Always logged, not gated on verbose.
     *
     * Once a cheat reaches the server, no matching `[debugproc]`, a completed
     * one, and an aborted one must be different outcomes. A line the client
     * consumed locally is a fourth case and deliberately produces no server
     * log; packet telemetry is what distinguishes that boundary. This result
     * makes every server-side outcome explicit rather than letting an abort
     * masquerade as an unknown command.
     *
     * If ::crystal_set ever appears to Cry or stays silent, use
     * ::~crystal_set with a pristine cache; do not start debugging here:
     * docs/CRYSTAL_SET_COMMAND.md records the client-local interception and the
     * exact packet boundary that proves whether this function ran.
     */
    {
        enum ToriRSServerTriggerResult result = ToriRSServer_ScriptsRunDebugproc(srv, text);

        fprintf(stderr, "torirsserver: cheat '%s' -> debugproc %s\n",
                text,
                result == TORIRSSERVER_TRIGGER_RAN
                    ? "ran"
                    : result == TORIRSSERVER_TRIGGER_FAILED ? "FAILED" : "not found");
        if( result == TORIRSSERVER_TRIGGER_FAILED )
        {
            say(srv, "Command ::~%s failed — see the server log.", text);
            return;
        }
        if( result == TORIRSSERVER_TRIGGER_RAN )
            return;
    }

    if( strncmp(text, "talk", 4) == 0 )
    {
        /*
         * `::talk <slot|name> [op]` fires [opnpc<op>] on an npc without needing
         * a right-click, so every trigger is drivable from a headless session.
         *
         * The name form is what makes it usable: the roster is built from the
         * map squares in whatever order the walk finds them, so a slot number is
         * a different npc between runs and after any content change. `::talk
         * hans 1` means the same thing every time.
         */
        int slot = -1;
        int op_num = 1;
        char name[64] = { 0 };

        if( sscanf(text, "talk %63s %d", name, &op_num) >= 1 && name[0] &&
            !(name[0] >= '0' && name[0] <= '9') )
        {
            int type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, name);

            for( int i = 0; i < TORIRSSERVER_NPC_MAX && slot < 0; i++ )
            {
                if( srv->npcs[i].active && srv->npcs[i].type == type )
                    slot = i;
            }
            if( slot < 0 )
            {
                say(srv, "No `%s` in the world.", name);
                return;
            }
            say(srv, "Talking to %s (slot %d).", ToriRSServer_NpcInfo(srv->npcs[slot].type)->name, slot);
        }
        else
        {
            slot = 0;
            (void)sscanf(text, "talk %d %d", &slot, &op_num);
        }
        if( op_num < 1 || op_num > 5 )
            op_num = 1;
        if( slot >= 0 && slot < TORIRSSERVER_NPC_MAX && srv->npcs[slot].active )
        {
            /* `::talk` is a diagnostic, so it reports both misses distinctly:
             * nothing bound, versus a script that was bound and blew up. Not a
             * fallback — it is the cheat telling its user what happened. */
            switch( ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1 + (op_num - 1),
                                                srv->npcs[slot].type, -1, slot) )
            {
            case TORIRSSERVER_TRIGGER_NONE:
                say(srv, "npc %d has no [opnpc%d] script.", srv->npcs[slot].type, op_num);
                break;
            case TORIRSSERVER_TRIGGER_FAILED:
                say(srv, "npc %d's [opnpc%d] script failed — see the log.",
                    srv->npcs[slot].type, op_num);
                break;
            default:
                break;
            }
        }
        return;
    }

    /* `::equip <slot>` was here and calling `equip_from_slot`. It is
     * `[debugproc,equip]` in general/scripts/misc/cheat_equip.rs2 now — it went
     * with `equip_from_slot`, being one of its four callers, and the cheat
     * handler was already offering the line to a debugproc first, so this
     * branch had been unreachable since that file landed. */

    if( strncmp(text, "setting ", 8) == 0 )
    {
        /*
         * `::setting <varbit> <value>` — the client mirroring an All Settings row.
         *
         * Not a debug command despite the channel it rides on. Ten rows of the
         * All Settings > Activities category are decided HERE and read a varbit
         * whose base is an ordinary server varp; the panel writes the client's
         * own copy and nothing in revision 239 carries that write to a server.
         * See `settings_mirror_varbit` in `src/game/rs_cs2_host.h` for the whole
         * of why, including why CLIENT_CHEAT is the transport rather than a new
         * opcode.
         *
         * Silent on success. This fires on every toggle of every settings row,
         * and a chat line per checkbox would bury the messages the player asked
         * for -- which is also why it is not routed through `say`. It is not
         * silent on a REFUSAL: a varbit this server cannot write is a mirror the
         * player will never see the effect of, and that is worth a line.
         */
        int varbit_id = -1;
        int value = 0;

        if( sscanf(text, "setting %d %d", &varbit_id, &value) != 2 || varbit_id < 0 )
        {
            say(srv, "setting: expected ::setting <varbit> <value>.");
            return;
        }
        if( ToriRSServer_VarbitSet(srv, varbit_id, value) < 0 )
        {
            say(srv, "setting: varbit %d is not in this cache.", varbit_id);
            return;
        }
        if( getenv("TORIRSSERVER_SETTINGS_DEBUG") )
            fprintf(stderr, "setting: varbit %d = %d (player %s)\n", varbit_id, value,
                    srv->active_player ? srv->active_player->display_name : "?");
        return;
    }

    if( strncmp(text, "style", 5) == 0 )
    {
        /*
         * `::style <0-3>` sets the attack style.
         *
         * The combat tab is the real way in, and it is not wired: at rev 230
         * the style lives in varp 43, which the tab's CS2 writes on a button
         * click — and the mock has no server-side handler for that interface's
         * buttons. Rather than pretend, the style is settable here and the varp
         * is mirrored so the tab renders the right selection.
         */
        int style = 0;

        (void)sscanf(text, "style %d", &style);
        if( style < 0 || style > 3 )
            style = 0;
        ToriRSServer_WorldSetAttackStyle(srv, style);
        say(srv, "Attack style: %s.",
            style == TORIRSSERVER_STYLE_ACCURATE      ? "accurate"
            : style == TORIRSSERVER_STYLE_AGGRESSIVE  ? "aggressive"
            : style == TORIRSSERVER_STYLE_DEFENSIVE   ? "defensive"
                                                 : "controlled");
        return;
    }

    if( strncmp(text, "setlevel", 8) == 0 )
    {
        /* `::setlevel <stat> <level>` — accept either the pack name (`magic`,
         * `summoning`) or the diagnostic numeric id.  Resolving through the
         * stat pack keeps game-facing names and ids out of this engine seam.
         * XP is set to the threshold for that level so base survives logout
         * (LostCity setLevel). */
        char stat_arg[64] = { 0 };
        int stat = -1;
        int level = 1;

        if( sscanf(text, "setlevel %63s %d", stat_arg, &level) == 2 )
        {
            char* end = NULL;
            long numeric = strtol(stat_arg, &end, 10);

            if( end && end != stat_arg && *end == '\0' )
                stat = (int)numeric;
            else
                stat = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_STAT, stat_arg);
        }
        if( stat >= 0 && stat < TORIRSSERVER_STAT_COUNT && level >= 1 && level <= 99 )
        {
            ToriRSServer_CombatSetLevel(player, stat, level);
            say(srv, "Set stat %d to %d.", stat, level);
        }
        return;
    }

    if( strncmp(text, "equipstats", 10) == 0 )
    {
        /* `::equipstats` opens the bonus screen without walking the sidebar —
         * "View equipment stats" is two clicks deep on the worn tab. */
        ToriRSServer_EquipmentOpenStats(srv);
        return;
    }

    if( strncmp(text, "run", 3) == 0 )
    {
        /* `::run [0|1]` — the run toggle without a keyboard. The client sets
         * it with ctrl held on a move request, which the headless harness has
         * no way to synthesise. */
        int want = !player->run_toggle;
        (void)sscanf(text, "run %d", &want);
        player->run_toggle = want != 0;
        ToriRSServer_WorldSetVarp(srv, ToriRSServer_WorldVarp("option_run"), player->run_toggle);
        say(srv, "Run %s (%d%%).", player->run_toggle ? "on" : "off",
            player->run_energy * 100 / TORIRSSERVER_RUN_ENERGY_MAX);
        return;
    }

    if( strncmp(text, "god", 3) == 0 )
    {
        /* `::god [0|1]` — debug invulnerability, gated in the one player damage
         * funnel (ToriRSServer_CombatHitPlayer) and against hitpoint drains from
         * the stat opcodes (godmode_blocks_stat_write). Its reason for existing
         * is measurement: profiling a live encounter means standing in it and
         * moving around, and without this the death sequence teleports the
         * player out and rebuilds the scene partway through every run, which
         * shows up in a frame-time trace as work that has nothing to do with
         * what was being measured. */
        int want = !player->godmode;
        (void)sscanf(text, "god %d", &want);
        player->godmode = want != 0;
        /* Top up on the way in, so a run does not start on a sliver of health
         * left over from before the flag was set. */
        if( player->godmode )
        {
            player->hitpoints = player->max_hitpoints;
            ToriRSServer_CombatSyncHitpoints(player);
            player->masks |= TORIRSSERVER_PMASK_DAMAGE;
        }
        say(srv, "God mode %s.", player->godmode ? "on" : "off");
        return;
    }

    if( strncmp(text, "bank", 4) == 0 )
    {
        /* `::bank` opens the bank without walking to a booth — the Lumbridge
         * ones are on the castle's top floor, two staircases from the spawn
         * tile, which is a long way to go to check a packet. */
        ToriRSServer_BankOpen(srv);
        return;
    }

    if( strncmp(text, "fight", 5) == 0 )
    {
        /*
         * `::fight [slot]` engages an npc without needing a right-click, so
         * combat is drivable from a headless session.
         *
         * With no slot it picks the nearest attackable one. A bare slot number
         * is a poor harness argument: the roster is built from the map squares
         * in whatever order the walk finds them, so slot 0 is wherever it
         * happens to be — usually across Lumbridge, sometimes a shopkeeper.
         * "The thing in front of me" is what a headless run actually means.
         */
        int slot = -1;

        if( sscanf(text, "fight %d", &slot) != 1 )
        {
            int best = -1;

            for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
            {
                struct ToriRSServerNpc* npc = &srv->npcs[i];
                int dx;
                int dz;
                int distance;

                if( !npc->active || npc->death_tick >= 0 || npc->level != player->level )
                    continue;
                if( !ToriRSServer_CombatAttackable(npc->type) )
                    continue;
                dx = npc->x > player->x ? npc->x - player->x : player->x - npc->x;
                dz = npc->z > player->z ? npc->z - player->z : player->z - npc->z;
                distance = dx > dz ? dx : dz;
                if( best < 0 || distance < best )
                {
                    best = distance;
                    slot = i;
                }
            }
            if( slot < 0 )
            {
                say(srv, "No attackable npc in the world.");
                return;
            }
            say(srv, "Attacking %s (slot %d, %d tiles).", ToriRSServer_NpcInfo(srv->npcs[slot].type)->name,
                slot, best);
        }
        ToriRSServer_CombatEngage(srv, slot);
        return;
    }

    if( strncmp(text, "useon", 5) == 0 )
    {
        /*
         * `::useon <a> <b>` — "use A on B", where B is the one clicked second,
         * the way the OPHELDU body orders them.
         *
         * The only other way to reach the four-rung resolver is a real client
         * with both items in the backpack, which is why the resolver could
         * swallow a whole family of pairs unnoticed: every skill selftest calls
         * its recipe `~proc` directly and never dispatches at all. Both items
         * are given first if absent, because "did this pair dispatch" is the
         * question and "do I happen to be holding them" is not.
         *
         * Reversing the arguments is a different test, not the same one — rung 2
         * is what makes one binding answer either order, so a pair that works one
         * way round and not the other is the interesting result. Run it both ways.
         */
        char arg_a[64] = { 0 };
        char arg_b[64] = { 0 };
        char suggest[256] = { 0 };
        int a;
        int b;
        int slot_a = -1;
        int slot_b = -1;
        struct ToriRSServerContainer* row;

        if( sscanf(text, "useon %63s %63s", arg_a, arg_b) != 2 )
        {
            say(srv, "Usage: ::useon <item_a> <item_b>   (b is the one clicked second)");
            return;
        }
        a = cheat_obj_from_name(arg_a, suggest, sizeof(suggest));
        if( a < 0 )
        {
            if( suggest[0] )
                say(srv, "Which %s? %s", arg_a, suggest);
            else
                say(srv, "No item named '%s'.", arg_a);
            return;
        }
        suggest[0] = '\0';
        b = cheat_obj_from_name(arg_b, suggest, sizeof(suggest));
        if( b < 0 )
        {
            if( suggest[0] )
                say(srv, "Which %s? %s", arg_b, suggest);
            else
                say(srv, "No item named '%s'.", arg_b);
            return;
        }

        row = ToriRSServer_ContainerResolve(srv, player, ToriRSServer_Ids()->inv_backpack);
        if( !row )
        {
            say(srv, "No backpack container.");
            return;
        }
        for( int i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
        {
            if( player->inv[i].obj_id == a && slot_a < 0 )
                slot_a = i;
            else if( player->inv[i].obj_id == b && slot_b < 0 )
                slot_b = i;
        }
        if( slot_a < 0 || slot_b < 0 )
        {
            if( slot_a < 0 )
                ToriRSServer_ContainerAdd(row, a, 1, 0);
            if( slot_b < 0 )
                ToriRSServer_ContainerAdd(row, b, 1, 0);
            slot_a = slot_b = -1;
            for( int i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
            {
                if( player->inv[i].obj_id == a && slot_a < 0 )
                    slot_a = i;
                else if( player->inv[i].obj_id == b && slot_b < 0 )
                    slot_b = i;
            }
        }
        if( slot_a < 0 || slot_b < 0 )
        {
            say(srv, "No room to hold both items.");
            return;
        }

        {
            uint8_t body[16];
            struct RSAreaBuf out;

            rsab_wrap(&out, body, sizeof(body));
            rsab_p2(&out, b);
            rsab_p2(&out, slot_b);
            rsab_p4(&out, 0);
            rsab_p2(&out, a);
            rsab_p2(&out, slot_a);
            rsab_p4(&out, 0);
            fprintf(stderr, "torirsserver: ::useon %s(%d,slot %d) on %s(%d,slot %d)\n", arg_a, a,
                    slot_a, arg_b, b, slot_b);
            ToriRSServer_WorldHandle(player, PKTOUT_NAME_OPHELDU, body, (int)rsab_len(&out));
        }
        return;
    }

    if( strncmp(text, "give", 4) == 0 )
    {
        /*
         * `::give <name> [count]` — any item in the game, by the name a player
         * would say it out loud, underscored: `::give scythe_of_vitur`,
         * `::give coins 100000`, `::give rune_platebody 2`.
         *
         * `::item` is the same act keyed on a number, and a number is not
         * something anyone has: an obj id has to be looked up in a table before
         * it can be typed, which is the step this removes. It stays because a
         * harness that already knows the id should not be made to spell it.
         *
         * The add goes through `ToriRSServer_ContainerAdd` — the same
         * `Inventory.add` the `inv_add` opcode uses — so a stackable merges
         * onto the stack already held, an unstackable takes one slot per unit,
         * and the backpack's listener sends an UPDATE_INV without this branch
         * knowing a packet exists. The one-at-a-time loop this used to need (the
         * shared add put a whole count in one slot) went with that gap.
         */
        char arg[64] = { 0 };
        char suggest[256] = { 0 };
        int want = 1;
        int given = 0;
        struct ToriRSServerContainer* row;
        const struct ToriRSServerObjInfo* info;

        if( sscanf(text, "give %63s %d", arg, &want) < 1 )
        {
            say(srv, "Usage: ::give <item_name> [count]");
            return;
        }
        if( want < 1 )
            want = 1;

        obj_id = cheat_obj_from_name(arg, suggest, sizeof(suggest));
        if( obj_id < 0 )
        {
            if( suggest[0] )
                say(srv, "Which %s? %s", arg, suggest);
            else
                say(srv, "No item named '%s'.", arg);
            return;
        }
        info = ToriRSServer_ObjInfo(obj_id);
        if( !info->known )
        {
            say(srv, "Obj %d ('%s') has no record in this cache.", obj_id, arg);
            return;
        }

        row = ToriRSServer_ContainerResolve(srv, player, ToriRSServer_Ids()->inv_backpack);
        if( !row )
        {
            say(srv, "No backpack container.");
            return;
        }
        given = ToriRSServer_ContainerAdd(row, obj_id, want, 0);
        if( given <= 0 )
            say(srv, "No room for %s.", info->name);
        else if( given < want )
            say(srv, "Gave %d x %s (%d), %d did not fit.", given, info->name, obj_id,
                want - given);
        else
            say(srv, "Gave %d x %s (%d).", given, info->name, obj_id);
        return;
    }

    if( strncmp(text, "spawn", 5) == 0 )
    {
        /*
         * `::spawn <npc_name|id> [count]` — `::give` for npcs, and the same
         * argument ladder resolves it (`cheat_npc_from_name`): a bare id, the
         * cache's gameval, the display name underscored, then a unique
         * substring. `::spawn goblin`, `::spawn 3028`, `::spawn hill_giant 3`.
         *
         * `::npc <id>` below is the id-only ancestor and stays for the harnesses
         * that already spell one — the same relationship `::item` has to
         * `::give`. It also spawns onto level 3 rather than the player's, which
         * is a bug nobody hit because nobody spawns onto a plane they cannot
         * see; this one uses the player's level.
         *
         * The count is capped rather than trusted. An npc pool that is full
         * behaves like a world where nothing spawns, and a typo'd `::spawn
         * goblin 10000` is a long way from the branch it broke.
         */
        char arg[64] = { 0 };
        char suggest[256] = { 0 };
        int want = 1;
        int spawned = 0;
        int type;
        const struct ToriRSServerNpcInfo* info;

        if( sscanf(text, "spawn %63s %d", arg, &want) < 1 )
        {
            say(srv, "Usage: ::spawn <npc_name> [count]");
            return;
        }
        if( want < 1 )
            want = 1;
        if( want > TORIRSSERVER_CHEAT_SPAWN_MAX )
            want = TORIRSSERVER_CHEAT_SPAWN_MAX;

        type = cheat_npc_from_name(arg, suggest, sizeof(suggest));
        if( type < 0 )
        {
            if( suggest[0] )
                say(srv, "Which %s? %s", arg, suggest);
            else
                say(srv, "No npc named '%s'.", arg);
            return;
        }
        info = ToriRSServer_NpcInfo(type);
        if( !ToriRSServer_NpcInfoKnown(type) )
        {
            /* Not a refusal: a spawn is allowed to name an npc this cache does
             * not describe (npc_spawn gives it the default config block), and a
             * content-only id is exactly that. Say so and carry on. */
            say(srv, "Npc %d ('%s') has no record in this cache; spawning anyway.",
                type, arg);
        }

        /* Side by side rather than stacked, so a count of three is three things
         * a player can see and click, and each takes its own collision. */
        for( int i = 0; i < want; i++ )
        {
            int slot =
                npc_spawn(srv, type, player->x + 1 + i, player->z + 1, player->level);
            if( slot < 0 )
                break;
            /* One instance, not a fixture: `::spawn` is a debugging `npc_add`,
             * so its npc is the command's the way a script's is the script's and
             * killing it is the end of it. A memset slot is the *world's*
             * (`EntityLifeCycle.RESPAWN` — see the field), which would stand
             * every test spawn back up `respawnrate` ticks later at the tile the
             * tester was standing on. */
            srv->npcs[slot].despawns_on_death = 1;
            spawned++;
        }
        if( spawned <= 0 )
            say(srv, "Could not spawn npc %d.", type);
        else
            say(srv, "Spawned %d x %s (%d) at %d,%d.", spawned,
                ToriRSServer_NpcInfoKnown(type) ? info->name : arg, type, player->x + 1,
                player->z + 1);
        return;
    }

    /*
     * The sailing debug trio (docs/SAILING_PLAN.md S2.5).
     *
     * These exist to be driven from `[net:boot] cheat=` on a cold login with
     * nothing else set up, so each one finds what it needs rather than being
     * handed it: `::vesselspawn` picks the tile, stamps the water, sources the
     * deck and builds the instance; `::vesselsail` and `::vesselboard` find the
     * hull by taking the lowest live handle when no argument names one.
     *
     * Before the `item %d %d` / `tele %d %d` sscanf fallbacks below because
     * those match on shape rather than on a name, and a mistyped vessel
     * command should say so rather than fall through to a teleport.
     */
    if( strncmp(text, "vesselgoto", 10) == 0 )
    {
        /*
         * `::vesselgoto <x> <z> [level]` — teleport, under a name content does
         * not own.
         *
         * `::tele` is claimed by the content pack's `[debugproc,tele]`, which
         * takes a `level,mx,mz,lx,lz` coordinate and answers the packet before
         * the C ladder below ever sees it — so a capture harness driving
         * `[net:boot] cheat=` cannot reach the C `tele` branch at all, and the
         * failure is silent: the debugproc "ran", and the player did not move.
         * Absolute tiles here because that is what `::vesselwater` prints and
         * what `::vesselspawnat` takes, and a capture that has to convert
         * between two coordinate systems gets one of them wrong.
         */
        int to_x = -1;
        int to_z = -1;
        int to_level = player->level;

        if( sscanf(text, "vesselgoto %d %d %d", &to_x, &to_z, &to_level) < 2 )
        {
            say(srv, "Usage: ::vesselgoto <x> <z> [level]");
            return;
        }
        ToriRSServer_WorldTeleport(srv, to_level, to_x, to_z);
        fprintf(
            stderr, "vesselgoto: player at %d,%d level %d\n", player->x, player->z,
            player->level);
        say(srv, "Teleported to %d,%d level %d.", to_x, to_z, to_level);
        return;
    }

    if( strncmp(text, "vesselwater", 11) == 0 )
    {
        /*
         * `::vesselwater [radius]` — print the sailable tiles around the
         * caller as ASCII, one line per row, z descending so the picture reads
         * like the world map (north at the top).
         *
         * Finding real water is otherwise guesswork. Only the map's BLOCK
         * setting becomes COLL_FLAG_FLOOR, and no screenshot can tell "the
         * hull is floating on the sea" apart from "the hull is parked on grass
         * that `::vesselspawn` stamped sailable" — so a capture of a boat on
         * water has to be aimed at tiles the CACHE calls water, and this is
         * how those are found. It reads the built scene, which is the only
         * place collision exists at all.
         */
        int radius = 24;

        sscanf(text, "vesselwater %d", &radius);
        if( radius < 1 )
            radius = 1;
        /* The built scene is 104 tiles wide. Past its edge SceneTileFlags reads
         * 0 and every tile prints as land, which is a lie rather than a map. */
        if( radius > 50 )
            radius = 50;

        fprintf(
            stderr,
            "vesselwater: level %d centre %d,%d radius %d "
            "('~' sailable, '.' not, '@' the caller)\n",
            player->level, player->x, player->z, radius);
        for( int dz = radius; dz >= -radius; dz-- )
        {
            char row[128];
            int n = 0;

            for( int dx = -radius; dx <= radius; dx++ )
            {
                int sailable = ToriRSServer_VesselTileSailable(
                    player->level, player->x + dx, player->z + dz);

                row[n++] = (dx == 0 && dz == 0) ? '@' : (sailable ? '~' : '.');
            }
            row[n] = '\0';
            fprintf(stderr, "vesselwater: %5d %s\n", player->z + dz, row);
        }
        say(srv, "Sailability around %d,%d dumped to stderr.", player->x, player->z);
        return;
    }

    /*
     * Before the `vesselspawn` branch below, which matches on an 11-character
     * prefix and would otherwise swallow this name and read its arguments as
     * a size pair.
     */
    if( strncmp(text, "vesselspawnat", 13) == 0 )
    {
        /*
         * `::vesselspawnat <x> <z> [size_x] [size_z] [config]` — the same hull
         * as `::vesselspawn`, at an absolute tile, and with NO water stamp.
         *
         * That omission is the whole point. `::vesselspawn` makes its own
         * water, so the hull it places is sailable by construction and sits on
         * whatever the map happens to draw there — grass, in Lumbridge. A
         * capture of a boat at SEA has to put the hull over tiles the cache
         * already calls water, and stamping any of them would make the picture
         * unfalsifiable. `::vesselwater` finds those tiles; this puts a hull on
         * them and reports what the collision map says about the result, so
         * the log records whether the water under a screenshot was real.
         */
        int at_x = -1;
        int at_z = -1;
        int size_x = 16;
        int size_z = 24;
        int config_id = 9;
        int src_x = VESSEL_DECK_TEMPLATE_X;
        int src_z = VESSEL_DECK_TEMPLATE_Z;
        /*
         * Initial yaw in 2048-space, and the reason this argument exists.
         *
         * A hull spawns pointing south and only `vesselsail` ever turns it —
         * at `turn_rate` units a tick, toward a heading, and then it sails off
         * on that heading. So there was no way to photograph the same boat at
         * a chosen angle: filming the turn gives you whichever yaws happened to
         * fall on a captured frame, and the hull leaves the frame while you
         * wait. Spawning AT an angle makes each of the sixteen compass
         * headings a separate, repeatable still.
         *
         * Sixteen hulls at once would be the obvious alternative and it does
         * not fit: the wire publishes 15 world-view ids (struct
         * ToriRSServerVessel::view_id), so the sixteenth boat would be spawned,
         * sailable, and invisible.
         */
        int angle = 0;
        int handle;
        struct ToriRSServerVessel* vessel;
        int zones_x = 0;
        int zones_z = 0;
        int base_tile_x = 0;
        int base_tile_z = 0;

        if( sscanf(
                text, "vesselspawnat %d %d %d %d %d %d %d %d", &at_x, &at_z, &size_x,
                &size_z, &config_id, &src_x, &src_z, &angle) < 2 )
        {
            say(srv,
                "Usage: ::vesselspawnat <x> <z> [size_x] [size_z] [config] "
                "[src_x] [src_z] [angle 0-2047]");
            return;
        }
        if( size_x < 1 )
            size_x = 1;
        if( size_z < 1 )
            size_z = 1;
        if( size_x > 96 )
            size_x = 96;
        if( size_z > 96 )
            size_z = 96;
        /*
         * Wrap rather than clamp: yaw is cyclic, so -128 and 2048 are the two
         * spellings a caller stepping a compass round is most likely to reach,
         * and clamping either would silently photograph the wrong boat.
         */
        angle %= TORIRSSERVER_VESSEL_ANGLE_UNITS;
        if( angle < 0 )
            angle += TORIRSSERVER_VESSEL_ANGLE_UNITS;

        handle = ToriRSServer_VesselSpawn(
            srv, config_id, size_x, size_z, player->level, at_x, at_z, angle);
        if( handle == 0 )
        {
            say(srv, "No deck instance free — the map-instance pool is full.");
            return;
        }
        vessel = ToriRSServer_VesselGet(srv, handle);
        if( vessel->view_id == 0 )
        {
            say(srv, "Vessel %d spawned, but all 15 world-view ids are taken; "
                     "it will not appear on any client.",
                handle);
            return;
        }

        ToriRSServer_VesselDeckZones(vessel, &zones_x, &zones_z);
        ToriRSServer_MapInstanceBase(vessel->instance, &base_tile_x, &base_tile_z);
        vessel_deck_fill_from(vessel, src_x, src_z);
        ToriRSServer_MapInstanceBuild(vessel->instance);
        ToriRSServer_WorldMapInstanceBuilt(srv, vessel->instance);

        fprintf(
            stderr,
            "vesselspawnat: vessel %d view %d at %d,%d level %d angle %d "
            "(heading %d/16); deck %dx%d zones from %d,%d; centre tile "
            "sailable=%d (unstamped)\n",
            handle, vessel->view_id, at_x, at_z, player->level, vessel->angle,
            vessel->angle / TORIRSSERVER_VESSEL_HEADING_STEP, zones_x, zones_z,
            src_x, src_z,
            ToriRSServer_VesselTileSailable(player->level, at_x, at_z));
        say(srv, "Vessel %d (config %d, view %d) at %d,%d angle %d; deck %d,%d.",
            handle, config_id, vessel->view_id, at_x, at_z, vessel->angle,
            base_tile_x, base_tile_z);
        return;
    }

    if( strncmp(text, "vesselspawn", 11) == 0 )
    {
        /*
         * `::vesselspawn [size_x] [size_z] [config] [src_x] [src_z]` — a
         * hull beside the player, on stamped water, with a real deck under it.
         *
         * Config 9 is "The Zenith" and is a real archive-72 record in
         * cache.osrs239 (src/world/test/wev_test.c pins its name and its
         * "Board" op). The client asserts the id is in its config table, so a
         * made-up default here would abort the client rather than draw a boat.
         *
         * The deck is sourced from the ship template at
         * VESSEL_DECK_TEMPLATE_X,_Z, and the default size is the template's own
         * 16x24. These sizes are TILES; the wire's size nibbles are ZONES, so
         * `2 3` asks for a 2x3-TILE hull under an 8x8-tile client deck box —
         * the mismatch that made an earlier capture look like a floating slab.
         * Whole-zone sizes keep the two in agreement.
         */
        int size_x = 16;
        int size_z = 24;
        int config_id = 9;
        int src_x = VESSEL_DECK_TEMPLATE_X;
        int src_z = VESSEL_DECK_TEMPLATE_Z;
        int tile_x;
        int tile_z;
        int handle;
        struct ToriRSServerVessel* vessel;
        int zones_x = 0;
        int zones_z = 0;
        int base_tile_x = 0;
        int base_tile_z = 0;

        sscanf(
            text, "vesselspawn %d %d %d %d %d", &size_x, &size_z, &config_id, &src_x,
            &src_z);
        if( size_x < 1 )
            size_x = 1;
        if( size_z < 1 )
            size_z = 1;
        /* The wire's size nibbles are zone counts; 15 zones is the widest view
         * an id can name, and the client's descriptor grid is narrower still. */
        if( size_x > 96 )
            size_x = 96;
        if( size_z > 96 )
            size_z = 96;

        tile_x = player->x + 3;
        tile_z = player->z + 3;

        /*
         * Water, stamped. In this cache only rivers and harbours carry the
         * BLOCK setting that becomes COLL_FLAG_FLOOR — open ground reads a
         * flag word of zero, which `ToriRSServer_VesselTileSailable` correctly
         * refuses. Without a patch under the hull the mover's very first step
         * is blocked and `::vesselsail` produces no deltas at all, which is
         * the failure this command exists to avoid.
         *
         * The patch REPLACES the tile's collision rather than adding floor to
         * it. Lumbridge is fences, walls and hedges: OR-ing FLOOR onto a tile
         * that already carries COLL_FLAG_LOC leaves it blocked for every
         * mover, so the hull turned on the spot and parked on its second tick
         * against a loc bit it had supposedly just flooded.
         */
        {
            struct CollisionMap* cm = ToriRSServer_SceneCollision(player->level);
            int base_x = ToriRSServer_SceneBaseX();
            int base_z = ToriRSServer_SceneBaseZ();

            if( cm && base_x >= 0 )
                for( int dx = -size_x - 8; dx <= size_x + 8; dx++ )
                    for( int dz = -size_z - 8; dz <= size_z + 8; dz++ )
                        collision_map_set_water(
                            cm, tile_x + dx - base_x, tile_z + dz - base_z);
        }

        handle = ToriRSServer_VesselSpawn(
            srv, config_id, size_x, size_z, player->level, tile_x, tile_z, 0);
        if( handle == 0 )
        {
            say(srv, "No deck instance free — the map-instance pool is full.");
            return;
        }
        vessel = ToriRSServer_VesselGet(srv, handle);
        if( vessel->view_id == 0 )
        {
            say(srv, "Vessel %d spawned, but all 15 world-view ids are taken; "
                     "it will not appear on any client.",
                handle);
            return;
        }

        /* The deck's terrain, one template zone per deck zone. An unset zone
         * decodes to 0 = void and the client draws nothing there, so a deck
         * bigger than the template still gets filled — it just repeats the
         * template's far edge past the ship's stern. */
        ToriRSServer_VesselDeckZones(vessel, &zones_x, &zones_z);
        ToriRSServer_MapInstanceBase(vessel->instance, &base_tile_x, &base_tile_z);
        vessel_deck_fill_from(vessel, src_x, src_z);
        ToriRSServer_MapInstanceBuild(vessel->instance);
        ToriRSServer_WorldMapInstanceBuilt(srv, vessel->instance);

        say(srv, "Vessel %d (config %d, view %d) at %d,%d; deck %d,%d.", handle,
            config_id, vessel->view_id, tile_x, tile_z, base_tile_x, base_tile_z);
        return;
    }

    if( strncmp(text, "vesselsail", 10) == 0 )
    {
        /*
         * `::vesselsail [heading] [tier] [handle]` — put a hull under way so
         * the op-2 delta path runs.
         *
         * Tier 1 by default (64 fine units, half a tile per tick): slow enough
         * that the client's 30-cycle interpolator is visibly interpolating
         * rather than snapping, which is the thing a capture is filming.
         */
        int heading = 4;
        int tier = 1;
        int handle = 0;
        struct ToriRSServerVessel* vessel;

        sscanf(text, "vesselsail %d %d %d", &heading, &tier, &handle);
        vessel = handle > 0 ? ToriRSServer_VesselGet(srv, handle)
                            : ToriRSServer_VesselByView(srv, 1);
        if( !vessel )
        {
            say(srv, "No such vessel. ::vesselspawn first.");
            return;
        }
        ToriRSServer_VesselSetHeading(vessel, heading & 15);
        ToriRSServer_VesselSetSpeed(vessel, tier < 1 ? 1 : (tier > 4 ? 4 : tier));
        say(srv, "Vessel %d sailing heading %d at tier %d.", vessel->index,
            heading & 15, tier);
        return;
    }

    if( strncmp(text, "vesselboard", 11) == 0 )
    {
        /*
         * `::vesselboard [handle]` — stand the caller on the deck.
         *
         * A deck tile is an ordinary absolute tile inside the instance's
         * reservation, so boarding is a plain teleport: nothing about the
         * player becomes special, and `ToriRSServer_VesselAtTile` answers "is
         * this player aboard" from the pool afterwards. The projection that
         * makes the shore see them is recomputed by
         * `ToriRSServer_WorldRefreshObservation` on the next tick.
         */
        int handle = 0;
        struct ToriRSServerVessel* vessel;
        int base_tile_x = 0;
        int base_tile_z = 0;

        sscanf(text, "vesselboard %d", &handle);
        vessel = handle > 0 ? ToriRSServer_VesselGet(srv, handle)
                            : ToriRSServer_VesselByView(srv, 1);
        if( !vessel )
        {
            say(srv, "No such vessel. ::vesselspawn first.");
            return;
        }
        if( !ToriRSServer_MapInstanceBase(vessel->instance, &base_tile_x, &base_tile_z) )
        {
            say(srv, "Vessel %d has no deck instance.", vessel->index);
            return;
        }
        ToriRSServer_WorldTeleport(
            srv, 0, base_tile_x + vessel->size_x_tiles / 2,
            base_tile_z + vessel->size_z_tiles / 2);
        say(srv, "Boarded vessel %d at %d,%d.", vessel->index,
            base_tile_x + vessel->size_x_tiles / 2,
            base_tile_z + vessel->size_z_tiles / 2);
        return;
    }

    if( sscanf(text, "item %d %d", &obj_id, &count) >= 1 )
    {
        int slot = inv_first_free(player);
        /* `::give` clamps its count the same way. A typed 0 means the typist
         * wants one of the thing, not an empty cell announced as "Spawned". */
        if( count < 1 )
            count = 1;
        if( slot >= 0 )
        {
            inv_set(player, slot, obj_id, count);
            say(srv, "Spawned %s.", ToriRSServer_ObjInfo(obj_id)->name);
        }
        return;
    }
    if( sscanf(text, "tele %d %d", &tile_x, &tile_z) == 2 )
    {
        ToriRSServer_WorldTeleport(srv, player->level, tile_x, tile_z);
        say(srv, "Teleported to %d,%d.", tile_x, tile_z);
        return;
    }
    if( sscanf(text, "npc %d", &npc_type) == 1 )
    {
        int slot = npc_spawn(srv, npc_type, player->x + 1, player->z + 1, 3);
        /* The id-only ancestor of `::spawn`, and one instance for the same
         * reason — see there. */
        if( slot >= 0 )
            srv->npcs[slot].despawns_on_death = 1;
        say(srv, "Spawned npc %d.", npc_type);
        return;
    }
    say(srv, "Unknown command: %s", text);
}

void
ToriRSServer_WorldTeleport(
    struct ToriRSServer* srv,
    int level,
    int abs_x,
    int abs_z)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    steps_clear(player);
    player->level = level;
    player->x = abs_x;
    player->z = abs_z;
    player->dest_x = -1;
    player->dest_z = -1;
    /* The next PLAYER_INFO has to carry an absolute placement, and the scene
     * around the new tile has to be built before anything reads collision from
     * it — a teleport past the rebuild margin is the common case, not the edge
     * one, when the destination came off a world map click. */
    player->place_dirty = 1;
    maybe_rebuild(srv);
}

/* ------------------------------------------------------------------ */
/* Inbound dispatch                                                    */
/* ------------------------------------------------------------------ */

/*
 * One handler per packet, and a table naming them.
 *
 * This was a 240-line `switch` with half its bodies written inline, which made
 * `ToriRSServer_WorldHandle` the function every new packet had to be threaded
 * through and the monolith's single widest merge target. A table costs one line
 * per packet and puts each body somewhere with a name.
 *
 * Every handler takes `name` even when it does not need it, because the
 * numbered families (`OPHELD1..5`, `OPNPC1..5`, `IF_BUTTON1..10`) derive their
 * op index from it — one entry per opcode in the table, one handler for the
 * family.
 */

static void
handle_move_gameclick(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_move(srv, payload, len, 0);
}

/* The minimap variant carries a 14-byte trailer the game click does not. */
static void
handle_move_minimapclick(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_move(srv, payload, len, 14);
}

static void
handle_opheld_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    /* INV_BUTTON<n> carries the same (obj, slot, component) triple as OPHELD.
     * Which of the two an inventory row produces depends on whether the menu
     * entry came from the objtype's ops or from the component's, and at rev 230
     * the gameframe's CS2 inventory script routes them through the component —
     * so both have to mean the same thing here or nothing is ever equipped. */
    int base = (name >= PKTOUT_NAME_INV_BUTTON1 && name <= PKTOUT_NAME_INV_BUTTON5)
                   ? PKTOUT_NAME_INV_BUTTON1
                   : PKTOUT_NAME_OPHELD1;

    handle_opheld(srv, name - base + 1, payload, len);
}

static void
handle_inv_buttond_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_inv_buttond(srv, payload, len);
}

static void
handle_opnpc_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    handle_opnpc(srv, name - PKTOUT_NAME_OPNPC1 + 1, payload, len);
}

static void
handle_oploc_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    handle_oploc(srv, name - PKTOUT_NAME_OPLOC1 + 1, payload, len);
}

static void
handle_opobj_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    handle_opobj(srv, name - PKTOUT_NAME_OPOBJ1 + 1, payload, len);
}

/* The four use-on packets. One name each — there is no op number to derive. */
static void
handle_opheldu_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_opheldu(srv, payload, len);
}

static void
handle_oplocu_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_oplocu(srv, payload, len);
}

static void
handle_opnpcu_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_opnpcu(srv, payload, len);
}

static void
handle_opobju_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_opobju(srv, payload, len);
}

/* The four targeted-cast packets, the `t` family. One name each, for the same
 * reason as the use-on four: a cast carries no op number. */
static void
handle_opheldt_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_opheldt(srv, payload, len);
}

static void
handle_oploct_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_oploct(srv, payload, len);
}

static void
handle_opnpct_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_opnpct(srv, payload, len);
}

static void
handle_opobjt_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_opobjt(srv, payload, len);
}

static void
handle_opplayert_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_opplayert(srv, payload, len);
}

static void
handle_cheat_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_cheat(srv, payload, len);
}

/* The client sends the packed (interface << 16) | child uid at full width
 * (GameProtoRevTable.component_id_bytes); truncating it would make every
 * interface's component 5 look alike. */
static void
handle_resume_pausebutton(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf resume;
    int uid;

    (void)name;
    rsab_wrap(&resume, (void*)payload, (size_t)len);
    uid = rsab_g4(&resume);
    int sub = -1;
    if( len >= 6 )
    {
        sub = rsab_g2(&resume);
        if( sub == 0xffff )
            sub = -1;
    }
    if( !rsab_ok(&resume) )
        return;
    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- RESUME_PAUSEBUTTON %d:%d sub=%d\n", uid >> 16,
                uid & 0xffff, sub);

    /* Revision 239 sends a dynamic IF3 child as uid(parent) + sub.  Server
     * scripts consume the row through last_slot before p_pausebutton resumes,
     * exactly as the IF_BUTTON1 path does.  Portal Nexus rows are zero-based,
     * so preserve both their parent component and row zero explicitly. */
    {
        int nexus_main = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_COMPONENT, "telenexus_teleport:key_listeners");
        int nexus_extra = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_COMPONENT, "telenexus_teleport:extra_key_listeners");

        if( uid == nexus_main || uid == nexus_extra )
        {
            srv->active_player->last_com = uid;
            srv->active_player->last_slot = sub;
        }
        else if( sub > 0 )
            srv->active_player->last_slot = sub;
    }
    if( ToriRSServer_ScriptsResumeButton(srv, uid) )
        return;

    /*
     * An unresolved resume is still a click, and content may own it.
     *
     * IF3 panels built by the cache's own clientscripts choose
     * RESUME_PAUSEBUTTON for rows they draw themselves — the Tombs of Amascut
     * party interface toggles an invocation by resuming a pause-button on a
     * child of `toa_partydetails:pausebuttons` whose SUB ID is the row, and no
     * server script is parked on it. Before this the packet decoded, matched no
     * parked script, and was dropped: the panel drew the raid level and could
     * not change it.
     *
     * `last_com`/`last_slot` are set first for the same reason the IF_BUTTON1
     * path sets them — the row is the whole message — and the world-map router
     * stays last, so nothing that used to reach it stops.
     */
    /*
     * The world-map router goes FIRST, and that order is a fix rather than a
     * preference: its close X is a component content also binds, so putting
     * content ahead of it swallowed the click and left the overlay mounted -
     * which is the exact defect the router was added to cure. `handle_if_button`
     * has resolved it in this same order since.
     */
    if( ToriRSServer_WorldMapHandleButton(srv, uid, 1) )
        return;
    srv->active_player->last_com = uid;
    if( !ToriRSServer_ScriptsFallback(srv, TORIRSSERVER_FALLBACK_IF_BUTTON,
                                  ToriRSServer_ScriptsRunIfButton(srv, uid, 0)) )
        return;

    /*
     * IF3 onOp listeners may choose RESUME_PAUSEBUTTON even for a component
     * which is not currently parking a server script.  The rev-239 world-map
     * close X (595:38) and its escape target (595:4) do exactly that: the
     * packet arrived and decoded correctly, but treating a failed resume as a
     * terminal no-op left the overlay mounted forever.  Give the same
     * world-map router used by IF_BUTTON/IF_BUTTON1 the unresolved resume.
     */
    (void)ToriRSServer_WorldMapHandleButton(srv, uid, 1);
}

static void
handle_if_button(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf button;
    int uid;

    (void)name;
    rsab_wrap(&button, (void*)payload, (size_t)len);
    uid = rsab_g4(&button);
    if( !rsab_ok(&button) )
        return;
    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- IF_BUTTON %d:%d\n", uid >> 16, uid & 0xffff);

    /* rev 230 has no separate resume opcode in practice — a component the
     * server enabled with IF_SETEVENTS answers a click with IF_BUTTON, and
     * the server decides what it meant. Try it as a resume first; a click
     * that matches no registered button falls through, which is how sidebar
     * tabs (switched client-side on a varc) stay a no-op. */
    if( ToriRSServer_ScriptsResumeButton(srv, uid) )
        return;

    /* The world map's close buttons carry no cache op, so a click on the
     * red X arrives here rather than as IF_BUTTON1. */
    if( ToriRSServer_WorldMapHandleButton(srv, uid, 1) )
        return;

    /* Content bound to the component wins. The bank's router is what is left,
     * and it is a declared fallback now rather than "what happens otherwise":
     * it used to keep the bank's toggles working with no script pack at all,
     * which is precisely the second implementation this stopped being. */
    srv->active_player->last_com = uid;
    srv->active_player->last_slot = -1;
    /* Op 0 — this is the op-*less* click (events bit 0), not op 1. It has no
     * numbered trigger to try, so the lookup goes straight to `[if_button,…]`. */
    if( ToriRSServer_ScriptsFallback(srv, TORIRSSERVER_FALLBACK_IF_BUTTON,
                                 ToriRSServer_ScriptsRunIfButton(srv, uid, 0)) )
        ToriRSServer_BankHandleButton(srv, uid, -1, -1, 1);
}

/*
 * IF_BUTTON1..10: op N of an IF3 component (RSProt If3Button). Distinct from
 * IF_BUTTON above, which is the op-less plain click — an op-bearing widget like
 * the world map orb names which verb was picked, and the verb is the whole
 * message ("Floating World Map" and "Fullscreen World Map" are ops 2 and 3 of
 * the same component).
 */
void
handle_if_button_op(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf button;
    int uid;
    int sub;
    int op_num = name - PKTOUT_NAME_IF_BUTTON1 + 1;

    rsab_wrap(&button, (void*)payload, (size_t)len);
    uid = rsab_g4(&button);
    sub = (int16_t)rsab_g2(&button);
    if( !rsab_ok(&button) )
        return;
    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- IF_BUTTON%d %d:%d sub=%d\n", op_num, uid >> 16,
                uid & 0xffff, sub);
    if( ToriRSServer_WorldMapHandleButton(srv, uid, op_num) )
        return;
    srv->active_player->last_com = uid;
    srv->active_player->last_slot = sub;
    srv->active_player->last_verb = op_num;
    ToriRSServer_TeleNexusStageClick(srv, uid, sub, op_num);

    /* The rev-239 Display dropdown has three rows, but WINDOW_STATUS does not:
     * the golden client writes 1 for fixed and 2 for either resizable layout
     * (Statics.method5862 -> method10079).  The dynamic IF op is therefore the
     * authoritative callback which distinguishes Classic from Modern.  Latch
     * it before content runs ~gameframe_set_mode, so the WINDOW_STATUS emitted
     * by its echoed clientscript cannot undo the selected resizable root. */
    if( op_num == 1 && sub >= 1 && sub <= 3 )
    {
        int layout_buttons = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_COMPONENT,
            "settings_side:display_dynamic_setting_1_buttons");
        if( layout_buttons > 0 && uid == layout_buttons )
        {
            srv->active_player->client_layout_mode = sub - 1;
            /* This row is authoritative and immediate; a choice still sitting
             * in the All Settings latch below is older and must not outrank
             * it on the WINDOW_STATUS that follows. */
            srv->active_player->settings_dropdown_choice = -1;
        }
    }

    /*
     * The same problem in the All Settings panel (interface 134), whose layout
     * row has one dropdown list shared with every other row on the page —
     * `settings:dropdown_buttons`, rebuilt per row by clientscript 9114 as
     * three dynamic children per choice with `Select` on the third. So the op
     * names the chosen INDEX and nothing else; whether the open dropdown was
     * the layout one is not on the wire.
     *
     * What settles it is WINDOW_STATUS: this client sends that packet from
     * exactly one place, the drain of a client-layout change (src/main.c), and
     * the All Settings layout row is the only thing in that panel which raises
     * it. So the index is only latched here, and handle_window_status is what
     * decides it meant a layout — every other dropdown selection latches a
     * value that the next selection overwrites and nothing ever reads.
     */
    if( op_num == 1 && sub >= 2 && (sub - 2) % 3 == 0 )
    {
        int dropdown = ToriRSServer_ContentSymbol(
            TORIRSSERVER_PACK_COMPONENT, "settings:dropdown_buttons");
        if( dropdown > 0 && uid == dropdown )
            srv->active_player->settings_dropdown_choice = (sub - 2) / 3;
    }
    /*
     * Latch first, then resume — same order as LostCity's IfButtonHandler.
     * Choice menus (`~p_choice*`) park on p_pausebutton with chatmenu:options
     * registered; the row is the sub-id in last_slot. Without the resume here,
     * IF_BUTTON1 alone would set last_slot and leave the script parked, so a
     * later resume with last_slot still 0 would always take p_choice's last
     * option (Leela's "escape equipment" line, and every other final else).
     *
     * Sub 0 is the chatmenu title (and other non-row children share the armed
     * range). Resuming those made every ~p_choice* take its last option.
     * Leave the script parked; content also re-opens on last_slot outside 1..N.
     * sub < 0 means a static component (no dynamic child) — continue prompts.
     */
    if( sub == 0 )
    {
        struct ToriRSServerPlayer* player = srv->active_player;
        for( int i = 0; i < player->resume_button_count; i++ )
        {
            if( player->resume_buttons[i] == uid )
                return;
        }
    }
    if( ToriRSServer_ScriptsResumeButton(srv, uid) )
        return;
    /*
     * The component uid is the trigger's subject; an interface button has
     * neither a category nor an npc. `sub` reaches content through last_slot,
     * which is where a RuneScript trigger reads it.
     *
     * The op index does not, and cannot: there is no `last_verb` command in
     * the reference and none here either, so `player->last_verb` is written
     * and read by nothing. The op index reaches content as the *trigger* —
     * `[if_button2,stats:attack]` — which is the shape the reference uses for
     * the other half of this packet family (INV_BUTTON1..5, and see
     * ClientGameProt.ts:71 on what that family is called at this revision).
     *
     * (uid, sub, obj, op) — the bank fallback needs the sub id, which is which
     * of the 1,220 dynamic children was clicked.
     */
    if( ToriRSServer_ScriptsFallback(srv, TORIRSSERVER_FALLBACK_IF_BUTTON,
                                 ToriRSServer_ScriptsRunIfButton(srv, uid, op_num)) )
        ToriRSServer_BankHandleButton(srv, uid, sub, -1, op_num);
}

/**
 * Revision 239 keeps all IF3 ops in two packets. Decode them here, while every
 * field is still present, then enter the established 230 handlers.
 *
 * The packet has no context-free "held operation" bit: an object id is also
 * present for ordinary component rows such as the bank's Withdraw and Deposit
 * verbs.  Only the named backpack component maps this modern packet back to
 * the classic OPHELD family.  Every other component retains its IF_BUTTONN
 * trigger so content receives its authored action.  IF_SUBOP's last byte is
 * latched before dispatch so selecting a submenu entry is no longer
 * indistinguishable from selecting its parent.
 */
static void
handle_if_buttonx_packet(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct Mock239IfButton button;
    struct ToriRSServerPlayer* player = srv->active_player;
    int has_subop = name == PKTOUT_NAME_IF_SUBOP;
    int held_op = 0;

    if( !mock239_if_button_decode(payload, len, has_subop, &button) )
        return;
    player->last_subop = button.subop;
    if( srv->verbose )
        fprintf(stderr,
                "torirsserver: <- %s %d:%d sub=%d obj=%d op=%d subop=%d\n",
                has_subop ? "IF_SUBOP" : "IF_BUTTONX",
                (button.component_id >> 16) & 0xffff,
                button.component_id & 0xffff,
                button.sub,
                button.object_id,
                button.op,
                button.subop);

    /* A worn component is an inventory-button surface regardless of the item
     * field. The official client normally emits the no-item sentinel for the
     * static leaf, but the component is the durable address and is what
     * selects content's [inv_buttonN,wornitems:slotN] binding. Routing only
     * the sentinel variant made the C client's item-backed leaf click fall
     * through as IF_BUTTONN, where no Remove binding exists. */
    if( ToriRSServer_EquipmentWornSlot(button.component_id) >= 0 )
    {
        handle_worn_inv_button(srv, button.component_id, button.op);
        return;
    }

    if( button.component_id == ToriRSServer_Ids()->com_inventory_items )
        held_op = mock239_if_button_backpack_op(&button);

    if( held_op )
    {
        uint8_t held[8];
        struct RSAreaBuf out;

        rsab_wrap(&out, held, sizeof(held));
        rsab_p2(&out, button.object_id);
        rsab_p2(&out, button.sub);
        rsab_p4(&out, button.component_id);
        handle_opheld(srv, held_op, held, (int)rsab_len(&out));
        return;
    }

    {
        uint8_t component_op[6];
        struct RSAreaBuf out;

        /* Preserve the row's object even though old IF_BUTTON payloads have no
         * object field. This is diagnostic/state parity; the numbered trigger
         * is still selected by op. */
        player->last_item = button.object_id == MOCK239_IF_OBJ_NONE
                                ? -1
                                : button.object_id;
        rsab_wrap(&out, component_op, sizeof(component_op));
        rsab_p4(&out, button.component_id);
        rsab_p2(&out, button.sub);
        handle_if_button_op(srv, PKTOUT_NAME_IF_BUTTON1 + (button.op - 1),
                            component_op, (int)rsab_len(&out));
    }
}

/* IF_TRIGGEROPLOCAL's signature is selected by crc and is not on the wire.
 * The only server-side consumer currently implemented is clientscript 9189's
 * skill-guide View-journal bridge, whose authoritative source declares `"i"`.
 * Unknown CRCs remain losslessly decoded and logged, but are not guessed into
 * a RuneScript argument list. */
static void
handle_if_script_trigger(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct Mock239IfScriptTrigger trigger;
    int skill_guide_trigger = ToriRSServer_ContentSymbol(
        TORIRSSERVER_PACK_COMPONENT, "skill_guide_v2:quest_journal_button_trigger");

    (void)name;
    if( !mock239_if_script_trigger_decode(payload, len, NULL, &trigger) )
        return;
    if( srv->verbose )
        fprintf(stderr,
                "torirsserver: <- IF_SCRIPT_TRIGGER crc=%d com=%d:%d child=%d obj=%d "
                "typed=%d byte(s)\n",
                trigger.crc,
                (trigger.component_id >> 16) & 0xffff,
                trigger.component_id & 0xffff,
                trigger.child,
                trigger.object_id,
                trigger.typed_len);

    {
        uint8_t component_op[6];
        struct RSAreaBuf out;
        int32_t sub;
        int32_t object_id;

        if( skill_guide_trigger < 0 ||
            !mock239_if_script_trigger_skill_guide_sub(
                payload, len, skill_guide_trigger, &sub, &object_id) )
            return;

        srv->active_player->last_item =
            object_id == MOCK239_IF_OBJ_NONE ? -1 : object_id;
        srv->active_player->last_subop = -1;
        rsab_wrap(&out, component_op, sizeof(component_op));
        rsab_p4(&out, trigger.component_id);
        rsab_p2(&out, sub);
        handle_if_button_op(srv, PKTOUT_NAME_IF_BUTTON1,
                            component_op, (int)rsab_len(&out));
    }
}

static void
handle_click_world_map(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf click;
    int packed;

    (void)name;
    rsab_wrap(&click, (void*)payload, (size_t)len);
    packed = rsab_g4(&click);
    if( !rsab_ok(&click) )
        return;
    ToriRSServer_WorldMapClick(srv, (packed >> 28) & 0x3, (packed >> 14) & 0x3fff,
                           packed & 0x3fff);
}

/*
 * Remember what is mounted in the gameframe's two modal slots.
 *
 * Called from the IF_OPENSUB / IF_CLOSESUB encoders rather than from each
 * opener, so it cannot drift: every mount the server makes goes through those
 * two functions, including the ones content scripts drive through
 * `if_openmain`. `group` is 0 for a close.
 */
void
ToriRSServer_NoteModalMount(
    struct ToriRSServer* srv,
    int uid,
    int group)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    if( !srv->active_player )
        return;
    player = srv->active_player;

    /* Compare against the live top's slots (bound by if_opentop), not the
     * stretch-only ids table — after a Display remount those diverge. */
    if( uid == ToriRSServer_PlayerMainmodal(player) )
        player->mainmodal_group = group;
    else if( uid == ToriRSServer_PlayerSidemodal(player) )
        player->sidemodal_group = group;
    else if( uid == ToriRSServer_Ids()->com_chatbox_modal )
        player->chatmodal_group = group;
    else if( uid == ToriRSServer_Ids()->com_gameframe_floater )
    {
        /* World map is also opened by content (quest overview's Show on Map),
         * not solely by the minimap-orb router. Keep the generic mount state
         * in sync so its close button and periodic tile sender behave alike. */
        player->worldmap_open = group == ToriRSServer_Ids()->iface_worldmap;
    }
}

/*
 * Take the chatbox dialogue off the screen.
 *
 * The mirror of `if_openchat` (torirs_server_scripts.c), and like it, one packet:
 * unmounting `chatbox:chatmodal` is what the client's own on_sub_change hook
 * watches, and script908 is what re-shows the chat scrollback behind it.
 */
static void
close_chat_modal(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    if( player->chatmodal_group <= 0 )
        return;
    ToriRSServer_SendIfClosesub(srv->active_player, ToriRSServer_Ids()->com_chatbox_modal);
    player->resume_button_count = 0;
}

void
ToriRSServer_WorldCloseModal(struct ToriRSServer* srv)
{
    ToriRSServer_WorldCloseModalEx(srv, 1);
}

void
ToriRSServer_WorldCloseModalEx(
    struct ToriRSServer* srv,
    int clear_weak_queue)
{
    struct ToriRSServerPlayer* player;
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();
    int main_group;
    int side_group;
    int bank_was_open;
    int abort_dialog;

    assert(srv);
    if( !srv->active_player )
        return;
    player = srv->active_player;

    /* Escape is also the user's early-exit control for a remote map view.
     * Restore before unmounting so the next client-out phase cannot emit
     * scene-local state against the temporary WorldView. */
    if( player->remote_view_active )
        ToriRSServer_WorldRemoteViewEnd(player);

    /*
     * The weak queue dies with the modal, and that is the only thing that
     * distinguishes a weak entry from a normal one — without it `weakqueue`
     * would be a synonym content could not tell apart. `Player.closeModal`
     * clears it first, before the early return below, so a close with nothing
     * mounted still discards it.
     */
    if( clear_weak_queue )
        ToriRSServer_ScriptsClearWeakQueue(player);

    /* TRIGGER_ONDIALOGABORT is not a generic close notification. It runs only
     * when server action interrupts a script whose continuation is parked on
     * dialogue UI, and it must reach the golden client's CS2 listeners before
     * either the state or its mounted component is released. A dialogue that
     * completed normally has already left active_script and sends nothing. */
    abort_dialog =
        player->active_script &&
        (player->active_script->execution == SSVM_PAUSEBUTTON ||
         player->active_script->execution == SSVM_COUNTDIALOG ||
         player->active_script->execution == SSVM_NAMEDIALOG);
    if( abort_dialog )
        ToriRSServer_SendTriggerOndialogabort(player);

    /*
     * The parked script goes first, and unconditionally.
     *
     * `chatmodal_group` says an interface is mounted; the parked script is the
     * conversation itself, and the two can come apart — a `p_countdialog` waits
     * with no chat mount of its own. Ending the wait is what actually frees the
     * player's one script slot, so it must not be conditional on the mount.
     */
    ToriRSServer_ScriptsCloseDialogue(srv);
    close_chat_modal(srv);

    main_group = player->mainmodal_group;
    side_group = player->sidemodal_group;
    if( main_group <= 0 && side_group <= 0 )
        return;

    /*
     * `[if_close]` is a notification, not a handler — and it was written here as
     * a handler.
     *
     * The reference is unambiguous (`Player.closeModal`): it runs the close
     * trigger *and then* clears `modalMain` and unmounts, in that order, with no
     * branch between them. Nothing content can do prevents an interface the
     * player closed from closing. This site read `if( ran ) return;` instead,
     * which made the tree's two `[if_close]` scripts — both of which only
     * `inv_stoptransmit` — suppress the unmount they had no opinion about, so
     * walking away from an open bank sent no IF_CLOSESUB and left it on screen.
     * That is exactly the failure mode of a fallback that should never have been
     * one, which is why it is not in `enum ToriRSServerFallback`.
     *
     * And underneath that, the reason nobody had noticed: **the subject is the
     * interface, not a component in it.** This asked with
     * `TORIRSSERVER_COM(main_group, 0)` — 12 << 16 for the bank — while the compiler
     * keys `[if_close,bankmain]` on the bare interface id 12
     * (`[if_button,bankmain:note_graphic]` is the one that keys on a packed uid,
     * because *that* subject names a child). The two never met, so no
     * `[if_close]` in this tree had ever run, and the suppression above was
     * therefore invisible: it could only fire on a script that could not be
     * found. The only cost so far was the server transmitting to a screen the
     * player had closed.
     */
    bank_was_open = main_group > 0 && player->bank.open;
    if( main_group > 0 )
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_IF_CLOSE, main_group, -1, -1);

    /* One screen keeps state of its own beyond the mount — the bank, which has
     * containers and a reorganise — so it closes through its own function.
     * Anything else is just a mount, and dropping it is the whole of closing
     * it: the equipment screen's repaint is gated on `mainmodal_group`, which
     * the closesub below already clears.
     *
     * Read before the trigger, because `[label,closebank]`'s own
     * `inv_stoptransmit(bankmain:scrollbar)` clears the flag — and
     * `ToriRSServer_BankClose` returns early on a bank it thinks is already shut,
     * which would swallow the unmount a second way. */
    if( main_group > 0 && bank_was_open )
    {
        player->bank.open = 1;
        ToriRSServer_BankClose(srv);
    }
    else if( main_group > 0 )
    {
        ToriRSServer_SendIfClosesub(srv->active_player, ids->com_gameframe_mainmodal);
    }

    /* House options (370) is mounted by the settings tab into sidemodal with
     * no mainmodal at all. CLOSE_MODAL carries no component id: the golden
     * client merely reports that CS2 executed `if_close`, so the server must
     * release every modal slot it owns. The old mainmodal early-return made a
     * side-only mount immortal on the server; the next house-button click then
     * found it already mounted and could not reopen it. Keep the group captured
     * above because closing mainmodal may update the tracked mount table. */
    if( side_group > 0 )
    {
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_IF_CLOSE, side_group, -1, -1);
        ToriRSServer_SendIfClosesub(srv->active_player, ids->com_gameframe_sidemodal);
    }
}

void
ToriRSServer_WorldClearPendingAction(struct ToriRSServer* srv)
{
    ToriRSServer_CombatStopPlayer(srv);
    ToriRSServer_WorldCloseModal(srv);
}

/*
 * The player pressed Escape, or clicked a component whose CS2 called
 * `if_close`. The client has already torn its own copy down; the server has to
 * agree, or the next open finds the interface still marked open and sends
 * nothing.
 */
static void
handle_close_modal(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    (void)payload;
    (void)len;

    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- CLOSE_MODAL (main=%d side=%d chat=%d)\n",
                srv->active_player->mainmodal_group, srv->active_player->sidemodal_group,
                srv->active_player->chatmodal_group);
    ToriRSServer_WorldCloseModal(srv);
}

/* Framed and named so revision telemetry can distinguish a healthy idle
 * heartbeat from an unknown opcode; it intentionally mutates no world state. */
static void
handle_idle_timer(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)srv;
    (void)name;
    (void)payload;
    (void)len;
}

/*
 * Revision 239 does not apply packets queued behind REBUILD_NORMAL until its
 * asynchronous WorldView load has finished. The authoritative client reports
 * that exact transition with MAP_BUILD_COMPLETE. Treating it as an idle packet
 * made the server race PLAYER_INFO, IF_OPENTOP and every login setevents packet
 * against a scene which did not exist yet.
 *
 * A second rebuild may become necessary while the first one is loading (for
 * example this player walks over their own window's margin, or their map
 * instance rebuilds under them). In that case
 * acknowledge the old scene by sending the new rebuild and wait for a second
 * completion; state for the login burst still must not escape between them.
 */
static void
handle_map_build_complete(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    (void)name;
    (void)payload;
    (void)len;
    if( !player )
        return;
    if( srv->verbose )
        fprintf(stderr,
                "torirsserver: <- MAP_BUILD_COMPLETE login_pending=%d scene_pending=%d "
                "rebuild_pending=%d\n",
                player->login_scene_pending, player->rebuild_scene_pending,
                player->rebuild_pending);
    if( !player->login_scene_pending && !player->rebuild_scene_pending )
        return;

    if( player->rebuild_pending )
    {
        if( srv->verbose )
            fprintf(stderr,
                    "torirsserver: login scene changed while loading; sending replacement "
                    "rebuild and retaining barrier\n");
        ToriRSServer_SendRebuild(player);
        player->rebuild_pending = 0;
        return;
    }

    if( player->rebuild_scene_pending )
    {
        player->rebuild_scene_pending = 0;
        if( srv->verbose )
            fprintf(stderr,
                    "torirsserver: rebuilt scene barrier complete; resuming player queues\n");
        return;
    }

    player->login_scene_pending = 0;
    if( srv->verbose )
        fprintf(stderr,
                "torirsserver: login scene barrier complete; sending player/UI/state burst\n");
    ToriRSServer_WorldLoginFinish(player);
}

/* The number a p_countdialog collected — "Withdraw-X". */
static void
handle_resume_countdialog(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf count;
    int32_t value;

    (void)name;
    rsab_wrap(&count, (void*)payload, (size_t)len);
    value = rsab_g4(&count);
    if( !rsab_ok(&count) )
        return;
    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- RESUME_P_COUNTDIALOG %d\n", (int)value);
    srv->active_player->last_int = value;
    /* A parked script owns the answer if there is one; only when nothing is
     * waiting does the engine's own pending "-X" row get it. */
    if( !ToriRSServer_ScriptsResumeCountdialog(srv, value) )
        ToriRSServer_BankResumeCountdialog(srv, (int)value);
}

/* These callbacks exist as separate prots at 239. Name replies release only a
 * NAMEDIALOG wait; string-dialog replies remain decoded and logged without
 * being mis-delivered to a different wait kind. */
static void
handle_resume_textdialog(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct Mock239ByteString text;

    if( !mock239_resume_text_decode(payload, len, &text) )
        return;
    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- %s \"%.*s\"\n",
                name == PKTOUT_NAME_RESUME_P_NAMEDIALOG
                    ? "RESUME_P_NAMEDIALOG"
                    : "RESUME_P_STRINGDIALOG",
                text.len, (const char*)text.bytes);
    if( name == PKTOUT_NAME_RESUME_P_NAMEDIALOG )
        ToriRSServer_ScriptsResumeNamedialog(srv, text.bytes, text.len);
}

static void
handle_resume_countdialog_long(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    int64_t value;

    (void)name;
    if( !mock239_resume_count_long_decode(payload, len, &value) )
        return;
    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- RESUME_P_COUNTDIALOG_LONG %lld\n",
                (long long)value);
    /* SSVM's declared last_int is int32. Values it can represent answer the
     * same COUNTDIALOG wait; larger values stay decoded and logged rather than
     * wrapping into a different number. */
    if( value >= INT32_MIN && value <= INT32_MAX )
    {
        srv->active_player->last_int = (int32_t)value;
        if( !ToriRSServer_ScriptsResumeCountdialog(srv, (int32_t)value) )
            ToriRSServer_BankResumeCountdialog(srv, (int)value);
    }
}

static void
handle_resume_objdialog(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    int32_t object_id;

    (void)name;
    if( !mock239_resume_object_decode(payload, len, &object_id) )
        return;
    /* There is no OBJDIALOG execution kind yet, but retaining last_item is the
     * lossless server-side state a future waiter will consume. */
    srv->active_player->last_item = object_id;
    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- RESUME_P_OBJDIALOG obj=%d\n", object_id);
}

/* ------------------------------------------------------------------ */
/* Social — friends, ignore, private chat                              */
/* ------------------------------------------------------------------ */

/*
 * The wire half of docs/FRIENDS_PRIVATE_CHAT.md. The *rules* are all in
 * torirs_server_friends.c (the roster, `isVisibleTo`, the caps, the pm ids); nothing
 * below decides anything, it only reads packets and writes packets.
 *
 * The shape is LostCity's, and the correspondence is one-to-one:
 *
 *   FriendListAddHandler.ts &c  -> the four mutation handlers here
 *   FriendServer.broadcastWorldToFollowers -> social_broadcast_to_followers
 *   FriendServer.sendPlayerWorldUpdate     -> social_send_world_update
 *   FriendServer.sendFriendsListToPlayer   -> ToriRSServer_WorldSocialLogin
 *
 * The one structural difference is the delivery step. The reference looks a
 * player up through `socketByWorld[world]` because the friend server is a
 * separate process serving many worlds; here "who is online" is a scan of this
 * world's own player pool. That is not a shortcut, it is the whole of what
 * §5.2(3) says is deliberately not ported — but it has a consequence worth
 * naming: the friend *roster* is process-scoped and outlives a session, while
 * *delivery* is world-scoped, so a name can be "online" to the roster and have
 * no player slot to send to. Every send below therefore tolerates a NULL
 * player, and none treats it as an error.
 */

/** The online player behind a base-37 name, or NULL. */
static struct ToriRSServerPlayer*
social_player_by_name37(
    struct ToriRSServer* srv,
    int64_t name37)
{
    if( name37 == 0 )
        return NULL;
    for( int i = 0; i < srv->player_count; i++ )
    {
        struct ToriRSServerPlayer* p = &srv->players[i];

        if( p->active && p->name37 == name37 )
            return p;
    }
    return NULL;
}

/*
 * Tell `viewer` where `other` is — the reference's `sendPlayerWorldUpdate`.
 *
 * The world byte is `isVisibleTo(viewer, other) ? world(other) : 0`, which is
 * the reference's expression verbatim. The service already folds that pair into
 * `ToriRSServer_FriendsGet`; this is the same rule for a name that may not be in
 * the viewer's list yet (the moment right after a FRIENDLIST_ADD).
 */
static void
social_send_world_update(
    struct ToriRSServer* srv,
    int64_t viewer37,
    int64_t other37)
{
    struct ToriRSServerPlayer* viewer = social_player_by_name37(srv, viewer37);

    if( !viewer )
        return;
    ToriRSServer_SendUpdateFriendlist(
        viewer,
        other37,
        ToriRSServer_FriendsVisibleTo(viewer37, other37) ? ToriRSServer_FriendsWorld(other37) : 0);
}

/*
 * Everyone who has `name37` in their friend list hears about it.
 *
 * The reference broadcasts on login, logout, chat-mode change and all four list
 * mutations — including the ignore ones, because ignoring someone has to make
 * you disappear from *their* panel, and including friend-add, because the
 * adder's own "Friends" privacy mode may have just started letting a follower
 * see them.
 */
static void
social_broadcast_to_followers(
    struct ToriRSServer* srv,
    int64_t name37)
{
    int64_t followers[TORIRSSERVER_SOCIAL_FRIENDS_MAX];
    int count = ToriRSServer_FriendsFollowers(name37, followers, (int)(sizeof(followers) /
                                                                  sizeof(followers[0])));

    if( count > (int)(sizeof(followers) / sizeof(followers[0])) )
    {
        /* An engine ceiling, not a game outcome: say so rather than silently
         * leaving the tail of the list un-notified. */
        fprintf(stderr,
                "torirsserver: %d followers is more than the %d this broadcast can carry; "
                "the rest were not told\n",
                count,
                (int)(sizeof(followers) / sizeof(followers[0])));
        count = (int)(sizeof(followers) / sizeof(followers[0]));
    }
    for( int i = 0; i < count; i++ )
        social_send_world_update(srv, followers[i], name37);
}

/*
 * "<name> has logged in." — the one sentence this feature adds, said by content.
 *
 * The reference has no counterpart. Its 2004 client watched the world byte in
 * UPDATE_FRIENDLIST go from 0 to non-zero and wrote its own line, so no LostCity
 * server ever worded this. The rev-230 client dropped that derivation, which
 * moves the sentence onto the server — and a sentence the server says is
 * content's (CONTENT_ARCHITECTURE.md §8.2(a)). So the engine here does exactly
 * three things and none of them is wording: it decides *who* hears (the same
 * followers the world update goes to), it applies `isVisibleTo` so a
 * notification never leaks presence a world byte would have hidden, and it hands
 * over the display name because only the engine knows it.
 *
 * `srv->active_player` is what `mes` writes to, so the follower is made active
 * around each call and the caller's own active player is put back afterwards —
 * a login runs this with the *newcomer* active and a logout runs it with the
 * departing player already gone, and neither may leak into the next statement.
 *
 * Silence when the trigger is unbound is deliberate and is the whole content
 * seam: whether a tree notifies at all is stated by whether it defines the trigger.
 */
static void
social_notify_followers(
    struct ToriRSServer* srv,
    int64_t name37,
    const char* display_name,
    int trigger)
{
    int64_t followers[TORIRSSERVER_SOCIAL_FRIENDS_MAX];
    const int max = (int)(sizeof(followers) / sizeof(followers[0]));
    struct ToriRSServerPlayer* was_active = srv->active_player;
    const char* strv[1];
    int count;

    assert(display_name);
    if( name37 == 0 || !display_name[0] )
        return;

    count = ToriRSServer_FriendsFollowers(name37, followers, max);
    if( count > max )
        count = max;

    strv[0] = display_name;
    for( int i = 0; i < count; i++ )
    {
        struct ToriRSServerPlayer* follower = social_player_by_name37(srv, followers[i]);

        if( !follower )
            continue;
        if( follower->name37 == name37 )
            continue;
        if( !ToriRSServer_FriendsVisibleTo(followers[i], name37) )
            continue;

        ToriRSServer_WorldSetActive(srv, follower);
        ToriRSServer_ScriptsRunTriggerSv(srv, trigger, -1, -1, -1, strv, 1);
    }
    ToriRSServer_WorldSetActive(srv, was_active);
}

/*
 * The login dump — `sendFriendsListToPlayer` + `sendIgnoreListToPlayer` +
 * `FriendlistLoaded(2)`, then the follower broadcast.
 *
 * Order matters. In the authoritative rev-239 client FRIENDLIST_LOADED is a
 * zero-length marker whose handler sets social state 1 ("loading"); parsing
 * UPDATE_FRIENDLIST sets state 2. The marker must therefore come first and the
 * snapshot — including an explicit empty snapshot — must finish the sequence.
 * The older revision's status byte is still encoded by its wire adapter.
 *
 * The ignore list goes out even when it is empty. The client replaces its store
 * wholesale on this packet, so an empty one is the statement "you have no
 * ignores", which is different from never having been told.
 */
void
ToriRSServer_WorldSocialLogin(struct ToriRSServerPlayer* player)
{
    struct ToriRSServer* srv = player->world;
    int64_t me = player->name37;
    int count;

    if( me == 0 )
        return;

    ToriRSServer_SendFriendlistLoaded(player, 2);

    count = ToriRSServer_FriendsCount(me);
    if( count == 0 )
        ToriRSServer_SendUpdateFriendlistEmpty(player);
    for( int i = 0; i < count; i++ )
    {
        int64_t friend37 = 0;
        int world = 0;

        if( ToriRSServer_FriendsGet(me, i, &friend37, &world) )
            ToriRSServer_SendUpdateFriendlist(player, friend37, world);
    }
    {
        int64_t ignores[TORIRSSERVER_SOCIAL_IGNORES_MAX];
        int n = ToriRSServer_FriendsIgnoreCount(me);

        if( n > (int)(sizeof(ignores) / sizeof(ignores[0])) )
            n = (int)(sizeof(ignores) / sizeof(ignores[0]));
        for( int i = 0; i < n; i++ )
            ToriRSServer_FriendsIgnoreGet(me, i, &ignores[i]);
        ToriRSServer_SendUpdateIgnorelist(player, ignores, n);
    }

    /* The client's own filter UI is driven by these three, and nothing else
     * tells it what the server thinks they are. The reference has no equivalent
     * because its 2004 client kept the modes locally; at rev 230 the CS2 side
     * asks through chat_getfilter_* and the server is the source. */
    {
        int public_mode = 0, private_mode = 0, trade_mode = 0;

        ToriRSServer_FriendsChatModes(me, &public_mode, &private_mode, &trade_mode);
        ToriRSServer_SendChatFilterSettings(player, public_mode, private_mode, trade_mode);
    }

    social_broadcast_to_followers(srv, me);

    /*
     * After the broadcast, not before: a follower's panel should already read
     * "World 1" beside the name by the time the line about it arrives. The
     * order is only cosmetic — both are packets on the same tick — but it is
     * the order the sentence describes.
     */
    social_notify_followers(srv, me, player->display_name,
                            SS_TRIGGER_FRIENDLOGIN);
}

/** The name37 in an 8-byte social packet, or 0 if the frame is short. */
static int64_t
social_read_name37(
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int64_t name37;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    name37 = rsab_g8(&buf);
    return rsab_ok(&buf) ? name37 : 0;
}

/*
 * The four list mutations. One handler, because the reference's four handlers
 * differ only in which repository call they make and they are otherwise
 * character-for-character identical.
 *
 * The reference checks `socialProtect || invalid_name` *before* latching, so an
 * invalid name costs the sender nothing. Here the validity check lives inside
 * the service (it owns the base-37 round-trip), so the gate is taken first and
 * an eight-byte garbage name spends the tick's one social packet. That is
 * stricter than the reference, never looser, and it is the honest option: the
 * alternative is a second public entry point on the service whose only purpose
 * is to be asked a question before the real call.
 *
 * Nothing is sent to the player on a failure. The reference is silent on every
 * one of these paths — duplicate add, full list, delete of a name that is not
 * there — and torirs_server_friends.h says in as many words that no caller may invent
 * a message here.
 */
static void
handle_social_list(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int64_t me;
    int64_t target;
    enum ToriRSServerSocialResult result;

    if( !player )
        return;
    me = player->name37;
    target = social_read_name37(payload, len);
    if( me == 0 || target == 0 )
        return;
    if( !ToriRSServer_FriendsSocialGate(player) )
        return;

    switch( name )
    {
    case PKTOUT_NAME_FRIENDLIST_ADD:
        result = ToriRSServer_FriendsAdd(me, target);
        /* Two updates, both the reference's: the new friend's world straight
         * back to the adder, and a broadcast because the adder's own "Friends"
         * privacy mode may now let this person see *them*. */
        social_send_world_update(srv, me, target);
        social_broadcast_to_followers(srv, me);
        break;
    case PKTOUT_NAME_FRIENDLIST_DEL:
        result = ToriRSServer_FriendsDel(me, target);
        social_broadcast_to_followers(srv, me);
        break;
    case PKTOUT_NAME_IGNORELIST_ADD:
        result = ToriRSServer_FriendsIgnoreAdd(me, target);
        social_broadcast_to_followers(srv, me);
        break;
    case PKTOUT_NAME_IGNORELIST_DEL:
        result = ToriRSServer_FriendsIgnoreDel(me, target);
        social_broadcast_to_followers(srv, me);
        break;
    default:
        return;
    }

    if( srv->verbose )
    {
        char who[32];

        base37tostr((uint64_t)target, who, (int)sizeof(who));
        fprintf(stderr, "torirsserver: <- social name=%d target=%s result=%d\n", name, who,
                (int)result);
    }
    else
        (void)result;
}

/*
 * CHAT_SETMODE: p1 public, p1 private, p1 trade.
 *
 * The reference stores all three on the player and forwards only `privateChat`
 * to the friend server, because that is the only one anything reads. Here the
 * service holds all three (single copy — FRIENDS_PRIVATE_CHAT.md §5.3 decision
 * 4) and still reads only the private one.
 *
 * Deliberately *not* gated by socialProtect: the reference's ChatSetModeHandler
 * is the one handler in this family that does not check it, and the modes are
 * not a broadcast primitive.
 */
static void
handle_chat_setmode(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct RSAreaBuf buf;
    int public_mode, private_mode, trade_mode;

    (void)name;
    if( !player || player->name37 == 0 )
        return;
    rsab_wrap(&buf, (void*)payload, (size_t)len);
    public_mode = rsab_g1(&buf);
    private_mode = rsab_g1(&buf);
    trade_mode = rsab_g1(&buf);
    if( !rsab_ok(&buf) )
        return;

    ToriRSServer_FriendsSetChatModes(player->name37, public_mode, private_mode, trade_mode);
    /* Echo, so the client's filter UI and the server's copy cannot drift — the
     * service clamps an out-of-range private mode to ON and the client has no
     * other way to learn that happened. */
    ToriRSServer_FriendsChatModes(player->name37, &public_mode, &private_mode, &trade_mode);
    ToriRSServer_SendChatFilterSettings(player, public_mode, private_mode, trade_mode);
    /* A mode change is a visibility change for every follower. */
    social_broadcast_to_followers(srv, player->name37);

    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- CHAT_SETMODE public=%d private=%d trade=%d\n",
                public_mode, private_mode, trade_mode);
}

/*
 * Revision-239 WINDOW_STATUS: p1 windowMode (1 fixed / 2 resizable), then
 * p2 width and p2 height.  Classic and Modern are deliberately indistinct on
 * this wire; their dynamic Display-row IF_BUTTON callback latches the precise
 * layout before this packet arrives.
 */
static void
handle_window_status(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int window_mode;
    int layout_mode;
    int dropdown_choice;
    int width;
    int height;
    int32_t args[1];

    (void)name;
    if( !player || len < 5 )
        return;
    window_mode = payload[0];
    width = (payload[1] << 8) | payload[2];
    height = (payload[3] << 8) | payload[4];
    if( srv->verbose )
        fprintf(stderr,
                "torirsserver: <- WINDOW_STATUS window=%d canvas=%dx%d layout=%d\n",
                window_mode, width, height, player->client_layout_mode);
    /* Consumed by this packet whatever it turns out to mean, and before the
     * revision branch that may not want it: a choice that survives its own
     * WINDOW_STATUS came from some other dropdown, and leaving it latched
     * would let it decide the NEXT layout change. */
    dropdown_choice = player->settings_dropdown_choice;
    player->settings_dropdown_choice = -1;

    ToriRSServer_PohInit(&player->poh);

    if( srv->wire && srv->wire->revision >= 239 )
    {
        if( window_mode != 1 && window_mode != 2 )
            return;
        if( window_mode == 1 )
            layout_mode = 0;
        /* The All Settings row (see handle_if_button) — newer than the belief
         * below, and the only thing that can tell Classic from Modern when the
         * change came from that panel. */
        else if( dropdown_choice == 1 || dropdown_choice == 2 )
            layout_mode = dropdown_choice;
        else if( player->client_layout_mode == 1 || player->client_layout_mode == 2 )
            layout_mode = player->client_layout_mode;
        else
            layout_mode = 1; /* leaving fixed defaults to Resizable Classic */
    }
    else if( window_mode >= 0 && window_mode <= 2 )
        layout_mode = window_mode; /* ToriRSServer's extended three-way convention */
    else
        return;

    if( player->client_layout_mode == layout_mode )
        return;
    player->client_layout_mode = layout_mode;
    /* WINDOW_STATUS is emitted before MAP_BUILD_COMPLETE during the rev-239
     * login. Remember the real canvas mode, but do not mount a gameframe into
     * the WorldView that is still being replaced. login_finish consumes the
     * latched mode once the client acknowledges the scene. */
    if( player->login_scene_pending || player->rebuild_scene_pending )
        return;
    args[0] = layout_mode;
    ToriRSServer_ScriptsRunProc(srv, "[proc,gameframe_set_mode]", args, 1);
}

/*
 * MESSAGE_PRIVATE: p8 to37, then the wordpacked text over the rest of the
 * var-u8 frame.
 *
 * The text is unpacked here and packed again by the outbound encoder rather
 * than relayed as bytes, which is what the reference does
 * (MessagePrivateHandler unpacks, MessagePrivateEncoder re-packs). It costs a
 * round trip and buys the property that the server has actually read what it is
 * forwarding — the cap below is stated in packed bytes precisely because that
 * is what the reference caps, and a server that never decoded could not log,
 * filter or truncate.
 *
 * What is deliberately NOT done, because the reference does not do it either:
 * the recipient's ignore list is not consulted. Dropping ignored PMs is the
 * *client's* job (and this client does not do it yet — recorded as a parity gap
 * in FRIENDS_PRIVATE_CHAT.md §9).
 */
static void
handle_message_private(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct ToriRSServerPlayer* target_player;
    struct RSCache_Buffer packed;
    int64_t me;
    int64_t target;
    char* text;
    int packed_len;

    (void)name;
    if( !player || player->name37 == 0 )
        return;
    me = player->name37;
    if( len < 8 )
        return;
    target = social_read_name37(payload, len);
    if( target == 0 )
        return;

    packed_len = len - 8;
    /* The reference's cap is on the *packed* length, not on the character count
     * (MessagePrivateHandler.ts:13 tests `input.length`, and `input` is the raw
     * slice off the wire). The number is content's; see torirs_server_friends.h. */
    if( packed_len < 0 || packed_len > ToriRSServer_FriendsCapPmBytes() )
        return;
    if( !ToriRSServer_FriendsSocialGate(player) )
        return;

    RSCache_BufferInit(&packed, (uint8_t*)payload + 8, (uint32_t)packed_len);
    text = wordpack_unpack(&packed, packed_len);
    if( !text )
        return;

    target_player = social_player_by_name37(srv, target);
    if( target_player )
        ToriRSServer_SendMessagePrivate(
            target_player, me, ToriRSServer_FriendsNextPmId(), /* staff */ 0, text);

    if( srv->verbose )
    {
        char who[32];

        base37tostr((uint64_t)target, who, (int)sizeof(who));
        fprintf(stderr, "torirsserver: <- MESSAGE_PRIVATE to=%s%s \"%s\"\n", who,
                target_player ? "" : " (offline; dropped)", text);
    }
    free(text);
}

/*
 * MESSAGE_PUBLIC: p1 colour, p1 effect, then the wordpacked text.
 *
 * The opposite decision to MESSAGE_PRIVATE above: the packed bytes are stored
 * and re-emitted verbatim rather than unpacked and re-packed. A public line is
 * not addressed to anybody, so there is no recipient to resolve and nothing for
 * the server to decide — and PLAYER_INFO's chat block carries exactly these
 * bytes, so a decode/encode round trip would be a Huffman table's worth of work
 * to arrive at the same buffer. (The reference does the same thing here, and
 * for the same reason: only the private path has an addressee to look up.)
 *
 * The sender is in its own view, so this is also the line's echo: the client
 * adds the chatbox row and the overhead bubble from the PLAYER_INFO block it
 * gets back, not from what it typed. Nothing here is sent to the sender
 * specially.
 */
static void
handle_message_public(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int packed_len;

    (void)name;
    if( !player )
        return;
    /* Two header bytes and at least one packed byte. Under that there is no
     * message, which is a malformed frame rather than an empty one: the
     * client's own submit path never sends a blank line. */
    if( len < 3 )
        return;
    packed_len = len - 2;
    if( packed_len > (int)sizeof(player->chat_data) )
        return;

    player->chat_colour_effect = (payload[0] << 8) | payload[1];
    player->chat_type = 0; /* 0 = ordinary public chat; 1 = the staff variant */
    memcpy(player->chat_data, payload + 2, (size_t)packed_len);
    player->chat_len = packed_len;
    player->masks |= TORIRSSERVER_PMASK_CHAT;

    if( srv->verbose )
    {
        struct RSCache_Buffer packed;
        char* text;

        RSCache_BufferInit(&packed, (uint8_t*)payload + 2, (uint32_t)packed_len);
        text = wordpack_unpack(&packed, packed_len);
        fprintf(
            stderr,
            "torirsserver: <- MESSAGE_PUBLIC colour=%d effect=%d \"%s\"\n",
            payload[0],
            payload[1],
            text ? text : "");
        free(text);
    }
}

typedef void (*ToriRSServerPacketHandler)(
    struct ToriRSServer* srv,
    int name,
    const uint8_t* payload,
    int len);

struct ToriRSServerPacketRoute
{
    int name;
    ToriRSServerPacketHandler handler;
};

/*
 * Packets which ask the player actor to do something in the game world.
 *
 * Keep transport/liveness, scene acknowledgements, camera telemetry and
 * social/chat packets outside this set: an action lock is a boss mechanic, not
 * a frozen connection. The contiguous ranges are canonical packet-name
 * families (not revision opcodes), so the same gate covers rev 230 and 239.
 */
int
player_action_packet(int name)
{
    if( (name >= PKTOUT_NAME_OPOBJ1 && name <= PKTOUT_NAME_OPOBJU) ||
        (name >= PKTOUT_NAME_OPNPC1 && name <= PKTOUT_NAME_OPNPCU) ||
        (name >= PKTOUT_NAME_OPLOC1 && name <= PKTOUT_NAME_OPLOCU) ||
        (name >= PKTOUT_NAME_OPPLAYER1 && name <= PKTOUT_NAME_OPPLAYERU) ||
        (name >= PKTOUT_NAME_OPHELD1 && name <= PKTOUT_NAME_OPHELDU) ||
        (name >= PKTOUT_NAME_INV_BUTTON1 && name <= PKTOUT_NAME_INV_BUTTOND) ||
        (name >= PKTOUT_NAME_IF_BUTTON1 && name <= PKTOUT_NAME_IF_BUTTON10) ||
        (name >= PKTOUT_NAME_RESUME_PAUSEBUTTON &&
         name <= PKTOUT_NAME_RESUME_P_OBJDIALOG) )
        return 1;

    switch( name )
    {
    case PKTOUT_NAME_IF_BUTTON:
    case PKTOUT_NAME_IF_BUTTONX:
    case PKTOUT_NAME_IF_SUBOP:
    case PKTOUT_NAME_IF_SCRIPT_TRIGGER:
    case PKTOUT_NAME_IF_BUTTONT:
    case PKTOUT_NAME_CLICK_WORLD_MAP:
    case PKTOUT_NAME_MOVE_OPCLICK:
    case PKTOUT_NAME_MOVE_MINIMAPCLICK:
    case PKTOUT_NAME_MOVE_GAMECLICK:
    case PKTOUT_NAME_CLIENT_CHEAT:
        return 1;
    default:
        return 0;
    }
}

/*
 * The subset of the above a STUN refuses.
 *
 * Narrower than `player_action_packet` on purpose, and the difference is the
 * point of having two predicates. A stunned player in OldSchool can still eat,
 * drink, switch gear and flick a protection prayer — those are inventory,
 * equipment and interface clicks, and surviving a stun is exactly what they
 * are for. What a stun takes away is the ability to go anywhere or touch
 * anything in the world: movement and the four world-interaction families.
 *
 * OPHELD is on the allowed side deliberately: it is "use the thing in my
 * inventory" (eat, wield, drink), while using an item ON something in the
 * world arrives as OPOBJU / OPNPCU / OPLOCU / OPPLAYERU, which are refused
 * with the rest of their families.
 */
int
player_stun_blocks_packet(int name)
{
    if( (name >= PKTOUT_NAME_OPOBJ1 && name <= PKTOUT_NAME_OPOBJU) ||
        (name >= PKTOUT_NAME_OPNPC1 && name <= PKTOUT_NAME_OPNPCU) ||
        (name >= PKTOUT_NAME_OPLOC1 && name <= PKTOUT_NAME_OPLOCU) ||
        (name >= PKTOUT_NAME_OPPLAYER1 && name <= PKTOUT_NAME_OPPLAYERU) )
        return 1;

    switch( name )
    {
    case PKTOUT_NAME_MOVE_OPCLICK:
    case PKTOUT_NAME_MOVE_MINIMAPCLICK:
    case PKTOUT_NAME_MOVE_GAMECLICK:
    case PKTOUT_NAME_CLICK_WORLD_MAP:
        return 1;
    default:
        return 0;
    }
}

/*
 * The routing table. Adding a packet is a line here plus a handler; nothing
 * else in the file has to change, which is the whole point of it being a table.
 */
static const struct ToriRSServerPacketRoute k_packet_routes[] = {
    { PKTOUT_NAME_MOVE_GAMECLICK, handle_move_gameclick },
    { PKTOUT_NAME_MOVE_MINIMAPCLICK, handle_move_minimapclick },

    { PKTOUT_NAME_OPHELD1, handle_opheld_packet },
    { PKTOUT_NAME_OPHELD2, handle_opheld_packet },
    { PKTOUT_NAME_OPHELD3, handle_opheld_packet },
    { PKTOUT_NAME_OPHELD4, handle_opheld_packet },
    { PKTOUT_NAME_OPHELD5, handle_opheld_packet },

    { PKTOUT_NAME_INV_BUTTON1, handle_opheld_packet },
    { PKTOUT_NAME_INV_BUTTON2, handle_opheld_packet },
    { PKTOUT_NAME_INV_BUTTON3, handle_opheld_packet },
    { PKTOUT_NAME_INV_BUTTON4, handle_opheld_packet },
    { PKTOUT_NAME_INV_BUTTON5, handle_opheld_packet },
    { PKTOUT_NAME_INV_BUTTOND, handle_inv_buttond_packet },

    { PKTOUT_NAME_OPNPC1, handle_opnpc_packet },
    { PKTOUT_NAME_OPNPC2, handle_opnpc_packet },
    { PKTOUT_NAME_OPNPC3, handle_opnpc_packet },
    { PKTOUT_NAME_OPNPC4, handle_opnpc_packet },
    { PKTOUT_NAME_OPNPC5, handle_opnpc_packet },

    { PKTOUT_NAME_OPLOC1, handle_oploc_packet },
    { PKTOUT_NAME_OPLOC2, handle_oploc_packet },
    { PKTOUT_NAME_OPLOC3, handle_oploc_packet },
    { PKTOUT_NAME_OPLOC4, handle_oploc_packet },
    { PKTOUT_NAME_OPLOC5, handle_oploc_packet },

    { PKTOUT_NAME_OPOBJ1, handle_opobj_packet },
    { PKTOUT_NAME_OPOBJ2, handle_opobj_packet },
    { PKTOUT_NAME_OPOBJ3, handle_opobj_packet },
    { PKTOUT_NAME_OPOBJ4, handle_opobj_packet },
    { PKTOUT_NAME_OPOBJ5, handle_opobj_packet },

    /* "Use A on B". The client has emitted all four since the minimenu learned
     * about objsel; nothing here had a row for them, so they were dropped as
     * unrouted and every `*u` script in the tree was unreachable. */
    { PKTOUT_NAME_OPHELDU, handle_opheldu_packet },
    { PKTOUT_NAME_OPLOCU, handle_oplocu_packet },
    { PKTOUT_NAME_OPNPCU, handle_opnpcu_packet },
    { PKTOUT_NAME_OPOBJU, handle_opobju_packet },
    { PKTOUT_NAME_OPHELDT, handle_opheldt_packet },
    { PKTOUT_NAME_OPLOCT, handle_oploct_packet },
    { PKTOUT_NAME_OPNPCT, handle_opnpct_packet },
    { PKTOUT_NAME_OPOBJT, handle_opobjt_packet },
    { PKTOUT_NAME_OPPLAYERT, handle_opplayert_packet },

    { PKTOUT_NAME_IF_BUTTON, handle_if_button },
    { PKTOUT_NAME_IF_BUTTON1, handle_if_button_op },
    { PKTOUT_NAME_IF_BUTTON2, handle_if_button_op },
    { PKTOUT_NAME_IF_BUTTON3, handle_if_button_op },
    { PKTOUT_NAME_IF_BUTTON4, handle_if_button_op },
    { PKTOUT_NAME_IF_BUTTON5, handle_if_button_op },
    { PKTOUT_NAME_IF_BUTTON6, handle_if_button_op },
    { PKTOUT_NAME_IF_BUTTON7, handle_if_button_op },
    { PKTOUT_NAME_IF_BUTTON8, handle_if_button_op },
    { PKTOUT_NAME_IF_BUTTON9, handle_if_button_op },
    { PKTOUT_NAME_IF_BUTTON10, handle_if_button_op },
    { PKTOUT_NAME_IF_BUTTONX, handle_if_buttonx_packet },
    { PKTOUT_NAME_IF_SUBOP, handle_if_buttonx_packet },
    { PKTOUT_NAME_IF_SCRIPT_TRIGGER, handle_if_script_trigger },

    { PKTOUT_NAME_RESUME_PAUSEBUTTON, handle_resume_pausebutton },
    { PKTOUT_NAME_RESUME_P_COUNTDIALOG, handle_resume_countdialog },
    { PKTOUT_NAME_RESUME_P_NAMEDIALOG, handle_resume_textdialog },
    { PKTOUT_NAME_RESUME_P_STRINGDIALOG, handle_resume_textdialog },
    { PKTOUT_NAME_RESUME_P_COUNTDIALOG_LONG, handle_resume_countdialog_long },
    { PKTOUT_NAME_RESUME_P_OBJDIALOG, handle_resume_objdialog },
    { PKTOUT_NAME_CLICK_WORLD_MAP, handle_click_world_map },
    { PKTOUT_NAME_CLOSE_MODAL, handle_close_modal },
    { PKTOUT_NAME_IDLE_TIMER, handle_idle_timer },
    { PKTOUT_NAME_NO_TIMEOUT, handle_idle_timer },
    { PKTOUT_NAME_EVENT_MOUSE_MOVE, handle_idle_timer },
    { PKTOUT_NAME_MAP_BUILD_COMPLETE, handle_map_build_complete },
    { PKTOUT_NAME_EVENT_APPLET_FOCUS, handle_idle_timer },
    { PKTOUT_NAME_CLIENT_CHEAT, handle_cheat_packet },

    { PKTOUT_NAME_FRIENDLIST_ADD, handle_social_list },
    { PKTOUT_NAME_FRIENDLIST_DEL, handle_social_list },
    { PKTOUT_NAME_IGNORELIST_ADD, handle_social_list },
    { PKTOUT_NAME_IGNORELIST_DEL, handle_social_list },
    { PKTOUT_NAME_CHAT_SETMODE, handle_chat_setmode },
    { PKTOUT_NAME_MESSAGE_PRIVATE, handle_message_private },
    { PKTOUT_NAME_MESSAGE_PUBLIC, handle_message_public },
    { PKTOUT_NAME_WINDOW_STATUS, handle_window_status },
};

void
ToriRSServer_WorldHandle(
    struct ToriRSServerPlayer* player,
    int name,
    const uint8_t* payload,
    int len)
{
    struct ToriRSServer* srv;

    /* A packet from a session with no player is a packet from a connection that
     * was refused a slot, or one still mid-handshake. Dropping it is the only
     * safe answer: every handler below writes player state. */
    assert(player);
    if( !player->active )
        return;
    srv = player->world;
    ToriRSServer_WorldSetActive(srv, player);

    /* A loading rev-239 WorldView is not a playable client. It may report its
     * canvas and liveness, but clicks/resumes/movement must not start scripts
     * whose interface and entity state have not been installed yet. */
    if( (player->login_scene_pending || player->rebuild_scene_pending) &&
        name != PKTOUT_NAME_MAP_BUILD_COMPLETE &&
        name != PKTOUT_NAME_WINDOW_STATUS && name != PKTOUT_NAME_IDLE_TIMER &&
        name != PKTOUT_NAME_NO_TIMEOUT && name != PKTOUT_NAME_EVENT_MOUSE_MOVE &&
        name != PKTOUT_NAME_EVENT_APPLET_FOCUS )
    {
        if( srv->verbose )
            fprintf(stderr, "torirsserver: <- packet name %d dropped behind scene barrier\n",
                    name);
        return;
    }

    if( player->action_locked && player_action_packet(name) )
    {
        if( srv->verbose )
            fprintf(stderr, "torirsserver: <- player action packet name %d dropped while locked\n",
                    name);
        return;
    }

    if( player->stun_ticks > 0 && player_stun_blocks_packet(name) )
    {
        if( srv->verbose )
            fprintf(stderr, "torirsserver: <- player packet name %d dropped while stunned (%d)\n",
                    name, player->stun_ticks);
        return;
    }

    /*
     * Somebody is at the keyboard.
     *
     * Everything the client sends is either an input (a click, a walk, a key, a
     * button, a chat line) or one of the four packets it sends BECAUSE nobody
     * is: two keepalives, the scene ack, and the canvas report. Only the first
     * class may reset the AFK clock — counting the keepalives would make the
     * clock unreachable and TORIRSSERVER_AFK_COMBAT_TICKS dead code.
     *
     * Set before the handler runs, not after: an OPNPC's own handler engages
     * combat, and it must see a clock this packet has already reset.
     */
    if( name != PKTOUT_NAME_NO_TIMEOUT && name != PKTOUT_NAME_IDLE_TIMER &&
        name != PKTOUT_NAME_MAP_BUILD_COMPLETE && name != PKTOUT_NAME_WINDOW_STATUS )
        player->last_input_tick = (int32_t)srv->tick;

    for( size_t i = 0; i < sizeof(k_packet_routes) / sizeof(k_packet_routes[0]); i++ )
    {
        if( k_packet_routes[i].name != name )
            continue;
        k_packet_routes[i].handler(srv, name, payload, len);
        return;
    }

    /* A packet with no route is not an error: the client sends plenty this
     * server has no use for (camera, focus, idle). The session layer already
     * framed it correctly, so ignoring it costs nothing. */
    if( srv->verbose )
        fprintf(stderr, "torirsserver: <- unrouted packet name %d (%d bytes)\n", name, len);
}


/* ------------------------------------------------------------------ */
/* Death                                                               */
/* ------------------------------------------------------------------ */

/*
 * An npc reached zero hitpoints. Dispatch only — this function has no body.
 *
 * `[ai_queue3,<npc>]` is LostCity's death trigger and its scripts are the drop
 * table: a sequence of `obj_add(npc_coord, ...)` calls under a `random(128)`
 * roll, exactly as the reference writes them. What an npc nothing binds leaves
 * behind is `[ai_queue3,_]` in skill_combat/npc_combat.rs2, which is where the
 * reference states it too (`[ai_queue3,_] gosub(npc_default_death)`).
 *
 * That last sentence used to be `TORIRSSERVER_FALLBACK_AI_QUEUE3`: five lines here
 * that read `ToriRSServerNpcDef.death_drop` and called `ToriRSServer_WorldObjAdd`
 * whenever nothing was bound. The row is deleted and the fallback count is 6.
 * The field stays — `record_authored_param` files the same value under param id
 * 2634 for `npc_param` to read, and `torirs_server_servercodec.c` carries it on the
 * wire as npc field 151 — but no engine logic reads it any more.
 *
 * Everything else about a death is still engine and stays here: hitpoints, the
 * death animation, the delay and the despawn (PORTING_GUIDE §2.3). That is why
 * all 76 ported drop tables dropped the reference's `gosub(npc_death)` —
 * `drop_tables/scripts/shared_droptables.rs2` note 1.
 *
 * The reference binds 16 of `drop tables/`'s 94 `[ai_queue3]` triggers to an npc
 * *category* rather than to an npc, so this dispatch has to offer the category
 * rung or those 16 tables can never bind. It used to pass a literal -1 on the
 * strength of a claim that this cache carries no npc category; it carries one on
 * 9,149 of its 16,292 records (triage §16.1), and `ToriRSServer_NpcCategory()` reads
 * it. Adopting it here is additive: the lookup chain is exact type -> category ->
 * global, so an npc whose type is bound is unaffected, and a category nothing
 * binds now falls to the `_` rung instead of to C.
 *
 * Measured rather than assumed, on `[ai_queue3,_chicken]` (category 444) against
 * `chicken_brown`, an npc of that category with no binding of its own:
 * with -1 the script does not run, with the category it does. The category rung
 * is load-bearing in a second way now that `_` is bound: without it a chicken
 * would fall past its own table to the default drop and leave bones and no
 * chicken. The selftest section "the death drop is content's" pins that.
 */
void
ToriRSServer_WorldNpcDied(
    struct ToriRSServer* srv,
    int slot)
{
    struct ToriRSServerNpc* npc = &srv->npcs[slot];

    ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_QUEUE3, npc->type,
                                ToriRSServer_NpcCategory(npc->type), slot);
    /*
     * Slayer kill credit — always, after the drop table.
     *
     * `[ai_queue3]` is exclusive (type → category → `_`), so a drop-table bind
     * suppresses `[ai_queue3,_]` and any credit hung only there would miss the
     * npcs players actually kill. Same shape as `npc_default_chat` through
     * `ToriRSServer_ScriptsRunProcOnNpc`: content owns the policy
     * (`[proc,slayer_on_npc_kill]`); the engine only promises the call. Missing
     * the proc is a silent no-op (scripts not loaded / older packs).
     */
    ToriRSServer_ScriptsRunProcOnNpc(srv, "[proc,slayer_on_npc_kill]", slot);
}

/*
 * There is no player respawn function here any more.
 *
 * It used to teleport the player to `g_home_x`/`g_home_z`, heal them, clear the
 * prayers and say "You wake up in Lumbridge." — a respawn point, a restore
 * policy and a string, none of which are the engine's. All four are
 * `[queue,player_death]` in player/death.rs2 now, built out of `p_teleport`,
 * `stat_heal`, `healenergy` and `mes`, which are the same ops any content
 * author has.
 */

/* ------------------------------------------------------------------ */
/* Login + tick                                                        */
/* ------------------------------------------------------------------ */

/*
 * A varp id by symbol.
 *
 * Every varp the engine itself writes goes through here rather than through a
 * `#define`: content/pack/varp.pack is the one place an id lives, and the
 * engine and the scripts then name the same thing. Resolved per call rather
 * than cached because the content tree loads after the world does — a value
 * cached at init would be the -1 from before the load.
 */
int
ToriRSServer_WorldVarp(const char* symbol)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, symbol);
}


/*
 * The combat_level varbit interface 593 builds itself from.
 *
 * `combat_level` is the number printed above the buttons, by the same formula
 * the minimenu colours npc levels with.
 *
 * `combat_weapon_category` is no longer computed here — content writes
 * %combat_weapon_category via ~combat_weapon_category_sync on equip/unequip.
 *
 * Level is written whenever its input changes — equipping, unequipping or
 * gaining a level — because a varbit the client is never told about reads as 0.
 */
void
ToriRSServer_WorldSyncCombatVarbits(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    int level_varbit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "combatlevel_transmit");
    int level = ToriRSServer_CombatLevel(player);

    /*
     * Compared before writing, unlike an ordinary varp write.
     *
     * A varp assignment always transmits — that is the reference's rule and
     * `[login] %com_mode = 0` depends on it. But this is *derived*: it is
     * recomputed every tick from the stats, so writing unconditionally would put
     * a varp on the wire fifty times a minute for a value that has not moved.
     * Derived state compares; authored state does not.
     */
    if( level_varbit >= 0 && ToriRSServer_VarbitGet(player, level_varbit) != level )
        ToriRSServer_VarbitSet(srv, level_varbit, level);
}

/*
 * The attack style lives in the `com_mode` varp and nowhere else.
 *
 * Resolved by symbol rather than by number so the engine and the content name
 * the same thing: content/pack/varp.pack says com_mode is 43, and if that ever
 * changes there is one place to change it. Resolved on each call rather than
 * cached because the content tree loads after the world does, and a cached -1
 * would outlive the load.
 */
static int
attack_style_varp(void)
{
    return ToriRSServer_WorldVarp("com_mode");
}

int
ToriRSServer_WorldAttackStyle(const struct ToriRSServer* srv)
{
    int varp = attack_style_varp();

    if( varp < 0 || varp >= TORIRSSERVER_VARP_COUNT )
        return TORIRSSERVER_STYLE_ACCURATE;
    return srv->active_player->varps[varp];
}

/*
 * A varp has been written: put it on the wire NOW.
 *
 * "Now" rather than "in phase 10", and the difference is a correctness one that
 * only shows up in interface content. The reference encodes the packet inside
 * the setter —
 *
 *     // Player.ts:1763
 *     setVar(id, value) { ... this.vars[varp.id] = value;
 *                             if (varp.transmit) this.writeVarp(id, value); }
 *
 * — and `if_opensub` writes immediately too, so **a script's source order is the
 * packet order**. Content relies on that. Slayer Rewards is the case that found
 * it:
 *
 *     %slayer_master_in_focus = $master;
 *     if_opensub(toplevel_osrs_stretch:mainmodal, slayer_rewards, 0);
 *
 * Interface 426's onload chain reads that varbit four times as it lays itself
 * out — the price of cancelling a task, the price of blocking one, what is in
 * each block slot, which master's task table "View List" shows. Batching the
 * varp to the end of the tick put the mount first, so the panel drew the
 * *default* master's prices (100 points instead of Turael's 40) and its task
 * list came up empty, with every packet present and correct on the wire and
 * nothing to see in the log but the order.
 *
 * Nothing generic can fix that downstream: the Tasks tab's own
 * `if_setonvartransmit` does not list this varp, so a late write does not even
 * repaint it. The panel is built once, from whatever the client knew at mount.
 *
 * What batching bought was a dedupe — a varbit is a bit range inside a varp, so
 * the bank's ten settings all land in varp 115 and used to queue it ten times.
 * Sending ten packets is what the reference does (ten `setVarBit` calls are ten
 * `writeVarp`s), it is what the client already tolerates, and it costs six bytes
 * each. The fixed 64-entry change list is gone with it, and so is the silent
 * drop past its end.
 *
 * An untransmitted varp still costs nothing: `transmit=` in the .varp config
 * decides, an undeclared varp is server-only, and the check is here rather than
 * at the call site so every writer gets it.
 */
void
ToriRSServer_WorldMarkVarp(
    struct ToriRSServerPlayer* player,
    int varp)
{
    const struct ToriRSServerVarpDef* def;
    int32_t value;

    if( varp < 0 || varp >= TORIRSSERVER_VARP_COUNT )
        return;
    def = ToriRSServer_ContentVarp(varp);
    if( !def || !def->transmit )
        return;
    /*
     * VARP_SMALL's value is a single signed byte. Special attack energy is in
     * tenths of a percent, so a full bar is 1000 and would land as -24; the
     * bank's tab counters occupy bits 0..25 of their varp. The encoder is
     * chosen per value rather than per call site, so content never has to know
     * there are two packets.
     */
    value = player->varps[varp];
    if( value >= -128 && value <= 127 )
        ToriRSServer_SendVarpSmall(player, varp, (int)value);
    else
        ToriRSServer_SendVarpLarge(player, varp, (int)value);
}

/*
 * A varp the engine also *keeps state for* has just been written.
 *
 * There are two ways a varp gets written and they used to have two different
 * sets of consequences. `ToriRSServer_WorldSetVarp` is the engine's own setter;
 * `SS_OP_POP_VARP` is `%varp = value` in a script, and it wrote
 * `player->varps[]` directly. Anything hanging off a varp therefore worked when
 * the engine set it and silently did nothing when content did — which is the
 * wrong way round, because content is where a varp is *supposed* to be written.
 *
 * `option_run` is the one that found it, and the symptom is worth recording
 * because it points nowhere near here: `::run` (engine setter) made the player
 * run, and clicking the run orb (content's `[if_button,orbs:runbutton]`, which
 * writes `%option_run`) lit the orb, transmitted the varp, and left the player
 * walking. Everything observable about the click was correct.
 *
 * Both writers call this, so a varp with an engine-side mirror cannot acquire
 * one that only half the writers honour. Keyed on the name, resolved from the
 * content pack, like every other id the engine addresses.
 */
static int g_carrier_writes;
static int g_carrier_write_last = -1;
/*
 * Which varps have already been reported, so each is named once.
 *
 * Every one of these writes is a content bug and the counter below still sees
 * all of them. The *report* is the problem: one unbuffered stderr write costs
 * about 6 ms on Windows (see `app_world_spawn_npc_now`), the embedded server
 * runs on the host's frame thread, and a varp written wholesale is written
 * wholesale on every tick that runs the script — `~player_combat_stat` writes
 * about thirty of them per swing. That turned one content bug into a 300 ms
 * tick, which is six dropped frames per attack. Reporting each varp once says
 * exactly as much and costs nothing after the first.
 */
static uint8_t g_carrier_reported[TORIRSSERVER_VARP_COUNT];

int
ToriRSServer_WorldCarrierWrites(int* out_last_varp)
{
    if( out_last_varp )
        *out_last_varp = g_carrier_write_last;
    return g_carrier_writes;
}

void
ToriRSServer_WorldCarrierWritesReset(void)
{
    g_carrier_writes = 0;
    g_carrier_write_last = -1;
    /* A fresh observation window wants to hear about the varps again — this is
     * what the selftest calls between scenarios. */
    memset(g_carrier_reported, 0, sizeof(g_carrier_reported));
}

/*
 * The runtime half of the carrier rule (docs/LOSTCITY_PORT_TRIAGE.md §7.5).
 *
 * sscompile refuses `%carrier = value` in a script, which covers content. It
 * cannot cover the other three writers — a `::` cheat, C, and a packet handler —
 * and CONTENT_ARCHITECTURE.md §8.2(d) is exactly about the end of a rule the
 * engine is left holding: *moving a rule to content leaves the engine holding
 * the other end of it, and the other end needs a test that fails when it is
 * dropped.* So the same fact is checked here, off the cache's own `basevar`
 * table, and the selftest asserts the count is zero.
 *
 * Three writes are not violations and are excluded by construction rather than
 * by a list: a varbit patch (which sets `ToriRSServer_VarbitPatching`), a varp
 * content declared `wholewrite=allow` for, and a varp nothing is based on.
 */
static void
check_carrier_write(
    int varp,
    int value)
{
    const struct ToriRSServerVarpDef* def;
    int bits;

    if( ToriRSServer_VarbitPatching() )
        return;
    bits = ToriRSServer_VarbitCarrierBits(varp);
    if( bits <= 0 )
        return;
    def = ToriRSServer_ContentVarp(varp);
    if( def && def->wholewrite_allowed )
        return;
    g_carrier_writes++;
    g_carrier_write_last = varp;
    if( varp >= 0 && varp < TORIRSSERVER_VARP_COUNT )
    {
        if( g_carrier_reported[varp] )
            return;
        g_carrier_reported[varp] = 1;
    }
    fprintf(stderr,
            "torirsserver: whole-varp write to varp %d (%s) = %d — %d varbit(s) are packed into "
            "it and this write destroys them; write the varbit, or declare "
            "`wholewrite=allow` on the varp\n",
            varp, ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_VARP, varp)
                      ? ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_VARP, varp)
                      : "?",
            value, bits);
}

static void
varp_side_effects(
    struct ToriRSServer* srv,
    int varp,
    int value)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    check_carrier_write(varp, value);

    /*
     * `option_run` is not a variable the server merely reports — it is where
     * the run flag lives. Without this the engine kept its own `run_toggle` and
     * mirrored it *out* to the varp, so a write from the other direction
     * transmitted a lit orb and the player still walked.
     */
    if( varp == ToriRSServer_WorldVarp("option_run") )
        player->run_toggle = value != 0;
}

void
ToriRSServer_WorldSetVarp(
    struct ToriRSServer* srv,
    int varp,
    int value)
{
    ToriRSServer_WorldSetVarpOn(srv, srv->active_player, varp, value);
}

/*
 * The same write on a NAMED player. See `ToriRSServer_VarbitSetOn` for why the
 * distinction exists at all: a script broadcasting to a hunted set is writing to
 * players who are not the one whose turn the server is taking.
 *
 * The side effects stay gated on the active player. They are engine state that
 * belongs to the turn being taken — `option_run` feeding `run_toggle` is the
 * shape — and applying them on behalf of a player the server is not currently
 * processing would move somebody else's run flag.
 */
void
ToriRSServer_WorldSetVarpOn(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int varp,
    int value)
{
    if( !player || varp < 0 || varp >= TORIRSSERVER_VARP_COUNT || player->varps[varp] == value )
        return;
    player->varps[varp] = value;
    ToriRSServer_WorldMarkVarp(player, varp);
    if( player == srv->active_player )
        varp_side_effects(srv, varp, value);
}

void
ToriRSServer_WorldVarpWritten(
    struct ToriRSServer* srv,
    int varp,
    int value)
{
    varp_side_effects(srv, varp, value);
}

void
ToriRSServer_WorldSetAttackStyle(
    struct ToriRSServer* srv,
    int style)
{
    ToriRSServer_WorldSetVarp(srv, attack_style_varp(), style);
}

void
ToriRSServer_WorldSetHome(
    int tile_x,
    int tile_z)
{
    g_home_x = tile_x;
    g_home_z = tile_z;
}

/*
 * The scene window a player's movement and view are judged against: pool slot
 * `1 + pid` (slot 0 is the root window, the world's boot anchor). The slot is
 * the player's for the lifetime of the pid — it may simply not be built yet,
 * which `ToriRSServer_SceneWindowBuilt` reports.
 */
struct ToriRSServerSceneWindow*
ToriRSServer_PlayerSceneWindow(const struct ToriRSServerPlayer* player)
{
    assert(player);
    return ToriRSServer_SceneWindowByIndex(1 + player->pid);
}

/*
 * Say whose turn it is. The one place `active_player` is written.
 *
 * Every subsystem that still reaches `srv->active_player` — the scripts, the
 * bank, combat, the world map — is asking "who am I doing this for", and this is
 * the answer. The per-player phases call it as they iterate and the session
 * calls it before dispatching a packet, so the question always has one.
 *
 * It is also the one bind policy for scene windows: the acting player's own
 * window when they have one built, the root window otherwise (including
 * player == NULL — world logic with nobody acting runs against the root).
 * Everything that rebinds temporarily (the loc mirror, per-window reapply)
 * saves and restores around itself, so between calls the binding always
 * reflects the active player.
 */
void
ToriRSServer_WorldSetActive(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    srv->active_player = player;
    if( player && ToriRSServer_SceneWindowBuilt(ToriRSServer_PlayerSceneWindow(player)) )
        ToriRSServer_SceneBindWindow(ToriRSServer_PlayerSceneWindow(player));
    else
        ToriRSServer_SceneBindWindow(ToriRSServer_SceneWindowRoot());
}

/*
 * Take a pool slot and give it its session.
 *
 * Called by a host before ToriRSServer_WorldLogin, because the session exists as
 * soon as the handshake does and the world does not. Splitting it out is what
 * makes "a world with no client" (the selftest) and "a world with one" the same
 * code path with a NULL in one field.
 *
 * The slot is the pid, and slots are never reused while occupied nor compacted
 * when freed: the wire carries the index, so moving a player would rename them
 * to every client tracking them.
 *
 * "Never reused while occupied" is not the whole hazard, though — a pid freed
 * by a logout and handed straight back out to a new connection, both drained
 * from the same between-tick pass of the host's connection loop, could still
 * be reused before any observer's PLAYER_INFO had a chance to report the old
 * occupant gone. The `pending_free` check below is what closes that: see
 * `ToriRSServer_WorldPlayerFree` and docs/torirs_server_npc_slot_reap.md, which is the
 * npc-side writeup of the identical hazard.
 */
struct ToriRSServerPlayer*
ToriRSServer_WorldAddPlayer(
    struct ToriRSServer* srv,
    struct ToriRSServerSession* session)
{
    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
    {
        struct ToriRSServerPlayer* player = &srv->players[i];
        uint32_t generation;

        if( player->active || player->pending_free )
            continue;
        generation = player->login_generation + 1;
        if( generation == 0 )
            generation = 1;
        memset(player, 0, sizeof(*player));
        player->active = 1;
        player->world = srv;
        player->pid = i;
        player->login_generation = generation;
        player->session = session;
        /* Logging in is input, and it has to be stated: the memset's 0 reads as
         * "idle since tick 0", so anyone joining a world older than
         * TORIRSSERVER_AFK_COMBAT_TICKS would arrive unable to fight. A slot with no
         * session has no client to time out — see `last_input_tick`. */
        player->last_input_tick =
            session ? (int32_t)srv->tick : TORIRSSERVER_INPUT_TICK_NEVER;
        ToriRSServer_IfStateInit(&player->interfaces);
        if( i >= srv->player_count )
            srv->player_count = i + 1;
        ToriRSServer_WorldSetActive(srv, player);
        return player;
    }

    /* Full. Refusing loudly is the only honest answer: silently overwriting
     * players[0] is what an implicit "the primary player" would have done. */
    fprintf(stderr, "torirsserver: the world is full (%d players); refusing the connection\n",
            TORIRSSERVER_PLAYER_MAX);
    return NULL;
}

/*
 * The despawn choke point for players. See docs/torirs_server_npc_slot_reap.md and
 * `ToriRSServer_WorldNpcFree`, whose comment this mirrors exactly — the only
 * difference is scale (one call site instead of five, `TORIRSSERVER_PLAYER_MAX`
 * instead of `TORIRSSERVER_NPC_MAX`).
 *
 * `active` still clears immediately — same-tick logic (friends lookups,
 * `ToriRSServer_WorldSetActive` bookkeeping, the caller's own use of the player
 * it is tearing down) has to keep seeing this pid as gone right away. What's
 * deferred is only the pid's ELIGIBILITY for `ToriRSServer_WorldAddPlayer`'s
 * free-slot scan, until `ToriRSServer_WorldPlayerReap` drains the queue — once
 * per tick, after every player's PLAYER_INFO for this tick has already gone
 * out.
 */
void
ToriRSServer_WorldPlayerFree(
    struct ToriRSServer* srv,
    int pid)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    if( pid < 0 || pid >= TORIRSSERVER_PLAYER_MAX )
        return;
    player = &srv->players[pid];
    if( !player->active )
        return; /* already gone (or already queued) — do not double-queue */

    player->active = 0;
    player->pending_free = 1;

    if( srv->player_free_queue_count < TORIRSSERVER_PLAYER_MAX )
    {
        struct ToriRSServerPlayerFreeCmd* cmd =
            &srv->player_free_queue[srv->player_free_queue_count++];
        cmd->pid = pid;
        cmd->generation = player->login_generation;
    }
    /* No overflow branch: sized to TORIRSSERVER_PLAYER_MAX and at most one command
     * per currently-active player can ever be pending between reaps. */

    /* The pid's scene window goes back with the slot, HERE at the choke point
     * rather than only in ToriRSServer_WorldRemovePlayer: maybe_rebuild builds
     * a window for every active player, and pids are reused, so a window left
     * built would hand the next login into this slot a fully stamped 104x104
     * of somebody else's map. Release also rebinds the root window if this
     * one happened to be bound. Then let the static-npc roster retire spawns
     * that only this window's centre kept in range. */
    ToriRSServer_SceneWindowRelease(ToriRSServer_PlayerSceneWindow(player));
    world_static_npcs_sync(srv);
}

/*
 * Once per tick, from phase_cleanup, after every player's PLAYER_INFO for
 * this tick has already gone out. Mirrors ToriRSServer_WorldNpcReap exactly.
 */
void
ToriRSServer_WorldPlayerReap(struct ToriRSServer* srv)
{
    assert(srv);
    for( int i = 0; i < srv->player_free_queue_count; i++ )
    {
        struct ToriRSServerPlayerFreeCmd* cmd = &srv->player_free_queue[i];
        struct ToriRSServerPlayer* player = &srv->players[cmd->pid];

        if( player->login_generation == cmd->generation )
            player->pending_free = 0;
    }
    srv->player_free_queue_count = 0;
}

void
ToriRSServer_WorldRemovePlayer(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player)
{
    int64_t name37;
    char display_name[sizeof(player->display_name)];

    assert(player);
    if( !player->active )
        return;
    /* Session-local mechanic state must not reach [logout] or a reused slot. */
    player->action_locked = 0;
    name37 = player->name37;
    /* Copied, not aliased: the slot this points into is reused by the next
     * login and the notification below runs after the slot has been released. */
    snprintf(display_name, sizeof(display_name), "%s", player->display_name);

    /*
     * `[logout]`, and it runs *above* the save on purpose.
     *
     * The reference puts this in the logout phase — `World.ts:780`,
     * `getByTriggerSpecific(LOGOUT, -1, -1)` with a protected active player,
     * immediately before `removePlayer` — and its content
     * (`login_logout/logout.rs2`) is a list of per-feature teardowns:
     * `~duel_arena_logout`, `~castlewars_logout`, the follower, the skull. That
     * is the shape of everything a session holds and nothing else can give
     * back.
     *
     * Here it hangs off `remove_player` rather than off `phase_logouts`,
     * because this function is the *only* logout path either host has (see the
     * save comment below): a socket disconnect calls it from the transport, not
     * through a tick phase, so a phase-only dispatch would silently skip the
     * case that matters most — the client that vanished mid-encounter.
     *
     * Above the save because a logout script's whole job may be to *move* the
     * player: a run that ends inside a map instance has to leave a saved coord
     * that is still map when the reservation goes back to the pool. Save first
     * and the character is stored standing on void.
     *
     * Two divergences from the reference, both stated rather than hidden:
     * it cannot *defer* (`canAccess()` / a non-empty engine queue make the
     * reference wait a tick, and a closed socket cannot wait), and a logout
     * script that suspends is dropped rather than parked, because the slot it
     * would park on is about to be reused.
     */
    {
        struct ToriRSServerPlayer* was_active = srv->active_player;

        ToriRSServer_WorldSetActive(srv, player);
        ToriRSServer_ScriptsRunTriggerSpecific(srv, SS_TRIGGER_LOGOUT, -1, -1, -1);
        if( player->active_script )
        {
            fprintf(stderr,
                    "torirsserver: [logout] suspended for %s; dropping the parked "
                    "script — a logout cannot wait for it\n",
                    display_name);
            ToriRSServer_ScriptsReleaseState(srv, player->active_script);
        }
        ToriRSServer_WorldSetActive(srv, was_active == player ? NULL : was_active);
    }
    /* A logout script may itself touch encounter state; the disconnected slot
     * still leaves unlocked regardless. */
    player->action_locked = 0;

    /*
     * The instance this session was standing in, released by default.
     *
     * This runs AFTER `[logout]` so content still gets first refusal: a script
     * that wants the square kept — a raid whose party is still inside — moves
     * the player out of it, and this then finds nobody there and does nothing.
     * What it will not do is leave a reservation alive because nobody
     * remembered to free it.
     *
     * Engine rather than content on purpose: which square is reserved, and for
     * how long, is bookkeeping about the simulation (PORTING_GUIDE §2.3's short
     * list) rather than a rule about the game. The *policy* — who may keep one
     * open, and for how long — stays content's, and this is the floor beneath
     * it, not a decision instead of it.
     *
     * A LINGERING instance is deliberately left whole, npcs included. The
     * linger clock (`world_mapinstance_linger`) owns its lifetime now: for the
     * linger window the character's saved coord is real map they can log back
     * into, content restarts the encounter from `[login]`, and the window
     * closing hands everything back through the same
     * `ToriRSServer_WorldMapInstanceFree` this used to call immediately. Only an
     * instance content opted OUT of linger (`map_instance_setlinger(h, 0)`)
     * still gets the immediate release, because for that one nothing else ever
     * will.
     *
     * In the immediate-release case npcs go with it. `map_instance_free`
     * deliberately leaves them and only counts them, because a content
     * teardown knows whose they are; a logout does not, and the pool re-issues
     * a released square immediately, so leaving them is how the next session
     * finds somebody else's boss already in the arena.
     */
    {
        int handle = ToriRSServer_MapInstanceFind(player->x, player->z);

        if( handle && ToriRSServer_MapInstanceLinger(handle) > 0 )
        {
            /* The leaving player IS proof of occupancy. The linger clock only
             * observes players once per tick, so a session that entered and
             * dropped inside one tick would otherwise leave `occupied_ever`
             * unset and the reservation unreclaimable forever. */
            ToriRSServer_MapInstanceLingerTick(handle, 1);
            if( getenv("TORIRSSERVER_VERBOSE") )
                fprintf(stderr,
                        "torirsserver: %s logged out inside map instance %d; leaving it "
                        "to linger\n",
                        display_name, handle);
        }
        else if( handle )
        {
            int others = 0;

            for( int i = 0; i < srv->player_count; i++ )
            {
                struct ToriRSServerPlayer* other = &srv->players[i];

                if( !other->active || other == player )
                    continue;
                if( ToriRSServer_MapInstanceFind(other->x, other->z) == handle )
                    others++;
            }
            if( others == 0 )
            {
                int cleared = 0;

                for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
                {
                    if( srv->npcs[i].active &&
                        ToriRSServer_MapInstanceFind(srv->npcs[i].x, srv->npcs[i].z) == handle )
                    {
                        /* The ordinary NPC_INFO remove — the same thing
                         * `npc_del` and a death do. See
                         * docs/torirs_server_npc_slot_reap.md for why this queues
                         * rather than clearing `active` directly. */
                        ToriRSServer_WorldNpcFree(srv, i);
                        cleared++;
                    }
                }
                ToriRSServer_WorldMapInstanceFree(srv, handle);
                if( getenv("TORIRSSERVER_VERBOSE") )
                    fprintf(stderr,
                            "torirsserver: %s logged out inside map instance %d; released it "
                            "and %d npc(s)\n",
                            display_name, handle, cleared);
            }
        }
    }

    /* `npc_setowner` binds a runtime actor to this login generation. Anything
     * still bound after feature-specific logout and instance teardown is an
     * abandoned private encounter actor; leaving it active makes it invisible
     * to every future login while still consuming a world slot. */
    for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active && ToriRSServer_WorldNpcOwner(srv, &srv->npcs[i]) == player )
            ToriRSServer_WorldNpcFree(srv, i);
    }

    /*
     * Tip-jar Setup may request that both balances move to the bank on logout.
     * This has to run here rather than in RuneScript: disconnect has no player
     * script turn, and the bank and durable POH record must enter the same save.
     * Each currency is all-or-nothing, so a full bank leaves that balance in
     * the jar instead of spilling or truncating it.
     *
     * https://oldschool.runescape.wiki/w/Tip_jar#Behaviour
     */
    if( player->poh.tip_auto_bank )
    {
        struct ToriRSServerContainer* bank =
            ToriRSServer_ContainerResolve(srv, player, ToriRSServer_Ids()->inv_bank);
        int coins = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "coins");
        int platinum = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_OBJ, "platinum");

        if( bank && player->poh.tip_coins > 0 && coins >= 0 &&
            ToriRSServer_ContainerAdd(bank, coins, player->poh.tip_coins, 1) ==
                player->poh.tip_coins )
            player->poh.tip_coins = 0;
        if( bank && player->poh.tip_platinum > 0 && platinum >= 0 &&
            ToriRSServer_ContainerAdd(bank, platinum, player->poh.tip_platinum, 1) ==
                player->poh.tip_platinum )
            player->poh.tip_platinum = 0;
    }

    /* RuneScript player_uid is a reusable pid + 1. A reservation may outlive
     * its host once social POHs allow guests to remain, so erase the binding
     * before this slot can be assigned to an unrelated login. Guests then see
     * an unavailable owner instead of crediting that new player.
     *
     * https://oldschool.runescape.wiki/w/Tip_jar#Behaviour
     */
    ToriRSServer_MapInstanceClearOwner(player->pid + 1);

    /*
     * Write the save first, while the player is still whole.
     *
     * Everything below this line takes something away — the bank is freed, the
     * slot is released, the friend service is told the name is gone — so a save
     * anywhere else in this function saves a partly demolished character. The
     * bank in particular is heap-allocated and `ToriRSServer_SavePlayer` walks it.
     *
     * This is the only logout path either host has: the socket server calls it
     * when the session dies and the embed host calls it when a client
     * disconnects, so both get persistence from one call.
     */
    ToriRSServer_SavePlayer(player, ToriRSServer_SavePath(display_name));

    /*
     * The bank is heap-allocated per player, so a logout that only cleared
     * `active` would leak it — and the next player into this slot would inherit
     * the pointer through the memset in ToriRSServer_WorldAddPlayer.
     */
    ToriRSServer_BankShutdownPlayer(player);
    /* Same argument for every other container this player accumulated: the
     * registry calloc'd them, and the memset in ToriRSServer_WorldAddPlayer would
     * otherwise hand the next player into this slot a pointer to them. */
    ToriRSServer_ContainerShutdownPlayer(player);
    /*
     * The friend service is told before the slot goes, because it is keyed by
     * name and the name is about to be unreachable. Only presence is dropped —
     * the lists stay, which is what lets a follower still see this name in
     * their panel with "Offline" beside it. The reference does the same thing
     * from `World.removePlayer` (World.ts:1590).
     */
    ToriRSServer_FriendsLogout(player->name37);
    player_set_occupancy(player, 0);
    /* The pid's scene window is released inside ToriRSServer_WorldPlayerFree
     * below — the despawn choke point every teardown path goes through, this
     * one included. Occupancy came out first (line above) so no other window
     * is left holding this player's stamp when the window goes. */
    /*
     * Out of the ZoneMap, both ends, BEFORE `active` clears.
     *
     * A pid is a pool index and the pool is reused, so a subscription left
     * behind is not merely stale — it is a subscription the *next* person to
     * log into this slot inherits, to zones they have never been near, with
     * whatever entities those zones hold already in their area. Both halves
     * have to go: the zones' subscriber lists hold this pid, and the player's
     * own filing holds them.
     *
     * That ZoneMap fix does not, by itself, protect PLAYER_INFO: a pid handed
     * straight back out to a new login before every observer's PLAYER_INFO has
     * reported this one gone would still read, to them, as the departed player
     * continuing. `ToriRSServer_WorldPlayerFree` is what closes that — see its
     * own comment and docs/torirs_server_npc_slot_reap.md.
     */
    ToriRSServer_PlayerzonemapClear(player);
    ToriRSServer_WorldPlayerFree(srv, player->pid);
    ToriRSServer_ZonePlayerRefile(srv, player->pid);
    player->session = NULL;
    /* Everyone else is holding this pid. Clearing `active` is what the next
     * PLAYER_INFO reads to retire it; nothing has to be sent from here. */
    if( srv->active_player == player )
        ToriRSServer_WorldSetActive(srv, NULL);
    /* `player_count` is a high-water mark, and it only shrinks when the tail of
     * the pool is empty — an interior hole has to stay iterated over. */
    while( srv->player_count > 0 && !srv->players[srv->player_count - 1].active )
        srv->player_count--;

    /*
     * Now that the slot is gone, tell the followers — the reference's
     * `broadcastWorldToFollowers` after `player_logout` (FriendServer.ts:159).
     *
     * After, not before, on purpose: the broadcast asks "is this name online",
     * and while the slot is still active the answer would be yes and every
     * follower would be told the world of a player who has just left. It also
     * has to come after `ToriRSServer_FriendsLogout`, which is what makes
     * `ToriRSServer_FriendsWorld` answer 0.
     */
    social_broadcast_to_followers(srv, name37);
    social_notify_followers(srv, name37, display_name,
                            SS_TRIGGER_FRIENDLOGOUT);
}

/*
 * The name the session collected at login.
 *
 * Separate from ToriRSServer_WorldPlayerInit, and called after it, because that
 * function memsets the player — a name written before it is a name erased by it.
 * This is the whole of why `displayname` used to come back empty.
 */
void
ToriRSServer_WorldSetDisplayName(
    struct ToriRSServerPlayer* player,
    const char* name)
{
    assert(name);
    if( !name[0] )
        return;
    snprintf(player->display_name, sizeof(player->display_name), "%s", name);
    /* The base-37 form is derived here and nowhere else: it is the key the
     * friend service files this player under, and two places packing it are two
     * places that can disagree about what this player is called. */
    player->name37 = (int64_t)strtobase37(player->display_name);
}

/*
 * Build the world. Runs once per world, however many players log in.
 *
 * The guard is not an optimisation. Re-running this on a second login would
 * respawn the npc roster on top of itself, return every taken ground spawn,
 * rebuild the scene under the first player and reset the tick counter — none of
 * which is visible as a crash, all of which is visible as the world lurching
 * whenever somebody connects.
 */
void
ToriRSServer_WorldInit(
    struct ToriRSServer* srv,
    int zone_x,
    int zone_z)
{
    if( srv->world_built )
    {
        if( srv->verbose )
            fprintf(stderr, "torirsserver: world already built; leaving it alone\n");
        return;
    }
    srv->world_built = 1;

    srv->zone_x = zone_x;
    srv->zone_z = zone_z;
    srv->rng = 0x5eed1234u;
    srv->tick = 0;

    /* Collision before anything is placed: a spawn on a blocked tile is worth
     * knowing about, and the walk helpers consult the scene from their first
     * call. */
    ToriRSServer_WorldSceneRebuild(srv);

    ToriRSServer_WorldBuildEntities(srv);
}

void
ToriRSServer_WorldReset(struct ToriRSServer* srv)
{
    srv->world_built = 0;
    /* Every scene window is the world's: the selftest runs many worlds in one
     * process, and a player window left built here would hand the next world's
     * first login a stamped 104x104 of the previous world's map. The next
     * WorldInit rebuilds the root; the player windows must start unbuilt. This
     * also rebinds the root, so no dangling preference survives the reset. */
    for( int i = 0; i < TORIRSSERVER_SCENE_WINDOW_MAX; i++ )
        ToriRSServer_SceneWindowRelease(ToriRSServer_SceneWindowByIndex(i));
    /* The windows are gone, so no player's placement survives either: a stale
     * attempt flag would make maybe_rebuild skip the next world's first
     * build for any player kept across the reset. */
    for( int i = 0; i < srv->player_count; i++ )
        srv->players[i].scene_build_attempted = 0;
    /* The ZoneMap is the world's memory of every runtime loc change and of
     * where every entity stands, so a world that is being thrown away has to
     * throw it away too — the selftest runs many worlds in one process, and a
     * surviving map would replay the previous world's doors into the next
     * one's. */
    ToriRSServer_ZoneFree(srv);
    /* Instance handles are the world's too: the selftest runs many worlds in one
     * process, and a surviving allocation would both leak its map squares out of
     * the pool and leave the next world's scene reading a previous world's
     * descriptors. */
    ToriRSServer_MapInstanceReset();
    /* Vessels hold instance handles, and the reset above just tore every
     * reservation down — a surviving vessel would sail on carrying a handle
     * the next world reissues to somebody else's POH. */
    memset(srv->vessels, 0, sizeof(srv->vessels));
    srv->vessel_count = 0;
    srv->npc_slot_max = 0;
    /* The roster's marks are the world's too, and for the same reason: the next
     * world's `ToriRSServer_WorldBuildEntities` memsets the npc pool, so a
     * surviving "already standing" byte would leave that spawn permanently
     * unrealised — an npc missing from a world for no reason anything logs. */
    srv->static_spawns_live = 0;
    srv->static_npcs_live = 0;
    world_static_npcs_reset();
}

/*
 * Reset one player to a newly-created character.
 *
 * Everything here used to be the tail of ToriRSServer_WorldInit, which is what made
 * a login and a world build the same event.
 */
void
ToriRSServer_WorldPlayerInit(struct ToriRSServerPlayer* player)
{
    /* The bank owns a heap allocation, and the memset below is what would
     * otherwise lose the pointer to it — a re-init (which the selftest does)
     * has to release the old container before the struct is cleared. */
    ToriRSServer_BankShutdownPlayer(player);
    ToriRSServer_ContainerShutdownPlayer(player);

    /*
     * The memset clears the *game* state, not the player's identity.
     *
     * `world`, `session`, `pid` and `active` are set by
     * ToriRSServer_WorldAddPlayer before this runs — the session exists as soon as
     * the handshake does, and the world does not — so they are saved across the
     * clear. Losing `session` here leaves a logged-in player the encoders
     * silently write nothing for; losing `world` crashes the first encoder that
     * reaches through it. Neither is a compile error.
     */
    {
        struct ToriRSServer* world = player->world;
        struct ToriRSServerSession* session = player->session;
        int pid = player->pid;
        uint32_t login_generation = player->login_generation;

        memset(player, 0, sizeof(*player));
        player->world = world;
        player->session = session;
        player->pid = pid;
        player->login_generation = login_generation;
        player->active = 1;
        ToriRSServer_IfStateInit(&player->interfaces);
    }
    /* On the home tile, not in the middle of the scene: the scene is 104 tiles
     * of whatever the origin zone happens to cover, and standing in the middle
     * of it puts you somewhere arbitrary. */
    player->x = g_home_x;
    player->z = g_home_z;
    player->combat_target = -1;
    player->level = 0;
    player->last_step_x = player->x - 1;
    player->last_step_z = player->z;
    player->follow_x = player->last_step_x;
    player->follow_z = player->last_step_z;
    player->waypoint_index = -1;
    player->walktrigger = -1;
    player_set_occupancy(player, 1);
    /* Same reason as the npc's: 0 is a sequence id, and the priority gate reads
     * this as the animation already queued for the tick. */
    player->anim_id = -1;
    /* Unarmed human_* appearance seqs — same ids put_appearance used to
     * hardcode. Content's ~update_bas replaces them when a weapon is worn. */
    player->readyanim = 808;
    player->turnanim = 823;
    player->walkanim = 819;
    player->walkanim_b = 820;
    player->walkanim_l = 821;
    player->walkanim_r = 822;
    player->runanim = 824;
    player->transmog_npc = -1;
    player->face_entity = -1;
    /* LostCity targetX/Z sentinel: memset leaves 0, which is a real half-tile
     * face point (scene origin), so reorient would fire a spurious FACE_COORD. */
    player->face_target_x = -1;
    player->face_target_z = -1;
    /* The memset above leaves this 0, which is a real dbtable id — so a
     * `db_findnext` with no query would iterate table 0 instead of reporting
     * that nothing was selected. Same class as `session->pending_opcode`. */
    player->db_query_table = -1;
    player->db_query_index = -1;
    player->db_query_column = -1;
    player->db_query_tuple = -1;
    /* -1, not the memset's 0, because 0 is a real obj id and a real backpack
     * slot: a script reading `last_useitem` outside a use-on must get a sentinel
     * rather than "the player used a Dwarf remains on it". `Player.ts:371-374`
     * declares all four `last_*` item fields -1; the two above these are left at
     * 0, which is a divergence this stage found rather than one it made. */
    player->last_useitem = -1;
    player->last_useslot = -1;
    player->last_subop = -1;
    /*
     * The two `updateMap` latches. -1 is what makes login fire `[zone]` and
     * `[mapzone]` with no `[zoneexit]`/`[mapzoneexit]` before them — the
     * reference gets the same from a fresh `Player` object and never resets
     * these fields anywhere, `cleanup()` included. A memset's 0 would be a real
     * map square (the south-west corner) and would fire an exit for a place the
     * player has never been.
     */
    player->last_zone_level = -1;
    player->last_zone_x = -1;
    player->last_zone_z = -1;
    player->last_map_x = -1;
    player->music_track = -1;
    player->ambient_scape = -1;
    player->ambient_script_map_x = -1;
    player->ambient_script_map_z = -1;
    player->last_map_z = -1;
    /* A memset leaves `zone_index` at 0, which is a real zone. This is what
     * makes the first flush compute an active window rather than believe the
     * player has been standing in the south-west corner of the map. */
    ToriRSServer_ZonePlayerReset(player);

    /*
     * Level 1 in everything, which is the *floor* rather than a starting state:
     * a stat of level 0 is not a weak character, it is an invalid one, and the
     * memset above would produce twenty-three of them. The reference draws the
     * line in the same place — `PlayerLoading.load()`'s `baseLevels[i] = 1` is
     * engine-side too.
     *
     * Which skills a new character starts *above* the floor is content's, and
     * says so in `[proc,newplayer_stats]` (player/newplayer.rs2): hitpoints at
     * 10, via the xp that means it.
     */
    for( int stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
    {
        player->stat_level[stat] = 1;
        player->stat_boosted[stat] = 1;
        player->stat_xp_tenths[stat] = 0;
    }
    /* memset left hitpoints at 0; sync copies hitpoints → boosted, so without
     * this the floor becomes 0/0 and the first damage event is a death. */
    player->hitpoints = player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS];
    ToriRSServer_CombatSyncHitpoints(player);

    player->dest_x = -1;
    player->dest_z = -1;
    player->waypoint_index = -1;
    player->place_dirty = 1;
    player->masks |= TORIRSSERVER_PMASK_APPEARANCE;

    /* Full energy, run off. The sent-values are deliberately impossible so the
     * first flush is a send whatever the starting state is. */
    player->run_energy = TORIRSSERVER_RUN_ENERGY_MAX;
    player->run_toggle = 0;
    player->running = 0;
    player->run_energy_sent = -1;
    player->run_weight_sent = INT32_MIN;
    /* Display-panel layout: Resizable Classic. The save overlays this; login
     * must not reset it after load. */
    player->client_layout_mode = 1;
    /* -1 is "no dropdown selection pending", and 0 is a real choice (Fixed),
     * so this cannot be left at the memset default. */
    player->settings_dropdown_choice = -1;

    for( int i = 0; i < TORIRSSERVER_INV_SLOTS; i++ )
    {
        player->inv[i].obj_id = -1;
        player->inv[i].count = 0;
        for( int v = 0; v < TORIRSSERVER_ITEM_VAR_MAX; v++ )
        {
            player->inv[i].var_key[v] = -1;
            player->inv[i].var_val[v] = 0;
        }
    }
    for( int i = 0; i < TORIRSSERVER_WORN_SLOTS; i++ )
    {
        player->worn[i].obj_id = -1;
        player->worn[i].count = 0;
        for( int v = 0; v < TORIRSSERVER_ITEM_VAR_MAX; v++ )
        {
            player->worn[i].var_key[v] = -1;
            player->worn[i].var_val[v] = 0;
        }
    }

    /*
     * The two containers that live inside the player struct join the registry
     * here, over the storage they already have and the dirty masks the
     * appearance path and two selftests already read. Everything else content
     * names is created on first use by ToriRSServer_ContainerResolve — no inv id
     * enters C, which is why the collection container needed no line here.
     *
     * The worn set is the one container a write to which changes how its owner
     * looks; the registry raises TORIRSSERVER_PMASK_APPEARANCE for it, so the two
     * places that used to remember that by hand no longer can forget.
     */
    ToriRSServer_ContainerAdopt(player, ToriRSServer_Ids()->inv_backpack, player->inv, TORIRSSERVER_INV_SLOTS,
                            &player->inv_dirty, NULL, 0);
    ToriRSServer_ContainerAdopt(player, ToriRSServer_Ids()->inv_worn, player->worn, TORIRSSERVER_WORN_SLOTS,
                            &player->worn_dirty, NULL, 1);

    /* The containers exist empty. What goes in them the first time a character
     * connects is content's — `[proc,newplayer_inv]` and `[proc,newplayer_bank]`
     * in player/newplayer.rs2, called from [login,_]. */
    ToriRSServer_BankInitPlayer(player);
    /* This client has no names for anybody yet. Explicit rather than relying on
     * the memset above, because 0 is a valid client slot and -1 is "free". */
    ToriRSServer_SlotMapReset(player);
}

/* ------------------------------------------------------------------ */
/* The world roster, and the window on to it                           */
/* ------------------------------------------------------------------ */

/*
 * Which roster entries are currently standing up.
 *
 * One byte per spawn, allocated on first use and sized to the content tree's
 * roster, which does not change while a process lives. File-static rather than
 * a field on the server for the same reason `ToriRSServer_ContentNpcSpawns` is:
 * the roster belongs to the content, and the selftest runs many *worlds*
 * against one content load. `world_static_npcs_reset` is what a new world
 * calls, and it clears the marks without reallocating.
 */
static uint8_t* g_static_realised;
static int g_static_realised_count;

static void
world_static_npcs_reset(void)
{
    int count = 0;

    ToriRSServer_ContentNpcSpawns(&count);
    if( count != g_static_realised_count )
    {
        free(g_static_realised);
        g_static_realised = count > 0 ? calloc((size_t)count, 1) : NULL;
        g_static_realised_count = g_static_realised ? count : 0;
        return;
    }
    if( g_static_realised )
        memset(g_static_realised, 0, (size_t)g_static_realised_count);
}

/* Chebyshev distance from a tile to the NEAREST of the sync's centres. */
static int
static_spawn_nearest_dist(
    const int* centre_x,
    const int* centre_z,
    int centre_count,
    int x,
    int z)
{
    int best = INT_MAX;

    assert(centre_x);
    assert(centre_z);
    assert(centre_count > 0);
    for( int i = 0; i < centre_count; i++ )
    {
        int dx = abs(x - centre_x[i]);
        int dz = abs(z - centre_z[i]);
        int dist = dx > dz ? dx : dz;

        if( dist < best )
            best = dist;
    }
    return best;
}

/*
 * Stand up the roster entries near the world's windows, retire the ones that
 * are near none of them.
 *
 * "Near" is against a set of centres now, not one: the root window's centre
 * (the boot anchor, always) plus the centre of every player's built window.
 * A spawn stands up within TORIRSSERVER_STATIC_SPAWN_IN of ANY centre and is
 * retired only beyond TORIRSSERVER_STATIC_SPAWN_OUT of ALL of them — the
 * hysteresis band is per centre, so a player walking a boundary still cannot
 * make a spawn flap, and a second player far away keeps their own
 * neighbourhood alive without disturbing anyone else's.
 *
 * Chebyshev distance from each centre tile, not Euclidean: the scene is a
 * square and so is everything else that reasons about it, and a circle
 * inscribed in it would retire npcs in the corners of the very window a
 * client is holding.
 *
 * Both halves are unconditional passes over their own domain rather than a diff
 * of the two, because the pool and the roster fall out of step for reasons that
 * are nobody's bug: an npc dies and its slot is reused, `npc_spawn` declines
 * because the pool is full, a script removes one. A pass that re-derives the
 * answer from `active` and `static_spawn` cannot drift; a pass that trusted a
 * remembered delta would leak a slot the first time one of those happened.
 */
static void
world_static_npcs_sync(struct ToriRSServer* srv)
{
    int count = 0;
    const struct ToriRSServerMapNpcSpawn* spawns = ToriRSServer_ContentNpcSpawns(&count);
    int centre_x[1 + TORIRSSERVER_PLAYER_MAX];
    int centre_z[1 + TORIRSSERVER_PLAYER_MAX];
    int centre_count = 0;
    int live = 0;
    int declined = 0;

    if( !srv->static_spawns_live || !spawns || count <= 0 )
        return;
    if( g_static_realised_count != count )
        world_static_npcs_reset();
    if( !g_static_realised )
        return;

    centre_x[centre_count] = ToriRSServer_SceneOrigin(srv->zone_x) + TORIRSSERVER_SCENE_TILES / 2;
    centre_z[centre_count] = ToriRSServer_SceneOrigin(srv->zone_z) + TORIRSSERVER_SCENE_TILES / 2;
    centre_count++;
    for( int i = 0; i < srv->player_count; i++ )
    {
        struct ToriRSServerPlayer* player = &srv->players[i];

        if( !player->active )
            continue;
        if( !ToriRSServer_SceneWindowBuilt(ToriRSServer_PlayerSceneWindow(player)) )
            continue;
        centre_x[centre_count] =
            ToriRSServer_SceneOrigin(player->zone_x) + TORIRSSERVER_SCENE_TILES / 2;
        centre_z[centre_count] =
            ToriRSServer_SceneOrigin(player->zone_z) + TORIRSSERVER_SCENE_TILES / 2;
        centre_count++;
    }

    /*
     * Retire first. This used to also make the freed slots available to the
     * incoming npcs below in the same pass — it no longer does, on purpose:
     * `ToriRSServer_WorldNpcFree` defers a slot's reuse eligibility until
     * `ToriRSServer_WorldNpcReap` runs at the end of the tick, after every
     * player's NPC_INFO has already gone out (docs/torirs_server_npc_slot_reap.md).
     * An incoming spawn below that would have landed on one of these exact
     * slots instead gets declined and picked up on the next rebuild pass —
     * a one-pass lag, not a capacity problem against a 4096-slot pool. Still
     * retiring first, rather than after the incoming loop: a player crossing
     * a boundary gains and loses roughly the same number, and doing it the
     * other way round would make the pool briefly need to hold both sides at
     * once regardless of the reap timing.
     */
    for( int slot = 0; slot < srv->npc_slot_max; slot++ )
    {
        struct ToriRSServerNpc* npc = &srv->npcs[slot];
        int index = npc->static_spawn;

        if( !npc->active || index < 0 || index >= count )
            continue;
        if( static_spawn_nearest_dist(centre_x, centre_z, centre_count, spawns[index].x,
                                      spawns[index].z) <= TORIRSSERVER_STATIC_SPAWN_OUT )
            continue;
        /* The ordinary NPC_INFO remove path, the same one a despawn uses: the
         * client is told the way it is told about everything else. Nothing here
         * is a death, so no drop is rolled and no `[ai_death]` runs — this npc
         * is not dying, the world is looking away from it. */
        npc_set_occupancy(npc, 0);
        ToriRSServer_WorldNpcFree(srv, slot);
        /*
         * And out of the ZoneMap, now rather than at the next sync.
         *
         * torirs_server_zone.h says the pool allocators must refile before they
         * recycle a slot, and this is one — orthogonal to `pending_free`,
         * which only gates *pool* reuse: the ZoneMap's own membership is a
         * separate concern, and waiting for phase 8 to reconcile it was
         * invisible while a client's area was rebuilt from the map every
         * tick, and is not now — the area is pushed to, so an entity that
         * leaves the map without saying so stays in whichever areas had
         * already taken it.
         */
        ToriRSServer_ZoneNpcRefile(srv, slot);
        g_static_realised[index] = 0;
    }

    for( int i = 0; i < count; i++ )
    {
        int slot;

        if( g_static_realised[i] )
        {
            live++;
            continue;
        }
        if( static_spawn_nearest_dist(centre_x, centre_z, centre_count, spawns[i].x,
                                      spawns[i].z) > TORIRSSERVER_STATIC_SPAWN_IN )
            continue;
        slot = npc_spawn(srv, spawns[i].npc_id, spawns[i].x, spawns[i].z, spawns[i].level);
        if( slot < 0 )
        {
            declined++;
            continue;
        }
        srv->npcs[slot].static_spawn = i;
        g_static_realised[i] = 1;
        live++;
    }

    srv->static_npcs_live = live;
    /* `npc_spawn` already names each refusal; this says how big the shortfall
     * was, which is the number that decides whether TORIRSSERVER_NPC_MAX is wrong.
     * Silence here would read as a scene bug — an empty patch of world — rather
     * than as the pool running out. */
    if( declined )
        fprintf(stderr,
                "torirsserver: %d roster spawn(s) declined across %d window centre(s) — the "
                "npc pool (%d) is smaller than these windows need\n",
                declined, centre_count, TORIRSSERVER_NPC_MAX);
}

/*
 * The npcs and ground objs the map squares state. Part of the world, not of a
 * login — which is why it moved out of what is now ToriRSServer_WorldPlayerInit.
 */
static void
ToriRSServer_WorldBuildEntities(struct ToriRSServer* srv)
{
    /*
     * The npc roster comes from the content tree's `.spawn` files — 23,139 of
     * them, every static npc in OldSchool, sourced and checked against this
     * cache by tools/gen_spawns.py (docs/ITEM_AND_NPCS.md).
     *
     * This used to create all of them, and the comment that stood here defended
     * it: the client is told about npcs within 15 tiles, but the player can
     * walk, and an npc that does not exist until you approach it would pop into
     * being with full hitpoints in front of you.
     *
     * The defence is right and the implementation could not survive the real
     * roster — 23,139 npcs cannot exist at once on a wire whose npc slot is 14
     * bits. `world_static_npcs_sync` keeps the property and drops the loop: a
     * spawn stands up 160 tiles out and is retired at 224, so it has existed
     * for a hundred tiles' walking before anyone can see it. See
     * TORIRSSERVER_STATIC_SPAWN_IN.
     */
    memset(srv->npcs, 0, sizeof(srv->npcs));
    /* memset leaves death/respawn/despawn at 0. Respawn treats `respawn_tick < 0`
     * as "not scheduled", so a zeroed slot would be revived on tick 0 as a
     * ghost and fill the whole pool before content ever npc_adds. */
    for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        srv->npcs[i].death_tick = -1;
        srv->npcs[i].respawn_tick = -1;
        srv->npcs[i].despawn_tick = -1;
        srv->npcs[i].anim_id = -1;
    }
    {
        int count = 0;

        ToriRSServer_ContentNpcSpawns(&count);
        world_static_npcs_reset();
        srv->static_spawns_live = 1;
        world_static_npcs_sync(srv);

        if( count == 0 )
        {
            /* No content tree. One npc beside the home tile keeps every
             * npc-facing path — talking, fighting, NPC_INFO itself — reachable
             * rather than dead, the same way the script fallbacks do. */
            npc_spawn(srv, 3105, g_home_x + 2, g_home_z + 1, 0);
        }
        fprintf(stderr, "torirsserver: npc roster %d spawn(s), %d standing, slot_max=%d\n",
                count, srv->static_npcs_live, srv->npc_slot_max);
        /*
         * Config ids the wire cannot state in an add record.
         *
         * NPC_INFO's add carries TORIRSSERVER_NPC_TYPE_BITS of type — 14, which is
         * the deob's `gBits(14)`. A larger config id is legal and reaches the
         * client through CHANGE_TYPE in the same packet
         * (`npc_initial_wire_type`), so this is a note rather than an error;
         * what it protects against is that shim quietly being removed while
         * content still allocates above the field.
         *
         * Not to be confused with the npc's *index*, which is 16 bits and is
         * this client's own name for it (struct ToriRSServerPlayerSlotMap) — that
         * one no longer has anything to do with how big the world pool is.
         */
        {
            int wide = 0;

            for( int slot = 0; slot < srv->npc_slot_max; slot++ )
                if( srv->npcs[slot].active && srv->npcs[slot].type > TORIRSSERVER_NPC_TYPE_MAX )
                    wide++;
            if( wide )
                fprintf(stderr,
                        "torirsserver: %d npc(s) carry a config id above %d — reaching the client "
                        "through CHANGE_TYPE, not the add record\n",
                        wide, TORIRSSERVER_NPC_TYPE_MAX);
        }
    }

    /* Ground objs, from the same map squares. Duration -1 marks them spawns,
     * which come back after they are taken. */
    memset(srv->ground, 0, sizeof(srv->ground));
    {
        int count = 0;
        const struct ToriRSServerMapObjSpawn* spawns = ToriRSServer_ContentObjSpawns(&count);

        for( int i = 0; i < count; i++ )
            ToriRSServer_WorldObjAdd(srv, spawns[i].obj_id, spawns[i].count, spawns[i].x,
                                  spawns[i].z, spawns[i].level, -1);
    }
}

void
ToriRSServer_WorldLogin(struct ToriRSServerPlayer* player)
{
    struct ToriRSServer* srv = player->world;

    /* Everything below is this player's, and several of the calls still reach
     * the world for it. */
    ToriRSServer_WorldSetActive(srv, player);

    /*
     * -1. The save, before anything reads or sends the player's state.
     *
     * `ToriRSServer_SavePlayer`/`ToriRSServer_LoadPlayer` were written complete and
     * then had **no callers at all** — persistence has been dead code since it
     * landed, which is why `player/newplayer.rs2`'s `%newplayer_seeded` gate
     * reads as a declaration of intent rather than a behaviour. These two calls
     * (and the one in ToriRSServer_WorldRemovePlayer) are the whole of it.
     *
     * Position matters twice over. Before step 1, because everything below
     * *sends* what the load just changed — the scene is placed from `x`/`z`,
     * UPDATE_STAT and UPDATE_INV_FULL go out of steps 4 and 5, and a load after
     * them would leave the client showing a fresh character. And before
     * `[login,_]` (which phase 7 runs on the next tick), because that is what
     * makes the seed gate work at all: `~newplayer_setup` tests
     * `%newplayer_seeded`, and a returning player has to be carrying it by
     * then or the opening kit is dealt again on top of whatever they had.
     *
     * A missing file is a new character, not an error — `ToriRSServer_LoadPlayer`
     * says so by returning 0 on ENOENT — so there is nothing to handle here.
     */
    ToriRSServer_LoadPlayer(player, ToriRSServer_SavePath(player->display_name));
    /* The save restores boosted HP into the stat array; the DAMAGE mask and
     * death check read player->hitpoints. LostCity's PlayerLoading writes both
     * together (`levels[i] = sav.g1()`); hydrate here so a returning character
     * is not already dead before the first hit. */
    player->hitpoints = player->stat_boosted[TORIRSSERVER_STAT_HITPOINTS];
    ToriRSServer_CombatSyncHitpoints(player);

    /*
     * The save decides where this player stands, and this player's own scene
     * window has never been built.  Centre and build it before the login
     * REBUILD/GPI init is encoded.  If this is deferred to phase_info on the
     * first tick, the client receives a perfectly valid GPI coordinate that is
     * outside its current WorldView: the tracker is high-resolution but no
     * local Player entity can be materialized.  RuneLite then enters
     * LOGGED_IN and delivers StatChanged/GameTick callbacks with
     * getLocalPlayer() == null until the corrective rebuild arrives.
     *
     * maybe_rebuild builds only the windows whose owners need one — for a
     * fresh login that is exactly this player, whose window is unbuilt. The
     * explicit login rebuild below fulfils the debt it marks; other players'
     * windows are untouched, so nobody else owes anything.
     */
    if( srv->verbose )
        fprintf(stderr,
                "torirsserver: login scene preflight player=%d,%d level=%d "
                "window_zone=%d,%d built=%d players=%d active=%d\n",
                player->x, player->z, player->level, player->zone_x, player->zone_z,
                ToriRSServer_SceneWindowBuilt(ToriRSServer_PlayerSceneWindow(player)),
                srv->player_count, player->active);
    maybe_rebuild(srv);
    if( srv->verbose )
        fprintf(stderr,
                "torirsserver: login scene ready zone=%d,%d origin=%d,%d pending=%d\n",
                player->zone_x, player->zone_z, ToriRSServer_SceneOrigin(player->zone_x),
                ToriRSServer_SceneOrigin(player->zone_z), player->rebuild_pending);

    /*
     * 0. Presence, before any packet.
     *
     * The reference's `player_login` message to the friend server
     * (FriendServer.ts:107-142), minus the parts a one-process, one-world
     * server has no use for. What it does NOT do here is the other half of that
     * handler — the friend-list dump and the follower broadcast — because those
     * are packets and this module owns no encoder; they belong with the wire.
     * Registering here regardless is what makes the roster true even while the
     * wire is unwritten: `alice adds bob` has to work whether or not anything
     * is told about it.
     *
     * Chat modes come in at the reference's own defaults (Player.ts:307-309 —
     * public, private and trade all ON, which is 0 in each of the three
     * encodings) rather than off a save, because nothing persists them; see
     * torirs_server_friends.h.
     */
    ToriRSServer_FriendsLogin(player->name37, /* public */ 0, TORIRSSERVER_CHAT_PRIVATE_ON,
                          /* trade */ 0, /* staff level */ 0);

    /*
     * 1. The scene.
     *
     * Revision 230 applies its rebuild synchronously and retains the legacy
     * one-piece login burst. Revision 239 swaps WorldViews asynchronously and
     * explicitly reports completion with MAP_BUILD_COMPLETE; packets sent now
     * are not evidence that the new scene consumed them. Hold every dependent
     * packet until that acknowledgement reaches handle_map_build_complete.
     */
    /*
     * 0b. The deferred reconnect response, between the save and the rebuild.
     *
     * torirs_server_session.c holds RECONNECT_OK back precisely to reach this point:
     * the response carries the player-info init block, and the block states
     * where the player is, which the save above has only just decided. It also
     * has to precede every game packet — a login response read out of the game
     * stream is 3+ bytes of desync, not a late message — and the rebuild on
     * the next line is the first of them.
     */
    if( player->session && player->session->reconnect )
    {
        ToriRSServer_SendReconnectOk(player);
        player->session->reconnect = 0;
    }

    player->login_pending = 0;
    player->login_scene_pending = srv->wire && srv->wire->revision >= 239;
    ToriRSServer_SendRebuild(player);
    player->rebuild_pending = 0;
    if( player->login_scene_pending )
    {
        if( srv->verbose )
            fprintf(stderr,
                    "torirsserver: login scene barrier armed; waiting for MAP_BUILD_COMPLETE\n");
        return;
    }

    ToriRSServer_WorldLoginFinish(player);
}

/* Everything that requires the rebuilt scene and its embedded GPI init. */
static void
ToriRSServer_WorldLoginFinish(struct ToriRSServerPlayer* player)
{
    struct ToriRSServer* srv = player->world;
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();

    ToriRSServer_WorldSetActive(srv, player);

    /* Root-world info batches start by selecting their world and plane. The
     * login rebuild's GPI block only seeds the player-coordinate tracker. */
    ToriRSServer_SendSetActiveWorld(player);

    /*
     * 1b. Establish the local actor before any plugin-visible client state.
     *
     * UPDATE_STAT posts RuneLite StatChanged events synchronously while the
     * packet is decoded.  When the first PLAYER_INFO used to live at the end of
     * the login burst, getLocalPlayer() was still null during all 23 events;
     * the stock Agility plugin consequently threw on login.  A painted scene
     * hid the ordering bug because the actor appeared a few packets later.
     *
     * Classic revisions need UPDATE_PID first. Revision 239 receives the same
     * index in the login response (its UPDATE_PID encoder deliberately emits
     * nothing), then the rebuild's embedded GPI init seeds that index. In both
     * cases the following PLAYER_INFO is the transition that materializes the
     * actor in the freshly rebuilt world view.
     */
    ToriRSServer_SendUpdatePid(player, player->pid);
    ToriRSServer_SendPlayerInfo(player);

    /* Prime client-visible wall-clock state before mounting the gameframe.
     * magic_spellbook's onload compares date_minutes (varp 3078) with the Home
     * and Minigame Teleport last-use stamps (892/888). A fresh client has zero
     * for all three, so mounting first makes 0-0 look like both teleports were
     * just used and dims them. The proc owns the var meanings and the minute
     * timer; this ordering is the engine's responsibility. */
    ToriRSServer_ScriptsRunProc(srv, "[proc,teleport_cooldowns_login]", NULL, 0);

    /* 2. Gameframe root + the HUD and sidebar panels mounted into it.
     *    Mount table is content `gameframe.enum`. Open the top that matches the
     *    saved Display-panel preference immediately (Fixed / Classic / Modern);
     *    ~gameframe_login_mode then syncs the client canvas without queuing a
     *    second IF_OPENTOP.  A second open on the following tick rebuilds every
     *    widget and discards all IF_SETEVENTS sent by the login procs.  Actual
     *    WINDOW_STATUS mode changes still use ~gameframe_set_mode and remount.
     *    Mode defaults to 1 in player init; the save overlays it. Do not reset
     *    it here. */
    {
        int mode = player->client_layout_mode;
        int iface = ids->iface_gameframe;
        int32_t args[1];

        if( mode < 0 || mode > 2 )
            mode = 1;
        player->client_layout_mode = mode;
        if( mode == 0 )
            iface = ids->iface_toplevel;
        else if( mode == 2 )
            iface = ids->iface_toplevel_pre_eoc;
        ToriRSServer_GameframeOpentop(player, iface);
        args[0] = mode;
        ToriRSServer_ScriptsRunProc(srv, "[proc,gameframe_login_mode]", args, 1);
    }

    /* Revision 239 moved the stock camera limits out of cache-script literals
     * and into four server-initialised varcs. Clientscript 605 writes exactly
     * (small min, small max, large min, large max); camera_do_zoom clamps both
     * wheel and slider paths against them. Leaving them at Java's zero default
     * collapses every input to a single FOV and makes all three controls look
     * dead even though AWT and the CS2 callbacks ran. These are the stock
     * limits used by the revision-239 scripts: 128 outer, 896 inner, for both
     * viewport endpoints. */
    if( srv->wire && srv->wire->revision >= 239 )
    {
        static const int zoom_limits[4] = { 128, 896, 128, 896 };
        ToriRSServer_SendRunClientscript(player, 605, zoom_limits, 4);
    }

    /*
     * Arm the client-side helpers the Activities settings are made of.
     *
     * Clientscript 4743 is the cache's own initialiser for that whole layer:
     * it installs the CLIENTOP_* rows ("Mark tile", "Tag") and sets up every
     * HIGHLIGHT_* group from its varbit -- the tile indicators, Agility
     * obstacles, fishing spots, poll booths, the clue scroll helper. Nothing
     * inside the cache calls it. Its two entry points are clientscript 876,
     * which the reference server runs at login, and 5487, which takes no
     * arguments and reaches it through 5488.
     *
     * 5487 is sent rather than 876 because 5488's body is a strict SUBSET of
     * 876's -- 4743, 5325, 4560, all three of which 876 also calls -- while
     * 876 additionally wants the login message's four arguments (a welcome
     * line and a last-login stamp) that this server does not have. Sending
     * 876 with invented arguments would arm the same layer and print a wrong
     * welcome message beside it.
     *
     * Without this the whole Activities category is inert in a way that looks
     * exactly like the client ignoring it: the panel's toggles write their
     * varbits, and the scripts that read them never run. See
     * NXT_CLIENT_PLUGINS.md.
     */
    if( srv->wire && srv->wire->revision >= 239 )
        ToriRSServer_SendRunClientscript(player, 5487, NULL, 0);

    /*
     * 3. What the item containers permit is content's.
     *
     * `~containers_login` and `~worn_tab_login` (player/containers.rs2) arm the
     * drag bits and the worn tab's Remove. This was two `if_setevents` calls
     * here with the mask spelled as `1 << 17 | 1 << 20`, and eleven more in
     * torirs_server_equipment.c — UI permissions, decided in C, which is the one kind
     * of decision the rev-230 protocol moved to the server precisely so it
     * could be a policy rather than a cache flag.
     *
     * There is no reference for it: the 2004 protocol has no IF_SETEVENTS, so
     * LostCity has nothing to port. The shape follows its `[login]` procs.
     */
    /*    …and on the world map orb, whose verbs are the server's alone. */
    ToriRSServer_WorldMapLogin(srv);

    /* 4. Player state. The pid and first PLAYER_INFO were deliberately sent in
     *    step 1b, before anything here can post a RuneLite event. */
    /* Both orb numbers, unconditionally: the per-tick flush only sends what
     * changed, and a session that starts full would otherwise never send one. */
    player->run_energy_sent = player->run_energy * 100 / TORIRSSERVER_RUN_ENERGY_MAX;
    ToriRSServer_SendRunEnergy(player, player->run_energy_sent);

    /* The combat tab's varps are NOT sent here. They are ordinary varp writes
     * in [login,_] (content/scripts/player/login.rs2), declared transmit=yes in
     * player_controls.varp, and phase 10 puts them on the wire like any other
     * changed varp. A server operator changing the opening attack style should
     * not need a compiler. */
    player->run_weight_sent = player_weight_grams(player) / 1000;
    ToriRSServer_SendRunWeight(player, player->run_weight_sent);
    for( int stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
        ToriRSServer_SendStat(
            player,
            stat,
            player->stat_level[stat],
            player->stat_xp_tenths[stat] / 10,
            player->stat_boosted[stat]);

    /*
     * 4b. The varps a save brought back, in full.
     *
     * The other half of persistence, and the half that is easy to miss: the
     * loader writes `player->varps[]` directly, so nothing marks them changed
     * and phase 10 — which sends only what moved *this tick* — has nothing to
     * send. The state was restored perfectly and the client was never told, so
     * a returning player's Tool Leprechaun store read 0/100 over five rakes
     * that really were in it.
     *
     * Non-zero is the right filter, not "was in the file": the client starts
     * every session with a zeroed varp table, so a zero is already agreed and
     * anything else has to be stated. For a new character that sends nothing at
     * all, which is exactly what it did before.
     *
     * Sent directly rather than through `ToriRSServer_WorldMarkVarp`, for the same
     * reason the containers above are: the dirty list is 64 entries wide and
     * describes one tick's changes, and a character with a hundred perm varps
     * is not a change.
     */
    for( int varp = 0; varp < TORIRSSERVER_VARP_COUNT; varp++ )
    {
        const struct ToriRSServerVarpDef* def;
        int32_t value = player->varps[varp];

        if( value == 0 )
            continue;
        def = ToriRSServer_ContentVarp(varp);
        if( !def || !def->transmit )
            continue;
        if( value >= -128 && value <= 127 )
            ToriRSServer_SendVarpSmall(player, varp, (int)value);
        else
            ToriRSServer_SendVarpLarge(player, varp, (int)value);
    }

    /*
     * 5. Containers, in full. Deltas take over from the next tick.
     *
     * A bind, not a bare send: binding is what puts the container on the
     * registry's flush list, and it sends the full update on the way (the
     * reference's `invListenOnCom`, which transmits when a listener is added).
     * The engine binds these two because nothing else does — no content script
     * runs `inv_transmit` for the sidebar's own panels — and the ids are still
     * content's, resolved names rather than literals.
     */
    ToriRSServer_ContainerBind(srv, player, ids->inv_backpack, ids->com_inventory_items);
    ToriRSServer_ContainerBind(srv, player, ids->inv_worn, TORIRSSERVER_COM(ids->iface_wornitems, 0));

    /* The combat tab builds itself from these; nothing else sends them. */
    ToriRSServer_WorldSyncCombatVarbits(srv);

    /* Anything a script would want to say belongs in [login], which phase 7
     * runs on the next tick. There was an empty `if( !srv->scripts_ok ) {}` here
     * — the two login messages that used to be its no-content fallback moved to
     * `player/messages.rs2` and left the branch behind. Deleted rather than
     * refilled: with no pack, a login that greets you is the one thing that
     * would make an otherwise dead server look alive. */
    player->login_pending = 1;

    /*
     * 5b. The social burst — the second half of the reference's `player_login`
     *     friend-server message (FriendServer.ts:136-141), whose first half
     *     (registering presence) ran at step 0 above.
     *
     *     It is here, after the panels are mounted, because the friends panel
     *     builds every row from the *client's* store on its onload: a store
     *     already populated when 429 mounts draws rows on the first paint
     *     instead of waiting for a repaint the friend-transmit channel does not
     *     exist to deliver yet (FRIENDS_PRIVATE_CHAT.md §4.3).
     */
    ToriRSServer_WorldSocialLogin(player);

    /* 6. The local player was placed in step 1b; now spawn the npcs and close
     *    the login tick. */
    ToriRSServer_SendNpcInfo(player);

    /*
     * 7. Music.
     *
     * The real server picks a track from the region the player is standing in;
     * this mock has no region-to-music table, so it sends one track on login.
     * That is not authenticity, it is *reachability*: until something sends
     * MIDI_SONG the entire music path -- the packed-MIDI unpacker, the
     * soundbank loader, the synth, the stream -- is unreachable from a running
     * client, and a subsystem nothing can reach is a subsystem nobody notices
     * is broken. Override with TORIRSSERVER_SONG=<id>, or TORIRSSERVER_SONG=-1 for none.
     */
    {
        const char* override = getenv("TORIRSSERVER_SONG");
        int song = override ? atoi(override) : 0;
        if( song >= 0 )
        {
            player->music_track = song;
            ToriRSServer_SendMidiSong(player, song);
        }
    }

    /*
     * 7b. The ambient bed is NOT sent here.
     *
     * It used to be, unconditionally, on the same reachability argument as the
     * song above -- and that made it a property of the *session* rather than of
     * the place, which is wrong in a way a login-time send cannot express: the
     * bed then played under every square the player ever stood on, including a
     * foreign rev-727 region whose ambience is carried entirely by its own loc
     * emitters. Two soundscapes, one of them from the wrong game.
     *
     * So it hangs off the map-square latch instead, beside the music that is
     * keyed the same way -- see `ToriRSServer_AmbientEnterRegion`. The latch fires
     * for the first time on the login tick (both `last_map_*` are -1), so the
     * path stays exactly as reachable as it was.
     */

    ToriRSServer_SendTickEnd(player);
}

/* ------------------------------------------------------------------ */
/* The tick                                                            */
/* ------------------------------------------------------------------ */

/*
 * Eleven phases, in the order LostCity's World.cycle() runs them.
 *
 * The order is not decoration. It is what decides, for example, that a script
 * suspended in phase 5 cannot resume in the same tick, that an npc spawned in
 * phase 4 gets its [ai_spawn] in the *next* tick's phase 3, and that a scene
 * rebuild is computed (9) before anything is encoded (10). Matching it means
 * behaviour ports across from the reference instead of being re-derived.
 *
 * Several phases are empty until the feature that fills them lands. They exist
 * now anyway: an empty named phase says where the work goes, whereas an absent
 * one invites putting it in whichever phase happens to be nearby.
 */

/*
 * TORIRS_SERVER_BREAKDOWN=<ms>: split one world tick by phase, and phase 5 by
 * the step inside it, printing the split when the tick exceeds <ms>.
 *
 * The embedded server shares the client's frame thread, so a slow tick is a
 * dropped frame -- and the tick is a 600 ms heartbeat that lands on one frame
 * in thirty, which is exactly the shape of a spike that disappears into the
 * averages. Off unless the variable is set: an unconditional write on a path
 * this hot costs more than what it measures (see app_world_spawn_npc_now).
 */
enum
{
    TICK_BD_PHASES = 13
};

enum
{
    PP_SCRIPTS,
    PP_FACE,
    PP_LOCKED,
    PP_INTERACT_PRE,
    PP_APPROACH,
    PP_ADVANCE,
    PP_INTERACT_POST,
    PP_TAIL,
    PP_COUNT
};

static int g_tick_bd_init;
static int g_tick_bd_on;
static int g_tick_bd_ms;
static FILE* g_tick_bd_out;
static uint64_t g_pp[PP_COUNT];

/* Two costs that cut across the phases rather than sitting inside one: script
 * dispatch (torirs_server_scripts.c) and the collision flood every route runs
 * (collision_map.c). Both are accumulated at their own source and zeroed here
 * per tick, so they overlap the phase columns instead of adding to them. */
extern uint64_t g_ToriRSServer_ScriptUs;
extern int g_ToriRSServer_ScriptRuns;
extern uint64_t g_ToriRSServer_ScriptSlowUs;
extern char g_ToriRSServer_ScriptSlowName[96];
extern uint64_t g_collision_route_us;
extern int g_collision_route_calls;
extern int g_collision_route_tiles;
/* g_ssvm_ops / g_ssvm_op_us / g_ssvm_op_hits are declared by ssvm.h. */

/*
 * TORIRS_SERVER_BREAKDOWN_LOG=<path> sends the split to a buffered file instead
 * of stderr, which is what makes a threshold of 0 usable: on Windows stderr is
 * unbuffered and one write costs about 6 ms, so logging every tick to it would
 * cost more than the tick measures and would land that cost on exactly the
 * frames under study. To a file it is free, and a whole run's ticks can be
 * ranked afterwards rather than fished for one spike at a time.
 */
static int
tick_bd_on(void)
{
    if( !g_tick_bd_init )
    {
        char const* v = getenv("TORIRS_SERVER_BREAKDOWN");
        char const* path = getenv("TORIRS_SERVER_BREAKDOWN_LOG");

        g_tick_bd_init = 1;
        if( path && path[0] )
            g_tick_bd_out = fopen(path, "w");
        g_tick_bd_ms = (v && v[0]) ? atoi(v) : 0;
        /* A log path on its own means every tick; a threshold on its own means
         * stderr and only the slow ones. Neither set is off. */
        g_tick_bd_on = g_tick_bd_out != NULL || g_tick_bd_ms > 0;
        if( !g_tick_bd_out )
            g_tick_bd_out = stderr;
    }
    return g_tick_bd_on;
}

static uint64_t
tick_bd_now_us(void)
{
    struct timespec ts;

    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0;
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* Accumulate into g_pp[slot] and restamp the cursor. `on` is hoisted by the
 * caller so the getenv-backed gate is read once per player, not once per step. */
#define PP_MARK(on, cursor, slot)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if( on )                                                                                   \
        {                                                                                          \
            uint64_t pp_now = tick_bd_now_us();                                                    \
            g_pp[slot] += pp_now - (cursor);                                                       \
            (cursor) = pp_now;                                                                     \
        }                                                                                          \
    } while( 0 )

/*
 * The linger clock: reclaim instances whose whole linger group has been empty
 * of players for their linger count (60s by default — see
 * TORIRSSERVER_MAPINSTANCE_LINGER_DEFAULT).
 *
 * This is what stands where the immediate release in
 * `ToriRSServer_WorldRemovePlayer` used to be: an abandoned encounter keeps its
 * map, its npcs and its instance registers for the linger window, so a session
 * that dropped mid-fight can log back in *inside* it and content can restart
 * the encounter ([login] finds the player standing in a live instance). Only
 * after the window does the engine take it all back, with the same
 * `ToriRSServer_WorldMapInstanceFree` teardown every other release uses.
 *
 * Occupancy is computed here rather than in the registry because who is
 * standing where is the world's knowledge. It is judged per linger GROUP: a
 * raid lobby that is empty for the whole fight (ToA's Nexus) and a satellite
 * map nobody stands in between visits (Sotetseg's shadow realm) must live as
 * long as the room their party is actually in.
 */
static void
world_mapinstance_linger(struct ToriRSServer* srv)
{
    int occupied[TORIRSSERVER_MAPINSTANCE_MAX + 1] = { 0 };

    for( int i = 0; i < srv->player_count; i++ )
    {
        const struct ToriRSServerPlayer* player = &srv->players[i];
        int handle;

        if( !player->active )
            continue;
        handle = ToriRSServer_MapInstanceFind(player->x, player->z);
        if( handle )
            occupied[handle] = 1;
    }
    for( int handle = 1; handle <= TORIRSSERVER_MAPINSTANCE_MAX; handle++ )
    {
        int group = ToriRSServer_MapInstanceLingerGroup(handle);
        int group_occupied = occupied[handle];

        if( group )
        {
            for( int other = 1; other <= TORIRSSERVER_MAPINSTANCE_MAX && !group_occupied;
                 other++ )
            {
                if( occupied[other] && ToriRSServer_MapInstanceLingerGroup(other) == group )
                    group_occupied = 1;
            }
        }
        if( ToriRSServer_MapInstanceLingerTick(handle, group_occupied) )
        {
            if( getenv("TORIRSSERVER_VERBOSE") )
                fprintf(stderr,
                        "torirsserver: map instance %d empty past its linger; released\n",
                        handle);
            ToriRSServer_WorldMapInstanceFree(srv, handle);
        }
    }
}

/** 1. World script queue (world_delay), delayed obj spawns, npc hunt. */
static void
phase_world(struct ToriRSServer* srv)
{
    ToriRSServer_ScriptsResumeWorld(srv);
    ToriRSServer_CombatRespawnTick(srv);
    world_mapinstance_linger(srv);
}

/**
 * 2. Turn latched client input into world state.
 *
 * The socket drain deliberately stays in torirs_server_main.c between ticks: moving
 * it in here would need a second buffering layer for no benefit. What belongs
 * in this phase is the *conversion* of what handlers latched into interactions
 * and directly-dispatched triggers, which arrives with the interaction model.
 */
static void
phase_clients_in(struct ToriRSServer* srv)
{
    (void)srv;
}

/**
 * 3. ai_spawn / ai_despawn, queued by phase 4 and by npc_add.
 *
 * `[ai_spawn,<npc>]` is how a behaviour starts: it is where the reference's
 * npcs set their first `npc_settimer`, and without it `[ai_timer]` can never
 * fire for an npc no script has touched — which was every npc in the world,
 * because nothing else calls `npc_settimer`. The imp's wander-teleport is the
 * first behaviour that needed it and will not be the last; anything with a
 * heartbeat starts here.
 *
 * A respawn counts as a spawn. The reference re-runs `[ai_spawn]` on respawn
 * for exactly this reason — a timer set once at login would stop the first time
 * the npc died, so the behaviour would decay out of the world one death at a
 * time with nothing to point at.
 *
 * `[ai_despawn]` is not dispatched: the mock has no despawn path a script could
 * usefully act on (death goes through `[ai_queue3]`, which is where the drop
 * tables already are), and a trigger that fires from nowhere is worse than one
 * that does not fire.
 */
static void
phase_npc_events(struct ToriRSServer* srv)
{
    for( int slot = 0; slot < srv->npc_slot_max; slot++ )
    {
        struct ToriRSServerNpc* npc = &srv->npcs[slot];

        if( !npc->active || !npc->spawn_pending )
            continue;
        npc->spawn_pending = 0;
        ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_AI_SPAWN, npc->type, -1, slot);
    }
}

/** 4. Every npc's turn: delays, timers, queues, then its mode. */
static void
phase_npcs(struct ToriRSServer* srv)
{
    for( int slot = 0; slot < srv->npc_slot_max; slot++ )
    {
        if( !srv->npcs[slot].active )
            continue;
        /* Resume before anything else: a delayed npc is busy, and starting a
         * second script on it would interleave two sets of writes.
         *
         * There used to be a second `[ai_timer]` drain here as well
         * (`ToriRSServer_ScriptsProcessNpcTimer`), reading a `ToriRSServerNpc.timer_script`
         * that was written in exactly one place and only ever to -1 — so it could
         * never run. The live drain is in `advance_npcs`, which resolves the
         * trigger by npc type. */
        ToriRSServer_ScriptsResumeNpc(srv, slot);
        ToriRSServer_CombatNpcTick(srv, slot);
    }
    advance_npcs(srv);
    /* After combat and modes have claimed facing: clear idle latches
     * (LostCity setFaceEntity with no pathing target). */
    for( int slot = 0; slot < srv->npc_slot_max; slot++ )
    {
        struct ToriRSServerNpc* npc = &srv->npcs[slot];

        if( !npc->active )
            continue;
        /*
         * A familiar is never "idle facing nobody" — it attends its owner.
         *
         * `ToriRSServer_NpcFaceClearIfIdle` keeps the latch for a combat target
         * or a player-facing MODE (>= PLAYERESCAPE), and `playerfollow` is one
         * of those, so a heeling familiar was already covered by `npc_run_mode`.
         * Mode `none` is not: it means "nothing owns this npc's movement", which
         * is exactly where a familiar is parked between a targeted special's
         * approach and its next order — and there the clear branch fired and
         * shipped "face nobody", permanently, because nothing re-derives a
         * latch for a modeless npc.
         *
         * So an owned npc with no fight re-latches onto its owner here instead
         * of being cleared. This is also what makes the facing self-heal after
         * an `npc_facesquare` (which drops our copy of the latch, see
         * SS_OP_NPC_FACESQUARE): the next turn puts the owner back on the wire.
         *
         * Gated on having no queued waypoint. A familiar walking a route a
         * script gave it — the special's approach — is going somewhere, and
         * turning it to face the owner mid-errand would make it strafe there
         * sideways. Idle means idle.
         */
        if( npc->owner_gen != 0 && npc->combat_target < 0 &&
            npc->combat_target_npc < 0 && npc->waypoint_index < 0 )
        {
            struct ToriRSServerPlayer* owner = ToriRSServer_WorldNpcOwner(srv, npc);

            if( owner )
            {
                ToriRSServer_NpcFacePlayer(npc, owner->pid);
                continue;
            }
        }
        ToriRSServer_NpcFaceClearIfIdle(npc);
    }
}

/*
 * The per-player phases, and why they are written this way.
 *
 * Each one iterates the pool and calls `ToriRSServer_WorldSetActive` before doing
 * anything, because most of what they call still reaches the player through the
 * world. That is the residue of the single-player era: the loop is what makes
 * the residue *correct* — the alternative, giving every one of those functions a
 * `ToriRSServerPlayer*`, is the same change spread over five files and can be done
 * one subsystem at a time behind this.
 *
 * The pool is iterated to `player_count` and skips inactive slots, because a
 * logout leaves a hole rather than compacting (see `ToriRSServerPlayer.pid`).
 */
#define TORIRSSERVER_FOR_EACH_PLAYER(srv, player)                                                       \
    for( int ToriRSServer_Pid_ = 0; ToriRSServer_Pid_ < (srv)->player_count; ToriRSServer_Pid_++ )                \
        if( !((player) = &(srv)->players[ToriRSServer_Pid_])->active )                                  \
        {                                                                                          \
        }                                                                                          \
        else

/*
 * LostCity PathingEntity.reorient(): after movement, face a pending loc/obj
 * fine target if we held still this tick. Pathing targets (npc/player) use
 * FACE_ENTITY and are skipped here. MUST run after advance_player so
 * steps_taken reflects this tick; client=true FACE_COORD is the only path
 * that ships loc/obj facing to watchers.
 */
static void
player_reorient(struct ToriRSServerPlayer* player)
{
    enum ToriRSServerInteractionKind kind = player->interaction.kind;

    if( kind == TORIRSSERVER_INTERACT_NPC || kind == TORIRSSERVER_INTERACT_PLAYER )
        return;
    if( player->face_target_x == -1 || player->steps_taken != 0 )
        return;

    player->face_x = player->face_target_x;
    player->face_z = player->face_target_z;
    player->masks |= TORIRSSERVER_PMASK_FACE_COORD;
    player->face_target_x = -1;
    player->face_target_z = -1;
}

/** 5. The player: delays, resumes, queues, timers, then interaction. */
static void
phase_player(struct ToriRSServerPlayer* player)
{
    struct ToriRSServer* srv = player->world;
    int bd_on = tick_bd_on();
    uint64_t bd_t = bd_on ? tick_bd_now_us() : 0;

    ToriRSServer_WorldSetActive(srv, player);

    /* Order matches the reference exactly, and it matters: a script resumed
     * here must not have a queue entry started on top of it in the same tick. */
    ToriRSServer_ScriptsResumePlayer(srv);
    ToriRSServer_ScriptsProcessQueues(srv);
    ToriRSServer_ScriptsProcessTimers(srv);
    /* The engine queue is drained *after* the timers, which is where
     * `World.processPlayers` puts `processEngineQueue()`. It holds the zone
     * family, detected in phase 10 of the previous tick — so a zone script runs
     * one tick after the crossing, before this tick's movement rather than
     * after it. */
    ToriRSServer_ScriptsProcessEngineQueue(srv);
    PP_MARK(bd_on, bd_t, PP_SCRIPTS);

    /*
     * Face the interaction / combat target before approach can return early
     * and before tryInteract can clear it — LostCity PathingEntity.setFaceEntity
     * in processPlayers, before processInteraction.
     *
     * Above the action-lock gate, and unconditional, because this is the only
     * writer of the latch: `ToriRSServer_CombatStopPlayerAt` no longer clears it
     * (see the comment there), so a turn this call is skipped on is a turn the
     * release cannot be derived. A locked or dying player is precisely the case
     * that would keep staring at whatever it was fighting. LostCity calls it
     * for every player each turn with no `delayed` gate for the same reason.
     */
    ToriRSServer_PlayerSetFaceEntity(player);
    PP_MARK(bd_on, bd_t, PP_FACE);

    /*
     * An action lock is deliberately below every script/queue/timer phase.
     * That placement is its contract: a queued projectile hit still executes,
     * and a queued player_unlock can release the player for this same turn.
     * What stays below the gate is exclusively actor-driven pathing and
     * interaction. A queued script which tried to arm either while the lock
     * remains set is cancelled here rather than resuming after the time-stop.
     */
    if( player->action_locked )
    {
        if( player_has_waypoints(player) || player->dest_x >= 0 ||
            player->dest_z >= 0 || player->combat_target >= 0 ||
            player->interaction.kind != TORIRSSERVER_INTERACT_NONE ||
            player->face_entity != -1 || player->face_target_x != -1 )
            player_cancel_locked_actions(player);
        advance_player(srv);
        run_energy_tick(srv, 0);
        ToriRSServer_CombatPlayerTick(srv);
        PP_MARK(bd_on, bd_t, PP_LOCKED);
        return;
    }

    /*
     * A stun sits below the script phases for the same reason the action lock
     * does — a queued projectile still lands, a queued heal still heals — and
     * above pathing and interaction, which are exactly what it takes away.
     *
     * Unlike the lock this does not return early: combat still ticks, so a
     * stunned player is still a target being hit, and the tick's ordinary
     * bookkeeping below still runs. What it skips is the approach and the
     * interaction, and `advance_player` walks nowhere because the interrupt
     * cleared the route and the inbound gate refuses new ones.
     */
    if( player->stun_ticks > 0 )
    {
        ToriRSServer_WorldStunInterrupt(player);
        advance_player(srv);
        run_energy_tick(srv, 0);
        ToriRSServer_CombatPlayerTick(srv);
        player->stun_ticks--;
        PP_MARK(bd_on, bd_t, PP_LOCKED);
        return;
    }

    /*
     * LostCity Player.processInteraction: tryInteract → pathToPathingTarget →
     * updateMovement → tryInteract. Pre-move try catches a mover that stepped
     * adjacent this tick before a stale walk carries the player past them;
     * last-waypoint full repath aims the step at where they are now.
     */
    if( player->interaction.kind != TORIRSSERVER_INTERACT_NONE )
    {
        if( !interaction_try(srv, 0) )
            interaction_path_to_pathing_target(srv);
    }
    PP_MARK(bd_on, bd_t, PP_INTERACT_PRE);

    /* Combat's every-tick pathToTarget — same pre-move slot as above, for the
     * engaged fight that no longer holds a latched interaction. */
    ToriRSServer_CombatPlayerApproach(srv);
    PP_MARK(bd_on, bd_t, PP_APPROACH);
    /* advance_player fires an armed walktrigger immediately before each
     * concrete tile. That preserves the ordinary freeze/stun veto and also
     * gives controller-style content both tiles of a running tick. */
    advance_player(srv);
    PP_MARK(bd_on, bd_t, PP_ADVANCE);
    player_process_locstep(srv);

    if( player->interaction.kind != TORIRSSERVER_INTERACT_NONE )
    {
        if( !interaction_try(srv, player->steps_taken == 0) )
            interaction_continue_or_give_up(srv);
    }
    PP_MARK(bd_on, bd_t, PP_INTERACT_POST);
    /* After movement: face a loc/obj target if we walked over and held still
     * (LostCity reorient(); needs this-tick steps_taken). The face_target
     * stash survives interaction_clear, so FACE_COORD ships on the same tick
     * the op fires. */
    player_reorient(player);
    /* Energy is spent on the steps that were actually taken, so a route that
     * ran out of tiles this tick regenerates instead of draining. */
    run_energy_tick(srv, player->running ? player->move_count : 0);
    /* Before the swing: a prayer that ran out this tick must not protect the
     * hit that lands on it. */
    ToriRSServer_CombatPlayerTick(srv);
    /* After the swing, so the bar shows the hitpoints this tick left behind
     * rather than the ones it started with. */
    ToriRSServer_HpBarTick(srv, player);
    /* Beside it, and for the same reason: the helper panel reports on what the
     * player is doing, so it reads the tick's outcome rather than its input. */
    ToriRSServer_HelperTick(srv, player);
    PP_MARK(bd_on, bd_t, PP_TAIL);
}

static void
phase_players(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;

    TORIRSSERVER_FOR_EACH_PLAYER(srv, player)
    {
        if( player->login_scene_pending || player->rebuild_scene_pending )
            continue;
        phase_player(player);
    }
}

/**
 * 6. Logouts.
 *
 * Empty, and the `[logout]` trigger it used to promise is **not** missing — it
 * lives in `ToriRSServer_WorldRemovePlayer`, because that is the only path either
 * host removes a player through and a dropped socket does not wait for a tick
 * phase. What is genuinely absent here is the reference's *deferral*: a
 * `p_logout` that has to wait for `canAccess()` or for the engine queue to
 * drain (`World.ts:764`). Nothing in this engine requests a logout and then
 * waits, so there is nothing for this phase to hold.
 */
static void
phase_logouts(struct ToriRSServer* srv)
{
    (void)srv;
}

/** 7. Logins, which run the [login] trigger. */
static void
phase_logins(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;

    TORIRSSERVER_FOR_EACH_PLAYER(srv, player)
    {
        if( !player->login_pending )
            continue;
        player->login_pending = 0;
        ToriRSServer_WorldSetActive(srv, player);
        /* The C burst already sent the scene, the gameframe and the containers —
         * that is engine work. [login] is where anything an operator would want
         * to change without recompiling belongs.
         *
         * Specific, not the chain: `[login]` has no subject, so the reference
         * asks for the global form directly (`Player.ts` getByTriggerSpecific)
         * rather than walking two rungs that cannot exist. */
        ToriRSServer_ScriptsRunTriggerSpecific(srv, SS_TRIGGER_LOGIN, -1, -1, -1);
    }
}

/** 8. Loc/obj respawn timers and the zone bookkeeping. */
/*
 * Every runtime loc mutation in the world goes through here.
 *
 * Three things have to happen together and used to happen in three places: the
 * scene's collision has to move, the ZoneMap has to record that this tile no
 * longer matches the map square, and the zone has to queue the wire event. The
 * old shape did only the third — `ToriRSServer_WorldBroadcastLoc` walked the player
 * pool and sent LOC_ADD_CHANGE to everyone on that level whose scene contained
 * the tile — which is correct for whoever is standing there and silently wrong
 * for everyone else forever, because a broadcast has no memory.
 *
 * The key is `(x, z, level, shape)`, which is what the wire uses: LOC_ADD_CHANGE
 * and LOC_DEL identify a loc by its tile and its shape, and a tile can hold a
 * wall and a piece of scenery at once.
 */
int
ToriRSServer_WorldLocSet(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle,
    enum ToriRSServerLocSetKind kind)
{
    struct ToriRSServerLocOps ops;

    ToriRSServer_LocOpsDefault(&ops);
    return ToriRSServer_WorldLocSetOps(srv, x, z, level, shape, loc_id, angle, kind, &ops);
}

/*
 * Replay one successful scene mutation into every OTHER built window covering
 * the tile — the second half of the window-coherence invariant (the first is
 * occupancy's broadcast in ToriRSServer_SceneChangeOccupancy). Slot numbers
 * are per-window, so each window runs its own lookup; the ZoneMap record the
 * caller writes covers windows built later.
 */
static void
world_loc_mirror(
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle,
    enum ToriRSServerLocSetKind kind,
    const struct ToriRSServerLocOps* ops)
{
    struct ToriRSServerSceneWindow* primary = ToriRSServer_SceneBoundWindow();

    assert(ops);
    for( int i = 0; i < TORIRSSERVER_SCENE_WINDOW_MAX; i++ )
    {
        struct ToriRSServerSceneWindow* window = ToriRSServer_SceneWindowByIndex(i);
        int slot;

        if( window == primary )
            continue;
        if( !ToriRSServer_SceneWindowContains(window, x, z) )
            continue;
        ToriRSServer_SceneBindWindow(window);
        slot = ToriRSServer_SceneFindLocExact(x, z, level, shape);
        if( loc_id < 0 )
        {
            /* A delete: this window's own occupant goes; if it was a loc_add
             * the window's static loc stands revealed underneath, same as in
             * the primary. Nothing to remove is fine — the windows agreed
             * before the mutation, so they agree after. */
            ToriRSServer_SceneRemoveLoc(slot);
        }
        else if( kind == TORIRSSERVER_LOC_SET_ADD || !ToriRSServer_SceneLoc(slot) )
        {
            slot = ToriRSServer_SceneAddLoc(x, z, level, loc_id, shape, angle);
            if( slot >= 0 )
                ToriRSServer_SceneLocSetOps(slot, ops);
        }
        else if( ToriRSServer_SceneReplaceLoc(slot, loc_id, angle) )
        {
            ToriRSServer_SceneLocSetOps(slot, ops);
        }
    }
    ToriRSServer_SceneBindWindow(primary);
}

/* The body of ToriRSServer_WorldLocSetOps, already bound to the window the
 * mutation lands in (`covered` says whether any built window holds the tile). */
static int
world_loc_set_ops_in_window(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle,
    enum ToriRSServerLocSetKind kind,
    const struct ToriRSServerLocOps* ops,
    int covered)
{
    int slot = ToriRSServer_SceneFindLocExact(x, z, level, shape);
    struct ToriRSServerSceneLoc* existing = ToriRSServer_SceneLoc(slot);
    /*
     * What the cache says is here, captured before the scene is touched and
     * only used when the ZoneMap has no record yet — a second change would ask
     * the scene and get our own first edit back. -1 for a tile the map square
     * has nothing on, which is how a `loc_add` is told from a `loc_change`.
     */
    struct ToriRSServerZoneLoc* known = ToriRSServer_ZoneLocFind(srv, x, z, level, shape);
    int base_id = known ? known->base_loc_id : (existing ? existing->loc_id : -1);
    int base_angle = known ? known->base_angle : (existing ? existing->angle : 0);
    /* Read before anything touches the scene: `ToriRSServer_SceneAddLoc` can grow
     * the loc array, and `existing` points into it. */
    int existing_active = existing && existing->active;
    int existing_angle = existing ? existing->angle : angle;
    int over_base = 0;

    assert(ops);

    /*
     * A tile no built window covers. The reference has no such case — its
     * World holds every zone — and content is entitled to the same reach:
     * puro-puro's crop circle rotates through eight farms and at most one of
     * them is ever near a player. The ZoneMap is world-indexed and is the
     * durable authority anyway (§3.17), so the mutation is recorded there and
     * the scene halves (collision, the slot array) are skipped; the rebuild's
     * `ToriRSServer_WorldLocsReapply` puts the record onto a window if one is
     * ever built over it.
     *
     * What this cannot know is the map square's own loc on that tile: `base`
     * stays -1 unless an earlier in-scene change captured it, so an
     * out-of-scene add onto a tile whose square already holds a same-shape
     * static loc records "nothing was here" and a later revert removes rather
     * than restores. Reading the square from the cache on this path would fix
     * that; nothing needs it yet.
     */
    if( !covered )
    {
        if( loc_id < 0 )
        {
            /* Deleting what nothing recorded: out here the ZoneMap is the only
             * memory, so an unknown tile is a caller bug, same as an in-scene
             * delete of an empty tile. */
            if( !known )
                return 0;
            angle = known->angle;
        }
        else if( ToriRSServer_LocInfoCount() > 0 && !ToriRSServer_LocKnown(loc_id) )
        {
            return 0;
        }
        /* No scene out here, so nothing is standing underneath to preserve —
         * `base_id` stays -1 unless an earlier in-scene change captured it. */
        ToriRSServer_ZoneLocChanged(srv, x, z, level, shape, loc_id, angle, base_id, base_angle,
                                 0, ops);
        return 1;
    }

    if( loc_id < 0 )
    {
        struct ToriRSServerSceneLoc* revealed;

        if( !existing || !ToriRSServer_SceneRemoveLoc(slot) )
            return 0;
        /* The removed loc's own angle, not the caller's: LOC_DEL carries
         * shape+angle and the client matches on both. */
        angle = existing_angle;

        /*
         * Removing a `loc_add`ed loc uncovers the map square's own loc, which
         * was never removed (ToriRSServer_SceneAddLoc). The tile is not empty, so
         * the clients must not be told it is: send that loc instead, which is
         * also what retires the ZoneMap record, because a tile back to what the
         * cache says has nothing left to replay.
         */
        revealed = ToriRSServer_SceneLoc(ToriRSServer_SceneFindLocExact(x, z, level, shape));
        if( revealed && revealed->active )
        {
            world_loc_mirror(x, z, level, shape, loc_id, angle, kind, ops);
            /* The uncovered loc's own menu, not the caller's: this is the map
             * square's loc reappearing, and what it offers is what its loctype
             * offers. Passing the removal's `ops` here would leave a closed
             * door wearing the open one's "Close". */
            ToriRSServer_ZoneLocChanged(srv, x, z, level, shape, revealed->loc_id,
                                     revealed->angle, base_id, base_angle, 0,
                                     &revealed->ops);
            return 1;
        }
    }
    else if( kind == TORIRSSERVER_LOC_SET_ADD )
    {
        int added = ToriRSServer_SceneAddLoc(x, z, level, loc_id, shape, angle);

        if( added < 0 )
            return 0;
        ToriRSServer_SceneLocSetOps(added, ops);
        over_base = existing_active;
    }
    else if( existing )
    {
        if( !ToriRSServer_SceneReplaceLoc(slot, loc_id, angle) )
            return 0;
        ToriRSServer_SceneLocSetOps(slot, ops);
    }
    else
    {
        int added = ToriRSServer_SceneAddLoc(x, z, level, loc_id, shape, angle);

        if( added < 0 )
            return 0;
        ToriRSServer_SceneLocSetOps(added, ops);
    }

    world_loc_mirror(x, z, level, shape, loc_id, angle, kind, ops);
    ToriRSServer_ZoneLocChanged(srv, x, z, level, shape, loc_id, angle, base_id, base_angle,
                             over_base, ops);
    return 1;
}

int
ToriRSServer_WorldLocSetOps(
    struct ToriRSServer* srv,
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle,
    enum ToriRSServerLocSetKind kind,
    const struct ToriRSServerLocOps* ops)
{
    /* The mutation lands in a window that actually covers the tile — the bound
     * one when it does (the acting player's own scene), else any built window
     * that does — and world_loc_mirror then repeats it into the rest. The
     * caller's binding is restored on every path: a script that mutates a loc
     * across the world must not leave the engine bound to a stranger's
     * window. */
    struct ToriRSServerSceneWindow* home = ToriRSServer_SceneBoundWindow();
    struct ToriRSServerSceneWindow* covering = ToriRSServer_SceneWindowFind(x, z);
    int result;

    if( covering )
        ToriRSServer_SceneBindWindow(covering);
    result = world_loc_set_ops_in_window(srv, x, z, level, shape, loc_id, angle, kind, ops,
                                         covering != NULL);
    ToriRSServer_SceneBindWindow(home);
    return result;
}

/** One recorded change, put back onto a window that has just been re-read from
 *  the cache. A record outside the bound window is left alone: it stays in the
 *  ZoneMap and re-applies if a window is ever built over it. */
static void
reapply_loc(
    struct ToriRSServerZoneLoc* loc,
    void* user)
{
    int slot;

    (void)user;
    if( !ToriRSServer_SceneWindowContains(ToriRSServer_SceneBoundWindow(), loc->x, loc->z) )
        return;
    slot = ToriRSServer_SceneFindLocExact(loc->x, loc->z, loc->level, loc->shape);
    if( loc->loc_id < 0 )
    {
        ToriRSServer_SceneRemoveLoc(slot);
        return;
    }
    /* `over_base` replays a `loc_add` as an add — the map square's own loc has
     * to keep standing underneath, or removing this one again leaves the tile
     * empty and the wall a door swung past never comes back. */
    if( loc->over_base || !ToriRSServer_SceneLoc(slot) )
        slot = ToriRSServer_SceneAddLoc(loc->x, loc->z, loc->level, loc->loc_id, loc->shape,
                                     loc->angle);
    else if( !ToriRSServer_SceneReplaceLoc(slot, loc->loc_id, loc->angle) )
        return;
    if( slot < 0 )
        return;
    /* The rebuild re-read the scene from the cache, so the slot is back to its
     * loctype's menu; the ZoneMap record is where the placement's own survived.
     * Without this the first scene rebuild after a door opened would leave it
     * drawn open and offering "Open" again. */
    ToriRSServer_SceneLocSetOps(slot, &loc->ops);
}

static void
world_locs_reapply_window(
    struct ToriRSServer* srv,
    struct ToriRSServerSceneWindow* window)
{
    struct ToriRSServerSceneWindow* bound = ToriRSServer_SceneBoundWindow();

    assert(window);
    ToriRSServer_SceneBindWindow(window);
    ToriRSServer_ZoneLocsForeach(srv, reapply_loc, NULL);
    ToriRSServer_SceneBindWindow(bound);
}

void
ToriRSServer_WorldLocsReapply(struct ToriRSServer* srv)
{
    /* Every built window: the replay is idempotent per window (a delete on an
     * already-deleted slot is a no-op, a change re-lands the same id), so a
     * window that already carries a record just keeps it. */
    for( int i = 0; i < TORIRSSERVER_SCENE_WINDOW_MAX; i++ )
    {
        struct ToriRSServerSceneWindow* window = ToriRSServer_SceneWindowByIndex(i);

        if( !ToriRSServer_SceneWindowBuilt(window) )
            continue;
        world_locs_reapply_window(srv, window);
    }
}

/*
 * Put a loc mutation back when its timer runs out.
 *
 * `loc_change`, `loc_del` and `loc_add` all carry a duration, and reverting is
 * what makes a skilling loop a loop: a tree becomes a stump for N ticks and
 * then is a tree again. The reference reverts in the zone phase and so does
 * this, which matters for ordering — a script suspended in phase 5 cannot see
 * a revert that has not happened yet, and a revert cannot land after the
 * encoders in phase 10 have already described the zone.
 *
 * A duration of 0 means "forever": the reference treats it as no timer at all,
 * which is what a quest permanently opening a wall wants.
 */
void
ToriRSServer_WorldLocReverts(struct ToriRSServer* srv)
{
    for( int i = 0; i < TORIRSSERVER_LOC_REVERT_MAX; i++ )
    {
        struct ToriRSServerLocRevert* entry = &srv->loc_reverts[i];

        if( !entry->active )
            continue;
        if( --entry->delay > 0 )
            continue;
        entry->active = 0;

        /*
         * One call for both directions. `loc_id < 0` undoes a `loc_add`, and
         * anything else puts a loc back whether the tile is currently empty
         * (a `loc_del` expiring) or holding the changed form (a `loc_change`
         * expiring) — `ToriRSServer_WorldLocSet` decides which by looking, rather
         * than by remembering which opcode armed the timer.
         *
         * It can refuse, and there is one way that happens which is worth
         * saying out loud: collision windows cover only where the world is
         * being watched from (the root anchor plus each player's own window),
         * so a revert armed before everyone walked away may be aimed at a tile
         * no built window covers any more. The ZoneMap record stands, which is
         * the safe direction — the clients were told the loc changed and it
         * stays changed — but the timer is spent, so the loc never comes back.
         * That is the windowed-collision limitation, not this table's, and it
         * is reported rather than swallowed.
         */
        /*
         * Undoing a `loc_add` takes away the loc that was added, and never the
         * map square's own.
         *
         * The two can end up looking alike. `loc_add` puts a dynamic loc over an
         * inactive static one and `ToriRSServer_ZoneLocChanged` then *retires* the
         * ZoneMap record, because the tile is back to the id and angle the cache
         * states and there is nothing left to replay — which is right, and which
         * means the next scene rebuild re-reads the square and the dynamic loc is
         * simply gone. The timer this entry armed is not: it fires later against a
         * tile whose only loc is the static one, and removing that deletes a
         * record of the world the cache owns. It does not come back on a rebuild
         * either, because this same call records `loc_id = -1` in the ZoneMap and
         * `ToriRSServer_WorldLocsReapply` faithfully re-deletes it every time.
         *
         * Lumbridge's castle door is where it was found. `[proc,door_open_active]`
         * is `loc_del(500)` + `loc_add(…, 500)` and closing it again is another
         * pair five ticks behind, so 500 ticks after any door in the world is
         * opened and shut the door's own tile is emptied: `ToriRSServer --selftest`'s
         * use-on section, which runs long after the doors section, found
         * `dugupsoil2_grey` where the door should be. A door that is opened and
         * left open is unaffected — its ZoneMap record stands, so the added loc is
         * still there for the timer to find.
         */
        if( entry->loc_id < 0 )
        {
            struct ToriRSServerSceneLoc* standing = ToriRSServer_SceneLoc(ToriRSServer_SceneFindLocExact(
                entry->x, entry->z, entry->level, entry->shape));

            if( standing && standing->active && standing->is_static )
                continue;
        }
        if( !ToriRSServer_WorldLocSet(srv, entry->x, entry->z, entry->level, entry->shape,
                                   entry->loc_id, entry->angle, TORIRSSERVER_LOC_SET_CHANGE) &&
            srv->verbose )
            fprintf(stderr,
                    "torirsserver: a loc revert at %d,%d could not apply — outside the built "
                    "scene; %d stays as it is\n",
                    entry->x, entry->z, entry->loc_id);
    }
}

/*
 * Queue a revert. `duration` of 0 is "never", matching the reference.
 *
 * Returns 0 when the table is full, and the caller's mutation still stands —
 * which is the right failure: a loc that changed and never changes back is a
 * visible bug, where refusing the change outright would make the script look
 * broken instead.
 *
 * A duration that cannot be incremented is "never" as well, and that is not a
 * theoretical case: `^inferno_loc_duration` is `^max_32bit_int`, which is how
 * the Inferno states a permanent mutation. `INT_MAX + 1` below is signed
 * overflow, and once the tree started building optimised by default the
 * wrapped counter read as already-expired — so every seal wall snapped back to
 * its intact form a tick after it changed, the middle slab that had just been
 * deleted came back, and the terminal rubble was removed again the tick after
 * it was placed. The whole Zuk cutscene played against locs that undid
 * themselves. Clamping here rather than in the content keeps any script's
 * "forever" from meaning "next tick".
 */
int
ToriRSServer_WorldLocRevertQueue(
    struct ToriRSServer* srv,
    int duration,
    int loc_id,
    int shape,
    int angle,
    int x,
    int z,
    int level)
{
    if( duration <= 0 || duration >= INT_MAX )
        return 1;
    /*
     * RE-STATING a tile's revert REPLACES the pending one; it does not add a
     * second. Two reverts for one tile is not a state the world can be in —
     * whichever fires first undoes the loc, and the other then undoes whatever
     * happens to be standing there when it lands.
     *
     * The case that made it matter is the Maiden's blood trails. A blood spawn
     * that walks back over its own trail re-covers the tile, and the reference
     * restarts the clock: Zenyte's `BloodTrail.resetTimer()` sets `ticks = 30`
     * again, which is what produces the runs of 40, 50, 60, 90 and 110 ticks in
     * the recorded raids (docs/TOB_RESEARCH.md M5). Queueing a second revert
     * instead left the FIRST one standing, so a re-covered tile expired 30
     * ticks after it was first painted however many times it was refreshed —
     * the patch under a circling spawn would blink out from under it.
     *
     * Matched on tile and shape, which together are what `loc_add` addresses:
     * one shape per tile is the scene's own rule (`ToriRSServer_SceneFindLocExact`
     * takes exactly that pair).
     */
    for( int i = 0; i < TORIRSSERVER_LOC_REVERT_MAX; i++ )
    {
        struct ToriRSServerLocRevert* entry = &srv->loc_reverts[i];

        if( !entry->active )
            continue;
        if( entry->x != x || entry->z != z || entry->level != level ||
            entry->shape != shape )
            continue;
        entry->delay = duration + 1;
        entry->loc_id = loc_id;
        entry->angle = angle;
        return 1;
    }
    for( int i = 0; i < TORIRSSERVER_LOC_REVERT_MAX; i++ )
    {
        struct ToriRSServerLocRevert* entry = &srv->loc_reverts[i];

        if( entry->active )
            continue;
        entry->active = 1;
        /* +1 for the same reason p_delay has one: the rest of this tick plus
         * `duration` more (docs/osrs230_mockserver.md §3.10). */
        entry->delay = duration + 1;
        entry->loc_id = loc_id;
        entry->shape = shape;
        entry->angle = angle;
        entry->x = x;
        entry->z = z;
        entry->level = level;
        return 1;
    }
    fprintf(stderr, "torirsserver: the loc revert table is full; %d will not change back\n",
            loc_id);
    return 0;
}

/*
 * Drop the objs whose flight time has run out.
 *
 * The reference's `objDelayedQueue`, which it drains in the same phase. Splitting
 * a non-stackable pile into singles is `ToriRSServer_WorldObjAdd`'s caller's job
 * everywhere else in this server, and it is not done here for one reason: the
 * only thing that arms an entry is ammo, `inv_dropitem_delayed` is always called
 * with a count the reference has already decided is one pile, and a stack of ten
 * arrows on the floor is one pile in the reference too.
 */
void
ToriRSServer_WorldObjDelayed(struct ToriRSServer* srv)
{
    for( int i = 0; i < TORIRSSERVER_OBJ_DELAYED_MAX; i++ )
    {
        struct ToriRSServerObjDelayed* entry = &srv->obj_delayed[i];

        if( !entry->active )
            continue;
        if( --entry->delay > 0 )
            continue;
        entry->active = 0;
        ToriRSServer_WorldObjAdd(srv, entry->obj_id, entry->count, entry->x, entry->z,
                              entry->level, entry->duration);
    }
}

int
ToriRSServer_WorldObjDelayedQueue(
    struct ToriRSServer* srv,
    int delay,
    int duration,
    int obj_id,
    int count,
    int x,
    int z,
    int level)
{
    assert(srv);

    if( delay <= 0 )
    {
        ToriRSServer_WorldObjAdd(srv, obj_id, count, x, z, level, duration);
        return 1;
    }
    for( int i = 0; i < TORIRSSERVER_OBJ_DELAYED_MAX; i++ )
    {
        struct ToriRSServerObjDelayed* entry = &srv->obj_delayed[i];

        if( entry->active )
            continue;
        entry->active = 1;
        /* +1 for the same reason a loc revert has one: the rest of this tick
         * plus `delay` more (docs/osrs230_mockserver.md §3.10). */
        entry->delay = delay + 1;
        entry->duration = duration;
        entry->obj_id = obj_id;
        entry->count = count;
        entry->x = x;
        entry->z = z;
        entry->level = level;
        return 1;
    }
    fprintf(stderr, "torirsserver: the delayed-drop table is full; %d x%d never lands\n",
            obj_id, count);
    return 0;
}

/**
 * 8. Loc reverts, delayed drops, obj respawns, and the zone membership reconcile.
 *
 * The reference's zone phase, in its order: the world's own timers first, then
 * the zones are brought into agreement with where everything now stands. What it
 * deliberately does *not* do any more is send anything — the per-client flush is
 * phase 10's `ToriRSServer_ZoneUpdatePlayer`, which is where the reference puts
 * `updateZones()` too.
 */
static void
phase_zones(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;

    ToriRSServer_WorldLocReverts(srv);
    ToriRSServer_WorldObjDelayed(srv);
    ground_tick(srv);
    ToriRSServer_ZoneSyncNpcs(srv);
    ToriRSServer_ZoneSyncObjs(srv);
    /* And the players. Membership is what lets a client's area be built by
     * asking the map "who is in the zones I hold" instead of walking the pool
     * and testing a tile radius. */
    ToriRSServer_ZoneSyncPlayers(srv);

    /*
     * Then each client's own view of it, built once and read three times.
     *
     * Here rather than in phase_client_out, and that is the whole ordering
     * argument: PLAYER_INFO, NPC_INFO and the zone flush all read
     * `player->area`, so it has to be current before the first of them runs
     * and identical for all three. It used to be rebuilt inside
     * `ToriRSServer_ZoneUpdatePlayer`, which comes *after* both entity streams.
     */
    TORIRSSERVER_FOR_EACH_PLAYER(srv, player)
    {
        if( !player->world )
            continue;
        ToriRSServer_PlayerzonemapMove(player);
    }
}

/**
 * 9. Recompute what the client needs to be told about.
 *
 * The rebuild check lives here rather than in phase 10 because the placement
 * PLAYER_INFO writes depends on the new origin zone, so it has to be decided
 * before any encoding starts.
 */
/*
 * Observation coordinates for every player (docs/SAILING_PLAN.md S2.4).
 *
 * `obs_*` is where a player is to be SEEN, which is not where they stand: a
 * player on a vessel deck stands in the map-instance pool, hundreds of squares
 * off the real map, and the projection through the hull transform is the only
 * frame they and a shore player share.
 *
 * `obs_jumped` records that the projection moved independently of this
 * player's own feet — the offset changing IS that motion, since obs = own +
 * off and own motion is exactly what the walk steps describe. Boarding,
 * disembarking, a plane change and a moving hull all land here, and the
 * PLAYER_INFO encoder turns every one of them into remove-and-re-add rather
 * than into a step it cannot express (plan risk R1).
 */
/*
 * One npc's projection, factored out because it is needed at two moments that
 * are not the same moment (docs/sailing_coverage.csv SAIL-50).
 *
 * The per-tick sweep below is the one that matters for the wire. The other is
 * `npc_spawn`, which stands an npc on a tile no sweep has projected yet: an
 * encoder reached in between — every selftest that builds a world and encodes
 * without ticking — would read the previous occupant's projection, or 0,0 for
 * a fresh slot, and place the npc there.
 *
 * `ToriRSServer_WorldNpcTeleport` deliberately does not call it. It runs in a
 * movement phase and the sweep is the first thing `phase_info` does, so the
 * ordering already covers it — and the npc struct carries no back-pointer to
 * its world, so giving it one would be a signature change for nothing.
 */
void
ToriRSServer_WorldNpcRefreshObservation(
    struct ToriRSServer* srv,
    struct ToriRSServerNpc* npc)
{
    struct ToriRSServerVessel* vessel;
    int prev_off_x;
    int prev_off_z;
    int prev_off_level;
    int fine_x = 0;
    int fine_z = 0;

    assert(srv);
    assert(npc);

    prev_off_x = npc->obs_off_x;
    prev_off_z = npc->obs_off_z;
    prev_off_level = npc->obs_off_level;

    vessel = ToriRSServer_VesselAtTile(srv, npc->x, npc->z);
    if( vessel )
    {
        ToriRSServer_VesselDeckTileToRoot(vessel, npc->x, npc->z, &fine_x, &fine_z);
        npc->obs_x = fine_x >> 7;
        npc->obs_z = fine_z >> 7;
        npc->obs_level = vessel->level;
    }
    else
    {
        npc->obs_x = npc->x;
        npc->obs_z = npc->z;
        npc->obs_level = npc->level;
    }
    npc->obs_off_x = npc->obs_x - npc->x;
    npc->obs_off_z = npc->obs_z - npc->z;
    npc->obs_off_level = npc->obs_level - npc->level;
    npc->obs_jumped = npc->obs_off_x != prev_off_x || npc->obs_off_z != prev_off_z ||
                      npc->obs_off_level != prev_off_level;
}

void
ToriRSServer_WorldRefreshObservation(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;

    assert(srv);
    TORIRSSERVER_FOR_EACH_PLAYER(srv, player)
    {
        struct ToriRSServerVessel* vessel;
        int prev_off_x = player->obs_off_x;
        int prev_off_z = player->obs_off_z;
        int prev_off_level = player->obs_off_level;
        int fine_x = 0;
        int fine_z = 0;

        vessel = ToriRSServer_VesselAtTile(srv, player->x, player->z);
        if( vessel )
        {
            ToriRSServer_VesselDeckTileToRoot(vessel, player->x, player->z, &fine_x, &fine_z);
            player->obs_x = fine_x >> 7;
            player->obs_z = fine_z >> 7;
            player->obs_level = vessel->level;
        }
        else
        {
            player->obs_x = player->x;
            player->obs_z = player->z;
            player->obs_level = player->level;
        }
        player->obs_off_x = player->obs_x - player->x;
        player->obs_off_z = player->obs_z - player->z;
        player->obs_off_level = player->obs_level - player->level;
        player->obs_jumped = player->obs_off_x != prev_off_x ||
                             player->obs_off_z != prev_off_z ||
                             player->obs_off_level != prev_off_level;
    }

    /*
     * And the npcs, by the same rule and in the same pass
     * (docs/sailing_coverage.csv SAIL-50).
     *
     * Here rather than in the npc mover for the reason the player loop is here:
     * every observer's NPC_INFO this tick has to agree about where a deckhand
     * was, and the streams all run after this point. A deck npc whose
     * projection is recomputed per observer would be at two places in one tick.
     *
     * The whole slot array rather than a live list because there is no live
     * list; 4096 slots once per tick is a rounding error next to the per-player
     * work that follows, and it is paid whether or not a vessel exists — the
     * OUTPUT for a vessel-free world is obs == own, which is what makes the
     * encoders byte-identical to their pre-sailing selves.
     */
    for( int slot = 0; slot < TORIRSSERVER_NPC_MAX; slot++ )
    {
        struct ToriRSServerNpc* npc = &srv->npcs[slot];

        if( !npc->active )
            continue;
        ToriRSServer_WorldNpcRefreshObservation(srv, npc);
    }
}

static void
phase_info(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;

    /* Before anything is encoded, so all three streams of every observer read
     * one snapshot of where everybody was this tick. */
    ToriRSServer_WorldRefreshObservation(srv);

    /*
     * `TORIRSSERVER_ANIM_TRACE=1` — what a tick is about to ENCODE, per player.
     *
     * Here rather than in the encoder because this is the last point at which
     * a mask is still a mask: `phase_cleanup` drops `masks`, `anim_id` and
     * `place_dirty` immediately after, so anything that reads them from outside
     * a tick reads the wrong thing and reads it silently.
     *
     * It exists because the ladder animation "worked" by every server-side
     * measure and drew nothing. One line of this said why: `anim=828 place=1
     * level=1` in a single encode — the climb and the arrival on the new plane
     * in one PLAYER_INFO. Any "the entity did not do the thing" bug in a mask
     * (anim, spotanim, say, hits) is one env var from being answered the same
     * way. See `~climb_ladder_anim` in ladders_stairs/scripts/ladders.rs2.
     */
    {
        static int trace_on = -1;

        if( trace_on < 0 )
            trace_on = getenv("TORIRSSERVER_ANIM_TRACE") != NULL;
        if( trace_on )
        {
            struct ToriRSServerPlayer* traced;

            TORIRSSERVER_FOR_EACH_PLAYER(srv, traced)
                fprintf(stderr,
                        "[anim-trace] tick=%d encode masks=0x%03x anim=%d place=%d level=%d "
                        "%d,%d\n",
                        srv->tick, traced->masks, traced->anim_id, traced->place_dirty,
                        traced->level, traced->x, traced->z);
        }
    }

    /* Derived client state, recomputed before anything is encoded. Cheap, and
     * it only writes when a value actually moved — so a quiet tick sends
     * nothing. */
    TORIRSSERVER_FOR_EACH_PLAYER(srv, player)
    {
        if( player->login_scene_pending || player->rebuild_scene_pending )
            continue;
        ToriRSServer_WorldSetActive(srv, player);
        ToriRSServer_WorldSyncCombatVarbits(srv);
    }
    /* One origin per player now: this walks every player and rebuilds only the
     * windows whose owners crossed their own margin — see maybe_rebuild. */
    maybe_rebuild(srv);
}

/*
 * Region music.
 *
 * The client cache names the tracks and says which varp bit records each
 * unlock, but it does not say which map square plays which track -- that is
 * server data and no cache carries it. `torirs_server_music_regions.gen.h` is a join
 * of a community region table with the cache's own DBTable 44; see
 * `tools/gen_music_regions.py` for provenance and `docs/AUDIO_ACCURACY.md` §2.
 *
 * A mapped square first unlocks its track if necessary, then starts it and
 * names it in the music tab. The unlock bit is a real varp bit the
 * music-player interface reads, so writing it is what makes the track
 * selectable afterwards rather than merely audible now. A MIDI_SONG has only
 * an archive id, so it cannot by itself populate the tab's display name.
 */
const struct ToriRSServerMusicRegion*
ToriRSServer_MusicForRegion(int region)
{
    /* The table is sorted by region, so this is a binary search rather than a
     * 433-entry walk on every map-square crossing. */
    int lo = 0;
    int hi = k_ToriRSServer_MusicRegionCount - 1;

    while( lo <= hi )
    {
        int mid = (lo + hi) / 2;
        int at = k_ToriRSServer_MusicRegions[mid].region;
        if( at == region )
            return &k_ToriRSServer_MusicRegions[mid];
        if( at < region )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

/*
 * The map square whose music this player should be hearing.
 *
 * Ordinarily the one they are standing on. Inside a map instance it is the one
 * the instance was *copied from*, and that difference is the whole reason this
 * function exists: `mapinstance_scan_pool` hands out squares from x >= 100, a
 * band chosen precisely because the real map does not reach it, so an
 * instanced player's own square is an address no music table has ever
 * described. Every instanced encounter in the game was therefore silent — not
 * "wrong track", no track — and the silence was indistinguishable from one of
 * the ~65,000 unmapped squares that are silent on purpose.
 *
 * The template square is the address that means something: docs/audio/
 * music_regions.tsv maps 9043 to Inferno because 9043 is where the Inferno's
 * map data lives, which is what `map_instance_setchunk` points at.
 *
 * This resolves per crossing rather than being cached on the player, and it
 * needs no invalidation: the latch in `ToriRSServer_WorldUpdateMap` fires on the
 * destination square, so entering, leaving and being rebuilt into a different
 * instance all re-ask. Walking between two destination squares of one instance
 * re-asks too and gets the same source, which the `music_track` compare below
 * already turns into a no-op.
 */
void
ToriRSServer_RegionSquareFor(
    struct ToriRSServerPlayer* player,
    int* out_map_x,
    int* out_map_z)
{
    int handle = ToriRSServer_MapInstanceFind(player->x, player->z);
    int src_x;
    int src_z;

    *out_map_x = player->x >> 6;
    *out_map_z = player->z >> 6;
    if( handle == 0 )
        return;
    if( !ToriRSServer_MapInstanceSourceTile(handle, player->level, player->x, player->z, &src_x,
                                         &src_z) )
        return; /* inside the reservation but on a zone nothing was copied to */
    *out_map_x = src_x >> 6;
    *out_map_z = src_z >> 6;
}

/**
 * The world's ambient bed for a map square.
 *
 * There is no region->soundscape table in any cache — the mapping is server
 * data and no copy of it survives, exactly as `gen_music_regions.py` says of
 * the music one — so this is not a lookup. It is one placeholder bed for the
 * whole world, kept because otherwise nothing reaches the group-15 decoder,
 * the multi-loop path or the timed random sets, and a subsystem nothing
 * reaches is one nobody notices is broken. Soundscape 1 is the record whose
 * four random sets `test_soundscape` transcribes; `TORIRSSERVER_AMBIENT=<id>`
 * picks another and `-1` silences it.
 *
 * What *is* keyed per square is the exception: a script may own its square's
 * ambience (`ambientsound`), and this must not talk over it. The QBD arena is
 * the case that motivated the split — a foreign rev-727 region whose cave
 * noise is authored as loc ambient emitters on its own scenery, so the
 * placeholder underneath it is a second soundscape from the wrong game.
 *
 * `ambient_script_map_*` is the square that claim was made for. While the
 * player is still on it, the world bed stays out of the way; stepping off it
 * releases the claim and the bed comes back. That comparison, rather than a
 * "has a script spoken" flag, is what makes this independent of whether the
 * latch runs before or after the script inside the entering tick.
 */
void
ToriRSServer_AmbientEnterRegion(
    struct ToriRSServerPlayer* player,
    int map_x,
    int map_z)
{
    const char* override;
    int scape;

    if( player->ambient_script_map_x == map_x && player->ambient_script_map_z == map_z )
        return;

    player->ambient_script_map_x = -1;
    player->ambient_script_map_z = -1;

    override = getenv("TORIRSSERVER_AMBIENT");
    scape = override ? atoi(override) : 1;
    if( scape < 0 || player->ambient_scape == scape )
        return;

    player->ambient_scape = scape;
    ToriRSServer_SendAmbientsoundStart(player, scape, 1);
}

void
ToriRSServer_MusicEnterRegion(
    struct ToriRSServerPlayer* player,
    int map_x,
    int map_z)
{
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();
    const struct ToriRSServerMusicRegion* track =
        ToriRSServer_MusicForRegion((map_x << 8) | map_z);

    if( !track )
        return; /* 433 squares are mapped; the rest of the world is silent */

    /*
     * Unlock first. `varp` is -1 for a track whose DBTable row carried no
     * unlock pair, which is a handful of them -- those play without ever
     * becoming selectable, which is better than writing varp -1.
     */
    if( track->varp >= 0 && track->bit >= 0 && track->varp < TORIRSSERVER_VARP_COUNT )
    {
        int mask = 1 << track->bit;
        if( (player->varps[track->varp] & mask) == 0 )
        {
            char line[128];
            /*
             * A bit write, not a varp write -- so it takes the varbit writers'
             * path (patch `varps[]`, then mark it for phase 10) rather than
             * `ToriRSServer_WorldSetVarp`.
             *
             * The distinction is enforced, not stylistic: music unlock flags
             * live in varps that also carry other varbits, and a whole-varp
             * write clears every neighbouring bit in the same word. The
             * selftest's carrier-write counter catches exactly this and caught
             * it here -- the first version of this function used the whole-varp
             * setter and wiped bits belonging to unrelated content.
             */
            player->varps[track->varp] |= mask;
            ToriRSServer_WorldMarkVarp(player, track->varp);
            snprintf(
                line,
                sizeof(line),
                "<col=ff0000>You have unlocked a new music track: %s",
                track->name);
            ToriRSServer_SendMessage(player, line);
        }
    }

    if( player->music_track != track->song )
    {
        player->music_track = track->song;
        /* Match the jukebox path: the UI learns the display name separately
         * from the audio packet, which only identifies the cache archive. */
        if( ids->com_music_now_playing_text > 0 )
            ToriRSServer_SendIfSettext(player, ids->com_music_now_playing_text, track->name);
        /* script9628 uses this proven reference profile for musical state
         * changes: 30 client cycles (600 ms) down and up. The backend has one
         * synthesizer, so it serializes rather than overlaps the two ramps. */
        ToriRSServer_SendMidiSongEnvelope(player, track->song, 0, 30, 0, 30);
    }
}

/**
 * The zone family's producer — `NetworkPlayer.updateMap`, triage §9 step 5c.
 *
 * Two latches, two granularities, four `snprintf`s' worth of dispatch. The
 * counts say 806 uses and hide a split: 427 of them (`zone`, `zoneexit`) key off
 * the 8-tile zone **including the level**, and 379 (`mapzone`, `mapzoneexit`)
 * key off the 64-tile map square with the level forced to 0. Re-measured against
 * the reference this stage: every one of the 427 has a five-part subject and
 * every one of the 379 has a three-part one beginning `0_`.
 *
 * So the two are not the same latch at a different scale. Climbing a ladder
 * without moving re-enters a *zone* and does not re-enter a *map square*, and a
 * single latch cannot express that whichever granularity it is kept at. The
 * ZoneMap (§3.17) is irrelevant to the 379 — nothing here consults it.
 *
 * Ordering is the reference's, and it is observable: map square before zone, and
 * within each, exit before enter. A teleport across both boundaries fires all
 * four in one tick with no special case.
 *
 * Nothing is *run* here. See `ToriRSServer_ScriptsQueueTriggerAt` for why phase 10
 * cannot be the execution site.
 */
static void
ToriRSServer_WorldUpdateMap(struct ToriRSServerPlayer* player)
{
    struct ToriRSServer* srv = player->world;
    int mx = player->x >> 6;
    int mz = player->z >> 6;
    int zx = player->x >> 3;
    int zz = player->z >> 3;

    if( player->last_map_x != mx || player->last_map_z != mz )
    {
        if( player->last_map_x >= 0 )
            ToriRSServer_ScriptsQueueTriggerAt(srv, SS_TRIGGER_MAPZONEEXIT, 0,
                                             player->last_map_x << 6, player->last_map_z << 6);
        ToriRSServer_ScriptsQueueTriggerAt(srv, SS_TRIGGER_MAPZONE, 0, player->x, player->z);
        player->last_map_x = mx;
        player->last_map_z = mz;
        /* The map square is exactly the granularity music is keyed at, which is
         * why this hangs off the mapzone latch rather than the zone one.
         *
         * The latch is the *destination* square (mx, mz) and the lookup is not:
         * inside an instance the player's own square is out past the edge of
         * the real map and describes nothing, so the track is resolved through
         * the square the instance was copied from. See
         * `ToriRSServer_RegionSquareFor`. */
        {
            int music_x;
            int music_z;

            ToriRSServer_RegionSquareFor(player, &music_x, &music_z);
            ToriRSServer_MusicEnterRegion(player, music_x, music_z);
            /* The bed is keyed at the same granularity, through the same
             * instance-aware square, for the same reason. */
            ToriRSServer_AmbientEnterRegion(player, music_x, music_z);
        }
    }

    if( player->last_zone_level != player->level || player->last_zone_x != zx ||
        player->last_zone_z != zz )
    {
        if( player->last_zone_level >= 0 )
            ToriRSServer_ScriptsQueueTriggerAt(srv, SS_TRIGGER_ZONEEXIT, player->last_zone_level,
                                             player->last_zone_x << 3, player->last_zone_z << 3);
        ToriRSServer_ScriptsQueueTriggerAt(srv, SS_TRIGGER_ZONE, player->level, player->x, player->z);
        player->last_zone_level = player->level;
        player->last_zone_x = zx;
        player->last_zone_z = zz;
    }

    /*
     * `SetMultiway` is the other half of the reference's zone branch and is
     * deliberately absent: it is driven by `World.gameMap.isMulti(zone)`, and
     * this tree has no multi-way map data to read. Adding a flag with nothing
     * behind it would be a wire packet asserting something nobody computed.
     */
}

/** 10. Everything the tick has to say, in the order the client expects it. */
static void
phase_client_out(struct ToriRSServerPlayer* player)
{
    struct ToriRSServer* srv = player->world;

    ToriRSServer_WorldSetActive(srv, player);

    /* A remote view has a different scene origin from every authoritative
     * entity and zone packet below. Sending even one would address the wrong
     * tile (and PLAYER_INFO would try to place the local actor outside the
     * temporary scene), so the view gets only a tick boundary until content or
     * the fail-safe ends it. */
    if( player->remote_view_active )
    {
        if( srv->tick < player->remote_view_until )
        {
            ToriRSServer_SendTickEnd(player);
            return;
        }
        ToriRSServer_WorldRemoteViewEnd(player);
    }

    /* "// - map update", the first thing `World.processClientsOut` does for a
     * player and the first thing done here. It only enqueues, so it is ahead of
     * every encoder below rather than entangled with them. */
    ToriRSServer_WorldUpdateMap(player);

    /*
     * The instance under this player is not the one its scene was copied from.
     *
     * `maybe_rebuild` cannot see this: it fires when the scene *window* moves,
     * and re-entering an instance does not move it — the allocator hands back
     * the same handle at the same square, so the coordinates are identical.
     * Without this the second `::zuk` plays against the first run's arena:
     * server collision and client scenery both still carry the crumbled walls
     * and the morphed-away roof, and the ledge behind them is exposed
     * (docs/ORANGE_WEDGE.md §18). The generation is pool-wide and bumped by
     * every build, so "same place, new map" is exactly what it detects.
     *
     * This player's own scene window is rebuilt here too, not just the
     * client's: it is a copy of the same descriptors and is equally stale.
     */
    {
        int instance_handle = ToriRSServer_MapInstanceFind(player->x, player->z);
        int instance_gen =
            instance_handle ? ToriRSServer_MapInstanceGeneration(instance_handle) : 0;

        if( getenv("TORIRS_GEN_PROBE") && instance_handle )
            fprintf(stderr, "  PROBE gen h=%d inst=%d scene=%d pending=%d\n", instance_handle,
                    instance_gen, player->scene_instance_generation, player->rebuild_pending);
        if( instance_gen != player->scene_instance_generation )
        {
            /*
             * Skip when a rebuild is already owed this tick (teleport in moved
             * the window and maybe_rebuild has run) — rebuilding the server
             * scene twice in one tick is wasted work, not a bug.
             *
             * BUT THE GENERATION IS ONLY CONSUMED BY A REBUILD THAT HAPPENED.
             * It used to be stamped either way, on the assumption that a
             * pending rebuild would cover this one. It does not when the window
             * has not MOVED: re-entering an activity gets the same handle at
             * the same base, so `maybe_rebuild` sees nothing to re-centre and
             * the owed rebuild is the client's, not the server's collision
             * copy. Stamping the generation there marked a scene as current
             * that had never been read from the new square.
             *
             * Measured on the Theatre: `::tob 2` refills reservation 1's zones
             * from m51_69 and the teleport sets `rebuild_pending`, so this
             * branch skipped and recorded the generation — and the server's
             * scene stayed the previous room's copy of m49_68 for the rest of
             * the run. Every `loc_find` in the Bloat room then answered about
             * Xarpus's square, which is why its central tank read as absent.
             *
             * Leaving it unconsumed costs at most one extra rebuild on the next
             * tick, which is the outcome the skip was protecting against in the
             * first place.
             */
            /*
             * The SERVER's copy is rebuilt unconditionally; only the client's
             * paperwork is conditional. The two were one branch, and they are
             * not the same thing: `rebuild_pending` says the CLIENT is owed a
             * REBUILD packet, while this is about the server's own collision
             * and loc arrays being a copy of the square the player is standing
             * in. Skipping the rebuild because the client already owed one left
             * the server reading the previous occupant of the same
             * reservation — forever, since the flag is set again every tick the
             * player is behind the scene barrier.
             *
             * Locs, occupancy and the npc roster are folded into the window
             * build itself — see world_window_scene_build. A first build
             * upgrades this player from the root binding, so refresh.
             */
            world_player_scene_build(srv, player);
            ToriRSServer_WorldSetActive(srv, player);
            player->scene_instance_generation = instance_gen;
            if( !player->rebuild_pending )
            {
                ToriRSServer_ZonePlayerReset(player);
                player->rebuild_pending = 1;
                player->place_dirty = 1;
            }
        }
    }

    /* A rebuild has to reach the client before the placement that depends on
     * it. Revision 239 replaces its WorldView asynchronously and explicitly
     * acknowledges that replacement with MAP_BUILD_COMPLETE. Do not merely
     * put dependent packets later in the same socket stream: stateful zone
     * changes can be replayed after a slow load, but LOC_ANIM and projectiles
     * are tick events and are discarded by phase_cleanup. Pause the player at
     * this barrier so its scripts cannot create either until the new scene is
     * real. */
    if( player->rebuild_pending )
    {
        ToriRSServer_SendRebuild(player);
        player->rebuild_pending = 0;
        /* Instance scenes are assembled at runtime and are the path where
         * content immediately follows a teleport with cutscene zone events.
         * Normal edge rebuilds carry no such ephemeral transition in this
         * server and retain their existing continuous-walk behavior.
         *
         * A VESSEL DECK IS NOT THAT PATH (docs/sailing_coverage.csv SAIL-37).
         * Boarding is a map-instance teleport like any other, so this barrier
         * used to arm on it — and `phase_players` skips every player holding
         * it, so a player who boarded took no step, ran no queue, resumed no
         * script and swung at nothing until MAP_BUILD_COMPLETE came back. A
         * boarding carries no cutscene zone events to protect, the deck the
         * client actually renders arrives on the world-entity path
         * (REBUILD_WORLDENTITY inside the hull's own view, which has its own
         * lifecycle and its own acknowledgement), and freezing the helmsman
         * of a moving hull is worse than any race this guards. */
        if( srv->wire && srv->wire->revision >= 239 &&
            ToriRSServer_MapInstanceFind(player->x, player->z) != 0 &&
            !ToriRSServer_VesselAtTile(srv, player->x, player->z) &&
            (player->x != player->v5_last_x || player->z != player->v5_last_z ||
             player->level != player->v5_last_level) )
        {
            player->rebuild_scene_pending = 1;
            if( srv->verbose )
                fprintf(stderr,
                        "torirsserver: rebuilt scene barrier armed; waiting for "
                        "MAP_BUILD_COMPLETE\n");
            /* The new WorldView cannot finish without the local placement
             * that follows REBUILD_REGION. This is the same indivisible
             * rebuild/GPI prefix used at login; only zone mutations and
             * scripts belong behind the acknowledgement. NPC_INFO belongs
             * beside it so the replacement view starts from one coherent
             * entity snapshot, and TICK_END closes that packet group. */
            ToriRSServer_SendSetActiveWorld(player);
            ToriRSServer_SendPlayerInfo(player);
            ToriRSServer_SendNpcInfo(player);
            ToriRSServer_SendTickEnd(player);
            return;
        }
        /* Doors. REBUILD_NORMAL rebuilds the client's scene from the cache,
         * which puts every opened door back the way the map square has it. The
         * loop that re-sent them used to live here, walking the scene's own
         * changed list — which the rebuild had just freed, so it re-sent
         * nothing. `ToriRSServer_ZonePlayerReset` (in `maybe_rebuild`, and in
         * `climb`) marks every zone unloaded instead, and the zone flush below
         * re-states each one from the ZoneMap. */
        /* Keep move_count: walk/run bits are world-absolute and stay valid
         * after App_WorldRebuildShift. Clearing them + place_dirty used to
         * force a mid-walk tile-centre snap. */
    }

    /* Root-world info batches select their world and plane immediately before
     * the entity streams. In particular this must follow REBUILD_REGION: the
     * login client has no initialized replacement WorldView before that
     * packet, while the active selection still has to precede PLAYER_INFO. */
    ToriRSServer_SendSetActiveWorld(player);
    /*
     * Sailing (docs/SAILING_PLAN.md S2). After the root selection because the
     * update records address the ROOT view's entity list positionally, and
     * before PLAYER_INFO because a spawn here is what makes the boat's view
     * live — the deck rebuild it sandwiches would otherwise name a view the
     * client asserts does not exist. The call re-selects the root on its way
     * out, so everything below is root-addressed as it always was.
     */
    ToriRSServer_SendWorldEntityInfo(player);
    ToriRSServer_SendPlayerInfo(player);
    ToriRSServer_SendNpcInfo(player);

    /* Zone updates go here, between the entity streams and the containers,
     * because that is where the reference puts `updateZones()` — and because a
     * zone packet naming a tile is only meaningful once the client has been
     * placed by the stream above it. */
    ToriRSServer_ZoneUpdatePlayer(player);

    /* After the containers would have changed but before they are flushed:
     * weight is a function of what is in them, and the orb should not lag a
     * tick behind the item that changed it. */
    run_energy_flush(srv);

    /* Content refreshes the bonus screen on equip/unequip directly, so no
     * engine-driven repaint is needed here any more. */
    (void)player->worn_dirty;

    /*
     * Every bound container, in one loop.
     *
     * This was two hardcoded `ToriRSServer_SendInvPartial` calls naming the
     * backpack and the worn set, which is why a fourth container could be
     * written but never reached the client. The loop picks partial or full from
     * the row's slot count, not from which container it is: 304 of the cache's
     * 1,026 invs are past the 32 slots UPDATE_INV_PARTIAL's mask can address.
     */
    ToriRSServer_ContainerFlush(player);

    /* Varps are NOT sent from here. `ToriRSServer_WorldMarkVarp` puts each one on
     * the wire at the point of the write, which is where the reference puts it
     * and the only place that preserves a script's own ordering against
     * `if_opensub` — see its comment for the panel that proved it. */

    ToriRSServer_BankFlush(srv);

    /* Stats. UPDATE_STAT carries the boosted level as well as the base one, and
     * the boosted hitpoints level is what the health orb draws — so every hit
     * taken has to reach the client here, not only every level gained. */
    for( int stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
    {
        if( (player->stat_dirty & (1u << stat)) == 0 )
            continue;
        ToriRSServer_SendStat(
            player,
            stat,
            player->stat_level[stat],
            player->stat_xp_tenths[stat] / 10,
            player->stat_boosted[stat]);
    }

    if( player->clear_map_flag )
        ToriRSServer_SendUnsetMapFlag(player);

    ToriRSServer_SendTickEnd(player);
}

static void
phase_clients_out(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;

    /* Shared (world-scoped) containers first, so a shop bought from or sold
     * to this tick reaches every listener's outgoing batch before that
     * player's own phase_client_out closes it with a tick-end. A player's own
     * containers are still flushed from inside phase_client_out, unchanged —
     * this is the sibling pass for rows no single player owns. */
    ToriRSServer_ContainerFlushWorld(srv);

    TORIRSSERVER_FOR_EACH_PLAYER(srv, player)
    {
        if( player->login_scene_pending || player->rebuild_scene_pending )
            continue;
        phase_client_out(player);
    }
}

/** 11. Drop everything that described only this tick. */
static void
phase_cleanup_player(struct ToriRSServerPlayer* player)
{
    player->stat_dirty = 0;
    player->inv_dirty = 0;
    player->worn_dirty = 0;
    player->clear_map_flag = 0;
    /* One social packet per tick, spent by whichever of the six arrived first.
     * The reference clears it in `resetEntity`, which is this phase. */
    player->social_protect = 0;

    /* Extended info describes one tick only. Clearing it here rather than
     * inside the encoder means a field set after PLAYER_INFO was written still
     * survives to the next tick instead of being silently dropped.
     *
     * `anim_id` goes with the mask, not with the entity. It is the incumbent
     * the priority gate compares against (ToriRSServer_AnimPlayNpc), and an
     * incumbent that outlives its tick is one that keeps refusing lower-priority
     * animations forever — a goblin that lands one attack would never flinch
     * again. The reference clears it in the same reset, for the same reason. */
    player->masks = 0;
    /* With the masks, and for exactly the same reason: the hitmark list
     * describes one tick, and every recipient's PLAYER_INFO has to have been
     * written before it is dropped. Clearing it anywhere earlier — inside the
     * encoder, say — is what would make a second observer see no splats. */
    player->hitmark_count = 0;
    player->anim_id = -1;
    player->anim_delay = 0;
    /* With the masks, and for the same reason: an absolute placement describes
     * one tick, and every recipient's PLAYER_INFO has to have been written
     * before it is dropped. ToriRSServer_SendPlayerInfo used to clear it. */
    player->place_dirty = 0;
    /* With it: `tele_glide` qualifies one placement. Left set, the *next*
     * teleport — a ladder, a lodestone — would ask the client to walk there,
     * and observers would be handed last tick's steps for it. */
    player->tele_glide = 0;
    player->tele_glide_step_count = 0;
    /*
     * `move_dirs`/`move_count` are NOT cleared here, and must not be: they are
     * read by every *other* player's PLAYER_INFO in phase 10, and phase 5 rewrites
     * them from scratch for whoever moves next tick. Clearing them here would be
     * harmless with one player and would delete the steps the observers still
     * need with two — the same class of bug as clearing `masks` inside the
     * encoder.
     */
}

static void
phase_cleanup(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;

    TORIRSSERVER_FOR_EACH_PLAYER(srv, player)
    {
        if( player->login_scene_pending || player->rebuild_scene_pending )
            continue;
        phase_cleanup_player(player);
    }

    for( int i = 0; i < srv->npc_slot_max; i++ )
    {
        srv->npcs[i].masks = 0;
        /* With the masks — see the player's copy of this line. */
        srv->npcs[i].hitmark_count = 0;
        /* LostCity `Npc.processMovement`: `lastMovement = currentTick + 1`
         * whenever the tile changed. Recorded here, off the tick's final
         * `step_dir`, so every mover feeds it. Read by `npc_arrivedelay`. */
        if( srv->npcs[i].step_dir >= 0 )
            srv->npcs[i].last_movement = srv->tick + 1;
        srv->npcs[i].step_dir = -1;
        srv->npcs[i].run_dir = -1;
        /* With the masks, and for the same reason: a teleport describes one
         * tick, and every recipient's NPC_INFO has to have been written before
         * it is dropped. See `ToriRSServerNpc.tele`. */
        srv->npcs[i].tele = 0;
        srv->npcs[i].anim_id = -1;
        srv->npcs[i].anim_delay = 0;
        /*
         * A DEATH SHOWN IS A DEATH OWED.
         *
         * A script that plays an npc's own death seq on a living npc has one
         * tick to make it dead — hitpoints to zero, or a real death armed. If
         * it is still standing on the tick after, with its death already on
         * every client's screen, the next hit double-books the death and the
         * client shows a frozen corpse. Caught for every npc in the world
         * rather than in one encounter's test, because the next encounter to
         * make this mistake will not be the Maiden.
         */
        /* Stamp the tick the death seq was played on. `ToriRSServer_AnimPlayNpc`
         * has no server pointer to read the clock from, and cleanup runs at the
         * end of the very tick it was played, so this is that tick. */
        if( srv->npcs[i].death_seq_sent && srv->npcs[i].death_seq_tick < 0 )
            srv->npcs[i].death_seq_tick = srv->tick;
        /* The held removal, now that this tick's NPC_INFO has gone out. */
        if( (srv->npcs[i].free_deferred_for_anim ||
             srv->npcs[i].free_wanted) && srv->npcs[i].active )
            ToriRSServer_WorldNpcFree(srv, i);
        if( srv->npcs[i].scripted_death_pending > 0 && srv->npcs[i].active )
        {
            if( --srv->npcs[i].scripted_death_pending == 0 &&
                srv->npcs[i].hitpoints > 0 && srv->npcs[i].death_tick < 0 )
            {
                srv->scripted_death_violations++;
                fprintf(stderr,
                        "torirsserver: CONTENT CONTRACT: npc type %d (slot %d) was shown "
                        "its death animation (seq %d) by a script and is still alive "
                        "a tick later (%d hp, not dying) — a hit now double-books the "
                        "death and the client shows a frozen corpse. Zero its "
                        "hitpoints (npc_statsub) or arm a real death.\n",
                        srv->npcs[i].type, i, srv->npcs[i].death_seq,
                        srv->npcs[i].hitpoints);
            }
        }
    }

    /* Shared shop stock, one nudge per baseline slot toward its target count
     * (docs/SHOPS_PLAN.md §3.4) — LostCity's World.ts puts this in the same
     * cleanup phase, right after the npc reset above. Marks dirty rows; the
     * client repaints on its own via `if_setoninvtransmit`, so nothing else
     * has to know a restock happened. */
    ToriRSServer_ShopRestockTick(srv, srv->tick);

    /* Every player's NPC_INFO for this tick is behind us (phase_clients_out
     * already ran) — slots freed this tick can now actually be reused. See
     * docs/torirs_server_npc_slot_reap.md. */
    ToriRSServer_WorldNpcReap(srv);
    /* Same reasoning, same timing, for pids — see ToriRSServer_WorldPlayerFree. */
    ToriRSServer_WorldPlayerReap(srv);

    /* The zones' event buffers, now that every client has been given them. The
     * state they hold — loc records, obj and npc membership — is not touched. */
    ToriRSServer_ZoneReset(srv);
}

static char const* const g_tick_bd_names[TICK_BD_PHASES] = {
    "world",  "clients_in", "npc_events", "npcs",    "players", "logouts",    "logins",
    "zones",  "worldmap",   "vessels",    "info",    "clients_out", "cleanup"
};

static char const* const g_pp_names[PP_COUNT] = {
    "scripts", "face", "locked",          "interact_pre",
    "approach", "advance", "interact_post", "tail"
};

void
ToriRSServer_WorldTick(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;
    uint64_t bd[TICK_BD_PHASES];
    uint64_t bd_t;
    uint64_t bd_t0;
    int bd_on = tick_bd_on();
    int bd_i = 0;

#define BD_MARK()                                                                                  \
    do                                                                                             \
    {                                                                                              \
        if( bd_on )                                                                                \
        {                                                                                          \
            uint64_t bd_now = tick_bd_now_us();                                                    \
            bd[bd_i++] = bd_now - bd_t;                                                            \
            bd_t = bd_now;                                                                         \
        }                                                                                          \
    } while( 0 )

    memset(bd, 0, sizeof(bd));
    memset(g_pp, 0, sizeof(g_pp));
    g_ToriRSServer_ScriptUs = 0;
    g_ToriRSServer_ScriptRuns = 0;
    g_ToriRSServer_ScriptSlowUs = 0;
    g_ToriRSServer_ScriptSlowName[0] = '\0';
    g_collision_route_us = 0;
    g_collision_route_calls = 0;
    g_collision_route_tiles = 0;
    g_ssvm_ops = 0;
    /* 150 KB of counters, cleared only for a run that is going to read them. The
     * VM fills them under its own switch; this one keeps the clear off a tick
     * that has no breakdown to print. */
    if( bd_on )
    {
        memset(g_ssvm_op_us, 0, sizeof(g_ssvm_op_us));
        memset(g_ssvm_op_hits, 0, sizeof(g_ssvm_op_hits));
    }
    bd_t = bd_on ? tick_bd_now_us() : 0;
    bd_t0 = bd_t;

    /*
     * The tick borrows `active_player` and gives it back.
     *
     * Every per-player phase below moves it, so without this a caller that had
     * set it — a host between ticks, the selftest driving one player directly —
     * would find it pointing at whoever the pool happened to end on. Restoring
     * is not the same as it being meaningless outside a phase: it is, and a
     * host that reads it without having set it is reading a leftover.
     */
    struct ToriRSServerPlayer* caller_active = srv->active_player;

    srv->tick++;

    phase_world(srv);
    BD_MARK();
    phase_clients_in(srv);
    BD_MARK();
    phase_npc_events(srv);
    BD_MARK();
    phase_npcs(srv);
    BD_MARK();
    phase_players(srv);
    BD_MARK();
    phase_logouts(srv);
    BD_MARK();
    phase_logins(srv);
    BD_MARK();
    phase_zones(srv);
    BD_MARK();
    /* After movement, before the info streams: the world map marker is derived
     * state, and this is the only place it can be refreshed with the tile the
     * player will actually be reported on. */
    TORIRSSERVER_FOR_EACH_PLAYER(srv, player)
    {
        if( player->login_scene_pending || player->rebuild_scene_pending )
            continue;
        ToriRSServer_WorldSetActive(srv, player);
        ToriRSServer_WorldMapTick(srv);
    }
    BD_MARK();
    /* Vessels move after every actor has, and before the info streams, so the
     * tick a hull advances is the tick S2's WORLDENTITY_INFO will describe it
     * on — never a frame behind the players standing on it
     * (docs/SAILING_PLAN.md S1). */
    ToriRSServer_VesselTickAll(srv);
    BD_MARK();
    phase_info(srv);
    BD_MARK();
    phase_clients_out(srv);
    BD_MARK();
    phase_cleanup(srv);
    BD_MARK();

    ToriRSServer_WorldSetActive(srv, caller_active);

    if( bd_on )
    {
        uint64_t total = tick_bd_now_us() - bd_t0;

        if( total >= (uint64_t)g_tick_bd_ms * 1000u )
        {
            FILE* out = g_tick_bd_out;

            fprintf(out, "server_bd: tick %d total %.2f ms |", srv->tick, total / 1000.0);
            for( int i = 0; i < TICK_BD_PHASES; i++ )
                if( bd[i] >= 100 )
                    fprintf(out, " %s %.2f", g_tick_bd_names[i], bd[i] / 1000.0);
            fprintf(out, " || players:");
            for( int i = 0; i < PP_COUNT; i++ )
                if( g_pp[i] >= 100 )
                    fprintf(out, " %s %.2f", g_pp_names[i], g_pp[i] / 1000.0);
            fprintf(out, " || script %.2f x%d ops %llu route %.2f x%d tiles %d",
                    g_ToriRSServer_ScriptUs / 1000.0, g_ToriRSServer_ScriptRuns,
                    (unsigned long long)g_ssvm_ops, g_collision_route_us / 1000.0,
                    g_collision_route_calls, g_collision_route_tiles);
            if( g_ToriRSServer_ScriptSlowName[0] )
                fprintf(out, " slowest %s %.2f", g_ToriRSServer_ScriptSlowName,
                        g_ToriRSServer_ScriptSlowUs / 1000.0);

            /* The tick's four costliest engine ops, by name. Empty unless
             * TORIRS_SSVM_OPS=1 armed the VM's per-instruction clock. Selection
             * is a scan per rank with the ones already printed struck out; four
             * passes over the opcode space is nothing against a tick already
             * slow enough to be printed. */
            {
                int taken[4];
                int n_taken = 0;

                for( int rank = 0; rank < 4; rank++ )
                {
                    int best = -1;

                    for( int i = 0; i < SSVM_OP_TIMING_MAX; i++ )
                    {
                        int skip = 0;

                        if( g_ssvm_op_us[i] == 0 )
                            continue;
                        for( int t = 0; t < n_taken; t++ )
                            skip |= taken[t] == i;
                        if( skip )
                            continue;
                        if( best < 0 || g_ssvm_op_us[i] > g_ssvm_op_us[best] )
                            best = i;
                    }
                    if( best < 0 )
                        break;
                    taken[n_taken++] = best;
                    fprintf(out, " | op %s %.2f x%d", SSVM_OpcodeName(best),
                            g_ssvm_op_us[best] / 1000.0, g_ssvm_op_hits[best]);
                }
            }
            fprintf(out, "\n");
        }
    }

#undef BD_MARK
}
