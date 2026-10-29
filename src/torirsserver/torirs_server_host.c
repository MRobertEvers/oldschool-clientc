/*
 * The standalone server's core loop: one thread that waits on every socket,
 * resumes the coroutine that owns whichever one is ready, and runs the tick
 * when it is due. See torirs_server_host.h for why the socket server stopped
 * serving one connection at a time, and torirs_server_core.h for the
 * coroutines.
 *
 * What runs where:
 *
 *   loop thread    the wait, every coroutine resume, the tick. It never makes
 *                  a call that can block: sockets are non-blocking, and
 *                  everything else goes to the pool.
 *   pool threads   a password check (account file + PBKDF2), a login's save
 *                  read, a logout's save write. One job at a time per
 *                  account, in order (torirs_server_pool.h).
 *
 * Each connection is a coroutine (host_conn_step): it sniffs the stream, runs
 * the handshake, sends its login's password check and save read to the pool
 * and waits for them, hands the login to the tick, and from then on moves
 * bytes between the socket and the session. The world is touched only at the
 * tick boundary, by the work queue (host_drain_queue), as before.
 */
#include "torirs_server_host.h"

#include "mem/torirs_mem.h"
#include "torirs_server.h"
#include "torirs_server_accountlink.h"
#include "torirs_server_auth.h"
#include "torirs_server_boot.h"
#include "torirs_server_claim.h"
#include "torirs_server_core.h"
#include "torirs_server_poll.h"
#include "torirs_server_pool.h"
#include "torirs_server_save.h"
#include "torirs_server_savefile.h"
#include "torirs_server_session.h"
#include "torirs_server_transport.h"
#include "torirs_server_ws.h"

#include <assert.h>
#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <fcntl.h>
#include <unistd.h>
#endif

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TORIRSSERVER_TICK_MS 600

/*
 * Pool threads for blocking calls. Logins and saves of different accounts run
 * in parallel up to this many; a password hash is the long pole (tens of ms),
 * so four keep a mass re-login after a restart off the critical path without
 * competing with the loop for cores. TORIRSSERVER_POOL_THREADS overrides.
 */
#define TORIRSSERVER_HOST_POOL_THREADS 4

enum ToriRSServerHostConnState
{
    /**
     * Accepted, but the stream has not said what it is yet: the first byte has
     * not arrived, or a WebSocket upgrade is still being read. No session
     * exists, so nothing may be sent or decoded.
     */
    TORIRSSERVER_HOST_CONN_OPENING = 1,
    /** A session owns it — a game login, or JS5. */
    TORIRSSERVER_HOST_CONN_LIVE,
    /**
     * Dead, and waiting for the tick boundary to be torn down. Out of the
     * poller and never resumed again; what it is still doing in the table is
     * holding the player pointer that the queued logout job needs.
     */
    TORIRSSERVER_HOST_CONN_CLOSING,
};

/* A password check, run on a pool thread. Copies in, verdict out. */
struct ToriRSServerHostAuthJob
{
    struct ToriRSServerPoolJob job;
    char user[64];
    char pass[64];
    int result;
    int admin;
};

/* A login's save file, read on a pool thread into a plain malloc block (the
 * ToriMem groups belong to the loop thread); the coroutine copies it over. */
struct ToriRSServerHostReadJob
{
    struct ToriRSServerPoolJob job;
    char path[1024];
    uint8_t* bytes;
    long len;
};

struct ToriRSServerHost;

