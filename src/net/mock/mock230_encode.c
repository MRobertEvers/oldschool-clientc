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
#include "mock230_ids.h"
#include "mock230_mapinstance.h"
#include "mock230_session.h"
#include "mock239_runclientscript.h"
#include "mock239_appearance.h"
#include "mock239_facing.h"
#include "mock239_playerinfo.h"

#include "net/isaac.h"
#include "net/jbase37.h"
#include "net/wordpack.h"

/* The client's framing table, for the length check in mock230_send. */
#include "net/mock/mock230_scene.h"
#include "net/rev/osrs230/packetin.h"
/* The appearance vocabulary and every wire spelling of it — the writers below
 * choose an encoding, never a tag. */
#include "net/rev/packets/pkt_player_appearance.h"

#include "ss_trigger.h"

#include <rsareabuf.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int v5_face_from_classic(
    struct Mock239Face* face,
    uint32_t classic,
    uint32_t entity_mask,
    uint32_t coord_mask,
    int face_entity,
    int face_x,
    int face_z,
    int force_latch);

/*
 * The packets this server can send, as CANONICAL names rather than wire
 * opcodes.
 *
 * These used to be literal rev-230 opcodes, one `enum` of 50 numbers, and that
 * was a second copy of what `src/net/rev/osrs230/packetin.h` already said. The
 * numbers now come from the wire adapter (mock230_wire.h) at send time, which
 * is what lets one build speak either revision -- and, less obviously, what
 * removes the chance of the two copies disagreeing.
 *
 * Every call site below is unchanged. `mock230_send` takes what used to be an
 * opcode and is now a name, resolves it through `world->wire`, and records the
 * RESOLVED opcode in the packet capture -- so the selftest's assertions, which
 * are written against wire numbers like 120 and 108, still mean what they
 * meant, and still pass unchanged at revision 230. That is the property that
 * makes this refactor checkable rather than merely plausible.
 */
enum
{
    OP_SET_MAP_FLAG = PKT_NAME_UNSET_MAP_FLAG,
    OP_CHAT_FILTER_SETTINGS = PKT_NAME_CHAT_FILTER_SETTINGS,
    OP_IF_OPENSUB = PKT_NAME_IF_OPENSUB,
    OP_FRIENDLIST_LOADED = PKT_NAME_FRIENDLIST_LOADED,
    OP_UPDATE_IGNORELIST = PKT_NAME_UPDATE_IGNORELIST,
    OP_MESSAGE_PRIVATE = PKT_NAME_MESSAGE_PRIVATE,
    OP_UPDATE_FRIENDLIST = PKT_NAME_UPDATE_FRIENDLIST,
    OP_UPDATE_INV_FULL = PKT_NAME_UPDATE_INV_FULL,
    OP_PLAYER_INFO = PKT_NAME_PLAYER_INFO,
    OP_UPDATE_RUNWEIGHT = PKT_NAME_UPDATE_RUNWEIGHT,
    OP_VARP_SMALL = PKT_NAME_VARP_SMALL,
    OP_UPDATE_INV_PARTIAL = PKT_NAME_UPDATE_INV_PARTIAL,
    OP_IF_SETEVENTS = PKT_NAME_IF_SETEVENTS,
    OP_IF_SETTEXT = PKT_NAME_IF_SETTEXT,
    OP_IF_SETNPCHEAD = PKT_NAME_IF_SETNPCHEAD,
    OP_IF_SETPLAYERHEAD = PKT_NAME_IF_SETPLAYERHEAD,
    OP_IF_SETANIM = PKT_NAME_IF_SETANIM,
    OP_IF_SETCOLOUR = PKT_NAME_IF_SETCOLOUR,
    OP_IF_SETHIDE = PKT_NAME_IF_SETHIDE,
    OP_IF_SETMODEL = PKT_NAME_IF_SETMODEL,
    OP_IF_SETOBJECT = PKT_NAME_IF_SETOBJECT,
    OP_IF_SETPOSITION = PKT_NAME_IF_SETPOSITION,
    OP_IF_SETSCROLLPOS = PKT_NAME_IF_SETSCROLLPOS,
    OP_IF_SETROTATESPEED = PKT_NAME_IF_SETROTATESPEED,
    OP_IF_SETANGLE = PKT_NAME_IF_SETANGLE,
    OP_IF_SETNPCHEAD_ACTIVE = PKT_NAME_IF_SETNPCHEAD_ACTIVE,
    OP_IF_SETPLAYERMODEL_BASECOLOUR = PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR,
    OP_IF_SETPLAYERMODEL_BODYTYPE = PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE,
    OP_IF_SETPLAYERMODEL_OBJ = PKT_NAME_IF_SETPLAYERMODEL_OBJ,
    OP_IF_SETPLAYERMODEL_SELF = PKT_NAME_IF_SETPLAYERMODEL_SELF,
    OP_IF_CLOSESUB = PKT_NAME_IF_CLOSESUB,
    OP_IF_MOVESUB = PKT_NAME_IF_MOVESUB,
    OP_IF_RESYNC_V2 = PKT_NAME_IF_RESYNC_V2,
    OP_IF_CLEARINV = PKT_NAME_IF_CLEARINV,
    OP_UPDATE_INV_STOP_TRANSMIT = PKT_NAME_UPDATE_INV_STOP_TRANSMIT,
    OP_RUNCLIENTSCRIPT = PKT_NAME_RUNCLIENTSCRIPT,
    OP_P_COUNTDIALOG = PKT_NAME_P_COUNTDIALOG,
    OP_IF_OPENTOP = PKT_NAME_IF_OPENTOP,
    OP_REBUILD_NORMAL = PKT_NAME_REBUILD_NORMAL,
    OP_REBUILD_REGION = PKT_NAME_REBUILD_REGION,
    OP_UPDATE_RUNENERGY = PKT_NAME_UPDATE_RUNENERGY,
    OP_MESSAGE_GAME = PKT_NAME_MESSAGE_GAME,
    OP_NPC_INFO = PKT_NAME_NPC_INFO,
    OP_SET_NPC_UPDATE_ORIGIN = PKT_NAME_SET_NPC_UPDATE_ORIGIN,
    OP_SERVER_TICK_END = PKT_NAME_SERVER_TICK_END,
    OP_UPDATE_STAT = PKT_NAME_UPDATE_STAT,
    OP_UPDATE_PID = PKT_NAME_UPDATE_PID,
    OP_VARP_LARGE = PKT_NAME_VARP_LARGE,
    OP_UPDATE_ZONE_PARTIAL_FOLLOWS = PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS,
    OP_UPDATE_ZONE_FULL_FOLLOWS = PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS,
    OP_UPDATE_ZONE_PARTIAL_ENCLOSED = PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED,
    OP_LOC_ADD_CHANGE = PKT_NAME_LOC_ADD_CHANGE,
    OP_LOC_DEL = PKT_NAME_LOC_DEL,
    OP_LOC_ANIM = PKT_NAME_LOC_ANIM,
    OP_LOC_MERGE = PKT_NAME_LOC_MERGE,
    OP_OBJ_ADD = PKT_NAME_OBJ_ADD,
    OP_OBJ_DEL = PKT_NAME_OBJ_DEL,
    OP_OBJ_COUNT = PKT_NAME_OBJ_COUNT,
    OP_MAP_PROJANIM = PKT_NAME_MAP_PROJANIM,
    OP_MAP_ANIM = PKT_NAME_MAP_ANIM,
    OP_SET_PLAYER_OP = PKT_NAME_SET_PLAYER_OP,
    OP_CAM_RESET = PKT_NAME_CAM_RESET,
    OP_CAM_MOVETO = PKT_NAME_CAM_MOVETO,
    OP_CAM_LOOKAT = PKT_NAME_CAM_LOOKAT,
    OP_CAM_SHAKE = PKT_NAME_CAM_SHAKE,
    OP_SYNTH_SOUND = PKT_NAME_SYNTH_SOUND,
    OP_MIDI_SONG = PKT_NAME_MIDI_SONG,
    OP_TRIGGER_ONDIALOGABORT = PKT_NAME_TRIGGER_ONDIALOGABORT,
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

/*
 * The old `opcode_name` switch lived here: a second table of the same 50
 * packets, keyed on wire opcode. It went with the opcodes -- a name for a
 * canonical packet is `mock230_wire_pkt_name`, and there is one of those.
 */

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
    const struct Mock230Wire* wire,
    int pkt_name,
    int opcode,
    int len,
    int var)
{
    int framed;
    /* Through the wire adapter rather than the 230 table directly: the whole
     * point of the check is "does what this encoder wrote match what THIS
     * client frames", and which client that is now depends on the revision. */
    int expect;

    framed = wire->payload_size ? wire->payload_size(opcode) : 0;
    expect = framed == PKTIN_LENGTH_VARU8 ? 1 : framed == PKTIN_LENGTH_VARU16 ? 2 : 0;

    /*
     * The LENGTH CLASS is checked before the length, and it is the check that
     * matters more.
     *
     * A fixed packet written at the wrong size costs one packet: the client
     * reads the declared number of bytes either way and the next opcode is
     * still on a boundary. Writing the wrong CLASS costs the connection --
     * a one-byte length where the client reads two (or none) leaves every
     * subsequent byte offset, so the client reads payload as opcodes until it
     * hits something fatal and drops the socket. Nothing about that failure
     * points back at the packet that caused it, and the packet itself was
     * perfectly formed.
     *
     * This used to `return` whenever `var != 0`, which is to say it checked
     * only the case that cannot desynchronise a stream and skipped the one
     * that can.
     */
    if( expect != var )
    {
        static char const* const k_class[] = { "fixed", "var-u8", "var-u16" };

        fprintf(stderr, "mock230: %s op %d (%s) sent as %s, client frames it as %s\n",
                wire->name, opcode, mock230_wire_pkt_name(pkt_name),
                k_class[var < 0 || var > 2 ? 0 : var], k_class[expect]);
        return;
    }
    if( var == 0 && framed >= 0 && framed != len )
        fprintf(stderr,
                "mock230: %s op %d (%s) wrote %d bytes, client frames it as %d\n",
                wire->name, opcode, mock230_wire_pkt_name(pkt_name), len, framed);
}

void
mock230_send(
    struct Mock230Player* player,
    int pkt_name,
    const uint8_t* payload,
    int len,
    int var)
{
    uint8_t frame[64 * 1024];
    struct RSAreaBuf buf;
    struct Mock230Server* srv;
    const struct Mock230Wire* wire;
    int opcode;

    /* A packet is addressed to a player, and every encoder above this now says
     * so. What is left of the old "send to the server's one player" shape is
     * this line: the capture is a property of the world, because a test asserts
     * on what the *server* emitted, not on what one client received. */
    if( !player )
        return;
    srv = player->world;

    /*
     * Resolve the canonical name to this world's revision.
     *
     * Two ways this returns "no": the revision has no such packet at all
     * (there is no UPDATE_PID at 239), or its payload has not been transcribed
     * for this revision. Both drop the packet and report once. Dropping is the
     * only answer that cannot corrupt: a packet written with another
     * revision's layout frames correctly, passes the length check below, and
     * arrives meaning something else.
     */
    wire = (srv && srv->wire) ? srv->wire : mock230_wire_default();
    opcode = mock230_wire_opcode(wire, pkt_name);
    if( opcode < 0 || !mock230_wire_can_write(wire, pkt_name) )
        return;

    check_frame_length(wire, pkt_name, opcode, len, var);

    /*
     * MOCK230_TRACE_OUT=1 -- one line per packet, opcode and the revision's own
     * name for it.
     *
     * The only view of the stream that exists. The client is obfuscated: when
     * it dies it prints a ring of recent opcodes and a stack of one-letter
     * method names, and matching that ring against what was actually sent is
     * the whole diagnosis. Named from the revision's table (`prot_name`) rather
     * than the canonical enum so a line can be grepped straight into RSProt.
     *
     * Guarded by an env lookup cached in a static: this is on the path of every
     * packet of every tick, and `getenv` per packet is measurable.
     */
    {
        static int trace = -1;

        if( trace < 0 )
        {
            char const* v = getenv("MOCK230_TRACE_OUT");
            trace = (v && *v && *v != '0') ? (*v == '2' ? 2 : 1) : 0;
        }
        if( trace )
        {
            char const* prot = wire->prot_name ? wire->prot_name(opcode) : NULL;

            fprintf(stderr, "mock230: -> op %3d %-28s %d byte(s)", opcode,
                    prot ? prot : "?", len);
            /* MOCK230_TRACE_OUT=2 adds the body. Capped because PLAYER_INFO's
             * init block is 4608 bytes and would bury everything around it. */
            if( trace > 1 )
            {
                int const cap = len < 48 ? len : 48;

                for( int i = 0; i < cap; i++ )
                    fprintf(stderr, " %02x", payload[i]);
                if( cap < len )
                    fprintf(stderr, " ...");
            }
            fprintf(stderr, "\n");
        }
    }

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
    /*
     * pSmart1Or2: a revision whose opcodes reach 0x80 writes the high ones as
     * two bytes, each stepped through the cipher separately. Revision 239's
     * reach 148. Writing such an opcode as one byte does not lose the packet —
     * the client reads the truncated value as some other opcode and the whole
     * stream is gone from there on.
     */
    if( wire->opcode_smart2 && opcode >= 0x80 )
    {
        rsab_p1(&buf,
                (((opcode >> 8) | 0x80) + isaac_next(player->session->cipher_out)) & 0xff);
        rsab_p1(&buf, ((opcode & 0xff) + isaac_next(player->session->cipher_out)) & 0xff);
    }
    else
    {
        rsab_p1(&buf, (opcode + isaac_next(player->session->cipher_out)) & 0xff);
    }
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
            mock230_wire_pkt_name(pkt_name),
            opcode,
            len);
}


/*
 * The revision's payload writer set, or NULL for "whatever this file has
 * always written".
 *
 * Every encoder that has a per-revision writer branches on this. The branch is
 * explicit rather than a dispatch table because there are ten of them and
 * because the 230 arm is the readable statement of what the mock's own client
 * expects — hiding it behind a pointer would leave that layout written nowhere.
 */
static const struct Mock230WirePayload*
wire_payload(struct Mock230Player* player)
{
    struct Mock230Server* srv = player ? player->world : NULL;
    const struct Mock230Wire* wire =
        (srv && srv->wire) ? srv->wire : mock230_wire_default();
    return wire->payload;
}

