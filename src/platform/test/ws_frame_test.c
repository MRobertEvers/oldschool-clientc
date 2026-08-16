#include "net_transport_ws_frame.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            g_fail = 1;                                                                            \
        }                                                                                          \
    } while( 0 )

/* Encode a client frame, then decode it back (a decoder unmasks in place) and
 * confirm the round trip. */
static void
test_roundtrip_small(void)
{
    uint8_t const payload[] = { 0x10, 0x20, 0x30, 0x40, 0x50 };
    uint8_t const mask[4] = { 0x37, 0x9a, 0xc2, 0x51 };
    uint8_t framed[64];

    int n = ws_frame_encode(WS_OP_BINARY, payload, (int)sizeof(payload), mask, framed, sizeof(framed));
    CHECK(n == 2 + 4 + (int)sizeof(payload)); /* header(2) + mask(4) + payload */
    CHECK(framed[0] == (0x80 | WS_OP_BINARY));
    CHECK(framed[1] == (0x80 | sizeof(payload))); /* MASK bit + len */

    struct WsFrame frame;
    int consumed = 0;
    enum WsDecodeStatus st = ws_frame_decode(framed, n, &frame, &consumed);
    CHECK(st == WS_DECODE_OK);
    CHECK(consumed == n);
    CHECK(frame.fin == 1);
    CHECK(frame.opcode == WS_OP_BINARY);
    CHECK(frame.payload_len == (int)sizeof(payload));
    CHECK(memcmp(frame.payload, payload, sizeof(payload)) == 0);
}

/* A 200-byte payload crosses the 126 threshold into the 16-bit length form. */
static void
test_roundtrip_extended_len(void)
{
    uint8_t payload[200];
    for( int i = 0; i < (int)sizeof(payload); i++ )
        payload[i] = (uint8_t)(i * 7 + 3);
    uint8_t const mask[4] = { 1, 2, 3, 4 };
    uint8_t framed[256];

    int n = ws_frame_encode(WS_OP_BINARY, payload, (int)sizeof(payload), mask, framed, sizeof(framed));
    CHECK(n == 4 + 4 + (int)sizeof(payload)); /* header(4) + mask(4) + payload */
    CHECK((framed[1] & 0x7f) == 126);

    struct WsFrame frame;
    int consumed = 0;
    CHECK(ws_frame_decode(framed, n, &frame, &consumed) == WS_DECODE_OK);
    CHECK(frame.payload_len == (int)sizeof(payload));
    CHECK(memcmp(frame.payload, payload, sizeof(payload)) == 0);
}

/* A server->client (unmasked) frame decodes without touching the payload. */
static void
test_decode_unmasked_server_frame(void)
{
    /* [FIN|binary][len=3][a b c] */
    uint8_t buf[] = { 0x82, 0x03, 'a', 'b', 'c' };
    struct WsFrame frame;
    int consumed = 0;
    CHECK(ws_frame_decode(buf, sizeof(buf), &frame, &consumed) == WS_DECODE_OK);
    CHECK(consumed == 5);
    CHECK(frame.opcode == WS_OP_BINARY);
    CHECK(frame.payload_len == 3);
    CHECK(frame.payload[0] == 'a' && frame.payload[2] == 'c');
}

/*
 * The server direction: mask==NULL emits an unmasked frame, and the
 * header-only form must agree with it byte for byte — that is what lets the
 * mock server write a large packet straight from its own buffer instead of
 * copying it through the codec.
 */
static void
test_encode_unmasked_and_header_only(void)
{
    uint8_t const payload[] = { 0xAA, 0xBB, 0xCC };
    uint8_t framed[64];
    uint8_t header[16];

    int n = ws_frame_encode(WS_OP_BINARY, payload, 3, NULL, framed, sizeof(framed));
    CHECK(n == 2 + 3); /* no 4-byte masking key */
    CHECK(framed[0] == (0x80 | WS_OP_BINARY));
    CHECK(framed[1] == 3); /* MASK bit clear */
    CHECK(memcmp(framed + 2, payload, 3) == 0);

    int h = ws_frame_encode_header(WS_OP_BINARY, 3, NULL, header, sizeof(header));
    CHECK(h == 2);
    CHECK(memcmp(header, framed, (size_t)h) == 0);

    /* And it round-trips through the decoder, which is what the client runs. */
    struct WsFrame frame;
    int consumed = 0;
    CHECK(ws_frame_decode(framed, n, &frame, &consumed) == WS_DECODE_OK);
    CHECK(consumed == n);
    CHECK(frame.payload_len == 3);
    CHECK(memcmp(frame.payload, payload, 3) == 0);

    /* A payload past 65535 takes the 10-byte header; a header-only caller must
     * get that length back even though no payload buffer was involved. */
    h = ws_frame_encode_header(WS_OP_BINARY, 100000, NULL, header, sizeof(header));
    CHECK(h == 10);
    CHECK((header[1] & 0x7f) == 127);
}

/* A ping frame decodes to opcode PING with its payload intact. */
static void
test_decode_ping(void)
{
    uint8_t buf[] = { 0x89, 0x02, 0xDE, 0xAD }; /* FIN|ping, len 2 */
    struct WsFrame frame;
    int consumed = 0;
    CHECK(ws_frame_decode(buf, sizeof(buf), &frame, &consumed) == WS_DECODE_OK);
    CHECK(frame.opcode == WS_OP_PING);
    CHECK(frame.payload_len == 2);
}

/* A truncated buffer must report INCOMPLETE, never over-read. */
static void
test_incomplete(void)
{
    uint8_t buf[] = { 0x82, 0x05, 'a', 'b' }; /* claims 5, only 2 present */
    struct WsFrame frame;
    int consumed = 99;
    CHECK(ws_frame_decode(buf, sizeof(buf), &frame, &consumed) == WS_DECODE_INCOMPLETE);
    CHECK(consumed == 0);

    /* Header itself incomplete (need the extended-length bytes). */
    uint8_t hdr[] = { 0x82, 0x7e }; /* len==126, but the 2 length bytes are missing */
    CHECK(ws_frame_decode(hdr, sizeof(hdr), &frame, &consumed) == WS_DECODE_INCOMPLETE);
}

/* Two concatenated frames decode one at a time (the batched-message case). */
static void
test_two_frames_back_to_back(void)
{
    uint8_t buf[] = { 0x82, 0x02, 0x11, 0x22, 0x82, 0x01, 0x33 };
    struct WsFrame frame;
    int consumed = 0;

    CHECK(ws_frame_decode(buf, sizeof(buf), &frame, &consumed) == WS_DECODE_OK);
    CHECK(consumed == 4);
    CHECK(frame.payload_len == 2 && frame.payload[0] == 0x11);

    CHECK(ws_frame_decode(buf + 4, (int)sizeof(buf) - 4, &frame, &consumed) == WS_DECODE_OK);
    CHECK(consumed == 3);
    CHECK(frame.payload_len == 1 && frame.payload[0] == 0x33);
}

int
main(void)
{
    test_roundtrip_small();
    test_roundtrip_extended_len();
    test_decode_unmasked_server_frame();
    test_encode_unmasked_and_header_only();
    test_decode_ping();
    test_incomplete();
    test_two_frames_back_to_back();

    if( g_fail )
    {
        fprintf(stderr, "ws_frame_test: FAILED\n");
        return 1;
    }
    printf("ws_frame_test: PASS\n");
    return 0;
}
