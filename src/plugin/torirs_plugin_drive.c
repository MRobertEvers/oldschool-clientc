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
#include "game/rs_entity_sync.h"
#include "plugin/torirs_plugin_host.h"
#include "plugin/torirs_plugin_lua.h"
#include "plugin/task_plugin_io.h"
#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_boot.h"
#include "torirsserver/torirs_server_content.h"
#include "torirsserver/torirs_server_embed.h"
#include "torirsserver/torirs_server_save.h"
#include "varp/varp_manager.h"

#include "lauxlib.h"
#include "lua.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

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
    /* The runner camera split (struct App_ViewSplit): while a script's view
     * is attached its events go to its own bus, drained after the physical
     * one, so the client knows whose every press is. */
    if( g_app && g_app->view_split.attached )
        return g_app->view_split.runner_bus;
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
    "plugins/quest_driver/combat.lua",
    /* After combat.lua, which only needs to follow pointer.lua and state.lua:
     * sail.lua wraps nothing, it adds QD.sail (docs/QUEST_SUITE_KIT.md). */
    "plugins/quest_driver/sail.lua",
    "plugins/quest_driver/session.lua",
    /* After combat.lua: t.player.cast stamps QD._combat_last, the record
     * npc.await_dead_engaged holds (seam cast_spell_on_npc, 2026-09-27). */
    "plugins/quest_driver/spell.lua",
    /* The raid seam (docs/RAID_ORCHESTRATOR.md section 4): prayer.lua adds
     * QD.prayer, raid.lua adds QD.raid, ticklog.lua adds QD.ticklog. Since
     * seam22 raid.lua wraps three things (combat.lua's death fence and
     * state.lua's death record for a party member, ticklog.lua's rows for
     * `area`); they follow combat.lua because raid.lua reads
     * QD._combat_last. */
    "plugins/quest_driver/prayer.lua",
    /* ticklog.lua BEFORE raid.lua (raid seam22): raid.lua wraps
     * QD.ticklog.rows to add the `area` filter (a room's own tiles, so a room
     * test never pulls every region npc) and asserts it is there. */
    "plugins/quest_driver/ticklog.lua",
    "plugins/quest_driver/raid.lua",
    /* Last: t.cutscene wraps QD.core_row_begin (core.lua) to remember the
     * camera serial each t.exec row began at, and reads QD.shot (ui.lua)
     * (seam32 cutscene_verb_and_camera_read). */
    "plugins/quest_driver/cutscene.lua",
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

/* ON DEMAND (raid seam23; torirs_plugin_drive.h, PluginDrive_OnDemand). The
 * knob is read once; everything below that depends on it is inert without it,
 * so a test run under run.py is the run it was. */
static int g_on_demand = -1;

int
PluginDrive_OnDemand(void)
{
    if( g_on_demand < 0 )
    {
        char const* knob = getenv("TORIRS_DRIVE_ON_DEMAND");
        g_on_demand = knob && strcmp(knob, "1") == 0;
    }
    return g_on_demand;
}

/* The on-demand driver's own state. IDLE: nothing started yet. RUNNING: a
 * start was accepted (the coroutine begins at the next pump the world is
 * ready for) and has not finished. FINISHED: its SUMMARY is written; a new
 * start is allowed once the boundary has released it. */
enum DriveDemandState
{
    DRIVE_DEMAND_IDLE,
    DRIVE_DEMAND_RUNNING,
    DRIVE_DEMAND_FINISHED,
};

static enum DriveDemandState g_demand_state = DRIVE_DEMAND_IDLE;
static char g_demand_script[1024];
static char g_demand_session[1024];
/* start accepted, coroutine not yet created (lua_drive_pump creates it) */
static int g_demand_start_pending;
/* stop accepted; PluginDrive_FrameBoundary ends the run */
static int g_demand_stop_pending;
/* finished; PluginDrive_FrameBoundary releases the coroutine and reloads */
static int g_demand_release_pending;
static int g_demand_runs;
/* Read back by api.drive.status: the last ledger row and the SUMMARY line, as
 * written. Kept in every mode (a status read on a test run answers too); they
 * change nothing a row or a file says. */
static char g_last_row_step[128];
static char g_last_row_verdict[16];
static char g_summary_line[256];

/* THE SCRIPTS TAB (raid seam24, scripts_tab_every_script). A script item read
 * through the IO layer (task_plugin_io.c CreateTask_PluginScriptRead): none,
 * in flight, landed, or absent. */
enum DriveFetch
{
    DRIVE_FETCH_NONE,
    DRIVE_FETCH_PENDING,
    DRIVE_FETCH_READY,
    DRIVE_FETCH_MISSING,
};

/* The scripts manifest (tests/tests.ini), as the tab last asked for it. */
static enum DriveFetch g_tests_state = DRIVE_FETCH_NONE;
static char* g_tests_bytes;
static int g_tests_size;
static int g_tests_serial;

/* A Play (api.drive.play): the test, the fresh account it plays on, and its
 * source and fixture as the IO layer delivered them for THIS Play. The serial
 * drops a late reply to a Play that was stopped before it landed. */
static int g_demand_play;
static char g_demand_id[64];
static char g_demand_suite[16];
static char g_demand_title[192];
static char g_demand_fixture_path[TORIRS_IOITEM_MAX_PATH];
static char g_demand_account[16];
static int g_demand_legs;
static int g_demand_leg;
static int g_demand_fetch_serial;
static enum DriveFetch g_demand_source_state;
static enum DriveFetch g_demand_fixture_state;
static char* g_demand_source_bytes;
static int g_demand_source_size;
static char* g_demand_fixture_bytes;
static int g_demand_fixture_size;
/* Why the last Play never began (a missing source, a fixture that could not
 * be written): read back by api.drive.status, cleared by the next Play. */
static char g_demand_refusal[600];
/* The camera pose the client had when the FIRST Play was pressed: what every
 * later Play's fresh account starts with (QD.core_run_test puts it back after
 * the log-in). A test's camera verbs leave the pose where they put it, and a
 * logout does not reset it -- measured, seam24 fin3: tob_verzik after five
 * rooms started top-down and its click on Verzik found nothing, while the same
 * Play as the first of a client passed verzik.talk. */
static int g_demand_camera_saved;
static int g_demand_camera_yaw;
static int g_demand_camera_pitch;
static int g_demand_camera_zoom;

char const*
PluginDrive_QuestScriptPath(void)
{
    char const* path;

    if( PluginDrive_OnDemand() )
        return NULL;
    path = getenv("TORIRS_QUEST_SCRIPT");
    return path && *path ? path : NULL;
}

/* The script this driver runs: TORIRS_QUEST_SCRIPT on a test run, the started
 * one while an on-demand run is RUNNING, else NULL. */
static char const*
drive_script_path(void)
{
    if( !PluginDrive_OnDemand() )
        return PluginDrive_QuestScriptPath();
    return g_demand_state == DRIVE_DEMAND_RUNNING ? g_demand_script : NULL;
}

char const*
DriveCore_SessionDir(void)
{
    char const* dir;

    if( PluginDrive_OnDemand() )
        return g_demand_session[0] ? g_demand_session : NULL;
    dir = getenv("TORIRS_CONTENT_TEST");
    return dir && *dir ? dir : NULL;
}

struct App*
PluginDrive_App(void)
{
    return g_app;
}

/* THE PARTY RUN (raid seam17, party_run_and_verbs). The leader's client hosts
 * the world and other client processes play in it over the party link
 * (torirs_server_embed.h, TORIRS_EMBED_PARTY_*). Two consequences for the
 * driver:
 *
 * 1. On the LEADER, `srv->active_player` is "whoever the world last acted
 *    for" -- with three raiders that is the last pid the player phase walked,
 *    a member, not this client. Every server-side read and t.cheat here
 *    (varps, ticklog, vessel, the cheat ladder) means THIS client's player, so
 *    the world is handed out with client 0's player active. With one client
 *    the active player already is client 0's, and nothing is called: a solo
 *    run is untouched.
 * 2. A MEMBER (TORIRS_EMBED_PARTY_JOIN) has no world in its process: g_embed
 *    is NULL. Its script starts once it is in game (below), its t.cheat goes
 *    out as the client's own typed ::command packet, and the server-side
 *    readers answer `unsupported` as on any socket-server run. */
static struct ToriRSServer*
drive_embed_world_as_leader(void)
{
    struct ToriRSServer* srv;
    struct ToriRSServerPlayer* own;

    assert(g_embed);
    srv = ToriRSServer_EmbedWorld(g_embed);
    if( !srv )
        return NULL;
    own = ToriRSServer_EmbedPlayer(g_embed, 0);
    if( own && srv->active_player != own )
        ToriRSServer_WorldSetActive(srv, own);
    return srv;
}

static int
drive_party_member(void)
{
    char const* join = getenv("TORIRS_EMBED_PARTY_JOIN");
    return join && *join;
}

/* A member's NAMES. Every symbol a test spells -- a component
 * ("tob_partydetails:action"), a loc, an npc, a varbit -- resolves through the
 * server's content tables (ToriRSServer_ContentSymbol), which a process loads
 * when its embedded world boots. A member's world is the leader's, so its own
 * process never boots one, and without this every symbol lookup on a member
 * answered no_row. The member loads the same tables from the same content
 * tree (ToriRSServer_BootLoad: read-only symbol and config tables, no world),
 * once, before its script starts. */
static int g_member_tables_loaded;

static void
drive_member_load_tables(void)
{
    static struct ToriRSServerBootConfig config;
    int errors;

    if( g_member_tables_loaded )
        return;
    g_member_tables_loaded = 1;
    ToriRSServer_BootDefaults(&config);
    errors = ToriRSServer_BootLoad(&config);
    fprintf(stderr, "quest-driver: party member loaded the content tables from %s (%d error(s))\n",
        config.content_dir ? config.content_dir : "?", errors);
}

