/*
 * Every server->client packet the mock sends.
 *
 * All encoding goes through 3rd/rsareabuf, which is why the byte-order variants
 * read as intent (`rsab_p2_alt2`) rather than as two hand-spelled `p1` calls,
 * and why the info bitstreams can be written with `rsab_pbit` instead of a
 * bespoke bit packer. Buffers come from a per-packet arena that is reset on
 * every send, so nothing here allocates.
 *
 * Two of these are bitstreams rather than field lists — PLAYER_INFO and
 * NPC_INFO — and they are the packets worth reading carefully. Both end their
 * bit section with a terminator whose only job is to stop the client's decode
 * loop before it walks into the byte-aligned extended-info section that
 * follows. Skipping the terminator is the classic way to make a stream decode
 * as garbage entities.
 */
#include "mock230.h"

#include <rsareabuf.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* rev-230 server->client opcodes (RSProt GameServerProt). */
enum
{
    OP_SET_MAP_FLAG = 2,
    OP_IF_OPENSUB = 6,
    OP_UPDATE_INV_FULL = 10,
    OP_PLAYER_INFO = 23,
    OP_UPDATE_RUNWEIGHT = 27,
    OP_VARP_SMALL = 35,
    OP_UPDATE_INV_PARTIAL = 37,
    OP_IF_SETEVENTS = 47,
    OP_IF_OPENTOP = 60,
    OP_REBUILD_NORMAL = 68,
    OP_UPDATE_RUNENERGY = 77,
    OP_MESSAGE_GAME = 90,
    OP_NPC_INFO = 104,
    OP_SERVER_TICK_END = 108,
    OP_UPDATE_STAT = 114,
};

/* One packet's worth of scratch. Reset per send; sized for the largest packet
 * the mock produces (a full REBUILD_NORMAL key block). */
static uint8_t g_arena_memory[64 * 1024];
static struct RSArena g_arena;
static int g_arena_ready;

static void
open_packet(
    struct RSAreaBuf* buf,
    size_t capacity)
{
    if( !g_arena_ready )
    {
        rsab_arena_init(&g_arena, g_arena_memory, sizeof(g_arena_memory));
        g_arena_ready = 1;
    }
    rsab_arena_reset(&g_arena);
    if( !rsab_open_arena(buf, &g_arena, capacity) )
        fprintf(stderr, "mock230: packet arena exhausted (%zu bytes)\n", capacity);
}

static const char*
opcode_name(int op)
{
    switch( op )
    {
    case OP_SET_MAP_FLAG:
        return "SET_MAP_FLAG";
    case OP_IF_OPENSUB:
        return "IF_OPENSUB";
    case OP_UPDATE_INV_FULL:
        return "UPDATE_INV_FULL";
    case OP_PLAYER_INFO:
        return "PLAYER_INFO";
    case OP_UPDATE_RUNWEIGHT:
        return "UPDATE_RUNWEIGHT";
    case OP_VARP_SMALL:
        return "VARP_SMALL";
    case OP_UPDATE_INV_PARTIAL:
        return "UPDATE_INV_PARTIAL";
    case OP_IF_SETEVENTS:
        return "IF_SETEVENTS";
    case OP_IF_OPENTOP:
        return "IF_OPENTOP";
    case OP_REBUILD_NORMAL:
        return "REBUILD_NORMAL";
    case OP_UPDATE_RUNENERGY:
        return "UPDATE_RUNENERGY";
    case OP_MESSAGE_GAME:
        return "MESSAGE_GAME";
    case OP_NPC_INFO:
        return "NPC_INFO";
    case OP_SERVER_TICK_END:
        return "SERVER_TICK_END";
    case OP_UPDATE_STAT:
        return "UPDATE_STAT";
    default:
        return "?";
    }
}

