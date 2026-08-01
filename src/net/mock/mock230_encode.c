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

#include "mock230_content.h"
#include "mock230_session.h"

#include "net/isaac.h"
#include "net/jbase37.h"

/* The client's framing table, for the length check in mock230_send. */
#include "net/rev/osrs230/packetin.h"

#include <rsareabuf.h>

#include <stdio.h>
#include <stdlib.h>
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
    OP_RUNCLIENTSCRIPT = 84,
    /* Assigned here, zero-length; see src/net/rev/osrs230/packetin.h. */
    OP_P_COUNTDIALOG = 128,
    OP_IF_OPENTOP = 60,
    OP_REBUILD_NORMAL = 68,
    OP_UPDATE_RUNENERGY = 77,
    OP_MESSAGE_GAME = 90,
    OP_NPC_INFO = 104,
    OP_SERVER_TICK_END = 108,
    OP_UPDATE_STAT = 114,
    OP_UPDATE_PID = 127,
    OP_VARP_LARGE = 82,

    /* Zones. 106 is a real rev-230 opcode; the sub-packet opcodes are assigned
     * here and must match src/net/rev/osrs230/packetin.h, which is where the
     * client resolves them (a zone sub-packet's opcode is looked up in the same
     * table as a top-level one, so they cannot collide with either). */
    OP_UPDATE_ZONE_PARTIAL_FOLLOWS = 106,
    OP_LOC_ADD_CHANGE = 70,
    OP_LOC_DEL = 71,
    OP_OBJ_ADD = 120,
    OP_OBJ_DEL = 121,
    OP_OBJ_COUNT = 122,
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
    case OP_RUNCLIENTSCRIPT:
        return "RUNCLIENTSCRIPT";
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
    case OP_UPDATE_PID:
        return "UPDATE_PID";
    case OP_VARP_LARGE:
        return "VARP_LARGE";
    case OP_UPDATE_ZONE_PARTIAL_FOLLOWS:
        return "UPDATE_ZONE_PARTIAL_FOLLOWS";
    case OP_LOC_ADD_CHANGE:
        return "LOC_ADD_CHANGE";
    case OP_LOC_DEL:
        return "LOC_DEL";
    case OP_OBJ_ADD:
        return "OBJ_ADD";
    case OP_OBJ_DEL:
        return "OBJ_DEL";
    case OP_OBJ_COUNT:
        return "OBJ_COUNT";
    default:
        return "?";
    }
}

/*
 * A fixed-length packet's framing comes from the client's table, not from what
 * was written here: send one byte short and the client's reader takes the next
 * packet's opcode as the missing field, so the stream desyncs at some unrelated
 * packet later on and the encoder that was actually wrong is nowhere in sight.
 * The table is right there in the client tree, so check against it rather than
 * waiting for the symptom.
 */
static void
check_frame_length(
    int opcode,
    int len,
    int var)
{
    if( var != 0 )
        return;
    for( int i = 0; i < (int)(sizeof(g_packet_in_definitions_osrs230) /
                              sizeof(g_packet_in_definitions_osrs230[0]));
         i++ )
    {
        struct Osrs230PacketInDef const* def = &g_packet_in_definitions_osrs230[i];

        if( def->code != opcode )
            continue;
        if( def->length >= 0 && def->length != len )
            fprintf(
                stderr,
                "mock230: op %d (%s) wrote %d bytes, client frames it as %d\n",
                opcode,
                opcode_name(opcode),
                len,
                def->length);
        return;
    }
}

