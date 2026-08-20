/*
 * ToriRSMapEd's front door: a loopback TCP daemon serving one content tree.
 *
 *   src/build/torirsmaped [port] [--content-dir <dir>] [--repo-root <dir>]
 *
 * Defaults: port 43610 (TORIRSMAPED_DEFAULT_PORT), content dir
 * $TORIRSMAPED_CONTENT_DIR or OSRS-Content/osrs239-content, repo root
 * $TORIRSMAPED_REPO_ROOT or "." (pass --repo-root '' to disable baking).
 *
 * Serves several connections at once — that is the point of the daemon. A
 * client is one stateful connection and a session may be spread across
 * many: a world-viewer process, a controller process with no world at all,
 * a late-joining second viewer. The server relays document facts and shared
 * state between them; the writer arbitration is the server's own (it holds
 * the tree lock and its document is the only one that matters), so
 * connections never contend with each other over files.
 *
 * The socket is bound BEFORE the tree is opened — ToriRSServer's rule, kept
 * even though this boot is fast today: a client launched beside the daemon
 * dials immediately, and a connection refused is terminal for it, while a
 * connection accepted late is just slow.
 */

#include "torirs_maped.h"

#include "platform/net_transport_ws_frame.h"
#include "platform/net_transport_ws_handshake.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

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
            fprintf(stderr, "torirsmaped: malformed WebSocket frame — closing\n");
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
                fprintf(stderr, "torirsmaped: oversized upgrade request — closing\n");
                c->dead = 1;
            }
            return;
        }
        if( status == WS_HANDSHAKE_ERROR )
        {
            fprintf(stderr, "torirsmaped: bad WebSocket upgrade — closing\n");
            c->dead = 1;
            return;
        }

        /* The 101 goes out UNFRAMED, which is why it is queued here rather
         * than through fd_send — by the next line this connection is framed. */
        ToriRSMapEd_BufWrite(
            &c->out, (const uint8_t*)handshake.response, handshake.response_len);
        ToriRSMapEd_BufConsume(&c->raw_in, handshake.consumed);
        c->mode = CONN_MODE_WS;
        fprintf(stderr, "torirsmaped: client upgraded to WebSocket\n");
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
    ssize_t taken;

    if( c->dead )
        return -1;

    taken = recv(c->fd, chunk, sizeof(chunk), 0);
    if( taken > 0 )
    {
        ToriRSMapEd_BufWrite(&c->raw_in, chunk, (int)taken);
        promote_inbound(c);
    }
    else if( taken == 0 )
        c->dead = 1; /* orderly close */
    else if( !(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) )
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
        ssize_t sent = send(c->fd, ToriRSMapEd_BufPeek(&c->out), (size_t)pending, 0);

        if( sent > 0 )
        {
            ToriRSMapEd_BufConsume(&c->out, (int)sent);
            continue;
        }
        if( sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) )
            return 1; /* the socket is full; the reactor will come back */
        if( sent < 0 && errno == EINTR )
            continue;
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
        fprintf(
            stderr,
            "torirsmaped: a client stopped reading (%d bytes queued) — dropping it\n",
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
    fprintf(stderr, "torirsmaped: client gone\n");
}

/** The reactor: accept up to TORIRSMAPED_SESSION_MAX connections and pump
 *  whichever have bytes. One thread — the sessions never block, so neither
 *  does this loop, and a broadcast provoked by one connection's intent is
 *  sent to the others from inside that connection's pump. */