void
mock230_send(
    struct Mock230Server* srv,
    int opcode,
    const uint8_t* payload,
    int len,
    int var)
{
    uint8_t frame[64 * 1024];
    struct RSAreaBuf buf;

    if( srv->fd < 0 )
        return;
    rsab_wrap(&buf, frame, sizeof(frame));
    rsab_p1(&buf, (opcode + isaac_next(srv->cipher_out)) & 0xff);
    if( var == 1 )
        rsab_p1(&buf, len);
    else if( var == 2 )
        rsab_p2(&buf, len);
    rsab_pdata(&buf, payload, (size_t)len);

    if( !rsab_ok(&buf) )
    {
        fprintf(stderr, "mock230: frame overflow for op %d (%d bytes)\n", opcode, len);
        return;
    }
    if( write(srv->fd, frame, rsab_len(&buf)) < 0 )
        srv->fd = -1;

    if( srv->verbose )
        fprintf(
            stderr,
            "mock230: -> %-18s op=%-3d payload=%d\n",
            opcode_name(opcode),
            opcode,
            len);
}

/* Send whatever the caller just built into `buf`. */
static void
flush(
    struct Mock230Server* srv,
    struct RSAreaBuf* buf,
    int opcode,
    int var)
{
    if( !rsab_ok(buf) )
    {
        fprintf(stderr, "mock230: dropped op %d — encode overflowed\n", opcode);
        return;
    }
    mock230_send(srv, opcode, buf->data, (int)rsab_len(buf), var);
}

/* ------------------------------------------------------------------ */
/* Scene                                                               */
/* ------------------------------------------------------------------ */

void
mock230_send_rebuild_normal(struct Mock230Server* srv)
{
    struct RSAreaBuf buf;
    int base_x = mock230_scene_origin(srv->zone_x);
    int base_z = mock230_scene_origin(srv->zone_z);
    int sq_x0 = base_x >> 6, sq_x1 = (base_x + MOCK230_SCENE_TILES - 1) >> 6;
    int sq_z0 = base_z >> 6, sq_z1 = (base_z + MOCK230_SCENE_TILES - 1) >> 6;
    int count = (sq_x1 - sq_x0 + 1) * (sq_z1 - sq_z0 + 1);

    open_packet(&buf, 4096);
    /* RSProt RebuildNormalEncoder: worldArea, zoneX (p2Alt2), zoneZ, keyCount,
     * then keyCount * 4 XTEA ints. Zero keys: unencrypted regions load, and
     * this cache ships its keys client-side via xteas.json. */
    rsab_p2(&buf, 0);
    rsab_p2_alt2(&buf, srv->zone_x);
    rsab_p2(&buf, srv->zone_z);
    rsab_p2(&buf, count);
    for( int i = 0; i < count * 4; i++ )
        rsab_p4(&buf, 0);

    flush(srv, &buf, OP_REBUILD_NORMAL, 2);
    if( srv->verbose )
        fprintf(
            stderr,
            "mock230: rebuild zone=%d,%d origin=%d,%d squares=%d\n",
            srv->zone_x,
            srv->zone_z,
            base_x,
            base_z,
            count);
}

/* ------------------------------------------------------------------ */
/* Interfaces                                                          */
/* ------------------------------------------------------------------ */

void
mock230_send_if_opentop(
    struct Mock230Server* srv,
    int group)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p2_alt1(&buf, group);
    flush(srv, &buf, OP_IF_OPENTOP, 0);
}

void
mock230_send_if_opensub(
    struct Mock230Server* srv,
    int parent,
    int child,
    int group,
    int type)
{
    /* RSProt IfOpenSubEncoder: p1 type, p2Alt2 interfaceId,
     * p4Alt3 destinationCombinedId (parent << 16 | child). */
    struct RSAreaBuf buf;
    open_packet(&buf, 16);
    rsab_p1(&buf, type);
    rsab_p2_alt2(&buf, group);
    rsab_p4_alt3(&buf, (parent << 16) | child);
    flush(srv, &buf, OP_IF_OPENSUB, 0);
}

