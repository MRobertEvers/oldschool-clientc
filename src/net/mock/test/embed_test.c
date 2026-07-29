/*
 * The rev-230 server hosted in-process: a real client subsystem and a real
 * server, wired together by two byte buffers and nothing else.
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
 *   2. The bytes are the real protocol. The client encrypts a real login block
 *      with the server's real modulus and both ends arm real ISAAC ciphers;
 *      nothing is short-circuited for being in-process. A packet that would
 *      desync over TCP desyncs here too.
 *   3. Packets torn across reads are survivable. The feed below deliberately
 *      hands the server its bytes in small chunks, which is the case the old
 *      blocking reader printed "split var-u8 header" and gave up on.
 *
 * Run: make -C src test-mock230-embed
 */

#include "net/mock/mock230.h"
#include "net/mock/mock230_embed.h"
#include "net/mock/mock230_session.h"

#include "cmd/cmdbus.h"
#include "net/net.h"
#include "net/rev/gameproto_parse.h"
#include "net/rev/gameproto_revisions.h"

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
    printf("embed: %-46s %s\n", what, condition ? "ok" : "FAILED");
    if( !condition )
        g_failures++;
}

/*
 * Deterministic client seed, so a failing run reproduces exactly. The values
 * are arbitrary; what matters is that they do not change between runs.
 */
static void
fixed_seed(
    void* user,
    int32_t* seed)
{
    (void)user;
    seed[0] = 0x13571357;
    seed[1] = 0x24682468;
    seed[2] = 0x0f0f0f0f;
    seed[3] = 0xf0f0f0f0;
}

/*
 * Move one round of bytes in both directions and let the server act.
 *
 * `chunk` caps how much of the client's output is handed over at once. Feeding
 * it in pieces is the point: it forces the server to re-enter its reader with
 * partial packets, which is exactly what a real socket does under load and what
 * the old blocking reader could not survive.
 */
static void
pump(
    struct ToriRS_Network* net,
    struct Mock230Embed* embed,
    int run_tick,
    int chunk)
{
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    static uint8_t inbound[65536];
    int got;

    /* client -> server */
    while( ToriRS_Network_PopOut(net, &header, payload) )
    {
        if( header.type != TORIRS_NET_OUT_SEND_DATA )
            continue; /* CONNECT carries a host:port an embedded server has no use for */
        for( int off = 0; off < header.length; off += chunk )
        {
            int len = header.length - off;

            if( len > chunk )
                len = chunk;
            mock230_embed_write(embed, payload + off, len);
            /* Pump between chunks so the server genuinely sees a torn stream
             * rather than a whole packet reassembled by the pipe. */
            mock230_embed_pump(embed, 0);
        }
    }

    mock230_embed_pump(embed, run_tick);

    /* server -> client */
    while( (got = mock230_embed_read(embed, inbound, (int)sizeof(inbound))) > 0 )
        ToriRS_Network_HandleCmd(net, TORIRS_CMD_NET_RECV, inbound, got);
}

int
main(void)
{
    struct ToriRS_Network net;
    struct Mock230Embed* embed;
    struct Mock230Server* world;
    int reached_game = 0;

    embed = mock230_embed_start();
    if( !embed )
    {
        fprintf(stderr, "embed: could not start the server\n");
        return 1;
    }

    ToriRS_Network_Init(&net, GameProtoRev_OSRS230(), MOCK230_RSA_PUBLIC_EXPONENT,
                        MOCK230_RSA_PUBLIC_MODULUS);
    check(net.rsa_ready, "client armed the server's public key");
    ToriRS_Network_SetSeedFn(&net, fixed_seed, NULL);

    /* The host:port is inert here — there is nothing to dial — but the client's
     * login machine wants one, and passing a real-looking value keeps this
     * path identical to the socket one. */
    ToriRS_Network_ConnectLogin(&net, "embedded:0", "embed", "embed");
    check(net.state == TORIRS_NET_LOGIN, "client entered LOGIN");

    /*
     * The client will not send its login block until it believes it is
     * connected, because the socket layer normally reports that. In-process
     * there is no connect to wait for, so say so directly.
     */
    ToriRS_Network_HandleCmd(&net, TORIRS_CMD_NET_STATUS, NULL, 0);
    {
        uint8_t status = TORIRS_NET_STATUS_CONNECTED;
        ToriRS_Network_HandleCmd(&net, TORIRS_CMD_NET_STATUS, &status, 1);
    }

    /*
     * Drive it. Sixty rounds is a hundred times what the handshake needs and
     * still a fraction of a second — the loop exists to give the login burst
     * somewhere to land, not to wait for anything.
     */
    for( int round = 0; round < 60; round++ )
    {
        pump(&net, embed, round % 2 == 0, 7);
        if( net.state == TORIRS_NET_GAME )
            reached_game = 1;
    }

    check(reached_game, "client reached GAME over the buffer pair");
    check(mock230_embed_online(embed), "server completed the handshake");

    world = mock230_embed_world(embed);
    check(world != NULL, "server exposes its world");
    if( world )
    {
        check(world->tick > 0, "the world ticked");
        check(world->player.x > 0 && world->player.z > 0, "the player is on a tile");
        check(strcmp(world->player.display_name, "embed") == 0,
              "the login name reached the player");
    }

    /* A parsed packet proves the whole path: encoded by the server, framed,
     * ISAAC-scrambled, carried through the pipe, descrambled and decoded. */
    {
        struct RevPacket packet;
        int parsed = 0;

        while( ToriRS_Network_PopPacket(&net, &packet) )
        {
            parsed++;
            gameproto_free(&packet);
        }
        check(parsed > 0, "the client parsed server packets");
    }

    ToriRS_Network_Free(&net);
    mock230_embed_stop(embed);

    printf("embed: %s\n", g_failures ? "FAILURES" : "all checks passed");
    return g_failures ? 1 : 0;
}
