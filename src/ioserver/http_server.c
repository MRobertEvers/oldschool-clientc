#include "http_server.h"
#include "mem/torirs_mem.h"
#include <assert.h>

/* mingw-w64 (see src/platform/sockstream.c for the same split on the client
 * side) has no POSIX socket headers at all -- Winsock2 is a different API,
 * not a POSIX-compatible one, so this whole file branches on _WIN32 rather
 * than trying to shim the POSIX names onto it. */
#ifdef _WIN32
/* The default 64-slot fd_set silently drops sockets past its capacity
 * instead of erroring; HTTP_MAX_CLIENTS plus the listener is already 65. */
#define FD_SETSIZE 128
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define close closesocket
#else
#include <netdb.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HTTP_MAX_CLIENTS 64

/*
 * A work loop, and no blocking IO in it.
 *
 * The thread that calls HttpServer_RunOn owns every socket, each one
 * non-blocking, and waits only in select(): the listener, the clients, the
 * outbound exchanges a handler asks for (HttpServer_Exchange), and a wake
 * channel. A complete request is a JOB: its bytes are copied and handed to the
 * worker thread, which runs the handler -- the handlers read files, the cache
 * and their databases, and that waiting belongs off the loop -- and hands the
 * response back through the wake channel. The loop sends it as the client's
 * socket takes it.
 *
 * One worker, so a handler never runs beside another: the handlers' state
 * (the gate's sessions, the site's buffers, single-threaded sqlite) needs no
 * locks. A handler that needs the network does not open a socket: it asks the
 * loop to make the exchange and waits on the worker for the answer.
 *
 * Before this the handler ran on the loop and each response was written to the
 * end before the next request was looked at: a phone that went to the
 * background mid-download through the proxy (75 KB stuck in the send queue)
 * hung torirs.mrobertevers.com until io_server was restarted (2026-10-27).
 */

/* How long a client may take nothing of its response before it is dropped. */
#define HTTP_SEND_STALL_MS 30000

/* Outbound exchanges in flight at once. The worker makes one at a time, so
 * more than one is never needed; the rest is room for a second worker. */
#define HTTP_MAX_EXCHANGES 4
/* A request body is an IO batch of a few dozen records; anything past this is
 * a client bug or an attack, and refusing beats growing without bound. */
#define HTTP_MAX_BODY (16 * 1024 * 1024)
#define HTTP_MAX_HEADERS (64 * 1024)

#ifdef _WIN32
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#else
typedef int sock_t;
#define SOCK_INVALID (-1)
#endif

/* perror() reads errno, which Winsock calls never set -- WSAGetLastError()
 * is the only place a Windows socket failure's real code shows up. */
static void
sock_perror(char const* msg)
{
#ifdef _WIN32
    fprintf(stderr, "%s: WSA error %d\n", msg, WSAGetLastError());
#else
    perror(msg);
#endif
}

#ifdef _WIN32
/* mingw-w64 ships no memmem() (it's a GNU/BSD extension, not POSIX, and not
 * part of the Windows CRT) -- the request parser below needs it to find
 * "\r\n\r\n" / "\r\n" inside a buffer that is not NUL-terminated. */
static void*
memmem(
    void const* haystack,
    size_t haystack_len,
    void const* needle,
    size_t needle_len)
{
    uint8_t const* h = (uint8_t const*)haystack;
    uint8_t const* n = (uint8_t const*)needle;
    size_t i;

    if( needle_len == 0 || haystack_len < needle_len )
        return NULL;
    for( i = 0; i + needle_len <= haystack_len; i++ )
        if( memcmp(h + i, n, needle_len) == 0 )
            return (void*)(h + i);
    return NULL;
}
#endif

struct HttpClient
{
    sock_t fd;
    uint8_t* buf;
    int len;
    int cap;
    /* The response not yet taken by the socket: out[out_off..out_len). */
    uint8_t* out;
    int out_len;
    int out_off;
    int out_cap;
    /* When the socket last took a byte of it. */
    long long out_progress_ms;
    /* Bumped whenever the slot is reset, so a job that finishes after its
     * client left is not answered to whoever took the slot. */
    unsigned generation;
    /* A request of this client is with the worker. */
    int busy;
};

static volatile sig_atomic_t g_stop = 0;

void
HttpServer_RequestStop(void)
{
    g_stop = 1;
}

char const*
HttpServer_ContentTypeForPath(char const* path)
{
    char const* dot = strrchr(path, '.');
    if( !dot )
        return "application/octet-stream";
    if( strcmp(dot, ".html") == 0 )
        return "text/html; charset=utf-8";
    if( strcmp(dot, ".js") == 0 || strcmp(dot, ".mjs") == 0 )
        return "text/javascript; charset=utf-8";
    /* Must be exact: the browser refuses to stream-compile a module served as
     * anything else, and silently falls back to a slower path at best. */
    if( strcmp(dot, ".wasm") == 0 )
        return "application/wasm";
    if( strcmp(dot, ".css") == 0 )
        return "text/css; charset=utf-8";
    if( strcmp(dot, ".json") == 0 || strcmp(dot, ".map") == 0 )
        return "application/json";
    if( strcmp(dot, ".png") == 0 )
        return "image/png";
    if( strcmp(dot, ".ico") == 0 )
        return "image/x-icon";
    if( strcmp(dot, ".woff") == 0 )
        return "font/woff";
    if( strcmp(dot, ".ttf") == 0 )
        return "font/ttf";
    if( strcmp(dot, ".eot") == 0 )
        return "application/vnd.ms-fontobject";
    return "application/octet-stream";
}

