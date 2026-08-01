/*
 * The rev-230 server hosted in-process: real client subsystems and a real
 * server, wired together by byte buffers and nothing else.
 *
 * No socket, no descriptor, no thread. `ToriRS_Network` was already
 * transport-agnostic — it takes bytes through HandleCmd(NET_RECV) and emits
 * them through PopOut — so the whole bridge is the pump loop below, moving each
 * side's output into the other's input.
 *
 * What this actually proves, and why it is worth a test rather than a demo:
 *
 *   1. The server's login handshake does not block. On one thread a blocking
 *      read is a deadlock, so if this test ever hangs, the state machine in
 *      mock230_session.c has regressed to waiting for bytes.
 *   2. The bytes are the real protocol. Each client encrypts a real login block
 *      with the server's real modulus and both ends arm real ISAAC ciphers;
 *      nothing is short-circuited for being in-process. A packet that would
 *      desync over TCP desyncs here too.
 *   3. Packets torn across reads are survivable. The feed below deliberately
 *      hands the server its bytes in small chunks, which is the case the old
 *      blocking reader printed "split var-u8 header" and gave up on.
 *   4. **Two clients in one world see each other.** This is the multiplayer
 *      check, and it is deliberately made against the *decoded* PLAYER_INFO
 *      rather than against server state: the server believing there are two
 *      players proves nothing about the stream, and every bug this change could
 *      introduce (an extended block attributed to the wrong entity, a missing
 *      terminator, a tracked list written in the wrong order) lives in the
 *      bitstream where only a reader can see it. `pkt_player_info_reader_read`
 *      is the client's own reader, so a stream it accepts is one the game
 *      accepts.
 *
 * Run: make -C src test-mock230-embed
 */

#include "net/mock/mock230.h"
#include "net/mock/mock230_content.h"
#include "net/mock/mock230_embed.h"
#include "net/mock/mock230_friends.h"
#include "net/mock/mock230_session.h"

#include "cmd/cmdbus.h"
#include "net/jbase37.h"
#include "net/net.h"
#include "net/net_out.h"
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "net/rev/packets/pkt_player_info.h"
#include "net/rev/pktnames.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    printf("embed: %-58s %s\n", what, condition ? "ok" : "FAILED");
    if( !condition )
        g_failures++;
}

/*
 * One client end: its network stack, its embed client id, and what its
 * PLAYER_INFO stream has said about anybody who is not itself.
 */
struct Peer
{
    struct ToriRS_Network net;
    int client_id;

    /** Pid this client was told about in a new-player record, or -1. */
    int saw_pid;
    /** Appearance blocks decoded for another player. */
    int saw_appearance;
    /** The name out of the most recent one. Checked because it is the only
     *  field in the blob that differs between the two players: an extended
     *  section written in the wrong order swaps two appearances that are
     *  otherwise byte-identical, and nothing else here would notice. */
    char saw_name[16];
    /** Walk/run direction ops decoded for another player, over every tick. */
    int saw_steps;
    /** Set if an op targeted a slot this client was never told about, which is
     *  the shape a mis-ordered extended section takes. */
    int saw_unknown_target;

    /** The client's own tracking list, carried between packets — the tracked
     *  section addresses players by their index in *last* tick's list, so a
     *  reader that does not keep it cannot resolve a single one. */
    int tracked[64];
    int tracked_count;

    /*
     * Zone state, kept exactly the way the real client keeps it (see
     * `rs_gameproto_exec.c`): a zone sub-packet carries no coordinate, only a
     * `pos` inside whichever zone the last UPDATE_ZONE_* header named. A reader
     * that does not carry that base between packets cannot place a single loc
     * change, which is also why getting the base wrong is not a decode failure —
     * it is loot landing a few tiles from the corpse.
     */
    int zone_base_x, zone_base_z;
    /** The most recent LOC_ADD_CHANGE this client's stream carried, resolved to
     *  an absolute tile, and how many it has seen. */
    int saw_loc_count;
    int saw_loc_x, saw_loc_z, saw_loc_id;

    /*
     * Social. Recorded per peer, out of each client's own decoded stream,
     * rather than off the server's capture buffer — and that is not a
     * preference. `mock230_encode.c`'s capture records (opcode, len, data) with
     * **no addressee**, so "the PM reached bob and not alice" is a question it
     * structurally cannot answer. Two decoded streams can.
     */
    /** UPDATE_FRIENDLIST: how many, and the most recent (name37, world). */
    int saw_friend_count;
    int64_t saw_friend_name37;
    int saw_friend_world;
    /** FRIENDLIST_LOADED's status byte, or -1 if none has arrived. */
    int saw_friendlist_loaded;
    /** UPDATE_IGNORELIST: how many packets, and the entry count of the last. */
    int saw_ignorelist_count;
    int saw_ignorelist_len;
    /** MESSAGE_PRIVATE: how many, and the fields of the most recent. */
    int saw_pm_count;
    int64_t saw_pm_from;
    int saw_pm_id;
    char saw_pm_text[128];
    /** CHAT_FILTER_SETTINGS: how many, and the last one's three modes. */
    int saw_filter_count;
    int saw_filter_public, saw_filter_private, saw_filter_trade;
    /*
     * MESSAGE_GAME: how many, and the last few lines.
     *
     * A ring rather than "the most recent", because the friend login/logout
     * notification shares this stream with everything `[login,_]` says and does
     * not arrive last: the social burst is step 5b of the login and `[login,_]`
     * runs a tick later. An assertion against only the newest line would pass
     * whether or not the notification was ever sent, which is a check that
     * cannot go red.
     */
    int saw_game_count;
    char saw_game_lines[8][160];

    /*
     * IF_SETEVENTS: which components the server armed, over the whole session.
     *
     * The only observable this test has for the arming half of the content diff
     * (`~friends_login`), because arming has no server-side state — it is one
     * packet and then it is the client's. Recorded from the first byte rather
     * than from a reset, since the arming happens inside the login burst.
     */
    int saw_setevents[192];
    int saw_setevents_count;
    int saw_setevents_overflow;
};

