/*
 * quest-driver: the module, the scheduler and the verbs nobody else owns.
 *
 * Owner: core-scheduler (docs/ARCHITECT.md).
 *
 * What is HERE:
 *   - registration of the test-only `api.drive` module, gated on
 *     ContentTest_Enabled(), assembled from the six per-group function arrays;
 *   - the Lua argument/result helpers every group uses;
 *   - the coroutine scheduler and the `await` primitive;
 *   - the content-symbol trampolines, t.cheat, t.settle, t.finish;
 *   - the chunk composition the sandbox's missing `require` forces on us.
 *
 * What is NOT here, and must not be added here: any verb another group owns.
 * Six files each define their own thunks; this one calls their six Register
 * functions in a fixed order and never knows their names.
 */

#include "plugin/torirs_plugin_drive.h"

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER

#include "app.h"
#include "game/content_test.h"
#include "plugin/torirs_plugin_lua.h"
#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_content.h"
#include "torirsserver/torirs_server_embed.h"

#include "lauxlib.h"
#include "lua.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The driver is a singleton by construction: one client, one quest, one
 * coroutine. A second App in the same process is not a state this can be in,
 * so the app is remembered rather than threaded through every thunk. */
static struct App* g_app;
static int g_finished;
static int g_finish_code;

/* Handed in once a frame by content_test.c (PluginDriveCore_SetEmbed): the
 * only place that already turns this run's NetTransport into a
 * struct ToriRSServerEmbed*. NULL on a socket-server run -- DriveCore_Cheat's
 * fallback there is U8, not built yet. */
static struct ToriRSServerEmbed* g_embed;

void
PluginDriveCore_SetEmbed(struct ToriRSServerEmbed* embed)
{
    g_embed = embed;
}

/* verbs-pointer's cross-group seam report (torirs_plugin_drive_pointer.c):
 * DrivePointer_MouseMove/MouseButton push onto the live command bus, which
 * content_test.c already receives every frame as ContentTest_Begin's own
 * `bus` parameter. Handed over the same way g_embed is. */
static struct ToriRS_CmdBus* g_cmdbus;

void
PluginDriveCore_SetCmdBus(struct ToriRS_CmdBus* bus)
{
    g_cmdbus = bus;
}

struct ToriRS_CmdBus*
PluginDriveCore_CmdBus(void)
{
    return g_cmdbus;
}

/* ------------------------------------------------------------- composition */

static char* g_compose;
static int g_compose_length;
static int g_compose_capacity;

/*
 * The manifest entry every part hangs off. `source=` in
 * script/plugins/quest_driver.ini names quest_driver.lua; the parts below are
 * prepended to it, in this order, and the entry file is the ONLY one with a
 * top-level `return plugin` -- a part with one would truncate the chunk.
 */
static char const* const DRIVE_SCRIPT_PARTS[] = {
    "plugins/quest_driver/core.lua",
    "plugins/quest_driver/state.lua",
    "plugins/quest_driver/chat.lua",
    "plugins/quest_driver/read.lua",
    "plugins/quest_driver/pointer.lua",
    "plugins/quest_driver/world.lua",
    "plugins/quest_driver/ui.lua",
    "plugins/quest_driver/quest.lua",
};

/* The manifest identity, not a file name: the loader asks by plugin name so a
 * renamed source file cannot silently drop the parts. */
#define DRIVE_PLUGIN_NAME "quest-driver"

int
PluginDrive_ScriptPartCount(char const* plugin_name)
{
    assert(plugin_name);
    if( strcmp(plugin_name, DRIVE_PLUGIN_NAME) != 0 )
        return 0;
    return (int)(sizeof(DRIVE_SCRIPT_PARTS) / sizeof(DRIVE_SCRIPT_PARTS[0]));
}

char const*
PluginDrive_ScriptPartPath(char const* plugin_name, int index)
{
    assert(plugin_name);
    assert(strcmp(plugin_name, DRIVE_PLUGIN_NAME) == 0);
    assert(index >= 0);
    (void)plugin_name;
    assert(index < (int)(sizeof(DRIVE_SCRIPT_PARTS) / sizeof(DRIVE_SCRIPT_PARTS[0])));
    return DRIVE_SCRIPT_PARTS[index];
}

void
PluginDrive_ComposeReset(void)
{
    free(g_compose);
    g_compose = NULL;
    g_compose_length = 0;
    g_compose_capacity = 0;
}

void
PluginDrive_ComposeAppend(char const* data, int length)
{
    assert(data);
    assert(length >= 0);
    if( length == 0 )
        return;
    /* +2: the newline that keeps a part's last line from running into the
     * next part's first, and the NUL the Lua loader wants. */
    if( g_compose_length + length + 2 > g_compose_capacity )
    {
        int capacity = g_compose_capacity ? g_compose_capacity * 2 : 16384;
        while( capacity < g_compose_length + length + 2 )
            capacity *= 2;
        g_compose = realloc(g_compose, (size_t)capacity);
        assert(g_compose);
        g_compose_capacity = capacity;
    }
    memcpy(g_compose + g_compose_length, data, (size_t)length);
    g_compose_length += length;
    g_compose[g_compose_length++] = '\n';
    g_compose[g_compose_length] = '\0';
}

