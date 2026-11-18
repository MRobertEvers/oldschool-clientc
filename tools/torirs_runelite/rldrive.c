/*
 * rldrive: run the quest driver (script/plugins/quest_driver*.lua, unchanged)
 * against the instrumented RuneLite gamepack instead of our C client.
 *
 * The C client hosts the driver as a Lua plugin and answers its `api.drive`
 * primitives from its own memory (src/plugin/torirs_plugin_drive*.c). The
 * gamepack cannot host Lua 5.5, so this process does: it builds the same chunk
 * (the parts torirs_plugin_drive_parts.h lists, then quest_driver.lua), runs
 * the same one-coroutine scheduler, writes the same ledger.tsv, and answers
 * every primitive it does not own by asking the gamepack over JCtl:
 *
 *     -> qd <primitive> <arg> <arg> ...
 *     <- ok <Lua expression list>          e.g.  ok "ok", {x=3222,z=3218,level=0}
 *     <- err <why>
 *
 * Arguments are tokens: i<integer>  f<number>  s<url-encoded string>  b1 b0  n.
 * The reply is evaluated as `return <list>` with no globals in scope, so the
 * gamepack's side (Deobfuscator instr/revisions/<rev>/instrumentation/
 * JQuest.java) writes plain literals and nothing else.
 *
 * Owned here, not forwarded: await, pump, tick's deadline arithmetic, symbol,
 * symbol_name, ledger, report, finish, session, shot, quit. Everything else is
 * the gamepack's, and a primitive the gamepack does not implement raises a Lua
 * error naming it -- the run fails at the verb that needs it.
 *
 * Usage (tools/torirs_runelite/rlctl.py quest wraps it):
 *     rldrive --port 43651 --script <wrapped.lua> --session <dir>
 *             [--root <3draster>] [--content <OSRS-Content/osrs-content>]
 */
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CYCLES_PER_TICK 30
#define FRAME_MS 20

/* ------------------------------------------------------------------ options */

static char const* g_root = ".";
static char const* g_content = NULL;
static char const* g_script = NULL;
static char const* g_session = NULL;
static char const* g_rev = "osrs242";
static int g_port = 43651;
static int g_trace;

/* ------------------------------------------------------------------- JCtl */

static FILE* g_jin;
static FILE* g_jout;

