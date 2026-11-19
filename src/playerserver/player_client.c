/*
 * Asking the player account server. See player_client.h.
 */
#include "player_client.h"

#include "mem/torirs_mem.h"
#include "platform/platform_x_http.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The server is on this machine; a world's login waits this long at most. */
#define CLIENT_CONNECT_SEC 2
#define CLIENT_READ_SEC 5

static PlayerClientExchange g_exchange;
static HttpHandler g_local;
static char g_problem[256];

void
PlayerClient_SetExchange(PlayerClientExchange exchange)
{
    g_exchange = exchange;
}

void
PlayerClient_SetLocal(HttpHandler handler)
{
    g_local = handler;
}

int
PlayerClient_RemoteNamed(void)
{
    char const* env = getenv("TORIRS_PLAYER_SERVER");

    return env && env[0];
}

static void
server_address(
    char* host,
    size_t cap,
    int* port)
{
    char const* env = getenv("TORIRS_PLAYER_SERVER");
    char const* colon = env ? strrchr(env, ':') : NULL;

    if( env && colon && colon > env )
    {
        snprintf(host, cap, "%.*s", (int)(colon - env), env);
        *port = atoi(colon + 1);
        return;
    }
    snprintf(host, cap, "127.0.0.1");
    *port = PLAYER_SERVER_PORT;
}

/* The handler, called as the port would call it. */
static char*
ask_local(
    char const* route,
    char const* body,
    int* out_size,
    int* out_status)
{
    struct HttpRequest req;
    struct HttpResponse res;
    char* copy;

    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    snprintf(req.method, sizeof(req.method), "%s", body ? "POST" : "GET");
    snprintf(req.path, sizeof(req.path), "%s", route);
    req.body = (uint8_t const*)body;
    req.body_len = body ? (int)strlen(body) : 0;
    g_local(NULL, &req, &res);
    *out_status = res.status ? res.status : 404;
    *out_size = res.body_len;
    copy = ToriMem_Alloc(ToriMem_Net(), (size_t)res.body_len + 1);
    assert(copy);
    if( res.body_len > 0 )
        memcpy(copy, res.body, (size_t)res.body_len);
    if( res.owns_body )
        ToriMem_Free(ToriMem_Net(), res.body);
    return copy;
}

/* The answer to `route` as a NUL-terminated copy (free with ToriMem_Net) and
 * its status; NULL when nothing answered. `body` NULL is a GET. */
static char*
ask_sized(
    char const* route,
    char const* body,
    int* out_status,
    int* out_size)
{
    char host[128];
    int port;
    int size = 0;
    int status = 0;
    char* raw;
    char* text;

    *out_size = 0;
    if( g_local )
    {
        text = ask_local(route, body, &size, out_status);
        text[size] = '\0';
        *out_size = size;
        return text;
    }
    server_address(host, sizeof(host), &port);
    if( g_exchange )
        raw = g_exchange(host, port, route, body, &size, &status);
    else if( body )
        raw = PlatformX_HttpPostForm(host, port, route, NULL, body, (int)strlen(body), &size,
                                     &status);
    else
        raw = PlatformX_HttpGetTimed(host, port, route, &size, &status, CLIENT_CONNECT_SEC,
                                     CLIENT_READ_SEC);
    *out_status = status;
    if( !raw && status == 0 )
    {
        fprintf(stderr, "player client: %s:%d did not answer %s (TORIRS_PLAYER_SERVER)\n", host,
                port, route);
        return NULL;
    }
    if( !raw )
    {
        /* A blocking GET keeps a refusal's body to itself; the status is the
         * answer (a 400 is an unusable name, not an outage). */
        text = ToriMem_Alloc(ToriMem_Net(), 1);
        assert(text);
        text[0] = '\0';
        return text;
    }
    text = ToriMem_Alloc(ToriMem_Net(), (size_t)size + 1);
    assert(text);
    memcpy(text, raw, (size_t)size);
    text[size] = '\0';
    ToriMem_Free(ToriMem_Net(), raw);
    *out_size = size;
    return text;
}