/** The wire adapter behind a player, or the default when the world has none. */
static const struct Mock230Wire*
wire_for(struct Mock230Player* player)
{
    struct Mock230Server* srv = player ? player->world : NULL;
    return (srv && srv->wire) ? srv->wire : mock230_wire_default();
}

/** Does this player's world speak the v5 entity streams? */
static int
wire_is_v5(struct Mock230Player* player)
{
    struct Mock230Server* srv = player ? player->world : NULL;
    const struct Mock230Wire* wire =
        (srv && srv->wire) ? srv->wire : mock230_wire_default();
    return wire->revision >= 239;
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

    open_packet(&buf, 8192);

    /*
     * At revision 239 the LOGIN rebuild carries the GPI init block first, ahead
     * of its own fields — RSProt's RebuildLogin variant.
     *
     * This is what seeds the client's 2048-slot player table: the local
     * player's absolute coord and a rough position for every other index.
     * Without it PLAYER_INFO is not merely incomplete, it is unreadable — the
     * client's high-resolution count is zero, so the first section's bits are
     * read as the second's and the stream decodes as noise.
     *
     * `player_tracked[pid]` is the server's own "has this client been told
     * about pid" flag, and the local player's own entry is what distinguishes
     * the login rebuild from a later one. A rebuild after login must NOT repeat
     * the block: the client would re-seed a table it is already tracking
     * against, and every player in it would jump.
     */
    if( wire_is_v5(player) && !player->player_tracked[player->pid] )
    {
        int32_t coord = (int32_t)(((player->level & 0x3) << 28) |
                                  ((player->x & 0x3fff) << 14) | (player->z & 0x3fff));
        mock239_playerinfo_write_init(&buf, mock230_wire_player_index(player->pid),
                                      coord);
        player->player_tracked[player->pid] = 1;
        /* The init block resets the client's cycle bits, so the next
         * PLAYER_INFO must place the crowd in section 4 again — and it stated
         * the absolute position, so the next delta is measured from there. */
        player->v5_playerinfo_sent = 0;
        player->v5_last_x = player->x;
        player->v5_last_z = player->z;
        player->v5_last_level = player->level;
    }

    /* RSProt RebuildNormalEncoder: worldArea, zoneX (p2Alt2), zoneZ, keyCount,
     * then keyCount * 4 XTEA ints. Zero keys: unencrypted regions load, and
     * this cache ships its keys client-side via xteas.json. */
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->rebuild_normal )
        {
            /* V2 carries no key block at all — OldSchool stores map archives
             * in the clear from revision 237 — so the writer is handed the
             * square count and ignores it, rather than this branch quietly
             * omitting a field. */
            pl->rebuild_normal(&buf, 0, srv->zone_x, srv->zone_z, NULL, count);
        }
        else
        {
            rsab_p2(&buf, 0);
            rsab_p2_alt2(&buf, srv->zone_x);
            rsab_p2(&buf, srv->zone_z);
            rsab_p2(&buf, count);
            for( int i = 0; i < count * 4; i++ )
                rsab_p4(&buf, 0);
        }
    }

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

/*
 * REBUILD_REGION — the instanced scene.
 *
 * The same header as REBUILD_NORMAL, then a 4 x 13 x 13 grid of template-chunk
 * descriptors, then the keys. One bit per destination zone says whether it has a
 * source; when it does, 26 bits say which:
 *
 *     bits  1..2   rotation, quarter-turns clockwise
 *     bits  3..13  source zone z   (11 bits)
 *     bits 14..23  source zone x   (10 bits)
 *     bits 24..25  source plane    (2 bits)
 *
 * That layout is the client's own, not this server's invention — it is what
 * every OSRS-era client reads out of `instanceTemplateChunks` (`rotation = z >> 1
 * & 0x3`, `chunkY = z >> 3 & 0x7FF`, `chunkX = z >> 14 & 0x3FF`, `plane = z >> 24
 * & 0x3`), and 2009scape's `BuildDynamicScene` and Kronos's `sendRegion` both
 * write exactly it.
 *
 * Note the asymmetry the 10-bit source-x field creates: a *source* zone must
 * have x < 1024, i.e. map square x < 128. Destinations are the loop position and
 * carry no such limit, which is why the instance pool can sit at map x >= 100
 * while every source it copies from is real map (all of this cache's squares are
 * within x 15..98).
 *
 * Keys are zeros, for the same reason REBUILD_NORMAL's are: this client reads
 * its XTEA keys from xteas.json beside the cache.
 */
void
mock230_send_rebuild_region(struct Mock230Player* player)
{
    struct Mock230Server* srv = player->world;
    struct RSAreaBuf buf;
    struct Mock230MapInstanceWindow window;
    int key_count = 0;

    mock230_mapinstance_window(srv->zone_x, srv->zone_z, &window);

    open_packet(&buf, 8192);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->rebuild_region )
        {
            /* V2 has no worldArea field and adds `reload`; zoneZ comes first. */
            pl->rebuild_region(&buf, srv->zone_x, srv->zone_z, 0);
        }
        else
        {
            rsab_p2(&buf, 0);
            rsab_p2_alt2(&buf, srv->zone_x);
            rsab_p2(&buf, srv->zone_z);
        }
    }

/*
     * The distinct source map squares.
     *
     * Counted before the grid rather than after it, because revision 239 wants
     * the number HERE -- `encodeRegionV2` back-patches a p2 in front of the bit
     * buffer -- while the classic packet writes it afterwards as the length of
     * a trailing XTEA key block. Same number, two different places, and V2 has
     * no key block at all (map archives are stored plain from 237).
     *
     * Getting this wrong is what logs a client out: the grid is read as though
     * the count were part of it, so every zone descriptor after the first is
     * shifted and the packet ends somewhere the client does not expect.
     */
    /* One key block per source square the descriptors name, which is what the
     * client would need if it were taking keys off the wire. Counted from the
     * window so the two never disagree about how many follow. */
    {
        int seen_x[MOCK230_MAPINSTANCE_LEVELS * MOCK230_MAPINSTANCE_SCENE_ZONES *
                   MOCK230_MAPINSTANCE_SCENE_ZONES];
        int seen_z[sizeof(seen_x) / sizeof(*seen_x)];

        for( int level = 0; level < MOCK230_MAPINSTANCE_LEVELS; level++ )
            for( int zx = 0; zx < MOCK230_MAPINSTANCE_SCENE_ZONES; zx++ )
                for( int zz = 0; zz < MOCK230_MAPINSTANCE_SCENE_ZONES; zz++ )
                {
                    const struct Mock230MapInstanceZone* zone = &window.zones[level][zx][zz];
                    int map_x;
                    int map_z;
                    int dup = 0;

                    if( !zone->set )
                        continue;
                    map_x = zone->src_zone_x >> 3;
                    map_z = zone->src_zone_z >> 3;
                    for( int i = 0; i < key_count && !dup; i++ )
                    {
                        if( seen_x[i] == map_x && seen_z[i] == map_z )
                            dup = 1;
                    }
                    if( dup )
                        continue;
                    seen_x[key_count] = map_x;
                    seen_z[key_count] = map_z;
                    key_count++;
                }
    }
    if( wire_is_v5(player) )
        rsab_p2(&buf, key_count);

    rsab_bits(&buf);
    for( int level = 0; level < MOCK230_MAPINSTANCE_LEVELS; level++ )
    {
        for( int zx = 0; zx < MOCK230_MAPINSTANCE_SCENE_ZONES; zx++ )
        {
            for( int zz = 0; zz < MOCK230_MAPINSTANCE_SCENE_ZONES; zz++ )
            {
                const struct Mock230MapInstanceZone* zone = &window.zones[level][zx][zz];

                if( !zone->set )
                {
                    rsab_pbit(&buf, 1, 0);
                    continue;
                }
                rsab_pbit(&buf, 1, 1);
                rsab_pbit(&buf, 26,
                          ((zone->rotation & 3) << 1) | ((zone->src_zone_z & 0x7ff) << 3) |
                              ((zone->src_zone_x & 0x3ff) << 14) |
                              ((zone->src_level & 3) << 24));
            }
        }
    }
    rsab_bytes(&buf);

    if( !wire_is_v5(player) )
    {
        rsab_p2(&buf, key_count);
        for( int i = 0; i < key_count * 4; i++ )
            rsab_p4(&buf, 0);
    }

    flush(player, &buf, OP_REBUILD_REGION, 2);
    if( srv->verbose )
        fprintf(
            stderr,
            "mock230: rebuild region zone=%d,%d source zones=%d squares=%d\n",
            srv->zone_x,
            srv->zone_z,
            window.set_count,
            key_count);
}

/*
 * Which rebuild the player is owed.
 *
 * The choice is made from where the player *is* rather than from a flag, because
 * that is the one thing that cannot go stale: an instance is a region of
 * coordinate space, and standing in it is what makes the scene instanced. Every
 * caller wants this rather than either encoder directly.
 */
void
mock230_send_rebuild(struct Mock230Player* player)
{
    if( mock230_mapinstance_find(player->x, player->z) != 0 )
        mock230_send_rebuild_region(player);
    else
        mock230_send_rebuild_normal(player);
}

/* ------------------------------------------------------------------ */
/* Interfaces                                                          */
/* ------------------------------------------------------------------ */

/*
 * Content often names `toplevel_osrs_stretch:sidemodal` / `:xp_drops` (etc.)
 * even after Display has remounted Fixed/Modern. Those are role aliases for
 * the live gameframe's matching slot — rewrite by the `:role` suffix. No list
 * of tops, no numeric ids.
 *
 * Modal slots use the uids if_opentop already bound on the player (also used
 * for modal-mount bookkeeping). Other HUD roles resolve `<live_top>:<role>`
 * through the component pack, but only when the *source* interface is itself
 * a gameframe top (has `:mainmodal`) — so nested panels like `orbs:xp_drops`
 * are never rewritten onto the HUD slot.
 *
 * Only rewrite when the named component lives on a *different* interface than
 * the session's live top; same-top spellings are already correct.
 */
static int
mock230_remap_gameframe_slot_uid(
    struct Mock230Player* player,
    int uid)
{
    const char* name;
    const char* colon;
    const char* role;
    const char* src_iface_name;
    const char* live_iface_name;
    char probe[128];
    int live;
    int live_iface;
    int src_iface;

    assert(player);
    if( uid <= 0 )
        return uid;
    live_iface = mock230_player_gameframe_iface(player);
    if( live_iface > 0 && MOCK230_COM_GROUP(uid) == live_iface )
        return uid;
    name = mock230_content_symbol_name(MOCK230_PACK_COMPONENT, uid);
    if( !name )
        return uid;
    colon = strrchr(name, ':');
    if( !colon || colon[1] == '\0' )
        return uid;
    role = colon + 1;
    if( strcmp(role, "mainmodal") == 0 )
        live = mock230_player_mainmodal(player);
    else if( strcmp(role, "sidemodal") == 0 )
        live = mock230_player_sidemodal(player);
    else if( strcmp(role, "floater") == 0 )
        live = mock230_player_floater(player);
    else
    {
        src_iface = MOCK230_COM_GROUP(uid);
        src_iface_name = mock230_content_symbol_name(MOCK230_PACK_INTERFACE, src_iface);
        live_iface_name = mock230_content_symbol_name(MOCK230_PACK_INTERFACE, live_iface);
        if( !src_iface_name || !live_iface_name || live_iface <= 0 )
            return uid;
        snprintf(probe, sizeof(probe), "%s:mainmodal", src_iface_name);
        if( mock230_content_symbol(MOCK230_PACK_COMPONENT, probe) <= 0 )
            return uid;
        snprintf(probe, sizeof(probe), "%s:%s", live_iface_name, role);
        live = mock230_content_symbol(MOCK230_PACK_COMPONENT, probe);
    }
    return live > 0 ? live : uid;
}

void
mock230_send_if_opentop(
    struct Mock230Player* player,
    int group)
{
    struct RSAreaBuf buf;

    /* Mutate authority before the packet is observable. A resync built from a
     * send-after-the-fact cache can otherwise preserve mounts the new root has
     * already destroyed in the golden client. */
    mock230_ifstate_open_top(&player->interfaces, group);
    open_packet(&buf, 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_opentop )
            pl->if_opentop(&buf, group);
        else
            rsab_p2_alt1(&buf, group);
    }
    flush(player, &buf, OP_IF_OPENTOP, 0);
}

void
mock230_send_if_movesub(
    struct Mock230Player* player,
    int source_uid,
    int dest_uid)
{
    struct RSAreaBuf buf;
    source_uid = mock230_remap_gameframe_slot_uid(player, source_uid);
    dest_uid = mock230_remap_gameframe_slot_uid(player, dest_uid);
    mock230_ifstate_move_sub(&player->interfaces, source_uid, dest_uid);
    open_packet(&buf, 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_movesub )
            pl->if_movesub(&buf, source_uid, dest_uid);
        else
        {
            /* Rev 230 IfMoveSubEncoder: destination then source, both p4Alt1. */
            rsab_p4_alt1(&buf, dest_uid);
            rsab_p4_alt1(&buf, source_uid);
        }
    }
    flush(player, &buf, OP_IF_MOVESUB, 0);
}

static void
mock230_gameframe_bind_slots(
    struct Mock230Player* player,
    int group,
    const char* top_name)
{
    char name[128];
    int uid;

    assert(player);
    assert(top_name);
    player->gameframe_iface = group;

    snprintf(name, sizeof(name), "%s:mainmodal", top_name);
    uid = mock230_content_symbol(MOCK230_PACK_COMPONENT, name);
    player->gameframe_mainmodal = uid;

    snprintf(name, sizeof(name), "%s:sidemodal", top_name);
    uid = mock230_content_symbol(MOCK230_PACK_COMPONENT, name);
    player->gameframe_sidemodal = uid;

    snprintf(name, sizeof(name), "%s:floater", top_name);
    uid = mock230_content_symbol(MOCK230_PACK_COMPONENT, name);
    player->gameframe_floater = uid;
}

