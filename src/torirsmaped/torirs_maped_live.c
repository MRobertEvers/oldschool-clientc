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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if TORIRSMAPED_JOBS_NATIVE
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
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
    ToriRSMapEd_BufFree(&jobs->ctl_in);
    memset(&jobs->ctl_in, 0, sizeof(jobs->ctl_in));
    snprintf(jobs->message, sizeof(jobs->message), "%s", message ? message : "");
    if( jobs->reload_waiting )
        jobs_run_finish(maped, EDITOR_HOST_IO_ERROR, TORIRSMAPED_RUN_STEP_RELOAD,
            "the live link dropped during the reload", NULL, NULL, 0);
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

/* ---- RUN's reload ----------------------------------------------------------- */

static void
jobs_run_reload(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    struct ToriRSMapEdBuf payload;
    char manifest[TORIRSMAPED_LANE_PATH_MAX + 32];

    if( !jobs->have_link || jobs->live_state != TORIRSMAPED_LIVE_ATTACHED )
    {
        jobs_run_finish(maped, EDITOR_HOST_UNSUPPORTED, TORIRSMAPED_RUN_STEP_RELOAD,
            jobs->have_link ? "no live server is attached (LIVE_ATTACH first); the packs are built"
                            : "this server has no live link; the packs are built",
            NULL, NULL, 0);
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
            jobs_run_finish(maped, EDITOR_HOST_OK, TORIRSMAPED_RUN_STEP_DONE, message, counts,
                &restart, restart_count);
            live_send_status_request(maped);
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

static void
live_service(struct ToriRSMapEd* maped)
{
    struct ToriRSMapEdJobs* jobs = maped->jobs;
    uint8_t chunk[16384];
    char why[256];
    int peer_gone = 0;

    if( !jobs->have_link || jobs->live_state == TORIRSMAPED_LIVE_DETACHED
        || jobs->live_state == TORIRSMAPED_LIVE_FAILED )
        return;

    if( jobs->live_state == TORIRSMAPED_LIVE_CONNECTING && !jobs->hello_sent )
    {
        int up = jobs->link.poll(jobs->link.ctx, why, sizeof(why));
        if( up < 0 )
        {
            jobs_live_drop(maped, TORIRSMAPED_LIVE_FAILED, why);
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
        if( now - jobs->last_status >= LIVE_STATUS_PERIOD_S )
            live_send_status_request(maped);
        if( jobs->reload_waiting && now > jobs->reload_deadline )
            jobs_run_finish(maped, EDITOR_HOST_IO_ERROR, TORIRSMAPED_RUN_STEP_RELOAD,
                "the game server did not answer the reload", NULL, NULL, 0);
    }
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
        if( jobs->live_state == TORIRSMAPED_LIVE_CONNECTING || jobs->live_state == TORIRSMAPED_LIVE_ATTACHED )
            jobs_live_drop(maped, TORIRSMAPED_LIVE_DETACHED, "re-attaching");
        memset(&target, 0, sizeof(target));
        target.launch = (int)mode;
        if( mode == 1 )
        {
            if( !jobs->have_lane )
            {
                live_unavailable(maped, "launch needs a lane (none is bound)");
                return;
            }
            snprintf(target.host, sizeof(target.host), "127.0.0.1");
            target.port = jobs->lane.game_port;
        }
        else
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
        if( jobs->have_link && jobs->live_state != TORIRSMAPED_LIVE_DETACHED )
            jobs_live_drop(maped, TORIRSMAPED_LIVE_DETACHED, "detached");
        else
            snprintf(jobs->message, sizeof(jobs->message), "detached");
        jobs->live_state = TORIRSMAPED_LIVE_DETACHED;
        jobs_live_broadcast_status(maped);
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
}

int
ToriRSMapEd_Busy(const struct ToriRSMapEd* maped)
{
    const struct ToriRSMapEdJobs* jobs;

    assert(maped);
    assert(maped->jobs);
    jobs = maped->jobs;
    return jobs->building || jobs->run_step != TORIRSMAPED_RUN_STEP_IDLE
           || jobs->live_state == TORIRSMAPED_LIVE_CONNECTING;
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
    int pids[3];
    struct ToriRSMapEdBuf out;
};

static int64_t
live_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void
live_native_disconnect(struct live_native* native)
{
    if( native->fd >= 0 )
        close(native->fd);
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

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port, sizeof(port), "%d", native->target.port);
    if( getaddrinfo(native->target.host, port, &hints, &found) != 0 || !found )
        return 0;
    fd = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if( fd < 0 )
    {
        freeaddrinfo(found);
        return 0;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#if defined(SO_NOSIGPIPE)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    rc = connect(fd, found->ai_addr, found->ai_addrlen);
    freeaddrinfo(found);
    if( rc != 0 && errno != EINPROGRESS )
    {
        close(fd);
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
    {
        if( native->pids[i] > 0 )
            jobs_stop_process(native->pids[i], 2000);
        native->pids[i] = 0;
    }
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
        char env[4][TORIRSMAPED_LANE_PATH_MAX + 32];
        const char* extra_env[5];
        char cache[TORIRSMAPED_LANE_PATH_MAX + 16];
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
        for( int i = 0; i < 4; i++ )
            extra_env[i] = env[i];
        extra_env[4] = NULL;
        {
            char* argv[] = { (char*)lane->server_bin, port[0], (char*)"--rev", (char*)lane->rev, NULL };
            native->pids[0] = jobs_spawn(argv, extra_env, NULL);
            if( native->pids[0] < 0 )
            {
                native->pids[0] = 0;
                snprintf(why, why_size, "cannot start %s: %s", lane->server_bin, strerror(errno));
                return EDITOR_HOST_IO_ERROR;
            }
        }
        if( lane->js5_bin[0] )
        {
            char* argv[] = { (char*)lane->js5_bin, (char*)"--cache", cache, (char*)"--revision",
                rev_number, (char*)"--port", port[1], (char*)"--max-pending", (char*)"65535", NULL };
            native->pids[1] = jobs_spawn(argv, NULL, NULL);
            if( native->pids[1] < 0 )
            {
                native->pids[1] = 0;
                snprintf(why, why_size, "cannot start %s: %s", lane->js5_bin, strerror(errno));
                live_native_stop_launched(native);
                return EDITOR_HOST_IO_ERROR;
            }
        }
        if( lane->io_bin[0] && lane->io_manifest[0] )
        {
            char* argv[] = { (char*)lane->io_bin, (char*)"--manifest", (char*)lane->io_manifest,
                (char*)"--root", (char*)lane->io_root, (char*)"--port", port[2], NULL };
            native->pids[2] = jobs_spawn(argv, NULL, NULL);
            if( native->pids[2] < 0 )
            {
                native->pids[2] = 0;
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
    if( native->target.launch && native->pids[0] > 0 )
    {
        int status = 0;
        if( waitpid((pid_t)native->pids[0], &status, WNOHANG) == (pid_t)native->pids[0] )
        {
            native->pids[0] = 0;
            live_native_disconnect(native);
            live_native_stop_launched(native);
            snprintf(why, why_size, "the launched game server exited (status %d) before it answered",
                WIFEXITED(status) ? WEXITSTATUS(status) : -1);
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
        getsockopt(native->fd, SOL_SOCKET, SO_ERROR, &error, &length);
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
        ssize_t sent = send(native->fd, ToriRSMapEd_BufPeek(&native->out),
            (size_t)ToriRSMapEd_BufAvailable(&native->out), LIVE_SEND_FLAGS);
        if( sent > 0 )
        {
            ToriRSMapEd_BufConsume(&native->out, (int)sent);
            continue;
        }
        if( sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) )
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
    ssize_t got;

    assert(native);
    if( !native->connected )
        return native->connecting ? 0 : -1;
    if( !live_native_flush(native) )
        return -1;
    got = recv(native->fd, dst, (size_t)max, 0);
    if( got > 0 )
        return (int)got;
    if( got == 0 )
        return -1;
    if( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR )
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

#endif /* TORIRSMAPED_JOBS_NATIVE */

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
            out_pids[i] = native->pids[i];
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
}

int
ToriRSMapEd_BindLiveFromEnv(
    struct ToriRSMapEd* maped,
    const char* repo_root,
    const char* content_dir)
{
    const char* live_dir = getenv("TORIRSMAPED_LIVE_DIR");
    struct ToriRSMapEdLane lane;
    struct ToriRSMapEdBuildRunner runner;
    struct ToriRSMapEdLiveLink link;

    assert(maped);
    if( !live_dir || !live_dir[0] || !repo_root || !content_dir )
        return 0;
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
    TORIRS_LOG("torirsmaped: live lane %s (builds write there; RUN reloads the attached server)\n",
        live_dir);
    return 1;
}
