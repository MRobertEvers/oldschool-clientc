/*
 * ToriRSMapEd's loopback front door (torirs_maped_net.h): the listening
 * socket, and per connection the byte transport a session reads and writes
 * -- raw frames or RFC 6455, decided by the first byte. Lifted out of the
 * daemon's main so the embedded server can serve the same door from the
 * client's frame loop.
 */

#include "torirs_maped_net.h"

#include "log/torirs_log.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stddef.h>

#if !defined(__EMSCRIPTEN__)

#include "torirs_maped.h"
#include "torirs_maped_buf.h"
#include "platform/net_transport_ws_frame.h"
#include "platform/net_transport_ws_handshake.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Sockets as the game server has them (torirsserver/torirs_server_host.c):
 * winsock on Windows, BSD sockets elsewhere, a socket an int either way. The
 * names below are this file's only differences between the two. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close(fd) closesocket((SOCKET)(fd))
#define net_errno() WSAGetLastError()
/* Nothing to read or room to write now; come back next poll. */
#define NET_RETRY(error) ((error) == WSAEWOULDBLOCK || (error) == WSAEINTR)
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define net_errno() errno
#define NET_RETRY(error) ((error) == EAGAIN || (error) == EWOULDBLOCK || (error) == EINTR)
#endif

/** A poll with no timeout must never block in accept() or recv(). */
static void
net_nonblocking(int fd)
{
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket((SOCKET)fd, FIONBIO, &on);
#else
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
}

/** The last socket error, for a person ("10048" means little). */
static const char*
net_why(void)
{
#ifdef _WIN32
    static char text[160];
    int const error = WSAGetLastError();
    if( !FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)error, 0, text,
            sizeof(text), NULL) )
        snprintf(text, sizeof(text), "winsock error %d", error);
    for( char* scan = text; *scan; scan++ )
        if( *scan == '\r' || *scan == '\n' )
            *scan = '\0';
    return text;
#else
    return strerror(errno);
#endif
}

/** Outbound backlog one connection may accumulate before the daemon gives
 *  up on it. Generous — a square's text is ~200KB — because the point is to
 *  catch a peer that has STOPPED reading, not one that is merely slow. */
#define MAPED_SEND_QUEUE_MAX (8 * 1024 * 1024)

/*
 * How this connection carries the protocol.
 *
 * A browser cannot open a TCP socket, so a wasm client's connect() arrives as
 * an HTTP upgrade and every byte after it is RFC 6455 framed — a daemon that
 * only speaks raw TCP is simply unreachable from a page. Both are served on
 * the one port, chosen by the first byte, the same way js5_server and the
 * game server do it: a raw client sends its opening bytes and waits, so
 * peeking further than one byte would deadlock.
 */
enum conn_mode
{
    /** Nothing read yet; the first byte decides. */
    CONN_MODE_UNDECIDED = 0,
    CONN_MODE_RAW,
    /** An upgrade request is arriving. */
    CONN_MODE_WS_HANDSHAKE,
    CONN_MODE_WS,
};

struct fd_ctx
{
    int fd;
    /** Queued outbound socket bytes — already framed when this is a
     *  WebSocket, so the flush path never has to know the mode. */
    struct ToriRSMapEdBuf out;
    /** Socket bytes not yet turned into protocol bytes: the upgrade request
     *  while handshaking, undecoded frames afterwards. */
    struct ToriRSMapEdBuf raw_in;
    /** Protocol bytes ready for the session — frame payloads, concatenated.
     *  On a raw connection this is just what arrived. */
    struct ToriRSMapEdBuf plain_in;
    enum conn_mode mode;
    int dead;
    /** The listener's log prefix. */
    const char* who;
};

/** Move everything waiting on a raw connection straight through. */
static void
promote_raw(struct fd_ctx* c)
{
    int pending = ToriRSMapEd_BufAvailable(&c->raw_in);

    if( pending <= 0 )
        return;
    ToriRSMapEd_BufWrite(&c->plain_in, ToriRSMapEd_BufPeek(&c->raw_in), pending);
    ToriRSMapEd_BufConsume(&c->raw_in, pending);
}

