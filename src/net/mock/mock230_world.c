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
#include "mock230_equipment.h"
#include "mock230_ids.h"
#include "mock230_scene.h"
#include "engine/world_builder/collision_map.h"
#include "ss_trigger.h"
#include "ssvm_provider.h"

#include "net/rev/pktnames.h"

#include <rsareabuf.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* The tile a session logs in on. See mock230_world_set_home. */
static int g_home_x = 3222;
static int g_home_z = 3218;

static void
mock230_world_build_entities(struct Mock230Server* srv);

static void
ground_forget(
    struct Mock230Server* srv,
    int slot);

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
        switch( interaction->kind )
        {
        case MOCK230_INTERACT_NPC:
            trigger = SS_TRIGGER_APNPC1 + (interaction->op - 1);
            break;
        case MOCK230_INTERACT_LOC:
            trigger = SS_TRIGGER_APLOC1 + (interaction->op - 1);
            break;
        case MOCK230_INTERACT_OBJ:
            trigger = SS_TRIGGER_APOBJ1 + (interaction->op - 1);
            break;
        default:
            trigger = -1;
            break;
        }
        if( trigger >= 0 &&
            mock230_scripts_run_trigger(srv, trigger, interaction->target_id, -1,
                                        interaction->npc_slot) )
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

        /*
         * Clear *before* running, not after. A script is allowed to start a new
         * interaction (`p_opnpc`), and clearing afterwards would throw away the
         * one it just asked for.
         */
        mock230_world_interaction_clear(srv);
        steps_clear(player);
        player->dest_x = -1;
        player->dest_z = -1;

        switch( kind )
        {
        case MOCK230_INTERACT_NPC:
            if( !mock230_scripts_run_trigger(srv, SS_TRIGGER_OPNPC1 + (op_num - 1), target_id,
                                             -1, slot) )
                interaction_engine_npc(srv, slot, op_num);
            break;
        case MOCK230_INTERACT_LOC:
            if( !mock230_scripts_run_trigger(srv, SS_TRIGGER_OPLOC1 + (op_num - 1), target_id,
                                             -1, -1) )
                interaction_engine_loc(srv, op_num, target_id, loc_x, loc_z, loc_level);
            break;
        case MOCK230_INTERACT_OBJ:
            if( !mock230_scripts_run_trigger(srv, SS_TRIGGER_OPOBJ1 + (op_num - 1), target_id,
                                             -1, -1) )
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
        /* A rebuild resets the client's zones, so everything it was told about
         * has to be said again. Cleared here, where the rebuild is *decided*,
         * rather than where it is sent: phase 8 flushes ground objs and phase 10
         * sends the rebuild, so clearing at send time would undo a flush that
         * already happened this tick. */
        memset(player->ground_sent, 0, sizeof(player->ground_sent));
    }

    /* The server's collision window moves with the client's scene. A door
     * opened inside the old window and still inside the new one is re-sent by
     * phase 10, which is why mock230_scene_build keeps the changed list. */
    mock230_scene_build(mock230_world_cache_dir(), srv->zone_x, srv->zone_z);
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
        npc->timer_script = -1;
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
    for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
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
         * `npc_queue` and `npc_settimer`, in phase 4's own order: queues before
         * timers, matching the reference's `npc.processQueue()` then
         * `npc.processTimers()`. Both dispatch by npc *type*, so an npc that
         * changed type between queueing and firing runs the new type's script —
         * which is what `npc_changetype` is for and why the script id is not
         * stored.
         */
        if( npc->active )
        {
            for( int i = 0; i < MOCK230_NPC_QUEUE_MAX; i++ )
            {
                if( !npc->queue[i].active )
                    continue;
                if( --npc->queue[i].delay > 0 )
                    continue;
                npc->queue[i].active = 0;
                mock230_scripts_run_trigger(srv, SS_TRIGGER_AI_QUEUE1 + (npc->queue[i].queue - 1),
                                            npc->type, -1, slot);
            }
        }
        if( npc->active && npc->timer_interval > 0 )
        {
            if( ++npc->timer_clock >= npc->timer_interval )
            {
                npc->timer_clock = 0;
                mock230_scripts_run_trigger(srv, SS_TRIGGER_AI_TIMER, npc->type, -1, slot);
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
            obj->count += count;
            /* Re-sent as an OBJ_ADD by phase 8, to everyone who had the old
             * count. (A real OBJ_COUNT would say old->new in one packet; this
             * server re-adds, which the client accepts as a replacement.) */
            ground_forget(srv, i);
            return i;
        }
    }

    for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
    {
        struct Mock230GroundObj* obj = &srv->ground[i];

        if( obj->active )
            continue;
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
        return i;
    }
    return -1;
}

