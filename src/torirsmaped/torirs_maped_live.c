/*
 * The live link: the game server's control protocol, LIVE_* and RUN's
 * reload, and the native link binding (torirs_maped_live.h has the contract).
 *
 * BUILD NOTE: compiled INTO torirs_maped.c after torirs_maped_build.c, whose
 * struct ToriRSMapEdJobs and jobs_* helpers it shares. Never linked on its
 * own. File-scope names start with `live_` / `jobs_` or `ToriRSMapEd`.
 */

#include "torirs_maped_live.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if TORIRSMAPED_JOBS_NATIVE
#include "torirs_maped_lanes.h"
/* Sockets as the game server has them (torirsserver/torirs_server_host.c):
 * winsock on Windows, BSD sockets elsewhere; a socket is an int either way. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define live_socket_close(fd) closesocket((SOCKET)(fd))
#define live_socket_error() WSAGetLastError()
#define LIVE_WOULD_BLOCK(error) ((error) == WSAEWOULDBLOCK || (error) == WSAEINPROGRESS)
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#define live_socket_close(fd) close(fd)
#define live_socket_error() errno
#define LIVE_WOULD_BLOCK(error) ((error) == EAGAIN || (error) == EWOULDBLOCK || (error) == EINPROGRESS || (error) == EINTR)
#endif
#endif

enum
{
    /** Seconds between CTL_STATUS polls while attached. */
    LIVE_STATUS_PERIOD_S = 5,
    /** Seconds a reload may take before RUN gives up on it. */
    LIVE_RELOAD_TIMEOUT_S = 180,
    /** A control frame larger than this is a broken peer. */
    LIVE_CTL_PAYLOAD_MAX = 4 * 1024 * 1024,
};

/* ------------------------------------------------------------------ */
/* STATE_LIVE_FOCUS                                                    */
/* ------------------------------------------------------------------ */

void
ToriRSMapEd_LiveFocusPack(
    const char* name,
    int x,
    int z,
    int level,
    int32_t out[TORIRSMAPED_LIVE_FOCUS_VALUES])
{
    size_t length;

    assert(name);
    assert(out);

    memset(out, 0, TORIRSMAPED_LIVE_FOCUS_VALUES * sizeof(int32_t));
    length = strlen(name);
    if( length > TORIRSMAPED_LIVE_FOCUS_NAME_MAX )
        length = TORIRSMAPED_LIVE_FOCUS_NAME_MAX;
    out[TORIRSMAPED_LIVE_FOCUS_NAME_LEN] = (int32_t)length;
    for( size_t i = 0; i < length; i++ )
    {
        uint32_t word = (uint32_t)out[TORIRSMAPED_LIVE_FOCUS_NAME + i / 4];
        word |= (uint32_t)(uint8_t)name[i] << (8 * (i % 4));
        out[TORIRSMAPED_LIVE_FOCUS_NAME + i / 4] = (int32_t)word;
    }
    out[TORIRSMAPED_LIVE_FOCUS_X] = x;
    out[TORIRSMAPED_LIVE_FOCUS_Z] = z;
    out[TORIRSMAPED_LIVE_FOCUS_LEVEL] = level;
}

int
ToriRSMapEd_LiveFocusName(
    const int32_t* values,
    int count,
    char* out,
    size_t out_size)
{
    int32_t length;

    assert(values || count == 0);
    assert(out);
    assert(out_size > TORIRSMAPED_LIVE_FOCUS_NAME_MAX);
    (void)out_size;

    out[0] = '\0';
    if( count < TORIRSMAPED_LIVE_FOCUS_VALUES )
        return 0;
    length = values[TORIRSMAPED_LIVE_FOCUS_NAME_LEN];
    if( length < 0 || length > TORIRSMAPED_LIVE_FOCUS_NAME_MAX )
        return 0;
    for( int32_t i = 0; i < length; i++ )
    {
        uint32_t const word = (uint32_t)values[TORIRSMAPED_LIVE_FOCUS_NAME + i / 4];
        char const c = (char)((word >> (8 * (i % 4))) & 0xff);
        if( c == '\0' )
            return 0;
        out[i] = c;
    }
    out[length] = '\0';
    return 1;
}

const char*
ToriRSMapEd_LiveStateName(uint32_t state)
{
    if( state == TORIRSMAPED_LIVE_DETACHED )
        return "detached";
    if( state == TORIRSMAPED_LIVE_CONNECTING )
        return "connecting";
    if( state == TORIRSMAPED_LIVE_ATTACHED )
        return "attached";
    if( state == TORIRSMAPED_LIVE_FAILED )
        return "failed";
    return "?";
}

const char*
ToriRSMapEd_PlayStateName(uint32_t state)
{
    if( state == TORIRSMAPED_PLAY_NONE )
        return "not started";
    if( state == TORIRSMAPED_PLAY_STARTING )
        return "starting";
    if( state == TORIRSMAPED_PLAY_ONLINE )
        return "logged in";
    if( state == TORIRSMAPED_PLAY_LOGGED_OUT )
        return "logged out";
    if( state == TORIRSMAPED_PLAY_EXITED )
        return "exited";
    return "?";
}

void
ToriRSMapEd_LanePlayAccount(
    const struct ToriRSMapEdLane* lane,
    char* out_user,
    size_t user_size,
    char* out_pass,
    size_t pass_size)
{
    assert(lane);
    assert(out_user);
    assert(out_pass);
    if( lane->play_user[0] )
        snprintf(out_user, user_size, "%s", lane->play_user);
    else
        snprintf(out_user, user_size, "maped%d", lane->game_port);
    if( lane->play_pass[0] )
        snprintf(out_pass, pass_size, "%s", lane->play_pass);
    else
        snprintf(out_pass, pass_size, "lane%drun", lane->game_port);
}

void
ToriRSMapEd_LaneWebPlayUrl(
    const struct ToriRSMapEdLane* lane,
    char* out,
    size_t out_size)
{
    char user[16];
    char pass[24];

    assert(lane);
    assert(out);
    out[0] = '\0';
    if( !lane->io_bin[0] || !lane->io_manifest[0] )
        return;
    ToriRSMapEd_LanePlayAccount(lane, user, sizeof(user), pass, sizeof(pass));
    snprintf(out, out_size,
        "http://127.0.0.1:%d/?args=--manifest,manifests/manifest_%s.ini,--connect,127.0.0.1:%d,"
        "--user,%s,--pass,%s&ws=ws://127.0.0.1:%d/&js5_url=ws://127.0.0.1:%d/&idb=maped%d",
        lane->io_port, lane->rev, lane->game_port, user, pass, lane->game_port, lane->js5_port,
        lane->game_port);
}

/* ------------------------------------------------------------------ */
/* Control frames                                                      */
/* ------------------------------------------------------------------ */

void
ToriRSMapEd_CtlFrame(
    struct ToriRSMapEdBuf* out,
    uint32_t type,
    const struct ToriRSMapEdBuf* payload)
{
    uint32_t const length = payload ? (uint32_t)ToriRSMapEd_BufAvailable(payload) : 0;

    assert(out);
    ToriRSMapEd_PutU32(out, type);
    ToriRSMapEd_PutU32(out, length);
    if( length > 0 )
        ToriRSMapEd_BufWrite(out, ToriRSMapEd_BufPeek(payload), (int)length);
}

static void live_service(struct ToriRSMapEd* maped);
static void jobs_run_play_decide(struct ToriRSMapEd* maped, int relaunched);

/** One control frame to the game server. 0 when the link is gone. */
static int
live_send(
    struct ToriRSMapEd* maped,
    uint32_t type,
    const struct ToriRSMapEdBuf* payload)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuf frame;
    int ok;

    if( !jobs->have_link || jobs->live_state != TORIRSMAPED_LIVE_ATTACHED )
        return 0;
    memset(&frame, 0, sizeof(frame));
    ToriRSMapEd_CtlFrame(&frame, type, payload);
    ok = jobs->link.send(
             jobs->link.ctx, ToriRSMapEd_BufPeek(&frame), ToriRSMapEd_BufAvailable(&frame))
         >= 0;
    ToriRSMapEd_BufFree(&frame);
    return ok;
}

static void
live_line(
    struct ToriRSMapEd* maped,
    const char* line,
    uint32_t length)
{
    struct ToriRSMapEdBuf payload;
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutBytes(&payload, line, length);
    jobs_broadcast(maped, TORIRSMAPED_FACT_LIVE_LINE, &payload);
    ToriRSMapEd_BufFree(&payload);
}

static void
jobs_live_broadcast_status(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, jobs->live_state);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->launched);
    ToriRSMapEd_PutString(&payload, jobs->address);
    ToriRSMapEd_PutString(&payload, jobs->server_name);
    ToriRSMapEd_PutU32(&payload, jobs->players);
    ToriRSMapEd_PutU32(&payload, jobs->tick);
    ToriRSMapEd_PutString(&payload, jobs->script_stamp);
    ToriRSMapEd_PutString(&payload, jobs->servpack_stamp);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->stale_count);
    for( int i = 0; i < jobs->stale_count; i++ )
        ToriRSMapEd_PutString(&payload, jobs->stale[i]);
    ToriRSMapEd_PutString(&payload, jobs->message);
    ToriRSMapEd_PutU32(&payload, jobs->run_step);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->pending_square_count);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->pending_spawn_count);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->pending_archive_count);
    jobs_broadcast(maped, TORIRSMAPED_FACT_LIVE_STATUS, &payload);
    ToriRSMapEd_BufFree(&payload);
}

/** End the link (BYE when it is up), leaving `state` and `message`. A RUN
 *  waiting on its reload stops here. */
static void
jobs_live_drop(
    struct ToriRSMapEd* maped,
    uint32_t state,
    const char* message)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;

    if( jobs->live_state == TORIRSMAPED_LIVE_ATTACHED )
        live_send(maped, TORIRSMAPED_CTL_BYE, NULL);
    if( jobs->have_link && jobs->live_state != TORIRSMAPED_LIVE_DETACHED )
        jobs->link.close(jobs->link.ctx);
    jobs->live_state = state;
    jobs->launched = 0;
    jobs->hello_sent = 0;
    jobs->players = 0;
    jobs->tick = 0;
    jobs->players_known = 0;
    jobs->player_row_count = 0;
    jobs->players_active = 0;
    jobs->logins = 0;
    ToriRSMapEd_BufFree(&jobs->ctl_in);
    memset(&jobs->ctl_in, 0, sizeof(jobs->ctl_in));
    snprintf(jobs->message, sizeof(jobs->message), "%s", message ? message : "");
    if( jobs->reload_waiting )
        jobs_run_finish(maped, EDITOR_HOST_IO_ERROR, TORIRSMAPED_RUN_STEP_RELOAD,
            "the live link dropped during the reload", NULL, NULL, 0);
    else if( jobs->run_play_waiting )
        jobs_run_play_decide(maped, 0);
}

static void
live_send_status_request(struct ToriRSMapEd* maped)
{
    maped->jobs->last_status = time(NULL);
    live_send(maped, TORIRSMAPED_CTL_STATUS, NULL);
}

static void
live_send_subscribe(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuf payload;

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, jobs->sub_player);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->sub_count);
    for( int i = 0; i < jobs->sub_count; i++ )
        ToriRSMapEd_PutU32(&payload, jobs->sub_varps[i]);
    live_send(maped, TORIRSMAPED_CTL_SUBSCRIBE, &payload);
    ToriRSMapEd_BufFree(&payload);
}

/* ---- the play client ---------------------------------------------------------- */

static int
live_name_equal(
    const char* a,
    const char* b)
{
    while( *a && *b )
    {
        char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
        char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b - 'A' + 'a') : *b;
        /* The server keys an account by its file name: ' ' and '-' are '_'. */
        if( ca == ' ' || ca == '-' )
            ca = '_';
        if( cb == ' ' || cb == '-' )
            cb = '_';
        if( ca != cb )
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void
jobs_play_account(
    const struct ToriRSMapEdJobs* jobs,
    char* user,
    size_t user_size,
    char* pass,
    size_t pass_size)
{
    user[0] = '\0';
    pass[0] = '\0';
    if( jobs->have_lane )
        ToriRSMapEd_LanePlayAccount(&jobs->lane, user, user_size, pass, pass_size);
}

static void
jobs_live_broadcast_play(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuf payload;
    char user[16];
    char pass[24];
    char url[1024];

    jobs_play_account(jobs, user, sizeof(user), pass, sizeof(pass));
    url[0] = '\0';
    if( jobs->have_lane )
        ToriRSMapEd_LaneWebPlayUrl(&jobs->lane, url, sizeof(url));
    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutU32(&payload, jobs->play_state);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->play_browser);
    ToriRSMapEd_PutString(&payload, user);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->play_pid);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->play_exit);
    ToriRSMapEd_PutString(&payload, jobs->play_message);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->play_x);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->play_z);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->play_level);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->play_starts);
    ToriRSMapEd_PutString(&payload, url);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->players_known);
    ToriRSMapEd_PutU32(&payload, jobs->logins);
    ToriRSMapEd_PutU32(&payload, jobs->players_active);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->player_row_count);
    for( int i = 0; i < jobs->player_row_count; i++ )
    {
        const struct ToriRSMapEdJobsPlayer* row = &jobs->player_rows[i];
        ToriRSMapEd_PutString(&payload, row->display[0] ? row->display : row->account);
        ToriRSMapEd_PutU32(&payload, (uint32_t)row->x);
        ToriRSMapEd_PutU32(&payload, (uint32_t)row->z);
        ToriRSMapEd_PutU32(&payload, (uint32_t)row->level);
    }
    jobs_broadcast(maped, TORIRSMAPED_FACT_LIVE_PLAY, &payload);
    ToriRSMapEd_BufFree(&payload);
}