/**
 * Turn buffered WebSocket frames into protocol bytes.
 *
 * Control frames are answered here rather than passed up: the session speaks
 * ToriRSMapEd frames and knows nothing about RFC 6455, which is the whole
 * point of putting the mode in the transport.
 */
static void
promote_ws(struct fd_ctx* c)
{
    for( ;; )
    {
        struct WsFrame frame;
        int consumed = 0;
        enum WsDecodeStatus status;
        int pending = ToriRSMapEd_BufAvailable(&c->raw_in);

        if( pending <= 0 )
            return;

        /* Decoding unmasks in place, so it needs the buffer's own storage
         * rather than a const view of it. */
        status = ws_frame_decode(
            c->raw_in.data + c->raw_in.head, pending, &frame, &consumed);
        if( status == WS_DECODE_INCOMPLETE )
            return;
        if( status == WS_DECODE_ERROR )
        {
            TORIRS_REPORT("%s: malformed WebSocket frame — closing\n", c->who);
            c->dead = 1;
            return;
        }

        switch( frame.opcode )
        {
        case WS_OP_BINARY:
        case WS_OP_TEXT:
        case WS_OP_CONT:
            if( frame.payload_len > 0 )
                ToriRSMapEd_BufWrite(&c->plain_in, frame.payload, frame.payload_len);
            break;
        case WS_OP_PING:
        {
            /* A pong carries the ping's payload back, unmasked (RFC 6455
             * §5.1: a server never masks). */
            uint8_t pong[128 + 14];
            int framed = ws_frame_encode(
                WS_OP_PONG,
                frame.payload,
                frame.payload_len > 125 ? 125 : frame.payload_len,
                NULL,
                pong,
                (int)sizeof(pong));
            if( framed > 0 )
                ToriRSMapEd_BufWrite(&c->out, pong, framed);
            break;
        }
        case WS_OP_CLOSE:
            c->dead = 1;
            ToriRSMapEd_BufConsume(&c->raw_in, consumed);
            return;
        default:
            break;
        }
        ToriRSMapEd_BufConsume(&c->raw_in, consumed);
    }
}

/** Decide the mode, complete an upgrade if that is what this is, and leave
 *  whatever protocol bytes resulted in `plain_in`. */
static void
promote_inbound(struct fd_ctx* c)
{
    if( c->mode == CONN_MODE_UNDECIDED )
    {
        if( ToriRSMapEd_BufAvailable(&c->raw_in) < 1 )
            return;
        c->mode = WsHandshake_LooksLikeHttp(ToriRSMapEd_BufPeek(&c->raw_in)[0])
                      ? CONN_MODE_WS_HANDSHAKE
                      : CONN_MODE_RAW;
    }

    if( c->mode == CONN_MODE_WS_HANDSHAKE )
    {
        struct WsHandshake handshake;
        enum WsHandshakeStatus status = WsHandshake_Consume(
            ToriRSMapEd_BufPeek(&c->raw_in),
            ToriRSMapEd_BufAvailable(&c->raw_in),
            &handshake);

        if( status == WS_HANDSHAKE_INCOMPLETE )
        {
            if( ToriRSMapEd_BufAvailable(&c->raw_in) > WS_HANDSHAKE_REQUEST_MAX )
            {
                TORIRS_REPORT("%s: oversized upgrade request — closing\n", c->who);
                c->dead = 1;
            }
            return;
        }
        if( status == WS_HANDSHAKE_ERROR )
        {
            TORIRS_REPORT("%s: bad WebSocket upgrade — closing\n", c->who);
            c->dead = 1;
            return;
        }

        /* The 101 goes out UNFRAMED, which is why it is queued here rather
         * than through fd_send — by the next line this connection is framed. */
        ToriRSMapEd_BufWrite(
            &c->out, (const uint8_t*)handshake.response, handshake.response_len);
        ToriRSMapEd_BufConsume(&c->raw_in, handshake.consumed);
        c->mode = CONN_MODE_WS;
        TORIRS_REPORT("%s: client upgraded to WebSocket\n", c->who);
    }

    if( c->mode == CONN_MODE_RAW )
        promote_raw(c);
    else if( c->mode == CONN_MODE_WS )
        promote_ws(c);
}