struct ToriRSServer*
PluginDrive_EmbedWorld(void)
{
    /* NULL, not an assert: a socket-server run has no in-process server and
     * every caller of this answers `unsupported` for that, the same way
     * DriveCore_Cheat below already does. */
    if( !g_embed )
        return NULL;
    return drive_embed_world_as_leader();
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
    /* On demand the client stays up: the boundary releases the coroutine (it
     * parks at its next row, shot or await -- quest_driver/core.lua `park`)
     * and the driver goes back to idle. */
    if( PluginDrive_OnDemand() && g_demand_state == DRIVE_DEMAND_RUNNING )
        g_demand_release_pending = 1;
}

int
PluginDrive_Finished(int* out_code)
{
    assert(out_code);
    if( PluginDrive_OnDemand() )
    {
        /* A watched client ends when its person closes it, never on a
         * script's verdict (torirs_plugin_drive.h). */
        *out_code = 0;
        return 0;
    }
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
     * correct for it. (An on-demand client answers NULL there: its clock is
     * the transport's own, never the content-test one.) */
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

    if( !g_embed && drive_party_member() )
    {
        /* A party member (see drive_embed_world_as_leader's banner): no world
         * here, so the line goes out the way a staff member's typed
         * `::command` does -- App_SendCommand, the client's own cheat packet,
         * handled for THIS player at the world's next boundary. The verdict
         * is "sent", not "ran": the server's reply line arrives as a chat
         * message (t.cheat waits for it), and the setup loop's own client
         * reads (backpack, stats, worn) are what prove the effect. */
        if( text[0] == ':' && text[1] == ':' )
            text += 2;
        return App_SendCommand(app, text) ? DRIVE_OK : DRIVE_REFUSED;
    }
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

    srv = drive_embed_world_as_leader();
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
    snprintf(g_last_row_step, sizeof(g_last_row_step), "%s", step);
    snprintf(g_last_row_verdict, sizeof(g_last_row_verdict), "%s", verdict);
    /* A legs file's rows are `leg.<k>.<name>` (QD.core_legs_drive): the tab's
     * "leg k of n". Status bookkeeping only; nothing written changes. */
    if( PluginDrive_OnDemand() && strncmp(step, "leg.", 4) == 0 && atoi(step + 4) > 0 )
        g_demand_leg = atoi(step + 4);
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

/* The SUMMARY line with its `exit=` token spelled by the caller: a code from
 * t.finish ("0", "1"), or "none" for a run stopped on demand -- the token
 * run.py's finish_unfinished_ledger writes for a run that never finished. */
static void
drive_ledger_write_summary_exit(char const* exit_text)
{
    char path[1024];
    char const* dir = DriveCore_SessionDir();
    FILE* f;
    int used;

    assert(exit_text);
    used = snprintf(g_summary_line, sizeof(g_summary_line), "SUMMARY\t%d\t%s\t%ld\texit=%s\tpass=%d fail=%d",
        g_ledger_index, g_ledger_fail == 0 ? "PASS" : "FAIL", g_ledger_total_ticks, exit_text,
        g_ledger_pass, g_ledger_fail);
    if( g_ledger_blocked > 0 && used > 0 && used < (int)sizeof(g_summary_line) )
        snprintf(g_summary_line + used, sizeof(g_summary_line) - (size_t)used, " blocked=%d", g_ledger_blocked);
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
     * OSRS-Content/.../selftest/quests/<quest_dir>/play/ (formerly
     * .../selftest/quest_tests/<test_id>/) was written without it, and a suite whose
     * rows are all PASS or FAIL should keep producing byte-identical summaries
     * to the ones a human has already read. So the token appears exactly when
     * it carries information. */
    fprintf(f, "SUMMARY\t%d\t%s\t%ld\texit=%s\tpass=%d fail=%d",
        g_ledger_index, g_ledger_fail == 0 ? "PASS" : "FAIL", g_ledger_total_ticks, exit_text,
        g_ledger_pass, g_ledger_fail);
    if( g_ledger_blocked > 0 )
        fprintf(f, " blocked=%d", g_ledger_blocked);
    fprintf(f, "\n");
    fclose(f);
}

static void
drive_ledger_write_summary(int code)
{
    char exit_text[16];

    snprintf(exit_text, sizeof(exit_text), "%d", code);
    drive_ledger_write_summary_exit(exit_text);
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
    /* As early as the first pump: before login, so the load is paid before
     * the member joins the leader's lock step rather than inside it. */
    if( !g_embed && drive_party_member() )
        drive_member_load_tables();
    if( !g_app || !g_app->world )
        return 0;
    if( !g_embed && drive_party_member() )
    {
        /* A party member hosts no world, so "online" is this client's own
         * reading: its local player is in the entity pool (the login burst
         * landed) and its worldview finished loading. */
        int world_idx;
        if( g_app->esync.local_pid < 0 ||
            !RS_EntitySync_FindPlayer(&g_app->esync, g_app->esync.local_pid, &world_idx, NULL) )
            return 0;
        return g_app->world->load_complete;
    }
    if( !g_embed || !ToriRSServer_EmbedOnline(g_embed, 0) )
        return 0;
    return g_app->world->load_complete;
}

static void
drive_scheduler_start(void)
{
    char const* path = drive_script_path();
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

/* ---------------------------------------------------------------- heartbeat
 *
 * seam32 stall_detector_heartbeat. The one defence run.py had against a
 * client that stops ticking was its wall-clock ceiling (400 s, scaled by
 * max_frames up to 1600 s for legends), so a frozen frame loop held an agent's
 * shell for that long. Every DRIVE_HEARTBEAT_TICKS server ticks this rewrites
 * <session>/heartbeat (TORIRS_CONTENT_TEST) with the tick it was written at;
 * run.py (launch_client) kills the process group once the file's mtime is
 * older than TORIRS_QUEST_STALL_SECONDS. It beats from the per-frame pump, so
 * it stops exactly when the frame loop or the server tick stops -- a hung
 * predicate, a C spin, a clock that no longer advances -- and never during a
 * long await, whose ticks keep coming.
 *
 * Written with fopen/fprintf rather than utime(): this file builds on every
 * desktop platform, and the content (the tick) lets run.py name where the
 * run stopped. The "quest-driver: heartbeat" stderr line is printed once, at
 * the first beat; run.py also looks for that string in the binary to know a
 * build writes heartbeats at all (an older binary never does, and must not
 * be killed at the end of the boot grace for it). */
#define DRIVE_HEARTBEAT_TICKS 25
#define DRIVE_HEARTBEAT_FILE "heartbeat"

static int g_heartbeat_tick = -1;

static void
drive_heartbeat(void)
{
    char const* dir = DriveCore_SessionDir();
    char path[1024];
    FILE* f;
    int tick;

    if( !dir || !drive_script_path() || g_finished )
        return;
    if( !g_app || !g_app->world )
        return;
    tick = (int)(g_app->world->cycle / APP_SERVER_TICK_LOGIC_CYCLES);
    /* A relog re-boots the world and its cycle restarts: a tick below the
     * last beat is a new clock, not a stall, and beats at once. */
    if( g_heartbeat_tick >= 0 && tick >= g_heartbeat_tick &&
        tick < g_heartbeat_tick + DRIVE_HEARTBEAT_TICKS )
        return;
    snprintf(path, sizeof(path), "%s/%s", dir, DRIVE_HEARTBEAT_FILE);
    f = fopen(path, "wb");
    /* Not an assert: the session directory is run.py's, and a heartbeat that
     * cannot be written is reported by run.py as a stall with the file's
     * absence named -- the frame loop itself is fine. */
    if( !f )
    {
        fprintf(stderr, "quest-driver: heartbeat NOT written to %s\n", path);
        g_heartbeat_tick = tick;
        return;
    }
    fprintf(f, "tick=%d\n", tick);
    fclose(f);
    if( g_heartbeat_tick < 0 )
        fprintf(stderr, "quest-driver: heartbeat %s every %d server ticks (first at tick %d)\n",
            path, DRIVE_HEARTBEAT_TICKS, tick);
    g_heartbeat_tick = tick;
}

/* ------------------------------------------------------------- on demand
 *
 * Raid seam23 (script_start_on_demand; torirs_plugin_drive.h,
 * PluginDrive_OnDemand). A watched client starts a prepared script when its
 * person asks (the Scripts tab: api.drive.start), stops it when they ask
 * (api.drive.stop), and reads where it is (api.drive.status). The coroutine
 * is the same one a test run gets -- created by drive_scheduler_start on the
 * quest-driver plugin's own Lua state, pumped by that plugin's on_frame_start
 * -- so what a person watches is what run.py runs, minus the virtual clock.
 *
 * Three hand-offs keep the pieces where they are safe:
 *   - start only validates and records; the next pump the world is ready for
 *     creates the coroutine (drive_demand_begin), inside the quest-driver's
 *     own callback, exactly where a test run's starts;
 *   - stop only records; PluginDrive_FrameBoundary (main.c, outside every
 *     plugin callback) writes the unfinished row and SUMMARY and releases;
 *   - a finish (t.finish, a returned run, a script error) marks the release
 *     the same boundary performs: the coroutine is destroyed, the tick log
 *     dropped, and the quest-driver plugin RELOADED -- a fresh Lua state, so
 *     no part's per-script state (core.lua's `finished`, its shot counter
 *     and row tallies, raid.lua's party counters, ...) reaches the next run.
 *     A script error disables the plugin through the host's fault path
 *     (PluginLua_ThreadResume); the release switches it back on first, or
 *     a watched client would be dead after one bad script.
 */

#ifdef _WIN32
#define DRIVE_MKDIR(path) _mkdir(path)
#define DRIVE_GETCWD(buffer, capacity) _getcwd((buffer), (int)(capacity))
#else
#include <unistd.h>
#define DRIVE_MKDIR(path) mkdir((path), 0755)
#define DRIVE_GETCWD(buffer, capacity) getcwd((buffer), (capacity))
#endif

/* `mkdir -p`. 0 when the directory exists afterwards. */
static int
drive_make_directories(char const* path)
{
    char partial[1024];
    size_t length;
    size_t i;
    struct stat info;

    assert(path);
    length = strlen(path);
    if( length == 0 || length >= sizeof(partial) )
        return -1;
    memcpy(partial, path, length + 1);
    for( i = 1; i <= length; i++ )
    {
        if( partial[i] == '/' || partial[i] == '\0' )
        {
            char const saved = partial[i];
            partial[i] = '\0';
            (void)DRIVE_MKDIR(partial);
            partial[i] = saved;
        }
    }
    return stat(path, &info) == 0 && (info.st_mode & S_IFMT) == S_IFDIR ? 0 : -1;
}

static int
drive_driver_plugin_index(void)
{
    assert(g_app);
    assert(g_app->plugins);
    return PluginHost_IndexOf(g_app->plugins, DRIVE_PLUGIN_NAME);
}

/* Every piece of per-script C state the run before this one left (start
 * calls it, so a status read between start and the first pump already reads
 * the new run). The Lua half was reset by the reload that released the last
 * run. */
static void
drive_demand_reset_run(void)
{
    g_finished = 0;
    g_finish_code = 0;
    g_ledger_index = 0;
    g_ledger_pass = 0;
    g_ledger_fail = 0;
    g_ledger_blocked = 0;
    g_ledger_total_ticks = 0;
    g_last_row_step[0] = '\0';
    g_last_row_verdict[0] = '\0';
    g_summary_line[0] = '\0';
    g_heartbeat_tick = -1;
    g_quest_name[0] = '\0';
    g_await.active = 0;
    g_await.level_ref = LUA_NOREF;
    g_await.match_ref = LUA_NOREF;
    g_demand_leg = 0;
}

/* ------------------------------------------------------------ the Scripts tab
 *
 * Raid seam24 (scripts_tab_every_script; torirs_plugin_drive.h,
 * PluginDrive_OnDemand). The tab asks for the scripts manifest and for a
 * chosen test's source and fixture through the IO layer -- SCRIPT items, the
 * kind plugins/plugins.ini is (task_plugin_io.c CreateTask_PluginScriptRead)
 * -- and a Play runs the test file itself on a FRESH account:
 *
 *   api.drive.play(test)  validates, picks the account (the id's letters and
 *                         a number, the first whose save file does not
 *                         exist yet), makes its session dir
 *                         build/quest_gate/watch/<account>/ and queues the two
 *                         reads. Every Play reads the source again: nothing
 *                         is cached, which is the hot reload.
 *   the deliveries        land in g_demand_source/fixture_bytes.
 *   the next pump         the world is ready and both have landed:
 *                         drive_demand_begin_play writes the fixture as the
 *                         account's save (run.py's write_session_fixture, in
 *                         C because the sandbox has no io and C holds the
 *                         bytes) and starts the coroutine on the RAW test
 *                         (PluginLua_TestThreadCreate), whose bootstrap calls
 *                         QD.core_run_test: log out, log in as the account,
 *                         then what run.py's wrapper does (the login-grant
 *                         wait, the setup list, a legs table in one sitting).
 */

#define DRIVE_WATCH_ROOT "build/quest_gate/watch"
#define DRIVE_WATCH_PASSWORD "test"

static void
drive_tests_deliver(void* user, int serial, char const* path, void* data, int size)
{
    (void)user;
    assert(path);
    if( serial != g_tests_serial )
    {
        free(data); /* a Refresh superseded this read */
        return;
    }
    free(g_tests_bytes);
    g_tests_bytes = (char*)data;
    g_tests_size = data ? size : 0;
    g_tests_state = data ? DRIVE_FETCH_READY : DRIVE_FETCH_MISSING;
    fprintf(stderr, "quest-driver: tests manifest %s: %s (%d bytes, read %d)\n", path,
        data ? "landed" : "absent", g_tests_size, serial);
}

/* `user` is 1 for the source, 2 for the fixture. */
static void
drive_play_deliver(void* user, int serial, char const* path, void* data, int size)
{
    int const which = (int)(intptr_t)user;

    assert(path);
    assert(which == 1 || which == 2);
    if( serial != g_demand_fetch_serial )
    {
        free(data); /* the Play that asked was stopped before this landed */
        return;
    }
    if( which == 1 )
    {
        free(g_demand_source_bytes);
        g_demand_source_bytes = (char*)data;
        g_demand_source_size = data ? size : 0;
        g_demand_source_state = data ? DRIVE_FETCH_READY : DRIVE_FETCH_MISSING;
    }
    else
    {
        free(g_demand_fixture_bytes);
        g_demand_fixture_bytes = (char*)data;
        g_demand_fixture_size = data ? size : 0;
        g_demand_fixture_state = data ? DRIVE_FETCH_READY : DRIVE_FETCH_MISSING;
    }
    fprintf(stderr, "quest-driver: on demand: play %d %s %s: %s (%d bytes)\n", serial,
        which == 1 ? "source" : "fixture", path, data ? "landed" : "absent", data ? size : 0);
}

/* Forget a Play's reads: its bytes, and any reply still in flight. */
static void
drive_play_discard(void)
{
    g_demand_fetch_serial++;
    free(g_demand_source_bytes);
    g_demand_source_bytes = NULL;
    g_demand_source_size = 0;
    free(g_demand_fixture_bytes);
    g_demand_fixture_bytes = NULL;
    g_demand_fixture_size = 0;
    g_demand_source_state = DRIVE_FETCH_NONE;
    g_demand_fixture_state = DRIVE_FETCH_NONE;
}

/* A Play that cannot begin: back to idle with the reason the status shows. */
static void
drive_play_refuse(char const* reason)
{
    assert(reason);
    snprintf(g_demand_refusal, sizeof(g_demand_refusal), "%s", reason);
    fprintf(stderr, "quest-driver: on demand: play %s refused: %s\n", g_demand_id, reason);
    drive_play_discard();
    g_demand_start_pending = 0;
    g_demand_state = DRIVE_DEMAND_IDLE;
}

/* The fresh account for a Play of `id`: up to eight of its letters and digits
 * and the first number whose save file and session dir do not exist yet, so a
 * name is never reused -- not across Plays and not across client restarts --
 * and is at most 12 characters (the login form's limit). */
static int
drive_play_pick_account(char const* id, char* out, size_t capacity)
{
    char prefix[9];
    int length = 0;
    int number;
    struct stat info;
    char session[1100];
    char const* save;

    assert(id);
    assert(out);
    assert(capacity >= 13);
    for( ; *id && length < 8; id++ )
    {
        unsigned char const ch = (unsigned char)*id;
        if( (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') )
            prefix[length++] = (char)ch;
        else if( ch >= 'A' && ch <= 'Z' )
            prefix[length++] = (char)(ch - 'A' + 'a');
    }
    if( length == 0 )
    {
        memcpy(prefix, "watch", 5);
        length = 5;
    }
    prefix[length] = '\0';
    for( number = 1; number <= 9999; number++ )
    {
        snprintf(out, capacity, "%s%d", prefix, number);
        save = ToriRSServer_SavePath(out);
        if( !save[0] )
            return -1;
        snprintf(session, sizeof(session), "%s/%s", DRIVE_WATCH_ROOT, out);
        if( stat(save, &info) != 0 && stat(session, &info) != 0 )
            return 0;
    }
    return -1;
}

/* run.py's write_session_fixture: the fixture with its first `name = ...` line
 * naming the account, written where the embedded server reads that account's
 * save. 0 on success, else `reason` says why. */
static int
drive_play_write_fixture(char* reason, size_t capacity)
{
    char const* save;
    char directory[1024];
    char* slash;
    FILE* f;
    char const* text = g_demand_fixture_bytes;
    int const size = g_demand_fixture_size;
    int line = 0;
    int name_start = -1;
    int name_end = -1;

    assert(reason);
    assert(text);
    while( line < size )
    {
        int end = line;
        int at;
        while( end < size && text[end] != '\n' )
            end++;
        at = line;
        if( end - at >= 4 && strncmp(text + at, "name", 4) == 0 )
        {
            at += 4;
            while( at < end && (text[at] == ' ' || text[at] == '\t') )
                at++;
            if( at < end && text[at] == '=' )
            {
                name_start = line;
                name_end = end;
                break;
            }
        }
        line = end + 1;
    }
    if( name_start < 0 )
    {
        snprintf(reason, capacity, "play: the fixture %s has no `name = ...` line to rewrite",
            g_demand_fixture_path);
        return -1;
    }
    save = ToriRSServer_SavePath(g_demand_account);
    snprintf(directory, sizeof(directory), "%s", save);
    slash = strrchr(directory, '/');
    if( slash )
    {
        *slash = '\0';
        if( drive_make_directories(directory) != 0 )
        {
            snprintf(reason, capacity, "play: cannot create the saves directory %s", directory);
            return -1;
        }
    }
    f = fopen(save, "wb");
    if( !f )
    {
        snprintf(reason, capacity, "play: cannot write the account's save %s", save);
        return -1;
    }
    fwrite(text, 1, (size_t)name_start, f);
    fprintf(f, "name = %s", g_demand_account);
    fwrite(text + name_end, 1, (size_t)(size - name_end), f);
    fclose(f);
    fprintf(stderr, "quest-driver: on demand: play %s: account %s from %s -> %s\n", g_demand_id,
        g_demand_account, g_demand_fixture_path, save);
    return 0;
}

/* The pump's half of a Play, once the world is ready and both reads landed. */
static void
drive_demand_begin_play(void)
{
    char reason[600];
    char chunk_name[TORIRS_IOITEM_MAX_PATH + 2];
    int status;
    int nresults;
    char error[256];

    assert(g_demand_play);
    if( g_demand_source_state == DRIVE_FETCH_MISSING )
    {
        snprintf(reason, sizeof(reason), "play: no test source at script item %s", g_demand_script);
        drive_play_refuse(reason);
        return;
    }
    if( g_demand_fixture_state == DRIVE_FETCH_MISSING )
    {
        snprintf(reason, sizeof(reason), "play: no fixture at script item %s", g_demand_fixture_path);
        drive_play_refuse(reason);
        return;
    }
    if( drive_play_write_fixture(reason, sizeof(reason)) != 0 )
    {
        drive_play_refuse(reason);
        return;
    }

    g_demand_start_pending = 0;
    g_event_cursor = g_app->drive_events.newest_serial;
    g_started = 1;
    drive_quest_name_from_path(g_demand_script);
    fprintf(stderr, "quest-driver: on demand: start %d: play %s (%s) as %s (session %s)\n",
        g_demand_runs, g_demand_id, g_demand_script, g_demand_account, g_demand_session);

    snprintf(chunk_name, sizeof(chunk_name), "@%s", g_demand_script);
    g_thread = PluginLua_TestThreadCreate(DRIVE_PLUGIN_NAME, chunk_name, g_demand_source_bytes,
        g_demand_source_size, &g_thread_ref);
    assert(g_thread);
    /* The coroutine holds the compiled chunk; the bytes are done with. */
    drive_play_discard();

    lua_getglobal(g_thread, "QD_ROOT");
    lua_createtable(g_thread, 0, 8);
    lua_pushstring(g_thread, g_demand_id);
    lua_setfield(g_thread, -2, "id");
    lua_pushstring(g_thread, g_demand_suite);
    lua_setfield(g_thread, -2, "suite");
    lua_pushstring(g_thread, g_demand_title);
    lua_setfield(g_thread, -2, "title");
    lua_pushstring(g_thread, g_demand_account);
    lua_setfield(g_thread, -2, "account");
    lua_pushstring(g_thread, DRIVE_WATCH_PASSWORD);
    lua_setfield(g_thread, -2, "password");
    lua_pushstring(g_thread, g_demand_script);
    lua_setfield(g_thread, -2, "source");
    lua_pushinteger(g_thread, g_demand_legs);
    lua_setfield(g_thread, -2, "legs");
    lua_createtable(g_thread, 0, 3);
    lua_pushinteger(g_thread, g_demand_camera_yaw);
    lua_setfield(g_thread, -2, "yaw");
    lua_pushinteger(g_thread, g_demand_camera_pitch);
    lua_setfield(g_thread, -2, "pitch");
    lua_pushinteger(g_thread, g_demand_camera_zoom);
    lua_setfield(g_thread, -2, "zoom");
    lua_setfield(g_thread, -2, "camera");
    status = PluginLua_ThreadResume(g_thread, 3, &nresults, error, sizeof(error));
    drive_scheduler_handle_status(status, error);
}

static void
drive_demand_begin(void)
{
    assert(PluginDrive_OnDemand());
    assert(g_demand_state == DRIVE_DEMAND_RUNNING);
    assert(!g_thread);
    /* The event ring's cursor starts at its newest entry: what happened before
     * the person pressed Play is not this script's to await. (A test run's
     * first pump reads from 0 -- everything since boot -- and is unchanged.) */
    g_event_cursor = g_app->drive_events.newest_serial;
    /* The pump's half of a start: the coroutine, created as a test run's is. */
    g_started = 1;
    fprintf(stderr, "quest-driver: on demand: start %d: %s (session %s)\n",
        g_demand_runs, g_demand_script, g_demand_session);
    drive_scheduler_start();
}

static void
drive_demand_release(void)
{
    int index;

    if( g_thread )
    {
        drive_await_free_refs();
        PluginLua_ThreadDestroy(g_thread_ref);
        g_thread = NULL;
        g_thread_ref = LUA_NOREF;
    }
    g_await.active = 0;
    /* The tick log belongs to the run that enabled it (t.ticklog.start): the
     * next run's own start begins a fresh one in its own session dir. */
    ToriRSServer_TicklogDisable();
    /* A finished or stopped script gives the watcher back the one view (the
     * run wrapper detaches at its own finish; a stop never reaches it). */
    App_ViewDetachPlayerClient(g_app);
    g_started = 0;
    g_demand_state = DRIVE_DEMAND_FINISHED;
    fprintf(stderr, "quest-driver: on demand: %s finished: %s\n", g_demand_script,
        g_summary_line[0] ? g_summary_line : "(no summary)");

    index = drive_driver_plugin_index();
    assert(index >= 0);
    if( !PluginHost_IsEnabled(g_app->plugins, index) )
    {
        fprintf(stderr, "quest-driver: on demand: the script's error disabled the driver; switching it back on\n");
        PluginHost_SetEnabled(g_app->plugins, index, true);
    }
    PluginHost_Reload(g_app->plugins, index);
}

void
PluginDrive_OnDemandHandOver(struct ToriRSServerEmbed* embed, struct ToriRS_CmdBus* bus)
{
    assert(PluginDrive_OnDemand());
    PluginDriveCore_SetEmbed(embed);
    PluginDriveCore_SetCmdBus(bus);
}

void
PluginDrive_FrameBoundary(void)
{
    assert(PluginDrive_OnDemand());
    if( !g_app )
        return; /* PluginDrive_Init never ran: this client has no plugins */
    if( g_demand_stop_pending && g_thread && g_await.active &&
        strncmp(g_await.note, "t.shot ", 7) == 0 )
    {
        /* Not while a screenshot is in flight (ui.lua's t.shot await): its
         * request is latched in torirs_plugin_drive_ui.c until a poll
         * collects it, and a coroutine destroyed mid-shot leaves that latch
         * for the NEXT run's first t.shot to collect -- measured 2026-10-05,
         * seam23 probe wall1: tob_bloat's 001-bloat.enter.png never written,
         * its row naming maiden's 006-options-p1 instead. The pump settles a
         * shot in about three frames; the stop lands at the first boundary
         * after it (the script's next yield that is not a shot). */
        return;
    }
    if( g_demand_stop_pending )
    {
        g_demand_stop_pending = 0;
        if( g_demand_start_pending )
        {
            /* Stopped before its first pump: there is no coroutine and no
             * ledger, so there is nothing to finish. */
            g_demand_start_pending = 0;
            g_demand_state = DRIVE_DEMAND_IDLE;
            /* A Play's reads may still be in flight: their replies are
             * dropped by serial, and what landed is freed. */
            drive_play_discard();
            fprintf(stderr, "quest-driver: on demand: %s stopped before it began\n", g_demand_script);
            return;
        }
        if( !g_finished )
        {
            /* The same two lines run.py's finish_unfinished_ledger appends to a
             * run that ended without its own SUMMARY. */
            char detail[400];
            snprintf(detail, sizeof(detail),
                "run ended without finishing: stopped on demand (api.drive.stop); last row written: %s",
                g_last_row_step[0] ? g_last_row_step : "none");
            drive_ledger_write("run.unfinished", "FAIL", 0, "", detail);
            drive_ledger_write_summary_exit("none");
            g_finished = 1;
            g_finish_code = 0;
        }
        g_demand_release_pending = 1;
    }
    if( g_demand_release_pending )
    {
        g_demand_release_pending = 0;
        drive_demand_release();
    }
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
    /* The runner camera split: every driver verb acts through the runner's
     * view because the pump runs between the stages that move frame_view
     * (the presented draw and the emit walk put it back to views[0]). */
    assert(!g_app || g_app->frame_view == App_RunnerView(g_app));
    if( PluginDrive_OnDemand() )
    {
        /* On demand nothing starts at world-ready: a start api.drive.start
         * accepted begins here, on the first pump the world is ready for
         * (a relog in between holds it), with every piece of per-script
         * state of the run before it reset. */
        if( g_demand_start_pending && g_demand_play )
        {
            /* A Play begins once both of its reads have answered (landed or
             * absent: an absent one is refused there, with its name). */
            if( g_demand_source_state != DRIVE_FETCH_PENDING &&
                g_demand_fixture_state != DRIVE_FETCH_PENDING && drive_world_ready() )
                drive_demand_begin_play();
        }
        else if( g_demand_start_pending && drive_world_ready() )
        {
            g_demand_start_pending = 0;
            drive_demand_begin();
        }
    }
    else if( !g_started && drive_world_ready() )
    {
        g_started = 1;
        drive_scheduler_start();
    }
    if( g_started )
        drive_heartbeat();
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
    char const* script = drive_script_path();

    int const lockstep = ToriRSServer_EmbedLockstepTick();

    lua_createtable(L, 0, 3);
    lua_pushstring(L, dir ? dir : "");
    lua_setfield(L, -2, "dir");
    lua_pushstring(L, script ? script : "");
    lua_setfield(L, -2, "script");
    /* `lockstep_tick` (raid seam22, party_death_and_member_readers): the world
     * tick of this process's last party boundary, ToriRSServer_EmbedLockstepTick(),
     * and absent (nil) outside a party. On a party MEMBER it is the tick its
     * last TICK frame carried (net_transport_embed.c party_member_boundary),
     * which the leader stamped from srv->tick right after that boundary's world
     * tick ran (torirs_server_embed.c party_flush) -- so between two boundaries
     * a member reads the number the leader's api_drive.server_tick reads, and
     * can write tick-stamped rows and wait "until tick T". core.lua's QD.tick
     * falls back to it only where server_tick answers unsupported (a member
     * holds no world); the leader and a solo run keep reading srv->tick. A
     * field of the session table rather than a verb of its own: it is a fact
     * of this process's place in the run, like `dir`. */
    if( lockstep != TORIRSSERVER_EMBED_LOCKSTEP_NONE )
    {
        lua_pushinteger(L, (lua_Integer)lockstep);
        lua_setfield(L, -2, "lockstep_tick");
    }
    /* `on_demand` (raid seam23): present, and true, only in a client started
     * with TORIRS_DRIVE_ON_DEMAND=1 -- a test run's table is unchanged. */
    if( PluginDrive_OnDemand() )
    {
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "on_demand");
    }
    return 1;
}

/* ------------------------------------------------- on demand: the three verbs
 *
 * api.drive.start(path, session_dir) -> "ok", path | "refused", reason
 * api.drive.stop()                   -> "ok", path | "refused", reason
 * api.drive.status()                 -> "ok", {state, script, session, step,
 *                                        verdict, rows, pass, fail, blocked,
 *                                        summary, exit, on_demand, runs,
 *                                        starting, stopping}
 *
 * On a test run (no TORIRS_DRIVE_ON_DEMAND) start and stop answer `refused`
 * -- that run's script is TORIRS_QUEST_SCRIPT and ends at t.finish -- and
 * status reads it all the same (state "running" while it runs). Never
 * `unsupported`: the verbs exist in every build that has the driver. */

/* Why a start would be refused, written into `reason`; NULL when it would
 * not. Every refusal is a runtime answer a watcher reads, not a contract
 * violation: the path and the directory are a person's choice. */
static char const*
drive_start_refusal(char const* path, char const* session, char* reason, size_t capacity)
{
    FILE* f;
    int index;
    char shots[1100];

    assert(path);
    assert(session);
    assert(reason);
    if( !PluginDrive_OnDemand() )
        snprintf(reason, capacity, "drive.start: not an on-demand client (TORIRS_DRIVE_ON_DEMAND=1 is "
            "unset); this run's script is TORIRS_QUEST_SCRIPT");
    else if( g_demand_release_pending || g_demand_stop_pending )
        snprintf(reason, capacity, "drive.start: the last script is still ending (ask again next "
            "frame): %s", g_demand_script);
    else if( g_demand_state == DRIVE_DEMAND_RUNNING )
        snprintf(reason, capacity, "drive.start: a script is running: %s", g_demand_script);
    else if( (index = drive_driver_plugin_index()) < 0 || !PluginHost_IsRunning(g_app->plugins, index) )
        snprintf(reason, capacity, "drive.start: the quest-driver plugin is not running in this client");
    else if( !drive_world_ready() )
        snprintf(reason, capacity, "drive.start: the world is not ready (log in first)");
    else if( strlen(path) >= sizeof(g_demand_script) )
        snprintf(reason, capacity, "drive.start: the path is too long: %s", path);
    else if( session[0] == '\0' || strlen(session) + 16 >= sizeof(g_demand_session) )
        snprintf(reason, capacity, "drive.start: the session directory is empty or too long: %s", session);
    else if( (f = fopen(path, "rb")) == NULL )
        snprintf(reason, capacity, "drive.start: no script at %s", path);
    else
    {
        fclose(f);
        snprintf(shots, sizeof(shots), "%s/shots", session);
        if( drive_make_directories(shots) == 0 )
            return NULL;
        snprintf(reason, capacity, "drive.start: cannot create the session directory %s", session);
    }
    return reason;
}

static int
lua_drive_start(struct lua_State* L)
{
    char const* path = PluginDrive_ArgString(L, 1);
    char const* session = PluginDrive_ArgString(L, 2);
    char reason[1200];
    char stale[1100];
    static char const* const STALE_FILES[] = { "ledger.tsv", "ticklog.tsv", "heartbeat" };
    size_t i;

    if( drive_start_refusal(path, session, reason, sizeof(reason)) )
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    /* A session directory a person reuses (Play twice on one script) must not
     * carry the last run's ledger, tick log or heartbeat into this one: the
     * ledger restarts at its first row anyway, but a tick log appends. Shots
     * are overwritten by name; leftovers past the new run's count stay. */
    for( i = 0; i < sizeof(STALE_FILES) / sizeof(STALE_FILES[0]); i++ )
    {
        snprintf(stale, sizeof(stale), "%s/%s", session, STALE_FILES[i]);
        (void)remove(stale);
    }

    snprintf(g_demand_script, sizeof(g_demand_script), "%s", path);
    snprintf(g_demand_session, sizeof(g_demand_session), "%s", session);
    g_demand_play = 0;
    g_demand_id[0] = '\0';
    g_demand_account[0] = '\0';
    g_demand_refusal[0] = '\0';
    g_demand_legs = 0;
    g_demand_state = DRIVE_DEMAND_RUNNING;
    g_demand_start_pending = 1;
    g_demand_runs++;
    drive_demand_reset_run();
    return PluginDrive_PushResult(L, DRIVE_OK, g_demand_script);
}

static int
lua_drive_stop(struct lua_State* L)
{
    char reason[1200];

    reason[0] = '\0';
    if( !PluginDrive_OnDemand() )
        snprintf(reason, sizeof(reason),
            "drive.stop: not an on-demand client; a test run's script ends at t.finish");
    else if( g_demand_state != DRIVE_DEMAND_RUNNING )
        snprintf(reason, sizeof(reason), "drive.stop: no script is running");
    else if( g_demand_release_pending )
        snprintf(reason, sizeof(reason), "drive.stop: already finished: %s", g_demand_script);
    if( reason[0] )
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    g_demand_stop_pending = 1;
    return PluginDrive_PushResult(L, DRIVE_OK, g_demand_script);
}

static char const*
drive_status_state(void)
{
    if( PluginDrive_OnDemand() )
    {
        if( g_demand_state == DRIVE_DEMAND_RUNNING )
            return g_demand_release_pending ? "finished" : "running";
        return g_demand_state == DRIVE_DEMAND_FINISHED ? "finished" : "idle";
    }
    if( !PluginDrive_QuestScriptPath() )
        return "idle";
    return g_finished ? "finished" : "running";
}

static int
lua_drive_status(struct lua_State* L)
{
    char const* state = drive_status_state();
    char const* script = PluginDrive_OnDemand() ? g_demand_script : PluginDrive_QuestScriptPath();
    char const* session = DriveCore_SessionDir();
    int const finished = strcmp(state, "finished") == 0;

    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, 0, 16);
    lua_pushstring(L, state);
    lua_setfield(L, -2, "state");
    lua_pushstring(L, script ? script : "");
    lua_setfield(L, -2, "script");
    lua_pushstring(L, session ? session : "");
    lua_setfield(L, -2, "session");
    lua_pushstring(L, g_last_row_step);
    lua_setfield(L, -2, "step");
    lua_pushstring(L, g_last_row_verdict);
    lua_setfield(L, -2, "verdict");
    lua_pushinteger(L, g_ledger_index);
    lua_setfield(L, -2, "rows");
    lua_pushinteger(L, g_ledger_pass);
    lua_setfield(L, -2, "pass");
    lua_pushinteger(L, g_ledger_fail);
    lua_setfield(L, -2, "fail");
    lua_pushinteger(L, g_ledger_blocked);
    lua_setfield(L, -2, "blocked");
    lua_pushstring(L, finished ? g_summary_line : "");
    lua_setfield(L, -2, "summary");
    lua_pushinteger(L, finished ? g_finish_code : 0);
    lua_setfield(L, -2, "exit");
    lua_pushboolean(L, PluginDrive_OnDemand());
    lua_setfield(L, -2, "on_demand");
    lua_pushinteger(L, g_demand_runs);
    lua_setfield(L, -2, "runs");
    lua_pushboolean(L, g_demand_start_pending);
    lua_setfield(L, -2, "starting");
    lua_pushboolean(L, g_demand_stop_pending);
    lua_setfield(L, -2, "stopping");
    /* Raid seam24 (the Scripts tab): what a Play started -- the test, its
     * suite, the fresh account, "leg k of n" -- and why the last Play never
     * began. Empty/0 for a start and on a test run. */
    lua_pushboolean(L, g_demand_play);
    lua_setfield(L, -2, "play");
    lua_pushstring(L, g_demand_id);
    lua_setfield(L, -2, "id");
    lua_pushstring(L, g_demand_suite);
    lua_setfield(L, -2, "suite");
    lua_pushstring(L, g_demand_account);
    lua_setfield(L, -2, "account");
    lua_pushinteger(L, g_demand_leg);
    lua_setfield(L, -2, "leg");
    lua_pushinteger(L, g_demand_legs);
    lua_setfield(L, -2, "legs");
    lua_pushstring(L, g_demand_refusal);
    lua_setfield(L, -2, "refusal");
    return 2;
}

/*
 * api.drive.tests([refresh]) -> "ok", <the manifest's text>
 *                             | "timeout", "pending" (asked; ask again later)
 *                             | "refused", reason
 *
 * The scripts manifest (TestsManifest_Path: tests/tests.ini under the script
 * dir) read through the IO layer as one SCRIPT item, the way the plugin host
 * reads plugins/plugins.ini. The first call asks; `refresh` asks again (the
 * tab's Refresh), so a manifest rewritten after the client started is seen.
 * Only in an on-demand client: a test run never reads it.
 */
static int
lua_drive_tests(struct lua_State* L)
{
    int const refresh = lua_toboolean(L, 1);
    char reason[400];

    if( !PluginDrive_OnDemand() )
        return PluginDrive_PushResult(L, DRIVE_REFUSED,
            "drive.tests: not an on-demand client (TORIRS_DRIVE_ON_DEMAND=1 is unset)");
    if( g_tests_state != DRIVE_FETCH_PENDING && (refresh || g_tests_state == DRIVE_FETCH_NONE) )
    {
        g_tests_serial++;
        g_tests_state = DRIVE_FETCH_PENDING;
        fprintf(stderr, "quest-driver: tests manifest: asking for script item %s (read %d)\n",
            TestsManifest_Path(), g_tests_serial);
        ToriRS_TaskQueue_Add(g_app->runner.queue,
            CreateTask_PluginScriptRead(TestsManifest_Path(), g_tests_serial, drive_tests_deliver, NULL));
    }
    if( g_tests_state == DRIVE_FETCH_PENDING )
        return PluginDrive_PushResult(L, DRIVE_TIMEOUT, "pending");
    if( g_tests_state == DRIVE_FETCH_MISSING )
    {
        snprintf(reason, sizeof(reason), "drive.tests: no scripts manifest at script item %s "
            "(./launch run osrs239-scripts writes it; or python3 tools/raid_gate/prepare_scripts.py)",
            TestsManifest_Path());
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    }
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_pushlstring(L, g_tests_bytes, (size_t)g_tests_size);
    return 2;
}

/*
 * api.drive.forget_varps() -> "ok", detail | "refused", reason
 *
 * QD.core_run_test's step between the log-out and the fresh account's log-in
 * (seam24). The client's varps outlive a logout -- nothing in this engine
 * clears them, and the embedded server's login sends only the new account's
 * non-zero varps -- so a fresh account read the LAST account's values for
 * every varp it holds at 0: measured, seaslug1 played after cooksass2 in one
 * session read `qp (varp) 1 -> 1` and failed quest.points, PASS in the suite.
 * A test run never meets this (one account per process). This zeroes the
 * client's copy (VarPManager_ResetAll, what a VARP_RESET packet does), which
 * is the state a fresh process logs in with. Only while a Play runs, and only
 * on the title screen (logged out), so no live account's varps are touched.
 */
static int
lua_drive_forget_varps(struct lua_State* L)
{
    if( !PluginDrive_OnDemand() || !g_demand_play || g_demand_state != DRIVE_DEMAND_RUNNING )
        return PluginDrive_PushResult(L, DRIVE_REFUSED,
            "drive.forget_varps: only a running Play (api.drive.play) forgets the last account's varps");
    if( g_app->screen != APP_SCREEN_TITLE )
        return PluginDrive_PushResult(L, DRIVE_REFUSED,
            "drive.forget_varps: not on the title screen (log out first)");
    VarPManager_ResetAll(&g_app->varps);
    fprintf(stderr, "quest-driver: on demand: play %s: the last account's client varps forgotten\n",
        g_demand_id);
    return PluginDrive_PushResult(L, DRIVE_OK, "client varps zeroed");
}

/* A string field of the Play table, copied; refused (returns the reason) when
 * absent or too long. */
static char const*
drive_play_field(struct lua_State* L, char const* name, int required, char* out, size_t capacity,
    char* reason, size_t reason_capacity)
{
    char const* value;
    size_t length = 0;

    lua_getfield(L, 1, name);
    value = lua_type(L, -1) == LUA_TSTRING ? lua_tolstring(L, -1, &length) : NULL;
    out[0] = '\0';
    if( !value || length == 0 )
    {
        lua_pop(L, 1);
        if( !required )
            return NULL;
        snprintf(reason, reason_capacity, "drive.play: the test table has no `%s`", name);
        return reason;
    }
    if( length >= capacity )
    {
        lua_pop(L, 1);
        snprintf(reason, reason_capacity, "drive.play: `%s` is too long (%d bytes)", name, (int)length);
        return reason;
    }
    memcpy(out, value, length + 1);
    lua_pop(L, 1);
    return NULL;
}

/*
 * api.drive.play({id =, source =, fixture =, suite =, title =, legs =})
 *   -> "ok", <the account it will play on> | "refused", reason
 *
 * The Scripts tab's Play (seam24): the test at script item `source` (a test
 * file as it sits in the tree, read again now), on a fresh account made from
 * the script item `fixture`, with its ledger and shots in
 * build/quest_gate/watch/<account>/. Refused, with the reason, exactly where
 * api.drive.start would be (not on demand, a script running or still ending,
 * no driver, the world not ready). The section banner above says what happens
 * next; api.drive.status follows it.
 */
static int
lua_drive_play(struct lua_State* L)
{
    char reason[600];
    char source[TORIRS_IOITEM_MAX_PATH];
    char session[1100];
    char shots[1200];
    int index;

    luaL_checktype(L, 1, LUA_TTABLE);
    reason[0] = '\0';
    if( !PluginDrive_OnDemand() )
        snprintf(reason, sizeof(reason), "drive.play: not an on-demand client (TORIRS_DRIVE_ON_DEMAND=1 is "
            "unset); this run's script is TORIRS_QUEST_SCRIPT");
    else if( g_demand_release_pending || g_demand_stop_pending )
        snprintf(reason, sizeof(reason), "drive.play: the last script is still ending (ask again next "
            "frame): %s", g_demand_script);
    else if( g_demand_state == DRIVE_DEMAND_RUNNING )
        snprintf(reason, sizeof(reason), "drive.play: a script is running: %s", g_demand_script);
    else if( (index = drive_driver_plugin_index()) < 0 || !PluginHost_IsRunning(g_app->plugins, index) )
        snprintf(reason, sizeof(reason), "drive.play: the quest-driver plugin is not running in this client");
    else if( !drive_world_ready() )
        snprintf(reason, sizeof(reason), "drive.play: the world is not ready (log in first)");
    if( reason[0] )
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);

    if( drive_play_field(L, "id", 1, g_demand_id, sizeof(g_demand_id), reason, sizeof(reason)) ||
        drive_play_field(L, "source", 1, source, sizeof(source), reason, sizeof(reason)) ||
        drive_play_field(L, "fixture", 1, g_demand_fixture_path, sizeof(g_demand_fixture_path), reason,
            sizeof(reason)) ||
        drive_play_field(L, "suite", 0, g_demand_suite, sizeof(g_demand_suite), reason, sizeof(reason)) ||
        drive_play_field(L, "title", 0, g_demand_title, sizeof(g_demand_title), reason, sizeof(reason)) )
    {
        g_demand_id[0] = '\0';
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    }
    lua_getfield(L, 1, "legs");
    g_demand_legs = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 0;
    lua_pop(L, 1);

    if( drive_play_pick_account(g_demand_id, g_demand_account, sizeof(g_demand_account)) != 0 )
    {
        g_demand_account[0] = '\0';
        snprintf(reason, sizeof(reason), "drive.play: no free account name for %s", g_demand_id);
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    }
    /* ABSOLUTE, as run.py's TORIRS_CONTENT_TEST is: App_RequestScreenshot
     * puts a relative capture dir under the plugin prefs' asset directory, so
     * a relative session dir sent every shot to
     * <prefs dir>/plugin_assets/client/build/quest_gate/watch/... (measured,
     * seam24 p1) while the ledger landed here. */
    {
        char cwd[600];
        if( !DRIVE_GETCWD(cwd, sizeof(cwd)) )
            return PluginDrive_PushResult(L, DRIVE_REFUSED, "drive.play: cannot read the working directory");
        snprintf(session, sizeof(session), "%s/%s/%s", cwd, DRIVE_WATCH_ROOT, g_demand_account);
    }
    snprintf(shots, sizeof(shots), "%s/shots", session);
    if( strlen(session) >= sizeof(g_demand_session) || drive_make_directories(shots) != 0 )
    {
        snprintf(reason, sizeof(reason), "drive.play: cannot create the session directory %s", session);
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    }

    if( !g_demand_camera_saved )
    {
        int owned = 0;
        (void)DrivePointer_CameraPose(g_app, &g_demand_camera_yaw, &g_demand_camera_pitch,
            &g_demand_camera_zoom, &owned);
        g_demand_camera_saved = 1;
        fprintf(stderr, "quest-driver: on demand: the first Play's camera pose yaw=%d pitch=%d zoom=%d "
            "is every Play's starting pose\n", g_demand_camera_yaw, g_demand_camera_pitch,
            g_demand_camera_zoom);
    }
    drive_play_discard();
    snprintf(g_demand_script, sizeof(g_demand_script), "%s", source);
    snprintf(g_demand_session, sizeof(g_demand_session), "%s", session);
    g_demand_refusal[0] = '\0';
    g_demand_play = 1;
    g_demand_state = DRIVE_DEMAND_RUNNING;
    g_demand_start_pending = 1;
    g_demand_runs++;
    drive_demand_reset_run();
    g_demand_source_state = DRIVE_FETCH_PENDING;
    g_demand_fixture_state = DRIVE_FETCH_PENDING;
    fprintf(stderr, "quest-driver: on demand: play %d: %s: asking for %s and %s; account %s\n",
        g_demand_fetch_serial, g_demand_id, source, g_demand_fixture_path, g_demand_account);
    ToriRS_TaskQueue_Add(g_app->runner.queue,
        CreateTask_PluginScriptRead(source, g_demand_fetch_serial, drive_play_deliver, (void*)(intptr_t)1));
    ToriRS_TaskQueue_Add(g_app->runner.queue,
        CreateTask_PluginScriptRead(g_demand_fixture_path, g_demand_fetch_serial, drive_play_deliver,
            (void*)(intptr_t)2));
    return PluginDrive_PushResult(L, DRIVE_OK, g_demand_account);
}

/* ------------------------------------------------------------ party barrier
 *
 * t.party.barrier (script/plugins/quest_driver/raid.lua): every raider of a
 * party run (tools/quest_gate/run.py run_party) writes
 * <run dir>/barrier.<name>.p<n> and waits until all of them are there. The run
 * dir is the session dir's parent (build/quest_gate/<id>/, the session dirs
 * being its p1/ .. pN/). These files are driver state, not game state: the
 * world never sees them. Lua has no io library in this state
 * (torirs_plugin_lua.c opens base, table, string, math, utf8), so the two
 * file operations live here, confined to a bare file name in that one
 * directory.
 *
 * THE FILES ARE STAMPED WITH THE LOCKSTEP TICK (raid seam21,
 * party_lockstep_frames). Between two boundaries the three clients run their
 * frames at the same time, in three processes, so "is raider 2's file there
 * yet?" asked in a frame of that interval was a wall-clock race: one run in
 * three the leader saw a member's mark a tick later, passed the barrier a tick
 * later, and everything after it shifted (seam19's one-tick shift; measured
 * 2026-10-04: `party.barrier.normal_out ... p1 waited 1 tick(s)` in one run,
 * `0` in the next, same build, same tick log). Now a mark records
 * ToriRSServer_EmbedLockstepTick() -- the tick of this process's last
 * boundary -- and counts as present only to a reader whose own lockstep tick
 * is LATER. That is a fact of the lock step, not of timing: a mark written
 * before boundary t+1 was written before its writer's READY (a member) or
 * before the leader ran t+1 (the leader), and every reader past t+1 got its
 * TICK after that. So every raider passes a barrier on the same tick, every
 * run. Outside a party (no lockstep tick) a mark counts as soon as it
 * exists, as before. */
static int
drive_run_dir_path(char const* file, char* out, size_t capacity)
{
    char const* dir = DriveCore_SessionDir();
    char const* slash;
    int length;
    int written;

    assert(file);
    assert(out);
    /* A bare file name: the run dir is the only directory these touch. */
    assert(file[0] != '\0');
    assert(strchr(file, '/') == NULL);
    if( !dir )
        return 0;
    slash = strrchr(dir, '/');
    length = slash ? (int)(slash - dir) : 0;
    /* A trailing slash on the session dir would name the session dir
     * itself; run.py never passes one. */
    assert(!slash || slash[1] != '\0');
    if( slash )
        written = snprintf(out, capacity, "%.*s/%s", length, dir, file);
    else
        written = snprintf(out, capacity, "./%s", file);
    assert(written > 0);
    assert((size_t)written < capacity);
    (void)written; /* only the assert reads it, and OPT=1 has no asserts */
    return 1;
}

static int
lua_drive_barrier_mark(struct lua_State* L)
{
    char const* file = PluginDrive_ArgString(L, 1);
    char path[1200];
    FILE* f;

    if( !drive_run_dir_path(file, path, sizeof(path)) )
        return PluginDrive_PushResult(L, DRIVE_UNSUPPORTED, NULL);
    f = fopen(path, "wb");
    if( !f )
        /* run.py made the directory; a write that fails is the run's
         * problem to report (the barrier then times out naming it), not a
         * contract violation by the caller. */
        return PluginDrive_PushResult(L, DRIVE_REFUSED, NULL);
    fprintf(f, "lockstep=%d\ntick=%d\n", ToriRSServer_EmbedLockstepTick(),
            g_app && g_app->world ? (int)(g_app->world->cycle / APP_SERVER_TICK_LOGIC_CYCLES)
                                  : -1);
    fclose(f);
    return PluginDrive_PushResult(L, DRIVE_OK, NULL);
}

static int
lua_drive_barrier_present(struct lua_State* L)
{
    char const* file = PluginDrive_ArgString(L, 1);
    char path[1200];
    FILE* f;

    if( !drive_run_dir_path(file, path, sizeof(path)) )
        return PluginDrive_PushResult(L, DRIVE_UNSUPPORTED, NULL);
    f = fopen(path, "rb");
    if( !f )
        return PluginDrive_PushResult(L, DRIVE_NOT_FOUND, NULL);
    {
        int const now = ToriRSServer_EmbedLockstepTick();
        int mark = TORIRSSERVER_EMBED_LOCKSTEP_NONE;
        int parsed = fscanf(f, "lockstep=%d", &mark) == 1;

        fclose(f);
        /* Not a party (no lockstep tick here): existence is the answer. */
        if( now == TORIRSSERVER_EMBED_LOCKSTEP_NONE )
            return PluginDrive_PushResult(L, DRIVE_OK, NULL);
        /* A mark with no stamp (torn, or another build's) or one from this
         * very interval is not there YET: it is honoured from the next
         * boundary on, by every raider at once. */
        if( !parsed || mark == TORIRSSERVER_EMBED_LOCKSTEP_NONE || mark >= now )
            return PluginDrive_PushResult(L, DRIVE_NOT_FOUND, NULL);
    }
    return PluginDrive_PushResult(L, DRIVE_OK, NULL);
}

/* api_drive.players() -> result, rows: every player in THIS client's entity
 * pool -- the raiders it can see -- as {name, x, z, level, pid, me}. The tile
 * is the world tile (scene base + grid), `me` marks the local player. A read
 * of what the client was sent, so it works on a party member as on the
 * leader; t.party.players (raid.lua) filters it by radius. */
static int
lua_drive_players(struct lua_State* L)
{
    struct World_EntityPool* pool;
    int i;
    int n = 0;
    int local_idx = -1;

    assert(g_app);
    if( !g_app->world )
        return PluginDrive_PushResult(L, DRIVE_NOT_FOUND, NULL);
    /* The local player the way drive_pointer_local_player finds it: the
     * entity sync's slot for local_pid (2047 before the server named one). */
    if( !RS_EntitySync_FindPlayer(&g_app->esync,
            g_app->esync.local_pid >= 0 ? g_app->esync.local_pid : 2047, &local_idx, NULL) )
        local_idx = -1;
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_newtable(L);
    pool = &g_app->world->entities.player;
    for( i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player const* player = World_EntityPoolGet(pool, i);
        if( !player )
            continue;
        lua_newtable(L);
        lua_pushstring(L, player->name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, g_app->world->_base_tile_x + player->grid_position.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, g_app->world->_base_tile_z + player->grid_position.z);
        lua_setfield(L, -2, "z");
        lua_pushinteger(L, player->grid_position.level);
        lua_setfield(L, -2, "level");
        lua_pushinteger(L, player->server_pid);
        lua_setfield(L, -2, "pid");
        lua_pushboolean(L, i == local_idx);
        lua_setfield(L, -2, "me");
        lua_rawseti(L, -2, ++n);
    }
    return 2;
}

/* ------------------------------------------------------------ render skip
 *
 * t.render.skip / t.render.frame (script/plugins/quest_driver/world.lua).
 * The switch and the one-frame request are the App's (App::render_skip,
 * app_render.c, which also lists every piece of render-time state and what
 * forces a draw for it); these only reach them. run.py starts every quest
 * client with TORIRS_RENDER_SKIP=1, so a test rarely needs either: the
 * pointer verbs and t.shot already ask for the frames they read. */

static void
drive_push_render_state(struct lua_State* L)
{
    assert(L);
    assert(g_app);
    lua_createtable(L, 0, 6);
    lua_pushboolean(L, App_RenderSkipEnabled(g_app));
    lua_setfield(L, -2, "skip");
    lua_pushinteger(L, (lua_Integer)g_app->frames_rendered);
    lua_setfield(L, -2, "rendered");
    lua_pushinteger(L, (lua_Integer)g_app->render_skip.frames_drawn);
    lua_setfield(L, -2, "drawn");
    lua_pushinteger(L, (lua_Integer)g_app->render_skip.frames_skipped);
    lua_setfield(L, -2, "skipped");
    lua_pushinteger(L, (lua_Integer)g_app->render_skip.frames_caught_up);
    lua_setfield(L, -2, "caught_up");
    /* While a hull stands in the scene every frame is drawn
     * (App_RenderSkipFrame): a reader wondering why frames are not being
     * skipped is told so. */
    lua_pushinteger(L, App_RenderSkipHullsInScene(g_app));
    lua_setfield(L, -2, "hulls");
}

/* api.drive.render_skip([on]) -> "ok", {skip, rendered, drawn, skipped}.
 * With no argument (or nil) a pure read; a boolean switches skip first. */
static int
lua_drive_render_skip(struct lua_State* L)
{
    assert(g_app);
    if( !lua_isnoneornil(L, 1) )
    {
        /* Not luaL_checktype: the argument is optional, and check_drive_abi
         * reads a luaL_check* index as the caller's required floor. */
        if( lua_type(L, 1) != LUA_TBOOLEAN )
            return luaL_error(L, "drive.render_skip: wants true, false or nothing");
        App_RenderSkipSet(g_app, lua_toboolean(L, 1));
    }
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    drive_push_render_state(L);
    return 2;
}

/* api.drive.render_frame() -> "ok", state: owe the current frame a draw.
 * t.render.frame awaits `rendered` moving past the state returned here. */
static int
lua_drive_render_frame(struct lua_State* L)
{
    assert(g_app);
    App_RenderSkipRequestDraw(g_app, 1);
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    drive_push_render_state(L);
    return 2;
}

/* ------------------------------------------------------------ the views
 *
 * The runner camera split (struct App_ViewSplit, app_render.c): a script's
 * own view of the world beside the watcher's. QD.core_run_test (the Scripts
 * tab's runner, core.lua) attaches at start and detaches at finish; the
 * on-demand release detaches too (a stop). A client that presents nothing --
 * every test run -- attaches nothing and says so.
 */

static void
drive_push_view(struct lua_State* L, struct App_WorldView const* view)
{
    lua_createtable(L, 0, 13);
    lua_pushinteger(L, view->orbit.yaw);
    lua_setfield(L, -2, "yaw");
    lua_pushinteger(L, view->orbit.pitch);
    lua_setfield(L, -2, "pitch");
    lua_pushinteger(L, view->world_cam_zoom);
    lua_setfield(L, -2, "zoom");
    lua_pushinteger(L, view->world_mouse_x);
    lua_setfield(L, -2, "pointer_x");
    lua_pushinteger(L, view->world_mouse_y);
    lua_setfield(L, -2, "pointer_y");
    lua_pushboolean(L, view->minimenu->visible);
    lua_setfield(L, -2, "menu_open");
    lua_pushinteger(L, view->world_pickset.count);
    lua_setfield(L, -2, "picked");
    /* The eye the frame draws from (a cutscene writes this, not the orbit). */
    lua_pushinteger(L, view->world_camera.yaw);
    lua_setfield(L, -2, "eye_yaw");
    lua_pushinteger(L, view->world_camera.pitch);
    lua_setfield(L, -2, "eye_pitch");
    lua_pushinteger(L, view->world_camera_pos.x);
    lua_setfield(L, -2, "eye_x");
    lua_pushinteger(L, view->world_camera_pos.y);
    lua_setfield(L, -2, "eye_y");
    lua_pushinteger(L, view->world_camera_pos.z);
    lua_setfield(L, -2, "eye_z");
}

/* {attached, views, presentable, interact, refusal, runner = {...},
 *  watcher = {...} (attached only), offscreen, offscreen_reads,
 *  offscreen_shots, presented, delivered, dropped, held, watcher_serial} */
static void
drive_push_view_status(struct lua_State* L, char const* reason)
{
    struct App_ViewSplit const* split = &g_app->view_split;

    lua_createtable(L, 0, 18);
    lua_pushboolean(L, split->attached);
    lua_setfield(L, -2, "attached");
    lua_pushinteger(L, g_app->view_split.view_count);
    lua_setfield(L, -2, "views");
    lua_pushboolean(L, split->presentable);
    lua_setfield(L, -2, "presentable");
    lua_pushboolean(L, split->interact);
    lua_setfield(L, -2, "interact");
    if( reason )
    {
        lua_pushstring(L, reason);
        lua_setfield(L, -2, "reason");
    }
    if( split->lane_refusal )
    {
        lua_pushstring(L, split->lane_refusal);
        lua_setfield(L, -2, "lane_refusal");
    }
    drive_push_view(L, &g_app->views[APP_VIEW_RUNNER]);
    lua_setfield(L, -2, "runner");
    if( split->attached )
    {
        drive_push_view(L, &g_app->views[APP_VIEW_PLAYER_CLIENT]);
        lua_setfield(L, -2, "watcher");
    }
    lua_pushinteger(L, (lua_Integer)split->offscreen_frames);
    lua_setfield(L, -2, "offscreen");
    lua_pushinteger(L, (lua_Integer)split->offscreen_frames_for_reads);
    lua_setfield(L, -2, "offscreen_reads");
    lua_pushinteger(L, (lua_Integer)split->offscreen_frames_for_shots);
    lua_setfield(L, -2, "offscreen_shots");
    lua_pushinteger(L, (lua_Integer)split->presented_frames);
    lua_setfield(L, -2, "presented");
    lua_pushinteger(L, (lua_Integer)split->physical_delivered);
    lua_setfield(L, -2, "delivered");
    lua_pushinteger(L, (lua_Integer)split->physical_dropped);
    lua_setfield(L, -2, "dropped");
    lua_pushinteger(L, (lua_Integer)split->physical_held);
    lua_setfield(L, -2, "held");
    lua_pushinteger(L, (lua_Integer)split->watcher_serial);
    lua_setfield(L, -2, "watcher_serial");
}

/* api.drive.view_attach("AutomationRunner") -> "ok", status. The only role a
 * script attaches is its own; any other name is "refused". `attached` false
 * with a `reason` is a client that presents nothing (one view, as before). */
static int
lua_drive_view_attach(struct lua_State* L)
{
    char const* role = luaL_checkstring(L, 1);
    char const* reason = NULL;

    assert(g_app);
    if( strcmp(role, "AutomationRunner") != 0 )
    {
        lua_pushstring(L, DriveResultName(DRIVE_REFUSED));
        lua_pushfstring(L, "drive.view_attach: a script attaches only \"AutomationRunner\", not \"%s\"", role);
        return 2;
    }
    (void)App_ViewAttachPlayerClient(g_app, &reason);
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    drive_push_view_status(L, reason);
    return 2;
}

/* api.drive.view_detach() -> "ok", status. Nothing attached: a no-op. */
static int
lua_drive_view_detach(struct lua_State* L)
{
    assert(g_app);
    App_ViewDetachPlayerClient(g_app);
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    drive_push_view_status(L, NULL);
    return 2;
}

/* api.drive.view_status() -> "ok", status. */
static int
lua_drive_view_status(struct lua_State* L)
{
    assert(g_app);
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    drive_push_view_status(L, NULL);
    return 2;
}

/* api.drive.view_interact([on]) -> "ok", status: the Interact switch (the
 * Scripts panel's button). A pure read with no argument; "refused" while no
 * script's view is attached. */
static int
lua_drive_view_interact(struct lua_State* L)
{
    assert(g_app);
    if( !lua_isnoneornil(L, 1) )
    {
        if( lua_type(L, 1) != LUA_TBOOLEAN )
            return luaL_error(L, "drive.view_interact: wants true, false or nothing");
        if( !g_app->view_split.attached )
        {
            lua_pushstring(L, DriveResultName(DRIVE_REFUSED));
            lua_pushstring(L, "drive.view_interact: no script's view is attached (one view: the person already has the game)");
            return 2;
        }
        App_ViewSetInteract(g_app, lua_toboolean(L, 1));
    }
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    drive_push_view_status(L, NULL);
    return 2;
}

/* api.drive.view_watcher([after]) -> "ok", {{serial, tick, what, x, y,
 * detail}, ...}: the watcher's actions while Interact was on, after serial
 * `after` (0 = all kept), oldest first. */
static int
lua_drive_view_watcher(struct lua_State* L)
{
    struct App_ViewSplitWatcherAction actions[APP_VIEW_SPLIT_WATCHER_MAX];
    lua_Integer after = 0;
    int count;

    assert(g_app);
    if( !lua_isnoneornil(L, 1) )
    {
        if( !lua_isinteger(L, 1) )
            return luaL_error(L, "drive.view_watcher: wants an integer serial or nothing");
        after = lua_tointeger(L, 1);
    }
    count = App_ViewWatcherActions(
        g_app, after > 0 ? (uint32_t)after : 0, actions, APP_VIEW_SPLIT_WATCHER_MAX);
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, count, 0);
    for( int i = 0; i < count; i++ )
    {
        lua_createtable(L, 0, 6);
        lua_pushinteger(L, (lua_Integer)actions[i].serial);
        lua_setfield(L, -2, "serial");
        lua_pushinteger(L, actions[i].tick);
        lua_setfield(L, -2, "tick");
        lua_pushstring(L, actions[i].what);
        lua_setfield(L, -2, "what");
        lua_pushinteger(L, actions[i].x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, actions[i].y);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, actions[i].detail);
        lua_setfield(L, -2, "detail");
        lua_rawseti(L, -2, i + 1);
    }
    return 2;
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
    {"start", lua_drive_start},
    {"stop", lua_drive_stop},
    {"status", lua_drive_status},
    {"tests", lua_drive_tests},
    {"play", lua_drive_play},
    {"forget_varps", lua_drive_forget_varps},
    {"barrier_mark", lua_drive_barrier_mark},
    {"barrier_present", lua_drive_barrier_present},
    {"players", lua_drive_players},
    {"render_skip", lua_drive_render_skip},
    {"render_frame", lua_drive_render_frame},
    {"view_attach", lua_drive_view_attach},
    {"view_detach", lua_drive_view_detach},
    {"view_status", lua_drive_view_status},
    {"view_interact", lua_drive_view_interact},
    {"view_watcher", lua_drive_view_watcher},
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

/* torirs_plugin_drive_ticklog.c (raid seam 1: api.drive.server_tick and the
 * tick log). Declared here, at its one call, because torirs_plugin_drive.h
 * belongs to another owner. */
void PluginDriveTicklog_RegisterLua(struct lua_State* L, void* script);

static void
drive_install_modules(struct lua_State* L, void* script)
{
    assert(L);
    assert(script);
    /* One flat `api.drive`, assembled from seven files. Order is registration
     * order only; the names are disjoint and the inventory test proves it. */
    lua_newtable(L);
    PluginDriveCore_RegisterLua(L, script);
    PluginDriveState_RegisterLua(L, script);
    PluginDriveChat_RegisterLua(L, script);
    PluginDriveRead_RegisterLua(L, script);
    PluginDrivePointer_RegisterLua(L, script);
    PluginDriveUi_RegisterLua(L, script);
    PluginDriveTicklog_RegisterLua(L, script);
    lua_setfield(L, -2, "drive");
}

void
PluginDrive_Init(struct App* app)
{
    assert(app);
    /* The same gate content_test.c uses, and for the same reason: `api.drive`
     * reaches the embedded server in-process and drives the client as a test
     * harness. It must not exist in a client a person is playing -- with the
     * one exception a person asks for by name: TORIRS_DRIVE_ON_DEMAND=1, the
     * client they launch to WATCH a test play (the Scripts tab, raid seam23;
     * profiles/osrs239-scripts.ini). */
    if( !ContentTest_Enabled() && !PluginDrive_OnDemand() )
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
    g_heartbeat_tick = -1;
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
int PluginDrive_OnDemand(void) { return 0; }
void PluginDrive_OnDemandHandOver(struct ToriRSServerEmbed* embed, struct ToriRS_CmdBus* bus)
{
    (void)embed;
    (void)bus;
    assert(0 && "PluginDrive_OnDemandHandOver: no on-demand driver without EMBED_SERVER");
}
void PluginDrive_FrameBoundary(void)
{
    assert(0 && "PluginDrive_FrameBoundary: no on-demand driver without EMBED_SERVER");
}
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
