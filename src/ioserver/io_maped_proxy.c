/*
 * The map editor's daemon, reached from a page on io_server's own origin.
 *
 * BUILD NOTE: compiled INTO io_server_main.c (`#include "io_maped_proxy.c"`),
 * never linked on its own, so the io-server target's source list stays as it
 * is. Every file-scope name starts with `maped_proxy`.
 *
 * Why a proxy and not the WebSocket the daemon already speaks: the editor's
 * client half (editor/editor_host_remote.c) is request/reply -- open a
 * document, wait for its text -- and a browser build without ASYNCIFY or
 * threads cannot wait on a WebSocket: its bytes arrive only when the page
 * returns to the event loop, which a C function that is waiting never does.
 * A synchronous XMLHttpRequest is the one blocking call a page has, so the
 * web build's link to the daemon is a byte pipe over three routes:
 *
 *   POST /maped/open?port=N    connect to 127.0.0.1:N (only --maped-port's N);
 *                              answers the session id as text
 *   POST /maped/io?s=ID&wait=MS  the body is written to the daemon; the answer
 *                              is every byte the daemon has said since, after
 *                              waiting up to MS (at most 250) for the first;
 *                              410 once the daemon closed and nothing is left
 *   POST /maped/close?s=ID     closes it (a session idle 120 s is closed too)
 *   GET  /maped/ping?port=N    200 when the daemon accepts a connection
 *
 * Off unless io_server runs with `--maped-port N`: a server facing a network
 * (the NUC behind its gate) must not hand out the editor of its content tree.
 * Loopback only on the far side, and only the configured port.
 *
 * The waits run on io_server's worker thread, which serves one request at a
 * time: a wait holds up the next request by at most 250 ms. The page waiting
 * is blocked in its own XHR meanwhile, and the daemon answers in milliseconds.
 */

enum
{
    MAPED_PROXY_SESSIONS = 32,
    MAPED_PROXY_WAIT_MAX_MS = 250,
    MAPED_PROXY_IDLE_S = 120,
    MAPED_PROXY_READ_MAX = 4 * 1024 * 1024,
};

#if !defined(_WIN32)

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

struct maped_proxy_session
{
    unsigned id;
    int fd;
    time_t last_used;
};

static struct maped_proxy_session maped_proxy_sessions[MAPED_PROXY_SESSIONS];
static unsigned maped_proxy_next_id;

/** A blocking-free connect to 127.0.0.1:`port` within `timeout_ms`; the fd
 *  (left non-blocking) or -1. */
