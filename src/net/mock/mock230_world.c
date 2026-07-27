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

#include "net/rev/pktnames.h"

#include <rsareabuf.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

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
    player->appearance_dirty = 1;
}

static int
inv_first_free(const struct Mock230Player* player)
{
    for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
        if( player->inv[i].obj_id < 0 )
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
    struct Mock230Player* player = &srv->player;
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
    struct Mock230Player* player = &srv->player;
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
 * apart, so each pair is interpolated the way a RuneScape actor walks:
 * diagonally while both axes still differ, then straight.
 */
static void
steps_walk_to(
    struct Mock230Player* player,
    int x,
    int z)
{
    int cur_x = player->step_count > 0 ? player->steps[player->step_count - 1].x : player->x;
    int cur_z = player->step_count > 0 ? player->steps[player->step_count - 1].z : player->z;

    while( cur_x != x || cur_z != z )
    {
        cur_x += sign_of(x - cur_x);
        cur_z += sign_of(z - cur_z);
        steps_push(player, cur_x, cur_z);
        if( player->step_count >= MOCK230_STEP_MAX )
            return;
    }
}

/* Consume up to `max_tiles` queued steps, recording the direction of each so
 * PLAYER_INFO can spell them out. */
static void
advance_player(struct Mock230Server* srv)
{
    struct Mock230Player* player = &srv->player;
    int max_tiles = player->running ? 2 : 1;

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
    struct Mock230Player* player = &srv->player;
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
    int wander_radius)
{
    /* The wire field is 11 bits. A wider id would not fail loudly — it would
     * truncate to a different, probably valid, npc. */
    if( type < 0 || type > 2047 )
    {
        fprintf(stderr, "mock230: npc type %d exceeds the 11-bit wire field; not spawned\n", type);
        return;
    }

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
        npc->spawn_x = x;
        npc->spawn_z = z;
        npc->wander_radius = wander_radius;
        npc->next_roam_tick = srv->tick + random_range(srv, 5, 30);
        npc->step_dir = -1;
        npc->face_entity = -1;
        return;
    }
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
        npc->step_dir = -1;
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
        npc->x = step_x;
        npc->z = step_z;
        npc->step_dir = dir;
    }
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
    struct Mock230Player* player = &srv->player;
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

    player->running = ctrl != 0;
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

/* OPHELD<n>: p2 objId, p2 slot, p2 componentId. `n` is the 1-based index into
 * the item's inventory ops, so the server can tell "Wear" from "Drop" instead
 * of guessing from the item type. */