void
mock230_send(
    struct Mock230Player* player,
    int opcode,
    const uint8_t* payload,
    int len,
    int var)
{
    uint8_t frame[64 * 1024];
    struct RSAreaBuf buf;
    struct Mock230Server* srv;

    /* A packet is addressed to a player, and every encoder above this now says
     * so. What is left of the old "send to the server's one player" shape is
     * this line: the capture is a property of the world, because a test asserts
     * on what the *server* emitted, not on what one client received. */
    if( !player )
        return;
    srv = player->world;

    check_frame_length(opcode, len, var);

    /* Above the fd check on purpose: the selftest runs with no socket, and this
     * is the one point every encoder has already passed through with its
     * payload built. Recording here makes all of them observable without any
     * encoder knowing the capture exists. */
    if( srv && srv->capture )
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

    /*
     * No session is a world with no client — the selftest. Everything above
     * this point still ran, so the capture saw the packet.
     *
     * `cipher_out` is checked, not just the state: a session is "alive" from
     * the moment it is accepted, but its ISAAC pair does not exist until the
     * login block is parsed, so anything the world emits in that window would
     * otherwise scramble its opcode against a null cipher. With one player
     * that window was invisible because nothing was addressed to a
     * half-logged-in session; with a pool, every tick's PLAYER_INFO is.
     */
    if( !player->session || !mock230_session_alive(player->session) ||
        !player->session->cipher_out )
        return;
    rsab_wrap(&buf, frame, sizeof(frame));
    rsab_p1(&buf, (opcode + isaac_next(player->session->cipher_out)) & 0xff);
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
    if( mock230_session_send(player->session, frame, (int)rsab_len(&buf)) < 0 )
        mock230_session_kill(player->session);

    if( srv && srv->verbose )
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
    struct Mock230Player* player,
    struct RSAreaBuf* buf,
    int opcode,
    int var)
{
    if( !rsab_ok(buf) )
    {
        fprintf(stderr, "mock230: dropped op %d — encode overflowed\n", opcode);
        return;
    }
    mock230_send(player, opcode, buf->data, (int)rsab_len(buf), var);
}

/* ------------------------------------------------------------------ */
/* Scene                                                               */
/* ------------------------------------------------------------------ */

void
mock230_send_rebuild_normal(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;
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

    flush(player, &buf, OP_REBUILD_NORMAL, 2);
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
    struct Mock230Player* player,
    int group)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p2_alt1(&buf, group);
    flush(player, &buf, OP_IF_OPENTOP, 0);
}

void
mock230_send_if_opensub(
    struct Mock230Player* player,
    int parent,
    int child,
    int group,
    int type)
{
    /* RSProt IfOpenSubEncoder: p1 type, p2Alt2 interfaceId,
     * p4Alt3 destinationCombinedId (parent << 16 | child). */
    struct Mock230Server* srv = player->world;
    struct RSAreaBuf buf;

    open_packet(&buf, 16);
    rsab_p1(&buf, type);
    rsab_p2_alt2(&buf, group);
    rsab_p4_alt3(&buf, (parent << 16) | child);
    flush(player, &buf, OP_IF_OPENSUB, 0);
    mock230_note_modal_mount(srv, (parent << 16) | child, group);
}

/*
 * Reference layout: the per-argument type string (newline-terminated), then the
 * arguments in REVERSE order, then the script id.
 *
 * The reverse order is not a quirk of this port — the reference's writer pushes
 * the CS2 operand stack, which unwinds last-argument-first. `osrs230_parse.c`
 * reads it back the same way.
 */
void
mock230_send_run_clientscript_mixed(
    struct Mock230Player* player,
    int script_id,
    const char* types,
    int const* intv,
    const char* const* strv,
    int argc)
{
    struct RSAreaBuf buf;

    /* Sized for the string case, not the int one. A multi-choice option list is
     * one string carrying every row (see PKT_RUNCLIENTSCRIPT_STR_LEN), and five
     * rows of dialogue is comfortably past a kilobyte. */
    open_packet(&buf, 4096);
    for( int i = 0; i < argc; i++ )
        rsab_p1(&buf, types && types[i] ? (uint8_t)types[i] : (uint8_t)'i');
    rsab_p1(&buf, '\n');
    for( int i = argc - 1; i >= 0; i-- )
    {
        if( types && types[i] == 's' )
            rsab_pjstr(&buf, strv && strv[i] ? strv[i] : "", RSAB_JSTR_NEWLINE);
        else
            rsab_p4(&buf, intv ? intv[i] : 0);
    }
    rsab_p4(&buf, script_id);
    flush(player, &buf, OP_RUNCLIENTSCRIPT, 2);
}

