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
};

struct HttpResponse
{
    int status;              /* 200, 400, 404, ... */
    char content_type[64];   /* defaults to application/octet-stream */
    void* body;              /* may be NULL */
    int body_len;
    /** When set, the server free()s body after writing it. */
    int owns_body;
};

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