static void
jctl_connect(void)
{
    struct sockaddr_in addr;
    int fd;
    int tries;

    for( tries = 0; tries < 600; tries++ )
    {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        assert(fd >= 0);
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((unsigned short)g_port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if( connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0 )
        {
            g_jin = fdopen(fd, "r");
            g_jout = fdopen(dup(fd), "w");
            assert(g_jin);
            assert(g_jout);
            return;
        }
        close(fd);
        usleep(500 * 1000);
    }
    fprintf(stderr, "rldrive: no JCtl on 127.0.0.1:%d (start the client with -DJCTL=1)\n", g_port);
    exit(2);
}

/* One request, one reply line. The returned buffer is valid until the next call. */
static char const*
jctl(char const* line)
{
    static char* reply;
    static size_t reply_cap;
    ssize_t n;

    if( g_trace )
        fprintf(stderr, "rldrive> %s\n", line);
    fprintf(g_jout, "%s\n", line);
    fflush(g_jout);
    n = getline(&reply, &reply_cap, g_jin);
    if( n <= 0 )
    {
        fprintf(stderr, "rldrive: JCtl closed the connection (the client exited?)\n");
        exit(3);
    }
    while( n > 0 && (reply[n - 1] == '\n' || reply[n - 1] == '\r') )
        reply[--n] = '\0';
    if( g_trace )
        fprintf(stderr, "rldrive< %.300s\n", reply);
    return reply;
}

/* ------------------------------------------------------------ the buffer */

struct Buf
{
    char* p;
    size_t n, cap;
};

static void
buf_put(struct Buf* b, char const* s, size_t len)
{
    if( b->n + len + 1 > b->cap )
    {
        b->cap = (b->n + len + 1) * 2;
        b->p = realloc(b->p, b->cap);
        assert(b->p);
    }
    memcpy(b->p + b->n, s, len);
    b->n += len;
    b->p[b->n] = '\0';
}

static void
buf_puts(struct Buf* b, char const* s)
{
    buf_put(b, s, strlen(s));
}

static void
buf_printf(struct Buf* b, char const* fmt, ...)
{
    char tmp[512];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    assert(len >= 0 && len < (int)sizeof(tmp));
    buf_put(b, tmp, (size_t)len);
}

static void
buf_urlencode(struct Buf* b, char const* s, size_t len)
{
    static char const HEX[] = "0123456789ABCDEF";
    size_t i;

    for( i = 0; i < len; i++ )
    {
        unsigned char c = (unsigned char)s[i];
        if( c > 32 && c < 127 && c != '%' && c != ';' && c != '=' && c != '+' )
            buf_put(b, (char const*)&c, 1);
        else
        {
            char e[3] = { '%', HEX[c >> 4], HEX[c & 15] };
            buf_put(b, e, 3);
        }
    }
}

/* ------------------------------------------------- forwarding a primitive */

static int
eval_reply(lua_State* L, char const* name, char const* reply)
{
    int base = lua_gettop(L);
    struct Buf code = { 0 };

    if( strncmp(reply, "err ", 4) == 0 || strcmp(reply, "err") == 0 )
        return luaL_error(L, "rldrive: api.drive.%s: %s", name, reply[3] ? reply + 4 : "error");
    if( strncmp(reply, "ok", 2) != 0 )
        return luaL_error(L, "rldrive: api.drive.%s: unexpected reply: %s", name, reply);
    reply += 2;
    while( *reply == ' ' )
        reply++;
    if( !*reply )
        return 0;
    buf_puts(&code, "return ");
    buf_puts(&code, reply);
    /* No globals: a reply is literals, never code that reaches anything. */
    if( luaL_loadbufferx(L, code.p, code.n, "=gamepack", "t") != LUA_OK )
    {
        free(code.p);
        return luaL_error(L, "rldrive: api.drive.%s: bad reply (%s): %s", name, lua_tostring(L, -1), reply);
    }
    free(code.p);
    lua_newtable(L);
    lua_setupvalue(L, -2, 1);
    lua_call(L, 0, LUA_MULTRET);
    return lua_gettop(L) - base;
}

/* One scalar as a token (i/f/s/b/n); 0 when the value is not a scalar. */
static int
put_scalar(lua_State* L, int i, struct Buf* b)
{
    switch( lua_type(L, i) )
    {
    case LUA_TNIL:
    case LUA_TNONE:
        buf_puts(b, "n");
        return 1;
    case LUA_TBOOLEAN:
        buf_puts(b, lua_toboolean(L, i) ? "b1" : "b0");
        return 1;
    case LUA_TNUMBER:
        if( lua_isinteger(L, i) )
            buf_printf(b, "i%lld", (long long)lua_tointeger(L, i));
        else
            buf_printf(b, "f%.17g", lua_tonumber(L, i));
        return 1;
    case LUA_TSTRING:
    {
        size_t len;
        char const* s = lua_tolstring(L, i, &len);
        buf_puts(b, "s");
        buf_urlencode(b, s, len);
        return 1;
    }
    default:
        return 0;
    }
}

static int
forward_call(lua_State* L, char const* name, int first_arg)
{
    struct Buf line = { 0 };
    int top = lua_gettop(L);
    int i;
    int nret;

    buf_puts(&line, "qd ");
    buf_puts(&line, name);
    for( i = first_arg; i <= top; i++ )
    {
        buf_puts(&line, " ");
        switch( lua_type(L, i) )
        {
        case LUA_TNIL:
        case LUA_TNONE:
            buf_puts(&line, "n");
            break;
        case LUA_TBOOLEAN:
            buf_puts(&line, lua_toboolean(L, i) ? "b1" : "b0");
            break;
        case LUA_TNUMBER:
            if( lua_isinteger(L, i) )
                buf_printf(&line, "i%lld", (long long)lua_tointeger(L, i));
            else
                buf_printf(&line, "f%.17g", lua_tonumber(L, i));
            break;
        case LUA_TSTRING:
        {
            size_t len;
            char const* s = lua_tolstring(L, i, &len);
            buf_puts(&line, "s");
            buf_urlencode(&line, s, len);
            break;
        }
        case LUA_TTABLE:
        {
            /* A flat table: t<url-encoded "key=token;key=token">, keys as
             * written (an array index is its number). */
            struct Buf inner = { 0 };
            int first = 1;
            lua_pushnil(L);
            while( lua_next(L, i) != 0 )
            {
                struct Buf tok = { 0 };
                if( !first )
                    buf_puts(&inner, ";");
                first = 0;
                if( lua_type(L, -2) == LUA_TSTRING )
                    buf_puts(&inner, lua_tostring(L, -2));
                else
                    buf_printf(&inner, "%lld", (long long)lua_tointeger(L, -2));
                buf_puts(&inner, "=");
                if( !put_scalar(L, lua_gettop(L), &tok) )
                {
                    free(inner.p);
                    free(line.p);
                    return luaL_error(L, "rldrive: api.drive.%s: argument %d holds a nested %s",
                        name, i - first_arg + 1, luaL_typename(L, -1));
                }
                buf_puts(&inner, tok.p);
                free(tok.p);
                lua_pop(L, 1);
            }
            buf_puts(&line, "t");
            if( inner.p )
                buf_urlencode(&line, inner.p, inner.n);
            free(inner.p);
            break;
        }
        default:
            free(line.p);
            return luaL_error(L, "rldrive: api.drive.%s: argument %d is a %s; the RuneLite backend "
                "forwards only nil/boolean/number/string and flat tables", name, i - first_arg + 1,
                luaL_typename(L, i));
        }
    }
    {
        char const* reply = jctl(line.p);
        free(line.p);
        /* The arguments stay below the results; return only the results. */
        nret = eval_reply(L, name, reply);
    }
    return nret;
}

static int
lua_forward(lua_State* L)
{
    char const* name = lua_tostring(L, lua_upvalueindex(1));
    return forward_call(L, name, 1);
}

/* --------------------------------------------------------------- symbols */

struct Sym
{
    char* name;
    int id;
};

struct SymTable
{
    struct Sym* rows;
    int count, cap;
};

enum
{
    SK_NPC, SK_OBJ, SK_LOC, SK_COMPONENT, SK_INTERFACE, SK_VARP, SK_VARBIT, SK_STAT, SK_INV,
    SK_SEQ, SK_SPOTANIM, SK_COUNT
};
static char const* const SK_NAMES[SK_COUNT] = {
    "npc", "obj", "loc", "component", "interface", "varp", "varbit", "stat", "inv", "seq", "spotanim",
};
static struct SymTable g_syms[SK_COUNT];

static void
sym_add(struct SymTable* t, char const* name, int id)
{
    if( t->count == t->cap )
    {
        t->cap = t->cap ? t->cap * 2 : 1024;
        t->rows = realloc(t->rows, sizeof(*t->rows) * (size_t)t->cap);
        assert(t->rows);
    }
    t->rows[t->count].name = strdup(name);
    assert(t->rows[t->count].name);
    t->rows[t->count].id = id;
    t->count++;
}

/* `id=name` lines; `//` comments. Returns rows read. */
static int
sym_load(struct SymTable* t, char const* path, char const* prefix)
{
    FILE* f = fopen(path, "r");
    char line[1024];
    int n = 0;

    if( !f )
        return 0;
    while( fgets(line, sizeof(line), f) )
    {
        char* eq;
        char* end;
        char full[1100];
        long id;

        if( line[0] == '/' || line[0] == '#' || line[0] == '\n' )
            continue;
        eq = strchr(line, '=');
        if( !eq )
            continue;
        *eq = '\0';
        id = strtol(line, &end, 10);
        if( end == line )
            continue;
        end = eq + 1 + strcspn(eq + 1, "\r\n \t");
        *end = '\0';
        if( !eq[1] )
            continue;
        snprintf(full, sizeof(full), "%s%s", prefix ? prefix : "", eq + 1);
        sym_add(t, full, (int)id);
        n++;
    }
    fclose(f);
    return n;
}

static void
sym_load_kind(char const* dir, int kind)
{
    char path[2048];
    char const* k = SK_NAMES[kind];

    if( kind == SK_INTERFACE )
    {
        snprintf(path, sizeof(path), "%s/pack/3_interfaces.pack", dir);
        sym_load(&g_syms[kind], path, NULL);
        return;
    }
    if( kind == SK_STAT )
    {
        snprintf(path, sizeof(path), "%s/pack/stat.pack", dir);
        sym_load(&g_syms[kind], path, NULL);
        return;
    }
    if( kind == SK_COMPONENT )
        return; /* composed from the interface table, below */
    snprintf(path, sizeof(path), "%s/configs/all.%s.compack", dir, k);
    sym_load(&g_syms[kind], path, NULL);
    snprintf(path, sizeof(path), "%s/pack/%s.pack", dir, k);
    sym_load(&g_syms[kind], path, NULL);
    snprintf(path, sizeof(path), "%s/pack/%s.alloc", dir, k);
    sym_load(&g_syms[kind], path, NULL);
}

static void
sym_load_components(char const* dir)
{
    struct SymTable* ifaces = &g_syms[SK_INTERFACE];
    int i;

    for( i = 0; i < ifaces->count; i++ )
    {
        struct SymTable children = { 0 };
        char path[2048];
        char prefix[600];
        int c;

        snprintf(path, sizeof(path), "%s/interfaces/%s.compack", dir, ifaces->rows[i].name);
        if( !sym_load(&children, path, NULL) )
            continue;
        snprintf(prefix, sizeof(prefix), "%s:", ifaces->rows[i].name);
        for( c = 0; c < children.count; c++ )
        {
            char full[1200];
            if( children.rows[c].id < 0 || children.rows[c].id > 0xffff )
                continue;
            snprintf(full, sizeof(full), "%s%s", prefix, children.rows[c].name);
            sym_add(&g_syms[SK_COMPONENT], full, (ifaces->rows[i].id << 16) | children.rows[c].id);
            free(children.rows[c].name);
        }
        free(children.rows);
    }
}

static void
sym_load_all(void)
{
    char ported[2048];
    DIR* d;
    struct dirent* e;
    int kind;

    for( kind = 0; kind < SK_COUNT; kind++ )
        sym_load_kind(g_content, kind);
    /* The ported lanes (summoning, RS2012 ...) layer their own packs. */
    snprintf(ported, sizeof(ported), "%s/ported", g_content);
    d = opendir(ported);
    while( d && (e = readdir(d)) != NULL )
    {
        char lane[2400];
        if( e->d_name[0] == '.' )
            continue;
        snprintf(lane, sizeof(lane), "%s/%s", ported, e->d_name);
        for( kind = 0; kind < SK_COUNT; kind++ )
            if( kind != SK_INTERFACE && kind != SK_STAT && kind != SK_COMPONENT )
            {
                char path[2600];
                snprintf(path, sizeof(path), "%s/pack/%s.pack", lane, SK_NAMES[kind]);
                sym_load(&g_syms[kind], path, NULL);
                snprintf(path, sizeof(path), "%s/pack/%s.alloc", lane, SK_NAMES[kind]);
                sym_load(&g_syms[kind], path, NULL);
            }
    }
    if( d )
        closedir(d);
    sym_load_components(g_content);
    for( kind = 0; kind < SK_COUNT; kind++ )
        if( g_trace )
            fprintf(stderr, "rldrive: %d %s symbols\n", g_syms[kind].count, SK_NAMES[kind]);
}

static int
sym_kind(lua_State* L, char const* name)
{
    int k;
    for( k = 0; k < SK_COUNT; k++ )
        if( strcmp(name, SK_NAMES[k]) == 0 )
            return k;
    return luaL_error(L, "drive.symbol: unknown kind '%s'", name);
}

static int
lua_symbol(lua_State* L)
{
    int kind = sym_kind(L, luaL_checkstring(L, 1));
    char const* name = luaL_checkstring(L, 2);
    struct SymTable* t = &g_syms[kind];
    int i;

    if( strcmp(name, "null") != 0 )
        for( i = 0; i < t->count; i++ )
            if( strcmp(t->rows[i].name, name) == 0 )
            {
                lua_pushstring(L, "ok");
                lua_pushinteger(L, t->rows[i].id);
                return 2;
            }
    lua_pushstring(L, "not_found");
    lua_pushinteger(L, -1);
    return 2;
}

static int
lua_symbol_name(lua_State* L)
{
    int kind = sym_kind(L, luaL_checkstring(L, 1));
    int id = (int)luaL_checkinteger(L, 2);
    struct SymTable* t = &g_syms[kind];
    int i;

    for( i = 0; i < t->count; i++ )
        if( t->rows[i].id == id )
        {
            lua_pushstring(L, "ok");
            lua_pushstring(L, t->rows[i].name);
            return 2;
        }
    lua_pushstring(L, "not_found");
    lua_pushstring(L, "");
    return 2;
}

/* component(symbol, sub): the qualified "<iface>:<child>" symbol to its id
 * (the handle `component | (sub + 1) << 32` for a dynamic child), answered
 * not_visible when the gamepack has no such live widget. */
static int
lua_component(lua_State* L)
{
    char const* name = luaL_checkstring(L, 1);
    lua_Integer sub = luaL_optinteger(L, 2, -1);
    struct SymTable* t = &g_syms[SK_COMPONENT];
    long long handle = -1;
    char line[96];
    char const* reply;
    int i;

    for( i = 0; i < t->count; i++ )
        if( strcmp(t->rows[i].name, name) == 0 )
        {
            handle = (long long)(unsigned)t->rows[i].id;
            break;
        }
    if( handle < 0 )
    {
        lua_pushstring(L, "no_row");
        lua_pushnil(L);
        return 2;
    }
    if( sub >= 0 )
        handle |= (long long)(sub + 1) << 32;
    snprintf(line, sizeof(line), "qd component_live i%lld", handle);
    reply = jctl(line);
    if( strstr(reply, "\"ok\"") == NULL )
    {
        lua_pushstring(L, "not_visible");
        lua_pushnil(L);
        return 2;
    }
    lua_pushstring(L, "ok");
    lua_pushinteger(L, (lua_Integer)handle);
    return 2;
}

/* ----------------------------------------------------------------- ledger */

static char g_quest_name[64];
static int g_ledger_index, g_ledger_pass, g_ledger_fail, g_ledger_blocked;
static long g_ledger_ticks;
static int g_finished, g_finish_code;

static void
ledger_open_header(FILE* f)
{
    fprintf(f, "quest-ledger-v1\nindex\tstep\tverdict\tticks\tshots\tdetail\n");
}

static void
ledger_write(char const* step, char const* verdict, int ticks, char const* shots, char const* detail)
{
    char path[1200];
    FILE* f;

    if( g_finished )
    {
        fprintf(stderr, "quest-driver: row after finish ignored: %s\n", step);
        return;
    }
    g_ledger_index++;
    g_ledger_ticks += ticks;
    if( strcmp(verdict, "PASS") == 0 )
        g_ledger_pass++;
    else if( strcmp(verdict, "BLOCKED") == 0 )
        g_ledger_blocked++;
    else
        g_ledger_fail++;
    fprintf(stderr, "QUEST %s %s %s ticks=%d shots=%s%s%s\n", g_quest_name, verdict, step, ticks, shots,
        detail[0] ? " why=" : "", detail);
    snprintf(path, sizeof(path), "%s/ledger.tsv", g_session);
    f = fopen(path, g_ledger_index == 1 ? "w" : "a");
    assert(f);
    if( g_ledger_index == 1 )
        ledger_open_header(f);
    fprintf(f, "%d\t%s\t%s\t%d\t%s\t%s\n", g_ledger_index, step, verdict, ticks, shots, detail);
    fclose(f);
}

static void
ledger_summary(char const* exit_text)
{
    char path[1200];
    FILE* f;

    snprintf(path, sizeof(path), "%s/ledger.tsv", g_session);
    f = fopen(path, g_ledger_index == 0 ? "w" : "a");
    assert(f);
    if( g_ledger_index == 0 )
        ledger_open_header(f);
    fprintf(f, "SUMMARY\t%d\t%s\t%ld\texit=%s\tpass=%d fail=%d", g_ledger_index,
        g_ledger_fail == 0 ? "PASS" : "FAIL", g_ledger_ticks, exit_text, g_ledger_pass, g_ledger_fail);
    if( g_ledger_blocked > 0 )
        fprintf(f, " blocked=%d", g_ledger_blocked);
    fprintf(f, "\n");
    fclose(f);
}

static void
finish(int code)
{
    char text[16];
    if( !g_finished )
    {
        snprintf(text, sizeof(text), "%d", code);
        ledger_summary(text);
    }
    g_finished = 1;
    g_finish_code = code;
}

static int
lua_ledger(lua_State* L)
{
    char const* step;
    char const* verdict;
    int ticks;
    char const* shots;
    char const* detail;

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "step");
    step = luaL_checkstring(L, -1);
    lua_getfield(L, 1, "verdict");
    verdict = luaL_checkstring(L, -1);
    lua_getfield(L, 1, "ticks");
    ticks = (int)luaL_optinteger(L, -1, 0);
    lua_getfield(L, 1, "shots");
    shots = luaL_optstring(L, -1, "");
    lua_getfield(L, 1, "detail");
    detail = luaL_optstring(L, -1, "");
    ledger_write(step, verdict, ticks, shots, detail);
    lua_pop(L, 5);
    lua_pushstring(L, "ok");
    lua_pushnil(L);
    return 2;
}

