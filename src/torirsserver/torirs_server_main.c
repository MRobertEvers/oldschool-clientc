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
 *   TORIRSSERVER_CACHE=dir   cache to read obj metadata from (default cache.osrs241.baked)
 *   TORIRSSERVER_CONTENT=dir content tree (default OSRS-Content/osrs-content)
 *   TORIRSSERVER_SCRIPTS=dir compiled script pack (default <content>/scripts/build)
 *   TORIRSSERVER_HOME=x,z    tile to log in on (default 3222,3218 — Lumbridge castle
 *                       courtyard, beside Hans; the scene's origin zone is
 *                       derived from it)
 *   TORIRSSERVER_STAFF_LEVEL=0..3 advertise rev239 staff privilege; isolated UI
 *                       harnesses use 2 so typed ::commands take the golden
 *                       CLIENT_CHEAT path (default 0)
 *   TORIRSSERVER_DEV=1   development mode: logins skip the password check
 *                       (also `--dev`; 0 turns the check back on in the
 *                       dev_torirsserver build). See torirs_server_auth.h.
 *   TORIRSSERVER_GATE=1  behind io_server's sign-in gate (also `--gate`):
 *                       WebSockets need its ticket, a web login made from
 *                       an invite (`--account invite`). torirs_server_auth.h.
 */
#include "mem/torirs_mem.h"
#include "mem/torirs_mem_libs.h"
#include "torirs_server.h"

#include "torirs_server_auth.h"
#include "torirs_server_boot.h"
#include "torirs_server_container.h"
#include "torirs_server_host.h"
#include "torirs_server_save.h"
#include "torirs_server_session.h"
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

/* Development mode -- logins skip the password check -- unless asked
 * otherwise. The `torirsserver-dev`, `-alt` and `-bank` builds define it to 1;
 * a world others log into is the default binary, which checks. */
#ifndef TORIRSSERVER_DEFAULT_DEV
#define TORIRSSERVER_DEFAULT_DEV 0
#endif

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

/*
 * Everything a stop signal is allowed to do.
 *
 * A handler may only touch async-signal-safe things, and writing a save is
 * about as far from that as this program gets — it allocates, opens files and
 * walks the bank. So this sets the flag and returns, and the loop does the
 * work. See ToriRSServer_HostRequestShutdown.
 */
static void
on_stop_signal(int sig)
{
    (void)sig;
    ToriRSServer_HostRequestShutdown();
}

/*
 * `torirsserver --account ...`: the operator's side of the sign-in gate
 * (torirs_server_auth.h). Touches only `<saves>/accounts/`, so it runs beside
 * a live world without loading one.
 *
 *   --account invite [name]           print a one-time invite to make a web
 *                                     login for the gate; with a name only
 *                                     that name can redeem it
 *   --account admin <name> on|off     set or clear a PLAYER account's admin
 *                                     flag (staff: jmod crown, cheats); it
 *                                     takes effect at the next login
 *
 * TORIRSSERVER_GATE_URL (e.g. https://torirs.example.com) makes the invite a
 * whole link.
 */
