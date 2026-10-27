/*
 * The sign-in gate. See io_gate.h.
 */
#include "io_gate.h"
#include "io_gate_links.h"
#include "io_site.h"

#include "mem/torirs_mem.h"
#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_oauth.h"
#include "torirsserver/torirs_server_savefile.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Five wrong passwords for one name in ten minutes, and that name waits out
 * the rest of the ten. PBKDF2 already makes each guess cost the server ~20 ms;
 * this makes a dictionary cost the attacker days. */
#define IO_GATE_THROTTLE_FAILURES 5
#define IO_GATE_THROTTLE_SECONDS (10 * 60)

#define IO_GATE_PAGE_MAX 8192
#define IO_GATE_FIELD_MAX 128

/* ------------------------------------------------------------------ */
/* Pages                                                               */
/* ------------------------------------------------------------------ */

static char const k_page[] =
    "<!doctype html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "<title>ToriRS</title>\n"
    "<style>\n"
    /* The account site's look (io_site.c), in miniature: a parchment card on
     * the dark field, gold title, the red button. */
    ":root { color-scheme: dark; }\n"
    "body { margin: 0; min-height: 100vh; display: grid; place-items: center;\n"
    "       font: 16px/1.45 Georgia, 'Times New Roman', serif; color: #2b1d0e;\n"
    "       background: #14100c radial-gradient(ellipse at top, #3a2a1a 0%%, #14100c 65%%) fixed; }\n"
    "main { width: min(340px, calc(100vw - 32px)); }\n"
    "h1 { font-size: 3rem; margin: 0 0 1rem; text-align: center; color: #f0c060;\n"
    "     letter-spacing: 2px; text-shadow: 3px 3px 0 #000, 0 0 24px rgba(240,192,96,.35); }\n"
    ".card { background: linear-gradient(#e9dfc4, #ddd0ae); border: 3px double #7a5a2e;\n"
    "        border-radius: 4px; padding: 20px; box-shadow: 0 4px 16px rgba(0,0,0,.6); }\n"
    "label { display: block; margin: 0 0 .75rem; font-size: .9rem; }\n"
    "input { display: block; box-sizing: border-box; width: 100%%; margin-top: .25rem;\n"
    "        padding: .5rem; font: inherit; border: 1px solid #7a5a2e; border-radius: 3px;\n"
    "        background: #fbf6e8; color: #2b1d0e; }\n"
    "input[readonly] { opacity: .7; }\n"
    "button { width: 100%%; padding: .6rem; font: inherit; font-weight: bold; color: #fff;\n"
    "         background: linear-gradient(#b8382a, #7a1c12); border: 1px solid #f0c060;\n"
    "         border-radius: 3px; cursor: pointer; text-shadow: 1px 1px 0 #000; }\n"
    ".error { color: #8e2318; margin: 0 0 1rem; }\n"
    ".note { color: #6b5537; font-size: .85rem; margin: 1rem 0 0; }\n"
    ".note a { color: #7a2a12; }\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<main>\n"
    "<h1>ToriRS</h1>\n"
    "<div class=\"card\">\n"
    "%s"
    "</div>\n"
    "</main>\n"
    "</body>\n"
    "</html>\n";

static char const k_rules[] =
    "<p class=\"note\">Passwords are 8 to 20 characters with at least one letter and one "
    "number, no spaces, and not containing your name. This login only opens the page; "
    "in the game you make or sign in to a separate player account.</p>\n";

/* `text` for an attribute or element body. Output is truncated, never
 * overrun; every caller's input is a bounded form field. */
static void
html_escape(
    char const* text,
    char* out,
    size_t cap)
{
    size_t used = 0;

    for( ; *text && used + 7 < cap; text++ )
    {
        char const* entity = NULL;

        switch( *text )
        {
        case '&':
            entity = "&amp;";
            break;
        case '<':
            entity = "&lt;";
            break;
        case '>':
            entity = "&gt;";
            break;
        case '"':
            entity = "&quot;";
            break;
        case '\'':
            entity = "&#39;";
            break;
        default:
            break;
        }
        if( entity )
        {
            memcpy(out + used, entity, strlen(entity));
            used += strlen(entity);
        }
        else
            out[used++] = *text;
    }
    out[used] = '\0';
}

static void
gate_page(
    struct HttpResponse* res,
    int status,
    char const* body)
{
    char* page = ToriMem_Alloc(ToriMem_Net(), IO_GATE_PAGE_MAX);
    int len;

    assert(page);
    len = snprintf(page, IO_GATE_PAGE_MAX, k_page, body);
    assert(len > 0 && len < IO_GATE_PAGE_MAX);
    res->status = status;
    snprintf(res->content_type, sizeof(res->content_type), "text/html; charset=utf-8");
    res->body = page;
    res->body_len = len;
    res->owns_body = 1;
}

static void
login_page(
    struct HttpResponse* res,
    int status,
    char const* error,
    char const* name)
{
    char body[IO_GATE_PAGE_MAX / 2];
    char error_html[256] = "";
    char name_html[IO_GATE_FIELD_MAX * 6];

    if( error )
        snprintf(error_html, sizeof(error_html), "<p class=\"error\">%s</p>\n", error);
    html_escape(name ? name : "", name_html, sizeof(name_html));
    snprintf(body, sizeof(body),
             "%s"
             "<form method=\"post\" action=\"/login\">\n"
             "<label>Name<input name=\"name\" autocomplete=\"username\" maxlength=\"12\" "
             "required autofocus value=\"%s\"></label>\n"
             "<label>Password<input name=\"password\" type=\"password\" "
             "autocomplete=\"current-password\" maxlength=\"20\" required></label>\n"
             "<button>Sign in</button>\n"
             "</form>\n"
             "<p class=\"note\">This page needs an invite link. Your player account "
             "is separate: you sign in to it in the game.</p>\n",
             error_html, name_html);
    gate_page(res, status, body);
}

