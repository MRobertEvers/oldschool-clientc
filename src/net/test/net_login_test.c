/*
 * Login state machine + packet frame reader tests. Deterministic: the client
 * seed is injected, RSA is exptmod of a fixed block, and ISAAC seeds are
 * derived from the same fixed seed both ends.
 */
#include "net/isaac.h"
#include "net/login/login_lc.h"
#include "net/packetbuffer.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/revpacket.h"
#include "net/rev/rev_list.h"
#include "net/torirs_control.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CHECK(cond)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                     \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

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

/*
 * The login block the machine put on the wire, captured out of the flush.
 *
 * Its first byte is the whole of what separates a reconnect from a fresh
 * login on this wire, so a test that only watches the state machine reach
 * SUCCESS cannot see the difference at all -- both do.
 */
struct LoginBlockCapture
{
    uint8_t data[512];
    int len;
};

static int
run_login_rev(
    int fragment_bytes,
    int response_byte,
    struct GameProtoRevTable const* rev,
    int reconnect,
    struct LoginBlockCapture* capture,
    struct LoginProto** out_lp)
{
    struct rsa rsa;
    struct Isaac* rin = isaac_new(NULL, 0);
    struct Isaac* rout = isaac_new(NULL, 0);
    TEST_CHECK(rsa_init(&rsa, TEST_RSA_E, TEST_RSA_N) == 0);

    struct LoginProto* lp = loginproto_new(rin, rout, &rsa, rev, "testuser", "testpass");
    loginproto_set_seed_fn(lp, fixed_seed, NULL);
    loginproto_set_reconnect(lp, reconnect);
    if( capture )
        capture->len = 0;

    uint8_t hello[17];
    build_server_hello(hello, 0xDEADBEEFCAFEF00DULL);

    uint8_t outbuf[512];
    int state;
    int hello_pos = 0;
    int response_sent = 0;

    for( int guard = 0; guard < 100; guard++ )
    {
        state = loginproto_poll(lp);

        /* Flush outbound. The credentials block is the flush that leaves the
         * machine waiting on the response byte; the earlier one is the 2-byte
         * connect. */
        int avail = (int)ringbuf_used(lp->out);
        if( avail > 0 )
        {
            int sent = loginproto_send(lp, outbuf, sizeof(outbuf));
            if( capture && lp->state == LOGINPROTO_LOGIN_RESPONSE && sent > 0 &&
                sent <= (int)sizeof(capture->data) )
            {
                memcpy(capture->data, outbuf, (size_t)sent);
                capture->len = sent;
            }
        }

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
        else if( response_sent == 1 && lp->state == LOGINPROTO_LOGIN_SUCCESS_TAIL )
        {
            /* Success carries a 2-byte tail: staffmodlevel + mouse flag. */
            uint8_t tail[2] = { 0, 1 };
            loginproto_recv(lp, tail, 2);
            response_sent = 2;
        }
    }

    loginproto_free(lp);
    isaac_free(rin);
    isaac_free(rout);
    *out_lp = NULL;
    return 0;
}

static int
run_login(int fragment_bytes, int response_byte, struct LoginProto** out_lp)
{
    return run_login_rev(
        fragment_bytes, response_byte, GameProtoRev_RS245_2(), /* reconnect */ 0, NULL, out_lp);
}

static void
test_login_success(void)
{
    struct LoginProto* lp = NULL;
    TEST_CHECK(run_login(17, 2, &lp)); /* whole hello at once, response 2 */
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
    /* One byte at a time, response 15 = reconnect handoff (no tail). */
    TEST_CHECK(run_login(1, 15, &lp));
    assert(lp && lp->state == LOGINPROTO_SUCCESS);
    loginproto_free(lp);
    printf("ok - login success, 1-byte-fragmented delivery (response 15)\n");
}

/* LostCity Packet.rsadec constructs a signed BigInteger from these bytes.
 * Independent modular-exponentiation vector for fixed_seed/hello/testuser:
 * pow(int.from_bytes(plain, 'big'), 65537, int(TEST_RSA_N, 16)). The magnitude
 * begins 0x9f and therefore needs BigInteger.toByteArray's positive sign byte.
 * Omitting it intermittently produces reply 6 despite matching revision/CRCs. */
