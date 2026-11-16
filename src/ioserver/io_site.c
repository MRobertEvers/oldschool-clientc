/*
 * The account site. See io_site.h.
 */
#include "io_site.h"

#include "io_gate.h"
#include "io_gate_links.h"
#include "mem/torirs_mem.h"
#include "accountserver/account_client.h"
#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_savefile.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The skills the site shows, in the hiscores' order -- which is the stat ids'
 * own, 0..23. Stat 24 (Summoning) is a feature-flagged port and not listed. */
#define SITE_SKILLS ACCOUNT_SKILLS
#define SITE_HITPOINTS 3
#define SITE_NAME_MAX ACCOUNT_NAME_MAX
#define SITE_TOP_ON_HOME 5
#define SITE_HISCORES_PAGE 50

static char const* const k_skill_names[SITE_SKILLS] = {
    "Attack",   "Defence",    "Strength",    "Hitpoints",  "Ranged",    "Prayer",
    "Magic",    "Cooking",    "Woodcutting", "Fletching",  "Fishing",   "Firemaking",
    "Crafting", "Smithing",   "Mining",      "Herblore",   "Agility",   "Thieving",
    "Slayer",   "Farming",    "Runecraft",   "Hunter",     "Construction", "Sailing",
};

/* ------------------------------------------------------------------ */
/* The page buffer                                                     */
/* ------------------------------------------------------------------ */

struct SiteBuf
{
    char* data;
    size_t len;
    size_t cap;
};

static void
buf_init(struct SiteBuf* b)
{
    b->cap = 16384;
    b->len = 0;
    b->data = ToriMem_Alloc(ToriMem_Net(), b->cap);
    assert(b->data);
    b->data[0] = '\0';
}

static void
buf_reserve(
    struct SiteBuf* b,
    size_t more)
{
    if( b->len + more + 1 <= b->cap )
        return;
    while( b->len + more + 1 > b->cap )
        b->cap *= 2;
    b->data = ToriMem_Realloc(ToriMem_Net(), b->data, b->cap);
    assert(b->data);
}

static void
buf_puts(
    struct SiteBuf* b,
    char const* text)
{
    size_t len = strlen(text);

    buf_reserve(b, len);
    memcpy(b->data + b->len, text, len + 1);
    b->len += len;
}

static void
buf_printf(
    struct SiteBuf* b,
    char const* format,
    ...) __attribute__((format(printf, 2, 3)));

static void
buf_printf(
    struct SiteBuf* b,
    char const* format,
    ...)
{
    va_list args;
    int need;

    va_start(args, format);
    need = vsnprintf(NULL, 0, format, args);
    va_end(args);
    assert(need >= 0);
    buf_reserve(b, (size_t)need);
    va_start(args, format);
    vsnprintf(b->data + b->len, (size_t)need + 1, format, args);
    va_end(args);
    b->len += (size_t)need;
}

/* `text` for an element body or a quoted attribute. */
static void
buf_esc(
    struct SiteBuf* b,
    char const* text)
{
    for( ; *text; text++ )
    {
        if( *text == '&' )
            buf_puts(b, "&amp;");
        else if( *text == '<' )
            buf_puts(b, "&lt;");
        else if( *text == '>' )
            buf_puts(b, "&gt;");
        else if( *text == '"' )
            buf_puts(b, "&quot;");
        else if( *text == '\'' )
            buf_puts(b, "&#39;");
        else
        {
            char one[2] = { *text, '\0' };

            buf_puts(b, one);
        }
    }
}