static void
invite_page(
    struct HttpResponse* res,
    int status,
    char const* code,
    char const* error,
    char const* fixed_name,
    char const* name)
{
    char body[IO_GATE_PAGE_MAX / 2];
    char error_html[256] = "";
    char name_html[IO_GATE_FIELD_MAX * 6];

    if( error )
        snprintf(error_html, sizeof(error_html), "<p class=\"error\">%s</p>\n", error);
    html_escape(fixed_name[0] ? fixed_name : name, name_html, sizeof(name_html));
    snprintf(body, sizeof(body),
             "<p>Create your login.</p>\n"
             "%s"
             "<form method=\"post\" action=\"/invite/%s\">\n"
             "<label>Name<input name=\"name\" autocomplete=\"username\" maxlength=\"12\" "
             "required %s value=\"%s\"></label>\n"
             "<label>Password<input name=\"password\" type=\"password\" "
             "autocomplete=\"new-password\" minlength=\"8\" maxlength=\"20\" required></label>\n"
             "<label>Password again<input name=\"confirm\" type=\"password\" "
             "autocomplete=\"new-password\" minlength=\"8\" maxlength=\"20\" required></label>\n"
             "<button>Create login</button>\n"
             "</form>\n"
             "%s",
             error_html, code, fixed_name[0] ? "readonly" : "autofocus", name_html, k_rules);
    gate_page(res, status, body);
}

static void
spent_invite_page(struct HttpResponse* res)
{
    gate_page(res, 404,
              "<p>This invite has already been used, or the link is not complete.</p>\n"
              "<p class=\"note\"><a href=\"/login\">Sign in</a></p>\n");
}

/* ------------------------------------------------------------------ */
/* Requests                                                            */
/* ------------------------------------------------------------------ */

/* Does the path, up to its query, equal `want`? */
static int
path_is(
    char const* path,
    char const* want)
{
    size_t len = strlen(want);

    return strncmp(path, want, len) == 0 && (path[len] == '\0' || path[len] == '?');
}