static void
test_login_rsa_positive_sign(void)
{
    static uint8_t const expected[] = {
        0, 0x9f, 0xc4, 0x5c, 0xba, 0xa0, 0x86, 0xe5,
        0x8e, 0xe4, 0xaa, 0x68, 0xbf, 0xd4, 0x9c, 0xc8,
        0x6a, 0x27, 0xcd, 0x3b, 0x45, 0x70, 0x23, 0xf1,
        0x5d, 0xab, 0x2b, 0xad, 0x4d, 0xef, 0xdc, 0x6f, 0xbb
    };
    struct GameProtoRevTable const* revisions[] = { GameProtoRev_RS245_2(), GameProtoRev_RS289() };
    for( size_t i = 0; i < sizeof(revisions) / sizeof(revisions[0]); i++ )
    {
        struct LoginProto* lp = NULL;
        struct LoginBlockCapture block;
        TEST_CHECK(run_login_rev(1, 2, revisions[i], 0, &block, &lp));
        int const rsa_offset = 2 + (revisions[i]->client_version > 254 ? 3 : 1) + 1 + 36;
        TEST_CHECK(block.data[rsa_offset] == sizeof(expected));
        TEST_CHECK(block.len == rsa_offset + 1 + (int)sizeof(expected));
        TEST_CHECK(block.data[1] == block.len - 2);
        TEST_CHECK(memcmp(block.data + rsa_offset + 1, expected, sizeof(expected)) == 0);
        loginproto_free(lp);
    }
    printf("ok - classic RSA ciphertext preserves the positive BigInteger sign\n");
}

/*
 * The reconnect opcode, and the fact that it is the ONLY thing that changes.
 *
 * LostCity's client writes `reconnect ? 18 : 16` and then an identical block
 * either way (Client-TS Client.ts login()), so byte 0 differing and bytes 1..n
 * matching is the whole compatibility claim -- a reconnect that also perturbed
 * the length byte or the CRCs would be read out of position by a server that
 * decodes both with one decoder.
 */
static void
test_reconnect_opcode_creds(void)
{
    struct LoginProto* lp = NULL;
    struct LoginBlockCapture fresh;
    struct LoginBlockCapture again;

    TEST_CHECK(run_login_rev(17, 2, GameProtoRev_RS245_2(), /* reconnect */ 0, &fresh, &lp));
    loginproto_free(lp);
    lp = NULL;
    TEST_CHECK(run_login_rev(17, 15, GameProtoRev_RS245_2(), /* reconnect */ 1, &again, &lp));
    loginproto_free(lp);

    TEST_CHECK(fresh.len > 1 && fresh.len == again.len);
    TEST_CHECK(fresh.data[0] == 16);
    TEST_CHECK(again.data[0] == 18);
    TEST_CHECK(memcmp(fresh.data + 1, again.data + 1, (size_t)fresh.len - 1) == 0);
    printf("ok - reconnect sends GAMERECONNECT with an otherwise identical block\n");
}

/*
 * A revision that does not declare a reconnect gets a fresh GAMELOGIN.
 *
 * Not a cosmetic fallback: a server that has never heard of opcode 18 drops
 * the connection on the first byte, which surfaces as "the reconnect never
 * connected" rather than as a rejected login -- so the table, not the caller,
 * is what may put an 18 on the wire.
 */
static void
test_reconnect_opcode_none(void)
{
    struct GameProtoRevTable rev = *GameProtoRev_RS245_2();
    struct LoginProto* lp = NULL;
    struct LoginBlockCapture block;

    rev.reconnect_kind = NET_RECONNECT_NONE;
    TEST_CHECK(run_login_rev(17, 2, &rev, /* reconnect */ 1, &block, &lp));
    loginproto_free(lp);

    TEST_CHECK(block.len > 1);
    TEST_CHECK(block.data[0] == 16);
    printf("ok - a revision without a reconnect re-establishes as a fresh login\n");
}

