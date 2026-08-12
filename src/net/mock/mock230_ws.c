/*
 * WebSocket (and raw TCP) transport for the mock 230 server. See mock230_ws.h
 * for why one port serves both.
 *
 * The browser build has no choice about this: emscripten implements BSD
 * sockets over WebSockets, so a page's connect() to 127.0.0.1:43595 arrives
 * here as an HTTP/1.1 upgrade and every later byte arrives inside a frame. The
 * native client still speaks raw TCP. Both are the same 230 byte stream once
 * the framing is peeled off, which is the whole point of putting the seam here
 * rather than teaching the protocol code about transports.
 */
#include "mock230_ws.h"

#include "platform/net_transport_ws_frame.h"
#include "platform/net_transport_ws_handshake.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
/* win32 build: winsock instead of BSD sockets. close()->closesocket(); the
 * embedded server never opens this websocket path at runtime (net_transport_embed
 * bridges in-process), so this only has to compile and link. */
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
/* This file does low-level read()/write() on socket fds; winsock uses
 * recv()/send(). The buffers are byte data, so the char* casts are safe. */
#define read(fd, buf, n)  recv((fd), (char*)(buf), (int)(n), 0)
#define write(fd, buf, n) send((fd), (const char*)(buf), (int)(n), 0)
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* Socket helpers                                                      */
/* ------------------------------------------------------------------ */

static int
write_all(
    int fd,
    uint8_t const* data,
    int len)
{
    int sent = 0;
    while( sent < len )
    {
        ssize_t step = write(fd, data + sent, (size_t)(len - sent));
        if( step < 0 )
        {
            if( errno == EINTR )
                continue;
            return -1;
        }
        if( step == 0 )
            return -1;
        sent += (int)step;
    }
    return sent;
}

/* Append to the deframed application buffer, refusing rather than truncating:
 * a dropped byte is a desynced ISAAC stream, which fails far from here. */