static char*
ask(
    char const* route,
    char const* body,
    int* out_status)
{
    int size;

    return ask_sized(route, body, out_status, &size);
}

/* The first line's fields when the answer is a 200 whose first field is
 * `verb`; the count, 0 when it is not, -1 when nothing answered. `*out_text`
 * owns the fields (free with ToriMem_Net) whenever the count is not -1. */
static int
ask_line(
    char const* route,
    char const* body,
    char const* verb,
    char** fields,
    int max,
    char** out_text)
{
    int status;
    char* text = ask(route, body, &status);
    int n;

    *out_text = text;
    if( !text )
        return -1;
    text[strcspn(text, "\n")] = '\0';
    n = PlayerProto_Fields(text, fields, max);
    if( status == 200 && n >= 1 && strcmp(fields[0], verb) == 0 )
        return n;
    fprintf(stderr, "player client: %s answered %d: %s\n", route, status, text);
    return 0;
}

/* A `result <n> [<problem>]` answer: n, the problem into g_problem; -1 for no
 * answer or one that is not a result. */
static int
ask_result(
    char const* route,
    char const* body)
{
    char* f[3];
    char* text;
    int n = ask_line(route, body, "result", f, 3, &text);
    int result = -1;

    g_problem[0] = '\0';
    if( n >= 2 )
    {
        result = atoi(f[1]);
        if( n == 3 )
            snprintf(g_problem, sizeof(g_problem), "%s", f[2]);
    }
    if( text )
        ToriMem_Free(ToriMem_Net(), text);
    return result;
}

/* ------------------------------------------------------------------ */
/* Logins and leases                                                   */
/* ------------------------------------------------------------------ */

void
PlayerClient_Login(
    char const* world,
    char const* boot,
    char const* name,
    char const* password,
    struct PlayerLogin* out)
{
    char body[512] = "";
    char* f[8];
    char* text;
    char* rest;
    int status;
    int size;

    assert(world);
    assert(boot);
    assert(name);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->result = TORIRSSERVER_AUTH_UNAVAILABLE;
    PlayerProto_FormAdd(body, sizeof(body), "world", world);
    PlayerProto_FormAdd(body, sizeof(body), "boot", boot);
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    if( password )
        PlayerProto_FormAdd(body, sizeof(body), "password", password);
    text = ask_sized(password ? "/login" : "/claim", body, &status, &size);
    memset(body, 0, sizeof(body));
    if( !text )
        return;
    rest = strchr(text, '\n');
    if( rest )
        *rest++ = '\0';
    if( status == 200 && rest && PlayerProto_Fields(text, f, 8) == 8 &&
        strcmp(f[0], "login") == 0 )
    {
        long save_len = atol(f[7]);

        out->result = atoi(f[1]);
        out->admin = atoi(f[2]) != 0;
        out->record = atoi(f[3]) != 0;
        if( strcmp(f[4], "-") != 0 )
            snprintf(out->lease, sizeof(out->lease), "%s", f[4]);
        if( strcmp(f[5], "-") != 0 )
            snprintf(out->holder, sizeof(out->holder), "%s", f[5]);
        out->had = atoi(f[6]) != 0;
        /* A grant with no lease would be a login nobody could hand back, and
         * a save cut short a character with half its things. */
        if( (out->result == TORIRSSERVER_AUTH_OK || out->result == TORIRSSERVER_AUTH_CLAIMED) &&
            (!out->lease[0] || save_len < 0 || save_len > (long)(text + size - rest)) )
        {
            fprintf(stderr, "player client: a login of '%s' came back malformed\n", name);
            out->result = TORIRSSERVER_AUTH_UNAVAILABLE;
            out->lease[0] = '\0';
        }
        else if( save_len > 0 )
        {
            out->save = ToriMem_Alloc(ToriMem_Net(), (size_t)save_len + 1);
            assert(out->save);
            memcpy(out->save, rest, (size_t)save_len);
            out->save[save_len] = '\0';
            out->save_len = save_len;
        }
    }
    else
        fprintf(stderr, "player client: a login of '%s' answered %d\n", name, status);
    ToriMem_Free(ToriMem_Net(), text);
}

