/*
 * The Live panel's model and the requests it sends (live_client.h).
 */

#include "live_client.h"

#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_build.h"
#include "torirsmaped/torirs_maped_live.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
Editor_LiveModelInit(struct Editor_LiveModel* model)
{
    assert(model);
    memset(model, 0, sizeof(*model));
    model->state = TORIRSMAPED_LIVE_DETACHED;
    model->run_step = TORIRSMAPED_RUN_STEP_IDLE;
    model->play_state = TORIRSMAPED_PLAY_NONE;
    model->play_x = model->play_z = model->play_level = -1;
}

int
Editor_LiveIsFact(uint32_t type)
{
    return type >= TORIRSMAPED_FACT_LIVE_RANGE_FIRST && type <= TORIRSMAPED_FACT_LIVE_RANGE_LAST;
}

static void
live_log(
    struct Editor_LiveModel* model,
    const char* format,
    ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

static void
live_log(
    struct Editor_LiveModel* model,
    const char* format,
    ...)
{
    va_list args;
    char text[1024];
    const char* at = text;
    int first = 1;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    /* A long line (a RUN stop naming a lock holder, a compiler error with
     * its path) continues on the next rows rather than losing its end. */
    do
    {
        size_t const room = EDITOR_LIVE_LOG_LINE_MAX - 1 - (first ? 0 : 4);
        size_t const length = strlen(at);
        size_t const take = length < room ? length : room;
        snprintf(model->log[model->log_head], EDITOR_LIVE_LOG_LINE_MAX, "%s%.*s", first ? "" : "    ", (int)take,
            at);
        at += take;
        first = 0;
        model->log_head = (model->log_head + 1) % EDITOR_LIVE_LOG_LINES;
        if( model->log_count < EDITOR_LIVE_LOG_LINES )
            model->log_count++;
    } while( *at );
    model->log_serial++;
}

const char*
Editor_LiveLogLine(
    const struct Editor_LiveModel* model,
    int index)
{
    assert(model);
    if( index < 0 || index >= model->log_count )
        return NULL;
    return model->log[(model->log_head - 1 - index + 2 * EDITOR_LIVE_LOG_LINES) % EDITOR_LIVE_LOG_LINES];
}

int
Editor_LiveVarValue(
    const struct Editor_LiveModel* model,
    uint32_t varp,
    int32_t* out_value)
{
    assert(model);
    assert(out_value);
    for( int i = 0; i < model->var_count; i++ )
    {
        if( model->vars[i].varp == varp )
        {
            *out_value = model->vars[i].value;
            return 1;
        }
    }
    return 0;
}

/** A string field into a fixed buffer, cut to fit (a status's text is for
 *  showing; a long one is not malformed). */
static void
get_text(
    struct ToriRSMapEdReader* reader,
    char* out,
    size_t out_size)
{
    uint32_t length;
    const uint8_t* bytes = ToriRSMapEd_GetBytes(reader, &length);
    size_t n;

    out[0] = '\0';
    if( !bytes )
        return;
    n = length < out_size - 1 ? length : out_size - 1;
    memcpy(out, bytes, n);
    out[n] = '\0';
    for( size_t i = 0; i < n; i++ )
    {
        if( out[i] == '\0' || out[i] == '\n' || out[i] == '\r' )
            out[i] = ' ';
    }
}

static int
apply_status(
    struct Editor_LiveModel* model,
    struct ToriRSMapEdReader* reader)
{
    struct Editor_LiveModel next = *model;
    uint32_t stale;

    next.state = ToriRSMapEd_GetU32(reader);
    next.launched = (int)ToriRSMapEd_GetU32(reader);
    get_text(reader, next.address, sizeof(next.address));
    get_text(reader, next.server_name, sizeof(next.server_name));
    next.players = ToriRSMapEd_GetU32(reader);
    next.tick = ToriRSMapEd_GetU32(reader);
    get_text(reader, next.script_stamp, sizeof(next.script_stamp));
    get_text(reader, next.servpack_stamp, sizeof(next.servpack_stamp));
    stale = ToriRSMapEd_GetU32(reader);
    next.stale_count = 0;
    for( uint32_t i = 0; i < stale && !reader->bad; i++ )
    {
        char row[192];
        get_text(reader, row, sizeof(row));
        if( next.stale_count < EDITOR_LIVE_STALE_MAX )
            snprintf(next.stale[next.stale_count++], sizeof(next.stale[0]), "%s", row);
    }
    get_text(reader, next.message, sizeof(next.message));
    next.run_step = ToriRSMapEd_GetU32(reader);
    next.pending_squares = ToriRSMapEd_GetU32(reader);
    next.pending_spawns = ToriRSMapEd_GetU32(reader);
    next.pending_archives = ToriRSMapEd_GetU32(reader);
    if( !ToriRSMapEd_ReaderDone(reader) )
        return 0;
    if( strcmp(next.message, model->message) != 0 && next.message[0] )
        live_log(&next, "live: %s", next.message);
    next.status_serial++;
    *model = next;
    return 1;
}

int
Editor_LiveModelApply(
    struct Editor_LiveModel* model,
    uint32_t type,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;

    assert(model);
    assert(body || length == 0);

    if( !Editor_LiveIsFact(type) )
        return 0;
    ToriRSMapEd_ReaderInit(&reader, body, length);

    if( type == TORIRSMAPED_FACT_BUILD_LINE )
    {
        uint32_t what = ToriRSMapEd_GetU32(&reader);
        char line[EDITOR_LIVE_LOG_LINE_MAX];
        get_text(&reader, line, sizeof(line));
        if( !ToriRSMapEd_ReaderDone(&reader) )
            goto malformed;
        model->building = 1;
        model->build_what = what;
        live_log(model, "%s", line);
        return 1;
    }
    if( type == TORIRSMAPED_FACT_BUILD_DONE )
    {
        uint32_t what = ToriRSMapEd_GetU32(&reader);
        uint32_t status = ToriRSMapEd_GetU32(&reader);
        uint32_t units = ToriRSMapEd_GetU32(&reader);
        uint32_t archives;
        for( uint32_t i = 0; i < units && !reader.bad; i++ )
        {
            uint32_t unit_length;
            (void)ToriRSMapEd_GetBytes(&reader, &unit_length);
        }
        archives = ToriRSMapEd_GetU32(&reader);
        if( reader.bad || archives > (reader.length - reader.at) / 8 )
            goto malformed;
        reader.at += 8 * archives;
        if( !ToriRSMapEd_ReaderDone(&reader) )
            goto malformed;
        model->building = 0;
        model->last_build_what = what;
        model->last_build_status = status;
        model->last_build_units = units;
        model->last_build_archives = archives;
        model->build_serial++;
        live_log(model, "build %s: %s (%u unit(s), %u archive(s))", ToriRSMapEd_BuildWhatName(what),
            status == 0 ? "ok" : "FAILED", units, archives);
        return 1;
    }
    if( type == TORIRSMAPED_FACT_RUN_DONE )
    {
        uint32_t status = ToriRSMapEd_GetU32(&reader);
        uint32_t step = ToriRSMapEd_GetU32(&reader);
        char message[1024];
        uint32_t counts[4];
        uint32_t restart;
        char rows[EDITOR_LIVE_RESTART_MAX][192];
        int row_count = 0;
        get_text(&reader, message, sizeof(message));
        for( int i = 0; i < 4; i++ )
            counts[i] = ToriRSMapEd_GetU32(&reader);
        restart = ToriRSMapEd_GetU32(&reader);
        for( uint32_t i = 0; i < restart && !reader.bad; i++ )
        {
            char row[192];
            get_text(&reader, row, sizeof(row));
            if( row_count < EDITOR_LIVE_RESTART_MAX )
                snprintf(rows[row_count++], sizeof(rows[0]), "%s", row);
        }
        if( !ToriRSMapEd_ReaderDone(&reader) )
            goto malformed;
        model->run_status = status;
        model->run_last_step = step;
        snprintf(model->run_message, sizeof(model->run_message), "%s", message);
        memcpy(model->run_counts, counts, sizeof(counts));
        model->restart_count = row_count;
        for( int i = 0; i < row_count; i++ )
            snprintf(model->restart[i], sizeof(model->restart[0]), "%s", rows[i]);
        model->run_serial++;
        if( status == 0 )
            live_log(model, "RUN done: %s", message);
        else
            live_log(model, "RUN stopped at %s: %s", ToriRSMapEd_RunStepName(step), message);
        for( int i = 0; i < row_count; i++ )
            live_log(model, "  restart needed: %s", rows[i]);
        return 1;
    }
    if( type == TORIRSMAPED_FACT_LIVE_STATUS )
    {
        if( !apply_status(model, &reader) )
            goto malformed;
        return 1;
    }
    if( type == TORIRSMAPED_FACT_RUN_PROGRESS )
    {
        struct Editor_LiveModel next = *model;
        next.progress_step = ToriRSMapEd_GetU32(&reader);
        next.progress_index = ToriRSMapEd_GetU32(&reader);
        next.progress_count = ToriRSMapEd_GetU32(&reader);
        next.progress_percent = ToriRSMapEd_GetU32(&reader);
        next.progress_step_percent = ToriRSMapEd_GetU32(&reader);
        next.progress_elapsed_ms = ToriRSMapEd_GetU32(&reader);
        next.progress_remaining_ms = ToriRSMapEd_GetU32(&reader);
        get_text(&reader, next.progress_detail, sizeof(next.progress_detail));
        if( !ToriRSMapEd_ReaderDone(&reader) )
            goto malformed;
        if( next.progress_percent > 100 )
            next.progress_percent = 100;
        /* A RUN's steps are its status's run_step too: a progress row that
         * arrives first keeps the run line honest. */
        if( next.progress_step != TORIRSMAPED_RUN_STEP_IDLE )
            next.run_step = next.progress_step;
        next.progress_serial++;
        *model = next;
        return 1;
    }
    if( type == TORIRSMAPED_FACT_LIVE_LINE )
    {
        char line[EDITOR_LIVE_LOG_LINE_MAX];
        get_text(&reader, line, sizeof(line));
        if( !ToriRSMapEd_ReaderDone(&reader) )
            goto malformed;
        live_log(model, "> %s", line);
        return 1;
    }
    if( type == TORIRSMAPED_FACT_LIVE_PLAY )
    {
        struct Editor_LiveModel next = *model;
        uint32_t rows;
        next.play_state = ToriRSMapEd_GetU32(&reader);
        next.play_browser = (int)ToriRSMapEd_GetU32(&reader);
        get_text(&reader, next.play_account, sizeof(next.play_account));
        next.play_pid = (int32_t)ToriRSMapEd_GetU32(&reader);
        next.play_exit = (int32_t)ToriRSMapEd_GetU32(&reader);
        get_text(&reader, next.play_message, sizeof(next.play_message));
        next.play_x = (int32_t)ToriRSMapEd_GetU32(&reader);
        next.play_z = (int32_t)ToriRSMapEd_GetU32(&reader);
        next.play_level = (int32_t)ToriRSMapEd_GetU32(&reader);
        next.play_starts = ToriRSMapEd_GetU32(&reader);
        get_text(&reader, next.play_web_url, sizeof(next.play_web_url));
        next.players_known = (int)ToriRSMapEd_GetU32(&reader);
        next.logins = ToriRSMapEd_GetU32(&reader);
        next.players = ToriRSMapEd_GetU32(&reader);
        rows = ToriRSMapEd_GetU32(&reader);
        next.player_row_count = 0;
        for( uint32_t i = 0; i < rows && !reader.bad; i++ )
        {
            struct Editor_LivePlayer row;
            get_text(&reader, row.name, sizeof(row.name));
            row.x = (int32_t)ToriRSMapEd_GetU32(&reader);
            row.z = (int32_t)ToriRSMapEd_GetU32(&reader);
            row.level = (int32_t)ToriRSMapEd_GetU32(&reader);
            if( next.player_row_count < EDITOR_LIVE_PLAYERS_MAX )
                next.player_rows[next.player_row_count++] = row;
        }
        if( !ToriRSMapEd_ReaderDone(&reader) )
            goto malformed;
        if( next.play_message[0]
            && (strcmp(next.play_message, model->play_message) != 0 || next.play_state != model->play_state) )
            live_log(&next, "play: %s", next.play_message);
        next.play_serial++;
        *model = next;
        return 1;
    }
    if( type == TORIRSMAPED_FACT_LIVE_VARS )
    {
        char player[64];
        uint32_t n;
        get_text(&reader, player, sizeof(player));
        n = ToriRSMapEd_GetU32(&reader);
        if( reader.bad || n > (reader.length - reader.at) / 8 )
            goto malformed;
        if( strcmp(player, model->vars_player) != 0 )
        {
            /* Another player's vars: start over. */
            snprintf(model->vars_player, sizeof(model->vars_player), "%s", player);
            model->var_count = 0;
        }
        for( uint32_t i = 0; i < n; i++ )
        {
            uint32_t varp = ToriRSMapEd_GetU32(&reader);
            int32_t value = (int32_t)ToriRSMapEd_GetU32(&reader);
            int slot = -1;
            for( int v = 0; v < model->var_count; v++ )
            {
                if( model->vars[v].varp == varp )
                    slot = v;
            }
            if( slot < 0 && model->var_count < EDITOR_LIVE_VARS_MAX )
                slot = model->var_count++;
            if( slot >= 0 )
            {
                model->vars[slot].varp = varp;
                model->vars[slot].value = value;
            }
        }
        if( !ToriRSMapEd_ReaderDone(&reader) )
            goto malformed;
        model->vars_serial++;
        return 1;
    }
    /* A live-range fact this build does not know: nothing to fold. */
    return 1;

malformed:
    live_log(model, "live: dropped a malformed fact %u", type);
    return -1;
}

/* ---- requests ---------------------------------------------------------------- */

uint32_t
Editor_LiveEncodeBuild(
    struct ToriRSMapEdBuf* out,
    uint32_t what)
{
    assert(out);
    ToriRSMapEd_PutU32(out, what);
    return TORIRSMAPED_REQ_BUILD;
}

uint32_t
Editor_LiveEncodeBuildQuest(
    struct ToriRSMapEdBuf* out,
    uint32_t what,
    const char* quest)
{
    assert(out);
    assert(quest);
    assert(ToriRSMapEd_BuildWhatTakesQuest(what));
    assert(ToriRSMapEd_BuildQuestValid(quest));
    ToriRSMapEd_PutU32(out, what);
    ToriRSMapEd_PutString(out, quest);
    return TORIRSMAPED_REQ_BUILD;
}

uint32_t
Editor_LiveEncodeRun(struct ToriRSMapEdBuf* out)
{
    assert(out);
    (void)out; /* RUN has no payload */
    return TORIRSMAPED_REQ_RUN;
}

uint32_t
Editor_LiveEncodeAttach(
    struct ToriRSMapEdBuf* out,
    int launch,
    const char* address,
    const char* ticket)
{
    assert(out);
    assert(address);
    assert(ticket);
    ToriRSMapEd_PutU32(out, launch ? 1u : 0u);
    ToriRSMapEd_PutString(out, launch ? "" : address);
    ToriRSMapEd_PutString(out, ticket);
    return TORIRSMAPED_REQ_LIVE_ATTACH;
}

uint32_t
Editor_LiveEncodeDetach(struct ToriRSMapEdBuf* out)
{
    assert(out);
    (void)out; /* LIVE_DETACH has no payload */
    return TORIRSMAPED_REQ_LIVE_DETACH;
}

uint32_t
Editor_LiveEncodeCheat(
    struct ToriRSMapEdBuf* out,
    const char* player,
    const char* line)
{
    assert(out);
    assert(player);
    assert(line);
    while( *line == ' ' )
        line++;
    if( line[0] == ':' && line[1] == ':' )
        line += 2;
    ToriRSMapEd_PutString(out, player);
    ToriRSMapEd_PutString(out, line);
    return TORIRSMAPED_REQ_LIVE_CHEAT;
}

uint32_t
Editor_LiveEncodePlay(
    struct ToriRSMapEdBuf* out,
    uint32_t action)
{
    assert(out);
    assert(action <= TORIRSMAPED_PLAY_BROWSER);
    ToriRSMapEd_PutU32(out, action);
    return TORIRSMAPED_REQ_LIVE_PLAY;
}

void
Editor_LiveProgressLine(
    const struct Editor_LiveModel* model,
    char* out,
    size_t out_size)
{
    char left[48];

    assert(model);
    assert(out);
    assert(out_size > 0);
    if( model->run_step == TORIRSMAPED_RUN_STEP_IDLE || model->progress_serial == 0 )
    {
        out[0] = '\0';
        return;
    }
    if( model->progress_remaining_ms == 0xFFFFFFFFu )
        snprintf(left, sizeof(left), "first RUN here, no estimate");
    else
        snprintf(left, sizeof(left), "~%u s left", (model->progress_remaining_ms + 999) / 1000);
    snprintf(out, out_size, "%s %u/%u: %s (%u s, %s)", ToriRSMapEd_RunStepName(model->progress_step),
        model->progress_index, model->progress_count, model->progress_detail[0] ? model->progress_detail : "...",
        model->progress_elapsed_ms / 1000, left);
}

void
Editor_LivePlayLine(
    const struct Editor_LiveModel* model,
    char* out,
    size_t out_size)
{
    assert(model);
    assert(out);
    if( model->play_state == TORIRSMAPED_PLAY_ONLINE && model->play_x >= 0 )
        snprintf(out, out_size, "play: logged in as %s at m%d_%d (%d,%d)%s", model->play_account,
            model->play_x >> 6, model->play_z >> 6, model->play_x, model->play_z,
            model->play_browser ? " in a browser tab" : "");
    else if( model->play_state == TORIRSMAPED_PLAY_STARTING )
        snprintf(out, out_size, "play: starting %s%s%s", model->play_account,
            model->play_message[0] ? " -- " : "", model->play_message);
    else if( model->play_state == TORIRSMAPED_PLAY_EXITED )
        snprintf(out, out_size, "play: exited -- %s", model->play_message);
    else if( model->play_state == TORIRSMAPED_PLAY_LOGGED_OUT )
        snprintf(out, out_size, "play: %s logged out (the client still runs)", model->play_account);
    else if( model->play_browser )
        snprintf(out, out_size, "play: in a browser tab%s", model->play_web_url[0] ? "" : " (no web page)");
    else
        snprintf(out, out_size, "play: not started%s%s", model->play_message[0] ? " -- " : "",
            model->play_message);
}

uint32_t
Editor_LiveEncodeSubscribe(
    struct ToriRSMapEdBuf* out,
    const char* player,
    const uint32_t* varps,
    int n)
{
    assert(out);
    assert(player);
    assert(varps || n == 0);
    assert(n >= 0);
    ToriRSMapEd_PutString(out, player);
    ToriRSMapEd_PutU32(out, (uint32_t)n);
    for( int i = 0; i < n; i++ )
        ToriRSMapEd_PutU32(out, varps[i]);
    return TORIRSMAPED_REQ_LIVE_SUBSCRIBE;
}

int
Editor_LiveParseVarps(
    const char* text,
    uint32_t* out,
    int max)
{
    int count = 0;
    const char* p = text;

    assert(text);
    assert(out);
    while( *p )
    {
        char* end;
        unsigned long value;
        while( *p == ' ' || *p == ',' )
            p++;
        if( !*p )
            break;
        if( *p < '0' || *p > '9' )
            return -1;
        value = strtoul(p, &end, 10);
        if( end == p || value > 0xffffu )
            return -1;
        if( count < max )
            out[count++] = (uint32_t)value;
        p = end;
        if( *p && *p != ' ' && *p != ',' )
            return -1;
    }
    return count;
}

void
Editor_LiveTeleCoord(
    int abs_x,
    int abs_z,
    int level,
    char* out,
    size_t out_size)
{
    assert(out);
    snprintf(out, out_size, "%d_%d_%d_%d_%d", level, abs_x >> 6, abs_z >> 6, abs_x & 63, abs_z & 63);
}

/* ---- the transport -------------------------------------------------------------- */

int
Editor_LiveSend(
    const struct Editor_LiveTransport* transport,
    uint32_t type,
    struct ToriRSMapEdBuf* payload)
{
    int sent;

    assert(transport);
    assert(transport->send);
    assert(payload);
    sent = transport->send(transport->user, type, ToriRSMapEd_BufPeek(payload),
        (uint32_t)ToriRSMapEd_BufAvailable(payload));
    ToriRSMapEd_BufFree(payload);
    memset(payload, 0, sizeof(*payload));
    return sent;
}

int
Editor_LiveDrain(
    const struct Editor_LiveTransport* transport,
    struct Editor_LiveModel* model)
{
    uint32_t type;
    uint8_t* body;
    uint32_t length;
    int applied = 0;

    assert(transport);
    assert(model);
    if( !transport->take )
        return 0;
    while( transport->take(transport->user, &type, &body, &length) )
    {
        if( Editor_LiveModelApply(model, type, body, length) > 0 )
            applied++;
        if( transport->release )
            transport->release(transport->user, body);
    }
    return applied;
}