struct ToriRSServerHostConn
{
    /* First: a poll event's owner and a pool job's owner are this pointer. */
    struct ToriRSServerCo co;
    struct ToriRSServerHost* host;
    int index;
    enum ToriRSServerHostConnState state;
    int fd;
    /** Watched for reading / writing right now (mirrors the poller). */
    int watching_read;
    int watching_write;
    /**
     * The handshake completed and a login job is queued.
     *
     * While this is set the connection is not read, which is not an
     * optimisation: the session stops decoding the moment the stream arms
     * (`login_raised`), so anything the client sent behind its login block
     * must not be dispatched until there is a player to dispatch it to.
     */
    int awaiting_login;
    /** Set by the login stage when the save read for this login is older than
     *  a save of the same account the world has written since: read again. */
    int refetch;
    /** `host->save_seq` when this login's save read was submitted. */
    uint64_t read_seq;
    struct ToriRSServerHostAuthJob auth;
    struct ToriRSServerHostReadJob read;
    struct ToriRSServerConn conn;
    struct ToriRSServerSession session;
    struct ToriRSServerTransport transport;
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

/*
 * Connections held at once, game and JS5 together: two per player (a client
 * keeps its JS5 connection open beside its game one) and room for clients
 * still priming their cache or being refused. The table holds pointers; a
 * connection's buffers are allocated when it is accepted.
 */
#define TORIRSSERVER_HOST_CONN_CAP (2 * TORIRSSERVER_PLAYER_MAX + 64)

/* Saves of one account remembered for staleness checks, by key modulo this.
 * A collision only costs a login a second read. */
#define TORIRSSERVER_HOST_SAVE_SLOTS 4096

struct ToriRSServerHost
{
    struct ToriRSServer* srv;
    const struct ToriRSServerBootConfig* config;
    int listener;
    struct ToriRSServerCore core;
    /* The listener's own coroutine: an accept per readiness. */
    struct ToriRSServerCo accept_co;

    struct ToriRSServerHostConn* conns[TORIRSSERVER_HOST_CONN_CAP];
    int conn_count;

    /*
     * Sized so it cannot overflow rather than sized by guess: a connection can
     * have at most one login and one logout outstanding at a time, and both are
     * cleared by the drain that runs before any further work is discovered.
     */
    struct ToriRSServerHostJob jobs[2 * TORIRSSERVER_HOST_CONN_CAP];
    int job_head;
    int job_count;

    long next_tick;

    /*
     * Every save handed to the pool bumps `save_seq` and stamps its account's
     * slot. A login whose save read was submitted before its account's slot
     * moved read a file a newer save is replacing (a session that took over
     * its own character, a logout that crossed a login), and reads again.
     */
    uint64_t save_seq;
    uint64_t save_stamp[TORIRSSERVER_HOST_SAVE_SLOTS];
    int saves_in_flight;

    /* Recently ended sessions, for a GAMERECONNECT to reclaim. */
    struct ToriRSServerClaims claims;
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
    assert(conn < TORIRSSERVER_HOST_CONN_CAP);
    /* Not a runtime condition — see the sizing note on `jobs`. A full queue
     * means a connection queued the same job twice, which is a bug in the
     * caller and not a load level. */
    assert(host->job_count < (int)(sizeof(host->jobs) / sizeof(host->jobs[0])));