/* `text` percent-encoded for a query value. */
static void
buf_url(
    struct SiteBuf* b,
    char const* text)
{
    for( ; *text; text++ )
    {
        unsigned char ch = (unsigned char)*text;

        if( isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' )
        {
            char one[2] = { (char)ch, '\0' };

            buf_puts(b, one);
        }
        else
            buf_printf(b, "%%%02X", ch);
    }
}

/* 1234567 -> "1,234,567". */
static void
buf_num(
    struct SiteBuf* b,
    long long value)
{
    char digits[32];
    int len;

    if( value < 0 )
    {
        buf_puts(b, "-");
        value = -value;
    }
    len = snprintf(digits, sizeof(digits), "%lld", value);
    for( int i = 0; i < len; i++ )
    {
        char one[2] = { digits[i], '\0' };

        if( i > 0 && (len - i) % 3 == 0 )
            buf_puts(b, ",");
        buf_puts(b, one);
    }
}

/* "26 Oct 2026": a day of the history (unix time / 86400), in UTC as the
 * days are counted. */
static void
buf_day(
    struct SiteBuf* b,
    long long day)
{
    time_t t = (time_t)(day * 86400);
    struct tm utc;
    char text[32];

    gmtime_r(&t, &utc);
    strftime(text, sizeof(text), "%d %b %Y", &utc);
    buf_puts(b, text);
}

/* "26 Oct 18:53": the sessions table's, where the year is noise. */
static void
buf_when(
    struct SiteBuf* b,
    long long when)
{
    time_t t = (time_t)when;
    struct tm local;
    char text[32];

    localtime_r(&t, &local);
    strftime(text, sizeof(text), "%d %b %H:%M", &local);
    buf_puts(b, text);
}

static void
buf_ago(
    struct SiteBuf* b,
    long long when,
    long long now)
{
    long long seconds = now - when;

    if( seconds < 60 )
        buf_puts(b, "just now");
    else if( seconds < 3600 )
        buf_printf(b, "%lld min ago", seconds / 60);
    else if( seconds < 86400 )
        buf_printf(b, "%lld hr ago", seconds / 3600);
    else
        buf_printf(b, "%lld day%s ago", seconds / 86400, seconds / 86400 == 1 ? "" : "s");
}

static void
buf_duration(
    struct SiteBuf* b,
    long long seconds)
{
    if( seconds < 60 )
        buf_printf(b, "%llds", seconds);
    else if( seconds < 3600 )
        buf_printf(b, "%lldm", seconds / 60);
    else
        buf_printf(b, "%lldh %02lldm", seconds / 3600, seconds / 60 % 60);
}

/* ------------------------------------------------------------------ */
/* The frame every page shares                                         */
/* ------------------------------------------------------------------ */

static char const k_css[] =
    ":root{color-scheme:dark;--ink:#2b1d0e;--parch:#e9dfc4;--parch2:#ddd0ae;--edge:#7a5a2e;"
    "--gold:#f0c060;--gold2:#c8963e;--wood:#3b2b1b;--red:#8e2318;--muted:#6b5537}"
    "*{box-sizing:border-box}"
    "body{margin:0;min-height:100vh;display:flex;flex-direction:column;color:var(--parch);"
    "font:16px/1.5 Georgia,'Times New Roman',serif;"
    "background:#14100c radial-gradient(ellipse at top,#3a2a1a 0%,#14100c 65%) fixed}"
    "a{color:inherit}"
    ".top{background:linear-gradient(#3b2b1b,#21170e);border-bottom:2px solid var(--gold2);"
    "box-shadow:0 2px 0 #000}"
    ".bar{max-width:1000px;margin:0 auto;padding:10px 16px;display:flex;flex-wrap:wrap;"
    "align-items:center;gap:8px 24px}"
    ".brand{font-size:28px;font-weight:bold;color:var(--gold);text-decoration:none;"
    "letter-spacing:1px;text-shadow:2px 2px 0 #000}"
    "nav{display:flex;flex-wrap:wrap;gap:4px;flex:1}"
    "nav a{padding:6px 12px;text-decoration:none;font-variant:small-caps;font-size:18px;"
    "letter-spacing:.5px;border-radius:3px}"
    "nav a:hover,nav a.on{background:rgba(240,192,96,.15);color:var(--gold)}"
    ".who{font-size:14px;color:#cdbf9e;display:flex;flex-wrap:wrap;align-items:center;gap:6px 12px}"
    ".who b{color:var(--gold)}"
    ".mini{padding:6px 14px;font:inherit;font-weight:bold;font-size:15px;color:#fff;"
    "text-decoration:none;background:linear-gradient(#b8382a,#7a1c12);border:1px solid var(--gold);"
    "border-radius:3px;text-shadow:1px 1px 0 #000}"
    ".linkbtn{background:none;border:0;padding:0;color:var(--gold);font:inherit;cursor:pointer;"
    "text-decoration:underline}"
    "main{flex:1;width:100%;max-width:1000px;margin:0 auto;padding:24px 16px 48px}"
    ".hero{text-align:center;padding:48px 16px 40px;margin:0 0 24px;border:2px solid var(--edge);"
    "border-radius:6px;box-shadow:inset 0 0 60px #000;"
    "background:linear-gradient(rgba(0,0,0,.2),rgba(0,0,0,.6)),"
    "repeating-linear-gradient(45deg,#2c2016 0 14px,#271c13 14px 28px)}"
    ".hero h1{font-size:clamp(44px,9vw,76px);line-height:1.1;margin:0;color:var(--gold);"
    "letter-spacing:3px;text-shadow:3px 3px 0 #000,0 0 28px rgba(240,192,96,.35)}"
    ".hero p{margin:10px 0 28px;color:#d8c9a6;font-style:italic;font-size:18px}"
    ".play{display:inline-block;padding:14px 44px;font-size:24px;font-weight:bold;color:#fff;"
    "text-decoration:none;letter-spacing:1px;text-shadow:1px 1px 0 #000;border-radius:4px;"
    "background:linear-gradient(#c23e2e,#7a1c12);border:2px solid var(--gold);"
    "box-shadow:0 3px 0 #3d0d08,0 0 24px rgba(194,62,46,.45)}"
    ".play:hover,.mini:hover{background:linear-gradient(#d24a38,#8e2318)}"
    ".title h1{margin:0 0 20px;color:var(--gold);font-size:34px;text-shadow:2px 2px 0 #000}"
    ".title h1 small{font-size:16px;color:#cdbf9e;font-weight:normal;margin-left:10px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,320px),1fr));gap:24px}"
    ".stack>*+*{margin-top:24px}"
    ".panel{background:linear-gradient(var(--parch),var(--parch2));color:var(--ink);"
    "border:3px double var(--edge);border-radius:4px;padding:20px;"
    "box-shadow:0 4px 16px rgba(0,0,0,.6);min-width:0}"
    ".panel h2{margin:0 0 12px;font-variant:small-caps;color:#5a1e0e;font-size:22px;"
    "border-bottom:1px solid #b89c6a;padding-bottom:6px}"
    ".panel a{color:#7a2a12}"
    ".panel p{margin:0 0 12px}"
    "table{width:100%;border-collapse:collapse;font-size:15px}"
    "th{background:var(--wood);color:var(--gold);text-align:left;padding:6px 10px;"
    "font-variant:small-caps;font-weight:normal;white-space:nowrap}"
    "td{padding:6px 10px;border-bottom:1px solid #c9b88f}"
    "tbody tr:nth-child(even) td{background:rgba(122,90,46,.08)}"
    ".num{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}"
    ".when{white-space:nowrap}"
    ".panel h2:not(:first-child){margin-top:28px}"
    "tr.me td{background:rgba(240,192,96,.35)!important;font-weight:bold}"
    ".scroll{overflow-x:auto}"
    ".tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:12px;"
    "margin:0 0 20px}"
    ".tile{background:var(--wood);color:var(--parch);border:1px solid var(--gold2);border-radius:4px;"
    "padding:10px 12px}"
    ".tile b{display:block;font-size:24px;color:var(--gold);line-height:1.2}"
    ".tile span{font-size:13px;font-variant:small-caps;color:#cdbf9e}"
    ".cards{display:grid;gap:10px}"
    ".card{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;gap:8px 16px;"
    "padding:10px 12px;border:1px solid #b89c6a;border-radius:4px;background:rgba(255,255,255,.3)}"
    ".card .name{font-size:19px;font-weight:bold}"
    ".card .meta{font-size:14px;color:var(--muted)}"
    ".note{color:var(--muted);font-size:14px}"
    ".flash{padding:8px 12px;border-radius:4px;margin:0 0 16px}"
    ".flash.ok{background:#e1ecc8;border:1px solid #6b8e3a;color:#2f4a12}"
    ".flash.bad{background:#f3d3c8;border:1px solid #a33a22;color:#6b1a0c}"
    "label{display:block;margin:0 0 10px;font-size:14px}"
    "input{display:block;width:100%;max-width:320px;margin-top:4px;padding:8px;font:inherit;"
    "border:1px solid var(--edge);border-radius:3px;background:#fbf6e8;color:var(--ink)}"
    ".btn{padding:8px 18px;font:inherit;font-weight:bold;color:#fff;cursor:pointer;"
    "background:linear-gradient(#b8382a,#7a1c12);border:1px solid #5a120a;border-radius:3px}"
    ".btn.quiet{background:var(--wood);color:var(--gold);border-color:var(--gold2)}"
    ".row{display:flex;flex-wrap:wrap;gap:12px;align-items:center}"
    ".search{display:flex;gap:8px;margin:0 0 16px}.search input{margin:0}"
    ".layout{display:grid;grid-template-columns:210px minmax(0,1fr);gap:24px;align-items:start}"
    "@media (max-width:720px){.layout{grid-template-columns:minmax(0,1fr)}"
    ".skills{display:flex;flex-wrap:wrap;gap:4px}}"
    ".skills a{display:block;padding:3px 10px;text-decoration:none;color:var(--ink);border-radius:3px}"
    ".skills a:hover{background:rgba(122,90,46,.15)}"
    ".skills a.on{background:var(--wood);color:var(--gold)}"
    ".chart{display:block;width:100%;height:auto;background:#2a1e14;border:1px solid var(--edge);"
    "border-radius:4px;margin:0}"
    ".badge{display:inline-block;font-size:14px;padding:2px 10px;border-radius:12px;"
    "background:var(--wood);color:var(--gold);vertical-align:middle;font-variant:small-caps;"
    "border:1px solid var(--gold2)}"
    ".up{color:#2f6a12}"
    "select{display:block;width:100%;max-width:320px;margin-top:4px;padding:8px;font:inherit;"
    "border:1px solid var(--edge);border-radius:3px;background:#fbf6e8;color:var(--ink)}"
    ".check{display:flex;gap:8px;align-items:center}"
    ".check input{display:inline;width:auto;margin:0}"
    ".subnav{margin:0 0 20px;gap:8px}"
    ".subnav a{border:1px solid var(--gold2);color:var(--parch);font-size:16px}"
    ".subnav a.on{background:var(--wood)}"
    ".asbar{background:#5a1e0e;border-bottom:2px solid var(--gold2);color:var(--parch)}"
    ".asbar .bar{padding:6px 16px;gap:6px 16px;font-size:15px}"
    ".asbar b{color:var(--gold)}"
    ".asbar form{margin:0}"
    "footer{text-align:center;color:#8a7a5a;font-size:13px;padding:16px}";

/* Where the account site's Play Now goes: the gate's landing (the game). */
static char const*
site_landing(struct IoGate const* gate)
{
    return gate->landing;
}

/* Staff: a web login one of whose linked player accounts is `admin = 1`.
 * The gate's login itself makes nobody staff (torirs_server_auth.h); the
 * admin flag lives on the player accounts, so a login is as staff as the
 * accounts it holds. */
static int
site_is_staff(
    struct IoGate const* gate,
    char const* who)
{
    char names[IO_GATE_LINKS_LIST_MAX][IO_GATE_LINKS_NAME_MAX];
    int linked = IoGateLinks_List(gate->links, who, names, IO_GATE_LINKS_LIST_MAX);

    for( int i = 0; i < linked; i++ )
    {
        if( ToriRSServer_AuthIsAdmin(names[i]) )
            return 1;
    }
    return 0;
}

/* The staff login behind this request when staff have signed in as another
 * login (io_gate.h IoGate_SignInAs), else "". Set per request by
 * IoSite_Handle; the handlers run one at a time, as g_view assumes. */
static char g_staff_behind[SITE_NAME_MAX];

/* Every web login's name, as the account server lists them. */
#define SITE_LOGINS_MAX 1024
static char g_logins[SITE_LOGINS_MAX][SITE_NAME_MAX];
static int g_login_count;

static void
collect_login(char const* name)
{
    if( g_login_count < SITE_LOGINS_MAX )
        snprintf(g_logins[g_login_count++], SITE_NAME_MAX, "%s", name);
}

/* Fill g_logins; their count, or -1 when the account server did not answer. */
static int
site_logins(void)
{
    g_login_count = 0;
    if( AccountClient_GateLoginEach(collect_login) < 0 )
        return -1;
    return g_login_count;
}

/* The g_logins index of the login keyed `key` (as a ticket names it), or -1. */
static int
site_login_find(char const* key)
{
    for( int i = 0; i < g_login_count; i++ )
    {
        char have[SITE_NAME_MAX];

        ToriRSServer_SaveFileName(g_logins[i], have, sizeof(have));
        if( strcmp(have, key) == 0 )
            return i;
    }
    return -1;
}

static void
page_begin(
    struct SiteBuf* b,
    struct IoGate const* gate,
    char const* who,
    char const* title,
    char const* nav)
{
    static char const* const k_nav[][2] = {
        { "/", "Home" },
        { "/hiscores", "Hiscores" },
        { "/account", "Account" },
    };

    buf_init(b);
    buf_puts(b, "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                "<title>");
    buf_esc(b, title);
    buf_puts(b, " - ToriRS</title>\n<style>");
    buf_puts(b, k_css);
    buf_puts(b, "</style>\n</head>\n<body>\n<header class=\"top\"><div class=\"bar\">"
                "<a class=\"brand\" href=\"/\">ToriRS</a><nav>");
    for( size_t i = 0; i < sizeof(k_nav) / sizeof(k_nav[0]); i++ )
        buf_printf(b, "<a href=\"%s\"%s>%s</a>", k_nav[i][0],
                   strcmp(nav, k_nav[i][0]) == 0 ? " class=\"on\"" : "", k_nav[i][1]);
    /* Staff pages share one tab, and a row of their own under it. */
    if( site_is_staff(gate, who) )
        buf_printf(b, "<a href=\"/admin/users\"%s>Admin</a>",
                   strncmp(nav, "/admin/", 7) == 0 ? " class=\"on\"" : "");
    buf_puts(b, "</nav><div class=\"who\"><span>Signed in as <b>");
    buf_esc(b, who);
    buf_puts(b, "</b></span><a class=\"mini\" href=\"");
    buf_esc(b, site_landing(gate));
    buf_puts(b, "\">Play</a><form method=\"post\" action=\"/logout\">"
                "<button class=\"linkbtn\">Sign out</button></form></div></div></header>\n");
    if( g_staff_behind[0] )
    {
        buf_puts(b, "<div class=\"asbar\"><div class=\"bar\"><span>Staff view: <b>");
        buf_esc(b, g_staff_behind);
        buf_puts(b, "</b> is signed in as <b>");
        buf_esc(b, who);
        buf_puts(b, "</b>. Play plays this login's accounts.</span>"
                    "<form method=\"post\" action=\"/account/return\"><button class=\"linkbtn\">"
                    "Return to ");
        buf_esc(b, g_staff_behind);
        buf_puts(b, "</button></form></div></div>\n");
    }
    buf_puts(b, "<main>\n");
    if( strncmp(nav, "/admin/", 7) == 0 )
    {
        static char const* const k_admin[][2] = {
            { "/admin/users", "Users" },
            { "/admin/players", "Players" },
            { "/admin/recordings", "Recordings" },
        };

        buf_puts(b, "<nav class=\"subnav\">");
        for( size_t i = 0; i < sizeof(k_admin) / sizeof(k_admin[0]); i++ )
            buf_printf(b, "<a href=\"%s\"%s>%s</a>", k_admin[i][0],
                       strcmp(nav, k_admin[i][0]) == 0 ? " class=\"on\"" : "", k_admin[i][1]);
        buf_puts(b, "</nav>\n");
    }
}

static void
page_send(
    struct SiteBuf* b,
    struct HttpResponse* res,
    int status)
{
    buf_puts(b, "</main>\n<footer>ToriRS &middot; a private world</footer>\n</body>\n</html>\n");
    res->status = status;
    snprintf(res->content_type, sizeof(res->content_type), "text/html; charset=utf-8");
    res->body = b->data;
    res->body_len = (int)b->len;
    res->owns_body = 1;
}

static void
redirect(
    struct HttpResponse* res,
    char const* location)
{
    res->status = 303;
    snprintf(res->location, sizeof(res->location), "%s", location);
}

/* ------------------------------------------------------------------ */
/* Players                                                             */
/* ------------------------------------------------------------------ */

struct SitePlayer
{
    /** As the save is keyed: the page's URL. */
    char key[SITE_NAME_MAX];
    char name[SITE_NAME_MAX];
    long long xp[SITE_SKILLS];
    int level[SITE_SKILLS];
    int total_level;
    long long total_xp;
    int combat;
    int quest_points;
    /** When the game last reported the player; 0 never played. */
    long long saved_at;
    /** Staff (`admin = 1` in the account record): never ranked. */
    int admin;
    /** The account server did not answer for this one. */
    int unknown;
};

/* The one page view the account server answers with; single-threaded, and
 * too big for the stack. */
static struct AccountPlayerView g_view;

static int
combat_level(int const* level)
{
    double base = 0.25 * (level[1] + level[SITE_HITPOINTS] + floor(level[5] / 2.0));
    double melee = 0.325 * (level[0] + level[2]);
    double range = 0.325 * floor(level[4] * 1.5);
    double magic = 0.325 * floor(level[6] * 1.5);
    double best = melee > range ? melee : range;

    if( magic > best )
        best = magic;
    return (int)floor(base + best);
}

static void
player_finish(struct SitePlayer* p)
{
    p->total_level = 0;
    p->total_xp = 0;
    for( int i = 0; i < SITE_SKILLS; i++ )
    {
        p->level[i] = AccountProto_LevelForXp(p->xp[i]);
        p->total_level += p->level[i];
        p->total_xp += p->xp[i];
    }
    p->combat = combat_level(p->level);
}

static void
player_from(
    struct AccountPlayer const* row,
    struct SitePlayer* out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->key, sizeof(out->key), "%s", row->key);
    snprintf(out->name, sizeof(out->name), "%s", row->name);
    for( int i = 0; i < SITE_SKILLS; i++ )
        out->xp[i] = row->xp[i];
    out->quest_points = row->quest_points;
    out->saved_at = row->last_seen;
    out->admin = !row->ranked;
    player_finish(out);
}

/* A card for a player with no stats to show: never played, or the account
 * server did not answer. */
static void
player_blank(
    char const* key,
    char const* name,
    int unknown,
    struct SitePlayer* out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->key, sizeof(out->key), "%s", key);
    snprintf(out->name, sizeof(out->name), "%s", name);
    out->xp[SITE_HITPOINTS] = 1154;
    out->unknown = unknown;
    player_finish(out);
}

/* The accounts linked to `who`, as players; the caller frees (ToriMem_Net). */
static int
linked_players(
    struct IoGate* gate,
    char const* who,
    struct SitePlayer** out)
{
    char names[IO_GATE_LINKS_LIST_MAX][IO_GATE_LINKS_NAME_MAX];
    int linked = IoGateLinks_List(gate->links, who, names, IO_GATE_LINKS_LIST_MAX);
    struct SitePlayer* list =
        ToriMem_Alloc(ToriMem_Net(), (size_t)(linked > 0 ? linked : 1) * sizeof(*list));

    assert(list);
    for( int i = 0; i < linked; i++ )
    {
        char key[SITE_NAME_MAX];
        int found;

        ToriRSServer_SaveFileName(names[i], key, sizeof(key));
        found = AccountClient_PlayerView(key, 0, &g_view);
        if( found > 0 )
            player_from(&g_view.player, &list[i]);
        else
            player_blank(key, names[i], found < 0, &list[i]);
    }
    *out = list;
    return linked > 0 ? linked : 0;
}

static int
owns(
    struct IoGate* gate,
    char const* who,
    char const* key)
{
    char names[IO_GATE_LINKS_LIST_MAX][IO_GATE_LINKS_NAME_MAX];
    int linked = IoGateLinks_List(gate->links, who, names, IO_GATE_LINKS_LIST_MAX);

    for( int i = 0; i < linked; i++ )
    {
        char have[SITE_NAME_MAX];

        ToriRSServer_SaveFileName(names[i], have, sizeof(have));
        if( strcmp(have, key) == 0 )
            return 1;
    }
    return 0;
}

static void
player_link_to(
    struct SiteBuf* b,
    char const* key,
    char const* name)
{
    buf_puts(b, "<a href=\"/players/");
    buf_esc(b, key);
    buf_puts(b, "\">");
    buf_esc(b, name);
    buf_puts(b, "</a>");
}