char*
PluginDrive_ComposeTake(int* out_length)
{
    char* taken = g_compose;

    assert(out_length);
    *out_length = g_compose_length;
    g_compose = NULL;
    g_compose_length = 0;
    g_compose_capacity = 0;
    return taken;
}

/* ---------------------------------------------------------------- lifecycle */

char const*
PluginDrive_QuestScriptPath(void)
{
    char const* path = getenv("TORIRS_QUEST_SCRIPT");
    return path && *path ? path : NULL;
}

char const*
DriveCore_SessionDir(void)
{
    char const* dir = getenv("TORIRS_CONTENT_TEST");
    return dir && *dir ? dir : NULL;
}

struct App*
PluginDrive_App(void)
{
    return g_app;
}

/* Defined below, in the scheduler section: writes the ledger's trailing
 * SUMMARY row. Forward-declared here because PluginDrive_Finish is the one
 * guaranteed convergence point for both an explicit t.finish(code) and a
 * coroutine the scheduler finishes on its own (drive_scheduler_handle_status)
 * -- the summary must be written exactly once, whichever path gets there
 * first. */
static void drive_ledger_write_summary(int code);

void
PluginDrive_Finish(int code)
{
    if( !g_finished )
        drive_ledger_write_summary(code);
    g_finished = 1;
    g_finish_code = code;
}

int
PluginDrive_Finished(int* out_code)
{
    assert(out_code);
    *out_code = g_finish_code;
    return g_finished;
}

int
PluginDrive_ClockWantsStep(struct App* app)
{
    assert(app);
    (void)app;
    /* The screenshot-in-flight hold is content_test.c's own state
     * (capture_path) and is checked there, not here -- this file has no
     * business knowing that variable's name. What THIS answers is narrower:
     * is there a quest coroutine that still has work to do? A run with no
     * TORIRS_QUEST_SCRIPT, or one that finished (crashed or returned), wants
     * no virtual time at all -- the ordinary mailbox path (or nothing) is
     * correct for it. */
    return g_app && PluginDrive_QuestScriptPath() && !g_finished;
}

/* -------------------------------------------------------------- Lua helpers */

int
PluginDrive_ArgInt(struct lua_State* L, int index)
{
    assert(L);
    return (int)luaL_checkinteger(L, index);
}

int
PluginDrive_ArgOptInt(struct lua_State* L, int index, int fallback)
{
    assert(L);
    return (int)luaL_optinteger(L, index, fallback);
}

char const*
PluginDrive_ArgString(struct lua_State* L, int index)
{
    assert(L);
    return luaL_checkstring(L, index);
}

int
PluginDrive_PushResult(struct lua_State* L, enum DriveResult result, char const* detail)
{
    assert(L);
    lua_pushstring(L, DriveResultName(result));
    if( detail )
        lua_pushstring(L, detail);
    else
        lua_pushnil(L);
    return 2;
}

/* ---------------------------------------------------------- content symbols */

/* Index by enum DriveSymbolKind (torirs_plugin_drive.h). One entry per row of
 * the header's own list; DRIVE_SYMBOL_KIND_COUNT keeps this array honest. */
static enum ToriRSServerPackKind const DRIVE_SYMBOL_PACK[DRIVE_SYMBOL_KIND_COUNT] = {
    TORIRSSERVER_PACK_NPC,
    TORIRSSERVER_PACK_OBJ,
    TORIRSSERVER_PACK_LOC,
    TORIRSSERVER_PACK_COMPONENT,
    TORIRSSERVER_PACK_INTERFACE,
    TORIRSSERVER_PACK_VARP,
    TORIRSSERVER_PACK_VARBIT,
    TORIRSSERVER_PACK_STAT,
    TORIRSSERVER_PACK_INV,
};

static char const* const DRIVE_SYMBOL_KIND_NAMES[DRIVE_SYMBOL_KIND_COUNT] = {
    "npc", "obj", "loc", "component", "interface", "varp", "varbit", "stat", "inv",
};

/* -1 on a typo: a test's own mistake, not a contract violation, and the Lua
 * thunks below turn it into a raised error naming the string the test wrote. */
static int
drive_symbol_kind_from_name(char const* name)
{
    int kind;
    assert(name);
    for( kind = 0; kind < DRIVE_SYMBOL_KIND_COUNT; kind++ )
        if( strcmp(name, DRIVE_SYMBOL_KIND_NAMES[kind]) == 0 )
            return kind;
    return -1;
}

enum DriveResult
DriveSymbol_Lookup(enum DriveSymbolKind kind, char const* name, int* out_id)
{
    int id;
    assert(name);
    assert(out_id);
    assert(kind >= 0);
    assert(kind < DRIVE_SYMBOL_KIND_COUNT);

    id = ToriRSServer_ContentSymbol(DRIVE_SYMBOL_PACK[kind], name);
    *out_id = id;
    return id >= 0 ? DRIVE_OK : DRIVE_NOT_FOUND;
}

