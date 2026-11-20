/*
 * The hiscores server's routes. See hiscores_server.h.
 */
#include "hiscores_server.h"

#include "hiscores_db.h"
#include "hiscores_proto.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The answer                                                          */
/* ------------------------------------------------------------------ */

struct Answer
{
    char* data;
    size_t len;
    size_t cap;
};

static void
answer_init(struct Answer* a)
{
    a->cap = 4096;
    a->len = 0;
    a->data = ToriMem_Alloc(ToriMem_Net(), a->cap);
    assert(a->data);
    a->data[0] = '\0';
}

static void
answer_printf(
    struct Answer* a,
    char const* fmt,
    ...)
{
    for( ;; )
    {
        va_list args;
        int n;

        va_start(args, fmt);
        n = vsnprintf(a->data + a->len, a->cap - a->len, fmt, args);
        va_end(args);
        assert(n >= 0);
        if( (size_t)n < a->cap - a->len )
        {
            a->len += (size_t)n;
            return;
        }
        a->cap = a->cap * 2 + (size_t)n;
        a->data = ToriMem_Realloc(ToriMem_Net(), a->data, a->cap);
        assert(a->data);
    }
}

/* A text field, last on its line: tabs and newlines made spaces. */
static void
answer_text(
    struct Answer* a,
    char const* text)
{
    char clean[256];

    HiscoresProto_Clean(text, clean, sizeof(clean));
    answer_printf(a, "%s", clean);
}

static void
answer_send(
    struct Answer* a,
    struct HttpResponse* res,
    int status)
{
    res->status = status;
    snprintf(res->content_type, sizeof(res->content_type), "text/plain; charset=utf-8");
    res->body = a->data;
    res->body_len = (int)a->len;
    res->owns_body = 1;
}

static void
refuse(
    struct HttpResponse* res,
    int status,
    char const* why)
{
    struct Answer a;

    answer_init(&a);
    answer_printf(&a, "error\t%s\n", why);
    answer_send(&a, res, status);
}

/* ------------------------------------------------------------------ */
/* Fields                                                              */
/* ------------------------------------------------------------------ */

