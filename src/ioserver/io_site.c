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
    "footer{text-align:center;color:#8a7a5a;font-size:13px;padding:16px}";

/* Where the account site's Play Now goes: the gate's landing (the game). */
static char const*
site_landing(struct IoGate const* gate)
{
    return gate->landing;
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
    buf_puts(b, "</nav><div class=\"who\"><span>Signed in as <b>");
    buf_esc(b, who);
    buf_puts(b, "</b></span><a class=\"mini\" href=\"");
    buf_esc(b, site_landing(gate));
    buf_puts(b, "\">Play</a><form method=\"post\" action=\"/logout\">"
                "<button class=\"linkbtn\">Sign out</button></form></div></div></header>\n"
                "<main>\n");
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
                     "<p class=\"note\">Link one on the <a href=\"/account\">Account</a> page, or "
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

static void
page_account(
    struct IoGate* gate,
    char const* who,
    struct HttpRequest const* req,
    struct HttpResponse* res,
    int status,
    char const* error,
    char const* typed_name)
{
    struct SiteBuf b;
    struct SitePlayer* mine;
    int mine_count = linked_players(gate, who, &mine);
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
        buf_puts(&b, strcmp(done, "linked") == 0
                         ? " is linked to this login. It now plays from this login's Play Now."
                         : " is unlinked. Its own password signs it in again.");
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
                 "through this login. The account must already exist: make it by logging in to "
                 "the game.</p>"
                 "</section>\n</div>\n");
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
                 "again.</p><form method=\"post\" action=\"/account/unlink\" class=\"row\">"
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
        page_account(gate, who, req, res, 200, NULL, NULL);
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
            page_account(gate, who, req, res, 400, message, username);
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
                         NULL);
        return 1;
    }
    return 0;
}
