/*
 * The listening socket, and nothing else.
 *
 * Everything that used to make this file long has moved:
 *
 *   torirs_server_host.c       the poll loop over every connection, the tick,
 *                        and the login/logout work queue
 *   torirs_server_session.c    the login handshake and inbound framing, as a state
 *                        machine rather than a run of blocking reads
 *   torirs_server_transport.c  where the bytes come from
 *   torirs_server_boot.c       the loader order
 *
 * What is left is the part that is genuinely about *starting* a socket server:
 * parsing the port, binding, and booting the static data in the right order. An
 * in-process server (torirs_server_embed.c) shares everything else and has none
 * of this.
 *
 * The `serve()` that used to live here — one connection, its own world, a
 * forked child per JS5 download — is gone. See torirs_server_host.h for what
 * replaced it and why.
 *
 *   make -C src ToriRSServer
 *   src/build/torirsserver [port]
 *   src/torirs --manifest manifests/manifest_osrs230.ini --user test --pass test
 *
 * Env:
 *   TORIRSSERVER_VERBOSE=1   log every packet in and out
 *   TORIRSSERVER_CACHE=dir   cache to read obj metadata from (default cache.osrs239.baked)
 *   TORIRSSERVER_CONTENT=dir content tree (default OSRS-Content/osrs239-content)
 *   TORIRSSERVER_SCRIPTS=dir compiled script pack (default <content>/scripts/build)
 *   TORIRSSERVER_HOME=x,z    tile to log in on (default 3222,3218 — Lumbridge castle
 *                       courtyard, beside Hans; the scene's origin zone is
 *                       derived from it)
 *   TORIRSSERVER_STAFF_LEVEL=0..3 advertise rev239 staff privilege; isolated UI
 *                       harnesses use 2 so typed ::commands take the golden
 *                       CLIENT_CHEAT path (default 0)
 */
#include "torirs_server.h"
#include <assert.h>

#include "torirs_server_boot.h"
#include "torirs_server_container.h"
#include "torirs_server_host.h"
#include "torirs_server_shop.h"

#include <signal.h>

/* mingw-w64 has no POSIX socket headers at all (see src/ioserver/http_server.c
 * and src/torirsserver/torirs_server_ws.c for the same split); Winsock2 replaces
 * sockets and select()'s error reporting. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* Overridden by the `torirsserver-dev` build (43597) so a second instance can run
 * beside the default one. `src/build/torirsserver <port>` still wins over both. */
#ifndef TORIRSSERVER_DEFAULT_PORT
#define TORIRSSERVER_DEFAULT_PORT 43595
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* perror() reads errno, which Winsock calls never set -- WSAGetLastError() is
 * the only place a Windows socket failure's real code shows up. */
static void
sock_perror(char const* msg)
{
#ifdef _WIN32
    fprintf(stderr, "%s: WSA error %d\n", msg, WSAGetLastError());
#else
    perror(msg);
#endif
}