void
mock230_send_if_setevents(
    struct Mock230Server* srv,
    int uid,
    int from,
    int to,
    int events)
{
    /* RSProt IfSetEventsEncoder: p4Alt3 combinedId, p2Alt2 start, p4Alt1
     * events, p2 end. */
    struct RSAreaBuf buf;
    open_packet(&buf, 16);
    rsab_p4_alt3(&buf, uid);
    rsab_p2_alt2(&buf, from);
    rsab_p4_alt1(&buf, events);
    rsab_p2(&buf, to);
    flush(srv, &buf, OP_IF_SETEVENTS, 0);
}

/* ------------------------------------------------------------------ */
/* Scalars                                                             */
/* ------------------------------------------------------------------ */

void
mock230_send_varp_small(
    struct Mock230Server* srv,
    int id,
    int value)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p2(&buf, id);
    rsab_p1(&buf, value);
    flush(srv, &buf, OP_VARP_SMALL, 0);
}

void
mock230_send_stat(
    struct Mock230Server* srv,
    int stat,
    int level,
    int xp)
{
    /* UPDATE_STAT_V2 (7 bytes). Field order is the mock's own — see the
     * osrs230_parse override that reads it back. */
    struct RSAreaBuf buf;
    open_packet(&buf, 16);
    rsab_p1(&buf, stat);
    rsab_p1(&buf, level);
    rsab_p4(&buf, xp);
    rsab_p1(&buf, level); /* boosted */
    flush(srv, &buf, OP_UPDATE_STAT, 0);
}

void
mock230_send_run_energy(
    struct Mock230Server* srv,
    int percent)
{
    /* Two bytes at rev 230: energy in hundredths of a percent. */
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p2(&buf, percent * 100);
    flush(srv, &buf, OP_UPDATE_RUNENERGY, 0);
}

void
mock230_send_run_weight(
    struct Mock230Server* srv,
    int kilograms)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p2(&buf, kilograms);
    flush(srv, &buf, OP_UPDATE_RUNWEIGHT, 0);
}

void
mock230_send_message(
    struct Mock230Server* srv,
    const char* text)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 512);
    rsab_p1(&buf, 0); /* message type: plain game message */
    rsab_pjstr(&buf, text, RSAB_JSTR_NUL);
    flush(srv, &buf, OP_MESSAGE_GAME, 1);
}

void
mock230_send_unset_map_flag(struct Mock230Server* srv)
{
    /* SET_MAP_FLAG with the 255,255 "no flag" sentinel. */
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p1(&buf, 255);
    rsab_p1(&buf, 255);
    flush(srv, &buf, OP_SET_MAP_FLAG, 0);
}

void
mock230_send_tick_end(struct Mock230Server* srv)
{
    mock230_send(srv, OP_SERVER_TICK_END, NULL, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Containers                                                          */
/* ------------------------------------------------------------------ */

/* The slot body both inventory encoders share: p1Alt2 count, escaping to a
 * p4Alt1 real count at 255, then p2 objId + 1 (0 = empty). */
static void
put_inv_slot(
    struct RSAreaBuf* buf,
    const struct Mock230Item* item)
{
    if( !item || item->obj_id < 0 || item->count <= 0 )
    {
        rsab_p1_alt2(buf, 0);
        rsab_p2(buf, 0);
        return;
    }
    rsab_p1_alt2(buf, item->count > 0xff ? 0xff : item->count);
    if( item->count >= 255 )
        rsab_p4_alt1(buf, item->count);
    rsab_p2(buf, item->obj_id + 1);
}

void
mock230_send_inv_full(
    struct Mock230Server* srv,
    int component,
    int container,
    const struct Mock230Item* slots,
    int slot_count)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8192);
    rsab_p4(&buf, component);
    rsab_p2(&buf, container);
    rsab_p2(&buf, slot_count);
    for( int i = 0; i < slot_count; i++ )
        put_inv_slot(&buf, slots ? &slots[i] : NULL);
    flush(srv, &buf, OP_UPDATE_INV_FULL, 2);
}

