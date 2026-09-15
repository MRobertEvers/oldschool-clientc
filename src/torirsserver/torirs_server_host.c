/*
 * The poll loop, the connection table and the tick-boundary work queue.
 * See torirs_server_host.h for why the socket server stopped serving one
 * connection at a time.
 */
#include "torirs_server_host.h"
#include <assert.h>

#include "torirs_server.h"
#include "torirs_server_boot.h"
#include "torirs_server_session.h"
#include "torirs_server_transport.h"
#include "torirs_server_ws.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TORIRSSERVER_TICK_MS 600

/* ------------------------------------------------------------------ */
/* Connections                                                         */
/* ------------------------------------------------------------------ */

enum ToriRSServerHostConnState
{
    /** Nothing here. */
    TORIRSSERVER_HOST_CONN_FREE = 0,
    /**
     * Accepted, but the stream has not said what it is yet: the first byte has
     * not arrived, or a WebSocket upgrade is still being read. No session
     * exists, so nothing may be sent or decoded.
     */
    TORIRSSERVER_HOST_CONN_OPENING,
    /** A session owns it — a game login, or JS5. */
    TORIRSSERVER_HOST_CONN_LIVE,
    /**
     * Dead, and waiting for the tick boundary to be torn down. It stays out of
     * the poll set and out of every service pass from here; what it is still
     * doing in the table is holding the player pointer that the queued logout
     * job needs.
     */
    TORIRSSERVER_HOST_CONN_CLOSING,
};

struct ToriRSServerHostConn
{
    enum ToriRSServerHostConnState state;
    int fd;
    struct ToriRSServerConn conn;
    struct ToriRSServerSession session;
    struct ToriRSServerTransport transport;

    /**
     * The handshake completed and a login job is queued.
     *
     * While this is set the connection is not polled and not pumped, which is
     * not an optimisation: the session stops decoding the moment the stream
     * arms (`login_raised`), so anything the client sent behind its login
     * block must not be dispatched until there is a player to dispatch it to.
     * Leaving the descriptor in the read set instead would spin on bytes this
     * pass has already decided not to read.
     */
    int awaiting_login;
};

/* ------------------------------------------------------------------ */
/* Work queue                                                          */
/* ------------------------------------------------------------------ */

enum ToriRSServerHostJobKind
{
    /** The stream armed: build the world if it is the first, and add a player. */
    TORIRSSERVER_HOST_JOB_LOGIN,
    /** The peer is gone: remove the player, then release the slot. */
    TORIRSSERVER_HOST_JOB_LOGOUT,
};

struct ToriRSServerHostJob
{
    enum ToriRSServerHostJobKind kind;
    int conn;
};

struct ToriRSServerHost
{
    struct ToriRSServer* srv;
    const struct ToriRSServerBootConfig* config;
    int listener;

    struct ToriRSServerHostConn conns[TORIRSSERVER_HOST_CONN_MAX];

    /*
     * Sized so it cannot overflow rather than sized by guess: a connection can
     * have at most one login and one logout outstanding at a time, and both are
     * cleared by the drain that runs before any further work is discovered.
     */
    struct ToriRSServerHostJob jobs[2 * TORIRSSERVER_HOST_CONN_MAX];
    int job_head;
    int job_count;

    long next_tick;
};

static void
host_job_push(
    struct ToriRSServerHost* host,
    enum ToriRSServerHostJobKind kind,
    int conn)
{
    struct ToriRSServerHostJob* job;

    assert(host);
    assert(conn >= 0);
    assert(conn < TORIRSSERVER_HOST_CONN_MAX);
    /* Not a runtime condition — see the sizing note on `jobs`. A full queue
     * means a connection queued the same job twice, which is a bug in the
     * caller and not a load level. */
    assert(host->job_count < (int)(sizeof(host->jobs) / sizeof(host->jobs[0])));

    job = &host->jobs[(host->job_head + host->job_count) %
                      (int)(sizeof(host->jobs) / sizeof(host->jobs[0]))];
    job->kind = kind;
    job->conn = conn;
    host->job_count++;
}