/*
 * Tell everyone whose client has this obj that it is gone, and forget it.
 *
 * A ground obj is world state, but "has been told about it" is per client, so
 * removing one is a broadcast rather than a send. Doing it for the active player
 * only is what would leave a taken item drawn on the floor for everybody else,
 * pickable, forever.
 */
static void
ground_withdraw(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230GroundObj* obj = &srv->ground[slot];

    for( int i = 0; i < srv->player_count; i++ )
    {
        struct Mock230Player* player = &srv->players[i];
        int pos;

        if( !player->active || !player->ground_sent[slot] )
            continue;
        pos = mock230_send_zone(player, obj->x, obj->z);
        mock230_send_obj_del(player, pos, obj->obj_id);
        player->ground_sent[slot] = 0;
    }
}

/** Forget without telling: for a change that phase 8 will re-send anyway. */
static void
ground_forget(
    struct Mock230Server* srv,
    int slot)
{
    for( int i = 0; i < MOCK230_PLAYER_MAX; i++ )
        srv->players[i].ground_sent[slot] = 0;
}

/**
 * Tell one client about every ground obj inside the scene it does not know
 * about, and take back the ones that left.
 *
 * The *world* half — despawning and respawning — is `ground_tick`, and runs
 * once. This runs per player, which is the whole difference: whether an obj
 * exists is the world's answer, whether this client has been told is not.
 */
static void
flush_ground(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;

    for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
    {
        struct Mock230GroundObj* obj = &srv->ground[i];
        int in_scene;

        if( !obj->active )
            continue;

        in_scene = obj->level == player->level && mock230_scene_contains(obj->x, obj->z);
        if( in_scene && !player->ground_sent[i] )
        {
            int pos = mock230_send_zone(player, obj->x, obj->z);

            mock230_send_obj_add(player, pos, obj->obj_id, obj->count);
            player->ground_sent[i] = 1;
        }
        else if( !in_scene && player->ground_sent[i] )
        {
            player->ground_sent[i] = 0;
        }
    }
}