static int
maped_proxy_dial(
    int port,
    int timeout_ms)
{
    struct sockaddr_in address;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;

    if( fd < 0 )
        return -1;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#if defined(SO_NOSIGPIPE)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if( connect(fd, (struct sockaddr*)&address, sizeof(address)) != 0 )
    {
        fd_set writable;
        struct timeval wait;
        int error = 0;
        socklen_t length = sizeof(error);
        if( errno != EINPROGRESS )
        {
            close(fd);
            return -1;
        }
        FD_ZERO(&writable);
        FD_SET(fd, &writable);
        wait.tv_sec = timeout_ms / 1000;
        wait.tv_usec = (timeout_ms % 1000) * 1000;
        if( select(fd + 1, NULL, &writable, NULL, &wait) <= 0
            || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0 )
        {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static void
maped_proxy_close(struct maped_proxy_session* session)
{
    if( session->fd >= 0 )
        close(session->fd);
    session->fd = -1;
    session->id = 0;
}

static void
maped_proxy_sweep(void)
{
    time_t const now = time(NULL);
    for( int i = 0; i < MAPED_PROXY_SESSIONS; i++ )
    {
        if( maped_proxy_sessions[i].id && now - maped_proxy_sessions[i].last_used > MAPED_PROXY_IDLE_S )
        {
            fprintf(stderr, "io_server: maped session %u idle %d s, closed\n", maped_proxy_sessions[i].id,
                MAPED_PROXY_IDLE_S);
            maped_proxy_close(&maped_proxy_sessions[i]);
        }
    }
}

static struct maped_proxy_session*
maped_proxy_find(char const* path)
{
    char text[32];
    unsigned long id;

    if( !query_param(path, "s", text, (int)sizeof(text)) )
        return NULL;
    id = strtoul(text, NULL, 10);
    for( int i = 0; i < MAPED_PROXY_SESSIONS && id; i++ )
    {
        if( maped_proxy_sessions[i].id == id )
            return &maped_proxy_sessions[i];
    }
    return NULL;
}

static void
maped_proxy_text(
    struct HttpResponse* res,
    int status,
    char const* text)
{
    int const length = (int)strlen(text);
    char* body = ToriMem_Alloc(ToriMem_Net(), (size_t)length + 1);
    assert(body);
    memcpy(body, text, (size_t)length + 1);
    res->status = status;
    snprintf(res->content_type, sizeof(res->content_type), "text/plain; charset=utf-8");
    res->body = body;
    res->body_len = length;
    res->owns_body = 1;
}

/** The route's `port=`, which must be the configured one. */
static int
maped_proxy_port_ok(
    int configured,
    char const* path)
{
    char text[16];
    if( !query_param(path, "port", text, (int)sizeof(text)) )
        return 1; /* unstated: the configured one */
    return atoi(text) == configured;
}

/** Write `length` bytes, waiting out a full socket buffer. 0 on a dead peer. */
static int
maped_proxy_write_all(
    int fd,
    uint8_t const* data,
    int length)
{
    int sent_total = 0;
    while( sent_total < length )
    {
        ssize_t sent = send(fd, data + sent_total, (size_t)(length - sent_total), 0);
        if( sent > 0 )
        {
            sent_total += (int)sent;
            continue;
        }
        if( sent < 0 && errno == EINTR )
            continue;
        if( sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) )
        {
            fd_set writable;
            struct timeval wait = { 1, 0 };
            FD_ZERO(&writable);
            FD_SET(fd, &writable);
            if( select(fd + 1, NULL, &writable, NULL, &wait) <= 0 )
                return 0;
            continue;
        }
        return 0;
    }
    return 1;
}

static void
handle_maped_proxy(
    int configured_port,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char const* route = req->path + strlen("/maped/");

    if( configured_port <= 0 )
    {
        maped_proxy_text(res, 404,
            "this io_server does not reach a map-editor daemon (start it with --maped-port 43610)\n");
        return;
    }
    maped_proxy_sweep();

    if( strncmp(route, "ping", 4) == 0 && (route[4] == '\0' || route[4] == '?') )
    {
        int fd;
        char line[160];
        if( !maped_proxy_port_ok(configured_port, req->path) )
        {
            maped_proxy_text(res, 403, "not the map-editor daemon's port\n");
            return;
        }
        fd = maped_proxy_dial(configured_port, 1000);
        if( fd < 0 )
        {
            snprintf(line, sizeof(line), "no torirsmaped daemon answers on 127.0.0.1:%d\n", configured_port);
            maped_proxy_text(res, 502, line);
            return;
        }
        close(fd);
        snprintf(line, sizeof(line), "torirsmaped on 127.0.0.1:%d\n", configured_port);
        maped_proxy_text(res, 200, line);
        return;
    }
    if( strcmp(req->method, "POST") != 0 )
    {
        res->status = 405;
        return;
    }
    if( strncmp(route, "open", 4) == 0 && (route[4] == '\0' || route[4] == '?') )
    {
        struct maped_proxy_session* free_slot = NULL;
        char line[64];
        int fd;
        if( !maped_proxy_port_ok(configured_port, req->path) )
        {
            maped_proxy_text(res, 403, "not the map-editor daemon's port\n");
            return;
        }
        for( int i = 0; i < MAPED_PROXY_SESSIONS && !free_slot; i++ )
        {
            if( !maped_proxy_sessions[i].id )
                free_slot = &maped_proxy_sessions[i];
        }
        if( !free_slot )
        {
            maped_proxy_text(res, 503, "every map-editor session slot is in use\n");
            return;
        }
        fd = maped_proxy_dial(configured_port, 2000);
        if( fd < 0 )
        {
            snprintf(line, sizeof(line), "no daemon on 127.0.0.1:%d\n", configured_port);
            maped_proxy_text(res, 502, line);
            return;
        }
        /* Ids only grow, and start somewhere a restarted server will not
         * repeat at once: a page holding an old id gets 410, never a
         * stranger's connection. */
        if( !maped_proxy_next_id )
            maped_proxy_next_id = (unsigned)time(NULL) * 2654435761u % 1000000u + 1u;
        free_slot->id = ++maped_proxy_next_id;
        free_slot->fd = fd;
        free_slot->last_used = time(NULL);
        snprintf(line, sizeof(line), "%u", free_slot->id);
        maped_proxy_text(res, 200, line);
        return;
    }
    if( strncmp(route, "close", 5) == 0 && (route[5] == '\0' || route[5] == '?') )
    {
        struct maped_proxy_session* session = maped_proxy_find(req->path);
        if( session )
            maped_proxy_close(session);
        res->status = 204;
        return;
    }
    if( strncmp(route, "io", 2) == 0 && (route[2] == '\0' || route[2] == '?') )
    {
        struct maped_proxy_session* session = maped_proxy_find(req->path);
        char text[16];
        int wait_ms = 0;
        uint8_t* out;
        int out_length = 0;
        int out_capacity = 65536;
        int peer_closed = 0;

        if( !session )
        {
            maped_proxy_text(res, 410, "no such map-editor session (closed, or this io_server restarted)\n");
            return;
        }
        session->last_used = time(NULL);
        if( query_param(req->path, "wait", text, (int)sizeof(text)) )
            wait_ms = atoi(text);
        if( wait_ms < 0 )
            wait_ms = 0;
        if( wait_ms > MAPED_PROXY_WAIT_MAX_MS )
            wait_ms = MAPED_PROXY_WAIT_MAX_MS;
        if( req->body_len > 0 && !maped_proxy_write_all(session->fd, req->body, req->body_len) )
        {
            maped_proxy_close(session);
            maped_proxy_text(res, 410, "the map-editor daemon closed the connection\n");
            return;
        }
        out = ToriMem_Alloc(ToriMem_Net(), (size_t)out_capacity);
        assert(out);
        for( ;; )
        {
            fd_set readable;
            struct timeval wait;
            int const first = out_length == 0;
            ssize_t got;
            FD_ZERO(&readable);
            FD_SET(session->fd, &readable);
            /* Wait for the first byte only; then take what is there. */
            wait.tv_sec = 0;
            wait.tv_usec = first ? wait_ms * 1000 : 0;
            if( select(session->fd + 1, &readable, NULL, NULL, &wait) <= 0 )
                break;
            if( out_capacity - out_length < 16384 )
            {
                uint8_t* grown;
                if( out_capacity >= MAPED_PROXY_READ_MAX )
                    break;
                grown = ToriMem_Alloc(ToriMem_Net(), (size_t)out_capacity * 2);
                assert(grown);
                memcpy(grown, out, (size_t)out_length);
                ToriMem_Free(ToriMem_Net(), out);
                out = grown;
                out_capacity *= 2;
            }
            got = recv(session->fd, out + out_length, (size_t)(out_capacity - out_length), 0);
            if( got > 0 )
            {
                out_length += (int)got;
                continue;
            }
            if( got < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) )
                continue;
            peer_closed = 1;
            break;
        }
        if( peer_closed && out_length == 0 )
        {
            ToriMem_Free(ToriMem_Net(), out);
            maped_proxy_close(session);
            maped_proxy_text(res, 410, "the map-editor daemon closed the connection\n");
            return;
        }
        if( peer_closed )
            maped_proxy_close(session); /* the next io answers 410 */
        res->status = 200;
        snprintf(res->content_type, sizeof(res->content_type), "application/octet-stream");
        res->body = out;
        res->body_len = out_length;
        res->owns_body = 1;
        return;
    }
    res->status = 404;
}

#else /* _WIN32 */

static void
handle_maped_proxy(
    int configured_port,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    (void)configured_port;
    (void)req;
    res->status = 501;
}

#endif
