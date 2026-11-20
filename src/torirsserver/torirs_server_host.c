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
 *   pool threads   a login's check, lease and save from the player account
 *                  server (accountserver/account_client.h), a login's save read,
 *                  a logout's save write and the save's return behind it.
 *                  One job at a time per account, in order
 *                  (torirs_server_pool.h).
 *
 * Each connection is a coroutine (host_conn_step): it sniffs the stream, runs
 * the handshake, sends its login's password check and save read to the pool
 * and waits for them, hands the login to the tick, and from then on moves
 * bytes between the socket and the session. The world is touched only at the
 * tick boundary, by the work queue (host_drain_queue), as before.
 */
#include "torirs_server_control.h"
#include "torirs_server_host.h"
#include "torirs_server_recording.h"

#include "mem/torirs_mem.h"
#include "accountserver/account_client.h"
#include "torirs_server.h"
#include "torirs_server_hiscoreslink.h"
#include "torirs_server_auth.h"
#include "torirs_server_boot.h"
#include "torirs_server_claim.h"
#include "torirs_server_core.h"
#include "torirs_server_oauth.h"
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

#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* A login's check, run on a pool thread: the password (or the one-time
 * token) and the character's lease, from the account server. Copies
 * in, verdict out. */
struct ToriRSServerHostAuthJob
{
    struct ToriRSServerPoolJob job;
    char user[64];
    char pass[64];
    /** enum ToriRSServerSessionAuthKind */
    int kind;
    /** The world's name and this run of it (ToriRSServer.player_world,
     *  player_boot): constant while it runs. */
    const char* world;
    const char* boot;
    struct AccountLogin login;
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
    /**
     * The claim's answer, kept across a refetch. Answering spends what it
     * answered from -- a departed record is marked spent, an old session is
     * evicted -- and both queue the save that forces the refetch, so asking
     * again after the re-read found nothing: a reconnect whose old socket
     * closed just before it was refused as "no session to reclaim".
     */
    int claim_held;
    int claim;
    /**
     * The character this connection asked the account server for, as
     * saves are keyed, and where that stands: `lease_pending` while the check
     * is with the pool, `lease_held` once it was granted. Each counts once in
     * the host's ToriRSServerHostLease for the key.
     */
    char lease_key[ACCOUNT_KEY_MAX];
    int lease_pending;
    int lease_held;
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

/*
 * A character this world holds the account server's lease on
 * (accountserver/account_server.h), and so whose save is this world's: kept in
 * the world's own directory (ToriRSServer_SaveSetCharacterDir) beside a
 * `<key>.lease` file naming the token, written at every save, and sent back
 * only when the character leaves.
 *
 * Kept per character rather than per connection, because one world can have
 * two connections for one character for a moment -- a reloaded tab beside the
 * socket it replaces, a reconnect the claim may still refuse -- and the
 * server's lease is the world's, under the newest grant's token. So the save
 * goes back only when no connection holds the character and none is asking
 * for it, and then from the last logout's own save job, the text just
 * composed.
 */
struct ToriRSServerHostLease
{
    char key[ACCOUNT_KEY_MAX];
    /** The live lease's token: the newest grant. "" until one comes back. */
    char token[ACCOUNT_LEASE_TOKEN_HEX + 1];
    /** Connections whose check granted it. */
    int refs;
    /** Connections whose check is with the pool. */
    int pending;
    /** Nobody here plays it, but its save has not been taken back yet (the
     *  server did not answer, or this world restarted holding it): the
     *  return is retried every ACCOUNT_RETURN_RETRY_MS. */
    int kept;
    /** A return job is with the pool. */
    int returning;
};

/* How often a save the account server did not take is offered again. */
#define ACCOUNT_RETURN_RETRY_MS 15000L
/* A crash can leave a held save for every player the world had, beside the
 * connections' own. */
#define TORIRSSERVER_HOST_LEASE_CAP (2 * TORIRSSERVER_HOST_CONN_CAP)

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

