/*
 * In-process loopback: drive ToriRS_Network through login -> GAME, feed it
 * scripted server packets via the mock server, and assert the parsed-packet
 * FIFO. No socket; the mock's emitted bytes are handed to the subsystem as
 * NET_RECV commands, exactly as the socket layer would.
 */
#include "net/mock/mock_server.h"
#include "net/net.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/gameproto_parse.h"
#include "net/rev/lc245_2/packetin.h"

#include "cmd/cmdbus.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The same RSA key + fixed seed the login test uses (deterministic). */
static const char* TEST_RSA_E = "10001";
static const char* TEST_RSA_N =
    "b6f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f1";

static int32_t g_client_seed[4] = { 0x11111111, 0x22222222, 0, 0 };

static void
fixed_seed(void* user, int32_t* seed)
{
    (void)user;
    seed[0] = 0x11111111;
    seed[1] = 0x22222222;
}

/* Drain the client's outbound ring (CONNECT / SEND_DATA). */
static void
drain_out(struct ToriRS_Network* net)
{
    struct ToriRS_CmdHeader h;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    while( ToriRS_Network_PopOut(net, &h, payload) )
        ; /* discard: the loopback drives recv directly */
}

static void
feed_recv(struct ToriRS_Network* net, uint8_t const* data, int len)
{
    ToriRS_Network_HandleCmd(net, TORIRS_CMD_NET_RECV, data, len);
}

int
main(void)
{
    struct ToriRS_Network net;
    struct MockServer server;
    uint64_t server_seed = 0xDEADBEEFCAFEF00DULL;
    uint8_t buf[512];
    int n;

    ToriRS_Network_Init(&net, GameProtoRev_LC245_2(), TEST_RSA_E, TEST_RSA_N);
    assert(net.rsa_ready);
    ToriRS_Network_SetSeedFn(&net, fixed_seed, NULL);

    MockServer_Init(&server, GameProtoRev_LC245_2(), server_seed, 2);
    g_client_seed[2] = (int32_t)(server_seed >> 32);
    g_client_seed[3] = (int32_t)(server_seed & 0xFFFFFFFF);

    /* Begin login; the CONNECT + initial connect bytes go to the out ring. */
    ToriRS_Network_ConnectLogin(&net, "localhost:43594", "testuser", "testpass");
    assert(net.state == TORIRS_NET_LOGIN);
    drain_out(&net);

    /* Server hello -> client sends credentials -> arm the server cipher from
     * the same client seed -> server response byte -> GAME. */
    n = MockServer_Hello(&server, buf, sizeof(buf));
    feed_recv(&net, buf, n);
    drain_out(&net);

    MockServer_ArmCipher(&server, g_client_seed);

    n = MockServer_Response(&server, buf, sizeof(buf));
    feed_recv(&net, buf, n);
    assert(net.state == TORIRS_NET_GAME);
    printf("ok - login handshake reached GAME state\n");

    /* Scripted VARP_SMALL(id=42, val=5). */
    {
        uint8_t body[3] = { 0x00, 0x2A, 0x05 };
        int code = GameProtoRev_LC245_2()->packetin_wire(PKT_NAME_VARP_SMALL);
        n = MockServer_Packet(&server, buf, sizeof(buf), code, body, sizeof(body));
        feed_recv(&net, buf, n);
    }

    /* Scripted MESSAGE_GAME "hello world" (var-u8: 1 zero + wordpacked text is
     * complex; use a raw text field the parser reads as bytes). We just verify
     * the packet reaches the FIFO with the right type. */
    {
        uint8_t body[16];
        int code = GameProtoRev_LC245_2()->packetin_wire(PKT_NAME_UPDATE_RUNENERGY);
        int size = GameProtoRev_LC245_2()->packetin_size(code);
        int blen = size > 0 ? size : 1;
        memset(body, 0, sizeof(body));
        body[0] = 55; /* run energy */
        n = MockServer_Packet(&server, buf, sizeof(buf), code, body, blen);
        feed_recv(&net, buf, n);
    }

    /* Drain the parsed FIFO and check types + a decoded field. */
    {
        struct RevPacket packet;
        int got_varp = 0;
        int got_energy = 0;
        while( ToriRS_Network_PopPacket(&net, &packet) )
        {
            if( packet.packet_type == PKT_NAME_VARP_SMALL )
            {
                got_varp = 1;
                assert(packet._varp_small.variable == 42);
                assert(packet._varp_small.value == 5);
            }
            else if( packet.packet_type == PKT_NAME_UPDATE_RUNENERGY )
            {
                got_energy = 1;
            }
            gameproto_free(&packet);
        }
        assert(got_varp);
        assert(got_energy);
        printf("ok - scripted VARP_SMALL + UPDATE_RUNENERGY parsed from FIFO\n");
    }

    ToriRS_Network_Free(&net);
    MockServer_Free(&server);
    printf("net-loopback: all tests passed\n");
    return 0;
}