static void
serve_forever(
    struct ToriRSMapEd* maped,
    int listen_fd)
{
    static struct conn conns[TORIRSMAPED_SESSION_MAX];

    for( ;; )
    {
        fd_set readable;
        fd_set writable;
        struct timeval timeout = { 0, 500 * 1000 };
        int top = listen_fd;

        FD_ZERO(&readable);
        FD_ZERO(&writable);
        FD_SET(listen_fd, &readable);
        for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
        {
            if( !conns[i].open )
                continue;
            FD_SET(conns[i].ctx.fd, &readable);
            /* Only ask about writability while something is queued: an idle
             * connection would otherwise report writable every pass and spin
             * the loop at full speed. */
            if( ToriRSMapEd_BufAvailable(&conns[i].ctx.out) > 0 )
                FD_SET(conns[i].ctx.fd, &writable);
            if( conns[i].ctx.fd > top )
                top = conns[i].ctx.fd;
        }
        if( select(top + 1, &readable, &writable, NULL, &timeout) < 0 )
        {
            if( errno == EINTR )
                continue;
            perror("torirsmaped: select");
            return;
        }

        if( FD_ISSET(listen_fd, &readable) )
        {
            int fd = accept(listen_fd, NULL, NULL);
            if( fd >= 0 )
            {
                int slot = -1;
                for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
                {
                    if( !conns[i].open )
                    {
                        slot = i;
                        break;
                    }
                }
                if( slot < 0 )
                {
                    /* Full is a hard close, not a queue: a client stuck in a
                     * backlog looks connected and hears nothing, which is the
                     * worst of both. */
                    fprintf(stderr, "torirsmaped: connection table full — refusing\n");
                    close(fd);
                }
                else
                {
                    struct ToriRSMapEdTransport transport;
                    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
                    memset(&conns[slot].ctx, 0, sizeof(conns[slot].ctx));
                    conns[slot].ctx.fd = fd;
                    transport.ctx = &conns[slot].ctx;
                    transport.recv = fd_recv;
                    transport.send = fd_send;
                    ToriRSMapEd_SessionInit(&conns[slot].session, maped, &transport);
                    conns[slot].open = 1;
                    fprintf(stderr, "torirsmaped: client connected\n");
                }
            }
            else if( errno != EINTR )
                perror("torirsmaped: accept");
        }

        for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
        {
            if( !conns[i].open )
                continue;

            /* Drain the backlog first: a pump can enqueue more, and a
             * connection whose queue never moves is the one to notice. */
            if( FD_ISSET(conns[i].ctx.fd, &writable) && !fd_flush(&conns[i].ctx) )
            {
                conn_close(&conns[i]);
                continue;
            }
            if( FD_ISSET(conns[i].ctx.fd, &readable)
                && !ToriRSMapEd_SessionPump(&conns[i].session) )
            {
                conn_close(&conns[i]);
                continue;
            }
            /* fd_send marks a peer dead when it stops reading; the session
             * itself may still look alive, so the reactor is what retires it. */
            if( conns[i].ctx.dead )
                conn_close(&conns[i]);
        }
    }
}

int
main(
    int argc,
    char** argv)
{
    const char* content_dir = getenv("TORIRSMAPED_CONTENT_DIR");
    const char* repo_root = getenv("TORIRSMAPED_REPO_ROOT");
    int port = TORIRSMAPED_DEFAULT_PORT;
    int listen_fd;
    struct sockaddr_in addr;
    int reuse = 1;
    static struct ToriRSMapEd maped;

    if( !content_dir )
        content_dir = "OSRS-Content/osrs239-content";
    if( !repo_root )
        repo_root = ".";

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--content-dir") == 0 && i + 1 < argc )
            content_dir = argv[++i];
        else if( strcmp(argv[i], "--repo-root") == 0 && i + 1 < argc )
            repo_root = argv[++i];
        else if( argv[i][0] >= '0' && argv[i][0] <= '9' )
            port = atoi(argv[i]);
        else
        {
            fprintf(
                stderr,
                "usage: torirsmaped [port] [--content-dir <dir>] [--repo-root <dir>]\n");
            return 1;
        }
    }
    if( repo_root[0] == '\0' )
        repo_root = NULL;

    signal(SIGPIPE, SIG_IGN);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if( listen_fd < 0 )
    {
        perror("torirsmaped: socket");
        return 1;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if( bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 )
    {
        perror("torirsmaped: bind");
        return 1;
    }
    if( listen(listen_fd, 1) < 0 )
    {
        perror("torirsmaped: listen");
        return 1;
    }

    ToriRSMapEd_Open(&maped, content_dir, repo_root);

    fprintf(
        stderr,
        "torirsmaped: serving %s on 127.0.0.1:%d%s\n",
        content_dir,
        port,
        repo_root ? "" : " (baking disabled)");

    serve_forever(&maped, listen_fd);

    ToriRSMapEd_Close(&maped);
    close(listen_fd);
    return 0;
}
