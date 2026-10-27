/*
 * The account site. See io_site.h.
 */
#include "io_site.h"

#include "io_gate.h"
#include "io_gate_links.h"
#include "mem/torirs_mem.h"
#include "torirsserver/torirs_server_auth.h"
#include "torirsserver/torirs_server_history.h"
#include "torirsserver/torirs_server_savefile.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* The skills the site shows, in the hiscores' order -- which is the stat ids'
 * own, 0..23. Stat 24 (Summoning) is a feature-flagged port and not listed. */
#define SITE_SKILLS 24
#define SITE_HITPOINTS 3
/* torirs_server.h's varp for quest points (configs/all.varp varp101_qp). */
#define SITE_VARP_QP 101
#define SITE_PLAYERS_MAX 1024
#define SITE_NAME_MAX 64
#define SITE_SESSIONS_SHOWN 15
#define SITE_TOP_ON_HOME 5

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

static void
buf_date(
    struct SiteBuf* b,
    long long when)
{
    time_t t = (time_t)when;
    struct tm local;
    char text[64];

    localtime_r(&t, &local);
    strftime(text, sizeof(text), "%d %b %Y, %H:%M", &local);
    buf_puts(b, text);
}

/* "3 days ago", or "just now". */
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
    /** As the save is keyed: the file name, and the page's URL. */
    char key[SITE_NAME_MAX];
    /** [player] name. */
    char name[SITE_NAME_MAX];
    long long xp[SITE_SKILLS];
    int level[SITE_SKILLS];
    int total_level;
    long long total_xp;
    int combat;
    int quest_points;
    /** When the save was last written. */
    long long saved_at;
};

/* OldSchool's experience table: the least experience for each level. */
static long long g_level_xp[100];

static void
levels_init(void)
{
    double points = 0;

    if( g_level_xp[2] )
        return;
    for( int level = 1; level < 99; level++ )
    {
        points += floor(level + 300.0 * pow(2.0, level / 7.0));
        g_level_xp[level + 1] = (long long)floor(points / 4);
    }
}

static int
level_for_xp(long long xp)
{
    int level = 1;

    while( level < 99 && xp >= g_level_xp[level + 1] )
        level++;
    return level;
}

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
        p->level[i] = level_for_xp(p->xp[i]);
        p->total_level += p->level[i];
        p->total_xp += p->xp[i];
    }
    p->combat = combat_level(p->level);
}

/* A key as saves are named: [a-z0-9_] only. */
static int
key_ok(char const* key)
{
    if( !key[0] || strlen(key) >= SITE_NAME_MAX )
        return 0;
    for( char const* scan = key; *scan; scan++ )
        if( !islower((unsigned char)*scan) && !isdigit((unsigned char)*scan) && *scan != '_' )
            return 0;
    return 1;
}

/*
 * The save `<saves>/<key>.ini` into `out`: its name, [stats] experience and
 * the quest-point varp. 0 when there is no such character.
 */
static int
player_read(
    char const* key,
    struct SitePlayer* out)
{
    char path[1024];
    char line[512];
    char section[64] = "";
    struct stat info;
    FILE* file;