static void
player_link(
    struct SiteBuf* b,
    struct SitePlayer const* p)
{
    player_link_to(b, p->key, p->name);
}

/* The linked accounts as cards, with a link to each page. */
static void
linked_cards(
    struct SiteBuf* b,
    struct SitePlayer const* list,
    int count,
    int with_unlink)
{
    long long now = (long long)time(NULL);

    buf_puts(b, "<div class=\"cards\">");
    for( int i = 0; i < count; i++ )
    {
        struct SitePlayer const* p = &list[i];

        buf_puts(b, "<div class=\"card\"><div><div class=\"name\">");
        player_link(b, p);
        if( p->unknown )
            buf_puts(b, "</div><div class=\"meta\">Stats unavailable right now");
        else
        {
            buf_printf(b, "</div><div class=\"meta\">Combat %d &middot; Total level %d", p->combat,
                       p->total_level);
            if( p->saved_at )
            {
                buf_puts(b, " &middot; played ");
                buf_ago(b, p->saved_at, now);
            }
            else
                buf_puts(b, " &middot; not played yet");
        }
        buf_puts(b, "</div></div><div class=\"row\"><a href=\"/players/");
        buf_esc(b, p->key);
        buf_puts(b, "\">Stats</a>");
        if( with_unlink )
        {
            buf_puts(b, "<a href=\"/account/unlink?player=");
            buf_url(b, p->name);
            buf_puts(b, "\">Unlink</a>");
        }
        buf_puts(b, "</div></div>");
    }
    buf_puts(b, "</div>");
}

/* A rank from the view, after `prefix`; a dash for an unranked (staff) one. */
static void
buf_rank(
    struct SiteBuf* b,
    int rank,
    char const* prefix)
{
    if( rank > 0 )
        buf_printf(b, "%s%d", prefix, rank);
    else
        buf_puts(b, "&ndash;");
}

static void
unavailable_page(
    struct IoGate const* gate,
    char const* who,
    char const* title,
    char const* nav,
    struct HttpResponse* res)
{
    struct SiteBuf b;

    page_begin(&b, gate, who, title, nav);
    buf_puts(&b, "<section class=\"panel\"><h2>Not available right now</h2><p>The account "
                 "service is not answering. Try again in a moment.</p></section>");
    page_send(&b, res, 503);
}

/* ------------------------------------------------------------------ */
/* Pages                                                               */
/* ------------------------------------------------------------------ */

static void
page_home(
    struct IoGate* gate,
    char const* who,
    struct HttpResponse* res)
{
    struct SiteBuf b;
    struct SitePlayer* mine;
    struct AccountRow top[SITE_TOP_ON_HOME];
    int ranked;
    int mine_count = linked_players(gate, who, &mine);
    int top_count =
        AccountClient_Hiscores(ACCOUNT_OVERALL, 0, SITE_TOP_ON_HOME, top, &ranked);

    page_begin(&b, gate, who, "Home", "/");
    buf_puts(&b, "<section class=\"hero\"><h1>ToriRS</h1>"
                 "<p>An old world, kept by a few. Pick up where you left off.</p>"
                 "<a class=\"play\" href=\"");
    buf_esc(&b, site_landing(gate));
    buf_puts(&b, "\">Play Now</a></section>\n<div class=\"grid\">\n");

    buf_puts(&b, "<section class=\"panel\"><h2>Your accounts</h2>");
    if( mine_count > 0 )
        linked_cards(&b, mine, mine_count, 0);
    else
        buf_puts(&b, "<p>No player accounts are linked to this login yet.</p>"
                     "<p class=\"note\">Create or link one on the <a href=\"/account\">Account</a> page, or "
                     "choose <i>Link account</i> when you log in to the game. A linked account "
                     "plays from this login's Play Now and keeps its history here.</p>");
    buf_puts(&b, "<p class=\"note\" style=\"margin-top:12px\"><a href=\"/account\">Manage linked "
                 "accounts</a></p></section>\n");

    buf_puts(&b, "<section class=\"panel\"><h2>Top players</h2>");
    if( top_count < 0 )
        buf_puts(&b, "<p>The hiscores are not available right now.</p>");
    else if( top_count == 0 )
        buf_puts(&b, "<p>Nobody has played yet.</p>");
    else
    {
        buf_puts(&b, "<div class=\"scroll\"><table><thead><tr><th class=\"num\">Rank</th>"
                     "<th>Name</th><th class=\"num\">Total</th><th class=\"num\">XP</th></tr>"
                     "</thead><tbody>");
        for( int i = 0; i < top_count; i++ )
        {
            int total_level;
            long long total_xp;

            AccountProto_OverallSplit(top[i].score, &total_level, &total_xp);
            buf_printf(&b, "<tr><td class=\"num\">%d</td><td>", i + 1);
            player_link_to(&b, top[i].key, top[i].name);
            buf_printf(&b, "</td><td class=\"num\">%d</td><td class=\"num\">", total_level);
            buf_num(&b, total_xp);
            buf_puts(&b, "</td></tr>");
        }
        buf_puts(&b, "</tbody></table></div>"
                     "<p class=\"note\" style=\"margin-top:12px\"><a href=\"/hiscores\">All "
                     "hiscores</a></p>");
    }
    buf_puts(&b, "</section>\n</div>\n");
    ToriMem_Free(ToriMem_Net(), mine);
    page_send(&b, res, 200);
}

static void
page_hiscores(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    struct SiteBuf b;
    struct AccountRow* rows;
    char field[16] = "";
    int skill = -1;
    int page = 0;
    int ranked = 0;
    int pages;
    int count;
    int category;

    if( IoGate_QueryField(req, "skill", field, sizeof(field)) )
    {
        skill = atoi(field);
        if( skill < 0 || skill >= SITE_SKILLS )
            skill = -1;
    }
    if( IoGate_QueryField(req, "page", field, sizeof(field)) )
        page = atoi(field) - 1;
    if( page < 0 )
        page = 0;
    category = skill < 0 ? ACCOUNT_OVERALL : skill;
    rows = ToriMem_Alloc(ToriMem_Net(), SITE_HISCORES_PAGE * sizeof(*rows));
    assert(rows);
    count = AccountClient_Hiscores(category, page * SITE_HISCORES_PAGE, SITE_HISCORES_PAGE, rows,
                                   &ranked);
    if( count < 0 )
    {
        ToriMem_Free(ToriMem_Net(), rows);
        unavailable_page(gate, who, "Hiscores", "/hiscores", res);
        return;
    }
    pages = ranked > 0 ? (ranked + SITE_HISCORES_PAGE - 1) / SITE_HISCORES_PAGE : 1;
    /* Past the end: the last page instead. */
    if( page >= pages )
    {
        page = pages - 1;
        count = AccountClient_Hiscores(category, page * SITE_HISCORES_PAGE, SITE_HISCORES_PAGE,
                                       rows, &ranked);
        if( count < 0 )
            count = 0;
    }

    page_begin(&b, gate, who, "Hiscores", "/hiscores");
    buf_printf(&b, "<div class=\"title\"><h1>Hiscores<small>%s</small></h1></div>\n",
               skill < 0 ? "Overall" : k_skill_names[skill]);
    buf_puts(&b, "<div class=\"layout\"><section class=\"panel\"><h2>Skills</h2>"
                 "<div class=\"skills\">");
    buf_printf(&b, "<a href=\"/hiscores\"%s>Overall</a>", skill < 0 ? " class=\"on\"" : "");
    for( int i = 0; i < SITE_SKILLS; i++ )
        buf_printf(&b, "<a href=\"/hiscores?skill=%d\"%s>%s</a>", i,
                   i == skill ? " class=\"on\"" : "", k_skill_names[i]);
    buf_puts(&b, "</div></section>\n<section class=\"panel\">"
                 "<form class=\"search\" method=\"get\" action=\"/players\">"
                 "<input name=\"name\" placeholder=\"Look up a player\" maxlength=\"12\" "
                 "aria-label=\"Player name\"><button class=\"btn quiet\">Search</button></form>");
    if( count == 0 )
        buf_puts(&b, "<p>Nobody has played yet.</p>");
    else
    {
        buf_puts(&b, "<div class=\"scroll\"><table><thead><tr><th class=\"num\">Rank</th>"
                     "<th>Name</th><th class=\"num\">Level</th><th class=\"num\">XP</th></tr>"
                     "</thead><tbody>");
        for( int i = 0; i < count; i++ )
        {
            int level;
            long long xp;

            if( skill < 0 )
                AccountProto_OverallSplit(rows[i].score, &level, &xp);
            else
            {
                xp = rows[i].score;
                level = AccountProto_LevelForXp(xp);
            }
            buf_printf(&b, "<tr%s><td class=\"num\">%d</td><td>",
                       owns(gate, who, rows[i].key) ? " class=\"me\"" : "",
                       page * SITE_HISCORES_PAGE + i + 1);
            player_link_to(&b, rows[i].key, rows[i].name);
            buf_printf(&b, "</td><td class=\"num\">%d</td><td class=\"num\">", level);
            buf_num(&b, xp);
            buf_puts(&b, "</td></tr>");
        }
        buf_puts(&b, "</tbody></table></div>");
        if( pages > 1 )
        {
            buf_puts(&b, "<div class=\"row\" style=\"margin-top:12px\">");
            if( page > 0 )
                buf_printf(&b, "<a href=\"/hiscores?skill=%d&amp;page=%d\">Previous</a>", skill,
                           page);
            buf_printf(&b, "<span class=\"note\">Page %d of %d</span>", page + 1, pages);
            if( page + 1 < pages )
                buf_printf(&b, "<a href=\"/hiscores?skill=%d&amp;page=%d\">Next</a>", skill,
                           page + 2);
            buf_puts(&b, "</div>");
        }
    }
    buf_puts(&b, "</section></div>\n");
    ToriMem_Free(ToriMem_Net(), rows);
    page_send(&b, res, 200);
}

/* Total experience over time, one point per day it changed: inline SVG. */
static void
history_chart(
    struct SiteBuf* b,
    struct AccountPlayerView const* v)
{
    enum
    {
        W = 960,
        H = 240,
        PAD_L = 70,
        PAD_R = 16,
        PAD_T = 16,
        PAD_B = 34,
    };
    int points = v->chart_count;
    long long t0;
    long long t1;
    long long lo;
    long long hi;

    if( points < 2 )
    {
        buf_puts(b, "<p class=\"note\">The experience chart appears once two days have "
                    "experience recorded.</p>");
        return;
    }
    t0 = v->chart_day[0];
    t1 = v->chart_day[points - 1];
    lo = hi = v->chart_xp[0];
    for( int i = 1; i < points; i++ )
    {
        if( v->chart_xp[i] < lo )
            lo = v->chart_xp[i];
        if( v->chart_xp[i] > hi )
            hi = v->chart_xp[i];
    }
    if( hi == lo )
        hi = lo + 1;

    buf_printf(b, "<svg class=\"chart\" viewBox=\"0 0 %d %d\" role=\"img\" "
                  "aria-label=\"Total experience over time\">",
               W, H);
    /* Gridlines and the value axis: low, middle, high. */
    for( int g = 0; g <= 2; g++ )
    {
        double y = PAD_T + (H - PAD_T - PAD_B) * (1.0 - g / 2.0);
        long long value = lo + (hi - lo) * g / 2;

        buf_printf(b, "<line x1=\"%d\" x2=\"%d\" y1=\"%.1f\" y2=\"%.1f\" stroke=\"#4a3826\"/>",
                   PAD_L, W - PAD_R, y, y);
        buf_printf(b, "<text x=\"%d\" y=\"%.1f\" fill=\"#cdbf9e\" font-size=\"12\" "
                      "text-anchor=\"end\" font-family=\"Georgia,serif\">",
                   PAD_L - 6, y + 4);
        buf_num(b, value);
        buf_puts(b, "</text>");
    }
    /* The area, then the line over it. */
    {
        struct SiteBuf line;
        double last_x = PAD_L;

        buf_init(&line);
        for( int i = 0; i < points; i++ )
        {
            double x = PAD_L + (double)(W - PAD_L - PAD_R) * (double)(v->chart_day[i] - t0) /
                                   (double)(t1 - t0);
            double y = PAD_T + (double)(H - PAD_T - PAD_B) *
                                   (1.0 - (double)(v->chart_xp[i] - lo) / (double)(hi - lo));

            buf_printf(&line, "%s%.1f,%.1f", i == 0 ? "" : " ", x, y);
            last_x = x;
        }
        buf_printf(b, "<polygon fill=\"rgba(240,192,96,.18)\" points=\"%d,%d %s %.1f,%d\"/>", PAD_L,
                   H - PAD_B, line.data, last_x, H - PAD_B);
        buf_printf(b, "<polyline fill=\"none\" stroke=\"#f0c060\" stroke-width=\"2.5\" "
                      "stroke-linejoin=\"round\" points=\"%s\"/>",
                   line.data);
        ToriMem_Free(ToriMem_Net(), line.data);
    }
    /* The time axis: the first and the last day. */
    buf_printf(b, "<text x=\"%d\" y=\"%d\" fill=\"#cdbf9e\" font-size=\"12\" "
                  "font-family=\"Georgia,serif\">",
               PAD_L, H - 10);
    buf_day(b, t0);
    buf_printf(b, "</text><text x=\"%d\" y=\"%d\" fill=\"#cdbf9e\" font-size=\"12\" "
                  "text-anchor=\"end\" font-family=\"Georgia,serif\">",
               W - PAD_R, H - 10);
    buf_day(b, t1);
    buf_puts(b, "</text></svg>");
}

