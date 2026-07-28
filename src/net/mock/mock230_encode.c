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

#include "mock230_ws.h"

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
    /* Assigned, not transcribed — see docs/osrs230_mockserver.md 3.5. */
    OP_IF_SETTEXT = 94,
    OP_IF_SETNPCHEAD = 95,
    OP_IF_SETPLAYERHEAD = 96,
    OP_IF_SETANIM = 97,
    OP_IF_SETHIDE = 98,
    OP_IF_CLOSESUB = 36,
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

    /* Above the fd check on purpose: the selftest runs with no socket, and this
     * is the one point every encoder has already passed through with its
     * payload built. Recording here makes all of them observable without any
     * encoder knowing the capture exists. */
    if( srv->capture )
    {
        struct Mock230Capture* capture = srv->capture;

        if( capture->count < MOCK230_CAPTURE_MAX && len <= MOCK230_CAPTURE_BYTES )
        {
            struct Mock230CapturedPacket* packet = &capture->packets[capture->count++];

            packet->opcode = opcode;
            packet->len = len;
            if( len > 0 )
                memcpy(packet->data, payload, (size_t)len);
        }
        else
        {
            /* Never silently truncate: a test asserting "this packet is absent"
             * against a full buffer would pass for the wrong reason. */
            capture->overflow = 1;
        }
    }

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
    if( mock230_conn_send(srv->conn, frame, (int)rsab_len(&buf)) < 0 )
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

/* ------------------------------------------------------------------ */
/* Interface setters                                                   */
/* ------------------------------------------------------------------ */

/*
 * All five carry a p4 combined uid ((interface << 16) | child), which is how
 * rev 230 addresses a component. lc254's flat p2 component id does not exist
 * here — see the osrs230_parse overrides for the reading half.
 */

void
mock230_send_if_settext(
    struct Mock230Server* srv,
    int uid,
    const char* text)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 512);
    rsab_p4(&buf, uid);
    rsab_pjstr(&buf, text ? text : "", RSAB_JSTR_NEWLINE);
    flush(srv, &buf, OP_IF_SETTEXT, 2);
}

void
mock230_send_if_setnpchead(
    struct Mock230Server* srv,
    int uid,
    int npc_id)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 16);
    rsab_p4(&buf, uid);
    rsab_p2(&buf, npc_id);
    flush(srv, &buf, OP_IF_SETNPCHEAD, 0);
}

void
mock230_send_if_setplayerhead(
    struct Mock230Server* srv,
    int uid)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    rsab_p4(&buf, uid);
    flush(srv, &buf, OP_IF_SETPLAYERHEAD, 0);
}

void
mock230_send_if_setanim(
    struct Mock230Server* srv,
    int uid,
    int anim_id)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 16);
    rsab_p4(&buf, uid);
    rsab_p2(&buf, anim_id);
    flush(srv, &buf, OP_IF_SETANIM, 0);
}

void
mock230_send_if_sethide(
    struct Mock230Server* srv,
    int uid,
    int hide)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    rsab_p4(&buf, uid);
    rsab_p1(&buf, hide ? 1 : 0);
    flush(srv, &buf, OP_IF_SETHIDE, 0);
}

void
mock230_send_if_closesub(
    struct Mock230Server* srv,
    int uid)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    rsab_p4(&buf, uid);
    flush(srv, &buf, OP_IF_CLOSESUB, 0);
}

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

/*
 * Write a player's extended-info block.
 *
 * Two rules, and getting either wrong corrupts everything after this player in
 * the stream rather than just dropping a field:
 *
 *   The mask is one byte unless any bit at 0x100 or above is set, in which case
 *   BIG_UPDATE (0x80) must be set AND the mask written as two bytes, low first.
 *   The reader does `mask = g1(); if (mask & 0x80) mask += g1() << 8;` — so a
 *   two-byte mask without 0x80 desyncs, and 0x80 without a second byte makes
 *   the reader eat the first field as mask bits.
 *
 *   Fields go in ascending bit order, because that is the order the reader
 *   tests them in. The mask says which fields are present, never where.
 */
static void
put_player_extended(
    struct RSAreaBuf* buf,
    struct Mock230Player* player)
{
    uint32_t mask = player->masks;

    if( mask >= 0x100 )
    {
        mask |= MOCK230_PMASK_BIG_UPDATE;
        rsab_p1(buf, (int32_t)(mask & 0xff));
        rsab_p1(buf, (int32_t)((mask >> 8) & 0xff));
    }
    else
    {
        rsab_p1(buf, (int32_t)mask);
    }

