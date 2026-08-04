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
 */
#include "mock230.h"

#include "mock230_boot.h"
#include "mock230_container.h"
#include "mock230_session.h"
#include "mock230_transport.h"
#include "mock230_ws.h"

#include <arpa/inet.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

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
#include <unistd.h>

#define MOCK230_TICK_MS 600

static long
now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long)tv.tv_sec * 1000L) + (tv.tv_usec / 1000L);
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
        return errno == EINTR ? 0 : -1;
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
     */
    signal(SIGPIPE, SIG_IGN);

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
        listener = socket(AF_INET, SOCK_STREAM, 0);
        if( listener < 0 )
        {
            perror("socket");
            return 1;
        }
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((uint16_t)port);
        if( bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
        {
            perror("bind");
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

    /* Listening since before the loaders ran; this is where it starts accepting. */
    fprintf(stderr,
            "mock230: listening on 127.0.0.1:%d, wire %s (home %d,%d — zone %d,%d)\n",
            port, wire->name, config.home_x, config.home_z,
            mock230_boot_zone(config.home_x), mock230_boot_zone(config.home_z));

    for( ;; )
    {
        int fd = accept(listener, NULL, NULL);
        if( fd < 0 )
            continue;
        fprintf(stderr, "mock230: client connected\n");
        if( mock230_conn_open(&conn, fd) )
            serve(&srv, &conn, &config, wire);
        close(fd);
        fprintf(stderr, "mock230: client disconnected\n");
    }

    mock230_boot_free();
    return 0;
}