static int
hex_value(char c)
{
    if( c >= '0' && c <= '9' )
        return c - '0';
    c = (char)tolower((unsigned char)c);
    if( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    return -1;
}

/* `name`'s value in `a=1&b=2` form encoding, decoded into `out`. 1 when
 * present and it fits. */
static int
form_field(
    char const* data,
    size_t len,
    char const* name,
    char* out,
    size_t cap)
{
    size_t name_len = strlen(name);
    size_t at = 0;

    while( at < len )
    {
        size_t end = at;
        size_t o = 0;

        while( end < len && data[end] != '&' )
            end++;
        if( end - at > name_len && strncmp(data + at, name, name_len) == 0 &&
            data[at + name_len] == '=' )
        {
            for( size_t i = at + name_len + 1; i < end; i++ )
            {
                char c = data[i];

                if( c == '+' )
                    c = ' ';
                else if( c == '%' && i + 2 < end && hex_value(data[i + 1]) >= 0 &&
                         hex_value(data[i + 2]) >= 0 )
                {
                    c = (char)(hex_value(data[i + 1]) * 16 + hex_value(data[i + 2]));
                    i += 2;
                }
                if( o + 1 >= cap )
                    return 0;
                out[o++] = c;
            }
            out[o] = '\0';
            return 1;
        }
        at = end + 1;
    }
    return 0;
}

static int
body_field(
    struct HttpRequest const* req,
    char const* name,
    char* out,
    size_t cap)
{
    return req->body && form_field((char const*)req->body, (size_t)req->body_len, name, out, cap);
}

static int
query_field(
    struct HttpRequest const* req,
    char const* name,
    char* out,
    size_t cap)
{
    char const* q = strchr(req->path, '?');

    return q && form_field(q + 1, strlen(q + 1), name, out, cap);
}

/* Whether the path, query aside, is `route`. */
static int
path_is(
    struct HttpRequest const* req,
    char const* route)
{
    size_t len = strcspn(req->path, "?");

    return strlen(route) == len && strncmp(req->path, route, len) == 0;
}

/* ------------------------------------------------------------------ */
/* Routes                                                              */
/* ------------------------------------------------------------------ */

static void
route_events(
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    static struct ToriRSServerHiscoresRecord records[HISCORES_SERVER_BATCH_MAX];
    static long long seqs[HISCORES_SERVER_BATCH_MAX];
    char source[64];
    char* body;
    char* line;
    int count = 0;
    struct Answer a;

    if( !query_field(req, "source", source, sizeof(source)) || !source[0] )
    {
        refuse(res, 400, "events need ?source=<world>");
        return;
    }
    body = ToriMem_Alloc(ToriMem_Net(), (size_t)req->body_len + 1);
    assert(body);
    if( req->body_len > 0 )
        memcpy(body, req->body, (size_t)req->body_len);
    body[req->body_len] = '\0';
    for( line = body; line && *line; )
    {
        char* next = strchr(line, '\n');

        if( next )
            *next++ = '\0';
        if( count == HISCORES_SERVER_BATCH_MAX ||
            !HiscoresProto_RecordParse(line, &records[count], &seqs[count]) ||
            (count > 0 && seqs[count] <= seqs[count - 1]) )
        {
            /* Nothing of a bad batch is applied: the sender keeps it and
             * says so, rather than a world's history going quietly missing
             * from the middle. */
            ToriMem_Free(ToriMem_Net(), body);
            fprintf(stderr, "hiscores_server: refused a batch from %s at its record %d\n", source,
                    count + 1);
            refuse(res, 400, count == HISCORES_SERVER_BATCH_MAX ? "too many records"
                                                               : "not a record, or out of order");
            return;
        }
        count++;
        line = next;
    }
    ToriMem_Free(ToriMem_Net(), body);
    answer_init(&a);
    answer_printf(&a, "acked\t%lld\n", HiscoresDb_ApplyBatch(source, records, seqs, count));
    answer_send(&a, res, 200);
}

static void
route_result(
    struct HttpResponse* res,
    int result,
    char const* problem)
{
    struct Answer a;

    answer_init(&a);
    answer_printf(&a, "result\t%d\t", result);
    answer_text(&a, problem ? problem : "");
    answer_printf(&a, "\n");
    answer_send(&a, res, 200);
}

static void
route_view_player(
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    static struct HiscoresPlayerView view;
    char key[HISCORES_NAME_MAX];
    char history[4] = "0";
    struct Answer a;

    if( !query_field(req, "key", key, sizeof(key)) )
    {
        refuse(res, 400, "a player view needs ?key=");
        return;
    }
    query_field(req, "history", history, sizeof(history));
    if( !HiscoresDb_PlayerView(key, history[0] == '1', &view) )
    {
        refuse(res, 404, "no such player");
        return;
    }
    answer_init(&a);
    answer_printf(&a, "player\t%lld\t%d\t%lld\t%d\t", view.player.id, view.player.ranked,
                  view.player.last_seen, view.player.quest_points);
    answer_text(&a, view.player.key);
    answer_printf(&a, "\t");
    answer_text(&a, view.player.name);
    answer_printf(&a, "\nxp");
    for( int i = 0; i < TORIRSSERVER_HISCORESLOG_STATS; i++ )
        answer_printf(&a, "\t%lld", view.player.xp[i]);
    answer_printf(&a, "\nrank\t%d", view.rank_overall);
    for( int i = 0; i < HISCORES_SKILLS; i++ )
        answer_printf(&a, "\t%d", view.rank[i]);
    answer_printf(&a, "\n");
    for( int i = 0; i < view.event_count; i++ )
    {
        answer_printf(&a, "event\t%lld\t%d\t%d\t", view.events[i].time, (int)view.events[i].kind,
                      view.events[i].amount);
        answer_text(&a, view.events[i].label);
        answer_printf(&a, "\n");
    }
    if( history[0] == '1' && view.has_history )
    {
        for( int w = 0; w < HISCORES_GAIN_WINDOWS; w++ )
        {
            answer_printf(&a, "gain\t%d\t%d", w, view.gain_partial[w]);
            for( int i = 0; i <= HISCORES_SKILLS; i++ )
                answer_printf(&a, "\t%lld", view.gain_base[w][i]);
            answer_printf(&a, "\n");
        }
        for( int i = 0; i < view.chart_count; i++ )
            answer_printf(&a, "chart\t%lld\t%lld\n", view.chart_day[i], view.chart_xp[i]);
    }
    if( history[0] == '1' )
    {
        for( int i = 0; i < view.session_count; i++ )
        {
            struct HiscoresSession const* s = &view.sessions[i];

            answer_printf(&a, "session\t%lld\t%lld\t%lld\t%d\t%lld\n", s->start, s->end,
                          s->xp_gained, s->top_stat, s->top_gain);
        }
        answer_printf(&a, "sessions\t%d\t%lld\n", view.session_total, view.played);
    }
    answer_send(&a, res, 200);
}

static void
route_view_hiscores(
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    static struct HiscoresRow rows[HISCORES_SERVER_BATCH_MAX];
    char field[24];
    int category = HISCORES_OVERALL;
    int first = 0;
    int max = 50;
    int count;
    struct Answer a;

    if( query_field(req, "category", field, sizeof(field)) )
        category = atoi(field);
    if( query_field(req, "first", field, sizeof(field)) )
        first = atoi(field);
    if( query_field(req, "max", field, sizeof(field)) )
        max = atoi(field);
    if( first < 0 || max < 1 || max > HISCORES_SERVER_BATCH_MAX )
    {
        refuse(res, 400, "first must be 0 or more, max 1 to 512");
        return;
    }
    count = HiscoresDb_Hiscores(category, first, max, rows);
    answer_init(&a);
    answer_printf(&a, "ranked\t%d\n", HiscoresDb_RankedCount(category));
    for( int i = 0; i < count; i++ )
    {
        answer_printf(&a, "row\t%lld\t", rows[i].score);
        answer_text(&a, rows[i].key);
        answer_printf(&a, "\t");
        answer_text(&a, rows[i].name);
        answer_printf(&a, "\n");
    }
    answer_send(&a, res, 200);
}

void
HiscoresServer_Handle(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char name[HISCORES_NAME_MAX];
    char field[16];
    int is_post;

    (void)user;
    assert(req);
    assert(res);
    is_post = strcmp(req->method, "POST") == 0;

    if( is_post && path_is(req, "/events") )
    {
        route_events(req, res);
        return;
    }
    if( is_post && path_is(req, "/player/ranked") )
    {
        if( !body_field(req, "name", name, sizeof(name)) ||
            !body_field(req, "ranked", field, sizeof(field)) )
        {
            refuse(res, 400, "name and ranked");
            return;
        }
        HiscoresDb_SetRanked(name, atoi(field) != 0);
        route_result(res, 1, NULL);
        return;
    }
    if( !is_post && path_is(req, "/view/player") )
    {
        route_view_player(req, res);
        return;
    }
    if( !is_post && path_is(req, "/view/hiscores") )
    {
        route_view_hiscores(req, res);
        return;
    }
    refuse(res, 404, "no such route");
}
