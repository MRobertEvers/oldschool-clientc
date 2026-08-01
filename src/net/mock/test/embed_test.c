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
#include "net/mock/mock230_embed.h"
#include "net/mock/mock230_session.h"

#include "cmd/cmdbus.h"
#include "net/net.h"
#include "net/net_out.h"
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "net/rev/packets/pkt_player_info.h"
#include "net/rev/pktnames.h"

#include <assert.h>
#include <stdint.h>
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
};

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

int
main(void)
{
    struct Peer peers[2];
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

    ToriRS_Network_Free(&peers[0].net);
    ToriRS_Network_Free(&peers[1].net);
    mock230_embed_stop(embed);

    printf("embed: %s\n", g_failures ? "FAILURES" : "all checks passed");
    return g_failures ? 1 : 0;
}
