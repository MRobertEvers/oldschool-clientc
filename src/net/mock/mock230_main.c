/*
 * The listening socket and the 600 ms tick loop.
 *
 * Everything that used to make this file long has moved:
 *
 *   mock230_session.c    the login handshake and inbound framing, as a state
 *                        machine rather than a run of blocking reads
 *   mock230_transport.c  where the bytes come from
 *   mock230_boot.c       the loader order
 *
 * What is left is the part that is genuinely about being a *socket* server:
 * binding, accepting, and deciding when a tick is due. An in-process server
 * (mock230_embed.c) shares everything else and has none of this.
 *
 *   make -C src mock230
 *   src/build/mock230 [port]
 *   src/torirs --manifest manifest_osrs230.ini --user test --pass test
 *
 * Env:
 *   MOCK230_VERBOSE=1   log every packet in and out
 *   MOCK230_CACHE=dir   cache to read obj metadata from (default cache.osrs239.baked)
 *   MOCK230_CONTENT=dir content tree (default OSRS-Content/osrs239-content)
 *   MOCK230_SCRIPTS=dir compiled script pack (default <content>/scripts/build)
 *   MOCK230_HOME=x,z    tile to log in on (default 3222,3218 — Lumbridge castle
 *                       courtyard, beside Hans; the scene's origin zone is
 *                       derived from it)
 *   MOCK230_STAFF_LEVEL=0..3 advertise rev239 staff privilege; isolated UI
 *                       harnesses use 2 so typed ::commands take the golden
 *                       CLIENT_CHEAT path (default 0)
 */
#include "mock230.h"
#include <assert.h>

#include "mock230_boot.h"
#include "mock230_container.h"
#include "mock230_shop.h"
#include "mock230_session.h"
#include "mock230_transport.h"
#include "mock230_ws.h"

#include <signal.h>

/* mingw-w64 has no POSIX socket headers at all (see src/ioserver/http_server.c
 * and src/net/mock/mock230_ws.c for the same split); Winsock2 replaces
 * sockets, select()'s error reporting, and gettimeofday all at once, and
 * fork() has no equivalent whatsoever -- a JS5 connection becomes a detached
 * thread below instead of a forked child. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h> /* _beginthreadex */
#define close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Overridden by the `mock230-dev` build (43597) so a second instance can run
 * beside the default one. `src/build/mock230 <port>` still wins over both. */
#ifndef MOCK230_DEFAULT_PORT
#define MOCK230_DEFAULT_PORT 43595
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * A boolean server flag that is ON unless the environment turns it off.
 *
 * The ordinary `getenv(...) != NULL` idiom above cannot express this: a flag
 * that defaults on needs a way to say "no", and "unset the variable" is not
 * available to a launcher that sets its whole environment from a config. `0`,
 * `no`, `off` and `false` all disable; anything else, including unset, leaves
 * it on.
 */
static int
mock230_flag_default_on(const char* name)
{
    const char* value = getenv(name);

    if( !value || !*value )
        return 1;
    return !(strcmp(value, "0") == 0 || strcmp(value, "no") == 0 ||
             strcmp(value, "off") == 0 || strcmp(value, "false") == 0);
}

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

#define MOCK230_TICK_MS 600

static long
now_ms(void)
{
#ifdef _WIN32
    /* Only ever compared to itself for tick scheduling, so milliseconds since
     * boot (not epoch) is fine -- and unlike gettimeofday(), mingw actually has
     * it. */
    return (long)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long)tv.tv_sec * 1000L) + (tv.tv_usec / 1000L);
#endif
}

/*
 * Sleep until the socket has something or the next tick is due.
 *
 * Returns 1 when there are bytes to read, 0 when the wait timed out, -1 when
 * the connection is gone.
 *
 * The distinction between 1 and 0 is load-bearing and not a tidiness matter: an
 * accepted socket is *blocking*, so this select() is the only thing standing
 * between the reader and a read that parks the entire server until the client
 * next says something. Pumping on a timeout would hang a session whose player
 * is standing still.
 */