void
mock230_send_run_clientscript(
    struct Mock230Player* player,
    int script_id,
    int const* args,
    int argc)
{
    mock230_send_run_clientscript_mixed(player, script_id, NULL, args, NULL, argc);
}

void
mock230_send_if_setevents(
    struct Mock230Player* player,
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
    flush(player, &buf, OP_IF_SETEVENTS, 0);
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
    struct Mock230Player* player,
    int uid,
    const char* text)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 512);
    rsab_p4(&buf, uid);
    rsab_pjstr(&buf, text ? text : "", RSAB_JSTR_NEWLINE);
    flush(player, &buf, OP_IF_SETTEXT, 2);
}

void
mock230_send_if_setnpchead(
    struct Mock230Player* player,
    int uid,
    int npc_id)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 16);
    rsab_p4(&buf, uid);
    rsab_p2(&buf, npc_id);
    flush(player, &buf, OP_IF_SETNPCHEAD, 0);
}

void
mock230_send_if_setplayerhead(
    struct Mock230Player* player,
    int uid)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    rsab_p4(&buf, uid);
    flush(player, &buf, OP_IF_SETPLAYERHEAD, 0);
}

void
mock230_send_if_setanim(
    struct Mock230Player* player,
    int uid,
    int anim_id)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 16);
    rsab_p4(&buf, uid);
    rsab_p2(&buf, anim_id);
    flush(player, &buf, OP_IF_SETANIM, 0);
}

void
mock230_send_if_sethide(
    struct Mock230Player* player,
    int uid,
    int hide)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    rsab_p4(&buf, uid);
    rsab_p1(&buf, hide ? 1 : 0);
    flush(player, &buf, OP_IF_SETHIDE, 0);
}

void
mock230_send_if_closesub(
    struct Mock230Player* player,
    int uid)
{
    struct Mock230Server* srv = player->world;
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    rsab_p4(&buf, uid);
    flush(player, &buf, OP_IF_CLOSESUB, 0);
    mock230_note_modal_mount(srv, uid, 0);
}

/*
 * P_COUNTDIALOG: open the "Enter amount" prompt.
 *
 * No payload — the packet is the whole message, and the answer comes back as
 * RESUME_P_COUNTDIALOG. The client's handler is `RS_Chat.dialog_input`, which
 * already existed; only the packet reaching it was missing.
 */
void
mock230_send_if_opencountdialog(struct Mock230Player* player)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 4);
    flush(player, &buf, OP_P_COUNTDIALOG, 0);
}

void
mock230_send_varp_small(
    struct Mock230Player* player,
    int id,
    int value)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p2(&buf, id);
    rsab_p1(&buf, value);
    flush(player, &buf, OP_VARP_SMALL, 0);
}

/*
 * VARP_LARGE: p2 id, p4 value.
 *
 * VARP_SMALL's value is a single signed byte, so anything outside -128..127
 * has to go this way. Special-attack energy is in tenths of a percent — 1000
 * for a full bar — which is exactly the case that made this necessary.
 */
void
mock230_send_varp_large(
    struct Mock230Player* player,
    int id,
    int value)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p2(&buf, id);
    rsab_p4(&buf, value);
    flush(player, &buf, OP_VARP_LARGE, 0);
}

