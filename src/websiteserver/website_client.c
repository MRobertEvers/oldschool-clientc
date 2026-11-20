/*
 * Asking the website server for its user accounts. See website_client.h.
 */
#include "website_client.h"

#include "service/service_form.h"
#include "service/service_link.h"
#include "website_users.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A page waits this long for an answer; the server is on this machine. */
#define CLIENT_CONNECT_SEC 2
#define CLIENT_READ_SEC 5

/* The most lines one answer is read as: a player view's chart and sessions. */

static char g_problem[256];
static struct ServiceLink g_link = { "website", "TORIRS_WEBSITE_SERVER", WEBSITE_SERVER_PORT, "/svc/users", NULL, NULL };

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
    n = ServiceForm_Fields(text, f, 3);
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
        fprintf(stderr, "website client: %s answered %d: %s\n", route, status, f[0]);
    ToriMem_Free(ToriMem_Net(), text);
    return result;
}

enum ToriRSServerAuthResult
WebsiteClient_GateLogin(
    const char* name,
    const char* password)
{
    char body[512] = "";
    int result;

    assert(name);
    assert(password);
    field_into(body, sizeof(body), "name", name);
    field_into(body, sizeof(body), "password", password);
    result = ask_result("/login", body, NULL);
    memset(body, 0, sizeof(body));
    return result < 0 ? TORIRSSERVER_AUTH_UNAVAILABLE : (enum ToriRSServerAuthResult)result;
}

enum ToriRSServerCreateResult
WebsiteClient_GateLoginCreate(
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
    result = ask_result("/create", body, out_problem);
    memset(body, 0, sizeof(body));
    return result < 0 ? TORIRSSERVER_CREATE_UNAVAILABLE : (enum ToriRSServerCreateResult)result;
}

int
WebsiteClient_GateLoginRevoke(const char* name)
{
    char body[256] = "";

    assert(name);
    field_into(body, sizeof(body), "name", name);
    return ask_result("/revoke", body, NULL);
}

int
WebsiteClient_GateLoginEach(void (*each)(const char* name))
{
    static char* lines[1024];
    int status;
    char* text;
    int count;

    assert(each);
    text = ask("/list", NULL, &status);
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
WebsiteClient_InviteCreate(
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
        fprintf(stderr, "website client: /invite/create answered %d: %s\n", status, text);
    ToriMem_Free(ToriMem_Net(), text);
    return ok ? 1 : -1;
}

int
WebsiteClient_InviteLookup(
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
    if( ServiceForm_Fields(text, f, 3) == 3 && strcmp(f[0], "invite") == 0 )
    {
        found = atoi(f[1]) != 0;
        snprintf(out_name, cap, "%s", found ? f[2] : "");
    }
    ToriMem_Free(ToriMem_Net(), text);
    return found;
}

enum ToriRSServerInviteResult
WebsiteClient_InviteRedeem(
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

void
WebsiteClient_SetLocal(HttpHandler handler)
{
    ServiceLink_SetLocal(&g_link, handler, NULL);
}