    job = &host->jobs
               [(host->job_head + host->job_count) %
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
    host->job_head = (host->job_head + 1) % (int)(sizeof(host->jobs) / sizeof(host->jobs[0]));
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
/* Accounts                                                            */
/* ------------------------------------------------------------------ */

/* The pool's ordering key for a name as the login or the save states it. Both
 * go through the save file's own name, so "Bob" and "bob" share a turn. */
static uint32_t
host_account_key(const char* name)
{
    char file[64];

    assert(name);
    ToriRSServer_SaveFileName(name, file, sizeof(file));
    return ToriRSServer_PoolKey(file);
}

/* ------------------------------------------------------------------ */
/* Pool jobs                                                           */
/* ------------------------------------------------------------------ */

static void
host_auth_run(struct ToriRSServerPoolJob* job)
{
    struct ToriRSServerHostAuthJob* auth = (struct ToriRSServerHostAuthJob*)job;

    auth->result = (int)ToriRSServer_AuthLogin(auth->user, auth->pass);
    memset(auth->pass, 0, sizeof(auth->pass));
    auth->admin =
        (auth->result == TORIRSSERVER_AUTH_OK || auth->result == TORIRSSERVER_AUTH_CLAIMED) &&
        ToriRSServer_AuthIsAdmin(auth->user);
}

static void
host_read_run(struct ToriRSServerPoolJob* job)
{
    struct ToriRSServerHostReadJob* read = (struct ToriRSServerHostReadJob*)job;
    FILE* file;

    read->bytes = NULL;
    read->len = 0;
    file = fopen(read->path, "rb");
    if( !file )
        return; /* a new character, not an error */
    if( fseek(file, 0, SEEK_END) == 0 )
        read->len = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( read->len > 0 )
    {
        read->bytes = (uint8_t*)malloc((size_t)read->len);
        assert(read->bytes);
        if( fread(read->bytes, 1, (size_t)read->len, file) != (size_t)read->len )
        {
            fprintf(stderr, "torirsserver: could not read %s\n", read->path);
            free(read->bytes);
            read->bytes = NULL;
            read->len = 0;
        }
    }
    else
        read->len = 0;
    fclose(file);
}

/* A logout's save, written on a pool thread. */
struct ToriRSServerHostSaveJob
{
    struct ToriRSServerCoreJob core; /* first */
    struct ToriRSServerHost* host;
    char path[1024];
    char* text;
    int ok;
};

static void
host_save_run(struct ToriRSServerPoolJob* job)
{
    struct ToriRSServerHostSaveJob* save = (struct ToriRSServerHostSaveJob*)job;

    save->ok = ToriRSServer_SaveWriteText(save->path, save->text);
}

static void
host_save_done(
    struct ToriRSServerCore* core,
    struct ToriRSServerPoolJob* job)
{
    struct ToriRSServerHostSaveJob* save = (struct ToriRSServerHostSaveJob*)job;

    (void)core;
    if( !save->ok )
        fprintf(stderr, "torirsserver: SAVE FAILED for %s\n", save->path);
    save->host->saves_in_flight--;
    ToriMem_Free(ToriMem_ServerPlayers(), save->text);
    ToriMem_Free(ToriMem_ServerNet(), save);
}

/* ToriRSServerSaveSink: the world composed a save on the tick; write it on
 * the pool, after anything else of this account's already queued there. */
static void
host_save_sink(
    void* ctx,
    const char* account,
    const char* path,
    char* text,
    size_t len)
{
    struct ToriRSServerHost* host = (struct ToriRSServerHost*)ctx;
    struct ToriRSServerHostSaveJob* save;
    uint32_t key;

    (void)len;
    assert(host);
    assert(account);
    assert(path);
    assert(text);
    save = ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*save));
    assert(save);
    key = host_account_key(account);
    save->host = host;
    save->text = text;
    snprintf(save->path, sizeof(save->path), "%s", path);
    save->core.job.fn = host_save_run;
    save->core.job.key = key;
    host->save_seq++;
    host->save_stamp[key % TORIRSSERVER_HOST_SAVE_SLOTS] = host->save_seq;
    host->saves_in_flight++;
    ToriRSServer_CoreSubmitDetached(&host->core, &save->core, host_save_done);
}

/* ------------------------------------------------------------------ */
/* Connection lifecycle                                                */
/* ------------------------------------------------------------------ */

static void
host_watch(
    struct ToriRSServerHostConn* c,
    int read,
    int write)
{
    struct ToriRSServerPoll* poll = c->host->core.poll;

    if( read && !c->watching_read )
    {
        ToriRSServer_PollAdd(poll, c->fd, &c->co);
        c->watching_read = 1;
        c->watching_write = 0;
    }
    else if( !read && c->watching_read )
    {
        ToriRSServer_PollRemove(poll, c->fd);
        c->watching_read = 0;
        c->watching_write = 0;
    }
    /* Writability rides on the read registration: only worth asking while
     * there is something queued, or the wait turns into a spin. */
    write = write && c->watching_read;
    if( write != c->watching_write )
    {
        ToriRSServer_PollWantWrite(poll, c->fd, &c->co, write);
        c->watching_write = write;
    }
}

/*
 * Take a connection out of service and queue its teardown.
 *
 * Nothing is freed here, and that is the point: this is reached from inside a
 * resume, from a failed flush, and from the login stage, and in the first two
 * of those the world may still be holding this session's player. The tick
 * boundary is the one place where nothing is mid-flight.
 */
static void
host_conn_kill(
    struct ToriRSServerHost* host,
    struct ToriRSServerHostConn* c)
{
    if( c->state == TORIRSSERVER_HOST_CONN_CLOSING )
        return;
    c->state = TORIRSSERVER_HOST_CONN_CLOSING;
    host_watch(c, 0, 0);
    ToriRSServer_SessionKill(&c->session);
    host_job_push(host, TORIRSSERVER_HOST_JOB_LOGOUT, c->index);
}

static void
host_conn_free(struct ToriRSServerCo* co)
{
    struct ToriRSServerHostConn* c = (struct ToriRSServerHostConn*)co;

    /* A job that finished after its connection went: whatever it produced is
     * not anyone's now. */
    free(c->read.bytes);
    memset(c->auth.pass, 0, sizeof(c->auth.pass));
    ToriMem_Free(ToriMem_ServerNet(), c);
}

/* The teardown itself, from the logout stage only. */
static void
host_conn_release(
    struct ToriRSServerHost* host,
    int index)
{
    struct ToriRSServerHostConn* c = host->conns[index];

    assert(c);
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
        ToriRSServer_ClaimNoteDeparted(&host->claims, c->session.player, now_ms());
        ToriRSServer_WorldRemovePlayer(host->srv, c->session.player);
        c->session.player = NULL;
    }
    ToriRSServer_SessionFree(&c->session);
    ToriRSServer_ConnFree(&c->conn);
    if( c->fd >= 0 )
        close(c->fd);
    c->fd = -1;
    host->conns[index] = NULL;
    host->conn_count--;
    /* A coroutine waiting on its password check or save read is freed when
     * that job comes back (the job's memory is inside it). */
    if( ToriRSServer_CoreDrop(&host->core, &c->co, host_conn_free) )
        host_conn_free(&c->co);
    fprintf(stderr, "torirsserver: client disconnected\n");
}