static int
hex_value(char c)
{
    if( c >= '0' && c <= '9' )
        return c - '0';
    if( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    if( c >= 'A' && c <= 'F' )
        return c - 'A' + 10;
    return -1;
}

/*
 * One field of application/x-www-form-urlencoded text (a POST body or a query
 * string), decoded. 0 when it is absent, too long for `out`, or decodes to a
 * control byte -- none of which a form here can legitimately send.
 */
static int
urlencoded_field(
    char const* body,
    int len,
    char const* key,
    char* out,
    size_t cap)
{
    size_t key_len = strlen(key);
    int at = 0;

    if( !body )
        return 0;
    while( at < len )
    {
        int end = at;

        while( end < len && body[end] != '&' )
            end++;
        if( (size_t)(end - at) > key_len && memcmp(body + at, key, key_len) == 0 &&
            body[at + (int)key_len] == '=' )
        {
            size_t used = 0;

            for( int i = at + (int)key_len + 1; i < end; i++ )
            {
                int ch = (unsigned char)body[i];

                if( ch == '+' )
                    ch = ' ';
                else if( ch == '%' )
                {
                    int hi = i + 2 < end ? hex_value(body[i + 1]) : -1;
                    int lo = i + 2 < end ? hex_value(body[i + 2]) : -1;

                    if( hi < 0 || lo < 0 )
                        return 0;
                    ch = hi << 4 | lo;
                    i += 2;
                }
                if( ch < ' ' || used + 1 >= cap )
                    return 0;
                out[used++] = (char)ch;
            }
            out[used] = '\0';
            return 1;
        }
        at = end + 1;
    }
    return 0;
}

static int
form_field(
    struct HttpRequest const* req,
    char const* key,
    char* out,
    size_t cap)
{
    return urlencoded_field((char const*)req->body, req->body_len, key, out, cap);
}

int
IoGate_FormField(
    struct HttpRequest const* req,
    char const* key,
    char* out,
    size_t cap)
{
    assert(req);
    assert(key);
    assert(out);
    return form_field(req, key, out, cap);
}

int
IoGate_PathIs(
    char const* path,
    char const* want)
{
    assert(path);
    assert(want);
    return path_is(path, want);
}

static int
query_field(
    struct HttpRequest const* req,
    char const* key,
    char* out,
    size_t cap)
{
    char const* query = strchr(req->path, '?');

    if( !query )
        return 0;
    query++;
    return urlencoded_field(query, (int)strcspn(query, "#"), key, out, cap);
}

int
IoGate_QueryField(
    struct HttpRequest const* req,
    char const* key,
    char* out,
    size_t cap)
{
    assert(req);
    assert(key);
    assert(out);
    return query_field(req, key, out, cap);
}

static int
signed_in(
    struct HttpRequest const* req,
    char* out_name,
    size_t cap)
{
    int len = 0;
    char const* cookie = HttpRequest_Header(req, "Cookie", &len);

    return cookie && ToriRSServer_GateCookieTicket(cookie, (size_t)len, (long long)time(NULL),
                                                   out_name, cap);
}

/* Sign `display_name` in: the ticket as a cookie, and a 303 to the account
 * site's home, whose Play Now is the game. */
static void
issue_ticket(
    struct IoGate* gate,
    struct HttpResponse* res,
    char const* display_name)
{
    char ticket[TORIRSSERVER_GATE_TICKET_MAX];

    /* Always Secure: the gate is only ever served behind TLS (the proxy
     * terminates it), so the cookie must never travel in the clear. A test
     * over plain http sends it by hand. */
    ToriRSServer_GateTicketMint(display_name,
                                (long long)time(NULL) + TORIRSSERVER_GATE_TICKET_SECONDS, ticket);
    snprintf(res->set_cookie, sizeof(res->set_cookie),
             TORIRSSERVER_GATE_COOKIE "=%s; Path=/; Max-Age=%lld; HttpOnly; Secure; SameSite=Lax",
             ticket, TORIRSSERVER_GATE_TICKET_SECONDS);
    (void)gate;
    res->status = 303;
    snprintf(res->location, sizeof(res->location), "/");
}

/* The throttle slot for a name: its own, else the oldest to reuse. */
static struct IoGateThrottle*
throttle_slot(
    struct IoGate* gate,
    char const* name)
{
    struct IoGateThrottle* oldest = &gate->throttle[0];

    for( int i = 0; i < IO_GATE_THROTTLE_SLOTS; i++ )
    {
        struct IoGateThrottle* slot = &gate->throttle[i];

        if( strcmp(slot->name, name) == 0 )
            return slot;
        if( slot->window_start < oldest->window_start )
            oldest = slot;
    }
    snprintf(oldest->name, sizeof(oldest->name), "%s", name);
    oldest->window_start = 0;
    oldest->failures = 0;
    return oldest;
}

static void
handle_login(
    struct IoGate* gate,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char name[IO_GATE_FIELD_MAX];
    char password[IO_GATE_FIELD_MAX];
    char key[64];
    struct IoGateThrottle* throttle;
    long long now = (long long)time(NULL);

    if( !form_field(req, "name", name, sizeof(name)) ||
        !form_field(req, "password", password, sizeof(password)) )
    {
        login_page(res, 400, "Enter your name and password.", NULL);
        return;
    }
    ToriRSServer_SaveFileName(name, key, sizeof(key));
    if( !key[0] )
    {
        login_page(res, 401, "Wrong name or password.", name);
        return;
    }
    throttle = throttle_slot(gate, key);
    if( now - throttle->window_start >= IO_GATE_THROTTLE_SECONDS )
    {
        throttle->window_start = now;
        throttle->failures = 0;
    }
    if( throttle->failures >= IO_GATE_THROTTLE_FAILURES )
    {
        login_page(res, 429, "Too many attempts. Try again in a few minutes.", name);
        return;
    }
    if( ToriRSServer_GateLogin(name, password) != TORIRSSERVER_AUTH_OK )
    {
        memset(password, 0, sizeof(password));
        throttle->failures++;
        fprintf(stderr, "io_server: gate: sign-in refused for '%s' (%d in this window)\n", key,
                throttle->failures);
        login_page(res, 401, "Wrong name or password.", name);
        return;
    }
    memset(password, 0, sizeof(password));
    throttle->failures = 0;
    fprintf(stderr, "io_server: gate: '%s' signed in\n", key);
    issue_ticket(gate, res, name);
}

static void
handle_invite(
    struct IoGate* gate,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1];
    char fixed_name[IO_GATE_FIELD_MAX];
    char name[IO_GATE_FIELD_MAX] = "";
    char password[IO_GATE_FIELD_MAX];
    char confirm[IO_GATE_FIELD_MAX];
    char const* tail = req->path + strlen("/invite/");
    size_t code_len = strcspn(tail, "?#");
    char const* problem = NULL;
    enum ToriRSServerInviteResult result;

    if( code_len >= sizeof(code) )
    {
        spent_invite_page(res);
        return;
    }
    memcpy(code, tail, code_len);
    code[code_len] = '\0';
    if( !ToriRSServer_InviteLookup(code, fixed_name, sizeof(fixed_name)) )
    {
        spent_invite_page(res);
        return;
    }
    if( strcmp(req->method, "POST") != 0 )
    {
        invite_page(res, 200, code, NULL, fixed_name, "");
        return;
    }

    if( !form_field(req, "name", name, sizeof(name)) ||
        !form_field(req, "password", password, sizeof(password)) ||
        !form_field(req, "confirm", confirm, sizeof(confirm)) )
    {
        invite_page(res, 400, code, "Fill in every field.", fixed_name, name);
        return;
    }
    if( strcmp(password, confirm) != 0 )
    {
        invite_page(res, 400, code, "The two passwords are different.", fixed_name, name);
        return;
    }
    result = ToriRSServer_InviteRedeem(code, name, password, &problem);
    memset(password, 0, sizeof(password));
    memset(confirm, 0, sizeof(confirm));
    switch( result )
    {
    case TORIRSSERVER_INVITE_OK:
        fprintf(stderr, "io_server: gate: invite %.8s... made the web login '%s'\n", code, name);
        issue_ticket(gate, res, name);
        return;
    case TORIRSSERVER_INVITE_UNKNOWN:
        spent_invite_page(res);
        return;
    case TORIRSSERVER_INVITE_WRONG_NAME:
        invite_page(res, 400, code, "This invite is for another name.", fixed_name, name);
        return;
    case TORIRSSERVER_INVITE_NAME_TAKEN:
        invite_page(res, 400, code, "That name is taken.", fixed_name, name);
        return;
    case TORIRSSERVER_INVITE_BAD_NAME:
    case TORIRSSERVER_INVITE_WEAK:
        invite_page(res, 400, code, problem, fixed_name, name);
        return;
    case TORIRSSERVER_INVITE_WRITE_FAILED:
        invite_page(res, 500, code, "The login could not be saved. Try again.", fixed_name,
                    name);
        return;
    }
    assert(!"unhandled invite result");
}

/* ------------------------------------------------------------------ */
/* OAuth (torirs_server_oauth.h): only ever reached with a gate ticket  */
/* ------------------------------------------------------------------ */

static void
json_response(
    struct HttpResponse* res,
    int status,
    char const* body)
{
    size_t len = strlen(body);
    char* copy = ToriMem_Alloc(ToriMem_Net(), len + 1);

    assert(copy);
    memcpy(copy, body, len + 1);
    res->status = status;
    snprintf(res->content_type, sizeof(res->content_type), "application/json");
    res->body = copy;
    res->body_len = (int)len;
    res->owns_body = 1;
}

