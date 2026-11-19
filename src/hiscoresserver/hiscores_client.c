/*
 * Asking the hiscores server. See hiscores_client.h.
 */
#include "hiscores_client.h"

#include "hiscores_server.h"
#include "service/service_link.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A page waits this long for an answer; the server is on this machine. */
#define CLIENT_CONNECT_SEC 2
#define CLIENT_READ_SEC 5

/* The most lines one answer is read as: a player view's chart and sessions. */
#define CLIENT_LINES_MAX (HISCORES_CHART_DAYS + 128)

static char g_problem[256];
static struct ServiceLink g_link = { "hiscores", "TORIRS_HISCORES_SERVER", HISCORES_SERVER_PORT, "/svc/hiscores", NULL, NULL };

/* `text` form-encoded onto the end of `out`. */
static void
encode_into(
    char* out,
    size_t cap,
    char const* text)
{
    static char const hex[] = "0123456789ABCDEF";
    size_t len = strlen(out);

    for( ; *text && len + 4 < cap; text++ )
    {
        unsigned char c = (unsigned char)*text;

        if( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' )
            out[len++] = (char)c;
        else
        {
            out[len++] = '%';
            out[len++] = hex[c >> 4];
            out[len++] = hex[c & 15];
        }
    }
    out[len] = '\0';
}

/* `&name=value` (or `name=value` first) onto `out`. */
static void
field_into(
    char* out,
    size_t cap,
    char const* name,
    char const* value)
{
    size_t len = strlen(out);

    snprintf(out + len, cap - len, "%s%s=", len ? "&" : "", name);
    encode_into(out, cap, value);
}

/* The answer to `route` as a NUL-terminated copy (free with ToriMem_Net), and
 * its status; NULL when nothing answered. `body` NULL is a GET. A GET's
 * refusal is no answer to read: a 404 is "no such player". */
static char*
ask(
    char const* route,
    char const* body,
    int* out_status)
{
    int size;
    char* text = ServiceLink_Ask(&g_link, body ? "POST" : "GET", route, body,
                                 body ? (int)strlen(body) : 0, out_status, &size);

    if( text && !body && *out_status != 200 )
    {
        ToriMem_Free(ToriMem_Net(), text);
        return NULL;
    }
    return text;
}

/* The answer's lines, in place. */
static int
lines_of(
    char* text,
    char** lines,
    int max)
{
    int count = 0;

    while( text && *text && count < max )
    {
        char* next = strchr(text, '\n');

        if( next )
            *next++ = '\0';
        lines[count++] = text;
        text = next;
    }
    return count;
}

/* A `result <n> <problem>` answer: n, with the problem in g_problem; -1 for no
 * answer or one that is not a result. */
static int
ask_result(
    char const* route,
    char const* body,
    char const** out_problem)
{
    int status;
    char* text = ask(route, body, &status);
    char* f[3];
    int n;
    int result = -1;

    g_problem[0] = '\0';
    if( out_problem )
        *out_problem = NULL;
    if( !text )
        return -1;
    text[strcspn(text, "\n")] = '\0';
    n = HiscoresProto_Fields(text, f, 3);
    if( status == 200 && n >= 2 && strcmp(f[0], "result") == 0 )
    {
        result = atoi(f[1]);
        if( n == 3 && f[2][0] )
        {
            snprintf(g_problem, sizeof(g_problem), "%s", f[2]);
            if( out_problem )
                *out_problem = g_problem;
        }
    }
    else
        fprintf(stderr, "hiscores client: %s answered %d: %s\n", route, status, f[0]);
    ToriMem_Free(ToriMem_Net(), text);
    return result;
}

int
HiscoresClient_SetRanked(
    const char* display_name,
    int ranked)
{
    char body[256] = "";

    assert(display_name);
    field_into(body, sizeof(body), "name", display_name);
    field_into(body, sizeof(body), "ranked", ranked ? "1" : "0");
    return ask_result("/player/ranked", body, NULL) < 0 ? -1 : 1;
}

/* `count` integers from fields[first..] into `out`. */
static void
ints_into(
    char** fields,
    int first,
    int count,
    long long* out)
{
    for( int i = 0; i < count; i++ )
        out[i] = strtoll(fields[first + i], NULL, 10);
}

int
HiscoresClient_PlayerView(
    const char* key,
    int history,
    struct HiscoresPlayerView* out)
{
    static char* lines[CLIENT_LINES_MAX];
    char route[256] = "/view/player?";
    int status;
    char* text;
    int count;
    int found = 0;

    assert(key);
    assert(out);
    memset(out, 0, sizeof(*out));
    field_into(route + 13, sizeof(route) - 13, "key", key);
    strncat(route, history ? "&history=1" : "&history=0", sizeof(route) - strlen(route) - 1);
    text = ask(route, NULL, &status);
    if( !text )
        return status == 404 ? 0 : -1;
    count = lines_of(text, lines, CLIENT_LINES_MAX);
    for( int i = 0; i < count; i++ )
    {
        char* f[HISCORES_SKILLS + 8];
        int n = HiscoresProto_Fields(lines[i], f, HISCORES_SKILLS + 8);

        if( strcmp(f[0], "player") == 0 && n == 7 )
        {
            out->player.id = strtoll(f[1], NULL, 10);
            out->player.ranked = atoi(f[2]);
            out->player.last_seen = strtoll(f[3], NULL, 10);
            out->player.quest_points = atoi(f[4]);
            snprintf(out->player.key, sizeof(out->player.key), "%s", f[5]);
            snprintf(out->player.name, sizeof(out->player.name), "%s", f[6]);
            found = 1;
        }
        else if( strcmp(f[0], "xp") == 0 && n == 1 + TORIRSSERVER_HISCORESLOG_STATS )
            ints_into(f, 1, TORIRSSERVER_HISCORESLOG_STATS, out->player.xp);
        else if( strcmp(f[0], "rank") == 0 && n == 2 + HISCORES_SKILLS )
        {
            out->rank_overall = atoi(f[1]);
            for( int s = 0; s < HISCORES_SKILLS; s++ )
                out->rank[s] = atoi(f[2 + s]);
        }
        else if( strcmp(f[0], "event") == 0 && n == 5 && out->event_count < HISCORES_EVENTS_SHOWN )
        {
            struct HiscoresEvent* e = &out->events[out->event_count++];

            e->time = strtoll(f[1], NULL, 10);
            e->kind = (enum ToriRSServerAdventureKind)atoi(f[2]);
            e->amount = atoi(f[3]);
            snprintf(e->label, sizeof(e->label), "%s", f[4]);
        }
        else if( strcmp(f[0], "gain") == 0 && n == 3 + HISCORES_SKILLS + 1 )
        {
            int w = atoi(f[1]);

            if( w >= 0 && w < HISCORES_GAIN_WINDOWS )
            {
                out->has_history = 1;
                out->gain_partial[w] = atoi(f[2]);
                ints_into(f, 3, HISCORES_SKILLS + 1, out->gain_base[w]);
            }
        }
        else if( strcmp(f[0], "chart") == 0 && n == 3 && out->chart_count < HISCORES_CHART_DAYS )
        {
            out->chart_day[out->chart_count] = strtoll(f[1], NULL, 10);
            out->chart_xp[out->chart_count] = strtoll(f[2], NULL, 10);
            out->chart_count++;
        }
        else if( strcmp(f[0], "session") == 0 && n == 6 &&
                 out->session_count < HISCORES_SESSIONS_SHOWN )
        {
            struct HiscoresSession* s = &out->sessions[out->session_count++];

            s->start = strtoll(f[1], NULL, 10);
            s->end = strtoll(f[2], NULL, 10);
            s->xp_gained = strtoll(f[3], NULL, 10);
            s->top_stat = atoi(f[4]);
            s->top_gain = strtoll(f[5], NULL, 10);
        }
        else if( strcmp(f[0], "sessions") == 0 && n == 3 )
        {
            out->session_total = atoi(f[1]);
            out->played = strtoll(f[2], NULL, 10);
        }
    }
    ToriMem_Free(ToriMem_Net(), text);
    return found ? 1 : -1;
}

int
HiscoresClient_Hiscores(
    int category,
    int first,
    int max,
    struct HiscoresRow* rows,
    int* out_ranked)
{
    static char* lines[HISCORES_SERVER_BATCH_MAX + 4];
    char route[128];
    int status;
    char* text;
    int count;
    int got = 0;

    assert(rows);
    assert(out_ranked);
    assert(max > 0);
    assert(max <= HISCORES_SERVER_BATCH_MAX);
    *out_ranked = 0;
    snprintf(route, sizeof(route), "/view/hiscores?category=%d&first=%d&max=%d", category, first,
             max);
    text = ask(route, NULL, &status);
    if( !text )
        return -1;
    count = lines_of(text, lines, HISCORES_SERVER_BATCH_MAX + 4);
    for( int i = 0; i < count; i++ )
    {
        char* f[4];
        int n = HiscoresProto_Fields(lines[i], f, 4);

        if( strcmp(f[0], "ranked") == 0 && n == 2 )
            *out_ranked = atoi(f[1]);
        else if( strcmp(f[0], "row") == 0 && n == 4 && got < max )
        {
            rows[got].score = strtoll(f[1], NULL, 10);
            snprintf(rows[got].key, sizeof(rows[got].key), "%s", f[2]);
            snprintf(rows[got].name, sizeof(rows[got].name), "%s", f[3]);
            got++;
        }
    }
    ToriMem_Free(ToriMem_Net(), text);
    return got;
}

void
HiscoresClient_SetLocal(HttpHandler handler)
{
    ServiceLink_SetLocal(&g_link, handler, NULL);
}
