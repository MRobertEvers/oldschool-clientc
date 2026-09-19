/*
 * Revision 239: each client is told ITS OWN slot in LoginResponse.Ok.
 *
 * At this revision the login response is the only statement of the local
 * player's index — UPDATE_PID is gone, and nothing on the wire restates it
 * (net/net.c turns the response's field into a synthetic UPDATE_PID so the
 * rest of the client can keep reading one field). Everything downstream is
 * keyed on it: the GPI init block skips exactly that slot, and PLAYER_INFO's
 * high-resolution records name it.
 *
 * So a server that states the wrong index does not produce a wrong number, it
 * produces a client watching somebody else. That was the bug this test exists
 * for: the response was written during the handshake, from
 * `session->player ? session->player->pid : 0`, and `session->player` is NULL
 * until a host answers the login the handshake is in the middle of raising.
 * Every client was therefore told "you are index 1". With one player that is
 * the right answer by coincidence; with two, the second client believed it was
 * the first, read every rough position in the init block one slot out, and
 * locked its camera onto the other player.
 *
 * Two clients is therefore the whole of the test. One would have passed
 * throughout.
 *
 * This is deliberately NOT asserted against the server's own player pool — the
 * server believing alice is pid 0 and bob is pid 1 was never in doubt. It is
 * asserted against the index each CLIENT came away with, decoded by the
 * client's own rev-239 login machine, because that is where the two numbers
 * were allowed to disagree.
 *
 * Run: make -C src test-torirsserver-login-index
 */

#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_embed.h"
#include "torirsserver/torirs_server_session.h"
#include "torirsserver/torirs_server_wire.h"

#include "cmd/cmdbus.h"
#include "net/net.h"
#include "net/net_out.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    printf("login-index: %-56s %s\n", what, condition ? "ok" : "FAILED");
    if( !condition )
        g_failures++;
}

struct Peer
{
    struct ToriRS_Network net;
    int client_id;
    /** The index the client believes is itself. -1 until the response lands;
     *  0 is not a usable "none yet" because index 0 is a real wire value (the
     *  unoccupied slot), and a test that read it as "nothing arrived" would
     *  pass against a server that sent one. */
    int local_index;
};

/*
 * Deterministic seeds that differ between the two clients, for the same reason
 * embed_test.c's do: identical ISAAC seeds would hide a server that mixed the
 * two sessions' ciphers up, which is a neighbouring way to answer the wrong
 * client.
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

static void
peer_login(
    struct Peer* peer,
    int client_id,
    void (*seed_fn)(void*, int32_t*),
    const char* name)
{
    memset(peer, 0, sizeof(*peer));
    peer->client_id = client_id;
    peer->local_index = -1;

    ToriRS_Network_Init(&peer->net, GameProtoRev_OSRS239(), TORIRSSERVER_RSA_PUBLIC_EXPONENT,
                        TORIRSSERVER_RSA_PUBLIC_MODULUS);
    ToriRS_Network_SetSeedFn(&peer->net, seed_fn, NULL);
    /* The host:port is inert in-process, but the login machine wants one. */
    ToriRS_Network_ConnectLogin(&peer->net, "embedded:0", name, name);
    ToriRS_Network_HandleCmd(&peer->net, TORIRS_CMD_NET_STATUS, NULL, 0);
    {
        uint8_t status = TORIRS_NET_STATUS_CONNECTED;
        ToriRS_Network_HandleCmd(&peer->net, TORIRS_CMD_NET_STATUS, &status, 1);
    }
}

