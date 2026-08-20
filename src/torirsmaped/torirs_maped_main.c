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

struct fd_ctx
{
    int fd;
};

static int
fd_recv(
    void* ctx,
    uint8_t* dst,
    int max)
{
    struct fd_ctx* c = ctx;
    ssize_t taken = recv(c->fd, dst, (size_t)max, 0);

    if( taken > 0 )
        return (int)taken;
    if( taken == 0 )
        return -1; /* orderly close */
    if( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR )
        return 0;
    return -1;
}

static int
fd_send(
    void* ctx,
    const uint8_t* src,
    int len)
{
    struct fd_ctx* c = ctx;
    int sent_total = 0;

    /* The fd is non-blocking for recv's sake, so a large fact (a square's
     * text, a whole square list) can short-write. The daemon has nothing
     * else to do with the time, so looping here is the whole of the
     * back-pressure story. */
    while( sent_total < len )
    {
        ssize_t sent = send(c->fd, src + sent_total, (size_t)(len - sent_total), 0);
        if( sent > 0 )
        {
            sent_total += (int)sent;
            continue;
        }
        if( sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) )
        {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(c->fd, &writable);
            if( select(c->fd + 1, NULL, &writable, NULL, NULL) < 0 && errno != EINTR )
                return -1;
            continue;
        }
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
        struct timeval timeout = { 0, 500 * 1000 };
        int top = listen_fd;

        FD_ZERO(&readable);
        FD_SET(listen_fd, &readable);
        for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
        {
            if( !conns[i].open )
                continue;
            FD_SET(conns[i].ctx.fd, &readable);
            if( conns[i].ctx.fd > top )
                top = conns[i].ctx.fd;
        }
        if( select(top + 1, &readable, NULL, NULL, &timeout) < 0 )
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
            if( !FD_ISSET(conns[i].ctx.fd, &readable) )
                continue;
            if( !ToriRSMapEd_SessionPump(&conns[i].session) )
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