    levels_init();
    memset(out, 0, sizeof(*out));
    if( !key_ok(key) )
        return 0;
    snprintf(path, sizeof(path), "%s/%s.ini", ToriRSServer_SaveDir(), key);
    if( stat(path, &info) != 0 || !S_ISREG(info.st_mode) )
        return 0;
    file = fopen(path, "rb");
    if( !file )
        return 0;
    snprintf(out->key, sizeof(out->key), "%s", key);
    out->saved_at = (long long)info.st_mtime;
    /* A fresh character's: level 10 Hitpoints, 1 elsewhere. */
    out->xp[SITE_HITPOINTS] = 1154;
    while( fgets(line, sizeof(line), file) )
    {
        char* eq;
        char* value;
        char* end = line + strcspn(line, "\r\n");

        *end = '\0';
        if( line[0] == '[' )
        {
            snprintf(section, sizeof(section), "%.*s", (int)strcspn(line + 1, "]"), line + 1);
            continue;
        }
        if( line[0] == ';' || !(eq = strchr(line, '=')) )
            continue;
        *eq = '\0';
        value = eq + 1;
        while( *value == ' ' )
            value++;
        for( char* trim = eq - 1; trim >= line && *trim == ' '; trim-- )
            *trim = '\0';
        if( strcmp(section, "player") == 0 && strcmp(line, "name") == 0 )
            snprintf(out->name, sizeof(out->name), "%s", value);
        else if( strcmp(section, "stats") == 0 )
        {
            /* `<stat> = <boosted> <xp_tenths>` */
            int stat = atoi(line);
            char* xp = strchr(value, ' ');

            if( stat >= 0 && stat < SITE_SKILLS && xp )
                out->xp[stat] = strtoll(xp + 1, NULL, 10) / 10;
        }
        else if( strcmp(section, "varps") == 0 && atoi(line) == SITE_VARP_QP )
            out->quest_points = atoi(value);
    }
    fclose(file);
    if( !out->name[0] )
        return 0;
    player_finish(out);
    return 1;
}

/* Every character save. The caller frees the list (ToriMem_Net). */
static int
players_all(struct SitePlayer** out)
{
    struct SitePlayer* list = ToriMem_Alloc(ToriMem_Net(), SITE_PLAYERS_MAX * sizeof(*list));
    DIR* dir;
    struct dirent* entry;
    int count = 0;

    assert(list);
    *out = list;
    dir = opendir(ToriRSServer_SaveDir());
    if( !dir )
        return 0;
    while( (entry = readdir(dir)) != NULL && count < SITE_PLAYERS_MAX )
    {
        char key[SITE_NAME_MAX];
        size_t len = strlen(entry->d_name);

        if( len <= 4 || len - 4 >= sizeof(key) || strcmp(entry->d_name + len - 4, ".ini") != 0 )
            continue;
        snprintf(key, sizeof(key), "%.*s", (int)(len - 4), entry->d_name);
        if( player_read(key, &list[count]) )
            count++;
    }
    closedir(dir);
    return count;
}

/* The ranking: overall (skill -1) by total level then experience, else by the
 * skill's experience; ties by name, so the order is stable. */
static int g_rank_skill;

static int
rank_compare(
    void const* a_void,
    void const* b_void)
{
    struct SitePlayer const* a = a_void;
    struct SitePlayer const* b = b_void;
    long long ka = g_rank_skill < 0 ? a->total_level : a->xp[g_rank_skill];
    long long kb = g_rank_skill < 0 ? b->total_level : b->xp[g_rank_skill];

    if( ka != kb )
        return ka > kb ? -1 : 1;
    if( g_rank_skill < 0 && a->total_xp != b->total_xp )
        return a->total_xp > b->total_xp ? -1 : 1;
    return strcmp(a->key, b->key);
}