void
mock230_send_stat(
    struct Mock230Player* player,
    int stat,
    int level,
    int xp,
    int boosted)
{
    /* UPDATE_STAT_V2 (7 bytes). Field order is the mock's own — see the
     * osrs230_parse override that reads it back.
     *
     * The boosted level is the one with a consumer: the client derives the base
     * level from the xp and writes this into `current_level`, which is what the
     * health orb reads for hitpoints. Sending the base twice pins the orb at
     * full health forever, which is exactly what it used to do. */
    struct RSAreaBuf buf;
    open_packet(&buf, 16);
    rsab_p1(&buf, stat);
    rsab_p1(&buf, level);
    rsab_p4(&buf, xp);
    rsab_p1(&buf, boosted);
    flush(player, &buf, OP_UPDATE_STAT, 0);
}

/*
 * Which player index the client is.
 *
 * The mock puts the local player at 2047 — the classic self index, which is
 * also what every fallback in the client assumes — but "assumes" is the
 * problem: `world->local_pid` stays -1 until something says otherwise, and the
 * minimenu gates its `(level-N)` suffix on a local player actually being
 * known. Sending it once at login is what makes the client sure.
 */
void
mock230_send_update_pid(
    struct Mock230Player* player,
    int local_pid)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p2(&buf, local_pid);
    rsab_p1(&buf, 0); /* members flag */
    flush(player, &buf, OP_UPDATE_PID, 0);
}

void
mock230_send_run_energy(
    struct Mock230Player* player,
    int percent)
{
    /* Two bytes at rev 230: energy in hundredths of a percent. */
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p2(&buf, percent * 100);
    flush(player, &buf, OP_UPDATE_RUNENERGY, 0);
}

void
mock230_send_run_weight(
    struct Mock230Player* player,
    int kilograms)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p2(&buf, kilograms);
    flush(player, &buf, OP_UPDATE_RUNWEIGHT, 0);
}

void
mock230_send_message(
    struct Mock230Player* player,
    const char* text)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 512);
    rsab_p1(&buf, 0); /* message type: plain game message */
    rsab_pjstr(&buf, text, RSAB_JSTR_NUL);
    flush(player, &buf, OP_MESSAGE_GAME, 1);
}

void
mock230_send_unset_map_flag(struct Mock230Player* player)
{
    /* SET_MAP_FLAG with the 255,255 "no flag" sentinel. */
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    rsab_p1(&buf, 255);
    rsab_p1(&buf, 255);
    flush(player, &buf, OP_SET_MAP_FLAG, 0);
}

void
mock230_send_tick_end(struct Mock230Player* player)
{
    mock230_send(player, OP_SERVER_TICK_END, NULL, 0, 0);
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
    struct Mock230Player* player,
    int component,
    int container,
    const struct Mock230Item* slots,
    int slot_count)
{
    struct RSAreaBuf buf;
    /* A slot is at most 7 bytes (count byte + 4-byte escape + 2-byte obj), and
     * the bank is 1220 of them — well past the 8 KB this used to reserve when
     * the only containers were the 28-slot backpack and the 14-slot worn set.
     * Sizing from the count keeps a full bank inside one packet. */
    open_packet(&buf, (size_t)(16 + ((slot_count > 0 ? slot_count : 0) * 7)));
    rsab_p4(&buf, component);
    rsab_p2(&buf, container);
    rsab_p2(&buf, slot_count);
    for( int i = 0; i < slot_count; i++ )
        put_inv_slot(&buf, slots ? &slots[i] : NULL);
    flush(player, &buf, OP_UPDATE_INV_FULL, 2);
}

void
mock230_send_inv_partial(
    struct Mock230Player* player,
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
    flush(player, &buf, OP_UPDATE_INV_PARTIAL, 2);
}

/* ------------------------------------------------------------------ */
/* PLAYER_INFO                                                         */
/* ------------------------------------------------------------------ */

/* The appearance blob the client's PktPlayerAppearance_Decode reads: gender,
 * head icon, 12 slots, 5 body colours, 7 movement animations, name37, combat
 * level. A slot is 0 when empty, 0x100 + idkId for a body kit, or 0x200 + objId
 * for a worn item — the same encoding PlayerModel_CollectAppearanceModelIds
 * splits on. The tag and the ordering are the wire's, so they are here; *which*
 * kit fills a bare slot is the player's character and is content's, in
 * `player/configs/appearance.enum`. */