static int
wait_readable(
    struct Mock230Session* session,
    long wait_ms)
{
    fd_set readable;
    struct timeval timeout;
    int fd = mock230_session_pollfd(session);
    int ready;

    /* No descriptor means nothing to wait on — the shape an embedded session
     * takes. It never reaches this loop, but reporting "readable" is the safe
     * answer: its transport never blocks. */
    if( fd < 0 )
        return 1;

    if( wait_ms < 0 )
        wait_ms = 0;
    FD_ZERO(&readable);
    FD_SET(fd, &readable);
    timeout.tv_sec = wait_ms / 1000;
    timeout.tv_usec = (wait_ms % 1000) * 1000;

    ready = select(fd + 1, &readable, NULL, NULL, &timeout);
    if( ready < 0 )
    {
#ifdef _WIN32
        return WSAGetLastError() == WSAEINTR ? 0 : -1;
#else
        return errno == EINTR ? 0 : -1;
#endif
    }
    return ready > 0 && FD_ISSET(fd, &readable);
}

static void
serve(
    struct Mock230Server* srv,
    struct Mock230Conn* conn,
    const struct Mock230BootConfig* config,
    const struct Mock230Wire* wire)
{
    struct Mock230Session session;
    struct Mock230Transport transport;
    long next_tick;

    memset(srv, 0, sizeof(*srv));
    srv->verbose = getenv("MOCK230_VERBOSE") != NULL;
    srv->familiar_singles_assist = mock230_flag_default_on("MOCK230_FAMILIAR_SINGLES");
    /*
     * After the memset, not before it.
     *
     * The revision was first set at the call site, which the memset two lines
     * up then erased -- so the server read every login block as revision 230
     * and answered "rsa decrypt failed", a message that points at the key and
     * not at the four bytes of `serverVersion` it had failed to skip. The wire
     * is per-process configuration, so it has to be re-applied to a struct this
     * function deliberately zeroes per connection.
     */
    srv->wire = wire;
    /* Same reason: the memset above wipes any earlier seeding of
     * `srv->world_containers`, so shared shops are reseeded fresh per
     * connection — one process serves one connection at a time here (the JS5
     * thread aside), so this is boot, not a mid-session reset. */
    mock230_shop_seed(srv);

    mock230_transport_socket(&transport, conn);
    mock230_session_init(&session, &transport, srv->verbose);

    next_tick = now_ms() + MOCK230_TICK_MS;

    while( mock230_session_alive(&session) )
    {
        int ready = wait_readable(&session, next_tick - now_ms());

        if( ready < 0 )
            break;

        if( ready > 0 )
        {
            if( !mock230_session_pump(&session, srv) )
                break;

            /*
             * The handshake completed inside that pump. The world comes up here
             * rather than inside the session because a session carries bytes
             * and knows nothing about ticks, npcs or scripts.
             */
            if( mock230_session_take_login(&session) )
            {
                struct Mock230Player* player;

                mock230_scripts_load(srv, config->script_dir);
                mock230_world_init(srv, mock230_boot_zone(config->home_x),
                                   mock230_boot_zone(config->home_z));
                player = mock230_world_add_player(srv, &session);
                if( !player )
                    break;
                session.player = player;
                mock230_world_player_init(player);
                mock230_world_set_display_name(player, session.display_name);
                mock230_world_login(player);
                /* Anything the client sent behind its login block is still in
                 * the session buffer; decode it now that there is a world. */
                if( !mock230_session_pump(&session, srv) )
                    break;
            }
        }

        if( now_ms() >= next_tick )
        {
            mock230_world_tick(srv);
            /* Anchor to the schedule rather than to "now", so a slow tick does
             * not push every later tick out. */
            next_tick += MOCK230_TICK_MS;
            if( now_ms() - next_tick > 5 * MOCK230_TICK_MS )
                next_tick = now_ms() + MOCK230_TICK_MS;
        }
    }

    /*
     * This server still accepts one connection at a time (§6.1 wants a
     * non-blocking accept with per-connection buffering), so the world goes away
     * with the session. `mock230_world_remove_player` releases the slot and the
     * bank; the shutdown below is the rest of the pool, which is empty here and
     * would not be in a multi-connection host.
     */
    mock230_world_remove_player(srv, session.player);
    mock230_bank_shutdown(srv);
    mock230_container_shutdown(srv);
    mock230_scripts_free(srv);
    mock230_session_free(&session);
    mock230_world_reset(srv);
}

