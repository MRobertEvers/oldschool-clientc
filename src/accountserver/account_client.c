/*
 * Asking the account server. See account_client.h.
 */
#include "account_client.h"

#include "account_server.h"
#include "mem/torirs_mem.h"
#include "platform/platform_x_http.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A page waits this long for an answer; the server is on this machine. */
#define CLIENT_CONNECT_SEC 2
#define CLIENT_READ_SEC 5

/* The most lines one answer is read as: a player view's chart and sessions. */
#define CLIENT_LINES_MAX (ACCOUNT_CHART_DAYS + 128)

static char g_problem[256];
static AccountClientExchange g_exchange;

void
AccountClient_SetExchange(AccountClientExchange exchange)
{
    g_exchange = exchange;
}

static void
server_address(
    char* host,
    size_t cap,
    int* port)
{
    char const* env = getenv("TORIRS_ACCOUNT_SERVER");
    char const* colon = env ? strrchr(env, ':') : NULL;

    if( env && colon && colon > env )
    {
        snprintf(host, cap, "%.*s", (int)(colon - env), env);
        *port = atoi(colon + 1);
        return;
    }
    snprintf(host, cap, "127.0.0.1");
    *port = ACCOUNT_SERVER_PORT;
}

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
 * its status; NULL when nothing answered. `body` NULL is a GET. */
static char*
ask(
    char const* route,
    char const* body,
    int* out_status)
{
    char host[128];
    int port;
    int size = 0;
    int status = 0;
    char* raw;
    char* text;

    server_address(host, sizeof(host), &port);
    if( g_exchange )
    {
        raw = g_exchange(host, port, route, body, &size, &status);
        /* A GET answers its body only when it is a 200, as the blocking GET
         * does: a 404 is "no such player", not an answer to read. */
        if( raw && !body && status != 200 )
        {
            ToriMem_Free(ToriMem_Net(), raw);
            raw = NULL;
        }
    }
    else if( body )
        raw = PlatformX_HttpPostForm(host, port, route, NULL, body, (int)strlen(body), &size,
                                     &status);
    else
        raw = PlatformX_HttpGetTimed(host, port, route, &size, &status, CLIENT_CONNECT_SEC,
                                     CLIENT_READ_SEC);
    *out_status = status;
    if( !raw )
    {
        /* A 404 GET answers (no such player); nothing at all is an outage. */
        if( status == 0 )
            fprintf(stderr, "account client: %s:%d did not answer %s\n", host, port, route);
        return NULL;
    }
    text = ToriMem_Alloc(ToriMem_Net(), (size_t)size + 1);
    assert(text);
    memcpy(text, raw, (size_t)size);
    text[size] = '\0';
    ToriMem_Free(ToriMem_Net(), raw);
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
    n = AccountProto_Fields(text, f, 3);
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
        fprintf(stderr, "account client: %s answered %d: %s\n", route, status, f[0]);
    ToriMem_Free(ToriMem_Net(), text);
    return result;
}

enum ToriRSServerAuthResult
AccountClient_GateLogin(
    const char* name,
    const char* password)
{
    char body[512] = "";
    int result;

    assert(name);
    assert(password);
    field_into(body, sizeof(body), "name", name);
    field_into(body, sizeof(body), "password", password);
    result = ask_result("/gate/login", body, NULL);
    memset(body, 0, sizeof(body));
    return result < 0 ? TORIRSSERVER_AUTH_UNAVAILABLE : (enum ToriRSServerAuthResult)result;
}

enum ToriRSServerCreateResult
AccountClient_GateLoginCreate(
    const char* name,
    const char* password,
    const char** out_problem)
{
    char body[512] = "";
    int result;

    assert(name);
    assert(password);
    assert(out_problem);
    field_into(body, sizeof(body), "name", name);
    field_into(body, sizeof(body), "password", password);
    result = ask_result("/gate/create", body, out_problem);
    memset(body, 0, sizeof(body));
    return result < 0 ? TORIRSSERVER_CREATE_UNAVAILABLE : (enum ToriRSServerCreateResult)result;
}

int
AccountClient_GateLoginRevoke(const char* name)
{
    char body[256] = "";

    assert(name);
    field_into(body, sizeof(body), "name", name);
    return ask_result("/gate/revoke", body, NULL);
}

int
AccountClient_GateLoginEach(void (*each)(const char* name))
{
    static char* lines[1024];
    int status;
    char* text;
    int count;

    assert(each);
    text = ask("/gate/list", NULL, &status);
    if( !text )
        return -1;
    count = lines_of(text, lines, 1024);
    for( int i = 0; i < count; i++ )
        if( strncmp(lines[i], "login\t", 6) == 0 )
            each(lines[i] + 6);
    ToriMem_Free(ToriMem_Net(), text);
    return 1;
}