    /* The characters this world holds leases on; empty in development mode,
     * which takes none. At most one per connection. */
    struct ToriRSServerHostLease leases[TORIRSSERVER_HOST_LEASE_CAP];
    int lease_count;
    /* When the kept saves are offered again; whether the boot's hello was
     * taken (until it is, it is asked again with them). */
    long next_return;
    int hello_done;
    int hello_in_flight;
    /*
     * Armed by host_conn_release around ToriRSServer_WorldRemovePlayer when
     * that logout is the character's last: the save the removal composes
     * carries the lease's release (host_save_sink), so the release is the
     * same pool job as the write and runs only once the write succeeded.
     */
    char release_key[ACCOUNT_KEY_MAX];
    char release_token[ACCOUNT_LEASE_TOKEN_HEX + 1];
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

/* ------------------------------------------------------------------ */
/* Held saves                                                          */
/* ------------------------------------------------------------------ */
/*
 * A character this world has: its save at ToriRSServer_SavePath (the world's
 * own directory, ToriRSServer_SaveSetCharacterDir) and the lease's token at
 * `<key>.lease` beside it. Together they are what the world owes the player
 * account server; a restart finds them there and returns them. Pool threads
 * only: these are file operations.
 */

static void
held_lease_path(
    const char* key,
    char* out,
    size_t cap)
{
    const char* dir = ToriRSServer_SaveCharacterDir();

    assert(dir);
    snprintf(out, cap, "%s/%s.lease", dir, key);
}

static int
held_file_exists(const char* path)
{
    FILE* file = fopen(path, "rb");

    if( !file )
        return 0;
    fclose(file);
    return 1;
}

/* The character's held save as text (NET-allocated), or NULL when there is
 * none. */
static char*
held_save_read(const char* key)
{
    FILE* file = fopen(ToriRSServer_SavePath(key), "rb");
    long len = 0;
    char* text;

    if( !file )
        return NULL;
    if( fseek(file, 0, SEEK_END) == 0 )
        len = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( len <= 0 )
    {
        fclose(file);
        return NULL;
    }
    text = ToriMem_Alloc(ToriMem_Net(), (size_t)len + 1);
    assert(text);
    if( fread(text, 1, (size_t)len, file) != (size_t)len )
    {
        /* Returned short, the server would take half a character. */
        fprintf(stderr, "torirsserver: cannot read the held save %s\n", ToriRSServer_SavePath(key));
        abort();
    }
    fclose(file);
    text[len] = '\0';
    return text;
}

/* A granted login's files: the token, and -- unless this world had the
 * character already and still has its own, newer save -- the server's copy
 * (none at all for a character never saved). 0 when they could not be
 * written: the login must not go on to read a save that is not there. */
static int
held_take(
    const char* user,
    const struct AccountLogin* login)
{
    char key[ACCOUNT_KEY_MAX];
    char path[1200];
    char line[ACCOUNT_LEASE_TOKEN_HEX + 2];
    const char* save_path;

    ToriRSServer_SaveFileName(user, key, sizeof(key));
    assert(key[0]);
    held_lease_path(key, path, sizeof(path));
    snprintf(line, sizeof(line), "%s\n", login->lease);
    if( !ToriRSServer_SaveWriteText(path, line) )
        return 0;
    save_path = ToriRSServer_SavePath(key);
    if( login->had && held_file_exists(save_path) )
        return 1;
    if( login->save_len > 0 )
        return ToriRSServer_SaveWriteText(save_path, login->save);
    /* A new character: no save, here or anywhere. */
    remove(save_path);
    return 1;
}

/* After a return: taken (1) -- the files go; refused (0) -- the save is set
 * aside where an operator can find it, since the server will never take it;
 * no answer (-1) -- both stay, to be offered again. */
static void
held_settle(
    const char* key,
    int result,
    int had_save)
{
    char lease_path[1200];

    if( result < 0 )
        return;
    held_lease_path(key, lease_path, sizeof(lease_path));
    if( result == 0 && had_save )
    {
        char aside[1300];

        snprintf(aside, sizeof(aside), "%s/refused/%s-%lld.ini", ToriRSServer_SaveCharacterDir(),
                 key, (long long)time(NULL));
        ToriRSServer_SaveMkdirParent(aside);
        if( ToriRSServer_SaveReplace(ToriRSServer_SavePath(key), aside) == 0 )
            fprintf(stderr, "torirsserver: '%s''s save from this world is set aside at %s\n", key,
                    aside);
    }
    else
        remove(ToriRSServer_SavePath(key));
    remove(lease_path);
}

/* ------------------------------------------------------------------ */
/* Pool jobs                                                           */
/* ------------------------------------------------------------------ */

static void
host_auth_run(struct ToriRSServerPoolJob* job)
{
    struct ToriRSServerHostAuthJob* auth = (struct ToriRSServerHostAuthJob*)job;
    int granted;

    memset(&auth->login, 0, sizeof(auth->login));
    if( auth->kind == TORIRSSERVER_SESSION_AUTH_TOKEN )
    {
        /* The token first: it names the one account it was minted for, and
         * only then is that account's lease asked for. */
        if( ToriRSServer_OAuthRedeemOtl(auth->pass, auth->user, (long long)time(NULL)) )
            AccountClient_Login(auth->world, auth->boot, auth->user, NULL, &auth->login);
        else
            auth->login.result = TORIRSSERVER_AUTH_REFUSED;
    }
    else
        AccountClient_Login(auth->world, auth->boot, auth->user,
                           auth->kind == TORIRSSERVER_SESSION_AUTH_PASSWORD ? auth->pass : NULL,
                           &auth->login);
    memset(auth->pass, 0, sizeof(auth->pass));
    granted = auth->login.result == TORIRSSERVER_AUTH_OK ||
              auth->login.result == TORIRSSERVER_AUTH_CLAIMED;
    if( granted && !held_take(auth->user, &auth->login) )
    {
        fprintf(stderr, "torirsserver: cannot keep '%s''s save here; the login is refused\n",
                auth->user);
        AccountClient_Release(auth->world, auth->user, auth->login.lease);
        auth->login.result = TORIRSSERVER_AUTH_UNAVAILABLE;
        auth->login.lease[0] = '\0';
    }
    AccountClient_LoginFree(&auth->login);
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

/* A save, written to the world's held copy on a pool thread -- and, when it
 * is the character's last logout on this world, handed back to the player
 * account server behind it: the text just composed, under the lease. */
struct ToriRSServerHostSaveJob
{
    struct ToriRSServerCoreJob core; /* first */
    struct ToriRSServerHost* host;
    char path[1024];
    char* text;
    int ok;
    /** "" for a save that returns nothing. */
    char release_key[ACCOUNT_KEY_MAX];
    char release_token[ACCOUNT_LEASE_TOKEN_HEX + 1];
    const char* world;
    /** AccountClient_Logout's answer: 1 taken, 0 refused, -1 no answer. */
    int returned;
    char holder[ACCOUNT_WORLD_MAX];
};

static void host_return_finished(
    struct ToriRSServerHost* host,
    const char* key,
    const char* token,
    int result,
    const char* holder);

static void
host_save_run(struct ToriRSServerPoolJob* job)
{
    struct ToriRSServerHostSaveJob* save = (struct ToriRSServerHostSaveJob*)job;

    save->ok = ToriRSServer_SaveWriteText(save->path, save->text);
    if( !save->release_key[0] )
        return;
    /* From the text, not the file: a held copy that could not be written
     * does not cost the logout its save. */
    save->returned = AccountClient_Logout(save->world, save->release_key, save->release_token,
                                         save->text, save->holder, sizeof(save->holder));
    held_settle(save->release_key, save->returned, 1);
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
    if( save->release_key[0] )
        host_return_finished(save->host, save->release_key, save->release_token, save->returned,
                             save->holder);
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
    if( host->release_key[0] )
    {
        char file[ACCOUNT_KEY_MAX];

        ToriRSServer_SaveFileName(account, file, sizeof(file));
        if( strcmp(file, host->release_key) == 0 )
        {
            snprintf(save->release_key, sizeof(save->release_key), "%s", host->release_key);
            snprintf(save->release_token, sizeof(save->release_token), "%s", host->release_token);
            save->world = host->srv->player_world;
            host->release_key[0] = '\0';
        }
    }
    save->core.job.fn = host_save_run;
    save->core.job.key = key;
    host->save_seq++;
    host->save_stamp[key % TORIRSSERVER_HOST_SAVE_SLOTS] = host->save_seq;
    host->saves_in_flight++;
    ToriRSServer_CoreSubmitDetached(&host->core, &save->core, host_save_done);
}

/* ------------------------------------------------------------------ */
/* Leases                                                              */
/* ------------------------------------------------------------------ */

/* A return with no save just composed: the character never got as far as
 * the world, the world composed no save at its removal, or an earlier return
 * went unanswered. Whatever held save there is goes; none, only the lease. */
struct ToriRSServerHostReturnJob
{
    struct ToriRSServerCoreJob core; /* first */
    struct ToriRSServerHost* host;
    const char* world;
    char key[ACCOUNT_KEY_MAX];
    char token[ACCOUNT_LEASE_TOKEN_HEX + 1];
    int result;
    char holder[ACCOUNT_WORLD_MAX];
};

static void
host_return_run(struct ToriRSServerPoolJob* job)
{
    struct ToriRSServerHostReturnJob* ret = (struct ToriRSServerHostReturnJob*)job;
    char* text = held_save_read(ret->key);

    if( text )
        ret->result = AccountClient_Logout(ret->world, ret->key, ret->token, text, ret->holder,
                                          sizeof(ret->holder));
    else
        ret->result = AccountClient_Release(ret->world, ret->key, ret->token);
    held_settle(ret->key, ret->result, text != NULL);
    ToriMem_Free(ToriMem_Net(), text);
}

static void
host_return_done(
    struct ToriRSServerCore* core,
    struct ToriRSServerPoolJob* job)
{
    struct ToriRSServerHostReturnJob* ret = (struct ToriRSServerHostReturnJob*)job;

    (void)core;
    host_return_finished(ret->host, ret->key, ret->token, ret->result, ret->holder);
    ToriMem_Free(ToriMem_ServerNet(), ret);
}

/* Queued behind any save of the account already with the pool. */
static void
host_return_submit(
    struct ToriRSServerHost* host,
    const char* key,
    const char* token)
{
    struct ToriRSServerHostReturnJob* ret = ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*ret));