void
PlayerClient_LoginFree(struct PlayerLogin* login)
{
    assert(login);
    ToriMem_Free(ToriMem_Net(), login->save);
    login->save = NULL;
    login->save_len = 0;
}

enum ToriRSServerAuthResult
PlayerClient_Auth(
    char const* name,
    char const* password)
{
    char body[512] = "";
    char* f[4];
    char* text;
    int n;
    enum ToriRSServerAuthResult result = TORIRSSERVER_AUTH_UNAVAILABLE;

    assert(name);
    assert(password);
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    PlayerProto_FormAdd(body, sizeof(body), "password", password);
    n = ask_line("/auth", body, "auth", f, 4, &text);
    memset(body, 0, sizeof(body));
    if( n == 4 )
        result = (enum ToriRSServerAuthResult)atoi(f[1]);
    if( text )
        ToriMem_Free(ToriMem_Net(), text);
    return result;
}

int
PlayerClient_Release(
    char const* world,
    char const* name,
    char const* lease)
{
    char body[512] = "";

    assert(world);
    assert(name);
    assert(lease);
    PlayerProto_FormAdd(body, sizeof(body), "world", world);
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    PlayerProto_FormAdd(body, sizeof(body), "lease", lease);
    return ask_result("/release", body);
}

int
PlayerClient_Hello(
    char const* world,
    char const* boot,
    char (*keep)[PLAYER_KEY_MAX],
    int count)
{
    size_t list_cap = (size_t)count * (PLAYER_KEY_MAX + 1) + 1;
    size_t body_cap = list_cap * 3 + PLAYER_WORLD_MAX * 3 + 32;
    char* list = ToriMem_Alloc(ToriMem_Net(), list_cap);
    char* body = ToriMem_Alloc(ToriMem_Net(), body_cap);
    size_t len = 0;
    int dropped;

    assert(world);
    assert(boot);
    assert(count >= 0);
    assert(list);
    assert(body);
    list[0] = '\0';
    body[0] = '\0';
    for( int i = 0; i < count; i++ )
    {
        assert(keep);
        len += (size_t)snprintf(list + len, list_cap - len, "%s%s", i ? "," : "", keep[i]);
        assert(len < list_cap);
    }
    PlayerProto_FormAdd(body, body_cap, "world", world);
    PlayerProto_FormAdd(body, body_cap, "boot", boot);
    PlayerProto_FormAdd(body, body_cap, "keep", list);
    ToriMem_Free(ToriMem_Net(), list);
    dropped = ask_result("/hello", body);
    ToriMem_Free(ToriMem_Net(), body);
    return dropped;
}

/* `route?name=..&..` with `save` as the body, answered `result <n> <who>`:
 * n, the world into `out_world`; -1 no answer. */
static int
ask_save_body(
    char const* route,
    char const* save,
    char* out_world,
    size_t world_cap)
{
    int result = ask_result(route, save);

    snprintf(out_world, world_cap, "%s", result >= 0 ? g_problem : "");
    return result;
}

int
PlayerClient_Logout(
    char const* world,
    char const* name,
    char const* lease,
    char const* save,
    char* out_holder,
    size_t holder_cap)
{
    char route[512] = "/logout?";

    assert(world);
    assert(name);
    assert(lease);
    assert(save);
    assert(save[0]);
    assert(out_holder);
    PlayerProto_FormAdd(route + 8, sizeof(route) - 8, "world", world);
    PlayerProto_FormAdd(route + 8, sizeof(route) - 8, "name", name);
    PlayerProto_FormAdd(route + 8, sizeof(route) - 8, "lease", lease);
    return ask_save_body(route, save, out_holder, holder_cap);
}

int
PlayerClient_Revoke(
    char const* name,
    char* out_world,
    size_t world_cap)
{
    char body[256] = "";
    int result;

    assert(name);
    assert(out_world);
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    result = ask_result("/account/release", body);
    snprintf(out_world, world_cap, "%s", result > 0 ? g_problem : "");
    return result;
}