int
main(
    int argc,
    char** argv)
{
    static struct ToriRSServer srv; /* ~200 KB of player state — not on the stack */
    struct ToriRSServerBootConfig config;
    /* --selftest: run the game logic with no socket and exit. Detected before
     * the port parse because atoi("--selftest") is 0. */
    int selftest = argc > 1 && strcmp(argv[1], "--selftest") == 0;
    int port = (argc > 1 && !selftest) ? atoi(argv[1]) : TORIRSSERVER_DEFAULT_PORT;
    int listener = -1;
    int reuse = 1;
    int status;
    struct sockaddr_in addr;
    /*
     * Which revision's bytes to write. `--rev <name>` beats TORIRSSERVER_REV beats
     * the default, which is osrs230 — so every existing manifest, script and
     * test keeps its behaviour by saying nothing. See torirs_server_wire.h.
     */
    char const* rev_name = getenv("TORIRSSERVER_REV");
    const struct ToriRSServerWire* wire;

    for( int i = 1; i < argc - 1; i++ )
        if( strcmp(argv[i], "--rev") == 0 )
            rev_name = argv[i + 1];

    wire = rev_name ? ToriRSServer_WireByName(rev_name) : ToriRSServer_WireDefault();
    if( !wire )
    {
        fprintf(stderr, "torirsserver: unknown --rev '%s' (osrs230, osrs239)\n", rev_name);
        return 1;
    }

    /*
     * A client that walks away mid-write must not take the server with it.
     *
     * This mattered little while every packet was a few hundred bytes and a
     * dead socket surfaced as a -1 from send(). It matters now that the same
     * port serves JS5: a cache download is megabytes written in a tight loop,
     * so a client that closes its update connection the moment it has what it
     * wants — which is the normal way that connection ends — lands SIGPIPE in
     * the middle of a write and killed the process. Exit code 141 is what that
     * looks like from outside, and it looks exactly like a crash.
     *
     * Windows sockets never raise SIGPIPE at all -- send() just returns an
     * error -- and mingw only declares the macro behind #ifdef _POSIX, which
     * this build does not define.
     *
     * SIGCHLD went with the fork: JS5 is served in the host's loop now, so
     * there is no child to reap.
     */
#ifndef _WIN32
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

    ToriRSServer_BootDefaults(&config);

    /*
     * Bind and listen *before* the loaders, not after. They take over a second
     * to read the cache and the content tree, and a client launched alongside
     * this process (run-live.sh starts both) dials the port well inside that
     * window. A socket that is listening but not yet accepting parks the
     * connection in the backlog until the loop below reaches accept(); a socket
     * that does not exist yet answers RST, and the client's connect is a
     * one-shot — a refusal is terminal, so the whole session comes up with no
     * world and only the cache-configured interface drawn.
     *
     * A bind failure still means "someone else holds this port", and now says so
     * a second sooner, which is what run-live.sh's already-running check reads.
     */
    if( !selftest )
    {
        listener = (int)socket(AF_INET, SOCK_STREAM, 0);
        if( listener < 0 )
        {
            sock_perror("socket");
#ifdef _WIN32
            WSACleanup();
#endif
            return 1;
        }
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((uint16_t)port);
        if( bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
        {
            sock_perror("bind");
#ifdef _WIN32
            WSACleanup();
#endif
            return 1;
        }
        /*
         * A backlog of one was right while the server drained the queue by
         * serving a connection to completion. It is wrong now for the reason
         * the rest of this change exists: a client opens JS5 and its game
         * socket back to back, several clients arrive together when a world
         * comes up, and a connection that does not fit in the backlog is
         * refused by the kernel before this process ever sees it.
         */
        listen(listener, 16);
    }

    ToriRSServer_BootLoad(&config);

    if( selftest )
    {
        int failures = ToriRSServer_WorldSelftest();
        ToriRSServer_BootFree();
        return failures ? 1 : 0;
    }

    /*
     * The world's server struct, set up once for the process.
     *
     * This used to be per connection — `serve()` memset it on every accept and
     * re-seeded the shops — which was the honest thing to do while one
     * connection *was* the world. It is boot now: the world outlives any one
     * session, so anything reset per connection would be a mid-session reset
     * for everybody else.
     */
    srv.verbose = getenv("TORIRSSERVER_VERBOSE") != NULL;
    srv.familiar_singles_assist = ToriRSServer_FlagDefaultOn("TORIRSSERVER_FAMILIAR_SINGLES");
    srv.members_world = ToriRSServer_FlagDefaultOn("TORIRSSERVER_MEMBERS_WORLD");
    srv.wire = wire;
    /* Shop definitions are global (ToriRSServer_ContentLoad populated them);
     * seeding their containers is per-server-instance, so it happens once the
     * boot that defined them has run. */
    ToriRSServer_ShopSeed(&srv);

    /*
     * Compile-check and load the script pack now, not at first login.
     *
     * `ToriRSServer_ScriptsLoad` is idempotent (`scripts_ok`), so the login path can
     * still call it and this is purely a matter of *when* the cost is paid. It
     * used to be paid by the first player: the login response had already gone
     * out, so every second spent here was a second the client spent on
     * "Connecting to server..." with nothing to attribute it to at either end.
     * Boot is the honest place — the server is not claiming to be ready yet.
     */
    ToriRSServer_ScriptsLoad(&srv, config.script_dir);

    /* Listening since before the loaders ran; this is where it starts accepting. */
    fprintf(stderr,
            "torirsserver: listening on 127.0.0.1:%d, wire %s (home %d,%d — zone %d,%d), "
            "up to %d players\n",
            port, wire->name, config.home_x, config.home_z,
            ToriRSServer_BootZone(config.home_x), ToriRSServer_BootZone(config.home_z),
            TORIRSSERVER_PLAYER_MAX);

    status = ToriRSServer_HostRun(&srv, &config, listener);

    ToriRSServer_BankShutdown(&srv);
    ToriRSServer_ContainerShutdown(&srv);
    ToriRSServer_ScriptsFree(&srv);
    ToriRSServer_BootFree();
    close(listener);
#ifdef _WIN32
    WSACleanup();
#endif
    return status;
}