static void
jobs_play_note(
    struct ToriRSMapEdJobs* jobs,
    uint32_t state,
    const char* format,
    ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

static void
jobs_play_note(
    struct ToriRSMapEdJobs* jobs,
    uint32_t state,
    const char* format,
    ...)
{
    va_list args;

    jobs->play_state = state;
    va_start(args, format);
    vsnprintf(jobs->play_message, sizeof(jobs->play_message), format, args);
    va_end(args);
    TORIRS_LOG("torirsmaped: play client: %s\n", jobs->play_message);
}

/** The process half of liveness: 1 when it exited since the last look (the
 *  state is EXITED then). */
static int
jobs_play_poll(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    int status = 0;

    if( !jobs->have_play || jobs->play_pid <= 0 )
        return 0;
    if( jobs->play.alive(jobs->play.ctx, &status) )
        return 0;
    jobs->play_pid = 0;
    jobs->play_exit = status;
    jobs->play_wait_lane = 0;
    jobs_play_note(jobs, TORIRSMAPED_PLAY_EXITED, "the play client exited (%s %d)",
        status < 0 ? "signal" : "status", status < 0 ? -status : status);
    return 1;
}

/** The server half: fold the latest player list into the play state. 1 when
 *  the state changed. */
static int
jobs_play_observe(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    char user[16];
    char pass[24];
    const struct ToriRSMapEdJobsPlayer* found = NULL;
    uint32_t const before = jobs->play_state;

    jobs_play_account(jobs, user, sizeof(user), pass, sizeof(pass));
    if( !user[0] )
        return 0;
    for( int i = 0; i < jobs->player_row_count && !found; i++ )
    {
        if( live_name_equal(jobs->player_rows[i].account, user)
            || live_name_equal(jobs->player_rows[i].display, user) )
            found = &jobs->player_rows[i];
    }
    if( found )
    {
        int const relogged = jobs->play_state == TORIRSMAPED_PLAY_ONLINE
                             && (found->slot != jobs->play_slot || found->generation != jobs->play_generation);
        int const moved = found->x != jobs->play_x || found->z != jobs->play_z || found->level != jobs->play_level;
        jobs->play_x = found->x;
        jobs->play_z = found->z;
        jobs->play_level = found->level;
        if( jobs->play_state != TORIRSMAPED_PLAY_ONLINE || relogged )
        {
            jobs->play_slot = found->slot;
            jobs->play_generation = found->generation;
            jobs->play_wait_lane = 0;
            jobs_play_note(jobs, TORIRSMAPED_PLAY_ONLINE, "%s as %s at %d,%d level %d%s",
                relogged ? "logged in again" : "logged in", user, found->x, found->z, found->level,
                jobs->play_pid > 0 || jobs->play_browser ? "" : " (a client this server did not start)");
            return 1;
        }
        return moved;
    }
    if( jobs->play_state == TORIRSMAPED_PLAY_ONLINE )
    {
        if( jobs->play_pid > 0 )
            jobs_play_note(jobs, TORIRSMAPED_PLAY_LOGGED_OUT, "%s left the server (the process still runs)", user);
        else
            jobs_play_note(jobs, TORIRSMAPED_PLAY_NONE, "%s left the server", user);
        jobs->play_x = jobs->play_z = jobs->play_level = -1;
    }
    return jobs->play_state != before;
}

/** Spawn now (the lane's ports are up). */
static enum EditorHost_Status
jobs_play_spawn(
    struct ToriRSMapEd* maped,
    char* why,
    size_t why_size)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdPlaySpec spec;
    enum EditorHost_Status status;
    int pid = 0;

    memset(&spec, 0, sizeof(spec));
    jobs_play_account(jobs, spec.user, sizeof(spec.user), spec.pass, sizeof(spec.pass));
    why[0] = '\0';
    jobs->play_wait_lane = 0;
    status = jobs->play.start(jobs->play.ctx, &spec, &pid, why, why_size);
    if( status != EDITOR_HOST_OK )
    {
        jobs->play_pid = 0;
        jobs_play_note(jobs, TORIRSMAPED_PLAY_EXITED, "the play client did not start: %s",
            why[0] ? why : "the launcher refused");
        return status;
    }
    jobs->play_pid = pid;
    jobs->play_exit = 0;
    jobs->play_starts++;
    jobs->play_started = time(NULL);
    jobs->play_slot = 0;
    jobs->play_generation = 0;
    jobs->play_x = jobs->play_z = jobs->play_level = -1;
    jobs_play_note(jobs, TORIRSMAPED_PLAY_STARTING, "starting as %s (pid %d)", spec.user, pid);
    return EDITOR_HOST_OK;
}

/** Start the play client (a running one is stopped first). Waits for the
 *  lane's ports without blocking: the spawn happens in a later service pass
 *  when they are not up yet. */
static enum EditorHost_Status
jobs_play_start(
    struct ToriRSMapEd* maped,
    char* why,
    size_t why_size)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;

    why[0] = '\0';
    if( jobs->play_browser )
    {
        snprintf(why, why_size, "a browser window plays this lane (its own tab)");
        return EDITOR_HOST_UNSUPPORTED;
    }
    if( !jobs->have_play || !jobs->have_lane )
    {
        snprintf(why, why_size, "no play client is configured (the lane has no play binary, or this host "
                                "cannot start processes)");
        return EDITOR_HOST_UNSUPPORTED;
    }
    if( jobs->play_pid > 0 )
        jobs->play.stop(jobs->play.ctx);
    jobs->play_pid = 0;
    if( !jobs->play.ready(jobs->play.ctx) )
    {
        jobs->play_wait_lane = 1;
        jobs->play_wait_deadline = time(NULL) + 60;
        jobs->play_x = jobs->play_z = jobs->play_level = -1;
        jobs_play_note(jobs, TORIRSMAPED_PLAY_STARTING, "waiting for the lane's game and JS5 ports");
        return EDITOR_HOST_OK;
    }
    return jobs_play_spawn(maped, why, why_size);
}

static void
jobs_play_stop(
    struct ToriRSMapEd* maped,
    const char* message)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;

    if( jobs->have_play && jobs->play_pid > 0 )
        jobs->play.stop(jobs->play.ctx);
    jobs->play_pid = 0;
    jobs->play_wait_lane = 0;
    jobs->play_x = jobs->play_z = jobs->play_level = -1;
    jobs_play_note(jobs, TORIRSMAPED_PLAY_NONE, "%s", message);
}

static void
live_send_players_request(struct ToriRSMapEd* maped)
{
    maped->jobs->last_players = time(NULL);
    live_send(maped, TORIRSMAPED_CTL_PLAYERS, NULL);
}

/**
 * 1 when the lane has something to launch on: a server pack, a script pack
 * and a client cache under the live dir, and no packs or client-cache build
 * whose last run failed. Else 0 and `why` says what to do. A server started
 * on a lane RUN never built dies at once ("no server pack at <live>/servpack")
 * and the panel used to show only its exit status (owner, 2026-11-02).
 */
static int
jobs_lane_built(
    const struct ToriRSMapEdJobs* jobs,
    char* why,
    size_t why_size)
{
#if TORIRSMAPED_JOBS_NATIVE
    static const char* const needed[] = { "servpack/pack.manifest", "scripts/pack.manifest", "cache" };
    char path[TORIRSMAPED_LANE_PATH_MAX + 64];

    if( jobs->build_failed_what )
    {
        snprintf(why, why_size, "the last %s build failed (its lines are in the log): fix it and RUN; the "
                                "lane is not launched on a failed build",
            ToriRSMapEd_BuildWhatName(jobs->build_failed_what));
        return 0;
    }
    for( size_t i = 0; i < sizeof(needed) / sizeof(needed[0]); i++ )
    {
        snprintf(path, sizeof(path), "%s/%s", jobs->lane.live_dir, needed[i]);
        if( access(path, F_OK) != 0 )
        {
            snprintf(why, why_size, "the lane has not been built (no %s): press RUN -- it builds the packs "
                                    "and the client cache, then launches the lane and the play client",
                path);
            return 0;
        }
    }
#else
    (void)jobs;
    (void)why;
    (void)why_size;
#endif
    return 1;
}

/** Launch the lane's servers and start dialling their game port (the
 *  LIVE_ATTACH launch path, and RUN's when the lane is down). */
static enum EditorHost_Status
jobs_live_launch(
    struct ToriRSMapEd* maped,
    char* why,
    size_t why_size)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdLiveTarget target;
    enum EditorHost_Status status;

    why[0] = '\0';
    if( !jobs->have_link )
    {
        snprintf(why, why_size, "this editor server has no live link");
        return EDITOR_HOST_UNSUPPORTED;
    }
    if( !jobs->have_lane )
    {
        snprintf(why, why_size, "launch needs a lane (none is bound)");
        return EDITOR_HOST_UNSUPPORTED;
    }
    if( !jobs_lane_built(jobs, why, why_size) )
    {
        snprintf(jobs->message, sizeof(jobs->message), "not launched: %s", why);
        jobs_live_broadcast_status(maped);
        return EDITOR_HOST_REFUSED;
    }
    if( jobs->live_state == TORIRSMAPED_LIVE_CONNECTING || jobs->live_state == TORIRSMAPED_LIVE_ATTACHED )
        jobs_live_drop(maped, TORIRSMAPED_LIVE_DETACHED, "re-attaching");
    memset(&target, 0, sizeof(target));
    target.launch = 1;
    snprintf(target.host, sizeof(target.host), "127.0.0.1");
    target.port = jobs->lane.game_port;
    snprintf(jobs->address, sizeof(jobs->address), "%s:%d", target.host, target.port);
    status = jobs->link.open(jobs->link.ctx, &target, why, why_size);
    if( status != EDITOR_HOST_OK )
    {
        jobs->live_state = TORIRSMAPED_LIVE_FAILED;
        snprintf(jobs->message, sizeof(jobs->message), "%s", why[0] ? why : "the link did not open");
        jobs_live_broadcast_status(maped);
        return status;
    }
    jobs->live_state = TORIRSMAPED_LIVE_CONNECTING;
    jobs->launched = 1;
    jobs->hello_sent = 0;
    snprintf(jobs->message, sizeof(jobs->message),
        "launched; connecting to %s (a torirsserver answers its control link after its first login)",
        jobs->address);
    jobs_live_broadcast_status(maped);
    live_service(maped);
    return EDITOR_HOST_OK;
}

/** 1 when the lane can be launched from here (a lane with a server binary
 *  and a link to dial it with). */
static int
jobs_lane_launchable(const struct ToriRSMapEdJobs* jobs)
{
    return jobs->have_link && jobs->have_lane && jobs->lane.server_bin[0];
}

/* ---- RUN's reload ----------------------------------------------------------- */

/** RUN's last step (RUN_STEP_PLAY): the play client after the reload (or the
 *  launch), then RUN_DONE with the reload's outcome and what the play client
 *  did. `relaunched`: the lane was just launched, so a client from before is
 *  connected to nothing and starts again. */