/*
 * A JS5 connection's own loop.
 *
 * Deliberately NOT `serve()`. That function ticks the world every 600 ms, and a
 * JS5 session never logs in, so the world it would tick is the memset-zero one
 * `serve` starts with — no scripts, no scene, no players. The child died on the
 * first tick after answering one request, which presents as a client that
 * fetches a single reference table and then renders a black screen forever:
 * the game connection is healthy the whole time, so nothing looks broken.
 *
 * JS5 needs none of it. Read, answer, repeat until the peer goes away.
 */
static void
serve_js5(
    struct Mock230Server* srv,
    struct Mock230Conn* conn)
{
    struct Mock230Session session;
    struct Mock230Transport transport;

    memset(srv, 0, sizeof(*srv));
    srv->verbose = getenv("MOCK230_VERBOSE") != NULL;
    srv->familiar_singles_assist = mock230_flag_default_on("MOCK230_FAMILIAR_SINGLES");

    mock230_transport_socket(&transport, conn);
    mock230_session_init(&session, &transport, srv->verbose);

    while( mock230_session_alive(&session) )
    {
        /* No tick deadline to race, so this blocks until there is something to
         * do — a cache download is request/response with long idle gaps. */
        if( wait_readable(&session, 60000) < 0 )
            break;
        if( !mock230_session_pump(&session, srv) )
            break;
    }
    mock230_session_free(&session);
}

#ifdef _WIN32
/*
 * fork()'s Windows replacement: a JS5 connection gets its own thread rather
 * than its own process.
 *
 * The comment on the POSIX branch below says a forked child "shares nothing
 * mutable" with the parent -- true because fork() copies the whole address
 * space, so the child's `serve_js5`, which memsets its `Mock230Server`
 * argument on entry, was always zeroing a private copy. A thread shares the
 * parent's address space for real, so handing it the same static `srv` the
 * game-connection loop is using would let a JS5 thread's memset stomp a live
 * game tick mid-flight. Each thread instead gets its own heap block; ~330 KB
 * (see the `srv`/`conn` comments in main()) is too much to also put on a
 * thread's stack.
 */
struct Mock230Js5Args
{
    const struct Mock230Wire* wire;
    int fd;
    struct Mock230Server srv;
    struct Mock230Conn conn;
};

static unsigned __stdcall
js5_thread_main(void* raw)
{
    struct Mock230Js5Args* args = (struct Mock230Js5Args*)raw;

    fprintf(stderr, "mock230: JS5 connection\n");
    args->srv.wire = args->wire; /* serve_js5() memsets the rest right away */
    if( mock230_conn_open(&args->conn, args->fd) )
        serve_js5(&args->srv, &args->conn);
    close(args->fd);
    free(args);
    return 0;
}
#endif