/*
 * One connection's whole life. A coroutine (torirs_server_core.h): resumed
 * when its socket has something, when a job it waits on is done, or when the
 * tick has output for it. No `switch` in here (CLAUDE.md).
 */
static int
host_conn_step(
    struct ToriRSServerCore* core,
    struct ToriRSServerCo* co)
{
    struct ToriRSServerHostConn* c = (struct ToriRSServerHostConn*)co;
    struct ToriRSServerHost* host = c->host;

    PT_BEGIN(&co->pt);

    /* The stream says what it is: raw TCP, or a WebSocket upgrade to finish. */
    for( ;; )
    {
        enum ToriRSServerConnOpen open = ToriRSServer_ConnOpenStep(&c->conn);

        if( open == TORIRSSERVER_CONN_OPEN_FAILED )
        {
            host_conn_kill(host, c);
            PT_EXIT(&co->pt);
        }
        if( open != TORIRSSERVER_CONN_OPENING )
            break;
        host_watch(c, 1, ToriRSServer_ConnPending(&c->conn) > 0);
        PT_YIELD(&co->pt);
    }
    ToriRSServer_TransportSocket(&c->transport, &c->conn);
    ToriRSServer_SessionInit(&c->session, &c->transport, host->srv->verbose);
    c->session.auth_async = 1;
    c->state = TORIRSSERVER_HOST_CONN_LIVE;