void
mock230_gameframe_opentop(
    struct Mock230Player* player,
    int group)
{
    const char* top_name;
    const struct Mock230EnumDef* frame;
    const struct Mock230Ids* ids = mock230_ids();

    assert(player);
    top_name = mock230_content_symbol_name(MOCK230_PACK_INTERFACE, group);
    if( !top_name )
    {
        fprintf(stderr, "mock230: if_opentop group=%d has no pack name\n", group);
        return;
    }

    mock230_send_if_opentop(player, group);
    mock230_gameframe_bind_slots(player, group, top_name);

    /* Keep the static ids table's "current stretch" aliases pointed at the
     * live top so C call sites that still read ids->com_gameframe_mainmodal
     * see the right slots after a switch. */
    if( ids )
    {
        /* iface_gameframe stays the stretch default for selftests that pin
         * login; session state is player->gameframe_*. */
        (void)ids;
    }

    frame = mock230_content_enum(top_name);
    if( !frame || frame->count == 0 )
    {
        fprintf(stderr,
                "mock230: no `%s` gameframe enum — HUD/tabs will be empty\n",
                top_name);
        mock230_send_if_resync_v2(player);
        return;
    }
    for( int i = 0; i < frame->count; i++ )
        mock230_send_if_opensub(
            player,
            group,
            frame->values[i].key & 0xffff,
            frame->values[i].value,
            1);
    /* The V2 snapshot is the authoritative commit for this root. It also
     * removes stale client-side mounts/event ranges left by an interrupted
     * layout switch. Revision 230's sender is deliberately a no-op. */
    mock230_send_if_resync_v2(player);
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
    int uid = mock230_remap_gameframe_slot_uid(player, (parent << 16) | (child & 0xffff));

    parent = (uid >> 16) & 0xffff;
    child = uid & 0xffff;

    if( !mock230_ifstate_open_sub(&player->interfaces, uid, group, type) )
    {
        fprintf(stderr,
                "mock230: cannot register IF_OPENSUB %d:%d <- %d type %d\n",
                parent, child, group, type);
        /* Revision 239 must never send a mutation its later IF_RESYNC_V2
         * cannot reproduce. The classic wire has no V2 registry contract. */
        if( wire_is_v5(player) )
            return;
    }

    open_packet(&buf, 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_opensub )
            pl->if_opensub(&buf, group, uid, type);
        else
        {
            rsab_p1(&buf, type);
            rsab_p2_alt2(&buf, group);
            rsab_p4_alt3(&buf, uid);
        }
    }
    flush(player, &buf, OP_IF_OPENSUB, 0);
    mock230_note_modal_mount(srv, uid, group);
    /* OpenRune's onIfOpen: nested fills (e.g. side_journal → tab body) run
     * here so their IF_OPENSUB is encoded immediately after the parent's on
     * the wire. Subject is the interface id, same shape as IF_CLOSE. */
    if( group > 0 && srv && srv->scripts )
        mock230_scripts_run_trigger(srv, SS_TRIGGER_IF_OPEN, group, -1, -1);
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
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->run_clientscript )
            pl->run_clientscript(&buf, script_id, types, intv, strv, argc);
        else
        {
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
        }
    }
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

int
mock230_send_run_clientscript_typed(
    struct Mock230Player* player,
    int script_id,
    const char* types,
    const struct Mock239ClientScriptArg* args,
    int argc)
{
    struct RSAreaBuf buf;

    if( !player || !wire_is_v5(player) )
        return 0;

    /* RUNCLIENTSCRIPT is VAR_SHORT, so 65535 is the protocol ceiling. Leave
     * headroom in the packet arena for ordinary adjacent server work; an
     * argument set larger than this fails closed through rsab_ok(). */
    open_packet(&buf, 60 * 1024);
    if( !mock239_encode_runclientscript(&buf, script_id, types, args, argc) )
    {
        fprintf(stderr, "mock230: refused invalid rev239 RUNCLIENTSCRIPT %d\n", script_id);
        return 0;
    }
    flush(player, &buf, OP_RUNCLIENTSCRIPT, 2);
    return 1;
}

void
mock230_send_if_setevents(
    struct Mock230Player* player,
    int uid,
    int from,
    int to,
    int events)
{
    uint32_t events1;
    uint32_t events2;

    mock230_ifstate_split_classic_events((uint32_t)events, &events1, &events2);
    mock230_send_if_setevents_v2(player, uid, from, to, events1, events2);
}

void
mock230_send_if_setevents_v2(
    struct Mock230Player* player,
    int uid,
    int from,
    int to,
    uint32_t events1,
    uint32_t events2)
{
    /* RSProt IfSetEventsEncoder: p4Alt3 combinedId, p2Alt2 start, p4Alt1
     * events, p2 end. */
    struct RSAreaBuf buf;

    if( !mock230_ifstate_setevents_v2(
            &player->interfaces, uid, from, to, events1, events2) )
    {
        fprintf(stderr,
                "mock230: cannot register IF_SETEVENTS %d:%d range %d..%d\n",
                (uid >> 16) & 0xffff, uid & 0xffff, from, to);
        if( wire_is_v5(player) )
            return;
    }

    open_packet(&buf, 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_setevents )
            pl->if_setevents(&buf, uid, from, to, events1, events2);
        else
        {
            uint32_t classic = events1 | ((events2 & UINT32_C(0x3ff)) << 1);

            rsab_p4_alt3(&buf, uid);
            rsab_p2_alt2(&buf, from);
            rsab_p4_alt1(&buf, (int32_t)classic);
            rsab_p2(&buf, to);
        }
    }
    flush(player, &buf, OP_IF_SETEVENTS, 0);
}

void
mock230_send_if_resync_v2(struct Mock230Player* player)
{
    struct RSAreaBuf buf;
    size_t capacity;

    if( !wire_is_v5(player) )
        return;
    capacity = mock230_ifstate_resync_payload_size(&player->interfaces);
    if( capacity == 0 || capacity > MOCK230_IF_RESYNC_MAX_PAYLOAD )
    {
        fprintf(stderr, "mock230: invalid IF_RESYNC_V2 registry size %zu\n", capacity);
        return;
    }
    open_packet(&buf, capacity);
    mock230_ifstate_encode_resync_v2(&buf, &player->interfaces);
    flush(player, &buf, OP_IF_RESYNC_V2, 2);
}

void
mock230_send_if_clearinv(
    struct Mock230Player* player,
    int uid)
{
    struct RSAreaBuf buf;

    if( !wire_is_v5(player) )
        return;
    open_packet(&buf, 4);
    mock230_ifstate_encode_clearinv(&buf, uid);
    flush(player, &buf, OP_IF_CLEARINV, 0);
}

void
mock230_send_trigger_ondialogabort(struct Mock230Player* player)
{
    struct RSAreaBuf buf;

    if( !wire_is_v5(player) )
        return;
    open_packet(&buf, 1);
    flush(player, &buf, OP_TRIGGER_ONDIALOGABORT, 0);
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
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_settext )
            pl->if_settext(&buf, uid, text);
        else
        {
            rsab_p4(&buf, uid);
            rsab_pjstr(&buf, text ? text : "", RSAB_JSTR_NEWLINE);
        }
    }
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
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_setnpchead )
            pl->if_setnpchead(&buf, uid, npc_id);
        else
        {
            rsab_p4(&buf, uid);
            rsab_p2(&buf, npc_id);
        }
    }
    flush(player, &buf, OP_IF_SETNPCHEAD, 0);
}

void
mock230_send_if_setplayerhead(
    struct Mock230Player* player,
    int uid)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_setplayerhead )
            pl->if_setplayerhead(&buf, uid);
        else
        {
            rsab_p4(&buf, uid);
        }
    }
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
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_setanim )
            pl->if_setanim(&buf, uid, anim_id);
        else
        {
            rsab_p4(&buf, uid);
            rsab_p2(&buf, anim_id);
        }
    }
    flush(player, &buf, OP_IF_SETANIM, 0);
}

void
mock230_send_if_setcolour(
    struct Mock230Player* player,
    int uid,
    int colour)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_setcolour )
            pl->if_setcolour(&buf, uid, colour);
        else
        {
            rsab_p4(&buf, uid);
            rsab_p2(&buf, colour);
        }
    }
    flush(player, &buf, OP_IF_SETCOLOUR, 0);
}

void
mock230_send_if_sethide(
    struct Mock230Player* player,
    int uid,
    int hide)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_sethide )
            pl->if_sethide(&buf, uid, hide);
        else
        {
            rsab_p4(&buf, uid);
            rsab_p1(&buf, hide ? 1 : 0);
        }
    }
    flush(player, &buf, OP_IF_SETHIDE, 0);
}

void
mock230_send_if_setmodel(
    struct Mock230Player* player,
    int uid,
    int model_id)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_setmodel )
            pl->if_setmodel(&buf, uid, model_id);
        else
        {
            rsab_p4(&buf, uid);
            rsab_p2(&buf, model_id);
        }
    }
    flush(player, &buf, OP_IF_SETMODEL, 0);
}

void
mock230_send_if_setobject(
    struct Mock230Player* player,
    int uid,
    int obj_id,
    int value)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_setobject )
            pl->if_setobject(&buf, uid, obj_id, value);
        else
        {
            rsab_p4(&buf, uid);
            rsab_p2(&buf, obj_id);
            rsab_p4(&buf, value);
        }
    }
    flush(player, &buf, OP_IF_SETOBJECT, 0);
}

void
mock230_send_if_setposition(
    struct Mock230Player* player,
    int uid,
    int x,
    int y)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_setposition )
            pl->if_setposition(&buf, uid, x, y);
        else
        {
            rsab_p4(&buf, uid);
            rsab_p2(&buf, x);
            rsab_p2(&buf, y);
        }
    }
    flush(player, &buf, OP_IF_SETPOSITION, 0);
}

void
mock230_send_if_setscroll(
    struct Mock230Player* player,
    int uid,
    int position)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_setscroll )
            pl->if_setscroll(&buf, uid, position);
        else
        {
            rsab_p4(&buf, uid);
            rsab_p2(&buf, position);
        }
    }
    flush(player, &buf, OP_IF_SETSCROLLPOS, 0);
}

void
mock230_send_if_setrotatespeed(
    struct Mock230Player* player,
    int uid,
    int x_speed,
    int y_speed)
{
    struct RSAreaBuf buf;
    const struct Mock230WirePayload* pl = wire_payload(player);

    /* These seven packets do not exist in the hybrid revision-230 table.
     * Refuse rather than inventing a classic body and preserve that wire's
     * established behaviour exactly. */
    if( !pl || !pl->if_setrotatespeed )
        return;
    open_packet(&buf, 8);
    pl->if_setrotatespeed(&buf, uid, x_speed, y_speed);
    flush(player, &buf, OP_IF_SETROTATESPEED, 0);
}

void
mock230_send_if_setangle(
    struct Mock230Player* player,
    int uid,
    int zoom,
    int angle_x,
    int angle_y)
{
    struct RSAreaBuf buf;
    const struct Mock230WirePayload* pl = wire_payload(player);

    if( !pl || !pl->if_setangle )
        return;
    open_packet(&buf, 10);
    pl->if_setangle(&buf, uid, zoom, angle_x, angle_y);
    flush(player, &buf, OP_IF_SETANGLE, 0);
}

void
mock230_send_if_setnpchead_active(
    struct Mock230Player* player,
    int uid,
    int index)
{
    struct RSAreaBuf buf;
    const struct Mock230WirePayload* pl = wire_payload(player);

    if( !pl || !pl->if_setnpchead_active )
        return;
    open_packet(&buf, 6);
    pl->if_setnpchead_active(&buf, uid, index);
    flush(player, &buf, OP_IF_SETNPCHEAD_ACTIVE, 0);
}

void
mock230_send_if_setplayermodel_basecolour(
    struct Mock230Player* player,
    int uid,
    int index,
    int colour)
{
    struct RSAreaBuf buf;
    const struct Mock230WirePayload* pl = wire_payload(player);

    if( !pl || !pl->if_setplayermodel_basecolour )
        return;
    open_packet(&buf, 6);
    pl->if_setplayermodel_basecolour(&buf, uid, index, colour);
    flush(player, &buf, OP_IF_SETPLAYERMODEL_BASECOLOUR, 0);
}

void
mock230_send_if_setplayermodel_bodytype(
    struct Mock230Player* player,
    int uid,
    int body_type)
{
    struct RSAreaBuf buf;
    const struct Mock230WirePayload* pl = wire_payload(player);

    if( !pl || !pl->if_setplayermodel_bodytype )
        return;
    open_packet(&buf, 5);
    pl->if_setplayermodel_bodytype(&buf, uid, body_type);
    flush(player, &buf, OP_IF_SETPLAYERMODEL_BODYTYPE, 0);
}

void
mock230_send_if_setplayermodel_obj(
    struct Mock230Player* player,
    int uid,
    int obj_id)
{
    struct RSAreaBuf buf;
    const struct Mock230WirePayload* pl = wire_payload(player);

    if( !pl || !pl->if_setplayermodel_obj )
        return;
    open_packet(&buf, 8);
    pl->if_setplayermodel_obj(&buf, uid, obj_id);
    flush(player, &buf, OP_IF_SETPLAYERMODEL_OBJ, 0);
}

void
mock230_send_if_setplayermodel_self(
    struct Mock230Player* player,
    int uid,
    int copy_objs)
{
    struct RSAreaBuf buf;
    const struct Mock230WirePayload* pl = wire_payload(player);

    if( !pl || !pl->if_setplayermodel_self )
        return;
    open_packet(&buf, 5);
    pl->if_setplayermodel_self(&buf, uid, copy_objs);
    flush(player, &buf, OP_IF_SETPLAYERMODEL_SELF, 0);
}

void
mock230_send_if_closesub(
    struct Mock230Player* player,
    int uid)
{
    struct Mock230Server* srv = player->world;
    struct RSAreaBuf buf;

    uid = mock230_remap_gameframe_slot_uid(player, uid);
    mock230_ifstate_close_sub(&player->interfaces, uid);

    open_packet(&buf, 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->if_closesub )
            pl->if_closesub(&buf, uid);
        else
            rsab_p4(&buf, uid);
    }
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

    if( wire_is_v5(player) )
    {
        const char* strings[] = { "Enter amount:" };

        /* P_COUNTDIALOG was removed before rev239. The golden client exposes
         * the same prompt through clientscript 108 and returns the existing
         * RESUME_P_COUNTDIALOG packet (op 75). */
        mock230_send_run_clientscript_mixed(player, 108, "s", NULL, strings, 1);
        return;
    }

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
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->varp_small )
            pl->varp_small(&buf, id, value);
        else
        {
            rsab_p2(&buf, id);
            rsab_p1(&buf, value);
        }
    }
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
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->varp_large )
            pl->varp_large(&buf, id, value);
        else
        {
            rsab_p2(&buf, id);
            rsab_p4(&buf, value);
        }
    }
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
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->update_stat )
            pl->update_stat(&buf, stat, level, xp, boosted);
        else
        {
            rsab_p1(&buf, stat);
            rsab_p1(&buf, level);
            rsab_p4(&buf, xp);
            rsab_p1(&buf, boosted);
        }
    }
    flush(player, &buf, OP_UPDATE_STAT, 0);
}