static int
default_kit(int wearpos)
{
    const struct Mock230EnumDef* kits = mock230_content_enum("default_appearance");

    for( int i = 0; kits && i < kits->count; i++ )
        if( kits->values[i].key == wearpos )
            return kits->values[i].value;
    /* `default=-1` — no body part is drawn at this position. A tree with no
     * such enum lands here for all twelve, which is a naked player rather than
     * a crash, and the missing config is the thing to fix. */
    return -1;
}

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
        int kit;

        if( i < MOCK230_WORN_SLOTS && player->worn[i].obj_id >= 0 )
            slots[i] = 0x200 + player->worn[i].obj_id;
        else if( covered[i] )
            slots[i] = 0;
        else if( (kit = default_kit(i)) >= 0 )
            slots[i] = 0x100 + kit;
        else
            slots[i] = 0;
    }

    rsab_p1(buf, (uint8_t)player->gender);
    /*
     * Overhead icons.
     *
     * A real rev-230 appearance carries two separate one-byte fields here — a
     * prayer icon index and a PK-skull index, each 255 for "none". This client
     * reads ONE byte and treats it as a bitmask over the `headicons` sprite
     * pack (app.c: app_overlay_build_player_headicons plots every set bit,
     * stacked upward), which is the older shape. The mask is what goes on the
     * wire because the client is the only consumer; see
     * docs/mock230_player_systems.md §4.
     */
    {
        int headicons = player->headicons;
        if( headicons && getenv("MOCK230_VERBOSE") )
            fprintf(stderr, "mock230: appearance headicons=0x%x\n", headicons);
        rsab_p1(buf, headicons);
    }
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

    /*
     * The player's name, base-37 packed.
     *
     * This was a literal 0 — "name37: empty" — and with one player it cost
     * nothing, because the only appearance a client ever decoded was its own and
     * it already knew who it was. It is the field that says *which* of two
     * players you are looking at: the client's minimenu and its overhead label
     * both read it out of the appearance blob (`PktPlayerAppearance.name`), so
     * an empty one makes everybody in the world an anonymous body.
     */
    rsab_p8(buf, (int64_t)strtobase37(player->display_name));
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

/* The same numbering read backwards, so a caller that has a direction can find
 * the tile it lands on without keeping a second copy of the table. */