static int
fd_recv(
    void* ctx,
    uint8_t* dst,
    int max)
{
    struct fd_ctx* c = ctx;
    uint8_t chunk[16384];
    int taken;

    if( c->dead )
        return -1;

    taken = (int)recv(c->fd, (char*)chunk, (int)sizeof(chunk), 0);
    if( taken > 0 )
    {
        ToriRSMapEd_BufWrite(&c->raw_in, chunk, taken);
        promote_inbound(c);
    }
    else if( taken == 0 )
        c->dead = 1; /* orderly close */
    else if( !NET_RETRY(net_errno()) )
        c->dead = 1;

    /* Drain what the modes produced before reporting the peer gone: a client
     * may send its last request and hang up in one breath. */
    if( ToriRSMapEd_BufAvailable(&c->plain_in) > 0 )
        return ToriRSMapEd_BufRead(&c->plain_in, dst, max);
    return c->dead ? -1 : 0;
}

/**
 * Push whatever is queued for this connection, as far as the socket will
 * take it right now. Returns 0 once the peer is gone.
 *
 * Never waits: this is a multi-connection reactor, and a connection that
 * blocked here would stop the server serving everyone else — including the
 * broadcasts that made it block, since a document fact goes to every session
 * from inside one session's pump.
 */
static int
fd_flush(struct fd_ctx* c)
{
    while( ToriRSMapEd_BufAvailable(&c->out) > 0 )
    {
        int pending = ToriRSMapEd_BufAvailable(&c->out);
        int sent = (int)send(c->fd, (const char*)ToriRSMapEd_BufPeek(&c->out), pending, 0);

        if( sent > 0 )
        {
            ToriRSMapEd_BufConsume(&c->out, sent);
            continue;
        }
        if( sent < 0 && NET_RETRY(net_errno()) )
            return 1; /* the socket is full (or interrupted); the reactor will come back */
        c->dead = 1;
        return 0;
    }
    return 1;
}

static int
fd_send(
    void* ctx,
    const uint8_t* src,
    int len)
{
    struct fd_ctx* c = ctx;

    if( c->dead )
        return -1;

    /*
     * Queue, then push what fits. The transport contract allows buffering
     * internally, and buffering is the only answer that keeps a reactor
     * honest — the alternative is one slow reader freezing the world.
     *
     * A WebSocket peer gets each write as one unmasked binary frame. The
     * header goes in ahead of the payload rather than copying the payload
     * into a scratch buffer, because a square's text is hundreds of KB and
     * this path runs for every fact.
     */
    if( c->mode == CONN_MODE_WS )
    {
        uint8_t header[14];
        int header_len = ws_frame_encode_header(
            WS_OP_BINARY, len, NULL, header, (int)sizeof(header));

        if( header_len < 0
            || ToriRSMapEd_BufWrite(&c->out, header, header_len) != header_len )
            return -1;
    }
    if( ToriRSMapEd_BufWrite(&c->out, src, len) != len )
        return -1;
    if( !fd_flush(c) )
        return -1;

    /*
     * A client that never reads is a leak with a deadline. The cap is far
     * above any legitimate backlog — a square's text is ~200KB and the
     * square list ~24KB — so reaching it means the peer stopped consuming,
     * and dropping it is kinder than growing until the daemon dies.
     */
    if( ToriRSMapEd_BufAvailable(&c->out) > MAPED_SEND_QUEUE_MAX )
    {
        TORIRS_REPORT("%s: a client stopped reading (%d bytes queued) — dropping it\n",
            c->who,
            ToriRSMapEd_BufAvailable(&c->out));
        c->dead = 1;
        return -1;
    }
    return len;
}

struct conn
{
    int open;
    struct fd_ctx ctx;
    struct ToriRSMapEdSession session;
};