/* RFC 6749 5.2's error body. invalid_client is a 401, the rest 400. */
static void
oauth_error(
    struct HttpResponse* res,
    enum ToriRSServerOAuthError error,
    char const* description)
{
    char body[512];
    char escaped[256];
    size_t used = 0;

    for( char const* scan = description ? description : ""; *scan && used + 2 < sizeof(escaped);
         scan++ )
    {
        if( *scan == '"' || *scan == '\\' )
            escaped[used++] = '\\';
        escaped[used++] = *scan;
    }
    escaped[used] = '\0';
    snprintf(body, sizeof(body), "{\"error\":\"%s\"%s%s%s}", ToriRSServer_OAuthErrorName(error),
             used ? ",\"error_description\":\"" : "", escaped, used ? "\"" : "");
    json_response(res, error == TORIRSSERVER_OAUTH_INVALID_CLIENT ? 401 : 400, body);
}

static void
oauth_tokens(
    struct HttpResponse* res,
    struct ToriRSServerOAuthTokens const* tokens)
{
    char* body = ToriMem_Alloc(ToriMem_Net(), 3 * TORIRSSERVER_OAUTH_TOKEN_MAX);
    char name[160];
    size_t used = 0;
    int len;

    assert(body);
    for( char const* scan = tokens->name; *scan && used + 2 < sizeof(name); scan++ )
    {
        if( *scan == '"' || *scan == '\\' )
            name[used++] = '\\';
        name[used++] = *scan;
    }
    name[used] = '\0';
    len = snprintf(body, 3 * TORIRSSERVER_OAUTH_TOKEN_MAX,
                   "{\"access_token\":\"%s\",\"token_type\":\"Bearer\",\"expires_in\":%d,"
                   "\"refresh_token\":\"%s\",\"scope\":\"%s\",\"name\":\"%s\"}",
                   tokens->access_token, tokens->expires_in, tokens->refresh_token, tokens->scope,
                   name);
    assert(len > 0 && len < 3 * TORIRSSERVER_OAUTH_TOKEN_MAX);
    res->status = 200;
    snprintf(res->content_type, sizeof(res->content_type), "application/json");
    res->body = body;
    res->body_len = len;
    res->owns_body = 1;
}

/* One throttle for every place a player password is tried: the token
 * endpoint's password grant and the authorize page. Keyed apart from the
 * gate's own sign-ins, which are a different account. 1 when allowed. */
static int
player_throttle_allows(
    struct IoGate* gate,
    char const* username,
    struct IoGateThrottle** out)
{
    char key[80];
    char file[64];
    long long now = (long long)time(NULL);
    struct IoGateThrottle* throttle;

    ToriRSServer_SaveFileName(username, file, sizeof(file));
    snprintf(key, sizeof(key), "player:%s", file);
    throttle = throttle_slot(gate, key);
    if( now - throttle->window_start >= IO_GATE_THROTTLE_SECONDS )
    {
        throttle->window_start = now;
        throttle->failures = 0;
    }
    *out = throttle;
    return throttle->failures < IO_GATE_THROTTLE_FAILURES;
}

/* POST /oauth/token (RFC 6749 3.2): one public client, three grants. */
static void
handle_token(
    struct IoGate* gate,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    static struct ToriRSServerOAuthTokens tokens;
    char grant[48];
    char client[64];
    char scope[64] = "";
    char const* description = NULL;
    enum ToriRSServerOAuthError error;
    long long now = (long long)time(NULL);

    if( strcmp(req->method, "POST") != 0 )
    {
        res->status = 405;
        return;
    }
    if( !form_field(req, "grant_type", grant, sizeof(grant)) )
    {
        oauth_error(res, TORIRSSERVER_OAUTH_INVALID_REQUEST, "grant_type is required");
        return;
    }
    if( !form_field(req, "client_id", client, sizeof(client)) ||
        strcmp(client, TORIRSSERVER_OAUTH_CLIENT_ID) != 0 )
    {
        oauth_error(res, TORIRSSERVER_OAUTH_INVALID_CLIENT, "unknown client_id");
        return;
    }
    form_field(req, "scope", scope, sizeof(scope));

    if( strcmp(grant, "password") == 0 )
    {
        char username[IO_GATE_FIELD_MAX];
        char password[IO_GATE_FIELD_MAX];
        struct IoGateThrottle* throttle;

        if( !form_field(req, "username", username, sizeof(username)) ||
            !form_field(req, "password", password, sizeof(password)) )
        {
            oauth_error(res, TORIRSSERVER_OAUTH_INVALID_REQUEST,
                        "username and password are required");
            return;
        }
        if( !player_throttle_allows(gate, username, &throttle) )
        {
            memset(password, 0, sizeof(password));
            oauth_error(res, TORIRSSERVER_OAUTH_INVALID_GRANT,
                        "Too many attempts. Try again in a few minutes.");
            return;
        }
        error = ToriRSServer_OAuthPasswordGrant(username, password, scope, now, &tokens,
                                                &description);
        memset(password, 0, sizeof(password));
        if( error == TORIRSSERVER_OAUTH_INVALID_GRANT )
            throttle->failures++;
    }
    else if( strcmp(grant, "refresh_token") == 0 )
    {
        char refresh[TORIRSSERVER_OAUTH_TOKEN_MAX];

        if( !form_field(req, "refresh_token", refresh, sizeof(refresh)) )
        {
            oauth_error(res, TORIRSSERVER_OAUTH_INVALID_REQUEST, "refresh_token is required");
            return;
        }
        error = ToriRSServer_OAuthRefreshGrant(refresh, scope, now, &tokens, &description);
    }
    else if( strcmp(grant, "authorization_code") == 0 )
    {
        char code[64];
        char redirect[256];
        char verifier[160];

        if( !form_field(req, "code", code, sizeof(code)) ||
            !form_field(req, "redirect_uri", redirect, sizeof(redirect)) ||
            !form_field(req, "code_verifier", verifier, sizeof(verifier)) )
        {
            oauth_error(res, TORIRSSERVER_OAUTH_INVALID_REQUEST,
                        "code, redirect_uri and code_verifier are required");
            return;
        }
        error = ToriRSServer_OAuthCodeGrant(code, redirect, verifier, now, &tokens, &description);
    }
    else
    {
        oauth_error(res, TORIRSSERVER_OAUTH_UNSUPPORTED_GRANT_TYPE, NULL);
        return;
    }

    if( error != TORIRSSERVER_OAUTH_OK )
    {
        oauth_error(res, error, description);
        return;
    }
    fprintf(stderr, "io_server: oauth: %s grant for '%s'\n", grant, tokens.name);
    oauth_tokens(res, &tokens);
    memset(&tokens, 0, sizeof(tokens));
}

