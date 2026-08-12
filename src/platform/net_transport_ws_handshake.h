#ifndef SRC_PLATFORM_NET_TRANSPORT_WS_HANDSHAKE_H
#define SRC_PLATFORM_NET_TRANSPORT_WS_HANDSHAKE_H

/*
 * The server side of the RFC 6455 opening handshake, as a pure function over
 * bytes — no sockets, no allocation, no blocking, the same way
 * net_transport_ws_frame.h treats framing.
 *
 * That shape is what lets one implementation serve two servers that could not
 * otherwise share code: the mock game server reads its request with a blocking
 * loop, and js5_server is a nonblocking reactor where the request may arrive
 * across any number of reads. Both hand whatever they have accumulated to
 * WsHandshake_Consume and get told to wait, proceed, or give up.
 *
 * Why a server in this tree needs it at all: emscripten implements BSD sockets
 * as WebSockets, so a browser build's connect() arrives as an HTTP upgrade and
 * every byte after it is framed. A server that only speaks raw TCP is simply
 * unreachable from a page.
 */

#include <stdint.h>

enum WsHandshakeStatus
{
    /* No complete request yet (no CRLF CRLF). Nothing was consumed. */
    WS_HANDSHAKE_INCOMPLETE = 0,
    WS_HANDSHAKE_OK = 1,
    /* Malformed, oversized, or missing Sec-WebSocket-Key. */
    WS_HANDSHAKE_ERROR = -1,
};

#define WS_HANDSHAKE_RESPONSE_MAX 512
#define WS_HANDSHAKE_PROTOCOL_MAX 128
/* A browser's upgrade request is a few hundred bytes; anything past this is
 * not one, and is refused rather than buffered. */
#define WS_HANDSHAKE_REQUEST_MAX 4096

struct WsHandshake
{
    /** The 101 response to write, on WS_HANDSHAKE_OK. */
    char response[WS_HANDSHAKE_RESPONSE_MAX];
    int response_len;
    /** The subprotocol echoed back, or "" when none was offered. */
    char protocol[WS_HANDSHAKE_PROTOCOL_MAX];
    /** Request bytes consumed. Anything after them is already frame data —
     *  a client may pipeline, and dropping that would desync the stream. */
    int consumed;
};

/*
 * Look at `len` bytes of a client's opening request.
 *
 * On WS_HANDSHAKE_OK, `out` holds the response to send and the count of
 * request bytes to drop from the caller's buffer.
 *
 * The one subtlety worth knowing is Sec-WebSocket-Protocol. Emscripten asks for
 * "binary", and a browser fails the connection outright if the server does not
 * confirm a requested subprotocol — so the first one offered is echoed rather
 * than ignored. A server that skips this looks, from the page, like one that is
 * simply not listening.
 */
enum WsHandshakeStatus
WsHandshake_Consume(
    uint8_t const* data,
    int len,
    struct WsHandshake* out);

/*
 * Is this the first byte of an HTTP upgrade rather than of a raw protocol?
 *
 * One byte is all a server may look at before deciding: a raw client sends its
 * opening opcode alone and then waits for the reply, so peeking further would
 * deadlock. 'G' (of "GET") is enough, because no raw protocol in this tree
 * opens with it — JS5 opens with 15 and the 230 login with 14.
 */
static inline int
WsHandshake_LooksLikeHttp(uint8_t first)
{
    return first == 'G';
}

#endif