    assert(ret);
    ret->host = host;
    ret->world = host->srv->player_world;
    snprintf(ret->key, sizeof(ret->key), "%s", key);
    snprintf(ret->token, sizeof(ret->token), "%s", token);
    ret->core.job.fn = host_return_run;
    ret->core.job.key = ToriRSServer_PoolKey(key);
    ToriRSServer_CoreSubmitDetached(&host->core, &ret->core, host_return_done);
}

static struct ToriRSServerHostLease*
host_lease_find(
    struct ToriRSServerHost* host,
    const char* key)
{
    for( int i = 0; i < host->lease_count; i++ )
        if( strcmp(host->leases[i].key, key) == 0 )
            return &host->leases[i];
    return NULL;
}

/* `key`'s entry, made when there is none; NULL only when the table is full,
 * which only a restart holding more saves than the table can reach. */
static struct ToriRSServerHostLease*
host_lease_get(
    struct ToriRSServerHost* host,
    const char* key)
{
    struct ToriRSServerHostLease* lease = host_lease_find(host, key);

    if( lease )
        return lease;
    if( host->lease_count == TORIRSSERVER_HOST_LEASE_CAP )
        return NULL;
    lease = &host->leases[host->lease_count++];
    memset(lease, 0, sizeof(*lease));
    snprintf(lease->key, sizeof(lease->key), "%s", key);
    return lease;
}