static int
lua_report(lua_State* L)
{
    fprintf(stderr, "QUEST %s\n", luaL_checkstring(L, 1));
    return 0;
}

static int
lua_finish(lua_State* L)
{
    finish((int)luaL_optinteger(L, 1, 0));
    lua_pushstring(L, "ok");
    lua_pushnil(L);
    return 2;
}

static int
lua_quit(lua_State* L)
{
    finish((int)luaL_optinteger(L, 1, 0));
    lua_pushstring(L, "ok");
    lua_pushnil(L);
    return 2;
}

static int
lua_session(lua_State* L)
{
    lua_createtable(L, 0, 2);
    lua_pushstring(L, g_session);
    lua_setfield(L, -2, "dir");
    lua_pushstring(L, g_script);
    lua_setfield(L, -2, "script");
    return 1;
}

/* tab_by_name(name): the revision's [tabs] map
 * (revconfig/<rev>/<rev>_dat2_cache.ini), as the C client reads it. */
static int
lua_tab_by_name(lua_State* L)
{
    char const* name = luaL_checkstring(L, 1);
    char path[2048];
    char line[256];
    int in_tabs = 0;
    FILE* f;

    snprintf(path, sizeof(path), "%s/revconfig/%s/%s_dat2_cache.ini", g_root, g_rev, g_rev);
    f = fopen(path, "r");
    if( f )
    {
        while( fgets(line, sizeof(line), f) )
        {
            char key[64];
            int value;
            if( line[0] == '[' )
            {
                in_tabs = strncmp(line, "[tabs]", 6) == 0;
                continue;
            }
            if( in_tabs && sscanf(line, "%63[a-z_]=%d", key, &value) == 2 && strcmp(key, name) == 0 )
            {
                fclose(f);
                lua_pushstring(L, "ok");
                lua_pushinteger(L, value);
                return 2;
            }
        }
        fclose(f);
    }
    lua_pushstring(L, "no_row");
    lua_pushnil(L);
    return 2;
}