/* POST /oauth/revoke (RFC 7009): 200 whatever the token was. */
static void
handle_revoke(
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char token[TORIRSSERVER_OAUTH_TOKEN_MAX];

    if( strcmp(req->method, "POST") != 0 )
    {
        res->status = 405;
        return;
    }
    if( !form_field(req, "token", token, sizeof(token)) )
    {
        oauth_error(res, TORIRSSERVER_OAUTH_INVALID_REQUEST, "token is required");
        return;
    }
    ToriRSServer_OAuthRevoke(token, (long long)time(NULL));
    json_response(res, 200, "{}");
}

/* POST /game/play: the deob's play call. The access token comes as RFC 6750
 * allows, in the Authorization header or a form-encoded access_token. */
static void
handle_play(
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char token[TORIRSSERVER_OAUTH_TOKEN_MAX] = "";
    char otl[TORIRSSERVER_OAUTH_ID_HEX + 1];
    char name[64];
    char body[256];
    int header_len = 0;
    char const* header = HttpRequest_Header(req, "Authorization", &header_len);
    enum ToriRSServerOAuthError error;

    if( strcmp(req->method, "POST") != 0 )
    {
        res->status = 405;
        return;
    }
    if( header && header_len > 7 && strncmp(header, "Bearer ", 7) == 0 &&
        header_len - 7 < (int)sizeof(token) )
    {
        memcpy(token, header + 7, (size_t)(header_len - 7));
        token[header_len - 7] = '\0';
    }
    else
        form_field(req, "access_token", token, sizeof(token));
    error = ToriRSServer_OAuthPlay(token, (long long)time(NULL), otl, name, sizeof(name));
    if( error != TORIRSSERVER_OAUTH_OK )
    {
        snprintf(res->www_authenticate, sizeof(res->www_authenticate),
                 "Bearer error=\"%s\"", ToriRSServer_OAuthErrorName(error));
        json_response(res, 401, "{\"error\":\"invalid_token\"}");
        return;
    }
    fprintf(stderr, "io_server: oauth: one-time login for '%s'\n", name);
    snprintf(body, sizeof(body), "{\"token\":\"%s\",\"expires_in\":%d}", otl,
             TORIRSSERVER_OAUTH_OTL_SECONDS);
    json_response(res, 200, body);
}

/* GET /.well-known/oauth-authorization-server (RFC 8414). Absolute https
 * URLs on the Host the request came to: the gate is only served behind TLS. */
static void
handle_metadata(
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char host[128] = "localhost";
    char body[1400];
    int host_len = 0;
    char const* header = HttpRequest_Header(req, "Host", &host_len);

    if( header && host_len > 0 && host_len < (int)sizeof(host) )
    {
        memcpy(host, header, (size_t)host_len);
        host[host_len] = '\0';
        for( char* scan = host; *scan; scan++ )
            if( *scan == '"' || *scan == '\\' || (unsigned char)*scan < ' ' )
                *scan = '_';
    }
    snprintf(body, sizeof(body),
             "{\"issuer\":\"https://%s\","
             "\"authorization_endpoint\":\"https://%s/oauth/authorize\","
             "\"token_endpoint\":\"https://%s/oauth/token\","
             "\"revocation_endpoint\":\"https://%s/oauth/revoke\","
             "\"response_types_supported\":[\"code\"],"
             "\"grant_types_supported\":[\"authorization_code\",\"password\",\"refresh_token\"],"
             "\"code_challenge_methods_supported\":[\"S256\"],"
             "\"token_endpoint_auth_methods_supported\":[\"none\"],"
             "\"revocation_endpoint_auth_methods_supported\":[\"none\"],"
             "\"scopes_supported\":[\"" TORIRSSERVER_OAUTH_SCOPE "\"]}",
             host, host, host, host);
    json_response(res, 200, body);
}

/* The authorize page: a player-account login, carrying the request's
 * parameters through as hidden fields. Every value is escaped; none of them
 * has been trusted yet except as text. */
static void
authorize_page(
    struct HttpResponse* res,
    int status,
    char const* error,
    char const* redirect,
    char const* state,
    char const* challenge,
    char const* scope,
    char const* username)
{
    char body[IO_GATE_PAGE_MAX / 2];
    char error_html[256] = "";
    char redirect_html[256 * 6];
    char state_html[128 * 6];
    char challenge_html[64 * 6];
    char scope_html[64 * 6];
    char name_html[IO_GATE_FIELD_MAX * 6];

    if( error )
        snprintf(error_html, sizeof(error_html), "<p class=\"error\">%s</p>\n", error);
    html_escape(redirect, redirect_html, sizeof(redirect_html));
    html_escape(state, state_html, sizeof(state_html));
    html_escape(challenge, challenge_html, sizeof(challenge_html));
    html_escape(scope, scope_html, sizeof(scope_html));
    html_escape(username, name_html, sizeof(name_html));
    snprintf(body, sizeof(body),
             "<p>Sign in to your player account.</p>\n"
             "%s"
             "<form method=\"post\" action=\"/oauth/authorize\">\n"
             "<input type=\"hidden\" name=\"response_type\" value=\"code\">\n"
             "<input type=\"hidden\" name=\"client_id\" value=\"" TORIRSSERVER_OAUTH_CLIENT_ID "\">\n"
             "<input type=\"hidden\" name=\"redirect_uri\" value=\"%s\">\n"
             "<input type=\"hidden\" name=\"state\" value=\"%s\">\n"
             "<input type=\"hidden\" name=\"code_challenge\" value=\"%s\">\n"
             "<input type=\"hidden\" name=\"code_challenge_method\" value=\"S256\">\n"
             "<input type=\"hidden\" name=\"scope\" value=\"%s\">\n"
             "<label>Username<input name=\"username\" autocomplete=\"username\" required "
             "autofocus value=\"%s\"></label>\n"
             "<label>Password<input name=\"password\" type=\"password\" "
             "autocomplete=\"current-password\" maxlength=\"20\" required></label>\n"
             "<button>Sign in</button>\n"
             "</form>\n",
             error_html, redirect_html, state_html, challenge_html, scope_html, name_html);
    gate_page(res, status, body);
}

