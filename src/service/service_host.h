#ifndef SRC_SERVICE_SERVICE_HOST_H
#define SRC_SERVICE_SERVICE_HOST_H

/*
 * One process's listener for the servers it hosts.
 *
 * A process serves the servers in it on one port (an HttpServer serves one
 * port per process): each under its prefix, `/svc/<name>/...`, where every
 * request must carry the servers' signature (service_auth.h) and the handler
 * is given the route without the prefix; and at most one FALLBACK for
 * everything else -- the website's public pages, or the IO server's browser
 * routes -- which needs the signature only when it is set to.
 *
 *   account_server        /svc/account       on its own port
 *   hiscores_server       /svc/hiscores
 *   website_server        /svc/users + the public site as the fallback
 *   io_server             the browser's routes as the fallback (signed when
 *                         it sits behind the website)
 *   torirsserver          /svc/account when it hosts the account server
 *
 * and any of them can host another's handler beside its own: the same routes
 * on the same port, answered in process.
 */

#include "service/http_server.h"

#define SERVICE_HOST_MAX 8

/** Serve `handler` under `prefix` ("/svc/account"): signed requests only. */
void
ServiceHost_Add(
    char const* prefix,
    HttpHandler handler,
    void* user);

/** Everything outside the prefixes goes to `handler`; `signed_only` 1 when
 *  it, too, may be reached only by the servers. */
void
ServiceHost_SetFallback(
    HttpHandler handler,
    void* user,
    int signed_only);

/** The HttpHandler that routes as above (`user` unused). */
void
ServiceHost_Handle(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res);

/** Serve on `port` until the process is stopped (HttpServer_RequestStop).
 *  `loopback_only` 1 binds 127.0.0.1 alone. Non-zero when the port could not
 *  be had. */
int
ServiceHost_Run(
    int loopback_only,
    int port);

/** Serve on `port` from a thread of this process: a server whose own work is
 *  elsewhere (the game's loop). 1 serving; 0 when the port could not be had,
 *  with the reason printed. */
int
ServiceHost_StartThread(
    int loopback_only,
    int port);

/** Stop the thread's server and wait for it. Nothing when none started. */
void
ServiceHost_StopThread(void);

#endif