static void
players_rank(
    struct SitePlayer* list,
    int count,
    int skill)
{
    g_rank_skill = skill;
    if( count > 1 )
        qsort(list, (size_t)count, sizeof(*list), rank_compare);
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
    int count = 0;

    assert(list);
    for( int i = 0; i < linked; i++ )
    {
        char key[SITE_NAME_MAX];

        ToriRSServer_SaveFileName(names[i], key, sizeof(key));
        if( player_read(key, &list[count]) )
            count++;
        else
        {
            /* Linked, but never played: a card with no stats yet. */
            memset(&list[count], 0, sizeof(list[count]));
            snprintf(list[count].key, sizeof(list[count].key), "%s", key);
            snprintf(list[count].name, sizeof(list[count].name), "%s", names[i]);
            for( int s = 0; s < SITE_SKILLS; s++ )
                list[count].xp[s] = s == SITE_HITPOINTS ? 1154 : 0;
            player_finish(&list[count]);
            count++;
        }
    }
    *out = list;
    return count;
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
player_link(
    struct SiteBuf* b,
    struct SitePlayer const* p)
{
    buf_puts(b, "<a href=\"/players/");
    buf_esc(b, p->key);
    buf_puts(b, "\">");
    buf_esc(b, p->name);
    buf_puts(b, "</a>");
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
        buf_printf(b, "</div><div class=\"meta\">Combat %d &middot; Total level %d", p->combat,
                   p->total_level);
        if( p->saved_at )
        {
            buf_puts(b, " &middot; played ");
            buf_ago(b, p->saved_at, now);
        }
        else
            buf_puts(b, " &middot; not played yet");
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
    struct SitePlayer* all;
    int mine_count = linked_players(gate, who, &mine);
    int all_count = players_all(&all);

    players_rank(all, all_count, -1);
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
    if( all_count == 0 )
        buf_puts(&b, "<p>Nobody has played yet.</p>");
    else
    {
        buf_puts(&b, "<div class=\"scroll\"><table><thead><tr><th class=\"num\">Rank</th>"
                     "<th>Name</th><th class=\"num\">Total</th><th class=\"num\">XP</th></tr>"
                     "</thead><tbody>");
        for( int i = 0; i < all_count && i < SITE_TOP_ON_HOME; i++ )
        {
            buf_printf(&b, "<tr><td class=\"num\">%d</td><td>", i + 1);
            player_link(&b, &all[i]);
            buf_printf(&b, "</td><td class=\"num\">%d</td><td class=\"num\">", all[i].total_level);
            buf_num(&b, all[i].total_xp);
            buf_puts(&b, "</td></tr>");
        }
        buf_puts(&b, "</tbody></table></div>"
                     "<p class=\"note\" style=\"margin-top:12px\"><a href=\"/hiscores\">All "
                     "hiscores</a></p>");
    }
    buf_puts(&b, "</section>\n</div>\n");
    ToriMem_Free(ToriMem_Net(), mine);
    ToriMem_Free(ToriMem_Net(), all);
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
    struct SitePlayer* all;
    int all_count = players_all(&all);
    char skill_text[8] = "";
    int skill = -1;

    if( IoGate_QueryField(req, "skill", skill_text, sizeof(skill_text)) )
    {
        skill = atoi(skill_text);
        if( skill < 0 || skill >= SITE_SKILLS )
            skill = -1;
    }
    players_rank(all, all_count, skill);
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
    if( all_count == 0 )
        buf_puts(&b, "<p>Nobody has played yet.</p>");
    else
    {
        buf_puts(&b, "<div class=\"scroll\"><table><thead><tr><th class=\"num\">Rank</th>"
                     "<th>Name</th><th class=\"num\">Level</th><th class=\"num\">XP</th></tr>"
                     "</thead><tbody>");
        for( int i = 0; i < all_count; i++ )
        {
            struct SitePlayer const* p = &all[i];

            buf_printf(&b, "<tr%s><td class=\"num\">%d</td><td>",
                       owns(gate, who, p->key) ? " class=\"me\"" : "", i + 1);
            player_link(&b, p);
            buf_printf(&b, "</td><td class=\"num\">%d</td><td class=\"num\">",
                       skill < 0 ? p->total_level : p->level[skill]);
            buf_num(&b, skill < 0 ? p->total_xp : p->xp[skill]);
            buf_puts(&b, "</td></tr>");
        }
        buf_puts(&b, "</tbody></table></div>");
    }
    buf_puts(&b, "</section></div>\n");
    ToriMem_Free(ToriMem_Net(), all);
    page_send(&b, res, 200);
}

/* A player's rank in `skill` (-1 overall) among `all`, 1-based. */
static int
rank_of(
    struct SitePlayer const* all,
    int count,
    struct SitePlayer const* p,
    int skill)
{
    int rank = 1;

    g_rank_skill = skill;
    for( int i = 0; i < count; i++ )
        if( strcmp(all[i].key, p->key) != 0 && rank_compare(&all[i], p) < 0 )
            rank++;
    return rank;
}

/* The sum of the listed skills' experience in a logout entry, whole xp. */
static long long
entry_total(struct ToriRSServerHistoryEntry const* e)
{
    long long total = 0;

    for( int i = 0; i < SITE_SKILLS; i++ )
        total += e->xp_tenths[i] / 10;
    return total;
}

/* Total experience over time, one point per logout: inline SVG. */
static void
history_chart(
    struct SiteBuf* b,
    struct ToriRSServerHistory const* h)
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
    long long t0 = 0;
    long long t1 = 0;
    long long lo = 0;
    long long hi = 0;
    int points = 0;

    for( int i = 0; i < h->count; i++ )
    {
        struct ToriRSServerHistoryEntry const* e = &h->entries[i];
        long long total;

        if( e->kind != TORIRSSERVER_HISTORY_LOGOUT )
            continue;
        total = entry_total(e);
        if( points == 0 )
        {
            t0 = e->time;
            lo = hi = total;
        }
        t1 = e->time;
        if( total < lo )
            lo = total;
        if( total > hi )
            hi = total;
        points++;
    }
    if( points < 2 )
    {
        buf_puts(b, "<p class=\"note\">The experience chart appears after two recorded "
                    "sessions.</p>");
        return;
    }
    if( t1 == t0 )
        t1 = t0 + 1;
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
        int first = 1;
        double last_x = PAD_L;

        buf_init(&line);
        for( int i = 0; i < h->count; i++ )
        {
            struct ToriRSServerHistoryEntry const* e = &h->entries[i];
            double x;
            double y;

            if( e->kind != TORIRSSERVER_HISTORY_LOGOUT )
                continue;
            x = PAD_L + (double)(W - PAD_L - PAD_R) * (double)(e->time - t0) / (double)(t1 - t0);
            y = PAD_T + (double)(H - PAD_T - PAD_B) *
                            (1.0 - (double)(entry_total(e) - lo) / (double)(hi - lo));
            buf_printf(&line, "%s%.1f,%.1f", first ? "" : " ", x, y);
            first = 0;
            last_x = x;
        }
        buf_printf(b, "<polygon fill=\"rgba(240,192,96,.18)\" points=\"%d,%d %s %.1f,%d\"/>", PAD_L,
                   H - PAD_B, line.data, last_x, H - PAD_B);
        buf_printf(b, "<polyline fill=\"none\" stroke=\"#f0c060\" stroke-width=\"2.5\" "
                      "stroke-linejoin=\"round\" points=\"%s\"/>",
                   line.data);
        ToriMem_Free(ToriMem_Net(), line.data);
    }
    /* The time axis: first and last session. */
    buf_printf(b, "<text x=\"%d\" y=\"%d\" fill=\"#cdbf9e\" font-size=\"12\" "
                  "font-family=\"Georgia,serif\">",
               PAD_L, H - 10);
    buf_date(b, t0);
    buf_printf(b, "</text><text x=\"%d\" y=\"%d\" fill=\"#cdbf9e\" font-size=\"12\" "
                  "text-anchor=\"end\" font-family=\"Georgia,serif\">",
               W - PAD_R, H - 10);
    buf_date(b, t1);
    buf_puts(b, "</text></svg>");
}