/** The world half: expire drops and bring taken spawns back. Once a tick, for
 *  everybody, before the per-player flush. */
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
                ground_forget(srv, i);
            }
            continue;
        }
        if( obj->despawn_tick >= 0 && srv->tick >= obj->despawn_tick )
        {
            ground_withdraw(srv, i);
            obj->active = 0;
        }
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

    ground_withdraw(srv, slot);
    obj->active = 0;
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
     * refusal through the [inv_button<n>] trigger; the engine's own router is
     * the no-content fallback.
     */
    if( bank_component(component) )
    {
        player->last_slot = slot;
        player->last_com = component;
        if( mock230_scripts_run_trigger(srv, SS_TRIGGER_INV_BUTTON1 + (op_num - 1), component,
                                        -1, -1) )
            return;
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
    if( op_num >= 1 && op_num <= 5 &&
        mock230_scripts_run_trigger(srv, SS_TRIGGER_OPHELD1 + (op_num - 1), obj_id,
                                    info->category > 0 ? info->category : -1, -1) )
        return;

    if( verb && (strcmp(verb, "Wear") == 0 || strcmp(verb, "Wield") == 0) )
    {
        equip_from_slot(srv, slot);
        return;
    }
    if( verb && strcmp(verb, "Drop") == 0 )
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
    if( verb && strcmp(verb, "Attack") == 0 )
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
    memset(player->ground_sent, 0, sizeof(player->ground_sent));
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

        if( !mock230_scene_replace_loc(slot, def->next_loc_stage, loc->angle) )
        {
            mock230_say(srv, "nothing_interesting_message", NULL);
            return;
        }
        /* A door is the canonical two-player case: whoever opened it is not the
         * only person who can now walk through it. */
        mock230_world_broadcast_loc(srv, loc->x, loc->z, loc->level, loc->shape, loc->angle,
                                    loc->loc_id);
        if( srv->verbose )
            fprintf(stderr, "mock230: %s %s at %d,%d\n", opening ? "opened" : "closed",
                    def->symbol ? def->symbol : "door", loc->x, loc->z);
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
    if( verb && (strcmp(verb, "Bank") == 0 || strcmp(verb, "Use-quickly") == 0) )
    {
        mock230_bank_open(srv);
        return;
    }
    if( verb && strcmp(verb, "Climb-up") == 0 )
    {
        climb(srv, +1);
        return;
    }
    if( verb && strcmp(verb, "Climb-down") == 0 )
    {
        climb(srv, -1);
        return;
    }
    if( verb && strcmp(verb, "Climb") == 0 )
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
            if( !mock230_scripts_run_trigger(srv, SS_TRIGGER_OPNPC1 + (op_num - 1),
                                             srv->npcs[slot].type, -1, slot) )
                say(srv, "npc %d has no [opnpc%d] script.", srv->npcs[slot].type, op_num);
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

    /* Content bound to the component wins; the engine's own routers are the
     * no-content fallback, which is what keeps the bank's toggles working with
     * no script pack. */
    srv->active_player->last_com = uid;
    srv->active_player->last_slot = -1;
    if( mock230_scripts_run_trigger(srv, SS_TRIGGER_IF_BUTTON, uid, -1, -1) )
        return;
    /* Components above interface 31 compile name-addressed — see
     * mock230_scripts_run_if_button_named. */
    if( mock230_scripts_run_if_button_named(srv, uid) )
        return;
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
    /* run_trigger's parameters are (trigger, type, category, npc_slot): the
     * component uid is the type, and an interface button has neither a category
     * nor an npc. `sub` and `op` reach content through last_slot and last_verb,
     * which is where a RuneScript trigger reads them. */
    if( mock230_scripts_run_trigger(srv, SS_TRIGGER_IF_BUTTON, uid, -1, -1) )
        return;
    if( mock230_scripts_run_if_button_named(srv, uid) )
        return;
    /* (uid, sub, obj, op) — the bank needs the sub id, which is which of the
     * 1,220 dynamic children was clicked. */
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
    struct Mock230Player* player;
    const struct Mock230Ids* ids = mock230_ids();
    int main_group;

    if( !srv || !srv->active_player )
        return;
    player = srv->active_player;

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
    /* Content's `[if_close,<iface>:0]` gets first refusal, exactly as it did for
     * the bank, and only a script that does not exist falls through to the
     * engine's own close below. */
    if( mock230_scripts_run_trigger(srv, SS_TRIGGER_IF_CLOSE, MOCK230_COM(main_group, 0), -1, -1) )
        return;

    /* One screen keeps state of its own beyond the mount — the bank, which has
     * containers and a reorganise — so it closes through its own function.
     * Anything else is just a mount, and dropping it is the whole of closing
     * it: the equipment screen's repaint is gated on `mainmodal_group`, which
     * the closesub below already clears. */
    if( player->bank.open )
        mock230_bank_close(srv);
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
 * An npc reached zero hitpoints.
 *
 * Content first: `[ai_queue3,<npc>]` is LostCity's death trigger, and its
 * scripts are the drop table — a sequence of `obj_add(npc_coord, ...)` calls
 * under a `random(128)` roll, exactly as the reference writes them. When
 * nothing is bound, the config's `param=death_drop` still drops (bones for
 * almost everything), so an npc with no script is not a silent kill.
 */