static void
jobs_run_play_decide(
    struct ToriRSMapEd* maped,
    int relaunched)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    char user[16];
    char pass[24];
    char why[256];
    char note[384];
    char message[sizeof(jobs->run_message) + sizeof(note) + 4];
    enum EditorHost_Status status = EDITOR_HOST_OK;
    struct ToriRSMapEdBuf restart;
    uint32_t restart_count;
    uint32_t counts[4];

    jobs->run_play_waiting = 0;
    jobs_run_step_enter(maped, TORIRSMAPED_RUN_STEP_PLAY);
    jobs_play_account(jobs, user, sizeof(user), pass, sizeof(pass));
    (void)jobs_play_poll(maped);
    note[0] = '\0';
    if( jobs->play_browser )
    {
        char url[1024];
        url[0] = '\0';
        if( jobs->have_lane )
            ToriRSMapEd_LaneWebPlayUrl(&jobs->lane, url, sizeof(url));
        if( jobs->play_state == TORIRSMAPED_PLAY_ONLINE && !relaunched )
            snprintf(note, sizeof(note), "reloaded in the running browser client (%s at %d,%d)", user,
                jobs->play_x, jobs->play_z);
        else if( url[0] )
            snprintf(note, sizeof(note), "open the play tab: %.300s", url);
        else
            snprintf(note, sizeof(note), "no play tab: the lane serves no web page (no io_server)");
    }
    else if( !jobs->have_play )
        snprintf(note, sizeof(note), "no play client (the lane has no play binary)");
    else if( relaunched )
    {
        status = jobs_play_start(maped, why, sizeof(why));
        snprintf(note, sizeof(note), status == EDITOR_HOST_OK ? "play client started as %s on the launched lane"
                                                              : "the play client did not start: %s",
            status == EDITOR_HOST_OK ? user : why);
    }
    else if( jobs->play_state == TORIRSMAPED_PLAY_ONLINE )
        snprintf(note, sizeof(note), "reloaded in the running client (%s at %d,%d, no relog)", user,
            jobs->play_x, jobs->play_z);
    else if( jobs->play_state == TORIRSMAPED_PLAY_STARTING
             && (jobs->play_wait_lane || !jobs->players_known || time(NULL) - jobs->play_started < 120) )
        snprintf(note, sizeof(note), jobs->players_known
                                         ? "the play client is still logging in (it loads the new content)"
                                         : "the play client runs (pid %d; this server does not report its "
                                           "players)",
            jobs->play_pid);
    else
    {
        uint32_t const was = jobs->play_state;
        int const exit_status = jobs->play_exit;
        status = jobs_play_start(maped, why, sizeof(why));
        if( status != EDITOR_HOST_OK )
            snprintf(note, sizeof(note), "the play client did not start: %s", why);
        else if( was == TORIRSMAPED_PLAY_EXITED )
            snprintf(note, sizeof(note), "restarted the play client (it had exited, status %d)", exit_status);
        else if( was == TORIRSMAPED_PLAY_LOGGED_OUT || was == TORIRSMAPED_PLAY_STARTING )
            snprintf(note, sizeof(note), "restarted the play client (it was not on the server)");
        else
            snprintf(note, sizeof(note), "started the play client (it logs in as %s)", user);
    }
    jobs_live_broadcast_play(maped);

    snprintf(message, sizeof(message), "%s; %s", jobs->run_message, note);
    memcpy(counts, jobs->run_counts, sizeof(counts));
    restart = jobs->run_restart;
    restart_count = jobs->run_restart_count;
    memset(&jobs->run_restart, 0, sizeof(jobs->run_restart));
    jobs->run_restart_count = 0;
    if( status == EDITOR_HOST_OK )
        jobs_run_finish(maped, EDITOR_HOST_OK, TORIRSMAPED_RUN_STEP_DONE, message, counts, &restart,
            restart_count);
    else
        jobs_run_finish(maped, status, TORIRSMAPED_RUN_STEP_PLAY, message, counts, &restart, restart_count);
    ToriRSMapEd_BufFree(&restart);
}

/** The reload landed (or the lane was launched on the new build): keep its
 *  outcome for RUN_DONE and go to the play step -- after one fresh player
 *  list when the server reports them. */
static void
jobs_run_play_step(
    struct ToriRSMapEd* maped,
    const char* message,
    const uint32_t* counts,
    const struct ToriRSMapEdBuf* restart,
    uint32_t restart_count,
    int relaunched)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;

    snprintf(jobs->run_message, sizeof(jobs->run_message), "%s", message);
    for( int i = 0; i < 4; i++ )
        jobs->run_counts[i] = counts ? counts[i] : 0;
    ToriRSMapEd_BufFree(&jobs->run_restart);
    memset(&jobs->run_restart, 0, sizeof(jobs->run_restart));
    if( restart && ToriRSMapEd_BufAvailable(restart) > 0 )
        ToriRSMapEd_BufWrite(&jobs->run_restart, ToriRSMapEd_BufPeek(restart), ToriRSMapEd_BufAvailable(restart));
    jobs->run_restart_count = restart_count;
    jobs_run_step_enter(maped, TORIRSMAPED_RUN_STEP_PLAY);
    if( !relaunched && jobs->players_known && jobs->live_state == TORIRSMAPED_LIVE_ATTACHED )
    {
        jobs->run_play_waiting = 1;
        jobs->run_play_deadline = time(NULL) + 5;
        live_send_players_request(maped);
        jobs_live_broadcast_status(maped);
        return;
    }
    jobs_run_play_decide(maped, relaunched);
}

static void
jobs_run_reload(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuf payload;
    char manifest[TORIRSMAPED_LANE_PATH_MAX + 32];

    if( !jobs->have_link || jobs->live_state != TORIRSMAPED_LIVE_ATTACHED )
    {
        char why[256];
        enum EditorHost_Status status;

        if( !jobs->have_link )
        {
            jobs_run_finish(maped, EDITOR_HOST_UNSUPPORTED, TORIRSMAPED_RUN_STEP_RELOAD,
                "this server has no live link; the packs are built", NULL, NULL, 0);
            return;
        }
        if( jobs->live_state == TORIRSMAPED_LIVE_CONNECTING && !jobs->launched )
        {
            jobs_run_finish(maped, EDITOR_HOST_UNSUPPORTED, TORIRSMAPED_RUN_STEP_RELOAD,
                "the live link is still connecting; the packs are built (RUN again once it is attached)", NULL,
                NULL, 0);
            return;
        }
        if( !jobs_lane_launchable(jobs) )
        {
            jobs_run_finish(maped, EDITOR_HOST_UNSUPPORTED, TORIRSMAPED_RUN_STEP_RELOAD,
                "no live server is attached (LIVE_ATTACH first); the packs are built. Launch the lane first",
                NULL, NULL, 0);
            return;
        }
        /* The lane is down (or launched and not answering yet -- no login has
         * built its world, so nothing is on it): (re)launch it on what this
         * RUN just built. A fresh server boots from those packs and the
         * play client's JS5 serves that cache, so nothing is owed a reload. */
        status = jobs_live_launch(maped, why, sizeof(why));
        if( status != EDITOR_HOST_OK )
        {
            char message[384];
            snprintf(message, sizeof(message), "the lane was not up and did not launch: %s", why);
            jobs_run_finish(maped, status, TORIRSMAPED_RUN_STEP_RELOAD, message, NULL, NULL, 0);
            return;
        }
        jobs->pending_square_count = 0;
        jobs->pending_spawn_count = 0;
        jobs->pending_archive_count = 0;
        jobs_run_play_step(maped, "the lane was not up: launched it on the new packs and cache", NULL, NULL, 0, 1);
        return;
    }
    if( jobs->have_lane )
        snprintf(manifest, sizeof(manifest), "%s/scripts/pack.manifest", jobs->lane.live_dir);
    else
        manifest[0] = '\0';

    memset(&payload, 0, sizeof(payload));
    ToriRSMapEd_PutString(&payload, manifest);
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->pending_archive_count);
    for( int i = 0; i < jobs->pending_archive_count; i++ )
    {
        ToriRSMapEd_PutU32(&payload, jobs->pending_archives[2 * i]);
        ToriRSMapEd_PutU32(&payload, jobs->pending_archives[2 * i + 1]);
    }
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->pending_square_count);
    for( int i = 0; i < jobs->pending_square_count; i++ )
    {
        ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->pending_squares[2 * i]);
        ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->pending_squares[2 * i + 1]);
    }
    ToriRSMapEd_PutU32(&payload, (uint32_t)jobs->pending_spawn_count);
    for( int i = 0; i < jobs->pending_spawn_count; i++ )
        ToriRSMapEd_PutString(&payload, jobs->pending_spawns[i]);
    if( !live_send(maped, TORIRSMAPED_CTL_RELOAD, &payload) )
    {
        ToriRSMapEd_BufFree(&payload);
        jobs_live_drop(maped, TORIRSMAPED_LIVE_FAILED, "the live link is gone");
        jobs_run_finish(maped, EDITOR_HOST_IO_ERROR, TORIRSMAPED_RUN_STEP_RELOAD,
            "the live link is gone", NULL, NULL, 0);
        return;
    }
    ToriRSMapEd_BufFree(&payload);
    jobs->reload_waiting = 1;
    jobs->reload_deadline = time(NULL) + LIVE_RELOAD_TIMEOUT_S;
    jobs_live_broadcast_status(maped);
}

/* ---- control facts ----------------------------------------------------------- */

