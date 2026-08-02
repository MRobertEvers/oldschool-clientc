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
 * form the info streams carry happens in mock230_encode.c, which is the only
 * place that needs to know about the origin zone.
 */
#include "mock230.h"

#include "mock230_content.h"
#include "mock230_db.h"
#include "mock230_equipment.h"
#include "mock230_friends.h"
#include "mock230_ids.h"
#include "mock230_scene.h"
#include "engine/world_builder/collision_map.h"
#include "ss_trigger.h"
#include "ssvm_provider.h"

#include "net/jbase37.h"
#include "net/rev/pktnames.h"
#include "net/wordpack.h"

#include <rsareabuf.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* The tile a session logs in on. See mock230_world_set_home. */
static int g_home_x = 3222;
static int g_home_z = 3218;

static void
mock230_world_build_entities(struct Mock230Server* srv);

/* Where the scene reads its map squares from. Set once beside the other cache
 * loaders; kept here rather than threaded through every rebuild path because
 * the mock opens exactly one cache. */
static char g_cache_dir[512] = MOCK230_CACHE_DIR_DEFAULT;

void
mock230_world_set_cache_dir(const char* dir)
{
    snprintf(g_cache_dir, sizeof(g_cache_dir), "%s", dir);
}

const char*
mock230_world_cache_dir(void)
{
    return g_cache_dir;
}


/* xorshift32: a session replays identically for a given seed, which matters
 * when a screenshot test has to land on the same frame twice. */
static uint32_t
next_random(struct Mock230Server* srv)
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
    struct Mock230Server* srv,
    int lo,
    int hi)
{
    if( hi <= lo )
        return lo;
    return lo + (int)(next_random(srv) % (uint32_t)(hi - lo + 1));
}

int
mock230_random(
    struct Mock230Server* srv,
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
 * same event as a spawn from the roamer's point of view, and mock230_combat.c
 * was answering it with `MOCK230_ATTACK_SPEED` — a different quantity that
 * happened to be a plausible number of ticks.
 */
void
mock230_world_npc_roam_stagger(
    struct Mock230Server* srv,
    struct Mock230Npc* npc)
{
    npc->next_roam_tick = srv->tick + random_range(srv, 5, 30);
}

static int
sign_of(int value)
{
    return value > 0 ? 1 : (value < 0 ? -1 : 0);
}

static void
say(
    struct Mock230Server* srv,
    const char* fmt,
    ...)
{
    char text[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    mock230_send_message(srv->active_player, text);
}

/* ------------------------------------------------------------------ */
/* Containers                                                          */
/* ------------------------------------------------------------------ */

static void
inv_set(
    struct Mock230Player* player,
    int slot,
    int obj_id,
    int count)
{
    if( slot < 0 || slot >= MOCK230_INV_SLOTS )
        return;
    player->inv[slot].obj_id = obj_id;
    player->inv[slot].count = count;
    player->inv_dirty |= 1u << slot;
}

static void
worn_set(
    struct Mock230Player* player,
    int slot,
    int obj_id,
    int count)
{
    if( slot < 0 || slot >= MOCK230_WORN_SLOTS )
        return;
    player->worn[slot].obj_id = obj_id;
    player->worn[slot].count = count;
    player->worn_dirty |= 1u << slot;
    player->masks |= MOCK230_PMASK_APPEARANCE;
}

static int
inv_first_free(const struct Mock230Player* player)
{
    for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
        if( player->inv[i].obj_id < 0 )
            return i;
    return -1;
}

/* Slot already holding a stack of `obj_id`, or -1 — the obj is not stackable
 * or there is none yet. Stackability is the cache's own field, not a list. */
static int
inv_stack_slot(
    const struct Mock230Player* player,
    int obj_id)
{
    if( !mock230_objinfo(obj_id)->stackable )
        return -1;
    for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Equipment                                                           */
/* ------------------------------------------------------------------ */

/*
 * Wear or wield the item in backpack `slot`.
 *
 * The interesting case is the one the cache's wearpos_2 / wearpos_3 fields
 * exist for: an item can claim slots beyond its own. A two-handed weapon
 * claims the shield slot, a full helm claims hair and jaw. Anything already in
 * a claimed slot has to come off first, and coming off needs a free backpack
 * slot — which is why a shortbow refuses to equip when the backpack is full
 * and a shield is on.
 */
static void
equip_from_slot(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Player* player = srv->active_player;
    int obj_id = (slot >= 0 && slot < MOCK230_INV_SLOTS) ? player->inv[slot].obj_id : -1;
    int count = (slot >= 0 && slot < MOCK230_INV_SLOTS) ? player->inv[slot].count : 0;
    const struct Mock230ObjInfo* info;
    int claimed[3];
    int claimed_count = 0;
    int returning = 0;

    if( obj_id < 0 )
        return;
    info = mock230_objinfo(obj_id);
    if( info->wearpos < 0 || info->wearpos >= MOCK230_WORN_SLOTS )
    {
        mock230_say(srv, "equip_wrong_slot_message", NULL);
        return;
    }
    /* The level requirement. Engine rather than content for the same reason
     * "Attack" is: it applies to every wearable obj in the cache, and a
     * per-item script binding would be a second copy of a table the content
     * tree already states. Content can still override by binding [opheld2]. */
    if( !mock230_equipment_may_wear(srv, obj_id) )
        return;

    claimed[claimed_count++] = info->wearpos;
    if( info->wearpos_2 >= 0 && info->wearpos_2 < MOCK230_WORN_SLOTS )
        claimed[claimed_count++] = info->wearpos_2;
    if( info->wearpos_3 >= 0 && info->wearpos_3 < MOCK230_WORN_SLOTS )
        claimed[claimed_count++] = info->wearpos_3;

    /* A slot the incoming item claims only needs unequipping when something is
     * actually in it. The backpack slot being vacated by this equip counts as
     * free, hence the -1 below. */
    for( int i = 0; i < claimed_count; i++ )
        if( player->worn[claimed[i]].obj_id >= 0 )
            returning++;
    if( returning - 1 > 0 )
    {
        int free_slots = 0;
        for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
            if( player->inv[i].obj_id < 0 )
                free_slots++;
        if( free_slots < returning - 1 )
        {
            mock230_say(srv, "equip_no_space_message", NULL);
            return;
        }
    }

    /* Vacate the backpack slot first so it can receive the first unequip. */
    inv_set(player, slot, -1, 0);
    for( int i = 0; i < claimed_count; i++ )
    {
        int worn_id = player->worn[claimed[i]].obj_id;
        int worn_count = player->worn[claimed[i]].count;
        if( worn_id < 0 )
            continue;
        worn_set(player, claimed[i], -1, 0);
        {
            int dest = inv_first_free(player);
            if( dest >= 0 )
                inv_set(player, dest, worn_id, worn_count);
        }
    }

    worn_set(player, info->wearpos, obj_id, count > 0 ? count : 1);
    mock230_say(srv, "equip_message", info->name);
}

/* Take the item off worn slot `slot` and put it back in the backpack. */
static void
unequip_slot(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Player* player = srv->active_player;
    int obj_id;
    int dest;

    if( slot < 0 || slot >= MOCK230_WORN_SLOTS )
        return;
    obj_id = player->worn[slot].obj_id;
    if( obj_id < 0 )
        return;
    dest = inv_first_free(player);
    if( dest < 0 )
    {
        mock230_say(srv, "equip_no_space_message", NULL);
        return;
    }
    inv_set(player, dest, obj_id, player->worn[slot].count);
    worn_set(player, slot, -1, 0);
    mock230_say(srv, "unequip_message", mock230_objinfo(obj_id)->name);
}

/* ------------------------------------------------------------------ */
/* Movement                                                            */
/* ------------------------------------------------------------------ */

/* Drop whatever route the player was walking. Exported because arriving at a
 * combat target ends the walk, and that decision is combat's
 * (`mock230_combat_player_approach`), not this file's. */
void
mock230_world_steps_clear(struct Mock230Player* player)
{
    player->step_count = 0;
    player->step_head = 0;
}

static void
steps_clear(struct Mock230Player* player)
{
    mock230_world_steps_clear(player);
}

static void
steps_push(
    struct Mock230Player* player,
    int x,
    int z)
{
    if( player->step_count >= MOCK230_STEP_MAX )
        return;
    player->steps[player->step_count].x = (int16_t)x;
    player->steps[player->step_count].z = (int16_t)z;
    player->step_count++;
}

/*
 * Fill in the tiles between the last queued position and (x, z).
 *
 * The client's move packet carries up to 25 waypoints of the path it already
 * routed, which for a short walk is every tile and for a long one is a
 * truncated prefix. Either way consecutive waypoints can be more than a tile
 * apart, so the gap has to be filled in.
 *
 * The filling is a real route through the scene's collision, not the
 * straight-line interpolation this used to do: the client routes around a wall
 * and sends the turning points, and interpolating between two turning points in
 * a straight line cuts the corner the client walked around. The player then
 * stands inside the castle wall, and every subsequent step disagrees with what
 * is on screen.
 *
 * An unreachable waypoint falls back to the straight line rather than to
 * nothing. A server that silently refuses to move is much harder to diagnose
 * than one that walks somewhere slightly wrong, and this is a mock.
 */
static void
steps_walk_to(
    struct Mock230Player* player,
    int x,
    int z)
{
    int cur_x = player->step_count > 0 ? player->steps[player->step_count - 1].x : player->x;
    int cur_z = player->step_count > 0 ? player->steps[player->step_count - 1].z : player->z;
    int path_x[MOCK230_STEP_MAX];
    int path_z[MOCK230_STEP_MAX];
    int steps = mock230_scene_route(player->level, cur_x, cur_z, x, z, path_x, path_z,
                                    MOCK230_STEP_MAX);

    if( steps >= 0 )
    {
        for( int i = 0; i < steps; i++ )
            steps_push(player, path_x[i], path_z[i]);
        return;
    }

    /*
     * Route failed, so do not move.
     *
     * There used to be a straight-line fallback here that stepped toward the
     * destination one tile at a time regardless of collision. A route of -1
     * means the flood could not reach the target, and the reason it could not
     * is almost always a wall — so the fallback's entire job was to walk
     * through the thing that had just refused the path, up to MOCK230_STEP_MAX
     * tiles of it.
     *
     * The reference has no such fallback: an unreachable click is a click that
     * does nothing. Leaving the step queue empty is the correct answer.
     */
}

/*
 * The tile to stand on to reach (x, z) from (from_x, from_z).
 *
 * Pick an ORTHOGONAL neighbour of the target, not a diagonal one.
 *
 * The player's approach used to be `dest = target - sign(target - from)`, which
 * lands on a diagonal whenever both axes differ — and melee cannot reach a
 * diagonal (see in_attack_range in mock230_combat.c). The walker arrived at the
 * corner, the range test refused, and the fight stalled with both parties
 * standing still: the "squaring up" step never happened.
 *
 * Of the four orthogonal neighbours, take the one nearest the approacher that it
 * can actually stand on, so the approach still looks direct.
 *
 * Shared with the npc chase, which wants the same tile for the same reason: an
 * npc closing on the player has to end up square with it or it can never swing.
 * That used to be a second rule in mock230_combat.c — "one tile out on both axes
 * means drop an axis" — which is this one written from the other end and does
 * not survive an obstacle in the way.
 */
void
mock230_world_beside_tile(
    int level,
    int from_x,
    int from_z,
    int x,
    int z,
    int* out_x,
    int* out_z)
{
    static const int k_off_x[4] = { -1, 1, 0, 0 };
    static const int k_off_z[4] = { 0, 0, -1, 1 };
    int best_x = x - sign_of(x - from_x);
    int best_z = z - sign_of(z - from_z);
    int best_cost = -1;

    for( int i = 0; i < 4; i++ )
    {
        int cand_x = x + k_off_x[i];
        int cand_z = z + k_off_z[i];
        int dx = cand_x - from_x;
        int dz = cand_z - from_z;
        int cost;

        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        cost = dx > dz ? dx : dz;

        if( mock230_scene_walk_blocked(level, cand_x, cand_z) )
            continue;
        if( best_cost < 0 || cost < best_cost )
        {
            best_cost = cost;
            best_x = cand_x;
            best_z = cand_z;
        }
    }

    *out_x = best_x;
    *out_z = best_z;
}

/*
 * Queue a walk to a tile beside (x, z) rather than onto it.
 *
 * Every "click a thing" op wants this: standing on top of an npc or a door is
 * wrong, and the adjacent tile is the one an interaction happens from.
 */
void
mock230_world_walk_beside(
    struct Mock230Server* srv,
    int x,
    int z)
{
    struct Mock230Player* player = srv->active_player;
    int best_x;
    int best_z;

    mock230_world_beside_tile(player->level, player->x, player->z, x, z, &best_x, &best_z);

    steps_clear(player);
    player->dest_x = best_x;
    player->dest_z = best_z;
    steps_walk_to(player, player->dest_x, player->dest_z);
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
static int
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
mock230_world_interaction_clear(struct Mock230Server* srv)
{
    memset(&srv->active_player->interaction, 0, sizeof(srv->active_player->interaction));
    srv->active_player->interaction.kind = MOCK230_INTERACT_NONE;
    srv->active_player->interaction.npc_slot = -1;
}

void
mock230_world_interaction_set(
    struct Mock230Server* srv,
    enum Mock230InteractionKind kind,
    int op,
    int npc_slot,
    int target_id,
    int tile_x,
    int tile_z,
    int level,
    int size_x,
    int size_z)
{
    struct Mock230Interaction* interaction = &srv->active_player->interaction;

    interaction->kind = kind;
    interaction->op = op;
    interaction->npc_slot = npc_slot;
    interaction->target_id = target_id;
    interaction->x = tile_x;
    interaction->z = tile_z;
    interaction->level = level;
    interaction->size_x = size_x > 0 ? size_x : 1;
    interaction->size_z = size_z > 0 ? size_z : 1;
    interaction->ap_tried = 0;
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
    int slot = mock230_scene_find_loc(tile_x, tile_z, level, loc_id);

    if( slot >= 0 )
        return slot;
    return mock230_scene_find_loc(tile_x, tile_z, level, -1);
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
    struct Mock230Server* srv,
    int* out_x,
    int* out_z,
    int* out_size_x,
    int* out_size_z)
{
    struct Mock230Interaction* interaction = &srv->active_player->interaction;

    switch( interaction->kind )
    {
    case MOCK230_INTERACT_NPC:
    {
        struct Mock230Npc* npc;

        if( interaction->npc_slot < 0 || interaction->npc_slot >= MOCK230_NPC_MAX )
            return 0;
        npc = &srv->npcs[interaction->npc_slot];
        /* Slot reuse: same index, different npc. Acting on it would attack
         * whatever respawned there. */
        if( !npc->active || npc->type != interaction->target_id )
            return 0;
        if( npc->level != srv->active_player->level )
            return 0;
        if( npc->death_tick >= 0 )
            return 0;
        *out_x = npc->x;
        *out_z = npc->z;
        *out_size_x = interaction->size_x;
        *out_size_z = interaction->size_z;
        return 1;
    }

    case MOCK230_INTERACT_LOC:
    {
        int slot = find_interaction_loc(interaction->x, interaction->z, interaction->level,
                                        interaction->target_id);
        struct Mock230SceneLoc* loc = mock230_scene_loc(slot);

        /* A tile with nothing on it at all is genuinely stale. */
        if( !loc )
            return 0;
        *out_x = loc->x;
        *out_z = loc->z;
        *out_size_x = loc->size_x > 0 ? loc->size_x : 1;
        *out_size_z = loc->size_z > 0 ? loc->size_z : 1;
        return 1;
    }

    case MOCK230_INTERACT_OBJ:
        for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
        {
            struct Mock230GroundObj* obj = &srv->ground[i];

            if( !obj->active || obj->obj_id != interaction->target_id )
                continue;
            if( obj->x != interaction->x || obj->z != interaction->z )
                continue;
            if( obj->level != interaction->level )
                continue;
            *out_x = obj->x;
            *out_z = obj->z;
            *out_size_x = 1;
            *out_size_z = 1;
            return 1;
        }
        return 0;

    case MOCK230_INTERACT_NONE:
    default:
        return 0;
    }
}

/*
 * The category rung of the trigger lookup, per subject kind.
 *
 * `Player.getOpTrigger` passes `type.category` for npc, loc and obj alike. Two
 * of the three can answer here; loc is -1 on purpose:
 *
 * - **obj** — config opcode 94, a number the cache states and
 *   `pack/category.pack` names. This is the rung `[opheld1,_bones]` binds
 *   through.
 * - **npc** — config opcode 18, the same arrangement one namespace over. The
 *   comment that used to sit here said an osrs239 npc record "carries no
 *   category at all… absent, which is why `struct Mock230NpcInfo` has no field
 *   for it". That was wrong, and wrong in the direction that costs the most:
 *   the cache states a category on **9,149 of its 16,292 npc records**, the
 *   decoder has read it into `RSCache_Dat2ConfigNpc.category` all along, and
 *   `cachepack` round-trips it. It was unread, not absent. Names for the ids
 *   come from the crawl in `pack/category.pack` (triage §7.6b, §9 step 3b).
 * - **loc** — `Mock230LocDef.category` exists and is *not this*. It is a private
 *   two-valued door enum (`door_closed` / `door_opened`) with no entry in
 *   `pack/category.pack`; passing it would alias every door onto category ids 0
 *   and 1 and bind unrelated scripts to them. Wrong quietly, which is the worst
 *   way to be wrong, so it stays -1 until locs carry the real field — which is
 *   blocked on `dat2_config_loc.c` throwing opcode 61 away, an rscache
 *   write-path change.
 */
static int
interaction_category(const struct Mock230Interaction* interaction)
{
    switch( interaction->kind )
    {
    case MOCK230_INTERACT_OBJ:
    {
        const struct Mock230ObjInfo* info = mock230_objinfo(interaction->target_id);

        return info->category > 0 ? info->category : -1;
    }
    case MOCK230_INTERACT_NPC:
        return mock230_npc_category(interaction->target_id);
    default:
        return -1;
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
 * `mock230_world_process_interaction` because that function holds three of the
 * seven `enum Mock230Fallback` call sites, and Phase 3 has to be able to delete
 * them out of arms nothing else has rewritten.
 */
static int
interaction_ap_trigger(
    enum Mock230InteractionKind kind,
    int op,
    int use_on)
{
    switch( kind )
    {
    case MOCK230_INTERACT_NPC:
        return use_on ? SS_TRIGGER_APNPCU : SS_TRIGGER_APNPC1 + (op - 1);
    case MOCK230_INTERACT_LOC:
        return use_on ? SS_TRIGGER_APLOCU : SS_TRIGGER_APLOC1 + (op - 1);
    case MOCK230_INTERACT_OBJ:
        return use_on ? SS_TRIGGER_APOBJU : SS_TRIGGER_APOBJ1 + (op - 1);
    default:
        return -1;
    }
}

/* The engine's own behaviour for an op nothing was bound to. Defined below;
 * these are what used to run inline inside the packet handlers. */
static void
interaction_engine_npc(
    struct Mock230Server* srv,
    int slot,
    int op_num);
static void
interaction_engine_loc(
    struct Mock230Server* srv,
    int op_num,
    int loc_id,
    int tile_x,
    int tile_z,
    int level);
static void
interaction_engine_obj(
    struct Mock230Server* srv);

/* ------------------------------------------------------------------ */
/* The verbs the engine answers itself                                 */
/* ------------------------------------------------------------------ */

/*
 * The cache menu verbs this engine implements, named once.
 *
 * Each of these is read off the record's own op list rather than an id list,
 * for the reason `interaction_engine_loc` spells out: OldSchool has dozens of
 * bank booths and every one of them says "Bank" in the cache, so a list would
 * be a second copy kept by hand and wrong for whichever booth nobody added.
 *
 * They are gathered here because a *second* reader needs them —
 * `mock230_scripts_report_shadowed_ops`, which asks at load which of these
 * verbs content has bound a trigger over. Two copies of the list is two
 * chances for the report to say the opposite of what the runtime does, and a
 * report that disagrees with the behaviour it describes is worse than none.
 *
 * That these are string literals in C is the standing violation
 * PORTING_GUIDE §2.4 item 2 names, and it is not fixed here: the verb is the
 * cache's own word and the comparison is how the engine reads it. What is
 * fixed is that there is now one occurrence of each instead of seven.
 */
#define MOCK230_VERB_ATTACK "Attack"
#define MOCK230_VERB_WEAR "Wear"
#define MOCK230_VERB_WIELD "Wield"
#define MOCK230_VERB_DROP "Drop"
#define MOCK230_VERB_BANK "Bank"
#define MOCK230_VERB_USE_QUICKLY "Use-quickly"
#define MOCK230_VERB_CLIMB_UP "Climb-up"
#define MOCK230_VERB_CLIMB_DOWN "Climb-down"
#define MOCK230_VERB_CLIMB "Climb"

static const char* const k_engine_npc_verbs[] = { MOCK230_VERB_ATTACK };
static const char* const k_engine_held_verbs[] = { MOCK230_VERB_WEAR, MOCK230_VERB_WIELD,
                                                   MOCK230_VERB_DROP };
static const char* const k_engine_loc_verbs[] = { MOCK230_VERB_BANK, MOCK230_VERB_USE_QUICKLY,
                                                  MOCK230_VERB_CLIMB_UP, MOCK230_VERB_CLIMB_DOWN,
                                                  MOCK230_VERB_CLIMB };

static const char*
claimed_in(
    const char* verb,
    const char* const* claimed,
    size_t count)
{
    if( !verb )
        return NULL;
    for( size_t i = 0; i < count; i++ )
        if( strcmp(verb, claimed[i]) == 0 )
            return claimed[i];
    return NULL;
}

const char*
mock230_world_engine_claimed_verb(
    int trigger,
    int32_t subject)
{
    if( trigger >= SS_TRIGGER_OPNPC1 && trigger <= SS_TRIGGER_OPNPC5 )
    {
        const struct Mock230NpcInfo* info = mock230_npcinfo((int)subject);
        int op_num = trigger - SS_TRIGGER_OPNPC1 + 1;

        return info ? claimed_in(info->ops[op_num - 1], k_engine_npc_verbs,
                                 sizeof(k_engine_npc_verbs) / sizeof(k_engine_npc_verbs[0]))
                    : NULL;
    }
    if( trigger >= SS_TRIGGER_OPHELD1 && trigger <= SS_TRIGGER_OPHELD5 )
    {
        const struct Mock230ObjInfo* info = mock230_objinfo((int)subject);
        int op_num = trigger - SS_TRIGGER_OPHELD1 + 1;

        return info ? claimed_in(info->if_ops[op_num - 1], k_engine_held_verbs,
                                 sizeof(k_engine_held_verbs) / sizeof(k_engine_held_verbs[0]))
                    : NULL;
    }
    if( trigger >= SS_TRIGGER_OPLOC1 && trigger <= SS_TRIGGER_OPLOC5 )
    {
        int op_num = trigger - SS_TRIGGER_OPLOC1 + 1;

        return claimed_in(mock230_scene_loc_op((int)subject, op_num), k_engine_loc_verbs,
                          sizeof(k_engine_loc_verbs) / sizeof(k_engine_loc_verbs[0]));
    }
    /*
     * `[opobj<n>]` is deliberately absent. Its engine fallback picks the pile
     * up whatever the cache calls the op, so there is no verb to shadow —
     * `interaction_engine_obj` takes no op number at all.
     */
    return NULL;
}

/**
 * Resolve the pending interaction, if it is time.
 *
 * Called once from the player's phase, after movement, and once by the packet
 * handler that set it — so an interaction that is already in range costs no
 * tick, and one that is not costs exactly as many as the walk.
 */
void
mock230_world_process_interaction(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->active_player;
    struct Mock230Interaction* interaction = &player->interaction;
    int target_x;
    int target_z;
    int size_x;
    int size_z;
    int distance;
    int trigger;
    int category;

    if( interaction->kind == MOCK230_INTERACT_NONE )
        return;

    if( !interaction_target(srv, &target_x, &target_z, &size_x, &size_z) )
    {
        mock230_world_interaction_clear(srv);
        return;
    }

    /* An npc that moved takes its walk with it. Re-routing every tick is what
     * makes following work; without it the player walks to a memory. */
    if( interaction->kind == MOCK230_INTERACT_NPC &&
        (target_x != interaction->x || target_z != interaction->z) )
    {
        interaction->x = target_x;
        interaction->z = target_z;
        mock230_world_walk_beside(srv, target_x, target_z);
    }

    distance = distance_to_rect(player->x, player->z, target_x, target_z, size_x, size_z);

    /*
     * At range: content's chance to handle this without closing the distance.
     * Only content can — the engine has no ranged behaviour of its own — so a
     * miss here just falls through to the walk.
     */
    if( !interaction->ap_tried && distance <= MOCK230_AP_RANGE_DEFAULT )
    {
        interaction->ap_tried = 1;
        trigger = interaction_ap_trigger(interaction->kind, interaction->op, interaction->use_on);
        /* A miss here has no fallback and needs none: "nothing bound at range"
         * is the ordinary case for almost every interaction in the game, and
         * what it means is "keep walking". It still reports once per
         * interaction under MOCK230_VERBOSE — once, because `ap_tried` latches
         * above and this runs on one tick of the walk rather than all of them. */
        if( trigger >= 0 &&
            mock230_scripts_run_trigger(srv, trigger, interaction->target_id,
                                        interaction_category(interaction),
                                        interaction->npc_slot) != MOCK230_TRIGGER_NONE )
        {
            steps_clear(player);
            mock230_world_interaction_clear(srv);
            return;
        }
    }

    /* A ground obj has to be stood on; everything else is reached from beside. */
    if( distance > (interaction->kind == MOCK230_INTERACT_OBJ ? 0 : 1) )
        return; /* still walking */

    {
        int op_num = interaction->op;
        int slot = interaction->npc_slot;
        enum Mock230InteractionKind kind = interaction->kind;
        int target_id = interaction->target_id;
        int loc_x = interaction->x;
        int loc_z = interaction->z;
        int loc_level = interaction->level;
        int use_on = interaction->use_on;

        /* Read before the clear below, with everything else the dispatch needs:
         * the interaction it is derived from does not survive to the switch. */
        category = interaction_category(interaction);

        /*
         * Clear *before* running, not after. A script is allowed to start a new
         * interaction (`p_opnpc`), and clearing afterwards would throw away the
         * one it just asked for.
         */
        mock230_world_interaction_clear(srv);
        steps_clear(player);
        player->dest_x = -1;
        player->dest_z = -1;

        /*
         * A use-on takes its own arm and leaves before the switch below, rather
         * than growing a fourth case inside it.
         *
         * The reason is the miss: there is **no engine use-on behaviour** to fall
         * back to. `Player.defaultOp` answers an unbound `*u` with the message
         * and nothing else, and routing it into MOCK230_FALLBACK_OPLOC/OPNPC/
         * OPOBJ would hand "use a bucket on the door" to the door handler and
         * open it. The message is content's — `[proc,nothing_interesting_message]`
         * — reached through the same `mock230_say` the other four literals moved
         * behind.
         *
         * The subject is the **target**, never the used item. That is
         * `Player.getOpTrigger` reading `type.id`/`type.category` off the loc,
         * npc or obj the interaction is against; the item reaches content only
         * as `last_useitem`/`last_useslot`.
         */
        if( use_on )
        {
            int ap = interaction_ap_trigger(kind, op_num, 1);

            if( ap < 0 || mock230_scripts_run_trigger(srv, ap + 7, target_id, category, slot) ==
                              MOCK230_TRIGGER_NONE )
                mock230_say(srv, "nothing_interesting_message", NULL);
            return;
        }

        switch( kind )
        {
        case MOCK230_INTERACT_NPC:
            if( mock230_scripts_fallback(
                    srv, MOCK230_FALLBACK_OPNPC,
                    mock230_scripts_run_trigger(srv, SS_TRIGGER_OPNPC1 + (op_num - 1),
                                                target_id, category, slot)) )
                interaction_engine_npc(srv, slot, op_num);
            break;
        case MOCK230_INTERACT_LOC:
            if( mock230_scripts_fallback(
                    srv, MOCK230_FALLBACK_OPLOC,
                    mock230_scripts_run_trigger(srv, SS_TRIGGER_OPLOC1 + (op_num - 1),
                                                target_id, category, -1)) )
                interaction_engine_loc(srv, op_num, target_id, loc_x, loc_z, loc_level);
            break;
        case MOCK230_INTERACT_OBJ:
            if( mock230_scripts_fallback(
                    srv, MOCK230_FALLBACK_OPOBJ,
                    mock230_scripts_run_trigger(srv, SS_TRIGGER_OPOBJ1 + (op_num - 1),
                                                target_id, category, -1)) )
                interaction_engine_obj(srv);
            break;
        default:
            break;
        }
    }
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
static int
player_weight_grams(const struct Mock230Player* player)
{
    int total = 0;

    /* Stackable objs are weightless and a non-stackable stack weighs its count.
     * Reference: Player.ts:640-645 — `if (!type || type.stackable) continue;`
     * then `this.runweight += type.weight * item.count`. Ignoring both made a
     * thousand coins weigh a thousand times a coin, and a stack of anything
     * weigh one. */
    for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
    {
        const struct Mock230ObjInfo* info;

        if( player->inv[i].obj_id < 0 )
            continue;
        info = mock230_objinfo(player->inv[i].obj_id);
        if( info->stackable )
            continue;
        total += info->weight * player->inv[i].count;
    }
    /* Worn equipment is one per slot, and worn stackables (ammo) are the same
     * weightless case. */
    for( int i = 0; i < MOCK230_WORN_SLOTS; i++ )
    {
        const struct Mock230ObjInfo* info;

        if( player->worn[i].obj_id < 0 )
            continue;
        info = mock230_objinfo(player->worn[i].obj_id);
        if( info->stackable )
            continue;
        total += info->weight;
    }
    return total;
}

/*
 * One tick of energy, in OldSchool's own arithmetic (xrsps
 * MovementService.updateRunEnergy, which is the same formula):
 *
 *   drain per running TICK = 67 + 67 * min(64, weight_kg) / 64
 *   regen per idle/walking tick = agility / 6 + 8
 *
 * so an unencumbered player gets a hair over 149 running ticks from full and a
 * fully-laden one about half that, and standing still refills at roughly one
 * percent every eight ticks at level 1.
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
 *
 * ⚠️ THE TWO REFERENCES DISAGREE HERE, and this is LostCity's. Recorded because
 * this is a rev-230 server and LostCity is 2004-era behaviour:
 *
 *              drain per running tick            restore per idle tick
 *   LostCity   67 + 67*kg/64  (kg 0..64)         agility/6 + 8      <- implemented
 *   OpenRune   64 + min(g,6400)/10000            computes a value, then discards
 *              (RunEnergy.kt:41-42)              it and adds a flat 500 (:51-55)
 *
 * LostCity's drain is strongly weight-dependent (67..134, a 2x span); OpenRune's
 * is not (64..64.64), which makes carried weight almost free. The 67 + 67*w/64
 * form is the widely-attested OldSchool one, so it is the one kept. OpenRune's
 * restore is not usable as an authority at all — `recovery` is computed and
 * never read, so its effective rate is a flat 500/tick regardless of Agility.
 *
 * The restore rate is the half worth revisiting for a modern server: OldSchool
 * changed how Agility feeds it after 2004, and neither reference here shows the
 * modern curve. Do not "fix" the drain to OpenRune's without checking a third
 * source — that direction makes weight nearly meaningless.
 */
static void
run_energy_tick(
    struct Mock230Server* srv,
    int run_steps)
{
    struct Mock230Player* player = srv->active_player;

    /* A delayed player neither burns nor recovers. Reference: Player.ts:703,
     * `if (this.delayed) return;`. */
    if( player->delayed_until > srv->tick )
        return;

    if( run_steps >= 2 )
    {
        int weight_kg = player_weight_grams(player) / 1000;
        int drain;

        if( weight_kg < 0 )
            weight_kg = 0;
        if( weight_kg > 64 )
            weight_kg = 64;
        drain = 67 + (67 * weight_kg) / 64;

        player->run_energy -= drain;
        if( player->run_energy <= 0 )
        {
            player->run_energy = 0;
            player->run_toggle = 0;
            player->running = 0;
            mock230_world_set_varp(srv, mock230_world_varp("option_run"), 0);
        }
        return;
    }

    if( player->run_energy < MOCK230_RUN_ENERGY_MAX )
    {
        /* Base level, not boosted: an agility potion does not speed recovery.
         * Reference: Player.ts:707 uses `baseLevels[AGILITY]`. */
        int agility = player->stat_level[MOCK230_STAT_AGILITY];
        if( agility < 1 )
            agility = 1;
        player->run_energy += agility / 6 + 8;
        if( player->run_energy > MOCK230_RUN_ENERGY_MAX )
            player->run_energy = MOCK230_RUN_ENERGY_MAX;
    }
}

/* Put the orb's two numbers on the wire, but only when one of them moved: at
 * one packet a tick each they would be a third of everything the mock sends. */
static void
run_energy_flush(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->active_player;
    int percent = player->run_energy * 100 / MOCK230_RUN_ENERGY_MAX;
    /* Kilograms, not grams. UPDATE_RUNWEIGHT's value is read back by CS2's
     * RUNWEIGHT_VISIBLE, and the gameframe prints that with "kg" after it —
     * sending grams put "31892 kg" beside a player carrying 32. */
    int weight = player_weight_grams(player) / 1000;

    if( percent != player->run_energy_sent )
    {
        player->run_energy_sent = percent;
        mock230_send_run_energy(srv->active_player, percent);
    }
    if( weight != player->run_weight_sent )
    {
        player->run_weight_sent = weight;
        mock230_send_run_weight(srv->active_player, weight);
    }
}

/* Consume up to `max_tiles` queued steps, recording the direction of each so
 * PLAYER_INFO can spell them out. */
static void
advance_player(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->active_player;
    int max_tiles;

    /* Running is a request, not a state: the toggle says the player wants to,
     * the energy says whether they can. Deciding it here rather than at the
     * move packet is what makes energy run out mid-walk. */
    player->running = player->run_toggle && player->run_energy > 0;
    max_tiles = player->running ? 2 : 1;

    player->move_count = 0;
    for( int i = 0; i < max_tiles; i++ )
    {
        int dir;
        if( player->step_head >= player->step_count )
            break;
        dir = mock230_step_direction(
            player->steps[player->step_head].x - player->x,
            player->steps[player->step_head].z - player->z);
        if( dir < 0 )
        {
            /* Not an adjacent tile — the only way to get there is a place, so
             * drop the rest of the route rather than emit a bogus direction. */
            steps_clear(player);
            break;
        }
        player->x = player->steps[player->step_head].x;
        player->z = player->steps[player->step_head].z;
        player->step_head++;
        player->move_dirs[player->move_count++] = dir;
    }

    if( player->step_head >= player->step_count && player->step_count > 0 )
    {
        steps_clear(player);
        if( player->dest_x >= 0 )
        {
            player->clear_map_flag = 1;
            player->dest_x = -1;
            player->dest_z = -1;
        }
    }
}

/*
 * Re-centre the scene when a player nears its edge.
 *
 * The client holds a 104x104 scene based at (zone - 6) * 8. Once a player is
 * within 16 tiles of an edge the scene has to be rebuilt around them, and
 * because a rebuild throws away every entity the client was tracking, the
 * ground-obj set is dropped and the player re-placed absolutely on the next
 * tick.
 *
 * **The origin is the world's, so re-centring it re-centres it for everybody.**
 * Every scene-local coordinate on the wire is measured from it, so a player who
 * was not the reason for the move still has to be told, or their next
 * PLAYER_INFO places them 40 tiles from where they are. That makes the world's
 * one build area a genuine multiplayer limit and not just an untidiness: two
 * players more than ~70 tiles apart pull the origin back and forth and rebuild
 * each other's scene every tick. Zones (§6.1 step 3) are what fixes it; until
 * then, everyone rebuilds together and it is correct rather than efficient.
 */
static void
maybe_rebuild(struct Mock230Server* srv)
{
    int edge = MOCK230_SCENE_TILES - MOCK230_REBUILD_MARGIN;
    struct Mock230Player* mover = NULL;

    for( int i = 0; i < srv->player_count && !mover; i++ )
    {
        struct Mock230Player* player = &srv->players[i];
        int local_x;
        int local_z;

        if( !player->active )
            continue;
        local_x = player->x - mock230_scene_origin(srv->zone_x);
        local_z = player->z - mock230_scene_origin(srv->zone_z);
        if( local_x < MOCK230_REBUILD_MARGIN || local_x >= edge ||
            local_z < MOCK230_REBUILD_MARGIN || local_z >= edge )
            mover = player;
    }

    if( !mover )
        return;

    srv->zone_x = mover->x >> 3;
    srv->zone_z = mover->z >> 3;

    for( int i = 0; i < srv->player_count; i++ )
    {
        struct Mock230Player* player = &srv->players[i];

        if( !player->active )
            continue;
        player->rebuild_pending = 1;
        player->place_dirty = 1;
        /* REBUILD_NORMAL resets the client's scene to the cache's version, which
         * un-opens every door it had been told about and drops every obj. So the
         * client is treated as holding no zones at all and phase 10 re-sends
         * each one in full. Cleared here, where the rebuild is *decided*, rather
         * than where it is sent: phase 10 is where the zones go out, so clearing
         * at send time would be a tick late. */
        mock230_zone_player_reset(player);
    }

    /*
     * The server's collision window moves with the client's scene.
     *
     * `mock230_scene_build` calls `mock230_scene_free` first, so this throws the
     * loc array away and re-reads it from the cache — which used to mean the
     * *server* forgot its own doors on every rebuild too, despite the comment
     * that stood here claiming the changed list survived. It never did: the
     * changed flags were on the freed array. The durable record is the ZoneMap's
     * now, and re-applying it is what puts collision back where the clients
     * believe it is.
     */
    mock230_scene_build(mock230_world_cache_dir(), srv->zone_x, srv->zone_z);
    mock230_world_locs_reapply(srv);
    /* Tracked npcs and players deliberately survive: the client shifts every
     * kept entity by the base-tile delta when it rebuilds
     * (App_WorldRebuildShift), so their slots stay valid. Dropping and re-adding
     * them would instead re-spawn into slots the client still holds. */
}

/* ------------------------------------------------------------------ */
/* NPCs                                                                */
/* ------------------------------------------------------------------ */

static int
npc_spawn(
    struct Mock230Server* srv,
    int type,
    int x,
    int z,
    int level)
{
    const struct Mock230NpcDef* def;

    /* A wider id would not fail loudly — it would truncate to a different,
     * probably valid, npc. rev 230 states 14 bits, which covers the whole
     * OldSchool npc space; the guard exists for the day that changes. */
    if( type < 0 || type > MOCK230_NPC_TYPE_MAX )
    {
        fprintf(
            stderr,
            "mock230: npc type %d exceeds the %d-bit wire field; not spawned\n",
            type,
            MOCK230_NPC_TYPE_BITS);
        return -1;
    }

    /* Content first, engine defaults second. A spawn is allowed to name an npc
     * no config block describes — it gets 10 hitpoints and unarmed animations,
     * which is enough to be a target rather than a crash. */
    def = mock230_content_npc(type);
    if( !def )
        def = mock230_content_npc_default();

    for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
    {
        struct Mock230Npc* npc = &srv->npcs[slot];
        if( npc->active )
            continue;
        memset(npc, 0, sizeof(*npc));
        npc->active = 1;
        npc->type = type;
        npc->x = x;
        npc->z = z;
        npc->level = level;
        npc->spawn_x = x;
        npc->spawn_z = z;
        npc->spawn_level = level;
        npc->def = def;
        npc->wander_radius = def->nomove ? 0 : def->wanderrange;
        /*
         * Wander is the *default*, not the absence of a mode — see the field's
         * comment. An npc with no radius starts on `none`.
         *
         * A content `defaultmode=` outranks both, and patrol is why it exists:
         * Hans walks a fixed ring round the castle grounds, which is a route,
         * not a radius. Expressing him as a wanderer (which is what this tree
         * did) puts him somewhere random in a hundred tiles and makes the
         * greeter outside the castle the hardest npc in Lumbridge to find.
         */
        npc->mode = npc->wander_radius > 0 ? MOCK230_NPCMODE_WANDER : MOCK230_NPCMODE_NONE;
        if( def->defaultmode != MOCK230_NPCMODE_NONE )
            npc->mode = def->defaultmode;
        if( def->patrol_count > 0 )
            npc->mode = MOCK230_NPCMODE_PATROL;
        npc->patrol_index = 0;
        npc->patrol_pause = 0;
        mock230_world_npc_roam_stagger(srv, npc);
        npc->step_dir = -1;
        npc->face_entity = -1;
        /* Explicit, because the memset above makes it 0 and 0 is a *sequence
         * id*: a fresh npc would arrive holding an incumbent animation the
         * priority gate has to beat. */
        npc->anim_id = -1;

        npc->base_hitpoints = def->hitpoints > 0 ? def->hitpoints : 1;
        npc->hitpoints = npc->base_hitpoints;
        npc->max_hitpoints = npc->base_hitpoints;
        npc->combat_target = -1;
        npc->death_tick = -1;
        npc->respawn_tick = -1;
        /* Explicit, because the memset above makes it 0 and 0 is a *tick*:
         * every npc in the world would vanish on the first pass. */
        npc->despawn_tick = -1;
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

        /* The per-tick phases walk to this rather than to MOCK230_NPC_MAX,
         * which is a memory ceiling now and not the roster. */
        if( slot >= srv->npc_slot_max )
            srv->npc_slot_max = slot + 1;
        return slot;
    }

    fprintf(stderr, "mock230: no free npc slot for type %d at %d,%d\n", type, x, z);
    return -1;
}

/*
 * `npc_add`'s entry point. Same spawn, but content gets the slot back so the
 * script can act on the npc it just created — the reference leaves it active,
 * and without the slot there would be no way to say which of six identical
 * goblins was meant.
 */
int
mock230_world_npc_spawn(
    struct Mock230Server* srv,
    int type,
    int x,
    int z,
    int level)
{
    return npc_spawn(srv, type, x, z, level);
}

/** One step in `dir`, if collision allows it. Returns 1 when the npc moved. */
static int
npc_take_step(
    struct Mock230Npc* npc,
    int dir)
{
    int dx;
    int dz;

    if( dir < 0 )
        return 0;
    if( !mock230_scene_can_step(npc->level, npc->x, npc->z, dir) )
        return 0;
    mock230_step_delta(dir, &dx, &dz);
    npc->x += dx;
    npc->z += dz;
    npc->step_dir = dir;
    return 1;
}

/*
 * One step of an npc's walk toward a tile — the reference's `pathToTarget()`
 * followed by `updateMovement()`, at one tile a tick.
 *
 * The straight-line step is tried first because it is what a route through open
 * ground produces anyway, and it costs nothing. **The router behind it is the
 * point.** What was here before was the greedy step alone, with a comment saying
 * a blocked step is "simply not taken — the npc will try again next tick", and
 * that reasoning only holds while the obstacle is something the player is also
 * walking around. It is not: the player walks away *through* the gap and the npc
 * spends the rest of the fight walking into the same wall, one tick at a time,
 * still in combat and still facing them. A goblin standing on the Lumbridge
 * castle path could not follow the player one tile east, because the tile east
 * of it happened to carry a wall flag.
 *
 * The reference has no such hole: an npc closing on a target sets a destination
 * and runs the same route-finder a player's click does (`Npc.pathToTarget` ->
 * `PathingEntity.updateMovement`). This is that, sharing `mock230_scene_route`
 * with the player's walk so a route the npc takes and a route the player would
 * have taken cannot disagree.
 *
 * Routing every tick rather than keeping waypoints is deliberate: the
 * destination is a moving player, so a stored path is stale the moment it is
 * computed. The flood only runs when the straight step is refused, which is the
 * case a stored path would not have helped with anyway.
 *
 * Returns 1 when the npc moved this tick.
 */
int
mock230_world_npc_walk_to(
    struct Mock230Npc* npc,
    int target_x,
    int target_z)
{
    const struct Mock230NpcDef* def = npc->def ? npc->def : mock230_content_npc_default();
    int path_x[MOCK230_STEP_MAX];
    int path_z[MOCK230_STEP_MAX];
    int steps;

    if( npc->x == target_x && npc->z == target_z )
        return 0;
    /*
     * `moverestrict=nomove`, checked in the mover rather than in each caller —
     * which is the reference's arrangement too (`MoveRestrict` is read by
     * `PathingEntity.updateMovement`, not by the modes).
     *
     * Roaming already respected it by way of a zero wander radius, so it never
     * needed saying while this was the only way an npc moved. Chasing does not
     * go near the radius: without this, provoking Bob would walk him out from
     * behind his counter and across Lumbridge.
     */
    if( def->nomove )
        return 0;

    if( npc_take_step(npc, mock230_step_direction(sign_of(target_x - npc->x),
                                                  sign_of(target_z - npc->z))) )
        return 1;

    steps = mock230_scene_route(npc->level, npc->x, npc->z, target_x, target_z, path_x, path_z,
                                MOCK230_STEP_MAX);
    if( steps <= 0 )
        return 0;
    return npc_take_step(npc, mock230_step_direction(path_x[0] - npc->x, path_z[0] - npc->z));
}

/*
 * One greedy step away from a tile — `playerescape`, and only that.
 *
 * Retreating has no destination to route to: the reference walks one tile
 * directly away and, when that is blocked, gives up the mode entirely
 * (`Npc.playerEscapeMode` -> `resetDefaults`). So this stays greedy where
 * `mock230_world_npc_walk_to` does not.
 */
static void
npc_step_away(
    struct Mock230Npc* npc,
    int target_x,
    int target_z)
{
    npc_take_step(npc, mock230_step_direction(-sign_of(target_x - npc->x),
                                              -sign_of(target_z - npc->z)));
}

/** Chebyshev tiles between an npc and the player. */
static int
npc_player_range(
    const struct Mock230Npc* npc,
    const struct Mock230Player* player)
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
 * One step of a walk to the tile *beside* the player, never onto it.
 *
 * The reference's `pathToTarget()` does the same thing through
 * `naiveDestination`: a target with a footprint is approached, not stood on. An
 * npc that routed to the player's own tile would spend every tick trying to
 * step into it and, since nothing there is walkable to it, stand still.
 */
static int
npc_walk_to_player(
    struct Mock230Npc* npc,
    const struct Mock230Player* player)
{
    int dest_x;
    int dest_z;

    mock230_world_beside_tile(npc->level, npc->x, npc->z, player->x, player->z, &dest_x, &dest_z);
    return mock230_world_npc_walk_to(npc, dest_x, dest_z);
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
static int
npc_run_mode(
    struct Mock230Server* srv,
    struct Mock230Npc* npc,
    int slot)
{
    struct Mock230Player* player = srv->active_player;
    int range;

    if( !player || npc->mode == MOCK230_NPCMODE_NONE || npc->mode == MOCK230_NPCMODE_NULL )
        return 0;
    if( npc->mode == MOCK230_NPCMODE_WANDER )
        return 0; /* The roam below is what wander means. */

    range = npc_player_range(npc, player);

    /* Every player-facing mode faces the player; only some of them move. */
    if( npc->mode >= MOCK230_NPCMODE_PLAYERESCAPE )
    {
        npc->face_entity = MOCK230_FACE_LOCAL_PLAYER;
        npc->masks |= MOCK230_NMASK_FACE_ENTITY;
    }

    switch( npc->mode )
    {
    case MOCK230_NPCMODE_PLAYERESCAPE:
        if( range < 8 )
            npc_step_away(npc, player->x, player->z);
        return 1;

    case MOCK230_NPCMODE_PLAYERFOLLOW:
    case MOCK230_NPCMODE_PLAYERFACECLOSE:
        if( range > 1 )
            npc_walk_to_player(npc, player);
        return 1;

    case MOCK230_NPCMODE_PLAYERFACE:
        /* Face only. The reference's `playerface` does not move the npc, which
         * is the whole difference from `playerfaceclose`. */
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
    if( npc->mode >= MOCK230_NPCMODE_OPPLAYER1 && npc->mode <= MOCK230_NPCMODE_OPPLAYER5 )
    {
        int op = npc->mode - MOCK230_NPCMODE_OPPLAYER1;

        if( range > 1 )
        {
            npc_walk_to_player(npc, player);
            return 1;
        }
        /* Cleared *before* the trigger runs, so a script that sets a new mode
         * keeps it — the same ordering the player's interaction resolver uses
         * and for the same reason. */
        npc->mode = MOCK230_NPCMODE_NONE;
        mock230_scripts_run_trigger(srv, SS_TRIGGER_AI_OPPLAYER1 + op, npc->type, -1, slot);
        return 1;
    }
    if( npc->mode >= MOCK230_NPCMODE_APPLAYER1 && npc->mode <= MOCK230_NPCMODE_APPLAYER5 )
    {
        int op = npc->mode - MOCK230_NPCMODE_APPLAYER1;

        if( range > MOCK230_AP_RANGE_DEFAULT )
        {
            npc_walk_to_player(npc, player);
            return 1;
        }
        npc->mode = MOCK230_NPCMODE_NONE; /* see above */
        mock230_scripts_run_trigger(srv, SS_TRIGGER_AI_APPLAYER1 + op, npc->type, -1, slot);
        return 1;
    }

    /*
     * Patrol: walk the route, pause where the route says to.
     *
     * The waypoints are content's (`patrol1..patrolN` on the `.npc` block) and
     * the route is a *ring* — reaching the last one goes back to the first, so
     * Hans keeps circling rather than stopping at the end of his round. Routing
     * is `mock230_world_npc_walk_to`, the same flood the player's click uses, so
     * a patrol that has to go round the castle wall does.
     *
     * A waypoint on another level is walked to as though it were on this one and
     * then arrived at: this server has no stairs for an npc to take. It is not a
     * case the Lumbridge routes reach, and doing it silently rather than
     * refusing keeps a route with one bad level from stalling the whole ring.
     */
    if( npc->mode == MOCK230_NPCMODE_PATROL )
    {
        const struct Mock230NpcDef* def = npc->def;

        if( !def || def->patrol_count <= 0 )
        {
            npc->mode = MOCK230_NPCMODE_NONE;
            return 0;
        }
        if( npc->patrol_index < 0 || npc->patrol_index >= def->patrol_count )
            npc->patrol_index = 0;

        if( npc->patrol_pause > 0 )
        {
            npc->patrol_pause--;
            return 1;
        }
        if( npc->x == def->patrol[npc->patrol_index].x &&
            npc->z == def->patrol[npc->patrol_index].z )
        {
            /* Arrived. Take this waypoint's pause, then aim at the next one —
             * in that order, or the pause would be charged to the waypoint the
             * npc is walking *away* from. */
            npc->patrol_pause = def->patrol[npc->patrol_index].pause;
            npc->patrol_index = (npc->patrol_index + 1) % def->patrol_count;
            return 1;
        }
        mock230_world_npc_walk_to(npc, def->patrol[npc->patrol_index].x,
                                  def->patrol[npc->patrol_index].z);
        return 1;
    }

    /* Anything else — the loc/obj/npc-targeted modes, which carry a
     * target this mode field has nowhere to put. Reported once per npc type so
     * a tree using one is not silently ignored. */
    {
        static unsigned char warned[1 << MOCK230_NPC_TYPE_BITS];

        if( npc->type >= 0 && npc->type < (int)(sizeof(warned) / sizeof(warned[0])) &&
            !warned[npc->type] )
        {
            warned[npc->type] = 1;
            fprintf(stderr, "mock230: npc mode %d is not implemented (npc %d); using none\n",
                    npc->mode, npc->type);
        }
    }
    npc->mode = MOCK230_NPCMODE_NONE;
    return 0;
}

/* One tile of idle roaming, on the xrsps/RSMod timer: an idle npc re-rolls a
 * roam every 15-30 ticks and stays inside its wander radius. */
static void
advance_npcs(struct Mock230Server* srv)
{
    for( int slot = 0; slot < srv->npc_slot_max; slot++ )
    {
        struct Mock230Npc* npc = &srv->npcs[slot];

        /*
         * An `npc_add` with a duration expires here, before anything else in
         * the phase looks at it. Clearing `active` is the ordinary NPC_INFO
         * remove path — the same one a death uses — so the client is told the
         * way it is told about everything else.
         */
        if( npc->active && npc->despawn_tick >= 0 && srv->tick >= npc->despawn_tick )
        {
            npc->active = 0;
            continue;
        }

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
                mock230_scripts_run_trigger(srv, SS_TRIGGER_AI_TIMER, npc->type, -1, slot);
            }
        }
        /*
         * The queue drain is gated on the npc not being delayed — the reference
         * only decrements while `!this.delayed` — and the comparison is against
         * the value *after* the decrement, so an npc's delay 0 and delay 1 both
         * fire on the next npc phase. A player's queue compares the value before
         * its decrement; the two conventions really do differ by one, which is
         * why it is written out here rather than shared.
         */
        if( npc->active && srv->tick >= npc->delayed_until )
        {
            for( int i = 0; i < MOCK230_NPC_QUEUE_MAX; i++ )
            {
                if( !npc->queue[i].active )
                    continue;
                npc->queue[i].delay--;
                if( npc->queue[i].delay > 0 )
                    continue;
                npc->queue[i].active = 0;
                mock230_scripts_run_trigger(srv, SS_TRIGGER_AI_QUEUE1 + (npc->queue[i].queue - 1),
                                            npc->type, -1, slot);
            }
        }
        int step_x;
        int step_z;
        int dir;

        if( !npc->active )
            continue;
        /* Combat and death own the npc's movement. Roaming used to clear
         * step_dir here, which also wiped the step the combat mover had just
         * produced — phase 11 does that clear, once, at the right time. */
        if( npc->combat_target >= 0 || npc->death_tick >= 0 )
            continue;
        if( npc_run_mode(srv, npc, slot) )
            continue;
        if( npc->mode != MOCK230_NPCMODE_WANDER )
            continue;
        if( npc->wander_radius <= 0 )
            continue;

        /*
         * Outside its radius, wandering means *going home*.
         *
         * Until an npc could chase it could never be out here, and the roll
         * below silently made that permanent: every candidate tile outside the
         * radius is rejected, so an npc standing more than one tile beyond it
         * has no legal roam at all and freezes where the chase ended. The
         * reference has the same asymmetry and answers it the same way —
         * `Npc.wander()` queues a *waypoint* near the spawn tile and the router
         * walks it back, rather than rolling one adjacent tile and hoping.
         *
         * Every tick rather than on the roam timer: the walk home is a path
         * being followed, not a fresh decision each time.
         */
        if( npc->x - npc->spawn_x > npc->wander_radius ||
            npc->spawn_x - npc->x > npc->wander_radius ||
            npc->z - npc->spawn_z > npc->wander_radius ||
            npc->spawn_z - npc->z > npc->wander_radius )
        {
            mock230_world_npc_walk_to(npc, npc->spawn_x, npc->spawn_z);
            continue;
        }

        if( srv->tick < npc->next_roam_tick )
            continue;

        npc->next_roam_tick = srv->tick + random_range(srv, 15, 30);
        step_x = npc->x + random_range(srv, -1, 1);
        step_z = npc->z + random_range(srv, -1, 1);
        if( step_x - npc->spawn_x > npc->wander_radius ||
            npc->spawn_x - step_x > npc->wander_radius ||
            step_z - npc->spawn_z > npc->wander_radius ||
            npc->spawn_z - step_z > npc->wander_radius )
            continue;

        dir = mock230_step_direction(step_x - npc->x, step_z - npc->z);
        if( dir < 0 )
            continue;
        /* Roaming used to walk npcs through walls, which is more visible than
         * the player doing it: a goblin wandering out of a fenced pen and
         * across a river reads as a broken server long before anyone checks
         * the pathfinding. */
        if( !mock230_scene_can_step(npc->level, npc->x, npc->z, dir) )
            continue;
        npc->x = step_x;
        npc->z = step_z;
        npc->step_dir = dir;
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

int
mock230_world_obj_add(
    struct Mock230Server* srv,
    int obj_id,
    int count,
    int x,
    int z,
    int level,
    int duration)
{
    if( obj_id < 0 || count <= 0 )
        return -1;

    /* Stack onto an existing pile of the same obj. A stackable obj has to
     * merge — three separate coin piles on one tile is not a thing the client
     * can draw — and merging a non-stackable one is wrong, so the count is
     * only combined when the cache says it stacks. */
    if( mock230_objinfo(obj_id)->stackable )
    {
        for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
        {
            struct Mock230GroundObj* obj = &srv->ground[i];

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
                mock230_zone_obj_counted(srv, i, old_count, obj->count);
            }
            return i;
        }
    }

    for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
    {
        struct Mock230GroundObj* obj = &srv->ground[i];

        if( obj->active )
            continue;
        /* Before the memset, while the slot is still inactive and still carries
         * the zone it was last filed under: a slot freed and reused inside one
         * tick would otherwise stay in the old zone's list forever. */
        mock230_zone_obj_refile(srv, i);
        memset(obj, 0, sizeof(*obj));
        obj->active = 1;
        obj->obj_id = obj_id;
        obj->count = count;
        obj->x = x;
        obj->z = z;
        obj->level = level;
        obj->despawn_tick = duration > 0 ? srv->tick + duration : -1;
        obj->respawn_tick = -1;
        obj->is_spawn = duration < 0;
        mock230_zone_obj_refile(srv, i);
        mock230_zone_obj_added(srv, i);
        return i;
    }
    return -1;
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
static void
ground_clear(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230GroundObj* obj = &srv->ground[slot];

    if( !obj->active )
        return;
    /* Queue the event while the obj is still filed in its zone — the event is
     * addressed to the zone the obj is *in*, and unfiling first would address
     * it to nowhere. */
    mock230_zone_obj_removed(srv, slot);
    obj->active = 0;
    mock230_zone_obj_refile(srv, slot);
}

/*
 * `flush_ground` was here.
 *
 * It ran per player, over all 256 ground slots, every tick, comparing each obj
 * against `mock230_scene_contains` and a per-player `ground_sent[]` bitmap.
 * Everything it did is now a property of zones: whether a client holds a zone
 * is one set membership test, the objs in that zone are a list, and a client
 * that has just been handed one is caught up by `write_state` in
 * mock230_zone.c. See mock230_zone.h.
 */

/** The world half: expire drops and bring taken spawns back. Once a tick, for
 *  everybody, before phase 10 flushes the zones. */
static void
ground_tick(struct Mock230Server* srv)
{
    for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
    {
        struct Mock230GroundObj* obj = &srv->ground[i];

        if( !obj->active )
        {
            /* A taken spawn comes back where it was. */
            if( obj->respawn_tick >= 0 && srv->tick >= obj->respawn_tick )
            {
                obj->active = 1;
                obj->respawn_tick = -1;
                mock230_zone_obj_refile(srv, i);
                mock230_zone_obj_added(srv, i);
            }
            continue;
        }
        if( obj->despawn_tick >= 0 && srv->tick >= obj->despawn_tick )
            ground_clear(srv, i);
    }
}

/** Remove a ground obj, telling every client that has it and arming its respawn
 *  if it was a map spawn. */
static void
ground_take(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230GroundObj* obj = &srv->ground[slot];

    ground_clear(srv, slot);
    obj->respawn_tick = obj->is_spawn ? srv->tick + mock230_ids()->lootdrop_duration : -1;
}

/* ------------------------------------------------------------------ */
/* Client packet handlers                                              */
/* ------------------------------------------------------------------ */

/*
 * MOVE_GAMECLICK / MOVE_OPCLICK / MOVE_MINIMAPCLICK.
 *
 * Wire form (net_out.c out_move): p1 ctrlHeld, p2 absolute start x, p2
 * absolute start z, then up to 24 signed byte pairs relative to that start.
 * The waypoints run from the tile nearest the player to the destination, so
 * the last one is where the click landed. The minimap variant appends a
 * 14-byte anti-cheat trailer, which must be subtracted before counting
 * waypoints — reading it as coordinate pairs would append seven junk tiles to
 * every minimap walk.
 */
static void
handle_move(
    struct Mock230Server* srv,
    const uint8_t* payload,
    int len,
    int trailer_len)
{
    struct Mock230Player* player = srv->active_player;
    struct RSAreaBuf buf;
    int ctrl;
    int start_x;
    int start_z;
    int waypoints;

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
        mock230_world_set_varp(srv, mock230_world_varp("option_run"), 1);
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
    mock230_world_clear_pending_action(srv);
    steps_clear(player);
    steps_walk_to(player, start_x, start_z);

    waypoints = (len - 5 - trailer_len) / 2;
    if( waypoints < 0 )
        waypoints = 0;
    for( int i = 0; i < waypoints; i++ )
    {
        int dx = rsab_g1s(&buf);
        int dz = rsab_g1s(&buf);
        if( !rsab_ok(&buf) )
            break;
        steps_walk_to(player, start_x + dx, start_z + dz);
    }

    if( player->step_count > 0 )
    {
        player->dest_x = player->steps[player->step_count - 1].x;
        player->dest_z = player->steps[player->step_count - 1].z;
    }
    if( srv->verbose )
        fprintf(
            stderr,
            "mock230: <- MOVE ctrl=%d start=%d,%d waypoints=%d steps=%d dest=%d,%d\n",
            ctrl,
            start_x,
            start_z,
            waypoints,
            player->step_count,
            player->dest_x,
            player->dest_z);
}

/** Is this component one of the bank's two item grids? Those are the only
 *  components whose `slot` does not index the backpack. */
static int
bank_component(int component)
{
    const struct Mock230Ids* ids = mock230_ids();

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
 * goes through mock230_equipment_worn_slot. */
static void
handle_opheld(
    struct Mock230Server* srv,
    int op_num,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = srv->active_player;
    struct RSAreaBuf buf;
    int obj_id;
    int slot;
    int component;
    const struct Mock230ObjInfo* info;
    const char* verb;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    obj_id = rsab_g2(&buf);
    slot = rsab_g2(&buf);
    component = rsab_g4(&buf);
    if( !rsab_ok(&buf) )
        return;

    info = mock230_objinfo(obj_id);
    verb = (op_num >= 1 && op_num <= 5) ? info->if_ops[op_num - 1] : NULL;
    if( srv->verbose )
        fprintf(
            stderr,
            "mock230: <- OPHELD%d obj=%d (%s) slot=%d com=%d|%d verb=%s\n",
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
     * declared engine fallback (MOCK230_FALLBACK_INV_BUTTON) rather than the
     * unconditional else it used to be.
     */
    if( bank_component(component) )
    {
        player->last_slot = slot;
        player->last_com = component;
        if( mock230_scripts_fallback(
                srv, MOCK230_FALLBACK_INV_BUTTON,
                mock230_scripts_run_trigger(srv, SS_TRIGGER_INV_BUTTON1 + (op_num - 1),
                                            component, -1, -1)) )
            mock230_bank_handle_button(srv, component, slot, obj_id, op_num);
        return;
    }

    /* The equipment tab's own components send their ops through the same
     * packet, so a click there means "take it off" rather than "put it on".
     * Which slot came off is in the component, not in `slot`. */
    {
        int worn = mock230_equipment_worn_slot(component);
        if( worn >= 0 )
        {
            unequip_slot(srv, worn);
            return;
        }
    }

    if( slot < 0 || slot >= MOCK230_INV_SLOTS || player->inv[slot].obj_id != obj_id )
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
    /* Everything below is MOCK230_FALLBACK_OPHELD — wear/wield and drop, which
     * the reference states as `[opheld2,_] ~equip(last_slot)` and
     * `[opheld5,_] ~dropslot(last_slot)` in `player/scripts/equip.rs2` and
     * `player/scripts/drop.rs2`. It does not run when a bound script aborted or
     * when there is no pack to have bound one.
     *
     * It is *not* here "because equipment is C" — that was the old reason and
     * it was wrong: the equipment screen is content and `mock230_equipment.c`
     * is a 134-line component -> worn-slot map with no rule in it. It is here
     * because eight opcodes a script would need are declared and unimplemented;
     * `k_engine_fallbacks[MOCK230_FALLBACK_OPHELD]` names all eight and
     * `mock230_scripts_stale_blockers` fails when one of them lands. */
    if( !mock230_scripts_fallback(
            srv, MOCK230_FALLBACK_OPHELD,
            (op_num >= 1 && op_num <= 5)
                ? mock230_scripts_run_trigger(srv, SS_TRIGGER_OPHELD1 + (op_num - 1), obj_id,
                                              info->category > 0 ? info->category : -1, -1)
                : MOCK230_TRIGGER_NONE) )
        return;

    if( verb && (strcmp(verb, MOCK230_VERB_WEAR) == 0 || strcmp(verb, MOCK230_VERB_WIELD) == 0) )
    {
        equip_from_slot(srv, slot);
        return;
    }
    if( verb && strcmp(verb, MOCK230_VERB_DROP) == 0 )
    {
        /* Dropping puts the obj on the floor rather than deleting it, which is
         * the whole difference between an inventory and a bin. It expires like
         * any other drop, so a session cannot litter Lumbridge indefinitely. */
        mock230_world_obj_add(srv, obj_id, player->inv[slot].count, player->x, player->z,
                              player->level, mock230_ids()->lootdrop_duration);
        inv_set(player, slot, -1, 0);
        mock230_say(srv, "drop_message", info->name);
        return;
    }
    /*
     * OPHELD5 with no verb behind it is the client's synthesised Drop row.
     *
     * A rev-230 obj record does not carry a "Drop" op — bones list only
     * "Bury" — because the client adds the row itself, always at index 5
     * (rs_minimenu_build.c add_inv_slot_rows). Waiting for a `verb` that says
     * "Drop" means waiting forever.
     */
    if( !verb && op_num == 5 )
    {
        mock230_world_obj_add(srv, obj_id, player->inv[slot].count, player->x, player->z,
                              player->level, mock230_ids()->lootdrop_duration);
        inv_set(player, slot, -1, 0);
        mock230_say(srv, "drop_message", info->name);
        return;
    }

    /* No verb for this index, but the item is wearable: the cache's op list is
     * sparse for a lot of items, and refusing to equip a helmet because its
     * "Wear" landed on a different index would be worse than acting on it. */
    if( info->wearpos >= 0 )
    {
        equip_from_slot(srv, slot);
        return;
    }
    mock230_say(srv, "nothing_interesting_message", NULL);
}

/* INV_BUTTOND: p4 componentId, p2 fromSlot, p2 toSlot, p1 mode. The client has
 * already applied the swap locally, so this confirms it; a server that
 * disagreed would send a partial update putting both slots back.
 *
 * A real rev-230 IfButtonD names both ends (selected component + sub, target
 * component + sub) so an item can be dragged between two interfaces. The mock
 * only ever moves within the backpack, so it carries one component and refuses
 * anything else — a drag onto the worn tab is not an equip here. */
static void
handle_inv_buttond(
    struct Mock230Server* srv,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = srv->active_player;
    struct RSAreaBuf buf;
    int component;
    int from_slot;
    int to_slot;
    struct Mock230Item swap;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    component = rsab_g4(&buf);
    from_slot = rsab_g2(&buf);
    to_slot = rsab_g2(&buf);
    (void)rsab_g1(&buf); /* mode */
    if( !rsab_ok(&buf) )
        return;

    if( srv->verbose )
        fprintf(
            stderr,
            "mock230: <- INV_BUTTOND com=%d|%d %d -> %d\n",
            (component >> 16) & 0xffff,
            component & 0xffff,
            from_slot,
            to_slot);

    if( component != mock230_ids()->com_inventory_items )
        return;
    if( from_slot < 0 || from_slot >= MOCK230_INV_SLOTS || to_slot < 0 ||
        to_slot >= MOCK230_INV_SLOTS || from_slot == to_slot )
        return;

    swap = player->inv[from_slot];
    inv_set(player, from_slot, player->inv[to_slot].obj_id, player->inv[to_slot].count);
    inv_set(player, to_slot, swap.obj_id, swap.count);
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
    struct Mock230Server* srv,
    int slot,
    int op_num)
{
    struct Mock230Npc* npc;
    const struct Mock230NpcInfo* info;
    const char* verb;

    if( slot < 0 || slot >= MOCK230_NPC_MAX || !srv->npcs[slot].active )
        return;
    npc = &srv->npcs[slot];

    info = mock230_npcinfo(npc->type);
    verb = (op_num >= 1 && op_num <= 5) ? info->ops[op_num - 1] : NULL;
    if( verb && strcmp(verb, MOCK230_VERB_ATTACK) == 0 )
    {
        mock230_combat_engage(srv, slot);
        return;
    }

    /*
     * Everything else is a greeting, and the words are content's:
     * `[proc,npc_default_chat]` (player/messages.rs2), run with this npc active
     * so it can use `npc_say` — which is also what makes the line *visible*.
     * The mask this used to set alone renders nowhere in this client.
     */
    npc->face_entity = MOCK230_FACE_LOCAL_PLAYER; /* the local player */
    npc->masks |= MOCK230_NMASK_FACE_ENTITY;
    mock230_scripts_run_proc_on_npc(srv, "[proc,npc_default_chat]", slot);
}

/*
 * OPNPC<n>: p2 npc slot.
 *
 * Latches the interaction and starts the walk; the acting happens in
 * mock230_world_process_interaction, either on this tick (already in range) or
 * on whichever tick the player arrives.
 */
static void
handle_opnpc(
    struct Mock230Server* srv,
    int op_num,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int slot;
    struct Mock230Npc* npc;
    const struct Mock230NpcInfo* info;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    slot = rsab_g2(&buf);
    if( !rsab_ok(&buf) || slot < 0 || slot >= MOCK230_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active )
        return;

    if( srv->verbose )
        fprintf(stderr, "mock230: <- OPNPC%d slot=%d type=%d\n", op_num, slot, npc->type);

    /* A new interaction ends the old one — including the facing, and including
     * a dialogue still on screen from the last one. Combat is re-established by
     * the engine handler if this op is "Attack".
     *
     * Clicking the *same* npc again is the case that makes this necessary
     * rather than tidy: the [opnpc] about to run wants to park a fresh script,
     * and it cannot while the previous conversation still holds the slot. */
    mock230_world_clear_pending_action(srv);

    info = mock230_npcinfo(npc->type);
    mock230_world_interaction_set(srv, MOCK230_INTERACT_NPC, op_num, slot, npc->type, npc->x,
                                  npc->z, npc->level, info->size, info->size);
    mock230_world_walk_beside(srv, npc->x, npc->z);

    /* Idle roaming resumes only after the response has had time to show. */
    npc->next_roam_tick = srv->tick + 8;

    mock230_world_process_interaction(srv);
}

/*
 * Move the player a level, which is all a staircase or a ladder does.
 *
 * The tile does not change, only the level. That is right for every ladder and
 * every spiral staircase in Lumbridge, and wrong for the handful of stairs
 * elsewhere that land you somewhere else — those need per-loc destinations,
 * which is content this tree does not have yet.
 */
static void
climb(
    struct Mock230Server* srv,
    int delta)
{
    struct Mock230Player* player = srv->active_player;
    int level = player->level + delta;

    if( level < 0 || level > 3 )
    {
        mock230_say(srv, "blocked_message", NULL);
        return;
    }
    steps_clear(player);
    player->level = level;
    player->place_dirty = 1;
    player->dest_x = -1;
    player->dest_z = -1;
    /* Every npc and ground obj *this* client holds is on the old level, and it
     * has no way to know they left. A rebuild is heavy-handed but it is exactly
     * what the reference does when a player changes plane — and it is this
     * player's rebuild, not the world's: nobody else moved. */
    player->rebuild_pending = 1;
    memset(player->npc_tracked, 0, sizeof(player->npc_tracked));
    player->tracked_count = 0;
    /* The zones on the new level are different zones — a zone key carries the
     * level — so this is not only "forget what you were told", it is what makes
     * the next flush compute a new active window. */
    mock230_zone_player_reset(player);
    /* The players on the old level go the same way. `player_in_view` would drop
     * them next tick anyway, but a rebuild has already thrown the client's list
     * away, so a remove op would name a slot it no longer holds. */
    memset(player->player_tracked, 0, sizeof(player->player_tracked));
    player->tracked_player_count = 0;
}

/*
 * OPOBJ<n>: p2 x, p2 z, p2 objId — picking something up off the floor.
 *
 * The walk and the take are one action here rather than a queued interaction:
 * the mock has no interaction model, so the player arrives instantly in game
 * terms and the client sees the walk happen underneath. Getting that wrong in
 * the other direction (take first, walk after) would let a player vacuum up
 * Lumbridge from the castle roof.
 */
/* Taking the pile the player is now standing on. */
static void
interaction_engine_obj(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->active_player;

    for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
    {
        struct Mock230GroundObj* obj = &srv->ground[i];
        int free_slot;

        if( !obj->active || obj->x != player->x || obj->z != player->z ||
            obj->level != player->level )
            continue;

        /* A stackable obj joins the pile it already has rather than taking a
         * slot of its own. Without this a full backpack refuses a single coin
         * while holding 15,000 of them two slots over. */
        free_slot = inv_stack_slot(player, obj->obj_id);
        if( free_slot >= 0 )
        {
            inv_set(player, free_slot, obj->obj_id, player->inv[free_slot].count + obj->count);
        }
        else
        {
            free_slot = inv_first_free(player);
            if( free_slot < 0 )
            {
                mock230_say(srv, "inv_no_space_message", NULL);
                return;
            }
            inv_set(player, free_slot, obj->obj_id, obj->count);
        }
        mock230_say(srv, "pickup_message", mock230_objinfo(obj->obj_id)->name);
        ground_take(srv, i);
        return;
    }
    mock230_say(srv, "cant_reach_message", NULL);
}

static void
handle_opobj(
    struct Mock230Server* srv,
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

    mock230_world_clear_pending_action(srv);
    mock230_world_interaction_set(srv, MOCK230_INTERACT_OBJ, op_num, -1, obj_id, tile_x,
                                  tile_z, srv->active_player->level, 1, 1);
    /* Onto the tile, not beside it: a pile is picked up from on top. */
    steps_clear(srv->active_player);
    srv->active_player->dest_x = tile_x;
    srv->active_player->dest_z = tile_z;
    steps_walk_to(srv->active_player, tile_x, tile_z);

    mock230_world_process_interaction(srv);
}

/*
 * OPLOC<n>: p2 x, p2 z, p2 locId — doors, gates, stairs and ladders.
 *
 * Two generic rules cover all of them, and neither needs a per-loc table:
 *
 * - A loc whose content block says `category=door_closed` (or `door_opened`)
 *   swaps for its `param=next_loc_stage` partner. The pairing is data, from
 *   OpenRune's curated door table plus name-derived pairs; see
 *   content/scripts/doors/configs/doors.loc.
 * - A loc whose *cache* menu text says "Climb-up" or "Climb-down" moves the
 *   player a level. The direction is already in the cache, so content does not
 *   restate it.
 */
/*
 * What a loc op does when no content claimed it.
 *
 * The door swap used to run *before* the script trigger, which made a door the
 * one thing content could not override. It is here now, behind the trigger,
 * so "content gets first refusal" is true of locs as it is of everything else.
 * Nothing in the tree binds a door script today, so this changes no behaviour —
 * it removes an exception that would have been discovered the hard way.
 */
static void
interaction_engine_loc(
    struct Mock230Server* srv,
    int op_num,
    int loc_id,
    int tile_x,
    int tile_z,
    int level)
{
    int slot;
    struct Mock230SceneLoc* loc;
    const struct Mock230LocDef* def;
    const char* verb;

    slot = find_interaction_loc(tile_x, tile_z, level, loc_id);
    loc = mock230_scene_loc(slot);
    if( !loc )
    {
        mock230_say(srv, "nothing_interesting_message", NULL);
        return;
    }

    def = mock230_content_loc(loc->loc_id);
    if( def && def->next_loc_stage >= 0 &&
        (def->category == MOCK230_LOC_CATEGORY_DOOR_CLOSED ||
         def->category == MOCK230_LOC_CATEGORY_DOOR_OPENED) )
    {
        int opening = def->category == MOCK230_LOC_CATEGORY_DOOR_CLOSED;
        int x = loc->x;
        int z = loc->z;
        int shape = loc->shape;
        /* The loc's own level, not the caller's: `find_interaction_loc` matched
         * on it, and a door on a bridge deck is addressed by where it is. */
        int loc_level = loc->level;

        /* A door is the canonical two-player case, and the canonical *three*-
         * player one: whoever opens it is not the only person who can now walk
         * through it, and the third player to arrive should not find it shut.
         * The first half was a broadcast; the second is what the ZoneMap is. */
        if( !mock230_world_loc_set(srv, x, z, loc_level, shape, def->next_loc_stage,
                                   loc->angle) )
        {
            mock230_say(srv, "nothing_interesting_message", NULL);
            return;
        }
        if( srv->verbose )
            fprintf(stderr, "mock230: %s %s at %d,%d\n", opening ? "opened" : "closed",
                    def->symbol ? def->symbol : "door", x, z);
        return;
    }

    verb = mock230_scene_loc_op(loc->loc_id, op_num);
    /*
     * Opening the bank is decided by the loc's own menu verb rather than by an
     * id list, for the same reason "Attack" is: OldSchool has dozens of bank
     * booths, chests and counters and every one of them says "Bank" in the
     * cache. An id list would be a second copy of that, kept by hand, and
     * wrong for whichever booth nobody added.
     */
    if( verb &&
        (strcmp(verb, MOCK230_VERB_BANK) == 0 || strcmp(verb, MOCK230_VERB_USE_QUICKLY) == 0) )
    {
        mock230_bank_open(srv);
        return;
    }
    if( verb && strcmp(verb, MOCK230_VERB_CLIMB_UP) == 0 )
    {
        climb(srv, +1);
        return;
    }
    if( verb && strcmp(verb, MOCK230_VERB_CLIMB_DOWN) == 0 )
    {
        climb(srv, -1);
        return;
    }
    if( verb && strcmp(verb, MOCK230_VERB_CLIMB) == 0 )
    {
        /* An unqualified "Climb" is the middle of a staircase, which offers
         * both directions. Up is the reasonable default and the qualified ops
         * are on the same loc for anyone who wants the other one. */
        climb(srv, +1);
        return;
    }
    mock230_say(srv, "nothing_interesting_message", NULL);
}

/*
 * OPLOC<n>: p2 x, p2 z, p2 locId — doors, gates, stairs and ladders.
 *
 * Latches the interaction and walks to a tile beside the loc; the acting is
 * interaction_engine_loc above, or whatever content bound to [oploc<n>].
 */
static void
handle_oploc(
    struct Mock230Server* srv,
    int op_num,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int tile_x;
    int tile_z;
    int loc_id;
    int slot;
    struct Mock230SceneLoc* loc;
    int size_x = 1;
    int size_z = 1;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    tile_x = rsab_g2(&buf);
    tile_z = rsab_g2(&buf);
    loc_id = rsab_g2(&buf);
    if( !rsab_ok(&buf) )
        return;

    mock230_world_clear_pending_action(srv);

    /* The footprint decides what counts as "beside it": a two-tile gate is
     * reachable from tiles a one-tile door is not. */
    slot = mock230_scene_find_loc(tile_x, tile_z, srv->active_player->level, loc_id);
    loc = mock230_scene_loc(slot);
    if( loc )
    {
        tile_x = loc->x;
        tile_z = loc->z;
        size_x = loc->size_x > 0 ? loc->size_x : 1;
        size_z = loc->size_z > 0 ? loc->size_z : 1;
    }

    mock230_world_interaction_set(srv, MOCK230_INTERACT_LOC, op_num, -1, loc_id, tile_x,
                                  tile_z, srv->active_player->level, size_x, size_z);
    mock230_world_walk_beside(srv, tile_x, tile_z);

    mock230_world_process_interaction(srv);
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
 * two as it goes (`mock230_scripts_run_opheldu`).
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
    struct Mock230Server* srv,
    struct RSAreaBuf* buf,
    int component_bytes,
    int* out_obj,
    int* out_slot)
{
    struct Mock230Player* player = srv->active_player;
    int use_obj = rsab_g2(buf);
    int use_slot = rsab_g2(buf);
    int use_com = component_bytes == 4 ? rsab_g4(buf) : rsab_g2(buf);

    (void)use_com;
    if( !rsab_ok(buf) )
        return 0;
    if( use_slot < 0 || use_slot >= MOCK230_INV_SLOTS )
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
    struct Mock230Server* srv,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = srv->active_player;
    struct RSAreaBuf buf;
    int obj_id;
    int slot;
    int component;
    int use_obj;
    int use_slot;
    const struct Mock230ObjInfo* info;
    const struct Mock230ObjInfo* use_info;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    obj_id = rsab_g2(&buf);
    slot = rsab_g2(&buf);
    component = rsab_g4(&buf);
    if( !rsab_ok(&buf) )
        return;
    if( slot < 0 || slot >= MOCK230_INV_SLOTS || player->inv[slot].obj_id != obj_id )
        return;
    if( !useon_tail(srv, &buf, 4, &use_obj, &use_slot) )
        return;

    info = mock230_objinfo(obj_id);
    use_info = mock230_objinfo(use_obj);
    if( srv->verbose )
        fprintf(stderr, "mock230: <- OPHELDU obj=%d (%s) slot=%d com=%d|%d use=%d (%s) slot=%d\n",
                obj_id, info->name, slot, (component >> 16) & 0xffff, component & 0xffff, use_obj,
                use_info->name, use_slot);

    mock230_world_clear_pending_action(srv);

    /* Latched before the dispatch, and in this order, because the dispatch
     * *swaps* them: `last_item` is whichever of the two the bound script is
     * named after by the time it runs. */
    player->last_item = obj_id;
    player->last_slot = slot;
    player->last_com = component;
    player->last_useitem = use_obj;
    player->last_useslot = use_slot;

    if( mock230_scripts_run_opheldu(srv, obj_id, info->category > 0 ? info->category : -1, use_obj,
                                    use_info->category > 0 ? use_info->category : -1) ==
        MOCK230_TRIGGER_NONE )
        mock230_say(srv, "nothing_interesting_message", NULL);
}

/**
 * The shared tail of the three use-on packets that *are* interactions.
 *
 * They differ only in what they name as a target, so the latch, the walk and the
 * "act on arrival" are one function. `use_on` is set on the interaction rather
 * than passed to `mock230_world_interaction_set`, so the three existing op
 * handlers' call sites stay untouched — Phase 3 has to delete fallback calls out
 * of this file and a re-flowed signature would collide with that.
 */
static void
useon_interact(
    struct Mock230Server* srv,
    enum Mock230InteractionKind kind,
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
    struct Mock230Player* player = srv->active_player;

    mock230_world_clear_pending_action(srv);

    player->last_useitem = use_obj;
    player->last_useslot = use_slot;

    /* op 1 is a placeholder: `interaction_ap_trigger` ignores it once `use_on`
     * is set, because a use-on has no op number — "use this on that" is the
     * whole verb. */
    mock230_world_interaction_set(srv, kind, 1, npc_slot, target_id, tile_x, tile_z, level, size_x,
                                  size_z);
    player->interaction.use_on = 1;

    if( kind == MOCK230_INTERACT_OBJ )
    {
        /* Onto the tile, not beside it — the same rule OPOBJ<n> follows. */
        steps_clear(player);
        player->dest_x = tile_x;
        player->dest_z = tile_z;
        steps_walk_to(player, tile_x, tile_z);
    }
    else
    {
        mock230_world_walk_beside(srv, tile_x, tile_z);
    }

    mock230_world_process_interaction(srv);
}

/* OPLOCU: p2 x, p2 z, p2 locId, then the used-item tail. */
static void
handle_oplocu(
    struct Mock230Server* srv,
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
    struct Mock230SceneLoc* loc;
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
        fprintf(stderr, "mock230: <- OPLOCU loc=%d at %d,%d use=%d slot=%d\n", loc_id, tile_x,
                tile_z, use_obj, use_slot);

    /* The footprint decides what counts as "beside it", exactly as OPLOC<n>. */
    slot = mock230_scene_find_loc(tile_x, tile_z, srv->active_player->level, loc_id);
    loc = mock230_scene_loc(slot);
    if( loc )
    {
        tile_x = loc->x;
        tile_z = loc->z;
        size_x = loc->size_x > 0 ? loc->size_x : 1;
        size_z = loc->size_z > 0 ? loc->size_z : 1;
    }

    useon_interact(srv, MOCK230_INTERACT_LOC, -1, loc_id, tile_x, tile_z,
                   srv->active_player->level, size_x, size_z, use_obj, use_slot);
}

/* OPNPCU: p2 npcSlot, then the used-item tail. */
static void
handle_opnpcu(
    struct Mock230Server* srv,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf buf;
    int slot;
    int use_obj;
    int use_slot;
    struct Mock230Npc* npc;
    const struct Mock230NpcInfo* info;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    slot = rsab_g2(&buf);
    if( !rsab_ok(&buf) || slot < 0 || slot >= MOCK230_NPC_MAX )
        return;
    if( !useon_tail(srv, &buf, 2, &use_obj, &use_slot) )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active )
        return;

    if( srv->verbose )
        fprintf(stderr, "mock230: <- OPNPCU slot=%d type=%d use=%d slot=%d\n", slot, npc->type,
                use_obj, use_slot);

    info = mock230_npcinfo(npc->type);
    useon_interact(srv, MOCK230_INTERACT_NPC, slot, npc->type, npc->x, npc->z, npc->level,
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
    struct Mock230Server* srv,
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
        fprintf(stderr, "mock230: <- OPOBJU obj=%d at %d,%d use=%d slot=%d\n", obj_id, tile_x,
                tile_z, use_obj, use_slot);

    useon_interact(srv, MOCK230_INTERACT_OBJ, -1, obj_id, tile_x, tile_z,
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

/* ::commands, so a session can be steered without a UI. */
static void
handle_cheat(
    struct Mock230Server* srv,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = srv->active_player;
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
    if( mock230_scripts_run_debugproc(srv, text) )
        return;

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
            int type = mock230_content_symbol(MOCK230_PACK_NPC, name);

            for( int i = 0; i < MOCK230_NPC_MAX && slot < 0; i++ )
            {
                if( srv->npcs[i].active && srv->npcs[i].type == type )
                    slot = i;
            }
            if( slot < 0 )
            {
                say(srv, "No `%s` in the world.", name);
                return;
            }
            say(srv, "Talking to %s (slot %d).", mock230_npcinfo(srv->npcs[slot].type)->name, slot);
        }
        else
        {
            slot = 0;
            (void)sscanf(text, "talk %d %d", &slot, &op_num);
        }
        if( op_num < 1 || op_num > 5 )
            op_num = 1;
        if( slot >= 0 && slot < MOCK230_NPC_MAX && srv->npcs[slot].active )
        {
            /* `::talk` is a diagnostic, so it reports both misses distinctly:
             * nothing bound, versus a script that was bound and blew up. Not a
             * fallback — it is the cheat telling its user what happened. */
            switch( mock230_scripts_run_trigger(srv, SS_TRIGGER_OPNPC1 + (op_num - 1),
                                                srv->npcs[slot].type, -1, slot) )
            {
            case MOCK230_TRIGGER_NONE:
                say(srv, "npc %d has no [opnpc%d] script.", srv->npcs[slot].type, op_num);
                break;
            case MOCK230_TRIGGER_FAILED:
                say(srv, "npc %d's [opnpc%d] script failed — see the log.",
                    srv->npcs[slot].type, op_num);
                break;
            default:
                break;
            }
        }
        return;
    }

    if( strncmp(text, "equip", 5) == 0 )
    {
        /* `::equip <slot>` wears an inventory item without a right-click. The
         * combat tab is built from the equipped weapon's category, so being
         * able to change weapons headlessly is what makes that testable. */
        int slot = 0;

        (void)sscanf(text, "equip %d", &slot);
        if( slot >= 0 && slot < MOCK230_INV_SLOTS && player->inv[slot].obj_id >= 0 )
        {
            const struct Mock230ObjInfo* info = mock230_objinfo(player->inv[slot].obj_id);

            say(srv, "Equipping %s (category %d).", info->name, info->category);
            equip_from_slot(srv, slot);
        }
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
        mock230_world_set_attack_style(srv, style);
        say(srv, "Attack style: %s.",
            style == MOCK230_STYLE_ACCURATE      ? "accurate"
            : style == MOCK230_STYLE_AGGRESSIVE  ? "aggressive"
            : style == MOCK230_STYLE_DEFENSIVE   ? "defensive"
                                                 : "controlled");
        return;
    }

    if( strncmp(text, "setlevel", 8) == 0 )
    {
        /* `::setlevel <stat> <level>` — the combat formulas are only
         * interesting if the inputs can be moved. */
        int stat = 0;
        int level = 1;

        if( sscanf(text, "setlevel %d %d", &stat, &level) == 2 && stat >= 0 &&
            stat < MOCK230_STAT_COUNT && level >= 1 && level <= 99 )
        {
            player->stat_level[stat] = level;
            player->stat_boosted[stat] = level;
            if( stat == MOCK230_STAT_HITPOINTS )
            {
                player->hitpoints = level;
                mock230_combat_sync_hitpoints(player);
            }
            mock230_combat_stat_mark(player, stat);
            say(srv, "Set stat %d to %d.", stat, level);
        }
        return;
    }

    if( strncmp(text, "equipstats", 10) == 0 )
    {
        /* `::equipstats` opens the bonus screen without walking the sidebar —
         * "View equipment stats" is two clicks deep on the worn tab. */
        mock230_equipment_open_stats(srv);
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
        mock230_world_set_varp(srv, mock230_world_varp("option_run"), player->run_toggle);
        say(srv, "Run %s (%d%%).", player->run_toggle ? "on" : "off",
            player->run_energy * 100 / MOCK230_RUN_ENERGY_MAX);
        return;
    }

    if( strncmp(text, "bank", 4) == 0 )
    {
        /* `::bank` opens the bank without walking to a booth — the Lumbridge
         * ones are on the castle's top floor, two staircases from the spawn
         * tile, which is a long way to go to check a packet. */
        mock230_bank_open(srv);
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

            for( int i = 0; i < MOCK230_NPC_MAX; i++ )
            {
                struct Mock230Npc* npc = &srv->npcs[i];
                int dx;
                int dz;
                int distance;

                if( !npc->active || npc->death_tick >= 0 || npc->level != player->level )
                    continue;
                if( !mock230_combat_attackable(npc->type) )
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
            say(srv, "Attacking %s (slot %d, %d tiles).", mock230_npcinfo(srv->npcs[slot].type)->name,
                slot, best);
        }
        mock230_combat_engage(srv, slot);
        return;
    }

    if( sscanf(text, "item %d %d", &obj_id, &count) >= 1 )
    {
        int slot = inv_first_free(player);
        if( slot >= 0 )
        {
            inv_set(player, slot, obj_id, count);
            say(srv, "Spawned %s.", mock230_objinfo(obj_id)->name);
        }
        return;
    }
    if( sscanf(text, "tele %d %d", &tile_x, &tile_z) == 2 )
    {
        mock230_world_teleport(srv, player->level, tile_x, tile_z);
        say(srv, "Teleported to %d,%d.", tile_x, tile_z);
        return;
    }
    if( sscanf(text, "npc %d", &npc_type) == 1 )
    {
        npc_spawn(srv, npc_type, player->x + 1, player->z + 1, 3);
        say(srv, "Spawned npc %d.", npc_type);
        return;
    }
    say(srv, "Unknown command: %s", text);
}

void
mock230_world_teleport(
    struct Mock230Server* srv,
    int level,
    int abs_x,
    int abs_z)
{
    struct Mock230Player* player = srv->active_player;

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
 * `mock230_world_handle` the function every new packet had to be threaded
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
    struct Mock230Server* srv,
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
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_move(srv, payload, len, 14);
}

static void
handle_opheld_packet(
    struct Mock230Server* srv,
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
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_inv_buttond(srv, payload, len);
}

static void
handle_opnpc_packet(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    handle_opnpc(srv, name - PKTOUT_NAME_OPNPC1 + 1, payload, len);
}

static void
handle_oploc_packet(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    handle_oploc(srv, name - PKTOUT_NAME_OPLOC1 + 1, payload, len);
}

static void
handle_opobj_packet(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    handle_opobj(srv, name - PKTOUT_NAME_OPOBJ1 + 1, payload, len);
}

/* The four use-on packets. One name each — there is no op number to derive. */
static void
handle_opheldu_packet(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_opheldu(srv, payload, len);
}

static void
handle_oplocu_packet(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_oplocu(srv, payload, len);
}

static void
handle_opnpcu_packet(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_opnpcu(srv, payload, len);
}

static void
handle_opobju_packet(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    handle_opobju(srv, payload, len);
}

static void
handle_cheat_packet(
    struct Mock230Server* srv,
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
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct RSAreaBuf resume;
    int uid;

    (void)name;
    rsab_wrap(&resume, (void*)payload, (size_t)len);
    uid = rsab_g4(&resume);
    if( !rsab_ok(&resume) )
        return;
    if( srv->verbose )
        fprintf(stderr, "mock230: <- RESUME_PAUSEBUTTON %d:%d\n", uid >> 16, uid & 0xffff);
    mock230_scripts_resume_button(srv, uid);
}

static void
handle_if_button(
    struct Mock230Server* srv,
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
        fprintf(stderr, "mock230: <- IF_BUTTON %d:%d\n", uid >> 16, uid & 0xffff);

    /* rev 230 has no separate resume opcode in practice — a component the
     * server enabled with IF_SETEVENTS answers a click with IF_BUTTON, and
     * the server decides what it meant. Try it as a resume first; a click
     * that matches no registered button falls through, which is how sidebar
     * tabs (switched client-side on a varc) stay a no-op. */
    if( mock230_scripts_resume_button(srv, uid) )
        return;

    /* The world map's close buttons carry no cache op, so a click on the
     * red X arrives here rather than as IF_BUTTON1. */
    if( mock230_worldmap_handle_button(srv, uid, 1) )
        return;

    /* Content bound to the component wins. The bank's router is what is left,
     * and it is a declared fallback now rather than "what happens otherwise":
     * it used to keep the bank's toggles working with no script pack at all,
     * which is precisely the second implementation this stopped being. */
    srv->active_player->last_com = uid;
    srv->active_player->last_slot = -1;
    if( mock230_scripts_fallback(srv, MOCK230_FALLBACK_IF_BUTTON,
                                 mock230_scripts_run_if_button(srv, uid)) )
        mock230_bank_handle_button(srv, uid, -1, -1, 1);
}

/*
 * IF_BUTTON1..10: op N of an IF3 component (RSProt If3Button). Distinct from
 * IF_BUTTON above, which is the op-less plain click — an op-bearing widget like
 * the world map orb names which verb was picked, and the verb is the whole
 * message ("Floating World Map" and "Fullscreen World Map" are ops 2 and 3 of
 * the same component).
 */
static void
handle_if_button_op(
    struct Mock230Server* srv,
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
        fprintf(stderr, "mock230: <- IF_BUTTON%d %d:%d sub=%d\n", op_num, uid >> 16,
                uid & 0xffff, sub);
    if( mock230_worldmap_handle_button(srv, uid, op_num) )
        return;
    srv->active_player->last_com = uid;
    srv->active_player->last_slot = sub;
    srv->active_player->last_verb = op_num;
    /* The component uid is the trigger's subject; an interface button has
     * neither a category nor an npc. `sub` and `op` reach content through
     * last_slot and last_verb, which is where a RuneScript trigger reads them.
     *
     * (uid, sub, obj, op) — the bank fallback needs the sub id, which is which
     * of the 1,220 dynamic children was clicked. */
    if( mock230_scripts_fallback(srv, MOCK230_FALLBACK_IF_BUTTON,
                                 mock230_scripts_run_if_button(srv, uid)) )
        mock230_bank_handle_button(srv, uid, sub, -1, op_num);
}

static void
handle_click_world_map(
    struct Mock230Server* srv,
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
    mock230_worldmap_click(srv, (packed >> 28) & 0x3, (packed >> 14) & 0x3fff,
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
mock230_note_modal_mount(
    struct Mock230Server* srv,
    int uid,
    int group)
{
    const struct Mock230Ids* ids = mock230_ids();

    if( !srv || !srv->active_player )
        return;
    if( uid == ids->com_gameframe_mainmodal )
        srv->active_player->mainmodal_group = group;
    else if( uid == ids->com_gameframe_sidemodal )
        srv->active_player->sidemodal_group = group;
    else if( uid == ids->com_chatbox_modal )
        srv->active_player->chatmodal_group = group;
}

/*
 * Take the chatbox dialogue off the screen.
 *
 * The mirror of `if_openchat` (mock230_scripts.c), and like it, one packet:
 * unmounting `chatbox:chatmodal` is what the client's own on_sub_change hook
 * watches, and script908 is what re-shows the chat scrollback behind it.
 */
static void
close_chat_modal(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->active_player;

    if( player->chatmodal_group <= 0 )
        return;
    mock230_send_if_closesub(srv->active_player, mock230_ids()->com_chatbox_modal);
    player->resume_button_count = 0;
}

void
mock230_world_close_modal(struct Mock230Server* srv)
{
    mock230_world_close_modal_ex(srv, 1);
}

void
mock230_world_close_modal_ex(
    struct Mock230Server* srv,
    int clear_weak_queue)
{
    struct Mock230Player* player;
    const struct Mock230Ids* ids = mock230_ids();
    int main_group;
    int bank_was_open;

    if( !srv || !srv->active_player )
        return;
    player = srv->active_player;

    /*
     * The weak queue dies with the modal, and that is the only thing that
     * distinguishes a weak entry from a normal one — without it `weakqueue`
     * would be a synonym content could not tell apart. `Player.closeModal`
     * clears it first, before the early return below, so a close with nothing
     * mounted still discards it.
     */
    if( clear_weak_queue )
        mock230_scripts_clear_weak_queue(player);

    /*
     * The parked script goes first, and unconditionally.
     *
     * `chatmodal_group` says an interface is mounted; the parked script is the
     * conversation itself, and the two can come apart — a `p_countdialog` waits
     * with no chat mount of its own. Ending the wait is what actually frees the
     * player's one script slot, so it must not be conditional on the mount.
     */
    mock230_scripts_close_dialogue(srv);
    close_chat_modal(srv);

    main_group = player->mainmodal_group;
    if( main_group <= 0 )
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
     * one, which is why it is not in `enum Mock230Fallback`.
     *
     * And underneath that, the reason nobody had noticed: **the subject is the
     * interface, not a component in it.** This asked with
     * `MOCK230_COM(main_group, 0)` — 12 << 16 for the bank — while the compiler
     * keys `[if_close,bankmain]` on the bare interface id 12
     * (`[if_button,bankmain:note_graphic]` is the one that keys on a packed uid,
     * because *that* subject names a child). The two never met, so no
     * `[if_close]` in this tree had ever run, and the suppression above was
     * therefore invisible: it could only fire on a script that could not be
     * found. The only cost so far was the server transmitting to a screen the
     * player had closed.
     */
    bank_was_open = player->bank.open;
    mock230_scripts_run_trigger(srv, SS_TRIGGER_IF_CLOSE, main_group, -1, -1);

    /* One screen keeps state of its own beyond the mount — the bank, which has
     * containers and a reorganise — so it closes through its own function.
     * Anything else is just a mount, and dropping it is the whole of closing
     * it: the equipment screen's repaint is gated on `mainmodal_group`, which
     * the closesub below already clears.
     *
     * Read before the trigger, because `[label,closebank]`'s own
     * `inv_stoptransmit(bankmain:scrollbar)` clears the flag — and
     * `mock230_bank_close` returns early on a bank it thinks is already shut,
     * which would swallow the unmount a second way. */
    if( bank_was_open )
    {
        player->bank.open = 1;
        mock230_bank_close(srv);
    }
    else
    {
        mock230_send_if_closesub(srv->active_player, ids->com_gameframe_mainmodal);
        if( player->sidemodal_group > 0 )
            mock230_send_if_closesub(srv->active_player, ids->com_gameframe_sidemodal);
    }
}

void
mock230_world_clear_pending_action(struct Mock230Server* srv)
{
    mock230_combat_stop_player(srv);
    mock230_world_interaction_clear(srv);
    mock230_world_close_modal(srv);
}

/*
 * The player pressed Escape, or clicked a component whose CS2 called
 * `if_close`. The client has already torn its own copy down; the server has to
 * agree, or the next open finds the interface still marked open and sends
 * nothing.
 */
static void
handle_close_modal(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    (void)name;
    (void)payload;
    (void)len;

    if( srv->verbose )
        fprintf(stderr, "mock230: <- CLOSE_MODAL (main=%d side=%d chat=%d)\n",
                srv->active_player->mainmodal_group, srv->active_player->sidemodal_group,
                srv->active_player->chatmodal_group);
    mock230_world_close_modal(srv);
}

/* The number a p_countdialog collected — "Withdraw-X". */
static void
handle_resume_countdialog(
    struct Mock230Server* srv,
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
        fprintf(stderr, "mock230: <- RESUME_P_COUNTDIALOG %d\n", (int)value);
    srv->active_player->last_int = value;
    /* A parked script owns the answer if there is one; only when nothing is
     * waiting does the engine's own pending "-X" row get it. */
    if( !mock230_scripts_resume_countdialog(srv, value) )
        mock230_bank_resume_countdialog(srv, (int)value);
}

/* ------------------------------------------------------------------ */
/* Social — friends, ignore, private chat                              */
/* ------------------------------------------------------------------ */

/*
 * The wire half of docs/FRIENDS_PRIVATE_CHAT.md. The *rules* are all in
 * mock230_friends.c (the roster, `isVisibleTo`, the caps, the pm ids); nothing
 * below decides anything, it only reads packets and writes packets.
 *
 * The shape is LostCity's, and the correspondence is one-to-one:
 *
 *   FriendListAddHandler.ts &c  -> the four mutation handlers here
 *   FriendServer.broadcastWorldToFollowers -> social_broadcast_to_followers
 *   FriendServer.sendPlayerWorldUpdate     -> social_send_world_update
 *   FriendServer.sendFriendsListToPlayer   -> mock230_world_social_login
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
static struct Mock230Player*
social_player_by_name37(
    struct Mock230Server* srv,
    int64_t name37)
{
    if( name37 == 0 )
        return NULL;
    for( int i = 0; i < srv->player_count; i++ )
    {
        struct Mock230Player* p = &srv->players[i];

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
 * `mock230_friends_get`; this is the same rule for a name that may not be in
 * the viewer's list yet (the moment right after a FRIENDLIST_ADD).
 */
static void
social_send_world_update(
    struct Mock230Server* srv,
    int64_t viewer37,
    int64_t other37)
{
    struct Mock230Player* viewer = social_player_by_name37(srv, viewer37);

    if( !viewer )
        return;
    mock230_send_update_friendlist(
        viewer,
        other37,
        mock230_friends_visible_to(viewer37, other37) ? mock230_friends_world(other37) : 0);
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
    struct Mock230Server* srv,
    int64_t name37)
{
    int64_t followers[MOCK230_SOCIAL_FRIENDS_MAX];
    int count = mock230_friends_followers(name37, followers, (int)(sizeof(followers) /
                                                                  sizeof(followers[0])));

    if( count > (int)(sizeof(followers) / sizeof(followers[0])) )
    {
        /* An engine ceiling, not a game outcome: say so rather than silently
         * leaving the tail of the list un-notified. */
        fprintf(stderr,
                "mock230: %d followers is more than the %d this broadcast can carry; "
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
 * Silence when the hook is unresolved is deliberate and is the whole content
 * seam: whether a tree notifies at all is stated by whether it defines the proc.
 */
static void
social_notify_followers(
    struct Mock230Server* srv,
    int64_t name37,
    const char* display_name,
    const struct SSVM_Script* hook)
{
    int64_t followers[MOCK230_SOCIAL_FRIENDS_MAX];
    const int max = (int)(sizeof(followers) / sizeof(followers[0]));
    struct Mock230Player* was_active = srv->active_player;
    const char* strv[1];
    int count;

    if( !hook || name37 == 0 || !display_name || !display_name[0] )
        return;

    count = mock230_friends_followers(name37, followers, max);
    if( count > max )
        count = max; /* social_broadcast_to_followers already said so, loudly. */

    strv[0] = display_name;
    for( int i = 0; i < count; i++ )
    {
        struct Mock230Player* follower = social_player_by_name37(srv, followers[i]);

        /* Not online here, so there is no chatbox to write to. */
        if( !follower )
            continue;
        /* Telling you that you have logged in is not a thing any client does,
         * and adding yourself as a friend is not refused anywhere. */
        if( follower->name37 == name37 )
            continue;
        /* The same gate the world byte gets. Without it a player whose privacy
         * is OFF still announces themselves to everyone who listed them, which
         * is precisely the leak `isVisibleTo` exists to stop. */
        if( !mock230_friends_visible_to(followers[i], name37) )
            continue;

        mock230_world_set_active(srv, follower);
        mock230_scripts_run_hook_sv(srv, hook, NULL, 0, strv, 1);
    }
    mock230_world_set_active(srv, was_active);
}

/*
 * The login dump — `sendFriendsListToPlayer` + `sendIgnoreListToPlayer` +
 * `FriendlistLoaded(2)`, then the follower broadcast.
 *
 * Order matters for one of these: FRIENDLIST_LOADED(2) has to come *after* the
 * entries, because the client's panel treats a negative/unloaded count as
 * "Loading friends list" and 2 is what ends that state.
 *
 * The ignore list goes out even when it is empty. The client replaces its store
 * wholesale on this packet, so an empty one is the statement "you have no
 * ignores", which is different from never having been told.
 */
static void
mock230_world_social_login(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;
    int64_t me = player->name37;
    int count;

    if( me == 0 )
        return;

    count = mock230_friends_count(me);
    for( int i = 0; i < count; i++ )
    {
        int64_t friend37 = 0;
        int world = 0;

        if( mock230_friends_get(me, i, &friend37, &world) )
            mock230_send_update_friendlist(player, friend37, world);
    }
    mock230_send_friendlist_loaded(player, 2);

    {
        int64_t ignores[MOCK230_SOCIAL_IGNORES_MAX];
        int n = mock230_friends_ignore_count(me);

        if( n > (int)(sizeof(ignores) / sizeof(ignores[0])) )
            n = (int)(sizeof(ignores) / sizeof(ignores[0]));
        for( int i = 0; i < n; i++ )
            mock230_friends_ignore_get(me, i, &ignores[i]);
        mock230_send_update_ignorelist(player, ignores, n);
    }

    /* The client's own filter UI is driven by these three, and nothing else
     * tells it what the server thinks they are. The reference has no equivalent
     * because its 2004 client kept the modes locally; at rev 230 the CS2 side
     * asks through chat_getfilter_* and the server is the source. */
    {
        int public_mode = 0, private_mode = 0, trade_mode = 0;

        mock230_friends_chat_modes(me, &public_mode, &private_mode, &trade_mode);
        mock230_send_chat_filter_settings(player, public_mode, private_mode, trade_mode);
    }

    social_broadcast_to_followers(srv, me);

    /*
     * After the broadcast, not before: a follower's panel should already read
     * "World 1" beside the name by the time the line about it arrives. The
     * order is only cosmetic — both are packets on the same tick — but it is
     * the order the sentence describes.
     */
    social_notify_followers(srv, me, player->display_name,
                            srv->hooks.friend_login_notification);
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
 * there — and mock230_friends.h says in as many words that no caller may invent
 * a message here.
 */
static void
handle_social_list(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = srv->active_player;
    int64_t me;
    int64_t target;
    enum Mock230SocialResult result;

    if( !player )
        return;
    me = player->name37;
    target = social_read_name37(payload, len);
    if( me == 0 || target == 0 )
        return;
    if( !mock230_friends_social_gate(player) )
        return;

    switch( name )
    {
    case PKTOUT_NAME_FRIENDLIST_ADD:
        result = mock230_friends_add(me, target);
        /* Two updates, both the reference's: the new friend's world straight
         * back to the adder, and a broadcast because the adder's own "Friends"
         * privacy mode may now let this person see *them*. */
        social_send_world_update(srv, me, target);
        social_broadcast_to_followers(srv, me);
        break;
    case PKTOUT_NAME_FRIENDLIST_DEL:
        result = mock230_friends_del(me, target);
        social_broadcast_to_followers(srv, me);
        break;
    case PKTOUT_NAME_IGNORELIST_ADD:
        result = mock230_friends_ignore_add(me, target);
        social_broadcast_to_followers(srv, me);
        break;
    case PKTOUT_NAME_IGNORELIST_DEL:
        result = mock230_friends_ignore_del(me, target);
        social_broadcast_to_followers(srv, me);
        break;
    default:
        return;
    }

    if( srv->verbose )
    {
        char who[32];

        base37tostr((uint64_t)target, who, (int)sizeof(who));
        fprintf(stderr, "mock230: <- social name=%d target=%s result=%d\n", name, who,
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
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = srv->active_player;
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

    mock230_friends_set_chat_modes(player->name37, public_mode, private_mode, trade_mode);
    /* Echo, so the client's filter UI and the server's copy cannot drift — the
     * service clamps an out-of-range private mode to ON and the client has no
     * other way to learn that happened. */
    mock230_friends_chat_modes(player->name37, &public_mode, &private_mode, &trade_mode);
    mock230_send_chat_filter_settings(player, public_mode, private_mode, trade_mode);
    /* A mode change is a visibility change for every follower. */
    social_broadcast_to_followers(srv, player->name37);

    if( srv->verbose )
        fprintf(stderr, "mock230: <- CHAT_SETMODE public=%d private=%d trade=%d\n",
                public_mode, private_mode, trade_mode);
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
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = srv->active_player;
    struct Mock230Player* target_player;
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
     * slice off the wire). The number is content's; see mock230_friends.h. */
    if( packed_len < 0 || packed_len > mock230_friends_cap_pm_bytes() )
        return;
    if( !mock230_friends_social_gate(player) )
        return;

    RSCache_BufferInit(&packed, (uint8_t*)payload + 8, (uint32_t)packed_len);
    text = wordpack_unpack(&packed, packed_len);
    if( !text )
        return;

    target_player = social_player_by_name37(srv, target);
    if( target_player )
        mock230_send_message_private(
            target_player, me, mock230_friends_next_pm_id(), /* staff */ 0, text);

    if( srv->verbose )
    {
        char who[32];

        base37tostr((uint64_t)target, who, (int)sizeof(who));
        fprintf(stderr, "mock230: <- MESSAGE_PRIVATE to=%s%s \"%s\"\n", who,
                target_player ? "" : " (offline; dropped)", text);
    }
    free(text);
}

typedef void (*Mock230PacketHandler)(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len);

struct Mock230PacketRoute
{
    int name;
    Mock230PacketHandler handler;
};

/*
 * The routing table. Adding a packet is a line here plus a handler; nothing
 * else in the file has to change, which is the whole point of it being a table.
 */
static const struct Mock230PacketRoute k_packet_routes[] = {
    { PKTOUT_NAME_MOVE_GAMECLICK, handle_move_gameclick },
    { PKTOUT_NAME_MOVE_OPCLICK, handle_move_gameclick },
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

    { PKTOUT_NAME_RESUME_PAUSEBUTTON, handle_resume_pausebutton },
    { PKTOUT_NAME_RESUME_P_COUNTDIALOG, handle_resume_countdialog },
    { PKTOUT_NAME_CLICK_WORLD_MAP, handle_click_world_map },
    { PKTOUT_NAME_CLOSE_MODAL, handle_close_modal },
    { PKTOUT_NAME_CLIENT_CHEAT, handle_cheat_packet },

    { PKTOUT_NAME_FRIENDLIST_ADD, handle_social_list },
    { PKTOUT_NAME_FRIENDLIST_DEL, handle_social_list },
    { PKTOUT_NAME_IGNORELIST_ADD, handle_social_list },
    { PKTOUT_NAME_IGNORELIST_DEL, handle_social_list },
    { PKTOUT_NAME_CHAT_SETMODE, handle_chat_setmode },
    { PKTOUT_NAME_MESSAGE_PRIVATE, handle_message_private },
};

void
mock230_world_handle(
    struct Mock230Player* player,
    int name,
    const uint8_t* payload,
    int len)
{
    struct Mock230Server* srv;

    /* A packet from a session with no player is a packet from a connection that
     * was refused a slot, or one still mid-handshake. Dropping it is the only
     * safe answer: every handler below writes player state. */
    if( !player || !player->active )
        return;
    srv = player->world;
    mock230_world_set_active(srv, player);

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
        fprintf(stderr, "mock230: <- unrouted packet name %d (%d bytes)\n", name, len);
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
 * That last sentence used to be `MOCK230_FALLBACK_AI_QUEUE3`: five lines here
 * that read `Mock230NpcDef.death_drop` and called `mock230_world_obj_add`
 * whenever nothing was bound. The row is deleted and the fallback count is 6.
 * The field stays — `record_authored_param` files the same value under param id
 * 2634 for `npc_param` to read, and `mock230_servercodec.c` carries it on the
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
 * 9,149 of its 16,292 records (triage §16.1), and `mock230_npc_category()` reads
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
mock230_world_npc_died(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Npc* npc = &srv->npcs[slot];

    mock230_scripts_run_trigger(srv, SS_TRIGGER_AI_QUEUE3, npc->type,
                                mock230_npc_category(npc->type), slot);
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
mock230_world_varp(const char* symbol)
{
    return mock230_content_symbol(MOCK230_PACK_VARP, symbol);
}


/*
 * The two varbits interface 593 builds itself from.
 *
 * `weapon_category` selects which of the button layouts the combat tab builds
 * — the tab's own CS2 (script 7593) hides every style button when it is 0,
 * which is why an unset one shows nothing but auto-retaliate. It carries a
 * weapon *type*, derived above from the worn weapon's cache category.
 *
 * `combat_level` is the number printed above the buttons, by the same formula
 * the minimenu colours npc levels with.
 *
 * Both are written whenever their input changes — equipping, unequipping or
 * gaining a level — because a varbit the client is never told about reads as 0,
 * and 0 is a meaningful category.
 */
void
mock230_world_sync_combat_varbits(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->active_player;
    int weapon = player->worn[MOCK230_WEAR_WEAPON].obj_id;
    /*
     * Which button layout the tab draws is content's.
     *
     * `[proc,combat_weapon_type]` is a `switch_category` over the worn weapon,
     * which is the shape the reference keeps it in
     * (`~combat_get_weapon_style_data`). It was a 26-row C table here,
     * transcribed from OpenRune's WeaponCategory.kt — a mapping between two id
     * spaces the cache does not relate, which is precisely the kind of claim
     * that belongs where someone can correct it.
     *
     * Falls back to 0 (unarmed) when content does not answer: an unknown weapon
     * showing three styles and no autocast is the safe wrong answer, since the
     * alternative promises buttons the server cannot handle.
     */
    int32_t weapon_arg = (int32_t)weapon;
    int32_t resolved = 0;
    int category = mock230_scripts_run_hook_int(srv, srv->hooks.combat_weapon_type,
                                                &weapon_arg, 1, &resolved)
                       ? (int)resolved
                       : 0;
    int category_varbit = mock230_content_symbol(MOCK230_PACK_VARBIT, "combat_weapon_category");
    int level_varbit = mock230_content_symbol(MOCK230_PACK_VARBIT, "combatlevel_transmit");
    int level = mock230_combat_level(player);

    /*
     * Compared before writing, unlike an ordinary varp write.
     *
     * A varp assignment always transmits — that is the reference's rule and
     * `[login] %com_mode = 0` depends on it. But these two are *derived*: they
     * are recomputed every tick from the equipment and the stats, so writing
     * unconditionally would put two varps on the wire fifty times a minute for
     * a value that has not moved. Derived state compares; authored state does
     * not.
     */
    if( category_varbit >= 0 && mock230_varbit_get(player, category_varbit) != category )
        mock230_varbit_set(srv, category_varbit, category);
    if( level_varbit >= 0 && mock230_varbit_get(player, level_varbit) != level )
        mock230_varbit_set(srv, level_varbit, level);
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
    return mock230_world_varp("com_mode");
}

int
mock230_world_attack_style(const struct Mock230Server* srv)
{
    int varp = attack_style_varp();

    if( varp < 0 || varp >= MOCK230_VARP_COUNT )
        return MOCK230_STYLE_ACCURATE;
    return srv->active_player->varps[varp];
}

/*
 * Queue a varp for phase 10, once.
 *
 * The dedupe is not an optimisation, it is correctness under varbits: a varbit
 * is a bit range *inside* a varp, so the ten bank settings written on open all
 * land in varp 115. Appending per write queued the same varp ten times, sent it
 * ten times, and — worse — spent ten of the change list's entries on it. The
 * list is a fixed 64 and dropping past it used to be silent, which is a varp the
 * client never hears about and a UI that stays stale with no diagnostic.
 *
 * This is the only copy. mock230_bank.c and mock230_scripts.c each had their
 * own, with different dedupe semantics, which is exactly how the varbit path
 * ended up on the one that had none.
 */
void
mock230_world_mark_varp(
    struct Mock230Player* player,
    int varp)
{
    if( varp < 0 || varp >= MOCK230_VARP_COUNT )
        return;
    for( int i = 0; i < player->varp_changed_count; i++ )
    {
        if( player->varp_changed[i] == varp )
            return;
    }
    if( player->varp_changed_count >= MOCK230_VARP_DIRTY_MAX )
    {
        fprintf(stderr, "mock230: varp change list full (%d), varp %d not sent\n",
                MOCK230_VARP_DIRTY_MAX, varp);
        return;
    }
    player->varp_changed[player->varp_changed_count++] = varp;
}

/*
 * A varp the engine also *keeps state for* has just been written.
 *
 * There are two ways a varp gets written and they used to have two different
 * sets of consequences. `mock230_world_set_varp` is the engine's own setter;
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

int
mock230_world_carrier_writes(int* out_last_varp)
{
    if( out_last_varp )
        *out_last_varp = g_carrier_write_last;
    return g_carrier_writes;
}

void
mock230_world_carrier_writes_reset(void)
{
    g_carrier_writes = 0;
    g_carrier_write_last = -1;
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
 * by a list: a varbit patch (which sets `mock230_varbit_patching`), a varp
 * content declared `wholewrite=allow` for, and a varp nothing is based on.
 */
static void
check_carrier_write(
    int varp,
    int value)
{
    const struct Mock230VarpDef* def;
    int bits;

    if( mock230_varbit_patching() )
        return;
    bits = mock230_varbit_carrier_bits(varp);
    if( bits <= 0 )
        return;
    def = mock230_content_varp(varp);
    if( def && def->wholewrite_allowed )
        return;
    g_carrier_writes++;
    g_carrier_write_last = varp;
    fprintf(stderr,
            "mock230: whole-varp write to varp %d (%s) = %d — %d varbit(s) are packed into "
            "it and this write destroys them; write the varbit, or declare "
            "`wholewrite=allow` on the varp\n",
            varp, mock230_content_symbol_name(MOCK230_PACK_VARP, varp)
                      ? mock230_content_symbol_name(MOCK230_PACK_VARP, varp)
                      : "?",
            value, bits);
}

static void
varp_side_effects(
    struct Mock230Server* srv,
    int varp,
    int value)
{
    struct Mock230Player* player = srv->active_player;

    check_carrier_write(varp, value);

    /*
     * `option_run` is not a variable the server merely reports — it is where
     * the run flag lives. Without this the engine kept its own `run_toggle` and
     * mirrored it *out* to the varp, so a write from the other direction
     * transmitted a lit orb and the player still walked.
     */
    if( varp == mock230_world_varp("option_run") )
        player->run_toggle = value != 0;
}

void
mock230_world_set_varp(
    struct Mock230Server* srv,
    int varp,
    int value)
{
    struct Mock230Player* player = srv->active_player;

    if( varp < 0 || varp >= MOCK230_VARP_COUNT || player->varps[varp] == value )
        return;
    player->varps[varp] = value;
    mock230_world_mark_varp(player, varp);
    varp_side_effects(srv, varp, value);
}

void
mock230_world_varp_written(
    struct Mock230Server* srv,
    int varp,
    int value)
{
    varp_side_effects(srv, varp, value);
}

void
mock230_world_set_attack_style(
    struct Mock230Server* srv,
    int style)
{
    mock230_world_set_varp(srv, attack_style_varp(), style);
}

void
mock230_world_set_home(
    int tile_x,
    int tile_z)
{
    g_home_x = tile_x;
    g_home_z = tile_z;
}

/*
 * Say whose turn it is. The one place `active_player` is written.
 *
 * Every subsystem that still reaches `srv->active_player` — the scripts, the
 * bank, combat, the world map — is asking "who am I doing this for", and this is
 * the answer. The per-player phases call it as they iterate and the session
 * calls it before dispatching a packet, so the question always has one.
 */
void
mock230_world_set_active(
    struct Mock230Server* srv,
    struct Mock230Player* player)
{
    srv->active_player = player;
}

/*
 * Take a pool slot and give it its session.
 *
 * Called by a host before mock230_world_login, because the session exists as
 * soon as the handshake does and the world does not. Splitting it out is what
 * makes "a world with no client" (the selftest) and "a world with one" the same
 * code path with a NULL in one field.
 *
 * The slot is the pid, and slots are never reused while occupied nor compacted
 * when freed: the wire carries the index, so moving a player would rename them
 * to every client tracking them.
 */
struct Mock230Player*
mock230_world_add_player(
    struct Mock230Server* srv,
    struct Mock230Session* session)
{
    for( int i = 0; i < MOCK230_PLAYER_MAX; i++ )
    {
        struct Mock230Player* player = &srv->players[i];

        if( player->active )
            continue;
        memset(player, 0, sizeof(*player));
        player->active = 1;
        player->world = srv;
        player->pid = i;
        player->session = session;
        if( i >= srv->player_count )
            srv->player_count = i + 1;
        mock230_world_set_active(srv, player);
        return player;
    }

    /* Full. Refusing loudly is the only honest answer: silently overwriting
     * players[0] is what an implicit "the primary player" would have done. */
    fprintf(stderr, "mock230: the world is full (%d players); refusing the connection\n",
            MOCK230_PLAYER_MAX);
    return NULL;
}

void
mock230_world_remove_player(
    struct Mock230Server* srv,
    struct Mock230Player* player)
{
    int64_t name37;
    char display_name[sizeof(player->display_name)];

    if( !player || !player->active )
        return;
    name37 = player->name37;
    /* Copied, not aliased: the slot this points into is reused by the next
     * login and the notification below runs after the slot has been released. */
    snprintf(display_name, sizeof(display_name), "%s", player->display_name);

    /*
     * The bank is heap-allocated per player, so a logout that only cleared
     * `active` would leak it — and the next player into this slot would inherit
     * the pointer through the memset in mock230_world_add_player.
     */
    mock230_bank_shutdown_player(player);
    /*
     * The friend service is told before the slot goes, because it is keyed by
     * name and the name is about to be unreachable. Only presence is dropped —
     * the lists stay, which is what lets a follower still see this name in
     * their panel with "Offline" beside it. The reference does the same thing
     * from `World.removePlayer` (World.ts:1590).
     */
    mock230_friends_logout(player->name37);
    player->active = 0;
    player->session = NULL;
    /* Everyone else is holding this pid. Clearing `active` is what the next
     * PLAYER_INFO reads to retire it; nothing has to be sent from here. */
    if( srv->active_player == player )
        mock230_world_set_active(srv, NULL);
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
     * has to come after `mock230_friends_logout`, which is what makes
     * `mock230_friends_world` answer 0.
     */
    social_broadcast_to_followers(srv, name37);
    social_notify_followers(srv, name37, display_name,
                            srv->hooks.friend_logout_notification);
}

/*
 * The name the session collected at login.
 *
 * Separate from mock230_world_player_init, and called after it, because that
 * function memsets the player — a name written before it is a name erased by it.
 * This is the whole of why `displayname` used to come back empty.
 */
void
mock230_world_set_display_name(
    struct Mock230Player* player,
    const char* name)
{
    if( !name || !name[0] )
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
mock230_world_init(
    struct Mock230Server* srv,
    int zone_x,
    int zone_z)
{
    if( srv->world_built )
    {
        if( srv->verbose )
            fprintf(stderr, "mock230: world already built; leaving it alone\n");
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
    mock230_scene_build(mock230_world_cache_dir(), zone_x, zone_z);

    mock230_world_build_entities(srv);
}

void
mock230_world_reset(struct Mock230Server* srv)
{
    srv->world_built = 0;
    /* The ZoneMap is the world's memory of every runtime loc change and of
     * where every entity stands, so a world that is being thrown away has to
     * throw it away too — the selftest runs many worlds in one process, and a
     * surviving map would replay the previous world's doors into the next
     * one's. */
    mock230_zone_free(srv);
    srv->npc_slot_max = 0;
}

/*
 * Reset one player to a newly-created character.
 *
 * Everything here used to be the tail of mock230_world_init, which is what made
 * a login and a world build the same event.
 */
void
mock230_world_player_init(struct Mock230Player* player)
{
    /* The bank owns a heap allocation, and the memset below is what would
     * otherwise lose the pointer to it — a re-init (which the selftest does)
     * has to release the old container before the struct is cleared. */
    mock230_bank_shutdown_player(player);

    /*
     * The memset clears the *game* state, not the player's identity.
     *
     * `world`, `session`, `pid` and `active` are set by
     * mock230_world_add_player before this runs — the session exists as soon as
     * the handshake does, and the world does not — so they are saved across the
     * clear. Losing `session` here leaves a logged-in player the encoders
     * silently write nothing for; losing `world` crashes the first encoder that
     * reaches through it. Neither is a compile error.
     */
    {
        struct Mock230Server* world = player->world;
        struct Mock230Session* session = player->session;
        int pid = player->pid;

        memset(player, 0, sizeof(*player));
        player->world = world;
        player->session = session;
        player->pid = pid;
        player->active = 1;
    }
    /* On the home tile, not in the middle of the scene: the scene is 104 tiles
     * of whatever the origin zone happens to cover, and standing in the middle
     * of it puts you somewhere arbitrary. */
    player->x = g_home_x;
    player->z = g_home_z;
    player->combat_target = -1;
    player->level = 0;
    /* Same reason as the npc's: 0 is a sequence id, and the priority gate reads
     * this as the animation already queued for the tick. */
    player->anim_id = -1;
    player->face_entity = -1;
    /* The memset above leaves this 0, which is a real dbtable id — so a
     * `db_findnext` with no query would iterate table 0 instead of reporting
     * that nothing was selected. Same class as `session->pending_opcode`. */
    player->db_query_table = -1;
    player->db_query_index = -1;
    player->db_query_column = -1;
    /* -1, not the memset's 0, because 0 is a real obj id and a real backpack
     * slot: a script reading `last_useitem` outside a use-on must get a sentinel
     * rather than "the player used a Dwarf remains on it". `Player.ts:371-374`
     * declares all four `last_*` item fields -1; the two above these are left at
     * 0, which is a divergence this stage found rather than one it made. */
    player->last_useitem = -1;
    player->last_useslot = -1;
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
    player->last_map_z = -1;
    /* A memset leaves `zone_index` at 0, which is a real zone. This is what
     * makes the first flush compute an active window rather than believe the
     * player has been standing in the south-west corner of the map. */
    mock230_zone_player_reset(player);

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
    for( int stat = 0; stat < MOCK230_STAT_COUNT; stat++ )
    {
        player->stat_level[stat] = 1;
        player->stat_boosted[stat] = 1;
        player->stat_xp_tenths[stat] = 0;
    }
    mock230_combat_sync_hitpoints(player);
    player->dest_x = -1;
    player->dest_z = -1;
    player->place_dirty = 1;
    player->masks |= MOCK230_PMASK_APPEARANCE;

    /* Full energy, run off. The sent-values are deliberately impossible so the
     * first flush is a send whatever the starting state is. */
    player->run_energy = MOCK230_RUN_ENERGY_MAX;
    player->run_toggle = 0;
    player->running = 0;
    player->run_energy_sent = -1;
    player->run_weight_sent = INT32_MIN;

    for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
    {
        player->inv[i].obj_id = -1;
        player->inv[i].count = 0;
    }
    for( int i = 0; i < MOCK230_WORN_SLOTS; i++ )
    {
        player->worn[i].obj_id = -1;
        player->worn[i].count = 0;
    }

    /* The containers exist empty. What goes in them the first time a character
     * connects is content's — `[proc,newplayer_inv]` and `[proc,newplayer_bank]`
     * in player/newplayer.rs2, called from [login,_]. */
    mock230_bank_init_player(player);
}

/*
 * The npcs and ground objs the map squares state. Part of the world, not of a
 * login — which is why it moved out of what is now mock230_world_player_init.
 */
static void
mock230_world_build_entities(struct Mock230Server* srv)
{
    /*
     * The npc roster comes from the content tree's map squares — LostCity's
     * `==== NPC ====` sections, which are OpenRune's Lumbridge spawn list
     * transcribed from OpenRune once and authored here since.
     *
     * Every spawn in the tree is created, not only the ones near the start
     * tile: the client is only ever told about npcs within 15 tiles, but the
     * player can walk, and an npc that does not exist until you approach it
     * would pop into being with full hitpoints in front of you.
     */
    memset(srv->npcs, 0, sizeof(srv->npcs));
    {
        int count = 0;
        const struct Mock230MapNpcSpawn* spawns = mock230_content_npc_spawns(&count);

        for( int i = 0; i < count; i++ )
            npc_spawn(srv, spawns[i].npc_id, spawns[i].x, spawns[i].z, spawns[i].level);

        if( count == 0 )
        {
            /* No content tree. One npc beside the home tile keeps every
             * npc-facing path — talking, fighting, NPC_INFO itself — reachable
             * rather than dead, the same way the script fallbacks do. */
            npc_spawn(srv, 3105, g_home_x + 2, g_home_z + 1, 0);
        }
    }

    /* Ground objs, from the same map squares. Duration -1 marks them spawns,
     * which come back after they are taken. */
    memset(srv->ground, 0, sizeof(srv->ground));
    {
        int count = 0;
        const struct Mock230MapObjSpawn* spawns = mock230_content_obj_spawns(&count);

        for( int i = 0; i < count; i++ )
            mock230_world_obj_add(srv, spawns[i].obj_id, spawns[i].count, spawns[i].x,
                                  spawns[i].z, spawns[i].level, -1);
    }
}

void
mock230_world_login(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;
    const struct Mock230Ids* ids = mock230_ids();

    /* Everything below is this player's, and several of the calls still reach
     * the world for it. */
    mock230_world_set_active(srv, player);

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
     * mock230_friends.h.
     */
    mock230_friends_login(player->name37, /* public */ 0, MOCK230_CHAT_PRIVATE_ON,
                          /* trade */ 0, /* staff level */ 0);

    /* 1. The scene. Everything after this is applied by the client behind the
     *    world load, because the packet queue is serial. */
    mock230_send_rebuild_normal(player);

    /* 2. Gameframe root + the HUD and sidebar panels mounted into it. Child
     *    ids are RuneLite InterfaceID.ToplevelOsrsStretch.*; group ids are
     *    InterfaceID.*, all verified present in cache.osrs230. type 1 =
     *    overlay. */
    mock230_send_if_opentop(player, ids->iface_gameframe);
    {
        /*
         * What goes in which slot is content: the `gameframe` enum in
         * content/scripts/player/configs/gameframe.enum, whose keys are
         * gameframe components and whose values are interfaces. It used to be a
         * 24-entry table of raw numbers here, which is the same list OpenRune's
         * GameframeLoader carries — so it may as well be named on both sides
         * and editable without a compiler.
         *
         * The key is a packed (interface << 16) | child uid; IF_OPENSUB wants
         * the child on its own, so the low half is what goes on the wire.
         * type 1 = overlay.
         */
        const struct Mock230EnumDef* frame = mock230_content_enum("toplevel_osrs_stretch");

        if( !frame || frame->count == 0 )
        {
            /* Without the gameframe there is no chatbox and no sidebar, so the
             * session is unusable rather than degraded — say so once rather
             * than leaving a blank screen to be diagnosed. */
            fprintf(stderr,
                    "mock230: no `gameframe` enum in the content tree — the "
                    "gameframe will be empty\n");
        }
        for( int i = 0; frame && i < frame->count; i++ )
            mock230_send_if_opensub(
                player,
                ids->iface_gameframe,
                frame->values[i].key & 0xffff,
                frame->values[i].value,
                1);
    }

    /*
     * 3. What the item containers permit is content's.
     *
     * `~containers_login` and `~worn_tab_login` (player/containers.rs2) arm the
     * drag bits and the worn tab's Remove. This was two `if_setevents` calls
     * here with the mask spelled as `1 << 17 | 1 << 20`, and eleven more in
     * mock230_equipment.c — UI permissions, decided in C, which is the one kind
     * of decision the rev-230 protocol moved to the server precisely so it
     * could be a policy rather than a cache flag.
     *
     * There is no reference for it: the 2004 protocol has no IF_SETEVENTS, so
     * LostCity has nothing to port. The shape follows its `[login]` procs.
     */
    /*    …and on the world map orb, whose verbs are the server's alone. */
    mock230_worldmap_login(srv);

    /* 4. Player state. The pid comes first: it decides which entity in the
     *    stream the client treats as itself, and several things it drives —
     *    the npc menu's level suffix among them — are computed the moment the
     *    first PLAYER_INFO lands. */
    mock230_send_update_pid(player, MOCK230_PLAYER_TERMINATOR);
    /* Both orb numbers, unconditionally: the per-tick flush only sends what
     * changed, and a session that starts full would otherwise never send one. */
    player->run_energy_sent = player->run_energy * 100 / MOCK230_RUN_ENERGY_MAX;
    mock230_send_run_energy(player, player->run_energy_sent);

    /* The combat tab's varps are NOT sent here. They are ordinary varp writes
     * in [login,_] (content/scripts/player/login.rs2), declared transmit=yes in
     * player_controls.varp, and phase 10 puts them on the wire like any other
     * changed varp. A server operator changing the opening attack style should
     * not need a compiler. */
    player->run_weight_sent = player_weight_grams(player) / 1000;
    mock230_send_run_weight(player, player->run_weight_sent);
    for( int stat = 0; stat < MOCK230_STAT_COUNT; stat++ )
        mock230_send_stat(
            player,
            stat,
            player->stat_level[stat],
            player->stat_xp_tenths[stat] / 10,
            player->stat_boosted[stat]);

    /* 5. Containers, in full. Deltas take over from the next tick. */
    mock230_send_inv_full(
        player, ids->com_inventory_items, ids->inv_backpack, player->inv, MOCK230_INV_SLOTS);
    mock230_send_inv_full(
        player,
        MOCK230_COM(ids->iface_wornitems, 0),
        ids->inv_worn,
        player->worn,
        MOCK230_WORN_SLOTS);
    player->inv_dirty = 0;
    player->worn_dirty = 0;

    /* The combat tab builds itself from these; nothing else sends them. */
    mock230_world_sync_combat_varbits(srv);

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
    mock230_world_social_login(player);

    /* 6. First info tick places the player and spawns the npcs. */
    mock230_send_player_info(player);
    mock230_send_npc_info(player);
    mock230_send_tick_end(player);
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

/** 1. World script queue (world_delay), delayed obj spawns, npc hunt. */
static void
phase_world(struct Mock230Server* srv)
{
    mock230_scripts_resume_world(srv);
    mock230_combat_respawn_tick(srv);
}

/**
 * 2. Turn latched client input into world state.
 *
 * The socket drain deliberately stays in mock230_main.c between ticks: moving
 * it in here would need a second buffering layer for no benefit. What belongs
 * in this phase is the *conversion* of what handlers latched into interactions
 * and directly-dispatched triggers, which arrives with the interaction model.
 */
static void
phase_clients_in(struct Mock230Server* srv)
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
phase_npc_events(struct Mock230Server* srv)
{
    for( int slot = 0; slot < srv->npc_slot_max; slot++ )
    {
        struct Mock230Npc* npc = &srv->npcs[slot];

        if( !npc->active || !npc->spawn_pending )
            continue;
        npc->spawn_pending = 0;
        mock230_scripts_run_trigger(srv, SS_TRIGGER_AI_SPAWN, npc->type, -1, slot);
    }
}

/** 4. Every npc's turn: delays, timers, queues, then its mode. */
static void
phase_npcs(struct Mock230Server* srv)
{
    for( int slot = 0; slot < srv->npc_slot_max; slot++ )
    {
        if( !srv->npcs[slot].active )
            continue;
        /* Resume before anything else: a delayed npc is busy, and starting a
         * second script on it would interleave two sets of writes.
         *
         * There used to be a second `[ai_timer]` drain here as well
         * (`mock230_scripts_process_npc_timer`), reading a `Mock230Npc.timer_script`
         * that was written in exactly one place and only ever to -1 — so it could
         * never run. The live drain is in `advance_npcs`, which resolves the
         * trigger by npc type. */
        mock230_scripts_resume_npc(srv, slot);
        mock230_combat_npc_tick(srv, slot);
    }
    advance_npcs(srv);
}

/*
 * The per-player phases, and why they are written this way.
 *
 * Each one iterates the pool and calls `mock230_world_set_active` before doing
 * anything, because most of what they call still reaches the player through the
 * world. That is the residue of the single-player era: the loop is what makes
 * the residue *correct* — the alternative, giving every one of those functions a
 * `Mock230Player*`, is the same change spread over five files and can be done
 * one subsystem at a time behind this.
 *
 * The pool is iterated to `player_count` and skips inactive slots, because a
 * logout leaves a hole rather than compacting (see `Mock230Player.pid`).
 */
#define MOCK230_FOR_EACH_PLAYER(srv, player)                                                       \
    for( int mock230_pid_ = 0; mock230_pid_ < (srv)->player_count; mock230_pid_++ )                \
        if( !((player) = &(srv)->players[mock230_pid_])->active )                                  \
        {                                                                                          \
        }                                                                                          \
        else

/** 5. The player: delays, resumes, queues, timers, then interaction. */
static void
phase_player(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;

    mock230_world_set_active(srv, player);

    /* Order matches the reference exactly, and it matters: a script resumed
     * here must not have a queue entry started on top of it in the same tick. */
    mock230_scripts_resume_player(srv);
    mock230_scripts_process_queues(srv);
    mock230_scripts_process_timers(srv);
    /* The engine queue is drained *after* the timers, which is where
     * `World.processPlayers` puts `processEngineQueue()`. It holds the zone
     * family, detected in phase 10 of the previous tick — so a zone script runs
     * one tick after the crossing, before this tick's movement rather than
     * after it. */
    mock230_scripts_process_engine_queue(srv);

    /* Movement is the tail of the interaction step in the reference, between
     * the pre-move and post-move interaction attempts. Combat brackets it the
     * same way: aim, step, then swing, so a player who arrives this tick
     * attacks on it rather than a tick later.
     *
     * The aim has to come first — `pathToTarget()` then `updateMovement()` —
     * or every step chases where the target stood last tick. */
    mock230_combat_player_approach(srv);
    advance_player(srv);
    /* Post-move: a player who reached their target this tick acts on it now,
     * not next tick. This is the other half of the interaction model — the
     * packet handler tried once when the click arrived, and this is every tick
     * of the walk that followed. */
    mock230_world_process_interaction(srv);
    /* Energy is spent on the steps that were actually taken, so a route that
     * ran out of tiles this tick regenerates instead of draining. */
    run_energy_tick(srv, player->running ? player->move_count : 0);
    /* Before the swing: a prayer that ran out this tick must not protect the
     * hit that lands on it. */
    mock230_combat_player_tick(srv);
}

static void
phase_players(struct Mock230Server* srv)
{
    struct Mock230Player* player;

    MOCK230_FOR_EACH_PLAYER(srv, player)
        phase_player(player);
}

/** 6. Logouts, which run the [logout] trigger before dropping the player. */
static void
phase_logouts(struct Mock230Server* srv)
{
    (void)srv;
}

/** 7. Logins, which run the [login] trigger. */
static void
phase_logins(struct Mock230Server* srv)
{
    struct Mock230Player* player;

    MOCK230_FOR_EACH_PLAYER(srv, player)
    {
        if( !player->login_pending )
            continue;
        player->login_pending = 0;
        mock230_world_set_active(srv, player);
        /* The C burst already sent the scene, the gameframe and the containers —
         * that is engine work. [login] is where anything an operator would want
         * to change without recompiling belongs.
         *
         * Specific, not the chain: `[login]` has no subject, so the reference
         * asks for the global form directly (`Player.ts` getByTriggerSpecific)
         * rather than walking two rungs that cannot exist. */
        mock230_scripts_run_trigger_specific(srv, SS_TRIGGER_LOGIN, -1, -1, -1);
    }
}

/** 8. Loc/obj respawn timers and the zone bookkeeping. */
/*
 * Every runtime loc mutation in the world goes through here.
 *
 * Three things have to happen together and used to happen in three places: the
 * scene's collision has to move, the ZoneMap has to record that this tile no
 * longer matches the map square, and the zone has to queue the wire event. The
 * old shape did only the third — `mock230_world_broadcast_loc` walked the player
 * pool and sent LOC_ADD_CHANGE to everyone on that level whose scene contained
 * the tile — which is correct for whoever is standing there and silently wrong
 * for everyone else forever, because a broadcast has no memory.
 *
 * The key is `(x, z, level, shape)`, which is what the wire uses: LOC_ADD_CHANGE
 * and LOC_DEL identify a loc by its tile and its shape, and a tile can hold a
 * wall and a piece of scenery at once.
 */
int
mock230_world_loc_set(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle)
{
    int slot = mock230_scene_find_loc_exact(x, z, level, shape);
    struct Mock230SceneLoc* existing = mock230_scene_loc(slot);
    /*
     * What the cache says is here, captured before the scene is touched and
     * only used when the ZoneMap has no record yet — a second change would ask
     * the scene and get our own first edit back. -1 for a tile the map square
     * has nothing on, which is how a `loc_add` is told from a `loc_change`.
     */
    struct Mock230ZoneLoc* known = mock230_zone_loc_find(srv, x, z, level, shape);
    int base_id = known ? known->base_loc_id : (existing ? existing->loc_id : -1);
    int base_angle = known ? known->base_angle : (existing ? existing->angle : 0);

    if( loc_id < 0 )
    {
        if( !existing || !mock230_scene_remove_loc(slot) )
            return 0;
        /* The removed loc's own angle, not the caller's: LOC_DEL carries
         * shape+angle and the client matches on both. */
        angle = existing->angle;
    }
    else if( existing )
    {
        if( !mock230_scene_replace_loc(slot, loc_id, angle) )
            return 0;
    }
    else if( mock230_scene_add_loc(x, z, level, loc_id, shape, angle) < 0 )
    {
        return 0;
    }

    mock230_zone_loc_changed(srv, x, z, level, shape, loc_id, angle, base_id, base_angle);
    return 1;
}

/** One recorded change, put back onto a scene that has just been re-read from
 *  the cache. A record outside the new window is left alone: it stays in the
 *  ZoneMap and re-applies if the origin ever moves back. */
static void
reapply_loc(
    struct Mock230ZoneLoc* loc,
    void* user)
{
    int slot;

    (void)user;
    if( !mock230_scene_contains(loc->x, loc->z) )
        return;
    slot = mock230_scene_find_loc_exact(loc->x, loc->z, loc->level, loc->shape);
    if( loc->loc_id < 0 )
    {
        mock230_scene_remove_loc(slot);
        return;
    }
    if( mock230_scene_loc(slot) )
        mock230_scene_replace_loc(slot, loc->loc_id, loc->angle);
    else
        mock230_scene_add_loc(loc->x, loc->z, loc->level, loc->loc_id, loc->shape, loc->angle);
}

void
mock230_world_locs_reapply(struct Mock230Server* srv)
{
    mock230_zone_locs_foreach(srv, reapply_loc, NULL);
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
mock230_world_loc_reverts(struct Mock230Server* srv)
{
    for( int i = 0; i < MOCK230_LOC_REVERT_MAX; i++ )
    {
        struct Mock230LocRevert* entry = &srv->loc_reverts[i];

        if( !entry->active )
            continue;
        if( --entry->delay > 0 )
            continue;
        entry->active = 0;

        /*
         * One call for both directions. `loc_id < 0` undoes a `loc_add`, and
         * anything else puts a loc back whether the tile is currently empty
         * (a `loc_del` expiring) or holding the changed form (a `loc_change`
         * expiring) — `mock230_world_loc_set` decides which by looking, rather
         * than by remembering which opcode armed the timer.
         *
         * It can refuse, and there is one way that happens which is worth
         * saying out loud: the scene is a single 104x104 window for the whole
         * world, so a revert armed before somebody walked far enough to move the
         * origin may be aimed at a tile the scene no longer covers. The ZoneMap
         * record stands, which is the safe direction — the clients were told the
         * loc changed and it stays changed — but the timer is spent, so the loc
         * never comes back. That is the single-scene-origin limitation
         * (docs/osrs230_mockserver.md §6.1 step 1), not this table's, and it is
         * reported rather than swallowed.
         */
        if( !mock230_world_loc_set(srv, entry->x, entry->z, entry->level, entry->shape,
                                   entry->loc_id, entry->angle) &&
            srv->verbose )
            fprintf(stderr,
                    "mock230: a loc revert at %d,%d could not apply — outside the built "
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
 */
int
mock230_world_loc_revert_queue(
    struct Mock230Server* srv,
    int duration,
    int loc_id,
    int shape,
    int angle,
    int x,
    int z,
    int level)
{
    if( duration <= 0 )
        return 1;
    for( int i = 0; i < MOCK230_LOC_REVERT_MAX; i++ )
    {
        struct Mock230LocRevert* entry = &srv->loc_reverts[i];

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
    fprintf(stderr, "mock230: the loc revert table is full; %d will not change back\n",
            loc_id);
    return 0;
}

/**
 * 8. Loc reverts, obj respawns, and the zone membership reconcile.
 *
 * The reference's zone phase, in its order: the world's own timers first, then
 * the zones are brought into agreement with where everything now stands. What it
 * deliberately does *not* do any more is send anything — the per-client flush is
 * phase 10's `mock230_zone_update_player`, which is where the reference puts
 * `updateZones()` too.
 */
static void
phase_zones(struct Mock230Server* srv)
{
    mock230_world_loc_reverts(srv);
    ground_tick(srv);
    mock230_zone_sync_npcs(srv);
    mock230_zone_sync_objs(srv);
}

/**
 * 9. Recompute what the client needs to be told about.
 *
 * The rebuild check lives here rather than in phase 10 because the placement
 * PLAYER_INFO writes depends on the new origin zone, so it has to be decided
 * before any encoding starts.
 */
static void
phase_info(struct Mock230Server* srv)
{
    struct Mock230Player* player;

    /* Derived client state, recomputed before anything is encoded. Cheap, and
     * it only writes when a value actually moved — so a quiet tick sends
     * nothing. */
    MOCK230_FOR_EACH_PLAYER(srv, player)
    {
        mock230_world_set_active(srv, player);
        mock230_world_sync_combat_varbits(srv);
    }
    /* One origin for the whole world, so this is decided once and may mark
     * every player as owing a rebuild — see maybe_rebuild. */
    maybe_rebuild(srv);
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
 * Nothing is *run* here. See `mock230_scripts_queue_trigger_at` for why phase 10
 * cannot be the execution site.
 */
static void
mock230_world_update_map(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;
    int mx = player->x >> 6;
    int mz = player->z >> 6;
    int zx = player->x >> 3;
    int zz = player->z >> 3;

    if( player->last_map_x != mx || player->last_map_z != mz )
    {
        if( player->last_map_x >= 0 )
            mock230_scripts_queue_trigger_at(srv, SS_TRIGGER_MAPZONEEXIT, 0,
                                             player->last_map_x << 6, player->last_map_z << 6);
        mock230_scripts_queue_trigger_at(srv, SS_TRIGGER_MAPZONE, 0, player->x, player->z);
        player->last_map_x = mx;
        player->last_map_z = mz;
    }

    if( player->last_zone_level != player->level || player->last_zone_x != zx ||
        player->last_zone_z != zz )
    {
        if( player->last_zone_level >= 0 )
            mock230_scripts_queue_trigger_at(srv, SS_TRIGGER_ZONEEXIT, player->last_zone_level,
                                             player->last_zone_x << 3, player->last_zone_z << 3);
        mock230_scripts_queue_trigger_at(srv, SS_TRIGGER_ZONE, player->level, player->x, player->z);
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
phase_client_out(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;

    mock230_world_set_active(srv, player);

    /* "// - map update", the first thing `World.processClientsOut` does for a
     * player and the first thing done here. It only enqueues, so it is ahead of
     * every encoder below rather than entangled with them. */
    mock230_world_update_map(player);

    /* A rebuild has to reach the client before the placement that depends on
     * it, and the client's serial packet queue holds every later packet until
     * the world load finishes — so ordering here is simply "rebuild first". */
    if( player->rebuild_pending )
    {
        mock230_send_rebuild_normal(player);
        player->rebuild_pending = 0;
        /* Doors. REBUILD_NORMAL rebuilds the client's scene from the cache,
         * which puts every opened door back the way the map square has it. The
         * loop that re-sent them used to live here, walking the scene's own
         * changed list — which the rebuild had just freed, so it re-sent
         * nothing. `mock230_zone_player_reset` (in `maybe_rebuild`, and in
         * `climb`) marks every zone unloaded instead, and the zone flush below
         * re-states each one from the ZoneMap. */
        /* The scene moved under the player, so the step directions computed
         * before it are meaningless. */
        player->move_count = 0;
    }

    mock230_send_player_info(player);
    mock230_send_npc_info(player);

    /* Zone updates go here, between the entity streams and the containers,
     * because that is where the reference puts `updateZones()` — and because a
     * zone packet naming a tile is only meaningful once the client has been
     * placed by the stream above it. */
    mock230_zone_update_player(player);

    /* After the containers would have changed but before they are flushed:
     * weight is a function of what is in them, and the orb should not lag a
     * tick behind the item that changed it. */
    run_energy_flush(srv);

    /* Same argument for the bonus screen — it is a view of the worn container,
     * so it repaints on the tick that container changed rather than the next
     * time it is opened. No-op unless the screen is up. */
    if( player->worn_dirty )
        mock230_equipment_refresh_stats(srv);

    mock230_send_inv_partial(
        player,
        mock230_ids()->com_inventory_items,
        mock230_ids()->inv_backpack,
        player->inv,
        MOCK230_INV_SLOTS,
        player->inv_dirty);
    mock230_send_inv_partial(
        player,
        MOCK230_COM(mock230_ids()->iface_wornitems, 0),
        mock230_ids()->inv_worn,
        player->worn,
        MOCK230_WORN_SLOTS,
        player->worn_dirty);

    /* VARP_SMALL's value is one signed byte. A varp holding packed varbits is
     * routinely wider than that — the bank's tab counters occupy bits 0..25 of
     * their varp — so the encoder is chosen per value rather than per call
     * site. Truncating instead would corrupt every bit above the eighth. */
    /*
     * Changed varps.
     *
     * Two decisions per varp, and both are data rather than code:
     *
     * - **Whether it goes at all.** A varp the client's own CS2 reads has to
     *   reach it; one that is purely server bookkeeping must not. `transmit=`
     *   in a .varp config says which, and an *undeclared* varp is server-only —
     *   the safe default, and what keeps the mock's own counters
     *   (`mock_greeting_count`, `lumbridge_visited`) off the wire.
     * - **Which encoder.** VARP_SMALL's value is a single signed byte. Special
     *   attack energy is in tenths of a percent, so a full bar is 1000 and
     *   would land as -24. Picking by magnitude means content never has to know
     *   there are two packets.
     */
    for( int i = 0; i < player->varp_changed_count; i++ )
    {
        int varp = player->varp_changed[i];
        const struct Mock230VarpDef* def = mock230_content_varp(varp);
        int32_t value = player->varps[varp];

        if( !def || !def->transmit )
            continue;
        if( value >= -128 && value <= 127 )
            mock230_send_varp_small(player, varp, (int)value);
        else
            mock230_send_varp_large(player, varp, (int)value);
    }

    mock230_bank_flush(srv);

    /* Stats. UPDATE_STAT carries the boosted level as well as the base one, and
     * the boosted hitpoints level is what the health orb draws — so every hit
     * taken has to reach the client here, not only every level gained. */
    for( int stat = 0; stat < MOCK230_STAT_COUNT; stat++ )
    {
        if( (player->stat_dirty & (1u << stat)) == 0 )
            continue;
        mock230_send_stat(
            player,
            stat,
            player->stat_level[stat],
            player->stat_xp_tenths[stat] / 10,
            player->stat_boosted[stat]);
    }

    if( player->clear_map_flag )
        mock230_send_unset_map_flag(player);

    mock230_send_tick_end(player);
}

static void
phase_clients_out(struct Mock230Server* srv)
{
    struct Mock230Player* player;

    MOCK230_FOR_EACH_PLAYER(srv, player)
        phase_client_out(player);
}

/** 11. Drop everything that described only this tick. */
static void
phase_cleanup_player(struct Mock230Player* player)
{
    player->stat_dirty = 0;
    player->inv_dirty = 0;
    player->worn_dirty = 0;
    player->varp_changed_count = 0;
    player->clear_map_flag = 0;
    /* One social packet per tick, spent by whichever of the six arrived first.
     * The reference clears it in `resetEntity`, which is this phase. */
    player->social_protect = 0;

    /* Extended info describes one tick only. Clearing it here rather than
     * inside the encoder means a field set after PLAYER_INFO was written still
     * survives to the next tick instead of being silently dropped.
     *
     * `anim_id` goes with the mask, not with the entity. It is the incumbent
     * the priority gate compares against (mock230_anim_play_npc), and an
     * incumbent that outlives its tick is one that keeps refusing lower-priority
     * animations forever — a goblin that lands one attack would never flinch
     * again. The reference clears it in the same reset, for the same reason. */
    player->masks = 0;
    player->anim_id = -1;
    player->anim_delay = 0;
    /* With the masks, and for the same reason: an absolute placement describes
     * one tick, and every recipient's PLAYER_INFO has to have been written
     * before it is dropped. mock230_send_player_info used to clear it. */
    player->place_dirty = 0;
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
phase_cleanup(struct Mock230Server* srv)
{
    struct Mock230Player* player;

    MOCK230_FOR_EACH_PLAYER(srv, player)
        phase_cleanup_player(player);

    for( int i = 0; i < srv->npc_slot_max; i++ )
    {
        srv->npcs[i].masks = 0;
        srv->npcs[i].step_dir = -1;
        srv->npcs[i].anim_id = -1;
        srv->npcs[i].anim_delay = 0;
    }

    /* The zones' event buffers, now that every client has been given them. The
     * state they hold — loc records, obj and npc membership — is not touched. */
    mock230_zone_reset(srv);
}

void
mock230_world_tick(struct Mock230Server* srv)
{
    struct Mock230Player* player;
    /*
     * The tick borrows `active_player` and gives it back.
     *
     * Every per-player phase below moves it, so without this a caller that had
     * set it — a host between ticks, the selftest driving one player directly —
     * would find it pointing at whoever the pool happened to end on. Restoring
     * is not the same as it being meaningless outside a phase: it is, and a
     * host that reads it without having set it is reading a leftover.
     */
    struct Mock230Player* caller_active = srv->active_player;

    srv->tick++;

    phase_world(srv);
    phase_clients_in(srv);
    phase_npc_events(srv);
    phase_npcs(srv);
    phase_players(srv);
    phase_logouts(srv);
    phase_logins(srv);
    phase_zones(srv);
    /* After movement, before the info streams: the world map marker is derived
     * state, and this is the only place it can be refreshed with the tile the
     * player will actually be reported on. */
    MOCK230_FOR_EACH_PLAYER(srv, player)
    {
        mock230_world_set_active(srv, player);
        mock230_worldmap_tick(srv);
    }
    phase_info(srv);
    phase_clients_out(srv);
    phase_cleanup(srv);

    mock230_world_set_active(srv, caller_active);
}

/* ------------------------------------------------------------------ */
/* Self-test                                                           */
/* ------------------------------------------------------------------ */

static int g_selftest_failures;

/*
 * The three varps this server keeps its own bookkeeping in.
 *
 * Named here because the checks below read them out of `player->varps[]`
 * directly — the scripts address them by name, but the assertions have to index
 * the array, and a bare `varps[7]` is unreadable and unsearchable the day the
 * id moves. It has moved once: these were 1, 2 and 3, which the cache names
 * `mcannonmulti`, `dropcannon` and `rockthrower`, and 1 has twelve varbits
 * packed into it that a whole-varp write destroys. 6, 7 and 8 have no gameval
 * name and no varbit is based on them, which is the property that matters and
 * the one `configs/all.varbit` is the authority for.
 *
 * The symbol-resolution check below pins these against the pack file, so the
 * two cannot drift apart silently.
 */
enum
{
    SELFTEST_VARP_GREETING_COUNT = 6,
    SELFTEST_VARP_QUEST_PROGRESS = 7,
    SELFTEST_VARP_LUMBRIDGE_VISITED = 8
};

#define SELFTEST_CHECK(cond, ...)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "  FAIL " __VA_ARGS__);                                                \
            fprintf(stderr, "\n       (%s at %s:%d)\n", #cond, __FILE__, __LINE__);                \
            g_selftest_failures++;                                                                 \
        }                                                                                          \
    } while( 0 )

/*
 * Put the world and the player back to a known state at a chosen origin zone.
 *
 * The selftest ran `mock230_world_init` for this, back when that call *was*
 * "reset everything and put the player on the home tile". It is the world half
 * only now, and idempotent, so a section that wants a clean world has to say so
 * — which is the point: a second *login* must not reset the world, and a test
 * that wants one is a different caller with a different need.
 */
static void
selftest_reset_world(
    struct Mock230Server* srv,
    struct Mock230Player* player,
    int zone_x,
    int zone_z)
{
    mock230_world_reset(srv);
    mock230_world_init(srv, zone_x, zone_z);
    mock230_world_player_init(player);
    mock230_world_set_active(srv, player);
}

/* Find the backpack slot holding `obj_id`, or -1. */
/*
 * Put the player on a tile, standing still and out of combat.
 *
 * Every section of the selftest runs against the same server, so one that ends
 * mid-walk or mid-fight changes what the next one measures — a queued step
 * lengthens PLAYER_INFO's bit section, and an aggressive npc nearby writes
 * masks nobody asked for. Resetting is cheaper than ordering the sections.
 */
static void
selftest_park_player(
    struct Mock230Server* srv,
    int tile_x,
    int tile_z)
{
    struct Mock230Player* player = srv->active_player;

    steps_clear(player);
    player->x = tile_x;
    player->z = tile_z;
    player->level = 0;
    player->running = 0;
    player->dest_x = -1;
    player->dest_z = -1;
    player->combat_target = -1;
    player->hitpoints = player->max_hitpoints;
    for( int i = 0; i < MOCK230_NPC_MAX; i++ )
        srv->npcs[i].combat_target = -1;
    /* A section that repositions the player is starting over, so anything the
     * last one left pending goes with it. Leaving it is how one section's
     * unfinished click resolved in the middle of the next one's fight.
     *
     * The modal goes with the interaction, which is the pairing the reference
     * makes too (`clearPendingAction` = clearInteraction + closeModal). It
     * matters now that queues and timers are gated on `canAccess()`: a stanza
     * that left a dialogue page on screen was leaving the *next* stanza's player
     * permanently busy, so its queue entries were held rather than run — which
     * is correct behaviour and useless test state. */
    mock230_world_interaction_clear(srv);
    mock230_world_close_modal(srv);
    /* And the queue, which is the same argument one step further on: a section
     * that fought with a level-up box on screen leaves one held entry per hit,
     * and the next section inherits both the pile and a full array.
     * `Player.cleanup()` clears these on logout for the same reason. */
    for( int i = 0; i < MOCK230_QUEUE_MAX; i++ )
        player->queue[i].active = 0;
    /* And the engine queue, for the same reason one step further on again: the
     * last section's final zone crossing is not this section's business. The two
     * latches are deliberately NOT reset — a park is a teleport, and a teleport
     * firing exit-then-enter is the behaviour under test, not contamination. */
    for( int i = 0; i < MOCK230_ENGINE_QUEUE_MAX; i++ )
        player->engine_queue[i].active = 0;
    /* Moving the player is a teleport, and a teleport may need the scene to
     * follow. Without this the collision window and the ground-obj visibility
     * test still describe wherever the last section left off. */
    player->place_dirty = 1;
    maybe_rebuild(srv);
}

/*
 * Sweep every drop off the floor, leaving the map's own spawns alone.
 *
 * A section that counts what a kill left behind has to start from a known
 * floor, and `srv.ground[]` is world state that outlives the section that made
 * it. `is_spawn` is the line between "the map states this obj" and "something
 * dropped it": clearing the spawns as well would make the next section's first
 * question ("is the knife at 3205,3212 still there") depend on running order.
 */
static void
selftest_clear_ground(struct Mock230Server* srv)
{
    for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
    {
        if( srv->ground[i].active && !srv->ground[i].is_spawn )
            ground_clear(srv, i);
    }
}

/**
 * Tick until the pending interaction resolves.
 *
 * The interaction model means a click on something out of reach is answered by
 * a walk, not by the act — so a test that fires OPNPC/OPLOC/OPOBJ from across
 * the map has to let the player get there. Returns the ticks it took, or -1
 * when it never resolved, which is a genuine failure rather than a slow test:
 * an interaction that cannot complete is one the player is stuck on.
 */
static int
selftest_settle(
    struct Mock230Server* srv,
    int max_ticks)
{
    for( int i = 0; i < max_ticks; i++ )
    {
        if( srv->active_player->interaction.kind == MOCK230_INTERACT_NONE )
            return i;
        mock230_world_tick(srv);
    }
    return srv->active_player->interaction.kind == MOCK230_INTERACT_NONE ? max_ticks : -1;
}

/*
 * Give the player its opening kit, bank stock and stats.
 *
 * `mock230_world_init` used to do this and no longer does: what a new character
 * owns is content's, in `[proc,newplayer_setup]` (player/newplayer.rs2), and a
 * freshly initialised world now correctly has an empty backpack and an empty
 * bank. A section that wants to *test* the fixture has to ask for it.
 *
 * The seeding proc rather than the whole [login] trigger, deliberately: the
 * login burst arms interfaces and pushes a dozen varps, none of which a bank
 * arithmetic test wants in its packet capture. That [login] calls this proc at
 * all is asserted in the login section, which is where that claim belongs.
 *
 * Returns 0 when there is no compiled script pack — a skip, not a failure,
 * because a fresh checkout has no build/script.dat until `mock230-scripts`
 * runs.
 */
static int
selftest_seed_new_player(struct Mock230Server* srv)
{
    int loaded = mock230_scripts_load(srv, "OSRS-Content/osrs239-content/server/scripts/build");

    if( !loaded )
        loaded = mock230_scripts_load(srv, "../OSRS-Content/osrs239-content/server/scripts/build");
    if( !loaded )
        return 0;
    mock230_scripts_run_proc(srv, "[proc,newplayer_setup]", NULL, 0);
    mock230_scripts_free(srv);
    return 1;
}

/*
 * Find a zone sub-packet inside a captured UPDATE_ZONE_PARTIAL_ENCLOSED.
 *
 * An enclosed packet's payload is the two base bytes and then a stream of
 * sub-packets, opcode byte and all — no ISAAC, since the client resolves those
 * inner opcodes through the plain rev table. Walking it needs the lengths, and
 * they are here rather than shared with the encoder on purpose: a test that
 * asked the encoder how long its own output is would agree with itself about a
 * field it had got wrong.
 */
static int
selftest_zone_sub_length(int opcode)
{
    switch( opcode )
    {
    case 70: /* LOC_ADD_CHANGE: pos, info, id */
        return 4;
    case 71: /* LOC_DEL: pos, info */
        return 2;
    case 120: /* OBJ_ADD: pos, id, count */
        return 5;
    case 121: /* OBJ_DEL: pos, id */
        return 3;
    case 122: /* OBJ_COUNT: pos, id, old, new */
        return 7;
    default:
        return -1;
    }
}

/** 1 when some enclosed packet in the capture carries this sub-opcode. */
static int
selftest_enclosed_has(
    const struct Mock230Capture* capture,
    int sub_opcode)
{
    for( int i = 0; i < capture->count; i++ )
    {
        const struct Mock230CapturedPacket* packet = &capture->packets[i];
        int at;

        if( packet->opcode != 38 /* UPDATE_ZONE_PARTIAL_ENCLOSED */ )
            continue;
        at = 2; /* past the zone base */
        while( at < packet->len )
        {
            int sub = packet->data[at];
            int length = selftest_zone_sub_length(sub);

            if( length < 0 )
                break; /* an opcode this walk does not know: stop rather than guess */
            if( sub == sub_opcode )
                return 1;
            at += 1 + length;
        }
    }
    return 0;
}

/*
 * Click "Click here to continue" until the parked script runs out of pages.
 *
 * It clicks whatever button is *currently registered* rather than a fixed uid,
 * and that is not a convenience: a conversation alternating speakers alternates
 * interfaces too — `~chatnpc` arms `chat_left:continue` (231:5) and
 * `~chatplayer` arms `chat_right:continue` (217:5) — and a resume is matched on
 * the full 4-byte uid, so a loop hard-coding one of them silently stalls on
 * every page spoken by the other. Which is exactly what it did.
 *
 * Returns the number of clicks it took; the caller decides whether the script
 * finishing matters.
 */
static int
selftest_click_through(
    struct Mock230Server* srv,
    int max_pages)
{
    int clicks = 0;

    while( clicks < max_pages && srv->active_player->active_script )
    {
        int uid;
        uint8_t resume[4];

        if( srv->active_player->resume_button_count <= 0 )
            break;
        uid = srv->active_player->resume_buttons[0];
        resume[0] = (uint8_t)(uid >> 24);
        resume[1] = (uint8_t)(uid >> 16);
        resume[2] = (uint8_t)(uid >> 8);
        resume[3] = (uint8_t)uid;
        mock230_world_handle(srv->active_player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
        clicks++;
    }
    return clicks;
}

/** Slot holding the first active npc of this type, or -1. */
static int
selftest_find_npc(
    const struct Mock230Server* srv,
    int npc_type)
{
    for( int i = 0; i < MOCK230_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active && srv->npcs[i].type == npc_type )
            return i;
    }
    return -1;
}

static int
selftest_find(
    const struct Mock230Player* player,
    int obj_id)
{
    for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
        if( player->inv[i].obj_id == obj_id )
            return i;
    return -1;
}

/*
 * Prayer, as the selftest can reach it now that the engine has no prayer module.
 *
 * One name does both jobs, which is not a coincidence and is worth stating: a
 * prayer's `^prayer_thickskin` constant and its `prayer_thickskin` varbit are
 * the same spelling in two namespaces — the constant says *which* prayer to a
 * script, the varbit is *whether it is on*. So the three helpers below take one
 * name, and none of them knows anything about prayer beyond that.
 */
static int
selftest_prayer(const char* name)
{
    return mock230_content_constant_int(name, -1);
}

static int
selftest_prayer_on(
    struct Mock230Server* srv,
    const char* name)
{
    return mock230_varbit_get(srv->active_player,
                              mock230_content_symbol(MOCK230_PACK_VARBIT, name)) != 0;
}

/** Toggle one, through the proc the prayer book's own button calls. */
static void
selftest_prayer_toggle(
    struct Mock230Server* srv,
    const char* name)
{
    int32_t prayer = selftest_prayer(name);

    mock230_scripts_run_proc(srv, "[proc,prayer_toggle]", &prayer, 1);
}

int
mock230_world_selftest(void)
{
    struct Mock230Server srv;
    struct Mock230Player* player;
    const struct Mock230Ids* ids = mock230_ids();
    int slot = 0;

    memset(&srv, 0, sizeof(srv));
    /* No session: a world with no client. Every mock230_send still builds its
     * payload and still reaches the capture hook, then writes nothing — which
     * is what makes every encoder assertable without a socket. */
    player = mock230_world_add_player(&srv, NULL);
    mock230_seqinfo_load(MOCK230_CACHE_DIR_DEFAULT);
    mock230_world_init(&srv, 426, 408);
    mock230_world_player_init(player);

    fprintf(stderr, "mock230 selftest: ids resolve out of the content tree\n");
    {
        /*
         * Every number below was a literal in a C header until the content tree
         * started stating them, and each one was verified against cache.osrs230
         * when it was written down — with tools/dump_interface, with the
         * decompiled clientscripts, or against OpenRune's own tables. Asserting
         * that the symbols still resolve to them is what makes this a *move*
         * rather than a change: a pack regenerated from a newer gameval table
         * that renumbered something fails here, which is the whole reason the
         * ids are checkable now.
         *
         * mock230_ids_resolve is idempotent, so re-running it is how the "no
         * content tree" case shows up as a failure instead of as a server that
         * silently addresses component 0.
         */
        /*
         * The player pool's invariants.
         *
         * `world` and `session` are set by mock230_world_attach_session and must
         * survive mock230_world_init's memset of the player — losing `session`
         * leaves a logged-in player every encoder silently writes nothing for,
         * and losing `world` crashes the first encoder that reaches through it.
         * Neither is a compile error, and the second only crashes once a packet
         * is actually sent.
         */
        SELFTEST_CHECK(player == &srv.players[0],
                       "the first player takes the pool's first slot");
        SELFTEST_CHECK(srv.player_count == 1, "and the world holds one player, got %d",
                       srv.player_count);
        SELFTEST_CHECK(player->world == &srv,
                       "the player points back at its world after player_init");
        SELFTEST_CHECK(player->pid == 0, "with pid 0, got %d", player->pid);
        SELFTEST_CHECK(player->active, "and is marked live in the pool");
        SELFTEST_CHECK(player->session == NULL,
                       "and no session — this world has no client");

        SELFTEST_CHECK(mock230_ids_resolve() == 0, "every id should resolve");

        /*
         * The two namespace layers agree.
         *
         * A clean load means no authored name shadows a *different* id's cache
         * name, and nothing is both a varp and a varbit. The second rule is not
         * pedantry: `%name` resolves varp before varbit, so an alias naming the
         * varp *behind* a bank varbit made every setting the content wrote go
         * out as a whole-varp write. varp 115 also holds bank_currenttab in bits
         * 4..7 and varp 304 holds bank_requestedquantity in bits 1..31, so
         * opening the bank reset the tab and toggling insert mode discarded the
         * pending quantity — both shipped, both invisible.
         *
         * mock230_content_error_count covers the whole load, so this is also the
         * assertion that no config line was rejected.
         */
        SELFTEST_CHECK(mock230_content_error_count() == 0,
                       "the content tree should load clean, got %d error(s)",
                       mock230_content_error_count());

        /* The specific names that were aliased. Each must be a varbit — the
         * thing that patches one bit range — and not a varp. */
        SELFTEST_CHECK(
            mock230_content_symbol(MOCK230_PACK_VARP, "bank_withdrawnotes") == -1 &&
                mock230_content_symbol(MOCK230_PACK_VARBIT, "bank_withdrawnotes") >= 0,
            "bank_withdrawnotes should be a varbit only");
        SELFTEST_CHECK(mock230_content_symbol(MOCK230_PACK_VARP, "bank_insertmode") == -1 &&
                           mock230_content_symbol(MOCK230_PACK_VARBIT, "bank_insertmode") >= 0,
                       "bank_insertmode should be a varbit only");
        SELFTEST_CHECK(
            mock230_content_symbol(MOCK230_PACK_VARP, "bank_quantity_type") == -1 &&
                mock230_content_symbol(MOCK230_PACK_VARBIT, "bank_quantity_type") >= 0,
            "bank_quantity_type should be a varbit only");

        /*
         * No varp answers to a name this world made up for it.
         *
         * A pack line binds one name to one id, so an authored name does not
         * add a second spelling — it takes the cache's away. Eleven varps were
         * relabelled after whichever varbit the content cared about
         * (`varp_weapon_category` for 843, `bank_tab_a`..`bank_tab_e`,
         * `bank_quantity`), and the cost was paid twice: `randomhitsound` and
         * `prayer23` stopped resolving, and the label asserted that a shared
         * varp belonged to one subsystem. It does not — 843 carries one bank
         * varbit and 1105 carries seven varbits from four unrelated systems.
         *
         * These eight are declared transmit=yes by bank.varp and
         * combat_tab.varp and are never written by name, so the cache's own
         * spelling costs nothing and keeps the namespace one-to-one.
         */
        {
            static const struct
            {
                const char* cache_name;
                const char* invented;
                int id;
            } relabelled[] = {
                { "randomhitsound", "varp_weapon_category", 843 },
                { "prayer23", "bank_tab_a", 867 },
                { "prayer25", "bank_tab_b", 1052 },
                { "prayer26", "bank_tab_c", 1053 },
                { "wilderness_statistics", "varp_combat_level", 1105 },
                { "gargboss_perm_transmit", "bank_quantity", 1666 },
                { "bankdeposit", "bank_tab_d", 1793 },
                { "bank_extratab", "bank_tab_e", 3750 },
            };

            for( size_t i = 0; i < sizeof(relabelled) / sizeof(relabelled[0]); i++ )
            {
                SELFTEST_CHECK(mock230_content_symbol(MOCK230_PACK_VARP,
                                                      relabelled[i].cache_name) ==
                                   relabelled[i].id,
                               "the cache's name for varp %d (%s) should resolve, got %d",
                               relabelled[i].id, relabelled[i].cache_name,
                               mock230_content_symbol(MOCK230_PACK_VARP,
                                                      relabelled[i].cache_name));
                SELFTEST_CHECK(
                    mock230_content_symbol(MOCK230_PACK_VARP, relabelled[i].invented) == -1,
                    "the invented name `%s` should be gone, got %d", relabelled[i].invented,
                    mock230_content_symbol(MOCK230_PACK_VARP, relabelled[i].invented));
            }
        }

        /*
         * The three names this world really did author sit on ids the gameval
         * table leaves blank, so they shadow nothing — and, the part that was a
         * live bug, on varps no varbit is based on.
         *
         * `mock_greeting_count` used to be varp 1, which the cache calls
         * `mcannonmulti` and packs twelve Dwarf Cannon varbits into. The
         * counter is written whole (`%mock_greeting_count = calc(... + 1)`),
         * because a varp is what the content thinks it is — so saying hello to
         * Hans reset the cannon's tool, safety and railing bits together. That
         * is CONTENT_ARCHITECTURE.md §6.1 with the roles reversed: not a varbit
         * written as a varp, but scratch state parked on a varp that was
         * already somebody's varbits.
         */
        SELFTEST_CHECK(mock230_content_symbol(MOCK230_PACK_VARP, "mcannonmulti") == 1 &&
                           mock230_content_symbol(MOCK230_PACK_VARP, "dropcannon") == 2 &&
                           mock230_content_symbol(MOCK230_PACK_VARP, "rockthrower") == 3,
                       "varps 1-3 should answer to the cache's names again");
        SELFTEST_CHECK(mock230_content_symbol(MOCK230_PACK_VARP, "mock_greeting_count") ==
                               SELFTEST_VARP_GREETING_COUNT &&
                           mock230_content_symbol(MOCK230_PACK_VARP, "mock_quest_progress") ==
                               SELFTEST_VARP_QUEST_PROGRESS &&
                           mock230_content_symbol(MOCK230_PACK_VARP, "lumbridge_visited") ==
                               SELFTEST_VARP_LUMBRIDGE_VISITED,
                       "this server's scratch varps should be %d/%d/%d, got %d/%d/%d",
                       SELFTEST_VARP_GREETING_COUNT, SELFTEST_VARP_QUEST_PROGRESS,
                       SELFTEST_VARP_LUMBRIDGE_VISITED,
                       mock230_content_symbol(MOCK230_PACK_VARP, "mock_greeting_count"),
                       mock230_content_symbol(MOCK230_PACK_VARP, "mock_quest_progress"),
                       mock230_content_symbol(MOCK230_PACK_VARP, "lumbridge_visited"));

        SELFTEST_CHECK(ids->iface_gameframe == 161, "gameframe should be 161, got %d",
                       ids->iface_gameframe);
        SELFTEST_CHECK(ids->iface_bankmain == 12 && ids->iface_bankside == 15,
                       "the bank should be 12/15, got %d/%d", ids->iface_bankmain,
                       ids->iface_bankside);
        SELFTEST_CHECK(ids->iface_equipment_stats == 84 && ids->iface_prayerbook == 541,
                       "equipment stats should be 84 and the prayer book 541, got %d/%d",
                       ids->iface_equipment_stats, ids->iface_prayerbook);
        SELFTEST_CHECK(ids->inv_backpack == 93 && ids->inv_worn == 94 && ids->inv_bank == 95,
                       "the three containers should be 93/94/95, got %d/%d/%d",
                       ids->inv_backpack, ids->inv_worn, ids->inv_bank);

        SELFTEST_CHECK(MOCK230_COM_CHILD(ids->com_gameframe_mainmodal) == 16 &&
                           MOCK230_COM_CHILD(ids->com_gameframe_sidemodal) == 74,
                       "the two modal slots should be 161:16 and 161:74, got %d/%d",
                       MOCK230_COM_CHILD(ids->com_gameframe_mainmodal),
                       MOCK230_COM_CHILD(ids->com_gameframe_sidemodal));
        /*
         * 12:12, not the 12:13 this used to pin.
         *
         * The old number came from a name table belonging to another server at
         * a newer revision; this cache's own gameval table (archive 14) calls
         * child 12 `items` and child 13 `scrollbar`. Every bank component here
         * was off by the same kind of drift — the quantity buttons pointed at
         * the `_text` labels beside them, which carry no op at all.
         */
        SELFTEST_CHECK(ids->com_bankmain_items == MOCK230_COM(12, 12) &&
                           ids->com_bankside_items == MOCK230_COM(15, 3),
                       "the two item grids should be 12:12 and 15:3");
        SELFTEST_CHECK(MOCK230_COM_CHILD(ids->com_bankmain_qty_1) == 29 &&
                           MOCK230_COM_CHILD(ids->com_bankmain_deposit_inv) == 47,
                       "the bank buttons should be the components carrying op1: "
                       "12:29 `quantity1` and 12:47 `depositinv`, got 12:%d and 12:%d",
                       MOCK230_COM_CHILD(ids->com_bankmain_qty_1),
                       MOCK230_COM_CHILD(ids->com_bankmain_deposit_inv));
        SELFTEST_CHECK(ids->com_worn_equipment_stats == MOCK230_COM(387, 1),
                       "the stats button should be 387:1");
        SELFTEST_CHECK(ids->com_equipment_stats_stabatt == MOCK230_COM(84, 24) &&
                           ids->com_equipment_stats_attackspeedactual == MOCK230_COM(84, 54),
                       "the stats rows should run 84:24..84:54");
        SELFTEST_CHECK(ids->varbit_bank_withdrawnotes == 3958 &&
                           ids->varbit_bank_currenttab == 4150,
                       "the bank varbits should be 3958/4150, got %d/%d",
                       ids->varbit_bank_withdrawnotes, ids->varbit_bank_currenttab);
        SELFTEST_CHECK(ids->bank_qty_1 == 0 && ids->bank_qty_all == 4,
                       "the quantity modes should run 0..4, got %d..%d", ids->bank_qty_1,
                       ids->bank_qty_all);

        /* The content table that replaced a C array: the worn slots, which were
         * never a straight run — the tab's eleven cells stand for wear slots
         * 0..5, 7, 9, 10, 12, 13.
         *
         * Prayer used to be checked here too, off `mock230_content_prayer()`.
         * The engine has no prayer table to check any more; what is left that
         * the engine can see is the two names both ends have to agree on, and
         * the prayer selftest below drives the rest through content. */
        SELFTEST_CHECK(selftest_prayer("prayer_count") == 29,
                       "the tree should declare 29 prayers, got %d",
                       selftest_prayer("prayer_count"));
        SELFTEST_CHECK(mock230_content_symbol(MOCK230_PACK_COMPONENT, "prayerbook:prayer1") ==
                           MOCK230_COM(541, 9),
                       "the first prayer button should be 541:9");
        SELFTEST_CHECK(mock230_equipment_worn_slot(MOCK230_COM(387, 15)) == MOCK230_WEAR_HEAD,
                       "387:15 should be the helmet slot, got %d",
                       mock230_equipment_worn_slot(MOCK230_COM(387, 15)));
        SELFTEST_CHECK(mock230_equipment_worn_slot(MOCK230_COM(387, 21)) == MOCK230_WEAR_LEGS,
                       "387:21 should be the legs slot (6 and 8 are skipped), got %d",
                       mock230_equipment_worn_slot(MOCK230_COM(387, 21)));
        SELFTEST_CHECK(mock230_equipment_worn_slot(MOCK230_COM(387, 26)) < 0,
                       "and 387:26 is not a slot at all");

        {
            const struct Mock230EnumDef* tabs = mock230_content_enum("bank_tabs");

            SELFTEST_CHECK(tabs && tabs->count == MOCK230_BANK_TABS,
                           "the bank_tabs enum should list %d tabs, got %d",
                           MOCK230_BANK_TABS, tabs ? tabs->count : -1);
            SELFTEST_CHECK(tabs && tabs->values[0].value == 4171,
                           "tab 1 should be varbit 4171");
        }
    }

    fprintf(stderr, "mock230 selftest: packet capture\n");
    {
        /* Proves the harness before anything depends on it. Every later case
         * that asserts on output is only as trustworthy as this one. */
        static struct Mock230Capture capture;
        static const int k_expected[] = { 23 /* PLAYER_INFO */, 104 /* NPC_INFO */,
                                          108 /* SERVER_TICK_END */ };
        int tick_end;

        mock230_capture_begin(&srv, &capture);
        mock230_world_tick(&srv);
        mock230_capture_end(&srv);

        SELFTEST_CHECK(capture.count > 0, "a tick should produce packets, got %d",
                       capture.count);
        SELFTEST_CHECK(!capture.overflow, "the capture buffer overflowed");
        SELFTEST_CHECK(
            mock230_capture_has_sequence(&capture, k_expected, 3),
            "a tick should emit PLAYER_INFO, then NPC_INFO, then SERVER_TICK_END");

        /* SERVER_TICK_END closes the tick, so nothing may follow it. */
        tick_end = mock230_capture_find(&capture, 108, 0);
        SELFTEST_CHECK(tick_end == capture.count - 1,
                       "SERVER_TICK_END should be last, was %d of %d", tick_end,
                       capture.count);
    }

    fprintf(stderr, "mock230 selftest: script-driven triggers\n");
    {
        static struct Mock230Capture capture;
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            /* Running with no content is a supported mode, so this is a skip
             * rather than a failure — but the fallback below still has to
             * hold, which the OPNPC case above already covered. */
            fprintf(stderr, "  SKIP  no compiled script pack (run: make -C src mock230-scripts)\n");
        }
        else
        {
            /*
             * Every opcode this tree uses is implemented.
             *
             * A regression here means content was written against an opcode the
             * engine does not have — which otherwise fails at the moment a
             * player triggers that script, possibly never during a test run.
             * The report names each one and the first script wanting it.
             */
            SELFTEST_CHECK(mock230_scripts_report_gaps(&srv) == 0,
                           "the content tree should not use unimplemented opcodes");
            uint8_t payload[2];
            int before;
            int hans;

            /* [login,_] should run in phase 7, not during the login burst. */
            player->login_pending = 1;
            mock230_capture_begin(&srv, &capture);
            mock230_world_tick(&srv);
            mock230_capture_end(&srv);
            SELFTEST_CHECK(mock230_capture_find(&capture, 90 /* MESSAGE_GAME */, 0) >= 0,
                           "[login] should produce a game message");
            SELFTEST_CHECK(player->login_pending == 0, "the login latch should be drained");

            /*
             * The opening fixture, which [login] seeds through
             * `~newplayer_setup` and mock230_world_init deliberately no longer
             * does. Three assertions because the three moved separately and
             * fail separately: the backpack, the bank behind the API that used
             * to be written past, and the stat block.
             */
            {
                int kit = 0;
                int hitpoints_xp = player->stat_xp_tenths[MOCK230_STAT_HITPOINTS];

                for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                    if( player->inv[i].obj_id >= 0 )
                        kit++;
                SELFTEST_CHECK(kit == 14,
                               "[login] should deal the 14-item opening kit, got %d", kit);
                SELFTEST_CHECK(mock230_bank_count(&srv, 995) == 250000,
                               "and stock the bank with 250000 coins, got %d",
                               mock230_bank_count(&srv, 995));
                SELFTEST_CHECK(player->stat_level[MOCK230_STAT_HITPOINTS] == 10 &&
                                   hitpoints_xp == 11540,
                               "and put hitpoints at level 10 / 1154 xp, got %d / %d",
                               player->stat_level[MOCK230_STAT_HITPOINTS], hitpoints_xp);

                /* Idempotent: the varp gate is what will keep a returning
                 * player's save from being re-seeded once mock230_save.c is
                 * finally called by something. */
                mock230_scripts_run_proc(&srv, "[proc,newplayer_setup]", NULL, 0);
                kit = 0;
                for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                    if( player->inv[i].obj_id >= 0 )
                        kit++;
                SELFTEST_CHECK(kit == 14,
                               "running the seed twice should deal nothing more, got %d",
                               kit);
            }

            /* [opnpc1,hans] replaces the hardcoded greeting and bumps a varp,
             * so both the script's effect and the varp flush are observable. */
            hans = selftest_find_npc(&srv, 3105);
            SELFTEST_CHECK(hans >= 0, "the roster should include Hans");
            before = player->varps[SELFTEST_VARP_GREETING_COUNT];
            payload[0] = (uint8_t)(hans >> 8);
            payload[1] = (uint8_t)(hans & 0xff);
            mock230_capture_begin(&srv, &capture);
            mock230_world_handle(player, PKTOUT_NAME_OPNPC1, payload, 2);
            /* The click starts a walk; the script runs when the player gets
             * there. Hans is across the courtyard, so that is several ticks. */
            SELFTEST_CHECK(selftest_settle(&srv, 40) >= 0,
                           "the walk to Hans should complete");
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_GREETING_COUNT] == before + 1,
                           "the script should bump %%mock_greeting_count, got %d",
                           player->varps[SELFTEST_VARP_GREETING_COUNT]);
            /* Hans's greeting is a four-page conversation, so the script is
             * still parked on its first p_pausebutton here. Both halves matter:
             * the varp above proves the script ran, this proves it suspended
             * rather than falling off the end. */
            SELFTEST_CHECK(player->active_script != NULL,
                           "[opnpc1,hans] should park on its first dialogue page");

            mock230_world_tick(&srv);
            mock230_capture_end(&srv);
            /*
             * `mock_greeting_count` is server bookkeeping: no .varp config
             * declares it, so it must NOT reach the client. That is the point
             * of the transmit gate — a counter nothing client-side reads costs
             * a packet per change and invites the client to react to it.
             *
             * The transmitted case is asserted in the login-burst section,
             * against a varp that really is declared.
             */
            SELFTEST_CHECK(mock230_content_varp(SELFTEST_VARP_GREETING_COUNT) == NULL,
                           "mock_greeting_count should have no varp declaration");
            SELFTEST_CHECK(mock230_capture_find(&capture, 35 /* VARP_SMALL */, 0) < 0,
                           "an undeclared varp must stay off the wire");

            /* A trigger with no script must fall through to the C behaviour,
             * which is what keeps the mock usable without content. */
            SELFTEST_CHECK(
                mock230_scripts_run_trigger(&srv, SS_TRIGGER_OPNPC5, 3105, -1, 0) == 0,
                "an unbound trigger should report that nothing ran");

            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: clicking away ends a conversation\n");
    {
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            uint8_t payload[2];
            uint8_t move[5];
            int hans;

            hans = selftest_find_npc(&srv, 3105);
            SELFTEST_CHECK(hans >= 0, "the roster should include Hans");

            payload[0] = (uint8_t)(hans >> 8);
            payload[1] = (uint8_t)(hans & 0xff);
            mock230_world_handle(player, PKTOUT_NAME_OPNPC1, payload, 2);
            SELFTEST_CHECK(selftest_settle(&srv, 40) >= 0, "the walk to Hans should complete");

            /* The precondition. Both halves are asserted because the fix has to
             * undo both: the parked script is the conversation, `chatmodal_group`
             * is the interface it put on the screen, and either one left behind
             * is a visible bug of its own. */
            SELFTEST_CHECK(player->active_script != NULL,
                           "talking to Hans should park a script on its first page");
            SELFTEST_CHECK(player->chatmodal_group > 0,
                           "and mount a chat interface, got %d", player->chatmodal_group);

            /* Click the ground mid-sentence. */
            move[0] = 0;
            move[1] = (uint8_t)((player->x + 3) >> 8);
            move[2] = (uint8_t)(player->x + 3);
            move[3] = (uint8_t)(player->z >> 8);
            move[4] = (uint8_t)player->z;
            mock230_world_handle(player, PKTOUT_NAME_MOVE_GAMECLICK, move, 5);

            SELFTEST_CHECK(player->active_script == NULL,
                           "walking away should end the parked conversation");
            SELFTEST_CHECK(player->chatmodal_group == 0,
                           "and take its interface down, got %d", player->chatmodal_group);
            SELFTEST_CHECK(player->resume_button_count == 0,
                           "and disarm its continue button, got %d",
                           player->resume_button_count);
            SELFTEST_CHECK(player->interaction.kind == MOCK230_INTERACT_NONE,
                           "and retire the pending op, got kind %d",
                           (int)player->interaction.kind);

            /*
             * The reason this matters beyond the screen: the player has exactly
             * one script slot, so a conversation left parked blocks every later
             * one. Talking to Hans again has to work.
             */
            mock230_world_handle(player, PKTOUT_NAME_OPNPC1, payload, 2);
            SELFTEST_CHECK(selftest_settle(&srv, 40) >= 0, "the walk back should complete");
            SELFTEST_CHECK(player->active_script != NULL,
                           "and a second conversation should still be able to park");
            /*
             * Exactly one, and that number is the whole assertion.
             *
             * "Non-null" cannot tell the two worlds apart: leave the first
             * conversation parked and it is still sitting in the slot, so the
             * pointer is non-null either way. The button list is what separates
             * them — the second script arms its continue row on top of a list
             * the click-away was supposed to have emptied, so the bug shows up
             * here as 2 rather than as a missing script.
             */
            SELFTEST_CHECK(player->resume_button_count == 1,
                           "on a continue button armed from an empty list, got %d",
                           player->resume_button_count);

            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: script suspension\n");
    {
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            const struct SSVM_Script* script;
            int start_tick;

            /* p_delay(3) at tick T resumes at T+4: the reference sets
             * delayedUntil = tick + 1 + n, so a delay of n costs the rest of
             * this tick plus n more. Getting the +1 wrong is a one-tick error
             * that nothing else in the system would notice. */
            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_delay]");
            SELFTEST_CHECK(script != NULL, "[proc,selftest_delay] should be in the pack");
            if( script )
            {
                start_tick = srv.tick;
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
                mock230_scripts_run_script(&srv, script->id);

                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1, "the script ran up to the delay");
                SELFTEST_CHECK(player->active_script != NULL,
                               "p_delay should park the script on the player");

                for( int i = 0; i < 3; i++ )
                {
                    mock230_world_tick(&srv);
                    SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1,
                                   "still delayed at tick +%d, varp is %d", i + 1,
                                   player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
                }
                mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 2,
                               "p_delay(3) should resume on tick +4, varp is %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
                SELFTEST_CHECK(player->active_script == NULL,
                               "a finished script should release its parking slot");
                SELFTEST_CHECK(srv.tick == start_tick + 4, "four ticks elapsed");
            }

            /* queue(script, 3, 0) runs on tick +4 for the same reason. */
            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_enqueue]");
            if( script )
            {
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0, "queueing should not run the script");

                for( int i = 0; i < 3; i++ )
                {
                    mock230_world_tick(&srv);
                    SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0, "queue not due at tick +%d", i + 1);
                }
                mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 7,
                               "the queued script should run on tick +4, varp is %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            }

            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: npc chat dialogue\n");
    {
        static struct Mock230Capture capture;
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            /* IF_SETNPCHEAD, IF_SETANIM, three IF_SETTEXTs, IF_OPENSUB. No
             * unhide: `chatbox:chatmodal` ships hidden, and revealing it is
             * the client's own script908 reacting to the mount — see
             * Mock230Ids.com_chatbox_modal. */
            static const int k_dialogue[] = { 95, 97, 94, 6 };
            int hans = selftest_find_npc(&srv, 3105);
            int continue_uid = (231 << 16) | 5;
            uint8_t payload[2] = { (uint8_t)(hans >> 8), (uint8_t)(hans & 0xff) };
            uint8_t resume[4];

            mock230_capture_begin(&srv, &capture);
            mock230_world_handle(player, PKTOUT_NAME_OPNPC3, payload, 2);
            SELFTEST_CHECK(selftest_settle(&srv, 40) >= 0,
                           "the walk to Hans should complete");
            mock230_capture_end(&srv);

            SELFTEST_CHECK(mock230_capture_has_sequence(&capture, k_dialogue, 4),
                           "a dialogue should set the head, anim and text, then mount");
            SELFTEST_CHECK(player->active_script != NULL,
                           "p_pausebutton should park the script");
            SELFTEST_CHECK(player->resume_button_count == 1,
                           "the continue button should be registered, got %d",
                           player->resume_button_count);

            /* A click on some other interface's component 5 must NOT release
             * the wait. This is what the 4-byte uid buys: at 2 bytes every
             * interface's component 5 looks identical on the wire. */
            resume[0] = (uint8_t)(((217 << 16) | 5) >> 24);
            resume[1] = (uint8_t)(((217 << 16) | 5) >> 16);
            resume[2] = (uint8_t)(((217 << 16) | 5) >> 8);
            resume[3] = (uint8_t)((217 << 16) | 5);
            mock230_world_handle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
            SELFTEST_CHECK(player->active_script != NULL,
                           "an unregistered button must leave the script parked");

            /* The registered one advances to page 2. */
            resume[0] = (uint8_t)(continue_uid >> 24);
            resume[1] = (uint8_t)(continue_uid >> 16);
            resume[2] = (uint8_t)(continue_uid >> 8);
            resume[3] = (uint8_t)continue_uid;

            mock230_capture_begin(&srv, &capture);
            mock230_world_handle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
            mock230_capture_end(&srv);
            SELFTEST_CHECK(mock230_capture_find(&capture, 94 /* IF_SETTEXT */, 0) >= 0,
                           "clicking continue should draw the next page");
            SELFTEST_CHECK(player->active_script != NULL, "and park again on page 2");
            SELFTEST_CHECK(player->last_com == continue_uid,
                           "last_com should name the button that resumed it");

            /* Page 2 is the last one Hans's "Age" reply has, so the next click
             * runs off the end of the script and into its if_close. */
            mock230_capture_begin(&srv, &capture);
            mock230_world_handle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
            mock230_capture_end(&srv);
            SELFTEST_CHECK(player->active_script == NULL,
                           "the script should finish after the last page");
            SELFTEST_CHECK(mock230_capture_find(&capture, 36 /* IF_CLOSESUB */, 0) >= 0,
                           "if_close should close the dialogue");
            SELFTEST_CHECK(player->resume_button_count == 0,
                           "and drop its resume buttons");

            /*
             * The multiple-choice dialogue — Hans's "Talk-to", which is a
             * three-way `~p_choice3`.
             *
             * Two things are worth pinning and neither is visible from the
             * packets alone.
             *
             * **The options reach the client at all.** rev 230's `chatmenu`
             * ships two components and no rows; the five are `cc_create`d by
             * the clientscript `chatbox_multi_init`, so the only way the server
             * can put a choice on screen is RUNCLIENTSCRIPT with *string*
             * arguments. Sending anything else produces a correctly mounted,
             * completely empty dialogue.
             *
             * **The answer is the sub-id.** All the rows are children of one
             * component, so the reference's `last_com` switch cannot work here;
             * the branch is chosen by `last_slot`. A resume that ignored the
             * sub-id would take branch 1 whichever row was clicked — three
             * different conversations, one of which is right, and no error.
             *
             * Row 3 is picked deliberately: it is the branch a wrong-sub-id
             * implementation could not reach, and it ends in dialogue rather
             * than in Hans running away.
             */
            {
                int hans_slot = selftest_find_npc(&srv, 3105);
                uint8_t opnpc[2] = { (uint8_t)(hans_slot >> 8), (uint8_t)(hans_slot & 0xff) };
                int rows_uid = mock230_content_symbol(MOCK230_PACK_COMPONENT, "chatmenu:options");
                uint8_t button[6];

                SELFTEST_CHECK(rows_uid > 0, "the content pack should name chatmenu:options");

                mock230_capture_begin(&srv, &capture);
                mock230_world_handle(player, PKTOUT_NAME_OPNPC1, opnpc, 2);
                SELFTEST_CHECK(selftest_settle(&srv, 40) >= 0, "the walk to Hans should complete");
                mock230_capture_end(&srv);

                /* Page one is an ordinary chatnpc; clicking through it is what
                 * runs `~p_choice3`, so the capture has to wrap the RESUME —
                 * the RUNCLIENTSCRIPT goes out inside that call, not on the
                 * next tick. */
                mock230_capture_begin(&srv, &capture);
                mock230_world_handle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
                mock230_capture_end(&srv);
                SELFTEST_CHECK(player->active_script != NULL,
                               "p_choice3 should park on p_pausebutton");

                /*
                 * The option list reaches the wire whole.
                 *
                 * Three separate caps sit on this one string — the packet
                 * parser's, the client's CS2-task buffer, and the encoder's
                 * packet size — and each hid the next until they were raised in
                 * step. Truncation does not fail: the clientscript splits on
                 * `|`, counts what survived, and lays out that many rows, so a
                 * three-option question renders as a tidy two-option one with
                 * the second cut off mid-word. Nothing reports it at any layer.
                 *
                 * Asserted as a length rather than by parsing the payload,
                 * because the number is the whole point — the shortest of
                 * Hans's three options is 35 characters and the list is 132.
                 */
                {
                    int idx = mock230_capture_find(&capture, 84 /* RUNCLIENTSCRIPT */, 0);

                    SELFTEST_CHECK(idx >= 0, "the choice should send RUNCLIENTSCRIPT");
                    if( idx >= 0 )
                    {
                        /*
                         * Searched for the LAST option's tail, not measured as a
                         * byte count. A length threshold is the obvious check
                         * and a bad one: truncating the 132-character list at
                         * 128 still leaves a 152-byte packet, so any threshold
                         * loose enough to be safe is loose enough to pass the
                         * bug. What truncation removes is the end of the string,
                         * so that is what to look for.
                         */
                        const struct Mock230CapturedPacket* pkt = &capture.packets[idx];
                        const char* tail = "Where am I?";
                        size_t tail_len = strlen(tail);
                        int found = 0;

                        for( int at = 0; at + (int)tail_len <= pkt->len && !found; at++ )
                        {
                            if( memcmp(pkt->data + at, tail, tail_len) == 0 )
                                found = 1;
                        }
                        SELFTEST_CHECK(found,
                                       "the third option should survive to the wire "
                                       "(%d-byte payload)",
                                       pkt->len);
                    }
                }

                if( rows_uid > 0 && player->active_script != NULL )
                {
                    /* IF_BUTTON1 on the container, sub = the row. */
                    button[0] = (uint8_t)(rows_uid >> 24);
                    button[1] = (uint8_t)(rows_uid >> 16);
                    button[2] = (uint8_t)(rows_uid >> 8);
                    button[3] = (uint8_t)rows_uid;
                    button[4] = 0;
                    button[5] = 3; /* the third option */

                    mock230_world_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
                    SELFTEST_CHECK(player->last_slot == 3,
                                   "the clicked row should arrive as last_slot, got %d",
                                   player->last_slot);
                    SELFTEST_CHECK(player->active_script != NULL,
                                   "and the conversation should continue into branch 3");
                }
                /* Whatever branch it is on, drain it so the sections after this
                 * one start from an idle player. */
                for( int i = 0; i < 12 && player->active_script; i++ )
                    mock230_world_handle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
                player->resume_button_count = 0;
            }

            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: emotes\n");
    {
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            /*
             * The emotes tab is entirely client-built: interface 216's onload
             * creates one cell per emote with the emote INDEX as the sub-id. So
             * the server's whole contribution is turning a sub-id into an
             * animation, and the failure worth catching is an index that maps to
             * the wrong emote — which on screen is a player doing a jig when
             * they asked to cry, and looks like an animation bug.
             *
             * Checked against the cache's own sequence names rather than
             * numbers: the content names `emote_wave`, so this asks the cache
             * what `emote_wave` is and requires the click to produce it.
             */
            static const struct
            {
                int index;
                const char* seq;
                const char* label;
            } k_emotes[] = {
                { 0, "emote_yes", "Yes" },
                { 5, "emote_wave", "Wave" },
                { 13, "emote_dance_scottish", "Jig" },
                { 16, "emote_cry", "Cry" },
                { 19, "emote_ya_boo_sucks", "Raspberry" },
                { 27, "emote_glass_wall", "Glass Wall" },
                { 39, "emote_run_on_spot", "Jog" },
            };
            int contents = mock230_content_symbol(MOCK230_PACK_COMPONENT, "emote:contents");
            uint8_t button[6];

            SELFTEST_CHECK(contents > 0, "the content pack should name emote:contents");

            button[0] = (uint8_t)(contents >> 24);
            button[1] = (uint8_t)(contents >> 16);
            button[2] = (uint8_t)(contents >> 8);
            button[3] = (uint8_t)contents;

            for( size_t i = 0; i < sizeof(k_emotes) / sizeof(k_emotes[0]); i++ )
            {
                int want = mock230_seq_by_name(k_emotes[i].seq);

                button[4] = (uint8_t)(k_emotes[i].index >> 8);
                button[5] = (uint8_t)k_emotes[i].index;
                player->anim_id = -1;
                mock230_world_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));

                SELFTEST_CHECK(want > 0, "the cache should name `%s`", k_emotes[i].seq);
                SELFTEST_CHECK(player->anim_id == want,
                               "emote %d (%s) should play %s = %d, got %d", k_emotes[i].index,
                               k_emotes[i].label, k_emotes[i].seq, want, player->anim_id);
            }

            /*
             * An index the cache draws and this world cannot animate says so
             * rather than doing nothing. 48 is the Premier Shield, which has no
             * sequence here — a silent no-op would be indistinguishable from the
             * click never arriving.
             */
            button[4] = 0;
            button[5] = 48;
            player->anim_id = -1;
            mock230_world_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
            SELFTEST_CHECK(player->anim_id == -1,
                           "an unmodelled emote should play nothing, got %d", player->anim_id);

            player->anim_id = -1;
            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: held-item content\n");
    {
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            const struct Mock230Ids* ids = mock230_ids();
            uint8_t held[8];
            int bones = mock230_content_symbol(MOCK230_PACK_OBJ, "bones");
            int meat = mock230_content_symbol(MOCK230_PACK_OBJ, "cooked_meat");
            int prayer_xp;
            /* This section rewrites the backpack, the worn slots and the
             * hitpoints stat, and the equipment sections after it are written
             * against the starting kit. Snapshot and put it all back. */
            static struct Mock230Item saved_inv[MOCK230_INV_SLOTS];
            static struct Mock230Item saved_worn[MOCK230_WORN_SLOTS];
            int saved_hp = player->hitpoints;
            int saved_hp_level = player->stat_level[MOCK230_STAT_HITPOINTS];

            memcpy(saved_inv, player->inv, sizeof(saved_inv));
            memcpy(saved_worn, player->worn, sizeof(saved_worn));

            SELFTEST_CHECK(bones > 0 && meat > 0, "bones and cooked_meat should be in obj.pack");
            SELFTEST_CHECK(mock230_objinfo(bones)->category == 6,
                           "bones should be category 6, got %d",
                           mock230_objinfo(bones)->category);

            /*
             * The npc rung of the same lookup — triage §9 step 3b.
             *
             * It did not exist: every npc call site passed -1 because a comment
             * in this file said an osrs239 npc record "carries no category at
             * all… absent". The cache states one on 9,149 of its 16,292 npc
             * records; it was unread. What this asserts is the whole chain in
             * one line — the decoder reads config opcode 18, mock230_npcinfo
             * keeps it, and the id it kept is the id the crawl read out of that
             * record's own group when it minted the name in
             * `pack/category.pack`. Either half moving breaks it.
             *
             * `chicken` and not `goblin`: goblin's 311 is one of the ids the
             * crawl deliberately did *not* name (see port/categories.map), so it
             * has no second side to compare against.
             */
            {
                int chicken = mock230_content_symbol(MOCK230_PACK_NPC, "chicken");
                int chicken_category =
                    mock230_content_symbol(MOCK230_PACK_CATEGORY, "chicken");
                int nameless = -1;

                SELFTEST_CHECK(chicken > 0 && chicken_category > 0,
                               "chicken should be in both the npc and the category pack");
                SELFTEST_CHECK(mock230_npc_category(chicken) == chicken_category,
                               "the chicken npc's category rung should read %d, got %d",
                               chicken_category, mock230_npc_category(chicken));

                /*
                 * And that the resolver does not go through `mock230_npcinfo`,
                 * which hides a *nameless* record's whole row. 1,585 of this
                 * cache's categorised npc records have no name — the multinpc
                 * instances are all of them — so reading the category through
                 * the accessor would answer "no category" for every one, and
                 * answer it silently. There has to be at least one.
                 */
                for( int id = 0; id < 16292 && nameless < 0; id++ )
                {
                    if( !mock230_npcinfo_known(id) && mock230_npc_category(id) > 0 )
                        nameless = id;
                }
                SELFTEST_CHECK(nameless >= 0,
                               "a nameless npc record should still answer its category");
            }

            /*
             * The server-allocated config rung — triage §9 step 3c.
             *
             * `sheep_table` and its four rows are the first records ported into
             * the five namespaces whose ids are *ours*, and what is worth pinning
             * is the whole allocation chain rather than any one number:
             * `tools/ss_allocate.py` gave the table an id off layer 0's
             * high-water mark, `configs/all.dbtable.compack` records it,
             * `mock230_db.c` looks it up by that id, and the row's `npc` and
             * `namedobj` columns hold ids it resolved through the *packs* — not
             * ids copied from the reference, where the same names are npc 1379
             * and obj 1929.
             *
             * The last check is the one that would have caught the failure this
             * step's gate exists for. A column whose declared type this runtime
             * cannot resolve used to fall through to `atoi()`, so a name in it
             * became 0 and nothing said so; asserting the value equals the pack's
             * id is the difference between "the row loaded" and "the row loaded
             * the right thing".
             */
            {
                int table_id = mock230_content_symbol(MOCK230_PACK_DBTABLE, "sheep_table");
                const struct Mock230DbTable* table = mock230_db_table(table_id);
                const struct Mock230DbRow* row = mock230_db_row_in_table(table_id, 0);
                int npc_column = table ? mock230_db_column_index(table, "sheep_type") : -1;
                int obj_column = table ? mock230_db_column_index(table, "sheep_bones") : -1;

                SELFTEST_CHECK(table_id >= 259,
                               "sheep_table should hold a server-allocated dbtable id, got %d",
                               table_id);
                SELFTEST_CHECK(table && row, "sheep_table should have loaded with rows");
                SELFTEST_CHECK(mock230_db_row_count(table_id) == 4,
                               "sheep_table should hold 4 rows, got %d",
                               mock230_db_row_count(table_id));
                if( table && row && npc_column >= 0 && obj_column >= 0 )
                {
                    int npc_id = mock230_content_symbol(MOCK230_PACK_NPC,
                                                        "herder_plaguesheep_1");
                    int obj_id = mock230_content_symbol(MOCK230_PACK_OBJ, "sheepbonesa");

                    SELFTEST_CHECK(row->columns[npc_column].values[0].value == npc_id,
                                   "the row's sheep_type should be npc %d, got %d", npc_id,
                                   row->columns[npc_column].values[0].value);
                    SELFTEST_CHECK(row->columns[obj_column].values[0].value == obj_id,
                                   "the row's sheep_bones should be obj %d, got %d", obj_id,
                                   row->columns[obj_column].values[0].value);
                }
            }

            /*
             * Bury: `[opheld1,_bones]` is bound to the obj *category*, not to
             * the obj, so what this really asserts is that a category-addressed
             * trigger resolves at all. It is the only one in the tree.
             */
            for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                inv_set(player, i, -1, 0);
            inv_set(player, 0, bones, 1);
            player->stat_xp_tenths[MOCK230_STAT_PRAYER] = 0;
            prayer_xp = player->stat_xp_tenths[MOCK230_STAT_PRAYER];

            held[0] = (uint8_t)(bones >> 8);
            held[1] = (uint8_t)(bones & 0xff);
            held[2] = 0;
            held[3] = 0; /* slot 0 */
            held[4] = (uint8_t)(ids->com_inventory_items >> 24);
            held[5] = (uint8_t)(ids->com_inventory_items >> 16);
            held[6] = (uint8_t)(ids->com_inventory_items >> 8);
            held[7] = (uint8_t)ids->com_inventory_items;
            mock230_world_handle(player, PKTOUT_NAME_OPHELD1, held, 8);

            /* p_delay(0) parks the script for the rest of this tick, which is
             * why the effects land on the next one rather than immediately. */
            SELFTEST_CHECK(player->active_script != NULL, "bury should park on its p_delay");
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->inv[0].obj_id == -1,
                           "inv_delslot should empty the bones slot, got %d",
                           player->inv[0].obj_id);
            SELFTEST_CHECK(player->stat_xp_tenths[MOCK230_STAT_PRAYER] == prayer_xp + 450,
                           "burying bones should award 45 prayer experience, got %d",
                           player->stat_xp_tenths[MOCK230_STAT_PRAYER] - prayer_xp);

            /*
             * Eat: same trigger, a different category, and the clamping half of
             * stat_heal — a heal never exceeds the base level, so eating at
             * full health costs the food and heals nothing.
             */
            player->stat_level[MOCK230_STAT_HITPOINTS] = 10;
            player->hitpoints = 4;
            mock230_combat_stat_mark(player, MOCK230_STAT_HITPOINTS);
            player->stat_boosted[MOCK230_STAT_HITPOINTS] = 4;
            inv_set(player, 0, meat, 1);
            held[0] = (uint8_t)(meat >> 8);
            held[1] = (uint8_t)(meat & 0xff);
            mock230_world_handle(player, PKTOUT_NAME_OPHELD1, held, 8);
            SELFTEST_CHECK(player->hitpoints == 7,
                           "cooked meat should heal 3, got %d", player->hitpoints);
            SELFTEST_CHECK(player->inv[0].obj_id == -1, "and be eaten");

            /*
             * `~eat_food` ends in `p_delay(^eat_delay)`, so the script above is
             * still parked and a second one sent this tick would be *dropped* —
             * which used to make this check pass for the wrong reason. It read
             * "hitpoints are still 10" off a script that never ran, and no
             * amount of breaking `stat_heal`'s clamp could have turned it red.
             * Run the delay out first, then assert on the food as well: an
             * eaten item is the evidence the heal was the clamped one.
             */
            for( int i = 0; i < 4 && player->active_script; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(player->active_script == NULL,
                           "the eat delay should have run out before the next bite");
            inv_set(player, 0, meat, 1);
            player->hitpoints = 10;
            player->stat_boosted[MOCK230_STAT_HITPOINTS] = 10;
            mock230_world_handle(player, PKTOUT_NAME_OPHELD1, held, 8);
            SELFTEST_CHECK(player->inv[0].obj_id == -1,
                           "eating at full health should still cost the food");
            SELFTEST_CHECK(player->hitpoints == 10,
                           "eating at full health should not overheal, got %d",
                           player->hitpoints);

            /* An obj with no [opheld] script must still reach the engine's own
             * verb table — the fallback that keeps the mock usable without a
             * script pack, now that content gets first refusal on this packet
             * too. */
            {
                int sword = mock230_content_symbol(MOCK230_PACK_OBJ, "bronze_sword");

                for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                    inv_set(player, i, -1, 0);
                inv_set(player, 3, sword, 1);
                held[0] = (uint8_t)(sword >> 8);
                held[1] = (uint8_t)(sword & 0xff);
                held[3] = 3;
                mock230_world_handle(player, PKTOUT_NAME_OPHELD2, held, 8);
                SELFTEST_CHECK(player->worn[MOCK230_WEAR_WEAPON].obj_id == sword,
                               "an unbound opheld should still fall through to Wield");
            }

            /*
             * Pickpocketing is the other half of the same story: `[opnpc3,man]`
             * is an op the cache offers and nothing answered until now. The
             * level gate is what makes it observable without depending on a
             * random roll — a guard needs 40 Thieving, so at level 1 the script
             * always takes the refusal branch and parks on its ~mesbox.
             */
            {
                int guard = selftest_find_npc(&srv, 3254);
                int thieving = mock230_content_symbol(MOCK230_PACK_STAT, "thieving");
                uint8_t npc_payload[2];

                SELFTEST_CHECK(guard >= 0, "the roster should include a guard");
                SELFTEST_CHECK(thieving == 17, "thieving should be stat 17, got %d", thieving);
                if( guard >= 0 && thieving >= 0 )
                {
                    npc_payload[0] = (uint8_t)(guard >> 8);
                    npc_payload[1] = (uint8_t)(guard & 0xff);
                    player->stat_level[thieving] = 1;
                    player->stat_boosted[thieving] = 1;
                    player->active_script = NULL;
                    /* Stand next to the guard: this section is about the level
                     * gate, not about the walk, and settling would also run the
                     * pickpocket script's own delay. */
                    player->x = srv.npcs[guard].x + 1;
                    player->z = srv.npcs[guard].z;
                    player->level = srv.npcs[guard].level;
                    mock230_world_handle(player, PKTOUT_NAME_OPNPC3, npc_payload, 2);
                    SELFTEST_CHECK(player->active_script != NULL,
                                   "pickpocketing under the level requirement should "
                                   "park on its mesbox");
                    mock230_scripts_free(&srv);
                }
            }

            memcpy(player->inv, saved_inv, sizeof(saved_inv));
            memcpy(player->worn, saved_worn, sizeof(saved_worn));
            player->stat_level[MOCK230_STAT_HITPOINTS] = saved_hp_level;
            player->hitpoints = saved_hp;
            mock230_combat_sync_hitpoints(player);

            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: combat\n");
    {
        static struct Mock230Capture capture;
        int goblin = -1;
        int xp_before[MOCK230_STAT_COUNT] = { 0 };

        /*
         * Combat needs the content pack, and that is new.
         *
         * The player's whole side of a roll comes out of `%com_*`, written by
         * `[proc,player_combat_stat]`. With no script pack those varps are 0,
         * the attack roll is 0, and `roll_hit` misses every swing — which is
         * what this block showed the moment the calculation moved to content:
         * seven failures that all read as "combat is broken" rather than as
         * "combat is content and content is absent".
         *
         * That consequence is the reference's too, and it is the right one — a
         * LostCity server with no content tree has no combat either. What used
         * to be true, and is written into skill_combat/combat.rs2's header, is
         * that the mock ran *at all* with no pack. It still does: it logs in,
         * walks and renders. It just cannot fight.
         */
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");
        if( !loaded )
            fprintf(stderr, "  SKIP  no compiled script pack (run: make -C src mock230-scripts)\n");

        /* The goblin roster entry, whatever slot it landed in. 3028 is the id
         * OpenRune's gameval table calls `npcs.goblin`, checked against
         * cache.osrs230 — which does name 3028 "Goblin". The mock's old roster
         * used 655, which cache.osrs230 also names "Goblin" but which OpenRune
         * calls `goblin_red_soldier_2`: two different monsters with the same
         * display name, and the reason ids get validated rather than trusted. */
        goblin = selftest_find_npc(&srv, 3028);
        SELFTEST_CHECK(goblin >= 0, "the roster should include a goblin");

        if( goblin >= 0 )
        {
            struct Mock230Npc* npc = &srv.npcs[goblin];
            int start_hp = npc->hitpoints;
            int ticks = 0;

            SELFTEST_CHECK(start_hp > 0, "an npc spawns with hitpoints, got %d", start_hp);

            /*
             * The priority gate, on the exact pair that motivated it.
             *
             * `goblin_attack_unarmed` declares `forcedpriority=6`;
             * `goblin_block` declares none, so it defaults to 5. A swing
             * therefore outranks a flinch, and the *order* the two arrive in
             * cannot matter — which is the whole point, because an npc swings
             * in phase 4 and is hit in phase 5, so the block always arrived
             * second and always won.
             *
             * Asserted as priorities and then as behaviour: the numbers alone
             * would still pass if `mock230_anim_play_npc` compared them the
             * wrong way round.
             */
            SELFTEST_CHECK(mock230_seq_priority(309) == 6,
                           "goblin_attack_unarmed declares priority 6, got %d",
                           mock230_seq_priority(309));
            SELFTEST_CHECK(mock230_seq_priority(312) == 5,
                           "goblin_block defaults to priority 5, got %d",
                           mock230_seq_priority(312));
            {
                npc->anim_id = -1;
                mock230_anim_play_npc(npc, 309, 0);
                mock230_anim_play_npc(npc, 312, 0);
                SELFTEST_CHECK(npc->anim_id == 309,
                               "a flinch must not overwrite a swing, got %d", npc->anim_id);

                npc->anim_id = -1;
                mock230_anim_play_npc(npc, 312, 0);
                mock230_anim_play_npc(npc, 309, 0);
                SELFTEST_CHECK(npc->anim_id == 309,
                               "and a swing replaces a flinch, got %d", npc->anim_id);

                /* Equal priority is `>=`, so the later one wins — otherwise a
                 * repeated swing would stick on the first frame it played. */
                npc->anim_id = -1;
                mock230_anim_play_npc(npc, 312, 0);
                mock230_anim_play_npc(npc, 313, 0);
                SELFTEST_CHECK(npc->anim_id == 313,
                               "equal priority lets the later animation win, got %d",
                               npc->anim_id);
                npc->anim_id = -1;
                npc->masks = 0;
            }

            /* Combat animations are named by content and resolved against the
             * cache, so a rename upstream shows up here rather than as an npc
             * that silently stops animating. */
            SELFTEST_CHECK(npc->attack_seq == 309,
                           "goblin_attack_unarmed should resolve to 309, got %d",
                           npc->attack_seq);
            SELFTEST_CHECK(npc->block_seq == 312, "goblin_block should be 312, got %d",
                           npc->block_seq);
            SELFTEST_CHECK(npc->death_seq == 313, "goblin_death should be 313, got %d",
                           npc->death_seq);
            SELFTEST_CHECK(mock230_seq_by_name("human_unarmedpunch") == 422,
                           "human_unarmedpunch should be 422");
            SELFTEST_CHECK(mock230_seq_by_name("not_a_real_sequence") == -1,
                           "an unknown name resolves to -1, never to 0");
            SELFTEST_CHECK(npc->max_hitpoints == start_hp,
                           "and at full health");

            /*
             * Every attackable npc in the world can swing, block and die.
             *
             * The check that matters is over the whole roster rather than one
             * goblin, because the failure this replaces was per-npc and silent:
             * a content block that named `death_anim` and nothing else
             * suppressed the convention fallback for the other two, so half the
             * roster attacked with no animation while the goblins looked fine.
             * A -1 seq is not sent at all (play_npc_seq returns), so there is no
             * wire evidence and no log line — the only place it can be caught is
             * here, at resolution.
             *
             * Attackable is the gate: a shopkeeper needs no swing, and pinning
             * one on it would be authoring content to satisfy a test.
             */
            {
                int unanimated = 0;

                for( int i = 0; i < MOCK230_NPC_MAX; i++ )
                {
                    struct Mock230Npc* other = &srv.npcs[i];

                    if( !other->active || !mock230_combat_attackable(other->type) )
                        continue;
                    if( other->attack_seq >= 0 && other->block_seq >= 0 &&
                        other->death_seq >= 0 )
                        continue;
                    if( unanimated == 0 )
                        fprintf(stderr,
                                "  npcs missing a combat animation (type/name: "
                                "attack block death)\n");
                    unanimated++;
                    fprintf(stderr, "    %5d %-24s %6d %6d %6d\n", other->type,
                            mock230_npcinfo(other->type)->name, other->attack_seq,
                            other->block_seq, other->death_seq);
                }
                SELFTEST_CHECK(unanimated == 0,
                               "%d attackable npc(s) resolve no combat animation",
                               unanimated);
            }

            /* Stand next to it so the fight starts immediately. */
            player->x = npc->x + 1;
            player->z = npc->z;
            steps_clear(player);
            player->hitpoints = player->max_hitpoints;

            /*
             * Snapshot, never zero. `stat_xp_tenths` is what `level_for_xp`
             * derives the level from, so zeroing hitpoints xp drops the player
             * to level 1 hitpoints the next time any xp lands — max_hitpoints
             * follows, and a one-hitpoint player loses every fight after. The
             * first version of this check did exactly that and produced
             * thirteen failures in five unrelated sections.
             */
            xp_before[MOCK230_STAT_ATTACK] = player->stat_xp_tenths[MOCK230_STAT_ATTACK];
            xp_before[MOCK230_STAT_STRENGTH] = player->stat_xp_tenths[MOCK230_STAT_STRENGTH];
            xp_before[MOCK230_STAT_DEFENCE] = player->stat_xp_tenths[MOCK230_STAT_DEFENCE];
            xp_before[MOCK230_STAT_HITPOINTS] = player->stat_xp_tenths[MOCK230_STAT_HITPOINTS];

            mock230_combat_engage(&srv, goblin);
            SELFTEST_CHECK(player->combat_target == goblin, "engaging sets the target");

            /* Fight to the death. The cap is generous but finite: a combat loop
             * that never resolves is the failure worth catching here. */
            mock230_capture_begin(&srv, &capture);
            while( npc->hitpoints > 0 && ticks < 200 )
            {
                mock230_world_tick(&srv);
                ticks++;
            }
            mock230_capture_end(&srv);

            SELFTEST_CHECK(ticks < 200, "the fight should end, took %d ticks", ticks);
            SELFTEST_CHECK(npc->hitpoints == 0, "the goblin should die, hp %d",
                           npc->hitpoints);
            SELFTEST_CHECK(npc->death_tick >= 0,
                           "and stay visible for its death animation");
            SELFTEST_CHECK(player->combat_target == -1,
                           "the player should stop attacking a corpse");

            /* Damage reached the client: NPC_INFO carries the hitsplat and the
             * health bar in one mask. */
            SELFTEST_CHECK(mock230_capture_find(&capture, 104 /* NPC_INFO */, 0) >= 0,
                           "NPC_INFO should have been sent during the fight");

            /* It retaliated rather than standing there. */
            SELFTEST_CHECK(player->hitpoints < player->max_hitpoints,
                           "the goblin should have hit back, player hp %d/%d",
                           player->hitpoints, player->max_hitpoints);

            /*
             * Damage dealt pays experience.
             *
             * Hitpoints is the one skill every style pays, so it is the check
             * that holds whatever `%com_mode` the block happened to resolve —
             * and it is the one that catches the whole chain being dead
             * (`~give_combat_experience` never called, `%damagestyle` matching
             * no case, `stat_advance` refusing the skill) rather than only the
             * accurate branch working.
             */
            SELFTEST_CHECK(player->stat_xp_tenths[MOCK230_STAT_HITPOINTS] >
                               xp_before[MOCK230_STAT_HITPOINTS],
                           "killing the goblin should pay hitpoints xp, %d -> %d tenths",
                           xp_before[MOCK230_STAT_HITPOINTS],
                           player->stat_xp_tenths[MOCK230_STAT_HITPOINTS]);
            SELFTEST_CHECK(
                player->stat_xp_tenths[MOCK230_STAT_ATTACK] > xp_before[MOCK230_STAT_ATTACK] ||
                    player->stat_xp_tenths[MOCK230_STAT_STRENGTH] >
                        xp_before[MOCK230_STAT_STRENGTH] ||
                    player->stat_xp_tenths[MOCK230_STAT_DEFENCE] >
                        xp_before[MOCK230_STAT_DEFENCE],
                "and a combat skill too (att +%d str +%d def +%d tenths)",
                player->stat_xp_tenths[MOCK230_STAT_ATTACK] - xp_before[MOCK230_STAT_ATTACK],
                player->stat_xp_tenths[MOCK230_STAT_STRENGTH] - xp_before[MOCK230_STAT_STRENGTH],
                player->stat_xp_tenths[MOCK230_STAT_DEFENCE] - xp_before[MOCK230_STAT_DEFENCE]);

            /* Corpse despawns, then respawns at its spawn tile at full health.
             * Both windows come off the npc's own record, so a tree that gives
             * the goblin a longer death or a slower respawn moves the test with
             * it instead of reddening it. */
            for( int i = 0; i < npc->def->death_delay + 2 && npc->active; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(!npc->active, "the corpse should despawn");

            /*
             * Stand well clear before waiting for the respawn.
             *
             * Without this the check below is racing the goblin's aggression: it
             * respawns beside the player, attacks, the player auto-retaliates
             * and it comes back to "full health" already down a point. That is
             * correct behaviour, and it made this a test of two things at once
             * — it went red when the melee swing moved to content and the RNG
             * draw order changed, which shifted the fight by a tick and let the
             * retaliation land inside the window.
             */
            player->x = npc->spawn_x + 12;
            player->z = npc->spawn_z + 12;
            steps_clear(player);

            for( int i = 0; i < npc->def->respawnrate + 4 && !npc->active; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(npc->active, "the goblin should respawn");
            SELFTEST_CHECK(npc->hitpoints == start_hp,
                           "at full health, got %d of %d", npc->hitpoints, start_hp);
            SELFTEST_CHECK(npc->x == npc->spawn_x && npc->z == npc->spawn_z,
                           "at its spawn tile");
            /* A respawned npc leaves every client's tracking set on the tick
             * it goes inactive, so the next NPC_INFO adds it as a new entity
             * rather than as a move of one the client already has — which by
             * now it has done, so the observable end state is that this client
             * has been told about it again. */
            SELFTEST_CHECK(player->npc_tracked[goblin],
                           "and re-added to the client's npc list");
        }
    }

    fprintf(stderr, "mock230 selftest: the player dies and the script revives them\n");
    {
        /*
         * The whole of a death, and the last check is the one that matters.
         *
         * `dying` is the engine's gate on a corpse acting, and the script is what
         * lifts it: `[queue,player_death]` ends with `stat_heal`, and
         * `mock230_combat_player_tick` reads hitpoints back above zero. Nothing
         * cleared it for a while, so a player died once and then could never
         * attack, be attacked or be engaged again for the rest of the session —
         * and every earlier check here still passed, because dying itself was
         * fine. That is why "and can fight again" is a separate assertion.
         *
         * Bounds rather than exact ticks: `^death_delay` and `^respawn_coord`
         * live in player/configs/death.constant, and a test that restated them
         * would be the same duplication the C constants were.
         */
        int goblin = selftest_find_npc(&srv, 3028);

        SELFTEST_CHECK(goblin >= 0, "the roster should include a goblin");
        if( goblin >= 0 )
        {
            struct Mock230Npc* npc = &srv.npcs[goblin];
            int died_x;
            int died_z;
            int ticks = 0;

            selftest_park_player(&srv, npc->x + 1, npc->z);
            player->level = npc->level;
            died_x = player->x;
            died_z = player->z;
            /* Emptied so the script's `healenergy(10000)` has something to do.
             * That command restored *hitpoints* until recently, which nothing
             * could see: it runs one line after `stat_heal(hitpoints, …)` on the
             * only path that calls it. */
            player->run_energy = 0;

            mock230_combat_hit_player(&srv, 0, player->hitpoints);
            SELFTEST_CHECK(player->hitpoints == 0, "the killing blow empties the bar");
            SELFTEST_CHECK(player->dying, "and puts the player in the dying state");
            SELFTEST_CHECK(player->combat_target == -1, "which ends the fight");

            /* Generous but finite: the script's delay plus the ticks its own
             * commands cost. Never reviving is the failure being tested for. */
            while( player->dying && ticks < 20 )
            {
                mock230_world_tick(&srv);
                ticks++;
            }
            SELFTEST_CHECK(!player->dying,
                           "the death script ends the death, took %d tick(s)", ticks);
            SELFTEST_CHECK(player->hitpoints == player->max_hitpoints,
                           "healing to full is what ends it, got %d of %d",
                           player->hitpoints, player->max_hitpoints);
            SELFTEST_CHECK(player->x != died_x || player->z != died_z,
                           "and the script's teleport moved them off the spot they "
                           "died on (%d,%d)", died_x, died_z);
            SELFTEST_CHECK(player->run_energy == MOCK230_RUN_ENERGY_MAX,
                           "and its healenergy refilled the run bar, got %d of %d",
                           player->run_energy, MOCK230_RUN_ENERGY_MAX);

            /* The regression: engaging is gated on `dying`, so a gate that never
             * clears reads exactly like a click that did nothing. */
            selftest_park_player(&srv, npc->x + 1, npc->z);
            player->level = npc->level;
            mock230_combat_engage(&srv, goblin);
            SELFTEST_CHECK(player->combat_target == goblin,
                           "and the player can start a fight again, got %d",
                           player->combat_target);
            selftest_park_player(&srv, died_x, died_z);
        }
    }

    fprintf(stderr, "mock230 selftest: facing clears\n");
    {
        /*
         * FACE_ENTITY is a latch. Every path that drops a target has to send
         * -1, and "the player clicked somewhere else" is one of those paths —
         * it is the one that produced "facing never clears", because the other
         * three end a fight and this one just leaves.
         */
        int goblin = selftest_find_npc(&srv, 3028);

        SELFTEST_CHECK(goblin >= 0, "the roster should include a goblin");
        if( goblin >= 0 )
        {
            struct Mock230Npc* npc = &srv.npcs[goblin];
            uint8_t move[7];

            selftest_park_player(&srv, npc->x + 1, npc->z);
            player->level = npc->level;
            mock230_combat_engage(&srv, goblin);
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->face_entity == goblin,
                           "engaging faces the target, got %d", player->face_entity);

            /*
             * The npc's half of the same latch, and the id space it lives in.
             *
             * A player is `32768 + index` and the local player's index is 2047,
             * so the only correct value here is 34815. The bare 2047 that used
             * to be written is a *valid-looking* npc slot, so the client's
             * lookup simply found nothing and the npc never turned — checked as
             * an exact number rather than "not -1" for that reason.
             */
            SELFTEST_CHECK(npc->face_entity == MOCK230_FACE_LOCAL_PLAYER,
                           "the npc faces the local player as %d, got %d",
                           MOCK230_FACE_LOCAL_PLAYER, npc->face_entity);
            SELFTEST_CHECK(npc->face_entity >= MOCK230_FACE_PLAYER_BASE,
                           "which must be in the player half of the id space");

            /* MOVE_GAMECLICK: p1 ctrl, p2 start x, p2 start z, then waypoints. */
            player->masks = 0;
            move[0] = 0;
            move[1] = (uint8_t)((player->x + 3) >> 8);
            move[2] = (uint8_t)(player->x + 3);
            move[3] = (uint8_t)(player->z >> 8);
            move[4] = (uint8_t)player->z;
            mock230_world_handle(player, PKTOUT_NAME_MOVE_GAMECLICK, move, 5);

            SELFTEST_CHECK(player->combat_target == -1,
                           "walking away ends the fight");
            SELFTEST_CHECK(player->face_entity == -1,
                           "and stops facing, got %d", player->face_entity);
            SELFTEST_CHECK((player->masks & MOCK230_PMASK_FACE_ENTITY) != 0,
                           "and says so on the wire — a clear nobody sends is a "
                           "clear that never happens");
        }
    }

    fprintf(stderr, "mock230 selftest: login burst\n");
    {
        static struct Mock230Capture capture;
        /* UPDATE_PID before the stats: it is what tells the client which entity
         * in the stream is itself, which everything asking "what is my combat
         * level" depends on — the npc menu's (level-N) suffix most visibly. */
        static const int k_burst[] = {
            68 /* REBUILD_NORMAL */, 60 /* IF_OPENTOP */, 127 /* UPDATE_PID */,
            114 /* UPDATE_STAT */,   10 /* UPDATE_INV_FULL */,
        };

        mock230_capture_begin(&srv, &capture);
        mock230_world_login(player);
        mock230_capture_end(&srv);
        SELFTEST_CHECK(mock230_capture_has_sequence(&capture, k_burst, 5),
                       "the login burst should place, open, identify, then fill");

        /*
         * The combat tab's varps are content, not burst.
         *
         * [login,_] writes them and phase 10 transmits them, so they arrive on
         * the tick AFTER the burst — which is the whole point of moving them:
         * the engine no longer knows they exist. What is asserted here is the
         * end state on the wire, not where in the C it came from.
         *
         * The wide form matters on its own: special-attack energy is in tenths
         * of a percent, so a full bar is 1000, and VARP_SMALL's signed byte
         * would put it on the wire as -24.
         */
        if( srv.scripts_ok )
        {
            int com_mode = mock230_content_symbol(MOCK230_PACK_VARP, "com_mode");
            int sa_energy = mock230_content_symbol(MOCK230_PACK_VARP, "sa_energy");

            player->login_pending = 1;
            mock230_capture_begin(&srv, &capture);
            mock230_world_tick(&srv);
            mock230_capture_end(&srv);

            SELFTEST_CHECK(com_mode >= 0 && sa_energy >= 0,
                           "com_mode and sa_energy should be in pack/varp.pack");
            SELFTEST_CHECK(mock230_capture_find(&capture, 35 /* VARP_SMALL */, 0) >= 0,
                           "[login] should transmit the small varps");
            SELFTEST_CHECK(mock230_capture_find(&capture, 82 /* VARP_LARGE */, 0) >= 0,
                           "and the spec bar through the wide form");
            SELFTEST_CHECK(sa_energy >= 0 && player->varps[sa_energy] == 1000,
                           "^sa_max_energy should be 1000, got %d",
                           sa_energy >= 0 ? player->varps[sa_energy] : -1);
            SELFTEST_CHECK(mock230_world_attack_style(&srv) == MOCK230_STYLE_ACCURATE,
                           "the opening attack style should be accurate, got %d",
                           mock230_world_attack_style(&srv));

            /* An undeclared varp is server-only. The mock's own counters have
             * no .varp config, so nothing they do reaches the client. */
            SELFTEST_CHECK(mock230_content_varp(SELFTEST_VARP_GREETING_COUNT) == NULL,
                           "mock_greeting_count should have no varp declaration");
            SELFTEST_CHECK(mock230_content_varp(com_mode) != NULL &&
                               mock230_content_varp(com_mode)->transmit,
                           "com_mode should be declared transmit=yes");
        }
    }

    fprintf(stderr, "mock230 selftest: combat arithmetic\n");
    {
        /*
         * The formula inputs, not the outcome. Every one of these is a number
         * read out of cache.osrs230's own param tables, and the whole combat
         * system is built on the claim that they mean what OpenRune's
         * ParamMapper says they mean. If a future cache moves them, this is
         * where it shows up — rather than as fights that feel slightly off.
         */
        const struct Mock230ObjInfo* scimitar = mock230_objinfo(1321);
        const struct Mock230NpcInfo* guard = mock230_npcinfo(3254);

        SELFTEST_CHECK(scimitar->has_params, "the bronze scimitar has cache params");
        SELFTEST_CHECK(scimitar->bonus[MOCK230_PARAM_SLASHATTACK] == 7,
                       "bronze scimitar slashattack should be +7, got %d",
                       scimitar->bonus[MOCK230_PARAM_SLASHATTACK]);
        SELFTEST_CHECK(scimitar->bonus[MOCK230_PARAM_STRENGTHBONUS] == 6,
                       "bronze scimitar strengthbonus should be +6, got %d",
                       scimitar->bonus[MOCK230_PARAM_STRENGTHBONUS]);
        SELFTEST_CHECK(scimitar->attackrate == 4,
                       "bronze scimitar attackrate should be 4 ticks, got %d",
                       scimitar->attackrate);
        {
            /*
             * Which of the three melee bonuses a weapon swings with is
             * content's — `[proc,combat_get_damagetype]`, largest attack bonus
             * wins. This asserted `scimitar->damagetype`, a field a C heuristic
             * in mock230_objinfo.c filled in; once the player's swing started
             * asking content, that field had exactly one reader left, and it
             * was this line. A test keeping its own subject alive is not a test.
             */
            /*
             * The style table, not a heuristic.
             *
             * `~combat_get_damagetype` takes a row and a mode now, so the
             * scimitar's answer depends on which button is selected — slash on
             * 0, 1 and 3, and a controlled STAB on 2. That last one is the
             * whole reason the table exists: the max-attack-bonus heuristic
             * this replaced returned slash for every slot, so the controlled
             * style rolled against the wrong defence.
             */
            int32_t args[2];
            int32_t type = -1;
            int32_t row = -1;
            int32_t weapon = 1321;

            SELFTEST_CHECK(mock230_scripts_run_proc_int(
                               &srv, "[proc,combat_get_weapon_style_data]", &weapon, 1, &row),
                           "content should resolve a weapon's style row");
            args[0] = row;
            args[1] = 0;
            SELFTEST_CHECK(mock230_scripts_run_proc_int(
                               &srv, "[proc,combat_get_damagetype]", args, 2, &type) &&
                               type == MOCK230_DAMAGE_SLASH,
                           "a scimitar's accurate style is slash, got %d", type);
            args[1] = 2;
            SELFTEST_CHECK(mock230_scripts_run_proc_int(
                               &srv, "[proc,combat_get_damagetype]", args, 2, &type) &&
                               type == MOCK230_DAMAGE_STAB,
                           "and its controlled style is stab, got %d", type);

            /* A spear is the case no heuristic can reach: identical bonuses
             * across four buttons that roll stab, slash, crush, stab. 1237 is
             * the bronze spear. */
            weapon = 1237;
            SELFTEST_CHECK(mock230_scripts_run_proc_int(
                               &srv, "[proc,combat_get_weapon_style_data]", &weapon, 1, &row),
                           "a spear should resolve a style row");
            args[0] = row;
            args[1] = 1;
            SELFTEST_CHECK(mock230_scripts_run_proc_int(
                               &srv, "[proc,combat_get_damagetype]", args, 2, &type) &&
                               type == MOCK230_DAMAGE_SLASH,
                           "a spear's second style is slash, got %d", type);
            args[1] = 2;
            SELFTEST_CHECK(mock230_scripts_run_proc_int(
                               &srv, "[proc,combat_get_damagetype]", args, 2, &type) &&
                               type == MOCK230_DAMAGE_CRUSH,
                           "and its third is crush, got %d", type);
        }
        SELFTEST_CHECK(guard->bonus[MOCK230_PARAM_SLASHDEFENCE] == 25,
                       "the guard's slashdefence should be +25, got %d",
                       guard->bonus[MOCK230_PARAM_SLASHDEFENCE]);

        /* An obj with no params at all must stay distinguishable from one whose
         * bonuses are genuinely zero: the first is unarmed, the second is a bad
         * weapon, and they time differently. */
        SELFTEST_CHECK(!mock230_objinfo(995)->has_params,
                       "coins should carry no combat params");

        /*
         * The splat ids, which pointed at two sprites that are not splats.
         *
         * Config group 32 gives hitsplat 26 sprite 1358 and hitsplat 28 sprite
         * 1359 — `hitmark_0` and `hitmark_1` in the cache's own gameval
         * archive, the blue zero splat and the red damage splat. They are a
         * matched pair: identical `opcodeorder=8,49,5,9,13`, differing only in
         * the sprite.
         *
         * These were previously asserted as 0 and 1, whose sprites are 2270
         * (`hitmark_17`, a purple star) and 3521 (`hitmark_blocked`, a red
         * circle-with-a-slash). So every hit drew a no-entry sign and every
         * miss drew a purple star. Nothing failed — both are valid hitsplat
         * records that decode and render — it just looked wrong.
         *
         * The ids were reached by *looking at the sprite* and calling 2270
         * "blue" and 3521 "red", which is how a purple star and a red icon
         * passed. Asserting the ids here is the cheap half;
         * OSRS-Content/osrs239-content/configs/all.hitsplat.compack records
         * that the method is the cache's gameval name and never the colour,
         * which is the half that stops it recurring.
         */
        SELFTEST_CHECK(mock230_content_symbol(MOCK230_PACK_HITSPLAT, "hitsplat_damage") == 28,
                       "hitsplat_damage should be 28 (sprite 1359, gameval hitmark_1)");
        SELFTEST_CHECK(mock230_content_symbol(MOCK230_PACK_HITSPLAT, "hitsplat_block") == 26,
                       "hitsplat_block should be 26 (sprite 1358, gameval hitmark_0)");

        /* Attackability is the cache's own op list, which is what the client's
         * right-click menu reads. */
        SELFTEST_CHECK(mock230_combat_attackable(3028), "a goblin is attackable");
        SELFTEST_CHECK(!mock230_combat_attackable(3105), "Hans is not");
    }

    fprintf(stderr, "mock230 selftest: the combat stat block is content's\n");
    {
        /*
         * `[proc,player_combat_stat]` writes `%com_*` and the engine reads it.
         *
         * Two things are pinned here and they fail for different reasons. The
         * first is that the block is populated at all: every one of these varps
         * is server-allocated at 5705 and up, and `MOCK230_VARP_COUNT` was 5000
         * until recently — a write past the bound is a silent return in
         * `mock230_world_set_varp`, so an undersized table looks exactly like a
         * script that never ran.
         *
         * The second is the prayer multiplier, which is the rule the C version
         * did not have. There was no missing call to notice: `player_effective`
         * was `level + style_bonus + 8` and simply had no term for it, so every
         * Attack, Strength and Defence prayer in the game was inert and nothing
         * said so. Asserting that Ultimate Strength MOVES the max hit is the
         * check that could not have passed before the port.
         */
        int strength_before;
        int strength_after;

        player->stat_level[MOCK230_STAT_STRENGTH] = 60;
        player->stat_boosted[MOCK230_STAT_STRENGTH] = 60;
        player->stat_level[MOCK230_STAT_PRAYER] = 60;
        player->stat_boosted[MOCK230_STAT_PRAYER] = 60;
        mock230_scripts_run_proc(&srv, "[proc,prayer_deactivate_all]", NULL, 0);

        mock230_scripts_run_proc(&srv, "[proc,player_combat_stat]", NULL, 0);
        strength_before = player->varps[mock230_world_varp("com_maxhit")];
        SELFTEST_CHECK(strength_before > 0,
                       "the block should be populated, com_maxhit was %d",
                       strength_before);
        SELFTEST_CHECK(player->varps[mock230_world_varp("com_crushattack")] > 0,
                       "and carry an attack roll for the unarmed crush type");

        /* Ultimate Strength: +15 % to the RAW strength level, before the +8 and
         * the style bonus. Named through the content rather than counted here —
         * `^prayer_ultimatestrength` is a position in the book and moves when a
         * prayer is inserted. */
        SELFTEST_CHECK(selftest_prayer("prayer_ultimatestrength") >= 0,
                       "prayers.constant should name ultimatestrength");
        selftest_prayer_toggle(&srv, "prayer_ultimatestrength");
        SELFTEST_CHECK(selftest_prayer_on(&srv, "prayer_ultimatestrength"),
                       "a level 60 prayer stat can switch Ultimate Strength on");
        mock230_scripts_run_proc(&srv, "[proc,player_combat_stat]", NULL, 0);
        strength_after = player->varps[mock230_world_varp("com_maxhit")];
        SELFTEST_CHECK(strength_after > strength_before,
                       "Ultimate Strength should raise the max hit: %d -> %d",
                       strength_before, strength_after);
        mock230_scripts_run_proc(&srv, "[proc,prayer_deactivate_all]", NULL, 0);

        /*
         * The weapon-type mapping, now that it is a `switch_category` in
         * content rather than a C table.
         *
         * Two id spaces the cache does not relate: 1321 is a bronze scimitar,
         * obj category 21, and its combat-tab weapon TYPE is 9 (slash sword).
         * Writing the category straight through — which is what a missing
         * mapping does — gives 21, and weapon type 21 is "bladed staff", so the
         * tab grows autocast buttons on a scimitar. That is the failure this
         * pins: a plausible wrong layout, never an error.
         */
        {
            int32_t weapon = 1321;
            int32_t type = -1;

            SELFTEST_CHECK(mock230_scripts_run_proc_int(
                               &srv, "[proc,combat_weapon_type]", &weapon, 1, &type),
                           "content should answer combat_weapon_type");
            SELFTEST_CHECK(type == 9,
                           "a bronze scimitar is weapon type 9 (slash sword), got %d",
                           type);

            weapon = 995; /* coins: no category, so unarmed */
            SELFTEST_CHECK(mock230_scripts_run_proc_int(
                               &srv, "[proc,combat_weapon_type]", &weapon, 1, &type) &&
                               type == 0,
                           "an obj with no weapon category is unarmed, got %d", type);
        }

        /* Server varps are server state: the client has no varp 5705, so none
         * of these may be queued for transmission. */
        {
            const struct Mock230VarpDef* def =
                mock230_content_varp(mock230_world_varp("com_maxhit"));

            SELFTEST_CHECK(def && !def->transmit,
                           "com_maxhit must be declared and must not transmit");
        }

        /*
         * Which skill each damage style pays.
         *
         * The table, not one row of it. `~give_combat_experience` matched
         * `^attack_style_*` (button-slot numbering) against a `^style_melee_*`
         * argument, and those two families agree on 0 and 1 — so any check that
         * only exercised accurate or aggressive passed while defensive and
         * controlled were transposed. Every style, or this proves nothing.
         *
         * Asserted as "which skills moved", not as exact amounts: the amounts
         * are content's to tune and pinning them here would make a balance
         * change look like a regression.
         */
        {
            static const struct
            {
                int style;
                const char* name;
                int attack;
                int strength;
                int defence;
            } k_styles[] = {
                /* style                     att str def */
                { 0, "accurate",              1,  0,  0 },
                { 1, "aggressive",            0,  1,  0 },
                { 2, "defensive",             0,  0,  1 },
                { 3, "controlled",            1,  1,  1 },
            };

            for( size_t i = 0; i < sizeof(k_styles) / sizeof(k_styles[0]); i++ )
            {
                int32_t args[2] = { k_styles[i].style, 4 /* damage */ };
                int before[MOCK230_STAT_COUNT];
                int moved[MOCK230_STAT_COUNT];

                for( int s = 0; s < MOCK230_STAT_COUNT; s++ )
                    before[s] = player->stat_xp_tenths[s];
                SELFTEST_CHECK(
                    mock230_scripts_run_proc(&srv, "[proc,give_combat_experience]", args, 2),
                    "content should answer give_combat_experience");
                for( int s = 0; s < MOCK230_STAT_COUNT; s++ )
                    moved[s] = player->stat_xp_tenths[s] > before[s];

                SELFTEST_CHECK(moved[MOCK230_STAT_ATTACK] == k_styles[i].attack &&
                                   moved[MOCK230_STAT_STRENGTH] == k_styles[i].strength &&
                                   moved[MOCK230_STAT_DEFENCE] == k_styles[i].defence,
                               "%s should pay att/str/def %d/%d/%d, paid %d/%d/%d",
                               k_styles[i].name, k_styles[i].attack, k_styles[i].strength,
                               k_styles[i].defence, moved[MOCK230_STAT_ATTACK],
                               moved[MOCK230_STAT_STRENGTH], moved[MOCK230_STAT_DEFENCE]);
                /* Hitpoints is paid by every style — the one row that would
                 * still pass with the whole switch mismatched, and so the one
                 * that says the proc ran at all. */
                SELFTEST_CHECK(moved[MOCK230_STAT_HITPOINTS],
                               "%s should pay hitpoints xp", k_styles[i].name);
            }
        }
    }

    fprintf(stderr, "mock230 selftest: experience and levels\n");
    {
        int before_hp = player->hitpoints;

        player->stat_level[MOCK230_STAT_ATTACK] = 1;
        player->stat_boosted[MOCK230_STAT_ATTACK] = 1;
        player->stat_xp_tenths[MOCK230_STAT_ATTACK] = 0;
        player->stat_dirty = 0;

        /* 83 points is level 2 exactly — the first entry of the OldSchool
         * table, and the one worth pinning because the table is built rather
         * than transcribed. */
        mock230_combat_add_xp(&srv, MOCK230_STAT_ATTACK, 820);
        SELFTEST_CHECK(player->stat_level[MOCK230_STAT_ATTACK] == 1,
                       "82 xp is still level 1, got %d",
                       player->stat_level[MOCK230_STAT_ATTACK]);
        mock230_combat_add_xp(&srv, MOCK230_STAT_ATTACK, 10);
        SELFTEST_CHECK(player->stat_level[MOCK230_STAT_ATTACK] == 2,
                       "83 xp is level 2, got %d",
                       player->stat_level[MOCK230_STAT_ATTACK]);
        SELFTEST_CHECK((player->stat_dirty & (1u << MOCK230_STAT_ATTACK)) != 0,
                       "a changed stat should be flushed");

        /* The hitpoints stat and the player's hitpoints are one number. */
        SELFTEST_CHECK(player->stat_boosted[MOCK230_STAT_HITPOINTS] == player->hitpoints,
                       "the hitpoints stat should track the player's hitpoints (%d vs %d)",
                       player->stat_boosted[MOCK230_STAT_HITPOINTS], player->hitpoints);
        SELFTEST_CHECK(player->hitpoints == before_hp, "and xp should not heal");
    }

    fprintf(stderr, "mock230 selftest: collision and routing\n");
    {
        /*
         * Lumbridge castle's ground floor. The exact tiles matter less than the
         * property: a route the server produces is a walk of single steps, each
         * of which the collision map itself agrees is legal. A straight-line
         * interpolation through a wall satisfies the first and fails the
         * second, which is the bug this replaces.
         */
        int path_x[MOCK230_STEP_MAX];
        int path_z[MOCK230_STEP_MAX];
        int steps;

        SELFTEST_CHECK(mock230_scene_collision(0) != NULL,
                       "the scene should have collision for level 0");
        SELFTEST_CHECK(mock230_scene_contains(3222, 3218),
                       "the Lumbridge home tile is inside the scene");

        steps = mock230_scene_route(0, 3222, 3218, 3234, 3226, path_x, path_z,
                                    MOCK230_STEP_MAX);
        SELFTEST_CHECK(steps > 0, "a route across the courtyard exists, got %d", steps);
        if( steps > 0 )
        {
            int at_x = 3222;
            int at_z = 3218;
            int contiguous = 1;

            for( int i = 0; i < steps; i++ )
            {
                int dir = mock230_step_direction(path_x[i] - at_x, path_z[i] - at_z);

                if( dir < 0 || !mock230_scene_can_step(0, at_x, at_z, dir) )
                {
                    contiguous = 0;
                    break;
                }
                at_x = path_x[i];
                at_z = path_z[i];
            }
            SELFTEST_CHECK(contiguous, "every step of a route is a legal single step");
            SELFTEST_CHECK(at_x == 3234 && at_z == 3226,
                           "and it arrives, at %d,%d", at_x, at_z);
        }

        /*
         * Terrain and locs actually blocked something.
         *
         * Counted rather than probed at a named tile: which tile is a wall is a
         * fact about one cache revision, and a test that pins it fails for the
         * wrong reason the day the map changes. What must hold is that a
         * Lumbridge scene is not an open field — the castle alone is hundreds
         * of blocked tiles.
         */
        {
            int blocked = 0;

            for( int x = 0; x < COLLISION_SIZE; x++ )
            {
                for( int z = 0; z < COLLISION_SIZE; z++ )
                {
                    if( collision_map_tile(mock230_scene_collision(0), x, z) !=
                        COLL_FLAG_OPEN )
                        blocked++;
                }
            }
            /* The border ring is always BOUNDS: 4 * 104 - 4 = 412 tiles. Real
             * scenery has to be well clear of that. */
            SELFTEST_CHECK(blocked > 1000,
                           "a Lumbridge scene should block far more than its border, "
                           "got %d tiles",
                           blocked);
        }
    }

    fprintf(stderr, "mock230 selftest: ground objs\n");
    {
        static struct Mock230Capture capture;
        int slot;
        int free_before;

        /* Map spawns landed. OpenRune's Lumbridge item list includes a knife
         * at 3205,3212. */
        slot = -1;
        for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
        {
            if( srv.ground[i].active && srv.ground[i].obj_id == 946 &&
                srv.ground[i].x == 3205 && srv.ground[i].z == 3212 )
                slot = i;
        }
        SELFTEST_CHECK(slot >= 0, "the knife spawn from m50_50.jm2 should exist");
        SELFTEST_CHECK(slot < 0 || srv.ground[slot].is_spawn,
                       "and be marked a spawn rather than a drop");

        /*
         * A drop reaches the client inside its zone's shared blob: one
         * UPDATE_ZONE_PARTIAL_ENCLOSED naming the zone, carrying an OBJ_ADD.
         *
         * It used to be two loose packets, a zone header then the sub-packet,
         * sent at the moment of the drop. Now the event is buffered on the zone
         * and encoded once for everyone standing in it — which is also why the
         * tick below is load-bearing: nothing goes out until phase 10.
         */
        selftest_park_player(&srv, 3222, 3218);
        /* One quiet tick first, and it is load-bearing: a client that has just
         * arrived in a zone is caught up with that zone's whole state instead,
         * which is a different (and separately checked, below) shape. The
         * enclosed stream is what a change produces for a client already
         * standing there. */
        mock230_world_tick(&srv);
        mock230_capture_begin(&srv, &capture);
        mock230_world_obj_add(&srv, 526 /* bones */, 1, 3222, 3218, 0,
                              mock230_ids()->lootdrop_duration);
        mock230_world_tick(&srv);
        mock230_capture_end(&srv);
        SELFTEST_CHECK(selftest_enclosed_has(&capture, 120 /* OBJ_ADD */),
                       "a drop should reach the client as an enclosed OBJ_ADD");

        /*
         * And the other half: a client that has *not* been told about the zone
         * is caught up from the ZoneMap's own state — UPDATE_ZONE_FULL_FOLLOWS
         * to clear whatever it held, then the objs standing there. This is the
         * replay, the thing a broadcast could never do, and it is what makes a
         * door someone else opened open for whoever logs in next.
         */
        mock230_zone_player_reset(player);
        mock230_capture_begin(&srv, &capture);
        mock230_world_tick(&srv);
        mock230_capture_end(&srv);
        SELFTEST_CHECK(mock230_capture_find(&capture, 41 /* UPDATE_ZONE_FULL_FOLLOWS */, 0) >= 0,
                       "a client that holds no zones should be sent FULL_FOLLOWS");
        SELFTEST_CHECK(mock230_capture_find(&capture, 120 /* OBJ_ADD */, 0) >= 0,
                       "and the objs already on the floor, without anything changing");

        /* Picking it up moves it into the backpack and tells the client it is
         * gone. */
        selftest_park_player(&srv, 3222, 3218);
        free_before = inv_first_free(player);
        {
            uint8_t payload[6] = { (uint8_t)(3222 >> 8), (uint8_t)3222,
                                   (uint8_t)(3218 >> 8), (uint8_t)3218,
                                   (uint8_t)(526 >> 8),  (uint8_t)526 };

            mock230_capture_begin(&srv, &capture);
            mock230_world_handle(player, PKTOUT_NAME_OPOBJ1, payload, 6);
            /* The take happens on the packet; the OBJ_DEL is a zone event and
             * goes out with the tick's flush. */
            mock230_world_tick(&srv);
            mock230_capture_end(&srv);
        }
        SELFTEST_CHECK(free_before >= 0 && player->inv[free_before].obj_id == 526,
                       "taking an obj puts it in the backpack");
        SELFTEST_CHECK(selftest_enclosed_has(&capture, 121 /* OBJ_DEL */),
                       "and tells the client it is gone");

        /* Dropping it puts it back on the floor rather than deleting it. */
        if( free_before >= 0 && player->inv[free_before].obj_id == 526 )
        {
            /*
             * Drop is OPHELD5 with no cache verb behind it.
             *
             * rev-230 obj records do not carry a "Drop" op at all — bones list
             * only "Bury". The client synthesises the row and sends it as
             * OPHELD5 (rs_minimenu_build.c add_inv_slot_rows), so the server
             * has to recognise the *index*, not a verb it will never see.
             */
            int on_floor = 0;
            uint8_t payload[8];
            struct RSAreaBuf drop;

            SELFTEST_CHECK(mock230_objinfo(526)->if_ops[4] == NULL,
                           "the cache should NOT name a Drop op — the client adds it");

            rsab_wrap(&drop, payload, sizeof(payload));
            rsab_p2(&drop, 526);
            rsab_p2(&drop, free_before);
            rsab_p4(&drop, mock230_ids()->com_inventory_items);
            mock230_world_handle(player, PKTOUT_NAME_OPHELD5, payload, (int)rsab_len(&drop));
            for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
            {
                if( srv.ground[i].active && srv.ground[i].obj_id == 526 &&
                    srv.ground[i].x == player->x && srv.ground[i].z == player->z )
                    on_floor = 1;
            }
            SELFTEST_CHECK(on_floor, "dropping an obj leaves it on the floor");
            SELFTEST_CHECK(player->inv[free_before].obj_id == -1,
                           "and takes it out of the backpack");
        }
    }

    fprintf(stderr, "mock230 selftest: the death drop is content's\n");
    {
        /*
         * `[ai_queue3,_]` used to be `MOCK230_FALLBACK_AI_QUEUE3`.
         *
         * The C read `Mock230NpcDef.death_drop` and called
         * `mock230_world_obj_add` whenever nothing was bound; the row is gone
         * and `skill_combat/npc_combat.rs2` says it instead, which is where the
         * reference says it too (`[ai_queue3,_] gosub(npc_default_death)`,
         * skill_combat/scripts/npc/npc_combat.rs2:3).
         *
         * Three cases, and the third is the regression guard. Deleting a
         * fallback is a claim that the behaviour moved, so the test has to
         * cover what the C did and not just what the new script does:
         *
         *   A. an npc with a bound table still runs it, and the `_` rung does
         *      not also fire (one drop, not two).
         *   B. an npc with no binding at all drops its `death_drop` — the case
         *      the C answered, and the only reason the row existed.
         *   C. an npc whose `death_drop` is `null` drops nothing. The C spelled
         *      this as `death_drop >= 0`; the script spells it as
         *      `if ($drop = null)`, which is the reference's own guard.
         *
         *      What this case does NOT pin, stated because a test that cannot
         *      fail is worse than no test: deleting the script's guard leaves
         *      it green, because `mock230_world_obj_add` refuses `obj_id < 0`
         *      on its own, so `obj_add(npc_coord, null, ...)` is already a
         *      no-op. The guard's *sense* is pinned — inverting it turns case B
         *      red — and the behaviour contract is pinned whichever layer
         *      enforces it. Its presence is not, and cannot be here.
         *
         * `param=death_drop` is never absent, which is worth stating because it
         * looks like it should be: `general/configs/npc_default.npc`'s
         * `[default]` block authors it and `npc_def_seed_from_cache` copies the
         * whole default record — params included — into every block. So the
         * value is `bones` for an npc nothing describes, and `null` is the only
         * way to say "leaves nothing".
         */
        int param_death_drop = mock230_content_symbol(MOCK230_PACK_PARAM, "death_drop");
        int obj_bones = mock230_content_symbol(MOCK230_PACK_OBJ, "bones");
        int obj_raw_chicken = mock230_content_symbol(MOCK230_PACK_OBJ, "raw_chicken");
        int npc_chicken = mock230_content_symbol(MOCK230_PACK_NPC, "chicken");
        int npc_duck = mock230_content_symbol(MOCK230_PACK_NPC, "duck");
        int tile_x = 0;
        int tile_z = 0;

        SELFTEST_CHECK(param_death_drop >= 0 && obj_bones >= 0 && obj_raw_chicken >= 0 &&
                           npc_chicken >= 0 && npc_duck >= 0,
                       "the death-drop test's names should all resolve through the pack");

        if( srv.scripts_ok && npc_chicken >= 0 && npc_duck >= 0 )
        {
            /* The roster's own chicken and duck, killed where they stand.
             * `mock230_world_npc_spawn` is not usable here: world init fills
             * the pool, so a fresh spawn fails with "no free npc slot" — and
             * moving an npc would need a zone refile for the drop to be filed
             * where the count looks for it. */
            int slot = selftest_find_npc(&srv, npc_chicken);
            int bones_here = 0;
            int chicken_here = 0;

            /* A. `[ai_queue3,_chicken]` is bound on the *category* rung, which
             * is the harder of the two to keep working: the `_` script added
             * below sits one rung past it, so a lookup that fell through would
             * leave bones and no chicken. */
            SELFTEST_CHECK(slot >= 0, "the roster should include a chicken");
            if( slot >= 0 )
            {
                tile_x = srv.npcs[slot].x;
                tile_z = srv.npcs[slot].z;
                mock230_world_npc_died(&srv, slot);
                for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
                {
                    if( !srv.ground[i].active || srv.ground[i].x != tile_x ||
                        srv.ground[i].z != tile_z )
                        continue;
                    if( srv.ground[i].obj_id == obj_bones )
                        bones_here++;
                    if( srv.ground[i].obj_id == obj_raw_chicken )
                        chicken_here++;
                }
                SELFTEST_CHECK(chicken_here == 1,
                               "a chicken's own table should still run, got %d raw chicken",
                               chicken_here);
                SELFTEST_CHECK(bones_here == 1,
                               "and its default drop exactly once — the `_` rung must not "
                               "also fire, got %d bones",
                               bones_here);
            }
            selftest_clear_ground(&srv);

            /* B. The case the deleted C answered: nothing binds `duck`, and
             * nothing binds its category, so the `_` script is the only thing
             * between a kill and a silent one. */
            slot = selftest_find_npc(&srv, npc_duck);
            SELFTEST_CHECK(slot >= 0, "the roster should include a duck");
            if( slot >= 0 )
            {
                int drops = 0;
                tile_x = srv.npcs[slot].x;
                tile_z = srv.npcs[slot].z;
                mock230_world_npc_died(&srv, slot);
                for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
                {
                    if( srv.ground[i].active && srv.ground[i].x == tile_x &&
                        srv.ground[i].z == tile_z && srv.ground[i].obj_id == obj_bones )
                        drops++;
                }
                SELFTEST_CHECK(drops == 1,
                               "an npc with no bound table should still leave its "
                               "death_drop, got %d",
                               drops);
            }
            selftest_clear_ground(&srv);

            /*
             * C. `param=death_drop,null`.
             *
             * No npc in this tree authors it, so the def is built here rather
             * than by adding a config block for a case no area wants — a test
             * fixture, not content. It is the engine default with the one param
             * overwritten, which is exactly what the config line produces
             * (`apply_param` resolves `null` to -1 and files it under the param
             * id like any other value).
             */
            if( slot >= 0 )
            {
                static struct Mock230NpcDef silent;
                const struct Mock230NpcDef* restore = srv.npcs[slot].def;
                int drops = 0;

                silent = *srv.npcs[slot].def;
                silent.death_drop = -1;
                for( int i = 0; i < silent.param_count; i++ )
                {
                    if( silent.params[i].key == param_death_drop )
                        silent.params[i].value = -1;
                }
                srv.npcs[slot].def = &silent;

                mock230_world_npc_died(&srv, slot);
                for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
                {
                    if( srv.ground[i].active && srv.ground[i].x == tile_x &&
                        srv.ground[i].z == tile_z )
                        drops++;
                }
                SELFTEST_CHECK(drops == 0,
                               "`param=death_drop,null` should leave nothing, got %d objs",
                               drops);
                srv.npcs[slot].def = restore;
            }
            selftest_clear_ground(&srv);
        }
        else if( !srv.scripts_ok )
        {
            fprintf(stderr, "  SKIP  no compiled script pack (run: make -C src mock230-scripts)\n");
        }
    }

    fprintf(stderr, "mock230 selftest: interactions walk before they act\n");
    {
        /*
         * The regression test for the interaction model.
         *
         * Before it existed, every op handler walked *and* acted in the same
         * call, so this section's obj would have been in the backpack the
         * instant the packet landed — from eight tiles away, through a wall.
         * What is asserted here is the *negative*: nothing happened yet.
         */
        int obj_x = 3230;
        int obj_z = 3218;
        int free_slot;
        int settled;

        selftest_park_player(&srv, 3222, 3218);
        free_slot = inv_first_free(player);
        mock230_world_obj_add(&srv, 526 /* bones */, 1, obj_x, obj_z, 0, mock230_ids()->lootdrop_duration);

        {
            uint8_t payload[6] = { (uint8_t)(obj_x >> 8), (uint8_t)obj_x,
                                   (uint8_t)(obj_z >> 8), (uint8_t)obj_z,
                                   (uint8_t)(526 >> 8),   (uint8_t)526 };

            mock230_world_handle(player, PKTOUT_NAME_OPOBJ1, payload, 6);
        }

        SELFTEST_CHECK(player->interaction.kind == MOCK230_INTERACT_OBJ,
                       "a click on something out of reach latches an interaction");
        SELFTEST_CHECK(free_slot < 0 || player->inv[free_slot].obj_id != 526,
                       "and does NOT act on it from eight tiles away");
        SELFTEST_CHECK(player->step_count > 0, "it starts a walk instead");

        settled = selftest_settle(&srv, 40);
        SELFTEST_CHECK(settled > 0, "arriving takes ticks, got %d", settled);
        SELFTEST_CHECK(free_slot >= 0 && player->inv[free_slot].obj_id == 526,
                       "and the obj is taken on arrival");
        SELFTEST_CHECK(player->x == obj_x && player->z == obj_z,
                       "standing on the tile it was on, got %d,%d", player->x, player->z);

        /* Changing your mind cancels it: a ground click must not leave an op
         * armed to fire whenever the player next wanders into range. */
        selftest_park_player(&srv, 3222, 3218);
        mock230_world_obj_add(&srv, 526, 1, obj_x, obj_z, 0, mock230_ids()->lootdrop_duration);
        {
            uint8_t payload[6] = { (uint8_t)(obj_x >> 8), (uint8_t)obj_x,
                                   (uint8_t)(obj_z >> 8), (uint8_t)obj_z,
                                   (uint8_t)(526 >> 8),   (uint8_t)526 };
            uint8_t move[5];
            struct RSAreaBuf walk;

            mock230_world_handle(player, PKTOUT_NAME_OPOBJ1, payload, 6);
            SELFTEST_CHECK(player->interaction.kind == MOCK230_INTERACT_OBJ,
                           "the interaction is armed");

            rsab_wrap(&walk, move, sizeof(move));
            rsab_p1(&walk, 0);
            rsab_p2(&walk, 3224);
            rsab_p2(&walk, 3218);
            mock230_world_handle(player, PKTOUT_NAME_MOVE_GAMECLICK, move, (int)rsab_len(&walk));
            SELFTEST_CHECK(player->interaction.kind == MOCK230_INTERACT_NONE,
                           "and walking somewhere else abandons it");
        }
    }

    fprintf(stderr, "mock230 selftest: doors\n");
    {
        static struct Mock230Capture capture;
        /* 1535 `poordoor` at 3226,3223 — a wall loc on Lumbridge's ground
         * floor, paired with 1536 `poordooropen` by OpenRune's curated table. */
        int slot = mock230_scene_find_loc(3226, 3223, 0, 1535);

        SELFTEST_CHECK(slot >= 0, "the castle door at 3226,3223 should be in the scene");
        if( slot >= 0 )
        {
            struct Mock230SceneLoc* loc = mock230_scene_loc(slot);
            const struct Mock230LocDef* def = mock230_content_loc(1535);
            uint8_t payload[6] = { (uint8_t)(3226 >> 8), (uint8_t)3226,
                                   (uint8_t)(3223 >> 8), (uint8_t)3223,
                                   (uint8_t)(1535 >> 8), (uint8_t)1535 };

            SELFTEST_CHECK(def != NULL, "doors.loc should describe loc 1535");
            SELFTEST_CHECK(def && def->category == MOCK230_LOC_CATEGORY_DOOR_CLOSED,
                           "as a closed door");
            SELFTEST_CHECK(def && def->next_loc_stage == 1536,
                           "opening into 1536, got %d", def ? def->next_loc_stage : -1);

            player->level = 0;
            mock230_capture_begin(&srv, &capture);
            mock230_world_handle(player, PKTOUT_NAME_OPLOC1, payload, 6);
            /* The door is across the courtyard: the click routes there and the
             * swap happens on arrival. That the walk is now part of opening a
             * door is the interaction model working. */
            SELFTEST_CHECK(selftest_settle(&srv, 40) >= 0,
                           "the walk to the door should complete");
            mock230_capture_end(&srv);

            SELFTEST_CHECK(loc->loc_id == 1536, "opening swaps the loc, got %d",
                           loc->loc_id);
            {
                struct Mock230ZoneLoc* record =
                    mock230_zone_loc_find(&srv, loc->x, loc->z, loc->level, loc->shape);

                SELFTEST_CHECK(record && record->loc_id == 1536,
                               "and records the change in the zone, got %d",
                               record ? record->loc_id : -1);
                SELFTEST_CHECK(record && record->base_loc_id == 1535,
                               "remembering what the map square has, got %d",
                               record ? record->base_loc_id : -1);
            }
            SELFTEST_CHECK(selftest_enclosed_has(&capture, 70 /* LOC_ADD_CHANGE */),
                           "and tells the client, in the zone's enclosed stream");

            /* Closing it again is the same rule read backwards, which is the
             * whole point of storing the pairing on both halves. */
            payload[4] = (uint8_t)(1536 >> 8);
            payload[5] = (uint8_t)1536;
            mock230_world_handle(player, PKTOUT_NAME_OPLOC1, payload, 6);
            /* Standing beside it now, so this one resolves on the click. */
            SELFTEST_CHECK(selftest_settle(&srv, 10) >= 0,
                           "closing the door should complete");
            SELFTEST_CHECK(loc->loc_id == 1535, "and closing swaps it back, got %d",
                           loc->loc_id);
        }
    }

    fprintf(stderr, "mock230 selftest: extended info masks\n");
    {
        static struct Mock230Capture capture;
        int index;

        /* THE TRAP. Any mask bit at 0x100 or above needs BIG_UPDATE (0x80) set
         * and the mask written as two bytes, because the reader does
         * `mask = g1(); if (mask & 0x80) mask += g1() << 8;`. Writing one byte
         * with SPOTANIM set silently loses it; writing two without 0x80 makes
         * the client read the first field as mask bits and misparse the whole
         * rest of the extended section. Neither shows up as a crash. */
        /* The byte offsets below assume an idle, unengaged player: a queued
         * step or a fight in progress lengthens the bit section and moves the
         * mask. Earlier sections leave both behind. */
        selftest_park_player(&srv, player->x, player->z);
        player->place_dirty = 0;
        player->move_count = 0;
        player->masks = MOCK230_PMASK_SEQUENCE | MOCK230_PMASK_SPOTANIM;
        player->anim_id = 808;
        player->anim_delay = 0;
        player->spotanim_id = 74;
        player->spotanim_height_delay = 0;

        mock230_capture_begin(&srv, &capture);
        mock230_world_tick(&srv);
        mock230_capture_end(&srv);

        index = mock230_capture_find(&capture, 23 /* PLAYER_INFO */, 0);
        SELFTEST_CHECK(index >= 0, "PLAYER_INFO should have been sent");
        if( index >= 0 )
        {
            const struct Mock230CapturedPacket* packet = &capture.packets[index];
            /* Bit section: 1 (has update) + 2 (op 0) + 8 (tracked count) +
             * 11 (terminator) = 22 bits, so the extended block starts at byte
             * 3 once rsab_bytes rounds up. */
            int mask_low = packet->data[3];
            int mask_high = packet->data[4];

            SELFTEST_CHECK((mask_low & 0x80) != 0,
                           "a mask above 0xff must set BIG_UPDATE, low byte was 0x%02x",
                           mask_low);
            SELFTEST_CHECK(mask_high == 0x01,
                           "the high mask byte should carry SPOTANIM, was 0x%02x", mask_high);
            SELFTEST_CHECK((mask_low & 0x02) != 0, "SEQUENCE should still be set");
        }

        /* A single-byte mask must NOT set BIG_UPDATE, or the client eats the
         * first field as a second mask byte. */
        player->masks = MOCK230_PMASK_SEQUENCE;
        mock230_capture_begin(&srv, &capture);
        mock230_world_tick(&srv);
        mock230_capture_end(&srv);
        index = mock230_capture_find(&capture, 23, 0);
        if( index >= 0 )
        {
            SELFTEST_CHECK((capture.packets[index].data[3] & 0x80) == 0,
                           "a mask below 0x100 must not set BIG_UPDATE");
        }

        /* Masks describe one tick, so a tick with nothing set must not emit an
         * extended block at all. */
        player->masks = 0;
        mock230_capture_begin(&srv, &capture);
        mock230_world_tick(&srv);
        mock230_capture_end(&srv);
        index = mock230_capture_find(&capture, 23, 0);
        SELFTEST_CHECK(index >= 0 && capture.packets[index].len == 3,
                       "an idle player should send only the bit section");
    }

    fprintf(stderr, "mock230 selftest: movement\n");
    {
        /*
         * Movement is about how many queued steps a tick consumes and how a
         * step is spelled on the wire, not about routing — so it needs a patch
         * of ground with nothing in it. Lumbridge has plenty, but which tiles
         * are clear is a fact about the cache, so the patch is *found* rather
         * than named. A test that hardcodes an open tile starts failing the day
         * someone puts a crate on it.
         */
        int start_x = -1;
        int start_z = -1;

        for( int ox = -20; ox <= 20 && start_x < 0; ox++ )
        {
            for( int oz = -20; oz <= 20; oz++ )
            {
                int candidate_x = 3222 + ox;
                int candidate_z = 3218 + oz;
                int clear = 1;

                if( !mock230_scene_contains(candidate_x, candidate_z + 5) )
                    continue;
                for( int step = 0; step < 5; step++ )
                {
                    if( !mock230_scene_can_step(0, candidate_x + step, candidate_z + step,
                                                2 /* north-east */) )
                        clear = 0;
                }
                /*
                 * The seven-tile north leg the interpolation check walks, from
                 * where the diagonal leaves the player: (start+3, start+3).
                 *
                 * This used to go unchecked, and the route to it genuinely
                 * failed — the straight-line fallback in `steps_walk_to` then
                 * filled the queue by walking through whatever was in the way,
                 * so the assertion passed on the strength of the bug it should
                 * have caught. With the fallback gone an unreachable leg is an
                 * empty queue, which is correct and which made this test fail
                 * honestly for the first time.
                 */
                if( !mock230_scene_contains(candidate_x + 3, candidate_z + 10) )
                    continue;
                for( int step = 0; step < 7; step++ )
                {
                    if( !mock230_scene_can_step(0, candidate_x + 3, candidate_z + 3 + step,
                                                1 /* north */) )
                        clear = 0;
                }
                if( clear )
                {
                    start_x = candidate_x;
                    start_z = candidate_z;
                    break;
                }
            }
        }
        SELFTEST_CHECK(start_x >= 0, "Lumbridge should have a clear five-tile diagonal");
        if( start_x < 0 )
        {
            start_x = player->x;
            start_z = player->z;
        }
        selftest_park_player(&srv, start_x, start_z);

        steps_clear(player);
        player->running = 0;
        steps_walk_to(player, start_x + 4, start_z + 4);
        SELFTEST_CHECK(player->step_count == 4, "diagonal walk should be 4 tiles, got %d",
                       player->step_count);

        mock230_world_tick(&srv);
        SELFTEST_CHECK(player->move_count == 1, "walking covers one tile per tick, got %d",
                       player->move_count);
        SELFTEST_CHECK(player->move_dirs[0] == 2, "north-east is direction 2, got %d",
                       player->move_dirs[0]);
        SELFTEST_CHECK(player->x == start_x + 1 && player->z == start_z + 1,
                       "one diagonal step, at %d,%d", player->x, player->z);

        /* The toggle, not `running` — the tick derives the latter from it and
         * from the energy, so setting `running` directly is overwritten. */
        player->run_toggle = 1;
        mock230_world_tick(&srv);
        SELFTEST_CHECK(player->move_count == 2, "running covers two tiles per tick, got %d",
                       player->move_count);
        SELFTEST_CHECK(player->x == start_x + 3, "ran two tiles, x=%d", player->x);

        /* Waypoints more than a tile apart are interpolated, not teleported:
         * the client only sends the turning points of its route. */
        steps_clear(player);
        steps_walk_to(player, player->x, player->z + 7);
        SELFTEST_CHECK(player->step_count == 7, "7-tile leg fills 7 steps, got %d",
                       player->step_count);
        player->run_toggle = 0;
    }

    fprintf(stderr, "mock230 selftest: prayer\n");
    {
        /*
         * Every name here is content's, and none of them is an engine call any
         * more: `^prayer_protectfrommelee` says which prayer, the varbit of the
         * same name says whether it is on, and `[proc,prayer_toggle]` is what
         * the book's button calls. The engine module that used to sit between
         * this test and the content is gone — which is the point, because a
         * test that drives the engine's copy of a rule cannot fail when the
         * content's copy is wrong.
         */
        const int overhead_melee = selftest_prayer("headicon_prayer_protectfrommelee");

        SELFTEST_CHECK(selftest_prayer("prayer_protectfrommelee") >= 0 &&
                           selftest_prayer("prayer_rockskin") >= 0 &&
                           selftest_prayer("prayer_steelskin") >= 0 && overhead_melee >= 0,
                       "the prayer content should declare the three this asserts on");

        mock230_scripts_run_proc(&srv, "[proc,prayer_deactivate_all]", NULL, 0);
        player->stat_level[MOCK230_STAT_PRAYER] = 1;
        player->stat_boosted[MOCK230_STAT_PRAYER] = 1;

        /* The level gate reads the base level, so a level-1 character is
         * refused Protect from Melee no matter how many points they have. */
        selftest_prayer_toggle(&srv, "prayer_protectfrommelee");
        SELFTEST_CHECK(!selftest_prayer_on(&srv, "prayer_protectfrommelee"),
                       "a level-1 character cannot protect");

        player->stat_level[MOCK230_STAT_PRAYER] = 99;
        player->stat_boosted[MOCK230_STAT_PRAYER] = 99;
        selftest_prayer_toggle(&srv, "prayer_protectfrommelee");
        SELFTEST_CHECK((player->headicons & (1 << overhead_melee)) != 0,
                       "protect from melee is up");
        SELFTEST_CHECK(player->headicons == (1 << overhead_melee),
                       "and is the only overhead icon");
        SELFTEST_CHECK((player->masks & MOCK230_PMASK_APPEARANCE) != 0,
                       "turning a prayer on re-sends the appearance");

        /* Same group: the second defence prayer replaces the first rather than
         * stacking with it. Both claim `^prayer_group_defence` in prayers.dbrow
         * and nothing else says they conflict. */
        selftest_prayer_toggle(&srv, "prayer_rockskin");
        selftest_prayer_toggle(&srv, "prayer_steelskin");
        SELFTEST_CHECK(!selftest_prayer_on(&srv, "prayer_rockskin"),
                       "steel skin replaces rock skin");
        SELFTEST_CHECK(selftest_prayer_on(&srv, "prayer_steelskin"), "steel skin is up");
        SELFTEST_CHECK((player->headicons & (1 << overhead_melee)) != 0,
                       "and left the overhead alone — a different group");

        /*
         * A combination prayer claims three groups, so Piety drops the defence
         * prayer AND the strength one. That is what `group` being a LIST buys,
         * and it is the case the six hand-written group procs could not state:
         * each handler called one of them, so Piety and Ultimate Strength were
         * up together.
         */
        selftest_prayer_toggle(&srv, "prayer_ultimatestrength");
        SELFTEST_CHECK(selftest_prayer_on(&srv, "prayer_ultimatestrength") &&
                           selftest_prayer_on(&srv, "prayer_steelskin"),
                       "strength and defence prayers stack with each other");
        selftest_prayer_toggle(&srv, "prayer_piety");
        SELFTEST_CHECK(selftest_prayer_on(&srv, "prayer_piety"), "piety is up");
        SELFTEST_CHECK(!selftest_prayer_on(&srv, "prayer_ultimatestrength") &&
                           !selftest_prayer_on(&srv, "prayer_steelskin"),
                       "and dropped both of the groups it claims");

        /*
         * The cheat, through the path a client's `::pray 18` takes: the engine
         * hands the line to `[debugproc,pray]` and the content does the rest.
         * Asserting it here is what keeps the headless harness honest — the
         * cheat is the only way a scripted session reaches the prayer book.
         */
        mock230_scripts_run_proc(&srv, "[proc,prayer_deactivate_all]", NULL, 0);
        SELFTEST_CHECK(mock230_scripts_run_debugproc(&srv, "pray 18"),
                       "::pray should reach [debugproc,pray]");
        SELFTEST_CHECK(selftest_prayer_on(&srv, "prayer_protectfrommelee"),
                       "::pray 18 is protect from melee");
        SELFTEST_CHECK(!mock230_scripts_run_debugproc(&srv, "nosuchcheat 1"),
                       "and a line no debugproc claims falls through to the engine");

        /*
         * Drain: protect from melee is 12 a tick against a resistance of 60, so
         * a point goes every fifth tick.
         *
         * `srv.tick` is advanced between the calls rather than left alone. A
         * timer's clock is the **absolute** tick it last fired at, not a
         * countdown, so five calls on one tick fire it once — this loop used to
         * advance the counter *by calling the drain*, which was the countdown
         * implementation showing through the test.
         */
        mock230_scripts_run_proc(&srv, "[proc,prayer_deactivate_all]", NULL, 0);
        player->stat_level[MOCK230_STAT_PRAYER] = 99;
        selftest_prayer_toggle(&srv, "prayer_protectfrommelee");
        player->stat_boosted[MOCK230_STAT_PRAYER] = 99;
        srv.tick++;
        mock230_scripts_process_timers(&srv);
        SELFTEST_CHECK(player->stat_boosted[MOCK230_STAT_PRAYER] == 99,
                       "12 units is not yet a point");
        srv.tick++;
        mock230_scripts_process_timers(&srv);
        SELFTEST_CHECK(player->stat_boosted[MOCK230_STAT_PRAYER] == 99, "nor is 24");
        for( int i = 0; i < 3; i++ )
        {
            srv.tick++;
            mock230_scripts_process_timers(&srv);
        }
        SELFTEST_CHECK(player->stat_boosted[MOCK230_STAT_PRAYER] == 98,
                       "60 units is one prayer point, got %d",
                       player->stat_boosted[MOCK230_STAT_PRAYER]);

        /* Running out drops everything, including the overhead. */
        player->stat_boosted[MOCK230_STAT_PRAYER] = 1;
        for( int i = 0; i < 10; i++ )
        {
            srv.tick++;
            mock230_scripts_process_timers(&srv);
        }
        SELFTEST_CHECK(!selftest_prayer_on(&srv, "prayer_protectfrommelee"),
                       "running out clears every prayer");
        SELFTEST_CHECK(player->headicons == 0, "and the overhead icon");

        player->stat_level[MOCK230_STAT_PRAYER] = 1;
        player->stat_boosted[MOCK230_STAT_PRAYER] = 1;
    }

    fprintf(stderr, "mock230 selftest: run energy\n");
    {
        int energy_before;
        int weight;

        /*
         * The two writers of `option_run` have to agree.
         *
         * `::run` goes through `mock230_world_set_varp`; the run orb goes
         * through content (`[if_button,orbs:runbutton]` writes `%option_run`),
         * which is `SS_OP_POP_VARP` writing `player->varps[]` directly. The
         * second one skipped the engine mirror, so clicking the orb lit it,
         * transmitted the varp, and left `run_toggle` at 0 — every observable
         * part of the click was correct and the player still walked.
         *
         * Driven through the *opcode's* entry point rather than the setter,
         * because the setter was never the broken one. Checked in both
         * directions: a one-way mirror would pass the "on" half.
         */
        {
            int option_run = mock230_world_varp("option_run");
            int loaded =
                mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

            if( !loaded )
                loaded = mock230_scripts_load(
                    &srv, "../OSRS-Content/osrs239-content/server/scripts/build");

            SELFTEST_CHECK(option_run >= 0, "content should declare option_run");
            player->run_toggle = 0;
            player->varps[option_run] = 1;
            mock230_world_varp_written(&srv, option_run, 1);
            SELFTEST_CHECK(player->run_toggle == 1,
                           "a script write of option_run should arm run_toggle");
            player->varps[option_run] = 0;
            mock230_world_varp_written(&srv, option_run, 0);
            SELFTEST_CHECK(player->run_toggle == 0,
                           "and clearing it should disarm, got %d", player->run_toggle);

            /*
             * And the whole chain the player actually drives: the packet the
             * orb click sends, resolved to a content script by component name,
             * writing the varp, reaching `run_toggle`.
             *
             * Every link of this was individually correct while the feature was
             * broken — the packet arrived, the script was found and ran, and the
             * varp went out to light the orb. Only the last one was missing, so
             * only an end-to-end check finds it. `orbs:runbutton` is 160:28,
             * which is `160 << 16` and does not fit a compiled trigger key's 21
             * bits, so it resolves through the *named* half of
             * `mock230_scripts_run_if_button` — the reason this needs the
             * script pack loaded and cannot be faked with a trigger id.
             */
            if( loaded )
            {
                int uid = mock230_content_symbol(MOCK230_PACK_COMPONENT, "orbs:runbutton");
                uint8_t button[6];

                SELFTEST_CHECK(uid > 0, "the content pack should name orbs:runbutton");
                if( uid > 0 )
                {
                    /* p4 uid, p2 sub — handle_if_button_op's layout. */
                    button[0] = (uint8_t)(uid >> 24);
                    button[1] = (uint8_t)(uid >> 16);
                    button[2] = (uint8_t)(uid >> 8);
                    button[3] = (uint8_t)uid;
                    button[4] = 0xff;
                    button[5] = 0xff; /* sub = -1 */

                    player->run_toggle = 0;
                    player->varps[option_run] = 0;
                    mock230_world_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
                    SELFTEST_CHECK(player->varps[option_run] == 1,
                                   "clicking the run orb should set option_run, got %d",
                                   player->varps[option_run]);
                    SELFTEST_CHECK(player->run_toggle == 1,
                                   "and arm run_toggle — a lit orb over a walking player is "
                                   "the bug this catches");

                    mock230_world_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
                    SELFTEST_CHECK(player->run_toggle == 0 && player->varps[option_run] == 0,
                                   "clicking it again should turn run off (varp %d toggle %d)",
                                   player->varps[option_run], player->run_toggle);
                }
                mock230_scripts_free(&srv);
            }
        }

        steps_clear(player);
        player->run_energy = MOCK230_RUN_ENERGY_MAX;
        player->run_toggle = 1;

        /* Drain is per tile, not per tick, and it is the carried weight that
         * decides the rate. The starting kit is heavy enough that the loaded
         * rate has to be strictly worse than the bare one — if the weight
         * lookup ever returns 0 for everything, this is what notices. */
        weight = player_weight_grams(player);
        SELFTEST_CHECK(weight > 0, "the starting kit weighs something (%d g)", weight);

        steps_walk_to(player, player->x + 6, player->z);
        energy_before = player->run_energy;
        mock230_world_tick(&srv);
        SELFTEST_CHECK(player->move_count == 2, "running covers two tiles");
        {
            int spent = energy_before - player->run_energy;
            int weight_kg = weight / 1000 > 64 ? 64 : weight / 1000;
            /* Once per tick, NOT once per tile. Covering two tiles is the base
             * cost, so there is no `2 *` here — the reference charges the loss
             * in the else-branch of `stepsTaken < 2` (Player.ts:705-713). This
             * assertion previously encoded the doubled form. */
            int expect = 67 + (67 * weight_kg) / 64;
            SELFTEST_CHECK(spent == expect, "a running tick costs %d, got %d", expect, spent);
        }

        /* Standing still refills, and never past full. */
        steps_clear(player);
        energy_before = player->run_energy;
        mock230_world_tick(&srv);
        SELFTEST_CHECK(player->run_energy > energy_before, "standing still regenerates");
        player->run_energy = MOCK230_RUN_ENERGY_MAX;
        mock230_world_tick(&srv);
        SELFTEST_CHECK(player->run_energy == MOCK230_RUN_ENERGY_MAX, "regen clamps at full");

        /* Empty means walk, and the toggle goes with it — otherwise the orb
         * stays lit over a player who is plainly walking. */
        player->run_energy = 1;
        steps_walk_to(player, player->x + 6, player->z);
        mock230_world_tick(&srv);
        SELFTEST_CHECK(player->run_energy == 0, "the last of the energy is spent");
        SELFTEST_CHECK(player->run_toggle == 0, "running out clears the toggle");
        SELFTEST_CHECK(player->varps[mock230_world_varp("option_run")] == 0,
                       "and the varp the orb reads");
        mock230_world_tick(&srv);
        SELFTEST_CHECK(player->move_count == 1, "out of energy is one tile a tick");

        steps_clear(player);
        player->run_energy = MOCK230_RUN_ENERGY_MAX;
        player->run_toggle = 0;
    }

    fprintf(stderr, "mock230 selftest: rebuild on scene edge\n");
    {
        int zone_before = srv.zone_x;
        steps_clear(player);
        player->x = mock230_scene_origin(srv.zone_x) + 4; /* inside the 16-tile margin */
        mock230_world_tick(&srv);
        SELFTEST_CHECK(srv.zone_x != zone_before, "walking to the scene edge re-centres the scene");
        SELFTEST_CHECK(player->place_dirty == 0,
                       "the placement is consumed by the PLAYER_INFO it triggered");
        {
            int local_x = player->x - mock230_scene_origin(srv.zone_x);
            SELFTEST_CHECK(local_x >= 0 && local_x < MOCK230_SCENE_TILES,
                           "player sits inside the new scene, local_x=%d", local_x);
        }
    }

    fprintf(stderr, "mock230 selftest: equip / unequip\n");
    {
        /* A full helm claims head + hair + jaw, so it must blank the body kit
         * in all three appearance slots. */
        slot = selftest_find(player, 1155);
        SELFTEST_CHECK(slot >= 0, "bronze full helm is in the starting kit");
        equip_from_slot(&srv, slot);
        SELFTEST_CHECK(player->worn[MOCK230_WEAR_HEAD].obj_id == 1155, "helm reaches the head slot");
        SELFTEST_CHECK(player->inv[slot].obj_id == -1, "helm left the backpack");
        SELFTEST_CHECK((player->masks & MOCK230_PMASK_APPEARANCE) != 0,
                       "equipping re-sends the appearance");
        SELFTEST_CHECK((player->worn_dirty & (1u << MOCK230_WEAR_HEAD)) != 0,
                       "the worn slot is marked for the next partial update");

        /* Taking it off returns it to the first free backpack slot. */
        unequip_slot(&srv, MOCK230_WEAR_HEAD);
        SELFTEST_CHECK(player->worn[MOCK230_WEAR_HEAD].obj_id == -1, "head slot is empty again");
        SELFTEST_CHECK(selftest_find(player, 1155) >= 0, "helm is back in the backpack");
    }

    fprintf(stderr, "mock230 selftest: equipment level requirements\n");
    {
        /*
         * The requirement table is merged from two sources that disagree with
         * each other (the cache's own params and the Kronos import — see
         * a Kronos dump), so what is worth pinning here is that
         * the *merge* comes out right at both ends: a rune scimitar's Attack 40
         * comes from the cache, and a mithril scimitar's Attack 20 comes from
         * the overlay. mock230_pack checks the whole ladder; this checks that
         * the engine acts on it.
         */
        int rune = mock230_content_symbol(MOCK230_PACK_OBJ, "rune_scimitar");
        int mithril = mock230_content_symbol(MOCK230_PACK_OBJ, "mithril_scimitar");
        int bronze = mock230_content_symbol(MOCK230_PACK_OBJ, "bronze_scimitar");
        int free_slot = -1;
        int saved_attack = player->stat_level[MOCK230_STAT_ATTACK];

        for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
            if( player->inv[i].obj_id < 0 )
            {
                free_slot = i;
                break;
            }
        SELFTEST_CHECK(rune > 0 && mithril > 0 && bronze > 0,
                       "the scimitar ladder is in pack/obj.pack");
        SELFTEST_CHECK(free_slot >= 0, "a free backpack slot to test with");

        if( free_slot >= 0 && rune > 0 )
        {
            const struct Mock230ObjRequire* require = mock230_obj_require(rune);

            SELFTEST_CHECK(require && require->count == 1 &&
                               require->req[0].stat == MOCK230_STAT_ATTACK &&
                               require->req[0].level == 40,
                           "rune scimitar requires Attack 40 (from the cache's own params)");

            player->stat_level[MOCK230_STAT_ATTACK] = 1;
            unequip_slot(&srv, MOCK230_WEAR_WEAPON);
            inv_set(player, free_slot, rune, 1);
            equip_from_slot(&srv, free_slot);
            SELFTEST_CHECK(player->worn[MOCK230_WEAR_WEAPON].obj_id != rune,
                           "Attack 1 cannot wield a rune scimitar");
            SELFTEST_CHECK(player->inv[free_slot].obj_id == rune,
                           "and the refused item stays in the backpack");


            /* Boosted must not count — the reference reads the base level, so a
             * potion cannot lift you over a requirement. */
            player->stat_boosted[MOCK230_STAT_ATTACK] = 99;
            equip_from_slot(&srv, free_slot);
            SELFTEST_CHECK(player->worn[MOCK230_WEAR_WEAPON].obj_id != rune,
                           "a boost does not satisfy a requirement");
            player->stat_boosted[MOCK230_STAT_ATTACK] = 1;

            player->stat_level[MOCK230_STAT_ATTACK] = 40;
            equip_from_slot(&srv, free_slot);
            SELFTEST_CHECK(player->worn[MOCK230_WEAR_WEAPON].obj_id == rune,
                           "Attack 40 can");
            unequip_slot(&srv, MOCK230_WEAR_WEAPON);
        }

        /* Mithril 20 is the overlay's, not the cache's — the cache says nothing
         * about mithril, which is the whole reason the overlay exists. */
        if( free_slot >= 0 && mithril > 0 )
        {
            const struct Mock230ObjRequire* require = mock230_obj_require(mithril);

            SELFTEST_CHECK(require && require->count == 1 &&
                               require->req[0].stat == MOCK230_STAT_ATTACK &&
                               require->req[0].level == 20,
                           "mithril scimitar requires Attack 20 (from the .obj overlay)");
            player->stat_level[MOCK230_STAT_ATTACK] = 19;
            for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                if( player->inv[i].obj_id == mithril )
                    inv_set(player, i, -1, 0);
            inv_set(player, free_slot, mithril, 1);
            equip_from_slot(&srv, free_slot);
            SELFTEST_CHECK(player->worn[MOCK230_WEAR_WEAPON].obj_id != mithril,
                           "Attack 19 cannot wield a mithril scimitar");
            player->stat_level[MOCK230_STAT_ATTACK] = 20;
            equip_from_slot(&srv, free_slot);
            SELFTEST_CHECK(player->worn[MOCK230_WEAR_WEAPON].obj_id == mithril,
                           "Attack 20 can");
            unequip_slot(&srv, MOCK230_WEAR_WEAPON);
        }

        /* And nothing below steel is gated at all, which is the case that would
         * break every new character if the importer ever emitted a level 1 row. */
        if( free_slot >= 0 && bronze > 0 )
        {
            SELFTEST_CHECK(mock230_obj_require(bronze) == NULL,
                           "a bronze scimitar has no requirement");
            player->stat_level[MOCK230_STAT_ATTACK] = 1;
            for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                if( player->inv[i].obj_id == bronze )
                    inv_set(player, i, -1, 0);
            inv_set(player, free_slot, bronze, 1);
            equip_from_slot(&srv, free_slot);
            SELFTEST_CHECK(player->worn[MOCK230_WEAR_WEAPON].obj_id == bronze,
                           "and Attack 1 wields it");
            unequip_slot(&srv, MOCK230_WEAR_WEAPON);
        }

        player->stat_level[MOCK230_STAT_ATTACK] = saved_attack;
        player->stat_boosted[MOCK230_STAT_ATTACK] = saved_attack;
        if( free_slot >= 0 )
            inv_set(player, free_slot, -1, 0);
    }

    fprintf(stderr, "mock230 selftest: the wield refusal is content's, words and all\n");
    {
        /*
         * The sentence, read off the wire.
         *
         * The engine sends the stat *id* and the level; `[proc,equip_level_message]`
         * turns the first into a word through `~stat_name`, which reads
         * `general/configs/stat.enum`. That chain replaced a 23-string table in
         * mock230_equipment.c, and the only way to know a chain is connected is to
         * look at what comes out the far end — the section above cannot, because it
         * runs with no script pack and every message in it is a no-op.
         *
         * It also catches the thing that is easy to get wrong in RuneScript: `<...>`
         * interpolates a *variable*, so a `<~proc(...)>` written inside a string
         * reaches the player as those literal characters. That compiles, runs, and
         * says `<~stat_name($stat)>` to the player.
         */
        static struct Mock230Capture capture;
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");
        int rune = mock230_content_symbol(MOCK230_PACK_OBJ, "rune_scimitar");
        int saved_attack = player->stat_level[MOCK230_STAT_ATTACK];

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");
        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else if( rune > 0 )
        {
            int said = 0;
            int literal = 0;

            player->stat_level[MOCK230_STAT_ATTACK] = 1;
            mock230_capture_begin(&srv, &capture);
            SELFTEST_CHECK(!mock230_equipment_may_wear(&srv, rune),
                           "Attack 1 may not wear a rune scimitar");
            mock230_capture_end(&srv);

            for( int i = mock230_capture_find(&capture, 90 /* MESSAGE_GAME */, 0); i >= 0;
                 i = mock230_capture_find(&capture, 90, i + 1) )
            {
                /* payload: one type byte, then a NUL-terminated string. */
                const struct Mock230CapturedPacket* packet = &capture.packets[i];
                const char* text = (const char*)packet->data + 1;

                if( packet->len < 2 || packet->data[packet->len - 1] != 0 )
                    continue;
                if( strstr(text, "Attack level of 40") )
                    said = 1;
                if( strstr(text, "<~") )
                    literal = 1;
            }
            SELFTEST_CHECK(said,
                           "the refusal should name the skill and the level — "
                           "\"an Attack level of 40\"");
            SELFTEST_CHECK(!literal,
                           "and must not carry an uninterpolated `<~proc(...)>` to "
                           "the player");
        }
        player->stat_level[MOCK230_STAT_ATTACK] = saved_attack;
        player->stat_boosted[MOCK230_STAT_ATTACK] = saved_attack;
        if( loaded )
            mock230_scripts_free(&srv);
    }

    fprintf(stderr, "mock230 selftest: two-handed weapon evicts the shield\n");
    {
        int shield_slot = selftest_find(player, 1189); /* bronze kiteshield */
        int bow_slot;
        equip_from_slot(&srv, shield_slot);
        SELFTEST_CHECK(player->worn[MOCK230_WEAR_SHIELD].obj_id == 1189, "kiteshield equipped");

        bow_slot = selftest_find(player, 841); /* shortbow: wearpos 3, blocks 5 */
        equip_from_slot(&srv, bow_slot);
        SELFTEST_CHECK(player->worn[MOCK230_WEAR_WEAPON].obj_id == 841, "shortbow wielded");
        SELFTEST_CHECK(player->worn[MOCK230_WEAR_SHIELD].obj_id == -1,
                       "the two-handed bow evicted the shield");
        SELFTEST_CHECK(selftest_find(player, 1189) >= 0, "the shield went back to the backpack");
    }

    fprintf(stderr, "mock230 selftest: inventory drag\n");
    {
        uint8_t payload[9];
        struct RSAreaBuf buf;
        int from_obj;
        int to_obj;

        /* Pick two occupied, distinct slots. */
        int from_slot = -1;
        int to_slot = -1;
        for( int i = 0; i < MOCK230_INV_SLOTS && to_slot < 0; i++ )
        {
            if( player->inv[i].obj_id < 0 )
                continue;
            if( from_slot < 0 )
                from_slot = i;
            else
                to_slot = i;
        }
        SELFTEST_CHECK(from_slot >= 0 && to_slot >= 0, "two occupied slots to swap");
        from_obj = player->inv[from_slot].obj_id;
        to_obj = player->inv[to_slot].obj_id;

        rsab_wrap(&buf, payload, sizeof(payload));
        rsab_p4(&buf, mock230_ids()->com_inventory_items);
        rsab_p2(&buf, from_slot);
        rsab_p2(&buf, to_slot);
        rsab_p1(&buf, 0);
        mock230_world_handle(player, PKTOUT_NAME_INV_BUTTOND, payload, (int)rsab_len(&buf));

        SELFTEST_CHECK(player->inv[from_slot].obj_id == to_obj, "slots swapped (from)");
        SELFTEST_CHECK(player->inv[to_slot].obj_id == from_obj, "slots swapped (to)");
        SELFTEST_CHECK((player->inv_dirty & (1u << from_slot)) != 0, "from slot marked dirty");
        SELFTEST_CHECK((player->inv_dirty & (1u << to_slot)) != 0, "to slot marked dirty");
    }

    fprintf(stderr, "mock230 selftest: npcs roam inside their radius\n");
    {
        int moved = 0;
        for( int tick = 0; tick < 200; tick++ )
        {
            advance_npcs(&srv);
            for( int i = 0; i < MOCK230_NPC_MAX; i++ )
            {
                struct Mock230Npc* npc = &srv.npcs[i];
                if( !npc->active )
                    continue;
                if( npc->step_dir >= 0 )
                    moved++;
                /* A patroller has a route, not a radius, and Hans's goes right
                 * round the castle — checking him against `wander_radius` (0,
                 * since he declares no `wanderrange`) asserts the opposite of
                 * what his content says. */
                if( npc->mode == MOCK230_NPCMODE_PATROL )
                    continue;
                SELFTEST_CHECK(npc->x - npc->spawn_x <= npc->wander_radius &&
                                   npc->spawn_x - npc->x <= npc->wander_radius,
                               "npc %d (type %d) stayed inside its wander radius %d: at %d,%d "
                               "from spawn %d,%d",
                               i, npc->type, npc->wander_radius, npc->x, npc->z, npc->spawn_x,
                               npc->spawn_z);
            }
            srv.tick++;
        }
        SELFTEST_CHECK(moved > 0, "at least one npc roamed over 200 ticks");

        /*
         * Hans walks his route.
         *
         * Three things at once, and the middle one is why this exists: content
         * declares a patrol, the engine puts the npc in patrol mode, and the
         * mode moves him along the ring. He used to be a wanderer with
         * `wanderrange=5` standing in for an unimplemented patrol, so "Hans
         * moves" was true the whole time and told you nothing.
         *
         * The first waypoint is checked as an ABSOLUTE tile, because the whole
         * content format hangs on `mapx * 64 + localx` — get that wrong and the
         * route is a map square away, still walking, still looping, and nowhere
         * near the castle.
         */
        {
            int hans = selftest_find_npc(&srv, 3105);

            SELFTEST_CHECK(hans >= 0, "the roster should include Hans");
            if( hans >= 0 )
            {
                struct Mock230Npc* npc = &srv.npcs[hans];
                const struct Mock230NpcDef* def = npc->def;
                int start_index = npc->patrol_index;
                int advanced = 0;

                SELFTEST_CHECK(npc->mode == MOCK230_NPCMODE_PATROL,
                               "content should put Hans on patrol, got mode %d", npc->mode);
                SELFTEST_CHECK(def && def->patrol_count == 10,
                               "with LostCity's ten waypoints, got %d",
                               def ? def->patrol_count : -1);
                if( def && def->patrol_count > 0 )
                {
                    SELFTEST_CHECK(def->patrol[0].x == 3207 && def->patrol[0].z == 3233,
                                   "0_50_50_7_33 is tile 3207,3233, got %d,%d", def->patrol[0].x,
                                   def->patrol[0].z);
                    SELFTEST_CHECK(def->patrol[3].pause == 10,
                                   "and patrol4 pauses for ten ticks, got %d", def->patrol[3].pause);
                }

                for( int tick = 0; tick < 600 && !advanced; tick++ )
                {
                    advance_npcs(&srv);
                    srv.tick++;
                    if( npc->patrol_index != start_index )
                        advanced = 1;
                }
                SELFTEST_CHECK(advanced,
                               "Hans should reach a waypoint and move on; he is at %d,%d",
                               npc->x, npc->z);
            }
        }
        {
            /* Bob (10619) is `moverestrict=nomove` in the content tree, which
             * is what a zero wander radius has to come out as. Found by type
             * rather than by slot: slot order follows the map files now.
             *
             * This used to assert on Hans (3105). Hans *patrols* the castle
             * grounds — LostCity gives him `moverestrict=outdoors` — so pinning
             * him here was asserting a content bug rather than an engine rule.
             * Bob stands behind an axe counter and genuinely does not move. */
            int bob = selftest_find_npc(&srv, 10619);

            SELFTEST_CHECK(bob >= 0, "the roster should include Bob");
            if( bob >= 0 )
                SELFTEST_CHECK(srv.npcs[bob].wander_radius == 0 &&
                                   srv.npcs[bob].x == srv.npcs[bob].spawn_x,
                               "a nomove npc never moves");
        }
    }

    /*
     * Aggression, pursuit and the leash — the three halves of "a monster
     * fights you", and each of them was broken in a way the other two hid.
     *
     * The pursuit half is the one worth stating. An npc closing on a player got
     * one greedy step per tick and nothing behind it, so the first wall between
     * the two ended the chase permanently: the npc walked into the same tile
     * every tick, in combat, facing the player, while the player strolled away.
     * In open ground it looked fine, which is why it survived — and open ground
     * is exactly where a test that named its own tiles would have run. So this
     * one *finds* a chase that has to turn a corner and asserts the npc still
     * arrives.
     */
    fprintf(stderr, "mock230 selftest: an npc pursues the player\n");
    {
        int goblin = selftest_find_npc(&srv, 3028);

        SELFTEST_CHECK(goblin >= 0, "the roster should include a goblin");
        if( goblin >= 0 )
        {
            struct Mock230Npc* npc = &srv.npcs[goblin];
            int home_x = npc->spawn_x;
            int home_z = npc->spawn_z;
            int maxrange = npc->def ? npc->def->maxrange : 7;
            int flee_x[MOCK230_STEP_MAX];
            int flee_z[MOCK230_STEP_MAX];
            int flee_steps = 0;

            /*
             * The router, on a step it cannot take straight.
             *
             * Found rather than named, for the reason the movement section
             * gives: a hardcoded pair of tiles is a fact about today's cache.
             * What is needed is any tile whose neighbour toward a target is
             * blocked while a route to that target exists — which is precisely
             * the case the greedy stepper could not answer.
             */
            {
                struct Mock230Npc probe;
                int path_x[MOCK230_STEP_MAX];
                int path_z[MOCK230_STEP_MAX];
                int found = 0;

                memset(&probe, 0, sizeof(probe));
                probe.level = 0;
                for( int ox = -30; ox <= 30 && !found; ox++ )
                {
                    for( int oz = -30; oz <= 30 && !found; oz++ )
                    {
                        int from_x = home_x + ox;
                        int from_z = home_z + oz;
                        int to_x = from_x + 4;
                        int to_z = from_z;

                        if( !mock230_scene_contains(from_x, from_z) ||
                            !mock230_scene_contains(to_x, to_z) )
                            continue;
                        /* Straight step refused... */
                        if( mock230_scene_can_step(0, from_x, from_z, 4 /* east */) )
                            continue;
                        /* ...but a way round exists. */
                        if( mock230_scene_route(0, from_x, from_z, to_x, to_z, path_x, path_z,
                                                MOCK230_STEP_MAX) <= 0 )
                            continue;

                        found = 1;
                        probe.x = from_x;
                        probe.z = from_z;
                        probe.step_dir = -1;
                        SELFTEST_CHECK(mock230_world_npc_walk_to(&probe, to_x, to_z),
                                       "an npc whose straight step is blocked still moves");
                        for( int step = 0; step < 40 && (probe.x != to_x || probe.z != to_z);
                             step++ )
                            mock230_world_npc_walk_to(&probe, to_x, to_z);
                        SELFTEST_CHECK(probe.x == to_x && probe.z == to_z,
                                       "and walks round to %d,%d, got %d,%d", to_x, to_z, probe.x,
                                       probe.z);
                    }
                }
                SELFTEST_CHECK(found, "a blocked-but-routable tile pair to test the router with");
            }

            /*
             * The route the player runs away down.
             *
             * A *route* rather than a straight line, and found rather than
             * named: the goblin's spawn is where the map put it, and requiring
             * eight clear tiles in a compass direction is a fact about
             * Lumbridge that this test has no business depending on. Walking
             * the player down a real path also means the chase has corners in
             * it, which is the case that used to end it.
             */
            {
                for( int ox = -12; ox <= 12 && flee_steps < 6; ox++ )
                {
                    for( int oz = -12; oz <= 12 && flee_steps < 6; oz++ )
                    {
                        int steps;

                        if( !mock230_scene_contains(home_x + ox, home_z + oz) )
                            continue;
                        steps = mock230_scene_route(0, home_x, home_z, home_x + ox, home_z + oz,
                                                    flee_x, flee_z, MOCK230_STEP_MAX);
                        /* Long enough to be a chase, short enough to stay
                         * inside the leash the next block goes on to test. */
                        if( steps >= 6 && steps <= maxrange - 1 )
                            flee_steps = steps;
                    }
                }
                SELFTEST_CHECK(flee_steps >= 6,
                               "a walkable route away from the goblin's spawn to run down");
            }

            if( flee_steps >= 6 )
            {
                int worst = 0;

                /* Put it home and stand on the first tile of the route.
                 * `selftest_park_player` clears every combat target, so
                 * aggression starts from nothing — which is the first thing
                 * under test. */
                npc->x = home_x;
                npc->z = home_z;
                npc->hitpoints = npc->max_hitpoints;
                npc->death_tick = -1;
                selftest_park_player(&srv, flee_x[0], flee_z[0]);
                player->hitpoints = player->max_hitpoints;

                mock230_world_tick(&srv);
                SELFTEST_CHECK(npc->combat_target == 0,
                               "an aggressive npc takes the player as a target");

                /*
                 * Walk away one tile a tick and check it keeps up.
                 *
                 * The player's own target is cleared each tick because a player
                 * walking off has clicked somewhere, and a click ends combat
                 * (`mock230_combat_stop_player`, from the move handler) — the
                 * npc's target is the one that has to survive.
                 *
                 * Two tiles rather than one: the npc moves in phase 4 and the
                 * player in phase 5, so a walking player is always a tile ahead
                 * within a tick. Anything worse than that is a chase falling
                 * behind, which is the failure.
                 */
                for( int step = 1; step < flee_steps; step++ )
                {
                    int gap;

                    mock230_combat_stop_player(&srv);
                    mock230_world_steps_clear(player);
                    player->x = flee_x[step];
                    player->z = flee_z[step];
                    mock230_world_tick(&srv);

                    gap = distance_to_rect(npc->x, npc->z, player->x, player->z, 1, 1);
                    if( gap > worst )
                        worst = gap;
                }
                /* Three, not one: the route has corners in it, and a chaser
                 * that has to come round one the player cut loses a tile until
                 * it is back on the straight. What matters is that it is
                 * following rather than falling away, which the settle below
                 * pins down exactly. */
                SELFTEST_CHECK(worst <= 3, "the goblin keeps up with a walking player, worst gap %d",
                               worst);
                SELFTEST_CHECK(npc->x != home_x || npc->z != home_z,
                               "which means it left its spawn tile");

                /* Stand still and it arrives. This is the assertion the whole
                 * section exists for: before the router, a chase that met one
                 * wall never closed again however long you waited. */
                for( int i = 0; i < 10; i++ )
                {
                    player->hitpoints = player->max_hitpoints;
                    mock230_world_tick(&srv);
                }
                SELFTEST_CHECK(distance_to_rect(npc->x, npc->z, player->x, player->z, 1, 1) <= 1,
                               "and catches a player who stops, at %d,%d vs %d,%d", npc->x, npc->z,
                               player->x, player->z);
                SELFTEST_CHECK(npc->combat_target == 0, "still hunting them");

                /*
                 * Past the leash it gives up — `maxrange`, measured from the
                 * spawn tile, the reference's `targetWithinMaxRange`. Without
                 * it a goblin follows you to the next county, which is the
                 * behaviour pursuit would otherwise have introduced.
                 */
                mock230_combat_stop_player(&srv);
                mock230_world_steps_clear(player);
                player->x = home_x + maxrange + 4;
                player->z = home_z;
                mock230_world_tick(&srv);
                SELFTEST_CHECK(npc->combat_target == -1,
                               "and drops a target dragged past maxrange %d", maxrange);

                /*
                 * Then it goes home. An npc that gave up outside its wander
                 * radius used to be stuck there forever: every roam candidate
                 * outside the radius is rejected, so from outside it there is
                 * no legal roll to make.
                 */
                for( int i = 0; i < 60; i++ )
                    mock230_world_tick(&srv);
                SELFTEST_CHECK(distance_to_rect(npc->x, npc->z, home_x, home_z, 1, 1) <=
                                   npc->wander_radius,
                               "and wanders home to within its radius %d, at %d,%d from %d,%d",
                               npc->wander_radius, npc->x, npc->z, home_x, home_z);
            }
        }
    }

    /*
     * The bank.
     *
     * Every one of these is reachable from the client only through a click the
     * rev-230 UI cannot currently produce (see docs/mock230_bank.md §6), so
     * this is where the arithmetic is actually exercised. It is also the only
     * place the varbit packing is checked: a bank setting is a bit range inside
     * a shared varp, and writing one as a whole varp destroys the others in it
     * without any symptom until the interface is looked at.
     */
    fprintf(stderr, "mock230 selftest: bank varbit packing\n");
    {
        int basevar = 0;
        int lsb = 0;
        int msb = 0;

        selftest_reset_world(&srv, player, 402, 402);
        /* 1,410 in rev 239; the bank grows with almost every OldSchool update, so
         * this is a claim about cache.osrs239 and not about banks in general. */
        SELFTEST_CHECK(mock230_bank_inv_size(ids->inv_bank) == 1410,
                       "the cache should say the bank has 1410 slots, got %d",
                       mock230_bank_inv_size(ids->inv_bank));

        /* The two that share varp 115 — the case a whole-varp write breaks. */
        SELFTEST_CHECK(
            mock230_bank_varbit_resolve(ids->varbit_bank_withdrawnotes, &basevar, &lsb,
                                        &msb) &&
                basevar == 115 && lsb == 0 && msb == 0,
            "withdraw-notes should resolve to varp 115 bit 0, got varp %d bits %d..%d",
            basevar, lsb, msb);
        SELFTEST_CHECK(
            mock230_bank_varbit_resolve(ids->varbit_bank_currenttab, &basevar, &lsb,
                                        &msb) &&
                basevar == 115 && lsb == 4 && msb == 7,
            "current-tab should resolve to varp 115 bits 4..7, got varp %d bits %d..%d",
            basevar, lsb, msb);

        mock230_bank_set_varbit(&srv, ids->varbit_bank_currenttab, 9);
        mock230_bank_set_varbit(&srv, ids->varbit_bank_withdrawnotes, 1);
        SELFTEST_CHECK(mock230_bank_get_varbit(&srv, ids->varbit_bank_currenttab) == 9,
                       "setting the neighbouring bit must not disturb the tab");
        SELFTEST_CHECK(player->varps[115] == ((9 << 4) | 1),
                       "both varbits should share varp 115, got %d", player->varps[115]);

        /* The widest one. 3960 is bits 1..31, which is where a naive
         * `1 << (msb - lsb + 1)` shifts by 32 and is undefined. */
        mock230_bank_set_varbit(&srv, ids->varbit_bank_requestedquantity, 100000);
        SELFTEST_CHECK(
            mock230_bank_get_varbit(&srv, ids->varbit_bank_requestedquantity) == 100000,
            "a 31-bit varbit should round-trip, got %d",
            mock230_bank_get_varbit(&srv, ids->varbit_bank_requestedquantity));
    }

    /*
     * Nothing wrote a shared container whole — the varp side of §7.5.
     *
     * The section above proves that *this* varp is patched correctly. This one
     * proves that nothing anywhere in the run did the other thing, which is the
     * assertion the bank bug needed and did not have: every check that shipped
     * with it passed, because the write was legal and the damage was to a
     * neighbour nobody was looking at.
     *
     * The count spans the whole selftest — every trigger, every cheat, every
     * packet handler already exercised above — so it fails on a write from any
     * of the three writers sscompile cannot see.
     */
    fprintf(stderr, "mock230 selftest: no whole-varp write to a carrier\n");
    {
        int last = -1;
        int writes = mock230_world_carrier_writes(&last);

        /* The set is the cache's, not a number typed here: if it were empty the
         * check below would pass for the wrong reason. */
        SELFTEST_CHECK(mock230_varbit_carrier_bits(115) > 0,
                       "varp 115 should be known to carry varbits, got %d",
                       mock230_varbit_carrier_bits(115));
        SELFTEST_CHECK(mock230_varbit_carrier_bits(MOCK230_VARP_CACHE_MAX + 1) == 0,
                       "a server-allocated varp carries nothing");
        SELFTEST_CHECK(writes == 0,
                       "%d whole-varp write(s) landed on a carrier varp (last: %d)", writes,
                       last);
    }

    fprintf(stderr, "mock230 selftest: bank deposit and withdraw\n");
    {
        struct Mock230Bank* bank = &player->bank;
        int coins_before;

        selftest_reset_world(&srv, player, 402, 402);
        /* The fixture this whole section deposits and withdraws is content's
         * now — see selftest_seed_new_player. */
        if( !selftest_seed_new_player(&srv) )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
            goto bank_seeded_done;
        }
        coins_before = mock230_bank_count(&srv, 995);
        SELFTEST_CHECK(coins_before > 0, "the starting bank should hold coins");
        /*
         * And that the seeding went through the bank rather than past it.
         *
         * The C this replaced assigned `bank.slots[i]` directly, which leaves
         * `dirty` clear — the rows existed but nothing would transmit them.
         * Asserted here rather than in the login section because phase 10's
         * flush drains the flag inside the same tick, so this is the only
         * place it is still observable.
         */
        SELFTEST_CHECK(player->bank.dirty,
                       "and a seeded bank should be marked for transmit");

        /* Deposit the backpack's coin stack onto the bank's. */
        {
            int slot = -1;
            int held;

            for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                if( player->inv[i].obj_id == 995 )
                    slot = i;
            SELFTEST_CHECK(slot >= 0, "the starting kit should include coins");
            held = slot >= 0 ? player->inv[slot].count : 0;
            mock230_bank_deposit(&srv, slot, held);
            SELFTEST_CHECK(mock230_bank_count(&srv, 995) == coins_before + held,
                           "a deposit should land on the existing stack");
            SELFTEST_CHECK(slot >= 0 && player->inv[slot].obj_id < 0,
                           "and empty the backpack slot it came from");
        }

        /* Withdraw a non-stackable obj: one backpack slot each, and no more
         * than there is room for. */
        {
            int free_before = 0;
            int slot = -1;

            for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                if( player->inv[i].obj_id < 0 )
                    free_before++;
            for( int i = 0; i < bank->size; i++ )
                if( bank->slots[i].obj_id == 1511 )
                    slot = i;
            SELFTEST_CHECK(slot >= 0, "the starting bank should hold logs");
            if( slot >= 0 )
            {
                int moved = mock230_bank_withdraw(&srv, slot, 3);
                int free_after = 0;

                for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                    if( player->inv[i].obj_id < 0 )
                        free_after++;
                SELFTEST_CHECK(moved == 3, "three logs should come out, got %d", moved);
                SELFTEST_CHECK(free_before - free_after == 3,
                               "a non-stackable withdraw takes one slot each, took %d",
                               free_before - free_after);
            }
        }

        /*
         * Note mode.
         *
         * Swordfish (373) is not stackable and its note (374) is, which is the
         * whole point of the feature: 20 of them need 20 backpack slots as
         * items and one as a note. Both directions of the link come out of the
         * obj record itself — see mock230_objinfo.
         */
        {
            const struct Mock230ObjInfo* fish = mock230_objinfo(373);
            int slot = -1;

            SELFTEST_CHECK(fish->cert_id == 374,
                           "the cache should give swordfish note 374, got %d",
                           fish->cert_id);
            SELFTEST_CHECK(mock230_objinfo(374)->noted_template >= 0 &&
                               mock230_objinfo(374)->noted_id == 373,
                           "and the note should point back at the fish");
            SELFTEST_CHECK(mock230_objinfo(314)->cert_id < 0,
                           "a feather has no note form, and that has to be sayable");

            for( int i = 0; i < bank->size; i++ )
                if( bank->slots[i].obj_id == 373 )
                    slot = i;
            bank->note_mode = 1;
            if( slot >= 0 )
                mock230_bank_withdraw(&srv, slot, 20);
            bank->note_mode = 0;
            {
                int found = 0;

                for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                    if( player->inv[i].obj_id == 374 )
                        found = player->inv[i].count;
                SELFTEST_CHECK(found == 20,
                               "20 noted swordfish should be in one slot, got %d", found);
            }
            /* And back in un-noted: a bank never holds two forms of one obj. */
            {
                int slot_of_note = -1;

                for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                    if( player->inv[i].obj_id == 374 )
                        slot_of_note = i;
                mock230_bank_deposit(&srv, slot_of_note, 20);
                SELFTEST_CHECK(mock230_bank_count(&srv, 374) == 0,
                               "a deposited note must not make a second bank stack");
                SELFTEST_CHECK(mock230_bank_count(&srv, 373) == 60,
                               "it should merge back onto the un-noted stack, got %d",
                               mock230_bank_count(&srv, 373));
            }
        }
    bank_seeded_done:;
    }

    fprintf(stderr, "mock230 selftest: bank op ladder\n");
    {
        struct Mock230Bank* bank = &player->bank;

        selftest_reset_world(&srv, player, 402, 402);

        /*
         * The conditional row list from script 669. With the default quantity
         * on "1", the row that would say Withdraw-1 is omitted, so the ladder
         * runs Default, 5, 10, X, All, All-but-1 — and "All" is op 5.
         */
        bank->quantity_mode = ids->bank_qty_1;
        bank->requested_quantity = 0;
        SELFTEST_CHECK(mock230_bank_quantity_for_op(&srv, 1, 100, 0) == 1,
                       "op 1 is the default quantity");
        SELFTEST_CHECK(mock230_bank_quantity_for_op(&srv, 2, 100, 0) == 5, "then 5");
        SELFTEST_CHECK(mock230_bank_quantity_for_op(&srv, 3, 100, 0) == 10, "then 10");
        SELFTEST_CHECK(mock230_bank_quantity_for_op(&srv, 4, 100, 0) == MOCK230_BANK_ASK,
                       "then the X prompt");
        SELFTEST_CHECK(mock230_bank_quantity_for_op(&srv, 5, 100, 0) == 100, "then All");
        SELFTEST_CHECK(mock230_bank_quantity_for_op(&srv, 6, 100, 0) == 99,
                       "then All-but-1");

        /* Switch the default to All and the ladder shifts: the All row is the
         * one omitted now, and Withdraw-1 reappears at op 2. */
        bank->quantity_mode = ids->bank_qty_all;
        SELFTEST_CHECK(mock230_bank_quantity_for_op(&srv, 1, 100, 0) == 100,
                       "op 1 is still the default, which is now All");
        SELFTEST_CHECK(mock230_bank_quantity_for_op(&srv, 2, 100, 0) == 1,
                       "and Withdraw-1 is back at op 2");
        SELFTEST_CHECK(mock230_bank_quantity_for_op(&srv, 6, 100, 0) == 99,
                       "All-but-1 stays last");

        /* The side panel numbers its rows with constants, so it does not move. */
        SELFTEST_CHECK(mock230_bank_quantity_for_op(&srv, 3, 100, 1) == 1,
                       "side op 3 is always Deposit-1");
        SELFTEST_CHECK(mock230_bank_quantity_for_op(&srv, 8, 100, 1) == 100,
                       "side op 8 is always Deposit-All");
    }

    fprintf(stderr, "mock230 selftest: bank open sends both halves\n");
    {
        static struct Mock230Capture capture;
        /* IF_OPENSUB twice (main then side), IF_SETEVENTS to make the grids
         * clickable, then the container. */
        static const int k_open[] = {
            6 /* IF_OPENSUB */, 6 /* IF_OPENSUB */, 47 /* IF_SETEVENTS */,
            10 /* UPDATE_INV_FULL */,
        };

        selftest_reset_world(&srv, player, 402, 402);
        mock230_capture_begin(&srv, &capture);
        mock230_bank_open(&srv);
        mock230_capture_end(&srv);
        SELFTEST_CHECK(mock230_capture_has_sequence(&capture, k_open, 4),
                       "opening the bank should mount, unlock, then fill");
        SELFTEST_CHECK(player->bank.open, "and leave the bank marked open");

        /* Insert mode moves a slot without displacing its neighbour. */
        {
            int first = player->bank.slots[0].obj_id;
            int second = player->bank.slots[1].obj_id;
            int third = player->bank.slots[2].obj_id;

            player->bank.insert_mode = 1;
            mock230_bank_move_slot(&srv, 0, 2);
            SELFTEST_CHECK(player->bank.slots[0].obj_id == second &&
                               player->bank.slots[1].obj_id == third &&
                               player->bank.slots[2].obj_id == first,
                           "insert should shuffle, not swap");
            player->bank.insert_mode = 0;
            mock230_bank_move_slot(&srv, 2, 0);
            SELFTEST_CHECK(player->bank.slots[0].obj_id == first &&
                               player->bank.slots[2].obj_id == second,
                           "swap should exchange the two slots");
        }

        mock230_bank_close(&srv);
        SELFTEST_CHECK(!player->bank.open, "closing should clear the open flag");
    }

    fprintf(stderr, "mock230 selftest: npc modes\n");
    {
        /*
         * `npc_setmode` was in front of 122 LostCity files, more than anything
         * else left. The opcode is one line; what it implies is a *standing
         * state* the npc holds across ticks, which is why phase 4 now runs one
         * per npc per tick.
         *
         * `playerfollow` is the clearest thing to assert on, because it has a
         * stopping condition: a mover that merely walks toward the player looks
         * right for several ticks and then stands on top of them.
         */
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            const struct SSVM_Script* script;
            int chicken = mock230_content_symbol(MOCK230_PACK_NPC, "chicken");
            int follower = -1;

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_npc_mode_follow]");
            if( !script )
            {
                fprintf(stderr, "  SKIP  mode scripts not in this pack\n");
            }
            else
            {
                int start_range;
                int end_range;

                mock230_scripts_run_script(&srv, script->id);
                for( int i = 0; i < MOCK230_NPC_MAX; i++ )
                    if( srv.npcs[i].active && srv.npcs[i].type == chicken &&
                        srv.npcs[i].x == 3230 )
                        follower = i;
                SELFTEST_CHECK(follower >= 0, "the follower should be spawned");

                if( follower >= 0 )
                {
                    start_range = srv.npcs[follower].x - player->x;
                    if( start_range < 0 )
                        start_range = -start_range;
                    SELFTEST_CHECK(srv.npcs[follower].mode == MOCK230_NPCMODE_PLAYERFOLLOW,
                                   "npc_setmode should store the mode, got %d",
                                   srv.npcs[follower].mode);

                    for( int i = 0; i < 30; i++ )
                        mock230_world_tick(&srv);
                    end_range = srv.npcs[follower].x - player->x;
                    if( end_range < 0 )
                        end_range = -end_range;
                    SELFTEST_CHECK(end_range < start_range,
                                   "playerfollow should close the distance, %d -> %d",
                                   start_range, end_range);
                    SELFTEST_CHECK(end_range <= 1,
                                   "and reach the player, got %d tiles", end_range);

                    /* And stop there rather than walking onto them. */
                    for( int i = 0; i < 5; i++ )
                        mock230_world_tick(&srv);
                    SELFTEST_CHECK(srv.npcs[follower].x != player->x ||
                                       srv.npcs[follower].z != player->z,
                                   "playerfollow should stop beside the player, not on them");

                    /* `none` has to actually stop it: park the npc, set none,
                     * and assert it does not move. Without the wander default
                     * this would be indistinguishable from never having set a
                     * mode. */
                    srv.npcs[follower].mode = MOCK230_NPCMODE_NONE;
                    {
                        int px = srv.npcs[follower].x;
                        int pz = srv.npcs[follower].z;

                        for( int i = 0; i < 10; i++ )
                            mock230_world_tick(&srv);
                        SELFTEST_CHECK(srv.npcs[follower].x == px && srv.npcs[follower].z == pz,
                                       "mode none should hold the npc still");
                    }
                    srv.npcs[follower].active = 0;
                }

                script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_npc_mode_op]");
                if( script )
                {
                    int runner = -1;

                    player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
                    mock230_scripts_run_script(&srv, script->id);
                    for( int i = 0; i < MOCK230_NPC_MAX; i++ )
                        if( srv.npcs[i].active && srv.npcs[i].type == chicken &&
                            srv.npcs[i].x == 3226 )
                            runner = i;
                    SELFTEST_CHECK(runner >= 0, "the opplayer2 npc should be spawned");

                    for( int i = 0; i < 30; i++ )
                        mock230_world_tick(&srv);
                    SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1,
                                   "opplayer2 should fire [ai_opplayer2] once on arrival, got %d",
                                   player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
                    /* The errand is over, so it must not re-fire. An npc left
                     * in the mode would trigger every tick from then on. */
                    for( int i = 0; i < 10; i++ )
                        mock230_world_tick(&srv);
                    SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1,
                                   "and drop back to none rather than firing again, got %d",
                                   player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
                    if( runner >= 0 )
                        srv.npcs[runner].active = 0;
                }
            }

            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: npc queues and timers\n");
    {
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            const struct SSVM_Script* script;
            int chicken = mock230_content_symbol(MOCK230_PACK_NPC, "chicken");
            int added = -1;

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_npc_queue]");
            SELFTEST_CHECK(script != NULL, "[proc,selftest_npc_queue] should be in the pack");
            if( script )
            {
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
                mock230_scripts_run_script(&srv, script->id);
                for( int i = 0; i < MOCK230_NPC_MAX; i++ )
                    if( srv.npcs[i].active && srv.npcs[i].type == chicken &&
                        srv.npcs[i].x == 3234 && srv.npcs[i].z == 3234 )
                        added = i;
                SELFTEST_CHECK(added >= 0, "the test chicken should be spawned");

                /*
                 * `npc_queue(q, arg, 1)` fires on tick **+1**, not +2, and this
                 * assertion used to say +2.
                 *
                 * `Npc.processQueue` compares the counter *after* its decrement
                 * (`request.delay--; if (!delayed && request.delay <= 0)`) where
                 * `Player.processQueue` compares it before — so an npc's delay 0
                 * and delay 1 both land on the next npc phase. This engine stored
                 * `delay + 1` for both and the test pinned the result: a test can
                 * be green and still encode the wrong convention.
                 */
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0, "the queue should not fire immediately");
                mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1,
                               "[ai_queue1,chicken] should fire on tick +1, got %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            }

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_npc_timer]");
            if( script && added >= 0 )
            {
                /*
                 * The timer needs an *active npc* to set it on, and a proc run
                 * by id has none — so it is armed directly. What is under test
                 * is phase 4 firing it on the interval, which is the half that
                 * did not exist: these three fields were on every npc already
                 * and nothing read them.
                 */
                srv.npcs[added].timer_interval = 2;
                srv.npcs[added].timer_clock = 0;
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
                mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0, "the timer should not fire early");
                mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 10,
                               "[ai_timer,chicken] should fire on the interval, got %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
                mock230_world_tick(&srv);
                mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 20,
                               "and again one interval later, got %d", player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

                /* npc_settimer(0) stops it, which is how content pauses a
                 * behaviour. Asserting the *absence* of a fire is the only way
                 * to tell "stopped" from "not due yet". */
                srv.npcs[added].timer_interval = 0;
                for( int i = 0; i < 6; i++ )
                    mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 20,
                               "a zero interval should stop the timer, got %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
                srv.npcs[added].active = 0;
            }

            /*
             * `[ai_spawn]` and the behaviour it starts — the imp's teleport.
             *
             * Three things at once, and deliberately: phase 3 firing the
             * trigger for a freshly spawned npc, `[ai_spawn,imp]` arming a
             * timer, and `[ai_timer,imp]` eventually moving the imp. None of
             * them is observable on its own — an npc that never teleports looks
             * identical whether the trigger never fired, the timer was never
             * set, or `map_findsquare` returned the tile it was already on.
             *
             * The loop is generous because the behaviour is *random*: a 50 %
             * roll on a 50-200 tick timer. 3,000 ticks is far past the point
             * where "it never moved" stops being bad luck (the chance of no
             * teleport in 3,000 ticks is around one in 2^15), and the failure
             * message says how long it waited so a genuine change in the
             * numbers reads as one.
             */
            {
                int imp_type = mock230_content_symbol(MOCK230_PACK_NPC, "imp");
                int imp = npc_spawn(&srv, imp_type, player->x + 6, player->z + 6, player->level);

                SELFTEST_CHECK(imp >= 0, "an imp should spawn");
                if( imp >= 0 )
                {
                    struct Mock230Npc* npc = &srv.npcs[imp];
                    int start_x = npc->x;
                    int start_z = npc->z;
                    int moved = 0;
                    int ticks = 0;

                    SELFTEST_CHECK(npc->spawn_pending,
                                   "a fresh npc owes its [ai_spawn]");
                    mock230_world_tick(&srv);
                    SELFTEST_CHECK(!npc->spawn_pending,
                                   "which phase 3 runs on the next tick");
                    SELFTEST_CHECK(npc->timer_interval > 0,
                                   "[ai_spawn,imp] should arm the teleport timer, got %d",
                                   npc->timer_interval);

                    for( ticks = 0; ticks < 3000 && !moved; ticks++ )
                    {
                        mock230_world_tick(&srv);
                        /* A teleport, not a walk: an imp has no wanderrange
                         * that could carry it this far a step at a time, and
                         * `npc_tele` clears step_dir so nothing is animated. */
                        if( npc->x != start_x || npc->z != start_z )
                            moved = 1;
                    }
                    SELFTEST_CHECK(moved, "an imp should teleport within 3000 ticks");
                    if( moved )
                    {
                        int dx = npc->x > start_x ? npc->x - start_x : start_x - npc->x;
                        int dz = npc->z > start_z ? npc->z - start_z : start_z - npc->z;

                        SELFTEST_CHECK((dx > dz ? dx : dz) <= 20,
                                       "and land inside the 20-tile radius, went %d,%d", dx, dz);
                        SELFTEST_CHECK(!mock230_scene_walk_blocked(npc->level, npc->x, npc->z),
                                       "onto a walkable tile, landed on %d,%d", npc->x, npc->z);
                    }
                    npc->active = 0;
                }
            }

            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: player queues, timers, name-keyed dispatch\n");
    {
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            const struct Mock230Ids* ids = mock230_ids();
            int varp_zone_log = mock230_content_symbol(MOCK230_PACK_VARP, "mock_zone_log");
            int varp_mapzone_log = mock230_content_symbol(MOCK230_PACK_VARP, "mock_mapzone_log");
            int armed_at;

            player->delayed_until = 0;
            player->mainmodal_group = 0;
            player->chatmodal_group = 0;
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;

            /*
             * ---- the name-keyed dispatch path -----------------------------
             *
             * `[zone,…]` is the only trigger family whose subject is a place,
             * and a place is not a type id. The dispatch function is called
             * *directly* here, with no latch involved, which is the point: "the
             * name resolves" and "the latch fires it" are two independent
             * failures that would otherwise present identically. The latch's own
             * assertions are the zone-family stanza further down.
             *
             * This is the only guard against a failure that is *permanently*
             * silent. If the format string disagrees with the compiler's
             * spelling by one character, or if the lexer ever stops preserving
             * a coord subject's raw text, every zone script in the tree simply
             * never runs and nothing anywhere says so.
             *
             * The scripts write `%mock_zone_log`/`%mock_mapzone_log` — an
             * accumulator, `log * 10 + n` — rather than the shared quest varp,
             * because once the latches exist a zone script fires from ordinary
             * movement inside whichever stanza happens to be walking. See
             * configs/selftest.varp.
             */
            SELFTEST_CHECK(varp_zone_log > 0 && varp_mapzone_log > 0,
                           "the zone-log varps should resolve, got %d/%d", varp_zone_log,
                           varp_mapzone_log);
            player->varps[varp_zone_log] = 0;
            SELFTEST_CHECK(mock230_scripts_run_trigger_at(&srv, SS_TRIGGER_ZONE, 0, 3222, 3218) ==
                               MOCK230_TRIGGER_RAN,
                           "[zone,0_50_50_16_16] should be reachable by name");
            SELFTEST_CHECK(player->varps[varp_zone_log] == 1,
                           "and it is the authored script that ran, got %d",
                           player->varps[varp_zone_log]);

            /* The last two components are tile offsets truncated to the 8-tile
             * zone, not the raw tile: a neighbour inside the same zone resolves
             * to the same script, and the next zone east does not. */
            SELFTEST_CHECK(mock230_scripts_run_trigger_at(&srv, SS_TRIGGER_ZONE, 0, 3223, 3221) ==
                               MOCK230_TRIGGER_RAN,
                           "a tile inside the same zone resolves to the same script");
            SELFTEST_CHECK(mock230_scripts_run_trigger_at(&srv, SS_TRIGGER_ZONE, 0, 3230, 3218) ==
                               MOCK230_TRIGGER_NONE,
                           "the zone 8 tiles east has none bound");
            SELFTEST_CHECK(player->varps[varp_zone_log] == 11,
                           "so the log reads 11 and not 111, got %d",
                           player->varps[varp_zone_log]);

            /* The level is part of a zone's name, and the map square's is a
             * literal 0 — which is the whole 427-vs-379 distinction. The same
             * tile one level up is a *different name*, and a different script is
             * bound to it, so this fails as a wrong digit rather than as a
             * missing one. */
            SELFTEST_CHECK(mock230_scripts_run_trigger_at(&srv, SS_TRIGGER_ZONE, 1, 3222, 3218) ==
                               MOCK230_TRIGGER_RAN &&
                               player->varps[varp_zone_log] == 114,
                           "the same tile on level 1 names a different zone, got %d",
                           player->varps[varp_zone_log]);
            SELFTEST_CHECK(mock230_scripts_run_trigger_at(&srv, SS_TRIGGER_ZONEEXIT, 0, 3222,
                                                          3218) == MOCK230_TRIGGER_RAN &&
                               player->varps[varp_zone_log] == 1142,
                           "and the exit half is a separate name, got %d",
                           player->varps[varp_zone_log]);
            SELFTEST_CHECK(mock230_scripts_run_trigger_at(&srv, SS_TRIGGER_ZONEEXIT, 1, 3222,
                                                          3218) == MOCK230_TRIGGER_NONE,
                           "which carries the level too — nothing is bound at level 1");

            /*
             * The other granularity, and the distinction that makes this stage
             * worth doing separately: `mapzone` keys off the map square, and its
             * name carries a literal 0 where the zone's carries the real level.
             * So the *same tile on level 3* resolves to the same `[mapzone]`.
             */
            player->varps[varp_mapzone_log] = 0;
            SELFTEST_CHECK(mock230_scripts_run_trigger_at(&srv, SS_TRIGGER_MAPZONE, 0, 3222,
                                                          3218) == MOCK230_TRIGGER_RAN,
                           "[mapzone,0_50_50] should be reachable by name");
            SELFTEST_CHECK(mock230_scripts_run_trigger_at(&srv, SS_TRIGGER_MAPZONE, 3, 3222,
                                                          3218) == MOCK230_TRIGGER_RAN,
                           "and the level is a literal 0 in its name, not the player's");
            SELFTEST_CHECK(player->varps[varp_mapzone_log] == 11,
                           "both of those ran the one script, got %d",
                           player->varps[varp_mapzone_log]);
            SELFTEST_CHECK(mock230_scripts_run_trigger_at(&srv, SS_TRIGGER_MAPZONE, 0, 3222 + 64,
                                                          3218) == MOCK230_TRIGGER_RAN &&
                               player->varps[varp_mapzone_log] == 113,
                           "the next map square east is its own name, got %d",
                           player->varps[varp_mapzone_log]);
            SELFTEST_CHECK(mock230_scripts_run_trigger_at(&srv, SS_TRIGGER_MAPZONE, 0, 3222 + 128,
                                                          3218) == MOCK230_TRIGGER_NONE,
                           "and two squares east has none bound");
            SELFTEST_CHECK(mock230_scripts_run_trigger_at(&srv, SS_TRIGGER_MAPZONEEXIT, 0,
                                                          3222 + 64, 3218) ==
                               MOCK230_TRIGGER_NONE,
                           "mapzoneexit is again a separate name");

            /*
             * ---- timer types ---------------------------------------------
             *
             * A soft timer runs while the player is busy; a normal one does not.
             * One `case` served both opcodes and no type was stored anywhere, so
             * both halves were wrong in opposite directions — and a test that
             * only asked "did a timer fire" would have stayed green for either.
             */
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            mock230_scripts_run_proc(&srv, "[proc,selftest_timers_arm]", NULL, 0);
            player->delayed_until = srv.tick + 10;
            for( int i = 0; i < 4; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 200,
                           "a busy player runs soft timers twice and normal ones not at all, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            player->delayed_until = 0;
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 201,
                           "and the normal one fires the moment access returns, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            /* `cleartimer` and `clearsofttimer` both stop their timer. Asserting
             * the absence of a fire is the only way to tell "stopped" from "not
             * due yet". */
            mock230_scripts_run_proc(&srv, "[proc,selftest_timers_clear]", NULL, 0);
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            for( int i = 0; i < 8; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0,
                           "cleared timers stay cleared, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            /*
             * ---- an interval of 0 -----------------------------------------
             *
             * A timer of 0 keeps running; it does not stop. `Player.processTimers`
             * tests `World.currentTick >= timer.clock + timer.interval` with no
             * lower bound on the interval, so 0 fires on every tick and only
             * `cleartimer` stops it.
             *
             * Worth an assertion of its own because the wrong answer is silent
             * in both directions: an `interval <= 0` guard leaves the timer
             * *set* — it holds its slot and `gettimer` still answers — while it
             * never fires again. And it is reachable from real content, not a
             * theoretical edge: the reference arms `settimer(agilityarena_pillar,
             * sub(%agilityarena_next_pillar_time, map_clock))`, which is 0 or
             * negative once that deadline has passed.
             */
            mock230_scripts_run_proc(&srv, "[proc,selftest_timer_zero_arm]", NULL, 0);
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1,
                           "an interval-0 timer fires on the next tick, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            mock230_world_tick(&srv);
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 3,
                           "and on every tick after it — once per tick, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            mock230_scripts_run_proc(&srv, "[proc,selftest_timers_clear]", NULL, 0);
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            for( int i = 0; i < 4; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0,
                           "and cleartimer is what stops it, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            /*
             * ---- gettimer -------------------------------------------------
             *
             * The clock is the absolute world tick the timer was last armed or
             * fired at. A countdown cannot answer this at all, which is what
             * makes the assertion worth having: the +1000 is only so an unset
             * timer's -1 stays a positive varp.
             */
            armed_at = srv.tick;
            mock230_scripts_run_proc(&srv, "[proc,selftest_gettimer]", NULL, 0);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1000 + armed_at,
                           "gettimer is the tick it was armed at (%d), got %d", armed_at,
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            mock230_scripts_run_proc(&srv, "[proc,selftest_gettimer_unset]", NULL, 0);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 999,
                           "and -1 for a timer that is not set, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            /*
             * ---- queue kinds ----------------------------------------------
             *
             * Each writes its own decimal column so one read says exactly which
             * of them landed. `queue`, `weakqueue` and `longqueue` at delay 0 all
             * fire on the next tick and none of them fires on this one.
             */
            mock230_scripts_run_proc(&srv, "[proc,selftest_queue_kinds]", NULL, 0);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0,
                           "a queued script does not run in the tick that queued it");
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1101,
                           "queue + weakqueue + longqueue all fire on tick +1, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            /*
             * ---- the access gate ------------------------------------------
             *
             * The one that makes a queue a queue. A busy player holds the entry;
             * the counter keeps running down underneath, so it fires on the
             * *first* tick after access returns rather than restarting its wait.
             * A delay of 0 could not tell those two apart, which is why the
             * script uses 2.
             */
            mock230_scripts_run_proc(&srv, "[proc,selftest_queue_normal_soon]", NULL, 0);
            player->delayed_until = srv.tick + 20;
            for( int i = 0; i < 5; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0,
                           "a busy player does not run a due queue entry, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            player->delayed_until = 0;
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1,
                           "and runs it on the very next tick, not one delay later, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            /*
             * ---- strongqueue ----------------------------------------------
             *
             * The kind's entire difference: it closes whatever modal is up
             * before the drain, so its own entry passes the access check on the
             * tick it is due. The mount goes through the same function every
             * IF_OPENSUB does, so this is the state a real interface leaves.
             */
            mock230_note_modal_mount(&srv, ids->com_gameframe_mainmodal,
                                     ids->iface_equipment_stats);
            SELFTEST_CHECK(player->mainmodal_group == ids->iface_equipment_stats,
                           "a modal is up");
            mock230_scripts_run_proc(&srv, "[proc,selftest_strongqueue_arm]", NULL, 0);
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->mainmodal_group == 0,
                           "a strong entry closes the modal before the drain");
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 10,
                           "and then runs on the same tick, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            /*
             * ---- clearqueue and getqueue ----------------------------------
             */
            mock230_scripts_run_proc(&srv, "[proc,selftest_queue_pending]", NULL, 0);
            mock230_world_tick(&srv);
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0,
                           "a delay-3 entry has not fired after two ticks, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            mock230_scripts_run_proc(&srv, "[proc,selftest_clearqueue]", NULL, 0);
            for( int i = 0; i < 6; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0,
                           "and clearqueue cancels it rather than delaying it, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            /* `getqueue` counts and does not clear; two copies of one script are
             * two entries. */
            mock230_scripts_run_proc(&srv, "[proc,selftest_getqueue]", NULL, 0);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 2,
                           "getqueue counts every copy, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            mock230_scripts_run_proc(&srv, "[proc,selftest_clearqueue]", NULL, 0);

            /*
             * ---- the weak queue -------------------------------------------
             *
             * A weak entry is discarded when a modal closes, and that is the
             * only thing that distinguishes it from a normal one. Without this,
             * `weakqueue` would be a synonym the content could not tell apart.
             */
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            mock230_scripts_run_proc(&srv, "[proc,selftest_queue_weak_only]", NULL, 0);
            mock230_note_modal_mount(&srv, ids->com_gameframe_mainmodal,
                                     ids->iface_equipment_stats);
            mock230_world_close_modal(&srv);
            for( int i = 0; i < 6; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0,
                           "closing a modal discards the weak queue, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            /* And the same entry survives when nothing closes. */
            mock230_scripts_run_proc(&srv, "[proc,selftest_queue_weak_only]", NULL, 0);
            for( int i = 0; i < 6; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 100,
                           "and survives when nothing closes, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            player->delayed_until = 0;
            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: use-on, the *u family\n");
    {
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            /*
             * Every id by name, and the three objs deliberately unrelated —
             * knife 946, bones 526, bucket_water 1929. A transposition test whose
             * two ids could stand in for each other tests nothing.
             */
            int knife = mock230_content_symbol(MOCK230_PACK_OBJ, "knife");
            int bones = mock230_content_symbol(MOCK230_PACK_OBJ, "bones");
            int bucket = mock230_content_symbol(MOCK230_PACK_OBJ, "bucket_water");
            int range = mock230_content_symbol(MOCK230_PACK_LOC, "cooksquestrange");
            int remains = mock230_content_symbol(MOCK230_PACK_LOC, "fire_remains");
            int door = mock230_content_symbol(MOCK230_PACK_LOC, "poordoor");
            int hans_type = mock230_content_symbol(MOCK230_PACK_NPC, "hans");
            /* Slots the packets name. Fixed and distinct, because the swap moves
             * `last_slot` as well as `last_item` and a shared slot would hide it. */
            enum
            {
                SLOT_KNIFE = 0,
                SLOT_BUCKET = 1,
                SLOT_BONES = 2,
                SLOT_SPARE = 3
            };
            uint8_t payload[16];
            struct RSAreaBuf out;

            SELFTEST_CHECK(knife > 0 && bones > 0 && bucket > 0,
                           "knife, bones and bucket_water should resolve by name");
            SELFTEST_CHECK(range > 0 && remains > 0 && door > 0 && hans_type > 0,
                           "cooksquestrange, fire_remains, poordoor and hans too");

            /*
             * The sentinel, before anything sets it. 0 is a real obj id and a
             * real backpack slot, so a script reading `last_useitem` outside a
             * use-on has to get -1 rather than "a Dwarf remains, from slot 0".
             */
            SELFTEST_CHECK(player->last_useitem == -1 && player->last_useslot == -1,
                           "last_useitem/last_useslot start at -1, got %d/%d",
                           player->last_useitem, player->last_useslot);

            selftest_park_player(&srv, 3222, 3218);
            inv_set(player, SLOT_KNIFE, knife, 1);
            inv_set(player, SLOT_BUCKET, bucket, 1);
            inv_set(player, SLOT_BONES, bones, 1);
            inv_set(player, SLOT_SPARE, bucket, 1);

            /*
             * ---- opheldu, rungs 1 and 2 -----------------------------------
             *
             * `[opheldu,knife]` is bound and `bucket_water` is not, so "use the
             * bucket on the knife" hits rung 1 and "use the knife on the bucket"
             * hits rung 2 — and rung 2 is the one that swaps.
             *
             * THE ASSERTION THAT MATTERS is that both directions leave exactly
             * the same state: `last_item` = the script's own subject, whichever
             * of the two the player picked up first. Binding both directions
             * would have made the swap invisible, which is why only one is bound.
             */
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            rsab_wrap(&out, payload, sizeof(payload));
            rsab_p2(&out, knife);
            rsab_p2(&out, SLOT_KNIFE);
            rsab_p4(&out, 0);
            rsab_p2(&out, bucket);
            rsab_p2(&out, SLOT_BUCKET);
            rsab_p4(&out, 0);
            mock230_world_handle(player, PKTOUT_NAME_OPHELDU, payload, (int)rsab_len(&out));
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1,
                           "bucket on knife runs [opheldu,knife] with both halves in place, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            SELFTEST_CHECK(player->last_item == knife && player->last_useitem == bucket,
                           "and the engine agrees: last_item %d, last_useitem %d",
                           player->last_item, player->last_useitem);
            SELFTEST_CHECK(player->last_slot == SLOT_KNIFE && player->last_useslot == SLOT_BUCKET,
                           "slots follow their items: last_slot %d, last_useslot %d",
                           player->last_slot, player->last_useslot);

            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            rsab_wrap(&out, payload, sizeof(payload));
            rsab_p2(&out, bucket);
            rsab_p2(&out, SLOT_BUCKET);
            rsab_p4(&out, 0);
            rsab_p2(&out, knife);
            rsab_p2(&out, SLOT_KNIFE);
            rsab_p4(&out, 0);
            mock230_world_handle(player, PKTOUT_NAME_OPHELDU, payload, (int)rsab_len(&out));
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1,
                           "knife on bucket runs the same script, the other way round, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            SELFTEST_CHECK(player->last_item == knife && player->last_useitem == bucket,
                           "and rung 2's SWAP puts the script's own subject back in last_item: "
                           "%d / %d",
                           player->last_item, player->last_useitem);
            SELFTEST_CHECK(player->last_slot == SLOT_KNIFE && player->last_useslot == SLOT_BUCKET,
                           "with the slots swapped alongside them: %d / %d", player->last_slot,
                           player->last_useslot);

            /*
             * ---- opheldu, rungs 3 and 4, and the inversion -----------------
             *
             * `[opheldu,_bones]` is a *category* binding. Rung 2's swap happens
             * whether or not rung 2 matched, so a category hit runs with the two
             * items the other way round from an id hit — `last_item` names the
             * item the script is NOT bound to. That is the reference's behaviour
             * and content is written against it; both directions are asserted
             * because rung 3 and rung 4 reach it by different routes.
             */
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            rsab_wrap(&out, payload, sizeof(payload));
            rsab_p2(&out, bones);
            rsab_p2(&out, SLOT_BONES);
            rsab_p4(&out, 0);
            rsab_p2(&out, bucket);
            rsab_p2(&out, SLOT_BUCKET);
            rsab_p4(&out, 0);
            mock230_world_handle(player, PKTOUT_NAME_OPHELDU, payload, (int)rsab_len(&out));
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 2,
                           "bucket on bones reaches [opheldu,_bones] by rung 3, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            SELFTEST_CHECK(player->last_item == bucket && player->last_useitem == bones,
                           "inverted, as the reference leaves it: last_item %d, last_useitem %d",
                           player->last_item, player->last_useitem);
            SELFTEST_CHECK(player->last_slot == SLOT_BUCKET && player->last_useslot == SLOT_BONES,
                           "slots inverted with them: %d / %d", player->last_slot,
                           player->last_useslot);

            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            rsab_wrap(&out, payload, sizeof(payload));
            rsab_p2(&out, bucket);
            rsab_p2(&out, SLOT_BUCKET);
            rsab_p4(&out, 0);
            rsab_p2(&out, bones);
            rsab_p2(&out, SLOT_BONES);
            rsab_p4(&out, 0);
            mock230_world_handle(player, PKTOUT_NAME_OPHELDU, payload, (int)rsab_len(&out));
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 2,
                           "bones on bucket reaches it by rung 4 instead, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            SELFTEST_CHECK(player->last_item == bucket && player->last_useitem == bones,
                           "and rung 4's second swap lands on the same inversion: %d / %d",
                           player->last_item, player->last_useitem);

            /*
             * ---- opheldu, all four rungs missing --------------------------
             *
             * Two buckets: no id is bound and neither carries a category, so
             * every rung misses. The reference answers with the message and
             * nothing else — there is no engine "use" behaviour to fall back to.
             */
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 77;
            rsab_wrap(&out, payload, sizeof(payload));
            rsab_p2(&out, bucket);
            rsab_p2(&out, SLOT_SPARE);
            rsab_p4(&out, 0);
            rsab_p2(&out, bucket);
            rsab_p2(&out, SLOT_BUCKET);
            rsab_p4(&out, 0);
            mock230_world_handle(player, PKTOUT_NAME_OPHELDU, payload, (int)rsab_len(&out));
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 77,
                           "a use-on nothing binds runs no script at all, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            /*
             * ---- oplocu: the subject is the LOC ---------------------------
             *
             * The single most likely wrong port, and the one the plan named: an
             * implementation keyed on the *used obj* would look up
             * `[oplocu,bucket_water]`, find nothing, and this would never run.
             *
             * Nothing binds `[aplocu,cooksquestrange]`, so it is only reachable
             * by closing the distance — which is what makes this the op rung.
             */
            selftest_park_player(&srv, 3222, 3218);
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            rsab_wrap(&out, payload, sizeof(payload));
            rsab_p2(&out, 3212);
            rsab_p2(&out, 3215);
            rsab_p2(&out, range);
            rsab_p2(&out, bucket);
            rsab_p2(&out, SLOT_BUCKET);
            rsab_p2(&out, 0);
            mock230_world_handle(player, PKTOUT_NAME_OPLOCU, payload, (int)rsab_len(&out));
            SELFTEST_CHECK(selftest_settle(&srv, 40) > 0,
                           "a use-on out of reach walks first, like every other interaction");
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 10,
                           "[oplocu,cooksquestrange] runs on arrival, with the bucket in "
                           "last_useitem, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

            /*
             * The same loc with a *different* item still runs the loc's script —
             * 19 is the script saying "I ran, and the item was not the bucket".
             * That is the positive half of the transposition test: the subject
             * does not move when the item does.
             */
            selftest_park_player(&srv, 3222, 3218);
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            rsab_wrap(&out, payload, sizeof(payload));
            rsab_p2(&out, 3212);
            rsab_p2(&out, 3215);
            rsab_p2(&out, range);
            rsab_p2(&out, knife);
            rsab_p2(&out, SLOT_KNIFE);
            rsab_p2(&out, 0);
            mock230_world_handle(player, PKTOUT_NAME_OPLOCU, payload, (int)rsab_len(&out));
            SELFTEST_CHECK(selftest_settle(&srv, 40) >= 0, "the walk should complete");
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 19,
                           "a different item on the same loc runs the same script, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            SELFTEST_CHECK(player->last_useitem == knife,
                           "and the item it carries is the one that was used, got %d",
                           player->last_useitem);

            /*
             * ---- a use-on miss must NOT run the engine's own verb ----------
             *
             * The door is the sharpest form of it: `[oploc1,poordoor]` has an
             * engine fallback that opens it, and routing an unbound `*u` into
             * that fallback would open a door because somebody used a bucket on
             * it. The reference's answer to an unbound use-on is the message and
             * nothing else (`Player.defaultOp`).
             */
            {
                int door_slot = mock230_scene_find_loc(3226, 3223, 0, door);
                struct Mock230SceneLoc* door_loc = mock230_scene_loc(door_slot);
                int before = door_loc ? door_loc->loc_id : -1;

                SELFTEST_CHECK(door_slot >= 0 && before == door,
                               "the castle door should be in the scene and shut, got %d", before);
                selftest_park_player(&srv, 3222, 3218);
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 78;
                rsab_wrap(&out, payload, sizeof(payload));
                rsab_p2(&out, 3226);
                rsab_p2(&out, 3223);
                rsab_p2(&out, door);
                rsab_p2(&out, bucket);
                rsab_p2(&out, SLOT_BUCKET);
                rsab_p2(&out, 0);
                mock230_world_handle(player, PKTOUT_NAME_OPLOCU, payload, (int)rsab_len(&out));
                SELFTEST_CHECK(selftest_settle(&srv, 40) >= 0, "the walk to the door completes");
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 78,
                               "a loc with no *u binding runs nothing, got %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
                door_loc = mock230_scene_loc(door_slot);
                SELFTEST_CHECK(door_loc && door_loc->loc_id == door,
                               "and the door stays SHUT — a use-on must not reach the engine's "
                               "own [oploc1] behaviour, got %d",
                               door_loc ? door_loc->loc_id : -1);
            }

            /*
             * ---- aplocu: the ap rung --------------------------------------
             *
             * A separate loc, because ap and op cannot both be observed on one
             * target: the ap form fires first and ends the interaction. The loc
             * is placed six tiles away — inside MOCK230_AP_RANGE_DEFAULT and well
             * outside "adjacent" — so a run of it is proof the ap rung fired.
             *
             * APLOCU is 64 and OPLOCU is 71; an implementation using +1 would
             * resolve the op form to APLOCT and never fire either.
             */
            selftest_park_player(&srv, 3222, 3218);
            mock230_scripts_run_proc(&srv, "[proc,selftest_useon_addloc]", NULL, 0);
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            rsab_wrap(&out, payload, sizeof(payload));
            rsab_p2(&out, 3220);
            rsab_p2(&out, 3224);
            rsab_p2(&out, remains);
            rsab_p2(&out, bucket);
            rsab_p2(&out, SLOT_BUCKET);
            rsab_p2(&out, 0);
            mock230_world_handle(player, PKTOUT_NAME_OPLOCU, payload, (int)rsab_len(&out));
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 20,
                           "[aplocu,fire_remains] fires on the click, from range, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            SELFTEST_CHECK(player->x == 3222 && player->z == 3218,
                           "without the player having moved a tile, at %d,%d", player->x,
                           player->z);
            SELFTEST_CHECK(player->interaction.kind == MOCK230_INTERACT_NONE,
                           "and an ap hit ends the interaction rather than walking on");

            /*
             * ---- opnpcu ---------------------------------------------------
             */
            {
                int hans = selftest_find_npc(&srv, hans_type);

                SELFTEST_CHECK(hans >= 0, "hans should be on the roster");
                if( hans >= 0 )
                {
                    selftest_park_player(&srv, 3222, 3218);
                    player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
                    rsab_wrap(&out, payload, sizeof(payload));
                    rsab_p2(&out, hans);
                    rsab_p2(&out, bucket);
                    rsab_p2(&out, SLOT_BUCKET);
                    rsab_p2(&out, 0);
                    mock230_world_handle(player, PKTOUT_NAME_OPNPCU, payload, (int)rsab_len(&out));
                    SELFTEST_CHECK(selftest_settle(&srv, 40) >= 0,
                                   "the walk to a wandering npc should complete");
                    SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 30,
                                   "[opnpcu,hans] runs with the npc as its subject, got %d",
                                   player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
                }
            }

            /*
             * ---- opobju ---------------------------------------------------
             *
             * A ground obj is reached by standing *on* it, not beside it, which
             * is the one rule this form does not share with the other two. The
             * pile must also still be there afterwards: picking it up is the
             * engine's answer to `[opobj<n>]`, and a use-on must not reach it.
             */
            selftest_park_player(&srv, 3222, 3218);
            mock230_scripts_run_proc(&srv, "[proc,selftest_useon_addobj]", NULL, 0);
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            rsab_wrap(&out, payload, sizeof(payload));
            rsab_p2(&out, 3224);
            rsab_p2(&out, 3218);
            rsab_p2(&out, bones);
            rsab_p2(&out, bucket);
            rsab_p2(&out, SLOT_BUCKET);
            rsab_p2(&out, 0);
            mock230_world_handle(player, PKTOUT_NAME_OPOBJU, payload, (int)rsab_len(&out));
            SELFTEST_CHECK(selftest_settle(&srv, 20) >= 0, "the walk onto the pile completes");
            SELFTEST_CHECK(player->x == 3224 && player->z == 3218,
                           "standing on the tile, not beside it, at %d,%d", player->x, player->z);
            SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 40,
                           "[opobju,bones] runs with the ground obj as its subject, got %d",
                           player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            SELFTEST_CHECK(selftest_find(player, bones) == SLOT_BONES,
                           "and the pile was not picked up — the backpack still holds only the "
                           "one the fixture put there");

            for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                inv_set(player, i, -1, 0);
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            selftest_park_player(&srv, 3222, 3218);
            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: the zone family, and its two latches\n");
    {
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            int zone_log = mock230_content_symbol(MOCK230_PACK_VARP, "mock_zone_log");
            int mapzone_log = mock230_content_symbol(MOCK230_PACK_VARP, "mock_mapzone_log");
            int zone_clock = mock230_content_symbol(MOCK230_PACK_VARP, "mock_zone_clock");
            int detected_at;

            SELFTEST_CHECK(zone_log > 0 && mapzone_log > 0 && zone_clock > 0,
                           "the zone-log varps should resolve, got %d/%d/%d", zone_log,
                           mapzone_log, zone_clock);

            /*
             * ---- login: enter with no exit --------------------------------
             *
             * -1 in both latches is what a fresh `Player` has in the reference,
             * which never resets them anywhere — `cleanup()` included. Setting
             * them here rather than relying on process start makes the case
             * assertable in the middle of a run, and makes "the exit is
             * suppressed on the first transition" a statement rather than an
             * accident of ordering.
             */
            selftest_park_player(&srv, 3222, 3218);
            player->last_zone_level = -1;
            player->last_zone_x = -1;
            player->last_zone_z = -1;
            player->last_map_x = -1;
            player->last_map_z = -1;
            player->varps[zone_log] = 0;
            player->varps[mapzone_log] = 0;
            player->varps[zone_clock] = 0;

            /*
             * One tick detects and dispatches *nothing*. This is the assertion
             * that pins the engine queue: detection is phase 10, execution is
             * phase 5 of the next tick, and an implementation that fired inline
             * would satisfy every other check in this stanza.
             *
             * It is not a stylistic preference. Firing inline from phase 10
             * would run a script that teleports *after* PLAYER_INFO had already
             * been encoded for the tile the player is leaving, and would hand a
             * mid-dialogue player's zone script to `run_or_park`, whose
             * one-parked-script rule refuses it outright rather than holding it.
             */
            mock230_world_tick(&srv);
            detected_at = srv.tick;
            SELFTEST_CHECK(player->varps[zone_log] == 0 && player->varps[mapzone_log] == 0,
                           "phase 10 detects the crossing and dispatches nothing, got %d/%d",
                           player->varps[zone_log], player->varps[mapzone_log]);

            mock230_world_tick(&srv);
            /* 1 and not 91: `[zoneexit,0_0_0_0_0]` and `[mapzoneexit,0_0_0]`
             * are bound, and they are what a latch starting at a memset's 0
             * would fire — a login claiming the player just left the map's
             * origin. -1 is what suppresses it, and the digit 9 is how that
             * shows. */
            SELFTEST_CHECK(player->varps[zone_log] == 1,
                           "the first transition fires [zone] and no [zoneexit], got %d",
                           player->varps[zone_log]);
            SELFTEST_CHECK(player->varps[mapzone_log] == 1,
                           "and [mapzone] with no [mapzoneexit], got %d",
                           player->varps[mapzone_log]);
            SELFTEST_CHECK(player->varps[zone_clock] == detected_at + 1,
                           "on the tick after the crossing (%d), got %d", detected_at + 1,
                           player->varps[zone_clock]);

            /* Standing still is not a crossing. The latch is a comparison
             * against a stored value, not a recomputation. */
            mock230_world_tick(&srv);
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[zone_log] == 1 && player->varps[mapzone_log] == 1,
                           "standing still fires nothing, got %d/%d", player->varps[zone_log],
                           player->varps[mapzone_log]);

            /*
             * ---- a rebuild is not a crossing ------------------------------
             *
             * The single most likely wrong implementation, and otherwise
             * invisible: `Mock230Player.zone_index` is the `>> 3` key including
             * the level and looks exactly like `lastZone` — but
             * `mock230_zone_player_reset` sets it to -1 on every REBUILD_NORMAL
             * and every climb. A latch hung off it would re-fire `[zone,…]`
             * whenever the world's origin moved under a standing player, and
             * swallow the `[zoneexit]` that should have paired with it.
             */
            mock230_zone_player_reset(player);
            player->rebuild_pending = 1;
            mock230_world_tick(&srv);
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[zone_log] == 1 && player->varps[mapzone_log] == 1,
                           "a REBUILD_NORMAL fires nothing, got %d/%d", player->varps[zone_log],
                           player->varps[mapzone_log]);

            /*
             * ---- the zone moves, the map square does not ------------------
             *
             * 16 tiles east, still inside square 50_50. The accumulator makes
             * the *order* observable: 23 is exit-then-enter and 32 is the other
             * way round, where a pair of counters would read the same for both.
             */
            player->varps[zone_log] = 0;
            player->x = 3238;
            player->place_dirty = 1;
            mock230_world_tick(&srv);
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[zone_log] == 23,
                           "moving zone fires [zoneexit] then [zone], got %d",
                           player->varps[zone_log]);
            SELFTEST_CHECK(player->varps[mapzone_log] == 1,
                           "and the map square is untouched, got %d",
                           player->varps[mapzone_log]);

            /*
             * ---- a climb is a zone crossing and not a square crossing -----
             *
             * The 427-vs-379 distinction, asserted. `[zone]` carries the real
             * level in its name; `[mapzone]` carries a literal 0, because the
             * reference builds that latch with `CoordGrid.packCoord(0, x, z)`.
             * Packing the real level into the map-square latch turns the second
             * check red while leaving the first green.
             */
            player->varps[zone_log] = 0;
            player->x = 3222;
            player->level = 1;
            player->place_dirty = 1;
            mock230_world_tick(&srv);
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[zone_log] == 4,
                           "level 1 at the home tile is its own [zone], got %d",
                           player->varps[zone_log]);
            SELFTEST_CHECK(player->varps[mapzone_log] == 1,
                           "and a climb does not re-enter the map square, got %d",
                           player->varps[mapzone_log]);

            /*
             * ---- the square moves ------------------------------------------
             *
             * Back to level 0 (which re-enters the home zone), then east past
             * x = 3264 into square 51_50 — where nothing is bound to any zone.
             * So the two accumulators must move independently: the mapzone one
             * gains exit-then-enter, the zone one gains only the exit.
             */
            player->varps[zone_log] = 0;
            player->varps[mapzone_log] = 0;
            player->level = 0;
            player->place_dirty = 1;
            mock230_world_tick(&srv);
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[zone_log] == 1 && player->varps[mapzone_log] == 0,
                           "coming back down re-enters the zone only, got %d/%d",
                           player->varps[zone_log], player->varps[mapzone_log]);

            player->x = 3270;
            player->place_dirty = 1;
            mock230_world_tick(&srv);
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[mapzone_log] == 23,
                           "crossing x=3264 fires [mapzoneexit] then [mapzone], got %d",
                           player->varps[mapzone_log]);
            SELFTEST_CHECK(player->varps[zone_log] == 12,
                           "and the new square's zone has nothing bound, so only the "
                           "[zoneexit] shows, got %d",
                           player->varps[zone_log]);

            /*
             * ---- a busy player holds it, and does not lose it --------------
             *
             * `processEngineQueue` gates the *run* on `canAccess()` and
             * decrements regardless, so a player who crosses a boundary with a
             * dialogue open has the script held until the dialogue closes. The
             * alternative — firing it anyway, or dropping it — is the difference
             * between a zone script that is late and one that never ran.
             */
            selftest_park_player(&srv, 3222, 3218);
            player->varps[zone_log] = 0;
            player->delayed_until = srv.tick + 20;
            for( int i = 0; i < 4; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[zone_log] == 0,
                           "a busy player does not run a queued zone script, got %d",
                           player->varps[zone_log]);
            player->delayed_until = 0;
            mock230_world_tick(&srv);
            SELFTEST_CHECK(player->varps[zone_log] == 1,
                           "and runs it on the first tick after access returns, got %d",
                           player->varps[zone_log]);

            player->varps[zone_log] = 0;
            player->varps[mapzone_log] = 0;
            player->varps[zone_clock] = 0;
            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: uid, gender, session_log\n");
    {
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            const struct SSVM_Script* script =
                SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_uid]");

            SELFTEST_CHECK(script != NULL, "[proc,selftest_uid] should be in the pack");
            if( script )
            {
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = -1;
                mock230_scripts_run_script(&srv, script->id);
                /*
                 * Five, not four: the run has to get *past* `session_log`. That
                 * opcode returns nothing and changes nothing, so the only way to
                 * tell "implemented" from "aborted the script" is a statement
                 * after it.
                 */
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 5,
                               "p_finduid / the false uid / text_gender / session_log "
                               "should all clear, got %d", player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            }
            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: find-all iterators\n");
    {
        /*
         * `findall` + `findnext` was six of the eight opcodes `test-mock230`'s
         * gap report was failing on, and the content that wanted them —
         * `[proc,npc_findcount]`, `[proc,loc_within_distance]`,
         * `[proc,sound_area]` — was already committed and unrunnable. So the
         * blocked content is the test.
         *
         * Both counts are compared against a walk this file does itself rather
         * than against a number written down, so changing the Lumbridge roster
         * cannot fail this for the wrong reason.
         */
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            const struct SSVM_Script* script;
            int goblin = mock230_content_symbol(MOCK230_PACK_NPC, "goblin");
            int expected = 0;

            for( int i = 0; i < MOCK230_NPC_MAX; i++ )
            {
                struct Mock230Npc* npc = &srv.npcs[i];
                int dx;
                int dz;

                if( !npc->active || npc->type != goblin || npc->level != 0 )
                    continue;
                dx = npc->x - 3222;
                dz = npc->z - 3218;
                if( dx < 0 )
                    dx = -dx;
                if( dz < 0 )
                    dz = -dz;
                if( (dx > dz ? dx : dz) <= 64 )
                    expected++;
            }
            SELFTEST_CHECK(expected > 0,
                           "the roster should have goblins within 64 tiles to count");

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_iterators]");
            SELFTEST_CHECK(script != NULL, "[proc,selftest_iterators] should be in the pack");
            if( script )
            {
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = -1;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == expected,
                               "~npc_findcount should agree with a direct walk, "
                               "script says %d and the roster has %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS], expected);
            }

            expected = 0;
            for( int slot = 0;; slot++ )
            {
                struct Mock230SceneLoc* loc = mock230_scene_loc(slot);

                if( !loc )
                    break;
                if( !loc->active || loc->level != 0 )
                    continue;
                if( loc->x < (3212 & ~7) || loc->x >= (3212 & ~7) + 8 ||
                    loc->z < (3215 & ~7) || loc->z >= (3215 & ~7) + 8 )
                    continue;
                expected++;
            }
            SELFTEST_CHECK(expected > 0, "the kitchen's zone should hold some locs");

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_loc_iterator]");
            SELFTEST_CHECK(script != NULL,
                           "[proc,selftest_loc_iterator] should be in the pack");
            if( script )
            {
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = -1;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == expected,
                               "loc_findallzone should agree with a direct walk, "
                               "script says %d and the zone has %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS], expected);
            }

            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: npc addressing\n");
    {
        /*
         * `npc_find` is in front of 139 LostCity files, more than any other
         * unimplemented opcode, because it is how content addresses an npc that
         * is not the one who triggered the script.
         *
         * The negative case is the one worth having: a radius that should find
         * nothing. A `npc_find` that ignores its distance argument passes every
         * positive test in this block and is wrong in exactly the way that
         * matters — content uses the radius to mean "beside me".
         */
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            const struct SSVM_Script* script;
            int chicken = mock230_content_symbol(MOCK230_PACK_NPC, "chicken");
            int before;

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_npc_find]");
            SELFTEST_CHECK(script != NULL, "[proc,selftest_npc_find] should be in the pack");
            if( script )
            {
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = -1;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 4,
                               "npc_find/stat/range should all clear, got %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            }

            before = 0;
            for( int i = 0; i < MOCK230_NPC_MAX; i++ )
                if( srv.npcs[i].active )
                    before++;

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_npc_add]");
            SELFTEST_CHECK(script != NULL, "[proc,selftest_npc_add] should be in the pack");
            if( script )
            {
                int after = 0;

                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = -1;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 3,
                               "add/tele/del should all clear, got %d", player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
                for( int i = 0; i < MOCK230_NPC_MAX; i++ )
                    if( srv.npcs[i].active )
                        after++;
                SELFTEST_CHECK(after == before,
                               "npc_del should leave the roster as it found it, %d -> %d",
                               before, after);
            }

            /*
             * The timed form. `npc_add(coord, npc, 0)` above stayed until
             * `npc_del`; this one has to go on its own, and the negative first
             * tick is what separates "expires" from "was never added".
             */
            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_npc_add_timed]");
            SELFTEST_CHECK(script != NULL,
                           "[proc,selftest_npc_add_timed] should be in the pack");
            if( script && chicken > 0 )
            {
                int found = -1;

                mock230_scripts_run_script(&srv, script->id);
                for( int i = 0; i < MOCK230_NPC_MAX; i++ )
                    if( srv.npcs[i].active && srv.npcs[i].type == chicken &&
                        srv.npcs[i].x == 3232 && srv.npcs[i].z == 3232 )
                        found = i;
                SELFTEST_CHECK(found >= 0, "a timed npc_add should spawn the npc");

                mock230_world_tick(&srv);
                SELFTEST_CHECK(found < 0 || srv.npcs[found].active,
                               "and it should still be there a tick later");
                for( int i = 0; i < 4; i++ )
                    mock230_world_tick(&srv);
                SELFTEST_CHECK(found < 0 || !srv.npcs[found].active,
                               "and be gone once the duration expires");
            }

            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: loc mutation\n");
    {
        /*
         * find -> change -> revert, against the scene the server really builds.
         *
         * The revert is the assertion that matters. A `loc_change` that lands
         * and never comes back passes any test that only checks the change, and
         * it is a one-way ratchet in play: the first player to mine a rock
         * removes it for the rest of the session.
         */
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            const struct SSVM_Script* script;
            int range = mock230_content_symbol(MOCK230_PACK_LOC, "cooksquestrange");
            int remains = mock230_content_symbol(MOCK230_PACK_LOC, "fire_remains");
            int slot = mock230_scene_find_loc(3212, 3215, 0, range);
            struct Mock230SceneLoc* loc;

            SELFTEST_CHECK(range > 0 && remains > 0,
                           "cooksquestrange and fire_remains resolve by name");
            SELFTEST_CHECK(slot >= 0,
                           "the Lumbridge cooking range should be in the scene at 3212,3215");

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_loc]");
            SELFTEST_CHECK(script != NULL, "[proc,selftest_loc] should be in the pack");
            if( script && slot >= 0 )
            {
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = -1;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 4,
                               "find/type/coord/change should all clear, got %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS]);

                loc = mock230_scene_loc(slot);
                SELFTEST_CHECK(loc && loc->loc_id == remains,
                               "the scene should hold the changed loc, got %d",
                               loc ? loc->loc_id : -1);
                {
                    /*
                     * The record that survives a rebuild. It used to be a
                     * `changed` flag on the scene loc, which the rebuild freed
                     * along with the array it was in — so the thing the flag
                     * existed for was the one thing it could not do.
                     */
                    struct Mock230ZoneLoc* record =
                        mock230_zone_loc_find(&srv, loc->x, loc->z, loc->level, loc->shape);

                    SELFTEST_CHECK(record && record->loc_id == remains,
                                   "and record it in the zone, got %d",
                                   record ? record->loc_id : -1);
                }

                /* duration 2 means the tick after next, for the same +1 reason
                 * p_delay(n) resumes on tick n+1. */
                mock230_world_tick(&srv);
                loc = mock230_scene_loc(slot);
                SELFTEST_CHECK(loc && loc->loc_id == remains,
                               "still changed one tick later");
                mock230_world_tick(&srv);
                mock230_world_tick(&srv);
                loc = mock230_scene_loc(slot);
                SELFTEST_CHECK(loc && loc->loc_id == range,
                               "and back to the cooking range once the duration expires, got %d",
                               loc ? loc->loc_id : -1);
            }

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_loc_add]");
            SELFTEST_CHECK(script != NULL, "[proc,selftest_loc_add] should be in the pack");
            if( script )
            {
                int added;

                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = -1;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1,
                               "loc_add should leave the new loc active, got %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
                /*
                 * The slot, not `mock230_scene_find_loc`. That function returns
                 * the first loc on the tile when no id matches — the ternary at
                 * the end of it is `loc_id >= 0 ? fallback : fallback`, so the
                 * id argument does not filter, which is deliberate for OPLOC
                 * (a stale id must still resolve to the door somebody else
                 * opened) and useless for asking "is *this* loc still here".
                 * 3220,3220 has other locs on it, so the find-based version of
                 * this check passed before the loc existed and after it was
                 * gone.
                 */
                added = mock230_scene_find_loc(3220, 3220, 0, remains);
                SELFTEST_CHECK(added >= 0, "and the loc should be in the scene");
                SELFTEST_CHECK(added >= 0 && mock230_scene_loc(added) &&
                                   mock230_scene_loc(added)->loc_id == remains,
                               "at the slot loc_add returned");

                /* An added loc expires by being removed again, not by turning
                 * into something else. */
                for( int i = 0; i < 5; i++ )
                    mock230_world_tick(&srv);
                SELFTEST_CHECK(added >= 0 && mock230_scene_loc(added) &&
                                   !mock230_scene_loc(added)->active,
                               "a loc_add with a duration should expire away again");
            }

            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: oc_param\n");
    {
        /*
         * `oc_param` reads a param off an obj record and pushes it onto whichever
         * stack the param's *declared* type calls for. Both halves are new:
         * `mock230_obj_param` keeps all 53,853 rows the cache states, and
         * `mock230_content_param_type` reads the declarations out of
         * `configs/all.param` — which makes this the first runtime reader of
         * anything in `configs/`, a directory CONTENT_ARCHITECTURE.md §3.5
         * describes as write-only.
         *
         * Nothing in this tree calls it. It is implemented for the port (351
         * call sites across 117 LostCity files), so the only way to know it
         * works is to drive it — and the string case has to be popped *as a
         * string*, because an int-stack assertion would pass even with the value
         * on the wrong stack, which is the exact failure `runtime_typed` names.
         */
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            const struct SSVM_Script* script;
            int longbow = mock230_content_symbol(MOCK230_PACK_OBJ, "magic_longbow");
            int rangeattack = mock230_content_symbol(MOCK230_PACK_PARAM, "rangeattack");
            int verb = mock230_content_symbol(MOCK230_PACK_PARAM, "param_451");
            int glory = mock230_content_symbol(MOCK230_PACK_OBJ, "amulet_of_glory");
            const struct Mock230ObjParam* row;

            /* The table itself, before the opcode over it. */
            SELFTEST_CHECK(longbow > 0 && rangeattack > 0,
                           "magic_longbow and rangeattack resolve by name");
            row = mock230_obj_param(longbow, rangeattack);
            SELFTEST_CHECK(row && row->ival == 69 && !row->sval,
                           "magic_longbow's rangeattack is the cache's 69, as an int");
            SELFTEST_CHECK(mock230_content_param_type(rangeattack) == 'i',
                           "and configs/all.param declares it 'i', got '%c'",
                           mock230_content_param_type(rangeattack));
            row = mock230_obj_param(glory, verb);
            SELFTEST_CHECK(row && row->sval && strcmp(row->sval, "Rub") == 0,
                           "amulet_of_glory's param_451 is the string \"Rub\"");
            SELFTEST_CHECK(mock230_content_param_type(verb) == 's',
                           "and it is declared 's', got '%c'",
                           mock230_content_param_type(verb));
            /* The absent row. Not an error — the reference answers with the
             * param's declared default. */
            SELFTEST_CHECK(mock230_obj_param(longbow, verb) == NULL,
                           "a param the obj does not carry is absent, not zero-valued");

            /* The declaration table behind the absent case, checked directly
             * so a script-level failure can be split into "the default was
             * read wrong" versus "the opcode ignored it". */
            {
                int param_87 = mock230_content_symbol(MOCK230_PACK_PARAM, "param_87");
                int attackrate = mock230_content_symbol(MOCK230_PACK_PARAM, "attackrate");

                SELFTEST_CHECK(param_87 > 0 && attackrate > 0,
                               "param_87 and attackrate resolve by name");
                SELFTEST_CHECK(mock230_content_param_default(param_87) == -1,
                               "param_87 declares default=-1, got %d",
                               mock230_content_param_default(param_87));
                SELFTEST_CHECK(mock230_content_param_default(attackrate) == 4,
                               "attackrate declares default=4, got %d",
                               mock230_content_param_default(attackrate));
                SELFTEST_CHECK(mock230_content_param_default(rangeattack) == 0,
                               "rangeattack declares no default, which reads as 0, got %d",
                               mock230_content_param_default(rangeattack));
            }

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_oc_param]");
            SELFTEST_CHECK(script != NULL, "[proc,selftest_oc_param] should be in the pack");
            if( script )
            {
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = -1;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 5,
                               "oc_param should clear all five int cases (two carried "
                               "rows, then absent with no default, default=-1, and "
                               "default=4), got %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            }

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_oc_param_string]");
            SELFTEST_CHECK(script != NULL,
                           "[proc,selftest_oc_param_string] should be in the pack");
            if( script )
            {
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = -1;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 2,
                               "a string param should reach the string stack and read "
                               "back as \"Rub\", got %d", player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            }

            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: nc_param\n");
    {
        /*
         * The same opcode over the npc table, and a separate section rather
         * than more checks in the one above because the two read different
         * tables — a shared section could not say which of them broke.
         *
         * The npc half was never blocked on decoding: `read_combat_params` in
         * mock230_npcinfo.c has always walked exactly these rows and kept the
         * fourteen keys it recognised. All that was missing was keeping the
         * rest. §3.13d called the family "blocked on data" and the data was one
         * line away, which is the reason this section exists at all — the
         * blocker was worth re-checking rather than quoting.
         *
         * cache.osrs239's values, and negative ones on purpose: a goblin's
         * bonuses are all -15, so a table that read the field unsigned would
         * still answer something.
         */
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            const struct SSVM_Script* script;
            int goblin = mock230_content_symbol(MOCK230_PACK_NPC, "goblin");
            int boss = mock230_content_symbol(MOCK230_PACK_NPC, "dagcave_ranged_boss");
            int strengthbonus = mock230_content_symbol(MOCK230_PACK_PARAM, "strengthbonus");
            int kings = mock230_content_symbol(MOCK230_PACK_PARAM, "param_510");
            int obj_typed = mock230_content_symbol(MOCK230_PACK_PARAM, "param_46");
            const struct Mock230NpcParam* row;

            /* The table itself, before the opcode over it. */
            SELFTEST_CHECK(goblin > 0 && boss > 0 && strengthbonus > 0,
                           "goblin, dagcave_ranged_boss and strengthbonus resolve by name");
            row = mock230_npc_param(goblin, strengthbonus);
            SELFTEST_CHECK(row && row->ival == -15 && !row->sval,
                           "a goblin's strengthbonus is the cache's -15, as an int");
            row = mock230_npc_param(boss, kings);
            SELFTEST_CHECK(row && row->sval &&
                               strcmp(row->sval, "Dagannoth Kings (Echo)") == 0,
                           "dagcave_ranged_boss's param_510 is the string it says it is");
            SELFTEST_CHECK(mock230_content_param_type(kings) == 's',
                           "and configs/all.param declares it 's', got '%c'",
                           mock230_content_param_type(kings));
            /*
             * A declared type that is neither 'i' nor 's'. `param_46` is 'o',
             * an obj id, and the VM has one int stack for every non-string
             * type — so this has to reach the int stack the same way a plain
             * 'i' does.
             */
            SELFTEST_CHECK(mock230_content_param_type(obj_typed) == 'o',
                           "param_46 is declared 'o', got '%c'",
                           mock230_content_param_type(obj_typed));
            /* The absent row, and the sortedness the binary search needs. A
             * goblin carries no rangeattack, and asking for a key below every
             * key it does carry is the lookup an unsorted table gets wrong. */
            SELFTEST_CHECK(
                mock230_npc_param(
                    goblin, mock230_content_symbol(MOCK230_PACK_PARAM, "rangeattack")) == NULL,
                "a param the npc does not carry is absent, not zero-valued");

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_nc_param]");
            SELFTEST_CHECK(script != NULL, "[proc,selftest_nc_param] should be in the pack");
            if( script )
            {
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = -1;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 5,
                               "nc_param should clear all five int cases (the last is "
                               "absent-with-declared-default: param_46 -> 526), got %d",
                               player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            }

            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_nc_param_string]");
            SELFTEST_CHECK(script != NULL,
                           "[proc,selftest_nc_param_string] should be in the pack");
            if( script )
            {
                player->varps[SELFTEST_VARP_QUEST_PROGRESS] = -1;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 2,
                               "a string param should reach the string stack and read back "
                               "as \"Dagannoth Kings (Echo)\", got %d", player->varps[SELFTEST_VARP_QUEST_PROGRESS]);
            }

            player->varps[SELFTEST_VARP_QUEST_PROGRESS] = 0;
            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: cook's assistant\n");
    {
        /*
         * The first quest ported from the LostCity tree, end to end
         * (docs/LOSTCITY_PORT_TRIAGE.md §10). It is here rather than only in
         * mock230_pack because every check below is about the *engine* acting
         * on ported content: the pack validator proves the ids resolve, this
         * proves the quest is playable.
         *
         * **It is last on purpose.** Placed earlier it made the combat section
         * fail — "the goblin should have hit back", with the player at full
         * health. Nothing about the quest touches combat; what it touches is
         * the number of `mock230_world_tick` calls before that fight, and the
         * fight's outcome depends on where the world RNG has got to. That is a
         * pre-existing fragility in the combat check rather than anything this
         * section does wrong, but it is real: any selftest inserted above it
         * that ticks the world can flip it. Left recorded here rather than
         * papered over, because the next person to add a section will hit it.
         *
         * Four things it pins, and each of them is a way the port could be
         * silently wrong:
         *
         *   - the Cook is in the world at all. The imported roster had every
         *     other Lumbridge Castle npc and not him, so the quest had nobody
         *     to start it and no error anywhere said so;
         *   - `%cookquest` is varp 29, the number the *modern* cache gives that
         *     name. It happens to be 29 in the rev-254 tree too, and a port
         *     that copied the number rather than re-resolving it would pass
         *     this test for the wrong reason — so the id is looked up by name
         *     here, exactly as the content does;
         *   - the state machine advances 0 -> 1 -> complete, and the middle
         *     state is reachable more than once (talking again mid-quest must
         *     not restart it);
         *   - the reward lands a tick late, through the queue, which is what
         *     the reference does and the one piece of its timing that ports
         *     verbatim.
         */
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            int cook_type = mock230_content_symbol(MOCK230_PACK_NPC, "cook");
            int cookquest = mock230_content_symbol(MOCK230_PACK_VARP, "cookquest");
            int milk = mock230_content_symbol(MOCK230_PACK_OBJ, "bucket_milk");
            int egg = mock230_content_symbol(MOCK230_PACK_OBJ, "egg");
            int flour = mock230_content_symbol(MOCK230_PACK_OBJ, "pot_flour");
            int cook_slot;

            SELFTEST_CHECK(cook_type > 0 && cookquest > 0 && milk > 0 && egg > 0 && flour > 0,
                           "cook / cookquest / the three ingredients all resolve by name");

            /* Every id the quest uses, re-resolved. The npc is the one that
             * moved between the two trees (rev254 278 -> osrs239 4626); the
             * other four did not, and pinning all five is what makes a future
             * cache bump say which. */
            SELFTEST_CHECK(cook_type == 4626, "cook is npc 4626, got %d", cook_type);
            SELFTEST_CHECK(cookquest == 29, "cookquest is varp 29, got %d", cookquest);

            cook_slot = selftest_find_npc(&srv, cook_type);
            SELFTEST_CHECK(cook_slot >= 0, "the Cook should be spawned in Lumbridge Castle");

            if( cook_slot >= 0 && cookquest > 0 )
            {
                uint8_t payload[2] = { (uint8_t)(cook_slot >> 8), (uint8_t)(cook_slot & 0xff) };
                int cooking = mock230_content_symbol(MOCK230_PACK_STAT, "cooking");
                int cooking_xp_before;
                int saved_x;
                int saved_z;
                int saved_level;
                int pages;

                /* Stand next to him rather than walking: the walk is the
                 * interaction model's test, not this one, and the Cook is
                 * behind a castle door the router would have to open.
                 *
                 * Where the player *is* is shared state — the combat section
                 * further down needs to be back among the goblins — so it is
                 * put back at the end of the block, the same way the held-item
                 * section restores the backpack it rewrites. */
                saved_x = player->x;
                saved_z = player->z;
                saved_level = player->level;
                mock230_world_teleport(&srv, srv.npcs[cook_slot].level,
                                       srv.npcs[cook_slot].x + 1, srv.npcs[cook_slot].z);
                player->varps[cookquest] = 0;

                mock230_world_handle(player, PKTOUT_NAME_OPNPC1, payload, 2);
                SELFTEST_CHECK(selftest_settle(&srv, 20) >= 0,
                               "talking to the Cook should resolve the interaction");
                SELFTEST_CHECK(player->active_script != NULL,
                               "[opnpc1,cook] should park on its first dialogue page");

                /* Click through. The count is not asserted — a page added to
                 * the conversation should not fail a test about the quest —
                 * but the cap is, because a script that never finishes would
                 * otherwise hang the suite rather than fail it. */
                pages = selftest_click_through(&srv, 24);
                SELFTEST_CHECK(player->active_script == NULL,
                               "the opening conversation should finish within 24 pages "
                               "(clicked %d)", pages);
                SELFTEST_CHECK(player->varps[cookquest] == 1,
                               "accepting should set %%cookquest to 1, got %d",
                               player->varps[cookquest]);

                /* Talking again with nothing in the backpack is the
                 * in-progress branch. It must not restart the quest, and it
                 * must not complete it either. */
                mock230_world_handle(player, PKTOUT_NAME_OPNPC1, payload, 2);
                pages = selftest_click_through(&srv, 24);
                SELFTEST_CHECK(player->varps[cookquest] == 1,
                               "talking again empty-handed should leave it in progress, got %d",
                               player->varps[cookquest]);

                /* Hand the ingredients in. */
                {
                    int free_slot = -1;
                    int given = 0;
                    const int wanted[3] = { milk, egg, flour };

                    for( int i = 0; i < MOCK230_INV_SLOTS && given < 3; i++ )
                    {
                        if( player->inv[i].obj_id >= 0 )
                            continue;
                        free_slot = i;
                        inv_set(player, free_slot, wanted[given], 1);
                        given++;
                    }
                    SELFTEST_CHECK(given == 3,
                                   "three free backpack slots for the ingredients, got %d",
                                   given);
                }

                SELFTEST_CHECK(cooking > 0 && cooking < MOCK230_STAT_COUNT,
                               "cooking should resolve out of pack/stat.pack, got %d", cooking);
                if( cooking < 0 || cooking >= MOCK230_STAT_COUNT )
                    cooking = 0;
                cooking_xp_before = player->stat_xp_tenths[cooking];

                mock230_world_handle(player, PKTOUT_NAME_OPNPC1, payload, 2);
                pages = selftest_click_through(&srv, 24);

                SELFTEST_CHECK(selftest_find(player, milk) < 0 &&
                                   selftest_find(player, egg) < 0 &&
                                   selftest_find(player, flour) < 0,
                               "handing in should take all three ingredients");

                /* The reward is queued, so it is deliberately NOT applied yet.
                 * Asserting the negative is the half that catches a port which
                 * dropped the queue and inlined the reward — which would work,
                 * and would fire while the last dialogue page is still up. */
                SELFTEST_CHECK(player->varps[cookquest] == 1,
                               "the reward should still be queued, got %d",
                               player->varps[cookquest]);

                mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[cookquest] == 2,
                               "the queue should complete the quest on the next tick, got %d",
                               player->varps[cookquest]);
                SELFTEST_CHECK(player->stat_xp_tenths[cooking] == cooking_xp_before + 3000,
                               "and award 300 Cooking xp (3000 tenths), got %d",
                               player->stat_xp_tenths[cooking] - cooking_xp_before);

                /*
                 * The queued script is now parked on its own message box —
                 * `~mesbox` ends in `p_pausebutton` like every other page in
                 * the toolkit. It has to be clicked away before anything else
                 * can talk, because there is one parking slot per player and a
                 * second script suspending while one waits is *dropped*, not
                 * queued (docs/osrs230_mockserver.md §3.10). Without this the
                 * post-quest check below passes while doing nothing at all.
                 */
                SELFTEST_CHECK(player->active_script != NULL,
                               "the reward should park on its completion message");
                pages = selftest_click_through(&srv, 8);
                SELFTEST_CHECK(player->active_script == NULL,
                               "and release the parking slot once dismissed (clicked %d)",
                               pages);

                /* The post-quest branch exists and does not undo anything. */
                mock230_world_handle(player, PKTOUT_NAME_OPNPC1, payload, 2);
                pages = selftest_click_through(&srv, 24);
                SELFTEST_CHECK(player->varps[cookquest] == 2,
                               "talking after completion should leave it complete, got %d",
                               player->varps[cookquest]);

                player->varps[cookquest] = 0;
                mock230_world_teleport(&srv, saved_level, saved_x, saved_z);
            }

            mock230_scripts_free(&srv);
        }
    }

    fprintf(stderr, "mock230 selftest: the equipment screen is content's\n");
    {
        /*
         * The screen moved out of mock230_equipment.c and into
         * interface_equipment/scripts/equipment.rs2, so what used to be
         * unmissable — a wrong label showed up on screen — is now a script that
         * can silently not run. Three things are asserted, and each one is a way
         * the move could be wrong while everything still compiles:
         *
         *   - the mount goes out and the eighteen rows follow it. A `~equipment_open`
         *     that failed to resolve `equipment:stabatt` would send the IF_OPENSUB
         *     and nothing else;
         *   - the text is the *engine's* old wording, built by content. The "+" on a
         *     non-negative bonus is the convention OldSchool prints and the one
         *     thing about these rows a player would notice immediately;
         *   - the refresh is gated on the mount and not on a flag. Nothing repaints
         *     while the screen is down, or it is eighteen IF_SETTEXTs to components
         *     that do not exist.
         *
         * Placed after the cook's assistant deliberately — see the note there
         * about anything inserted above it that ticks the world.
         */
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            static struct Mock230Capture capture;
            const struct Mock230Ids* ids = mock230_ids();
            /* IF_OPENSUB, then the container, then the rows. */
            static const int k_open[] = {
                6 /* IF_OPENSUB */, 10 /* UPDATE_INV_FULL */, 94 /* IF_SETTEXT */,
            };
            int settext = 0;
            int stab_at = -1;

            mock230_capture_begin(&srv, &capture);
            mock230_equipment_open_stats(&srv);
            mock230_capture_end(&srv);

            SELFTEST_CHECK(mock230_capture_has_sequence(&capture, k_open, 3),
                           "opening should mount, fill the worn container, then paint");
            SELFTEST_CHECK(player->mainmodal_group == ids->iface_equipment_stats,
                           "and leave the screen mounted, got group %d",
                           player->mainmodal_group);

            for( int i = 0; i < capture.count; i++ )
            {
                if( capture.packets[i].opcode != 94 )
                    continue;
                settext++;
                if( stab_at < 0 )
                {
                    const uint8_t* d = capture.packets[i].data;
                    int uid = (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3];

                    if( uid == ids->com_equipment_stats_stabatt )
                        stab_at = i;
                }
            }
            SELFTEST_CHECK(settext == 18, "eighteen rows should be painted, got %d", settext);

            /* The row's wording, byte for byte off the wire. `pjstr` writes the
             * text then a newline, so the payload after the uid is the string. */
            if( stab_at >= 0 )
            {
                const struct Mock230CapturedPacket* packet = &capture.packets[stab_at];
                char text[64];
                int n = packet->len - 4 - 1;

                if( n < 0 )
                    n = 0;
                if( n > (int)sizeof(text) - 1 )
                    n = (int)sizeof(text) - 1;
                memcpy(text, packet->data + 4, (size_t)n);
                text[n] = '\0';
                /* Naked, so a zero bonus reads "+0" — the sign is the convention,
                 * not a property of the number. */
                SELFTEST_CHECK(strcmp(text, "Stab: +0") == 0,
                               "an unarmed stab bonus should read \"Stab: +0\", got \"%s\"",
                               text);
            }
            else
            {
                SELFTEST_CHECK(0, "no IF_SETTEXT addressed equipment:stabatt");
            }

            /* Down again: the gate is the mount, so a repaint with nothing
             * mounted must send nothing at all. */
            mock230_capture_reset(&capture);
            mock230_capture_begin(&srv, &capture);
            mock230_send_if_closesub(player, ids->com_gameframe_mainmodal);
            mock230_equipment_refresh_stats(&srv);
            mock230_capture_end(&srv);
            SELFTEST_CHECK(mock230_capture_find(&capture, 94, 0) < 0,
                           "a closed screen should paint nothing");

            mock230_scripts_free(&srv);
        }
    }

    /*
     * The inverted fallback (PORTING_GUIDE §6 phase 1 item 3).
     *
     * Three claims, and the third is the one this exists for:
     *
     * 1. The engine fallbacks are enumerated, and the list shrinks. A tenth row
     *    appearing is a second implementation of a behaviour content could have
     *    stated, added without anybody deciding to.
     * 2. A script that *failed* does not hand its click to C. That was the
     *    indistinguishable case: `if( !run_trigger() )` treated an aborted
     *    script and an unbound trigger identically, so a content bug looked
     *    exactly like a content gap and produced a plausible-looking game.
     * 3. **With no script pack, nothing falls back.** A fresh checkout has no
     *    pack (the compiler's output is gitignored), and what used to happen
     *    then was that every trigger site answered out of C — a whole parallel
     *    server, silently different from the content tree, discoverable only by
     *    finding a behaviour where the two disagreed. Now it visibly does
     *    nothing.
     */
    fprintf(stderr, "mock230 selftest: the inverted fallback\n");
    {
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        /* Not a skip: claim 3 is *about* the no-pack case, so it is the half
         * that runs either way. */
        /* Was 7. `ai_queue3` moved to `[ai_queue3,_]` in
         * skill_combat/npc_combat.rs2 and the row went with it — the number is
         * the evidence, which is why the assertion is on the number and not on
         * the names. */
        SELFTEST_CHECK(MOCK230_FALLBACK_COUNT == 6,
                       "the engine fallback list should still be 6 long, not %d — it may "
                       "shrink as content grows, never grow",
                       (int)MOCK230_FALLBACK_COUNT);
        SELFTEST_CHECK(mock230_scripts_report_fallbacks(&srv) == MOCK230_FALLBACK_COUNT,
                       "the boot report should name every one of them");

        /*
         * And that each row's *reason* is still a reason.
         *
         * The count above cannot catch the way this list actually failed.
         * `ai_queue3`'s row was correct C answering a real gap, and its
         * `blocked_on` string had been false for two stages — npc categories,
         * the category rung and 69 drop-table files had all landed underneath
         * it. Nothing changed in the row, which is exactly why nobody noticed:
         * an expired reason and a live one are the same text.
         *
         * So the opcode-shaped half of every blocker is a citation the machine
         * can resolve, and this asserts that none of them has arrived. It goes
         * red on the day somebody implements OBJ_DEL or declares a `last_verb`
         * reader — which is the day the row it justifies is either deletable or
         * lying. Mutation: temporarily add `SS_OP_OBJ_ADD` (implemented) to
         * `k_blocked_opobj` and this turns red with the STALE BLOCKER line;
         * that was run.
         */
        SELFTEST_CHECK(mock230_scripts_stale_blockers() == 0,
                       "no fallback row should still be waiting on an opcode that has "
                       "landed — %d row(s) are, and each is either deletable or has to "
                       "have its reason rewritten",
                       mock230_scripts_stale_blockers());

        if( loaded )
        {
            const struct Mock230Ids* ids = mock230_ids();
            int sword = mock230_content_symbol(MOCK230_PACK_OBJ, "bronze_sword");
            uint8_t held[8];
            static struct Mock230Item saved_inv[MOCK230_INV_SLOTS];
            static struct Mock230Item saved_worn[MOCK230_WORN_SLOTS];

            memcpy(saved_inv, player->inv, sizeof(saved_inv));
            memcpy(saved_worn, player->worn, sizeof(saved_worn));

            /* The gate itself, at all three inputs. */
            SELFTEST_CHECK(
                mock230_scripts_fallback(&srv, MOCK230_FALLBACK_OPHELD, MOCK230_TRIGGER_NONE) == 1,
                "an unbound trigger should let its engine fallback run");
            SELFTEST_CHECK(
                mock230_scripts_fallback(&srv, MOCK230_FALLBACK_OPHELD, MOCK230_TRIGGER_RAN) == 0,
                "a trigger content handled should not also run the fallback");
            SELFTEST_CHECK(
                mock230_scripts_fallback(&srv, MOCK230_FALLBACK_OPHELD, MOCK230_TRIGGER_FAILED) == 0,
                "a script that FAILED should not hand its click to C");

            /* And the tri-state that feeds it. `[opnpc5]` is bound on nothing in
             * the tree, so goblin 3105 answers NONE rather than the RAN/aborted
             * 0 the old two-valued return could not tell apart. */
            SELFTEST_CHECK(mock230_scripts_run_trigger(&srv, SS_TRIGGER_OPNPC5, 3105, -1, -1) ==
                               MOCK230_TRIGGER_NONE,
                           "an unbound trigger should answer NONE");

            /*
             * Triage §7.7 — the *other* way a trigger and the engine can
             * disagree, which inverting the fallback does not catch: content
             * that binds an op the cache gives a verb to takes that verb over,
             * silently and successfully.
             *
             * Ids by name, never typed in: a cache bump that moves the goblin
             * should say so here rather than quietly stop testing anything,
             * which is the failure the `cook == 4626` pin exists for.
             */
            {
                int goblin = mock230_content_symbol(MOCK230_PACK_NPC, "goblin");
                int booth = mock230_content_symbol(MOCK230_PACK_LOC, "bankbooth");

                SELFTEST_CHECK(goblin > 0 && booth > 0,
                               "goblin and bankbooth should both resolve by name");

                /* The claim is read off the record's own op list, so it has to
                 * land on the op the cache actually put the verb on — a goblin's
                 * Attack is op 2, and an implementation that assumed op 1 would
                 * pass every test that only ever asked about op 1. */
                SELFTEST_CHECK(
                    mock230_world_engine_claimed_verb(SS_TRIGGER_OPNPC2, goblin) != NULL,
                    "the engine should claim the goblin's op 2, which the cache calls Attack");
                SELFTEST_CHECK(
                    mock230_world_engine_claimed_verb(SS_TRIGGER_OPNPC1, goblin) == NULL,
                    "the goblin's op 1 carries no verb, so there is nothing to shadow");
                SELFTEST_CHECK(
                    mock230_world_engine_claimed_verb(SS_TRIGGER_OPLOC2, booth) != NULL,
                    "the engine should claim a bank booth's op 2, which the cache calls Bank");
                /* An npc's Attack is claimed; an npc's op 3 "Talk-to" is not.
                 * Without this the predicate could be "any verb at all" and
                 * every assertion above would still pass. */
                SELFTEST_CHECK(
                    mock230_world_engine_claimed_verb(SS_TRIGGER_OPLOC3, booth) == NULL,
                    "a booth's op 3 is Collect, which the engine does not answer");

                /*
                 * And the report over the whole tree. Pinned, like the fallback
                 * count above and for the same reason — it is a review list, so
                 * it changing is the signal. `[opnpc2,goblin]` is *not* in it:
                 * that script calls p_opnpc(2), which is what discharges the
                 * obligation, and a version of this check that could not tell
                 * the goblin from the booth would be measuring nothing.
                 */
                SELFTEST_CHECK(mock230_scripts_report_shadowed_ops(&srv) == 1,
                               "exactly one script should shadow an engine verb without "
                               "re-issuing it ([oploc2,bankbooth], which opens the bank itself). "
                               "If this moved, read the new list and say why each is right");
            }

            SELFTEST_CHECK(sword > 0, "bronze_sword should be in obj.pack");

            /*
             * Now the part that only means anything end to end: the same packet,
             * once with a pack and once without.
             *
             * Wielding a sword is MOCK230_FALLBACK_OPHELD — the C that stands in
             * for the reference's `[opheld2,_] ~equip(last_slot)`. With a pack it
             * runs, because no script claims bronze_sword. Without one it must
             * not, and that is the whole inversion in one assertion.
             */
            for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
                inv_set(player, i, -1, 0);
            for( int i = 0; i < MOCK230_WORN_SLOTS; i++ )
                player->worn[i].obj_id = -1;
            inv_set(player, 3, sword, 1);
            held[0] = (uint8_t)(sword >> 8);
            held[1] = (uint8_t)(sword & 0xff);
            held[2] = 0;
            held[3] = 3;
            held[4] = (uint8_t)(ids->com_inventory_items >> 24);
            held[5] = (uint8_t)(ids->com_inventory_items >> 16);
            held[6] = (uint8_t)(ids->com_inventory_items >> 8);
            held[7] = (uint8_t)ids->com_inventory_items;
            mock230_world_handle(player, PKTOUT_NAME_OPHELD2, held, 8);
            SELFTEST_CHECK(player->worn[MOCK230_WEAR_WEAPON].obj_id == sword,
                           "with a pack loaded, an unbound opheld2 should still wield");

            mock230_scripts_free(&srv);
            SELFTEST_CHECK(!srv.scripts_ok, "and now there is no pack");
            SELFTEST_CHECK(
                mock230_scripts_fallback(&srv, MOCK230_FALLBACK_OPHELD, MOCK230_TRIGGER_NONE) == 0,
                "with no pack, no fallback may run — nothing is a gap when "
                "everything is");

            for( int i = 0; i < MOCK230_WORN_SLOTS; i++ )
                player->worn[i].obj_id = -1;
            inv_set(player, 3, sword, 1);
            mock230_world_handle(player, PKTOUT_NAME_OPHELD2, held, 8);
            SELFTEST_CHECK(player->worn[MOCK230_WEAR_WEAPON].obj_id == -1,
                           "with no pack, the same click should do nothing at all, got %d",
                           player->worn[MOCK230_WEAR_WEAPON].obj_id);
            SELFTEST_CHECK(player->inv[3].obj_id == sword,
                           "and leave the sword in the backpack");

            memcpy(player->inv, saved_inv, sizeof(saved_inv));
            memcpy(player->worn, saved_worn, sizeof(saved_worn));
        }
        mock230_scripts_free(&srv);
    }

    /*
     * `[if_close]` is not a fallback, and proving it needs a pack loaded.
     *
     * The bank stanza above closes the bank with no content bound, so it could
     * never see this: with `[if_close,bankmain]` in the pack, `close_modal` used
     * to run the script and `return`, and the unmount it was suppressing never
     * happened. Walking away from an open bank left it on screen. The reference
     * runs the close script *and then* unmounts, unconditionally
     * (`Player.closeModal`), which is what this pins.
     */
    fprintf(stderr, "mock230 selftest: [if_close] does not suppress the unmount\n");
    {
        int loaded = mock230_scripts_load(&srv, "OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
            loaded = mock230_scripts_load(&srv, "../OSRS-Content/osrs239-content/server/scripts/build");

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            static struct Mock230Capture capture;

            selftest_reset_world(&srv, player, 402, 402);
            SELFTEST_CHECK(SSVM_ProviderGetByName(srv.scripts, "[if_close,bankmain]") != NULL,
                           "the tree should bind [if_close,bankmain] — without it this "
                           "stanza proves nothing");
            /*
             * And the engine has to be able to *reach* it, which it could not:
             * the compiler keys `[if_close,bankmain]` on the bare interface id
             * 12 and the close asked with `MOCK230_COM(12, 0)`. The two never
             * met, so this stanza was vacuous the first time it was written —
             * it "passed" against a trigger that resolved to nothing.
             *
             * What this pins is the *convention* (subject = interface id), not
             * the call site's expression, which nothing in one process can
             * observe: `@closebank` is two `inv_stoptransmit`s and sends no
             * packet, so a close that ran the script and one that did not look
             * identical from outside. Restating the convention here is the
             * honest amount of coverage available, and it fails if the
             * compiler's subject encoding moves.
             */
            SELFTEST_CHECK(SSVM_ProviderGetByTrigger(srv.scripts, SS_TRIGGER_IF_CLOSE,
                                                     mock230_ids()->iface_bankmain,
                                                     -1) != NULL,
                           "and [if_close] should key on the interface id, not a com uid");
            SELFTEST_CHECK(SSVM_ProviderGetByTrigger(srv.scripts, SS_TRIGGER_IF_CLOSE,
                                                     MOCK230_COM(mock230_ids()->iface_bankmain, 0),
                                                     -1) == NULL,
                           "and a com uid should find nothing — that was the bug");

            mock230_bank_open(&srv);
            SELFTEST_CHECK(player->bank.open, "the bank should be open");

            mock230_capture_begin(&srv, &capture);
            mock230_world_close_modal(&srv);
            mock230_capture_end(&srv);

            SELFTEST_CHECK(!player->bank.open,
                           "closing should clear the open flag even with a script bound");
            SELFTEST_CHECK(mock230_capture_find(&capture, 36 /* IF_CLOSESUB */, 0) >= 0,
                           "and the unmount must still reach the client");
        }
        mock230_scripts_free(&srv);
    }

    fprintf(stderr, "mock230 selftest: the friend service\n");
    {
        /*
         * The service is a process-scoped singleton, so it has whatever the
         * sections above left in it — the player logged in at the top of this
         * function is in there. Starting from a known roster is the section's
         * job, not the service's.
         */
        const int64_t alice = (int64_t)strtobase37("alice");
        const int64_t bob = (int64_t)strtobase37("bob");
        const int64_t carol = (int64_t)strtobase37("carol");
        int64_t out37 = 0;
        int world = -1;
        int64_t followers[4];

        mock230_friends_reset();

        /* A name has to survive the round trip, or the reference refuses it —
         * `fromBase37(x) === 'invalid_name'`. 0 is the case that matters: it is
         * what a player with no name has, and what an empty wire field decodes
         * to. */
        SELFTEST_CHECK(mock230_friends_add(0, bob) == MOCK230_SOCIAL_INVALID_NAME,
                       "an unnamed player must not be able to add a friend");
        SELFTEST_CHECK(mock230_friends_add(alice, 0) == MOCK230_SOCIAL_INVALID_NAME,
                       "an empty name must not be addable");

        /* alice adds bob while bob has never logged in. This is the case the
         * whole name-keyed design exists for. */
        SELFTEST_CHECK(mock230_friends_add(alice, bob) == MOCK230_SOCIAL_OK,
                       "alice should be able to add bob");
        SELFTEST_CHECK(mock230_friends_add(alice, bob) == MOCK230_SOCIAL_UNCHANGED,
                       "adding the same friend twice should change nothing");
        SELFTEST_CHECK(mock230_friends_count(alice) == 1, "alice should have one friend, got %d",
                       mock230_friends_count(alice));
        SELFTEST_CHECK(mock230_friends_is_friend(alice, bob), "bob should be in alice's list");
        SELFTEST_CHECK(!mock230_friends_is_friend(bob, alice), "friendship is not symmetric");

        /* getFollowers, answered for a player who is offline and has never been
         * seen — a per-player array could not do this. */
        SELFTEST_CHECK(mock230_friends_followers(bob, followers, 4) == 1,
                       "bob should have exactly one follower");
        SELFTEST_CHECK(followers[0] == alice, "bob's follower should be alice");

        /* Offline: the world byte is 0 and the panel reads "Offline". */
        SELFTEST_CHECK(mock230_friends_get(alice, 0, &out37, &world) && out37 == bob &&
                           world == 0,
                       "an offline friend should report world 0, got %d", world);

        /*
         * `isVisibleTo`, one branch at a time. The default for a name the
         * service has only ever heard *of* is OFF, so bob logging in is not
         * enough on its own — his mode has to say so.
         */
        mock230_friends_login(bob, 0, MOCK230_CHAT_PRIVATE_OFF, 0, 0);
        SELFTEST_CHECK(mock230_friends_world(bob) != 0, "bob should be online");
        SELFTEST_CHECK(mock230_friends_get(alice, 0, &out37, &world) && world == 0,
                       "private chat OFF should hide bob from alice, got world %d", world);

        mock230_friends_set_chat_modes(bob, 0, MOCK230_CHAT_PRIVATE_FRIENDS, 0);
        SELFTEST_CHECK(mock230_friends_get(alice, 0, &out37, &world) && world == 0,
                       "FRIENDS should hide bob until he adds alice back");
        SELFTEST_CHECK(mock230_friends_add(bob, alice) == MOCK230_SOCIAL_OK, "bob adds alice");
        SELFTEST_CHECK(mock230_friends_get(alice, 0, &out37, &world) && world != 0,
                       "FRIENDS should reveal bob once it is mutual");

        mock230_friends_set_chat_modes(bob, 0, MOCK230_CHAT_PRIVATE_ON, 0);
        SELFTEST_CHECK(mock230_friends_get(alice, 0, &out37, &world) && world != 0,
                       "ON should reveal bob to anyone");

        /* Ignored-by-target beats everything except staff, and it beats
         * friendship: bob has alice on his friend list right now. */
        SELFTEST_CHECK(mock230_friends_ignore_add(bob, alice) == MOCK230_SOCIAL_OK,
                       "bob should be able to ignore alice");
        SELFTEST_CHECK(!mock230_friends_visible_to(alice, bob),
                       "being ignored by bob should hide him from alice");
        SELFTEST_CHECK(mock230_friends_get(alice, 0, &out37, &world) && world == 0,
                       "...and the friend row should read offline");
        /* The other direction is untouched — the rule reads the *target's*
         * list and the *target's* mode. alice has never logged in, so her mode
         * is the default OFF and she is invisible for that reason, not for
         * anything bob's ignore list says. */
        SELFTEST_CHECK(mock230_friends_visible_to(bob, alice) == 0,
                       "a player who has never logged in defaults to OFF");
        SELFTEST_CHECK(mock230_friends_ignore_del(bob, alice) == MOCK230_SOCIAL_OK,
                       "bob unignores alice");
        SELFTEST_CHECK(mock230_friends_visible_to(alice, bob), "bob is visible again");

        /* Staff bypass. The service is the only thing that can set this today;
         * the rule is ported because dropping the line would be dropping a
         * rule, not because anything reaches it yet. */
        mock230_friends_login(carol, 0, MOCK230_CHAT_PRIVATE_ON, 0, /* staff */ 2);
        mock230_friends_set_chat_modes(bob, 0, MOCK230_CHAT_PRIVATE_OFF, 0);
        SELFTEST_CHECK(mock230_friends_visible_to(carol, bob),
                       "staff should see through private chat OFF");
        SELFTEST_CHECK(!mock230_friends_visible_to(alice, bob),
                       "...and nobody else should");

        /* Logging out keeps the lists and drops only presence. */
        mock230_friends_logout(bob);
        SELFTEST_CHECK(mock230_friends_world(bob) == 0, "bob should be offline");
        SELFTEST_CHECK(mock230_friends_count(bob) == 1,
                       "a logout must not lose bob's friend list");

        /* Deleting: order is preserved, and deleting what is not there is the
         * reference's silent no-op rather than an error. */
        SELFTEST_CHECK(mock230_friends_del(alice, carol) == MOCK230_SOCIAL_UNCHANGED,
                       "deleting a non-friend should change nothing");
        SELFTEST_CHECK(mock230_friends_add(alice, carol) == MOCK230_SOCIAL_OK, "alice adds carol");
        SELFTEST_CHECK(mock230_friends_del(alice, bob) == MOCK230_SOCIAL_OK, "alice drops bob");
        SELFTEST_CHECK(mock230_friends_count(alice) == 1, "one friend left");
        SELFTEST_CHECK(mock230_friends_get(alice, 0, &out37, &world) && out37 == carol,
                       "the gap should close rather than the tail being swapped in");

        /* The cap. It is content's number; with no content constant it falls
         * back to the storage ceiling, and either way the list stops there. */
        {
            int cap = mock230_friends_cap_friends();
            enum Mock230SocialResult last = MOCK230_SOCIAL_OK;

            for( int i = 0; i < cap + 4; i++ )
            {
                char name[16];

                snprintf(name, sizeof(name), "cap%d", i);
                last = mock230_friends_add(bob, (int64_t)strtobase37(name));
                if( last == MOCK230_SOCIAL_FULL )
                    break;
            }
            SELFTEST_CHECK(last == MOCK230_SOCIAL_FULL,
                           "the friend list should refuse at the cap, got result %d", (int)last);
            SELFTEST_CHECK(mock230_friends_count(bob) == cap,
                           "the list should stop at %d, got %d", cap,
                           mock230_friends_count(bob));
        }

        /* Private message ids are never 0 — the client's dedupe ring is
         * zero-filled and would swallow one that was. */
        {
            int32_t first = mock230_friends_next_pm_id();
            int32_t second = mock230_friends_next_pm_id();

            SELFTEST_CHECK(first != 0 && second != 0, "a pm id must never be 0");
            SELFTEST_CHECK(first != second, "pm ids must not repeat");
        }

        /* One social packet per tick, per player, across the whole family. */
        SELFTEST_CHECK(mock230_friends_social_gate(player),
                       "the first social packet of a tick should be allowed");
        SELFTEST_CHECK(!mock230_friends_social_gate(player),
                       "the second should not");
        mock230_world_tick(&srv);
        SELFTEST_CHECK(mock230_friends_social_gate(player),
                       "the tick should have released the latch");

        /* The world's half of the wiring: the base-37 key is derived where the
         * name is set, so the two cannot drift. */
        mock230_world_set_display_name(player, "zezima");
        SELFTEST_CHECK(player->name37 == (int64_t)strtobase37("zezima"),
                       "setting a display name should derive its base-37 key");

        mock230_friends_reset();
    }

    if( g_selftest_failures )
        fprintf(stderr, "mock230 selftest: %d failure(s)\n", g_selftest_failures);
    else
        fprintf(stderr, "mock230 selftest: all checks passed\n");
    return g_selftest_failures;
}
