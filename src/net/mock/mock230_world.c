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
#include "mock230_prayer.h"
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
    mock230_send_message(srv, text);
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
    struct Mock230Player* player = srv->player;
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
        say(srv, "You can't wear that.");
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
            say(srv, "You don't have enough inventory space to do that.");
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
    say(srv, "You equip the %s.", info->name);
}

/* Take the item off worn slot `slot` and put it back in the backpack. */
static void
unequip_slot(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230Player* player = srv->player;
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
        say(srv, "You don't have enough inventory space to do that.");
        return;
    }
    inv_set(player, dest, obj_id, player->worn[slot].count);
    worn_set(player, slot, -1, 0);
    say(srv, "You remove the %s.", mock230_objinfo(obj_id)->name);
}

/* ------------------------------------------------------------------ */
/* Movement                                                            */
/* ------------------------------------------------------------------ */

static void
steps_clear(struct Mock230Player* player)
{
    player->step_count = 0;
    player->step_head = 0;
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

    while( cur_x != x || cur_z != z )
    {
        cur_x += sign_of(x - cur_x);
        cur_z += sign_of(z - cur_z);
        steps_push(player, cur_x, cur_z);
        if( player->step_count >= MOCK230_STEP_MAX )
            return;
    }
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
    struct Mock230Player* player = srv->player;

    steps_clear(player);
    player->dest_x = x - sign_of(x - player->x);
    player->dest_z = z - sign_of(z - player->z);
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
    memset(&srv->player->interaction, 0, sizeof(srv->player->interaction));
    srv->player->interaction.kind = MOCK230_INTERACT_NONE;
    srv->player->interaction.npc_slot = -1;
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
    struct Mock230Interaction* interaction = &srv->player->interaction;

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
    struct Mock230Interaction* interaction = &srv->player->interaction;

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
        if( npc->level != srv->player->level )
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
    struct Mock230Player* player = srv->player;
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

    for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
        if( player->inv[i].obj_id >= 0 )
            total += mock230_objinfo(player->inv[i].obj_id)->weight;
    for( int i = 0; i < MOCK230_WORN_SLOTS; i++ )
        if( player->worn[i].obj_id >= 0 )
            total += mock230_objinfo(player->worn[i].obj_id)->weight;
    return total;
}

/*
 * One tick of energy, in OldSchool's own arithmetic (xrsps
 * MovementService.updateRunEnergy, which is the same formula):
 *
 *   drain per running step = 67 + 67 * min(64, weight_kg) / 64
 *   regen per idle/walking tick = agility / 6 + 8
 *
 * so an unencumbered player gets a hair over 74 running steps from full and a
 * fully-laden one about half that, and standing still refills at roughly one
 * percent every eight ticks at level 1. Energy is spent per STEP, not per
 * tick: a running tick covers two tiles and costs twice as much as a walking
 * one would.
 *
 * Reaching zero clears the toggle rather than merely refusing to run, which is
 * what makes the orb go dark instead of the player silently walking with a lit
 * orb.
 */
static void
run_energy_tick(
    struct Mock230Server* srv,
    int run_steps)
{
    struct Mock230Player* player = srv->player;

    if( run_steps > 0 )
    {
        int weight_kg = player_weight_grams(player) / 1000;
        int drain;

        if( weight_kg < 0 )
            weight_kg = 0;
        if( weight_kg > 64 )
            weight_kg = 64;
        drain = (67 + (67 * weight_kg) / 64) * run_steps;

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
        int agility = player->stat_boosted[MOCK230_STAT_AGILITY];
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
    struct Mock230Player* player = srv->player;
    int percent = player->run_energy * 100 / MOCK230_RUN_ENERGY_MAX;
    /* Kilograms, not grams. UPDATE_RUNWEIGHT's value is read back by CS2's
     * RUNWEIGHT_VISIBLE, and the gameframe prints that with "kg" after it —
     * sending grams put "31892 kg" beside a player carrying 32. */
    int weight = player_weight_grams(player) / 1000;

    if( percent != player->run_energy_sent )
    {
        player->run_energy_sent = percent;
        mock230_send_run_energy(srv, percent);
    }
    if( weight != player->run_weight_sent )
    {
        player->run_weight_sent = weight;
        mock230_send_run_weight(srv, weight);
    }
}

/* Consume up to `max_tiles` queued steps, recording the direction of each so
 * PLAYER_INFO can spell them out. */
static void
advance_player(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;
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
            srv->clear_map_flag = 1;
            player->dest_x = -1;
            player->dest_z = -1;
        }
    }
}

/*
 * Re-centre the scene when the player nears its edge.
 *
 * The client holds a 104x104 scene based at (zone - 6) * 8. Once the player is
 * within 16 tiles of an edge the scene has to be rebuilt around them, and
 * because a rebuild throws away every entity the client was tracking, the npc
 * list is dropped and the player re-placed absolutely on the next tick.
 */
static void
maybe_rebuild(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;
    int local_x = player->x - mock230_scene_origin(srv->zone_x);
    int local_z = player->z - mock230_scene_origin(srv->zone_z);
    int edge = MOCK230_SCENE_TILES - MOCK230_REBUILD_MARGIN;

    if( local_x >= MOCK230_REBUILD_MARGIN && local_x < edge &&
        local_z >= MOCK230_REBUILD_MARGIN && local_z < edge )
        return;

    srv->zone_x = player->x >> 3;
    srv->zone_z = player->z >> 3;
    srv->rebuild_pending = 1;
    player->place_dirty = 1;
    /* A rebuild resets the client's zones, so everything it was told about has
     * to be said again. Cleared here, where the rebuild is *decided*, rather
     * than where it is sent: phase 8 flushes ground objs and phase 10 sends the
     * rebuild, so clearing at send time would undo a flush that already
     * happened this tick. */
    for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
        srv->ground[i].sent = 0;
    /* The server's collision window moves with the client's scene. A door
     * opened inside the old window and still inside the new one is re-sent by
     * phase 10, which is why mock230_scene_build keeps the changed list. */
    mock230_scene_build(mock230_world_cache_dir(), srv->zone_x, srv->zone_z);
    /* Tracked npcs deliberately survive: the client shifts every kept entity
     * by the base-tile delta when it rebuilds (App_WorldRebuildShift), so their
     * slots stay valid. Dropping and re-adding them would instead re-spawn
     * into slots the client still holds. */
}

/* ------------------------------------------------------------------ */
/* NPCs                                                                */
/* ------------------------------------------------------------------ */

static void
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
        return;
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
        npc->next_roam_tick = srv->tick + random_range(srv, 5, 30);
        npc->step_dir = -1;
        npc->face_entity = -1;

        npc->base_hitpoints = def->hitpoints > 0 ? def->hitpoints : 1;
        npc->hitpoints = npc->base_hitpoints;
        npc->max_hitpoints = npc->base_hitpoints;
        npc->combat_target = -1;
        npc->death_tick = -1;
        npc->respawn_tick = -1;
        npc->timer_script = -1;

        /*
         * Animations come from the content block, which names them by symbol.
         * The old convention-based lookup (`<lowercased npc name><suffix>`) is
         * still the fallback for an npc nothing describes: it is right often
         * enough to be worth keeping and -1 — "play nothing" — where it is not.
         */
        npc->attack_seq = def->attack_anim;
        npc->block_seq = def->defend_anim;
        npc->death_seq = def->death_anim;
        if( def == mock230_content_npc_default() )
        {
            int derived = mock230_seq_for_npc(type, "_attack");
            if( derived >= 0 )
                npc->attack_seq = derived;
            derived = mock230_seq_for_npc(type, "_block");
            if( derived >= 0 )
                npc->block_seq = derived;
            derived = mock230_seq_for_npc(type, "_death");
            if( derived >= 0 )
                npc->death_seq = derived;
        }
        return;
    }

    fprintf(stderr, "mock230: no free npc slot for type %d at %d,%d\n", type, x, z);
}