/* The newest logout at or before `when`, else the oldest logout; NULL when
 * there is none. `*out_partial` says the window reaches past the history. */
static struct ToriRSServerHistoryEntry const*
baseline_at(
    struct ToriRSServerHistory const* h,
    long long when,
    int* out_partial)
{
    struct ToriRSServerHistoryEntry const* found = NULL;
    struct ToriRSServerHistoryEntry const* oldest = NULL;

    for( int i = 0; i < h->count; i++ )
    {
        struct ToriRSServerHistoryEntry const* e = &h->entries[i];

        if( e->kind != TORIRSSERVER_HISTORY_LOGOUT )
            continue;
        if( !oldest )
            oldest = e;
        if( e->time <= when )
            found = e;
    }
    *out_partial = found == NULL;
    return found ? found : oldest;
}

/* Gains over the last day, week and month, per skill that gained. */
static void
history_gains(
    struct SiteBuf* b,
    struct ToriRSServerHistory const* h,
    struct SitePlayer const* p)
{
    static long long const k_windows[3] = { 86400, 7 * 86400, 30 * 86400 };
    static char const* const k_window_names[3] = { "Day", "Week", "Month" };
    struct ToriRSServerHistoryEntry const* base[3];
    int partial[3];
    long long now = (long long)time(NULL);