int
PlayerClient_SaveGet(
    char const* name,
    char** out_save,
    char* out_world,
    size_t world_cap)
{
    char route[256] = "/save?";
    char* f[4];
    char* text;
    char* rest;
    int status;
    int size;
    int found = -1;

    assert(name);
    assert(out_save);
    assert(out_world);
    *out_save = NULL;
    out_world[0] = '\0';
    PlayerProto_FormAdd(route + 6, sizeof(route) - 6, "name", name);
    text = ask_sized(route, NULL, &status, &size);
    if( !text )
        return -1;
    rest = strchr(text, '\n');
    if( rest )
        *rest++ = '\0';
    if( status == 200 && rest && PlayerProto_Fields(text, f, 4) == 4 && strcmp(f[0], "save") == 0 )
    {
        long len = atol(f[3]);

        if( strcmp(f[2], "-") != 0 )
            snprintf(out_world, world_cap, "%s", f[2]);
        found = atoi(f[1]) != 0 && len > 0 && len <= (long)(text + size - rest);
        if( found )
        {
            *out_save = ToriMem_Alloc(ToriMem_Net(), (size_t)len + 1);
            assert(*out_save);
            memcpy(*out_save, rest, (size_t)len);
            (*out_save)[len] = '\0';
        }
    }
    else if( status == 400 )
        found = 0;
    ToriMem_Free(ToriMem_Net(), text);
    return found;
}

int
PlayerClient_SavePut(
    char const* name,
    char const* save,
    char* out_world,
    size_t world_cap)
{
    char route[256] = "/save?";

    assert(name);
    assert(save);
    assert(save[0]);
    assert(out_world);
    PlayerProto_FormAdd(route + 6, sizeof(route) - 6, "name", name);
    return ask_save_body(route, save, out_world, world_cap);
}

int
PlayerClient_NameOwner(
    char const* name,
    char* owner,
    size_t cap)
{
    char route[256] = "/name?";
    char* f[3];
    char* text;
    int n;
    int held = -1;

    assert(name);
    assert(owner);
    assert(cap > 0);
    owner[0] = '\0';
    PlayerProto_FormAdd(route + 6, sizeof(route) - 6, "name", name);
    n = ask_line(route, NULL, "name", f, 3, &text);
    if( n == 3 )
    {
        held = atoi(f[1]) != 0;
        if( held && strcmp(f[2], "-") != 0 )
            snprintf(owner, cap, "%s", f[2]);
    }
    if( text )
        ToriMem_Free(ToriMem_Net(), text);
    return held;
}

int
PlayerClient_NameClaim(
    char const* name,
    char const* account)
{
    char body[256] = "";

    assert(name);
    assert(account);
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    PlayerProto_FormAdd(body, sizeof(body), "account", account);
    return ask_result("/name/claim", body) == 1;
}

void
PlayerClient_NameRelease(
    char const* name,
    char const* account)
{
    char body[256] = "";

    assert(name);
    assert(account);
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    PlayerProto_FormAdd(body, sizeof(body), "account", account);
    ask_result("/name/release", body);
}

/* ------------------------------------------------------------------ */
/* Records                                                             */
/* ------------------------------------------------------------------ */

/* `account <key> <exists> <admin> <record> <linked> <owner> <world>`, in
 * place. 1 parsed. */
static int
account_parse(
    char* line,
    struct PlayerAccountInfo* out,
    int* out_exists)
{
    char* f[8];

    if( PlayerProto_Fields(line, f, 8) != 8 || strcmp(f[0], "account") != 0 )
        return 0;
    memset(out, 0, sizeof(*out));
    snprintf(out->key, sizeof(out->key), "%s", f[1]);
    *out_exists = atoi(f[2]) != 0;
    out->admin = atoi(f[3]) != 0;
    out->record = atoi(f[4]) != 0;
    out->linked = atoi(f[5]);
    if( strcmp(f[6], "-") != 0 )
        snprintf(out->owner, sizeof(out->owner), "%s", f[6]);
    if( strcmp(f[7], "-") != 0 )
        snprintf(out->world, sizeof(out->world), "%s", f[7]);
    return 1;
}