/*
 * Which player index the client is.
 *
 * Its real pool slot. The client's fallback when this never arrives is 2047 —
 * the classic self index (`local_player_pid`, task_exec_entity_info.c) — and
 * the mock used to send that sentinel deliberately, which worked only while
 * there was one client: with two, "you are 2047" is true of both, so no
 * absolute reference to a player could name one. An npc's FACE_ENTITY is the
 * absolute reference that made it visible.
 *
 * Sent once at login, before the first PLAYER_INFO, so the client registers its
 * own entity under this pid rather than under the fallback.
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
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->update_runenergy )
            pl->update_runenergy(&buf, percent * 100);
        else
            rsab_p2(&buf, percent * 100);
    }
    flush(player, &buf, OP_UPDATE_RUNENERGY, 0);
}

void
mock230_send_cam_reset(struct Mock230Player* player)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 4);
    flush(player, &buf, OP_CAM_RESET, 0);
}

void
mock230_send_cam_moveto(
    struct Mock230Player* player,
    int world_x,
    int world_z,
    int height,
    int rate,
    int rate2)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        /* Absolute at 239, scene-local at 230 -- see mock230.h. */
        if( pl && pl->cam_moveto )
            pl->cam_moveto(&buf, world_x, world_z, height, rate, rate2);
        else
        {
            rsab_p1(&buf, world_x - mock230_scene_base_x());
            rsab_p1(&buf, world_z - mock230_scene_base_z());
            rsab_p2(&buf, height);
            rsab_p1(&buf, rate);
            rsab_p1(&buf, rate2);
        }
    }
    flush(player, &buf, OP_CAM_MOVETO, 0);
}

void
mock230_send_cam_lookat(
    struct Mock230Player* player,
    int world_x,
    int world_z,
    int height,
    int rate,
    int rate2)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        /* Absolute at 239, scene-local at 230 -- see mock230.h. */
        if( pl && pl->cam_lookat )
            pl->cam_lookat(&buf, world_x, world_z, height, rate, rate2);
        else
        {
            rsab_p1(&buf, world_x - mock230_scene_base_x());
            rsab_p1(&buf, world_z - mock230_scene_base_z());
            rsab_p2(&buf, height);
            rsab_p1(&buf, rate);
            rsab_p1(&buf, rate2);
        }
    }
    flush(player, &buf, OP_CAM_LOOKAT, 0);
}

void
mock230_send_cam_shake(
    struct Mock230Player* player,
    int axis,
    int jitter,
    int amplitude,
    int frequency)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->cam_shake )
            pl->cam_shake(&buf, axis, jitter, amplitude, frequency);
        else
        {
            rsab_p1(&buf, axis);
            rsab_p1(&buf, jitter);
            rsab_p1(&buf, amplitude);
            rsab_p1(&buf, frequency);
        }
    }
    flush(player, &buf, OP_CAM_SHAKE, 0);
}

/*
 * SYNTH_SOUND — WEAPON_FX.md §6. Field order is not a choice: it matches the
 * client's own reader (gameproto_parse.c:706-710) exactly — id g2, loops g1,
 * delay g2, 5 bytes total. lc254/packetin.h:157 and lc245_2/packetin.h:154
 * carry the same shape.
 */
void
mock230_send_synth_sound(
    struct Mock230Player* player,
    int id,
    int loops,
    int delay)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->synth_sound )
            pl->synth_sound(&buf, id, loops, delay);
        else
        {
            rsab_p2(&buf, id);
            rsab_p1(&buf, loops);
            rsab_p2(&buf, delay);
        }
    }
    flush(player, &buf, OP_SYNTH_SOUND, 0);
}

void
mock230_send_midi_song(
    struct Mock230Player* player,
    int id)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->midi_song )
        {
            /* Old MIDI_SONG semantics expressed through V2: immediate fade
             * out, 60-cycle fade, 60-cycle start delay, immediate fade in. */
            pl->midi_song(&buf, id, 0, 60, 60, 0);
        }
    }
    flush(player, &buf, OP_MIDI_SONG, 0);
}

void
mock230_send_run_weight(
    struct Mock230Player* player,
    int kilograms)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->update_runweight )
            pl->update_runweight(&buf, kilograms);
        else
            rsab_p2(&buf, kilograms);
    }
    flush(player, &buf, OP_UPDATE_RUNWEIGHT, 0);
}

void
mock230_send_message(
    struct Mock230Player* player,
    const char* text)
{
    struct RSAreaBuf buf;
    /* MOCK230_ECHO_MES=1: mirror every game message to stderr. The chat box is
     * the only place a `mes` lands, which makes a content self-test that
     * reports through it unreadable from a headless run. */
    if( getenv("MOCK230_ECHO_MES") )
        fprintf(stderr, "mes: %s\n", text ? text : "");
    open_packet(&buf, 512);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->message_game )
            pl->message_game(&buf, 0, text);
        else
        {
            rsab_p1(&buf, 0); /* message type: plain game message */
            rsab_pjstr(&buf, text, RSAB_JSTR_NUL);
        }
    }
    flush(player, &buf, OP_MESSAGE_GAME, 1);
}

/* ------------------------------------------------------------------ */
/* Social                                                              */
/* ------------------------------------------------------------------ */

/*
 * The five server->client social packets.
 *
 * Each is a transcription of the matching LostCity encoder under
 * engine/src/network/game/server/codec/ — UpdateFriendListEncoder,
 * UpdateIgnoreListEncoder, FriendlistLoadedEncoder, MessagePrivateEncoder,
 * ChatFilterSettingsEncoder — and each was checked against the *client's own*
 * reader in src/net/rev/gameproto_parse.c, which is the half that has to agree.
 * Three of the five readers assert full frame consumption, so a field out of
 * place here is an abort in the client rather than a subtle drawing bug.
 *
 * None of them decides anything. Which friend, at which world, and whether the
 * viewer may see it at all is mock230_friends.c's answer (`isVisibleTo`); these
 * only write it down.
 */

void
mock230_send_update_friendlist(
    struct Mock230Player* player,
    int64_t name37,
    int world)
{
    /* One entry per packet, both for the login dump and for the deltas after
     * it. `world` is 0 for "offline, or not visible to you" — the conflation is
     * the reference's (FriendServer.sendPlayerWorldUpdate) and is the whole of
     * how "Private chat: off" hides a player from their own friends. */
    struct RSAreaBuf buf;
    open_packet(&buf, 64);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->friend_entry )
        {
            char name[16];
            base37tostr((uint64_t)name37, name, (int)sizeof(name));
            pl->friend_entry(&buf, name, world);
        }
        else
        {
            rsab_p8(&buf, name37);
            rsab_p1(&buf, world);
        }
    }
    flush(player, &buf, OP_UPDATE_FRIENDLIST, 2);
}

void
mock230_send_update_friendlist_empty(struct Mock230Player* player)
{
    /* Rev 239's zero-length FRIENDLIST_LOADED packet only moves the client to
     * state 1 (loading). UPDATE_FRIENDLIST's decoder is what sets state 2, even
     * for an empty list. Without this explicit empty var-short packet a fresh
     * account's Friends and Ignore panels say "Loading ... Please wait"
     * forever although login otherwise completed successfully. */
    struct RSAreaBuf buf;
    open_packet(&buf, 1);
    flush(player, &buf, OP_UPDATE_FRIENDLIST, 2);
}

void
mock230_send_update_ignorelist(
    struct Mock230Player* player,
    const int64_t* names37,
    int count)
{
    /* The whole list at once — the client replaces its store wholesale
     * (rs_gameproto_exec.c, PKT_NAME_UPDATE_IGNORELIST), which is why there is
     * no single-entry form to pair with UPDATE_FRIENDLIST's. */
    struct RSAreaBuf buf;
    open_packet(&buf, (size_t)(count > 0 ? count : 0) * 8 + 16);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);

        for( int i = 0; i < count; i++ )
        {
            if( pl && pl->ignore_entry )
            {
                char name[16];
                base37tostr((uint64_t)names37[i], name, (int)sizeof(name));
                pl->ignore_entry(&buf, name);
                continue;
            }
            rsab_p8(&buf, names37[i]);
        }
    }
    flush(player, &buf, OP_UPDATE_IGNORELIST, 2);
}

void
mock230_send_friendlist_loaded(
    struct Mock230Player* player,
    int status)
{
    /* 0 loading, 1 connecting to friendserver, 2 online, anything else "please
     * wait" (FriendlistLoadedEncoder's own comment). The client files it in
     * `social.server_status`. */
    struct RSAreaBuf buf;
    open_packet(&buf, 4);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->friendlist_loaded )
            pl->friendlist_loaded(&buf, status);
        else
        {
            rsab_p1(&buf, status);
        }
    }
    flush(player, &buf, OP_FRIENDLIST_LOADED, 0);
}

void
mock230_send_message_private(
    struct Mock230Player* player,
    int64_t from37,
    int32_t message_id,
    int staff_mod,
    const char* text)
{
    /*
     * p8 from, p4 messageId, p1 staffModLevel, then wordpack over the rest.
     * The client reads `data_size - 13` bytes of wordpack, so the length has to
     * be the var-u16 frame's, not a field.
     *
     * `message_id` must be non-zero: the client dedupes private messages
     * against a zero-filled ring, so a 0 id is a message it silently drops.
     * mock230_friends_next_pm_id guarantees that; this encoder does not
     * re-check, because a caller that made one up should fail visibly.
     */
    struct RSAreaBuf buf;
    uint8_t packed[512];
    struct RSCache_Buffer text_buf;

    RSCache_BufferInit(&text_buf, packed, (uint32_t)sizeof(packed));
    wordpack_pack(&text_buf, text ? text : "");

    open_packet(&buf, 16 + text_buf.position);
    rsab_p8(&buf, from37);
    rsab_p4(&buf, (int32_t)message_id);
    rsab_p1(&buf, staff_mod > 3 ? 3 : staff_mod); /* MessagePrivateEncoder's clamp */
    rsab_pdata(&buf, packed, (size_t)text_buf.position);
    flush(player, &buf, OP_MESSAGE_PRIVATE, 2);
}

void
mock230_send_chat_filter_settings(
    struct Mock230Player* player,
    int public_mode,
    int private_mode,
    int trade_mode)
{
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->chat_filter )
            pl->chat_filter(&buf, public_mode, private_mode, trade_mode);
        else
        {
            rsab_p1(&buf, public_mode);
            rsab_p1(&buf, private_mode);
            rsab_p1(&buf, trade_mode);
        }
    }
    flush(player, &buf, OP_CHAT_FILTER_SETTINGS, 0);
}

void
mock230_send_unset_map_flag(struct Mock230Player* player)
{
    /* SET_MAP_FLAG with the 255,255 "no flag" sentinel. */
    struct RSAreaBuf buf;
    open_packet(&buf, 8);
    {
        /* No 255,255 clear sentinel at V2 -- the packet carries an absolute
         * coord, and a cleared flag is coord 0. */
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->set_map_flag )
            pl->set_map_flag(&buf, 0, 0, 0);
        else
        {
            rsab_p1(&buf, 255);
            rsab_p1(&buf, 255);
        }
    }
    flush(player, &buf, OP_SET_MAP_FLAG, 0);
}

void
mock230_send_set_map_flag(struct Mock230Player* player, int local_x, int local_z)
{
    struct RSAreaBuf buf;
    assert(player);
    open_packet(&buf, 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        if( pl && pl->set_map_flag )
            pl->set_map_flag(&buf, player->level, mock230_scene_origin(player->world->zone_x) + local_x,
                             mock230_scene_origin(player->world->zone_z) + local_z);
        else
        {
            rsab_p1(&buf, local_x & 0xff);
            rsab_p1(&buf, local_z & 0xff);
        }
    }
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
    {
        const struct Mock230WirePayload* pl = wire_payload(player);

        if( pl && pl->inv_header && pl->inv_slot )
        {
            pl->inv_header(&buf, PKT_NAME_UPDATE_INV_FULL, component, container,
                           slot_count);
            for( int i = 0; i < slot_count; i++ )
            {
                const struct Mock230Item* it = slots ? &slots[i] : NULL;
                pl->inv_slot(&buf, PKT_NAME_UPDATE_INV_FULL, i,
                             (it && it->count > 0) ? it->obj_id : -1,
                             it ? it->count : 0);
            }
        }
        else
        {
            rsab_p4(&buf, component);
            rsab_p2(&buf, container);
            rsab_p2(&buf, slot_count);
            for( int i = 0; i < slot_count; i++ )
                put_inv_slot(&buf, slots ? &slots[i] : NULL);
        }
    }
    flush(player, &buf, OP_UPDATE_INV_FULL, 2);
}

void
mock230_send_inv_stop_transmit(
    struct Mock230Player* player,
    int component,
    int container)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);

        if( pl && pl->inv_stop_transmit )
            pl->inv_stop_transmit(&buf, component, container);
        else
        {
            /* Rev 230 UpdateInvStopTransmitEncoder names the component. */
            rsab_p4(&buf, component);
        }
    }
    flush(player, &buf, OP_UPDATE_INV_STOP_TRANSMIT, 0);
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
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        int const v5 = pl && pl->inv_header && pl->inv_slot;

        if( v5 )
            pl->inv_header(&buf, PKT_NAME_UPDATE_INV_PARTIAL, component, container, 0);
        else
        {
            rsab_p4(&buf, component);
            rsab_p2(&buf, container);
        }
        for( int i = 0; i < slot_count; i++ )
        {
            if( !(dirty & (1u << i)) )
                continue;
            if( v5 )
            {
                pl->inv_slot(&buf, PKT_NAME_UPDATE_INV_PARTIAL, i,
                             slots[i].count > 0 ? slots[i].obj_id : -1,
                             slots[i].count);
                continue;
            }
            rsab_psmart(&buf, i);
            put_inv_slot(&buf, &slots[i]);
        }
    }
    flush(player, &buf, OP_UPDATE_INV_PARTIAL, 2);
}

/* ------------------------------------------------------------------ */
/* PLAYER_INFO                                                         */
/* ------------------------------------------------------------------ */