enum DriveResult
DriveSymbol_Name(enum DriveSymbolKind kind, int id, char* out, int out_cap)
{
    char const* name;
    assert(out);
    assert(out_cap > 0);
    assert(kind >= 0);
    assert(kind < DRIVE_SYMBOL_KIND_COUNT);

    name = ToriRSServer_ContentSymbolName(DRIVE_SYMBOL_PACK[kind], id);
    if( !name )
    {
        out[0] = '\0';
        return DRIVE_NOT_FOUND;
    }
    snprintf(out, (size_t)out_cap, "%s", name);
    return DRIVE_OK;
}

enum DriveResult
DriveCore_Cheat(struct App* app, char const* text)
{
    struct ToriRSServer* srv;
    int result;

    assert(app);
    assert(text);
    (void)app;

    if( !g_embed )
        /* Socket-server run: t.cheat's packet fallback is U6/U8, not first
         * class yet. Answering `unsupported` rather than silently doing
         * nothing keeps that gap visible to a test that hits it. */
        return DRIVE_UNSUPPORTED;

    /* net_out_client_cheat writes the body without the leading "::"
     * (torirs_server_world.c:7767-7769); handle_cheat only ever sees that
     * stripped form and in turn only strips the server-side "~" namespace
     * escape (:7771-7774). A test author spells a cheat the way every
     * documented example does -- "::setlevel cooking 10" -- so this call
     * site is the one place that owns undoing the "::" the real client
     * would already have removed. "::~foo" becomes "~foo", same as a real
     * client's packet; "~foo" and a bare "foo" are untouched. */
    if( text[0] == ':' && text[1] == ':' )
        text += 2;

    srv = ToriRSServer_EmbedWorld(g_embed);
    assert(srv);
    /* The WHOLE of handle_cheat's dispatch -- content's `[debugproc]` first,
     * then the C ladder -- not just its first half. This used to call
     * ToriRSServer_RunDebugprocForTest, which reaches only content, so
     * `::give`, `::setlevel`, `::spawn`, `::wield`, `::tele <x> <z>` and every
     * other engine cheat answered `no_row` and did nothing at all to the test
     * that asked for one (docs/QUEST_SERVER_CHEATS.md B). The verdict
     * vocabulary is unchanged, so the mapping below is too. */
    result = (int)ToriRSServer_RunCheatForTest(srv, text);
    if( result == TORIRSSERVER_TRIGGER_FAILED )
        return DRIVE_REFUSED;
    if( result == TORIRSSERVER_TRIGGER_RAN )
        return DRIVE_OK;
    return DRIVE_NO_ROW;
}

enum DriveResult
DriveCore_Settled(struct App* app, int* out_settled)
{
    assert(app);
    assert(out_settled);
    /* The same three checks content_test.c's own static `settled()` makes
     * (content_test.c:132-135) -- t.settle exists to export exactly this. */
    *out_settled = !App_AsyncPending(app) && App_FrameSettled(app) && !app->world_load_inflight;
    return DRIVE_OK;
}

/* -------------------------------------------------------------- scheduler */

/*
 * One coroutine per process (docs/QUEST_DRIVER_DESIGN.md). `g_await` is the
 * ONE outstanding drive.await; the driver never has two, because the
 * sandbox's Lua has no way to start a second thread of its own.
 */
struct DriveAwait
{
    int active;
    int level_ref;   /* LUA_NOREF, or a registry ref to a 0-arg predicate */
    int match_ref;   /* LUA_NOREF, or a registry ref to a 1-arg predicate */
    int event_kind;  /* -1, or an enum App_DriveEventKind to filter on */
    int deadline_cycle; /* -1 = no deadline (level-only, never reached here) */
    char note[96];
};

static lua_State* g_thread;
static int g_thread_ref = LUA_NOREF;
static int g_started;
static uint32_t g_event_cursor;
static struct DriveAwait g_await;
static char g_quest_name[64];

/* See the doc comment on the declaration (torirs_plugin_drive.h): B0's
 * answer to content_test.c's forced-draw gap in quest mode. */
int
PluginDriveCore_LevelAwaitPending(void)
{
    return g_await.active && g_await.level_ref != LUA_NOREF;
}

static int g_ledger_index;
static int g_ledger_pass;
static int g_ledger_fail;
/* BLOCKED is its own count, not a third kind of failure.
 *
 * A quest that cannot be finished today -- a boss with no skip arm, a step
 * that needs a verb nobody has written -- must say so in the ledger rather
 * than be absent from it or lie about passing. Folding it into `fail` would
 * make a suite of honest stubs look like a suite of regressions, and folding
 * it into `pass` would make the stub indistinguishable from the real thing.
 * So the SUMMARY verdict stays keyed on `fail == 0` alone, and this count
 * rides alongside it for gate.py to report separately. */
static int g_ledger_blocked;
static long g_ledger_total_ticks;

