/*
 * The gate's device-code grant (website_gate.h): what the RuneLite plugin does to
 * get a gate ticket without a password typed into it. Driven through
 * WebsiteGate_Handle with real requests, as the browser and the client send them:
 *
 *   client  POST /device/authorize            -> device_code, user_code
 *   client  POST /device/token (device_code)  -> authorization_pending
 *   browser GET  /device?code=, signed out    -> /login?next=/device?code=...
 *   browser GET  /login?next=...              -> the form carries next
 *   browser GET  /device?code=, signed in     -> the approval page
 *   browser POST /device/approve              -> approved
 *   client  POST /device/token (device_code)  -> a ticket the gate accepts
 *
 * and the ways it must refuse: a spent code, a denied one, an unknown one, a
 * `next` that leads anywhere but a device page.
 *
 * Run: make -C src test-website-gate-device
 */

#include "mem/torirs_mem.h"
#include "service/http_server.h"
#include "websiteserver/website_gate.h"
#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_oauth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    printf("website-gate-device: %-62s %s\n", what, condition ? "ok" : "FAILED");
    if( !condition )
        g_failures++;
}

static struct WebsiteGate g_gate;
static struct HttpResponse g_res;
static char g_body[16384];

/* One request; the answer in g_res, its body copied into g_body. */
static int
request(
    const char* method,
    const char* path,
    const char* cookie,
    const char* form)
{
    struct HttpRequest req;
    char headers[512] = "";
    int handled;

    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "%s", method);
    snprintf(req.path, sizeof(req.path), "%s", path);
    if( cookie )
        snprintf(headers, sizeof(headers), "Cookie: %s\r\n", cookie);
    strcat(headers, "Content-Type: application/x-www-form-urlencoded\r\n");
    req.headers = headers;
    req.headers_len = (int)strlen(headers);
    req.body = (const uint8_t*)(form ? form : "");
    req.body_len = form ? (int)strlen(form) : 0;
    if( g_res.owns_body )
        ToriMem_Free(ToriMem_Net(), g_res.body);
    memset(&g_res, 0, sizeof(g_res));
    handled = WebsiteGate_Handle(&g_gate, &req, &g_res);
    g_body[0] = '\0';
    if( g_res.body && g_res.body_len > 0 )
    {
        int n = g_res.body_len < (int)sizeof(g_body) - 1 ? g_res.body_len : (int)sizeof(g_body) - 1;

        memcpy(g_body, g_res.body, (size_t)n);
        g_body[n] = '\0';
    }
    return handled;
}

/* A string field of a flat JSON object, into `out`. */
static int
json_field(
    const char* json,
    const char* key,
    char* out,
    size_t cap)
{
    char pattern[64];
    const char* at;
    size_t n = 0;

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    at = strstr(json, pattern);
    if( !at )
        return 0;
    at += strlen(pattern);
    while( at[n] && at[n] != '"' && n + 1 < cap )
        n++;
    memcpy(out, at, n);
    out[n] = '\0';
    return 1;
}