/* What a player looks like is decided once, here, in the canonical slot
 * vocabulary (pkt_player_appearance.h); each writer below only spells that out
 * in its revision's tags, through Appearance_WirePack. *Which* kit fills a bare
 * slot is the player's character and is content's, in
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

/*
 * The player's canonical appearance: one slot per wear position, in the
 * vocabulary every renderer and every wire encoding is expressed in.
 *
 * Both writers below build from this, so the rules live once:
 *
 *   - a worn item blanks the positions it claims through wearpos_2 / wearpos_3
 *     — that is what makes a full helm hide hair and jaw, and a platebody hide
 *     arms. Those claimed positions are body positions, never worn ones, so
 *     blanking before placing the worn obj (the order RSProt's pEquipment uses)
 *     and after it come to the same thing;
 *   - an unclaimed position falls back to the body kit. That fallback is not
 *     redundancy: an empty slot means "nothing here", not "see the kit", so a
 *     character wearing nothing would otherwise render with no body at all —
 *     the scene draws, the player is present and positioned, and there is
 *     nothing to look at.
 */
static void
appearance_slots(
    const struct Mock230Player* player,
    int slots[APPEARANCE_SLOT_COUNT])
{
    int covered[APPEARANCE_SLOT_COUNT] = { 0 };

    for( int i = 0; i < MOCK230_WORN_SLOTS; i++ )
    {
        const struct Mock230ObjInfo* info;

        if( player->worn[i].obj_id < 0 )
            continue;
        info = mock230_objinfo(player->worn[i].obj_id);
        if( !info )
            continue;
        if( info->wearpos_2 >= 0 && info->wearpos_2 < APPEARANCE_SLOT_COUNT )
            covered[info->wearpos_2] = 1;
        if( info->wearpos_3 >= 0 && info->wearpos_3 < APPEARANCE_SLOT_COUNT )
            covered[info->wearpos_3] = 1;
    }

    for( int i = 0; i < APPEARANCE_SLOT_COUNT; i++ )
    {
        int kit;

        if( covered[i] )
            slots[i] = 0;
        else if( i < MOCK230_WORN_SLOTS && player->worn[i].obj_id >= 0 )
            slots[i] = Appearance_PackObj(player->worn[i].obj_id);
        else if( (kit = default_kit(i)) >= 0 )
            slots[i] = Appearance_PackKit(kit);
        else
            slots[i] = 0;
    }
}

/** The body underneath, whatever is worn over it (239's second array). */
static void
appearance_identkit_slots(int slots[APPEARANCE_SLOT_COUNT])
{
    for( int i = 0; i < APPEARANCE_SLOT_COUNT; i++ )
    {
        int kit = default_kit(i);
        slots[i] = kit >= 0 ? Appearance_PackKit(kit) : 0;
    }
}

/** One slot entry: an empty slot is a single zero byte, anything else is the
 *  encoding's tagged word in two. */
static void
put_appearance_slots(
    struct RSAreaBuf* buf,
    enum AppearanceEncoding encoding,
    const int slots[APPEARANCE_SLOT_COUNT])
{
    for( int i = 0; i < APPEARANCE_SLOT_COUNT; i++ )
    {
        int wire = Appearance_WirePack(encoding, slots[i]);
        if( wire == 0 )
            rsab_p1(buf, 0);
        else
            rsab_p2(buf, wire);
    }
}

/*
 * The revision-239 appearance block.
 *
 * Not a reordering of the classic one — a different shape, and the differences
 * are the kind that frame perfectly and render as nothing:
 *
 *   - the 12 wear slots become TWO arrays of 12: `equipment` (what is drawn)
 *     and `identKit` (the body underneath). The classic packs both into one
 *     array, so a 239 client reading it consumes the whole thing as equipment
 *     and then reads the colours as an identKit;
 *   - a worn obj is tagged `+ 0x800`, not `+ 0x200` — the classic tag lands
 *     inside the kit range here and would draw some unrelated body part.
 *     Appearance_WirePack owns that difference so it cannot be half-applied;
 *   - a skull icon and a head icon sit right after the gender;
 *   - the name is a NUL-terminated STRING, not the classic 8-byte base-37;
 *   - and there are five trailing fields the classic has no room for at all
 *     (skill level, hidden, a customisation flag, three name extras).
 *
 * Transcribed from RSProt's `PlayerAppearanceEncoder`.
 */
static void
put_appearance_v5(
    struct RSAreaBuf* buf,
    const struct Mock230Player* player)
{
    int equipment[APPEARANCE_SLOT_COUNT];
    int identkit[APPEARANCE_SLOT_COUNT];
    int headicon;

    appearance_slots(player, equipment);
    appearance_identkit_slots(identkit);

    /* Rev 239 carries one prayer-icon archive index, not the older appearance
     * bitmask. Content still owns the mask through HEADICONS_SET; choose its
     * active bit at the wire boundary, just as the decoder converts the index
     * back into the client's generic mask. */
    headicon = mock239_appearance_headicon_index(player->headicons);

    rsab_p1(buf, (uint8_t)player->gender);
    rsab_p1(buf, 255); /* skullIcon: -1, none */
    rsab_p1(buf, headicon < 0 ? 255 : headicon);

    put_appearance_slots(buf, APPEARANCE_ENC_V5, equipment);
    put_appearance_slots(buf, APPEARANCE_ENC_V5, identkit);

    for( int i = 0; i < 5; i++ )
        rsab_p1(buf, 0); /* body colours */

    {
        int anims[7] = {
            player->readyanim, player->turnanim, player->walkanim,
            player->walkanim_b, player->walkanim_l, player->walkanim_r,
            player->runanim
        };
        for( int i = 0; i < 7; i++ )
            rsab_p2(buf, anims[i] < 0 ? 65535 : anims[i]);
    }

    rsab_pjstr(buf, player->display_name, RSAB_JSTR_NUL);
    rsab_p1(buf, 3); /* combat level */
    rsab_p2(buf, 0); /* skill level, shown only in some minigames */
    rsab_p1(buf, 0); /* hidden */
    /*
     * The obj-customisation flag: bits 1..12 (one per wear position) each say a
     * variable-length per-obj recolour/retexture block follows, and bit 15 is
     * forceModelRefresh. This stays 0 until those blocks are written — a
     * decoder that cannot skip a block it does not understand reads every later
     * field at the wrong offset, which is why our own reader rejects the whole
     * appearance when it sees one.
     */
    rsab_p2(buf, 0);
    rsab_pjstr(buf, "", RSAB_JSTR_NUL); /* beforeName */
    rsab_pjstr(buf, "", RSAB_JSTR_NUL); /* afterName */
    rsab_pjstr(buf, "", RSAB_JSTR_NUL); /* afterCombatLevel */
    /*
     * The pronoun, and it is the last byte of the block.
     *
     * Easy to miss: RSProt's reference DECODER stops after the three name
     * strings, so a writer transcribed from that alone is one byte short. The
     * real encoder writes this, and the length prefix counts it — so omitting
     * it does not truncate a field, it shifts the whole block's length and the
     * client rejects the packet.
     */
    rsab_p1(buf, 0);
}

/*
 * The classic appearance block (lc254 / lc245_2 / xrsps233 / osrs230): one slot
 * array, `0x100 + kit` / `0x200 + obj`, a base-37 name, and no trailing fields.
 */