void
mock230_step_delta(
    int dir,
    int* dx,
    int* dz)
{
    static const int k_dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const int k_dz[8] = { 1, 1, 1, 0, 0, -1, -1, -1 };

    if( dir < 0 || dir > 7 )
    {
        *dx = 0;
        *dz = 0;
        return;
    }
    *dx = k_dx[dir];
    *dz = k_dz[dir];
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
    struct Mock230Player* player,
    int force_appearance)
{
    uint32_t mask = player->masks;

    /*
     * A player entering someone's view needs their appearance whether or not
     * they happened to change it this tick.
     *
     * `masks` is the player's own per-tick set, cleared in phase 11, so it says
     * "what changed", and for an observer who has never seen this player before
     * the answer has to be "everything they are". Without this the client spawns
     * the default composited model and never replaces it — a player-shaped
     * outline that walks around correctly, which is exactly the kind of bug that
     * reads as a rendering problem.
     */
    if( force_appearance )
        mask |= MOCK230_PMASK_APPEARANCE;

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

/* ------------------------------------------------------------------ */
/* Zones                                                               */
/* ------------------------------------------------------------------ */

/*
 * A zone sub-packet does not carry a coordinate — only `pos`, the tile's
 * offset inside an 8x8 zone as `(local_x << 4) | local_z`. Which zone that is
 * comes from the UPDATE_ZONE_* packet before it, and the client keeps it as
 * state until the next one. Sending a sub-packet without a zone header applies
 * it to whatever zone was last named, which is a wrong-place bug rather than a
 * decode failure.
 *
 * The base is in **classic scene-local tiles**: the client adds `scene_off_x`
 * (its own scene base is the 64-aligned map-square corner, the server's origin
 * is `(zone - 6) * 8`) and then `pos >> 4`. That is the same conversion every
 * entity coordinate goes through, so getting it wrong here shows up as loot
 * landing a few tiles from the corpse rather than as anything louder.
 *
 * The header is the two base bytes and nothing else — real rev-230 carries a
 * level as well, which packetin.h deliberately drops (the mock is single-plane
 * per scene). It has to stay in step with the length that table declares:
 * writing a byte the client's framing does not expect makes the frame eat the
 * opcode of whatever follows, so the stream desyncs a packet later, nowhere
 * near here. `check_frame_length` is what catches that.
 *
 * Returns the `pos` byte for this tile, because the two are only correct
 * together.
 */
int
mock230_send_zone(
    struct Mock230Player* player,
    int tile_x,
    int tile_z)
{
    struct Mock230Server* srv = player->world;
    struct RSAreaBuf buf;
    int base_x = (tile_x & ~7) - mock230_scene_origin(srv->zone_x);
    int base_z = (tile_z & ~7) - mock230_scene_origin(srv->zone_z);

    open_packet(&buf, 8);
    rsab_p1(&buf, base_x);
    rsab_p1(&buf, base_z);
    flush(player, &buf, OP_UPDATE_ZONE_PARTIAL_FOLLOWS, 0);
    return ((tile_x & 7) << 4) | (tile_z & 7);
}

void
mock230_send_obj_add(
    struct Mock230Player* player,
    int pos,
    int obj_id,
    int count)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    rsab_p1(&buf, pos);
    rsab_p2(&buf, obj_id);
    /* The count is 16 bits on the wire; a bigger stack is drawn as its
     * thousands abbreviation by the client, which reads the count it was
     * given. Clamping is better than wrapping 65,536 coins to zero. */
    rsab_p2(&buf, count > 0xffff ? 0xffff : count);
    flush(player, &buf, OP_OBJ_ADD, 0);
}

void
mock230_send_obj_del(
    struct Mock230Player* player,
    int pos,
    int obj_id)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    rsab_p1(&buf, pos);
    rsab_p2(&buf, obj_id);
    flush(player, &buf, OP_OBJ_DEL, 0);
}

void
mock230_send_obj_count(
    struct Mock230Player* player,
    int pos,
    int obj_id,
    int old_count,
    int new_count)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    rsab_p1(&buf, pos);
    rsab_p2(&buf, obj_id);
    rsab_p2(&buf, old_count > 0xffff ? 0xffff : old_count);
    rsab_p2(&buf, new_count > 0xffff ? 0xffff : new_count);
    flush(player, &buf, OP_OBJ_COUNT, 0);
}

/* `info` packs the loc's shape and angle into one byte: (shape << 2) | angle.
 * The client unpacks it the same way for every loc packet, which is why LOC_DEL
 * carries it too — a tile can hold a wall and a scenery loc at once, and the
 * shape says which one is meant. */
void
mock230_send_loc_add_change(
    struct Mock230Player* player,
    int pos,
    int shape,
    int angle,
    int loc_id)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    rsab_p1(&buf, pos);
    rsab_p1(&buf, ((shape & 0x1f) << 2) | (angle & 3));
    rsab_p2(&buf, loc_id);
    flush(player, &buf, OP_LOC_ADD_CHANGE, 0);
}

void
mock230_send_loc_del(
    struct Mock230Player* player,
    int pos,
    int shape,
    int angle)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    rsab_p1(&buf, pos);
    rsab_p1(&buf, ((shape & 0x1f) << 2) | (angle & 3));
    flush(player, &buf, OP_LOC_DEL, 0);
}