    for( int w = 0; w < 3; w++ )
        base[w] = baseline_at(h, now - k_windows[w], &partial[w]);
    if( !base[2] )
        return;
    buf_puts(b, "<h2>Experience gained</h2><div class=\"scroll\"><table><thead><tr>"
                "<th>Skill</th>");
    for( int w = 0; w < 3; w++ )
        buf_printf(b, "<th class=\"num\">%s%s</th>", k_window_names[w], partial[w] ? "*" : "");
    buf_puts(b, "</tr></thead><tbody>");
    for( int s = -1; s < SITE_SKILLS; s++ )
    {
        long long gain[3];
        int gained = 0;

        for( int w = 0; w < 3; w++ )
        {
            long long then = s < 0 ? entry_total(base[w]) : base[w]->xp_tenths[s] / 10;
            long long current = s < 0 ? p->total_xp : p->xp[s];

            gain[w] = current - then;
            if( gain[w] > 0 )
                gained = 1;
        }
        if( !gained && s >= 0 )
            continue;
        buf_printf(b, "<tr><td>%s</td>", s < 0 ? "<b>Overall</b>" : k_skill_names[s]);
        for( int w = 0; w < 3; w++ )
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
    if( partial[0] || partial[1] || partial[2] )
        buf_puts(b, "<p class=\"note\">* since the first recorded session.</p>");
}

/* The newest sessions: when, how long, what was gained, mostly where. */
static void
history_sessions(
    struct SiteBuf* b,
    struct ToriRSServerHistory const* h)
{
    struct
    {
        long long start;
        long long end;
        long long gained;
        int top_skill;
        long long top_gain;
        int known_gain;
    } sessions[TORIRSSERVER_HISTORY_MAX];
    int count = 0;
    long long open_login = 0;
    long long played = 0;
    struct ToriRSServerHistoryEntry const* previous = NULL;

    for( int i = 0; i < h->count; i++ )
    {
        struct ToriRSServerHistoryEntry const* e = &h->entries[i];

        if( e->kind == TORIRSSERVER_HISTORY_LOGIN )
        {
            open_login = e->time;
            continue;
        }
        sessions[count].start = open_login;
        sessions[count].end = e->time;
        sessions[count].known_gain = previous != NULL;
        sessions[count].gained = previous ? entry_total(e) - entry_total(previous) : 0;
        sessions[count].top_skill = -1;
        sessions[count].top_gain = 0;
        if( previous )
            for( int s = 0; s < SITE_SKILLS; s++ )
            {
                long long gain = (e->xp_tenths[s] - previous->xp_tenths[s]) / 10;

                if( gain > sessions[count].top_gain )
                {
                    sessions[count].top_gain = gain;
                    sessions[count].top_skill = s;
                }
            }
        if( open_login && e->time >= open_login )
            played += e->time - open_login;
        open_login = 0;
        previous = e;
        count++;
    }

    buf_puts(b, "<h2>Recent sessions</h2>");
    if( count == 0 )
    {
        buf_puts(b, "<p class=\"note\">No sessions recorded yet: history starts with this "
                    "account's next log out.</p>");
        return;
    }
    buf_printf(b, "<p class=\"note\">%d session%s recorded, ", count, count == 1 ? "" : "s");
    buf_duration(b, played);
    buf_puts(b, " played.</p><div class=\"scroll\"><table><thead><tr><th>Logged in</th>"
                "<th class=\"num\">Length</th><th class=\"num\">XP gained</th>"
                "<th>Most trained</th></tr></thead><tbody>");
    for( int i = count - 1; i >= 0 && i >= count - SITE_SESSIONS_SHOWN; i-- )
    {
        buf_puts(b, "<tr><td class=\"when\">");
        buf_when(b, sessions[i].start ? sessions[i].start : sessions[i].end);
        buf_puts(b, "</td><td class=\"num\">");
        if( sessions[i].start && sessions[i].end >= sessions[i].start )
            buf_duration(b, sessions[i].end - sessions[i].start);
        else
            buf_puts(b, "&ndash;");
        buf_puts(b, "</td><td class=\"num\">");
        if( sessions[i].known_gain )
        {
            if( sessions[i].gained > 0 )
                buf_puts(b, "+");
            buf_num(b, sessions[i].gained);
        }
        else
            buf_puts(b, "&ndash;");
        buf_puts(b, "</td><td>");
        if( sessions[i].top_skill >= 0 )
        {
            buf_printf(b, "%s (+", k_skill_names[sessions[i].top_skill]);
            buf_num(b, sessions[i].top_gain);
            buf_puts(b, ")");
        }
        else
            buf_puts(b, "&ndash;");
        buf_puts(b, "</td></tr>");
    }
    buf_puts(b, "</tbody></table></div>");
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
    struct SitePlayer* all;
    int all_count;
    int mine;
    struct ToriRSServerHistory* history = NULL;
    long long now = (long long)time(NULL);