/* One tile of idle roaming, on the xrsps/RSMod timer: an idle npc re-rolls a
 * roam every 15-30 ticks and stays inside its wander radius. */
static void
advance_npcs(struct Mock230Server* srv)
{
    for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
    {
        struct Mock230Npc* npc = &srv->npcs[slot];
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
        if( npc->wander_radius <= 0 || srv->tick < npc->next_roam_tick )
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
            obj->sent = 0; /* re-sent as an OBJ_COUNT by phase 8 */
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

/** Tell the client about every ground obj inside the scene it does not know
 *  about, and take back the ones that left. */
static void
flush_ground(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;

    for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
    {
        struct Mock230GroundObj* obj = &srv->ground[i];
        int in_scene;

        if( !obj->active )
        {
            /* A taken spawn comes back where it was. */
            if( obj->respawn_tick >= 0 && srv->tick >= obj->respawn_tick )
            {
                obj->active = 1;
                obj->respawn_tick = -1;
                obj->sent = 0;
            }
            continue;
        }
        if( obj->despawn_tick >= 0 && srv->tick >= obj->despawn_tick )
        {
            if( obj->sent )
            {
                int pos = mock230_send_zone(srv, obj->x, obj->z);
                mock230_send_obj_del(srv, pos, obj->obj_id);
            }
            obj->active = 0;
            continue;
        }

        in_scene = obj->level == player->level && mock230_scene_contains(obj->x, obj->z);
        if( in_scene && !obj->sent )
        {
            int pos = mock230_send_zone(srv, obj->x, obj->z);

            mock230_send_obj_add(srv, pos, obj->obj_id, obj->count);
            obj->sent = 1;
        }
        else if( !in_scene && obj->sent )
        {
            obj->sent = 0;
        }
    }
}

/** Remove a ground obj, telling the client and arming its respawn if it was a
 *  map spawn. */
static void
ground_take(
    struct Mock230Server* srv,
    int slot)
{
    struct Mock230GroundObj* obj = &srv->ground[slot];

    if( obj->sent )
    {
        int pos = mock230_send_zone(srv, obj->x, obj->z);

        mock230_send_obj_del(srv, pos, obj->obj_id);
    }
    obj->active = 0;
    obj->sent = 0;
    obj->respawn_tick = obj->is_spawn ? srv->tick + MOCK230_LOOT_TICKS : -1;
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
    struct Mock230Player* player = srv->player;
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
     * one that does not open at all. */
    mock230_combat_stop_player(srv);
    mock230_world_interaction_clear(srv);
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
    struct Mock230Player* player = srv->player;
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
                              player->level, MOCK230_LOOT_TICKS);
        inv_set(player, slot, -1, 0);
        say(srv, "You drop the %s.", info->name);
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
                              player->level, MOCK230_LOOT_TICKS);
        inv_set(player, slot, -1, 0);
        say(srv, "You drop the %s.", info->name);
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
    say(srv, "Nothing interesting happens.");
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
    struct Mock230Player* player = srv->player;
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

    npc->face_entity = MOCK230_PLAYER_TERMINATOR; /* the local player */
    snprintf(npc->say, sizeof(npc->say), "Hello there, adventurer!");
    npc->masks |= MOCK230_NMASK_FACE_ENTITY | MOCK230_NMASK_SAY;
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

    /* A new interaction ends the old one — including the facing. Combat is
     * re-established by the engine handler if this op is "Attack". */
    mock230_combat_stop_player(srv);

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
    struct Mock230Player* player = srv->player;
    int level = player->level + delta;

    if( level < 0 || level > 3 )
    {
        say(srv, "You can't go any further.");
        return;
    }
    steps_clear(player);
    player->level = level;
    player->place_dirty = 1;
    player->dest_x = -1;
    player->dest_z = -1;
    /* Every npc and ground obj the client holds is on the old level, and it has
     * no way to know they left. A rebuild is heavy-handed but it is exactly
     * what the reference does when a player changes plane. */
    srv->rebuild_pending = 1;
    for( int i = 0; i < MOCK230_NPC_MAX; i++ )
        srv->npcs[i].tracked = 0;
    srv->tracked_count = 0;
    for( int i = 0; i < MOCK230_GROUND_MAX; i++ )
        srv->ground[i].sent = 0;
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
    struct Mock230Player* player = srv->player;

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
                say(srv, "You don't have enough inventory space.");
                return;
            }
            inv_set(player, free_slot, obj->obj_id, obj->count);
        }
        say(srv, "You pick up the %s.", mock230_objinfo(obj->obj_id)->name);
        ground_take(srv, i);
        return;
    }
    say(srv, "You can't reach that.");
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

    mock230_combat_stop_player(srv);
    mock230_world_interaction_set(srv, MOCK230_INTERACT_OBJ, op_num, -1, obj_id, tile_x,
                                  tile_z, srv->player->level, 1, 1);
    /* Onto the tile, not beside it: a pile is picked up from on top. */
    steps_clear(srv->player);
    srv->player->dest_x = tile_x;
    srv->player->dest_z = tile_z;
    steps_walk_to(srv->player, tile_x, tile_z);

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
        say(srv, "Nothing interesting happens.");
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
            say(srv, "Nothing interesting happens.");
            return;
        }
        {
            int pos = mock230_send_zone(srv, loc->x, loc->z);

            mock230_send_loc_add_change(srv, pos, loc->shape, loc->angle, loc->loc_id);
        }
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
    say(srv, "Nothing interesting happens.");
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

    mock230_combat_stop_player(srv);

    /* The footprint decides what counts as "beside it": a two-tile gate is
     * reachable from tiles a one-tile door is not. */
    slot = mock230_scene_find_loc(tile_x, tile_z, srv->player->level, loc_id);
    loc = mock230_scene_loc(slot);
    if( loc )
    {
        tile_x = loc->x;
        tile_z = loc->z;
        size_x = loc->size_x > 0 ? loc->size_x : 1;
        size_z = loc->size_z > 0 ? loc->size_z : 1;
    }

    mock230_world_interaction_set(srv, MOCK230_INTERACT_LOC, op_num, -1, loc_id, tile_x,
                                  tile_z, srv->player->level, size_x, size_z);
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
    struct Mock230Player* player = srv->player;
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

    if( strncmp(text, "talk", 4) == 0 )
    {
        /* `::talk <slot> [op]` fires [opnpc<op>] on an npc without needing a
         * right-click, so every trigger is drivable from a headless session. */
        int slot = 0;
        int op_num = 1;

        (void)sscanf(text, "talk %d %d", &slot, &op_num);
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

    if( strncmp(text, "pray", 4) == 0 )
    {
        /* `::pray <0-28>` — toggle a prayer by its index in interface 541's
         * button order (18 is Protect from Melee). The prayer tab does the
         * same thing; this is for the headless harness. */
        int prayer = -1;
        (void)sscanf(text, "pray %d", &prayer);
        if( prayer >= 0 )
        {
            /* The mock has no altars, so a session starts at prayer level 1 and
             * could not turn most of these on. Grant the level the prayer needs
             * rather than adding a second cheat to do it. */
            if( player->stat_level[MOCK230_STAT_PRAYER] < 99 )
            {
                player->stat_level[MOCK230_STAT_PRAYER] = 99;
                player->stat_boosted[MOCK230_STAT_PRAYER] = 99;
                mock230_combat_stat_mark(player, MOCK230_STAT_PRAYER);
            }
            mock230_prayer_toggle(srv, prayer);
            say(srv, "%s %s.", mock230_prayer_name(prayer),
                (player->prayer_active & (1u << prayer)) ? "on" : "off");
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
        /* `::fight <slot>` engages an npc without needing a right-click, so
         * combat is drivable from a headless session. */
        int slot = 0;

        (void)sscanf(text, "fight %d", &slot);
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
    struct Mock230Player* player = srv->player;

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
    srv->player->last_com = uid;
    srv->player->last_slot = -1;
    if( mock230_scripts_run_trigger(srv, SS_TRIGGER_IF_BUTTON, uid, -1, -1) )
        return;
    if( mock230_bank_handle_button(srv, uid, -1, -1, 1) )
        return;
    mock230_equipment_handle_button(srv, uid, -1, 1);
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
    srv->player->last_com = uid;
    srv->player->last_slot = sub;
    srv->player->last_verb = op_num;
    /* run_trigger's parameters are (trigger, type, category, npc_slot): the
     * component uid is the type, and an interface button has neither a category
     * nor an npc. `sub` and `op` reach content through last_slot and last_verb,
     * which is where a RuneScript trigger reads them. */
    if( mock230_scripts_run_trigger(srv, SS_TRIGGER_IF_BUTTON, uid, -1, -1) )
        return;
    /* (uid, sub, obj, op) — the bank needs the sub id, which is which of the
     * 1,220 dynamic children was clicked. */
    if( mock230_bank_handle_button(srv, uid, sub, -1, op_num) )
        return;
    if( mock230_equipment_handle_button(srv, uid, sub, op_num) )
        return;
    mock230_prayer_handle_button(srv, uid, op_num);
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
 * The player pressed Escape, or clicked a component whose CS2 called
 * `if_close`. The client has already torn its own copy down; the server has to
 * agree, or the next open finds the bank still marked open and sends nothing.
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
        fprintf(stderr, "mock230: <- CLOSE_MODAL\n");
    if( !srv->player->bank.open )
        return;
    if( !mock230_scripts_run_trigger(srv, SS_TRIGGER_IF_CLOSE,
                                     MOCK230_COM(mock230_ids()->iface_bankmain, 0), -1, -1) )
        mock230_bank_close(srv);
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
    srv->player->last_int = value;
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
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
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
                              MOCK230_LOOT_TICKS);
}

void
mock230_world_player_respawn(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;

    steps_clear(player);
    player->x = g_home_x;
    player->z = g_home_z;
    player->level = 0;
    player->death_tick = -1;
    player->place_dirty = 1;
    player->dest_x = -1;
    player->dest_z = -1;
    player->hitpoints = player->stat_level[MOCK230_STAT_HITPOINTS];
    mock230_combat_sync_hitpoints(player);
    /* Death drops every prayer — and with them the overhead icon, which would
     * otherwise follow the corpse back to Lumbridge. */
    mock230_prayer_clear(srv);
    /* Nothing is lost. A mock that drops your inventory on death is a mock
     * nobody uses twice, and item protection is a whole system of its own. */
    mock230_send_message(srv, "You wake up in Lumbridge.");
    maybe_rebuild(srv);
}

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
 * `weapon_category` selects which of the ten button layouts the combat tab
 * builds — the tab's own CS2 (script 7593) hides every style button when it is
 * 0, which is why an unset one shows nothing but auto-retaliate. The category
 * is a field on the weapon's cache record, so the engine reads it there rather
 * than making content restate it.
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
    struct Mock230Player* player = srv->player;
    int weapon = player->worn[MOCK230_WEAR_WEAPON].obj_id;
    int category = weapon >= 0 ? mock230_objinfo(weapon)->category : 0;
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
    return srv->player->varps[varp];
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

void
mock230_world_set_varp(
    struct Mock230Server* srv,
    int varp,
    int value)
{
    struct Mock230Player* player = srv->player;

    if( varp < 0 || varp >= MOCK230_VARP_COUNT || player->varps[varp] == value )
        return;
    player->varps[varp] = value;
    mock230_world_mark_varp(player, varp);
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
 * Bind the primary player and give it its session.
 *
 * Called by a host before mock230_world_init, because the session exists as
 * soon as the handshake does and the world does not. Splitting it out is what
 * makes "a world with no client" (the selftest) and "a world with one" the same
 * code path with a NULL in one field.
 */
void
mock230_world_attach_session(
    struct Mock230Server* srv,
    struct Mock230Session* session)
{
    srv->player = &srv->players[0];
    srv->player_count = 1;
    srv->player->world = srv;
    srv->player->pid = 0;
    srv->player->session = session;
}

/*
 * The name the session collected at login.
 *
 * Separate from mock230_world_init, and called after it, because that function
 * memsets the player — a name written before it is a name erased by it. This is
 * the whole of why `displayname` used to come back empty.
 */
void
mock230_world_set_display_name(
    struct Mock230Server* srv,
    const char* name)
{
    if( !name || !name[0] )
        return;
    snprintf(srv->player->display_name, sizeof(srv->player->display_name), "%s", name);
}

void
mock230_world_init(
    struct Mock230Server* srv,
    int zone_x,
    int zone_z)
{
    struct Mock230Player* player = srv->player;

    srv->zone_x = zone_x;
    srv->zone_z = zone_z;
    srv->rng = 0x5eed1234u;
    srv->tick = 0;

    /* Collision before anything is placed: a spawn on a blocked tile is worth
     * knowing about, and the walk helpers consult the scene from their first
     * call. */
    mock230_scene_build(mock230_world_cache_dir(), zone_x, zone_z);

    /* The bank owns a heap allocation, and the memset below is what would
     * otherwise lose the pointer to it — a re-init (which the selftest does)
     * has to release the old container before the struct is cleared. */
    mock230_bank_shutdown(srv);

    /*
     * The memset clears the *game* state, not the player's identity.
     *
     * `world`, `session` and `pid` are set by mock230_world_attach_session
     * before this runs — the session exists as soon as the handshake does, and
     * the world does not — so they are saved across the clear. Losing `session`
     * here leaves a logged-in player the encoders silently write nothing for;
     * losing `world` crashes the first encoder that reaches through it. Neither
     * is a compile error.
     */
    {
        struct Mock230Server* world = player->world;
        struct Mock230Session* session = player->session;
        int pid = player->pid;

        memset(player, 0, sizeof(*player));
        player->world = world;
        player->session = session;
        player->pid = pid;
    }
    /* On the home tile, not in the middle of the scene: the scene is 104 tiles
     * of whatever the origin zone happens to cover, and standing in the middle
     * of it puts you somewhere arbitrary. */
    player->x = g_home_x;
    player->z = g_home_z;
    player->combat_target = -1;
    player->death_tick = -1;
    player->level = 0;
    /* The memset above leaves this 0, which is a real dbtable id — so a
     * `db_findnext` with no query would iterate table 0 instead of reporting
     * that nothing was selected. Same class as `session->pending_opcode`. */
    player->db_query_table = -1;
    player->db_query_index = -1;

    /*
     * A fresh account: every skill at 1 except hitpoints, which starts at 10.
     * That is OldSchool's own starting state, and it is what makes the combat
     * formulas mean anything — a level-1 character with a bronze scimitar
     * really does take a while to kill a goblin.
     */
    for( int stat = 0; stat < MOCK230_STAT_COUNT; stat++ )
    {
        player->stat_level[stat] = 1;
        player->stat_boosted[stat] = 1;
        player->stat_xp_tenths[stat] = 0;
    }
    player->stat_level[MOCK230_STAT_HITPOINTS] = 10;
    player->stat_xp_tenths[MOCK230_STAT_HITPOINTS] = 11540; /* 1154 xp = level 10 */
    player->hitpoints = 10;
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

    /* A starting kit that covers every visible equipment slot plus a stack and
     * a spare weapon, so equipping, swapping and dragging all have something
     * to act on. Ids verified present in cache.osrs230. */
    {
        static const struct
        {
            int obj_id;
            int count;
        } kit[] = {
            { 1155, 1     }, /* Bronze full helm — claims hair + jaw */
            { 1117, 1     }, /* Bronze platebody — claims arms */
            { 1075, 1     }, /* Bronze platelegs */
            { 1189, 1     }, /* Bronze kiteshield */
            { 1321, 1     }, /* Bronze scimitar */
            { 841,  1     }, /* Shortbow — two-handed, so it evicts the shield */
            { 1731, 1     }, /* Amulet of power */
            { 1021, 1     }, /* Blue cape */
            { 1059, 1     }, /* Leather gloves */
            { 1061, 1     }, /* Leather boots */
            { 1635, 1     }, /* Gold ring */
            { 4151, 1     }, /* Abyssal whip */
            { 995,  15000 }, /* Coins — exercises the >=255 count escape */
            { 882,  50    }, /* Bronze arrow — a second stack */
        };
        for( size_t i = 0; i < sizeof(kit) / sizeof(kit[0]); i++ )
            inv_set(player, (int)i, kit[i].obj_id, kit[i].count);
    }

    /*
     * The bank. Seeded with a handful of objs rather than left empty, because
     * an empty bank and a bank that failed to transmit look identical, and
     * the withdraw side is the half that cannot be exercised from a full
     * backpack anyway.
     */
    mock230_bank_init(srv);
    {
        static const struct
        {
            int obj_id;
            int count;
        } stock[] = {
            { 995,  250000 }, /* Coins — stackable, and past the 255 escape */
            { 1511, 100    }, /* Logs — a stack of a non-stackable obj (note 1512) */
            { 314,  5000   }, /* Feather — stackable, and has NO note form */
            { 373,  60     }, /* Swordfish (note 374) */
            { 1163, 1      }, /* Rune full helm */
            { 1127, 1      }, /* Rune platebody */
            { 1079, 1      }, /* Rune platelegs */
            { 1333, 1      }, /* Rune scimitar */
        };
        for( size_t i = 0; i < sizeof(stock) / sizeof(stock[0]); i++ )
            if( i < (size_t)player->bank.size )
            {
                player->bank.slots[i].obj_id = stock[i].obj_id;
                player->bank.slots[i].count = stock[i].count;
            }
    }

    /*
     * The npc roster comes from the content tree's map squares — LostCity's
     * `==== NPC ====` sections, which are OpenRune's Lumbridge spawn list
     * transcribed by tools/spawn_import.py.
     *
     * Every spawn in the tree is created, not only the ones near the start
     * tile: the client is only ever told about npcs within 15 tiles, but the
     * player can walk, and an npc that does not exist until you approach it
     * would pop into being with full hitpoints in front of you.
     */
    memset(srv->npcs, 0, sizeof(srv->npcs));
    srv->tracked_count = 0;
    {
        int count = 0;
        const struct Mock230MapNpcSpawn* spawns = mock230_content_npc_spawns(&count);

        for( int i = 0; i < count; i++ )
            npc_spawn(srv, spawns[i].npc_id, spawns[i].x, spawns[i].z, spawns[i].level);

        if( count == 0 )
        {
            /* No content tree. One npc beside the player keeps every
             * npc-facing path — talking, fighting, NPC_INFO itself — reachable
             * rather than dead, the same way the script fallbacks do. */
            npc_spawn(srv, 3105, player->x + 2, player->z + 1, player->level);
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
mock230_world_login(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;
    const struct Mock230Ids* ids = mock230_ids();

    /* 1. The scene. Everything after this is applied by the client behind the
     *    world load, because the packet queue is serial. */
    mock230_send_rebuild_normal(srv);

    /* 2. Gameframe root + the HUD and sidebar panels mounted into it. Child
     *    ids are RuneLite InterfaceID.ToplevelOsrsStretch.*; group ids are
     *    InterfaceID.*, all verified present in cache.osrs230. type 1 =
     *    overlay. */
    mock230_send_if_opentop(srv, ids->iface_gameframe);
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
            mock230_send_if_opensub(srv, ids->iface_gameframe,
                                    frame->values[i].key & 0xffff, frame->values[i].value,
                                    1);
    }

    /*
     * 3. Unlock dragging on the inventory and equipment containers.
     *
     *    Drag bits only, deliberately. The backpack's verbs are the ObjType's —
     *    Wear, Eat, Drop — and the client builds those rows itself and sends
     *    them as OPHELD; the container names no op of its own. Arming ops 1..9
     *    here (which this used to do) told the client the paint script's stray
     *    op4 "Read" was live, and a component op that IS live replaces the
     *    ObjType rows rather than joining them, so every backpack item offered
     *    "Read" and nothing else. The worn tab's real "Remove" is armed on the
     *    slot components themselves — see mock230_equipment_arm_worn_tab.
     */
    {
        const int drag_depth_1 = 1 << 17;
        const int drag_target = 1 << 20;

        mock230_send_if_setevents(srv, ids->com_inventory_items, 0, MOCK230_INV_SLOTS - 1,
                                  drag_depth_1 | drag_target);
        mock230_send_if_setevents(srv, MOCK230_COM(ids->iface_wornitems, 0), 0,
                                  MOCK230_WORN_SLOTS - 1, drag_depth_1 | drag_target);
    }
    /*    …and on the world map orb, whose verbs are the server's alone. */
    mock230_worldmap_login(srv);

    /* 4. Player state. The pid comes first: it decides which entity in the
     *    stream the client treats as itself, and several things it drives —
     *    the npc menu's level suffix among them — are computed the moment the
     *    first PLAYER_INFO lands. */
    mock230_send_update_pid(srv, MOCK230_PLAYER_TERMINATOR);
    /* Both orb numbers, unconditionally: the per-tick flush only sends what
     * changed, and a session that starts full would otherwise never send one. */
    player->run_energy_sent = player->run_energy * 100 / MOCK230_RUN_ENERGY_MAX;
    mock230_send_run_energy(srv, player->run_energy_sent);

    /* The combat tab's varps are NOT sent here. They are ordinary varp writes
     * in [login,_] (content/scripts/player/login.rs2), declared transmit=yes in
     * player_controls.varp, and phase 10 puts them on the wire like any other
     * changed varp. A server operator changing the opening attack style should
     * not need a compiler. */
    player->run_weight_sent = player_weight_grams(player) / 1000;
    mock230_send_run_weight(srv, player->run_weight_sent);
    mock230_equipment_arm_worn_tab(srv);
    mock230_prayer_arm_buttons(srv);
    for( int stat = 0; stat < MOCK230_STAT_COUNT; stat++ )
        mock230_send_stat(srv, stat, player->stat_level[stat],
                          player->stat_xp_tenths[stat] / 10, player->stat_boosted[stat]);

    /* 5. Containers, in full. Deltas take over from the next tick. */
    mock230_send_inv_full(srv, ids->com_inventory_items, ids->inv_backpack, player->inv,
                          MOCK230_INV_SLOTS);
    mock230_send_inv_full(srv, MOCK230_COM(ids->iface_wornitems, 0), ids->inv_worn,
                          player->worn, MOCK230_WORN_SLOTS);
    player->inv_dirty = 0;
    player->worn_dirty = 0;

    /* The combat tab builds itself from these; nothing else sends them. */
    mock230_world_sync_combat_varbits(srv);

    /* Anything a script would want to say belongs in [login], which phase 7
     * runs on the next tick. These two are the no-content fallback. */
    if( !srv->scripts_ok )
    {
        mock230_send_message(srv, "Welcome to the mock 230 world.");
        mock230_send_message(srv, "Click to walk. Right-click an npc to talk.");
    }
    srv->login_pending = 1;

    /* 6. First info tick places the player and spawns the npcs. */
    mock230_send_player_info(srv);
    mock230_send_npc_info(srv);
    mock230_send_tick_end(srv);
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

/** 3. ai_spawn / ai_despawn, queued by phase 4 and by npc_add. */
static void
phase_npc_events(struct Mock230Server* srv)
{
    (void)srv;
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

/** 5. The player: delays, resumes, queues, timers, then interaction. */
static void
phase_players(struct Mock230Server* srv)
{
    /* Order matches the reference exactly, and it matters: a script resumed
     * here must not have a queue entry started on top of it in the same tick. */
    mock230_scripts_resume_player(srv);
    mock230_scripts_process_queues(srv);
    mock230_scripts_process_timers(srv);

    /* Movement is the tail of the interaction step in the reference, between
     * the pre-move and post-move interaction attempts. Combat brackets it the
     * same way: step first, then swing, so a player who arrives this tick
     * attacks on it rather than a tick later. */
    advance_player(srv);
    /* Post-move: a player who reached their target this tick acts on it now,
     * not next tick. This is the other half of the interaction model — the
     * packet handler tried once when the click arrived, and this is every tick
     * of the walk that followed. */
    mock230_world_process_interaction(srv);
    /* Energy is spent on the steps that were actually taken, so a route that
     * ran out of tiles this tick regenerates instead of draining. */
    run_energy_tick(srv, srv->player->running ? srv->player->move_count : 0);
    /* Before the swing: a prayer that ran out this tick must not protect the
     * hit that lands on it. */
    mock230_prayer_tick(srv);
    mock230_combat_player_tick(srv);
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
    if( !srv->login_pending )
        return;
    srv->login_pending = 0;
    /* The C burst already sent the scene, the gameframe and the containers —
     * that is engine work. [login] is where anything an operator would want to
     * change without recompiling belongs. */
    mock230_scripts_run_trigger(srv, SS_TRIGGER_LOGIN, -1, -1, -1);
}

/** 8. Loc/obj respawn timers and the per-zone event flush. */
static void
phase_zones(struct Mock230Server* srv)
{
    flush_ground(srv);
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
    /* Derived client state, recomputed before anything is encoded. Cheap, and
     * it only writes when a value actually moved — so a quiet tick sends
     * nothing. */
    mock230_world_sync_combat_varbits(srv);
    maybe_rebuild(srv);
}

/** 10. Everything the tick has to say, in the order the client expects it. */
static void
phase_clients_out(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;

    /* A rebuild has to reach the client before the placement that depends on
     * it, and the client's serial packet queue holds every later packet until
     * the world load finishes — so ordering here is simply "rebuild first". */
    if( srv->rebuild_pending )
    {
        mock230_send_rebuild_normal(srv);
        srv->rebuild_pending = 0;
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
            pos = mock230_send_zone(srv, loc->x, loc->z);
            mock230_send_loc_add_change(srv, pos, loc->shape, loc->angle, loc->loc_id);
        }
        /* The scene moved under the player, so the step directions computed
         * before it are meaningless. */
        player->move_count = 0;
    }

    mock230_send_player_info(srv);
    mock230_send_npc_info(srv);

    /* After the containers would have changed but before they are flushed:
     * weight is a function of what is in them, and the orb should not lag a
     * tick behind the item that changed it. */
    run_energy_flush(srv);

    /* Same argument for the bonus screen — it is a view of the worn container,
     * so it repaints on the tick that container changed rather than the next
     * time it is opened. No-op unless the screen is up. */
    if( player->worn_dirty )
        mock230_equipment_refresh_stats(srv);

    mock230_send_inv_partial(srv, mock230_ids()->com_inventory_items,
                             mock230_ids()->inv_backpack, player->inv, MOCK230_INV_SLOTS,
                             player->inv_dirty);
    mock230_send_inv_partial(srv, MOCK230_COM(mock230_ids()->iface_wornitems, 0),
                             mock230_ids()->inv_worn, player->worn, MOCK230_WORN_SLOTS,
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
            mock230_send_varp_small(srv, varp, (int)value);
        else
            mock230_send_varp_large(srv, varp, (int)value);
    }

    mock230_bank_flush(srv);

    /* Stats. UPDATE_STAT carries the boosted level as well as the base one, and
     * the boosted hitpoints level is what the health orb draws — so every hit
     * taken has to reach the client here, not only every level gained. */
    for( int stat = 0; stat < MOCK230_STAT_COUNT; stat++ )
    {
        if( (player->stat_dirty & (1u << stat)) == 0 )
            continue;
        mock230_send_stat(srv, stat, player->stat_level[stat],
                          player->stat_xp_tenths[stat] / 10,
                          player->stat_boosted[stat]);
    }

    if( srv->clear_map_flag )
        mock230_send_unset_map_flag(srv);

    mock230_send_tick_end(srv);
}

/** 11. Drop everything that described only this tick. */
static void
phase_cleanup(struct Mock230Server* srv)
{
    struct Mock230Player* player = srv->player;

    player->stat_dirty = 0;
    player->inv_dirty = 0;
    player->worn_dirty = 0;
    player->varp_changed_count = 0;
    srv->clear_map_flag = 0;

    /* Extended info describes one tick only. Clearing it here rather than
     * inside the encoder means a field set after PLAYER_INFO was written still
     * survives to the next tick instead of being silently dropped. */
    player->masks = 0;
    for( int i = 0; i < MOCK230_NPC_MAX; i++ )
    {
        srv->npcs[i].masks = 0;
        srv->npcs[i].step_dir = -1;
    }
}

void
mock230_world_tick(struct Mock230Server* srv)
{
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
    mock230_worldmap_tick(srv);
    phase_info(srv);
    phase_clients_out(srv);
    phase_cleanup(srv);
}

/* ------------------------------------------------------------------ */
/* Self-test                                                           */
/* ------------------------------------------------------------------ */

static int g_selftest_failures;

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
    struct Mock230Player* player = srv->player;

    steps_clear(player);
    player->x = tile_x;
    player->z = tile_z;
    player->level = 0;
    player->running = 0;
    player->dest_x = -1;
    player->dest_z = -1;
    player->combat_target = -1;
    player->death_tick = -1;
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
        if( srv->player->interaction.kind == MOCK230_INTERACT_NONE )
            return i;
        mock230_world_tick(srv);
    }
    return srv->player->interaction.kind == MOCK230_INTERACT_NONE ? max_ticks : -1;
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

int
mock230_world_selftest(void)
{
    struct Mock230Server srv;
    struct Mock230Player* player;
    const struct Mock230Ids* ids = mock230_ids();
    int slot;

    memset(&srv, 0, sizeof(srv));
    /* No session: a world with no client. Every mock230_send still builds its
     * payload and still reaches the capture hook, then writes nothing — which
     * is what makes every encoder assertable without a socket. */
    mock230_world_attach_session(&srv, NULL);
    mock230_seqinfo_load(MOCK230_CACHE_DIR_DEFAULT);
    mock230_world_init(&srv, 426, 408);
    player = srv.player;

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
        SELFTEST_CHECK(srv.player == &srv.players[0],
                       "the primary player is the pool's first slot");
        SELFTEST_CHECK(srv.player_count == 1, "and the world holds one player, got %d",
                       srv.player_count);
        SELFTEST_CHECK(srv.player->world == &srv,
                       "the player points back at its world after world_init");
        SELFTEST_CHECK(srv.player->pid == 0, "with pid 0, got %d", srv.player->pid);
        SELFTEST_CHECK(srv.player->session == NULL,
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

        /* Layer 1 still does its actual job: an id this world repurposed
         * resolves to the authored name, not the cache's. */
        SELFTEST_CHECK(mock230_content_symbol(MOCK230_PACK_VARP, "varp_weapon_category") == 843,
                       "the authored alias for varp 843 should resolve, got %d",
                       mock230_content_symbol(MOCK230_PACK_VARP, "varp_weapon_category"));
        SELFTEST_CHECK(mock230_content_symbol(MOCK230_PACK_VARP, "randomhitsound") == 843,
                       "and the cache's own name for it should still resolve too, got %d",
                       mock230_content_symbol(MOCK230_PACK_VARP, "randomhitsound"));

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

        /* The two content tables that replaced C arrays. The worn one is the
         * one that was never a straight run: the tab's eleven cells stand for
         * wear slots 0..5, 7, 9, 10, 12, 13. */
        SELFTEST_CHECK(mock230_content_prayer_count() == 29,
                       "the tree should declare 29 prayers, got %d",
                       mock230_content_prayer_count());
        SELFTEST_CHECK(mock230_content_prayer(0) &&
                           mock230_content_prayer(0)->button == MOCK230_COM(541, 9),
                       "the first prayer should sit on 541:9");
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
            srv.login_pending = 1;
            mock230_capture_begin(&srv, &capture);
            mock230_world_tick(&srv);
            mock230_capture_end(&srv);
            SELFTEST_CHECK(mock230_capture_find(&capture, 90 /* MESSAGE_GAME */, 0) >= 0,
                           "[login] should produce a game message");
            SELFTEST_CHECK(srv.login_pending == 0, "the login latch should be drained");

            /* [opnpc1,hans] replaces the hardcoded greeting and bumps a varp,
             * so both the script's effect and the varp flush are observable. */
            hans = selftest_find_npc(&srv, 3105);
            SELFTEST_CHECK(hans >= 0, "the roster should include Hans");
            before = player->varps[1];
            payload[0] = (uint8_t)(hans >> 8);
            payload[1] = (uint8_t)(hans & 0xff);
            mock230_capture_begin(&srv, &capture);
            mock230_world_handle(&srv, PKTOUT_NAME_OPNPC1, payload, 2);
            /* The click starts a walk; the script runs when the player gets
             * there. Hans is across the courtyard, so that is several ticks. */
            SELFTEST_CHECK(selftest_settle(&srv, 40) >= 0,
                           "the walk to Hans should complete");
            SELFTEST_CHECK(player->varps[1] == before + 1,
                           "the script should bump %%mock_greeting_count, got %d",
                           player->varps[1]);
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
            SELFTEST_CHECK(mock230_content_varp(1) == NULL,
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
                player->varps[2] = 0;
                mock230_scripts_run_script(&srv, script->id);

                SELFTEST_CHECK(player->varps[2] == 1, "the script ran up to the delay");
                SELFTEST_CHECK(player->active_script != NULL,
                               "p_delay should park the script on the player");

                for( int i = 0; i < 3; i++ )
                {
                    mock230_world_tick(&srv);
                    SELFTEST_CHECK(player->varps[2] == 1,
                                   "still delayed at tick +%d, varp is %d", i + 1,
                                   player->varps[2]);
                }
                mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[2] == 2,
                               "p_delay(3) should resume on tick +4, varp is %d",
                               player->varps[2]);
                SELFTEST_CHECK(player->active_script == NULL,
                               "a finished script should release its parking slot");
                SELFTEST_CHECK(srv.tick == start_tick + 4, "four ticks elapsed");
            }

            /* queue(script, 3, 0) runs on tick +4 for the same reason. */
            script = SSVM_ProviderGetByName(srv.scripts, "[proc,selftest_enqueue]");
            if( script )
            {
                player->varps[2] = 0;
                mock230_scripts_run_script(&srv, script->id);
                SELFTEST_CHECK(player->varps[2] == 0, "queueing should not run the script");

                for( int i = 0; i < 3; i++ )
                {
                    mock230_world_tick(&srv);
                    SELFTEST_CHECK(player->varps[2] == 0, "queue not due at tick +%d", i + 1);
                }
                mock230_world_tick(&srv);
                SELFTEST_CHECK(player->varps[2] == 7,
                               "the queued script should run on tick +4, varp is %d",
                               player->varps[2]);
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
            /* IF_SETNPCHEAD, IF_SETANIM, three IF_SETTEXTs, IF_SETHIDE(unhide),
             * IF_OPENSUB. The unhide matters as much as the mount: 162:559
             * ships hidden, so without it the dialogue is built and never
             * drawn. */
            static const int k_dialogue[] = { 95, 97, 94, 98, 6 };
            int hans = selftest_find_npc(&srv, 3105);
            uint8_t payload[2] = { (uint8_t)(hans >> 8), (uint8_t)(hans & 0xff) };
            uint8_t resume[4];
            int continue_uid = (231 << 16) | 5;

            mock230_capture_begin(&srv, &capture);
            mock230_world_handle(&srv, PKTOUT_NAME_OPNPC3, payload, 2);
            SELFTEST_CHECK(selftest_settle(&srv, 40) >= 0,
                           "the walk to Hans should complete");
            mock230_capture_end(&srv);

            SELFTEST_CHECK(mock230_capture_has_sequence(&capture, k_dialogue, 5),
                           "a dialogue should set the head, anim, text, unhide and mount");
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
            mock230_world_handle(&srv, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
            SELFTEST_CHECK(player->active_script != NULL,
                           "an unregistered button must leave the script parked");

            /* The registered one advances to page 2. */
            resume[0] = (uint8_t)(continue_uid >> 24);
            resume[1] = (uint8_t)(continue_uid >> 16);
            resume[2] = (uint8_t)(continue_uid >> 8);
            resume[3] = (uint8_t)continue_uid;

            mock230_capture_begin(&srv, &capture);
            mock230_world_handle(&srv, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
            mock230_capture_end(&srv);
            SELFTEST_CHECK(mock230_capture_find(&capture, 94 /* IF_SETTEXT */, 0) >= 0,
                           "clicking continue should draw the next page");
            SELFTEST_CHECK(player->active_script != NULL, "and park again on page 2");
            SELFTEST_CHECK(player->last_com == continue_uid,
                           "last_com should name the button that resumed it");

            /* Page 2 is the last one Hans's "Age" reply has, so the next click
             * runs off the end of the script and into its if_close. */
            mock230_capture_begin(&srv, &capture);
            mock230_world_handle(&srv, PKTOUT_NAME_RESUME_PAUSEBUTTON, resume, 4);
            mock230_capture_end(&srv);
            SELFTEST_CHECK(player->active_script == NULL,
                           "the script should finish after the last page");
            SELFTEST_CHECK(mock230_capture_find(&capture, 36 /* IF_CLOSESUB */, 0) >= 0,
                           "if_close should close the dialogue");
            SELFTEST_CHECK(player->resume_button_count == 0,
                           "and drop its resume buttons");

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
            mock230_world_handle(&srv, PKTOUT_NAME_OPHELD1, held, 8);

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
            mock230_world_handle(&srv, PKTOUT_NAME_OPHELD1, held, 8);
            SELFTEST_CHECK(player->hitpoints == 7,
                           "cooked meat should heal 3, got %d", player->hitpoints);
            SELFTEST_CHECK(player->inv[0].obj_id == -1, "and be eaten");

            inv_set(player, 0, meat, 1);
            player->hitpoints = 10;
            player->stat_boosted[MOCK230_STAT_HITPOINTS] = 10;
            mock230_world_handle(&srv, PKTOUT_NAME_OPHELD1, held, 8);
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
                mock230_world_handle(&srv, PKTOUT_NAME_OPHELD2, held, 8);
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
                    mock230_world_handle(&srv, PKTOUT_NAME_OPNPC3, npc_payload, 2);
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

            /* Combat animations are resolved from the cache's sequence names,
             * so a rename upstream shows up here rather than as an npc that
             * silently stops animating. */
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

            /* Stand next to it so the fight starts immediately. */
            player->x = npc->x + 1;
            player->z = npc->z;
            steps_clear(player);
            player->hitpoints = player->max_hitpoints;

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

            /* Corpse despawns, then respawns at its spawn tile at full health. */
            for( int i = 0; i < MOCK230_DEATH_TICKS + 2 && npc->active; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(!npc->active, "the corpse should despawn");

            for( int i = 0; i < MOCK230_RESPAWN_TICKS + 4 && !npc->active; i++ )
                mock230_world_tick(&srv);
            SELFTEST_CHECK(npc->active, "the goblin should respawn");
            SELFTEST_CHECK(npc->hitpoints == start_hp,
                           "at full health, got %d of %d", npc->hitpoints, start_hp);
            SELFTEST_CHECK(npc->x == npc->spawn_x && npc->z == npc->spawn_z,
                           "at its spawn tile");
            /* Respawn clears `tracked` so the next NPC_INFO adds it as a new
             * entity rather than as a move of one the client already has —
             * which by now it has done, so the observable end state is that the
             * client has been told about it again. */
            SELFTEST_CHECK(npc->tracked,
                           "and re-added to the client's npc list");
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

            /* MOVE_GAMECLICK: p1 ctrl, p2 start x, p2 start z, then waypoints. */
            player->masks = 0;
            move[0] = 0;
            move[1] = (uint8_t)((player->x + 3) >> 8);
            move[2] = (uint8_t)(player->x + 3);
            move[3] = (uint8_t)(player->z >> 8);
            move[4] = (uint8_t)player->z;
            mock230_world_handle(&srv, PKTOUT_NAME_MOVE_GAMECLICK, move, 5);

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
        mock230_world_login(&srv);
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

            srv.login_pending = 1;
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
            SELFTEST_CHECK(mock230_content_varp(1) == NULL,
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
        SELFTEST_CHECK(scimitar->damagetype == MOCK230_DAMAGE_SLASH,
                       "a scimitar should derive as a slash weapon, got %d",
                       scimitar->damagetype);
        SELFTEST_CHECK(guard->bonus[MOCK230_PARAM_SLASHDEFENCE] == 25,
                       "the guard's slashdefence should be +25, got %d",
                       guard->bonus[MOCK230_PARAM_SLASHDEFENCE]);

        /* An obj with no params at all must stay distinguishable from one whose
         * bonuses are genuinely zero: the first is unarmed, the second is a bad
         * weapon, and they time differently. */
        SELFTEST_CHECK(!mock230_objinfo(995)->has_params,
                       "coins should carry no combat params");

        /*
         * The splat ids, which were backwards.
         *
         * Config group 32 gives hitsplat 0 sprite 2270 (blue, the zero splat)
         * and hitsplat 1 sprite 3521 (red, damage). The mock had them the other
         * way round, so every hit drew a block and every miss drew damage —
         * nothing failed, it just looked wrong. Asserting the ids here is the
         * cheap half; content/pack/hitsplat.pack records how they were
         * identified, which is the half that stops it recurring.
         */
        SELFTEST_CHECK(mock230_content_symbol(MOCK230_PACK_HITSPLAT, "hitsplat_damage") == 1,
                       "hitsplat_damage should be 1 (sprite 3521, red)");
        SELFTEST_CHECK(mock230_content_symbol(MOCK230_PACK_HITSPLAT, "hitsplat_block") == 0,
                       "hitsplat_block should be 0 (sprite 2270, blue)");

        /* Attackability is the cache's own op list, which is what the client's
         * right-click menu reads. */
        SELFTEST_CHECK(mock230_combat_attackable(3028), "a goblin is attackable");
        SELFTEST_CHECK(!mock230_combat_attackable(3105), "Hans is not");
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
                              MOCK230_LOOT_TICKS);
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
            mock230_world_handle(&srv, PKTOUT_NAME_OPOBJ1, payload, 6);
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
            mock230_world_handle(&srv, PKTOUT_NAME_OPHELD5, payload, (int)rsab_len(&drop));
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
        mock230_world_obj_add(&srv, 526 /* bones */, 1, obj_x, obj_z, 0, MOCK230_LOOT_TICKS);

        {
            uint8_t payload[6] = { (uint8_t)(obj_x >> 8), (uint8_t)obj_x,
                                   (uint8_t)(obj_z >> 8), (uint8_t)obj_z,
                                   (uint8_t)(526 >> 8),   (uint8_t)526 };

            mock230_world_handle(&srv, PKTOUT_NAME_OPOBJ1, payload, 6);
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
        mock230_world_obj_add(&srv, 526, 1, obj_x, obj_z, 0, MOCK230_LOOT_TICKS);
        {
            uint8_t payload[6] = { (uint8_t)(obj_x >> 8), (uint8_t)obj_x,
                                   (uint8_t)(obj_z >> 8), (uint8_t)obj_z,
                                   (uint8_t)(526 >> 8),   (uint8_t)526 };
            uint8_t move[5];
            struct RSAreaBuf walk;

            mock230_world_handle(&srv, PKTOUT_NAME_OPOBJ1, payload, 6);
            SELFTEST_CHECK(player->interaction.kind == MOCK230_INTERACT_OBJ,
                           "the interaction is armed");

            rsab_wrap(&walk, move, sizeof(move));
            rsab_p1(&walk, 0);
            rsab_p2(&walk, 3224);
            rsab_p2(&walk, 3218);
            mock230_world_handle(&srv, PKTOUT_NAME_MOVE_GAMECLICK, move, (int)rsab_len(&walk));
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
            mock230_world_handle(&srv, PKTOUT_NAME_OPLOC1, payload, 6);
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
            mock230_world_handle(&srv, PKTOUT_NAME_OPLOC1, payload, 6);
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
        /* Indices into prayers.prayer, which is also the order the book lists
         * them in. Named through the file rather than counted here so a prayer
         * inserted into the content does not silently move what is asserted. */
        const int protect_melee = mock230_prayer_index("prayer_protect_from_melee");
        const int rock_skin = mock230_prayer_index("prayer_rock_skin");
        const int steel_skin = mock230_prayer_index("prayer_steel_skin");
        const int overhead_melee = mock230_prayer_headicon("headicon_prayer_protectfrommelee");

        SELFTEST_CHECK(protect_melee >= 0 && rock_skin >= 0 && steel_skin >= 0 &&
                           overhead_melee >= 0,
                       "the prayer content should declare the three this asserts on");

        mock230_prayer_clear(&srv);
        player->stat_level[MOCK230_STAT_PRAYER] = 1;
        player->stat_boosted[MOCK230_STAT_PRAYER] = 1;

        /* The level gate reads the base level, so a level-1 character is
         * refused Protect from Melee no matter how many points they have. */
        mock230_prayer_toggle(&srv, protect_melee);
        SELFTEST_CHECK(player->prayer_active == 0, "a level-1 character cannot protect");

        player->stat_level[MOCK230_STAT_PRAYER] = 99;
        player->stat_boosted[MOCK230_STAT_PRAYER] = 99;
        mock230_prayer_toggle(&srv, protect_melee);
        SELFTEST_CHECK(mock230_prayer_protecting(player, overhead_melee),
                       "protect from melee is up");
        SELFTEST_CHECK(mock230_prayer_headicon_mask(player) == (1 << overhead_melee),
                       "and is the only overhead icon");
        SELFTEST_CHECK((player->masks & MOCK230_PMASK_APPEARANCE) != 0,
                       "turning a prayer on re-sends the appearance");

        /* Same group: the second defence prayer replaces the first rather than
         * stacking with it. */
        mock230_prayer_toggle(&srv, rock_skin);
        mock230_prayer_toggle(&srv, steel_skin);
        SELFTEST_CHECK((player->prayer_active & (1u << rock_skin)) == 0,
                       "steel skin replaces rock skin");
        SELFTEST_CHECK((player->prayer_active & (1u << steel_skin)) != 0, "steel skin is up");
        SELFTEST_CHECK(mock230_prayer_protecting(player, overhead_melee),
                       "and left the overhead alone — a different group");

        /* Drain: steel skin (12) + protect from melee (12) is 24 a tick against
         * a resistance of 60, so a point goes every third tick. */
        mock230_prayer_clear(&srv);
        mock230_prayer_toggle(&srv, protect_melee);
        player->stat_boosted[MOCK230_STAT_PRAYER] = 99;
        player->prayer_drain_acc = 0;
        mock230_prayer_tick(&srv);
        SELFTEST_CHECK(player->stat_boosted[MOCK230_STAT_PRAYER] == 99,
                       "12 units is not yet a point");
        mock230_prayer_tick(&srv);
        SELFTEST_CHECK(player->stat_boosted[MOCK230_STAT_PRAYER] == 99, "nor is 24");
        for( int i = 0; i < 3; i++ )
            mock230_prayer_tick(&srv);
        SELFTEST_CHECK(player->stat_boosted[MOCK230_STAT_PRAYER] == 98,
                       "60 units is one prayer point, got %d",
                       player->stat_boosted[MOCK230_STAT_PRAYER]);

        /* Running out drops everything, including the overhead. */
        player->stat_boosted[MOCK230_STAT_PRAYER] = 1;
        for( int i = 0; i < 10; i++ )
            mock230_prayer_tick(&srv);
        SELFTEST_CHECK(player->prayer_active == 0, "running out clears every prayer");
        SELFTEST_CHECK(mock230_prayer_headicon_mask(player) == 0, "and the overhead icon");

        player->stat_level[MOCK230_STAT_PRAYER] = 1;
        player->stat_boosted[MOCK230_STAT_PRAYER] = 1;
    }

    fprintf(stderr, "mock230 selftest: run energy\n");
    {
        int energy_before;
        int weight;

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
            int expect = 2 * (67 + (67 * weight_kg) / 64);
            SELFTEST_CHECK(spent == expect, "two running tiles cost %d, got %d", expect, spent);
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
         * tools/kronos_item_import.py), so what is worth pinning here is that
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
        mock230_world_handle(&srv, PKTOUT_NAME_INV_BUTTOND, payload, (int)rsab_len(&buf));

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
                SELFTEST_CHECK(npc->x - npc->spawn_x <= npc->wander_radius &&
                                   npc->spawn_x - npc->x <= npc->wander_radius,
                               "npc %d stayed inside its wander radius", i);
            }
            srv.tick++;
        }
        SELFTEST_CHECK(moved > 0, "at least one npc roamed over 200 ticks");
        {
            /* Hans is `moverestrict=nomove` in the content tree, which is what
             * a zero wander radius has to come out as. Found by type rather
             * than by slot: slot order follows the map files now. */
            int hans = selftest_find_npc(&srv, 3105);

            SELFTEST_CHECK(hans >= 0, "the roster should include Hans");
            if( hans >= 0 )
                SELFTEST_CHECK(srv.npcs[hans].wander_radius == 0 &&
                                   srv.npcs[hans].x == srv.npcs[hans].spawn_x,
                               "a nomove npc never moves");
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

        mock230_world_init(&srv, 402, 402);
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

        mock230_world_init(&srv, 402, 402);
        coins_before = mock230_bank_count(&srv, 995);
        SELFTEST_CHECK(coins_before > 0, "the starting bank should hold coins");

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
    }

    fprintf(stderr, "mock230 selftest: bank op ladder\n");
    {
        struct Mock230Bank* bank = &player->bank;

        mock230_world_init(&srv, 402, 402);

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

        mock230_world_init(&srv, 402, 402);
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

    if( g_selftest_failures )
        fprintf(stderr, "mock230 selftest: %d failure(s)\n", g_selftest_failures);
    else
        fprintf(stderr, "mock230 selftest: all checks passed\n");
    return g_selftest_failures;
}
