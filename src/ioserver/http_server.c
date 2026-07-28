#include "http_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define HTTP_MAX_CLIENTS 64
/* A request body is an IO batch of a few dozen records; anything past this is
 * a client bug or an attack, and refusing beats growing without bound. */
#define HTTP_MAX_BODY (16 * 1024 * 1024)
#define HTTP_MAX_HEADERS (64 * 1024)

struct HttpClient
{
    int fd;
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
    if( client->fd >= 0 )
        close(client->fd);
    free(client->buf);
    client->fd = -1;
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
    int fd,
    void const* data,
    int count)
{
    uint8_t const* p = (uint8_t const*)data;
    int written = 0;
    while( written < count )
    {
        ssize_t n = send(fd, p + written, (size_t)(count - written), 0);
        if( n <= 0 )
        {
            if( n < 0 && (errno == EINTR || errno == EAGAIN) )
                continue;
            return -1;
        }
        written += (int)n;
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
    int fd,
    struct HttpResponse const* res)
{
    char header[512];
    int header_len = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        /* The page may be opened from a file:// URL or another dev server
         * while this one only answers IO; without these the browser drops the
         * response before the harness ever sees it. */
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        res->status,
        status_text(res->status),
        res->content_type[0] ? res->content_type : "application/octet-stream",
        res->body_len);

    if( write_all(fd, header, header_len) != 0 )
        return -1;
    if( res->body_len > 0 && write_all(fd, res->body, res->body_len) != 0 )
        return -1;
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
    int listen_fd;
    int one = 1;

    for( int i = 0; i < HTTP_MAX_CLIENTS; i++ )
    {
        clients[i].fd = -1;
        clients[i].buf = NULL;
        clients[i].len = 0;
        clients[i].cap = 0;
    }

    /* A peer that closes mid-write must fail the send, not kill the process. */
    signal(SIGPIPE, SIG_IGN);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if( listen_fd < 0 )
    {
        perror("socket");
        return 1;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if( bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
    {
        perror("bind");
        close(listen_fd);
        return 1;
    }
    if( listen(listen_fd, 32) != 0 )
    {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    while( !g_stop )
    {
        fd_set readable;
        int max_fd = listen_fd;
        struct timeval timeout = { 0, 200 * 1000 };
        int ready;

        FD_ZERO(&readable);
        FD_SET(listen_fd, &readable);
        for( int i = 0; i < HTTP_MAX_CLIENTS; i++ )
        {
            if( clients[i].fd < 0 )
                continue;
            FD_SET(clients[i].fd, &readable);
            if( clients[i].fd > max_fd )
                max_fd = clients[i].fd;
        }

        ready = select(max_fd + 1, &readable, NULL, NULL, &timeout);
        if( ready < 0 )
        {
            if( errno == EINTR )
                continue;
            perror("select");
            break;
        }

        if( FD_ISSET(listen_fd, &readable) )
        {
            int fd = accept(listen_fd, NULL, NULL);
            if( fd >= 0 )
            {
                int slot = -1;
                for( int i = 0; i < HTTP_MAX_CLIENTS && slot < 0; i++ )
                    if( clients[i].fd < 0 )
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
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                    clients[slot].fd = fd;
                }
            }
        }

        for( int i = 0; i < HTTP_MAX_CLIENTS; i++ )
        {
            uint8_t chunk[16384];
            ssize_t n;
            int rc;

            if( clients[i].fd < 0 || !FD_ISSET(clients[i].fd, &readable) )
                continue;

            n = recv(clients[i].fd, chunk, sizeof(chunk), 0);
            if( n <= 0 )
            {
                client_reset(&clients[i]);
                continue;
            }
            if( client_append(&clients[i], chunk, (int)n) != 0 )
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
    return 0;
}
