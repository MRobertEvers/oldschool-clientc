#ifndef SRC_IOSERVER_HTTP_SERVER_H
#define SRC_IOSERVER_HTTP_SERVER_H

/*
 * A small HTTP/1.1 server, sized for exactly one job: being the thing a browser
 * tab talks to during development.
 *
 * It multiplexes with select() rather than serving one connection at a time,
 * because a browser holds its connections open (keep-alive) and a sequential
 * loop would deadlock the moment the page fetched two things at once — the
 * second request would sit unread behind the first connection's idle socket.
 *
 * Not a general-purpose server: no chunked bodies, no TLS, no compression. It
 * refuses what it does not understand rather than guessing.
 */

#include <stdint.h>

#define HTTP_MAX_PATH 1024

struct HttpRequest
{
    char method[8];
    char path[HTTP_MAX_PATH];
    uint8_t const* body;
    int body_len;
    /** The raw header block, without the request line and without the blank
     *  line that ends it. Read it through HttpRequest_Header rather than
     *  scanning it: header names are case-insensitive and browsers vary. */
    char const* headers;
    int headers_len;
};

struct HttpResponse
{
    int status;              /* 200, 304, 400, 404, ... */
    char content_type[64];   /* defaults to application/octet-stream */
    void* body;              /* may be NULL */
    int body_len;
    /** When set, the server free()s body after writing it. */
    int owns_body;
    /**
     * Validator for this representation, sent as ETag. When set, the response
     * is also marked `Cache-Control: no-cache` instead of `no-store` — store
     * it, but ask every time — which is what makes a conditional request
     * possible at all. `no-store` forbids the browser from keeping the copy it
     * would revalidate.
     */
    char etag[64];
};

/**
 * One request header's value, or NULL when absent. The returned pointer is into
 * the request buffer and is valid for the handler call only; `len` is its
 * length, since it is not NUL-terminated.
 */
char const*
HttpRequest_Header(
    struct HttpRequest const* req,
    char const* name,
    int* len);

/** Does the request's If-None-Match cover `etag`? A 304 is correct when so. */
int
HttpRequest_MatchesETag(
    struct HttpRequest const* req,
    char const* etag);

/**
 * Handle one request. Fill `res`; leaving it untouched sends 404.
 * Called from the server's thread with the whole body already read.
 */
typedef void (*HttpHandler)(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res);

/**
 * Bind, listen and serve until the process is interrupted.
 * Returns non-zero if the socket could not be opened.
 */
int
HttpServer_Run(
    int port,
    HttpHandler handler,
    void* user);

/** Ask the running server to return from HttpServer_Run (signal-safe). */
void
HttpServer_RequestStop(void);

/** Best-effort content type for a file name, by extension. */
char const*
HttpServer_ContentTypeForPath(char const* path);

#endif