static int
host_job_pop(
    struct ToriRSServerHost* host,
    struct ToriRSServerHostJob* out)
{
    assert(host);
    assert(out);
    if( host->job_count == 0 )
        return 0;
    *out = host->jobs[host->job_head];
    host->job_head = (host->job_head + 1) %
                     (int)(sizeof(host->jobs) / sizeof(host->jobs[0]));
    host->job_count--;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Clock                                                               */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Connection lifecycle                                                */
/* ------------------------------------------------------------------ */

/*
 * Take a connection out of service and queue its teardown.
 *
 * Nothing is freed here, and that is the point: this is reached from inside a
 * pump, from a failed flush, and from the login stage, and in the first two of
 * those the world may still be holding this session's player. The tick
 * boundary is the one place where nothing is mid-flight.
 */
static void
host_conn_kill(
    struct ToriRSServerHost* host,
    int index)
{
    struct ToriRSServerHostConn* c = &host->conns[index];

    if( c->state == TORIRSSERVER_HOST_CONN_FREE ||
        c->state == TORIRSSERVER_HOST_CONN_CLOSING )
        return;
    c->state = TORIRSSERVER_HOST_CONN_CLOSING;
    ToriRSServer_SessionKill(&c->session);
    host_job_push(host, TORIRSSERVER_HOST_JOB_LOGOUT, index);
}

/* The teardown itself, from the logout stage only. */
static void
host_conn_release(
    struct ToriRSServerHost* host,
    int index)
{
    struct ToriRSServerHostConn* c = &host->conns[index];

    /*
     * The world lets go of the player while the session is still addressable,
     * because that is what makes the packets a logout generates reach everyone
     * *else* before this slot goes. The embed host does the same, in the same
     * order.
     *
     * The test for a player belongs here and not inside WorldRemovePlayer,
     * which keeps its assert: a JS5 connection never logs in, and a game
     * connection can die during the handshake or fail it, so all of those
     * arrive with nothing to remove.
     */
    if( c->session.player )
    {
        ToriRSServer_WorldRemovePlayer(host->srv, c->session.player);
        c->session.player = NULL;
    }
    ToriRSServer_SessionFree(&c->session);
    ToriRSServer_ConnFree(&c->conn);
    if( c->fd >= 0 )
        close(c->fd);
    memset(c, 0, sizeof(*c));
    c->state = TORIRSSERVER_HOST_CONN_FREE;
    c->fd = -1;
    fprintf(stderr, "torirsserver: client disconnected\n");
}

static void
host_accept(struct ToriRSServerHost* host)
{
    int nodelay = 1;
    int fd = (int)accept(host->listener, NULL, NULL);
    int index;

    if( fd < 0 )
        return;

    /*
     * Every game packet is its own send(), and a tick's output is a run of
     * small ones ending in SERVER_TICK_END. With Nagle on, everything after
     * the first unacknowledged segment waits for the peer's delayed ACK, so
     * the fence could land tens of milliseconds after the packet that opened
     * the tick — and the client's UI transaction latch (correctly) withholds
     * frames until the fence. Measured as a 2-4 logic tick frame freeze every
     * server cycle in the browser client.
     */
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

    for( index = 0; index < TORIRSSERVER_HOST_CONN_MAX; index++ )
        if( host->conns[index].state == TORIRSSERVER_HOST_CONN_FREE )
            break;
    if( index == TORIRSSERVER_HOST_CONN_MAX )
    {
        /* Closed at once rather than left in the backlog: a client that is
         * refused says so, and a client that is ignored hangs on a blank
         * screen. */
        fprintf(stderr, "torirsserver: %d connections already; refusing\n",
                TORIRSSERVER_HOST_CONN_MAX);
        close(fd);
        return;
    }

    memset(&host->conns[index], 0, sizeof(host->conns[index]));
    host->conns[index].state = TORIRSSERVER_HOST_CONN_OPENING;
    host->conns[index].fd = fd;
    /*
     * No sniff here for what the connection carries. It used to matter — 15
     * meant a JS5 download and was handed to a forked child, 14 stayed — and
     * it does not any more: the session's own state machine reads that byte
     * (step_init) and both services run in this loop. One accept path, one
     * kind of connection as far as this file is concerned.
     */
    ToriRSServer_ConnBegin(&host->conns[index].conn, fd);
    fprintf(stderr, "torirsserver: client connected (slot %d)\n", index);
}

/* ------------------------------------------------------------------ */
/* Service                                                             */
/* ------------------------------------------------------------------ */

/*
 * Advance one connection as far as the bytes it has allow.
 *
 * Called for every live connection after every wakeup rather than only for the
 * descriptors select() named, because "readable" is not the same question as
 * "has something to do": a WebSocket read takes whole frames, so a pump can
 * leave application bytes deframed and buffered while the socket has nothing
 * further to say — and the peer that sent them is waiting for the reply. A
 * recv that finds nothing costs one syscall.
 */
static void
host_service(
    struct ToriRSServerHost* host,
    int index)
{
    struct ToriRSServerHostConn* c = &host->conns[index];

    if( c->state == TORIRSSERVER_HOST_CONN_OPENING )
    {
        enum ToriRSServerConnOpen step = ToriRSServer_ConnOpenStep(&c->conn);

        if( step == TORIRSSERVER_CONN_OPENING )
            return;
        if( step == TORIRSSERVER_CONN_OPEN_FAILED )
        {
            host_conn_kill(host, index);
            return;
        }
        ToriRSServer_TransportSocket(&c->transport, &c->conn);
        ToriRSServer_SessionInit(&c->session, &c->transport, host->srv->verbose);
        c->state = TORIRSSERVER_HOST_CONN_LIVE;
    }

    if( c->state != TORIRSSERVER_HOST_CONN_LIVE || c->awaiting_login )
        return;

    if( !ToriRSServer_SessionPump(&c->session, host->srv) )
    {
        host_conn_kill(host, index);
        return;
    }

    /*
     * The handshake completed inside that pump. The world comes up in the
     * login stage rather than here, because a session carries bytes and knows
     * nothing about ticks, npcs or scripts — and because building a world and
     * running a save load is not work to do between two ticks.
     */
    if( ToriRSServer_SessionTakeLogin(&c->session) )
    {
        c->awaiting_login = 1;
        host_job_push(host, TORIRSSERVER_HOST_JOB_LOGIN, index);
    }
}

/* ------------------------------------------------------------------ */
/* Tick-boundary stages                                                */
/* ------------------------------------------------------------------ */

static void
host_stage_login(
    struct ToriRSServerHost* host,
    int index)
{
    struct ToriRSServerHostConn* c = &host->conns[index];
    struct ToriRSServerPlayer* player;

    c->awaiting_login = 0;
    /* The peer can leave between raising the login and this stage running. */
    if( c->state != TORIRSSERVER_HOST_CONN_LIVE )
        return;

    /* Both idempotent, and both are the first login's bill rather than the
     * world's: the pack is already loaded at boot, and the world is built
     * once however many players arrive. */
    ToriRSServer_ScriptsLoad(host->srv, host->config->script_dir);
    ToriRSServer_WorldInit(host->srv, ToriRSServer_BootZone(host->config->home_x),
                           ToriRSServer_BootZone(host->config->home_z));

    player = ToriRSServer_WorldAddPlayer(host->srv, &c->session);
    if( !player )
    {
        /* The world is full and said so. The connection goes rather than
         * hanging on a login screen that will never answer. */
        host_conn_kill(host, index);
        return;
    }
    c->session.player = player;
    ToriRSServer_WorldPlayerInit(player);
    ToriRSServer_WorldSetDisplayName(player, c->session.display_name);
    ToriRSServer_WorldLogin(player);
    fprintf(stderr, "torirsserver: %s logged in (pid %d)\n", player->display_name, player->pid);

    /*
     * Anything the client sent behind its login block is still in the session
     * buffer, and there is a player to dispatch it to now. Pumping here rather
     * than leaving it to the next service pass is what keeps the first packets
     * of a session in front of the tick that follows.
     */
    if( !ToriRSServer_SessionPump(&c->session, host->srv) )
        host_conn_kill(host, index);
}

/*
 * The queue, drained at the top of a tick, in arrival order.
 *
 * Order within the drain is the order things happened, not a policy: a logout
 * queued before a login is a slot released before the next client asks for
 * one, which is the only sequencing either job cares about. `WorldPlayerReap`
 * is what stops that slot being handed out inside the same tick anyway.
 *
 * A job may queue another — the login stage kills a connection it cannot seat
 * — so this drains until empty rather than draining a snapshot.
 */
static void
host_drain_queue(struct ToriRSServerHost* host)
{
    struct ToriRSServerHostJob job;

    while( host_job_pop(host, &job) )
    {
        switch( job.kind )
        {
        case TORIRSSERVER_HOST_JOB_LOGIN:
            host_stage_login(host, job.conn);
            break;
        case TORIRSSERVER_HOST_JOB_LOGOUT:
            host_conn_release(host, job.conn);
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Loop                                                                */
/* ------------------------------------------------------------------ */

int
ToriRSServer_HostRun(
    struct ToriRSServer* srv,
    const struct ToriRSServerBootConfig* config,
    int listener)
{
    struct ToriRSServerHost* host;

    assert(srv);
    assert(config);
    assert(listener >= 0);

    /* ~3 MB of per-connection buffers. Heap rather than a static, so a host
     * that is never run costs nothing, and rather than the stack for the
     * obvious reason. */
    host = (struct ToriRSServerHost*)calloc(1, sizeof(*host));
    assert(host);
    host->srv = srv;
    host->config = config;
    host->listener = listener;
    host->next_tick = now_ms() + TORIRSSERVER_TICK_MS;
    for( int i = 0; i < TORIRSSERVER_HOST_CONN_MAX; i++ )
        host->conns[i].fd = -1;

    for( ;; )
    {
        fd_set readable;
        fd_set writable;
        struct timeval timeout;
        int top = listener;
        int immediate = 0;
        long wait_ms;
        int ready;

        FD_ZERO(&readable);
        FD_ZERO(&writable);
        FD_SET(listener, &readable);

        for( int i = 0; i < TORIRSSERVER_HOST_CONN_MAX; i++ )
        {
            struct ToriRSServerHostConn* c = &host->conns[i];

            if( c->state == TORIRSSERVER_HOST_CONN_FREE ||
                c->state == TORIRSSERVER_HOST_CONN_CLOSING )
                continue;
            assert(c->fd >= 0);

            if( !c->awaiting_login )
                FD_SET(c->fd, &readable);
            /*
             * Writability is only interesting when there is something to
             * write. A descriptor left in the write set with an empty queue is
             * readable-for-write essentially always, which turns this select
             * into a spin.
             */
            if( ToriRSServer_ConnPending(&c->conn) > 0 )
                FD_SET(c->fd, &writable);
            if( c->fd > top )
                top = c->fd;

            /*
             * Bytes already in hand beat any descriptor.
             *
             * A framed transport reads whole frames, so one read can leave a
             * complete request deframed and queued while the socket has
             * nothing further to say — and the client that sent it is waiting
             * for the answer, so nothing further ever arrives. Sleeping there
             * waits out the whole timeout with the request sitting in the
             * buffer: 600 ms on a game connection, where the tick deadline
             * hides it, and unbounded on a JS5 one, where it looks exactly
             * like a server that accepted the handshake and then went silent.
             */
            if( !c->awaiting_login && ToriRSServer_ConnBuffered(&c->conn) > 0 )
                immediate = 1;
        }

        wait_ms = immediate ? 0 : host->next_tick - now_ms();
        if( wait_ms < 0 )
            wait_ms = 0;
        timeout.tv_sec = wait_ms / 1000;
        timeout.tv_usec = (wait_ms % 1000) * 1000;

        ready = select(top + 1, &readable, &writable, NULL, &timeout);
        if( ready < 0 )
        {
#ifdef _WIN32
            if( WSAGetLastError() == WSAEINTR )
                continue;
            fprintf(stderr, "torirsserver: select failed (WSA %d)\n", WSAGetLastError());
#else
            if( errno == EINTR )
                continue;
            perror("select");
#endif
            free(host);
            return 1;
        }

        if( ready > 0 && FD_ISSET(listener, &readable) )
            host_accept(host);

        /*
         * Drain what the kernel refused earlier, before servicing. A
         * connection whose queue clears is a JS5 download that stopped
         * building answers (see TORIRSSERVER_JS5_BACKLOG_MAX) and can build
         * more the moment the service pass below re-enters it.
         */
        for( int i = 0; i < TORIRSSERVER_HOST_CONN_MAX; i++ )
        {
            struct ToriRSServerHostConn* c = &host->conns[i];

            if( c->state == TORIRSSERVER_HOST_CONN_FREE ||
                c->state == TORIRSSERVER_HOST_CONN_CLOSING )
                continue;
            if( ready > 0 && FD_ISSET(c->fd, &writable) && !ToriRSServer_ConnFlush(&c->conn) )
                host_conn_kill(host, i);
        }

        for( int i = 0; i < TORIRSSERVER_HOST_CONN_MAX; i++ )
        {
            if( host->conns[i].state == TORIRSSERVER_HOST_CONN_FREE ||
                host->conns[i].state == TORIRSSERVER_HOST_CONN_CLOSING )
                continue;
            host_service(host, i);
        }

        if( now_ms() >= host->next_tick )
        {
            host_drain_queue(host);
            /*
             * No world, no tick. Until the first login there is nothing built
             * to advance — `ToriRSServer_WorldInit` has not run, so the npc
             * pool, the scene and the zone map are all the calloc's zeroes.
             * After it, the world keeps ticking whether or not anyone is on
             * it: it is the world's clock, not a session's, and npc respawns
             * and instance linger windows are owed it.
             */
            if( host->srv->world_built )
                ToriRSServer_WorldTick(host->srv);

            /* Anchor to the schedule rather than to "now", so a slow tick does
             * not push every later tick out. */
            host->next_tick += TORIRSSERVER_TICK_MS;
            if( now_ms() - host->next_tick > 5 * TORIRSSERVER_TICK_MS )
                host->next_tick = now_ms() + TORIRSSERVER_TICK_MS;
        }
    }
}