void
mock230_send_inv_partial(
    struct Mock230Server* srv,
    int component,
    int container,
    const struct Mock230Item* slots,
    int slot_count,
    uint32_t dirty)
{
    struct RSAreaBuf buf;
    if( dirty == 0 )
        return;
    open_packet(&buf, 8192);
    rsab_p4(&buf, component);
    rsab_p2(&buf, container);
    for( int i = 0; i < slot_count; i++ )
    {
        if( !(dirty & (1u << i)) )
            continue;
        rsab_psmart(&buf, i);
        put_inv_slot(&buf, &slots[i]);
    }
    flush(srv, &buf, OP_UPDATE_INV_PARTIAL, 2);
}

/* ------------------------------------------------------------------ */
/* PLAYER_INFO                                                         */
/* ------------------------------------------------------------------ */

/* The appearance blob the client's PktPlayerAppearance_Decode reads: gender,
 * head icon, 12 slots, 5 body colours, 7 movement animations, name37, combat
 * level. A slot is 0 when empty, 0x100 + idkId for a body kit, or 0x200 + objId
 * for a worn item — the same encoding PlayerModel_CollectAppearanceModelIds
 * splits on. */
static const int k_default_kits[12] = {
    /* head */ 0,
    /* cape */ 0,
    /* amulet */ 0,
    /* weapon */ 0,
    /* torso */ 0x100 + 18,
    /* shield */ 0,
    /* arms */ 0x100 + 26,
    /* legs */ 0x100 + 36,
    /* hair */ 0x100 + 0,
    /* hands */ 0x100 + 33,
    /* feet */ 0x100 + 42,
    /* jaw */ 0x100 + 10,
};

static void
put_appearance(
    struct RSAreaBuf* buf,
    const struct Mock230Player* player)
{
    /* Worn items win over the body kit in their own slot, and additionally
     * blank the slots they claim through wearpos_2 / wearpos_3 — which is what
     * makes a full helm hide hair and jaw, and a platebody hide arms. */
    int slots[12];
    int covered[12] = { 0 };

    for( int i = 0; i < MOCK230_WORN_SLOTS; i++ )
    {
        const struct Mock230ObjInfo* info;
        if( player->worn[i].obj_id < 0 )
            continue;
        info = mock230_objinfo(player->worn[i].obj_id);
        if( info->wearpos_2 >= 0 && info->wearpos_2 < 12 )
            covered[info->wearpos_2] = 1;
        if( info->wearpos_3 >= 0 && info->wearpos_3 < 12 )
            covered[info->wearpos_3] = 1;
    }

    for( int i = 0; i < 12; i++ )
    {
        if( i < MOCK230_WORN_SLOTS && player->worn[i].obj_id >= 0 )
            slots[i] = 0x200 + player->worn[i].obj_id;
        else if( covered[i] )
            slots[i] = 0;
        else
            slots[i] = k_default_kits[i];
    }

    rsab_p1(buf, 0); /* gender: male */
    rsab_p1(buf, 0); /* head icon */
    for( int i = 0; i < 12; i++ )
    {
        if( slots[i] == 0 )
            rsab_p1(buf, 0);
        else
            rsab_p2(buf, slots[i]);
    }
    for( int i = 0; i < 5; i++ )
        rsab_p1(buf, 0); /* body colours */

    /* idle, turn, walk, walk-back, walk-left, walk-right, run. */
    {
        static const int anims[7] = { 808, 823, 819, 820, 821, 822, 824 };
        for( int i = 0; i < 7; i++ )
            rsab_p2(buf, anims[i]);
    }

    rsab_p8(buf, 0); /* name37: empty */
    rsab_p1(buf, 3); /* combat level */
}

int
mock230_step_direction(
    int dx,
    int dz)
{
    if( dx < -1 || dx > 1 || dz < -1 || dz > 1 || (dx == 0 && dz == 0) )
        return -1;
    if( dz > 0 )
        return dx < 0 ? 0 : (dx == 0 ? 1 : 2);
    if( dz == 0 )
        return dx < 0 ? 3 : 4;
    return dx < 0 ? 5 : (dx == 0 ? 6 : 7);
}

