/*
 * WebSocket (and raw TCP) transport for the mock 230 server. See torirs_server_ws.h
 * for why one port serves both.
 *
 * The browser build has no choice about this: emscripten implements BSD
 * sockets over WebSockets, so a page's connect() to 127.0.0.1:43595 arrives
 * here as an HTTP/1.1 upgrade and every later byte arrives inside a frame. The
 * native client still speaks raw TCP. Both are the same 230 byte stream once
 * the framing is peeled off, which is the whole point of putting the seam here
 * rather than teaching the protocol code about transports.
 */
#include "torirs_server_ws.h"
#include <assert.h>

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
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* Socket helpers                                                      */
/* ------------------------------------------------------------------ */

/*
 * "Nothing right now" and "interrupted", asked portably.
 *
 * Winsock never touches errno -- every failure code is behind
 * WSAGetLastError() -- so the errno tests this file used to do inline were
 * reading a stale value on Windows. On a blocking socket that was harmless
 * (neither condition could arise); on a non-blocking one, EWOULDBLOCK is the
 * ordinary case and misreading it drops a live connection.
 */
static int
sock_would_block(void)
{
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int
sock_interrupted(void)
{
#ifdef _WIN32
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

static void
sock_set_nonblocking(int fd)
{
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(fd, FIONBIO, &on);
#else
    int flags = fcntl(fd, F_GETFL, 0);

    if( flags >= 0 )
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

/*
 * Queue bytes for the peer and hand the kernel as many as it will take.
 *
 * Always accepts the whole of `data`: a caller that had to cope with a short
 * write would need its own remainder buffer, which is this one. Returns 0 once
 * the connection is dead -- the peer left, or it has fallen further behind than
 * TORIRSSERVER_CONN_OUT_MAX, which is not slowness but a peer that stopped
 * reading.
 */
static int
conn_out_push(
    struct ToriRSServerConn* conn,
    uint8_t const* data,
    int len)
{
    if( conn->closed )
        return 0;
    if( ToriRSServer_PipeAvailable(&conn->out) + len > TORIRSSERVER_CONN_OUT_MAX )
    {
        fprintf(stderr, "torirsserver: peer is %d bytes behind; dropping the connection\n",
                ToriRSServer_PipeAvailable(&conn->out));
        conn->closed = 1;
        return 0;
    }
    ToriRSServer_PipeWrite(&conn->out, data, len);
    return ToriRSServer_ConnFlush(conn);
}

/* Append to the deframed application buffer, refusing rather than truncating:
 * a dropped byte is a desynced ISAAC stream, which fails far from here. */
static int
app_append(
    struct ToriRSServerConn* conn,
    uint8_t const* data,
    int len)
{
    if( len <= 0 )
        return 1;
    if( conn->app_len + len > TORIRSSERVER_CONN_APP_MAX )
    {
        fprintf(stderr, "torirsserver: inbound overflow (%d + %d bytes)\n", conn->app_len, len);
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
 * Read what has arrived of the upgrade request and answer it once it is whole.
 *
 * The parsing and the Sec-WebSocket-Accept digest are shared with js5_server
 * through net_transport_ws_handshake.h; what stays here is this server's own
 * choice about how the request is read, which is now one non-blocking read per
 * call with the partial request held in `raw`. It used to be a loop that
 * blocked until the headers were complete, which one connection per process
 * could afford and one thread per server cannot: a peer that opens a socket
 * and sends half a header line would otherwise hold the world tick.
 */
static enum ToriRSServerConnOpen
ws_handshake_step(struct ToriRSServerConn* conn)
{
    struct WsHandshake handshake;
    enum WsHandshakeStatus status;
    ssize_t got;

    if( conn->raw_len >= WS_HANDSHAKE_REQUEST_MAX )
    {
        fprintf(stderr, "torirsserver: websocket request headers too large\n");
        return TORIRSSERVER_CONN_OPEN_FAILED;
    }

    got = read(conn->fd, conn->raw + conn->raw_len,
               (size_t)(WS_HANDSHAKE_REQUEST_MAX - conn->raw_len));
    if( got < 0 )
    {
        if( sock_interrupted() || sock_would_block() )
            return TORIRSSERVER_CONN_OPENING;
        return TORIRSSERVER_CONN_OPEN_FAILED;
    }
    if( got == 0 )
        return TORIRSSERVER_CONN_OPEN_FAILED;
    conn->raw_len += (int)got;

    status = WsHandshake_Consume(conn->raw, conn->raw_len, &handshake);
    if( status == WS_HANDSHAKE_INCOMPLETE )
        return TORIRSSERVER_CONN_OPENING;
    if( status != WS_HANDSHAKE_OK )
    {
        fprintf(stderr, "torirsserver: malformed websocket upgrade request\n");
        return TORIRSSERVER_CONN_OPEN_FAILED;
    }

    /* `ws` before the response goes out, so the queue this pushes into is the
     * one every later send shares -- ordering, not framing: the response is
     * plain HTTP either way because conn_out_push does not frame. */
    conn->ws = 1;
    if( !conn_out_push(conn, (uint8_t const*)handshake.response, handshake.response_len) )
        return TORIRSSERVER_CONN_OPEN_FAILED;

    /* Anything the client pipelined after the headers is already frame data. */
    conn->raw_len -= handshake.consumed;
    if( conn->raw_len > 0 )
        memmove(conn->raw, conn->raw + handshake.consumed, (size_t)conn->raw_len);
    else
        conn->raw_len = 0;

    conn->opening = 0;
    fprintf(
        stderr,
        "torirsserver: websocket client (subprotocol %s)\n",
        handshake.protocol[0] ? handshake.protocol : "none");
    return TORIRSSERVER_CONN_OPEN_READY;
}

void
ToriRSServer_ConnBegin(
    struct ToriRSServerConn* conn,
    int fd)
{
    assert(conn);
    memset(conn, 0, sizeof(*conn));
    conn->fd = fd;
    conn->opening = 1;
    sock_set_nonblocking(fd);
}

enum ToriRSServerConnOpen
ToriRSServer_ConnOpenStep(struct ToriRSServerConn* conn)
{
    uint8_t first = 0;
    ssize_t peeked;

    assert(conn);
    if( conn->closed )
        return TORIRSSERVER_CONN_OPEN_FAILED;
    if( !conn->opening )
        return TORIRSSERVER_CONN_OPEN_READY;
    if( conn->upgrade )
        return ws_handshake_step(conn);

    /*
     * One byte is enough to tell the two apart and is all that can be looked
     * at: a raw 230 client sends opcode 14 alone and then waits for the
     * response, so peeking any further would deadlock. It is peeked rather
     * than taken because a raw connection's first byte is the session's, and
     * the session reads it through the ordinary recv path.
     */
    peeked = recv(conn->fd, (char*)&first, 1, MSG_PEEK);
    if( peeked < 0 )
    {
        if( sock_interrupted() || sock_would_block() )
            return TORIRSSERVER_CONN_OPENING;
        return TORIRSSERVER_CONN_OPEN_FAILED;
    }
    if( peeked == 0 )
        return TORIRSSERVER_CONN_OPEN_FAILED;

    if( first == 'G' )
    {
        conn->upgrade = 1;
        return ws_handshake_step(conn);
    }
    conn->opening = 0;
    return TORIRSSERVER_CONN_OPEN_READY;
}

/* ------------------------------------------------------------------ */
/* Byte stream                                                         */
/* ------------------------------------------------------------------ */

/* Turn whatever whole frames sit in `raw` into application bytes. */
static int
ws_deframe(struct ToriRSServerConn* conn)
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
            fprintf(stderr, "torirsserver: malformed websocket frame\n");
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
            if( framed > 0 && !conn_out_push(conn, pong, framed) )
                return 0;
            break;
        }
        case WS_OP_PONG:
            break;
        case WS_OP_CLOSE:
            return 0;
        default:
            fprintf(stderr, "torirsserver: unknown websocket opcode %d\n", frame.opcode);
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
conn_fill(struct ToriRSServerConn* conn)
{
    ssize_t got;
    int space;

    if( conn->closed )
        return 0;

    if( !conn->ws )
    {
        space = TORIRSSERVER_CONN_APP_MAX - conn->app_len;
        if( space <= 0 )
            return 1;
        got = read(conn->fd, conn->app + conn->app_len, (size_t)space);
        if( got < 0 )
        {
            if( sock_interrupted() || sock_would_block() )
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

    space = TORIRSSERVER_CONN_RAW_MAX - conn->raw_len;
    if( space <= 0 )
    {
        fprintf(stderr, "torirsserver: websocket frame larger than the read buffer\n");
        conn->closed = 1;
        return 0;
    }
    got = read(conn->fd, conn->raw + conn->raw_len, (size_t)space);
    if( got < 0 )
    {
        if( sock_interrupted() || sock_would_block() )
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

int
ToriRSServer_ConnBuffered(const struct ToriRSServerConn* conn)
{
    return conn->app_len;
}

int
ToriRSServer_ConnPending(const struct ToriRSServerConn* conn)
{
    return ToriRSServer_PipeAvailable(&conn->out);
}

int
ToriRSServer_ConnFlush(struct ToriRSServerConn* conn)
{
    assert(conn);
    if( conn->closed || conn->fd < 0 )
        return 0;

    for( ;; )
    {
        int len = 0;
        const uint8_t* live = ToriRSServer_PipePeek(&conn->out, &len);
        ssize_t step;

        if( !live )
            return 1;
        step = write(conn->fd, live, (size_t)len);
        if( step < 0 )
        {
            if( sock_interrupted() )
                continue;
            /* The ordinary case: the send window is full. What is left stays
             * queued and the host re-enters when select() says writable. */
            if( sock_would_block() )
                return 1;
            conn->closed = 1;
            return 0;
        }
        if( step == 0 )
        {
            conn->closed = 1;
            return 0;
        }
        ToriRSServer_PipeDrop(&conn->out, (int)step);
    }
}

void
ToriRSServer_ConnFree(struct ToriRSServerConn* conn)
{
    assert(conn);
    ToriRSServer_PipeFree(&conn->out);
}

static int
app_take(
    struct ToriRSServerConn* conn,
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
ToriRSServer_ConnRecv(
    struct ToriRSServerConn* conn,
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
ToriRSServer_ConnSend(
    struct ToriRSServerConn* conn,
    uint8_t const* data,
    int len)
{
    assert(conn);
    if( conn->closed || conn->fd < 0 )
        return -1;
    if( len <= 0 )
        return 0;

    if( conn->ws )
    {
        /* Header separately — the payload is queued unmasked and unmodified,
         * so it never has to be copied twice however large the packet is. The
         * two pushes are adjacent in one queue, so nothing can be interleaved
         * between a header and its payload. */
        uint8_t header[16];
        int header_len = ws_frame_encode_header(WS_OP_BINARY, len, NULL, header, sizeof(header));
        if( header_len < 0 )
        {
            conn->closed = 1;
            return -1;
        }
        if( !conn_out_push(conn, header, header_len) )
            return -1;
    }

    if( !conn_out_push(conn, data, len) )
        return -1;
    return len;
}