static int
account_command(
    int argc,
    char** argv)
{
    if( argc >= 3 && argc <= 4 && strcmp(argv[2], "invite") == 0 )
    {
        char code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1];
        const char* name = argc == 4 ? argv[3] : NULL;
        const char* base = getenv("TORIRSSERVER_GATE_URL");

        if( name && ToriRSServer_AuthNameProblem(name) )
        {
            fprintf(stderr, "torirsserver: '%s': %s\n", name, ToriRSServer_AuthNameProblem(name));
            return 1;
        }
        if( !ToriRSServer_InviteCreate(name, code) )
            return 1;
        printf("%s/invite/%s\n", base ? base : "", code);
        fprintf(stderr, "torirsserver: web login invite for %s, in %s/gate/invites/\n",
                name ? name : "a name of the visitor's choosing", ToriRSServer_SaveDir());
        return 0;
    }
    if( argc == 5 && strcmp(argv[2], "admin") == 0 &&
        (strcmp(argv[4], "on") == 0 || strcmp(argv[4], "off") == 0) )
    {
        int admin = strcmp(argv[4], "on") == 0;

        if( !ToriRSServer_AuthSetAdmin(argv[3], admin) )
        {
            fprintf(stderr, "torirsserver: '%s' has no usable account record (%s)\n", argv[3],
                    ToriRSServer_AccountPath(argv[3]));
            return 1;
        }
        fprintf(stderr, "torirsserver: '%s' admin %s\n", argv[3], admin ? "on" : "off");
        return 0;
    }
    return 2;
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
    /* --botrun: a party of sessionless bots driven by an agent process, no
     * socket (torirs_server_botrun.c). Also before the port parse. */
    int botrun = argc > 1 && strcmp(argv[1], "--botrun") == 0;
    /* --scriptrun: the same sessionless bots, each running a quest-driver test
     * script on a Lua state inside this process (torirs_server_scriptrun.c). */
    int scriptrun = argc > 1 && strcmp(argv[1], "--scriptrun") == 0;
    if( argc > 1 && strcmp(argv[1], "--account") == 0 )
    {
        int status = account_command(argc, argv);

        if( status == 2 )
            fprintf(stderr, "usage: %s --account invite [name]\n"
                            "       %s --account admin <name> on|off\n",
                    argv[0], argv[0]);
        return status;
    }
    /* A leading flag is not a port: `torirsserver --dev` must not atoi to 0
     * and bind wherever the kernel likes. */
    int port = (argc > 1 && strncmp(argv[1], "--", 2) != 0) ? atoi(argv[1]) : TORIRSSERVER_DEFAULT_PORT;
    int listener = -1;
    int reuse = 1;
    /* What the startup line reports, so it names where the socket actually
     * went rather than where it used to go. @see TORIRSSERVER_BIND below. */
    char const* bind_host = "127.0.0.1";
    int status;
    struct sockaddr_in addr;
    /*
     * Which revision's bytes to write. `--rev <name>` beats TORIRSSERVER_REV beats
     * the default, which is osrs230 — so every existing manifest, script and
     * test keeps its behaviour by saying nothing. See torirs_server_wire.h.
     */
    char const* rev_name = getenv("TORIRSSERVER_REV");
    const struct ToriRSServerWire* wire;

    /* Before anything allocates: the arena mode and site tracking fix every
     * block's layout (src/mem/torirs_mem.h). */
    ToriMem_Init();
    /* Every library the server links allocates from its lib:* group; server
     * code that frees rscache's output names lib:rscache
     * (src/mem/torirs_mem_libs.h). */
    ToriMem_InstallBZip();
    ToriMem_InstallRSAreaBuf();
    ToriMem_InstallRSProt();
    ToriMem_InstallRSCache();

    for( int i = 1; i < argc - 1; i++ )
        if( strcmp(argv[i], "--rev") == 0 )
            rev_name = argv[i + 1];

    wire = rev_name ? ToriRSServer_WireByName(rev_name) : ToriRSServer_WireDefault();
    if( !wire )
    {
        fprintf(stderr, "torirsserver: unknown --rev '%s' (osrs230, osrs239, osrs241)\n", rev_name);
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

    /*
     * A stop must save the people who are online.
     *
     * The only path that writes a player save is a logout, so until this was
     * here every SIGTERM threw away whatever every logged-in player had done
     * since they arrived — a container restart, a `docker stop`, a supervisor
     * swap, Ctrl-C. The handler does nothing but set a flag;
     * `ToriRSServer_HostRun` sees it at the top of its next pass and runs the
     * real logout for every connection, which is what saves them.
     *
     * SIGINT is in for Ctrl-C at a terminal. SIGHUP is not: it arrives when a
     * terminal goes away, which for a server started from an ssh session is
     * routine and is not a request to stop.
     */
    signal(SIGTERM, on_stop_signal);
    signal(SIGINT, on_stop_signal);

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
    if( !selftest && !botrun && !scriptrun )
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
        /*
         * Loopback by default, and that default is deliberate: this is a
         * development server first -- one that checks passwords only outside
         * development mode -- and one that silently accepted connections from
         * the network the moment it started would be a much worse default
         * than one that has to be asked.
         *
         * TORIRSSERVER_BIND asks. It exists because "the client and the server
         * are on the same machine" stopped being true: the Android lane
         * (docs/android_architecture.md) runs the client on a phone, which
         * cannot reach another host's loopback by any means. Without this the
         * only ways to connect a real device were a USB port-forward or a TCP
         * relay -- both of which are workarounds for a one-line limitation.
         *
         *   TORIRSSERVER_BIND=0.0.0.0     every interface
         *   TORIRSSERVER_BIND=192.168.1.5 one interface
         *   unset                          127.0.0.1, as before
         */
        {
            char const* bind_env = getenv("TORIRSSERVER_BIND");
            if( bind_env && bind_env[0] )
            {
                if( strcmp(bind_env, "0.0.0.0") == 0 )
                {
                    addr.sin_addr.s_addr = htonl(INADDR_ANY);
                }
                else if( inet_pton(AF_INET, bind_env, &addr.sin_addr) != 1 )
                {
                    /* A misspelled address must not silently fall back to
                     * loopback: the whole point of setting it is to be
                     * reachable, and a server listening somewhere other than
                     * where it was told is the failure this refuses to hide. */
                    fprintf(stderr,
                            "torirsserver: TORIRSSERVER_BIND='%s' is not an IPv4 address\n",
                            bind_env);
#ifdef _WIN32
                    WSACleanup();
#endif
                    return 1;
                }
                bind_host = bind_env;
            }
            else
            {
                addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            }
        }
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

    /* No server pack, or a stale one: the message names the command that
     * builds it, and the world does not start on half a content load. */
    if( ToriRSServer_BootLoad(&config) == TORIRSSERVER_BOOT_NO_PACK )
    {
        ToriRSServer_BootFree();
        return 1;
    }

    if( selftest )
    {
        int failures = ToriRSServer_WorldSelftest();
        ToriRSServer_BootFree();
        ToriMem_Shutdown();
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
    {
        /* `--dev` beats TORIRSSERVER_DEV beats the build's default. */
        char const* dev_env = getenv("TORIRSSERVER_DEV");
        int dev = TORIRSSERVER_DEFAULT_DEV;

        if( dev_env && dev_env[0] )
            dev = ToriRSServer_FlagDefaultOn("TORIRSSERVER_DEV");
        for( int i = 1; i < argc; i++ )
            if( strcmp(argv[i], "--dev") == 0 )
                dev = 1;
        srv.check_passwords = !dev;
        if( !selftest && !botrun && !scriptrun )
            fprintf(stderr, "torirsserver: %s\n",
                    dev ? "development mode: logins do not check passwords"
                        : "logins check passwords (accounts in <saves>/accounts/)");
    }
    {
        /* `--gate` or TORIRSSERVER_GATE=1: behind io_server's sign-in gate
         * (torirs_server_auth.h). Meaningless without passwords -- the gate's
         * ticket names an account, and a development server has none -- so the
         * two together are refused rather than half-honoured. */
        char const* gate_env = getenv("TORIRSSERVER_GATE");
        int gate = gate_env && gate_env[0] && strcmp(gate_env, "0") != 0;

        for( int i = 1; i < argc; i++ )
            if( strcmp(argv[i], "--gate") == 0 )
                gate = 1;
        if( gate && !selftest && !botrun && !scriptrun )
        {
            if( !srv.check_passwords )
            {
                fprintf(stderr, "torirsserver: --gate needs passwords; drop --dev / TORIRSSERVER_DEV\n");
                return 1;
            }
            ToriRSServer_GateKeyLoad();
            srv.gate = 1;
            fprintf(stderr, "torirsserver: behind the sign-in gate: WebSockets need its cookie, "
                            "raw TCP is refused, only staff may cheat\n");
        }
    }
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

    if( botrun )
    {
        status = ToriRSServer_BotRun(&srv, &config, argc, argv);
        /* The run's players are never logged out; their banks and containers
         * (and the seeded shops) go here, as the socket host's do below. */
        ToriRSServer_BankShutdown(&srv);
        ToriRSServer_ContainerShutdown(&srv);
        ToriRSServer_ScriptsFree(&srv);
        ToriRSServer_WorldShutdown(&srv);
        ToriRSServer_BootFree();
        ToriMem_Shutdown();
        return status;
    }
    if( scriptrun )
    {
        status = ToriRSServer_ScriptRun(&srv, &config, argc, argv);
        /* The run's players are never logged out; their banks and containers
         * (and the seeded shops) go here, as the socket host's do below. */
        ToriRSServer_BankShutdown(&srv);
        ToriRSServer_ContainerShutdown(&srv);
        ToriRSServer_ScriptsFree(&srv);
        ToriRSServer_WorldShutdown(&srv);
        ToriRSServer_BootFree();
        ToriMem_Shutdown();
        return status;
    }

    /* Listening since before the loaders ran; this is where it starts accepting. */
    fprintf(stderr,
            "torirsserver: listening on %s:%d, wire %s (home %d,%d — zone %d,%d), "
            "up to %d players\n",
            bind_host, port, wire->name, config.home_x, config.home_z,
            ToriRSServer_BootZone(config.home_x), ToriRSServer_BootZone(config.home_z),
            TORIRSSERVER_PLAYER_MAX);

    status = ToriRSServer_HostRun(&srv, &config, listener);

    ToriRSServer_BankShutdown(&srv);
    ToriRSServer_ContainerShutdown(&srv);
    ToriRSServer_ScriptsFree(&srv);
    ToriRSServer_WorldShutdown(&srv);
    ToriRSServer_SessionJs5Release();
    ToriRSServer_BootFree();
    close(listener);
#ifdef _WIN32
    WSACleanup();
#endif
    ToriMem_Shutdown();
    return status;
}