/** One whole control fact. Returns 0 when it is malformed (the link drops). */
static int
live_fact(
    struct ToriRSMapEd* maped,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdReader reader;

    ToriRSMapEd_ReaderInit(&reader, body, length);
    if( type == TORIRSMAPED_CTL_FACT_HELLO )
    {
        uint32_t version = ToriRSMapEd_GetU32(&reader);
        uint32_t ok = ToriRSMapEd_GetU32(&reader);
        char name[sizeof(jobs->server_name)];
        uint32_t players;
        ToriRSMapEd_GetString(&reader, name, sizeof(name));
        players = ToriRSMapEd_GetU32(&reader);
        if( !ToriRSMapEd_ReaderDone(&reader) )
            return 0;
        if( !ok || version != TORIRSMAPED_CTL_VERSION )
        {
            char why[160];
            snprintf(why, sizeof(why),
                !ok ? "the game server refused the control link (gate ticket?)"
                    : "the game server speaks control version %u",
                version);
            jobs_live_drop(maped, TORIRSMAPED_LIVE_FAILED, why);
            jobs_live_broadcast_status(maped);
            return 1;
        }
        snprintf(jobs->server_name, sizeof(jobs->server_name), "%s", name);
        jobs->players = players;
        jobs->live_state = TORIRSMAPED_LIVE_ATTACHED;
        snprintf(jobs->message, sizeof(jobs->message), "attached to %s", jobs->address);
        TORIRS_LOG("torirsmaped: live link attached to %s (%s, %u players)\n",
            jobs->address, name, players);
        if( jobs->sub_active )
            live_send_subscribe(maped);
        live_send_status_request(maped);
        jobs_live_broadcast_status(maped);
        return 1;
    }
    if( type == TORIRSMAPED_CTL_FACT_STATUS )
    {
        uint32_t players = ToriRSMapEd_GetU32(&reader);
        uint32_t tick = ToriRSMapEd_GetU32(&reader);
        uint32_t stale;
        char script_stamp[sizeof(jobs->script_stamp)];
        char servpack_stamp[sizeof(jobs->servpack_stamp)];
        ToriRSMapEd_GetString(&reader, script_stamp, sizeof(script_stamp));
        ToriRSMapEd_GetString(&reader, servpack_stamp, sizeof(servpack_stamp));
        stale = ToriRSMapEd_GetU32(&reader);
        if( reader.bad )
            return 0;
        jobs->stale_count = 0;
        for( uint32_t i = 0; i < stale && !reader.bad; i++ )
        {
            char row[256];
            ToriRSMapEd_GetString(&reader, row, sizeof(row));
            if( jobs->stale_count < JOBS_STALE_MAX )
                snprintf(jobs->stale[jobs->stale_count++], sizeof(jobs->stale[0]), "%s", row);
        }
        if( !ToriRSMapEd_ReaderDone(&reader) )
            return 0;
        jobs->players = players;
        jobs->tick = tick;
        snprintf(jobs->script_stamp, sizeof(jobs->script_stamp), "%s", script_stamp);
        snprintf(jobs->servpack_stamp, sizeof(jobs->servpack_stamp), "%s", servpack_stamp);
        jobs_live_broadcast_status(maped);
        return 1;
    }
    if( type == TORIRSMAPED_CTL_FACT_LINE )
    {
        uint32_t line_length;
        const uint8_t* line = ToriRSMapEd_GetBytes(&reader, &line_length);
        if( !line || !ToriRSMapEd_ReaderDone(&reader) )
            return 0;
        live_line(maped, (const char*)line, line_length);
        return 1;
    }
    if( type == TORIRSMAPED_CTL_FACT_RELOAD_DONE )
    {
        uint32_t counts[4];
        uint32_t status = ToriRSMapEd_GetU32(&reader);
        uint32_t restart_count;
        struct ToriRSMapEdBuf restart;
        for( int i = 0; i < 4; i++ )
            counts[i] = ToriRSMapEd_GetU32(&reader);
        restart_count = ToriRSMapEd_GetU32(&reader);
        memset(&restart, 0, sizeof(restart));
        for( uint32_t i = 0; i < restart_count && !reader.bad; i++ )
        {
            uint32_t row_length;
            const uint8_t* row = ToriRSMapEd_GetBytes(&reader, &row_length);
            if( row )
                ToriRSMapEd_PutBytes(&restart, row, row_length);
        }
        if( !ToriRSMapEd_ReaderDone(&reader) )
        {
            ToriRSMapEd_BufFree(&restart);
            return 0;
        }
        if( !jobs->reload_waiting )
        {
            /* Someone else's reload (another editor on the same world). */
            ToriRSMapEd_BufFree(&restart);
            return 1;
        }
        jobs->reload_waiting = 0;
        /* 0 OK; 1 RESTART_NEEDED (torirs_server_control.h): everything that can
         * be taken live was, and the restart rows name the rest -- re-sending
         * them would only be refused again, so that is a finished RUN too. A 1
         * that names nothing to restart is a failed reload, as 2 is. */
        if( status == 0 || (status == 1 && restart_count > 0) )
        {
            char message[160];
            snprintf(message, sizeof(message),
                "%u script(s) swapped, %u config group(s), %u square(s), %u spawn change(s)%s",
                counts[0], counts[1], counts[2], counts[3],
                restart_count ? "; some changes need a restart" : "");
            /* Everything named is live: nothing is pending any more. */
            jobs->pending_square_count = 0;
            jobs->pending_spawn_count = 0;
            jobs->pending_archive_count = 0;
            live_send_status_request(maped);
            /* Then the play client: is it on the server to have taken it? */
            jobs_run_play_step(maped, message, counts, &restart, restart_count, 0);
        }
        else
        {
            char message[160];
            snprintf(message, sizeof(message), "the game server's reload failed (status %u)", status);
            jobs_run_finish(maped, EDITOR_HOST_REFUSED, TORIRSMAPED_RUN_STEP_RELOAD, message, counts,
                &restart, restart_count);
        }
        ToriRSMapEd_BufFree(&restart);
        return 1;
    }
    if( type == TORIRSMAPED_CTL_FACT_PLAYERS )
    {
        uint32_t logins = ToriRSMapEd_GetU32(&reader);
        uint32_t active = ToriRSMapEd_GetU32(&reader);
        uint32_t rows = ToriRSMapEd_GetU32(&reader);
        struct ToriRSMapEdJobsPlayer kept[JOBS_PLAYER_ROWS];
        int kept_count = 0;
        char user[16];
        char pass[24];
        int changed;
        if( reader.bad )
            return 0;
        jobs_play_account(jobs, user, sizeof(user), pass, sizeof(pass));
        for( uint32_t i = 0; i < rows && !reader.bad; i++ )
        {
            struct ToriRSMapEdJobsPlayer row;
            memset(&row, 0, sizeof(row));
            ToriRSMapEd_GetString(&reader, row.account, sizeof(row.account));
            ToriRSMapEd_GetString(&reader, row.display, sizeof(row.display));
            row.slot = ToriRSMapEd_GetU32(&reader);
            row.generation = ToriRSMapEd_GetU32(&reader);
            row.x = (int32_t)ToriRSMapEd_GetU32(&reader);
            row.z = (int32_t)ToriRSMapEd_GetU32(&reader);
            row.level = (int32_t)ToriRSMapEd_GetU32(&reader);
            /* Keep the play account always, then the first others. */
            if( user[0] && (live_name_equal(row.account, user) || live_name_equal(row.display, user)) )
            {
                if( kept_count == JOBS_PLAYER_ROWS )
                    kept_count--;
                memmove(&kept[1], &kept[0], (size_t)kept_count * sizeof(kept[0]));
                kept[0] = row;
                kept_count++;
            }
            else if( kept_count < JOBS_PLAYER_ROWS )
                kept[kept_count++] = row;
        }
        if( !ToriRSMapEd_ReaderDone(&reader) )
            return 0;
        changed = !jobs->players_known || logins != jobs->logins || active != jobs->players_active
                  || kept_count != jobs->player_row_count
                  || memcmp(kept, jobs->player_rows, (size_t)kept_count * sizeof(kept[0])) != 0;
        jobs->players_known = 1;
        jobs->logins = logins;
        jobs->players_active = active;
        jobs->players = active;
        memcpy(jobs->player_rows, kept, (size_t)kept_count * sizeof(kept[0]));
        jobs->player_row_count = kept_count;
        jobs->last_players = time(NULL);
        changed |= jobs_play_observe(maped);
        if( jobs->run_play_waiting )
            jobs_run_play_decide(maped, 0);
        else if( changed )
            jobs_live_broadcast_play(maped);
        return 1;
    }
    if( type == TORIRSMAPED_CTL_FACT_VARS )
    {
        char player[64];
        uint32_t n;
        struct ToriRSMapEdBuf payload;
        ToriRSMapEd_GetString(&reader, player, sizeof(player));
        n = ToriRSMapEd_GetU32(&reader);
        if( reader.bad || n > (reader.length - reader.at) / 8 )
            return 0;
        memset(&payload, 0, sizeof(payload));
        ToriRSMapEd_PutString(&payload, player);
        ToriRSMapEd_PutU32(&payload, n);
        for( uint32_t i = 0; i < n; i++ )
        {
            ToriRSMapEd_PutU32(&payload, ToriRSMapEd_GetU32(&reader));
            ToriRSMapEd_PutU32(&payload, ToriRSMapEd_GetU32(&reader));
        }
        if( !ToriRSMapEd_ReaderDone(&reader) )
        {
            ToriRSMapEd_BufFree(&payload);
            return 0;
        }
        jobs_broadcast(maped, TORIRSMAPED_FACT_LIVE_VARS, &payload);
        ToriRSMapEd_BufFree(&payload);
        return 1;
    }
    TORIRS_LOG("torirsmaped: ignoring control fact %u from the game server\n", type);
    return 1;
}

/**
 * A launched server died (or never answered): repeat the last lines of its
 * log, `<live>/logs/server.log`, to the panel, and append the last one to
 * `message` -- the server says why it refused to run ("no server pack at
 * ..."), and "exited (status 1)" alone told nobody anything.
 */
static void
jobs_live_server_log_tail(
    struct ToriRSMapEd* maped,
    char* message,
    size_t message_size)
{
#if TORIRSMAPED_JOBS_NATIVE
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    char path[TORIRSMAPED_LANE_PATH_MAX + 32];
    char text[8192];
    const char* lines[JOBS_FAILURE_LINES];
    int count = 0;
    FILE* file;
    long size;
    size_t got;
    char line[700];

    if( !jobs->have_lane )
        return;
    snprintf(path, sizeof(path), "%s/logs/server.log", jobs->lane.live_dir);
    file = fopen(path, "rb");
    if( !file )
        return;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, size > (long)sizeof(text) - 1 ? size - (long)sizeof(text) + 1 : 0, SEEK_SET);
    got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    /* The last JOBS_FAILURE_LINES non-empty lines, a ring. */
    for( char* p = text; *p; )
    {
        char* end = strchr(p, '\n');
        if( end )
            *end = '\0';
        if( *p )
            lines[count++ % JOBS_FAILURE_LINES] = p;
        if( !end )
            break;
        p = end + 1;
    }
    if( count == 0 )
        return;
    snprintf(line, sizeof(line), "the lane's game server log (%s) ends:", path);
    live_line(maped, line, (uint32_t)strlen(line));
    for( int i = count > JOBS_FAILURE_LINES ? count - JOBS_FAILURE_LINES : 0; i < count; i++ )
    {
        snprintf(line, sizeof(line), "  %s", lines[i % JOBS_FAILURE_LINES]);
        live_line(maped, line, (uint32_t)strlen(line));
    }
    {
        size_t const at = strlen(message);
        snprintf(message + at, message_size - at, ": %s", lines[(count - 1) % JOBS_FAILURE_LINES]);
    }
#else
    (void)maped;
    (void)message;
    (void)message_size;
#endif
}

static void
live_service(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    uint8_t chunk[16384];
    char why[512];
    int peer_gone = 0;

    if( !jobs->have_link || jobs->live_state == TORIRSMAPED_LIVE_DETACHED
        || jobs->live_state == TORIRSMAPED_LIVE_FAILED )
        return;

    if( jobs->live_state == TORIRSMAPED_LIVE_CONNECTING && !jobs->hello_sent )
    {
        int up = jobs->link.poll(jobs->link.ctx, why, sizeof(why));
        if( up < 0 )
        {
            int const was_launched = jobs->launched;
            char message[sizeof(jobs->message)];
            snprintf(message, sizeof(message), "%s", why);
            if( was_launched )
                jobs_live_server_log_tail(maped, message, sizeof(message));
            jobs_live_drop(maped, TORIRSMAPED_LIVE_FAILED, message);
            if( was_launched && jobs->play_wait_lane )
            {
                /* It was waiting for this lane's ports: they never come. */
                jobs->play_wait_lane = 0;
                jobs_play_note(jobs, TORIRSMAPED_PLAY_EXITED,
                    "the lane's game server did not come up; the play client was not started");
                jobs_live_broadcast_play(maped);
            }
            jobs_live_broadcast_status(maped);
            return;
        }
        if( up == 0 )
            return;
        {
            /* The select byte, then HELLO with the gate ticket. */
            struct ToriRSMapEdBuf frame;
            struct ToriRSMapEdBuf payload;
            uint8_t select = TORIRSMAPED_CTL_SELECT_BYTE;
            memset(&frame, 0, sizeof(frame));
            memset(&payload, 0, sizeof(payload));
            ToriRSMapEd_BufWrite(&frame, &select, 1);
            ToriRSMapEd_PutU32(&payload, TORIRSMAPED_CTL_VERSION);
            ToriRSMapEd_PutString(&payload, jobs->ticket);
            ToriRSMapEd_CtlFrame(&frame, TORIRSMAPED_CTL_HELLO, &payload);
            if( jobs->link.send(jobs->link.ctx, ToriRSMapEd_BufPeek(&frame),
                    ToriRSMapEd_BufAvailable(&frame))
                < 0 )
            {
                ToriRSMapEd_BufFree(&frame);
                ToriRSMapEd_BufFree(&payload);
                jobs_live_drop(maped, TORIRSMAPED_LIVE_FAILED, "the game server closed the connection");
                jobs_live_broadcast_status(maped);
                return;
            }
            ToriRSMapEd_BufFree(&frame);
            ToriRSMapEd_BufFree(&payload);
            jobs->hello_sent = 1;
        }
    }

    for( ;; )
    {
        int got = jobs->link.recv(jobs->link.ctx, chunk, sizeof(chunk));
        if( got < 0 )
        {
            /* Gone -- but its last frames (a refusal's HELLO, a reload's
             * answer) may be whole in the buffer: act on them first. */
            peer_gone = 1;
            break;
        }
        if( got == 0 )
            break;
        ToriRSMapEd_BufWrite(&jobs->ctl_in, chunk, got);
    }
    while( ToriRSMapEd_BufAvailable(&jobs->ctl_in) >= 8 )
    {
        const uint8_t* head = ToriRSMapEd_BufPeek(&jobs->ctl_in);
        uint32_t const type = ToriRSMapEd_ReadU32(head);
        uint32_t const length = ToriRSMapEd_ReadU32(head + 4);
        if( length > LIVE_CTL_PAYLOAD_MAX )
        {
            jobs_live_drop(maped, TORIRSMAPED_LIVE_FAILED, "the game server sent a corrupt frame");
            jobs_live_broadcast_status(maped);
            return;
        }
        if( ToriRSMapEd_BufAvailable(&jobs->ctl_in) < 8 + (int)length )
            break;
        ToriRSMapEd_BufConsume(&jobs->ctl_in, 8);
        if( !live_fact(maped, type, ToriRSMapEd_BufPeek(&jobs->ctl_in), length) )
        {
            jobs_live_drop(maped, TORIRSMAPED_LIVE_FAILED, "the game server sent a malformed frame");
            jobs_live_broadcast_status(maped);
            return;
        }
        if( jobs->live_state == TORIRSMAPED_LIVE_DETACHED || jobs->live_state == TORIRSMAPED_LIVE_FAILED )
            return; /* live_fact dropped the link */
        ToriRSMapEd_BufConsume(&jobs->ctl_in, (int)length);
    }
    if( peer_gone )
    {
        jobs_live_drop(maped, TORIRSMAPED_LIVE_FAILED, "the game server closed the control link");
        jobs_live_broadcast_status(maped);
        return;
    }

    if( jobs->live_state == TORIRSMAPED_LIVE_ATTACHED )
    {
        time_t const now = time(NULL);
        /* Every second while the play client is on its way in, so its line
         * turns "logged in" promptly; else at the status cadence. */
        int const players_period = jobs->play_state == TORIRSMAPED_PLAY_STARTING
                                           || jobs->play_state == TORIRSMAPED_PLAY_LOGGED_OUT
                                       ? 1
                                       : LIVE_STATUS_PERIOD_S;
        if( now - jobs->last_status >= LIVE_STATUS_PERIOD_S )
            live_send_status_request(maped);
        if( jobs->players_known && now - jobs->last_players >= players_period )
            live_send_players_request(maped);
        if( jobs->reload_waiting && now > jobs->reload_deadline )
            jobs_run_finish(maped, EDITOR_HOST_IO_ERROR, TORIRSMAPED_RUN_STEP_RELOAD,
                "the game server did not answer the reload", NULL, NULL, 0);
    }
}