int
PlayerClient_Account(
    char const* name,
    struct PlayerAccountInfo* out)
{
    char route[256] = "/account?";
    int status;
    char* text;
    int exists = 0;
    int found = -1;

    assert(name);
    assert(out);
    memset(out, 0, sizeof(*out));
    PlayerProto_FormAdd(route + 9, sizeof(route) - 9, "name", name);
    text = ask(route, NULL, &status);
    if( !text )
        return -1;
    text[strcspn(text, "\n")] = '\0';
    if( status == 200 && account_parse(text, out, &exists) )
        found = exists;
    else if( status == 400 )
        /* An unusable name has no account. */
        found = 0;
    ToriMem_Free(ToriMem_Net(), text);
    return found;
}

int
PlayerClient_Each(
    char const* filter,
    void (*each)(struct PlayerAccountInfo const* account, void* ctx),
    void* ctx)
{
    char route[256] = "/accounts";
    int status;
    char* text;
    char* line;
    int count = 0;

    assert(filter);
    assert(each);
    if( filter[0] )
    {
        snprintf(route, sizeof(route), "/accounts?");
        PlayerProto_FormAdd(route + 10, sizeof(route) - 10, "q", filter);
    }
    text = ask(route, NULL, &status);
    if( !text )
        return -1;
    if( status != 200 )
    {
        ToriMem_Free(ToriMem_Net(), text);
        return -1;
    }
    line = text;
    while( line && *line )
    {
        char* next = strchr(line, '\n');
        struct PlayerAccountInfo account;
        int exists;

        if( next )
            *next++ = '\0';
        if( account_parse(line, &account, &exists) )
        {
            each(&account, ctx);
            count++;
        }
        line = next;
    }
    ToriMem_Free(ToriMem_Net(), text);
    return count;
}

int
PlayerClient_IsAdmin(char const* name)
{
    struct PlayerAccountInfo account;

    return PlayerClient_Account(name, &account) == 1 && account.admin;
}

int
PlayerClient_IsRecorded(char const* name)
{
    struct PlayerAccountInfo account;

    return PlayerClient_Account(name, &account) == 1 && account.record;
}

int
PlayerClient_ManagedBy(
    char const* name,
    char* out_user,
    size_t cap)
{
    struct PlayerAccountInfo account;
    int found = PlayerClient_Account(name, &account);

    assert(out_user);
    assert(cap > 0);
    out_user[0] = '\0';
    if( found < 0 )
        return -1;
    if( found == 0 )
        return 0;
    if( account.linked == 1 )
        snprintf(out_user, cap, "%s", account.owner);
    return account.linked;
}

int
PlayerClient_LoginIsStaff(char const* login)
{
    char route[256] = "/staff?";

    assert(login);
    PlayerProto_FormAdd(route + 7, sizeof(route) - 7, "login", login);
    return ask_result(route, NULL) == 1;
}

int
PlayerClient_LinkCheck(
    char const* name,
    char const* user,
    char const* link_token)
{
    char body[512] = "";
    int verified;

    assert(name);
    assert(user);
    assert(link_token);
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    PlayerProto_FormAdd(body, sizeof(body), "user", user);
    PlayerProto_FormAdd(body, sizeof(body), "token", link_token);
    verified = ask_result("/account/verify", body);
    memset(body, 0, sizeof(body));
    return verified < 0 ? -1 : verified == 1;
}

int
PlayerClient_LinkVerify(
    char const* name,
    char const* user,
    char const* link_token)
{
    return PlayerClient_LinkCheck(name, user, link_token) == 1;
}