static void
client_reset(struct HttpClient* client)
{
    if( client->fd != SOCK_INVALID )
        close(client->fd);
    ToriMem_Free(ToriMem_Net(), client->buf);
    ToriMem_Free(ToriMem_Net(), client->out);
    client->fd = SOCK_INVALID;
    client->buf = NULL;
    client->len = 0;
    client->cap = 0;
    client->out = NULL;
    client->out_len = 0;
    client->out_off = 0;
    client->out_cap = 0;
    client->out_progress_ms = 0;
    client->generation++;
    client->busy = 0;
}

static int
client_has_out(struct HttpClient const* client)
{
    return client->out_off < client->out_len;
}

static int
client_append(
    struct HttpClient* client,
    uint8_t const* data,
    int count)
{
    if( client->len + count > client->cap )
    {
        int cap = client->cap ? client->cap : 8192;
        uint8_t* grown;
        while( cap < client->len + count )
            cap *= 2;
        if( cap > HTTP_MAX_BODY + HTTP_MAX_HEADERS )
            return -1;
        grown = ToriMem_Realloc(ToriMem_Net(), client->buf, (size_t)cap);
        client->buf = grown;
        client->cap = cap;
    }
    memcpy(client->buf + client->len, data, (size_t)count);
    client->len += count;
    return 0;
}

/* Milliseconds on a clock that only goes forward. */
static long long
now_ms(void)
{
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

/* Add to the response waiting for the client's socket. */
static void
client_queue_out(
    struct HttpClient* client,
    void const* data,
    int count)
{
    if( client->out_off > 0 && client->out_off == client->out_len )
        client->out_off = client->out_len = 0;
    if( client->out_len + count > client->out_cap )
    {
        int cap = client->out_cap ? client->out_cap : 16384;
        while( cap < client->out_len + count )
            cap *= 2;
        client->out = ToriMem_Realloc(ToriMem_Net(), client->out, (size_t)cap);
        assert(client->out);
        client->out_cap = cap;
    }
    memcpy(client->out + client->out_len, data, (size_t)count);
    client->out_len += count;
}

/* Send what the socket takes now. 0 when the rest can wait for the socket,
 * -1 when the connection is gone. */
static int
client_flush(struct HttpClient* client)
{
    while( client_has_out(client) )
    {
        int n = send(client->fd, (char const*)(client->out + client->out_off),
                     client->out_len - client->out_off, 0);
        if( n > 0 )
        {
            client->out_off += n;
            client->out_progress_ms = now_ms();
            continue;
        }
#ifdef _WIN32
        if( n < 0 && WSAGetLastError() == WSAEWOULDBLOCK )
            return 0;
        if( n < 0 && WSAGetLastError() == WSAEINTR )
            continue;
#else
        if( n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) )
            return 0;
        if( n < 0 && errno == EINTR )
            continue;
#endif
        return -1;
    }
    client->out_off = client->out_len = 0;
    return 0;
}

static char const*
status_text(int status)
{
    switch( status )
    {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 302:
        return "Found";
    case 303:
        return "See Other";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    case 429:
        return "Too Many Requests";
    case 500:
        return "Internal Server Error";
    case 503:
        return "Service Unavailable";
    default:
        break;
    }
    /*
     * Never "OK", which is what this used to answer for everything it did not
     * know: a 503 went out on the wire spelled "503 OK". Clients read the
     * number, so nothing broke — but the reason phrase is what a person reads
     * in a log or a devtools panel, and one that contradicts the code is worse
     * than a vague one.
     */
    if( status >= 500 )
        return "Server Error";
    if( status >= 400 )
        return "Client Error";
    if( status >= 300 )
        return "Redirect";
    return "OK";
}

static int
send_response(
    struct HttpClient* client,
    struct HttpResponse const* res)
{
    char header[2048];
    /* A 304 carries no body by definition (RFC 7232 4.1), and the length is
     * spelled 0 rather than omitted so keep-alive framing stays unambiguous. */
    int body_len = res->status == 304 ? 0 : res->body_len;
    int header_len = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        /* The page may be opened from a file:// URL or another dev server
         * while this one only answers IO; without these the browser drops the
         * response before the harness ever sees it. Expose-Headers is what
         * lets the harness read ETag back off a cross-origin response — it is
         * hidden from script otherwise, and a validator nobody can read is the
         * same as no validator. */
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Expose-Headers: ETag, Content-Length\r\n"
        "%s%s%s"
        "%s%s%s"
        "%s%s%s"
        "%s%s%s"
        "Cache-Control: %s\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        res->status,
        status_text(res->status),
        res->content_type[0] ? res->content_type : "application/octet-stream",
        body_len,
        res->etag[0] ? "ETag: " : "",
        res->etag[0] ? res->etag : "",
        res->etag[0] ? "\r\n" : "",
        res->location[0] ? "Location: " : "",
        res->location[0] ? res->location : "",
        res->location[0] ? "\r\n" : "",
        res->set_cookie[0] ? "Set-Cookie: " : "",
        res->set_cookie[0] ? res->set_cookie : "",
        res->set_cookie[0] ? "\r\n" : "",
        res->www_authenticate[0] ? "WWW-Authenticate: " : "",
        res->www_authenticate[0] ? res->www_authenticate : "",
        res->www_authenticate[0] ? "\r\n" : "",
        /* no-store on anything without a validator: an IO batch is a POST
         * result and a stats page is a snapshot, neither of which may be
         * reused. A validated representation is the opposite case — it must be
         * stored to be revalidated. */
        res->etag[0] ? "no-cache" : "no-store");

    if( header_len < 0 || header_len >= (int)sizeof(header) )
        return -1;
    /* Copied: the body is the handler's and is freed when this returns. The
     * stall clock starts with the response, not with an older one. */
    if( !client_has_out(client) )
        client->out_progress_ms = now_ms();
    client_queue_out(client, header, header_len);
    if( body_len > 0 )
        client_queue_out(client, res->body, body_len);
    return client_flush(client);
}