static void
drive_quest_name_from_path(char const* path)
{
    char const* slash;
    char const* base;
    char const* dot;
    int length;

    assert(path);
    slash = strrchr(path, '/');
    base = slash ? slash + 1 : path;
    dot = strrchr(base, '.');
    length = dot ? (int)(dot - base) : (int)strlen(base);
    if( length >= (int)sizeof(g_quest_name) )
        length = (int)sizeof(g_quest_name) - 1;
    memcpy(g_quest_name, base, (size_t)length);
    g_quest_name[length] = '\0';
}

static void
drive_ledger_write(char const* step, char const* verdict, int ticks, char const* shots, char const* detail)
{
    char path[1024];
    char const* dir;
    FILE* f;

    assert(step);
    assert(verdict);
    assert(shots);
    assert(detail);

    /*
     * THE LEDGER CLOSES AT t.finish, and this is where it closes.
     *
     * drive_ledger_write_summary has already appended SUMMARY to the file by
     * the time g_finished is set (PluginDrive_Finish, above), so a row written
     * after that point lands BELOW the summary: the counts in that line no
     * longer describe the rows above it, gate.py reads a file whose last line
     * is a step rather than a verdict, and a quest that stopped itself at step
     * three can still publish twenty rows of whatever ran afterwards. Two
     * authors in the 2026-09-19 pilot reported "blocked" while their file ran
     * on through expect_complete and appended FAIL rows past its own SUMMARY.
     *
     * So the writer refuses, once, out loud. The script-side half of the same
     * rule -- the coroutine parks at the first row or shot it tries after
     * finishing, so nothing downstream of it runs at all -- is
     * quest_driver/core.lua's `park`; this is the floor under it, and it holds
     * for any caller, including one that reaches api.drive.ledger directly.
     * (drive_scheduler_handle_status's own "script-error" row is written
     * before PluginDrive_Finish, so it is never the row being refused here.)
     */
    if( g_finished )
    {
        fprintf(stderr, "quest-driver: row after finish ignored: %s\n", step);
        return;
    }

    g_ledger_index++;
    g_ledger_total_ticks += ticks;
    if( strcmp(verdict, "PASS") == 0 )
        g_ledger_pass++;
    else if( strcmp(verdict, "BLOCKED") == 0 )
        g_ledger_blocked++;
    else
        g_ledger_fail++;

    fprintf(stderr, "QUEST %s %s %s ticks=%d shots=%s%s%s\n",
        g_quest_name[0] ? g_quest_name : "quest", verdict, step, ticks, shots,
        detail[0] ? " why=" : "", detail);

    dir = DriveCore_SessionDir();
    if( !dir )
        return; /* stderr mirror is all a run with no session dir gets */

    snprintf(path, sizeof(path), "%s/ledger.tsv", dir);
    f = fopen(path, g_ledger_index == 1 ? "w" : "a");
    assert(f);
    if( g_ledger_index == 1 )
        fprintf(f, "quest-ledger-v1\nindex\tstep\tverdict\tticks\tshots\tdetail\n");
    fprintf(f, "%d\t%s\t%s\t%d\t%s\t%s\n", g_ledger_index, step, verdict, ticks, shots, detail);
    fclose(f);
}

static void
drive_ledger_write_summary(int code)
{
    char path[1024];
    char const* dir = DriveCore_SessionDir();
    FILE* f;

    if( !dir )
        return;
    snprintf(path, sizeof(path), "%s/ledger.tsv", dir);
    f = fopen(path, g_ledger_index == 0 ? "w" : "a");
    assert(f);
    if( g_ledger_index == 0 )
        fprintf(f, "quest-ledger-v1\nindex\tstep\tverdict\tticks\tshots\tdetail\n");
    /* ` blocked=K` is APPENDED, and only when there is one to report.
     *
     * The column is a free-text token bag that gate.py splits on whitespace
     * and reads `key=value` out of, so a new token is compatible by
     * construction -- but every ledger already published under
     * OSRS-Content/.../quest_tests/ was written without it, and a suite whose
     * rows are all PASS or FAIL should keep producing byte-identical summaries
     * to the ones a human has already read. So the token appears exactly when
     * it carries information. */
    fprintf(f, "SUMMARY\t%d\t%s\t%ld\texit=%d\tpass=%d fail=%d",
        g_ledger_index, g_ledger_fail == 0 ? "PASS" : "FAIL", g_ledger_total_ticks, code,
        g_ledger_pass, g_ledger_fail);
    if( g_ledger_blocked > 0 )
        fprintf(f, " blocked=%d", g_ledger_blocked);
    fprintf(f, "\n");
    fclose(f);
}

static void
drive_push_event(struct lua_State* L, struct App_DriveEvent const* ev)
{
    assert(L);
    assert(ev);
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, (lua_Integer)ev->serial);
    lua_setfield(L, -2, "serial");
    lua_pushstring(L, DriveEventKindName((enum App_DriveEventKind)ev->kind));
    lua_setfield(L, -2, "kind");
    lua_pushinteger(L, ev->cycle);
    lua_setfield(L, -2, "cycle");
    lua_pushinteger(L, ev->a);
    lua_setfield(L, -2, "a");
    lua_pushinteger(L, ev->b);
    lua_setfield(L, -2, "b");
    lua_pushinteger(L, ev->c);
    lua_setfield(L, -2, "c");
    lua_pushinteger(L, ev->d);
    lua_setfield(L, -2, "d");
}