static void
handle_opheld(
    struct Mock230Server* srv,
    int op_num,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = &srv->player;
    struct RSAreaBuf buf;
    int obj_id;
    int slot;
    int component;
    const struct Mock230ObjInfo* info;
    const char* verb;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    obj_id = rsab_g2(&buf);
    slot = rsab_g2(&buf);
    component = rsab_g2(&buf);
    if( !rsab_ok(&buf) )
        return;

    info = mock230_objinfo(obj_id);
    verb = (op_num >= 1 && op_num <= 5) ? info->if_ops[op_num - 1] : NULL;
    if( srv->verbose )
        fprintf(
            stderr,
            "mock230: <- OPHELD%d obj=%d (%s) slot=%d com=%d verb=%s\n",
            op_num,
            obj_id,
            info->name,
            slot,
            component,
            verb ? verb : "-");

    /* The equipment tab's own component sends its ops through the same packet,
     * so a click there means "take it off" rather than "put it on". */
    if( (component >> 16) == MOCK230_WORN_IFACE )
    {
        unequip_slot(srv, slot);
        return;
    }

    if( slot < 0 || slot >= MOCK230_INV_SLOTS || player->inv[slot].obj_id != obj_id )
        return;

    if( verb && (strcmp(verb, "Wear") == 0 || strcmp(verb, "Wield") == 0) )
    {
        equip_from_slot(srv, slot);
        return;
    }
    if( verb && strcmp(verb, "Drop") == 0 )
    {
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

/* INV_BUTTOND: p2 componentId, p2 fromSlot, p2 toSlot, p1 mode. The client has
 * already applied the swap locally, so this confirms it; a server that
 * disagreed would send a partial update putting both slots back. */
static void
handle_inv_buttond(
    struct Mock230Server* srv,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = &srv->player;
    struct RSAreaBuf buf;
    int component;
    int from_slot;
    int to_slot;
    struct Mock230Item swap;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    component = rsab_g2(&buf);
    from_slot = rsab_g2(&buf);
    to_slot = rsab_g2(&buf);
    (void)rsab_g1(&buf); /* mode */
    if( !rsab_ok(&buf) )
        return;

    if( srv->verbose )
        fprintf(
            stderr,
            "mock230: <- INV_BUTTOND com=%d %d -> %d\n",
            component,
            from_slot,
            to_slot);

    if( from_slot < 0 || from_slot >= MOCK230_INV_SLOTS || to_slot < 0 ||
        to_slot >= MOCK230_INV_SLOTS || from_slot == to_slot )
        return;

    swap = player->inv[from_slot];
    inv_set(player, from_slot, player->inv[to_slot].obj_id, player->inv[to_slot].count);
    inv_set(player, to_slot, swap.obj_id, swap.count);
}

/* OPNPC<n>: p2 npc slot. Walk next to it and have it acknowledge you. */
static void
handle_opnpc(
    struct Mock230Server* srv,
    int op_num,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = &srv->player;
    struct RSAreaBuf buf;
    int slot;
    struct Mock230Npc* npc;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    slot = rsab_g2(&buf);
    if( !rsab_ok(&buf) || slot < 0 || slot >= MOCK230_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active )
        return;

    if( srv->verbose )
        fprintf(stderr, "mock230: <- OPNPC%d slot=%d type=%d\n", op_num, slot, npc->type);

    /* Stop next to the npc rather than on top of it. */
    steps_clear(player);
    player->dest_x = npc->x - sign_of(npc->x - player->x);
    player->dest_z = npc->z - sign_of(npc->z - player->z);
    steps_walk_to(player, player->dest_x, player->dest_z);

    npc->face_entity = MOCK230_PLAYER_TERMINATOR; /* the local player */
    npc->face_entity_dirty = 1;
    npc->say_dirty = 1;
    snprintf(npc->say, sizeof(npc->say), "Hello there, adventurer!");
    /* Idle roaming resumes only after the greeting has had time to show. */
    npc->next_roam_tick = srv->tick + 8;
}

/* OPLOC / OPOBJ: p2 x, p2 z, p2 id. The mock has no loc or ground-item state,
 * so it walks the player over and says something. */
static void
handle_op_at_tile(
    struct Mock230Server* srv,
    const char* what,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = &srv->player;
    struct RSAreaBuf buf;
    int tile_x;
    int tile_z;
    int id;

    rsab_wrap(&buf, (void*)payload, (size_t)len);
    tile_x = rsab_g2(&buf);
    tile_z = rsab_g2(&buf);
    id = rsab_g2(&buf);
    if( !rsab_ok(&buf) )
        return;

    steps_clear(player);
    player->dest_x = tile_x;
    player->dest_z = tile_z;
    steps_walk_to(player, tile_x, tile_z);
    say(srv, "Nothing interesting happens. (%s %d at %d,%d)", what, id, tile_x, tile_z);
}

/* ::commands, so a session can be steered without a UI. */
static void
handle_cheat(
    struct Mock230Server* srv,
    const uint8_t* payload,
    int len)
{
    struct Mock230Player* player = &srv->player;
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
        steps_clear(player);
        player->x = tile_x;
        player->z = tile_z;
        player->place_dirty = 1;
        maybe_rebuild(srv);
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
mock230_world_handle(
    struct Mock230Server* srv,
    int name,
    const uint8_t* payload,
    int len)
{
    switch( name )
    {
    case PKTOUT_NAME_MOVE_GAMECLICK:
    case PKTOUT_NAME_MOVE_OPCLICK:
        handle_move(srv, payload, len, 0);
        break;
    case PKTOUT_NAME_MOVE_MINIMAPCLICK:
        handle_move(srv, payload, len, 14);
        break;

    case PKTOUT_NAME_OPHELD1:
    case PKTOUT_NAME_OPHELD2:
    case PKTOUT_NAME_OPHELD3:
    case PKTOUT_NAME_OPHELD4:
    case PKTOUT_NAME_OPHELD5:
        handle_opheld(srv, name - PKTOUT_NAME_OPHELD1 + 1, payload, len);
        break;

    /* INV_BUTTON<n> carries the same (obj, slot, component) triple as OPHELD.
     * Which of the two an inventory row produces depends on whether the menu
     * entry came from the objtype's ops or from the component's, and at rev 230
     * the gameframe's CS2 inventory script routes them through the component —
     * so both have to mean the same thing here or nothing is ever equipped. */
    case PKTOUT_NAME_INV_BUTTON1:
    case PKTOUT_NAME_INV_BUTTON2:
    case PKTOUT_NAME_INV_BUTTON3:
    case PKTOUT_NAME_INV_BUTTON4:
    case PKTOUT_NAME_INV_BUTTON5:
        handle_opheld(srv, name - PKTOUT_NAME_INV_BUTTON1 + 1, payload, len);
        break;

    case PKTOUT_NAME_INV_BUTTOND:
        handle_inv_buttond(srv, payload, len);
        break;

    case PKTOUT_NAME_OPNPC1:
    case PKTOUT_NAME_OPNPC2:
    case PKTOUT_NAME_OPNPC3:
    case PKTOUT_NAME_OPNPC4:
    case PKTOUT_NAME_OPNPC5:
        handle_opnpc(srv, name - PKTOUT_NAME_OPNPC1 + 1, payload, len);
        break;

    case PKTOUT_NAME_OPLOC1:
    case PKTOUT_NAME_OPLOC2:
    case PKTOUT_NAME_OPLOC3:
    case PKTOUT_NAME_OPLOC4:
    case PKTOUT_NAME_OPLOC5:
        handle_op_at_tile(srv, "loc", payload, len);
        break;

    case PKTOUT_NAME_OPOBJ1:
    case PKTOUT_NAME_OPOBJ2:
    case PKTOUT_NAME_OPOBJ3:
    case PKTOUT_NAME_OPOBJ4:
    case PKTOUT_NAME_OPOBJ5:
        handle_op_at_tile(srv, "obj", payload, len);
        break;

    case PKTOUT_NAME_CLIENT_CHEAT:
        handle_cheat(srv, payload, len);
        break;

    case PKTOUT_NAME_IF_BUTTON:
        /* Sidebar tabs are switched entirely client-side at rev 230 (the CS2
         * gameframe swaps the mounted panel on a varc), so a button click here
         * needs no server state — it is logged and dropped. */
        if( srv->verbose )
            fprintf(stderr, "mock230: <- IF_BUTTON (%d bytes)\n", len);
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Login + tick                                                        */
/* ------------------------------------------------------------------ */

void
mock230_world_init(
    struct Mock230Server* srv,
    int zone_x,
    int zone_z)
{
    struct Mock230Player* player = &srv->player;

    srv->zone_x = zone_x;
    srv->zone_z = zone_z;
    srv->rng = 0x5eed1234u;
    srv->tick = 0;

    memset(player, 0, sizeof(*player));
    /* Start in the middle of the scene. */
    player->x = mock230_scene_origin(zone_x) + MOCK230_SCENE_TILES / 2;
    player->z = mock230_scene_origin(zone_z) + MOCK230_SCENE_TILES / 2;
    player->level = 0;
    player->dest_x = -1;
    player->dest_z = -1;
    player->place_dirty = 1;
    player->appearance_dirty = 1;

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

    /* NPC roster around the spawn: humans that roam plus a stationary greeter.
     *
     * EVERY ID MUST BE UNDER 2048. The classic NPC_INFO stream carries the npc
     * type in an 11-bit field, so 3106 ("Man") silently truncates to 1058 and
     * the client spawns whatever that happens to be — or nothing, if the
     * truncated id is empty. npc_spawn refuses anything wider. All ids below
     * are verified present in cache.osrs230. */
    memset(srv->npcs, 0, sizeof(srv->npcs));
    srv->tracked_count = 0;
    {
        static const struct
        {
            int type;
            int dx;
            int dz;
            int radius;
        } roster[] = {
            { 385, 2,  1,  0 }, /* Man — stands still, the greeter */
            { 397, -3, 2,  4 }, /* Guard */
            { 398, 4,  -2, 4 }, /* Guard */
            { 687, -2, -4, 3 }, /* Bartender */
            { 766, 1,  5,  0 }, /* Banker */
            { 305, -5, -1, 4 }, /* Jennifer */
            { 542, 6,  3,  4 }, /* Monk */
            { 655, -6, 4,  5 }, /* Goblin */
            { 731, 5,  -6, 6 }, /* Sheep */
            { 1020, -4, -7, 6 }, /* Rat */
        };
        for( size_t i = 0; i < sizeof(roster) / sizeof(roster[0]); i++ )
            npc_spawn(
                srv,
                roster[i].type,
                player->x + roster[i].dx,
                player->z + roster[i].dz,
                roster[i].radius);
    }
}

void
mock230_world_login(struct Mock230Server* srv)
{
    struct Mock230Player* player = &srv->player;

    /* 1. The scene. Everything after this is applied by the client behind the
     *    world load, because the packet queue is serial. */
    mock230_send_rebuild_normal(srv);

    /* 2. Gameframe root + the HUD and sidebar panels mounted into it. Child
     *    ids are RuneLite InterfaceID.ToplevelOsrsStretch.*; group ids are
     *    InterfaceID.*, all verified present in cache.osrs230. type 1 =
     *    overlay. */
    mock230_send_if_opentop(srv, MOCK230_ROOT_IFACE);
    {
        static const struct
        {
            int slot;
            int group;
        } opensubs[] = {
            /* HUD overlays */
            { 96, 162 }, /* chatbox */
            { 6,  651 }, /* buff bar */
            { 5,  708 }, /* stat boosts */
            { 93, 163 }, /* private chat */
            { 2,  303 }, /* hp bar */
            { 3,  90  }, /* pvp icons */
            { 33, 160 }, /* orbs */
            { 9,  122 }, /* xp drops */
            { 35, 728 }, /* popout */
            { 36, 896 }, /* worldhop */
            /* Sidebar tabs, side0..side13 */
            { 76, 593 }, /* combat */
            { 77, 320 }, /* stats */
            { 78, 629 }, /* journal */
            { 79, 149 }, /* inventory */
            { 80, 387 }, /* worn equipment */
            { 81, 541 }, /* prayer */
            { 82, 218 }, /* spellbook */
            { 83, 707 }, /* channels */
            { 84, 109 }, /* account */
            { 85, 429 }, /* friends */
            { 86, 182 }, /* logout */
            { 87, 116 }, /* settings */
            { 88, 216 }, /* emotes */
            { 89, 239 }, /* music */
        };
        for( size_t i = 0; i < sizeof(opensubs) / sizeof(opensubs[0]); i++ )
            mock230_send_if_opensub(
                srv, MOCK230_ROOT_IFACE, opensubs[i].slot, opensubs[i].group, 1);
    }

    /* 3. Unlock clicks on the inventory and equipment slot grids. Without this
     *    the components render but swallow every click, so nothing can be
     *    equipped or dragged. */
    mock230_send_if_setevents(srv, (MOCK230_INV_IFACE << 16) | 0, 0, MOCK230_INV_SLOTS, 0x3fe);
    mock230_send_if_setevents(srv, (MOCK230_WORN_IFACE << 16) | 0, 0, MOCK230_WORN_SLOTS, 0x3fe);

    /* 4. Player state. */
    mock230_send_run_energy(srv, 100);
    mock230_send_run_weight(srv, 0);
    for( int stat = 0; stat < 23; stat++ )
        mock230_send_stat(srv, stat, 1, 121);

    /* 5. Containers, in full. Deltas take over from the next tick. */
    mock230_send_inv_full(
        srv,
        (MOCK230_INV_IFACE << 16) | 0,
        MOCK230_INV_BACKPACK,
        player->inv,
        MOCK230_INV_SLOTS);
    mock230_send_inv_full(
        srv,
        (MOCK230_WORN_IFACE << 16) | 0,
        MOCK230_INV_WORN,
        player->worn,
        MOCK230_WORN_SLOTS);
    player->inv_dirty = 0;
    player->worn_dirty = 0;

    mock230_send_message(srv, "Welcome to the mock 230 world.");
    mock230_send_message(srv, "Click to walk. Right-click an npc to talk.");

    /* 6. First info tick places the player and spawns the npcs. */
    mock230_send_player_info(srv);
    mock230_send_npc_info(srv);
    mock230_send_tick_end(srv);
}

void
mock230_world_tick(struct Mock230Server* srv)
{
    struct Mock230Player* player = &srv->player;

    srv->tick++;

    advance_player(srv);
    advance_npcs(srv);
    maybe_rebuild(srv);

    /* A rebuild has to reach the client before the placement that depends on
     * it, and the client's serial packet queue holds every later packet until
     * the world load finishes — so ordering here is simply "rebuild first". */
    if( srv->rebuild_pending )
    {
        mock230_send_rebuild_normal(srv);
        srv->rebuild_pending = 0;
        /* The scene moved under the player, so the step directions computed
         * before it are meaningless. */
        player->move_count = 0;
    }

    mock230_send_player_info(srv);
    mock230_send_npc_info(srv);

    mock230_send_inv_partial(
        srv,
        (MOCK230_INV_IFACE << 16) | 0,
        MOCK230_INV_BACKPACK,
        player->inv,
        MOCK230_INV_SLOTS,
        player->inv_dirty);
    mock230_send_inv_partial(
        srv,
        (MOCK230_WORN_IFACE << 16) | 0,
        MOCK230_INV_WORN,
        player->worn,
        MOCK230_WORN_SLOTS,
        player->worn_dirty);
    player->inv_dirty = 0;
    player->worn_dirty = 0;

    if( srv->clear_map_flag )
    {
        mock230_send_unset_map_flag(srv);
        srv->clear_map_flag = 0;
    }

    mock230_send_tick_end(srv);
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
    int slot;

    memset(&srv, 0, sizeof(srv));
    srv.fd = -1; /* every mock230_send becomes a no-op */
    mock230_world_init(&srv, 426, 408);
    player = &srv.player;

    fprintf(stderr, "mock230 selftest: movement\n");
    {
        int start_x = player->x;
        int start_z = player->z;

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

        player->running = 1;
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
        SELFTEST_CHECK(player->appearance_dirty == 1, "equipping re-sends the appearance");
        SELFTEST_CHECK((player->worn_dirty & (1u << MOCK230_WEAR_HEAD)) != 0,
                       "the worn slot is marked for the next partial update");

        /* Taking it off returns it to the first free backpack slot. */
        unequip_slot(&srv, MOCK230_WEAR_HEAD);
        SELFTEST_CHECK(player->worn[MOCK230_WEAR_HEAD].obj_id == -1, "head slot is empty again");
        SELFTEST_CHECK(selftest_find(player, 1155) >= 0, "helm is back in the backpack");
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
        uint8_t payload[7];
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
        rsab_p2(&buf, (MOCK230_INV_IFACE << 16) | 0);
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
        SELFTEST_CHECK(srv.npcs[0].wander_radius == 0 && srv.npcs[0].x == srv.npcs[0].spawn_x,
                       "a zero-radius npc never moves");
    }

    if( g_selftest_failures )
        fprintf(stderr, "mock230 selftest: %d failure(s)\n", g_selftest_failures);
    else
        fprintf(stderr, "mock230 selftest: all checks passed\n");
    return g_selftest_failures;
}