/* Gains over the last day, week and month, per skill that gained: the
 * experience now against the view's base for each window. */
static void
history_gains(
    struct SiteBuf* b,
    struct AccountPlayerView const* v,
    struct SitePlayer const* p)
{
    static char const* const k_window_names[ACCOUNT_GAIN_WINDOWS] = { "Day", "Week", "Month" };

    if( !v->has_history )
        return;
    buf_puts(b, "<h2>Experience gained</h2><div class=\"scroll\"><table><thead><tr>"
                "<th>Skill</th>");
    for( int w = 0; w < ACCOUNT_GAIN_WINDOWS; w++ )
        buf_printf(b, "<th class=\"num\">%s%s</th>", k_window_names[w],
                   v->gain_partial[w] ? "*" : "");
    buf_puts(b, "</tr></thead><tbody>");
    for( int s = -1; s < SITE_SKILLS; s++ )
    {
        int at = s < 0 ? SITE_SKILLS : s;
        long long current = s < 0 ? p->total_xp : p->xp[s];
        long long gain[ACCOUNT_GAIN_WINDOWS];
        int gained = 0;

        for( int w = 0; w < ACCOUNT_GAIN_WINDOWS; w++ )
        {
            gain[w] = current - v->gain_base[w][at];
            if( gain[w] > 0 )
                gained = 1;
        }
        if( !gained && s >= 0 )
            continue;
        buf_printf(b, "<tr><td>%s</td>", s < 0 ? "<b>Overall</b>" : k_skill_names[s]);
        for( int w = 0; w < ACCOUNT_GAIN_WINDOWS; w++ )
        {
            buf_printf(b, "<td class=\"num%s\">", gain[w] > 0 ? " up" : "");
            if( gain[w] > 0 )
                buf_puts(b, "+");
            buf_num(b, gain[w]);
            buf_puts(b, "</td>");
        }
        buf_puts(b, "</tr>");
    }
    buf_puts(b, "</tbody></table></div>");
    if( v->gain_partial[0] || v->gain_partial[1] || v->gain_partial[2] )
        buf_puts(b, "<p class=\"note\">* since the first recorded day.</p>");
}

/* The newest sessions: when, how long, what was gained, mostly where. */
static void
history_sessions(
    struct SiteBuf* b,
    struct AccountPlayerView const* v)
{
    buf_puts(b, "<h2>Recent sessions</h2>");
    if( v->session_count == 0 )
    {
        buf_puts(b, "<p class=\"note\">No sessions recorded yet: history starts with this "
                    "account's next log out.</p>");
        return;
    }
    buf_printf(b, "<p class=\"note\">%d session%s recorded, ", v->session_total,
               v->session_total == 1 ? "" : "s");
    buf_duration(b, v->played);
    buf_puts(b, " played.</p><div class=\"scroll\"><table><thead><tr><th>Logged in</th>"
                "<th class=\"num\">Length</th><th class=\"num\">XP gained</th>"
                "<th>Most trained</th></tr></thead><tbody>");
    for( int i = 0; i < v->session_count; i++ )
    {
        struct AccountSession const* s = &v->sessions[i];

        buf_puts(b, "<tr><td class=\"when\">");
        buf_when(b, s->start ? s->start : s->end);
        buf_puts(b, "</td><td class=\"num\">");
        if( s->start && s->end >= s->start )
            buf_duration(b, s->end - s->start);
        else
            buf_puts(b, "&ndash;");
        buf_puts(b, "</td><td class=\"num\">");
        if( s->xp_gained > 0 )
            buf_puts(b, "+");
        buf_num(b, s->xp_gained);
        buf_puts(b, "</td><td>");
        if( s->top_stat >= 0 && s->top_stat < SITE_SKILLS )
        {
            buf_printf(b, "%s (+", k_skill_names[s->top_stat]);
            buf_num(b, s->top_gain);
            buf_puts(b, ")");
        }
        else
            buf_puts(b, "&ndash;");
        buf_puts(b, "</td></tr>");
    }
    buf_puts(b, "</tbody></table></div>");
}

/* The adventure log: the newest lines, newest first. */
static void
adventure_log(
    struct SiteBuf* b,
    struct AccountPlayerView const* v)
{
    long long now = (long long)time(NULL);

    buf_puts(b, "<section class=\"panel\" style=\"margin:0 0 24px\"><h2>Adventure log</h2>");
    if( v->event_count == 0 )
    {
        buf_puts(b, "<p class=\"note\">Nothing yet: completed quests appear here.</p>"
                    "</section>\n");
        return;
    }
    buf_puts(b, "<div class=\"scroll\"><table><tbody>");
    for( int i = 0; i < v->event_count; i++ )
    {
        struct AccountEvent const* e = &v->events[i];

        buf_puts(b, "<tr><td class=\"when\">");
        buf_ago(b, e->time, now);
        buf_puts(b, "</td><td>");
        if( e->kind == TORIRSSERVER_ADVENTURE_QUEST )
        {
            buf_puts(b, "Completed the quest <b>");
            buf_esc(b, e->label);
            buf_puts(b, "</b>");
            if( e->amount > 0 )
                buf_printf(b, " <span class=\"note\">(+%d quest point%s)</span>", e->amount,
                           e->amount == 1 ? "" : "s");
        }
        else
            buf_esc(b, e->label);
        buf_puts(b, "</td></tr>");
    }
    buf_puts(b, "</tbody></table></div></section>\n");
}

static void
page_player(
    struct IoGate* gate,
    char const* who,
    char const* key,
    struct HttpResponse* res)
{
    struct SiteBuf b;
    struct SitePlayer p;
    int mine = owns(gate, who, key);
    int found = AccountClient_PlayerView(key, mine, &g_view);
    long long now = (long long)time(NULL);

    if( found < 0 )
    {
        unavailable_page(gate, who, "Player", "/hiscores", res);
        return;
    }
    if( found == 0 )
    {
        page_begin(&b, gate, who, "Player not found", "/hiscores");
        buf_puts(&b, "<section class=\"panel\"><h2>No such player</h2><p>Nobody by that name has "
                     "played here.</p><p><a href=\"/hiscores\">Back to the hiscores</a></p>"
                     "</section>");
        page_send(&b, res, 404);
        return;
    }
    player_from(&g_view.player, &p);
    page_begin(&b, gate, who, p.name, "/hiscores");
    buf_puts(&b, "<div class=\"title\"><h1>");
    buf_esc(&b, p.name);
    if( mine )
        buf_puts(&b, " <span class=\"badge\">linked to you</span>");
    buf_puts(&b, "</h1></div>\n<div class=\"tiles\">");
    buf_printf(&b, "<div class=\"tile\"><b>%d</b><span>Combat level</span></div>", p.combat);
    buf_printf(&b, "<div class=\"tile\"><b>%d</b><span>Total level</span></div>", p.total_level);
    buf_puts(&b, "<div class=\"tile\"><b>");
    buf_num(&b, p.total_xp);
    buf_puts(&b, "</b><span>Total XP</span></div>");
    buf_printf(&b, "<div class=\"tile\"><b>%d</b><span>Quest points</span></div>", p.quest_points);
    buf_puts(&b, "<div class=\"tile\"><b>");
    buf_rank(&b, g_view.rank_overall, "#");
    buf_puts(&b, "</b><span>Overall rank</span></div>");
    buf_puts(&b, "<div class=\"tile\"><b>");
    buf_ago(&b, p.saved_at, now);
    buf_puts(&b, "</b><span>Last played</span></div></div>\n");

    /* An owner's history: the chart across the page, the tables beside the
     * skills. */
    if( mine )
    {
        buf_puts(&b, "<section class=\"panel\" style=\"margin:0 0 24px\"><h2>Total experience"
                     "</h2>");
        history_chart(&b, &g_view);
        buf_puts(&b, "</section>\n");
    }
    /* Everyone's adventure log is public, except staff's: theirs is only on
     * their own page. */
    if( !p.admin || mine )
        adventure_log(&b, &g_view);
    buf_puts(&b, "<div class=\"grid\">\n");

    buf_puts(&b, "<section class=\"panel\"><h2>Skills</h2><div class=\"scroll\"><table><thead>"
                 "<tr><th>Skill</th><th class=\"num\">Rank</th><th class=\"num\">Level</th>"
                 "<th class=\"num\">XP</th></tr></thead><tbody>");
    buf_puts(&b, "<tr><td><a href=\"/hiscores\"><b>Overall</b></a></td><td class=\"num\">");
    buf_rank(&b, g_view.rank_overall, "");
    buf_printf(&b, "</td><td class=\"num\">%d</td><td class=\"num\">", p.total_level);
    buf_num(&b, p.total_xp);
    buf_puts(&b, "</td></tr>");
    for( int s = 0; s < SITE_SKILLS; s++ )
    {
        buf_printf(&b, "<tr><td><a href=\"/hiscores?skill=%d\">%s</a></td><td class=\"num\">", s,
                   k_skill_names[s]);
        buf_rank(&b, g_view.rank[s], "");
        buf_printf(&b, "</td><td class=\"num\">%d</td><td class=\"num\">", p.level[s]);
        buf_num(&b, p.xp[s]);
        buf_puts(&b, "</td></tr>");
    }
    buf_puts(&b, "</tbody></table></div></section>\n");

    buf_puts(&b, "<section class=\"panel\">");
    if( mine )
    {
        history_gains(&b, &g_view, &p);
        history_sessions(&b, &g_view);
    }
    else
        buf_puts(&b, "<h2>History</h2><p>Session history is private to the login this account "
                     "is linked to.</p><p class=\"note\">Your own linked accounts show their "
                     "sessions, play time and experience gained here.</p>");
    buf_puts(&b, "</section>\n</div>\n");
    page_send(&b, res, 200);
}

/* The name, and for staff the admin box: the create form's fields on
 * /account and /admin/users. */
static void
create_form_fields(
    struct SiteBuf* b,
    char const* name,
    int staff,
    int admin)
{
    buf_puts(b, "<label>Player name<input name=\"username\" maxlength=\"12\" required "
                "autocomplete=\"off\" value=\"");
    buf_esc(b, name ? name : "");
    buf_puts(b, "\"></label><p class=\"note\">1 to 12 letters, digits, spaces, - or _, with "
                "at least one letter.</p>");
    if( staff )
        buf_printf(b, "<label class=\"check\"><input type=\"checkbox\" name=\"admin\" "
                      "value=\"1\"%s> Admin (staff): the jmod crown and ::commands, off the "
                      "hiscores</label>",
                   admin ? " checked" : "");
}