static void
test_login_rejected(void)
{
    struct LoginProto* lp = NULL;
    TEST_CHECK(!run_login(17, 3, &lp)); /* response 3 = bad user/pass */
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
    struct GameProtoRevTable const* rev = GameProtoRev_RS245_2();
    struct PacketBuffer pb;
    packetbuffer_init(&pb, dec, rev);

    /* VARP_SMALL: fixed size 3 (opcode + variable u16 + value). */
    int name = PKT_NAME_VARP_SMALL;
    int code = rev->packetin_wire(name);
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

    struct RevPacket packet;
    memset(&packet, 0, sizeof(packet));
    int ok = rev->parse_wire(rev, code, packetbuffer_data(&pb), packetbuffer_size(&pb), &packet) > 0;
    assert(ok);
    gameproto_free(&packet);

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
    struct GameProtoRevTable const* rev = GameProtoRev_RS245_2();
    struct PacketBuffer pb;
    packetbuffer_init(&pb, dec, rev);

    int code = rev->packetin_wire(PKT_NAME_UPDATE_INV_FULL);
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

/* Literal UpdateInvPartialEncoder body for two adjacent dirty slots. The empty
 * slot ends after its zero object sentinel; there is deliberately no count
 * byte before slot 1. This is the exact cursor boundary that item-on-item hits
 * when it consumes one ingredient and replaces the other. */
static void
test_osrs239_partial_empty_slot(void)
{
    static uint8_t body[] = {
        0xff, 0xff, 0xff, 0xff, /* combined id: normal IF3 inventory */
        0x00, 0x5d,             /* inventory id 93 */
        0x00, 0x00, 0x00,       /* slot 0, empty; record ends here */
        0x01, 0x06, 0x8c, 0x01, /* slot 1, obj 1675, count 1 */
    };
    struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();
    struct RevPacket packet;

    memset(&packet, 0, sizeof(packet));
    packet.packet_type = PKT_NAME_UPDATE_INV_PARTIAL;
    TEST_CHECK(rev->parse_wire(
        rev, rev->packetin_wire(PKT_NAME_UPDATE_INV_PARTIAL), body, sizeof(body), &packet) == 1);
    assert(packet._update_inv_partial.component_id == -1);
    assert(packet._update_inv_partial.inv_id == 93);
    assert(packet._update_inv_partial.count == 2);
    assert(packet._update_inv_partial.entries[0].slot == 0);
    assert(packet._update_inv_partial.entries[0].obj_id == -1);
    assert(packet._update_inv_partial.entries[0].count == 0);
    assert(packet._update_inv_partial.entries[1].slot == 1);
    assert(packet._update_inv_partial.entries[1].obj_id == 1675);
    assert(packet._update_inv_partial.entries[1].count == 1);
    gameproto_free(&packet);

    /* All-empty partials use the three-byte minimum for every record. This
     * catches sizing the entry array with the old four-byte assumption. */
    {
        uint8_t empty_body[6 + 28 * 3] = {
            0xff, 0xff, 0xff, 0xff, 0x00, 0x5d,
        };

        for( int i = 0; i < 28; i++ )
            empty_body[6 + i * 3] = (uint8_t)i;
        memset(&packet, 0, sizeof(packet));
        packet.packet_type = PKT_NAME_UPDATE_INV_PARTIAL;
        TEST_CHECK(rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_UPDATE_INV_PARTIAL), empty_body,
                                   sizeof(empty_body), &packet) == 1);
        assert(packet._update_inv_partial.count == 28);
        for( int i = 0; i < 28; i++ )
        {
            assert(packet._update_inv_partial.entries[i].slot == i);
            assert(packet._update_inv_partial.entries[i].obj_id == -1);
            assert(packet._update_inv_partial.entries[i].count == 0);
        }
        gameproto_free(&packet);
    }
    printf("ok - osrs239 partial inventory empty-slot boundary\n");
}

/*
 * The classic (2004-era) inventory packets, as UpdateInvFullEncoder and
 * UpdateInvPartialEncoder write them and as the reference client reads them
 * back: the full packet's slot count is TWO bytes, and a partial's slot index
 * is a smart.
 *
 * Read as one byte, the count is the high half of a number that never reaches
 * 256 -- so every container on this generation decoded as size 0 and painted
 * empty, with nothing on the wire or in the log to say so.
 */
