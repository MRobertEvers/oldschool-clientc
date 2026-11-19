/*
 * The website's user accounts, for the processes outside it. See
 * website_users.h.
 */
#include "website_users.h"

#include "mem/torirs_mem.h"
#include "service/service_form.h"
#include "website_users_db.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USERS_NAME_MAX 64

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

    ServiceForm_Clean(text, clean, sizeof(clean));
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
each_login(
    char const* name,
    void* ctx)
{
    answer_printf((struct Answer*)ctx, "login\t");
    answer_text((struct Answer*)ctx, name);
    answer_printf((struct Answer*)ctx, "\n");
}

void
WebsiteUsers_Handle(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char name[USERS_NAME_MAX];
    char password[128];
    char code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1];
    int is_post;
    char const* problem = NULL;

    (void)user;
    assert(req);
    assert(res);
    is_post = strcmp(req->method, "POST") == 0;

    if( is_post && path_is(req, "/login") )
    {
        if( !body_field(req, "name", name, sizeof(name)) ||
            !body_field(req, "password", password, sizeof(password)) )
        {
            refuse(res, 400, "name and password");
            return;
        }
        route_result(res, (int)WebsiteUsersDb_GateLogin(name, password), NULL);
        memset(password, 0, sizeof(password));
        return;
    }
    if( is_post && path_is(req, "/create") )
    {
        enum ToriRSServerCreateResult result;

        if( !body_field(req, "name", name, sizeof(name)) ||
            !body_field(req, "password", password, sizeof(password)) )
        {
            refuse(res, 400, "name and password");
            return;
        }
        result = WebsiteUsersDb_GateLoginCreate(name, password, &problem);
        memset(password, 0, sizeof(password));
        route_result(res, (int)result, problem);
        return;
    }
    if( is_post && path_is(req, "/revoke") )
    {
        if( !body_field(req, "name", name, sizeof(name)) )
        {
            refuse(res, 400, "name");
            return;
        }
        route_result(res, WebsiteUsersDb_GateLoginRevoke(name), NULL);
        return;
    }
    if( !is_post && path_is(req, "/list") )
    {
        struct Answer a;

        answer_init(&a);
        WebsiteUsersDb_GateLoginEach(each_login, &a);
        answer_send(&a, res, 200);
        return;
    }
    if( is_post && path_is(req, "/invite/create") )
    {
        int named = body_field(req, "name", name, sizeof(name)) && name[0];
        struct Answer a;

        if( named && ToriRSServer_AuthNameProblem(name) )
        {
            refuse(res, 400, ToriRSServer_AuthNameProblem(name));
            return;
        }
        WebsiteUsersDb_InviteCreate(named ? name : NULL, code);
        answer_init(&a);
        answer_printf(&a, "code\t%s\n", code);
        answer_send(&a, res, 200);
        return;
    }
    if( !is_post && path_is(req, "/invite") )
    {
        int found = query_field(req, "code", code, sizeof(code)) &&
                    WebsiteUsersDb_InviteLookup(code, name, sizeof(name));
        struct Answer a;

        answer_init(&a);
        answer_printf(&a, "invite\t%d\t", found);
        answer_text(&a, found ? name : "");
        answer_printf(&a, "\n");
        answer_send(&a, res, 200);
        return;
    }
    if( is_post && path_is(req, "/invite/redeem") )
    {
        enum ToriRSServerInviteResult result;

        if( !body_field(req, "code", code, sizeof(code)) ||
            !body_field(req, "name", name, sizeof(name)) ||
            !body_field(req, "password", password, sizeof(password)) )
        {
            /* A code too long to be one is no invite, as the gate said
             * before: not a malformed request. */
            route_result(res, TORIRSSERVER_INVITE_UNKNOWN, NULL);
            return;
        }
        result = WebsiteUsersDb_InviteRedeem(code, name, password, &problem);
        memset(password, 0, sizeof(password));
        route_result(res, (int)result, problem);
        return;
    }
    refuse(res, 404, "no such route");
}