int
main(
    int argc,
    char** argv)
{
    static struct Mock230Server srv; /* ~200 KB of player state — not on the stack */
    static struct Mock230Conn conn;  /* 128 KB of buffers — likewise */
    struct Mock230BootConfig config;
    /* --selftest: run the game logic with no socket and exit. Detected before
     * the port parse because atoi("--selftest") is 0. */
    int selftest = argc > 1 && strcmp(argv[1], "--selftest") == 0;
    int port = (argc > 1 && !selftest) ? atoi(argv[1]) : MOCK230_DEFAULT_PORT;
    int listener = -1;
    int reuse = 1;
    struct sockaddr_in addr;
    /*
     * Which revision's bytes to write. `--rev <name>` beats MOCK230_REV beats
     * the default, which is osrs230 — so every existing manifest, script and
     * test keeps its behaviour by saying nothing. See mock230_wire.h.
     */
    char const* rev_name = getenv("MOCK230_REV");
    const struct Mock230Wire* wire;

    for( int i = 1; i < argc - 1; i++ )
        if( strcmp(argv[i], "--rev") == 0 )
            rev_name = argv[i + 1];

    wire = rev_name ? mock230_wire_by_name(rev_name) : mock230_wire_default();
    if( !wire )
    {
        fprintf(stderr, "mock230: unknown --rev '%s' (osrs230, osrs239)\n", rev_name);
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
     */
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
    /* JS5 connections are served by forked children; without this each one
     * leaves a zombie for the lifetime of the process. Threads (the Windows
     * substitute, see js5_thread_main) have no zombie-process concept. */
    signal(SIGCHLD, SIG_IGN);
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

    mock230_boot_defaults(&config);

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
        listen(listener, 1);
    }

    mock230_boot_load(&config);

    if( selftest )
    {
        int failures = mock230_world_selftest();
        mock230_boot_free();
        return failures ? 1 : 0;
    }

    /*
     * Compile-check and load the script pack now, not at first login.
     *
     * `mock230_scripts_load` is idempotent (`scripts_ok`), so the login path can
     * still call it and this is purely a matter of *when* the cost is paid. It
     * used to be paid by the first player: the login response had already gone
     * out, so every second spent here was a second the client spent on
     * "Connecting to server..." with nothing to attribute it to at either end.
     * Boot is the honest place — the server is not claiming to be ready yet.
     */
    srv.wire = wire;
    mock230_scripts_load(&srv, config.script_dir);

    /* Listening since before the loaders ran; this is where it starts accepting. */
    fprintf(stderr,
            "mock230: listening on 127.0.0.1:%d, wire %s (home %d,%d — zone %d,%d)\n",
            port, wire->name, config.home_x, config.home_z,
            mock230_boot_zone(config.home_x), mock230_boot_zone(config.home_z));

    for( ;; )
    {
        uint8_t first = 0;
        int fd = (int)accept(listener, NULL, NULL);
        if( fd < 0 )
            continue;

        /*
         * JS5 and the game are two SEPARATE, CONCURRENT connections.
         *
         * This loop used to serve one connection to completion before
         * accepting the next, which is fine while a client only ever holds one.
         * An OldSchool client holds both: it opens JS5, keeps it open for
         * on-demand archive loads for the whole session, and opens a second
         * socket for the game. Serially, the game connection sits in the
         * backlog forever — the client reaches its login screen (JS5 answered)
         * and then hangs on login with no error at either end, because nothing
         * ever accepts it.
         *
         * Peeking the first byte says which this is (15 JS5, 14 game). A JS5
         * connection is handed to a forked child: it shares nothing mutable —
         * read-only cache file handles and a computed master index — so a
         * process is a legitimate way to hold it, and it costs none of the
         * restructuring a multi-session select loop would. (Windows has no
         * fork(); js5_thread_main above is the substitute, and heap-allocates
         * its own state for the same reason a forked child gets its own copy.)
         */
        if( recv(fd, (char*)&first, 1, MSG_PEEK) == 1 && first == 15 )
        {
#ifdef _WIN32
            struct Mock230Js5Args* args = (struct Mock230Js5Args*)calloc(1, sizeof(*args));
            HANDLE thread;
            assert(args);
            args->wire = wire;
            args->fd = fd;
            thread = (HANDLE)_beginthreadex(NULL, 0, js5_thread_main, args, 0, NULL);
            if( thread )
                CloseHandle(thread); /* detached: nothing joins a JS5 connection */
            else
            {
                /* Not an allocation failure: the thread limit is a real
                 * runtime condition, and the connection is dropped. */
                close(fd);
                free(args);
            }
#else
            pid_t child = fork();
            if( child == 0 )
            {
                close(listener);
                fprintf(stderr, "mock230: JS5 connection\n");
                srv.wire = wire;
                if( mock230_conn_open(&conn, fd) )
                    serve_js5(&srv, &conn);
                close(fd);
                _exit(0);
            }
            close(fd);
            if( child < 0 )
                perror("fork");
#endif
            continue;
        }

        fprintf(stderr, "mock230: client connected\n");
        if( mock230_conn_open(&conn, fd) )
            serve(&srv, &conn, &config, wire);
        close(fd);
        fprintf(stderr, "mock230: client disconnected\n");
    }

    mock230_boot_free();
    return 0;
}