/*
 * Is `other` someone `player`'s client should be tracking?
 *
 * The 5-bit signed deltas a new-player record carries reach -16..15, so the add
 * radius cannot exceed 15 tiles — beyond that the coordinate wraps and the
 * player appears on the wrong side of the observer. Removal uses the same
 * radius, so a player walking out is dropped rather than smeared against the
 * edge of the range.
 */
static int
player_in_view(
    const struct Mock230Player* player,
    const struct Mock230Player* other)
{
    int dx;
    int dz;

    if( !other->active || other == player || other->level != player->level )
        return 0;
    /* A player mid-handshake has no ciphers and no position worth reporting;
     * `place_dirty` is set by mock230_world_player_init, so the first tick after
     * login is the first tick they can be seen on. */
    if( !other->world )
        return 0;
    dx = other->x - player->x;
    dz = other->z - player->z;
    return dx >= -15 && dx <= 15 && dz >= -15 && dz <= 15;
}

/*
 * PLAYER_INFO: the local player, then everyone else this client can see.
 *
 * Three sections, in this order, and the client's reader depends on all three
 * being present even when empty:
 *
 *   1. the local player's movement (op 3 here means *teleport*, not remove);
 *   2. an 8-bit count and then that many *already-tracked* players, in the
 *      order this client last saw them — op 3 here means remove;
 *   3. players entering view, each an 11-bit pid + two 5-bit deltas, closed by
 *      the 2047 terminator.
 *
 * Then, byte-aligned, one extended block per entity that asked for one — **in
 * the order the bit section queued them**, which is why `queued[]` exists rather
 * than the encoder walking the tracked list a second time. Getting that order
 * wrong does not drop a field, it applies one player's appearance to another.
 *
 * The tracked list is rebuilt into `kept[]` as it is written and swapped in at
 * the end, because a player removed in section 2 must not be counted into the
 * order section 3 and the extended blocks index against.
 */