/* status(): the run's own state -- this host keeps the ledger. Never
 * on_demand: a rldrive run is a scripted run, not a watched client. */
static int
drive_status(lua_State* L)
{
    lua_pushstring(L, "ok");
    lua_createtable(L, 0, 8);
    lua_pushstring(L, g_finished ? "finished" : "running");
    lua_setfield(L, -2, "state");
    lua_pushstring(L, g_script);
    lua_setfield(L, -2, "script");
    lua_pushstring(L, g_session);
    lua_setfield(L, -2, "session");
    lua_pushinteger(L, g_ledger_index);
    lua_setfield(L, -2, "rows");
    lua_pushinteger(L, g_ledger_pass);
    lua_setfield(L, -2, "pass");
    lua_pushinteger(L, g_ledger_fail);
    lua_setfield(L, -2, "fail");
    lua_pushinteger(L, g_ledger_blocked);
    lua_setfield(L, -2, "blocked");
    return 2;
}

/* ------------------------------------------------------------------ shots */

static unsigned char* g_last_shot;
static long g_last_shot_size;
static char g_last_shot_name[256];

static unsigned char*
slurp(char const* path, long* out_size)
{
    FILE* f = fopen(path, "rb");
    unsigned char* data;
    long size;

    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)size + 1);
    assert(data);
    if( fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = size;
    return data;
}