/* Both predicates run on g_thread directly -- legal even while it sits
 * suspended in a yield, as long as nothing below tries to yield itself, which
 * neither a level() nor a match() predicate has any means to do (the sandbox
 * has no `coroutine`). A predicate error is the test author's bug, not a
 * scheduler fault; it is reported and treated as "not satisfied yet", which
 * surfaces as an ordinary timeout rather than a second, confusing crash path. */
static int
drive_await_level_true(int level_ref)
{
    int result;
    if( level_ref == LUA_NOREF )
        return 0;
    lua_rawgeti(g_thread, LUA_REGISTRYINDEX, level_ref);
    if( lua_pcall(g_thread, 0, 1, 0) != LUA_OK )
    {
        fprintf(stderr, "QUEST %s await level() error: %s\n",
            g_quest_name, lua_tostring(g_thread, -1));
        lua_pop(g_thread, 1);
        return 0;
    }
    result = lua_toboolean(g_thread, -1);
    lua_pop(g_thread, 1);
    return result;
}

static int
drive_await_match_true(int match_ref, struct App_DriveEvent const* ev)
{
    int result;
    if( match_ref == LUA_NOREF )
        return 0;
    lua_rawgeti(g_thread, LUA_REGISTRYINDEX, match_ref);
    drive_push_event(g_thread, ev);
    if( lua_pcall(g_thread, 1, 1, 0) != LUA_OK )
    {
        fprintf(stderr, "QUEST %s await match() error: %s\n",
            g_quest_name, lua_tostring(g_thread, -1));
        lua_pop(g_thread, 1);
        return 0;
    }
    result = lua_toboolean(g_thread, -1);
    lua_pop(g_thread, 1);
    return result;
}

static void
drive_await_free_refs(void)
{
    if( g_await.level_ref != LUA_NOREF )
    {
        luaL_unref(g_thread, LUA_REGISTRYINDEX, g_await.level_ref);
        g_await.level_ref = LUA_NOREF;
    }
    if( g_await.match_ref != LUA_NOREF )
    {
        luaL_unref(g_thread, LUA_REGISTRYINDEX, g_await.match_ref);
        g_await.match_ref = LUA_NOREF;
    }
    g_await.active = 0;
}

static void
drive_scheduler_handle_status(int status, char const* error)
{
    if( status == LUA_YIELD )
        return; /* lua_drive_await armed g_await again during this resume. */

    if( status != LUA_OK )
    {
        drive_ledger_write("script-error", "FAIL", 0, "", error);
        if( !g_finished )
            PluginDrive_Finish(1);
    }
    else if( !g_finished )
        PluginDrive_Finish(0);

    PluginLua_ThreadDestroy(g_thread_ref);
    g_thread = NULL;
    g_thread_ref = LUA_NOREF;
}

/* Resume with the two-value verdict every await primitive answers with; this
 * IS what await(...) returns to the script, via plain (non-k) lua_yield
 * semantics -- the values passed to lua_resume become the yielded call's own
 * return values with no continuation function needed. */
static void
drive_scheduler_resume(enum DriveResult result, char const* detail)
{
    int status;
    int nresults;
    char error[256];

    assert(g_thread);
    lua_pushstring(g_thread, DriveResultName(result));
    if( detail )
        lua_pushstring(g_thread, detail);
    else
        lua_pushnil(g_thread);
    status = PluginLua_ThreadResume(g_thread, 2, &nresults, error, sizeof(error));
    drive_scheduler_handle_status(status, error);
}

static void
drive_await_settle(enum DriveResult result, char const* detail)
{
    drive_await_free_refs();
    drive_scheduler_resume(result, detail);
}

/* The same gate tools/content_selftest.py:92 waits on before it treats the
 * embedded session as usable ("online" && "world_ready" in state_json,
 * content_test.c:154-160): a client_id-0 handshake finished, and this
 * client's own worldview has finished loading. Before that, `g_app->world`
 * can be NULL, the server has no active player, and resuming the quest
 * coroutine here means every content symbol trampoline answers `not_found`
 * and every t.cheat answers `no_row` against a world that has not booted --
 * a test that "passed" having driven nothing (docs/ARCHITECT.md: "the
 * embedded world does not tick until login completes"). */
static int
drive_world_ready(void)
{
    if( !g_app || !g_app->world )
        return 0;
    if( !g_embed || !ToriRSServer_EmbedOnline(g_embed, 0) )
        return 0;
    return g_app->world->load_complete;
}