void
mock230_send_player_info(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;
    struct RSAreaBuf buf;
    int local_x = player->x - mock230_scene_origin(srv->zone_x);
    int local_z = player->z - mock230_scene_origin(srv->zone_z);
    int extended = player->masks != 0;
    /* Who gets an extended block, in bit-section order. `player` itself is
     * spelled as its own pointer rather than as a pid, because the local player
     * is 2047 to itself and 2047 is not a pool slot. */
    struct Mock230Player* queued[MOCK230_PLAYER_MAX + 1];
    int queued_new[MOCK230_PLAYER_MAX + 1];
    int queued_count = 0;
    int kept[MOCK230_PLAYER_MAX];
    int kept_count = 0;

    open_packet(&buf, 4096);
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

    if( extended )
    {
        queued_new[queued_count] = 0;
        queued[queued_count++] = player;
    }

    /* --- players this client is already tracking --- */
    rsab_pbit(&buf, 8, player->tracked_player_count);
    for( int i = 0; i < player->tracked_player_count; i++ )
    {
        int pid = player->tracked_players[i];
        struct Mock230Player* other = &srv->players[pid];
        int other_extended;

        if( !player_in_view(player, other) )
        {
            /* Op 3 on a tracked player is "remove". It is the one op that does
             * not keep the slot, so it must not go into `kept`. */
            rsab_pbit(&buf, 1, 1);
            rsab_pbit(&buf, 2, 3);
            player->player_tracked[pid] = 0;
            continue;
        }

        kept[kept_count++] = pid;
        other_extended = other->masks != 0;
        if( other->move_count == 2 )
        {
            rsab_pbit(&buf, 1, 1);
            rsab_pbit(&buf, 2, 2);
            rsab_pbit(&buf, 3, other->move_dirs[0]);
            rsab_pbit(&buf, 3, other->move_dirs[1]);
            rsab_pbit(&buf, 1, other_extended);
        }
        else if( other->move_count == 1 )
        {
            rsab_pbit(&buf, 1, 1);
            rsab_pbit(&buf, 2, 1);
            rsab_pbit(&buf, 3, other->move_dirs[0]);
            rsab_pbit(&buf, 1, other_extended);
        }
        else if( other_extended )
        {
            rsab_pbit(&buf, 1, 1);
            rsab_pbit(&buf, 2, 0);
        }
        else
        {
            rsab_pbit(&buf, 1, 0);
        }
        if( other_extended )
        {
            queued_new[queued_count] = 0;
            queued[queued_count++] = other;
        }
    }

    /*
     * --- players entering view ---
     *
     * A new record is unconditionally followed by an extended block carrying the
     * appearance: the client spawns a default-looking body on the pid alone and
     * has nothing else to replace it with.
     */
    for( int pid = 0; pid < srv->player_count; pid++ )
    {
        struct Mock230Player* other = &srv->players[pid];
        int dx;
        int dz;

        if( player->player_tracked[pid] || !player_in_view(player, other) )
            continue;

        dx = other->x - player->x;
        dz = other->z - player->z;
        rsab_pbit(&buf, 11, pid);
        rsab_pbit(&buf, 5, dx & 0x1f);
        rsab_pbit(&buf, 5, dz & 0x1f);
        rsab_pbit(&buf, 1, 1); /* jump: appear on the tile, do not glide to it */
        rsab_pbit(&buf, 1, 1); /* extended info follows — the appearance */

        queued_new[queued_count] = 1;
        queued[queued_count++] = other;
        player->player_tracked[pid] = 1;
        kept[kept_count++] = pid;
    }

    /* The terminator is not optional. Without it the client keeps reading
     * 11-bit ids out of whatever follows, which at best invents players and at
     * worst eats the extended-info section. */
    rsab_pbit(&buf, 11, MOCK230_PLAYER_TERMINATOR);
    rsab_bytes(&buf);

    /* --- extended info, byte aligned, in the order the bits queued it --- */
    for( int i = 0; i < queued_count; i++ )
        put_player_extended(&buf, queued[i], queued_new[i]);

    flush(player, &buf, OP_PLAYER_INFO, 2);
    player->place_dirty = 0;

    memcpy(player->tracked_players, kept, sizeof(int) * (size_t)kept_count);
    player->tracked_player_count = kept_count;
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
mock230_send_npc_info(struct Mock230Player* player)
{
    /*
     * `tracked` is the *player's* list: which npcs this client holds, and in
     * what order, is a fact about the client. It was the world's while the pool
     * held one, which encoded the first player's npc set — deltas and all — for
     * whoever the packet was addressed to.
     */
    struct Mock230Server* srv = player->world;
    struct RSAreaBuf buf;
    /* Extended blocks are appended in the order the bit section queued them,
     * so remember that order while writing the bits. */
    int queued[MOCK230_NPC_MAX];
    int queued_count = 0;
    int kept[MOCK230_NPC_MAX];
    int kept_count = 0;

    open_packet(&buf, 8192);
    rsab_bits(&buf);

    /* --- tracked npcs, in the client's list order --- */
    rsab_pbit(&buf, 8, player->tracked_count);
    for( int i = 0; i < player->tracked_count; i++ )
    {
        int slot = player->tracked[i];
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
            player->npc_tracked[slot] = 0;
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
        if( !npc->active || player->npc_tracked[slot] )
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
        player->npc_tracked[slot] = 1;
        kept[kept_count++] = slot;
    }

    rsab_pbit(&buf, MOCK230_NPC_SLOT_BITS, MOCK230_NPC_TERMINATOR);
    rsab_bytes(&buf);

    for( int i = 0; i < queued_count; i++ )
        put_npc_extended(&buf, &srv->npcs[queued[i]]);

    flush(player, &buf, OP_NPC_INFO, 2);

    memcpy(player->tracked, kept, sizeof(int) * (size_t)kept_count);
    player->tracked_count = kept_count;
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