char const*
HttpRequest_Header(
    struct HttpRequest const* req,
    char const* name,
    int* len)
{
    size_t name_len;
    int i;

    if( len )
        *len = 0;
    assert(req);
    if( !req->headers )
        return NULL;
    assert(name);
    name_len = strlen(name);

    for( i = 0; i + (int)name_len + 1 < req->headers_len; i++ )
    {
        int line_start = i == 0 || (i >= 2 && req->headers[i - 1] == '\n');
        int value;
        int end;

        if( !line_start )
            continue;
        if( strncasecmp(req->headers + i, name, name_len) != 0 )
            continue;
        if( req->headers[i + name_len] != ':' )
            continue;

        value = i + (int)name_len + 1;
        while( value < req->headers_len &&
               (req->headers[value] == ' ' || req->headers[value] == '\t') )
            value++;
        end = value;
        while( end < req->headers_len && req->headers[end] != '\r' &&
               req->headers[end] != '\n' )
            end++;
        while( end > value && (req->headers[end - 1] == ' ' || req->headers[end - 1] == '\t') )
            end--;
        if( len )
            *len = end - value;
        return req->headers + value;
    }
    return NULL;
}

int
HttpRequest_MatchesETag(
    struct HttpRequest const* req,
    char const* etag)
{
    int len = 0;
    char const* value = HttpRequest_Header(req, "If-None-Match", &len);
    size_t etag_len;

    assert(etag);
    if( !value || !etag[0] )
        return 0;
    etag_len = strlen(etag);
    /* "*" matches any current representation. */
    if( len == 1 && value[0] == '*' )
        return 1;
    /* A comma-separated list; a exact match on any member is enough. Weak
     * comparison is the right one for a plain GET, but nothing here emits a
     * W/ prefix, so an exact compare is equivalent and simpler to be sure of. */
    for( int i = 0; i < len; i++ )
    {
        if( i + (int)etag_len <= len && strncmp(value + i, etag, etag_len) == 0 )
            return 1;
    }
    return 0;
}

/* Case-insensitive header lookup within [head, head+head_len). */
static long
header_value_long(
    char const* head,
    int head_len,
    char const* name)
{
    size_t name_len = strlen(name);
    for( int i = 0; i + (int)name_len < head_len; i++ )
    {
        if( (i == 0 || head[i - 1] == '\n') && strncasecmp(head + i, name, name_len) == 0 &&
            head[i + name_len] == ':' )
        {
            char const* cursor = head + i + name_len + 1;
            while( *cursor == ' ' )
                cursor++;
            return strtol(cursor, NULL, 10);
        }
    }
    return -1;
}

/* ---- Threads ----------------------------------------------------------- */

#ifdef _WIN32
typedef CRITICAL_SECTION http_mutex;
typedef CONDITION_VARIABLE http_cond;

static void
mutex_init(http_mutex* m)
{
    InitializeCriticalSection(m);
}
static void
mutex_lock(http_mutex* m)
{
    EnterCriticalSection(m);
}
static void
mutex_unlock(http_mutex* m)
{
    LeaveCriticalSection(m);
}
static void
cond_init(http_cond* c)
{
    InitializeConditionVariable(c);
}
static void
cond_wait(
    http_cond* c,
    http_mutex* m)
{
    SleepConditionVariableCS(c, m, INFINITE);
}
static void
cond_broadcast(http_cond* c)
{
    WakeAllConditionVariable(c);
}
#else
typedef pthread_mutex_t http_mutex;
typedef pthread_cond_t http_cond;

static void
mutex_init(http_mutex* m)
{
    pthread_mutex_init(m, NULL);
}
static void
mutex_lock(http_mutex* m)
{
    pthread_mutex_lock(m);
}
static void
mutex_unlock(http_mutex* m)
{
    pthread_mutex_unlock(m);
}
static void
cond_init(http_cond* c)
{
    pthread_cond_init(c, NULL);
}
static void
cond_wait(
    http_cond* c,
    http_mutex* m)
{
    pthread_cond_wait(c, m);
}
static void
cond_broadcast(http_cond* c)
{
    pthread_cond_broadcast(c);
}
#endif