static enum ToriRSServerLinkResult
ask_link(
    char const* route,
    char* body,
    char out_token[TORIRSSERVER_LINK_TOKEN_HEX + 1])
{
    char* f[3];
    char* text;
    int n = ask_line(route, body, "link", f, 3, &text);
    enum ToriRSServerLinkResult result = TORIRSSERVER_LINK_UNAVAILABLE;

    out_token[0] = '\0';
    if( n == 3 )
    {
        result = (enum ToriRSServerLinkResult)atoi(f[1]);
        if( result == TORIRSSERVER_LINK_OK )
        {
            if( strlen(f[2]) != TORIRSSERVER_LINK_TOKEN_HEX )
                result = TORIRSSERVER_LINK_WRITE_FAILED;
            else
                snprintf(out_token, TORIRSSERVER_LINK_TOKEN_HEX + 1, "%s", f[2]);
        }
        memset(f[2], 0, strlen(f[2]));
    }
    if( text )
        ToriMem_Free(ToriMem_Net(), text);
    return result;
}

enum ToriRSServerLinkResult
PlayerClient_Link(
    char const* name,
    char const* password,
    char const* user,
    char out_token[TORIRSSERVER_LINK_TOKEN_HEX + 1])
{
    char body[512] = "";
    enum ToriRSServerLinkResult result;

    assert(name);
    assert(password);
    assert(user);
    assert(out_token);
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    PlayerProto_FormAdd(body, sizeof(body), "password", password);
    PlayerProto_FormAdd(body, sizeof(body), "user", user);
    result = ask_link("/account/link", body, out_token);
    memset(body, 0, sizeof(body));
    return result;
}

enum ToriRSServerLinkResult
PlayerClient_Assign(
    char const* name,
    char const* user,
    char out_token[TORIRSSERVER_LINK_TOKEN_HEX + 1])
{
    char body[512] = "";

    assert(name);
    assert(user);
    assert(out_token);
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    PlayerProto_FormAdd(body, sizeof(body), "user", user);
    return ask_link("/account/assign", body, out_token);
}

int
PlayerClient_Unlink(char const* name)
{
    char body[256] = "";

    assert(name);
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    return ask_result("/account/unlink", body) == 1;
}

enum ToriRSServerCreateResult
PlayerClient_Create(
    char const* name,
    char const* password,
    int admin,
    char const** out_problem)
{
    char body[512] = "";
    int result;

    assert(name);
    assert(password);
    assert(out_problem);
    *out_problem = NULL;
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    PlayerProto_FormAdd(body, sizeof(body), "password", password);
    PlayerProto_FormAdd(body, sizeof(body), "admin", admin ? "1" : "0");
    result = ask_result("/account/create", body);
    memset(body, 0, sizeof(body));
    if( result < 0 )
        return TORIRSSERVER_CREATE_UNAVAILABLE;
    if( g_problem[0] )
        *out_problem = g_problem;
    return (enum ToriRSServerCreateResult)result;
}

int
PlayerClient_SetPassword(
    char const* name,
    char const* password,
    char const** out_problem)
{
    char body[512] = "";
    int result;

    assert(name);
    assert(password);
    assert(out_problem);
    *out_problem = NULL;
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    PlayerProto_FormAdd(body, sizeof(body), "password", password);
    result = ask_result("/account/password", body);
    memset(body, 0, sizeof(body));
    if( result < 0 )
    {
        snprintf(g_problem, sizeof(g_problem), "%s",
                 "the player account server did not answer (TORIRS_PLAYER_SERVER)");
        *out_problem = g_problem;
        return 0;
    }
    if( result == 0 )
        *out_problem = g_problem[0] ? g_problem : "refused";
    return result == 1;
}

static int
set_flag(
    char const* route,
    char const* name,
    int on)
{
    char body[256] = "";

    assert(name);
    PlayerProto_FormAdd(body, sizeof(body), "name", name);
    PlayerProto_FormAdd(body, sizeof(body), "on", on ? "1" : "0");
    return ask_result(route, body) == 1;
}

int
PlayerClient_SetAdmin(
    char const* name,
    int admin)
{
    return set_flag("/account/admin", name, admin);
}

int
PlayerClient_SetRecorded(
    char const* name,
    int record)
{
    return set_flag("/account/record", name, record);
}