static void
host_lease_remove(
    struct ToriRSServerHost* host,
    struct ToriRSServerHostLease* lease)
{
    *lease = host->leases[--host->lease_count];
}

/* Nobody holds or asks for it, and nothing is owed: forget it. */
static void
host_lease_settle(
    struct ToriRSServerHost* host,
    struct ToriRSServerHostLease* lease)
{
    if( lease->refs > 0 || lease->pending > 0 || lease->kept || lease->returning )
        return;
    host_lease_remove(host, lease);
}

/* A return is back (`result` as AccountClient_Logout's). */
static void
host_return_finished(
    struct ToriRSServerHost* host,
    const char* key,
    const char* token,
    int result,
    const char* holder)
{
    struct ToriRSServerHostLease* lease = host_lease_find(host, key);

    if( result == 0 )
        fprintf(stderr, "torirsserver: the account server refused '%s''s save from this "
                        "world: %s has the character\n",
                key, holder[0] ? holder : "an operator took it back, and nobody");
    if( result < 0 )
        fprintf(stderr, "torirsserver: the account server did not take '%s''s save; it "
                        "stays here and is offered again every %ld s\n",
                key, ACCOUNT_RETURN_RETRY_MS / 1000);
    if( !lease && result < 0 )
    {
        lease = host_lease_get(host, key);
        if( !lease )
        {
            fprintf(stderr, "torirsserver: '%s' is returned at the next start\n", key);
            return;
        }
        snprintf(lease->token, sizeof(lease->token), "%s", token);
        lease->kept = 1;
    }
    if( !lease )
        return;
    lease->returning = 0;
    /* Logged in again meanwhile: the newer grant is that session's. */
    if( lease->refs > 0 || lease->pending > 0 )
        return;
    if( result >= 0 )
    {
        lease->kept = 0;
        host_lease_settle(host, lease);
    }
}