    for( ;; )
    {
        /* What the tick or a reply queued goes first. A JS5 download that
         * stopped building answers at its backlog (TORIRSSERVER_JS5_BACKLOG_MAX)
         * builds more in the pump right after. */
        if( ToriRSServer_ConnPending(&c->conn) > 0 && !ToriRSServer_ConnFlush(&c->conn) )
        {
            host_conn_kill(host, c);
            PT_EXIT(&co->pt);
        }
        if( !ToriRSServer_SessionPump(&c->session, host->srv) )
        {
            /* A refused login's answer is already queued; give it one try. */
            ToriRSServer_ConnFlush(&c->conn);
            host_conn_kill(host, c);
            PT_EXIT(&co->pt);
        }

        if( c->session.state == TORIRSSERVER_SESSION_AUTH )
        {
            /* The password check, off the loop. Not read meanwhile: the
             * session decodes nothing until it has the answer. */
            host_watch(c, 0, 0);
            snprintf(c->auth.user, sizeof(c->auth.user), "%s", c->session.auth_user);
            memcpy(c->auth.pass, c->session.auth_pass, sizeof(c->auth.pass));
            memset(c->session.auth_pass, 0, sizeof(c->session.auth_pass));
            c->auth.job.fn = host_auth_run;
            c->auth.job.key = host_account_key(c->auth.user);
            TORIRSSERVER_CO_AWAIT_JOB(core, co, &c->auth.job);
            ToriRSServer_SessionAuthDone(&c->session, host->srv, c->auth.result, c->auth.admin);
            host_watch(c, 1, 0);
            continue;
        }

        if( ToriRSServer_SessionTakeLogin(&c->session) )
        {
            /* The save, read off the loop: queued behind any save of this
             * account still being written, so it is the newest. The login
             * stage checks that nothing newer was queued after it. */
            host_watch(c, 0, 0);
            do
            {
                c->refetch = 0;
                snprintf(
                    c->read.path,
                    sizeof(c->read.path),
                    "%s",
                    ToriRSServer_SavePath(c->session.display_name));
                c->read.job.fn = host_read_run;
                c->read.job.key = host_account_key(c->session.display_name);
                c->read_seq = host->save_seq;
                TORIRSSERVER_CO_AWAIT_JOB(core, co, &c->read.job);
                ToriMem_Free(ToriMem_ServerPlayers(), c->session.save_bytes);
                c->session.save_bytes = NULL;
                c->session.save_len = c->read.len;
                if( c->read.len > 0 )
                {
                    c->session.save_bytes =
                        (uint8_t*)ToriMem_Alloc(ToriMem_ServerPlayers(), (size_t)c->read.len);
                    assert(c->session.save_bytes);
                    memcpy(c->session.save_bytes, c->read.bytes, (size_t)c->read.len);
                }
                free(c->read.bytes);
                c->read.bytes = NULL;
                c->session.save_prefetched = 1;

                /* The world comes up in the login stage rather than here,
                 * because a session carries bytes and knows nothing about
                 * ticks, npcs or scripts. */
                c->awaiting_login = 1;
                host_job_push(host, TORIRSSERVER_HOST_JOB_LOGIN, c->index);
                PT_WAIT_UNTIL(&co->pt, !c->awaiting_login);
            } while( c->refetch );
            if( c->state != TORIRSSERVER_HOST_CONN_LIVE )
                PT_EXIT(&co->pt);
            host_watch(c, 1, 0);
            continue;
        }

        host_watch(c, 1, ToriRSServer_ConnPending(&c->conn) > 0);
        /*
         * Bytes already in hand beat any descriptor. A framed transport reads
         * whole frames, so one read can leave a complete request deframed
         * and queued while the socket has nothing further to say -- and the
         * client that sent it is waiting for the answer, so nothing further
         * ever arrives. Without this the request would wait out the tick.
         */
        if( ToriRSServer_ConnBuffered(&c->conn) > 0 )
            ToriRSServer_CoreReady(core, co);
        PT_YIELD(&co->pt);
    }
    PT_END(&co->pt);
}

static void
host_accept_one(struct ToriRSServerHost* host)
{
    int nodelay = 1;
    int fd = (int)accept(host->listener, NULL, NULL);
    int index;
    struct ToriRSServerHostConn* c;

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

    for( index = 0; index < TORIRSSERVER_HOST_CONN_CAP; index++ )
        if( !host->conns[index] )
            break;
    if( index == TORIRSSERVER_HOST_CONN_CAP )
    {
        /* Closed at once rather than left in the backlog: a client that is
         * refused says so, and a client that is ignored hangs on a blank
         * screen. */
        fprintf(
            stderr, "torirsserver: %d connections already; refusing\n", TORIRSSERVER_HOST_CONN_CAP);
        close(fd);
        return;
    }

    /* ~150 KB of buffers, held only while the connection is. */
    c = ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*c));
    assert(c);
    ToriRSServer_CoInit(&c->co, host_conn_step);
    c->host = host;
    c->index = index;
    c->state = TORIRSSERVER_HOST_CONN_OPENING;
    c->fd = fd;
    host->conns[index] = c;
    host->conn_count++;
    /*
     * No sniff here for what the connection carries: the session's own state
     * machine reads that byte (step_init) and both services run in this loop.
     * One accept path, one kind of connection as far as this file is
     * concerned.
     */
    ToriRSServer_ConnBegin(&c->conn, fd);
    c->conn.require_ticket = host->srv->gate;
    fprintf(stderr, "torirsserver: client connected (slot %d)\n", index);
    ToriRSServer_CoreReady(&host->core, &c->co);
}