static void
page_account(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res,
    int status,
    char const* error,
    char const* typed_name,
    char const* create_name,
    int create_admin)
{
    struct SiteBuf b;
    struct SitePlayer* mine;
    int mine_count = linked_players(gate, who, &mine);
    int staff = site_is_staff(gate, who);
    char done[16] = "";
    char player[SITE_NAME_MAX] = "";

    page_begin(&b, gate, who, "Account", "/account");
    buf_puts(&b, "<div class=\"title\"><h1>Account<small>");
    buf_esc(&b, who);
    buf_puts(&b, "</small></h1></div>\n");
    if( error )
    {
        buf_puts(&b, "<p class=\"flash bad\">");
        buf_esc(&b, error);
        buf_puts(&b, "</p>");
    }
    else if( IoGate_QueryField(req, "done", done, sizeof(done)) &&
             IoGate_QueryField(req, "player", player, sizeof(player)) )
    {
        buf_puts(&b, "<p class=\"flash ok\">");
        buf_esc(&b, player);
        if( strcmp(done, "created") == 0 )
            buf_puts(&b, " is made and linked to this login. Press Play and choose it on the "
                         "Play Now screen.");
        else if( strcmp(done, "linked") == 0 )
            buf_puts(&b, " is linked to this login. It now plays from this login's Play Now.");
        else
            buf_puts(&b, " is unlinked. Its own password signs it in again.");
        buf_puts(&b, "</p>");
    }
    buf_puts(&b, "<div class=\"grid\">\n<section class=\"panel\"><h2>Linked player accounts</h2>");
    if( mine_count > 0 )
        linked_cards(&b, mine, mine_count, 1);
    else
        buf_puts(&b, "<p>None yet.</p>");
    buf_puts(&b, "<p class=\"note\" style=\"margin-top:12px\">A linked account belongs to this "
                 "login alone: it signs in only from here, never with its own password, and "
                 "its session history is kept on its page.</p></section>\n");

    buf_puts(&b, "<div class=\"stack\"><section class=\"panel\"><h2>Create a player account</h2>"
                 "<form method=\"post\" action=\"/account/create\">");
    create_form_fields(&b, create_name, staff, create_admin);
    buf_puts(&b, "<button class=\"btn\">Create account</button></form>"
                 "<p class=\"note\" style=\"margin-top:12px\">The new account is linked to this "
                 "login: press Play and choose it on the Play Now screen. No password needed.</p>"
                 "</section>\n");

    buf_puts(&b, "<section class=\"panel\"><h2>Link a player account</h2>"
                 "<form method=\"post\" action=\"/account/link-player\">"
                 "<label>Player name<input name=\"username\" maxlength=\"12\" required "
                 "autocomplete=\"username\" value=\"");
    buf_esc(&b, typed_name ? typed_name : "");
    buf_puts(&b, "\"></label><label>Its password<input name=\"password\" type=\"password\" "
                 "maxlength=\"20\" required autocomplete=\"current-password\"></label>"
                 "<button class=\"btn\">Link account</button></form>"
                 "<p class=\"note\" style=\"margin-top:12px\">The password proves the account "
                 "is yours. After linking, that password stops working: the account plays "
                 "through this login. Use this for an account made in the game; a new one is "
                 "made above.</p>"
                 "</section></div>\n</div>\n");
    ToriMem_Free(ToriMem_Net(), mine);
    page_send(&b, res, status);
}

static void
page_unlink_confirm(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    struct SiteBuf b;
    char player[SITE_NAME_MAX] = "";

    if( !IoGate_QueryField(req, "player", player, sizeof(player)) || !player[0] )
    {
        redirect(res, "/account");
        return;
    }
    page_begin(&b, gate, who, "Unlink", "/account");
    buf_puts(&b, "<section class=\"panel\" style=\"max-width:560px\"><h2>Unlink ");
    buf_esc(&b, player);
    buf_puts(&b, "?</h2><p>It leaves this login: its card goes from the Play Now screen, its "
                 "history is no longer shown to you, and it signs in with its own password "
                 "again.</p><p class=\"note\">An account made on this site has no password "
                 "anyone knows: after unlinking, an operator has to set one before it can "
                 "play.</p><form method=\"post\" action=\"/account/unlink\" class=\"row\">"
                 "<input type=\"hidden\" name=\"player\" value=\"");
    buf_esc(&b, player);
    buf_puts(&b, "\"><button class=\"btn\">Unlink</button><a href=\"/account\">Cancel</a></form>"
                 "</section>");
    page_send(&b, res, 200);
}

/* `/account?done=<what>&player=<name>` */
static void
redirect_done(
    struct HttpResponse* res,
    char const* what,
    char const* player)
{
    struct SiteBuf b;

    buf_init(&b);
    buf_printf(&b, "/account?done=%s&player=", what);
    buf_url(&b, player);
    redirect(res, b.data);
    ToriMem_Free(ToriMem_Net(), b.data);
}


/* ------------------------------------------------------------------ */
/* Staff: session recordings                                           */
/* ------------------------------------------------------------------ */
/*
 * torirsserver writes every session of an account whose record says
 * `record = 1` to <saves>/recordings/<account>/<yyyymmdd-hhmmss>.bin, an
 * RSProx recording (torirsserver/torirs_server_recording.h). These pages list
 * them, switch recording on and off per account, serve the files, and play
 * one in the game page's replay mode (`/?replay=<url>`, web/torirs_host.js)
 * under manifests/manifest_replay.ini, the package's replay manifest. Staff
 * only: a recording holds everything the player saw and typed, private chat
 * included.
 */

#define SITE_RECORDINGS_MAX 4000

struct SiteRecording
{
    char account[SITE_NAME_MAX];
    /** "yyyymmdd-hhmmss.bin" */
    char file[32];
    /** Unix seconds: the name's start, and the file's last write. */
    long long start;
    long long last;
    long long size;
};

/* [a-z0-9_]+: what ToriRSServer_SaveFileName makes, so nothing else can name
 * a directory under recordings/. */
static int
recording_account_ok(char const* name)
{
    if( !name[0] || strlen(name) >= SITE_NAME_MAX )
        return 0;
    for( char const* c = name; *c; c++ )
    {
        if( !(islower((unsigned char)*c) || isdigit((unsigned char)*c) || *c == '_') )
            return 0;
    }
    return 1;
}