/* `text` percent-encoded for a URL query value. */
static void
url_encode(
    char const* text,
    char* out,
    size_t cap)
{
    static char const digits[] = "0123456789ABCDEF";
    size_t used = 0;

    for( ; *text && used + 4 < cap; text++ )
    {
        unsigned char ch = (unsigned char)*text;

        if( isalnum(ch) || ch == '-' || ch == '.' || ch == '_' || ch == '~' )
            out[used++] = (char)ch;
        else
        {
            out[used++] = '%';
            out[used++] = digits[ch >> 4];
            out[used++] = digits[ch & 15];
        }
    }
    out[used] = '\0';
}

/*
 * GET|POST /oauth/authorize (RFC 6749 4.1, RFC 7636): the authorization code
 * flow with PKCE, S256 only. Nothing is redirected anywhere until the
 * redirect_uri has been checked, so a bad request is a page, never a bounce
 * to wherever it asked.
 */
static void
handle_authorize(
    struct IoGate* gate,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    int is_post = strcmp(req->method, "POST") == 0;
    char response_type[16] = "";
    char client[64] = "";
    char redirect[256] = "";
    char state[128] = "";
    char challenge[64] = "";
    char method[16] = "";
    char scope[64] = "";
    char username[IO_GATE_FIELD_MAX] = "";
    char password[IO_GATE_FIELD_MAX] = "";
    char code[TORIRSSERVER_OAUTH_ID_HEX + 1];
    char state_query[128 * 3 + 1];
    char const* description = NULL;
    struct IoGateThrottle* throttle;
    enum ToriRSServerOAuthError error;
    int (*field)(struct HttpRequest const*, char const*, char*, size_t) =
        is_post ? form_field : query_field;

    if( !is_post && strcmp(req->method, "GET") != 0 && strcmp(req->method, "HEAD") != 0 )
    {
        res->status = 405;
        return;
    }
    field(req, "response_type", response_type, sizeof(response_type));
    field(req, "client_id", client, sizeof(client));
    field(req, "redirect_uri", redirect, sizeof(redirect));
    field(req, "state", state, sizeof(state));
    field(req, "code_challenge", challenge, sizeof(challenge));
    field(req, "code_challenge_method", method, sizeof(method));
    field(req, "scope", scope, sizeof(scope));
    if( strcmp(client, TORIRSSERVER_OAUTH_CLIENT_ID) != 0 ||
        !ToriRSServer_OAuthRedirectAllowed(redirect) )
    {
        gate_page(res, 400, "<p class=\"error\">This sign-in link is not valid: unknown "
                            "client or redirect.</p>\n");
        return;
    }
    if( strcmp(response_type, "code") != 0 || strcmp(method, "S256") != 0 ||
        strlen(challenge) != 43 )
    {
        gate_page(res, 400, "<p class=\"error\">This sign-in link is not valid: it needs "
                            "response_type=code and an S256 code_challenge.</p>\n");
        return;
    }
    if( !is_post )
    {
        authorize_page(res, 200, NULL, redirect, state, challenge, scope, "");
        return;
    }

    if( !form_field(req, "username", username, sizeof(username)) ||
        !form_field(req, "password", password, sizeof(password)) )
    {
        authorize_page(res, 400, "Enter your username and password.", redirect, state, challenge,
                       scope, username);
        return;
    }
    if( !player_throttle_allows(gate, username, &throttle) )
    {
        memset(password, 0, sizeof(password));
        authorize_page(res, 429, "Too many attempts. Try again in a few minutes.", redirect,
                       state, challenge, scope, username);
        return;
    }
    error = ToriRSServer_OAuthAuthorize(username, password, redirect, challenge, scope,
                                        (long long)time(NULL), code, &description);
    memset(password, 0, sizeof(password));
    if( error == TORIRSSERVER_OAUTH_INVALID_GRANT )
        throttle->failures++;
    if( error != TORIRSSERVER_OAUTH_OK )
    {
        authorize_page(res, 400, description ? description : "Sign-in failed.", redirect, state,
                       challenge, scope, username);
        return;
    }
    url_encode(state, state_query, sizeof(state_query));
    res->status = 303;
    snprintf(res->location, sizeof(res->location), "%s%scode=%s%s%s", redirect,
             strchr(redirect, '?') ? "&" : "?", code, state[0] ? "&state=" : "", state_query);
}

/* ------------------------------------------------------------------ */
/* Player accounts linked to the web login (torirs_server_auth.h)      */
/* ------------------------------------------------------------------ */

/* `text` as a JSON string body onto `out` (quotes not included). Names reach
 * here already free of control characters but for the '\n' list separator. */