static int
app_append(
    struct Mock230Conn* conn,
    uint8_t const* data,
    int len)
{
    if( len <= 0 )
        return 1;
    if( conn->app_len + len > MOCK230_CONN_APP_MAX )
    {
        fprintf(stderr, "mock230: inbound overflow (%d + %d bytes)\n", conn->app_len, len);
        return 0;
    }
    memcpy(conn->app + conn->app_len, data, (size_t)len);
    conn->app_len += len;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Handshake                                                           */
/* ------------------------------------------------------------------ */

/*
 * Read the upgrade request and answer it.
 *
 * The parsing and the Sec-WebSocket-Accept digest are shared with js5_server
 * through net_transport_ws_handshake.h; what stays here is this server's own
 * choice about how the request is read, which is a blocking loop because this
 * connection layer is a blocking one.
 */
static int
ws_handshake(struct Mock230Conn* conn)
{
    uint8_t request[WS_HANDSHAKE_REQUEST_MAX];
    int len = 0;
    struct WsHandshake handshake;
    enum WsHandshakeStatus status = WS_HANDSHAKE_INCOMPLETE;

    for( ;; )
    {
        ssize_t got;

        if( len >= (int)sizeof(request) )
        {
            fprintf(stderr, "mock230: websocket request headers too large\n");
            return 0;
        }
        got = read(conn->fd, request + len, sizeof(request) - (size_t)len);
        if( got < 0 )
        {
            if( errno == EINTR )
                continue;
            return 0;
        }
        if( got == 0 )
            return 0;
        len += (int)got;

        status = WsHandshake_Consume(request, len, &handshake);
        if( status != WS_HANDSHAKE_INCOMPLETE )
            break;
    }

    if( status != WS_HANDSHAKE_OK )
    {
        fprintf(stderr, "mock230: malformed websocket upgrade request\n");
        return 0;
    }

    if( write_all(conn->fd, (uint8_t const*)handshake.response, handshake.response_len) < 0 )
        return 0;

    /* Anything the client pipelined after the headers is already frame data. */
    conn->raw_len = len - handshake.consumed;
    if( conn->raw_len > 0 )
        memcpy(conn->raw, request + handshake.consumed, (size_t)conn->raw_len);
    else
        conn->raw_len = 0;

    conn->ws = 1;
    fprintf(
        stderr,
        "mock230: websocket client (subprotocol %s)\n",
        handshake.protocol[0] ? handshake.protocol : "none");
    return 1;
}

int
mock230_conn_open(
    struct Mock230Conn* conn,
    int fd)
{
    uint8_t first = 0;
    ssize_t peeked;

    memset(conn, 0, sizeof(*conn));
    conn->fd = fd;

    /*
     * One byte is enough to tell the two apart and is all that can be looked
     * at: a raw 230 client sends opcode 14 alone and then waits for the
     * response, so peeking any further would deadlock.
     */
    for( ;; )
    {
        peeked = recv(fd, &first, 1, MSG_PEEK);
        if( peeked < 0 && errno == EINTR )
            continue;
        break;
    }
    if( peeked <= 0 )
        return 0;

    if( first == 'G' )
        return ws_handshake(conn);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Byte stream                                                         */
/* ------------------------------------------------------------------ */

/* Turn whatever whole frames sit in `raw` into application bytes. */
static int
ws_deframe(struct Mock230Conn* conn)
{
    int pos = 0;

    for( ;; )
    {
        struct WsFrame frame;
        int consumed = 0;
        enum WsDecodeStatus status =
            ws_frame_decode(conn->raw + pos, conn->raw_len - pos, &frame, &consumed);

        if( status == WS_DECODE_INCOMPLETE )
            break;
        if( status == WS_DECODE_ERROR )
        {
            fprintf(stderr, "mock230: malformed websocket frame\n");
            return 0;
        }

        switch( frame.opcode )
        {
        case WS_OP_BINARY:
        case WS_OP_TEXT:
        case WS_OP_CONT:
            if( !app_append(conn, frame.payload, frame.payload_len) )
                return 0;
            break;
        case WS_OP_PING:
        {
            uint8_t pong[256];
            int n = frame.payload_len > 125 ? 125 : frame.payload_len;
            int framed = ws_frame_encode(WS_OP_PONG, frame.payload, n, NULL, pong, sizeof(pong));
            if( framed > 0 && write_all(conn->fd, pong, framed) < 0 )
                return 0;
            break;
        }
        case WS_OP_PONG:
            break;
        case WS_OP_CLOSE:
            return 0;
        default:
            fprintf(stderr, "mock230: unknown websocket opcode %d\n", frame.opcode);
            return 0;
        }

        pos += consumed;
    }

    if( pos > 0 )
    {
        conn->raw_len -= pos;
        if( conn->raw_len > 0 )
            memmove(conn->raw, conn->raw + pos, (size_t)conn->raw_len);
    }
    return 1;
}

/* One read() off the socket, deframed into `app`. Returns 0 once dead. */
static int
conn_fill(struct Mock230Conn* conn)
{
    ssize_t got;
    int space;

    if( conn->closed )
        return 0;

    if( !conn->ws )
    {
        space = MOCK230_CONN_APP_MAX - conn->app_len;
        if( space <= 0 )
            return 1;
        got = read(conn->fd, conn->app + conn->app_len, (size_t)space);
        if( got < 0 )
        {
            if( errno == EINTR )
                return 1;
            if( errno == EAGAIN || errno == EWOULDBLOCK )
                return 1;
            conn->closed = 1;
            return 0;
        }
        if( got == 0 )
        {
            conn->closed = 1;
            return 0;
        }
        conn->app_len += (int)got;
        return 1;
    }

    space = MOCK230_CONN_RAW_MAX - conn->raw_len;
    if( space <= 0 )
    {
        fprintf(stderr, "mock230: websocket frame larger than the read buffer\n");
        conn->closed = 1;
        return 0;
    }
    got = read(conn->fd, conn->raw + conn->raw_len, (size_t)space);
    if( got < 0 )
    {
        if( errno == EINTR )
            return 1;
        if( errno == EAGAIN || errno == EWOULDBLOCK )
            return 1;
        conn->closed = 1;
        return 0;
    }
    if( got == 0 )
    {
        conn->closed = 1;
        return 0;
    }
    conn->raw_len += (int)got;

    if( !ws_deframe(conn) )
    {
        conn->closed = 1;
        return 0;
    }
    return 1;
}

static int
app_take(
    struct Mock230Conn* conn,
    uint8_t* out,
    int max)
{
    int take = conn->app_len < max ? conn->app_len : max;
    if( take <= 0 )
        return 0;
    memcpy(out, conn->app, (size_t)take);
    conn->app_len -= take;
    if( conn->app_len > 0 )
        memmove(conn->app, conn->app + take, (size_t)conn->app_len);
    return take;
}

int
mock230_conn_recv(
    struct Mock230Conn* conn,
    uint8_t* out,
    int max)
{
    if( conn->app_len == 0 )
    {
        if( !conn_fill(conn) )
            return conn->app_len > 0 ? app_take(conn, out, max) : -1;
    }
    return app_take(conn, out, max);
}

int
mock230_conn_send(
    struct Mock230Conn* conn,
    uint8_t const* data,
    int len)
{
    if( !conn || conn->closed || conn->fd < 0 )
        return -1;
    if( len <= 0 )
        return 0;

    if( conn->ws )
    {
        /* Header only — the payload goes out unmasked and unmodified, so it
         * never has to be copied however large the packet is. */
        uint8_t header[16];
        int header_len = ws_frame_encode_header(WS_OP_BINARY, len, NULL, header, sizeof(header));
        if( header_len < 0 )
        {
            conn->closed = 1;
            return -1;
        }
        if( write_all(conn->fd, header, header_len) < 0 )
        {
            conn->closed = 1;
            return -1;
        }
    }

    if( write_all(conn->fd, data, len) < 0 )
    {
        conn->closed = 1;
        return -1;
    }
    return len;
}