static void
test_lc_inv_full_size_is_two_bytes(void)
{
    /* component 8847, 3 slots: obj 250 x1, empty, obj 1526 x255-escape(1000). */
    static uint8_t body[] = {
        0x22, 0x8f,             /* component 8847 */
        0x00, 0x03,             /* slot count 3 -- two bytes */
        0x00, 0xfa, 0x01,       /* obj id+1 = 250 -> 249, count 1 */
        0x00, 0x00, 0x00,       /* empty slot */
        0x05, 0xf7, 0xff,       /* obj id+1 = 1527 -> 1526, count escape... */
        0x00, 0x00, 0x03, 0xe8, /* ...1000 */
    };
    struct GameProtoRevTable const* rev = GameProtoRev_RS289();
    struct RevPacket packet;

    memset(&packet, 0, sizeof(packet));
    TEST_CHECK(rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_UPDATE_INV_FULL), body, sizeof(body), &packet) == 1);
    assert(packet._update_inv_full.component_id == 8847);
    assert(packet._update_inv_full.size == 3);
    assert(packet._update_inv_full.obj_ids[0] == 249);
    assert(packet._update_inv_full.obj_counts[0] == 1);
    assert(packet._update_inv_full.obj_ids[1] == -1);
    assert(packet._update_inv_full.obj_ids[2] == 1526);
    assert(packet._update_inv_full.obj_counts[2] == 1000);
    gameproto_free(&packet);
    printf("ok - classic UPDATE_INV_FULL slot count is two bytes\n");
}

static void
test_lc_inv_partial_slot_is_smart(void)
{
    /* Bank slots 5 and 200: the second is over the one-byte smart boundary and
     * goes out as 0x8000 + 200. Reading it as a byte ate the object id. */
    static uint8_t body[] = {
        0x00, 0x0c,       /* component 12 */
        0x05, 0x01, 0x0d, 0x02, /* slot 5, obj id+1 = 269 -> 268, count 2 */
        0x80, 0xc8, 0x00, 0xfb, 0x07, /* slot 200, obj id+1 = 251 -> 250, count 7 */
    };
    struct GameProtoRevTable const* rev = GameProtoRev_RS289();
    struct RevPacket packet;

    memset(&packet, 0, sizeof(packet));
    TEST_CHECK(
        rev->parse_wire(rev, rev->packetin_wire(PKT_NAME_UPDATE_INV_PARTIAL), body, sizeof(body), &packet) == 1);
    assert(packet._update_inv_partial.component_id == 12);
    assert(packet._update_inv_partial.count == 2);
    assert(packet._update_inv_partial.entries[0].slot == 5);
    assert(packet._update_inv_partial.entries[0].obj_id == 268);
    assert(packet._update_inv_partial.entries[0].count == 2);
    assert(packet._update_inv_partial.entries[1].slot == 200);
    assert(packet._update_inv_partial.entries[1].obj_id == 250);
    assert(packet._update_inv_partial.entries[1].count == 7);
    gameproto_free(&packet);
    printf("ok - classic UPDATE_INV_PARTIAL slot index is a smart\n");
}

/* One opcode as the server writes it: pSmart1Or2 on a revision that has it. */
static int
put_opcode(
    struct Isaac* enc,
    struct GameProtoRevTable const* rev,
    int code,
    uint8_t* out)
{
    if( rev->opcode_smart2 && code >= 0x80 )
    {
        out[0] = (uint8_t)((((code >> 8) | 0x80) + isaac_next(enc)) & 0xff);
        out[1] = (uint8_t)(((code & 0xff) + isaac_next(enc)) & 0xff);
        return 2;
    }
    out[0] = (uint8_t)((code + isaac_next(enc)) & 0xff);
    return 1;
}

/*
 * The control escape (net/torirs_control.h) between two packets, as
 * torirs_server_encode.c writes it: VARP_SMALL, a CACHE_INVALIDATE control
 * frame, then SERVER_TICK_END (no payload). Fed in chunks of every size from
 * one byte up, the reader yields the three in order, the frame's payload
 * intact, the zero-length packet without waiting for a byte after it, and the
 * cipher stepped exactly as often as the writer stepped it.
 */
