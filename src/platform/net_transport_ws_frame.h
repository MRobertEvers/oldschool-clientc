#ifndef SRC_PLATFORM_NET_TRANSPORT_WS_FRAME_H
#define SRC_PLATFORM_NET_TRANSPORT_WS_FRAME_H

/*
 * Pure RFC 6455 frame codec — no sockets, no allocation, so it is unit-testable
 * in isolation (test-ws-frame). Encodes always-masked client frames; decodes a
 * single frame from a byte buffer, unmasking in place when needed.
 */

#include <stdint.h>

#define WS_OP_CONT 0x0
#define WS_OP_TEXT 0x1
#define WS_OP_BINARY 0x2
#define WS_OP_CLOSE 0x8
#define WS_OP_PING 0x9
#define WS_OP_PONG 0xA

/* Result of a decode attempt. */
enum WsDecodeStatus
{
    WS_DECODE_INCOMPLETE = 0, /* need more bytes; consumed == 0 */
    WS_DECODE_OK = 1,
    WS_DECODE_ERROR = -1, /* malformed / absurd length */
};

struct WsFrame
{
    int fin;
    int opcode;
    uint8_t const* payload; /* points into the decoded buffer */
    int payload_len;
};

/* Encode one client frame (FIN=1, always masked with `mask`) into out.
 * Returns the framed byte count, or -1 if out_cap is too small. */
int
ws_frame_encode(
    int opcode,
    uint8_t const* payload,
    int payload_len,
    uint8_t const mask[4],
    uint8_t* out,
    int out_cap);

/* Decode a single frame from buf[0..len). On WS_DECODE_OK, *consumed is the
 * total frame size (header + payload) and *frame is filled (frame->payload
 * points into buf; a masked frame is unmasked in place, hence buf is mutable).
 * On WS_DECODE_INCOMPLETE, *consumed == 0. */
enum WsDecodeStatus
ws_frame_decode(uint8_t* buf, int len, struct WsFrame* frame, int* consumed);

#endif