/** Did the server arm this component on this client? */
static int
peer_saw_setevents(
    const struct Peer* peer,
    int com_id)
{
    for( int i = 0; i < peer->saw_setevents_count; i++ )
        if( peer->saw_setevents[i] == com_id )
            return 1;
    return 0;
}

/** Did this client's chatbox get this exact game line since the last reset? */
static int
peer_saw_game(
    const struct Peer* peer,
    const char* text)
{
    int n = peer->saw_game_count;

    if( n > (int)(sizeof(peer->saw_game_lines) / sizeof(peer->saw_game_lines[0])) )
        n = (int)(sizeof(peer->saw_game_lines) / sizeof(peer->saw_game_lines[0]));
    for( int i = 0; i < n; i++ )
        if( strcmp(peer->saw_game_lines[i], text) == 0 )
            return 1;
    return 0;
}

/** Forget the lines so far, so a step asserts on what *it* produced. */
static void
peer_reset_game(struct Peer* peer)
{
    peer->saw_game_count = 0;
    memset(peer->saw_game_lines, 0, sizeof(peer->saw_game_lines));
}

/*
 * Deterministic client seeds, so a failing run reproduces exactly. The values
 * are arbitrary; what matters is that they do not change between runs and that
 * the two clients differ — identical ISAAC seeds would hide a server that mixed
 * the two sessions' ciphers up.
 */
static void
seed_a(
    void* user,
    int32_t* seed)
{
    (void)user;
    seed[0] = 0x13571357;
    seed[1] = 0x24682468;
    seed[2] = 0x0f0f0f0f;
    seed[3] = (int32_t)0xf0f0f0f0;
}

static void
seed_b(
    void* user,
    int32_t* seed)
{
    (void)user;
    seed[0] = 0x0badc0de;
    seed[1] = 0x51ced00d;
    seed[2] = 0x1a2b3c4d;
    seed[3] = 0x5e6f7a8b;
}

/* A third, for the client that arrives after the world has already changed. */
static void
seed_c(
    void* user,
    int32_t* seed)
{
    (void)user;
    seed[0] = 0x2b2b2b2b;
    seed[1] = 0x6c6c6c6c;
    seed[2] = 0x77889900;
    seed[3] = 0x0099aabb;
}

/*
 * Decode one PLAYER_INFO with the client's own reader and record what it says
 * about anyone who is not the local player.
 *
 * The op stream is a flat list with target-setting ops in it, so the walk below
 * is the same state machine `task_exec_entity_info.c` runs: whichever
 * ADD/SET/CLEAR op came last says who the following ops describe. `active[]` is
 * this client's rebuilt tracking order — the list an extended block is indexed
 * against — which is why a removal must not go into it.
 */
static void
absorb_player_info(
    struct Peer* peer,
    const uint8_t* data,
    int length)
{
    static struct PktPlayerInfoOp ops[512];
    struct PktPlayerInfoReader reader;
    /* This packet's list, which becomes `peer->tracked` at the end. */
    int next[64];
    int next_count = 0;
    /* The old-section entry whose fate the following ops decide. */
    int pending = -1;
    int local = 1;
    int target = -1;
    int n;

    memset(&reader, 0, sizeof(reader));
    n = pkt_player_info_reader_read(&reader, data, length, ops,
                                    (int)(sizeof(ops) / sizeof(ops[0])));

    for( int i = 0; i < n; i++ )
    {
        struct PktPlayerInfoOp* op = &ops[i];

        switch( op->kind )
        {
        case PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER:
            local = 1;
            target = -1;
            break;
        case PKT_PLAYER_INFO_OPBITS_COUNT_RESET:
            next_count = 0;
            break;
        case PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX:
            /* An index into *last* packet's list, not a pid. Held rather than
             * appended: the ops that follow say whether this entry stays, and a
             * removed one is not in the order extended blocks index against. */
            local = 0;
            pending = (int)op->_bitvalue < peer->tracked_count
                          ? peer->tracked[op->_bitvalue]
                          : -1;
            if( pending < 0 )
                peer->saw_unknown_target = 1;
            target = pending;
            break;
        case PKT_PLAYER_INFO_OPBITS_INFO:
            /* Emitted for the local player too; only an old-section entry is
             * pending. */
            if( pending >= 0 && next_count < (int)(sizeof(next) / sizeof(next[0])) )
                next[next_count++] = pending;
            pending = -1;
            break;
        case PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID:
            local = 0;
            target = (int)op->_bitvalue;
            peer->saw_pid = target;
            if( next_count < (int)(sizeof(next) / sizeof(next[0])) )
                next[next_count++] = target;
            break;
        case PKT_PLAYER_INFO_OP_SET_PLAYER_OPBITS_IDX:
            local = 0;
            target = (int)op->_bitvalue < next_count ? next[op->_bitvalue] : -1;
            if( target < 0 )
                peer->saw_unknown_target = 1;
            break;
        case PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX:
            /* A removal. The INFO op just counted this entry into the order, so
             * take it back out. */
            if( next_count > 0 )
                next_count--;
            local = 0;
            target = -1;
            break;
        case PKT_PLAYER_INFO_OPBITS_WALKDIR:
        case PKT_PLAYER_INFO_OPBITS_RUNDIR:
            if( !local && target >= 0 )
                peer->saw_steps++;
            break;
        case PKT_PLAYER_INFO_OP_APPEARANCE:
            if( !local && target >= 0 )
            {
                struct PktPlayerAppearance decoded;

                peer->saw_appearance++;
                memset(&decoded, 0, sizeof(decoded));
                if( PktPlayerAppearance_Decode(&decoded, op->_appearance.appearance,
                                               op->_appearance.len) )
                    snprintf(peer->saw_name, sizeof(peer->saw_name), "%s", decoded.name);
            }
            break;
        default:
            break;
        }
    }

    memcpy(peer->tracked, next, sizeof(int) * (size_t)next_count);
    peer->tracked_count = next_count;

    pkt_player_info_ops_free(ops, n);
}