/* shot(name, keep) -> result, path|"unchanged since <name>", unchanged, captured.
 * The gamepack writes the client's framebuffer synchronously (JCtl `shot`), so
 * there is never an in-flight capture to poll. */
static int
lua_shot(lua_State* L)
{
    char const* name = luaL_checkstring(L, 1);
    int keep = lua_toboolean(L, 2);
    char dir[1200];
    char path[1600];
    char line[1700];
    char const* reply;
    unsigned char* data;
    long size = 0;
    int unchanged = 0;

    snprintf(dir, sizeof(dir), "%s/shots", g_session);
    mkdir(dir, 0755);
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    unlink(path);
    snprintf(line, sizeof(line), "shot %s", path);
    reply = jctl(line);
    if( strncmp(reply, "ok", 2) != 0 )
    {
        lua_pushstring(L, "refused");
        lua_pushstring(L, reply);
        lua_pushboolean(L, 0);
        lua_pushboolean(L, 1);
        return 4;
    }
    data = slurp(path, &size);
    if( !data || size == 0 )
    {
        free(data);
        lua_pushstring(L, "refused");
        lua_pushstring(L, "the gamepack wrote no file");
        lua_pushboolean(L, 0);
        lua_pushboolean(L, 1);
        return 4;
    }
    /* The C client compares the encoded file with the last one written; a PNG
     * of identical pixels encodes identically, so the same test holds. */
    if( g_last_shot && size == g_last_shot_size && memcmp(data, g_last_shot, (size_t)size) == 0 && !keep )
        unchanged = 1;
    lua_pushstring(L, "ok");
    if( unchanged )
    {
        char detail[300];
        snprintf(detail, sizeof(detail), "unchanged since %s", g_last_shot_name);
        lua_pushstring(L, detail);
        unlink(path);
        free(data);
    }
    else
    {
        lua_pushstring(L, path);
        free(g_last_shot);
        g_last_shot = data;
        g_last_shot_size = size;
        snprintf(g_last_shot_name, sizeof(g_last_shot_name), "%s", name);
    }
    lua_pushboolean(L, unchanged);
    lua_pushboolean(L, 1);
    return 4;
}

/* -------------------------------------------------------------- scheduler */

struct Await
{
    int active;
    int level_ref, match_ref;
    char kind[32]; /* "" = any */
    long deadline_cycle;
    char note[96];
};

static lua_State* g_L;
static lua_State* g_thread;
static int g_thread_ref = LUA_NOREF;
static int g_started;
static long g_event_cursor;
static long g_cycle;
static struct Await g_await;
/* Events stamped while the coroutine ran, before it armed this await: the
 * serials (g_skip_lo, g_skip_hi]. In the C client the world stands still while
 * the coroutine runs, so an await never sees an event from before it was
 * armed (except the rest of the batch that woke it, which is <= g_skip_lo).
 * Here the gamepack keeps running through the coroutine's shots and round
 * trips, and a menu mounted seconds before a click would settle the await
 * that waits for the click's answer (Hans' choose, 2026-11-17). */
static long g_skip_lo, g_skip_hi;

static int
call_pred(int ref, int nargs_on_stack)
{
    int r;
    lua_rawgeti(g_thread, LUA_REGISTRYINDEX, ref);
    if( nargs_on_stack )
        lua_insert(g_thread, -2);
    if( lua_pcall(g_thread, nargs_on_stack, 1, 0) != LUA_OK )
    {
        fprintf(stderr, "QUEST %s await predicate error: %s\n", g_quest_name, lua_tostring(g_thread, -1));
        lua_pop(g_thread, 1);
        return 0;
    }
    r = lua_toboolean(g_thread, -1);
    lua_pop(g_thread, 1);
    return r;
}

static void
await_free(void)
{
    if( g_await.level_ref != LUA_NOREF )
        luaL_unref(g_thread, LUA_REGISTRYINDEX, g_await.level_ref);
    if( g_await.match_ref != LUA_NOREF )
        luaL_unref(g_thread, LUA_REGISTRYINDEX, g_await.match_ref);
    g_await.active = 0;
    g_await.level_ref = g_await.match_ref = LUA_NOREF;
}

static void
thread_status(int status)
{
    if( status == LUA_YIELD )
        return;
    if( status != LUA_OK )
    {
        char detail[512];
        snprintf(detail, sizeof(detail), "%s", lua_tostring(g_thread, -1));
        for( char* p = detail; *p; p++ )
            if( *p == '\t' || *p == '\n' )
                *p = ' ';
        fprintf(stderr, "QUEST %s script error: %s\n", g_quest_name, detail);
        ledger_write("script-error", "FAIL", 0, "", detail);
        finish(1);
    }
    else if( !g_finished )
        finish(0);
    g_thread = NULL;
}