/* The listener's coroutine: readiness on the listening socket means a
 * connection (or several) is waiting. The socket is non-blocking, so a
 * spurious wake costs one failed accept. */
static int
host_accept_step(
    struct ToriRSServerCore* core,
    struct ToriRSServerCo* co)
{
    struct ToriRSServerHost* host =
        (struct ToriRSServerHost*)((char*)co - offsetof(struct ToriRSServerHost, accept_co));

    (void)core;
    /* Bounded so a connection storm cannot hold the loop off the tick; the
     * listener stays readable and is resumed again. */
    for( int i = 0; i < 64; i++ )
    {
        int before = host->conn_count;

        host_accept_one(host);
        if( host->conn_count == before )
            break;
    }
    return PT_YIELDED;
}

/* ------------------------------------------------------------------ */
/* Tick-boundary stages                                                */
/* ------------------------------------------------------------------ */

static void
host_stage_login(
    struct ToriRSServerHost* host,
    int index)
{
    struct ToriRSServerHostConn* c = host->conns[index];
    struct ToriRSServerPlayer* player;
    struct ToriRSServerPlayer* evict;
    int claim;

    /* The peer can leave between raising the login and this stage running. */
    if( !c )
        return;
    if( c->state != TORIRSSERVER_HOST_CONN_LIVE )
    {
        c->awaiting_login = 0;
        return;
    }

    claim = ToriRSServer_ClaimSlot(&host->claims, host->srv, &c->session, now_ms(), &evict);
    if( evict )
    {
        /* Kill before removing, the order host_conn_release keeps: the session
         * is closing but still addressable while the world lets go. Its queued
         * release then finds no player and only frees the session. */
        for( int other = 0; other < TORIRSSERVER_HOST_CONN_CAP; other++ )
        {
            if( !host->conns[other] || host->conns[other]->session.player != evict )
                continue;
            host_conn_kill(host, host->conns[other]);
            host->conns[other]->session.player = NULL;
        }
        ToriRSServer_WorldRemovePlayer(host->srv, evict);
    }
    if( claim == TORIRSSERVER_CLAIM_REFUSE )
    {
        /* The client reads the closed link as a failed reconnect and logs in
         * with its password instead (ToriRS_Network_TakeResumeRefused). */
        c->awaiting_login = 0;
        host_conn_kill(host, c);
        return;
    }

    /*
     * The save this login read is the newest only if no save of the account
     * was queued after the read: the eviction just above queues one, and so
     * does a logout that crossed this login. Read again, behind it, and come
     * back on a later tick.
     */
    if( host->save_stamp[host_account_key(c->session.display_name) % TORIRSSERVER_HOST_SAVE_SLOTS] >
        c->read_seq )
    {
        c->refetch = 1;
        c->awaiting_login = 0;
        ToriRSServer_CoreReady(&host->core, &c->co);
        return;
    }

    /* Both idempotent, and both are the first login's bill rather than the
     * world's: the pack is already loaded at boot, and the world is built
     * once however many players arrive. */
    ToriRSServer_ScriptsLoad(host->srv, host->config->script_dir);
    ToriRSServer_WorldInit(
        host->srv,
        ToriRSServer_BootZone(host->config->home_x),
        ToriRSServer_BootZone(host->config->home_z));

    player = claim == TORIRSSERVER_CLAIM_ANY_SLOT
                 ? ToriRSServer_WorldAddPlayer(host->srv, &c->session)
                 : ToriRSServer_WorldAddPlayerAt(host->srv, &c->session, claim);
    c->awaiting_login = 0;
    if( !player )
    {
        /* The world is full and said so. The connection goes rather than
         * hanging on a login screen that will never answer. */
        host_conn_kill(host, c);
        return;
    }
    c->session.player = player;
    ToriRSServer_WorldPlayerInit(player);
    ToriRSServer_WorldSetDisplayName(player, c->session.display_name);
    player->staff_level = c->session.staff_level;
    player->account_admin = c->session.account_admin;
    ToriRSServer_WorldLogin(player);
    fprintf(stderr, "torirsserver: %s logged in (pid %d)\n", player->display_name, player->pid);

    /*
     * Anything the client sent behind its login block is still in the session
     * buffer, and there is a player to dispatch it to now. Pumping here rather
     * than leaving it to the next resume is what keeps the first packets of a
     * session in front of the tick that follows.
     */
    if( !ToriRSServer_SessionPump(&c->session, host->srv) )
    {
        host_conn_kill(host, c);
        return;
    }
    ToriRSServer_CoreReady(&host->core, &c->co);
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
        if( job.kind == TORIRSSERVER_HOST_JOB_LOGIN )
            host_stage_login(host, job.conn);
        else
            host_conn_release(host, job.conn);
    }
}