/* `c`'s check goes to the pool: its character is being asked for. */
static void
host_lease_pend(
    struct ToriRSServerHost* host,
    struct ToriRSServerHostConn* c)
{
    struct ToriRSServerHostLease* lease;

    assert(!c->lease_pending);
    assert(!c->lease_held);
    ToriRSServer_SaveFileName(c->auth.user, c->lease_key, sizeof(c->lease_key));
    /* An unusable name is refused by the server; nothing to count. */
    if( !c->lease_key[0] )
        return;
    lease = host_lease_get(host, c->lease_key);
    /* One per connection, and the table holds twice the connections. */
    assert(lease);
    lease->pending++;
    c->lease_pending = 1;
}

/* `c`'s check is back (whether or not `c` is still connected). */
static void
host_lease_answer(
    struct ToriRSServerHost* host,
    struct ToriRSServerHostConn* c)
{
    struct ToriRSServerHostLease* lease;
    int granted = c->auth.login.result == TORIRSSERVER_AUTH_OK ||
                  c->auth.login.result == TORIRSSERVER_AUTH_CLAIMED;

    if( !c->lease_pending )
        return;
    lease = host_lease_find(host, c->lease_key);
    assert(lease);
    lease->pending--;
    c->lease_pending = 0;
    if( granted )
    {
        /* The newest grant is the lease, and its save is the held one. */
        snprintf(lease->token, sizeof(lease->token), "%s", c->auth.login.lease);
        lease->refs++;
        lease->kept = 0;
        c->lease_held = 1;
    }
    else
        host_lease_settle(host, lease);
}

