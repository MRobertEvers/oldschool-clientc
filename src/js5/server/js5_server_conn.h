#ifndef SRC_JS5_SERVER_JS5_SERVER_CONN_H
#define SRC_JS5_SERVER_JS5_SERVER_CONN_H

/*
 * One JS5 connection's bytes, with no socket in sight.
 *
 * Js5ServerSession is already socket-free, and this is the layer above it that
 * is also socket-free: framing. Between them they are a complete JS5 server
 * connection expressed as "here are bytes that arrived" and "here are bytes to
 * send", which is what lets two different servers host it —
 *
 *   js5_server   a dedicated select() reactor, one connection per client
 *   io_server    the web build's HTTP server, on a second listening port,
 *                inside the same loop that serves files and IO batches
 *
 * — without either of them reimplementing the other's framing, and without a
 * thread.
 *
 * ## Framing is decided, never configured
 *
 * The first byte says which protocol this is: an HTTP upgrade opens with 'G',
 * a JS5 stream with opcode 15. One byte is also all that may be looked at,
 * because a raw client sends its handshake and then waits for the reply, so
 * reading further would deadlock.
 *
 * The WebSocket branch is not optional in practice. Emscripten implements BSD
 * sockets as WebSockets, so the browser build's connect() arrives as an upgrade
 * and every byte after it is inside a frame; a server that speaks only raw TCP
 * is unreachable from a page.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct Js5ServerCache;
struct Js5ServerSession;
struct Js5ServerSessionConfig;

/* Inbound only has to hold an upgrade request and a little slack: the largest
 * thing a client sends is the 21-byte handshake or a 4-byte packet. Outbound
 * holds one framed chunk of session output, which PeekOutput caps at its 16KB
 * scratch, plus a frame header. */
#define JS5_CONN_IN_BYTES 8192
#define JS5_CONN_OUT_BYTES (16384 + 32)

enum Js5ServerFraming
{
    JS5_FRAMING_UNKNOWN = 0,
    JS5_FRAMING_RAW,
    JS5_FRAMING_WS_HANDSHAKE,
    JS5_FRAMING_WS,
};

struct Js5ServerConn
{
    struct Js5ServerSession* session;
    enum Js5ServerFraming framing;

    /* Bytes off the socket that are not yet a whole request or frame. A raw
     * connection never accumulates here — there is nothing to reassemble. */
    uint8_t in[JS5_CONN_IN_BYTES];
    int in_len;

    /* Framed bytes waiting for the socket. Used under WebSocket framing, where
     * a half-written frame cannot be resumed from the session's own view: the
     * header announces a length that must arrive whole. */
    uint8_t out[JS5_CONN_OUT_BYTES];
    int out_len;
    int out_pos;
};

/*
 * Attach a session to this connection. `cache` may be NULL only when the config
 * says server_full, which is how a capacity rejection is expressed.
 * Returns false when the session could not be created.
 */
bool
Js5ServerConn_Open(
    struct Js5ServerConn* conn,
    struct Js5ServerCache* cache,
    const struct Js5ServerSessionConfig* config,
    uint64_t now_ms);

void
Js5ServerConn_Close(struct Js5ServerConn* conn);

/*
 * Hand over bytes that arrived. Returns 0 to keep the connection and -1 to drop
 * it — a protocol error, a CLOSE frame, or a session the request desynced.
 */
int
Js5ServerConn_Feed(
    struct Js5ServerConn* conn,
    const uint8_t* data,
    int size,
    uint64_t now_ms);

/*
 * The next contiguous bytes to write, framed as this connection needs. Returns
 * 1 with a view, 0 when there is nothing to send, -1 on failure. The view stays
 * valid until the next call on this connection.
 *
 * `budget` caps how much session output is taken in one go, so one client
 * cannot monopolise a shared loop.
 */
int
Js5ServerConn_TakeOutput(
    struct Js5ServerConn* conn,
    size_t budget,
    const uint8_t** data,
    int* size,
    uint64_t now_ms);

/*
 * Report how much of the last view actually reached the socket.
 *
 * now_ms is not decoration: under raw framing this reaches the session's
 * output-progress deadline, and a stale value there reads as a client that has
 * not accepted a byte in output_timeout_ms — which disconnects a healthy
 * download.
 */
void
Js5ServerConn_Consumed(
    struct Js5ServerConn* conn,
    int size,
    uint64_t now_ms);

/** True when bytes are already framed and waiting — the connection wants to be
 *  polled for writability even if the session itself has nothing queued. */
bool
Js5ServerConn_HasPendingOutput(const struct Js5ServerConn* conn);

#endif