    if( mask & MOCK230_PMASK_APPEARANCE )
    {
        size_t marker = rsab_psize1_begin(buf);

        put_appearance(buf, player);
        rsab_psize1_end(buf, marker);
    }
    if( mask & MOCK230_PMASK_SEQUENCE )
    {
        /* -1 goes on the wire as 65535, which is how the client spells "stop
         * whatever is playing". */
        rsab_p2(buf, player->anim_id < 0 ? 65535 : player->anim_id);
        rsab_p1(buf, player->anim_delay);
    }
    if( mask & MOCK230_PMASK_FACE_ENTITY )
        rsab_p2(buf, player->face_entity < 0 ? 0xffff : player->face_entity);
    if( mask & MOCK230_PMASK_SAY )
        rsab_pjstr(buf, player->say, RSAB_JSTR_NEWLINE);
    if( mask & MOCK230_PMASK_DAMAGE )
    {
        rsab_p1(buf, player->damage);
        rsab_p1(buf, player->damage_type);
        rsab_p1(buf, player->hitpoints);
        rsab_p1(buf, player->max_hitpoints);
    }
    if( mask & MOCK230_PMASK_FACE_COORD )
    {
        rsab_p2(buf, player->face_x);
        rsab_p2(buf, player->face_z);
    }
    if( mask & MOCK230_PMASK_CHAT )
    {
        rsab_p2(buf, player->chat_colour_effect);
        rsab_p1(buf, player->chat_type);
        rsab_p1(buf, player->chat_len);
        rsab_pdata(buf, player->chat_data, (size_t)player->chat_len);
    }
    if( mask & MOCK230_PMASK_SPOTANIM )
    {
        rsab_p2(buf, player->spotanim_id < 0 ? 65535 : player->spotanim_id);
        rsab_p4(buf, player->spotanim_height_delay);
    }
    if( mask & MOCK230_PMASK_DAMAGE2 )
    {
        rsab_p1(buf, player->damage);
        rsab_p1(buf, player->damage_type);
        rsab_p1(buf, player->hitpoints);
        rsab_p1(buf, player->max_hitpoints);
    }
}

void
mock230_send_player_info(struct Mock230Server* srv)
{
    struct RSAreaBuf buf;
    struct Mock230Player* player = &srv->player;
    int local_x = player->x - mock230_scene_origin(srv->zone_x);
    int local_z = player->z - mock230_scene_origin(srv->zone_z);
    int extended = player->masks != 0;

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
        put_player_extended(&buf, player);

    flush(srv, &buf, OP_PLAYER_INFO, 2);
    player->place_dirty = 0;
}

/* ------------------------------------------------------------------ */
/* NPC_INFO                                                            */
/* ------------------------------------------------------------------ */

static int
npc_extended_pending(const struct Mock230Npc* npc)
{
    return npc->masks != 0;
}

/*
 * The npc mask is a single byte — unlike the player's, there is no widening
 * bit, so the eight fields below are all there is room for. Fields go in
 * ascending bit order, matching the reader's test order.
 */
static void
put_npc_extended(
    struct RSAreaBuf* buf,
    struct Mock230Npc* npc)
{
    uint32_t mask = npc->masks & 0xff;

    rsab_p1(buf, (int32_t)mask);

    if( mask & MOCK230_NMASK_DAMAGE2 )
    {
        rsab_p1(buf, npc->damage);
        rsab_p1(buf, npc->damage_type);
        rsab_p1(buf, npc->hitpoints);
        rsab_p1(buf, npc->max_hitpoints);
    }
    if( mask & MOCK230_NMASK_ANIM )
    {
        rsab_p2(buf, npc->anim_id < 0 ? 65535 : npc->anim_id);
        rsab_p1(buf, npc->anim_delay);
    }
    if( mask & MOCK230_NMASK_FACE_ENTITY )
        rsab_p2(buf, npc->face_entity < 0 ? 0xffff : npc->face_entity);
    if( mask & MOCK230_NMASK_SAY )
        rsab_pjstr(buf, npc->say, RSAB_JSTR_NEWLINE);
    if( mask & MOCK230_NMASK_DAMAGE )
    {
        rsab_p1(buf, npc->damage);
        rsab_p1(buf, npc->damage_type);
        rsab_p1(buf, npc->hitpoints);
        rsab_p1(buf, npc->max_hitpoints);
    }
    if( mask & MOCK230_NMASK_CHANGE_TYPE )
        rsab_p2(buf, npc->change_type);
    if( mask & MOCK230_NMASK_SPOTANIM )
    {
        rsab_p2(buf, npc->spotanim_id < 0 ? 65535 : npc->spotanim_id);
        rsab_p4(buf, npc->spotanim_height_delay);
    }
    if( mask & MOCK230_NMASK_FACE_COORD )
    {
        rsab_p2(buf, npc->face_x);
        rsab_p2(buf, npc->face_z);
    }
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

/* ------------------------------------------------------------------ */
/* Capture                                                             */
/* ------------------------------------------------------------------ */

void
mock230_capture_begin(
    struct Mock230Server* srv,
    struct Mock230Capture* capture)
{
    mock230_capture_reset(capture);
    srv->capture = capture;
}

void
mock230_capture_end(struct Mock230Server* srv)
{
    srv->capture = NULL;
}

void
mock230_capture_reset(struct Mock230Capture* capture)
{
    capture->count = 0;
    capture->overflow = 0;
}

int
mock230_capture_find(
    const struct Mock230Capture* capture,
    int opcode,
    int from)
{
    for( int i = from < 0 ? 0 : from; i < capture->count; i++ )
    {
        if( capture->packets[i].opcode == opcode )
            return i;
    }
    return -1;
}

int
mock230_capture_has_sequence(
    const struct Mock230Capture* capture,
    const int* opcodes,
    int count)
{
    int at = 0;

    /* Order matters, adjacency does not: a tick interleaves packets from
     * several phases, and a test that demanded adjacency would break every time
     * an unrelated encoder was added. */
    for( int i = 0; i < count; i++ )
    {
        at = mock230_capture_find(capture, opcodes[i], at);
        if( at < 0 )
            return 0;
        at++;
    }
    return 1;
}