/** The play client's half of a service pass: reap it, spawn it once the
 *  lane's ports are up, finish a RUN whose player list never came. */
static void
play_service(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    int changed = 0;

    if( jobs->have_play && jobs->play_wait_lane )
    {
        if( jobs->play.ready(jobs->play.ctx) )
        {
            char why[256];
            (void)jobs_play_spawn(maped, why, sizeof(why));
            changed = 1;
        }
        else if( time(NULL) > jobs->play_wait_deadline )
        {
            jobs->play_wait_lane = 0;
            jobs_play_note(jobs, TORIRSMAPED_PLAY_EXITED,
                "the lane's game and JS5 ports did not come up in 60 s; the play client was not started");
            changed = 1;
        }
    }
    changed |= jobs_play_poll(maped);
    if( jobs->run_play_waiting && time(NULL) > jobs->run_play_deadline )
        jobs_run_play_decide(maped, 0);
    else if( changed )
        jobs_live_broadcast_play(maped);
}

/* ---- LIVE_* ------------------------------------------------------------------ */

static void
live_unavailable(
    struct ToriRSMapEd* maped,
    const char* why)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    char saved[sizeof(jobs->message)];

    /* The status says why, without forgetting the link's own message. */
    snprintf(saved, sizeof(saved), "%s", jobs->message);
    snprintf(jobs->message, sizeof(jobs->message), "%s", why);
    jobs_live_broadcast_status(maped);
    snprintf(jobs->message, sizeof(jobs->message), "%s", saved);
}

static void
handle_live(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdReader reader;

    ToriRSMapEd_ReaderInit(&reader, body, length);

    if( type == TORIRSMAPED_REQ_LIVE_ATTACH )
    {
        uint32_t mode = ToriRSMapEd_GetU32(&reader);
        char address[256];
        char ticket[256];
        struct ToriRSMapEdLiveTarget target;
        char why[256];
        enum EditorHost_Status status;

        ToriRSMapEd_GetString(&reader, address, sizeof(address));
        ToriRSMapEd_GetString(&reader, ticket, sizeof(ticket));
        if( !ToriRSMapEd_ReaderDone(&reader) || mode > 1 )
        {
            session->alive = 0;
            return;
        }
        if( !jobs->have_link )
        {
            live_unavailable(maped, "this editor server has no live link");
            return;
        }
        if( jobs->run_step != TORIRSMAPED_RUN_STEP_IDLE )
        {
            live_unavailable(maped, "a RUN is in flight; attach after it");
            return;
        }
        if( mode == 1 )
        {
            if( !jobs->have_lane )
            {
                live_unavailable(maped, "launch needs a lane (none is bound)");
                return;
            }
            snprintf(jobs->ticket, sizeof(jobs->ticket), "%s", ticket);
            (void)jobs_live_launch(maped, why, sizeof(why));
            return;
        }
        if( jobs->live_state == TORIRSMAPED_LIVE_CONNECTING || jobs->live_state == TORIRSMAPED_LIVE_ATTACHED )
            jobs_live_drop(maped, TORIRSMAPED_LIVE_DETACHED, "re-attaching");
        memset(&target, 0, sizeof(target));
        target.launch = 0;
        {
            const char* colon = strrchr(address, ':');
            long port = colon ? strtol(colon + 1, NULL, 10) : 0;
            if( !colon || colon == address || port <= 0 || port > 65535 )
            {
                live_unavailable(maped, "attach needs an address host:port");
                return;
            }
            snprintf(target.host, sizeof(target.host), "%.*s", (int)(colon - address), address);
            target.port = (int)port;
        }
        snprintf(jobs->address, sizeof(jobs->address), "%s:%d", target.host, target.port);
        snprintf(jobs->ticket, sizeof(jobs->ticket), "%s", ticket);
        why[0] = '\0';
        status = jobs->link.open(jobs->link.ctx, &target, why, sizeof(why));
        if( status != EDITOR_HOST_OK )
        {
            jobs->live_state = TORIRSMAPED_LIVE_FAILED;
            snprintf(jobs->message, sizeof(jobs->message), "%s", why[0] ? why : "the link did not open");
            jobs_live_broadcast_status(maped);
            return;
        }
        jobs->live_state = TORIRSMAPED_LIVE_CONNECTING;
        jobs->launched = (int)mode;
        jobs->hello_sent = 0;
        snprintf(jobs->message, sizeof(jobs->message), "%s %s", mode ? "launched; connecting to" : "connecting to",
            jobs->address);
        jobs_live_broadcast_status(maped);
        live_service(maped);
        return;
    }
    if( type == TORIRSMAPED_REQ_LIVE_DETACH )
    {
        if( length != 0 )
        {
            session->alive = 0;
            return;
        }
        {
            int const was_launched = jobs->launched;
            if( jobs->have_link && jobs->live_state != TORIRSMAPED_LIVE_DETACHED )
                jobs_live_drop(maped, TORIRSMAPED_LIVE_DETACHED, "detached");
            else
                snprintf(jobs->message, sizeof(jobs->message), "detached");
            jobs->live_state = TORIRSMAPED_LIVE_DETACHED;
            /* The lane's servers stopped: its play client would sit on a
             * dead connection. It goes with them. */
            if( was_launched && jobs->play_pid > 0 )
            {
                jobs_play_stop(maped, "stopped with the lane");
                jobs_live_broadcast_play(maped);
            }
        }
        jobs_live_broadcast_status(maped);
        return;
    }
    if( type == TORIRSMAPED_REQ_LIVE_PLAY )
    {
        uint32_t action = ToriRSMapEd_GetU32(&reader);
        char why[256];
        if( !ToriRSMapEd_ReaderDone(&reader) || action > TORIRSMAPED_PLAY_BROWSER )
        {
            session->alive = 0;
            return;
        }
        (void)jobs_play_poll(maped);
        if( action == TORIRSMAPED_PLAY_BROWSER )
        {
            if( jobs->play_pid > 0 )
                jobs->play.stop(jobs->play.ctx);
            jobs->play_pid = 0;
            jobs->play_wait_lane = 0;
            jobs->play_browser = 1;
            (void)jobs_play_observe(maped);
            if( jobs->play_state != TORIRSMAPED_PLAY_ONLINE )
                jobs_play_note(jobs, TORIRSMAPED_PLAY_NONE, "a browser window plays this lane");
        }
        else if( action == TORIRSMAPED_PLAY_STOP )
            jobs_play_stop(maped, "stopped");
        else if( action == TORIRSMAPED_PLAY_START && jobs->play_state == TORIRSMAPED_PLAY_ONLINE )
        {
            char user[16];
            char pass[24];
            jobs_play_account(jobs, user, sizeof(user), pass, sizeof(pass));
            snprintf(jobs->play_message, sizeof(jobs->play_message), "already logged in as %s at %d,%d", user,
                jobs->play_x, jobs->play_z);
        }
        else if( action == TORIRSMAPED_PLAY_START && jobs->play_state == TORIRSMAPED_PLAY_STARTING
                 && (jobs->play_pid > 0 || jobs->play_wait_lane) )
            ; /* on its way in: the line already says so */
        else
        {
            /* START or RESTART: the lane must be up for it to log into. A
             * lane that is down is launched (it boots on whatever its last
             * build left in the live dir). */
            enum EditorHost_Status status = EDITOR_HOST_OK;
            if( (jobs->live_state == TORIRSMAPED_LIVE_DETACHED || jobs->live_state == TORIRSMAPED_LIVE_FAILED)
                && jobs_lane_launchable(jobs) && !jobs->play_browser && jobs->have_play )
                status = jobs_live_launch(maped, why, sizeof(why));
            if( status == EDITOR_HOST_OK )
                status = jobs_play_start(maped, why, sizeof(why));
            if( status != EDITOR_HOST_OK )
                snprintf(jobs->play_message, sizeof(jobs->play_message), "%s", why);
        }
        jobs_live_broadcast_play(maped);
        return;
    }
    if( type == TORIRSMAPED_REQ_LIVE_CHEAT )
    {
        char player[64];
        char line[512];
        struct ToriRSMapEdBuf payload;
        ToriRSMapEd_GetString(&reader, player, sizeof(player));
        ToriRSMapEd_GetString(&reader, line, sizeof(line));
        if( !ToriRSMapEd_ReaderDone(&reader) )
        {
            session->alive = 0;
            return;
        }
        if( jobs->live_state != TORIRSMAPED_LIVE_ATTACHED )
        {
            live_unavailable(maped, "no live server is attached");
            return;
        }
        memset(&payload, 0, sizeof(payload));
        ToriRSMapEd_PutString(&payload, player);
        ToriRSMapEd_PutString(&payload, line);
        live_send(maped, TORIRSMAPED_CTL_CHEAT, &payload);
        ToriRSMapEd_BufFree(&payload);
        return;
    }
    if( type == TORIRSMAPED_REQ_LIVE_SUBSCRIBE )
    {
        char player[64];
        uint32_t n;
        ToriRSMapEd_GetString(&reader, player, sizeof(player));
        n = ToriRSMapEd_GetU32(&reader);
        if( reader.bad || n > JOBS_SUBSCRIBE_MAX || reader.length - reader.at != 4 * n )
        {
            session->alive = 0;
            return;
        }
        snprintf(jobs->sub_player, sizeof(jobs->sub_player), "%s", player);
        jobs->sub_count = (int)n;
        for( uint32_t i = 0; i < n; i++ )
            jobs->sub_varps[i] = ToriRSMapEd_GetU32(&reader);
        jobs->sub_active = n > 0;
        if( jobs->live_state != TORIRSMAPED_LIVE_ATTACHED )
        {
            live_unavailable(maped, "no live server is attached; the subscription waits for one");
            return;
        }
        live_send_subscribe(maped);
        return;
    }
    session->alive = 0;
}

/* ------------------------------------------------------------------ */
/* Service                                                             */
/* ------------------------------------------------------------------ */

void
ToriRSMapEd_Service(struct ToriRSMapEd* maped)
{
    assert(maped);
    assert(maped->open);
    assert(maped->jobs);
    jobs_build_service(maped);
    live_service(maped);
    play_service(maped);
    jobs_progress_service(maped);
    /* The catalog's warm build and freshness sweeps, a slice at a time
     * (torirs_maped_catalog.h ToriRSMapEdCatalog_Service). */
    if( maped->catalog )
    {
        uint32_t const maps = ToriRSMapEdCatalog_MapsRevision(maped->catalog);
        (void)ToriRSMapEdCatalog_Service(maped->catalog, ToriRSMapEdCatalog_Built(maped->catalog)
                                                             ? TORIRSMAPED_CATALOG_SLICE_MS
                                                             : TORIRSMAPED_CATALOG_WARM_SLICE_MS);
        /* A sweep that merged a square changed on disk: every window's atlas. */
        if( ToriRSMapEdCatalog_MapsRevision(maped->catalog) != maps )
            catalog_atlas_changed(maped, 0);
    }
}

int
ToriRSMapEd_Busy(const struct ToriRSMapEd* maped)
{
    const struct ToriRSMapEdJobs* jobs;

    assert(maped);
    assert(maped->jobs);
    jobs = maped->jobs;
    return jobs->building || jobs->run_step != TORIRSMAPED_RUN_STEP_IDLE
           || jobs->live_state == TORIRSMAPED_LIVE_CONNECTING || jobs->play_wait_lane
           || (maped->catalog && ToriRSMapEdCatalog_Working(maped->catalog));
}

void
ToriRSMapEd_SetLiveLink(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdLiveLink* link)
{
    struct ToriRSMapEdJobs* jobs;

    assert(maped);
    assert(maped->jobs);
    jobs = maped->jobs;

    if( jobs->have_link )
    {
        jobs_live_drop(maped, TORIRSMAPED_LIVE_DETACHED, "the live link was replaced");
        if( jobs->link.free_ )
            jobs->link.free_(jobs->link.ctx);
    }
    memset(&jobs->link, 0, sizeof(jobs->link));
    jobs->have_link = 0;
    maped->capabilities &= ~(uint32_t)TORIRSMAPED_CAP_LIVE;
    if( link )
    {
        assert(link->open);
        assert(link->poll);
        assert(link->recv);
        assert(link->send);
        assert(link->close);
        jobs->link = *link;
        jobs->have_link = 1;
        maped->capabilities |= TORIRSMAPED_CAP_LIVE;
    }
}

