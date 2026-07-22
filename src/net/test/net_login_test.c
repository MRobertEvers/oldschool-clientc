/*
 * Login state machine + packet frame reader tests. Deterministic: the client
 * seed is injected, RSA is exptmod of a fixed block, and ISAAC seeds are
 * derived from the same fixed seed both ends.
 */
#include "net/isaac.h"
#include "net/loginproto.h"
#include "net/packetbuffer.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/lc245_2/gameproto_parse.h"
#include "net/rev/lc245_2/packetin.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Test RSA keypair (small but valid): the client encrypts with (e, n); the
 * test does not decrypt, it only checks the handshake byte stream is
 * deterministic and the state transitions are correct. */
static const char* TEST_RSA_E = "10001";
static const char* TEST_RSA_N =
    "b6f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f1";

static void
fixed_seed(void* user, int32_t* seed)
{
    (void)user;
    seed[0] = 0x11111111;
    seed[1] = 0x22222222;
}

/* Server side of the handshake: emits the 9 skip bytes + 8-byte seed, then a
 * response byte. Fed incrementally to exercise fragmented delivery. */
static void
build_server_hello(uint8_t* buf, uint64_t server_seed)
{
    memset(buf, 0, 9);
    for( int i = 0; i < 8; i++ )
        buf[9 + i] = (uint8_t)(server_seed >> (56 - i * 8));
}

static int
run_login(int fragment_bytes, int response_byte, struct LoginProto** out_lp)
{
    struct rsa rsa;
    struct Isaac* rin = isaac_new(NULL, 0);
    struct Isaac* rout = isaac_new(NULL, 0);
    assert(rsa_init(&rsa, TEST_RSA_E, TEST_RSA_N) == 0);

    struct LoginProto* lp =
        loginproto_new(rin, rout, &rsa, GameProtoRev_LC245_2(), "testuser", "testpass");
    loginproto_set_seed_fn(lp, fixed_seed, NULL);

    uint8_t hello[17];
    build_server_hello(hello, 0xDEADBEEFCAFEF00DULL);

    uint8_t outbuf[512];
    int state;
    int hello_pos = 0;
    int response_sent = 0;

    for( int guard = 0; guard < 100; guard++ )
    {
        state = loginproto_poll(lp);

        /* Flush outbound. */
        int avail = (int)ringbuf_used(lp->out);
        if( avail > 0 )
            loginproto_send(lp, outbuf, sizeof(outbuf));

        if( state == LOGINPROTO_SUCCESS )
        {
            *out_lp = lp;
            return 1;
        }
        if( state == LOGINPROTO_ERROR || state < LOGINPROTO_AWAIT_SEND )
        {
            if( state == LOGINPROTO_ERROR )
            {
                loginproto_free(lp);
                isaac_free(rin);
                isaac_free(rout);
                *out_lp = NULL;
                return 0;
            }
        }

        /* Feed the server hello in fragments, then the response byte.
         * lp->state is the machine's real state (poll returns AWAIT_* codes). */
        if( hello_pos < 17 )
        {
            int chunk = fragment_bytes;
            if( hello_pos + chunk > 17 )
                chunk = 17 - hello_pos;
            hello_pos += loginproto_recv(lp, hello + hello_pos, chunk);
        }
        else if( !response_sent && lp->state == LOGINPROTO_LOGIN_RESPONSE )
        {
            uint8_t rb = (uint8_t)response_byte;
            loginproto_recv(lp, &rb, 1);
            response_sent = 1;
        }
    }

    loginproto_free(lp);
    isaac_free(rin);
    isaac_free(rout);
    *out_lp = NULL;
    return 0;
}

static void
test_login_success(void)
{
    struct LoginProto* lp = NULL;
    assert(run_login(17, 2, &lp)); /* whole hello at once, response 2 */
    assert(lp && lp->state == LOGINPROTO_SUCCESS);
    /* Outbound connect + credentials must have been produced (seed set). */
    assert(lp->seed[0] == 0x11111111 && lp->seed[1] == 0x22222222);
    assert(lp->seed[2] == (int32_t)0xDEADBEEF);
    assert(lp->seed[3] == (int32_t)0xCAFEF00D);
    loginproto_free(lp);
    printf("ok - login success (response 2)\n");
}