/* After a tick, every connection with output gets a resume to send it. */
static void
host_ready_output(struct ToriRSServerHost* host)
{
    for( int i = 0; i < TORIRSSERVER_HOST_CONN_CAP; i++ )
    {
        struct ToriRSServerHostConn* c = host->conns[i];

        if( c && c->state == TORIRSSERVER_HOST_CONN_LIVE && !c->awaiting_login &&
            ToriRSServer_ConnPending(&c->conn) > 0 )
            ToriRSServer_CoreReady(&host->core, &c->co);
    }
}

/* ------------------------------------------------------------------ */
/* Shutdown                                                            */
/* ------------------------------------------------------------------ */

/* Written by a signal handler, read by the loop. `sig_atomic_t` is the only
 * type the C standard promises can be assigned there without tearing, and
 * `volatile` is what stops the compiler hoisting the read out of the loop. */
static volatile sig_atomic_t g_shutdown_requested;

void
ToriRSServer_HostRequestShutdown(void)
{
    g_shutdown_requested = 1;
}

int
ToriRSServer_HostShutdownRequested(void)
{
    return g_shutdown_requested != 0;
}

/*
 * Log every player out, which is what saves them.
 *
 * Deliberately not a save loop of its own. `host_conn_kill` -> LOGOUT job ->
 * `host_conn_release` -> `ToriRSServer_WorldRemovePlayer` is the ONLY logout
 * path this host has, and the save inside it is placed where the character is
 * still whole (torirs_server_world.c says so at the call). A second path that
 * walked the player table and called `ToriRSServer_SavePlayer` would be a
 * second definition of what "logged out" means, and would drift from this one.
 *
 * The saves themselves go to the pool; ToriRSServer_CoreShutdown waits every
 * one of them out before the process goes.
 */
static void
host_logout_everyone(struct ToriRSServerHost* host)
{
    int live = 0;

    for( int i = 0; i < TORIRSSERVER_HOST_CONN_CAP; i++ )
    {
        struct ToriRSServerHostConn* c = host->conns[i];

        if( !c || c->state == TORIRSSERVER_HOST_CONN_CLOSING )
            continue;
        if( c->session.player )
            live++;
        host_conn_kill(host, c);
    }
    fprintf(stderr, "torirsserver: shutting down — saving %d player(s)\n", live);
    host_drain_queue(host);
    /* Their logouts, which the loop will not reach to send: a moment to hand
     * them to the account server, and what it does not take waits in the
     * spool for the next start. */
    if( host->srv->account_link )
    {
        ToriRSServer_AccountLinkWrite(host->srv->account_link, host->srv->account_log);
        ToriRSServer_AccountLinkDrain(host->srv->account_link, 2000);
    }
}

/* ------------------------------------------------------------------ */
/* Loop                                                                */
/* ------------------------------------------------------------------ */