void
mock230_send_player_info(struct Mock230Server* srv)
{
    struct RSAreaBuf buf;
    struct Mock230Player* player = &srv->player;
    int local_x = player->x - mock230_scene_origin(srv->zone_x);
    int local_z = player->z - mock230_scene_origin(srv->zone_z);
    int extended = player->appearance_dirty;

    open_packet(&buf, 2048);
    rsab_bits(&buf);

    /* --- local player --- */
    if( player->place_dirty )
    {
        /* Move op 3 on the local player is an absolute placement, not the
         * "remove" it means for a tracked player. */
        rsab_pbit(&buf, 1, 1);
        rsab_pbit(&buf, 2, 3);
        rsab_pbit(&buf, 2, player->level);
        rsab_pbit(&buf, 7, local_x & 0x7f);
        rsab_pbit(&buf, 7, local_z & 0x7f);
        rsab_pbit(&buf, 1, 1); /* jump: snap rather than glide */
        rsab_pbit(&buf, 1, extended);
    }
    else if( player->move_count == 2 )
    {
        rsab_pbit(&buf, 1, 1);
        rsab_pbit(&buf, 2, 2);
        rsab_pbit(&buf, 3, player->move_dirs[0]);
        rsab_pbit(&buf, 3, player->move_dirs[1]);
        rsab_pbit(&buf, 1, extended);
    }
    else if( player->move_count == 1 )
    {
        rsab_pbit(&buf, 1, 1);
        rsab_pbit(&buf, 2, 1);
        rsab_pbit(&buf, 3, player->move_dirs[0]);
        rsab_pbit(&buf, 1, extended);
    }
    else if( extended )
    {
        rsab_pbit(&buf, 1, 1);
        rsab_pbit(&buf, 2, 0); /* stationary, extended info follows */
    }
    else
    {
        rsab_pbit(&buf, 1, 0); /* nothing to say about the local player */
    }

    /* --- tracked players: the mock is single-player --- */
    rsab_pbit(&buf, 8, 0);

    /* --- new players: none, straight to the terminator ---
     * The terminator is not optional. Without it the client keeps reading
     * 11-bit ids out of whatever follows, which at best invents players and at
     * worst eats the extended-info section. */
    rsab_pbit(&buf, 11, MOCK230_PLAYER_TERMINATOR);
    rsab_bytes(&buf);

    /* --- extended info, byte aligned, in the order the bits queued it --- */
    if( extended )
    {
        size_t mark;
        rsab_p1(&buf, 0x01); /* mask: APPEARANCE */
        mark = rsab_psize1_begin(&buf);
        put_appearance(&buf, player);
        rsab_psize1_end(&buf, mark);
    }

    flush(srv, &buf, OP_PLAYER_INFO, 2);
    player->appearance_dirty = 0;
    player->place_dirty = 0;
}

/* ------------------------------------------------------------------ */
/* NPC_INFO                                                            */
/* ------------------------------------------------------------------ */

/* Extended-info mask bits the client's pkt_npc_info reader knows. */
enum
{
    NPC_MASK_ANIM = 0x2,
    NPC_MASK_FACE_ENTITY = 0x4,
    NPC_MASK_SAY = 0x8,
};

static int
npc_extended_pending(const struct Mock230Npc* npc)
{
    return npc->say_dirty || npc->face_entity_dirty;
}

static void
put_npc_extended(
    struct RSAreaBuf* buf,
    struct Mock230Npc* npc)
{
    int mask = 0;
    if( npc->face_entity_dirty )
        mask |= NPC_MASK_FACE_ENTITY;
    if( npc->say_dirty )
        mask |= NPC_MASK_SAY;
    rsab_p1(buf, mask);

