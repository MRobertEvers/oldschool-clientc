#ifndef SRC_IOSERVER_IO_SERVER_H
#define SRC_IOSERVER_IO_SERVER_H

/*
 * The IO server: what a running web client reads -- the raw page and the
 * assets it loads (build-web/), manifests and RevConfig INIs (/boot/), cache
 * reads as IOWire batches (/io), the dat1 proxy, plugins, the black box
 * (/telemetry), the map editor's pipe (/maped/) -- and, with --js5-port, the
 * cache over JS5 for any client. See io_server.c's header for the routes.
 *
 * One per process, as a handler:
 *
 *   alone        io_server_main.c: `io_server [--port 8088]` for a local web
 *                client with no website in front
 *   behind the   the website forwards a signed-in visitor's requests to it
 *   website      (TORIRS_IO_SERVER, default 127.0.0.1:IO_SERVER_BEHIND_PORT),
 *                signed: `io_server --behind-website` answers nothing else
 *   in process   `website_server --host io`: the website calls the handler
 */

#include "service/http_server.h"

#include <stdio.h>

/** Alone, for a local web client. */
#define IO_SERVER_DEFAULT_PORT 8088
/** Behind the website, which has 8088. */
#define IO_SERVER_BEHIND_PORT 8089

/** The IO server's flags, for a usage message. */
void
IoServer_Usage(FILE* out);

/** `argv[*index]` if it is one of the IO server's flags: 1 consumed (and
 *  `*index` moved past its value), 0 not one of them, -1 one of them but
 *  wrong (said on stderr). */
int
IoServer_ParseArg(
    int argc,
    char** argv,
    int* index);

/** After the flags: the preopened cache, the on-demand wire, JS5. `port` is
 *  the one browsers reach it on, for its status page. 0 when it cannot start
 *  (said on stderr). */
int
IoServer_Start(int port);

/** The HttpHandler (`user` unused). */
void
IoServer_Handle(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res);

/** Stop JS5, print the counters, close the caches. */
void
IoServer_Stop(void);

#endif