/* Days from 1970-01-01 to y-m-d (proleptic Gregorian). */
static long long
days_from_civil(
    long long y,
    unsigned m,
    unsigned d)
{
    long long era;
    unsigned yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

/* "yyyymmdd-hhmmss.bin" -> its UTC start in unix seconds, or -1. */
static long long
recording_file_start(char const* name)
{
    int y, mo, d, h, mi, se;
    char tail[8];

    if( strlen(name) != 19 )
        return -1;
    for( int i = 0; i < 15; i++ )
    {
        if( i == 8 ? name[i] != '-' : !isdigit((unsigned char)name[i]) )
            return -1;
    }
    if( strcmp(name + 15, ".bin") != 0 )
        return -1;
    if( sscanf(name, "%4d%2d%2d-%2d%2d%2d%4s", &y, &mo, &d, &h, &mi, &se, tail) != 7 )
        return -1;
    return days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + se;
}

static void
recordings_dir(
    char* out,
    size_t cap)
{
    snprintf(out, cap, "%s/recordings", ToriRSServer_SaveDir());
}

static int
recording_cmp(
    void const* a,
    void const* b)
{
    struct SiteRecording const* x = a;
    struct SiteRecording const* y = b;

    if( x->start != y->start )
        return x->start < y->start ? 1 : -1;
    return strcmp(x->account, y->account);
}

/* Every recording, newest first, or only `account`'s when it is not "". The
 * caller frees *out (ToriMem_Net). */
static int
recordings_list(
    char const* account,
    struct SiteRecording** out)
{
    char root[1024];
    DIR* top;
    struct dirent* entry;
    struct SiteRecording* list =
        ToriMem_Alloc(ToriMem_Net(), SITE_RECORDINGS_MAX * sizeof(*list));
    int count = 0;

    assert(list);
    *out = list;
    recordings_dir(root, sizeof(root));
    top = opendir(root);
    if( !top )
        return 0;
    while( (entry = readdir(top)) != NULL && count < SITE_RECORDINGS_MAX )
    {
        char dir[1280];
        DIR* files;
        struct dirent* file;

        if( !recording_account_ok(entry->d_name) )
            continue;
        if( account[0] && strcmp(account, entry->d_name) != 0 )
            continue;
        snprintf(dir, sizeof(dir), "%s/%s", root, entry->d_name);
        files = opendir(dir);
        if( !files )
            continue;
        while( (file = readdir(files)) != NULL && count < SITE_RECORDINGS_MAX )
        {
            char path[1600];
            struct stat st;
            long long start = recording_file_start(file->d_name);
            struct SiteRecording* r;

            if( start < 0 )
                continue;
            snprintf(path, sizeof(path), "%s/%s", dir, file->d_name);
            if( stat(path, &st) != 0 )
                continue;
            r = &list[count++];
            snprintf(r->account, sizeof(r->account), "%s", entry->d_name);
            snprintf(r->file, sizeof(r->file), "%s", file->d_name);
            r->start = start;
            r->last = (long long)st.st_mtime;
            r->size = (long long)st.st_size;
        }
        closedir(files);
    }
    closedir(top);
    qsort(list, (size_t)count, sizeof(*list), recording_cmp);
    return count;
}

static void
buf_bytes(
    struct SiteBuf* b,
    long long n)
{
    if( n < 1024 * 1024 )
        buf_printf(b, "%lld KB", (n + 1023) / 1024);
    else
        buf_printf(b, "%.1f MB", (double)n / (1024.0 * 1024.0));
}

static void
page_staff_only(
    struct IoGate* gate,
    char const* who,
    struct HttpResponse* res)
{
    struct SiteBuf b;

    page_begin(&b, gate, who, "Staff only", "");
    buf_puts(&b, "<section class=\"panel\" style=\"max-width:560px\"><h2>Staff only</h2>"
                 "<p>This page is for staff: a web login with a linked player account that is "
                 "an admin.</p></section>");
    page_send(&b, res, 403);
}

/* One player account's row in the "who is recorded" table. */
struct SiteRecordedAccount
{
    char key[SITE_NAME_MAX];
    int record;
};

static int
recorded_account_cmp(
    void const* a,
    void const* b)
{
    struct SiteRecordedAccount const* x = a;
    struct SiteRecordedAccount const* y = b;

    if( x->record != y->record )
        return y->record - x->record;
    return strcmp(x->key, y->key);
}

static void
page_recordings(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    struct SiteBuf b;
    struct SiteRecording* list;
    char account[SITE_NAME_MAX] = "";
    char done[16] = "";
    char player[SITE_NAME_MAX] = "";
    char accounts_dir[1024];
    struct SiteRecordedAccount* accounts =
        ToriMem_Alloc(ToriMem_Net(), 1024 * sizeof(*accounts));
    int account_count = 0;
    DIR* dir;
    struct dirent* entry;
    long long total = 0;
    long long now = (long long)time(NULL);
    int count;

    assert(accounts);
    IoGate_QueryField(req, "account", account, sizeof(account));
    if( account[0] && !recording_account_ok(account) )
        account[0] = '\0';
    IoGate_QueryField(req, "done", done, sizeof(done));
    IoGate_QueryField(req, "player", player, sizeof(player));
    count = recordings_list(account, &list);

    snprintf(accounts_dir, sizeof(accounts_dir), "%s/accounts", ToriRSServer_SaveDir());
    dir = opendir(accounts_dir);
    if( dir )
    {
        while( (entry = readdir(dir)) != NULL && account_count < 1024 )
        {
            size_t len = strlen(entry->d_name);
            struct SiteRecordedAccount* a;

            if( len <= 4 || len - 4 >= SITE_NAME_MAX ||
                strcmp(entry->d_name + len - 4, ".ini") != 0 )
                continue;
            a = &accounts[account_count++];
            snprintf(a->key, sizeof(a->key), "%.*s", (int)(len - 4), entry->d_name);
            a->record = ToriRSServer_AuthIsRecorded(a->key);
        }
        closedir(dir);
    }
    qsort(accounts, (size_t)account_count, sizeof(*accounts), recorded_account_cmp);

    page_begin(&b, gate, who, "Recordings", "/admin/recordings");
    buf_puts(&b, "<div class=\"title\"><h1>Recordings<small>every packet of a recorded "
                 "account's sessions, both ways</small></h1></div>\n");
    if( done[0] && player[0] )
    {
        buf_puts(&b, "<p class=\"flash ok\">Recording ");
        buf_esc(&b, strcmp(done, "on") == 0 ? "on" : "off");
        buf_puts(&b, " for <b>");
        buf_esc(&b, player);
        buf_puts(&b, "</b>, from its next login.</p>");
    }
    buf_puts(&b, "<div class=\"stack\">");

    /* The sessions. */
    buf_puts(&b, "<section class=\"panel\"><h2>Sessions");
    if( account[0] )
    {
        buf_puts(&b, " of ");
        buf_esc(&b, account);
        buf_puts(&b, " <a class=\"note\" href=\"/admin/recordings\">(all)</a>");
    }
    buf_puts(&b, "</h2>");
    if( count == 0 )
        buf_puts(&b, "<p>No recordings yet. Switch an account on below; its next login is "
                     "recorded.</p>");
    else
    {
        buf_puts(&b, "<div class=\"scroll\"><table><thead><tr><th>Account</th><th>Started</th>"
                     "<th class=\"num\">Length</th><th class=\"num\">Size</th><th></th>"
                     "</tr></thead><tbody>");
        for( int i = 0; i < count; i++ )
        {
            struct SiteRecording const* r = &list[i];
            long long length = r->last - r->start;

            total += r->size;
            buf_puts(&b, "<tr><td><a href=\"/admin/recordings?account=");
            buf_url(&b, r->account);
            buf_puts(&b, "\">");
            buf_esc(&b, r->account);
            buf_puts(&b, "</a></td><td class=\"when\">");
            buf_when(&b, r->start);
            buf_puts(&b, "</td><td class=\"num\">");
            buf_duration(&b, length > 0 ? length : 0);
            /* Written in the last minute: probably still being recorded. */
            if( now - r->last < 60 )
                buf_puts(&b, " <span class=\"badge\">live</span>");
            buf_puts(&b, "</td><td class=\"num\">");
            buf_bytes(&b, r->size);
            buf_puts(&b, "</td><td class=\"row\" style=\"flex-wrap:nowrap\">"
                         "<a href=\"/admin/replay?f=");
            buf_url(&b, r->account);
            buf_puts(&b, "/");
            buf_url(&b, r->file);
            buf_puts(&b, "\">Watch</a><a href=\"/admin/recordings/file/");
            buf_esc(&b, r->account);
            buf_puts(&b, "/");
            buf_esc(&b, r->file);
            buf_puts(&b, "\" download>.bin</a></td></tr>");
        }
        buf_puts(&b, "</tbody></table></div><p class=\"note\" style=\"margin-top:12px\">");
        buf_printf(&b, "%d recording%s, ", count, count == 1 ? "" : "s");
        buf_bytes(&b, total);
        buf_puts(&b, " on disk. A .bin is an RSProx recording: the rsprox tools read it "
                     "too.</p>");
    }
    buf_puts(&b, "</section>");

    /* Who is recorded. */
    buf_puts(&b, "<section class=\"panel\"><h2>Recorded accounts</h2>"
                 "<p class=\"note\">Read at each login: switching takes effect the next time "
                 "the player logs in. The same switch is <code>./torirsctl record &lt;name&gt; "
                 "on|off</code>.</p><div class=\"scroll\"><table><thead><tr><th>Player "
                 "account</th><th>Recording</th><th></th></tr></thead><tbody>");
    for( int i = 0; i < account_count; i++ )
    {
        struct SiteRecordedAccount const* a = &accounts[i];

        buf_puts(&b, "<tr><td>");
        buf_esc(&b, a->key);
        buf_puts(&b, "</td><td>");
        buf_puts(&b, a->record ? "<b>on</b>" : "off");
        buf_puts(&b, "</td><td><form method=\"post\" action=\"/admin/recordings/record\" "
                     "style=\"margin:0\"><input type=\"hidden\" name=\"player\" value=\"");
        buf_esc(&b, a->key);
        buf_printf(&b, "\"><input type=\"hidden\" name=\"state\" value=\"%s\">"
                       "<button class=\"btn%s\">Turn %s</button></form></td></tr>",
                   a->record ? "off" : "on", a->record ? " quiet" : "",
                   a->record ? "off" : "on");
    }
    buf_puts(&b, "</tbody></table></div></section></div>");
    ToriMem_Free(ToriMem_Net(), accounts);
    ToriMem_Free(ToriMem_Net(), list);
    page_send(&b, res, 200);
}

/* `/admin/recordings/file/<account>/<file>`: the bytes. */
static void
serve_recording(
    char const* rest,
    struct HttpResponse* res)
{
    char account[SITE_NAME_MAX];
    char file[32];
    char root[1024];
    char path[1600];
    size_t slash = strcspn(rest, "/");
    FILE* f;
    long size;
    uint8_t* data;

    res->status = 404;
    if( slash == 0 || slash >= sizeof(account) || rest[slash] != '/' )
        return;
    snprintf(account, sizeof(account), "%.*s", (int)slash, rest);
    snprintf(file, sizeof(file), "%.*s", (int)strcspn(rest + slash + 1, "?#"), rest + slash + 1);
    if( !recording_account_ok(account) || recording_file_start(file) < 0 )
        return;
    recordings_dir(root, sizeof(root));
    snprintf(path, sizeof(path), "%s/%s/%s", root, account, file);
    f = fopen(path, "rb");
    if( !f )
        return;
    if( fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 )
    {
        fclose(f);
        return;
    }
    data = ToriMem_Alloc(ToriMem_Net(), (size_t)(size > 0 ? size : 1));
    assert(data);
    /* A recording still being written grows while this reads: the length
     * read is what is sent, and it ends on a record boundary because the
     * server writes whole records. */
    size = (long)fread(data, 1, (size_t)size, f);
    fclose(f);
    res->status = 200;
    snprintf(res->content_type, sizeof(res->content_type), "application/octet-stream");
    res->body = data;
    res->body_len = (int)size;
    res->owns_body = 1;
}

/* `/admin/replay?f=<account>/<file>`: the game page in replay mode, framed,
 * with a scrubber over its ToriRS_Replay controls (web/torirs_host.js). */
static void
page_replay(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    struct SiteBuf b;
    char f[96] = "";
    char account[SITE_NAME_MAX];
    char const* file;
    size_t slash;

    IoGate_QueryField(req, "f", f, sizeof(f));
    slash = strcspn(f, "/");
    if( slash == 0 || slash >= sizeof(account) || f[slash] != '/' )
    {
        redirect(res, "/admin/recordings");
        return;
    }
    snprintf(account, sizeof(account), "%.*s", (int)slash, f);
    file = f + slash + 1;
    if( !recording_account_ok(account) || recording_file_start(file) < 0 )
    {
        redirect(res, "/admin/recordings");
        return;
    }

    page_begin(&b, gate, who, "Replay", "/admin/recordings");
    buf_puts(&b, "<style>main{max-width:1400px}"
                 ".replay{display:block;width:100%;aspect-ratio:4/3;max-height:80vh;"
                 "border:2px solid var(--edge);border-radius:4px;background:#000}"
                 ".scrub{display:flex;flex-wrap:wrap;gap:8px 12px;align-items:center;margin:12px 0}"
                 ".scrub input[type=range]{flex:1;min-width:200px;max-width:none;margin:0}"
                 ".scrub .tick{font-variant-numeric:tabular-nums;min-width:150px}"
                 ".scrub input[type=number]{width:110px;margin:0;display:inline-block}</style>");
    buf_puts(&b, "<div class=\"title\"><h1>Replay<small>");
    buf_esc(&b, account);
    buf_puts(&b, " &middot; ");
    buf_when(&b, recording_file_start(file));
    buf_puts(&b, " &middot; <a href=\"/admin/recordings?account=");
    buf_url(&b, account);
    buf_puts(&b, "\">back</a></small></h1></div>");
    buf_puts(&b, "<iframe class=\"replay\" id=\"replay\" allow=\"autoplay; fullscreen\" "
                 "title=\"Replay\"></iframe>"
                 "<div class=\"scrub\">"
                 "<button class=\"btn\" id=\"play\" disabled>Pause</button>"
                 "<label style=\"margin:0\"><input type=\"checkbox\" id=\"fast\" "
                 "style=\"display:inline;width:auto\"> Fast</label>"
                 "<input type=\"range\" id=\"pos\" min=\"0\" max=\"0\" value=\"0\" disabled>"
                 "<span class=\"tick\" id=\"tick\">loading&hellip;</span>"
                 "<form id=\"go\" style=\"margin:0\" class=\"row\"><input type=\"number\" "
                 "id=\"to\" min=\"0\" placeholder=\"tick\"><button class=\"btn quiet\">Go"
                 "</button></form></div>"
                 "<p class=\"note\">A tick is 0.6 s. Seeking replays the session from the "
                 "nearest checkpoint, so a far jump forward takes a moment the first time. "
                 "The player's own clicks are not shown, only what the server sent "
                 "back.</p>");
    buf_puts(&b, "<script>(function(){\n"
                 "var file='/admin/recordings/file/");
    buf_esc(&b, account);
    buf_puts(&b, "/");
    buf_esc(&b, file);
    buf_puts(&b, "';\n"
        "var gl=false;try{gl=!!document.createElement('canvas').getContext('webgl2');}catch(e){}\n"
        "var q=new URLSearchParams({replay:file,args:'--manifest,manifests/manifest_replay.ini'"
        "+(gl?',--webgl2-zbuffer':''),ws:'ws'});\n"
        "var frame=document.getElementById('replay');frame.src='/?'+q;\n"
        "var play=document.getElementById('play'),fast=document.getElementById('fast'),"
        "pos=document.getElementById('pos'),tick=document.getElementById('tick'),"
        "go=document.getElementById('go'),to=document.getElementById('to');\n"
        "var dragging=false;\n"
        "function ctl(){try{return frame.contentWindow&&frame.contentWindow.ToriRS_Replay;}"
        "catch(e){return null;}}\n"
        "function secs(t){var s=Math.round(t*0.6),m=Math.floor(s/60),h=Math.floor(m/60);"
        "return (h?h+':'+String(m%60).padStart(2,'0'):m)+':'+String(s%60).padStart(2,'0');}\n"
        "function seek(t){var c=ctl();if(c)c.seek(Math.max(0,t|0));}\n"
        "play.onclick=function(){var c=ctl();if(!c)return;var st=c.status();if(!st)return;"
        "if(st.held)c.run();else c.hold();};\n"
        "fast.onchange=function(){var c=ctl();if(c)c.setUnlocked(fast.checked);};\n"
        "pos.oninput=function(){dragging=true;tick.textContent='tick '+pos.value+' ('+secs(+pos.value)+')';};\n"
        "pos.onchange=function(){dragging=false;seek(+pos.value);};\n"
        "go.onsubmit=function(e){e.preventDefault();if(to.value!=='')seek(+to.value);};\n"
        "setInterval(function(){var c=ctl();var st=null;try{st=c&&c.status();}catch(e){}\n"
        "if(!st){return;}\n"
        "play.disabled=false;pos.disabled=false;pos.max=st.ticks;\n"
        "if(!dragging)pos.value=st.tick;\n"
        "play.textContent=st.held?'Play':'Pause';fast.checked=!!st.unlocked;\n"
        "if(!dragging)tick.textContent='tick '+st.tick+' / '+st.ticks+' ('+secs(st.tick)+' / '"
        "+secs(st.ticks)+')'+(st.seeking?' seeking\\u2026':'');},250);\n"
        "})();</script>");
    page_send(&b, res, 200);
}

/* ------------------------------------------------------------------ */
/* Staff: users                                                        */
/* ------------------------------------------------------------------ */
/*
 * Every web login with the player accounts linked to it; a player account
 * made for any of them (admin or not); and Sign in as, which turns this
 * browser into that login until Return (io_gate.h IoGate_SignInAs) -- its
 * account page, and its Play Now in the game, which plays its accounts.
 */

static void
page_users(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res,
    int status,
    char const* error,
    char const* typed_owner,
    char const* typed_name,
    int typed_admin)
{
    struct SiteBuf b;
    char done[16] = "";
    char player[SITE_NAME_MAX] = "";
    char owner[SITE_NAME_MAX] = "";
    char selected[SITE_NAME_MAX];
    int logins = site_logins();

    if( logins < 0 )
    {
        unavailable_page(gate, who, "Users", "/admin/users", res);
        return;
    }
    ToriRSServer_SaveFileName(typed_owner && typed_owner[0] ? typed_owner : who, selected,
                              sizeof(selected));
    page_begin(&b, gate, who, "Users", "/admin/users");
    buf_puts(&b, "<div class=\"title\"><h1>Users<small>web logins and the player accounts "
                 "under them</small></h1></div>\n");
    if( error )
    {
        buf_puts(&b, "<p class=\"flash bad\">");
        buf_esc(&b, error);
        buf_puts(&b, "</p>");
    }
    else if( IoGate_QueryField(req, "done", done, sizeof(done)) &&
             IoGate_QueryField(req, "player", player, sizeof(player)) &&
             IoGate_QueryField(req, "owner", owner, sizeof(owner)) && strcmp(done, "created") == 0 )
    {
        buf_puts(&b, "<p class=\"flash ok\"><b>");
        buf_esc(&b, player);
        buf_puts(&b, "</b> is made and linked to <b>");
        buf_esc(&b, owner);
        buf_puts(&b, "</b>.</p>");
    }

    buf_puts(&b, "<div class=\"grid\">\n<section class=\"panel\"><h2>Web logins</h2>"
                 "<div class=\"scroll\"><table><thead><tr><th>Login</th><th>Player accounts</th>"
                 "<th></th></tr></thead><tbody>");
    for( int i = 0; i < logins; i++ )
    {
        char key[SITE_NAME_MAX];
        char names[IO_GATE_LINKS_LIST_MAX][IO_GATE_LINKS_NAME_MAX];
        int linked;

        ToriRSServer_SaveFileName(g_logins[i], key, sizeof(key));
        if( !key[0] )
            continue;
        linked = IoGateLinks_List(gate->links, key, names, IO_GATE_LINKS_LIST_MAX);
        buf_puts(&b, "<tr><td><b>");
        buf_esc(&b, g_logins[i]);
        buf_puts(&b, "</b></td><td>");
        if( linked == 0 )
            buf_puts(&b, "<span class=\"note\">none</span>");
        for( int n = 0; n < linked; n++ )
        {
            char player_key[SITE_NAME_MAX];

            ToriRSServer_SaveFileName(names[n], player_key, sizeof(player_key));
            if( n > 0 )
                buf_puts(&b, ", ");
            player_link_to(&b, player_key, names[n]);
            if( ToriRSServer_AuthIsAdmin(names[n]) )
                buf_puts(&b, "&nbsp;<span class=\"note\">(admin)</span>");
        }
        buf_puts(&b, "</td><td>");
        if( strcmp(key, who) == 0 )
            buf_puts(&b, "<span class=\"note\">you</span>");
        else
        {
            buf_puts(&b, "<form method=\"post\" action=\"/admin/users/sign-in-as\" "
                         "style=\"margin:0\"><input type=\"hidden\" name=\"login\" value=\"");
            buf_esc(&b, key);
            buf_puts(&b, "\"><button class=\"btn quiet\" style=\"white-space:nowrap\">Sign in as"
                         "</button></form>");
        }
        buf_puts(&b, "</td></tr>");
    }
    buf_puts(&b, "</tbody></table></div><p class=\"note\" style=\"margin-top:12px\">Sign in as "
                 "makes this browser that login until you press Return in the bar at the top: "
                 "its account page, and its Play Now in the game, which plays its linked "
                 "accounts. The game's own staff powers come from the player account played, "
                 "not from you.</p><p class=\"note\">To link an existing player account to a "
                 "login, or move one, use <a href=\"/admin/players\">Players</a>.</p>"
                 "</section>\n");

    buf_puts(&b, "<section class=\"panel\"><h2>Create a player account</h2>"
                 "<form method=\"post\" action=\"/admin/users/create\">"
                 "<label>For the login<select name=\"owner\">");
    for( int i = 0; i < logins; i++ )
    {
        char key[SITE_NAME_MAX];

        ToriRSServer_SaveFileName(g_logins[i], key, sizeof(key));
        if( !key[0] )
            continue;
        buf_puts(&b, "<option value=\"");
        buf_esc(&b, key);
        buf_printf(&b, "\"%s>", strcmp(key, selected) == 0 ? " selected" : "");
        buf_esc(&b, g_logins[i]);
        if( strcmp(key, who) == 0 )
            buf_puts(&b, " (you)");
        buf_puts(&b, "</option>");
    }
    buf_puts(&b, "</select></label>");
    create_form_fields(&b, typed_name, 1, typed_admin);
    buf_puts(&b, "<button class=\"btn\">Create account</button></form>"
                 "<p class=\"note\" style=\"margin-top:12px\">The account is linked to the login "
                 "chosen: it plays from that login's Play Now and shows on its account page. No "
                 "password needed.</p></section>\n</div>\n");
    page_send(&b, res, status);
}

/* ------------------------------------------------------------------ */
/* Staff: player accounts                                              */
/* ------------------------------------------------------------------ */
/*
 * Every player account (an .ini in <saves>/accounts/) with the web login it is
 * linked to, and per row a login to link it to -- no player password: staff
 * decide, and an account linked elsewhere moves (IoGate_AssignPlayer) -- or
 * Unlink.
 */

#define SITE_PLAYER_ACCOUNTS_MAX 4096

struct SitePlayerAccount
{
    char key[SITE_NAME_MAX];
    char owner[SITE_NAME_MAX];
    /** ToriRSServer_AuthManagedBy's: 1 linked, 0 not, -1 unreadable. */
    int linked;
    int admin;
};

static int
player_account_cmp(
    void const* a,
    void const* b)
{
    return strcmp(((struct SitePlayerAccount const*)a)->key,
                  ((struct SitePlayerAccount const*)b)->key);
}

/* The accounts whose key holds `filter` ("" every one), sorted; the caller
 * frees *out (ToriMem_Net). */
static int
player_accounts_list(
    char const* filter,
    struct SitePlayerAccount** out)
{
    char dir_path[1024];
    DIR* dir;
    struct dirent* entry;
    struct SitePlayerAccount* list =
        ToriMem_Alloc(ToriMem_Net(), SITE_PLAYER_ACCOUNTS_MAX * sizeof(*list));
    int count = 0;

    assert(list);
    *out = list;
    snprintf(dir_path, sizeof(dir_path), "%s/accounts", ToriRSServer_SaveDir());
    dir = opendir(dir_path);
    if( !dir )
        return 0;
    while( (entry = readdir(dir)) != NULL && count < SITE_PLAYER_ACCOUNTS_MAX )
    {
        size_t len = strlen(entry->d_name);
        struct SitePlayerAccount* a;

        if( len <= 4 || len - 4 >= SITE_NAME_MAX || strcmp(entry->d_name + len - 4, ".ini") != 0 )
            continue;
        a = &list[count];
        snprintf(a->key, sizeof(a->key), "%.*s", (int)(len - 4), entry->d_name);
        if( !recording_account_ok(a->key) || (filter[0] && !strstr(a->key, filter)) )
            continue;
        a->linked = ToriRSServer_AuthManagedBy(a->key, a->owner, sizeof(a->owner));
        if( a->linked != 1 )
            a->owner[0] = '\0';
        a->admin = ToriRSServer_AuthIsAdmin(a->key);
        count++;
    }
    closedir(dir);
    qsort(list, (size_t)count, sizeof(*list), player_account_cmp);
    return count;
}

/* `/admin/players?done=..&player=..&login=..[&q=..]` */
static void
redirect_players(
    struct HttpResponse* res,
    char const* done,
    char const* player,
    char const* login,
    char const* filter)
{
    struct SiteBuf to;

    buf_init(&to);
    buf_printf(&to, "/admin/players?done=%s&player=", done);
    buf_url(&to, player);
    buf_puts(&to, "&login=");
    buf_url(&to, login);
    if( filter[0] )
    {
        buf_puts(&to, "&q=");
        buf_url(&to, filter);
    }
    redirect(res, to.data);
    ToriMem_Free(ToriMem_Net(), to.data);
}

static void
page_players(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res,
    int status,
    char const* error)
{
    struct SiteBuf b;
    struct SitePlayerAccount* list;
    char q[SITE_NAME_MAX] = "";
    char filter[SITE_NAME_MAX] = "";
    char done[16] = "";
    char player[SITE_NAME_MAX] = "";
    char login[SITE_NAME_MAX] = "";
    int logins = site_logins();
    int count;

    if( logins < 0 )
    {
        unavailable_page(gate, who, "Player accounts", "/admin/players", res);
        return;
    }
    /* The filter from the query, or from a POST's hidden field on an error. */
    if( !IoGate_QueryField(req, "q", q, sizeof(q)) )
        IoGate_FormField(req, "q", q, sizeof(q));
    ToriRSServer_SaveFileName(q, filter, sizeof(filter));
    count = player_accounts_list(filter, &list);

    page_begin(&b, gate, who, "Player accounts", "/admin/players");
    buf_puts(&b, "<div class=\"title\"><h1>Player accounts<small>which login each one plays "
                 "from</small></h1></div>\n");
    if( error )
    {
        buf_puts(&b, "<p class=\"flash bad\">");
        buf_esc(&b, error);
        buf_puts(&b, "</p>");
    }
    else if( IoGate_QueryField(req, "done", done, sizeof(done)) &&
             IoGate_QueryField(req, "player", player, sizeof(player)) )
    {
        IoGate_QueryField(req, "login", login, sizeof(login));
        buf_puts(&b, "<p class=\"flash ok\"><b>");
        buf_esc(&b, player);
        if( strcmp(done, "linked") == 0 )
        {
            buf_puts(&b, "</b> is linked to <b>");
            buf_esc(&b, login);
            buf_puts(&b, "</b>: it plays from that login's Play Now.</p>");
        }
        else
        {
            buf_puts(&b, "</b> is unlinked from <b>");
            buf_esc(&b, login);
            buf_puts(&b, "</b>.</p>");
        }
    }

    buf_puts(&b, "<section class=\"panel\"><form class=\"search\" method=\"get\" "
                 "action=\"/admin/players\"><input name=\"q\" placeholder=\"Find a player "
                 "account\" maxlength=\"12\" aria-label=\"Player account\" value=\"");
    buf_esc(&b, q);
    buf_puts(&b, "\"><button class=\"btn quiet\">Find</button>");
    if( q[0] )
        buf_puts(&b, "<a href=\"/admin/players\" style=\"align-self:center\">All</a>");
    buf_puts(&b, "</form>");
    if( count == 0 )
        buf_puts(&b, q[0] ? "<p>No player account matches.</p>" : "<p>No player accounts yet.</p>");
    else
    {
        buf_puts(&b, "<div class=\"scroll\"><table><thead><tr><th>Player account</th>"
                     "<th>Linked to</th><th>Link to a login</th><th></th></tr></thead><tbody>");
        for( int i = 0; i < count; i++ )
        {
            struct SitePlayerAccount const* a = &list[i];
            char shown[SITE_NAME_MAX];

            /* The spelling it plays under where it was ever linked, else the key. */
            if( !IoGateLinks_Display(gate->links, a->key, shown, sizeof(shown)) )
                snprintf(shown, sizeof(shown), "%s", a->key);
            buf_puts(&b, "<tr><td>");
            player_link_to(&b, a->key, shown);
            if( a->admin )
                buf_puts(&b, "&nbsp;<span class=\"note\">(admin)</span>");
            buf_puts(&b, "</td><td>");
            if( a->linked == 1 )
            {
                buf_puts(&b, "<b>");
                buf_esc(&b, a->owner);
                buf_puts(&b, "</b>");
            }
            else if( a->linked < 0 )
                buf_puts(&b, "<span class=\"note\">record unreadable</span>");
            else
                buf_puts(&b, "<span class=\"note\">&ndash;</span>");
            buf_puts(&b, "</td><td><form method=\"post\" action=\"/admin/players/link\" "
                         "class=\"row\" style=\"margin:0;flex-wrap:nowrap;gap:8px\">"
                         "<input type=\"hidden\" name=\"player\" value=\"");
            buf_esc(&b, a->key);
            buf_puts(&b, "\"><input type=\"hidden\" name=\"q\" value=\"");
            buf_esc(&b, q);
            buf_puts(&b, "\"><select name=\"login\" aria-label=\"Login\" "
                         "style=\"margin:0;min-width:120px\">");
            if( a->linked != 1 )
                buf_puts(&b, "<option value=\"\" selected disabled>choose&hellip;</option>");
            for( int l = 0; l < logins; l++ )
            {
                char key[SITE_NAME_MAX];

                ToriRSServer_SaveFileName(g_logins[l], key, sizeof(key));
                if( !key[0] )
                    continue;
                buf_puts(&b, "<option value=\"");
                buf_esc(&b, key);
                buf_printf(&b, "\"%s>", a->linked == 1 && strcmp(key, a->owner) == 0
                                            ? " selected"
                                            : "");
                buf_esc(&b, g_logins[l]);
                buf_puts(&b, "</option>");
            }
            buf_printf(&b, "</select><button class=\"btn quiet\">%s</button></form></td><td>",
                       a->linked == 1 ? "Move" : "Link");
            if( a->linked == 1 )
            {
                buf_puts(&b, "<form method=\"post\" action=\"/admin/players/unlink\" "
                             "style=\"margin:0\"><input type=\"hidden\" name=\"player\" "
                             "value=\"");
                buf_esc(&b, a->key);
                buf_puts(&b, "\"><input type=\"hidden\" name=\"q\" value=\"");
                buf_esc(&b, q);
                buf_puts(&b, "\"><button class=\"linkbtn\" style=\"color:#7a2a12\">Unlink"
                             "</button></form>");
            }
            buf_puts(&b, "</td></tr>");
        }
        buf_puts(&b, "</tbody></table></div>");
    }
    buf_puts(&b, "<p class=\"note\" style=\"margin-top:12px\">Linking needs no player password: "
                 "the account then plays only from that login's Play Now, and an account linked "
                 "to another login moves. Unlinking gives the account its own password back; "
                 "one made on this site has none anyone knows, so set one with "
                 "<code>./torirsctl password &lt;name&gt;</code> first.</p></section>\n");
    ToriMem_Free(ToriMem_Net(), list);
    page_send(&b, res, status);
}

/* POST /admin/players/link: {player, login, q}. */
static void
handle_players_link(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char player[SITE_NAME_MAX] = "";
    char login[SITE_NAME_MAX] = "";
    char key[SITE_NAME_MAX];
    char q[SITE_NAME_MAX] = "";
    char const* error = NULL;

    IoGate_FormField(req, "player", player, sizeof(player));
    IoGate_FormField(req, "login", login, sizeof(login));
    IoGate_FormField(req, "q", q, sizeof(q));
    ToriRSServer_SaveFileName(login, key, sizeof(key));
    if( site_logins() < 0 )
    {
        unavailable_page(gate, who, "Player accounts", "/admin/players", res);
        return;
    }
    if( !key[0] || site_login_find(key) < 0 )
        error = "Choose a login to link it to.";
    else if( IoGate_AssignPlayer(gate, key, player, &error) == IO_GATE_LINK_LINKED )
    {
        fprintf(stderr, "io_server: '%s' linked player account '%s' to '%s'\n", who, player,
                key);
        redirect_players(res, "linked", player, key, q);
        return;
    }
    page_players(gate, who, req, res, 400, error);
}

/* POST /admin/players/unlink: {player, q}. */
static void
handle_players_unlink(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char player[SITE_NAME_MAX] = "";
    char owner[SITE_NAME_MAX] = "";
    char q[SITE_NAME_MAX] = "";

    IoGate_FormField(req, "player", player, sizeof(player));
    IoGate_FormField(req, "q", q, sizeof(q));
    if( !player[0] || ToriRSServer_AuthManagedBy(player, owner, sizeof(owner)) != 1 ||
        !IoGate_UnlinkPlayer(gate, owner, player) )
    {
        page_players(gate, who, req, res, 400, "That account is not linked to a login.");
        return;
    }
    fprintf(stderr, "io_server: '%s' unlinked player account '%s' from '%s'\n", who, player,
            owner);
    redirect_players(res, "unlinked", player, owner, q);
}

/* POST /account/create and /admin/users/create: the form's fields to
 * IoGate_CreatePlayer, for `who` itself or, from staff, for the login named
 * in `owner`. */
static void
handle_create(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res,
    int staff_form)
{
    char owner[SITE_NAME_MAX] = "";
    char owner_key[SITE_NAME_MAX] = "";
    char username[SITE_NAME_MAX] = "";
    char admin_field[8] = "";
    char const* error = NULL;
    int staff = site_is_staff(gate, who);
    int admin;
    int status = 400;

    IoGate_FormField(req, "username", username, sizeof(username));
    IoGate_FormField(req, "admin", admin_field, sizeof(admin_field));
    admin = strcmp(admin_field, "1") == 0;
    if( staff_form )
    {
        IoGate_FormField(req, "owner", owner, sizeof(owner));
        ToriRSServer_SaveFileName(owner, owner_key, sizeof(owner_key));
        if( site_logins() < 0 )
        {
            error = "The account service is not answering. Try again in a moment.";
            status = 503;
        }
        else if( !owner_key[0] || site_login_find(owner_key) < 0 )
            error = "There is no web login by that name.";
    }
    else
        snprintf(owner_key, sizeof(owner_key), "%s", who);
    if( !error && admin && !staff )
    {
        error = "Only staff can make an admin account.";
        status = 403;
    }
    if( !error )
    {
        enum IoGateLinkResult result =
            IoGate_CreatePlayer(gate, owner_key, username, admin, &error);

        if( result == IO_GATE_LINK_LINKED )
        {
            struct SiteBuf to;

            if( staff_form )
                fprintf(stderr, "io_server: '%s' made player account '%s' for '%s'\n", who,
                        username, owner_key);
            buf_init(&to);
            if( staff_form )
            {
                buf_puts(&to, "/admin/users?done=created&player=");
                buf_url(&to, username);
                buf_puts(&to, "&owner=");
                buf_url(&to, owner_key);
            }
            else
            {
                buf_puts(&to, "/account?done=created&player=");
                buf_url(&to, username);
            }
            redirect(res, to.data);
            ToriMem_Free(ToriMem_Net(), to.data);
            return;
        }
        status = result == IO_GATE_LINK_FAILED ? 500 : 400;
    }
    if( staff_form )
        page_users(gate, who, req, res, status, error, owner_key, username, admin);
    else
        page_account(gate, who, req, res, status, error, NULL, username, admin);
}

/* ------------------------------------------------------------------ */
/* Routes                                                              */
/* ------------------------------------------------------------------ */

int
IoSite_Handle(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    int is_get;
    int is_post;

    assert(gate);
    assert(who);
    assert(req);
    assert(res);
    is_get = strcmp(req->method, "GET") == 0 || strcmp(req->method, "HEAD") == 0;
    is_post = strcmp(req->method, "POST") == 0;
    if( !IoGate_StaffBehind(req, g_staff_behind, sizeof(g_staff_behind)) ||
        strcmp(g_staff_behind, who) == 0 )
        g_staff_behind[0] = '\0';

    /* The bare "/" only: "/?args=..." is the game's page. */
    if( is_get && strcmp(req->path, "/") == 0 )
    {
        page_home(gate, who, res);
        return 1;
    }
    if( is_get && IoGate_PathIs(req->path, "/hiscores") )
    {
        page_hiscores(gate, who, req, res);
        return 1;
    }
    if( is_get && IoGate_PathIs(req->path, "/players") )
    {
        char name[SITE_NAME_MAX] = "";
        char key[SITE_NAME_MAX];

        IoGate_QueryField(req, "name", name, sizeof(name));
        ToriRSServer_SaveFileName(name, key, sizeof(key));
        if( key[0] )
        {
            char location[SITE_NAME_MAX + 16];

            snprintf(location, sizeof(location), "/players/%s", key);
            redirect(res, location);
        }
        else
            redirect(res, "/hiscores");
        return 1;
    }
    if( is_get && strncmp(req->path, "/players/", 9) == 0 )
    {
        char key[SITE_NAME_MAX];

        snprintf(key, sizeof(key), "%.*s", (int)strcspn(req->path + 9, "?#"), req->path + 9);
        page_player(gate, who, key, res);
        return 1;
    }
    if( is_get && IoGate_PathIs(req->path, "/account") )
    {
        page_account(gate, who, req, res, 200, NULL, NULL, NULL, 0);
        return 1;
    }
    if( IoGate_PathIs(req->path, "/account/create") )
    {
        if( !is_post )
            redirect(res, "/account");
        else
            handle_create(gate, who, req, res, 0);
        return 1;
    }
    /* Staff back from Sign in as: the staff cookie is their own ticket, so
     * this only ever restores the login that set it. */
    if( IoGate_PathIs(req->path, "/account/return") )
    {
        if( is_post && g_staff_behind[0] )
            IoGate_SignInBack(res, g_staff_behind, who);
        else
            redirect(res, "/");
        return 1;
    }
    if( IoGate_PathIs(req->path, "/account/link-player") )
    {
        char username[SITE_NAME_MAX] = "";
        char password[128] = "";
        char const* message = NULL;

        if( !is_post )
        {
            redirect(res, "/account");
            return 1;
        }
        IoGate_FormField(req, "username", username, sizeof(username));
        IoGate_FormField(req, "password", password, sizeof(password));
        if( IoGate_LinkPlayer(gate, who, username, password, sizeof(password), &message) ==
            IO_GATE_LINK_LINKED )
            redirect_done(res, "linked", username);
        else
            page_account(gate, who, req, res, 400, message, username, NULL, 0);
        return 1;
    }
    if( IoGate_PathIs(req->path, "/account/unlink") )
    {
        char player[SITE_NAME_MAX] = "";

        if( is_get )
        {
            page_unlink_confirm(gate, who, req, res);
            return 1;
        }
        if( !is_post )
            return 0;
        IoGate_FormField(req, "player", player, sizeof(player));
        if( IoGate_UnlinkPlayer(gate, who, player) )
            redirect_done(res, "unlinked", player);
        else
            page_account(gate, who, req, res, 400, "That account is not linked to this login.",
                         NULL, NULL, 0);
        return 1;
    }
    if( strncmp(req->path, "/admin/", 7) == 0 )
    {
        if( !site_is_staff(gate, who) )
        {
            page_staff_only(gate, who, res);
            return 1;
        }
        if( is_get && IoGate_PathIs(req->path, "/admin/users") )
        {
            page_users(gate, who, req, res, 200, NULL, NULL, NULL, 0);
            return 1;
        }
        if( is_post && IoGate_PathIs(req->path, "/admin/users/create") )
        {
            handle_create(gate, who, req, res, 1);
            return 1;
        }
        if( is_get && IoGate_PathIs(req->path, "/admin/players") )
        {
            page_players(gate, who, req, res, 200, NULL);
            return 1;
        }
        if( is_post && IoGate_PathIs(req->path, "/admin/players/link") )
        {
            handle_players_link(gate, who, req, res);
            return 1;
        }
        if( is_post && IoGate_PathIs(req->path, "/admin/players/unlink") )
        {
            handle_players_unlink(gate, who, req, res);
            return 1;
        }
        if( is_post && IoGate_PathIs(req->path, "/admin/users/sign-in-as") )
        {
            char login[SITE_NAME_MAX] = "";
            char key[SITE_NAME_MAX];

            IoGate_FormField(req, "login", login, sizeof(login));
            ToriRSServer_SaveFileName(login, key, sizeof(key));
            if( !key[0] || strcmp(key, who) == 0 || site_logins() < 0 || site_login_find(key) < 0 )
            {
                redirect(res, "/admin/users");
                return 1;
            }
            IoGate_SignInAs(res, who, key);
            return 1;
        }
        if( is_get && IoGate_PathIs(req->path, "/admin/recordings") )
        {
            page_recordings(gate, who, req, res);
            return 1;
        }
        if( is_get && strncmp(req->path, "/admin/recordings/file/", 23) == 0 )
        {
            serve_recording(req->path + 23, res);
            return 1;
        }
        if( is_get && IoGate_PathIs(req->path, "/admin/replay") )
        {
            page_replay(gate, who, req, res);
            return 1;
        }
        if( is_post && IoGate_PathIs(req->path, "/admin/recordings/record") )
        {
            char player[SITE_NAME_MAX] = "";
            char state[8] = "";
            int on;
            struct SiteBuf to;

            IoGate_FormField(req, "player", player, sizeof(player));
            IoGate_FormField(req, "state", state, sizeof(state));
            on = strcmp(state, "on") == 0;
            if( !player[0] || !ToriRSServer_AuthSetRecorded(player, on) )
            {
                redirect(res, "/admin/recordings");
                return 1;
            }
            fprintf(stderr, "io_server: '%s' turned recording %s for '%s'\n", who,
                    on ? "on" : "off", player);
            buf_init(&to);
            buf_printf(&to, "/admin/recordings?done=%s&player=", on ? "on" : "off");
            buf_url(&to, player);
            redirect(res, to.data);
            ToriMem_Free(ToriMem_Net(), to.data);
            return 1;
        }
        return 0;
    }
    return 0;
}