static void
conn_close(struct conn* conn)
{
    ToriRSMapEd_SessionFree(&conn->session);
    ToriRSMapEd_BufFree(&conn->ctx.out);
    ToriRSMapEd_BufFree(&conn->ctx.raw_in);
    ToriRSMapEd_BufFree(&conn->ctx.plain_in);
    close(conn->ctx.fd);
    conn->open = 0;
    TORIRS_REPORT("%s: client gone\n", conn->ctx.who);
}

struct ToriRSMapEdListener
{
    struct ToriRSMapEd* maped;
    int fd;
    int port;
    const char* who;
    struct conn conns[TORIRSMAPED_SESSION_MAX];
};

/** A slot in the SERVER's session table: shared with every other transport
 *  of the same server, so the listener's own array is not the whole answer. */
static int
listener_server_has_room(const struct ToriRSMapEd* maped)
{
    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
        if( !maped->sessions[i] )
            return 1;
    return 0;
}

struct ToriRSMapEdListener*
ToriRSMapEd_ListenerOpen(
    struct ToriRSMapEd* maped,
    int port,
    const char* who)
{
    struct ToriRSMapEdListener* listener;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int reuse = 1;
    int fd;

    assert(maped);
    assert(who);
    assert(port >= 0);
    assert(port <= 65535);

#ifdef _WIN32
    {
        /* Counted by winsock: whoever else in this process started it too. */
        WSADATA data;
        if( WSAStartup(MAKEWORD(2, 2), &data) != 0 )
        {
            TORIRS_REPORT("%s: WSAStartup failed\n", who);
            return NULL;
        }
    }
#endif
    fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if( fd < 0 )
    {
        TORIRS_REPORT("%s: socket: %s\n", who, net_why());
        return NULL;
    }
#ifdef _WIN32
    /* Windows' SO_REUSEADDR lets a second process take a port that is in use;
     * EXCLUSIVEADDRUSE is the POSIX meaning (a busy port refuses the bind). */
    setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&reuse, sizeof(reuse));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if( bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 )
    {
        TORIRS_REPORT("%s: bind 127.0.0.1:%d: %s\n", who, port, net_why());
        close(fd);
        return NULL;
    }
    if( listen(fd, 4) < 0 )
    {
        TORIRS_REPORT("%s: listen: %s\n", who, net_why());
        close(fd);
        return NULL;
    }
    if( getsockname(fd, (struct sockaddr*)&addr, &addr_len) < 0 )
    {
        TORIRS_REPORT("%s: getsockname: %s\n", who, net_why());
        close(fd);
        return NULL;
    }
    net_nonblocking(fd);

    listener = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*listener));
    assert(listener);
    listener->maped = maped;
    listener->fd = fd;
    listener->port = ntohs(addr.sin_port);
    listener->who = who;
    return listener;
}

int
ToriRSMapEd_ListenerPort(const struct ToriRSMapEdListener* listener)
{
    assert(listener);
    return listener->port;
}

int
ToriRSMapEd_ListenerConnections(const struct ToriRSMapEdListener* listener)
{
    int open = 0;

    assert(listener);
    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
        open += listener->conns[i].open;
    return open;
}

static void
listener_accept(struct ToriRSMapEdListener* listener)
{
    for( ;; )
    {
        int slot = -1;
        int fd = (int)accept(listener->fd, NULL, NULL);
        struct ToriRSMapEdTransport transport;
        struct conn* conn;

        if( fd < 0 )
        {
            if( !NET_RETRY(net_errno()) )
                TORIRS_REPORT("%s: accept: %s\n", listener->who, net_why());
            return;
        }
        for( int i = 0; i < TORIRSMAPED_SESSION_MAX && slot < 0; i++ )
            if( !listener->conns[i].open )
                slot = i;
        if( slot < 0 || !listener_server_has_room(listener->maped) )
        {
            /* Full is a hard close, not a queue: a client stuck in a
             * backlog looks connected and hears nothing, which is the
             * worst of both. */
            TORIRS_REPORT("%s: connection table full — refusing\n", listener->who);
            close(fd);
            continue;
        }
        conn = &listener->conns[slot];
        net_nonblocking(fd);
        memset(&conn->ctx, 0, sizeof(conn->ctx));
        conn->ctx.fd = fd;
        conn->ctx.who = listener->who;
        transport.ctx = &conn->ctx;
        transport.recv = fd_recv;
        transport.send = fd_send;
        ToriRSMapEd_SessionInit(&conn->session, listener->maped, &transport);
        conn->open = 1;
        TORIRS_REPORT("%s: client connected\n", listener->who);
    }
}