static void
json_append(
    char* out,
    size_t cap,
    char const* text)
{
    size_t used = strlen(out);

    for( ; *text && used + 3 < cap; text++ )
    {
        if( *text == '"' || *text == '\\' )
            out[used++] = '\\';
        if( *text == '\n' )
        {
            out[used++] = '\\';
            out[used++] = 'n';
            continue;
        }
        out[used++] = *text;
    }
    out[used] = '\0';
}

/* A player name from a form: what OAuth's check_account accepts. */
static int
player_name_ok(char const* name)
{
    if( !name[0] || strlen(name) >= IO_GATE_LINKS_NAME_MAX )
        return 0;
    for( char const* scan = name; *scan; scan++ )
        if( (unsigned char)*scan < ' ' )
            return 0;
    return 1;
}

/* A one-time login for the linked `display_name`, as the play call answers. */
static void
account_play_answer(
    struct IoGate* gate,
    struct HttpResponse* res,
    char const* who,
    char const* display_name)
{
    char token[TORIRSSERVER_LINK_TOKEN_HEX + 1];
    char otl[TORIRSSERVER_OAUTH_ID_HEX + 1];
    char name[64];
    char body[256];
    enum ToriRSServerOAuthError error = TORIRSSERVER_OAUTH_INVALID_GRANT;

    if( IoGateLinks_Token(gate->links, who, display_name, token) )
        error = ToriRSServer_OAuthUserPlay(who, display_name, token, (long long)time(NULL), otl,
                                           name, sizeof(name));
    memset(token, 0, sizeof(token));
    if( error != TORIRSSERVER_OAUTH_OK )
    {
        oauth_error(res, error, "That account is not linked to this login.");
        return;
    }
    fprintf(stderr, "io_server: gate: '%s' plays linked account '%s'\n", who, name);
    snprintf(body, sizeof(body), "{\"token\":\"%s\",\"expires_in\":%d,\"name\":\"", otl,
             TORIRSSERVER_OAUTH_OTL_SECONDS);
    json_append(body, sizeof(body) - 3, name);
    strcat(body, "\"}");
    json_response(res, 200, body);
}

/* POST /account/players: {"user": <web login>, "players": "A\nB"}, newline
 * separated because a name cannot hold one and the client reads flat JSON. */
static void
handle_account_players(
    struct IoGate* gate,
    struct HttpResponse* res,
    char const* who)
{
    char names[IO_GATE_LINKS_LIST_MAX][IO_GATE_LINKS_NAME_MAX];
    char body[2048];
    int count = IoGateLinks_List(gate->links, who, names, IO_GATE_LINKS_LIST_MAX);

    snprintf(body, sizeof(body), "{\"user\":\"");
    json_append(body, sizeof(body) - 32, who);
    strcat(body, "\",\"players\":\"");
    for( int i = 0; i < count; i++ )
    {
        if( i )
            json_append(body, sizeof(body) - 3, "\n");
        json_append(body, sizeof(body) - 3, names[i]);
    }
    strcat(body, "\"}");
    json_response(res, 200, body);
}

/* POST /account/link: the player's own name and password prove it is theirs
 * to give; the record takes this login as its one owner and the token's hash,
 * the links database the token, every refresh token the account had is
 * revoked, and the answer is a one-time login so linking also plays. */
enum IoGateLinkResult
IoGate_LinkPlayer(
    struct IoGate* gate,
    char const* who,
    char const* username,
    char* password,
    size_t password_cap,
    char const** out_message)
{
    char token[TORIRSSERVER_LINK_TOKEN_HEX + 1];
    struct IoGateThrottle* throttle;
    enum ToriRSServerLinkResult result;
    long long now = (long long)time(NULL);

    assert(gate);
    assert(who);
    assert(username);
    assert(password);
    assert(out_message);
    *out_message = NULL;
    if( !player_name_ok(username) || !password[0] )
    {
        memset(password, 0, password_cap);
        *out_message = "Enter the player account's name and password.";
        return IO_GATE_LINK_REFUSED;
    }
    if( !player_throttle_allows(gate, username, &throttle) )
    {
        memset(password, 0, password_cap);
        *out_message = "Too many attempts. Try again in a few minutes.";
        return IO_GATE_LINK_REFUSED;
    }
    result = ToriRSServer_AuthLink(username, password, who, token);
    if( result == TORIRSSERVER_LINK_WEAK )
        *out_message = ToriRSServer_AuthPasswordProblem(username, password);
    memset(password, 0, password_cap);
    if( result == TORIRSSERVER_LINK_WEAK )
        return IO_GATE_LINK_REFUSED;
    if( result == TORIRSSERVER_LINK_REFUSED )
    {
        throttle->failures++;
        *out_message = "Invalid username or password.";
        return IO_GATE_LINK_REFUSED;
    }
    if( result == TORIRSSERVER_LINK_OTHER_USER )
    {
        fprintf(stderr, "io_server: gate: '%s' tried to link '%s', which another login owns\n",
                who, username);
        *out_message = "That account is already linked to another login.";
        return IO_GATE_LINK_REFUSED;
    }
    if( result != TORIRSSERVER_LINK_OK )
    {
        *out_message = "The link could not be saved.";
        return IO_GATE_LINK_FAILED;
    }
    IoGateLinks_Put(gate->links, who, username, token, now);
    memset(token, 0, sizeof(token));
    ToriRSServer_OAuthRevokeAccount(username, now);
    fprintf(stderr, "io_server: gate: '%s' linked player account '%s'\n", who, username);
    return IO_GATE_LINK_LINKED;
}

int
IoGate_UnlinkPlayer(
    struct IoGate* gate,
    char const* who,
    char const* player)
{
    char owner[64];

    assert(gate);
    assert(who);
    assert(player);
    if( !player_name_ok(player) ||
        ToriRSServer_AuthManagedBy(player, owner, sizeof(owner)) != 1 || strcmp(owner, who) != 0 )
        return 0;
    if( !ToriRSServer_AuthUnlink(player) )
        return 0;
    IoGateLinks_Drop(gate->links, who, player);
    fprintf(stderr, "io_server: gate: '%s' unlinked player account '%s'\n", who, player);
    return 1;
}