/* `c` lets go of its character. 1 when that was the last hold, with the
 * lease's token in `out_token`: the caller returns the save (and the entry
 * says a return is out); 0 when another connection still holds or asks for
 * it. */
static int
host_lease_drop(
    struct ToriRSServerHost* host,
    struct ToriRSServerHostConn* c,
    char out_token[ACCOUNT_LEASE_TOKEN_HEX + 1])
{
    struct ToriRSServerHostLease* lease;

    out_token[0] = '\0';
    if( !c->lease_held )
        return 0;
    c->lease_held = 0;
    lease = host_lease_find(host, c->lease_key);
    assert(lease);
    assert(lease->refs > 0);
    lease->refs--;
    if( lease->refs > 0 || lease->pending > 0 )
        return 0;
    assert(lease->token[0]);
    snprintf(out_token, ACCOUNT_LEASE_TOKEN_HEX + 1, "%s", lease->token);
    lease->kept = 1;
    lease->returning = 1;
    return 1;
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
    char token[ACCOUNT_LEASE_TOKEN_HEX + 1];

    /* A check that came back after its connection went: a lease it was
     * granted is nobody's, and goes straight back. */
    host_lease_answer(c->host, c);
    if( host_lease_drop(c->host, c, token) )
        host_return_submit(c->host, c->lease_key, token);
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
    char token[ACCOUNT_LEASE_TOKEN_HEX + 1];
    int last_hold;
    int armed;

    assert(c);
    /* The character's last hold on this world: its save goes back from the
     * save job the removal below composes (host_save_sink), or on its own
     * when there is no player to save. */
    last_hold = host_lease_drop(host, c, token);
    armed = last_hold && c->session.player;
    if( armed )
    {
        snprintf(host->release_key, sizeof(host->release_key), "%s", c->lease_key);
        snprintf(host->release_token, sizeof(host->release_token), "%s", token);
    }
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
    /* Not taken by a save: the return goes on its own, still behind any
     * save of the account already with the pool. */
    if( last_hold && (!armed || host->release_key[0]) )
        host_return_submit(host, c->lease_key, token);
    host->release_key[0] = '\0';
    host->release_token[0] = '\0';
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
            /* The check and the lease, off the loop. Not read meanwhile: the
             * session decodes nothing until it has the answer. */
            assert(host->srv->player_world[0]);
            host_watch(c, 0, 0);
            snprintf(c->auth.user, sizeof(c->auth.user), "%s", c->session.auth_user);
            memcpy(c->auth.pass, c->session.auth_pass, sizeof(c->auth.pass));
            memset(c->session.auth_pass, 0, sizeof(c->session.auth_pass));
            c->auth.kind = c->session.auth_kind;
            c->auth.world = host->srv->player_world;
            c->auth.boot = host->srv->player_boot;
            c->auth.job.fn = host_auth_run;
            c->auth.job.key = host_account_key(c->auth.user);
            host_lease_pend(host, c);
            TORIRSSERVER_CO_AWAIT_JOB(core, co, &c->auth.job);
            host_lease_answer(host, c);
            if( c->auth.login.result == TORIRSSERVER_AUTH_ONLINE )
                fprintf(stderr, "torirsserver: '%s' is logged in on world %s\n", c->auth.user,
                        c->auth.login.holder);
            ToriRSServer_SessionAuthDone(&c->session, host->srv, c->auth.login.result,
                                         c->auth.login.admin, c->auth.login.record);
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
/* Saves owed, and the hello                                           */
/* ------------------------------------------------------------------ */

/* This run's hello, when the one at boot went unanswered: it names every
 * character this world has an entry for, and the server lets go of only what
 * an earlier run held (the boot id), so logins since cannot be dropped. */
struct ToriRSServerHostHelloJob
{
    struct ToriRSServerCoreJob core; /* first */
    struct ToriRSServerHost* host;
    int count;
    char (*keys)[ACCOUNT_KEY_MAX];
    int result;
};

static void
host_hello_run(struct ToriRSServerPoolJob* job)
{
    struct ToriRSServerHostHelloJob* hello = (struct ToriRSServerHostHelloJob*)job;

    hello->result = AccountClient_Hello(hello->host->srv->player_world,
                                       hello->host->srv->player_boot, hello->keys, hello->count);
}

static void
host_hello_done(
    struct ToriRSServerCore* core,
    struct ToriRSServerPoolJob* job)
{
    struct ToriRSServerHostHelloJob* hello = (struct ToriRSServerHostHelloJob*)job;

    (void)core;
    hello->host->hello_in_flight = 0;
    if( hello->result >= 0 )
    {
        hello->host->hello_done = 1;
        fprintf(stderr, "torirsserver: the account server took this world's start; %d "
                        "character(s) an earlier run held are free\n",
                hello->result);
    }
    ToriMem_Free(ToriMem_ServerNet(), hello->keys);
    ToriMem_Free(ToriMem_ServerNet(), hello);
}

static struct ToriRSServerHostHelloJob*
host_hello_compose(struct ToriRSServerHost* host)
{
    struct ToriRSServerHostHelloJob* hello = ToriMem_Calloc(ToriMem_ServerNet(), 1, sizeof(*hello));

    assert(hello);
    hello->host = host;
    hello->keys = ToriMem_Calloc(ToriMem_ServerNet(), (size_t)host->lease_count + 1,
                                 sizeof(*hello->keys));
    assert(hello->keys);
    for( int i = 0; i < host->lease_count; i++ )
        snprintf(hello->keys[hello->count++], ACCOUNT_KEY_MAX, "%s", host->leases[i].key);
    return hello;
}

/* Every ACCOUNT_RETURN_RETRY_MS: the saves the server has not taken yet,
 * offered again, and the hello until it is taken. */
static void
host_return_retry(struct ToriRSServerHost* host)
{
    if( !host->hello_done && !host->hello_in_flight )
    {
        struct ToriRSServerHostHelloJob* hello = host_hello_compose(host);

        hello->core.job.fn = host_hello_run;
        hello->core.job.key = ToriRSServer_PoolKey("account-server-hello");
        host->hello_in_flight = 1;
        ToriRSServer_CoreSubmitDetached(&host->core, &hello->core, host_hello_done);
    }
    for( int i = 0; i < host->lease_count; i++ )
    {
        struct ToriRSServerHostLease* lease = &host->leases[i];

        if( !lease->kept || lease->returning || lease->refs > 0 || lease->pending > 0 )
            continue;
        lease->returning = 1;
        host_return_submit(host, lease->key, lease->token);
    }
}

/*
 * The saves a previous run left in this world's directory: a `<key>.lease`
 * for every character it had when it stopped -- all of them, after a crash.
 * Each is owed to the account server; the hello names them so their
 * leases stay this world's until the return. Before the loop, so blocking
 * here costs nobody.
 */
static void
host_held_recover(struct ToriRSServerHost* host)
{
    const char* dir = ToriRSServer_SaveCharacterDir();
    struct ToriRSServerHostHelloJob* hello;
    DIR* handle;
    struct dirent* entry;
    int found = 0;

    assert(dir);
    handle = opendir(dir);
    /* No directory: this world has never held a character. */
    if( handle )
    {
        while( (entry = readdir(handle)) != NULL )
        {
            size_t len = strlen(entry->d_name);
            char key[ACCOUNT_KEY_MAX];
            char path[1200];
            char token[ACCOUNT_LEASE_TOKEN_HEX + 2] = "";
            struct ToriRSServerHostLease* lease;
            FILE* file;

            if( len <= 6 || len - 6 >= sizeof(key) ||
                strcmp(entry->d_name + len - 6, ".lease") != 0 )
                continue;
            snprintf(key, sizeof(key), "%.*s", (int)(len - 6), entry->d_name);
            held_lease_path(key, path, sizeof(path));
            file = fopen(path, "rb");
            if( !file )
                continue;
            if( !fgets(token, sizeof(token), file) )
                token[0] = '\0';
            fclose(file);
            token[strcspn(token, "\r\n")] = '\0';
            if( strlen(token) != ACCOUNT_LEASE_TOKEN_HEX )
            {
                fprintf(stderr, "torirsserver: %s is not a lease; skipped\n", path);
                continue;
            }
            lease = host_lease_get(host, key);
            if( !lease )
            {
                fprintf(stderr, "torirsserver: more held saves than this run can track; the "
                                "rest are returned at the next start\n");
                break;
            }
            snprintf(lease->token, sizeof(lease->token), "%s", token);
            lease->kept = 1;
            found++;
        }
        closedir(handle);
    }
    if( found )
        fprintf(stderr, "torirsserver: %d character save(s) from an earlier run to hand back\n",
                found);
    hello = host_hello_compose(host);
    hello->result = AccountClient_Hello(host->srv->player_world, host->srv->player_boot,
                                       hello->keys, hello->count);
    host->hello_done = hello->result >= 0;
    if( !host->hello_done )
        fprintf(stderr, "torirsserver: the account server did not answer this world's "
                        "start; logins are refused (\"login server offline\") until it does\n");
    ToriMem_Free(ToriMem_ServerNet(), hello->keys);
    ToriMem_Free(ToriMem_ServerNet(), hello);
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

    if( c->claim_held )
    {
        claim = c->claim;
        evict = NULL;
        c->claim_held = 0;
    }
    else
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
        c->claim_held = 1;
        c->claim = claim;
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
    player->account_record = c->session.account_record;
    /* Before the login sends anything: RECONNECT_OK and the login's REBUILD
     * are the first records of a recorded session. */
    ToriRSServer_RecordingLogin(player, c->session.reconnect);
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
    if( host->srv->hiscores_link )
    {
        ToriRSServer_HiscoresLinkWrite(host->srv->hiscores_link, host->srv->hiscores_log);
        ToriRSServer_HiscoresLinkDrain(host->srv->hiscores_link, 2000);
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
    if( srv->player_world[0] )
        host_held_recover(host);
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
            else
                /* The control link still answers before the first login (who
                 * is online: nobody), or a tool asking right after a restart
                 * waits on a world nobody has built. The tick runs it first
                 * thing once there is one. */
                ToriRSServer_ControlTick(host->srv);
            /* What the tick and the logins/logouts drained above told the
             * account log, into the spool for the account server
             * (torirs_server_hiscoreslink.h): an append, never a wait. */
            if( host->srv->hiscores_link )
                ToriRSServer_HiscoresLinkWrite(host->srv->hiscores_link, host->srv->hiscores_log);
            host_ready_output(host);

            /* Anchor to the schedule rather than to "now", so a slow tick does
             * not push every later tick out. */
            host->next_tick += TORIRSSERVER_TICK_MS;
            if( now_ms() - host->next_tick > 5 * TORIRSSERVER_TICK_MS )
                host->next_tick = now_ms() + TORIRSSERVER_TICK_MS;
        }

        /* One non-blocking step of sending to the account server, every
         * pass: what its socket takes now, and no more. */
        if( host->srv->hiscores_link )
            ToriRSServer_HiscoresLinkPump(host->srv->hiscores_link, now_ms());

        /* Saves the account server has not taken yet, offered
         * again on the pool. */
        if( host->srv->player_world[0] && now_ms() >= host->next_return )
        {
            host->next_return = now_ms() + ACCOUNT_RETURN_RETRY_MS;
            host_return_retry(host);
        }

        /* The standalone server's FRAME reset: one loop iteration is its
         * frame. (The tick inside it is a checked scope of its own.) */
        ToriMem_FrameEnd();
    }
}