int
ToriRSMapEd_ListenerPoll(
    struct ToriRSMapEdListener* listener,
    int timeout_ms)
{
    fd_set readable;
    fd_set writable;
    struct timeval timeout;
    int top;

    assert(listener);
    assert(timeout_ms >= 0);

    top = listener->fd;
    FD_ZERO(&readable);
    FD_ZERO(&writable);
    FD_SET(listener->fd, &readable);
    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
    {
        struct conn const* conn = &listener->conns[i];
        if( !conn->open )
            continue;
        FD_SET(conn->ctx.fd, &readable);
        /* Only ask about writability while something is queued: an idle
         * connection would otherwise report writable every pass and spin
         * the loop at full speed. */
        if( ToriRSMapEd_BufAvailable(&conn->ctx.out) > 0 )
            FD_SET(conn->ctx.fd, &writable);
        if( conn->ctx.fd > top )
            top = conn->ctx.fd;
    }
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    if( select(top + 1, &readable, &writable, NULL, &timeout) < 0 )
    {
        if( NET_RETRY(net_errno()) )
            return ToriRSMapEd_ListenerConnections(listener);
        TORIRS_REPORT("%s: select: %s\n", listener->who, net_why());
        return -1;
    }

    if( FD_ISSET(listener->fd, &readable) )
        listener_accept(listener);

    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
    {
        struct conn* conn = &listener->conns[i];
        if( !conn->open )
            continue;

        /* Drain the backlog first: a pump can enqueue more, and a
         * connection whose queue never moves is the one to notice. */
        if( FD_ISSET(conn->ctx.fd, &writable) && !fd_flush(&conn->ctx) )
        {
            conn_close(conn);
            continue;
        }
        if( FD_ISSET(conn->ctx.fd, &readable) && !ToriRSMapEd_SessionPump(&conn->session) )
        {
            conn_close(conn);
            continue;
        }
        /* fd_send marks a peer dead when it stops reading; the session
         * itself may still look alive, so the poll is what retires it. */
        if( conn->ctx.dead )
            conn_close(conn);
    }
    return ToriRSMapEd_ListenerConnections(listener);
}

void
ToriRSMapEd_ListenerClose(struct ToriRSMapEdListener* listener)
{
    if( !listener )
        return;
    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
        if( listener->conns[i].open )
        {
            /* Whatever the server still had queued for it, as far as the
             * socket takes it now: a departing host owes no more. */
            (void)fd_flush(&listener->conns[i].ctx);
            conn_close(&listener->conns[i]);
        }
    close(listener->fd);
    ToriMem_Free(ToriMem_Editor(), listener);
}

#else /* __EMSCRIPTEN__ */

struct ToriRSMapEdListener
{
    int unused;
};

struct ToriRSMapEdListener*
ToriRSMapEd_ListenerOpen(
    struct ToriRSMapEd* maped,
    int port,
    const char* who)
{
    assert(maped);
    assert(who);
    (void)port;
    TORIRS_REPORT("%s: no loopback listener on this platform\n", who);
    return NULL;
}

int
ToriRSMapEd_ListenerPort(const struct ToriRSMapEdListener* listener)
{
    assert(listener);
    return 0;
}

int
ToriRSMapEd_ListenerConnections(const struct ToriRSMapEdListener* listener)
{
    assert(listener);
    return 0;
}

int
ToriRSMapEd_ListenerPoll(
    struct ToriRSMapEdListener* listener,
    int timeout_ms)
{
    assert(listener);
    (void)timeout_ms;
    return 0;
}

void
ToriRSMapEd_ListenerClose(struct ToriRSMapEdListener* listener)
{
    (void)listener;
}

#endif