/* ------------------------------------------------------------------ */
/* The play client's files                                             */
/* ------------------------------------------------------------------ */

/**
 * Write a client manifest for the lane from lane->play_manifest: the
 * [cache:boot] dir becomes `cache_dir`, [net:boot] dials the lane's game
 * port on 127.0.0.1 over tcp, and with `absolute` every "../" or "./" value
 * is made absolute against the source's directory (the copy lives elsewhere).
 */
static int
play_manifest_write(
    const struct ToriRSMapEdLane* lane,
    const char* out_path,
    const char* cache_dir,
    int absolute,
    char* why,
    size_t why_size)
{
    char source_dir[TORIRSMAPED_LANE_PATH_MAX];
    char line[4096];
    char section[128] = "";
    const char* slash;
    FILE* in;
    FILE* out;
    int failed = 0;

    in = fopen(lane->play_manifest, "rb");
    if( !in )
    {
        snprintf(why, why_size, "cannot read the play manifest %s: %s", lane->play_manifest, strerror(errno));
        return 0;
    }
    out = fopen(out_path, "wb");
    if( !out )
    {
        snprintf(why, why_size, "cannot write %s: %s", out_path, strerror(errno));
        fclose(in);
        return 0;
    }
    slash = strrchr(lane->play_manifest, '/');
    if( slash )
        snprintf(source_dir, sizeof(source_dir), "%.*s", (int)(slash - lane->play_manifest), lane->play_manifest);
    else
        snprintf(source_dir, sizeof(source_dir), ".");
    fprintf(out, "; Written by ToriRSMapEd for the live lane's play client from\n; %s\n; (cache dir, net "
                 "host and port%s rewritten; regenerated at every start).\n\n",
        lane->play_manifest, absolute ? ", relative paths" : "");
    while( fgets(line, sizeof(line), in) )
    {
        size_t length = strlen(line);
        const char* eq;
        while( length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r') )
            line[--length] = '\0';
        if( line[0] == '[' )
        {
            const char* close = strchr(line, ']');
            snprintf(section, sizeof(section), "%.*s", close ? (int)(close - line + 1) : (int)length, line);
            fprintf(out, "%s\n", line);
            continue;
        }
        eq = strchr(line, '=');
        if( strcmp(section, "[cache:boot]") == 0 && strncmp(line, "dir=", 4) == 0 )
            fprintf(out, "dir=%s\n", cache_dir);
        else if( strcmp(section, "[net:boot]") == 0 && strncmp(line, "host=", 5) == 0 )
            fprintf(out, "host=127.0.0.1\n");
        else if( strcmp(section, "[net:boot]") == 0 && strncmp(line, "port=", 5) == 0 )
            fprintf(out, "port=%d\n", lane->game_port);
        else if( strcmp(section, "[net:boot]") == 0 && strncmp(line, "transport=", 10) == 0 )
            fprintf(out, "transport=tcp\n");
        else if( absolute && eq && line[0] != ';' && line[0] != '#'
                 && (strncmp(eq + 1, "../", 3) == 0 || strncmp(eq + 1, "./", 2) == 0) )
            fprintf(out, "%.*s%s/%s\n", (int)(eq - line + 1), line, source_dir, eq + 1);
        else
            fprintf(out, "%s\n", line);
    }
    if( ferror(in) )
        failed = 1;
    fclose(in);
    if( fclose(out) != 0 )
        failed = 1;
    if( failed )
    {
        snprintf(why, why_size, "writing %s failed", out_path);
        return 0;
    }
    return 1;
}

int
ToriRSMapEd_PlayManifestWrite(
    const struct ToriRSMapEdLane* lane,
    const char* out_path,
    char* why,
    size_t why_size)
{
    char live[TORIRSMAPED_LANE_PATH_MAX + 256];
    char cache[TORIRSMAPED_LANE_PATH_MAX + 300];

    assert(lane);
    assert(out_path);
    assert(why);
    build_resolve(lane->live_dir, live, sizeof(live));
    snprintf(cache, sizeof(cache), "%s/play/cache", live);
    return play_manifest_write(lane, out_path, cache, 1, why, why_size);
}

/* ------------------------------------------------------------------ */
/* The native link                                                     */
/* ------------------------------------------------------------------ */

#if TORIRSMAPED_JOBS_NATIVE

/* A peer that hangs up must not take the editor server with it. */
#if defined(MSG_NOSIGNAL)
#define LIVE_SEND_FLAGS MSG_NOSIGNAL
#else
#define LIVE_SEND_FLAGS 0
#endif

enum
{
    LIVE_CONNECT_RETRY_MS = 250,
    LIVE_CONNECT_GIVE_UP_MS = 60 * 1000,
};

struct live_native
{
    int have_lane;
    struct ToriRSMapEdLane lane;
    struct ToriRSMapEdLiveTarget target;
    int fd;
    int connecting;
    int connected;
    int64_t started_ms;
    int64_t next_try_ms;
    /** The game server, js5_server and io_server a launch started. */
    struct jobs_process launched[3];
    struct ToriRSMapEdBuf out;
};

static int64_t
live_now_ms(void)
{
    return LaunchSessions_MonotonicMs(NULL);
}

/** The socket layer up once per process (winsock counts its starts, and the
 *  host that started it first may be the game client or no one). */
static void
live_sockets_up(void)
{
#ifdef _WIN32
    static int started = 0;
    if( !started )
    {
        WSADATA data;
        started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
#endif
}

static void
live_socket_nonblocking(int fd)
{
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket((SOCKET)fd, FIONBIO, &on);
#else
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
}

/** `mkdir -p`. */
static void
live_mkdirs(const char* path)
{
    char partial[TORIRSMAPED_LANE_PATH_MAX + 64];
    size_t used = 0;

    for( const char* scan = path; *scan && used + 1 < sizeof(partial); scan++ )
    {
        if( *scan == '/' && used > 0 )
        {
            partial[used] = '\0';
            jobs_mkdir(partial);
        }
        partial[used++] = *scan;
    }
    partial[used] = '\0';
    if( used > 0 )
        jobs_mkdir(partial);
}

/**
 * The tree io_server's --boot-root is for a browser playing the lane:
 * <live>/play/web/manifests/manifest_<rev>.ini (the play manifest with its
 * relative paths kept and dir=../cache), <live>/play/web/cache, a link to the
 * lane's cache (io_server opens only a cache inside its boot root), and
 * <live>/play/web/revconfig, a link to the repository's revconfig/ the
 * manifest's relative paths name. `out_root` gets <live>/play/web.
 */
static void play_copy_fixture(const struct ToriRSMapEdLane* lane, const char* live, const char* user);

static void
live_web_play_prepare(
    const struct ToriRSMapEdLane* lane,
    char* out_root,
    size_t out_size)
{
    char live[TORIRSMAPED_LANE_PATH_MAX + 256];
    char path[TORIRSMAPED_LANE_PATH_MAX + 512];
    char target[TORIRSMAPED_LANE_PATH_MAX + 64];
    char why[512];

    build_resolve(lane->live_dir, live, sizeof(live));
    snprintf(out_root, out_size, "%s/play/web", live);
    snprintf(path, sizeof(path), "%s/play/web/manifests", live);
    live_mkdirs(path);
    snprintf(path, sizeof(path), "%s/play/web/revconfig", live);
    snprintf(target, sizeof(target), "%s/revconfig", lane->repo_root);
    if( access(path, F_OK) != 0 && !ToriRSMapEdLanes_LinkDirectory(target, path) )
        TORIRS_LOG("torirsmaped: cannot link %s -> %s: %s\n", path, target, strerror(errno));
    snprintf(path, sizeof(path), "%s/play/web/cache", live);
    snprintf(target, sizeof(target), "%s/cache", live);
    if( access(path, F_OK) != 0 && !ToriRSMapEdLanes_LinkDirectory(target, path) )
        TORIRS_LOG("torirsmaped: cannot link %s -> %s: %s\n", path, target, strerror(errno));
    snprintf(path, sizeof(path), "%s/play/web/manifests/manifest_%s.ini", live, lane->rev);
    if( !play_manifest_write(lane, path, "../cache", 0, why, sizeof(why)) )
        TORIRS_LOG("torirsmaped: the web play manifest: %s\n", why);
}

static void
live_native_disconnect(struct live_native* native)
{
    if( native->fd >= 0 )
        live_socket_close(native->fd);
    native->fd = -1;
    native->connecting = 0;
    native->connected = 0;
    ToriRSMapEd_BufFree(&native->out);
    memset(&native->out, 0, sizeof(native->out));
}

/** Start one non-blocking connect. 1 started (or done), 0 failed now. */
static int
live_native_dial(struct live_native* native)
{
    struct addrinfo hints;
    struct addrinfo* found = NULL;
    char port[16];
    int fd;
    int rc;
    int one = 1;

    live_sockets_up();
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port, sizeof(port), "%d", native->target.port);
    if( getaddrinfo(native->target.host, port, &hints, &found) != 0 || !found )
        return 0;
    fd = (int)socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if( fd < 0 )
    {
        freeaddrinfo(found);
        return 0;
    }
    live_socket_nonblocking(fd);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
#if defined(SO_NOSIGPIPE)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    rc = connect(fd, found->ai_addr, (int)found->ai_addrlen);
    freeaddrinfo(found);
    if( rc != 0 && !LIVE_WOULD_BLOCK(live_socket_error()) )
    {
        live_socket_close(fd);
        return 0;
    }
    native->fd = fd;
    native->connecting = rc != 0;
    native->connected = rc == 0;
    return 1;
}

static void
live_native_stop_launched(struct live_native* native)
{
    for( int i = 0; i < 3; i++ )
        jobs_process_stop(&native->launched[i], 2000);
}

static enum EditorHost_Status
live_native_open(
    void* ctx,
    const struct ToriRSMapEdLiveTarget* target,
    char* why,
    size_t why_size)
{
    struct live_native* native = ctx;

    assert(native);
    assert(target);
    assert(why);

    live_native_disconnect(native);
    native->target = *target;
    if( target->launch )
    {
        const struct ToriRSMapEdLane* lane = &native->lane;
        char port[3][16];
        char env[6][TORIRSMAPED_LANE_PATH_MAX + 32];
        const char* extra_env[7];
        char cache[TORIRSMAPED_LANE_PATH_MAX + 16];
        char logs[3][TORIRSMAPED_LANE_PATH_MAX + 32];
        char web_root[TORIRSMAPED_LANE_PATH_MAX + 32];
        char rev_number[16];
        const char* digits;

        if( !native->have_lane || !lane->server_bin[0] )
        {
            snprintf(why, why_size, "launch needs the lane's torirsserver binary");
            return EDITOR_HOST_UNSUPPORTED;
        }
        snprintf(port[0], sizeof(port[0]), "%d", lane->game_port);
        snprintf(port[1], sizeof(port[1]), "%d", lane->js5_port);
        snprintf(port[2], sizeof(port[2]), "%d", lane->io_port);
        snprintf(cache, sizeof(cache), "%s/cache", lane->live_dir);
        digits = lane->rev;
        while( *digits && (*digits < '0' || *digits > '9') )
            digits++;
        snprintf(rev_number, sizeof(rev_number), "%s", *digits ? digits : "241");
        snprintf(env[0], sizeof(env[0]), "TORIRSSERVER_CONTENT=%s", lane->content_dir);
        snprintf(env[1], sizeof(env[1]), "TORIRSSERVER_CACHE=%s", cache);
        snprintf(env[2], sizeof(env[2]), "TORIRSSERVER_SCRIPTS=%s/scripts", lane->live_dir);
        snprintf(env[3], sizeof(env[3]), "TORIRSSERVER_PACK_DIR=%s/servpack", lane->live_dir);
        /* The lane's accounts and saves (the play account's among them) stay
         * in the lane, never in whatever directory the editor runs from. */
        snprintf(env[4], sizeof(env[4]), "TORIRSSERVER_SAVES=%s/saves", lane->live_dir);
        /* A dev lane: the play client goes straight into the world rather
         * than waiting on the welcome screen's Play. */
        snprintf(env[5], sizeof(env[5]), "TORIRSSERVER_SKIP_WELCOME=1");
        for( int i = 0; i < 6; i++ )
            extra_env[i] = env[i];
        extra_env[6] = NULL;
        snprintf(logs[0], sizeof(logs[0]), "%s/logs", lane->live_dir);
        live_mkdirs(logs[0]);
        snprintf(logs[0], sizeof(logs[0]), "%s/saves", lane->live_dir);
        live_mkdirs(logs[0]);
        snprintf(logs[0], sizeof(logs[0]), "%s/logs/server.log", lane->live_dir);
        snprintf(logs[1], sizeof(logs[1]), "%s/logs/js5.log", lane->live_dir);
        snprintf(logs[2], sizeof(logs[2]), "%s/logs/io.log", lane->live_dir);
        {
            char* argv[] = { (char*)lane->server_bin, port[0], (char*)"--rev", (char*)lane->rev, NULL };
            if( !jobs_process_start(argv, extra_env, logs[0], &native->launched[0]) )
            {
                snprintf(why, why_size, "cannot start %s: %s", lane->server_bin, strerror(errno));
                return EDITOR_HOST_IO_ERROR;
            }
        }
        if( lane->js5_bin[0] )
        {
            char* argv[] = { (char*)lane->js5_bin, (char*)"--cache", cache, (char*)"--revision",
                rev_number, (char*)"--port", port[1], (char*)"--max-pending", (char*)"65535", NULL };
            if( !jobs_process_start(argv, NULL, logs[1], &native->launched[1]) )
            {
                snprintf(why, why_size, "cannot start %s: %s", lane->js5_bin, strerror(errno));
                live_native_stop_launched(native);
                return EDITOR_HOST_IO_ERROR;
            }
        }
        if( lane->io_bin[0] && lane->io_manifest[0] )
        {
            /* --boot-root is the web play tree (live_web_play_prepare): the
             * page boots manifests/manifest_<rev>.ini from it, which names
             * the lane's game port and (through the tree's link) its cache.
             * No --manifest preopen: io_server reads a preopen's file from
             * its working directory but resolves the cache it names under
             * the boot root, and the page's first request opens it anyway. */
            char* argv[] = { (char*)lane->io_bin, (char*)"--root", (char*)lane->io_root, (char*)"--port", port[2],
                (char*)"--boot-root", web_root, NULL };
            live_web_play_prepare(lane, web_root, sizeof(web_root));
            /* A browser plays as the lane's account too: give it the
             * fixture's first save, as the native play client gets, or the
             * play tab opens on "Set display name" in a new character. */
            {
                char user[64];
                char pass[64];
                char live[TORIRSMAPED_LANE_PATH_MAX + 256];
                build_resolve(lane->live_dir, live, sizeof(live));
                ToriRSMapEd_LanePlayAccount(lane, user, sizeof(user), pass, sizeof(pass));
                play_copy_fixture(lane, live, user);
            }
            if( !jobs_process_start(argv, NULL, logs[2], &native->launched[2]) )
            {
                snprintf(why, why_size, "cannot start %s: %s", lane->io_bin, strerror(errno));
                live_native_stop_launched(native);
                return EDITOR_HOST_IO_ERROR;
            }
        }
    }
    native->started_ms = live_now_ms();
    native->next_try_ms = native->started_ms;
    return EDITOR_HOST_OK;
}

static int
live_native_poll(
    void* ctx,
    char* why,
    size_t why_size)
{
    struct live_native* native = ctx;
    int64_t const now = live_now_ms();

    assert(native);
    if( native->connected )
        return 1;
    /* A launched server that died before it listened will never answer. */
    if( native->target.launch && native->launched[0].pid > 0 )
    {
        int status = 0;
        if( jobs_process_ended(&native->launched[0], &status) )
        {
            live_native_disconnect(native);
            live_native_stop_launched(native);
            snprintf(why, why_size, "the launched game server exited (status %d) before it answered", status);
            return -1;
        }
    }
    if( native->connecting )
    {
        fd_set writable;
        struct timeval zero = { 0, 0 };
        int error = 0;
        socklen_t length = sizeof(error);
        FD_ZERO(&writable);
        FD_SET(native->fd, &writable);
        if( select(native->fd + 1, NULL, &writable, NULL, &zero) <= 0 )
            return 0;
        getsockopt(native->fd, SOL_SOCKET, SO_ERROR, (char*)&error, &length);
        if( error == 0 )
        {
            native->connecting = 0;
            native->connected = 1;
            return 1;
        }
        live_native_disconnect(native);
        native->next_try_ms = now + LIVE_CONNECT_RETRY_MS;
    }
    if( now - native->started_ms > LIVE_CONNECT_GIVE_UP_MS )
    {
        snprintf(why, why_size, "%s:%d did not accept a connection within %d s", native->target.host,
            native->target.port, LIVE_CONNECT_GIVE_UP_MS / 1000);
        live_native_stop_launched(native);
        return -1;
    }
    if( now < native->next_try_ms )
        return 0;
    if( !live_native_dial(native) )
    {
        if( !native->target.launch )
        {
            snprintf(why, why_size, "cannot connect to %s:%d", native->target.host, native->target.port);
            return -1;
        }
        native->next_try_ms = now + LIVE_CONNECT_RETRY_MS;
        return 0;
    }
    return native->connected ? 1 : 0;
}

static int
live_native_flush(struct live_native* native)
{
    while( ToriRSMapEd_BufAvailable(&native->out) > 0 )
    {
        int sent = (int)send(native->fd, (const char*)ToriRSMapEd_BufPeek(&native->out),
            ToriRSMapEd_BufAvailable(&native->out), LIVE_SEND_FLAGS);
        if( sent > 0 )
        {
            ToriRSMapEd_BufConsume(&native->out, sent);
            continue;
        }
        if( sent < 0 && LIVE_WOULD_BLOCK(live_socket_error()) )
            return 1;
        return 0;
    }
    return 1;
}

static int
live_native_recv(
    void* ctx,
    uint8_t* dst,
    int max)
{
    struct live_native* native = ctx;
    int got;

    assert(native);
    if( !native->connected )
        return native->connecting ? 0 : -1;
    if( !live_native_flush(native) )
        return -1;
    got = (int)recv(native->fd, (char*)dst, max, 0);
    if( got > 0 )
        return got;
    if( got == 0 )
        return -1;
    if( LIVE_WOULD_BLOCK(live_socket_error()) )
        return 0;
    return -1;
}

static int
live_native_send(
    void* ctx,
    const uint8_t* src,
    int len)
{
    struct live_native* native = ctx;

    assert(native);
    if( !native->connected )
        return -1;
    ToriRSMapEd_BufWrite(&native->out, src, len);
    return live_native_flush(native) ? len : -1;
}

static void
live_native_close(void* ctx)
{
    struct live_native* native = ctx;
    assert(native);
    if( native->connected )
        live_native_flush(native);
    live_native_disconnect(native);
    live_native_stop_launched(native);
}

static void
live_native_free(void* ctx)
{
    struct live_native* native = ctx;
    if( !native )
        return;
    live_native_close(native);
    free(native);
}

/* ---- the native play launcher -------------------------------------------- */

enum
{
    PLAY_ENV_MAX = 32,
};

struct play_native
{
    struct ToriRSMapEdLane lane;
    struct jobs_process process;
};

/** 1 when 127.0.0.1:`port` accepts a connection now (loopback answers at
 *  once either way). */
static int
play_port_open(int port)
{
    struct sockaddr_in addr;
    int fd;
    int ok;

    live_sockets_up();
    fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if( fd < 0 )
        return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok = connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    live_socket_close(fd);
    return ok;
}

static int
play_native_ready(void* ctx)
{
    struct play_native* play = ctx;

    assert(play);
    if( !play_port_open(play->lane.game_port) )
        return 0;
    return !play->lane.js5_bin[0] || play_port_open(play->lane.js5_port);
}

/** The fixture as the account's first save: copied once (a save the server
 *  wrote since is the player's), its `name =` the account's. */
static void
play_copy_fixture(
    const struct ToriRSMapEdLane* lane,
    const char* live,
    const char* user)
{
    char path[TORIRSMAPED_LANE_PATH_MAX + 128];
    char line[4096];
    char file[64];
    size_t used = 0;
    FILE* in;
    FILE* out;

    if( !lane->play_fixture[0] )
        return;
    for( const char* scan = user; *scan && used + 1 < sizeof(file); scan++ )
    {
        char c = *scan;
        if( c >= 'A' && c <= 'Z' )
            file[used++] = (char)(c - 'A' + 'a');
        else if( (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') )
            file[used++] = c;
        else if( c == ' ' || c == '-' || c == '_' )
            file[used++] = '_';
    }
    file[used] = '\0';
    snprintf(path, sizeof(path), "%s/saves/%s.ini", live, file);
    if( access(path, F_OK) == 0 )
        return;
    in = fopen(lane->play_fixture, "rb");
    if( !in )
    {
        TORIRS_LOG("torirsmaped: play client: no fixture save %s (%s): the account starts as a new "
                   "character\n", lane->play_fixture, strerror(errno));
        return;
    }
    out = fopen(path, "wb");
    if( !out )
    {
        TORIRS_LOG("torirsmaped: play client: cannot write %s: %s\n", path, strerror(errno));
        fclose(in);
        return;
    }
    while( fgets(line, sizeof(line), in) )
    {
        if( strncmp(line, "name = ", 7) == 0 || strncmp(line, "name=", 5) == 0 )
            fprintf(out, "name = %s\n", user);
        else
            fputs(line, out);
    }
    fclose(in);
    fclose(out);
}

static enum EditorHost_Status
play_native_start(
    void* ctx,
    const struct ToriRSMapEdPlaySpec* spec,
    int* out_pid,
    char* why,
    size_t why_size)
{
    static const char* const drop[] = { "TORIRS_SIM_HOTKEY", "TORIRS_EXIT_BMP", "TORIRS_MAX_FRAMES",
        "TORIRS_BMP_SERIES", "TORIRS_PRESENT_BMP*", "TORIRS_SCREENSHOT*", "TORIRS_SHOT_FRAME", "TORIRS_EDITOR_*",
        "TORIRS_PREFS", "TORIRSMAPED_*", NULL };
    struct play_native* play = ctx;
    const struct ToriRSMapEdLane* lane;
    char live[TORIRSMAPED_LANE_PATH_MAX + 256];
    char path[TORIRSMAPED_LANE_PATH_MAX + 320];
    char manifest[TORIRSMAPED_LANE_PATH_MAX + 320];
    char log_path[TORIRSMAPED_LANE_PATH_MAX + 320];
    char js5_port[16];
    char extra_store[PLAY_ENV_MAX][TORIRSMAPED_LANE_PATH_MAX + 64];
    const char* extra[PLAY_ENV_MAX + 1];
    int extra_count = 0;
    char** env;
    int started;

    assert(play);
    assert(spec);
    assert(out_pid);
    assert(why);
    lane = &play->lane;
    jobs_process_stop(&play->process, 3000);
#ifdef _WIN32
    if( access(lane->play_bin, F_OK) != 0 ) /* Windows has no execute bit */
#else
    if( access(lane->play_bin, X_OK) != 0 )
#endif
    {
        snprintf(why, why_size, "the play client %s is not an executable (%s)", lane->play_bin, strerror(errno));
        return EDITOR_HOST_UNSUPPORTED;
    }
    build_resolve(lane->live_dir, live, sizeof(live));
    snprintf(path, sizeof(path), "%s/play/cache", live);
    live_mkdirs(path);
    snprintf(path, sizeof(path), "%s/saves", live);
    live_mkdirs(path);
    snprintf(path, sizeof(path), "%s/logs", live);
    live_mkdirs(path);
    snprintf(manifest, sizeof(manifest), "%s/play/manifest.ini", live);
    if( !ToriRSMapEd_PlayManifestWrite(lane, manifest, why, why_size) )
        return EDITOR_HOST_IO_ERROR;
    play_copy_fixture(lane, live, spec->user);

    snprintf(extra_store[extra_count], sizeof(extra_store[0]), "TORIRS_PREFS=%s/play/preferences.ini", live);
    extra[extra_count] = extra_store[extra_count];
    extra_count++;
    snprintf(extra_store[extra_count], sizeof(extra_store[0]), "TORIRS_STDERR_UNBUFFERED=1");
    extra[extra_count] = extra_store[extra_count];
    extra_count++;
    /* TORIRSMAPED_PLAY_ENV_<NAME>=<value> -> NAME=<value>: the play client's
     * own keys (its capture settings in a headless run). */
    for( int i = 0; environ[i] && extra_count < PLAY_ENV_MAX; i++ )
    {
        static const char prefix[] = "TORIRSMAPED_PLAY_ENV_";
        if( strncmp(environ[i], prefix, sizeof(prefix) - 1) == 0 && strchr(environ[i], '=') )
        {
            snprintf(extra_store[extra_count], sizeof(extra_store[0]), "%s", environ[i] + sizeof(prefix) - 1);
            extra[extra_count] = extra_store[extra_count];
            extra_count++;
        }
    }
    extra[extra_count] = NULL;
    snprintf(js5_port, sizeof(js5_port), "%d", lane->js5_port);
    snprintf(log_path, sizeof(log_path), "%s/logs/play.log", live);
    {
        /* The last client's log is kept beside (a restart is often "why did
         * it die"). */
        char previous[TORIRSMAPED_LANE_PATH_MAX + 320];
        snprintf(previous, sizeof(previous), "%s/logs/play.prev.log", live);
        remove(previous); /* Win32's rename keeps an existing target */
        (void)rename(log_path, previous);
    }
    env = jobs_env_build(extra, drop);
    {
        char* argv[] = { (char*)lane->play_bin, (char*)"--manifest", manifest, (char*)"--js5", (char*)"--js5-host",
            (char*)"127.0.0.1", (char*)"--js5-port", js5_port, (char*)"--js5-fallback-port", (char*)"0",
            (char*)"--user", (char*)spec->user, (char*)"--pass", (char*)spec->pass, NULL };
        started = jobs_process_start_envp(argv, env, log_path, &play->process);
    }
    free(env);
    if( !started )
    {
        snprintf(why, why_size, "cannot start %s: %s", lane->play_bin, strerror(errno));
        return EDITOR_HOST_IO_ERROR;
    }
    *out_pid = play->process.pid;
    TORIRS_LOG("torirsmaped: play client pid %d: %s --manifest %s --js5-port %d --user %s (log %s)\n", play->process.pid,
        lane->play_bin, manifest, lane->js5_port, spec->user, log_path);
    return EDITOR_HOST_OK;
}

static int
play_native_alive(
    void* ctx,
    int* out_status)
{
    struct play_native* play = ctx;

    assert(play);
    assert(out_status);
    *out_status = 0;
    if( play->process.pid <= 0 )
        return 0;
    return !jobs_process_ended(&play->process, out_status);
}

static void
play_native_stop(void* ctx)
{
    struct play_native* play = ctx;

    assert(play);
    jobs_process_stop(&play->process, 3000);
}

static void
play_native_free(void* ctx)
{
    struct play_native* play = ctx;
    if( !play )
        return;
    play_native_stop(play);
    free(play);
}

#endif /* TORIRSMAPED_JOBS_NATIVE */

int
ToriRSMapEd_PlayLauncherNative(
    struct ToriRSMapEdPlayLauncher* out,
    const struct ToriRSMapEdLane* lane)
{
    assert(out);
    assert(lane);

    memset(out, 0, sizeof(*out));
#if TORIRSMAPED_JOBS_NATIVE
    if( !lane->play_bin[0] || !g_jobs_spawner )
        return 0;
    {
        struct play_native* play = calloc(1, sizeof(*play));
        assert(play);
        play->lane = *lane;
        out->ctx = play;
        out->ready = play_native_ready;
        out->start = play_native_start;
        out->alive = play_native_alive;
        out->stop = play_native_stop;
        out->free_ = play_native_free;
        return 1;
    }
#else
    return 0;
#endif
}

void
ToriRSMapEd_SetPlayLauncher(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdPlayLauncher* launcher)
{
    struct ToriRSMapEdJobs* jobs;

    assert(maped);
    assert(maped->jobs);
    jobs = maped->jobs;
    if( jobs->have_play )
    {
        jobs->play.stop(jobs->play.ctx);
        if( jobs->play.free_ )
            jobs->play.free_(jobs->play.ctx);
    }
    memset(&jobs->play, 0, sizeof(jobs->play));
    jobs->have_play = 0;
    jobs->play_pid = 0;
    jobs->play_wait_lane = 0;
    jobs->play_state = TORIRSMAPED_PLAY_NONE;
    jobs->play_x = jobs->play_z = jobs->play_level = -1;
    if( launcher )
    {
        assert(launcher->ready);
        assert(launcher->start);
        assert(launcher->alive);
        assert(launcher->stop);
        jobs->play = *launcher;
        jobs->have_play = 1;
    }
}

int
ToriRSMapEd_LiveLinkNative(
    struct ToriRSMapEdLiveLink* out,
    const struct ToriRSMapEdLane* lane)
{
    assert(out);

    memset(out, 0, sizeof(*out));
#if TORIRSMAPED_JOBS_NATIVE
    {
        struct live_native* native = calloc(1, sizeof(*native));
        assert(native);
        native->fd = -1;
        if( lane )
        {
            native->lane = *lane;
            native->have_lane = 1;
        }
        out->ctx = native;
        out->open = live_native_open;
        out->poll = live_native_poll;
        out->recv = live_native_recv;
        out->send = live_native_send;
        out->close = live_native_close;
        out->free_ = live_native_free;
        return 1;
    }
#else
    (void)lane;
    return 0;
#endif
}

void
ToriRSMapEd_LiveLinkNativePids(
    const struct ToriRSMapEdLiveLink* link,
    int out_pids[3])
{
    assert(link);
    assert(out_pids);
#if TORIRSMAPED_JOBS_NATIVE
    {
        const struct live_native* native = link->ctx;
        assert(link->open == live_native_open);
        for( int i = 0; i < 3; i++ )
            out_pids[i] = native->launched[i].pid;
    }
#else
    for( int i = 0; i < 3; i++ )
        out_pids[i] = 0;
#endif
}

/** A path override: set (even to "", which means "do not start it") when
 *  the variable exists. */
static void
live_env_path(
    const char* name,
    char* out,
    size_t out_size)
{
    const char* value = getenv(name);
    if( value )
        snprintf(out, out_size, "%s", value);
}

static void
live_env_port(
    const char* name,
    int* out)
{
    const char* value = getenv(name);
    if( value && value[0] )
    {
        int port = atoi(value);
        if( port > 0 && port < 65536 )
            *out = port;
        else
            TORIRS_LOG("torirsmaped: %s=%s is not a port; keeping %d\n", name, value, *out);
    }
}

/*
 * The lane's launch settings past ToriRSMapEd_LaneDefaults, from the
 * environment (every one optional):
 *
 *   TORIRSMAPED_LIVE_SERVER_BIN   the game server LIVE_ATTACH launch starts
 *   TORIRSMAPED_LIVE_JS5_BIN      js5_server ("" = not started)
 *   TORIRSMAPED_LIVE_IO_BIN       io_server
 *   TORIRSMAPED_LIVE_IO_MANIFEST  io_server's --manifest (none = not started)
 *   TORIRSMAPED_LIVE_IO_ROOT      io_server's --root
 *   TORIRSMAPED_LIVE_BASE_CACHE   the cache a first client build copies
 *   TORIRSMAPED_LIVE_GAME_PORT, TORIRSMAPED_LIVE_JS5_PORT, TORIRSMAPED_LIVE_IO_PORT
 *   TORIRSMAPED_LIVE_PLAY_BIN     the play client ("" = none)
 *   TORIRSMAPED_LIVE_PLAY_MANIFEST the client manifest the play manifest is written from
 *   TORIRSMAPED_LIVE_PLAY_FIXTURE the play account's first save ("" = a new character)
 *   TORIRSMAPED_LIVE_PLAY_USER, TORIRSMAPED_LIVE_PLAY_PASS   the play account
 */
static void
live_lane_from_env(struct ToriRSMapEdLane* lane)
{
    assert(lane);
    live_env_path("TORIRSMAPED_LIVE_SERVER_BIN", lane->server_bin, sizeof(lane->server_bin));
    live_env_path("TORIRSMAPED_LIVE_JS5_BIN", lane->js5_bin, sizeof(lane->js5_bin));
    live_env_path("TORIRSMAPED_LIVE_IO_BIN", lane->io_bin, sizeof(lane->io_bin));
    live_env_path("TORIRSMAPED_LIVE_IO_MANIFEST", lane->io_manifest, sizeof(lane->io_manifest));
    live_env_path("TORIRSMAPED_LIVE_IO_ROOT", lane->io_root, sizeof(lane->io_root));
    live_env_path("TORIRSMAPED_LIVE_BASE_CACHE", lane->base_cache, sizeof(lane->base_cache));
    live_env_port("TORIRSMAPED_LIVE_GAME_PORT", &lane->game_port);
    live_env_port("TORIRSMAPED_LIVE_JS5_PORT", &lane->js5_port);
    live_env_port("TORIRSMAPED_LIVE_IO_PORT", &lane->io_port);
    live_env_path("TORIRSMAPED_LIVE_PLAY_BIN", lane->play_bin, sizeof(lane->play_bin));
    live_env_path("TORIRSMAPED_LIVE_PLAY_MANIFEST", lane->play_manifest, sizeof(lane->play_manifest));
    live_env_path("TORIRSMAPED_LIVE_PLAY_FIXTURE", lane->play_fixture, sizeof(lane->play_fixture));
    live_env_path("TORIRSMAPED_LIVE_PLAY_USER", lane->play_user, sizeof(lane->play_user));
    live_env_path("TORIRSMAPED_LIVE_PLAY_PASS", lane->play_pass, sizeof(lane->play_pass));
}

/** 1 when `path` is absolute: "/x", and on Windows "C:/x", "C:\x" or "\x"
 *  too (a drive-letter path read as relative gets the working directory
 *  glued in front of it: "C:\repo/C:/repo/build/..."). */
static int
live_path_absolute(const char* path)
{
#ifdef _WIN32
    if( path[0] == '\\' || (((path[0] | 0x20) >= 'a' && (path[0] | 0x20) <= 'z') && path[1] == ':') )
        return 1;
#endif
    return path[0] == '/';
}

int
ToriRSMapEd_BindLiveFromEnv(
    struct ToriRSMapEd* maped,
    const char* repo_root,
    const char* content_dir)
{
    const char* live_dir = getenv("TORIRSMAPED_LIVE_DIR");
    char default_dir[1024];
    char repo_abs[TORIRSMAPED_LANE_PATH_MAX];
    char content_abs[TORIRSMAPED_LANE_PATH_MAX];
    char live_abs[TORIRSMAPED_LANE_PATH_MAX];
    struct ToriRSMapEdLane lane;
    struct ToriRSMapEdBuildRunner runner;
    struct ToriRSMapEdLiveLink link;

    assert(maped);
    if( !repo_root || !content_dir )
        return 0;
    /* Absolute, all three: the builds run `make -C <repo>/src`, so a path
     * relative to this process's directory names something else there. A
     * manifest's `repo_root=..` arrives as "manifests/.." and the lane's base
     * cache became src/manifests/../cache.osrs241 -- a client cache built
     * from nothing (master index 29 bytes), and a play client that logged in
     * and crashed on the missing interfaces table (2026-11-02). */
    build_resolve(repo_root, repo_abs, sizeof(repo_abs));
    build_resolve(content_dir, content_abs, sizeof(content_abs));
    repo_root = repo_abs;
    content_dir = content_abs;
    /* Unset: the lane lives under the repo's build tree, so RUN works from
     * the default profile with nothing configured (the owner hit RUN with no
     * lane bound and nothing happened, 2026-11-02). Set but empty: no lane,
     * the same rule as TORIRS_EDITOR_LAYOUT. */
    if( !live_dir )
    {
        snprintf(default_dir, sizeof(default_dir), "%s/build/live/default", repo_root);
        live_dir = default_dir;
    }
    if( !live_dir[0] )
        return 0;
    if( !live_path_absolute(live_dir) )
    {
        /* A relative live dir is the process's; a missing parent is made so
         * the resolve can anchor it. */
        build_resolve(live_dir, live_abs, sizeof(live_abs));
#if TORIRSMAPED_JOBS_NATIVE
        if( !live_path_absolute(live_abs) )
        {
            char cwd[TORIRSMAPED_LANE_PATH_MAX];
            if( getcwd(cwd, sizeof(cwd)) )
                snprintf(live_abs, sizeof(live_abs), "%.500s/%.500s", cwd, live_dir);
        }
#endif
        live_dir = live_abs;
    }
    ToriRSMapEd_LaneDefaults(&lane, repo_root, content_dir, live_dir);
    live_lane_from_env(&lane);
    if( !ToriRSMapEd_SetLane(maped, &lane) )
        return 0;
    if( !ToriRSMapEd_BuildRunnerNative(&runner, &lane) || !ToriRSMapEd_LiveLinkNative(&link, &lane) )
    {
        if( runner.free_ )
            runner.free_(runner.ctx);
        return 0;
    }
    ToriRSMapEd_SetBuildRunner(maped, &runner);
    ToriRSMapEd_SetLiveLink(maped, &link);
    {
        struct ToriRSMapEdPlayLauncher play;
        if( ToriRSMapEd_PlayLauncherNative(&play, &lane) )
            ToriRSMapEd_SetPlayLauncher(maped, &play);
    }
    TORIRS_LOG("torirsmaped: live lane %s (builds write there; RUN reloads the attached server)\n",
        live_dir);
    return 1;
}