static int
socket_set_nonblocking(sock_t fd)
{
#ifdef _WIN32
    u_long nonblocking = 1;
    return ioctlsocket(fd, FIONBIO, &nonblocking) == 0 ? 0 : -1;
#else
    int const flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

static int
socket_would_block(void)
{
#ifdef _WIN32
    int const e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

/*
 * The wake channel: a connected pair of sockets, so the worker's "done" is
 * one more readable descriptor in the loop's select(). A socket pair rather
 * than a pipe because Winsock's select() takes nothing but sockets.
 */
static int
wake_pair(sock_t out[2])
{
#ifdef _WIN32
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);
    sock_t listener = socket(AF_INET, SOCK_STREAM, 0);

    out[0] = out[1] = SOCK_INVALID;
    if( listener == SOCK_INVALID )
        return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if( bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(listener, 1) != 0 ||
        getsockname(listener, (struct sockaddr*)&addr, &addr_len) != 0 )
    {
        close(listener);
        return -1;
    }
    out[1] = socket(AF_INET, SOCK_STREAM, 0);
    if( out[1] == SOCK_INVALID || connect(out[1], (struct sockaddr*)&addr, sizeof(addr)) != 0 )
    {
        close(listener);
        return -1;
    }
    out[0] = accept(listener, NULL, NULL);
    close(listener);
    if( out[0] == SOCK_INVALID )
        return -1;
#else
    int fds[2];
    if( socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0 )
        return -1;
    out[0] = fds[0];
    out[1] = fds[1];
#endif
    socket_set_nonblocking(out[0]);
    socket_set_nonblocking(out[1]);
    return 0;
}

/* ---- Jobs and exchanges --------------------------------------------------- */

struct HttpJob
{
    int client;
    unsigned generation;
    /* The request's bytes, which `req` points into. */
    uint8_t* raw;
    struct HttpRequest req;
    struct HttpResponse res;
    struct HttpJob* next;
};

enum HttpExchangeState
{
    EXCHANGE_FREE = 0,
    EXCHANGE_ASKED,      /* the worker posted it; the loop has not started it */
    EXCHANGE_CONNECTING,
    EXCHANGE_SENDING,
    EXCHANGE_RECEIVING,
};

struct HttpExchange
{
    enum HttpExchangeState state;
    struct sockaddr_storage addr;
    int addr_len;
    sock_t fd;
    char* request;
    int request_len;
    int request_off;
    char* response;
    int response_len;
    int response_cap;
    long long deadline_ms;
    /* Set by the loop, under the lock: the answer is in. */
    int done;
    int status;
};

static struct
{
    http_mutex lock;
    http_cond changed;
    struct HttpJob* todo_head;
    struct HttpJob* todo_tail;
    struct HttpJob* done_head;
    struct HttpJob* done_tail;
    struct HttpExchange exchanges[HTTP_MAX_EXCHANGES];
    sock_t wake[2];
    HttpHandler handler;
    void* user;
    int stopping;
    int running;
} g_http;

/* Wake the loop: one byte; a full channel is already a wake. */
static void
wake_loop(void)
{
    char const byte = 1;
    send(g_http.wake[1], &byte, 1, 0);
}

static void
wake_drain(void)
{
    char sink[256];
    while( recv(g_http.wake[0], sink, sizeof(sink), 0) > 0 )
    {
    }
}

static void
job_free(struct HttpJob* job)
{
    if( job->res.owns_body )
        ToriMem_Free(ToriMem_Net(), job->res.body); /* every owned body is net's (http_server.h) */
    ToriMem_Free(ToriMem_Net(), job->raw);
    ToriMem_Free(ToriMem_Net(), job);
}

/* The worker: every handler runs here, one at a time. */
#ifdef _WIN32
static DWORD WINAPI
worker_main(LPVOID unused)
#else
static void*
worker_main(void* unused)
#endif
{
    (void)unused;
    for( ;; )
    {
        struct HttpJob* job;

        mutex_lock(&g_http.lock);
        while( !g_http.todo_head && !g_http.stopping )
            cond_wait(&g_http.changed, &g_http.lock);
        if( !g_http.todo_head )
        {
            mutex_unlock(&g_http.lock);
            break;
        }
        job = g_http.todo_head;
        g_http.todo_head = job->next;
        if( !g_http.todo_head )
            g_http.todo_tail = NULL;
        mutex_unlock(&g_http.lock);

        job->next = NULL;
        memset(&job->res, 0, sizeof(job->res));
        job->res.status = 404;
        snprintf(job->res.content_type, sizeof(job->res.content_type), "text/plain; charset=utf-8");
        g_http.handler(g_http.user, &job->req, &job->res);
        if( job->res.status == 404 && !job->res.body )
        {
            job->res.body = (void*)"not found\n";
            job->res.body_len = 10;
            job->res.owns_body = 0;
        }

        mutex_lock(&g_http.lock);
        if( g_http.done_tail )
            g_http.done_tail->next = job;
        else
            g_http.done_head = job;
        g_http.done_tail = job;
        mutex_unlock(&g_http.lock);
        wake_loop();
    }
    return 0;
}

/*
 * Take one complete request off the front of the client's buffer and hand it
 * to the worker. 1 when one was dispatched (and consumed), 0 when more bytes
 * are needed, -1 when the connection should be dropped.
 */
static int
client_dispatch(
    struct HttpClient* client,
    int slot)
{
    char const* buf = (char const*)client->buf;
    char const* head_end;
    int head_len;
    long content_length;
    int total;
    struct HttpJob* job;
    struct HttpRequest* req;

    if( client->len < 4 )
        return 0;
    head_end = memmem(buf, (size_t)client->len, "\r\n\r\n", 4);
    if( !head_end )
        return client->len > HTTP_MAX_HEADERS ? -1 : 0;
    head_len = (int)(head_end - buf);

    content_length = header_value_long(buf, head_len, "Content-Length");
    if( content_length < 0 )
        content_length = 0;
    if( content_length > HTTP_MAX_BODY )
        return -1;

    total = head_len + 4 + (int)content_length;
    if( client->len < total )
        return 0;

    job = ToriMem_Alloc(ToriMem_Net(), sizeof(*job));
    assert(job);
    memset(job, 0, sizeof(*job));
    /* Copied: the client's buffer moves on to the next request while the
     * worker reads this one. */
    job->raw = ToriMem_Alloc(ToriMem_Net(), (size_t)total + 1);
    assert(job->raw);
    memcpy(job->raw, client->buf, (size_t)total);
    job->raw[total] = 0;
    buf = (char const*)job->raw;
    req = &job->req;
    {
        /* Request line: METHOD SP PATH SP VERSION */
        int i = 0;
        int m = 0;
        while( i < head_len && buf[i] != ' ' && m < (int)sizeof(req->method) - 1 )
            req->method[m++] = buf[i++];
        req->method[m] = '\0';
        while( i < head_len && buf[i] == ' ' )
            i++;
        {
            int p = 0;
            while( i < head_len && buf[i] != ' ' && buf[i] != '\r' &&
                   p < (int)sizeof(req->path) - 1 )
                req->path[p++] = buf[i++];
            req->path[p] = '\0';
        }
        if( req->method[0] == '\0' || req->path[0] == '\0' )
        {
            job_free(job);
            return -1;
        }
    }
    req->body = job->raw + head_len + 4;
    req->body_len = (int)content_length;
    /* The header block, past the request line: what a conditional request's
     * If-None-Match arrives in. */
    {
        char const* line_end = memmem(buf, (size_t)head_len, "\r\n", 2);
        if( line_end )
        {
            req->headers = line_end + 2;
            req->headers_len = head_len - (int)(line_end + 2 - buf);
        }
    }

    /* Consume the request, keeping any pipelined bytes that followed it. */
    memmove(client->buf, client->buf + total, (size_t)(client->len - total));
    client->len -= total;

    job->client = slot;
    job->generation = client->generation;
    client->busy = 1;
    mutex_lock(&g_http.lock);
    if( g_http.todo_tail )
        g_http.todo_tail->next = job;
    else
        g_http.todo_head = job;
    g_http.todo_tail = job;
    cond_broadcast(&g_http.changed);
    mutex_unlock(&g_http.lock);
    return 1;
}

/* ---- Outbound exchanges, made by the loop --------------------------------- */

static void
exchange_finish(
    struct HttpExchange* x,
    int status)
{
    if( x->fd != SOCK_INVALID )
        close(x->fd);
    x->fd = SOCK_INVALID;
    mutex_lock(&g_http.lock);
    x->status = status;
    x->done = 1;
    cond_broadcast(&g_http.changed);
    mutex_unlock(&g_http.lock);
}

/* The status of a complete response in `x`, or 0 while it is not complete.
 * Complete is the declared length, or everything up to the peer's close
 * (`closed`). */
static int
exchange_complete(
    struct HttpExchange const* x,
    int closed)
{
    char const* head_end;
    int head_len;
    long length;

    if( x->response_len < 12 || memcmp(x->response, "HTTP/1.", 7) != 0 )
        return closed ? -1 : 0;
    head_end = memmem(x->response, (size_t)x->response_len, "\r\n\r\n", 4);
    if( !head_end )
        return closed ? -1 : 0;
    head_len = (int)(head_end - x->response) + 4;
    length = header_value_long(x->response, head_len - 4, "Content-Length");
    if( length >= 0 && x->response_len < head_len + length )
        return closed ? -1 : 0;
    if( length < 0 && !closed )
        return 0;
    return (x->response[9] - '0') * 100 + (x->response[10] - '0') * 10 + (x->response[11] - '0');
}

/* Advance one exchange as far as its socket allows. */
static void
exchange_step(
    struct HttpExchange* x,
    int readable,
    int writable)
{
    if( x->state == EXCHANGE_ASKED )
    {
        x->fd = socket(x->addr.ss_family, SOCK_STREAM, 0);
        if( x->fd == SOCK_INVALID || socket_set_nonblocking(x->fd) != 0 )
        {
            exchange_finish(x, 0);
            return;
        }
        if( connect(x->fd, (struct sockaddr*)&x->addr, x->addr_len) != 0 && !socket_would_block() )
        {
            exchange_finish(x, 0);
            return;
        }
        x->state = EXCHANGE_CONNECTING;
        return;
    }
    if( x->state == EXCHANGE_CONNECTING && writable )
    {
        int error = 0;
        socklen_t error_len = sizeof(error);

        if( getsockopt(x->fd, SOL_SOCKET, SO_ERROR, (char*)&error, &error_len) != 0 || error )
        {
            exchange_finish(x, 0);
            return;
        }
        x->state = EXCHANGE_SENDING;
    }
    if( x->state == EXCHANGE_SENDING && (writable || x->request_off == 0) )
    {
        while( x->request_off < x->request_len )
        {
            int const n = send(x->fd, x->request + x->request_off, x->request_len - x->request_off, 0);
            if( n > 0 )
            {
                x->request_off += n;
                continue;
            }
            if( n < 0 && socket_would_block() )
                return;
            exchange_finish(x, 0);
            return;
        }
        x->state = EXCHANGE_RECEIVING;
        return;
    }
    if( x->state == EXCHANGE_RECEIVING && readable )
    {
        for( ;; )
        {
            int n;
            int status;

            if( x->response_cap - x->response_len < 4096 )
            {
                x->response_cap = x->response_cap ? x->response_cap * 2 : 16384;
                x->response = ToriMem_Realloc(ToriMem_Net(), x->response, (size_t)x->response_cap);
                assert(x->response);
            }
            n = recv(x->fd, x->response + x->response_len, x->response_cap - x->response_len - 1, 0);
            if( n > 0 )
            {
                x->response_len += n;
                status = exchange_complete(x, 0);
                if( status > 0 )
                {
                    exchange_finish(x, status);
                    return;
                }
                continue;
            }
            if( n < 0 && socket_would_block() )
                return;
            /* Closed, or failed: what arrived is the answer if it is whole. */
            status = exchange_complete(x, 1);
            exchange_finish(x, status > 0 ? status : 0);
            return;
        }
    }
}

char*
HttpServer_Exchange(
    char const* host,
    int port,
    char const* method,
    char const* route,
    char const* content_type,
    void const* body,
    int body_len,
    int timeout_ms,
    int* out_size,
    int* out_status)
{
    struct addrinfo hints;
    struct addrinfo* found = NULL;
    char port_text[16];
    struct HttpExchange* x = NULL;
    char* answer = NULL;
    int header_len;

    assert(host);
    assert(method);
    assert(route);
    assert(out_size);
    assert(out_status);
    assert(body_len >= 0);
    /* The worker's call: the loop makes the exchange, so the loop must run. */
    assert(g_http.running);
    *out_size = 0;
    *out_status = 0;

    /* Resolved here, on the worker: a name lookup is the one wait a loop
     * cannot select() on. A numeric address answers at once. */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%d", port);
    if( getaddrinfo(host, port_text, &hints, &found) != 0 || !found )
        return NULL;

    {
        struct HttpExchange built;

        memset(&built, 0, sizeof(built));
        built.fd = SOCK_INVALID;
        memcpy(&built.addr, found->ai_addr, (size_t)found->ai_addrlen);
        built.addr_len = (int)found->ai_addrlen;
        freeaddrinfo(found);
        built.request = ToriMem_Alloc(ToriMem_Net(), 1024 + (size_t)body_len);
        assert(built.request);
        header_len = snprintf(built.request, 1024,
                              "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n"
                              "%s%s%sContent-Length: %d\r\n\r\n",
                              method, route, host, port, content_type ? "Content-Type: " : "",
                              content_type ? content_type : "", content_type ? "\r\n" : "",
                              body_len);
        assert(header_len > 0 && header_len < 1024);
        if( body_len > 0 )
            memcpy(built.request + header_len, body, (size_t)body_len);
        built.request_len = header_len + body_len;
        built.deadline_ms = now_ms() + timeout_ms;
        built.state = EXCHANGE_ASKED;

        /* Published whole, under the lock: the loop never sees half of one. */
        mutex_lock(&g_http.lock);
        /* Shutting down: the loop that would make it is going, and a wait for
         * it would hold the worker the loop is about to join. */
        if( !g_http.stopping )
        {
            for( int i = 0; i < HTTP_MAX_EXCHANGES && !x; i++ )
                if( g_http.exchanges[i].state == EXCHANGE_FREE )
                    x = &g_http.exchanges[i];
            /* One worker makes one exchange at a time. */
            assert(x);
            *x = built;
        }
        mutex_unlock(&g_http.lock);
        if( !x )
        {
            ToriMem_Free(ToriMem_Net(), built.request);
            return NULL;
        }
    }
    wake_loop();

    mutex_lock(&g_http.lock);
    while( !x->done )
        cond_wait(&g_http.changed, &g_http.lock);
    mutex_unlock(&g_http.lock);

    *out_status = x->status;
    if( x->status > 0 )
    {
        char const* head_end = memmem(x->response, (size_t)x->response_len, "\r\n\r\n", 4);
        int const head = (int)(head_end - x->response) + 4;
        long length = header_value_long(x->response, head - 4, "Content-Length");

        if( length < 0 || length > x->response_len - head )
            length = x->response_len - head;
        answer = ToriMem_Alloc(ToriMem_Net(), (size_t)length + 1);
        assert(answer);
        memcpy(answer, x->response + head, (size_t)length);
        answer[length] = '\0';
        *out_size = (int)length;
    }
    ToriMem_Free(ToriMem_Net(), x->request);
    ToriMem_Free(ToriMem_Net(), x->response);
    mutex_lock(&g_http.lock);
    x->state = EXCHANGE_FREE;
    mutex_unlock(&g_http.lock);
    return answer;
}

/* ---- The loop -------------------------------------------------------------- */

int
HttpServer_Run(
    int port,
    HttpHandler handler,
    void* user)
{
    return HttpServer_RunOn(0, port, handler, user);
}

/* A finished job back to its client, if the client is still the one that
 * asked. */
static void
deliver(
    struct HttpClient* clients,
    struct HttpJob* job)
{
    struct HttpClient* client = &clients[job->client];

    if( client->fd != SOCK_INVALID && client->generation == job->generation )
    {
        client->busy = 0;
        if( send_response(client, &job->res) != 0 )
            client_reset(client);
    }
    job_free(job);
}

int
HttpServer_RunOn(
    int loopback_only,
    int port,
    HttpHandler handler,
    void* user)
{
    struct HttpClient clients[HTTP_MAX_CLIENTS];
    struct sockaddr_in addr;
    sock_t listen_fd;
    int one = 1;

    assert(handler);
    memset(clients, 0, sizeof(clients));
    for( int i = 0; i < HTTP_MAX_CLIENTS; i++ )
        clients[i].fd = SOCK_INVALID;

#ifndef _WIN32
    /* A peer that closes mid-write must fail the send, not kill the process.
     * Windows sockets never raise SIGPIPE at all -- send() just returns an
     * error -- and mingw only declares the macro behind #ifdef _POSIX, which
     * this build does not define. */
    signal(SIGPIPE, SIG_IGN);
#endif

#ifdef _WIN32
    {
        WSADATA wsa_data;
        if( WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0 )
        {
            fprintf(stderr, "WSAStartup failed\n");
            return 1;
        }
    }
#endif

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if( listen_fd == SOCK_INVALID )
    {
        sock_perror("socket");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if( bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
    {
        sock_perror("bind");
        close(listen_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    if( listen(listen_fd, 32) != 0 )
    {
        sock_perror("listen");
        close(listen_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    socket_set_nonblocking(listen_fd);

    mutex_init(&g_http.lock);
    cond_init(&g_http.changed);
    g_http.handler = handler;
    g_http.user = user;
    g_http.stopping = 0;
    for( int i = 0; i < HTTP_MAX_EXCHANGES; i++ )
    {
        g_http.exchanges[i].state = EXCHANGE_FREE;
        g_http.exchanges[i].fd = SOCK_INVALID;
    }
    if( wake_pair(g_http.wake) != 0 )
    {
        sock_perror("wake channel");
        close(listen_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    g_http.running = 1;
    {
#ifdef _WIN32
        HANDLE worker = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
        assert(worker);
#else
        pthread_t worker;
        int const started = pthread_create(&worker, NULL, worker_main, NULL);
        assert(started == 0);
        (void)started;
#endif

        while( !g_stop )
        {
            fd_set readable;
            fd_set writable;
            struct timeval timeout = { 0, 200 * 1000 };
            int ready;
            struct HttpJob* finished;
            sock_t max_fd = listen_fd;

/* Windows ignores select()'s nfds argument entirely (Winsock's fd_set is an
 * explicit array, not a bitmask indexed by fd number); the high-water mark is
 * POSIX's, and harmless there. */
#define WATCH(fd, set)                                                                             \
    do                                                                                             \
    {                                                                                              \
        FD_SET((fd), (set));                                                                       \
        if( (fd) > max_fd )                                                                        \
            max_fd = (fd);                                                                         \
    } while( 0 )

            FD_ZERO(&readable);
            FD_ZERO(&writable);
            WATCH(listen_fd, &readable);
            WATCH(g_http.wake[0], &readable);
            for( int i = 0; i < HTTP_MAX_CLIENTS; i++ )
            {
                if( clients[i].fd == SOCK_INVALID )
                    continue;
                /* A client with a response going out, or a request with the
                 * worker, is not read from: its next request waits behind the
                 * answer to this one, and a client that will not read cannot
                 * pile up more answers. */
                if( client_has_out(&clients[i]) )
                    WATCH(clients[i].fd, &writable);
                else if( !clients[i].busy )
                    WATCH(clients[i].fd, &readable);
            }
            mutex_lock(&g_http.lock);
            for( int i = 0; i < HTTP_MAX_EXCHANGES; i++ )
            {
                struct HttpExchange* x = &g_http.exchanges[i];

                if( x->done || x->fd == SOCK_INVALID )
                    continue;
                if( x->state == EXCHANGE_CONNECTING || x->state == EXCHANGE_SENDING )
                    WATCH(x->fd, &writable);
                else if( x->state == EXCHANGE_RECEIVING )
                    WATCH(x->fd, &readable);
            }
            mutex_unlock(&g_http.lock);
#undef WATCH

#ifdef _WIN32
            (void)max_fd;
            ready = select(0, &readable, &writable, NULL, &timeout);
#else
            ready = select(max_fd + 1, &readable, &writable, NULL, &timeout);
#endif
            if( ready < 0 )
            {
#ifdef _WIN32
                if( WSAGetLastError() == WSAEINTR )
                    continue;
#else
                if( errno == EINTR )
                    continue;
#endif
                sock_perror("select");
                break;
            }

            /* What the worker finished. */
            if( FD_ISSET(g_http.wake[0], &readable) )
                wake_drain();
            mutex_lock(&g_http.lock);
            finished = g_http.done_head;
            g_http.done_head = g_http.done_tail = NULL;
            mutex_unlock(&g_http.lock);
            while( finished )
            {
                struct HttpJob* next = finished->next;
                deliver(clients, finished);
                finished = next;
            }

            /* The exchanges the worker is waiting on. */
            for( int i = 0; i < HTTP_MAX_EXCHANGES; i++ )
            {
                struct HttpExchange* x = &g_http.exchanges[i];
                enum HttpExchangeState state;

                mutex_lock(&g_http.lock);
                state = x->done ? EXCHANGE_FREE : x->state;
                mutex_unlock(&g_http.lock);
                if( state == EXCHANGE_FREE )
                    continue;
                if( now_ms() >= x->deadline_ms )
                {
                    exchange_finish(x, 0);
                    continue;
                }
                exchange_step(x, x->fd != SOCK_INVALID && FD_ISSET(x->fd, &readable),
                              x->fd != SOCK_INVALID && FD_ISSET(x->fd, &writable));
            }

            if( FD_ISSET(listen_fd, &readable) )
            {
                sock_t fd;
                while( (fd = accept(listen_fd, NULL, NULL)) != SOCK_INVALID )
                {
                    int slot = -1;
                    for( int i = 0; i < HTTP_MAX_CLIENTS && slot < 0; i++ )
                        if( clients[i].fd == SOCK_INVALID )
                            slot = i;
                    if( slot < 0 )
                    {
                        close(fd);
                        continue;
                    }
                    /* Responses are one write; Nagle would add 40ms to every
                     * small one, which on a per-frame request loop is the
                     * whole frame budget. */
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
                    socket_set_nonblocking(fd);
                    clients[slot].fd = fd;
                }
            }

            for( int i = 0; i < HTTP_MAX_CLIENTS; i++ )
            {
                uint8_t chunk[16384];
                int n;
                int rc = 1;

                if( clients[i].fd == SOCK_INVALID || clients[i].busy )
                    continue;

                if( client_has_out(&clients[i]) )
                {
                    if( FD_ISSET(clients[i].fd, &writable) && client_flush(&clients[i]) != 0 )
                    {
                        client_reset(&clients[i]);
                        continue;
                    }
                    if( client_has_out(&clients[i]) )
                    {
                        if( now_ms() - clients[i].out_progress_ms >= HTTP_SEND_STALL_MS )
                            client_reset(&clients[i]);
                        continue;
                    }
                    /* Drained: a request that arrived behind it is taken
                     * now, whether or not the socket has anything new. */
                }
                else if( FD_ISSET(clients[i].fd, &readable) )
                {
                    n = recv(clients[i].fd, (char*)chunk, sizeof(chunk), 0);
                    if( n < 0 && socket_would_block() )
                        continue;
                    if( n <= 0 )
                    {
                        client_reset(&clients[i]);
                        continue;
                    }
                    if( client_append(&clients[i], chunk, n) != 0 )
                    {
                        client_reset(&clients[i]);
                        continue;
                    }
                }
                else if( clients[i].len == 0 )
                    continue;

                if( clients[i].len > 0 && !client_has_out(&clients[i]) )
                    rc = client_dispatch(&clients[i], i);
                if( rc < 0 )
                    client_reset(&clients[i]);
            }
        }

        /* Down: the worker finishes what it holds and stops. An exchange it
         * is waiting on ends unanswered rather than holding it forever. */
        mutex_lock(&g_http.lock);
        g_http.stopping = 1;
        cond_broadcast(&g_http.changed);
        mutex_unlock(&g_http.lock);
        for( int spins = 0;; spins++ )
        {
            int waiting = 0;
            for( int i = 0; i < HTTP_MAX_EXCHANGES; i++ )
            {
                struct HttpExchange* x = &g_http.exchanges[i];
                mutex_lock(&g_http.lock);
                waiting = x->state != EXCHANGE_FREE && !x->done;
                mutex_unlock(&g_http.lock);
                if( waiting )
                    exchange_finish(x, 0);
            }
            mutex_lock(&g_http.lock);
            waiting = g_http.todo_head != NULL;
            mutex_unlock(&g_http.lock);
            if( !waiting || spins > 0 )
                break;
        }
#ifdef _WIN32
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
#else
        pthread_join(worker, NULL);
#endif
    }
    g_http.running = 0;
    while( g_http.done_head )
    {
        struct HttpJob* next = g_http.done_head->next;
        job_free(g_http.done_head);
        g_http.done_head = next;
    }
    g_http.done_tail = NULL;

    for( int i = 0; i < HTTP_MAX_CLIENTS; i++ )
        client_reset(&clients[i]);
    close(listen_fd);
    close(g_http.wake[0]);
    close(g_http.wake[1]);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