static void
test_control_escape(void)
{
    int32_t seed[4] = { 41, 42, 43, 44 };
    struct GameProtoRevTable const* rev = GameProtoRev_OSRS241();
    int const archives[3] = { 7, 70000, 123456789 };
    uint8_t payload[TORIRS_CONTROL_INVALIDATE_BYTES_MAX];
    uint8_t wire[256];
    int varp_code = rev->packetin_wire(PKT_NAME_VARP_SMALL);
    int tick_code = rev->packetin_wire(PKT_NAME_SERVER_TICK_END);
    int payload_len = torirs_control_invalidate_encode(5, archives, 3, payload);
    int total = 0;

    assert(rev->opcode_smart2);
    assert(rev->packetin_size(varp_code) == 3);
    assert(rev->packetin_size(tick_code) == 0);

    for( int chunk = 1; chunk <= 64; chunk++ )
    {
        struct Isaac* enc = isaac_new(seed, 4);
        struct Isaac* dec = isaac_new(seed, 4);
        struct PacketBuffer pb;
        int seen = 0;
        int fed = 0;

        total = 0;
        total += put_opcode(enc, rev, varp_code, wire + total);
        wire[total++] = 0x00;
        wire[total++] = 0x2A;
        wire[total++] = 0x05;
        total += put_opcode(enc, rev, TORIRS_CONTROL_ESCAPE, wire + total);
        torirs_control_put_header(wire + total, TORIRS_CONTROL_CACHE_INVALIDATE, payload_len);
        total += TORIRS_CONTROL_HEADER_BYTES;
        memcpy(wire + total, payload, (size_t)payload_len);
        total += payload_len;
        total += put_opcode(enc, rev, tick_code, wire + total);

        packetbuffer_init(&pb, dec, rev);
        while( fed < total )
        {
            int n = total - fed < chunk ? total - fed : chunk;

            /* The reader stops at a complete item, so a read yields at most one. */
            fed += packetbuffer_read(&pb, wire + fed, n);
            if( packetbuffer_ready(&pb) )
            {
                if( seen == 0 )
                {
                    assert(!packetbuffer_is_control(&pb));
                    assert(packetbuffer_packet_type(&pb) == varp_code);
                    assert(packetbuffer_size(&pb) == 3);
                }
                else if( seen == 1 )
                {
                    struct TorirsControlFrame frame;
                    int index;
                    int count;

                    assert(packetbuffer_is_control(&pb));
                    assert(packetbuffer_control_type(&pb) == TORIRS_CONTROL_CACHE_INVALIDATE);
                    frame.type = packetbuffer_control_type(&pb);
                    frame.length = packetbuffer_size(&pb);
                    frame.payload = packetbuffer_data(&pb);
                    assert(torirs_control_invalidate_decode(&frame, &index, &count));
                    assert(index == 5);
                    assert(count == 3);
                    for( int i = 0; i < 3; i++ )
                        assert(torirs_control_invalidate_archive(&frame, i) == archives[i]);
                }
                else
                {
                    assert(seen == 2);
                    assert(!packetbuffer_is_control(&pb));
                    assert(packetbuffer_packet_type(&pb) == tick_code);
                    assert(packetbuffer_size(&pb) == 0);
                }
                seen++;
                packetbuffer_reset(&pb);
            }
        }
        /* The last packet has no payload: it is complete with its opcode. */
        if( packetbuffer_ready(&pb) )
        {
            assert(seen == 2);
            seen++;
            packetbuffer_reset(&pb);
        }
        assert(seen == 3);
        assert(isaac_next(enc) == isaac_next(dec));
        isaac_free(enc);
        isaac_free(dec);
    }
    printf("ok - control escape between packets, every chunk size (%d bytes)\n", total);
}

/*
 * The replay's three control frames (TORIRS_CONTROL_REPLAY_*) as the replay
 * transport stages them: each through the escape, read back by the same
 * reader byte by byte, and decoded to what was encoded. A payload that does not
 * match its own lengths does not decode.
 */
