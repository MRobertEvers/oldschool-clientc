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
    /** When set, the server frees body after writing it, from ToriMem_Net():
     *  an owned body is allocated there. */
    int owns_body;
    /**
     * Validator for this representation, sent as ETag. When set, the response
     * is also marked `Cache-Control: no-cache` instead of `no-store` — store
     * it, but ask every time — which is what makes a conditional request
     * possible at all. `no-store` forbids the browser from keeping the copy it
     * would revalidate.
     */
    char etag[64];
    /**
     * When set, sent as Location. Only meaningful with a 3xx status; a browser
     * that follows it re-requests, so the handler that set it does no work for
     * the destination.
     */
    char location[768];
    /** When set, sent as one Set-Cookie header (io_gate.c's ticket). */
    char set_cookie[256];
    /** When set, sent as WWW-Authenticate (a Bearer refusal, RFC 6750 3). */
    char www_authenticate[96];
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
/**
 * A request's answer. Runs on the server's worker thread, never beside another
 * handler, so handlers share their state without locks; the thread that called
 * HttpServer_Run owns the sockets and never waits on a handler.
 */
typedef void (*HttpHandler)(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res);

/**
 * One HTTP request to `host`:`port`, from a handler: the server's loop makes
 * it without blocking while the worker waits for the answer, so a handler
 * never holds a socket of its own. The response body NET-allocated (free with
 * ToriMem_Net), whatever its status, with `*out_status` the status; NULL with
 * status 0 when nothing answered within `timeout_ms`. `content_type` NULL for
 * none; `body_len` 0 for no body.
 */
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
    int* out_status);

/**
 * Bind, listen and serve until the process is interrupted.
 * Returns non-zero if the socket could not be opened.
 */
int
HttpServer_Run(
    int port,
    HttpHandler handler,
    void* user);

/**
 * As HttpServer_Run, but `loopback_only` 1 binds 127.0.0.1 alone: a service
 * meant only for processes on this machine (the account server, which answers
 * password checks and takes the game's records without any credential of its
 * own) is unreachable from the network rather than trusted to it.
 */
int
HttpServer_RunOn(
    int loopback_only,
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
