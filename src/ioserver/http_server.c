#include "http_server.h"
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
#define close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
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

#define HTTP_MAX_CLIENTS 64
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
    return "application/octet-stream";
}

static void
client_reset(struct HttpClient* client)
{
    if( client->fd != SOCK_INVALID )
        close(client->fd);
    free(client->buf);
    client->fd = SOCK_INVALID;
    client->buf = NULL;
    client->len = 0;
    client->cap = 0;
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
        grown = realloc(client->buf, (size_t)cap);
        if( !grown )
            return -1;
        client->buf = grown;
        client->cap = cap;
    }
    memcpy(client->buf + client->len, data, (size_t)count);
    client->len += count;
    return 0;
}

static int
write_all(
    sock_t fd,
    void const* data,
    int count)
{
    uint8_t const* p = (uint8_t const*)data;
    int written = 0;
    while( written < count )
    {
        int n = send(fd, (char const*)(p + written), count - written, 0);
        if( n <= 0 )
        {
#ifdef _WIN32
            if( n < 0 && WSAGetLastError() == WSAEINTR )
                continue;
#else
            if( n < 0 && (errno == EINTR || errno == EAGAIN) )
                continue;
#endif
            return -1;
        }
        written += n;
    }
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
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 413:
        return "Payload Too Large";
    case 500:
        return "Internal Server Error";
    default:
        return "OK";
    }
}

static int
send_response(
    sock_t fd,
    struct HttpResponse const* res)
{
    char header[768];
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
        /* no-store on anything without a validator: an IO batch is a POST
         * result and a stats page is a snapshot, neither of which may be
         * reused. A validated representation is the opposite case — it must be
         * stored to be revalidated. */
        res->etag[0] ? "no-cache" : "no-store");

    if( header_len < 0 || header_len >= (int)sizeof(header) )
        return -1;
    if( write_all(fd, header, header_len) != 0 )
        return -1;
    if( body_len > 0 && write_all(fd, res->body, body_len) != 0 )
        return -1;
    return 0;
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

/*
 * Try to take one complete request off the front of the client's buffer.
 * Returns 1 when one was handled (and consumed), 0 when more bytes are needed,
 * -1 when the connection should be dropped.
 */
static int
client_try_request(
    struct HttpClient* client,
    HttpHandler handler,
    void* user)
{
    char const* buf = (char const*)client->buf;
    char const* head_end;
    int head_len;
    long content_length;
    int total;
    struct HttpRequest req;
    struct HttpResponse res;
    int rc;

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

    memset(&req, 0, sizeof(req));
    {
        /* Request line: METHOD SP PATH SP VERSION */
        int i = 0;
        int m = 0;
        while( i < head_len && buf[i] != ' ' && m < (int)sizeof(req.method) - 1 )
            req.method[m++] = buf[i++];
        req.method[m] = '\0';
        while( i < head_len && buf[i] == ' ' )
            i++;
        {
            int p = 0;
            while( i < head_len && buf[i] != ' ' && buf[i] != '\r' &&
                   p < (int)sizeof(req.path) - 1 )
                req.path[p++] = buf[i++];
            req.path[p] = '\0';
        }
        if( req.method[0] == '\0' || req.path[0] == '\0' )
            return -1;
    }
    req.body = client->buf + head_len + 4;
    req.body_len = (int)content_length;
    /* The header block, past the request line: what a conditional request's
     * If-None-Match arrives in. */
    {
        char const* line_end = memmem(buf, (size_t)head_len, "\r\n", 2);
        if( line_end )
        {
            req.headers = line_end + 2;
            req.headers_len = head_len - (int)(line_end + 2 - buf);
        }
    }

    memset(&res, 0, sizeof(res));
    res.status = 404;
    snprintf(res.content_type, sizeof(res.content_type), "text/plain; charset=utf-8");
    handler(user, &req, &res);
    if( res.status == 404 && !res.body )
    {
        res.body = (void*)"not found\n";
        res.body_len = 10;
        res.owns_body = 0;
    }

    rc = send_response(client->fd, &res);
    if( res.owns_body )
        free(res.body);

    /* Consume the request, keeping any pipelined bytes that followed it. */
    memmove(client->buf, client->buf + total, (size_t)(client->len - total));
    client->len -= total;
    return rc == 0 ? 1 : -1;
}

int
HttpServer_Run(
    int port,
    HttpHandler handler,
    void* user)
{
    struct HttpClient clients[HTTP_MAX_CLIENTS];
    struct sockaddr_in addr;
    sock_t listen_fd;
    int one = 1;

    for( int i = 0; i < HTTP_MAX_CLIENTS; i++ )
    {
        clients[i].fd = SOCK_INVALID;
        clients[i].buf = NULL;
        clients[i].len = 0;
        clients[i].cap = 0;
    }

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
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
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

    while( !g_stop )
    {
        fd_set readable;
        struct timeval timeout = { 0, 200 * 1000 };
        int ready;
#ifndef _WIN32
        /* Windows ignores select()'s nfds argument entirely (Winsock's
         * fd_set is an explicit array, not a bitmask indexed by fd number),
         * so tracking the high-water fd is POSIX-only work. */
        sock_t max_fd = listen_fd;
#endif

        FD_ZERO(&readable);
        FD_SET(listen_fd, &readable);
        for( int i = 0; i < HTTP_MAX_CLIENTS; i++ )
        {
            if( clients[i].fd == SOCK_INVALID )
                continue;
            FD_SET(clients[i].fd, &readable);
#ifndef _WIN32
            if( clients[i].fd > max_fd )
                max_fd = clients[i].fd;
#endif
        }

#ifdef _WIN32
        ready = select(0, &readable, NULL, NULL, &timeout);
#else
        ready = select(max_fd + 1, &readable, NULL, NULL, &timeout);
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

        if( FD_ISSET(listen_fd, &readable) )
        {
            sock_t fd = accept(listen_fd, NULL, NULL);
            if( fd != SOCK_INVALID )
            {
                int slot = -1;
                for( int i = 0; i < HTTP_MAX_CLIENTS && slot < 0; i++ )
                    if( clients[i].fd == SOCK_INVALID )
                        slot = i;
                if( slot < 0 )
                {
                    close(fd);
                }
                else
                {
                    /* Responses are one write; Nagle would add 40ms to every
                     * small one, which on a per-frame request loop is the
                     * whole frame budget. */
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
                    clients[slot].fd = fd;
                }
            }
        }

        for( int i = 0; i < HTTP_MAX_CLIENTS; i++ )
        {
            uint8_t chunk[16384];
            int n;
            int rc;

            if( clients[i].fd == SOCK_INVALID || !FD_ISSET(clients[i].fd, &readable) )
                continue;

            n = recv(clients[i].fd, (char*)chunk, sizeof(chunk), 0);
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
            do
            {
                rc = client_try_request(&clients[i], handler, user);
            } while( rc == 1 && clients[i].len > 0 );
            if( rc < 0 )
                client_reset(&clients[i]);
        }
    }

    for( int i = 0; i < HTTP_MAX_CLIENTS; i++ )
        client_reset(&clients[i]);
    close(listen_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