static void
drive_scheduler_start(void)
{
    char const* path = PluginDrive_QuestScriptPath();
    FILE* f;
    long length;
    char* source;
    size_t read_bytes;
    int status;
    int nresults;
    char error[256];

    if( !path )
        return;

    drive_quest_name_from_path(path);

    f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    source = malloc((size_t)length + 1);
    assert(source);
    read_bytes = fread(source, 1, (size_t)length, f);
    assert(read_bytes == (size_t)length);
    /* An assert is nothing under NDEBUG (OPT=1), so a short read is an
     * unasserted possibility in the shipping build; the buffer is still
     * NUL-terminated at `length` below regardless. */
    (void)read_bytes;
    fclose(f);
    source[length] = '\0';

    g_thread = PluginLua_ThreadCreate(DRIVE_PLUGIN_NAME, path, source, (int)length, &g_thread_ref);
    free(source);
    assert(g_thread);

    /* The `arg` half of the bootstrap's (loader, arg) -- QD_ROOT is the one
     * global quest_driver/core.lua exports so C can reach it without a new
     * api.drive primitive (every other name in QD stays a fast chunk-scope
     * local). This is `t` in the test's own `run = function(t) ... end`. */
    lua_getglobal(g_thread, "QD_ROOT");

    status = PluginLua_ThreadResume(g_thread, 2, &nresults, error, sizeof(error));
    drive_scheduler_handle_status(status, error);
}

static void
drive_pump_once(void)
{
    struct App_DriveEvent events[32];
    int count = 0;
    uint32_t next_serial = g_event_cursor;
    int i;

    if( !g_app || !g_thread )
        return;

    /* DRIVE_REFUSED just means the cursor fell off the ring's tail; the level
     * re-check below is still the source of truth for a level-only await, and
     * an event-only await that was missed this way still resolves the moment
     * its deadline passes -- a late timeout, never a silent hang. */
    (void)App_DriveEventsRead(g_app, g_event_cursor, events, 32, &count, &next_serial);
    g_event_cursor = next_serial;

    if( !g_await.active )
        return;

    for( i = 0; i < count; i++ )
    {
        if( g_await.event_kind >= 0 && events[i].kind != g_await.event_kind )
            continue;
        if( !drive_await_match_true(g_await.match_ref, &events[i]) )
            continue;
        drive_await_settle(DRIVE_OK, NULL);
        /* drive_await_settle resumes the coroutine, which routinely arms a
         * NEW await in that same resume (e.g. chat.continue_ awaiting
         * resume_answered, then chat.drain awaiting sub_mounted). If both
         * edges landed in this same batch, the new await's match can still
         * be sitting at events[i+1..]; stop scanning only once the coroutine
         * has finished (g_thread torn down by drive_scheduler_handle_status)
         * or nothing is armed to check events against, never merely because
         * this event settled the old one (R3). */
        if( !g_thread || !g_await.active )
            return;
    }

    if( !g_thread || !g_await.active )
        return;

    if( drive_await_level_true(g_await.level_ref) )
    {
        drive_await_settle(DRIVE_OK, NULL);
        return;
    }

    if( g_await.deadline_cycle >= 0 && g_app->world &&
        g_app->world->cycle >= g_await.deadline_cycle )
    {
        drive_await_settle(DRIVE_TIMEOUT, g_await.note[0] ? g_await.note : NULL);
    }
}

/* ------------------------------------------------------------- core verbs */

static int
lua_drive_await(struct lua_State* L)
{
    int deadline_ticks;
    int level_ref = LUA_NOREF;
    int match_ref = LUA_NOREF;
    int kind = -1;
    char note[96] = "";
    int cycle_now;

    luaL_checktype(L, 1, LUA_TTABLE);
    deadline_ticks = PluginDrive_ArgOptInt(L, 2, 0);

    lua_getfield(L, 1, "event");
    if( lua_type(L, -1) == LUA_TSTRING )
        kind = DriveEventKindFromName(lua_tostring(L, -1));
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

    /* EDGE + LEVEL: an already-true level predicate resolves without ever
     * registering an await, let alone yielding. */
    if( drive_await_level_true(level_ref) )
    {
        if( level_ref != LUA_NOREF ) luaL_unref(L, LUA_REGISTRYINDEX, level_ref);
        if( match_ref != LUA_NOREF ) luaL_unref(L, LUA_REGISTRYINDEX, match_ref);
        return PluginDrive_PushResult(L, DRIVE_OK, NULL);
    }

    /* "A deadline of 0 means level only, never yield" (torirs_plugin_drive.h):
     * the level already failed above, so this resolves as an immediate
     * timeout rather than suspending a coroutine that would never be woken. */
    if( deadline_ticks <= 0 )
    {
        if( level_ref != LUA_NOREF ) luaL_unref(L, LUA_REGISTRYINDEX, level_ref);
        if( match_ref != LUA_NOREF ) luaL_unref(L, LUA_REGISTRYINDEX, match_ref);
        return PluginDrive_PushResult(L, DRIVE_TIMEOUT, note[0] ? note : NULL);
    }

    cycle_now = g_app && g_app->world ? (int)g_app->world->cycle : 0;
    g_await.active = 1;
    g_await.level_ref = level_ref;
    g_await.match_ref = match_ref;
    g_await.event_kind = kind;
    /*
     * `deadline_ticks` is SERVER ticks -- that is the unit every verb's
     * default deadline in docs/QUEST_DRIVER_PLAN.md is written in -- while
     * `world->cycle` counts CLIENT logic cycles. They are not the same clock:
     * APP_SERVER_TICK_LOGIC_CYCLES of the second make one of the first, and
     * src/app.h calls that "the only ratio between the client's two clocks".
     *
     * Conflating them is what made the whole verb layer look broken: an await
     * for "20 ticks" expired after 20 cycles, which is 400 ms of virtual time
     * and ZERO server ticks, so the server had not yet published a single
     * NPC_INFO and every world query answered honestly that it could see
     * nothing. Waiting the same 20 in the right unit fills the npc pool.
     */
    g_await.deadline_cycle = cycle_now + deadline_ticks * APP_SERVER_TICK_LOGIC_CYCLES;
    snprintf(g_await.note, sizeof(g_await.note), "%s", note);

    return lua_yield(L, 0);
}