int
main(void)
{
    char dir[] = "/tmp/io_gate_device_test_XXXXXX";
    char device_code[80];
    char user_code[16];
    char path[128];
    char form[256];
    char ticket[TORIRSSERVER_GATE_TICKET_MAX];
    char cookie[TORIRSSERVER_GATE_TICKET_MAX + 32];
    char issued[TORIRSSERVER_GATE_TICKET_MAX];
    char who[64];

    if( !mkdtemp(dir) )
    {
        perror("mkdtemp");
        return 1;
    }
    setenv("TORIRSSERVER_SAVES", dir, 1);
    WebsiteGate_Init(&g_gate, "/?args=x");

    /* Mint the browser's ticket the way /login does, for web login Dave (a
     * ticket names the login by its key, "dave"). */
    ToriRSServer_GateTicketMint("Dave", (long long)time(NULL) + 3600, ticket);
    snprintf(cookie, sizeof(cookie), TORIRSSERVER_GATE_COOKIE "=%s", ticket);

    request("POST", "/device/authorize", NULL, "device=RuneLite%20on%20test");
    check(g_res.status == 200, "authorize answers without a ticket");
    check(json_field(g_body, "device_code", device_code, sizeof(device_code)) &&
              strlen(device_code) == 64,
          "a 64-digit device code");
    check(json_field(g_body, "user_code", user_code, sizeof(user_code)) && strlen(user_code) == 9 &&
              user_code[4] == '-',
          "a BCDF-GHJK user code");
    check(strstr(g_body, "\"verification_path_complete\":\"/device?code=") != NULL,
          "and the page to open, as a path");

    snprintf(form, sizeof(form),
             "grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code&device_code=%s",
             device_code);
    request("POST", "/device/token", NULL, form);
    check(g_res.status == 400 && strstr(g_body, "authorization_pending"),
          "polling before approval: authorization_pending");
    request("POST", "/device/token", NULL, form);
    check(g_res.status == 400 && strstr(g_body, "slow_down"), "polling too fast: slow_down");

    snprintf(path, sizeof(path), "/device?code=%s", user_code);
    request("GET", path, NULL, NULL);
    check(g_res.status == 303 && strstr(g_res.location, "/login?next=%2Fdevice%3Fcode%3D") &&
              strstr(g_res.location, user_code),
          "the page, signed out: to the sign-in form with next");

    snprintf(path, sizeof(path), "/login?next=%%2Fdevice%%3Fcode%%3D%s", user_code);
    request("GET", path, NULL, NULL);
    check(g_res.status == 200 && strstr(g_body, "name=\"next\"") && strstr(g_body, user_code) &&
              strstr(g_body, "autocomplete=\"current-password\""),
          "the sign-in form carries next (and the password filler's hints)");
    request("GET", path, cookie, NULL);
    check(g_res.status == 303 && strncmp(g_res.location, "/device?code=", 13) == 0,
          "signed in already: straight on to the page");
    request("GET", "/login?next=https%3A%2F%2Fevil.example%2F", NULL, NULL);
    check(g_res.status == 200 && !strstr(g_body, "evil.example"), "a next elsewhere is dropped");
    request("GET", "/login?next=%2Fadmin", cookie, NULL);
    check(g_res.status == 303 && strcmp(g_res.location, "/") == 0,
          "and a next to any other page goes to the landing");

    snprintf(path, sizeof(path), "/device?code=%s", user_code);
    request("GET", path, cookie, NULL);
    check(g_res.status == 200 && strstr(g_body, "RuneLite on test") && strstr(g_body, "dave") &&
              strstr(g_body, "Approve"),
          "the page, signed in: who asks, as whom, Approve");

    snprintf(form, sizeof(form), "code=%s&decision=approve", user_code);
    request("POST", "/device/approve", NULL, form);
    check(g_res.status == 401, "approving needs the ticket");
    request("POST", "/device/approve", cookie, form);
    check(g_res.status == 200 && strstr(g_body, "Signed in"), "approved");

    snprintf(form, sizeof(form),
             "grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code&device_code=%s",
             device_code);
    request("POST", "/device/token", NULL, form);
    check(g_res.status == 200 && json_field(g_body, "ticket", issued, sizeof(issued)) &&
              strstr(g_body, "\"refresh_token\":\"") && strstr(g_body, "\"name\":\"dave\""),
          "the device's poll: a ticket and a refresh token for dave");
    check(ToriRSServer_GateTicketCheck(issued, strlen(issued), (long long)time(NULL), who, sizeof(who)) &&
              strcmp(who, "dave") == 0,
          "a ticket the gate accepts, for the web login's key");
    request("POST", "/device/token", NULL, form);
    check(g_res.status == 400 && strstr(g_body, "invalid_grant"), "the code is spent");

    /* Denied. */
    request("POST", "/device/authorize", NULL, "device=x");
    json_field(g_body, "device_code", device_code, sizeof(device_code));
    json_field(g_body, "user_code", user_code, sizeof(user_code));
    snprintf(form, sizeof(form), "code=%s&decision=deny", user_code);
    request("POST", "/device/approve", cookie, form);
    check(g_res.status == 200 && strstr(g_body, "Denied"), "denied");
    snprintf(form, sizeof(form),
             "grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code&device_code=%s",
             device_code);
    request("POST", "/device/token", NULL, form);
    check(g_res.status == 400 && strstr(g_body, "access_denied"), "the device hears access_denied");

    request("GET", "/device?code=BCDF-GHJK", cookie, NULL);
    check(g_res.status == 200 && strstr(g_body, "expired"), "an unknown code: no Approve button");
    check(!strstr(g_body, "Approve"), "  (none)");

    if( g_res.owns_body )
        ToriMem_Free(ToriMem_Net(), g_res.body);
    printf("website-gate-device: %s (%d failure%s)\n", g_failures ? "FAILED" : "ok", g_failures,
           g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
