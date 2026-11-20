#ifndef SRC_SERVICE_SERVICE_LINK_H
#define SRC_SERVICE_SERVICE_LINK_H

/*
 * How one server reaches another: in this process, or over HTTP, signed.
 *
 * Every server is a handler (an HttpHandler over its own routes, `/login`,
 * `/view/hiscores`, ...) that can be linked into any process. A ServiceLink is
 * a client's name for one of them:
 *
 *   in process   ServiceLink_SetLocal: a call is the handler, directly, with
 *                the request marked `trusted` -- no socket, no signature
 *   over HTTP    `<env>` = host:port (default 127.0.0.1:<default_port>): the
 *                route goes out under the server's prefix (`/svc/account`
 *                + `/login`), signed (service_auth.h)
 *
 * The far side is a ServiceHost (service_host.h), which checks the signature
 * and hands the handler the route without its prefix, so the handler is the
 * same code either way.
 *
 * A call blocks until the answer. A process that must not block a thread on
 * a socket -- the website and the IO server, whose handlers run on an
 * HttpServer's worker -- routes its calls through the server's loop
 * (ServiceLink_SetExchange(HttpServer_ExchangeEx)).
 */

#include "service/http_server.h"

#include <stddef.h>

/** How a process makes a request it cannot block a thread on. */
typedef void (*ServiceExchange)(
    char const* host,
    int port,
    char const* method,
    char const* route,
    char const* extra_headers,
    char const* content_type,
    void const* body,
    int body_len,
    int timeout_ms,
    struct HttpExchangeAnswer* out);

struct ServiceLink
{
    /** For messages: "account", "hiscores", ... */
    char const* name;
    /** The variable naming its host:port. */
    char const* env;
    int default_port;
    /** What its routes are served under by a ServiceHost ("" for none: the
     *  IO server's routes are a browser's paths). */
    char const* prefix;
    /** Set when the server is in this process. */
    HttpHandler local;
    void* local_user;
};

/** The server is in this process: every call is `handler`. NULL goes back to
 *  HTTP. */
void
ServiceLink_SetLocal(
    struct ServiceLink* link,
    HttpHandler handler,
    void* user);

/** 1 when `link`'s variable names a server (so a process should not host one
 *  of its own). */
int
ServiceLink_Named(struct ServiceLink const* link);

/** Where `link` is reached over HTTP. */
void
ServiceLink_Address(
    struct ServiceLink const* link,
    char* host,
    size_t cap,
    int* port);

/** How this process makes its requests; NULL is a blocking socket. For every
 *  link. */
void
ServiceLink_SetExchange(ServiceExchange exchange);

/**
 * `method` `route` (with its query) to `link`, `body` (`body_len` bytes,
 * NULL with 0 for a GET) as its body. The answer's body, NUL-terminated and
 * NET-allocated (free with ToriMem_Net), with `*out_status` and `*out_size`;
 * NULL with status 0 when nothing answered -- said on stderr. A refusal's
 * status and body come back like any answer.
 */
char*
ServiceLink_Ask(
    struct ServiceLink* link,
    char const* method,
    char const* route,
    char const* body,
    int body_len,
    int* out_status,
    int* out_size);

/**
 * `req`, as it came, to `link`, and its answer into `res`: the website's
 * forward of a browser's request to the IO server. In process, the handler
 * itself; over HTTP, signed, with the request's If-None-Match passed on and
 * the answer's type, ETag and Location kept. A server that does not answer
 * is a 502. Only from an HttpServer's handler.
 */
void
ServiceLink_Forward(
    struct ServiceLink* link,
    struct HttpRequest const* req,
    struct HttpResponse* res);

#endif