/*
 * Record what a zone packet said, the way the client's own exec does.
 *
 * `origin_x`/`origin_z` are the absolute tile of the scene's south-west corner:
 * the wire
 * base is scene-local, so this is what turns "three tiles into zone 5" back into
 * a tile the test can compare against the door's own coordinates. The three
 * headers all set the base; only FULL_FOLLOWS also means "forget what you held
 * here", which is what makes the replay that follows it exact.
 */
static void
absorb_zone(
    struct Peer* peer,
    const struct RevPacket* packet,
    int origin_x,
    int origin_z)
{
    switch( packet->packet_type )
    {
    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS:
        peer->zone_base_x = packet->_update_zone_partial_follows.base_x;
        peer->zone_base_z = packet->_update_zone_partial_follows.base_z;
        break;
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
        peer->zone_base_x = packet->_update_zone_full_follows.base_x;
        peer->zone_base_z = packet->_update_zone_full_follows.base_z;
        break;
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED:
    {
        const struct PktUpdateZoneEnclosed* enc = &packet->_update_zone_enclosed;

        peer->zone_base_x = enc->base_x;
        peer->zone_base_z = enc->base_z;
        for( int i = 0; i < enc->count; i++ )
        {
            if( enc->entries[i].name != PKT_NAME_LOC_ADD_CHANGE )
                continue;
            peer->saw_loc_count++;
            peer->saw_loc_id = enc->entries[i]._loc_add_change.loc_id;
            peer->saw_loc_x =
                origin_x + peer->zone_base_x + (enc->entries[i]._loc_add_change.pos >> 4);
            peer->saw_loc_z =
                origin_z + peer->zone_base_z + (enc->entries[i]._loc_add_change.pos & 7);
        }
        break;
    }
    case PKT_NAME_LOC_ADD_CHANGE:
        peer->saw_loc_count++;
        peer->saw_loc_id = packet->_loc_add_change.loc_id;
        peer->saw_loc_x = origin_x + peer->zone_base_x + (packet->_loc_add_change.pos >> 4);
        peer->saw_loc_z = origin_z + peer->zone_base_z + (packet->_loc_add_change.pos & 7);
        break;
    default:
        break;
    }
}

/*
 * Record the five social packets.
 *
 * Returns 1 if it consumed the packet, so the caller can leave the zone reader
 * alone. Nothing here interprets: it stores what the *client's own* decoder
 * (src/net/rev/gameproto_parse.c) produced, which is the half that has to agree
 * with the server's encoders.
 */
static int
absorb_social(
    struct Peer* peer,
    const struct RevPacket* packet)
{
    switch( packet->packet_type )
    {
    case PKT_NAME_UPDATE_FRIENDLIST:
        peer->saw_friend_count++;
        peer->saw_friend_name37 = packet->_update_friendlist.name37;
        peer->saw_friend_world = packet->_update_friendlist.world;
        return 1;
    case PKT_NAME_FRIENDLIST_LOADED:
        peer->saw_friendlist_loaded = packet->_friendlist_loaded.status;
        return 1;
    case PKT_NAME_UPDATE_IGNORELIST:
        peer->saw_ignorelist_count++;
        peer->saw_ignorelist_len = packet->_update_ignorelist.count;
        return 1;
    case PKT_NAME_MESSAGE_PRIVATE:
        peer->saw_pm_count++;
        peer->saw_pm_from = packet->_message_private.from;
        peer->saw_pm_id = packet->_message_private.message_id;
        snprintf(peer->saw_pm_text, sizeof(peer->saw_pm_text), "%s",
                 packet->_message_private.text ? packet->_message_private.text : "");
        return 1;
    case PKT_NAME_IF_SETEVENTS:
        if( peer->saw_setevents_count <
            (int)(sizeof(peer->saw_setevents) / sizeof(peer->saw_setevents[0])) )
            peer->saw_setevents[peer->saw_setevents_count++] =
                packet->_if_setevents.component_id;
        else
            peer->saw_setevents_overflow = 1;
        return 1;
    case PKT_NAME_MESSAGE_GAME:
    {
        int slot = peer->saw_game_count %
                   (int)(sizeof(peer->saw_game_lines) / sizeof(peer->saw_game_lines[0]));

        snprintf(peer->saw_game_lines[slot], sizeof(peer->saw_game_lines[slot]), "%s",
                 packet->_message_game.text ? packet->_message_game.text : "");
        peer->saw_game_count++;
        return 1;
    }
    case PKT_NAME_CHAT_FILTER_SETTINGS:
        peer->saw_filter_count++;
        peer->saw_filter_public = packet->_chat_filter_settings.chat_public_mode;
        peer->saw_filter_private = packet->_chat_filter_settings.chat_private_mode;
        peer->saw_filter_trade = packet->_chat_filter_settings.chat_trade_mode;
        return 1;
    default:
        return 0;
    }
}

/*
 * Move one round of bytes in both directions for every peer, then let the
 * server act.
 *
 * `chunk` caps how much of a client's output is handed over at once. Feeding it
 * in pieces is the point: it forces the server to re-enter its reader with
 * partial packets, which is exactly what a real socket does under load and what
 * the old blocking reader could not survive.
 */