static void
put_appearance(
    struct RSAreaBuf* buf,
    const struct Mock230Player* player)
{
    int slots[APPEARANCE_SLOT_COUNT];

    appearance_slots(player, slots);

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
    put_appearance_slots(buf, APPEARANCE_ENC_CLASSIC, slots);
    for( int i = 0; i < 5; i++ )
        rsab_p1(buf, 0); /* body colours */

    /* idle, turn, walk, walk-back, walk-left, walk-right, run.
     * Content's READYANIM…RUNANIM own these; player_init seeds the unarmed
     * defaults the client already expects at spawn. */
    {
        int anims[7] = {
            player->readyanim, player->turnanim, player->walkanim,
            player->walkanim_b, player->walkanim_l, player->walkanim_r,
            player->runanim
        };
        for( int i = 0; i < 7; i++ )
            rsab_p2(buf, anims[i] < 0 ? 65535 : anims[i]);
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
    {
        mask |= MOCK230_PMASK_APPEARANCE;
        /* LostCity PlayerInfoEncoder.lowdefinition: re-emit a latched
         * FACE_ENTITY on enter-view even when the per-tick mask was cleared. */
        if( player->face_entity != -1 )
            mask |= MOCK230_PMASK_FACE_ENTITY;
    }

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
 * The base is in **classic scene-local tiles**: the client's scene base is
 * also `(zone - 6) * 8`, so those tiles need no further offset, and then
 * `pos >> 4`. That is the same coordinate space every entity coordinate uses,
 * so getting it wrong here shows up as loot landing a few tiles from the
 * corpse rather than as anything louder.
 *
 * The header is the two base bytes and nothing else — real rev-230 carries a
 * level as well, which packetin.h deliberately drops (the mock is single-plane
 * per scene). It has to stay in step with the length that table declares:
 * writing a byte the client's framing does not expect makes the frame eat the
 * opcode of whatever follows, so the stream desyncs a packet later, nowhere
 * near here. `check_frame_length` is what catches that.
 *
 * Three headers, not one, and the difference is what the client does to its own
 * memory of the zone:
 *
 *   PARTIAL_FOLLOWS (106)   name the zone. Sub-packets follow as ordinary
 *                           packets.
 *   FULL_FOLLOWS (41)       name the zone *and reset it* — the client drops
 *                           every obj stack it holds there. What follows is the
 *                           zone's whole state, which is how a client that has
 *                           never held this zone is caught up.
 *   PARTIAL_ENCLOSED (38)   name the zone and carry the sub-packets inside this
 *                           packet's own payload, opcodes and all. Those inner
 *                           opcodes are plain wire bytes — no ISAAC — resolved
 *                           through the same table as a top-level one, which is
 *                           why the sub-opcodes had to be assigned clear of the
 *                           top-level ones (§3.1b). This is the shared form: one
 *                           encode, however many clients stand in the zone.
 */

static int
zone_base(
    struct Mock230Server* srv,
    int zone_x,
    int zone_z,
    int* base_z)
{
    *base_z = (zone_z * MOCK230_ZONE_TILES) - mock230_scene_origin(srv->zone_z);
    return (zone_x * MOCK230_ZONE_TILES) - mock230_scene_origin(srv->zone_x);
}

/*
 * `level` is the ZONE's plane, not the player's.
 *
 * It used to be `player->level`, which is right for every zone a player is
 * standing in and wrong for every other plane of the same region — and since
 * the flush only ever offered zones at the player's own level, nothing noticed.
 * Both are fixed together: the window now spans all four planes
 * (`rebuild_active`), so this has to be told which one it is describing.
 */
void
mock230_send_zone_header(
    struct Mock230Player* player,
    int zone_x,
    int zone_z,
    int level,
    int full)
{
    struct RSAreaBuf buf;
    int base_z;
    int base_x = zone_base(player->world, zone_x, zone_z, &base_z);

    open_packet(&buf, 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);
        int name = full ? OP_UPDATE_ZONE_FULL_FOLLOWS : OP_UPDATE_ZONE_PARTIAL_FOLLOWS;

        if( pl && pl->zone_header )
            pl->zone_header(&buf, name, base_x, base_z, level);
        else
        {
            rsab_p1(&buf, base_x);
            rsab_p1(&buf, base_z);
        }
    }
    flush(player, &buf, full ? OP_UPDATE_ZONE_FULL_FOLLOWS : OP_UPDATE_ZONE_PARTIAL_FOLLOWS,
          0);
}

/*
 * The right-click ops on another player, one slot at a time.
 *
 * `SetPlayerOpEncoder.ts`: p1 slot, p1 primary, pjstr text — and the client's
 * parser already read exactly that for lc254, which is why this needed no new
 * decode. The op is *cleared* by sending a null text, which is how the
 * reference's `login.rs2` takes "Attack" away outside the wilderness.
 *
 * Slots are 1..5 and the client stores them at `player_ops[slot - 1]`; anything
 * else is dropped on arrival, so the range check belongs in the opcode that
 * calls this rather than here.
 *
 * `primary` decides whether the op is the *left-click* action or lives in the
 * menu. It is a wire encoding rather than a config value, which is why it is a
 * plain int here and `^true`/`^false` in content.
 */
void
mock230_send_set_player_op(
    struct Mock230Player* player,
    int slot,
    int primary,
    const char* text)
{
    struct RSAreaBuf buf;

    open_packet(&buf, 64);
    rsab_p1(&buf, slot);
    rsab_p1(&buf, primary ? 1 : 0);
    rsab_pjstr(&buf, text ? text : "", RSAB_JSTR_NEWLINE);
    flush(player, &buf, OP_SET_PLAYER_OP, 1);
}

/*
 * One sub-packet's opcode and payload.
 *
 * Written into a caller's buffer rather than sent, because it has two consumers
 * with the same bytes: `mock230_send_zone_sub` puts it on the wire as a packet
 * of its own, and `mock230_zone.c` concatenates a zone's worth into the shared
 * blob PARTIAL_ENCLOSED carries. Having one encoder is the point — the blob and
 * the loose packet cannot describe the same event differently.
 *
 * The count is 16 bits on the wire; a bigger stack is drawn as its thousands
 * abbreviation by the client, which reads the count it was given. Clamping is
 * better than wrapping 65,536 coins to zero.
 *
 * `info` packs a loc's shape and angle into one byte: (shape << 2) | angle. The
 * client unpacks it the same way for every loc packet, which is why LOC_DEL
 * carries it too — a tile can hold a wall and a scenery loc at once, and the
 * shape says which one is meant.
 */
/**
 * The length class this revision frames a packet with: 0 fixed, 1 var-u8,
 * 2 var-u16 -- the `var` argument `flush` wants.
 *
 * Exists because the class is a property of the REVISION, and the callers that
 * used to pass a literal were stating 230's. See `check_frame_length`.
 */
static int
wire_var_class(const struct Mock230Wire* wire, int pkt_name)
{
    int opcode;
    int size;

    if( !wire || !wire->payload_size )
        return 0;
    opcode = mock230_wire_opcode(wire, pkt_name);
    if( opcode < 0 )
        return 0;
    size = wire->payload_size(opcode);
    if( size == PKTIN_LENGTH_VARU8 )
        return 1;
    if( size == PKTIN_LENGTH_VARU16 )
        return 2;
    return 0;
}

static int
zone_sub_opcode(int kind)
{
    switch( kind )
    {
    case MOCK230_ZONE_EV_LOC_ADD_CHANGE:
        return OP_LOC_ADD_CHANGE;
    case MOCK230_ZONE_EV_LOC_DEL:
        return OP_LOC_DEL;
    case MOCK230_ZONE_EV_LOC_ANIM:
        return OP_LOC_ANIM;
    case MOCK230_ZONE_EV_LOC_MERGE:
        return OP_LOC_MERGE;
    case MOCK230_ZONE_EV_OBJ_ADD:
        return OP_OBJ_ADD;
    case MOCK230_ZONE_EV_OBJ_DEL:
        return OP_OBJ_DEL;
    case MOCK230_ZONE_EV_OBJ_COUNT:
        return OP_OBJ_COUNT;
    case MOCK230_ZONE_EV_PROJANIM:
        return OP_MAP_PROJANIM;
    case MOCK230_ZONE_EV_MAPANIM:
        return OP_MAP_ANIM;
    default:
        return -1;
    }
}

static int
clamp16(int count)
{
    return count > 0xffff ? 0xffff : count;
}

/** The payload alone, without the opcode. */
static int
zone_sub_payload(
    struct RSAreaBuf* buf,
    const struct Mock230ZoneEvent* event,
    const struct Mock230Wire* wire)
{
    /*
     * The revision's own writer first. Its absence is not a fallback to the
     * classic layout — a revision with a payload set refuses what it has not
     * transcribed, because these packets frame to the right length either way
     * and decode to a different loc on a different tile.
     */
    if( wire && wire->payload && wire->payload->zone_payload )
    {
        int name = zone_sub_opcode(event->kind);
        int props = ((event->shape & 0x1f) << 2) | (event->angle & 3);

        if( name < 0 )
            return 0;
        return wire->payload->zone_payload(buf, name, event, event->pos, props);
    }

    switch( event->kind )
    {
    case MOCK230_ZONE_EV_LOC_ADD_CHANGE:
        rsab_p1(buf, event->pos);
        rsab_p1(buf, ((event->shape & 0x1f) << 2) | (event->angle & 3));
        rsab_p2(buf, event->id);
        return 1;
    case MOCK230_ZONE_EV_LOC_DEL:
        rsab_p1(buf, event->pos);
        rsab_p1(buf, ((event->shape & 0x1f) << 2) | (event->angle & 3));
        return 1;
    case MOCK230_ZONE_EV_LOC_ANIM:
        rsab_p1(buf, event->pos);
        rsab_p1(buf, ((event->shape & 0x1f) << 2) | (event->angle & 3));
        rsab_p2(buf, event->id);
        return 1;
    case MOCK230_ZONE_EV_LOC_MERGE:
        rsab_p1(buf, event->pos);
        rsab_p1(buf, ((event->shape & 0x1f) << 2) | (event->angle & 3));
        rsab_p2(buf, event->id);
        rsab_p2(buf, event->start_cycle);
        rsab_p2(buf, event->end_cycle);
        rsab_p2(buf, event->player_pid);
        rsab_p1(buf, (uint8_t)event->east);
        rsab_p1(buf, (uint8_t)event->south);
        rsab_p1(buf, (uint8_t)event->west);
        rsab_p1(buf, (uint8_t)event->north);
        return 1;
    case MOCK230_ZONE_EV_OBJ_ADD:
        rsab_p1(buf, event->pos);
        rsab_p2(buf, event->id);
        rsab_p2(buf, clamp16(event->count));
        return 1;
    case MOCK230_ZONE_EV_OBJ_DEL:
        rsab_p1(buf, event->pos);
        rsab_p2(buf, event->id);
        return 1;
    case MOCK230_ZONE_EV_OBJ_COUNT:
        rsab_p1(buf, event->pos);
        rsab_p2(buf, event->id);
        rsab_p2(buf, clamp16(event->old_count));
        rsab_p2(buf, clamp16(event->count));
        return 1;
    /*
     * Fifteen bytes, and the client asserts it consumed exactly that
     * (`gameproto_parse.c` MAP_PROJANIM), so the order below is the whole
     * contract. Three of the fields are *signed* on the wire — the two tile
     * offsets and the target — and every one of them is routinely negative: a
     * shot to the west has a negative dx, and a projectile aimed at a player
     * carries `-slot - 1`. `rsab_p1`/`rsab_p2` write the low bits either way, so
     * the sign survives as two's complement and the client's `g1b`/`g2b` read it
     * back; the masks are here to say that is deliberate rather than to fix
     * anything.
     */
    case MOCK230_ZONE_EV_PROJANIM:
        rsab_p1(buf, event->pos);
        rsab_p1(buf, event->dx_offset & 0xff);
        rsab_p1(buf, event->dz_offset & 0xff);
        rsab_p2(buf, event->target & 0xffff);
        rsab_p2(buf, event->id);
        rsab_p1(buf, event->src_height);
        rsab_p1(buf, event->dst_height);
        rsab_p2(buf, event->start_delay);
        rsab_p2(buf, event->end_delay);
        rsab_p1(buf, event->peak);
        rsab_p1(buf, event->arc);
        return 1;
    /* Six bytes; client asserts exactly that (`gameproto_parse.c` MAP_ANIM). */
    case MOCK230_ZONE_EV_MAPANIM:
        rsab_p1(buf, event->pos);
        rsab_p2(buf, event->id);
        rsab_p1(buf, event->src_height);
        rsab_p2(buf, event->start_delay);
        return 1;
    default:
        return 0;
    }
}

int
mock230_encode_zone_sub(
    const struct Mock230Wire* wire,
    uint8_t* dst,
    int max,
    const struct Mock230ZoneEvent* event)
{
    struct RSAreaBuf buf;
    int pkt_name = zone_sub_opcode(event->kind);
    int code;

    if( !wire )
        wire = mock230_wire_default();
    if( pkt_name < 0 )
        return 0;
    /*
     * The byte that leads a sub-packet inside PARTIAL_ENCLOSED is NOT the
     * top-level opcode at every revision -- 239 uses an ordinal. Resolving it
     * here rather than reusing `opcode` is the difference between the client
     * reading this event and reading a different one: the enclosed blob is
     * length-prefixed as a whole, so a wrong lead byte still frames.
     */
    code = wire->zone_sub_code ? wire->zone_sub_code(pkt_name) : -1;
    if( code < 0 )
        return 0;
    rsab_wrap(&buf, dst, (size_t)max);
    rsab_p1(&buf, code);
    if( !zone_sub_payload(&buf, event, wire) || !rsab_ok(&buf) )
        return 0;
    return (int)rsab_len(&buf);
}

int
mock230_zone_sub_standalone(
    const struct Mock230Wire* wire,
    int kind)
{
    int name = zone_sub_opcode(kind);

    if( name < 0 )
        return 0;
    return mock230_wire_opcode(wire ? wire : mock230_wire_default(), name) >= 0;
}

void
mock230_send_zone_sub(
    struct Mock230Player* player,
    const struct Mock230ZoneEvent* event)
{
    struct RSAreaBuf buf;
    const struct Mock230Wire* wire = wire_for(player);
    int opcode = zone_sub_opcode(event->kind);

    if( opcode < 0 )
        return;
    open_packet(&buf, 512);
    if( !zone_sub_payload(&buf, event, wire) )
        return;
    /*
     * The length class comes from the revision's table, not from a constant.
     *
     * These were all sent as fixed-size, which they are at 230. At 239
     * LOC_ADD_CHANGE_V2 is VAR-U16, because it can carry a list of
     * op-override strings; sending it fixed writes no length prefix at all,
     * and the client reads the first two payload bytes as one and then keeps
     * reading. The stream never recovers, so the symptom is the connection
     * dropping some packets after a door opened -- not a wrong door.
     */
    flush(player, &buf, opcode, wire_var_class(wire, opcode));
}

/* `level` is the zone's plane — same reason as mock230_send_zone_header. */
void
mock230_send_zone_enclosed(
    struct Mock230Player* player,
    int zone_x,
    int zone_z,
    int level,
    const uint8_t* blob,
    int len)
{
    struct RSAreaBuf buf;
    int base_z;
    int base_x = zone_base(player->world, zone_x, zone_z, &base_z);

    if( len <= 0 )
        return;
    open_packet(&buf, (size_t)len + 8);
    {
        const struct Mock230WirePayload* pl = wire_payload(player);

        if( pl && pl->zone_header )
            pl->zone_header(&buf, OP_UPDATE_ZONE_PARTIAL_ENCLOSED, base_x, base_z,
                            level);
        else
        {
            rsab_p1(&buf, base_x);
            rsab_p1(&buf, base_z);
        }
    }
    rsab_pdata(&buf, blob, (size_t)len);
    flush(player, &buf, OP_UPDATE_ZONE_PARTIAL_ENCLOSED, 2);
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

    /*
     * Revision 239 is a different CODEC here, not a different field order, so
     * it forks before a single bit is written rather than branching per field.
     *
     * What it sends is the local player only: high resolution with an
     * appearance block, every other slot held in low resolution. Other players
     * are not in it yet — see mock239_playerinfo.h — which is why this does not
     * fall through into the loop below afterwards. Sending the classic stream's
     * other-player records after a v5 header would frame cleanly and decode as
     * noise.
     */
    if( wire_is_v5(player) )
    {
        uint8_t appearance[512];
        struct RSAreaBuf ap;
        int32_t coord;
        enum Mock239PlayerMovement movement = MOCK239_PLAYER_NOMOVE;
        int32_t movement_value = 0;

        /*
         * The appearance goes out once, on the tick after the init block, and
         * then only when it changes. Re-sending it every tick is not merely
         * wasteful: it forces the extended-info bit on, which keeps the player
         * out of the skip path above and makes every tick an update.
         */
        rsab_wrap(&ap, appearance, sizeof(appearance));
        if( !player->v5_playerinfo_sent || (player->masks & MOCK230_PMASK_APPEARANCE) )
            put_appearance_v5(&ap, player);

        /*
         * A DELTA against what this client was last told, which the init block
         * seeded with the absolute position. Zero while standing still.
         *
         * The 30-bit field is added to the client's own copy, so sending the
         * absolute coord here moves the player by their whole world position
         * every tick — a world that builds correctly and goes black seconds
         * later as they leave the loaded scene, with no packet malformed.
         */
        coord = (int32_t)((((player->level - player->v5_last_level) & 0x3) << 28) |
                          (((player->x - player->v5_last_x) & 0x3fff) << 14) |
                          ((player->z - player->v5_last_z) & 0x3fff));

        /*
         * PLAYER_INFO v5 has dedicated walk/run opcodes.  Sending every delta
         * as TELEPORT kept coordinates correct but told the golden client the
         * player had jumped, so it never selected walkanim/runanim.
         *
         * Its 3-bit direction table has south first, while this server's
         * World_CoordStep table has north first.  Derive from the measured
         * coordinate delta so the conversion is explicit.  The 4-bit run
         * table is the outer ring of a 5x5 square, exactly as class109's
         * authoritative decoder spells it.
         *
         * A two-step turn can finish one diagonal tile away (for example east
         * then north).  That displacement is representable by WALK, not RUN;
         * using WALK preserves both the coordinate and a locomotion pose.
         */
        {
            int dx = player->x - player->v5_last_x;
            int dz = player->z - player->v5_last_z;

            if( player->place_dirty || player->level != player->v5_last_level )
            {
                movement = MOCK239_PLAYER_TELEPORT;
                movement_value = coord;
            }
            else if( dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1 &&
                     (dx != 0 || dz != 0) )
            {
                movement = MOCK239_PLAYER_WALK;
                movement_value = mock230_step_direction(dx, -dz);
            }
            else if( dx >= -2 && dx <= 2 && dz >= -2 && dz <= 2 &&
                     (dx == -2 || dx == 2 || dz == -2 || dz == 2) )
            {
                static const int k_run_dir[5][5] = {
                    { 0, 1, 2, 3, 4 },
                    { 5, -1, -1, -1, 6 },
                    { 7, -1, -1, -1, 8 },
                    { 9, -1, -1, -1, 10 },
                    { 11, 12, 13, 14, 15 },
                };

                movement = MOCK239_PLAYER_RUN;
                movement_value = k_run_dir[dz + 2][dx + 2];
            }
            else if( dx != 0 || dz != 0 )
            {
                movement = MOCK239_PLAYER_TELEPORT;
                movement_value = coord;
            }
        }
        /*
         * The rest of the local player's extended info, from the same masks the
         * classic writer reads. Hitsplats and animations were absent here while
         * the npc side had them, which reads as "npcs take damage and the player
         * does not" -- the packets were fine, the block was simply never built.
         */
        {
            struct Mock239PlayerExt ext;

            memset(&ext, 0, sizeof(ext));
            if( player->masks & (MOCK230_PMASK_DAMAGE | MOCK230_PMASK_DAMAGE2) )
            {
                ext.has_hit = 1;
                ext.hit_type = player->damage_type;
                ext.hit_value = player->damage;
            }
            if( player->masks & MOCK230_PMASK_SEQUENCE )
            {
                ext.has_seq = 1;
                ext.seq_id = player->anim_id;
                ext.seq_delay = player->anim_delay;
            }
            ext.has_face = v5_face_from_classic(
                &ext.face, player->masks, MOCK230_PMASK_FACE_ENTITY,
                MOCK230_PMASK_FACE_COORD, player->face_entity, player->face_x,
                player->face_z, 0);
            if( movement == MOCK239_PLAYER_RUN )
            {
                /* Opcode 2 states a two-tile displacement; it does not select
                 * the run locomotion by itself. Revision 239 stages movement
                 * using TEMP_MOVE_SPEED=2 in the extended tail. */
                ext.has_temp_move_speed = 1;
                ext.temp_move_speed = 2;
            }
            /* The player's half of MOCK230_EXT_DEBUG. The npc writer has had
             * one since it was written; without the pair, "the animation did
             * not play" cannot be split into "the server never set the mask"
             * and "the client dropped the block". */
            if( getenv("MOCK230_EXT_DEBUG") )
                fprintf(stderr,
                        "ext player: masks=0x%x movement=%d/%d speed=%d hit=%d/%d "
                        "seq=%d/%d face=%d appearance=%d\n",
                        player->masks, movement, movement_value,
                        ext.has_temp_move_speed ? ext.temp_move_speed : -1, ext.hit_type,
                        ext.hit_value, ext.seq_id, ext.seq_delay, ext.has_face,
                        (int)rsab_len(&ap));
            mock239_playerinfo_write(&buf, mock230_wire_player_index(player->pid), movement,
                                     movement_value, player->v5_playerinfo_sent, appearance,
                                     (int)rsab_len(&ap), &ext);
        }
        player->v5_playerinfo_sent = 1;
        player->v5_last_x = player->x;
        player->v5_last_z = player->z;
        player->v5_last_level = player->level;
        flush(player, &buf, OP_PLAYER_INFO, 2);
        return;
    }

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

        /*
         * `place_dirty` is a teleport, and the tracked section has no way to
         * express one: its four movement ops are "nothing", one step, two
         * steps, and remove. So a teleport *is* a remove — and the entering-view
         * loop below re-adds them, in the same packet, at their new tile. The
         * client's reader handles the pair in order, so the entity is dropped
         * and respawned inside one tick.
         *
         * Without this, the observer's copy of a player who teleported stays
         * where they were until they take a step, and then walks there from the
         * wrong place. The local player has op 3 to itself precisely because
         * this section cannot lend it one.
         */
        if( other->place_dirty || !player_in_view(player, other) )
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

    /*
     * `place_dirty` is NOT cleared here. It used to be, and with one recipient
     * that was the same thing; with several it is not. Phase 10 encodes one
     * PLAYER_INFO per player, and a teleport has to be described in *all* of
     * them — the mover's own (as an absolute placement) and every observer's
     * (as a remove-and-re-add above). Clearing it inside the encoder means
     * whoever is encoded first consumes it and everyone after sees a player who
     * did not move. Phase 11 clears it, beside `masks`, for the same reason
     * `masks` is cleared there.
     */
    memcpy(player->tracked_players, kept, sizeof(int) * (size_t)kept_count);
    player->tracked_player_count = kept_count;
}

/* ------------------------------------------------------------------ */
/* NPC_INFO                                                            */
/* ------------------------------------------------------------------ */


/* ------------------------------------------------------------------ */
/* Revision 239 extended info                                          */
/* ------------------------------------------------------------------ */

/*
 * The npc extended-info block at revision 239.
 *
 * A DIFFERENT FLAG SPACE from the classic one-byte mask beside it, not a wider
 * version of it. The classic bits are 0x01 DAMAGE2, 0x02 ANIM, 0x04
 * FACE_ENTITY, ... in the client's own read order; at 239 the same eight
 * concepts are scattered across 26 bits with no relationship to those values,
 * and the ORDER the blocks are written in is a third thing again -- neither
 * ascending flag order nor the classic order. Both come from RSProt's
 * NpcAvatarExtendedInfoDesktopWriter, whose `convertFlags` exists precisely
 * because the server-side constants and the client's bits are different sets.
 *
 * Getting either wrong is quiet: the flag byte frames, the blocks are read in
 * whatever order the client wants them, and the npc plays some other npc's
 * animation or none.
 */
enum
{
    /* "another flag byte follows" -- one per additional byte, and they are
     * part of the flag, so they are set from the value rather than counted. */
    V5_NEXT_BYTE_1 = 0x40,
    V5_NEXT_BYTE_2 = 0x800,
    V5_NEXT_BYTE_3 = 0x200000,

    V5_NPC_TRANSFORMATION = 0x1,
    V5_NPC_SAY = 0x2,
    V5_NPC_FACING = 0x8,
    V5_NPC_SEQUENCE = 0x80,
    V5_NPC_SPOTANIM = 0x40000,
    V5_NPC_HITMARKS = 0x80000,
};

/*
 * pSmart1or2 is `rsab_psmart`. This file used to carry a private copy — one of
 * five in src/net (docs/BUFFER_ACCESSOR_AUDIT.md §1.1), byte-identical in range
 * and silently truncating outside it where the library latches an overflow.
 *
 * Merge note: v3 deduplicated this at the same time, onto
 * `mock239_face_psmart1or2` in mock239_facing.h — which was itself a fresh copy
 * of the same encoding, byte-identical again. Both are now the library's, so
 * the count went to one rather than to two.
 */
#define v5_psmart1or2 rsab_psmart

/* Translate the mock's revision-230 facing latch to 239's one-block model.
 * The old FACE_COORD value is an absolute half-tile centre (2 * tile + 1);
 * Face.Loc wants absolute tile coordinates plus a footprint. */
static int
v5_face_from_classic(
    struct Mock239Face* face,
    uint32_t classic,
    uint32_t entity_mask,
    uint32_t coord_mask,
    int face_entity,
    int face_x,
    int face_z,
    int force_latch)
{
    mock239_face_init(face);

    /* A loc and an entity cannot coexist in a V5 Face block.  `reorient`
     * intentionally clears the entity and emits the loc in its completion
     * tick, so a coordinate wins when both legacy masks are present. */
    if( (classic & coord_mask) != 0 ||
        (force_latch && face_entity < 0 && (face_x != 0 || face_z != 0)) )
    {
        face->kind = MOCK239_FACE_LOC;
        face->x = face_x / 2;
        face->z = face_z / 2;
        return 1;
    }
    if( (classic & entity_mask) == 0 && !(force_latch && face_entity >= 0) )
        return 0;

    if( face_entity < 0 )
    {
        face->kind = MOCK239_FACE_RESET;
        return 1;
    }
    face->kind = MOCK239_FACE_ENTITY;
    if( face_entity >= MOCK230_FACE_PLAYER_BASE )
    {
        int const pool_pid = face_entity - MOCK230_FACE_PLAYER_BASE;

        face->entity_type = MOCK239_FACE_PLAYER;
        /* Rev-239 NpcFaceEncoder writes the player's GPI index. The classic
         * latch stores the mock's pool pid, so it needs the same +1 mapping as
         * login and PLAYER_INFO. Sending pool pid 0 here names the unoccupied
         * client slot 0 while the local player lives at wire slot 1. */
        face->entity_index = mock230_wire_player_index(pool_pid);
    }
    else
    {
        face->entity_type = MOCK239_FACE_NPC;
        face->entity_index = face_entity;
    }
    return 1;
}


/** The flag itself, plus the continuation bits its own width implies. */
static void
v5_put_extended_flag(struct RSAreaBuf* buf, uint32_t flag)
{
    if( flag & 0xffffff00u )
        flag |= V5_NEXT_BYTE_1;
    if( flag & 0xffff0000u )
        flag |= V5_NEXT_BYTE_2;
    if( flag & 0xff000000u )
        flag |= V5_NEXT_BYTE_3;

    rsab_p1(buf, (int32_t)(flag & 0xff));
    if( flag & V5_NEXT_BYTE_1 )
        rsab_p1(buf, (int32_t)((flag >> 8) & 0xff));
    if( flag & V5_NEXT_BYTE_2 )
        rsab_p1(buf, (int32_t)((flag >> 16) & 0xff));
    if( flag & V5_NEXT_BYTE_3 )
        rsab_p1(buf, (int32_t)((flag >> 24) & 0xff));
}

static void
put_npc_extended_v5(struct RSAreaBuf* buf, struct Mock230Npc* npc, int force_face_latch)
{
    uint32_t const classic = npc->masks;
    uint32_t flag = 0;
    int const hit = (classic & (MOCK230_NMASK_DAMAGE | MOCK230_NMASK_DAMAGE2)) != 0;
    struct Mock239Face face;
    int const has_face = v5_face_from_classic(
        &face, classic, MOCK230_NMASK_FACE_ENTITY, MOCK230_NMASK_FACE_COORD,
        npc->face_entity, npc->face_x, npc->face_z, force_face_latch);

    if( hit )
        flag |= V5_NPC_HITMARKS;
    if( classic & MOCK230_NMASK_ANIM )
        flag |= V5_NPC_SEQUENCE;
    if( classic & MOCK230_NMASK_SAY )
        flag |= V5_NPC_SAY;
    if( classic & MOCK230_NMASK_SPOTANIM )
        flag |= V5_NPC_SPOTANIM;
    if( classic & MOCK230_NMASK_CHANGE_TYPE )
        flag |= V5_NPC_TRANSFORMATION;
    if( has_face )
        flag |= V5_NPC_FACING;

    if( getenv("MOCK230_EXT_DEBUG") )
        fprintf(stderr, "ext npc: classic=0x%x flag=0x%x hit=%d/%d seq=%d\n", classic, flag,
                npc->damage_type, npc->damage, npc->anim_id);
    v5_put_extended_flag(buf, flag);

    /*
     * Blocks in the writer's order, which is neither the flag order nor the
     * classic order: HITMARKS before SEQUENCE before SAY before SPOTANIM before
     * TRANSFORMATION. The client reads them in this sequence and nothing on the
     * wire separates them, so a block out of place is read as the next one.
     */
    if( hit )
    {
        /* NpcHitmarkEncoder: p1Alt1 count, then per hit
         * pSmart1or2 type, value, delay, limit. One hit per tick is all the
         * classic mask could express, so one is all there is to convert. */
        rsab_p1_alt1(buf, 1);
        v5_psmart1or2(buf, npc->damage_type);
        v5_psmart1or2(buf, npc->damage);
        v5_psmart1or2(buf, 0); /* delay: lands this tick */
        /* Actor.method3560 only inserts the hitmark when this slot limit is
         * positive. Revision 239 actors retain four concurrent hitmarks. */
        v5_psmart1or2(buf, 4);
    }
    if( classic & MOCK230_NMASK_ANIM )
    {
        /* NpcSequenceEncoder: p2 id, p1Alt2 delay. 65535 cancels. */
        rsab_p2(buf, npc->anim_id < 0 ? 65535 : npc->anim_id);
        rsab_p1_alt2(buf, npc->anim_delay);
    }
    if( classic & MOCK230_NMASK_SAY )
        rsab_pjstr(buf, npc->say, RSAB_JSTR_NUL);
    if( classic & MOCK230_NMASK_SPOTANIM )
    {
        /*
         * NpcSpotAnimEncoder: p1Alt2 count, then per entry p1 slot, p2 id,
         * p4Alt2 (delay | height << 16).
         *
         * A LIST at this revision -- an npc can carry several graphics at once,
         * in numbered slots -- where the classic packet carried exactly one and
         * had no slot. Slot 0 is the one the classic mask means.
         */
        rsab_p1_alt2(buf, 1);
        rsab_p1(buf, 0);
        rsab_p2(buf, npc->spotanim_id < 0 ? 65535 : npc->spotanim_id);
        rsab_p4_alt2(buf, npc->spotanim_height_delay);
    }
    if( classic & MOCK230_NMASK_CHANGE_TYPE )
        rsab_p2_alt3(buf, npc->change_type); /* NpcTransformationEncoder */
    if( has_face )
        mock239_face_write_npc(buf, &face);

    /*
     * Not converted yet:
     *
     *   the health bar            a separate HEADBARS block keyed by a headbar
     *                             CONFIG ID. That is content, and there is no
     *                             name for it in this tree yet, so inventing
     *                             one here would be exactly the hardcoded
     *                             config id the porting guide forbids. Until it
     *                             exists, damage numbers appear and the bar
     *                             above the npc does not.
     */
}

static int
npc_extended_pending(const struct Mock230Npc* npc)
{
    return npc->masks != 0;
}

/*
 * Revision 239 cannot carry every classic NPC mask yet.  In particular its
 * FACING block is not a byte-layout variant of FACE_ENTITY/FACE_COORD (see
 * put_npc_extended_v5), so those two masks are intentionally omitted.  They
 * must also be omitted from the traversal's "extended info follows" bit.
 *
 * Queueing a face-only NPC while writing an empty v5 flag is not harmless.
 * The low-resolution reader decides whether it can read another 16-bit slot
 * from the number of bits left in the *whole* packet.  A short face-only block
 * can leave only 27 bits after the tracked section: too few for the reader to
 * consume our 0xffff terminator.  It then byte-aligns onto the terminator,
 * reads 0xff as the extended flag and walks beyond the packet (the captured
 * failure was "66,9" on NPC_INFO).
 */
static int
npc_extended_pending_v5(const struct Mock230Npc* npc, int force_face_latch)
{
    uint32_t const supported = MOCK230_NMASK_DAMAGE | MOCK230_NMASK_DAMAGE2 |
                               MOCK230_NMASK_ANIM | MOCK230_NMASK_SAY |
                               MOCK230_NMASK_SPOTANIM | MOCK230_NMASK_CHANGE_TYPE;
    struct Mock239Face face;

    return (npc->masks & supported) != 0 ||
           v5_face_from_classic(&face, npc->masks, MOCK230_NMASK_FACE_ENTITY,
                                MOCK230_NMASK_FACE_COORD, npc->face_entity,
                                npc->face_x, npc->face_z, force_face_latch);
}

/*
 * The npc mask is a single byte — unlike the player's, there is no widening
 * bit, so the eight fields below are all there is room for. Fields go in
 * ascending bit order, matching the reader's test order.
 *
 * `force_face_latch`: LostCity NpcInfoEncoder.lowdefinition — on enter-view,
 * re-emit a latched FACE_ENTITY even when the per-tick mask bit was cleared.
 */
static void
put_npc_extended(
    struct RSAreaBuf* buf,
    struct Mock230Npc* npc,
    int force_face_latch)
{
    uint32_t mask = npc->masks & 0xff;

    if( force_face_latch && npc->face_entity != -1 )
        mask |= MOCK230_NMASK_FACE_ENTITY;

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
    {
        /*
         * The chokepoint, and the reason the check is here rather than at the
         * five writers.
         *
         * A player face id is absolute — `MOCK230_FACE_PLAYER_BASE + pid` — so
         * the same npc facing the same player encodes to the same bytes on every
         * stream. The old self-alias `BASE + 2047` meant "whoever is reading
         * this", which is right for one observer and wrong for every other. Its
         * named constant is deleted, but a writer can still reach the value by
         * arithmetic or by passing the terminator as a pid — and three of the
         * five writers (`npc_run_mode`, the opnpc greeting, `npc_say`) are
         * exercised by no test, so a regression at one of them would ship
         * silently.
         *
         * Every face id reaches the wire through this line, whichever writer
         * produced it. Checking here is what makes the invariant total rather
         * than per-writer.
         */
        if( npc->face_entity == MOCK230_FACE_PLAYER_BASE + MOCK230_PLAYER_TERMINATOR )
        {
            fprintf(stderr,
                    "mock230: npc %d face id %d is the self-alias — it must name an "
                    "absolute pid (MOCK230_FACE_PLAYER_BASE + player->pid), or every "
                    "observer but one sees it facing the wrong player\n",
                    npc->type, npc->face_entity);
        }
        rsab_p2(buf, npc->face_entity < 0 ? 0xffff : npc->face_entity);
    }
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
     * so remember that order while writing the bits. queued_force_face marks
     * enter-view slots that must re-emit a latched FACE_ENTITY. */
    int queued[MOCK230_TRACKED_NPC_MAX];
    int queued_force_face[MOCK230_TRACKED_NPC_MAX];
    int queued_count = 0;
    int kept[MOCK230_TRACKED_NPC_MAX];
    int kept_count = 0;
    /* The candidates for the entering-view section: whoever the ZoneMap says
     * stands within the add radius, rather than every npc in the world. */
    int nearby[MOCK230_TRACKED_NPC_MAX];
    int nearby_count;

    /*
     * Revision 239's npc stream.
     *
     * The HIGH-RESOLUTION section is byte-for-byte the shape the classic
     * encoder below already writes -- an 8-bit count, then per npc a
     * "has update" bit and a 2-bit type (0 stay, 1 walk, 2 run/crawl,
     * 3 remove). That is why it is duplicated here rather than shared: the
     * agreement is a coincidence of two revisions, not a guarantee, and a
     * shared body would silently follow whichever one changed first.
     *
     * The LOW-RESOLUTION adds are where it diverges, and every field moved:
     *
     *   classic   14-bit slot, 14-bit type, 5-bit dx, 5-bit dz, 1-bit extended
     *   v5        16-bit index, 1-bit spawn-cycle flag (+32 bits if set),
     *             1-bit extended, 6-bit dx, 3-bit direction, 6-bit dz,
     *             1-bit jump, 14-bit id
     *
     * The id moved to the END and the deltas widened from 5 bits to 6, so a
     * client reading the classic record takes the type as part of the index and
     * places an npc that does not exist at a coordinate that is not there.
     *
     * The terminator is a 16-bit 0xFFFF and it is NOT written here; see the
     * end of the low-resolution loop for why it is only correct once extended
     * info follows it.
     */
    if( wire_is_v5(player) )
    {
        uint8_t extended_data[4096];
        struct RSAreaBuf extended_buf;
        int adds[MOCK230_TRACKED_NPC_MAX];
        int add_count = 0;

        /*
         * SET_NPC_UPDATE_ORIGIN first, every tick, and it is not optional.
         *
         * The low-resolution deltas below are relative to an origin the CLIENT
         * holds, and since revision 222 the client does not infer it -- world
         * entities mean the reference point is not always the local player, so
         * the server states it. Two bytes, scene-local, "the player's
         * coordinate in the current build area" for the ordinary case.
         *
         * Omitting it is silent in every way that matters: the client's origin
         * stays 0,0, so an npc six tiles north-west of the player is placed six
         * tiles from the scene's south-west CORNER. It is in the npc table with
         * the right id, RuneLite's API reports it, extended info applies to it,
         * and it is drawn nowhere near the player -- usually off the scene
         * entirely, and so not drawn at all. Nothing about "no npcs appear"
         * points at a missing two-byte packet.
         *
         * Sent unconditionally rather than on change: it costs four bytes on
         * the wire, and the tick it is skipped is the tick after a rebuild
         * moves the build area under a stationary player.
         */
        open_packet(&buf, 4);
        rsab_p1(&buf, player->x - mock230_scene_base_x());
        rsab_p1(&buf, player->z - mock230_scene_base_z());
        flush(player, &buf, OP_SET_NPC_UPDATE_ORIGIN, 0);

        open_packet(&buf, 4096);
        rsab_bits(&buf);

        rsab_pbit(&buf, 8, player->tracked_count);
        for( int i = 0; i < player->tracked_count; i++ )
        {
            int slot = player->tracked[i];
            struct Mock230Npc* npc = &srv->npcs[slot];
            int dx = npc->x - player->x;
            int dz = npc->z - player->z;
            int in_range = npc->active && npc->level == player->level && dx >= -15 &&
                           dx <= 15 && dz >= -15 && dz <= 15;

            if( !in_range || npc->tele )
            {
                rsab_pbit(&buf, 1, 1);
                rsab_pbit(&buf, 2, 3); /* remove */
                /*
                 * Clearing this is what lets the npc come BACK. `npc_tracked`
                 * is the "this client already holds it" gate on the
                 * entering-view scan below; leaving it set on a remove means
                 * the npc is dropped from the client's list and can never be
                 * re-added, so the world empties out one npc at a time as
                 * things wander in and out of range -- a scene that starts
                 * populated and is bare a few ticks later, with a perfectly
                 * well-formed packet every tick.
                 */
                player->npc_tracked[slot] = 0;
                continue;
            }
            kept[kept_count++] = slot;
            {
                int const extended = npc_extended_pending_v5(npc, 0);

                if( npc->step_dir >= 0 )
                {
                    rsab_pbit(&buf, 1, 1);
                    rsab_pbit(&buf, 2, 1); /* walk */
                    rsab_pbit(&buf, 3, npc->step_dir);
                    rsab_pbit(&buf, 1, extended);
                }
                else if( extended )
                {
                    /* "no movement, but something to say about it" -- update
                     * type 0. Without this an npc that stands still and swings
                     * has no way to carry its animation. */
                    rsab_pbit(&buf, 1, 1);
                    rsab_pbit(&buf, 2, 0);
                }
                else
                {
                    rsab_pbit(&buf, 1, 0); /* unchanged */
                }
                if( extended )
                {
                    queued[queued_count++] = slot;
                    queued_force_face[queued_count - 1] = 0;
                }
            }
        }

        nearby_count = mock230_zone_npcs_near(srv, player->x, player->z, player->level, 15,
                                              nearby, MOCK230_TRACKED_NPC_MAX);
        for( int i = 0; i < nearby_count; i++ )
        {
            int slot = nearby[i];
            struct Mock230Npc* npc = &srv->npcs[slot];
            int dx;
            int dz;

            if( !npc->active || player->npc_tracked[slot] )
                continue;
            dx = npc->x - player->x;
            dz = npc->z - player->z;
            /*
             * The SAME radius the high-resolution loop keeps at, and it has to
             * be. The 6-bit delta could carry +-31, but an npc added at 20 tiles
             * is out of range on the very next tick, so it is removed, re-added,
             * removed... every tick, and never renders. The wire's capacity is
             * not the view distance.
             */
            if( dx < -15 || dx > 15 || dz < -15 || dz > 15 )
                continue;
            if( kept_count >= MOCK230_TRACKED_NPC_MAX )
                break;
            adds[add_count++] = slot;
            player->npc_tracked[slot] = 1;
            kept[kept_count++] = slot;
        }

        for( int i = 0; i < add_count; i++ )
        {
            int slot = adds[i];
            struct Mock230Npc* npc = &srv->npcs[slot];
            int dx = npc->x - player->x;
            int dz = npc->z - player->z;

            int const extended = npc_extended_pending_v5(npc, 1);

            rsab_pbit(&buf, 16, slot);
            rsab_pbit(&buf, 1, 0); /* no spawn cycle */
            rsab_pbit(&buf, 1, extended);
            rsab_pbit(&buf, 6, dx & 0x3f);
            /*
             * Facing, and the client applies it ONLY here -- `if (isNew)` in
             * its own decode. An npc that enters view facing the wrong way
             * stays that way until it walks, so this is not cosmetic for a boss
             * that never moves.
             */
            rsab_pbit(&buf, 3, npc->face_dir & 7);
            rsab_pbit(&buf, 6, dz & 0x3f);
            rsab_pbit(&buf, 1, 1); /* jump: appear on the tile */
            rsab_pbit(&buf, 14, npc->type);
            if( extended )
            {
                queued[queued_count++] = slot;
                queued_force_face[queued_count - 1] = 1;
            }
        }

        /* Encode the byte-aligned tail separately. Whether a sentinel is
         * needed depends on its exact size, which cannot be inferred from the
         * number of queued NPCs: a bare zero mask is one byte while SAY,
         * hitmarks and spotanims are variable-length blocks. */
        memset(extended_data, 0, sizeof(extended_data));
        rsab_wrap(&extended_buf, extended_data, sizeof(extended_data));
        for( int i = 0; i < queued_count; i++ )
            put_npc_extended_v5(&extended_buf, &srv->npcs[queued[i]],
                                queued_force_face[i]);

        /*
         * The terminator, only when the golden client's low-resolution guard
         * can actually reach it.
         *
         * The client's low-resolution loop has two exits and the sentinel is
         * the second one: it first checks `readableBits() >= 16 + 12` and
         * returns if the buffer cannot hold another record, and only then reads
         * an index and compares it to 0xFFFF. `readableBits()` spans the WHOLE
         * remaining packet, including the extended-info section, so the sentinel
         * is what stops it from reading extended-info bytes as another add.
         *
         * With nothing after the bit section there is nothing to stop: byte
         * alignment leaves at most 7 bits, the first check ends the loop, and a
         * sentinel written anyway is 16 bits the client never consumes. It then
         * fails its own end-of-packet check -- which is how the unconditional
         * version was found:
         *
         *     Client error: 85,28,74,147,...
         *     RuntimeException: 145,147
         *
         * 145 bytes read of the 147 sent, and the two were the sentinel.
         *
         * The guard is not "does extended info exist". It is exactly
         * `readableBits >= 16 + 12`. With 13 tracked NPCs, one NOMOVE extended
         * update leaves the bit cursor at 45 and a one-byte mask after three
         * padding bits: 11 readable bits without a sentinel, but only 27 even
         * after adding one. The client exits before reading the sentinel and
         * then decodes 0xffff as the mask, producing RuntimeException 66,9 on
         * the literal nine-byte packet. Mirror the guard instead.
         */
        if( mock239_npcinfo_tail_needs_sentinel(buf.bit_pos, rsab_len(&extended_buf)) )
            rsab_pbit(&buf, 16, 0xffff);
        rsab_bytes(&buf);

        /*
         * The extended blocks, byte-aligned, in the order the bit section
         * queued them -- high resolution first, then the entering-view adds.
         * Nothing on the wire says which npc a block belongs to; the client
         * replays its own queue, so the two orders have to be the same walk.
         */
        rsab_pdata(&buf, extended_data, rsab_len(&extended_buf));

        if( getenv("MOCK230_NPC_INFO_DEBUG") )
        {
            size_t const len = rsab_len(&buf);
            fprintf(stderr,
                    "mock230: NPC_INFO v5 tracked=%d kept=%d added=%d extended=%d len=%zu hex=",
                    player->tracked_count, kept_count, add_count, queued_count, len);
            for( size_t i = 0; i < len; i++ )
                fprintf(stderr, "%02x", buf.data[i]);
            fputc('\n', stderr);
        }
        flush(player, &buf, OP_NPC_INFO, 2);
        memcpy(player->tracked, kept, sizeof(int) * (size_t)kept_count);
        player->tracked_count = kept_count;
        return;
    }
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
        /* Level is part of range, the same way it is in `player_in_view`. It
         * was not, which was invisible while the entering-view scan was flat and
         * ignored level too; now that the candidates come from the ZoneMap —
         * which is keyed by level — an npc left tracked across a climb could
         * never be re-added, only re-encoded forever. */
        int in_range = npc->active && npc->level == player->level && dx >= -15 && dx <= 15 &&
                       dz >= -15 && dz <= 15;
        int extended = npc_extended_pending(npc);

        /*
         * `tele` is the npc half of the player section's `place_dirty`, and it
         * is here for the same reason: this section's four movement ops are
         * "nothing", one step, two steps, and remove, so a teleport *is* a
         * remove — and the entering-view loop below re-adds the npc, in the
         * same packet, at its new tile. Phase 8 refiles the ZoneMap before
         * anything is encoded, so the scan finds it there.
         *
         * Without this the observer's copy stays where the npc was until it
         * takes a step, and then walks on from the wrong tile — while the
         * server routes clicks to the tile it is really standing on. That is
         * an npc answering from somewhere other than where it is drawn.
         */
        if( !in_range || npc->tele )
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
        {
            queued_force_face[queued_count] = 0;
            queued[queued_count++] = slot;
        }
    }

    /*
     * --- npcs entering view ---
     *
     * The candidate set comes from the ZoneMap: the npcs standing in the zones
     * the add radius touches, which is at most 5x5 zones. It used to be every
     * slot in the world, per player, per tick — the scan that made the npc cap
     * and the wire's tracked-count field the same number (mock230.h).
     *
     * The zone query is coarse (a zone is 8 tiles, the radius is 15) so the
     * exact range test below still decides; what changed is how many npcs it is
     * asked about.
     */
    nearby_count = mock230_zone_npcs_near(srv, player->x, player->z, player->level, 15, nearby,
                                          MOCK230_TRACKED_NPC_MAX);
    for( int i = 0; i < nearby_count; i++ )
    {
        int slot = nearby[i];
        struct Mock230Npc* npc = &srv->npcs[slot];
        int dx, dz;
        if( !npc->active || player->npc_tracked[slot] )
            continue;
        dx = npc->x - player->x;
        dz = npc->z - player->z;
        if( dx < -15 || dx > 15 || dz < -15 || dz > 15 )
            continue;
        /* The tracked count is 8 bits, so 255 is the ceiling the *stream* has —
         * nothing to do with how many npcs the world holds. */
        if( kept_count >= MOCK230_TRACKED_NPC_MAX )
            break;

        /* Slot and type at the revision's own widths — 14 and 14 here; this
         * comment used to say "11-bit type" while the line below wrote
         * MOCK230_NPC_TYPE_BITS. Then 5-bit signed deltas from the local
         * player and 1-bit "extended info follows". No jump bit — unlike the
         * player stream's new-entity record. */
        {
            int force_face = npc->face_entity != -1;
            int extended = npc_extended_pending(npc) || force_face;

            rsab_pbit(&buf, MOCK230_NPC_SLOT_BITS, slot);
            rsab_pbit(&buf, MOCK230_NPC_TYPE_BITS, npc->type);
            rsab_pbit(&buf, 5, dx & 0x1f);
            rsab_pbit(&buf, 5, dz & 0x1f);
            rsab_pbit(&buf, 1, extended);
            if( extended )
            {
                queued_force_face[queued_count] = force_face;
                queued[queued_count++] = slot;
            }
        }
        player->npc_tracked[slot] = 1;
        kept[kept_count++] = slot;
    }

    rsab_pbit(&buf, MOCK230_NPC_SLOT_BITS, MOCK230_NPC_TERMINATOR);
    rsab_bytes(&buf);

    for( int i = 0; i < queued_count; i++ )
        put_npc_extended(&buf, &srv->npcs[queued[i]], queued_force_face[i]);

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