static void
host_listener_nonblocking(int listener)
{
#ifdef _WIN32
    u_long on = 1;

    ioctlsocket((SOCKET)listener, FIONBIO, &on);
#else
    int flags = fcntl(listener, F_GETFL, 0);

    if( flags >= 0 )
        fcntl(listener, F_SETFL, flags | O_NONBLOCK);
#endif
}

/* Two descriptors per player and some to spare. The macOS default soft limit
 * is 256, which a few hundred players exhaust long before the cap. */
static void
host_raise_fd_limit(void)
{
#ifndef _WIN32
    struct rlimit limit;
    rlim_t want = (rlim_t)TORIRSSERVER_HOST_CONN_CAP + 64;

    if( getrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur >= want )
        return;
    limit.rlim_cur =
        limit.rlim_max == RLIM_INFINITY || limit.rlim_max >= want ? want : limit.rlim_max;
    if( setrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur < want )
        fprintf(
            stderr,
            "torirsserver: descriptor limit is %llu, below the %llu %d players need; "
            "raise it (ulimit -n / LimitNOFILE)\n",
            (unsigned long long)limit.rlim_cur,
            (unsigned long long)want,
            TORIRSSERVER_PLAYER_MAX);
#endif
}

static int
host_pool_threads(void)
{
    const char* configured = getenv("TORIRSSERVER_POOL_THREADS");

    if( configured && configured[0] )
        return atoi(configured) > 0 ? atoi(configured) : 0;
    return TORIRSSERVER_HOST_POOL_THREADS;
}

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

    host = (struct ToriRSServerHost*)ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*host));
    assert(host);
    host->srv = srv;
    host->config = config;
    host->listener = listener;
    host->next_tick = now_ms() + TORIRSSERVER_TICK_MS;
    host_raise_fd_limit();
    host_listener_nonblocking(listener);
    ToriRSServer_CoreInit(&host->core, host_pool_threads());
    ToriRSServer_CoInit(&host->accept_co, host_accept_step);
    ToriRSServer_PollAdd(host->core.poll, listener, &host->accept_co);
    ToriRSServer_SaveSetSink(host_save_sink, host);
    fprintf(
        stderr,
        "torirsserver: core loop up — %d players, %d connections, %d pool thread(s)\n",
        TORIRSSERVER_PLAYER_MAX,
        TORIRSSERVER_HOST_CONN_CAP,
        host_pool_threads());

    for( ;; )
    {
        long wait_ms;

        /*
         * Checked here, at the top, because that is where every path lands: a
         * signal ends the wait early (EINTR comes back as zero events), and
         * with no signal at all the tick deadline brings us here within
         * 600 ms, which is what Windows relies on -- there a signal does not
         * interrupt the wait at all.
         */
        if( ToriRSServer_HostShutdownRequested() )
        {
            host_logout_everyone(host);
            ToriRSServer_CoreShutdown(&host->core);
            ToriRSServer_SaveSetSink(NULL, NULL);
            ToriMem_Free(ToriMem_ServerNet(), host);
            return 0;
        }

        wait_ms = host->next_tick - now_ms();
        if( wait_ms < 0 )
            wait_ms = 0;
        ToriRSServer_CoreStep(&host->core, wait_ms);

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
            /* What the tick and the logins/logouts drained above told the
             * account log, into the spool for the account server
             * (torirs_server_accountlink.h): an append, never a wait. */
            if( host->srv->account_link )
                ToriRSServer_AccountLinkWrite(host->srv->account_link, host->srv->account_log);
            host_ready_output(host);

            /* Anchor to the schedule rather than to "now", so a slow tick does
             * not push every later tick out. */
            host->next_tick += TORIRSSERVER_TICK_MS;
            if( now_ms() - host->next_tick > 5 * TORIRSSERVER_TICK_MS )
                host->next_tick = now_ms() + TORIRSSERVER_TICK_MS;
        }

        /* One non-blocking step of sending to the account server, every
         * pass: what its socket takes now, and no more. */
        if( host->srv->account_link )
            ToriRSServer_AccountLinkPump(host->srv->account_link, now_ms());

        /* The standalone server's FRAME reset: one loop iteration is its
         * frame. (The tick inside it is a checked scope of its own.) */
        ToriMem_FrameEnd();
    }
}