static void
pump(
    struct Peer* peers,
    int peer_count,
    struct Mock230Embed* embed,
    int run_tick,
    int chunk)
{
    struct Mock230Server* world;
    int origin_x;
    int origin_z;
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    static uint8_t inbound[65536];
    int got;

    /* clients -> server */
    for( int p = 0; p < peer_count; p++ )
    {
        while( ToriRS_Network_PopOut(&peers[p].net, &header, payload) )
        {
            if( header.type != TORIRS_NET_OUT_SEND_DATA )
                continue; /* CONNECT carries a host:port an embed has no use for */
            for( int off = 0; off < header.length; off += chunk )
            {
                int len = header.length - off;

                if( len > chunk )
                    len = chunk;
                mock230_embed_write(embed, peers[p].client_id, payload + off, len);
                /* Pump between chunks so the server genuinely sees a torn stream
                 * rather than a whole packet reassembled by the pipe. */
                mock230_embed_pump(embed, 0);
            }
        }
    }

    mock230_embed_pump(embed, run_tick);

    /* The scene's south-west corner, which is what the zone packets' bases are
     * relative to. Read after the tick, because the tick is what can move it. */
    world = mock230_embed_world(embed);
    origin_x = world ? mock230_scene_origin(world->zone_x) : 0;
    origin_z = world ? mock230_scene_origin(world->zone_z) : 0;

    /* server -> clients */
    for( int p = 0; p < peer_count; p++ )
    {
        struct Peer* peer = &peers[p];
        struct RevPacket packet;

        while( (got = mock230_embed_read(embed, peer->client_id, inbound,
                                         (int)sizeof(inbound))) > 0 )
            ToriRS_Network_HandleCmd(&peer->net, TORIRS_CMD_NET_RECV, inbound, got);

        /* Drain the parsed FIFO every round: PLAYER_INFO carries a heap payload
         * and the queue is finite, so a test that only looked at the end would
         * be asserting on whatever survived rather than on the whole session. */
        while( ToriRS_Network_PopPacket(&peer->net, &packet) )
        {
            if( packet.packet_type == PKT_NAME_PLAYER_INFO )
                absorb_player_info(peer, packet._player_info.data, packet._player_info.length);
            else if( !absorb_social(peer, &packet) )
                absorb_zone(peer, &packet, origin_x, origin_z);
            gameproto_free(&packet);
        }
    }
}

static void
peer_login(
    struct Peer* peer,
    int client_id,
    void (*seed_fn)(void*, int32_t*),
    const char* name)
{
    memset(peer, 0, sizeof(*peer));
    peer->client_id = client_id;
    peer->saw_pid = -1;
    /* 0 is a real FRIENDLIST_LOADED status ("loading"), so "none yet" needs its
     * own value or the check below would pass before the packet existed. */
    peer->saw_friendlist_loaded = -1;

    ToriRS_Network_Init(&peer->net, GameProtoRev_OSRS230(), MOCK230_RSA_PUBLIC_EXPONENT,
                        MOCK230_RSA_PUBLIC_MODULUS);
    ToriRS_Network_SetSeedFn(&peer->net, seed_fn, NULL);
    /* The host:port is inert here — there is nothing to dial — but the client's
     * login machine wants one, and passing a real-looking value keeps this path
     * identical to the socket one. */
    ToriRS_Network_ConnectLogin(&peer->net, "embedded:0", name, name);
    /* The client will not send its login block until it believes it is
     * connected, because the socket layer normally reports that. In-process
     * there is no connect to wait for, so say so directly. */
    ToriRS_Network_HandleCmd(&peer->net, TORIRS_CMD_NET_STATUS, NULL, 0);
    {
        uint8_t status = TORIRS_NET_STATUS_CONNECTED;
        ToriRS_Network_HandleCmd(&peer->net, TORIRS_CMD_NET_STATUS, &status, 1);
    }
}

/** Send a real MOVE_GAMECLICK: the same encoder app.c uses for a ground click,
 *  through the same ISAAC stream. A one-waypoint route is a walk to that tile,
 *  and a zero scene base makes the waypoint absolute. */
static void
peer_walk_to(
    struct Peer* peer,
    int abs_x,
    int abs_z)
{
    uint8_t buf[64];
    int route_x[1] = { abs_x };
    int route_z[1] = { abs_z };
    int n = net_out_move_gameclick(peer->net.rev, peer->net.random_out, buf, (int)sizeof(buf), 0,
                                   0, route_x, route_z, 1, 0);

    assert(n > 0);
    ToriRS_Network_SendRaw(&peer->net, buf, n);
}

/** Send a real OPLOC1 — the "Open" click on a door — through the same encoder
 *  and the same ISAAC stream a real client uses. */
static void
peer_oploc(
    struct Peer* peer,
    int abs_x,
    int abs_z,
    int loc_id)
{
    uint8_t buf[64];
    int n = net_out_oploc(peer->net.rev, peer->net.random_out, buf, (int)sizeof(buf), 1, abs_x,
                          abs_z, loc_id);

    assert(n > 0);
    ToriRS_Network_SendRaw(&peer->net, buf, n);
}

/*
 * The four social sends, through the *client's* own builders and its own ISAAC
 * stream — the same functions app.c calls. Asserting on a hand-rolled frame
 * would prove nothing about whether the client and the server agree, which is
 * the only thing these packets have to do.
 *
 * `net_out_*` returns -1 when the revision has no opcode for the packet, which
 * is exactly what every one of these did before this stage; the assert is what
 * makes a missing packetout.h row a failure here rather than silence.
 */
static void
peer_social_name37(
    struct Peer* peer,
    int which,
    const char* target)
{
    uint8_t buf[64];
    int64_t name37 = (int64_t)strtobase37(target);
    int n = -1;

    switch( which )
    {
    case PKTOUT_NAME_FRIENDLIST_ADD:
        n = net_out_friendlist_add(peer->net.rev, peer->net.random_out, buf, (int)sizeof(buf),
                                   name37);
        break;
    case PKTOUT_NAME_FRIENDLIST_DEL:
        n = net_out_friendlist_del(peer->net.rev, peer->net.random_out, buf, (int)sizeof(buf),
                                   name37);
        break;
    case PKTOUT_NAME_IGNORELIST_ADD:
        n = net_out_ignorelist_add(peer->net.rev, peer->net.random_out, buf, (int)sizeof(buf),
                                   name37);
        break;
    case PKTOUT_NAME_IGNORELIST_DEL:
        n = net_out_ignorelist_del(peer->net.rev, peer->net.random_out, buf, (int)sizeof(buf),
                                   name37);
        break;
    default:
        break;
    }
    assert(n > 0);
    ToriRS_Network_SendRaw(&peer->net, buf, n);
}

static void
peer_pm(
    struct Peer* peer,
    const char* target,
    const char* text)
{
    uint8_t buf[128];
    int n = net_out_message_private(peer->net.rev, peer->net.random_out, buf, (int)sizeof(buf),
                                    (int64_t)strtobase37(target), text);

    assert(n > 0);
    ToriRS_Network_SendRaw(&peer->net, buf, n);
}