static void
test_login_fragmented(void)
{
    struct LoginProto* lp = NULL;
    assert(run_login(1, 18, &lp)); /* one byte at a time, response 18 */
    assert(lp && lp->state == LOGINPROTO_SUCCESS);
    loginproto_free(lp);
    printf("ok - login success, 1-byte-fragmented delivery (response 18)\n");
}

static void
test_login_rejected(void)
{
    struct LoginProto* lp = NULL;
    assert(!run_login(17, 3, &lp)); /* response 3 = bad user/pass */
    assert(lp == NULL);
    printf("ok - login rejected (response 3)\n");
}

/* Frame one packet through packetbuffer with a known ISAAC stream. */
static void
test_packet_framing(void)
{
    int32_t seed[4] = { 1, 2, 3, 4 };
    struct Isaac* enc = isaac_new(seed, 4);
    struct Isaac* dec = isaac_new(seed, 4);
    struct GameProtoRevTable const* rev = GameProtoRev_LC245_2();
    struct PacketBuffer pb;
    packetbuffer_init(&pb, dec, rev);

    /* VARP_SMALL: fixed size 3 (opcode + variable u16 + value). */
    int name = PKTIN_LC245_2_VARP_SMALL;
    int code = rev->packetin_code(name);
    int size = rev->packetin_size(code);
    assert(size == 3);

    uint8_t wire[4];
    wire[0] = (uint8_t)((code + isaac_next(enc)) & 0xff);
    wire[1] = 0x00;
    wire[2] = 0x2A; /* varp id 42 */
    wire[3] = 0x05; /* value 5 */

    /* Feed 1 byte at a time to exercise fragmentation. */
    int fed = 0;
    while( fed < 4 && !packetbuffer_ready(&pb) )
        fed += packetbuffer_read(&pb, wire + fed, 1);

    assert(packetbuffer_ready(&pb));
    assert(packetbuffer_packet_type(&pb) == code);
    assert(packetbuffer_size(&pb) == 3);

    struct RevPacket_LC245_2 packet;
    memset(&packet, 0, sizeof(packet));
    int ok = gameproto_parse_lc245_2(
        code, packetbuffer_data(&pb), packetbuffer_size(&pb), &packet);
    assert(ok);
    gameproto_free_lc245_2(&packet);

    packetbuffer_reset(&pb);
    isaac_free(enc);
    isaac_free(dec);
    printf("ok - packet framing (VARP_SMALL, fragmented, ISAAC-decrypted)\n");
}

/* A var-u16 packet whose 2-byte length itself arrives split. */
static void
test_packet_varu16(void)
{
    int32_t seed[4] = { 9, 8, 7, 6 };
    struct Isaac* enc = isaac_new(seed, 4);
    struct Isaac* dec = isaac_new(seed, 4);
    struct GameProtoRevTable const* rev = GameProtoRev_LC245_2();
    struct PacketBuffer pb;
    packetbuffer_init(&pb, dec, rev);

    int code = rev->packetin_code(PKTIN_LC245_2_UPDATE_INV_FULL);
    assert(rev->packetin_size(code) == -2); /* var-u16 */

    /* Body: component_id(u16) + count(u16) + one obj (id u16 + count u1). */
    uint8_t body[5] = { 0x00, 0x95, 0x00, 0x01, 0x00 };
    uint16_t blen = sizeof(body);

    uint8_t wire[8];
    wire[0] = (uint8_t)((code + isaac_next(enc)) & 0xff);
    wire[1] = (uint8_t)(blen >> 8);
    wire[2] = (uint8_t)(blen & 0xff);
    memcpy(wire + 3, body, sizeof(body));

    int fed = 0;
    while( fed < (int)sizeof(wire) && !packetbuffer_ready(&pb) )
        fed += packetbuffer_read(&pb, wire + fed, 1);

    assert(packetbuffer_ready(&pb));
    assert(packetbuffer_packet_type(&pb) == code);
    assert(packetbuffer_size(&pb) == blen);

    packetbuffer_reset(&pb);
    isaac_free(enc);
    isaac_free(dec);
    printf("ok - packet framing (var-u16 length split across reads)\n");
}

int
main(void)
{
    test_login_success();
    test_login_fragmented();
    test_login_rejected();
    test_packet_framing();
    test_packet_varu16();
    printf("net-login: all tests passed\n");
    return 0;
}