static void
test_control_replay_frames(void)
{
    int32_t seed[4] = { 7, 8, 9, 10 };
    struct GameProtoRevTable const* rev = GameProtoRev_OSRS241();
    static uint8_t wire[4096];
    uint8_t coord_payload[TORIRS_CONTROL_REPLAY_COORD_BYTES_MAX];
    uint8_t world_payload[TORIRS_CONTROL_REPLAY_WORLD_BYTES];
    static uint8_t chat_payload[TORIRS_CONTROL_REPLAY_CHAT_BYTES_MAX];
    uint8_t dialog_payload[TORIRS_CONTROL_REPLAY_DIALOG_BYTES];
    uint8_t const chat_text[5] = { 'h', 'e', 'l', 'l', 'o' };
    int const lens[4] = {
        torirs_control_replay_coord_encode((1 << 28) | (3222 << 14) | 3218, 870, "Zezima", coord_payload),
        torirs_control_replay_world_encode(2983, 17, world_payload),
        torirs_control_replay_chat_encode(1, 2, 0x0102, "", chat_text, 5, chat_payload),
        torirs_control_replay_dialog_encode(PKTOUT_NAME_RESUME_P_COUNTDIALOG, dialog_payload),
    };
    int const types[4] = { TORIRS_CONTROL_REPLAY_COORD, TORIRS_CONTROL_REPLAY_WORLD,
                           TORIRS_CONTROL_REPLAY_CHAT, TORIRS_CONTROL_REPLAY_DIALOG };
    uint8_t const* const payloads[4] = { coord_payload, world_payload, chat_payload, dialog_payload };
    struct Isaac* enc = isaac_new(seed, 4);
    struct Isaac* dec = isaac_new(seed, 4);
    struct PacketBuffer pb;
    int total = 0;
    int fed = 0;
    int seen = 0;

    for( int i = 0; i < 4; i++ )
    {
        assert(torirs_control_type_is_replay(types[i]));
        total += put_opcode(enc, rev, TORIRS_CONTROL_ESCAPE, wire + total);
        torirs_control_put_header(wire + total, types[i], lens[i]);
        total += TORIRS_CONTROL_HEADER_BYTES;
        memcpy(wire + total, payloads[i], (size_t)lens[i]);
        total += lens[i];
    }
    assert(!torirs_control_type_is_replay(TORIRS_CONTROL_CACHE_INVALIDATE));
    packetbuffer_init(&pb, dec, rev);
    while( fed < total )
    {
        fed += packetbuffer_read(&pb, wire + fed, 1);
        if( !packetbuffer_ready(&pb) )
            continue;
        {
            struct TorirsControlFrame frame;

            assert(packetbuffer_is_control(&pb));
            frame.type = packetbuffer_control_type(&pb);
            frame.length = packetbuffer_size(&pb);
            frame.payload = packetbuffer_data(&pb);
            assert(frame.type == types[seen]);
            assert(frame.length == lens[seen]);
            if( seen == 0 )
            {
                struct TorirsControlReplayCoord c;

                assert(torirs_control_replay_coord_decode(&frame, &c));
                assert(c.coord == ((1 << 28) | (3222 << 14) | 3218));
                assert(c.charge == 870);
                assert(strcmp(c.name, "Zezima") == 0);
                /* One byte short: the name runs off the end. */
                frame.length--;
                assert(!torirs_control_replay_coord_decode(&frame, &c));
            }
            else if( seen == 1 )
            {
                struct TorirsControlReplayWorld w;

                assert(torirs_control_replay_world_decode(&frame, &w));
                assert(w.tick == 2983);
                assert(w.serial == 17);
            }
            else if( seen == 3 )
            {
                int name = 0;

                assert(torirs_control_replay_dialog_decode(&frame, &name));
                assert(name == PKTOUT_NAME_RESUME_P_COUNTDIALOG);
                /* One byte short. */
                frame.length--;
                assert(!torirs_control_replay_dialog_decode(&frame, &name));
            }
            else
            {
                struct TorirsControlReplayChat c;

                assert(torirs_control_replay_chat_decode(&frame, &c));
                assert(c.say == 1);
                assert(c.crown == 2);
                assert(c.colour_effect == 0x0102);
                assert(c.name[0] == '\0');
                assert(c.length == 5);
                assert(memcmp(c.data, "hello", 5) == 0);
            }
        }
        seen++;
        packetbuffer_reset(&pb);
    }
    assert(seen == 4);
    assert(isaac_next(enc) == isaac_next(dec));
    isaac_free(enc);
    isaac_free(dec);
    printf("ok - the replay's control frames through the escape (%d bytes)\n", total);
}

int
main(void)
{
    test_login_success();
    test_login_fragmented();
    test_login_rsa_positive_sign();
    test_login_rejected();
    test_reconnect_opcode_creds();
    test_reconnect_opcode_none();
    test_packet_framing();
    test_packet_varu16();
    test_control_escape();
    test_control_replay_frames();
    test_osrs239_partial_empty_slot();
    test_lc_inv_full_size_is_two_bytes();
    test_lc_inv_partial_slot_is_smart();
    printf("net-login: all tests passed\n");
    return 0;
}