static int
lua_drive_pump(struct lua_State* L)
{
    /* Called once a frame from the driver plugin's own on_frame_start
     * (docs/ARCHITECT.md A1: one pump, not two -- the ring is a cursor read,
     * so nothing stamped between pumps is ever lost). The first call once
     * the world is ready also starts the coroutine: a run with no
     * TORIRS_QUEST_SCRIPT still loads the driver plugin, so starting lazily
     * here, not from PluginDrive_Init, keeps that boot from ever creating a
     * thread at all. Before the world is ready this is a no-op every frame,
     * not a wait with its own state -- login can take an arbitrary number of
     * frames and there is nothing to remember between them. */
    (void)L;
    if( !g_started && drive_world_ready() )
    {
        g_started = 1;
        drive_scheduler_start();
    }
    drive_pump_once();
    return 0;
}

static int
lua_drive_events(struct lua_State* L)
{
    struct App_DriveEvent events[64];
    int count = 0;
    uint32_t after = (uint32_t)PluginDrive_ArgOptInt(L, 1, 0);
    uint32_t next_serial = after;
    enum DriveResult result;
    int i;

    result = g_app ? App_DriveEventsRead(g_app, after, events, 64, &count, &next_serial)
                    : DRIVE_UNSUPPORTED;

    lua_pushstring(L, DriveResultName(result));
    lua_createtable(L, count, 1);
    for( i = 0; i < count; i++ )
    {
        drive_push_event(L, &events[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushinteger(L, (lua_Integer)next_serial);
    lua_setfield(L, -2, "next_serial");
    return 2;
}

/* SERVER ticks, which is the unit every deadline and every t.ticks(n) in a
 * quest test is written in. The client's own world cycle is 30x faster, and
 * returning that here is what made t.ticks(10) mean 200 ms instead of six
 * seconds -- see the note on the deadline in drive_await. */
static int
lua_drive_tick(struct lua_State* L)
{
    lua_pushinteger(
        L,
        g_app && g_app->world
            ? (lua_Integer)(g_app->world->cycle / APP_SERVER_TICK_LOGIC_CYCLES)
            : 0);
    return 1;
}

static int
lua_drive_settled(struct lua_State* L)
{
    int settled = 0;
    enum DriveResult result = g_app ? DriveCore_Settled(g_app, &settled) : DRIVE_UNSUPPORTED;
    lua_pushboolean(L, result == DRIVE_OK && settled);
    return 1;
}

static int
lua_drive_symbol(struct lua_State* L)
{
    char const* kind_name = PluginDrive_ArgString(L, 1);
    char const* name = PluginDrive_ArgString(L, 2);
    int kind = drive_symbol_kind_from_name(kind_name);
    int id = -1;
    enum DriveResult result;

    if( kind < 0 )
        return luaL_error(L, "drive.symbol: unknown kind '%s'", kind_name);
    result = DriveSymbol_Lookup((enum DriveSymbolKind)kind, name, &id);
    lua_pushstring(L, DriveResultName(result));
    if( result == DRIVE_OK )
        lua_pushinteger(L, id);
    else
        lua_pushnil(L);
    return 2;
}

static int
lua_drive_symbol_name(struct lua_State* L)
{
    char const* kind_name = PluginDrive_ArgString(L, 1);
    int id = PluginDrive_ArgInt(L, 2);
    int kind = drive_symbol_kind_from_name(kind_name);
    char buf[128];
    enum DriveResult result;

    if( kind < 0 )
        return luaL_error(L, "drive.symbol_name: unknown kind '%s'", kind_name);
    result = DriveSymbol_Name((enum DriveSymbolKind)kind, id, buf, sizeof(buf));
    lua_pushstring(L, DriveResultName(result));
    if( result == DRIVE_OK )
        lua_pushstring(L, buf);
    else
        lua_pushnil(L);
    return 2;
}

static int
lua_drive_cheat(struct lua_State* L)
{
    char const* text = PluginDrive_ArgString(L, 1);
    enum DriveResult result = g_app ? DriveCore_Cheat(g_app, text) : DRIVE_UNSUPPORTED;
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_ledger(struct lua_State* L)
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

    drive_ledger_write(step, verdict, ticks, shots, detail);
    lua_pop(L, 5);
    return PluginDrive_PushResult(L, DRIVE_OK, NULL);
}

static int
lua_drive_report(struct lua_State* L)
{
    char const* text = PluginDrive_ArgString(L, 1);
    fprintf(stderr, "QUEST %s\n", text);
    return 0;
}

static int
lua_drive_finish(struct lua_State* L)
{
    PluginDrive_Finish(PluginDrive_ArgOptInt(L, 1, 0));
    return PluginDrive_PushResult(L, DRIVE_OK, NULL);
}

static int
lua_drive_session(struct lua_State* L)
{
    char const* dir = DriveCore_SessionDir();
    char const* script = PluginDrive_QuestScriptPath();

    lua_createtable(L, 0, 2);
    lua_pushstring(L, dir ? dir : "");
    lua_setfield(L, -2, "dir");
    lua_pushstring(L, script ? script : "");
    lua_setfield(L, -2, "script");
    return 1;
}

static struct LuaFn const LUA_DRIVE_CORE_FNS[] = {
    {"await", lua_drive_await},
    {"pump", lua_drive_pump},
    {"events", lua_drive_events},
    {"tick", lua_drive_tick},
    {"settled", lua_drive_settled},
    {"symbol", lua_drive_symbol},
    {"symbol_name", lua_drive_symbol_name},
    {"cheat", lua_drive_cheat},
    {"ledger", lua_drive_ledger},
    {"report", lua_drive_report},
    {"finish", lua_drive_finish},
    {"session", lua_drive_session},
    {NULL, NULL},
};

void
PluginDriveCore_RegisterLua(struct lua_State* L, void* script)
{
    assert(L);
    assert(script);
    PluginLua_AppendModule(L, script, LUA_DRIVE_CORE_FNS);
}

/* ------------------------------------------------------------ registration */

static void
drive_install_modules(struct lua_State* L, void* script)
{
    assert(L);
    assert(script);
    /* One flat `api.drive`, assembled from six files. Order is registration
     * order only; the names are disjoint and the inventory test proves it. */
    lua_newtable(L);
    PluginDriveCore_RegisterLua(L, script);
    PluginDriveState_RegisterLua(L, script);
    PluginDriveChat_RegisterLua(L, script);
    PluginDriveRead_RegisterLua(L, script);
    PluginDrivePointer_RegisterLua(L, script);
    PluginDriveUi_RegisterLua(L, script);
    lua_setfield(L, -2, "drive");
}

void
PluginDrive_Init(struct App* app)
{
    assert(app);
    /* The same gate content_test.c uses, and for the same reason: `api.drive`
     * reaches the embedded server in-process and drives the client as a test
     * harness. It must not exist in a client a person is playing. */
    if( !ContentTest_Enabled() )
        return;
    g_app = app;
    PluginLua_SetTestModules(drive_install_modules);
}

void
PluginDrive_Shutdown(void)
{
    PluginLua_SetTestModules(NULL);
    PluginDrive_ComposeReset();
    if( g_thread )
    {
        drive_await_free_refs();
        PluginLua_ThreadDestroy(g_thread_ref);
        g_thread = NULL;
        g_thread_ref = LUA_NOREF;
    }
    g_app = NULL;
    g_embed = NULL;
    g_started = 0;
}

#else /* !TORIRS_EMBED_SERVER */

#include <assert.h>
#include <stddef.h>

/*
 * No embedded server, no quest driver. The seams the client calls
 * unconditionally answer "there is nothing here"; everything else is
 * unreachable and not defined at all.
 */

void PluginDrive_Init(struct App* app) { (void)app; }
void PluginDrive_Shutdown(void) {}
struct App* PluginDrive_App(void) { return NULL; }
char const* PluginDrive_QuestScriptPath(void) { return NULL; }
int PluginDrive_ClockWantsStep(struct App* app) { (void)app; return 0; }
int PluginDrive_ScriptPartCount(char const* plugin_name) { (void)plugin_name; return 0; }
char const* PluginDrive_ScriptPartPath(char const* plugin_name, int index)
{
    (void)plugin_name;
    (void)index;
    assert(0 && "PluginDrive_ScriptPartPath: no parts without EMBED_SERVER");
    return NULL;
}
void PluginDrive_ComposeReset(void) {}
void PluginDrive_ComposeAppend(char const* data, int length) { (void)data; (void)length; }
char* PluginDrive_ComposeTake(int* out_length)
{
    assert(out_length);
    *out_length = 0;
    return NULL;
}
void PluginDrive_Finish(int code) { (void)code; }
int PluginDrive_Finished(int* out_code)
{
    assert(out_code);
    *out_code = 0;
    return 0;
}

#endif /* TORIRS_EMBED_SERVER */