    /* Field order follows the reader, which tests the mask bits in a fixed
     * order rather than in numeric order. */
    if( npc->face_entity_dirty )
        rsab_p2(buf, npc->face_entity < 0 ? 0xffff : npc->face_entity);
    if( npc->say_dirty )
        rsab_pjstr(buf, npc->say, RSAB_JSTR_NEWLINE);

    npc->face_entity_dirty = 0;
    npc->say_dirty = 0;
}

void
mock230_send_npc_info(struct Mock230Server* srv)
{
    struct RSAreaBuf buf;
    struct Mock230Player* player = &srv->player;
    /* Extended blocks are appended in the order the bit section queued them,
     * so remember that order while writing the bits. */
    int queued[MOCK230_NPC_MAX];
    int queued_count = 0;
    int kept[MOCK230_NPC_MAX];
    int kept_count = 0;

    open_packet(&buf, 8192);
    rsab_bits(&buf);

    /* --- tracked npcs, in the client's list order --- */
    rsab_pbit(&buf, 8, srv->tracked_count);
    for( int i = 0; i < srv->tracked_count; i++ )
    {
        int slot = srv->tracked[i];
        struct Mock230Npc* npc = &srv->npcs[slot];
        int dx = npc->x - player->x;
        int dz = npc->z - player->z;
        int in_range = npc->active && dx >= -15 && dx <= 15 && dz >= -15 && dz <= 15;
        int extended = npc_extended_pending(npc);

        if( !in_range )
        {
            /* Move op 3 removes the npc from the client's list. It is the one
             * op that does not keep the slot, so it must not be counted into
             * the new tracked order. */
            rsab_pbit(&buf, 1, 1);
            rsab_pbit(&buf, 2, 3);
            npc->tracked = 0;
            continue;
        }

        kept[kept_count++] = slot;
        if( npc->step_dir >= 0 )
        {
            rsab_pbit(&buf, 1, 1);
            rsab_pbit(&buf, 2, 1);
            rsab_pbit(&buf, 3, npc->step_dir);
            rsab_pbit(&buf, 1, extended);
        }
        else if( extended )
        {
            rsab_pbit(&buf, 1, 1);
            rsab_pbit(&buf, 2, 0);
        }
        else
        {
            rsab_pbit(&buf, 1, 0);
        }
        if( extended )
            queued[queued_count++] = slot;
    }

    /* --- npcs entering view --- */
    for( int slot = 0; slot < MOCK230_NPC_MAX; slot++ )
    {
        struct Mock230Npc* npc = &srv->npcs[slot];
        int dx, dz;
        if( !npc->active || npc->tracked )
            continue;
        dx = npc->x - player->x;
        dz = npc->z - player->z;
        if( dx < -15 || dx > 15 || dz < -15 || dz > 15 )
            continue;
        if( kept_count >= MOCK230_NPC_MAX - 1 )
            break;

        /* 14-bit slot, 11-bit type, 5-bit signed deltas from the local player,
         * 1-bit "extended info follows". No jump bit here — unlike the player
         * stream's new-entity record. */
        rsab_pbit(&buf, MOCK230_NPC_SLOT_BITS, slot);
        rsab_pbit(&buf, MOCK230_NPC_TYPE_BITS, npc->type);
        rsab_pbit(&buf, 5, dx & 0x1f);
        rsab_pbit(&buf, 5, dz & 0x1f);
        rsab_pbit(&buf, 1, npc_extended_pending(npc));
        if( npc_extended_pending(npc) )
            queued[queued_count++] = slot;
        npc->tracked = 1;
        kept[kept_count++] = slot;
    }

    rsab_pbit(&buf, MOCK230_NPC_SLOT_BITS, MOCK230_NPC_TERMINATOR);
    rsab_bytes(&buf);

    for( int i = 0; i < queued_count; i++ )
        put_npc_extended(&buf, &srv->npcs[queued[i]]);

    flush(srv, &buf, OP_NPC_INFO, 2);

    memcpy(srv->tracked, kept, sizeof(int) * (size_t)kept_count);
    srv->tracked_count = kept_count;
}