static void
peer_chat_setmode(
    struct Peer* peer,
    int public_mode,
    int private_mode,
    int trade_mode)
{
    uint8_t buf[64];
    int n = net_out_chat_setmode(peer->net.rev, peer->net.random_out, buf, (int)sizeof(buf),
                                 public_mode, private_mode, trade_mode);

    assert(n > 0);
    ToriRS_Network_SendRaw(&peer->net, buf, n);
}

int
main(void)
{
    /* Three: alice and bob, who are here when the door opens, and carol, who is
     * not. Carol is the whole point of the zone map. */
    struct Peer peers[3];
    struct Mock230Embed* embed;
    struct Mock230Server* world;
    struct Mock230Player* alice;
    struct Mock230Player* bob;
    int second_client;
    int reached_game = 0;

    embed = mock230_embed_start();
    if( !embed )
    {
        fprintf(stderr, "embed: could not start the server\n");
        return 1;
    }

    second_client = mock230_embed_connect(embed);
    check(second_client == 1, "the embed opened a second client");
    if( second_client < 0 )
    {
        mock230_embed_stop(embed);
        return 1;
    }

    peer_login(&peers[0], 0, seed_a, "alice");
    peer_login(&peers[1], second_client, seed_b, "bob");
    check(peers[0].net.rsa_ready && peers[1].net.rsa_ready,
          "both clients armed the server's public key");
    check(peers[0].net.state == TORIRS_NET_LOGIN, "both clients entered LOGIN");

    /*
     * Drive it. Sixty rounds is a hundred times what the handshake needs and
     * still a fraction of a second — the loop exists to give the login burst
     * somewhere to land, not to wait for anything.
     */
    for( int round = 0; round < 60; round++ )
    {
        pump(peers, 2, embed, round % 2 == 0, 7);
        if( peers[0].net.state == TORIRS_NET_GAME && peers[1].net.state == TORIRS_NET_GAME )
            reached_game = 1;
    }

    check(reached_game, "both clients reached GAME over their buffer pairs");
    check(mock230_embed_online(embed, 0) && mock230_embed_online(embed, second_client),
          "the server completed both handshakes");

    world = mock230_embed_world(embed);
    alice = mock230_embed_player(embed, 0);
    bob = mock230_embed_player(embed, second_client);
    check(world != NULL, "server exposes its world");
    if( !world || !alice || !bob )
    {
        printf("embed: FAILURES (no world, or a login took no pool slot)\n");
        mock230_embed_stop(embed);
        return 1;
    }

    check(alice != bob, "the two logins took two pool slots");
    check(alice->pid == 0 && bob->pid == 1, "with pids 0 and 1");
    check(world->player_count == 2, "and the world holds two players");
    check(strcmp(alice->display_name, "alice") == 0 && strcmp(bob->display_name, "bob") == 0,
          "each login name reached its own player");

    check(world->tick > 0, "the world ticked");
    check(alice->x > 0 && alice->z > 0 && bob->x > 0 && bob->z > 0,
          "both players are on a tile");

    /*
     * The opening fixture, over a real login rather than a direct call.
     *
     * `mock230_world_init` used to deal the kit, stock the bank and set the
     * stats; all three are content now, in `[proc,newplayer_setup]`, which
     * `[login,_]` calls in phase 7. That makes this the end-to-end check the
     * move needs: a client that handshakes, logs in and ticks should end up
     * holding exactly what the C used to hand it — and *both* clients should,
     * which is also the check that a second login runs [login] for the newcomer
     * rather than a second time for whoever was already here.
     */
    for( int p = 0; p < 2; p++ )
    {
        struct Mock230Player* player = p == 0 ? alice : bob;
        int kit = 0;

        for( int i = 0; i < MOCK230_INV_SLOTS; i++ )
            if( player->inv[i].obj_id >= 0 )
                kit++;
        check(kit == 14, p == 0 ? "[login] dealt alice the 14-item opening kit"
                                : "and dealt bob the same kit");
        check(mock230_bank_count_player(player, 995) == 250000,
              p == 0 ? "[login] stocked alice's bank with 250000 coins"
                     : "and bob's, in his own container");
        check(player->stat_level[MOCK230_STAT_HITPOINTS] == 10,
              p == 0 ? "[login] put alice's hitpoints at level 10" : "and bob's");
        check(player->stat_level[MOCK230_STAT_ATTACK] == 1,
              "and left every other skill on the engine's level-1 floor");
    }

    /* Separate allocations — one pointer would mean one player's deposit
     * showing up in the other's bank. */
    check(alice->bank.slots != bob->bank.slots, "the two players have separate banks");

    /*
     * ── Do they see each other? ──────────────────────────────────────
     *
     * Both log in on the home tile, so each is inside the other's 15-tile add
     * radius before anybody moves.
     */
    check(peers[0].saw_pid == bob->pid, "alice's client was told about bob");
    check(peers[1].saw_pid == alice->pid, "bob's client was told about alice");
    check(peers[0].saw_appearance > 0 && peers[1].saw_appearance > 0,
          "each got the other's appearance with the spawn");
    /* The blob has to be the *other* player's. Two appearances in one packet
     * are byte-identical apart from the name, so this is what distinguishes
     * "the extended section is in the bit section's order" from "there are the
     * right number of blocks". */
    check(strcmp(peers[0].saw_name, "bob") == 0,
          "and alice's copy of it names bob");
    check(strcmp(peers[1].saw_name, "alice") == 0, "and bob's names alice");
    check(!peers[0].saw_unknown_target && !peers[1].saw_unknown_target,
          "and no extended block landed on an untracked entity");

    /*
     * ── Do they see each other *move*? ───────────────────────────────
     *
     * A real MOVE_GAMECLICK from alice's client, and then bob's decoded
     * PLAYER_INFO has to carry step directions for alice's pid. This is the
     * whole point of the change: with the world's single tracked set, bob's
     * stream described alice's view and never alice.
     */
    {
        int start_x = alice->x;
        int bob_start_x = bob->x;
        int steps_before = peers[1].saw_steps;

        peer_walk_to(&peers[0], alice->x + 4, alice->z);
        for( int round = 0; round < 20; round++ )
            pump(peers, 2, embed, 1, 64);

        check(alice->x != start_x, "alice walked");
        check(bob->x == bob_start_x, "and bob did not — one client's click moves one player");
        check(peers[1].saw_steps > steps_before,
              "bob's client decoded alice's steps out of PLAYER_INFO");
        check(!peers[1].saw_unknown_target, "with every step attributed to a tracked player");
    }

    /*
     * ── And when one of them teleports? ──────────────────────────────
     *
     * The tracked section's four movement ops are "nothing", one step, two
     * steps and remove; none of them is a placement, so a teleport has to be
     * spelled as remove-then-re-add. The observable end of that is a second
     * new-player record for a pid the client already had — which is also a fresh
     * appearance, so the name check below is what proves the re-add happened
     * rather than the packet simply having gone quiet.
     */
    {
        int appearances_before = peers[1].saw_appearance;

        peers[1].saw_name[0] = '\0';
        /* A host saying whose turn it is, which is what the seam is for: this
         * stands in for the `::tele` cheat or a world-map click arriving on
         * alice's session. */
        mock230_world_set_active(world, alice);
        mock230_world_teleport(world, alice->level, alice->x + 6, alice->z + 6);
        for( int round = 0; round < 6; round++ )
            pump(peers, 2, embed, 1, 64);

        check(peers[1].saw_appearance > appearances_before,
              "bob's client was re-told about alice after she teleported");
        check(strcmp(peers[1].saw_name, "alice") == 0, "and it was still alice");
        check(!peers[1].saw_unknown_target, "with the remove and the re-add in step");
    }

    /*
     * ── Does a door one player opens stay open for the next one? ─────
     *
     * The check the ZoneMap exists for, and the half a broadcast could never
     * do. A loc change used to be sent to whoever was standing there at the
     * time and to nobody else, ever: the server kept no record a later client
     * could be caught up from, and REBUILD_NORMAL hands that client a scene
     * straight out of the cache with every door shut. So the two halves are
     * asserted separately —
     *
     *   1. bob, who is already connected, is told when alice opens it. That is
     *      the broadcast's job and it worked before.
     *   2. carol, who connects afterwards, is told the same thing without
     *      anybody touching the door again. That is the replay, and it is new.
     *
     * Both are read out of the *decoded* stream rather than off the server, for
     * the same reason the PLAYER_INFO checks above are: the server believing the
     * door is open proves nothing about what a client would draw.
     *
     * The door is the one the selftest uses — Lumbridge castle's courtyard door
     * at 3226,3223, which the content tree pairs 1535 (closed) with 1536 (open).
     */
    {
        const int door_x = 3226;
        const int door_z = 3223;
        const int door_closed = 1535;
        const int door_open = 1536;
        int third;
        int carol_saw_before;

        peers[0].saw_loc_count = 0;
        peers[1].saw_loc_count = 0;

        /* Put alice beside it and let the placement settle before she clicks —
         * `handle_oploc` walks first and acts on arrival, so a click sent from
         * across Lumbridge would resolve some ticks later and make the rest of
         * this section depend on how many rounds were pumped. */
        mock230_world_set_active(world, alice);
        mock230_world_teleport(world, 0, door_x - 1, door_z);
        for( int round = 0; round < 4; round++ )
            pump(peers, 2, embed, 1, 64);

        peer_oploc(&peers[0], door_x, door_z, door_closed);
        for( int round = 0; round < 6; round++ )
            pump(peers, 2, embed, 1, 64);

        check(peers[0].saw_loc_id == door_open && peers[0].saw_loc_count > 0,
              "alice's own client was told the door opened");
        check(peers[0].saw_loc_x == door_x && peers[0].saw_loc_z == door_z,
              "on the door's own tile, so the zone base survived the wire");
        check(peers[1].saw_loc_count > 0 && peers[1].saw_loc_id == door_open,
              "and so was bob, who was standing somewhere else entirely");

        /* ── The late arrival ── */
        third = mock230_embed_connect(embed);
        check(third >= 0, "the embed opened a third client");
        if( third >= 0 )
        {
            peer_login(&peers[2], third, seed_c, "carol");
            carol_saw_before = peers[2].saw_loc_count;
            for( int round = 0; round < 40; round++ )
                pump(peers, 3, embed, round % 2 == 0, 7);

            check(mock230_embed_online(embed, third), "carol handshaked into the same world");
            check(peers[2].saw_loc_count > carol_saw_before,
                  "carol's client was told about a loc it never saw change");
            check(peers[2].saw_loc_id == door_open,
                  "and it was the open door, not the closed one");
            check(peers[2].saw_loc_x == door_x && peers[2].saw_loc_z == door_z,
                  "on the same tile alice opened");

            ToriRS_Network_Free(&peers[2].net);
        }
    }

    /*
     * ── Friends, ignore and private chat ──────────────────────────────
     *
     * The routing proof for docs/FRIENDS_PRIVATE_CHAT.md §3 (the wire). Every
     * send below goes through the *client's* own net_out builder and every
     * assertion reads the *client's* own decoder, so what is being tested is
     * that the two halves agree — not that the server agrees with itself.
     *
     * Two properties of the server shape this section:
     *
     *  - `socialProtect` allows one social packet per player per tick, so each
     *    step pumps a few rounds before the next send rather than batching them.
     *  - the friend roster is process-scoped and survives a logout, while
     *    *delivery* needs a live player slot. That is why the logout step at the
     *    end asserts on what alice receives, not on what bob does.
     *
     * `mock230_friends_reset()` is deliberately NOT called first: the door
     * section above left no social state, and a reset here would also throw
     * away the presence the two logins registered.
     */
    {
        int64_t alice37 = (int64_t)strtobase37("alice");
        int64_t bob37 = (int64_t)strtobase37("bob");

        /* ── What the login burst already sent ── */
        check(peers[0].saw_friendlist_loaded == 2,
              "alice's login told her client the friend list finished loading");
        check(peers[0].saw_ignorelist_count > 0 && peers[0].saw_ignorelist_len == 0,
              "and handed it an ignore list, empty but stated");
        check(peers[0].saw_filter_count > 0,
              "and the three chat filter modes the client's UI reads");

        /*
         * ── the caps are content's numbers ──
         *
         * Not a number typed here: the check reads the `.constant` the engine
         * reads and asserts the service is using it. Skipped when the content
         * tree does not declare it, because this lane specified that file
         * (docs/FRIENDS_PRIVATE_CHAT_CONTENT.md) but does not own the tree — and
         * a skip that says so is better than a check that quietly asserts the
         * fallback ceiling is 256.
         */
        /*
         * ── the panel swap is armed ──
         *
         * `friends:ignore` and `ignore:friends` are the only two components in
         * this whole feature that need arming: they carry an op1 with no cache
         * `onop=`, so at rev 230 they are server buttons and are inert until
         * IF_SETEVENTS says otherwise. Everything else on both panels is pure
         * clientscript. This is the one thing the arming half of the content
         * diff (`~friends_login`) can be observed doing, because arming leaves
         * no server-side state — it is one packet and then it is the client's.
         *
         * The component id is resolved through the pack, never written here.
         */
        {
            int swap_to_ignore = mock230_content_symbol(MOCK230_PACK_COMPONENT, "friends:ignore");
            int swap_to_friends = mock230_content_symbol(MOCK230_PACK_COMPONENT, "ignore:friends");

            check(!peers[0].saw_setevents_overflow,
                  "the arming recorder kept up with the login burst");
            if( swap_to_ignore >= 0 && swap_to_friends >= 0 &&
                (peer_saw_setevents(&peers[0], swap_to_ignore) ||
                 peer_saw_setevents(&peers[0], swap_to_friends)) )
            {
                check(peer_saw_setevents(&peers[0], swap_to_ignore),
                      "friends:ignore is armed, so the ignore panel is reachable");
                check(peer_saw_setevents(&peers[0], swap_to_friends),
                      "and ignore:friends, so it is escapable");
            }
            else
            {
                printf("embed: SKIP  the panel-swap arming — "
                       "~friends_login is not in this content tree\n");
            }
        }

        {
            const char* declared = mock230_content_constant("friend_max");

            if( declared )
                check(mock230_friends_cap_friends() == atoi(declared),
                      "^friend_max is what the service caps at");
            else
                printf("embed: SKIP  ^friend_max — not in this content tree\n");
        }

        /* ── alice adds bob ── */
        peers[0].saw_friend_count = 0;
        peers[1].saw_friend_count = 0;
        peer_social_name37(&peers[0], PKTOUT_NAME_FRIENDLIST_ADD, "bob");
        for( int round = 0; round < 6; round++ )
            pump(peers, 2, embed, 1, 64);

        check(mock230_friends_is_friend(alice37, bob37),
              "FRIENDLIST_ADD reached the service (it had no rev-230 opcode before)");
        check(peers[0].saw_friend_count > 0 && peers[0].saw_friend_name37 == bob37,
              "and alice's client was told about bob");
        check(peers[0].saw_friend_world != 0, "with a non-zero world, because bob is online");
        check(peers[1].saw_friend_count == 0,
              "bob heard nothing — he does not have alice in his list");

        /* ── the private message ── */
        peers[0].saw_pm_count = 0;
        peers[1].saw_pm_count = 0;
        /*
         * "hi there" is eight characters, all of them in the wordpack table's
         * low-13 single-nibble set. That is deliberate: an odd nibble count
         * pads the last byte with a zero nibble, which decodes as a trailing
         * space, and a message chosen without noticing that would look like a
         * round-trip bug. The capitalisation below is the same kind of thing —
         * `wordpack_unpack` sentence-cases, exactly as the reference's
         * WordPack.unpack does, so the text alice typed is not the text bob is
         * shown, and that is correct.
         *
         * The text is unpacked twice on the way: once by the server (which is
         * what the reference does too, so it has read what it forwards) and
         * once by bob's client.
         */
        peer_pm(&peers[0], "bob", "hi there");
        for( int round = 0; round < 6; round++ )
            pump(peers, 2, embed, 1, 64);

        check(peers[1].saw_pm_count == 1, "alice's PM reached bob");
        check(peers[1].saw_pm_from == alice37, "from alice");
        check(peers[1].saw_pm_id != 0,
              "with a non-zero message id, which the client's dedupe ring requires");
        check(strcmp(peers[1].saw_pm_text, "Hi there") == 0,
              "and the text survived two wordpack round trips");
        check(peers[0].saw_pm_count == 0, "and it did not also come back to alice");

        /* ── bob adds alice, so alice has a follower to broadcast to ── */
        peers[1].saw_friend_count = 0;
        peer_social_name37(&peers[1], PKTOUT_NAME_FRIENDLIST_ADD, "alice");
        for( int round = 0; round < 6; round++ )
            pump(peers, 2, embed, 1, 64);
        check(peers[1].saw_friend_count > 0 && peers[1].saw_friend_name37 == alice37 &&
                  peers[1].saw_friend_world != 0,
              "bob's client now says alice is online");

        /* ── "Private chat: off" hides alice from her own friends ── */
        peers[1].saw_friend_count = 0;
        peers[0].saw_filter_count = 0;
        peer_chat_setmode(&peers[0], 0, MOCK230_CHAT_PRIVATE_OFF, 0);
        for( int round = 0; round < 6; round++ )
            pump(peers, 2, embed, 1, 64);

        check(peers[0].saw_filter_count > 0 &&
                  peers[0].saw_filter_private == MOCK230_CHAT_PRIVATE_OFF,
              "CHAT_SETMODE is echoed back as CHAT_FILTER_SETTINGS");
        check(peers[1].saw_friend_count > 0 && peers[1].saw_friend_name37 == alice37 &&
                  peers[1].saw_friend_world == 0,
              "and alice going private-chat-off reads as offline in bob's panel");

        peers[1].saw_friend_count = 0;
        peer_chat_setmode(&peers[0], 0, MOCK230_CHAT_PRIVATE_ON, 0);
        for( int round = 0; round < 6; round++ )
            pump(peers, 2, embed, 1, 64);
        check(peers[1].saw_friend_world != 0, "and turning it back on restores her");

        /* ── ignoring somebody hides you from them ── */
        peers[0].saw_friend_count = 0;
        peer_social_name37(&peers[1], PKTOUT_NAME_IGNORELIST_ADD, "alice");
        for( int round = 0; round < 6; round++ )
            pump(peers, 2, embed, 1, 64);

        check(mock230_friends_is_ignored(bob37, alice37), "IGNORELIST_ADD reached the service");
        check(peers[0].saw_friend_count > 0 && peers[0].saw_friend_name37 == bob37 &&
                  peers[0].saw_friend_world == 0,
              "and bob ignoring alice makes him read offline in her panel");

        /* The reference does NOT drop an ignored sender's private message
         * server-side — the client does. Asserting delivery is what pins that
         * decision down; if someone later "fixes" it here, this fails. */
        peers[1].saw_pm_count = 0;
        peer_pm(&peers[0], "bob", "still here");
        for( int round = 0; round < 6; round++ )
            pump(peers, 2, embed, 1, 64);
        check(peers[1].saw_pm_count == 1,
              "an ignored sender's PM is still delivered (the client filters, not the server)");

        peers[0].saw_friend_count = 0;
        peer_social_name37(&peers[1], PKTOUT_NAME_IGNORELIST_DEL, "alice");
        for( int round = 0; round < 6; round++ )
            pump(peers, 2, embed, 1, 64);
        check(peers[0].saw_friend_count > 0 && peers[0].saw_friend_world != 0,
              "and un-ignoring her brings him back online");

        /* ── the delete ── */
        peers[1].saw_friend_count = 0;
        peer_social_name37(&peers[1], PKTOUT_NAME_FRIENDLIST_DEL, "alice");
        for( int round = 0; round < 6; round++ )
            pump(peers, 2, embed, 1, 64);
        check(!mock230_friends_is_friend(bob37, alice37), "FRIENDLIST_DEL reached the service");

        /* ── the logout ──
         *
         * Through `mock230_embed_disconnect`, which is the socket server's own
         * close sequence (release the pool slot, free the session, free the byte
         * queues) rather than a bare `mock230_world_remove_player` — the bare
         * form left the embed holding a session whose player was gone, which
         * only survived because nothing pumped bob afterwards.
         *
         * Bob's slot goes and alice, who still has him listed, is told. After
         * this only alice is pumped: bob has no client id left. */
        peers[0].saw_friend_count = 0;
        peer_reset_game(&peers[0]);
        check(mock230_embed_disconnect(embed, second_client), "bob's client closed");
        bob = NULL;
        for( int round = 0; round < 4; round++ )
            pump(peers, 1, embed, 1, 64);

        check(peers[0].saw_friend_count > 0 && peers[0].saw_friend_name37 == bob37 &&
                  peers[0].saw_friend_world == 0,
              "bob logging out reaches alice as world 0");
        check(mock230_friends_is_friend(alice37, bob37),
              "and the list itself outlives the session, so he is still listed");

        /*
         * ── the sentence, which is content's ──
         *
         * The engine decides who hears it and hands over the name; the wording
         * is `[proc,friend_logout_notification]` in the content tree. So this
         * check is really two: that the engine reached content at all, and that
         * it reached it with the right player active — a `mes` inside that proc
         * writes to whoever the engine made active, and getting that wrong sends
         * bob's logout line to bob.
         *
         * Skipped, loudly, when the proc is absent: this lane specified the
         * content diff (docs/FRIENDS_PRIVATE_CHAT_CONTENT.md) but does not own
         * the content tree, so a tree without it must not fail here. Run with
         * MOCK230_CONTENT pointed at a tree that has it and the check is real.
         */
        if( world->hooks.friend_logout_notification )
            check(peer_saw_game(&peers[0], "bob has logged out."),
                  "alice is told bob logged out, in content's words");
        else
            printf("embed: SKIP  the logout notification — "
                   "[proc,friend_logout_notification] is not in this content tree\n");

        /* ── and back again ──
         *
         * A second login on a fresh client id, which is the only reason this
         * step is here rather than being folded into the first: the login
         * notification needs somebody to already be following the name, and at
         * the top of this section nobody was following anybody.
         */
        ToriRS_Network_Free(&peers[1].net);
        /* Bob lists himself. Nothing in the service refuses that, and without it
         * the "bob was not told about his own login" check below has no
         * follower to exclude and so could not go red. */
        check(mock230_friends_add(bob37, bob37) == MOCK230_SOCIAL_OK,
              "a player may list themselves");
        {
            int rejoin = mock230_embed_connect(embed);

            check(rejoin >= 0, "the embed reopened a client for bob");
            if( rejoin >= 0 )
            {
                peer_login(&peers[1], rejoin, seed_b, "bob");
                peers[0].saw_friend_count = 0;
                peer_reset_game(&peers[0]);
                for( int round = 0; round < 20; round++ )
                    pump(peers, 2, embed, round % 2 == 0, 64);

                check(peers[0].saw_friend_count > 0 && peers[0].saw_friend_name37 == bob37 &&
                          peers[0].saw_friend_world != 0,
                      "bob logging back in reaches alice as a non-zero world");
                if( world->hooks.friend_login_notification )
                    check(peer_saw_game(&peers[0], "bob has logged in."),
                          "and alice is told so, in content's words");
                else
                    printf("embed: SKIP  the login notification — "
                           "[proc,friend_login_notification] is not in this content tree\n");

                /* Nobody is told about their own login, which is the one branch
                 * in social_notify_followers that is not the reference's — a
                 * player may list themselves and nothing refuses it. */
                check(peers[1].saw_game_count > 0, "bob's own login burst still reached him");
                check(!peer_saw_game(&peers[1], "bob has logged in."),
                      "and bob was not told about his own login");
            }
        }
    }

    ToriRS_Network_Free(&peers[0].net);
    ToriRS_Network_Free(&peers[1].net);
    mock230_embed_stop(embed);

    printf("embed: %s\n", g_failures ? "FAILURES" : "all checks passed");
    return g_failures ? 1 : 0;
}