void
mock230_world_npc_died(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Npc* npc = &srv->npcs[slot];

    if( mock230_scripts_run_trigger(srv, SS_TRIGGER_AI_QUEUE3, npc->type, -1, slot) )
        return;

    if( npc->def && npc->def->death_drop >= 0 )
        mock230_world_obj_add(srv, npc->def->death_drop, 1, npc->x, npc->z, npc->level,
                              mock230_ids()->lootdrop_duration);
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
static void
varp_side_effects(
    struct Mock230Server* srv,
    int varp,
    int value)
{
    struct Mock230Player* player = srv->active_player;

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
    if( !player || !player->active )
        return;

    /*
     * The bank is heap-allocated per player, so a logout that only cleared
     * `active` would leak it — and the next player into this slot would inherit
     * the pointer through the memset in mock230_world_add_player.
     */
    mock230_bank_shutdown_player(player);
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
     * runs on the next tick. These two are the no-content fallback. */
    if( !srv->scripts_ok )
    {
        
        
    }
    player->login_pending = 1;

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
    for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
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
    for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
    {
        if( !srv->npcs[slot].active )
            continue;
        /* Resume before the timer fires: a delayed npc is busy, and starting a
         * second script on it would interleave two sets of writes. */
        mock230_scripts_resume_npc(srv, slot);
        if( !srv->npcs[slot].active_script )
            mock230_scripts_process_npc_timer(srv, slot);
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
         * to change without recompiling belongs. */
        mock230_scripts_run_trigger(srv, SS_TRIGGER_LOGIN, -1, -1, -1);
    }
}

/** 8. Loc/obj respawn timers and the per-zone event flush. */
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
/*
 * A loc change is a *zone* event: it happened in the world, and everyone
 * standing where they could see it has to be told.
 *
 * Sending it to the active player only is the single-player shape, and it fails
 * in a way nobody would report as a protocol bug — a door one player opens stays
 * shut on the other's screen, and walking through it looks like the collision
 * map disagreeing with the picture.
 *
 * The recipient set here is "every player on that level", not "every player in
 * that zone", because the server has no zone index yet and the client discards a
 * zone packet for a zone it does not hold. §6.1 step 3 replaces this with a
 * `ZoneMap` whose events also *replay* to whoever walks in afterwards, which is
 * the half a broadcast cannot do: a player who logs in later still sees the door
 * shut, because the scene is rebuilt from the cache and nothing re-states the
 * change. `mock230_scene_next_changed_loc` covers exactly that case for a
 * rebuild, and phase 10 uses it.
 */
void
mock230_world_broadcast_loc(
    struct Mock230Server* srv,
    int x,
    int z,
    int level,
    int shape,
    int angle,
    int loc_id)
{
    struct Mock230Player* player;

    MOCK230_FOR_EACH_PLAYER(srv, player)
    {
        int pos;

        if( player->level != level || !mock230_scene_contains(x, z) )
            continue;
        pos = mock230_send_zone(player, x, z);
        if( loc_id < 0 )
            mock230_send_loc_del(player, pos, shape, angle);
        else
            mock230_send_loc_add_change(player, pos, shape, angle, loc_id);
    }
}

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

        if( entry->loc_id < 0 )
        {
            /* Undo a loc_add. */
            if( mock230_scene_remove_loc(entry->slot) )
                mock230_world_broadcast_loc(srv, entry->x, entry->z, entry->level, entry->shape,
                                            entry->angle, -1);
        }
        else
        {
            struct Mock230SceneLoc* loc = mock230_scene_loc(entry->slot);

            /*
             * A `loc_del` freed the slot, so putting the loc back is an *add*,
             * not a replace. Telling the two apart by the slot's own `active`
             * flag rather than by remembering which opcode queued it means a
             * revert does the right thing even if something else touched the
             * slot meanwhile.
             */
            if( loc && loc->active )
            {
                if( mock230_scene_replace_loc(entry->slot, entry->loc_id, entry->angle) )
                    mock230_world_broadcast_loc(srv, entry->x, entry->z, entry->level,
                                                entry->shape, entry->angle, entry->loc_id);
            }
            else if( mock230_scene_add_loc(entry->x, entry->z, entry->level, entry->loc_id,
                                           entry->shape, entry->angle) >= 0 )
            {
                mock230_world_broadcast_loc(srv, entry->x, entry->z, entry->level, entry->shape,
                                            entry->angle, entry->loc_id);
            }
        }
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
    int slot,
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
        entry->slot = slot;
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

static void
phase_zones(struct Mock230Server* srv)
{
    struct Mock230Player* player;

    mock230_world_loc_reverts(srv);
    /* The world's half first — an obj that despawns this tick must not be
     * flushed *to* anyone as an add and then removed. */
    ground_tick(srv);
    MOCK230_FOR_EACH_PLAYER(srv, player)
        flush_ground(player);
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

/** 10. Everything the tick has to say, in the order the client expects it. */
static void
phase_client_out(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;

    mock230_world_set_active(srv, player);

    /* A rebuild has to reach the client before the placement that depends on
     * it, and the client's serial packet queue holds every later packet until
     * the world load finishes — so ordering here is simply "rebuild first". */
    if( player->rebuild_pending )
    {
        mock230_send_rebuild_normal(player);
        player->rebuild_pending = 0;
        /* Doors. REBUILD_NORMAL rebuilds the scene from the
         * cache, which puts every opened door back the way the map square has
         * it — so each one has to be re-opened. Without this, walking far
         * enough to trigger a rebuild and coming back finds every door you
         * opened shut again, and the server still believing they are open. */
        for( int slot = mock230_scene_next_changed_loc(0); slot >= 0;
             slot = mock230_scene_next_changed_loc(slot + 1) )
        {
            struct Mock230SceneLoc* loc = mock230_scene_loc(slot);
            int pos;

            if( !loc || loc->level != player->level )
                continue;
            pos = mock230_send_zone(player, loc->x, loc->z);
            mock230_send_loc_add_change(player, pos, loc->shape, loc->angle, loc->loc_id);
        }
        /* The scene moved under the player, so the step directions computed
         * before it are meaningless. */
        player->move_count = 0;
    }

    mock230_send_player_info(player);
    mock230_send_npc_info(player);

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

    for( int i = 0; i < MOCK230_NPC_MAX; i++ )
    {
        srv->npcs[i].masks = 0;
        srv->npcs[i].step_dir = -1;
        srv->npcs[i].anim_id = -1;
        srv->npcs[i].anim_delay = 0;
    }
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
     * unfinished click resolved in the middle of the next one's fight. */
    mock230_world_interaction_clear(srv);
    /* Moving the player is a teleport, and a teleport may need the scene to
     * follow. Without this the collision window and the ground-obj visibility
     * test still describe wherever the last section left off. */
    player->place_dirty = 1;
    maybe_rebuild(srv);
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

            inv_set(player, 0, meat, 1);
            player->hitpoints = 10;
            player->stat_boosted[MOCK230_STAT_HITPOINTS] = 10;
            mock230_world_handle(player, PKTOUT_NAME_OPHELD1, held, 8);
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

        /* A drop reaches the client as a zone header plus an OBJ_ADD. Both, in
         * that order: a sub-packet with no zone before it applies to whatever
         * zone was named last. */
        selftest_park_player(&srv, 3222, 3218);
        mock230_capture_begin(&srv, &capture);
        mock230_world_obj_add(&srv, 526 /* bones */, 1, 3222, 3218, 0,
                              mock230_ids()->lootdrop_duration);
        mock230_world_tick(&srv);
        mock230_capture_end(&srv);
        {
            static const int k_drop[] = { 106 /* UPDATE_ZONE */, 120 /* OBJ_ADD */ };

            SELFTEST_CHECK(mock230_capture_has_sequence(&capture, k_drop, 2),
                           "a drop should send its zone then its OBJ_ADD");
        }

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
            mock230_capture_end(&srv);
        }
        SELFTEST_CHECK(free_before >= 0 && player->inv[free_before].obj_id == 526,
                       "taking an obj puts it in the backpack");
        SELFTEST_CHECK(mock230_capture_find(&capture, 121 /* OBJ_DEL */, 0) >= 0,
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
            SELFTEST_CHECK(loc->changed, "and marks it changed so a rebuild re-sends it");
            {
                static const int k_open[] = { 106 /* UPDATE_ZONE */, 70 /* LOC_ADD_CHANGE */ };

                SELFTEST_CHECK(mock230_capture_has_sequence(&capture, k_open, 2),
                               "and tells the client, zone first");
            }

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

        /* Drain: protect from melee is 12 a tick against a resistance of 60, so
         * a point goes every fifth tick. */
        mock230_scripts_run_proc(&srv, "[proc,prayer_deactivate_all]", NULL, 0);
        player->stat_level[MOCK230_STAT_PRAYER] = 99;
        selftest_prayer_toggle(&srv, "prayer_protectfrommelee");
        player->stat_boosted[MOCK230_STAT_PRAYER] = 99;
        mock230_scripts_process_timers(&srv);
        SELFTEST_CHECK(player->stat_boosted[MOCK230_STAT_PRAYER] == 99,
                       "12 units is not yet a point");
        mock230_scripts_process_timers(&srv);
        SELFTEST_CHECK(player->stat_boosted[MOCK230_STAT_PRAYER] == 99, "nor is 24");
        for( int i = 0; i < 3; i++ )
            mock230_scripts_process_timers(&srv);
        SELFTEST_CHECK(player->stat_boosted[MOCK230_STAT_PRAYER] == 98,
                       "60 units is one prayer point, got %d",
                       player->stat_boosted[MOCK230_STAT_PRAYER]);

        /* Running out drops everything, including the overhead. */
        player->stat_boosted[MOCK230_STAT_PRAYER] = 1;
        for( int i = 0; i < 10; i++ )
            mock230_scripts_process_timers(&srv);
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
             * bits, so it resolves through the *named* path
             * (mock230_scripts_run_if_button_named) — the reason this needs the
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

                /* delay 1 means tick +2, for the same +1 reason p_delay has. */
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0, "the queue should not fire immediately");
                mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 0, "nor on the next tick");
                mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[SELFTEST_VARP_QUEST_PROGRESS] == 1,
                               "[ai_queue1,chicken] should fire on tick +2, got %d",
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
                SELFTEST_CHECK(loc && loc->changed,
                               "and mark the slot changed so a rebuild re-sends it");

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

    if( g_selftest_failures )
        fprintf(stderr, "mock230 selftest: %d failure(s)\n", g_selftest_failures);
    else
        fprintf(stderr, "mock230 selftest: all checks passed\n");
    return g_selftest_failures;
}