    if( !player_read(key, &p) )
    {
        page_begin(&b, gate, who, "Player not found", "/hiscores");
        buf_puts(&b, "<section class=\"panel\"><h2>No such player</h2><p>Nobody by that name has "
                     "played here.</p><p><a href=\"/hiscores\">Back to the hiscores</a></p>"
                     "</section>");
        page_send(&b, res, 404);
        return;
    }
    all_count = players_all(&all);
    mine = owns(gate, who, p.key);
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
    buf_printf(&b, "<div class=\"tile\"><b>#%d</b><span>Overall rank</span></div>",
               rank_of(all, all_count, &p, -1));
    buf_puts(&b, "<div class=\"tile\"><b>");
    buf_ago(&b, p.saved_at, now);
    buf_puts(&b, "</b><span>Last played</span></div></div>\n");

    /* An owner's history: the chart across the page, the tables beside the
     * skills. ~106 KB, so off the stack. */
    if( mine )
    {
        history = ToriMem_Alloc(ToriMem_Net(), sizeof(struct ToriRSServerHistory));
        assert(history);
        ToriRSServer_HistoryRead(p.key, history);
        buf_puts(&b, "<section class=\"panel\" style=\"margin:0 0 24px\"><h2>Total experience"
                     "</h2>");
        history_chart(&b, history);
        buf_puts(&b, "</section>\n");
    }
    buf_puts(&b, "<div class=\"grid\">\n");

    buf_puts(&b, "<section class=\"panel\"><h2>Skills</h2><div class=\"scroll\"><table><thead>"
                 "<tr><th>Skill</th><th class=\"num\">Rank</th><th class=\"num\">Level</th>"
                 "<th class=\"num\">XP</th></tr></thead><tbody>");
    buf_printf(&b, "<tr><td><a href=\"/hiscores\"><b>Overall</b></a></td><td class=\"num\">%d</td>"
                   "<td class=\"num\">%d</td><td class=\"num\">",
               rank_of(all, all_count, &p, -1), p.total_level);
    buf_num(&b, p.total_xp);
    buf_puts(&b, "</td></tr>");
    for( int s = 0; s < SITE_SKILLS; s++ )
    {
        buf_printf(&b, "<tr><td><a href=\"/hiscores?skill=%d\">%s</a></td><td class=\"num\">%d</td>"
                       "<td class=\"num\">%d</td><td class=\"num\">",
                   s, k_skill_names[s], rank_of(all, all_count, &p, s), p.level[s]);
        buf_num(&b, p.xp[s]);
        buf_puts(&b, "</td></tr>");
    }
    buf_puts(&b, "</tbody></table></div></section>\n");

    buf_puts(&b, "<section class=\"panel\">");
    if( history )
    {
        history_gains(&b, history, &p);
        history_sessions(&b, history);
        ToriMem_Free(ToriMem_Net(), history);
    }
    else
        buf_puts(&b, "<h2>History</h2><p>Session history is private to the login this account "
                     "is linked to.</p><p class=\"note\">Your own linked accounts show their "
                     "sessions, play time and experience gained here.</p>");
    buf_puts(&b, "</section>\n</div>\n");
    ToriMem_Free(ToriMem_Net(), all);
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
                 "through this login. A name nobody has yet is created and linked at once.</p>"
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