/** One round of "move each side's output into the other's input". */
static void
pump(
    struct Peer* peers,
    int peer_count,
    struct ToriRSServerEmbed* embed,
    int run_tick)
{
    struct ToriRS_CmdHeader header;
    uint8_t payload[65536];
    uint8_t inbound[65536];
    int got;

    for( int p = 0; p < peer_count; p++ )
    {
        while( ToriRS_Network_PopOut(&peers[p].net, &header, payload) )
        {
            if( header.type != TORIRS_NET_OUT_SEND_DATA )
                continue; /* CONNECT carries a host:port an embed has no use for */
            ToriRSServer_EmbedWrite(embed, peers[p].client_id, payload, header.length);
            ToriRSServer_EmbedPump(embed, 0);
        }
    }

    ToriRSServer_EmbedPump(embed, run_tick);

    for( int p = 0; p < peer_count; p++ )
    {
        struct Peer* peer = &peers[p];
        struct RevPacket packet;

        while( (got = ToriRSServer_EmbedRead(embed, peer->client_id, inbound,
                                             (int)sizeof(inbound))) > 0 )
            ToriRS_Network_HandleCmd(&peer->net, TORIRS_CMD_NET_RECV, inbound, got);

        /* Drained every round: the parsed FIFO is finite and PLAYER_INFO
         * carries a heap payload, so looking only at the end would assert on
         * whatever survived. UPDATE_PID is the client's own name for the
         * field it read out of LoginResponse.Ok (net/net.c). */
        while( ToriRS_Network_PopPacket(&peer->net, &packet) )
            if( packet.packet_type == PKT_NAME_UPDATE_PID )
                peer->local_index = packet._update_pid.local_player_index;
    }
}

int
main(void)
{
    struct Peer peers[2];
    struct ToriRSServerEmbed* embed;
    struct ToriRSServer* world;
    struct ToriRSServerPlayer* alice;
    struct ToriRSServerPlayer* bob;
    int second_client;
    int reached_game = 0;

    /* The revision IS the test: the deferred response exists only at 239, and
     * at 230 the local index arrives as a real UPDATE_PID from the world, long
     * after the pool slot is decided. Set before the embed starts, because the
     * wire is chosen at boot. */
    setenv("TORIRSSERVER_REV", "osrs239", 1);
    setenv("TORIRSSERVER_SAVES", "build/login_index_test_saves", 1);
    remove("build/login_index_test_saves/alice.ini");
    remove("build/login_index_test_saves/bob.ini");

    embed = ToriRSServer_EmbedStart(NULL);
    if( !embed )
    {
        fprintf(stderr, "login-index: could not start the server\n");
        return 1;
    }

    second_client = ToriRSServer_EmbedConnect(embed);
    if( second_client < 0 )
    {
        fprintf(stderr, "login-index: the embed refused a second client\n");
        ToriRSServer_EmbedStop(embed);
        return 1;
    }

    peer_login(&peers[0], 0, seed_a, "alice");
    peer_login(&peers[1], second_client, seed_b, "bob");

    for( int round = 0; round < 120; round++ )
    {
        pump(peers, 2, embed, round % 2 == 0);
        if( peers[0].net.state == TORIRS_NET_GAME && peers[1].net.state == TORIRS_NET_GAME )
            reached_game = 1;
    }

    check(reached_game, "both clients reached GAME over the 239 handshake");

    world = ToriRSServer_EmbedWorld(embed);
    alice = ToriRSServer_EmbedPlayer(embed, 0);
    bob = ToriRSServer_EmbedPlayer(embed, second_client);
    if( !world || !alice || !bob )
    {
        printf("login-index: FAILURES (no world, or a login took no pool slot)\n");
        ToriRSServer_EmbedStop(embed);
        return 1;
    }

    check(alice->pid == 0 && bob->pid == 1, "the two logins took pool slots 0 and 1");

    /*
     * The two halves of the bug, separately.
     *
     * The first client passed before the fix and passes after it — kept
     * because it is what made the second one look like a one-off rather than
     * the rule, and a regression that goes the other way (deferring the
     * response past the point the client needs it) shows up here first.
     */
    check(peers[0].local_index >= 0 && peers[1].local_index >= 0,
          "each client was told an index at all");
    check(peers[0].local_index == ToriRSServer_WirePlayerIndex(alice->pid),
          "alice was told her own wire slot");
    check(peers[1].local_index == ToriRSServer_WirePlayerIndex(bob->pid),
          "bob was told HIS own wire slot, not alice's");
    check(peers[0].local_index != peers[1].local_index,
          "so the two clients were told different slots");

    ToriRSServer_EmbedStop(embed);

    if( g_failures )
    {
        printf("login-index: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("login-index: all checks passed\n");
    return 0;
}