static void
resume(char const* result, char const* detail)
{
    int nres = 0;
    int status;

    lua_pushstring(g_thread, result);
    if( detail )
        lua_pushstring(g_thread, detail);
    else
        lua_pushnil(g_thread);
    status = lua_resume(g_thread, g_L, 2, &nres);
    if( status == LUA_YIELD || status == LUA_OK )
        lua_pop(g_thread, nres);
    thread_status(status);
}

static void
settle(char const* result, char const* detail)
{
    char note[96];
    snprintf(note, sizeof(note), "%s", detail ? detail : "");
    await_free();
    resume(result, detail ? note : NULL);
}

static int
lua_await(lua_State* L)
{
    int deadline;
    int level_ref = LUA_NOREF, match_ref = LUA_NOREF;
    char kind[32] = "";
    char note[96] = "";

    luaL_checktype(L, 1, LUA_TTABLE);
    deadline = (int)luaL_optinteger(L, 2, 0);
    lua_getfield(L, 1, "event");
    if( lua_type(L, -1) == LUA_TSTRING )
        snprintf(kind, sizeof(kind), "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, 1, "level");
    if( lua_type(L, -1) == LUA_TFUNCTION )
        level_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    else
        lua_pop(L, 1);
    lua_getfield(L, 1, "match");
    if( lua_type(L, -1) == LUA_TFUNCTION )
        match_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    else
        lua_pop(L, 1);
    lua_getfield(L, 1, "note");
    if( lua_type(L, -1) == LUA_TSTRING )
        snprintf(note, sizeof(note), "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    if( level_ref == LUA_NOREF && match_ref == LUA_NOREF )
        return luaL_error(L, "drive.await: descriptor needs a level or a match predicate");

    if( level_ref != LUA_NOREF )
    {
        int now;
        lua_rawgeti(L, LUA_REGISTRYINDEX, level_ref);
        if( lua_pcall(L, 0, 1, 0) != LUA_OK )
        {
            fprintf(stderr, "QUEST %s await level() error: %s\n", g_quest_name, lua_tostring(L, -1));
            now = 0;
        }
        else
            now = lua_toboolean(L, -1);
        lua_pop(L, 1);
        if( now )
        {
            luaL_unref(L, LUA_REGISTRYINDEX, level_ref);
            if( match_ref != LUA_NOREF )
                luaL_unref(L, LUA_REGISTRYINDEX, match_ref);
            lua_pushstring(L, "ok");
            lua_pushnil(L);
            return 2;
        }
    }
    if( deadline <= 0 || !lua_isyieldable(L) )
    {
        if( level_ref != LUA_NOREF )
            luaL_unref(L, LUA_REGISTRYINDEX, level_ref);
        if( match_ref != LUA_NOREF )
            luaL_unref(L, LUA_REGISTRYINDEX, match_ref);
        if( deadline > 0 )
            return luaL_error(L, "drive.await '%s' would suspend inside an await predicate", note);
        lua_pushstring(L, "timeout");
        if( note[0] )
            lua_pushstring(L, note);
        else
            lua_pushnil(L);
        return 2;
    }
    {
        char const* head = jctl("qd head");
        g_skip_lo = g_event_cursor;
        g_skip_hi = strncmp(head, "ok ", 3) == 0 ? atol(head + 3) : g_event_cursor;
    }
    g_await.active = 1;
    g_await.level_ref = level_ref;
    g_await.match_ref = match_ref;
    snprintf(g_await.kind, sizeof(g_await.kind), "%s", kind);
    g_await.deadline_cycle = g_cycle + (long)deadline * CYCLES_PER_TICK;
    snprintf(g_await.note, sizeof(g_await.note), "%s", note);
    return lua_yield(L, 0);
}

static void
start_script(void)
{
    char* source;
    long size = 0;
    static char const* const BOOTSTRAP =
        "local loader, arg = ...\n"
        "if type(loader) ~= \"function\" then error(loader, 0) end\n"
        "local quest = loader()\n"
        "return quest.run(arg)\n";
    int nres = 0;
    int status;

    source = (char*)slurp(g_script, &size);
    if( !source )
    {
        fprintf(stderr, "rldrive: cannot read %s\n", g_script);
        exit(2);
    }
    g_thread = lua_newthread(g_L);
    g_thread_ref = luaL_ref(g_L, LUA_REGISTRYINDEX);
    status = luaL_loadstring(g_thread, BOOTSTRAP);
    assert(status == LUA_OK);
    if( luaL_loadbufferx(g_thread, source, (size_t)size, g_script, "t") != LUA_OK )
    {
        /* error(loader) in the bootstrap reports it as the script's error */
    }
    free(source);
    lua_getglobal(g_thread, "QD_ROOT");
    status = lua_resume(g_thread, g_L, 2, &nres);
    if( status == LUA_YIELD || status == LUA_OK )
        lua_pop(g_thread, nres);
    thread_status(status);
}

/* The frame: one `qd frame <cursor>` answers {ready, cycle, tick, events, next}. */
static int
lua_pump(lua_State* L)
{
    char line[64];
    char const* reply;
    int n, i, nevents;

    (void)L;
    snprintf(line, sizeof(line), "qd frame i%ld", g_event_cursor);
    reply = jctl(line);
    n = eval_reply(g_L, "frame", reply);
    if( n != 1 || !lua_istable(g_L, -1) )
    {
        fprintf(stderr, "rldrive: bad frame reply: %s\n", reply);
        exit(3);
    }
    lua_getfield(g_L, -1, "cycle");
    g_cycle = (long)lua_tointeger(g_L, -1);
    lua_pop(g_L, 1);
    lua_getfield(g_L, -1, "next");
    g_event_cursor = (long)lua_tointeger(g_L, -1);
    lua_pop(g_L, 1);
    lua_getfield(g_L, -1, "ready");
    if( !g_started && lua_toboolean(g_L, -1) )
    {
        lua_pop(g_L, 1);
        g_started = 1;
        fprintf(stderr, "quest-driver: world ready, starting %s\n", g_script);
        lua_getfield(g_L, -1, "events"); /* keep the table alive across the start */
        lua_pop(g_L, 1);
        start_script();
        lua_pop(g_L, 1);
        return 0;
    }
    lua_pop(g_L, 1);

    if( !g_thread || !g_await.active )
    {
        lua_pop(g_L, 1);
        return 0;
    }

    /* Events, in order, against the armed await (the C pump: a settled await
     * may arm a new one that the rest of the batch can settle too). */
    lua_getfield(g_L, -1, "events");
    nevents = (int)lua_rawlen(g_L, -1);
    for( i = 1; i <= nevents && g_thread && g_await.active; i++ )
    {
        lua_rawgeti(g_L, -1, i);
        {
            long serial;
            lua_getfield(g_L, -1, "serial");
            serial = (long)lua_tointeger(g_L, -1);
            lua_pop(g_L, 1);
            if( serial > g_skip_lo && serial <= g_skip_hi )
            {
                lua_pop(g_L, 1);
                continue;
            }
        }
        if( g_await.kind[0] )
        {
            lua_getfield(g_L, -1, "kind");
            int same = lua_type(g_L, -1) == LUA_TSTRING && strcmp(lua_tostring(g_L, -1), g_await.kind) == 0;
            lua_pop(g_L, 1);
            if( !same )
            {
                lua_pop(g_L, 1);
                continue;
            }
        }
        if( g_await.match_ref == LUA_NOREF )
        {
            lua_pop(g_L, 1);
            continue;
        }
        lua_xmove(g_L, g_thread, 1);
        if( call_pred(g_await.match_ref, 1) )
        {
            if( g_trace )
                fprintf(stderr, "rldrive: await '%s' settled by event serial in batch index %d\n",
                    g_await.note, i);
            settle("ok", NULL);
        }
    }
    lua_pop(g_L, 2);

    if( !g_thread || !g_await.active )
        return 0;
    if( g_await.level_ref != LUA_NOREF && call_pred(g_await.level_ref, 0) )
    {
        settle("ok", NULL);
        return 0;
    }
    if( g_cycle >= g_await.deadline_cycle )
        settle("timeout", g_await.note[0] ? g_await.note : NULL);
    return 0;
}

/* ------------------------------------------------------------ api.core */

static int
lua_core_log(lua_State* L)
{
    fprintf(stderr, "%s\n", luaL_checkstring(L, 1));
    return 0;
}

static int
lua_core_screen(lua_State* L)
{
    char const* reply = jctl("qd screen");
    int n = eval_reply(L, "screen", reply);
    return n;
}

/* --------------------------------------------------------------- the chunk */

static void
chunk_append_file(struct Buf* b, char const* rel)
{
    char path[2048];
    long size = 0;
    char* data;

    snprintf(path, sizeof(path), "%s/script/%s", g_root, rel);
    data = (char*)slurp(path, &size);
    if( !data )
    {
        fprintf(stderr, "rldrive: cannot read driver part %s\n", path);
        exit(2);
    }
    buf_put(b, data, (size_t)size);
    buf_puts(b, "\n");
    free(data);
}

/* The part list is the C client's own (torirs_plugin_drive_parts.h), read
 * here rather than copied so the two can never disagree. */
static void
chunk_build(struct Buf* b)
{
    char path[2048];
    FILE* f;
    char line[512];
    int parts = 0;

    snprintf(path, sizeof(path), "%s/src/plugin/torirs_plugin_drive_parts.h", g_root);
    f = fopen(path, "r");
    if( !f )
    {
        fprintf(stderr, "rldrive: cannot read %s\n", path);
        exit(2);
    }
    while( fgets(line, sizeof(line), f) )
    {
        char* q1 = strstr(line, "\"plugins/quest_driver/");
        char* q2;
        if( !q1 )
            continue;
        q1++;
        q2 = strchr(q1, '"');
        if( !q2 )
            continue;
        *q2 = '\0';
        chunk_append_file(b, q1);
        parts++;
    }
    fclose(f);
    chunk_append_file(b, "plugins/quest_driver.lua");
    fprintf(stderr, "rldrive: driver chunk from %d parts + quest_driver.lua\n", parts);
}

/* The names api.drive carries: every `{"name", lua_drive_...}` row of the C
 * client's registration arrays (src/plugin/torirs_plugin_drive*.c), so a
 * primitive added there is forwarded here without an edit. */
static void
install_forwarders(lua_State* L, int drive_index)
{
    char dir[2048];
    DIR* d;
    struct dirent* e;
    int count = 0;

    snprintf(dir, sizeof(dir), "%s/src/plugin", g_root);
    d = opendir(dir);
    assert(d);
    while( (e = readdir(d)) != NULL )
    {
        char path[2400];
        FILE* f;
        char line[1024];

        if( strncmp(e->d_name, "torirs_plugin_drive", 19) != 0 || !strstr(e->d_name, ".c") )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        f = fopen(path, "r");
        assert(f);
        while( fgets(line, sizeof(line), f) )
        {
            char* p = line;
            char name[96];
            while( (p = strstr(p, "{\"")) != NULL )
            {
                char* q = strchr(p + 2, '"');
                if( !q || (size_t)(q - (p + 2)) >= sizeof(name) || !strstr(q, "lua_drive_") )
                    break;
                memcpy(name, p + 2, (size_t)(q - (p + 2)));
                name[q - (p + 2)] = '\0';
                p = q;
                lua_getfield(L, drive_index, name);
                if( lua_isnil(L, -1) )
                {
                    lua_pop(L, 1);
                    lua_pushstring(L, name);
                    lua_pushcclosure(L, lua_forward, 1);
                    lua_setfield(L, drive_index, name);
                    count++;
                }
                else
                    lua_pop(L, 1);
            }
        }
        fclose(f);
    }
    closedir(d);
    fprintf(stderr, "rldrive: %d primitives forwarded to the gamepack\n", count);
}

static void
open_sandbox(lua_State* L)
{
    static luaL_Reg const LIBS[] = {
        { LUA_GNAME, luaopen_base }, { LUA_TABLIBNAME, luaopen_table }, { LUA_STRLIBNAME, luaopen_string },
        { LUA_MATHLIBNAME, luaopen_math }, { LUA_UTF8LIBNAME, luaopen_utf8 }, { NULL, NULL }
    };
    static char const* const REMOVE[] = { "dofile", "loadfile", "load", "require", "print", "warn",
        "pcall", "xpcall", "setmetatable", "getmetatable" };
    luaL_Reg const* lib;
    size_t i;

    for( lib = LIBS; lib->name; lib++ )
    {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    for( i = 0; i < sizeof(REMOVE) / sizeof(REMOVE[0]); i++ )
    {
        lua_pushnil(L);
        lua_setglobal(L, REMOVE[i]);
    }
}

static long
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int
main(int argc, char** argv)
{
    struct Buf chunk = { 0 };
    char const* slash;
    char const* dot;
    int api_index, plugin_index;
    int i;

    for( i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--port") == 0 && i + 1 < argc )
            g_port = atoi(argv[++i]);
        else if( strcmp(argv[i], "--script") == 0 && i + 1 < argc )
            g_script = argv[++i];
        else if( strcmp(argv[i], "--session") == 0 && i + 1 < argc )
            g_session = argv[++i];
        else if( strcmp(argv[i], "--root") == 0 && i + 1 < argc )
            g_root = argv[++i];
        else if( strcmp(argv[i], "--content") == 0 && i + 1 < argc )
            g_content = argv[++i];
        else if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            g_rev = argv[++i];
        else if( strcmp(argv[i], "--trace") == 0 )
            g_trace = 1;
        else
        {
            fprintf(stderr, "usage: rldrive --port N --script F --session DIR [--root R] [--content C] [--trace]\n");
            return 2;
        }
    }
    if( !g_script || !g_session )
    {
        fprintf(stderr, "rldrive: --script and --session are required\n");
        return 2;
    }
    if( !g_content )
    {
        static char content[2048];
        snprintf(content, sizeof(content), "%s/OSRS-Content/osrs-content", g_root);
        g_content = content;
    }
    mkdir(g_session, 0755);
    slash = strrchr(g_script, '/');
    slash = slash ? slash + 1 : g_script;
    dot = strrchr(slash, '.');
    snprintf(g_quest_name, sizeof(g_quest_name), "%.*s", dot ? (int)(dot - slash) : (int)strlen(slash), slash);
    g_await.level_ref = g_await.match_ref = LUA_NOREF;

    sym_load_all();
    jctl_connect();

    g_L = luaL_newstate();
    assert(g_L);
    open_sandbox(g_L);

    /* api = { core = {...}, drive = {...} } */
    lua_newtable(g_L);
    api_index = lua_gettop(g_L);
    lua_newtable(g_L);
    lua_pushcfunction(g_L, lua_core_log);
    lua_setfield(g_L, -2, "log");
    lua_pushcfunction(g_L, lua_core_screen);
    lua_setfield(g_L, -2, "screen");
    lua_setfield(g_L, api_index, "core");
    lua_newtable(g_L);
    {
        static luaL_Reg const OWN[] = {
            { "await", lua_await }, { "pump", lua_pump }, { "symbol", lua_symbol },
            { "symbol_name", lua_symbol_name }, { "ledger", lua_ledger }, { "report", lua_report },
            { "finish", lua_finish }, { "quit", lua_quit }, { "session", lua_session },
            { "shot", lua_shot }, { "component", lua_component }, { "status", drive_status }, { "tab_by_name", lua_tab_by_name }, { NULL, NULL }
        };
        luaL_setfuncs(g_L, OWN, 0);
    }
    install_forwarders(g_L, lua_gettop(g_L));
    lua_setfield(g_L, api_index, "drive");

    chunk_build(&chunk);
    if( luaL_loadbufferx(g_L, chunk.p, chunk.n, "=quest_driver", "t") != LUA_OK )
    {
        fprintf(stderr, "rldrive: driver chunk: %s\n", lua_tostring(g_L, -1));
        return 2;
    }
    free(chunk.p);
    if( lua_pcall(g_L, 0, 1, 0) != LUA_OK )
    {
        fprintf(stderr, "rldrive: driver chunk: %s\n", lua_tostring(g_L, -1));
        return 2;
    }
    plugin_index = lua_gettop(g_L);
    lua_getfield(g_L, plugin_index, "on_start");
    lua_pushvalue(g_L, api_index);
    if( lua_pcall(g_L, 1, 0, 0) != LUA_OK )
    {
        fprintf(stderr, "rldrive: on_start: %s\n", lua_tostring(g_L, -1));
        return 2;
    }

    /* The frame loop: one pump per client cycle (20 ms), as the C client's
     * on_frame_start does, until the script finishes. */
    while( !g_finished )
    {
        long t0 = now_ms();
        long spent;
        lua_getfield(g_L, plugin_index, "on_frame_start");
        lua_pushvalue(g_L, api_index);
        if( lua_pcall(g_L, 1, 0, 0) != LUA_OK )
        {
            fprintf(stderr, "rldrive: on_frame_start: %s\n", lua_tostring(g_L, -1));
            ledger_write("driver-error", "FAIL", 0, "", lua_tostring(g_L, -1));
            finish(1);
            break;
        }
        spent = now_ms() - t0;
        if( spent < FRAME_MS )
            usleep((useconds_t)(FRAME_MS - spent) * 1000);
    }
    fprintf(stderr, "quest-driver: finished, exit %d\n", g_finish_code);
    return g_finish_code;
}