static void
handle_account_link(
    struct IoGate* gate,
    struct HttpRequest const* req,
    struct HttpResponse* res,
    char const* who)
{
    char username[IO_GATE_FIELD_MAX] = "";
    char password[IO_GATE_FIELD_MAX] = "";
    char const* message = NULL;
    enum IoGateLinkResult result;

    if( !form_field(req, "username", username, sizeof(username)) ||
        !form_field(req, "password", password, sizeof(password)) )
    {
        memset(password, 0, sizeof(password));
        oauth_error(res, TORIRSSERVER_OAUTH_INVALID_REQUEST, "username and password are required");
        return;
    }
    result = IoGate_LinkPlayer(gate, who, username, password, sizeof(password), &message);
    if( result != IO_GATE_LINK_LINKED )
    {
        oauth_error(res,
                    result == IO_GATE_LINK_FAILED ? TORIRSSERVER_OAUTH_SERVER_ERROR
                                                  : TORIRSSERVER_OAUTH_INVALID_GRANT,
                    message);
        return;
    }
    account_play_answer(gate, res, who, username);
}

/* POST /account/play: a linked account's one-time login, on this session. */
static void
handle_account_play(
    struct IoGate* gate,
    struct HttpRequest const* req,
    struct HttpResponse* res,
    char const* who)
{
    char player[IO_GATE_FIELD_MAX];

    if( !form_field(req, "player", player, sizeof(player)) || !player_name_ok(player) )
    {
        oauth_error(res, TORIRSSERVER_OAUTH_INVALID_REQUEST, "player is required");
        return;
    }
    account_play_answer(gate, res, who, player);
}

/* 1 when `req` was an /account route and `res` is its answer. POST only: each
 * either changes the account or mints a login. */
static int
handle_account(
    struct IoGate* gate,
    struct HttpRequest const* req,
    struct HttpResponse* res,
    char const* who)
{
    int is_post = strcmp(req->method, "POST") == 0;

    if( path_is(req->path, "/account/players") )
    {
        if( is_post )
            handle_account_players(gate, res, who);
        else
            res->status = 405;
    }
    else if( path_is(req->path, "/account/link") )
    {
        if( is_post )
            handle_account_link(gate, req, res, who);
        else
            res->status = 405;
    }
    else if( path_is(req->path, "/account/play") )
    {
        if( is_post )
            handle_account_play(gate, req, res, who);
        else
            res->status = 405;
    }
    else
        return 0;
    return 1;
}

/* 1 when `req` was an OAuth route and `res` is its answer. */
static int
handle_oauth(
    struct IoGate* gate,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    if( path_is(req->path, "/oauth/token") )
        handle_token(gate, req, res);
    else if( path_is(req->path, "/oauth/authorize") )
        handle_authorize(gate, req, res);
    else if( path_is(req->path, "/oauth/revoke") )
        handle_revoke(req, res);
    else if( path_is(req->path, "/game/play") )
        handle_play(req, res);
    else if( path_is(req->path, "/.well-known/oauth-authorization-server") )
        handle_metadata(req, res);
    else
        return 0;
    return 1;
}

void
IoGate_Init(
    struct IoGate* gate,
    char const* landing)
{
    assert(gate);
    assert(landing);
    memset(gate, 0, sizeof(*gate));
    snprintf(gate->landing, sizeof(gate->landing), "%s", landing);
    ToriRSServer_GateKeyLoad();
    ToriRSServer_OAuthKeyLoad();
    {
        char path[1024];

        snprintf(path, sizeof(path), "%s/gate/links.sqlite", ToriRSServer_SaveDir());
        gate->links = IoGateLinks_Open(path);
    }
}

int
IoGate_Handle(
    struct IoGate* gate,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char who[64];
    int is_get;
    int is_post;

    assert(gate);
    assert(req);
    assert(res);
    is_get = strcmp(req->method, "GET") == 0 || strcmp(req->method, "HEAD") == 0;
    is_post = strcmp(req->method, "POST") == 0;

    if( path_is(req->path, "/login") )
    {
        if( is_post )
            handle_login(gate, req, res);
        else if( is_get && signed_in(req, who, sizeof(who)) )
        {
            res->status = 303;
            snprintf(res->location, sizeof(res->location), "/");
        }
        else if( is_get )
            login_page(res, 200, NULL, NULL);
        else
            res->status = 405;
        return 1;
    }
    if( strncmp(req->path, "/invite/", 8) == 0 )
    {
        if( is_get || is_post )
            handle_invite(gate, req, res);
        else
            res->status = 405;
        return 1;
    }
    if( path_is(req->path, "/logout") )
    {
        snprintf(res->set_cookie, sizeof(res->set_cookie),
                 TORIRSSERVER_GATE_COOKIE "=; Path=/; Max-Age=0; HttpOnly; Secure; SameSite=Lax");
        res->status = 303;
        snprintf(res->location, sizeof(res->location), "/login");
        return 1;
    }

    if( !signed_in(req, who, sizeof(who)) )
    {
        if( is_get )
        {
            res->status = 303;
            snprintf(res->location, sizeof(res->location), "/login");
        }
        else
            res->status = 401;
        return 1;
    }
    /* Player-account OAuth, after the gate: a token is only ever minted for a
     * browser (or client) that already holds the gate's ticket. */
    if( handle_oauth(gate, req, res) )
        return 1;
    /* The web login's own player accounts: `who` is the ticket's login, so a
     * session only ever reaches the accounts linked to it. */
    if( handle_account(gate, req, res, who) )
        return 1;
    /* The account site: the bare "/" and its pages. The game is the landing
     * URL ("/?args=..."), which is not the bare path and falls through to the
     * static files. */
    if( IoSite_Handle(gate, who, req, res) )
        return 1;
    return 0;
}