int
AccountClient_InviteCreate(
    const char* name,
    char out_code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1])
{
    char body[256] = "";
    int status;
    char* text;
    int ok = 0;

    assert(out_code);
    if( name )
        field_into(body, sizeof(body), "name", name);
    text = ask("/invite/create", body, &status);
    if( !text )
        return -1;
    if( status == 200 && strncmp(text, "code\t", 5) == 0 )
    {
        snprintf(out_code, TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1, "%.*s",
                 TORIRSSERVER_INVITE_CODE_BYTES * 2, text + 5);
        ok = 1;
    }
    else
        fprintf(stderr, "account client: /invite/create answered %d: %s\n", status, text);
    ToriMem_Free(ToriMem_Net(), text);
    return ok ? 1 : -1;
}

int
AccountClient_InviteLookup(
    const char* code,
    char* out_name,
    size_t cap)
{
    char route[256] = "/invite?";
    int status;
    char* text;
    char* f[3];
    int found = -1;

    assert(code);
    assert(out_name);
    field_into(route + 8, sizeof(route) - 8, "code", code);
    text = ask(route, NULL, &status);
    if( !text )
        return -1;
    text[strcspn(text, "\n")] = '\0';
    if( AccountProto_Fields(text, f, 3) == 3 && strcmp(f[0], "invite") == 0 )
    {
        found = atoi(f[1]) != 0;
        snprintf(out_name, cap, "%s", found ? f[2] : "");
    }
    ToriMem_Free(ToriMem_Net(), text);
    return found;
}

enum ToriRSServerInviteResult
AccountClient_InviteRedeem(
    const char* code,
    const char* name,
    const char* password,
    const char** out_problem)
{
    char body[512] = "";
    int result;

    assert(code);
    assert(name);
    assert(password);
    assert(out_problem);
    field_into(body, sizeof(body), "code", code);
    field_into(body, sizeof(body), "name", name);
    field_into(body, sizeof(body), "password", password);
    result = ask_result("/invite/redeem", body, out_problem);
    memset(body, 0, sizeof(body));
    return result < 0 ? TORIRSSERVER_INVITE_UNAVAILABLE : (enum ToriRSServerInviteResult)result;
}

int
AccountClient_SetRanked(
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
AccountClient_PlayerView(
    const char* key,
    int history,
    struct AccountPlayerView* out)
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
        char* f[ACCOUNT_SKILLS + 8];
        int n = AccountProto_Fields(lines[i], f, ACCOUNT_SKILLS + 8);

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
        else if( strcmp(f[0], "xp") == 0 && n == 1 + TORIRSSERVER_ACCOUNTLOG_STATS )
            ints_into(f, 1, TORIRSSERVER_ACCOUNTLOG_STATS, out->player.xp);
        else if( strcmp(f[0], "rank") == 0 && n == 2 + ACCOUNT_SKILLS )
        {
            out->rank_overall = atoi(f[1]);
            for( int s = 0; s < ACCOUNT_SKILLS; s++ )
                out->rank[s] = atoi(f[2 + s]);
        }
        else if( strcmp(f[0], "event") == 0 && n == 5 && out->event_count < ACCOUNT_EVENTS_SHOWN )
        {
            struct AccountEvent* e = &out->events[out->event_count++];

            e->time = strtoll(f[1], NULL, 10);
            e->kind = (enum ToriRSServerAdventureKind)atoi(f[2]);
            e->amount = atoi(f[3]);
            snprintf(e->label, sizeof(e->label), "%s", f[4]);
        }
        else if( strcmp(f[0], "gain") == 0 && n == 3 + ACCOUNT_SKILLS + 1 )
        {
            int w = atoi(f[1]);

            if( w >= 0 && w < ACCOUNT_GAIN_WINDOWS )
            {
                out->has_history = 1;
                out->gain_partial[w] = atoi(f[2]);
                ints_into(f, 3, ACCOUNT_SKILLS + 1, out->gain_base[w]);
            }
        }
        else if( strcmp(f[0], "chart") == 0 && n == 3 && out->chart_count < ACCOUNT_CHART_DAYS )
        {
            out->chart_day[out->chart_count] = strtoll(f[1], NULL, 10);
            out->chart_xp[out->chart_count] = strtoll(f[2], NULL, 10);
            out->chart_count++;
        }
        else if( strcmp(f[0], "session") == 0 && n == 6 &&
                 out->session_count < ACCOUNT_SESSIONS_SHOWN )
        {
            struct AccountSession* s = &out->sessions[out->session_count++];

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
AccountClient_Hiscores(
    int category,
    int first,
    int max,
    struct AccountRow* rows,
    int* out_ranked)
{
    static char* lines[ACCOUNT_SERVER_BATCH_MAX + 4];
    char route[128];
    int status;
    char* text;
    int count;
    int got = 0;

    assert(rows);
    assert(out_ranked);
    assert(max > 0);
    assert(max <= ACCOUNT_SERVER_BATCH_MAX);
    *out_ranked = 0;
    snprintf(route, sizeof(route), "/view/hiscores?category=%d&first=%d&max=%d", category, first,
             max);
    text = ask(route, NULL, &status);
    if( !text )
        return -1;
    count = lines_of(text, lines, ACCOUNT_SERVER_BATCH_MAX + 4);
    for( int i = 0; i < count; i++ )
    {
        char* f[4];
        int n = AccountProto_Fields(lines[i], f, 4);

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
