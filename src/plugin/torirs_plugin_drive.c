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
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(TORIRS_PLATFORM_WEB)
/* The launch channel's free loopback port and the party host's listener. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
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
#include "torirs_plugin_drive_parts.h"

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
/* Raid seam25: the Play's starting state ("reset" | "fresh" | "as_is"),
 * and the fixture a reset applies in place ("" = the plain reset). */
static char g_demand_start[16];
static char g_demand_reset_fixture[64];
static int g_demand_leg;
static int g_demand_fetch_serial;
static enum DriveFetch g_demand_source_state;
static char* g_demand_source_bytes;
static int g_demand_source_size;
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
/* THE PARTY OF A PLAY (raid seam37, client_launch_channel). api.drive.play's
 * `party` table: this client's role (1 the leader .. size), the size, every
 * raider's account, and whether this client LAUNCHES the others (the leader
 * of a launched party: its fixture is written for every account and
 * QD.core_run_test brings the members up through the launch service). Size 0:
 * a Play of one. QD_PARTY is set from it before the test's chunk runs, so a
 * Play run as a member has the QD_PARTY a run.py member's wrapper gives. */
enum
{
    DRIVE_PARTY_MAX = 4,
    DRIVE_ACCOUNT_MAX = 16,
};
static int g_demand_party_role;
static int g_demand_party_size;
static int g_demand_party_launch;
static char g_demand_party_names[DRIVE_PARTY_MAX][DRIVE_ACCOUNT_MAX];
/* A launching leader's `party.windowed[seat]` (the Scripts tab's Windowed
 * tick, raid seam37 scripts_tab_party_play): that member starts with a
 * window instead of headless. Seat 1 (the leader) is never read. */
static int g_demand_party_windowed[DRIVE_PARTY_MAX];
/* The fixture each seat's save is written from, by seat - 1. One read unless
 * the path names "{seat}" (a party's per-seat LOADOUT, tools/raid_agent/
 * loadout.py; run.py write_session_fixture): then seat n's is the path with
 * n in its place, and a launching leader reads every seat's. A seat this
 * Play writes no save for stays DRIVE_FETCH_NONE. */
static enum DriveFetch g_demand_fixture_state[DRIVE_PARTY_MAX];
static char* g_demand_fixture_bytes[DRIVE_PARTY_MAX];
static int g_demand_fixture_size[DRIVE_PARTY_MAX];
/* The launch session this process's driver last opened (its open answer's
 * session= and token=), so another plugin of this client -- the Scripts tab's
 * PARTY block -- can read launch/status and send Stop-all / close for the
 * party a Play brought up (api.drive.party().launch_session/_token). Empty
 * until an open lands; cleared when a close of it lands. */
static char g_launch_own_session[64];
static char g_launch_own_token[96];
/* The account came from the Play (a member's, given by its leader), not
 * from the picker. */
static int g_demand_account_given;
/* api.drive.quit / a `quit` command: the client ends at this frame's end. */
static int g_quit_requested;
static int g_quit_code;
/* The Play's `quit = true`: the client exits with the Play's exit code once
 * it finishes (a headless leader's proof run; a person's tab never sets it). */
static int g_demand_quit_after;
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
    if( g_quit_requested )
    {
        /* api.drive.quit, or a `quit` its leader sent (raid seam37): the one
         * way a watched client ends itself. */
        *out_code = g_quit_code;
        return 1;
    }
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
    TORIRSSERVER_PACK_SEQ,
    TORIRSSERVER_PACK_SPOTANIM,
};

static char const* const DRIVE_SYMBOL_KIND_NAMES[DRIVE_SYMBOL_KIND_COUNT] = {
    "npc", "obj", "loc", "component", "interface", "varp", "varbit", "stat", "inv", "seq",
    "spotanim",
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
 * suspended in a yield, as long as nothing below tries to yield itself. The
 * sandbox has no `coroutine`, so the one way a predicate can try is a nested
 * api.drive.await that has to suspend, and lua_drive_await refuses that with
 * an error before it touches g_await (the driver's own verbs answer "not
 * yet" there instead: quest_driver/pointer.lua QD.player._loc_variants).
 * A predicate error is the test author's bug, not a
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

    /* This await has to suspend, and from inside an await predicate it
     * cannot: drive_await_level_true/match_true run the predicate under
     * lua_pcall, a C frame no yield may cross. lua_yield would raise
     * "attempt to yield across a C-call boundary" -- but only after the lines
     * below had overwritten g_await with THIS descriptor, leaking the outer
     * await's refs and leaving the outer t.await polling the nested level
     * and settling on it (b70: t.world.loc_near in a t.await level, via the
     * multiloc resolve's def wait). Refuse here, with g_await untouched; the
     * outer poll reports the error and reads the predicate as not yet true. */
    if( !lua_isyieldable(L) )
    {
        if( level_ref != LUA_NOREF ) luaL_unref(L, LUA_REGISTRYINDEX, level_ref);
        if( match_ref != LUA_NOREF ) luaL_unref(L, LUA_REGISTRYINDEX, match_ref);
        return luaL_error(L,
            "drive.await '%s' would suspend inside an await predicate: a predicate cannot wait, "
            "answer not-yet and let the outer await poll again",
            note);
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

/* `user` is 0 for the source, else the seat (1..) whose fixture this is. */
static void
drive_play_deliver(void* user, int serial, char const* path, void* data, int size)
{
    int const which = (int)(intptr_t)user;

    assert(path);
    assert(which >= 0);
    assert(which <= DRIVE_PARTY_MAX);
    if( serial != g_demand_fetch_serial )
    {
        free(data); /* the Play that asked was stopped before this landed */
        return;
    }
    if( which == 0 )
    {
        free(g_demand_source_bytes);
        g_demand_source_bytes = (char*)data;
        g_demand_source_size = data ? size : 0;
        g_demand_source_state = data ? DRIVE_FETCH_READY : DRIVE_FETCH_MISSING;
    }
    else
    {
        free(g_demand_fixture_bytes[which - 1]);
        g_demand_fixture_bytes[which - 1] = (char*)data;
        g_demand_fixture_size[which - 1] = data ? size : 0;
        g_demand_fixture_state[which - 1] = data ? DRIVE_FETCH_READY : DRIVE_FETCH_MISSING;
    }
    fprintf(stderr, "quest-driver: on demand: play %d %s %s: %s (%d bytes)\n", serial,
        which == 0 ? "source" : "fixture", path, data ? "landed" : "absent", data ? size : 0);
}

/* Seat `seat`'s fixture path: g_demand_fixture_path with "{seat}" replaced by
 * the seat number, or the path itself when it names no "{seat}". */
static void
drive_play_fixture_path(int seat, char* out, size_t capacity)
{
    char const* mark;

    assert(seat >= 1);
    assert(seat <= DRIVE_PARTY_MAX);
    assert(out);
    mark = strstr(g_demand_fixture_path, "{seat}");
    if( !mark )
    {
        snprintf(out, capacity, "%s", g_demand_fixture_path);
        return;
    }
    snprintf(out, capacity, "%.*s%d%s", (int)(mark - g_demand_fixture_path), g_demand_fixture_path,
        seat, mark + strlen("{seat}"));
}

/* Whether any fixture read of this Play is still in flight. */
static int
drive_play_fixtures_pending(void)
{
    for( int i = 0; i < DRIVE_PARTY_MAX; i++ )
        if( g_demand_fixture_state[i] == DRIVE_FETCH_PENDING )
            return 1;
    return 0;
}

/* Forget a Play's reads: its bytes, and any reply still in flight. */
static void
drive_play_discard(void)
{
    g_demand_fetch_serial++;
    free(g_demand_source_bytes);
    g_demand_source_bytes = NULL;
    g_demand_source_size = 0;
    for( int i = 0; i < DRIVE_PARTY_MAX; i++ )
    {
        free(g_demand_fixture_bytes[i]);
        g_demand_fixture_bytes[i] = NULL;
        g_demand_fixture_size[i] = 0;
        g_demand_fixture_state[i] = DRIVE_FETCH_NONE;
    }
    g_demand_source_state = DRIVE_FETCH_NONE;
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

/* run.py's write_session_fixture: seat `seat`'s fixture with its first
 * `name = ...` line naming the account, written where the embedded server
 * reads that account's save. 0 on success, else `reason` says why. */
static int
drive_play_write_fixture(char const* account, int seat, char* reason, size_t capacity)
{
    char const* save;
    char directory[1024];
    char fixture_path[TORIRS_IOITEM_MAX_PATH];
    char* slash;
    FILE* f;
    char const* text;
    int size;
    int slot;
    int line = 0;
    int name_start = -1;
    int name_end = -1;

    assert(account);
    assert(seat >= 1);
    assert(seat <= DRIVE_PARTY_MAX);
    assert(reason);
    /* One fixture for every seat unless the path is per seat. */
    slot = strstr(g_demand_fixture_path, "{seat}") ? seat - 1 : 0;
    drive_play_fixture_path(seat, fixture_path, sizeof(fixture_path));
    if( g_demand_fixture_state[slot] != DRIVE_FETCH_READY )
    {
        snprintf(reason, capacity, "play: no fixture at script item %s", fixture_path);
        return -1;
    }
    text = g_demand_fixture_bytes[slot];
    size = g_demand_fixture_size[slot];
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
            fixture_path);
        return -1;
    }
    save = ToriRSServer_SavePath(account);
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
    fprintf(f, "name = %s", account);
    fwrite(text + name_end, 1, (size_t)(size - name_end), f);
    fclose(f);
    fprintf(stderr, "quest-driver: on demand: play %s: account %s from %s -> %s\n", g_demand_id,
        account, fixture_path, save);
    return 0;
}

/* {role, size, names = {...}} -- and `launch` with `with_launch` -- of the
 * Play's party. */
static void
drive_push_party(struct lua_State* L, int with_launch)
{
    assert(L);
    assert(g_demand_party_size > 1);
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, g_demand_party_role);
    lua_setfield(L, -2, "role");
    lua_pushinteger(L, g_demand_party_size);
    lua_setfield(L, -2, "size");
    lua_createtable(L, g_demand_party_size, 0);
    for( int seat = 1; seat <= g_demand_party_size; seat++ )
    {
        lua_pushstring(L, g_demand_party_names[seat - 1]);
        lua_rawseti(L, -2, seat);
    }
    lua_setfield(L, -2, "names");
    if( with_launch )
    {
        lua_pushboolean(L, g_demand_party_launch);
        lua_setfield(L, -2, "launch");
        lua_createtable(L, g_demand_party_size, 0);
        for( int seat = 1; seat <= g_demand_party_size; seat++ )
        {
            lua_pushboolean(L, g_demand_party_windowed[seat - 1]);
            lua_rawseti(L, -2, seat);
        }
        lua_setfield(L, -2, "windowed");
    }
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
    /* A member of a launched party plays on the account its leader wrote
     * (the fixture is the leader's to write, before it spawns the member:
     * the member logs in at boot). Every other Play writes its own, and the
     * leader of a launched party writes every raider's. */
    if( !g_demand_account_given &&
        drive_play_write_fixture(g_demand_account, g_demand_party_size > 1 ? g_demand_party_role : 1,
            reason, sizeof(reason)) != 0 )
    {
        drive_play_refuse(reason);
        return;
    }
    if( g_demand_party_launch )
    {
        for( int seat = 2; seat <= g_demand_party_size; seat++ )
            if( drive_play_write_fixture(g_demand_party_names[seat - 1], seat, reason, sizeof(reason)) != 0 )
            {
                drive_play_refuse(reason);
                return;
            }
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

    /* The party (raid seam37): QD_PARTY as run.py's wrapper writes it, set
     * before the test's chunk runs (QD.core_run_test calls the loader), and
     * the same table as options.party with `launch` added. A Play of one
     * sets nothing: a solo test reads no QD_PARTY. */
    if( g_demand_party_size > 1 )
    {
        drive_push_party(g_thread, 0);
        lua_setglobal(g_thread, "QD_PARTY");
    }

    lua_getglobal(g_thread, "QD_ROOT");
    lua_createtable(g_thread, 0, 11);
    if( g_demand_party_size > 1 )
    {
        drive_push_party(g_thread, 1);
        lua_setfield(g_thread, -2, "party");
    }
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
    /* Raid seam37: a launching leader hands its members the same fixture
     * (a "{seat}" one unexpanded: each member reads its own seat's). */
    lua_pushstring(g_thread, g_demand_fixture_path);
    lua_setfield(g_thread, -2, "fixture");
    lua_pushinteger(g_thread, g_demand_legs);
    lua_setfield(g_thread, -2, "legs");
    lua_pushstring(g_thread, g_demand_start[0] ? g_demand_start : "fresh");
    lua_setfield(g_thread, -2, "start");
    if( g_demand_reset_fixture[0] )
    {
        lua_pushstring(g_thread, g_demand_reset_fixture);
        lua_setfield(g_thread, -2, "reset_fixture");
    }
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
    /* An agent the Play started (api.drive.botdrive) stops driving with it. */
    if( g_embed )
        ToriRSServer_EmbedBotDriveRelease(g_embed);
    g_started = 0;
    g_demand_state = DRIVE_DEMAND_FINISHED;
    if( g_demand_quit_after )
    {
        g_quit_requested = 1;
        g_quit_code = g_finish_code;
        fprintf(stderr, "quest-driver: on demand: the Play asked to quit when done (exit %d)\n",
            g_finish_code);
    }
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

static void drive_launch_member_pump(struct lua_State* L);
static void drive_own_animation_watch(void);

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
    /* The runner camera split: every driver verb acts through the runner's
     * view because the pump runs between the stages that move frame_view
     * (the presented draw and the emit walk put it back to views[0]). */
    assert(!g_app || g_app->frame_view == App_RunnerView(g_app));
    /* Raid seam37: TORIRS_DRIVE_AUTOPLAY, and a launched member's mailbox
     * and status (nothing at all in a client started without either). */
    if( g_app )
        drive_launch_member_pump(L);
    /* Raid seam48: every frame, so no start of the player's own action seq
     * goes unseen between two of the script's reads. */
    if( g_app )
        drive_own_animation_watch();
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
            if( g_demand_source_state != DRIVE_FETCH_PENDING && !drive_play_fixtures_pending() &&
                drive_world_ready() )
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
int
PluginDrive_ServerTick(void)
{
    struct ToriRSServer* srv = PluginDrive_EmbedWorld();
    int const lockstep = ToriRSServer_EmbedLockstepTick();

    if( srv && srv->world_built )
        return (int)srv->tick;
    if( lockstep != TORIRSSERVER_EMBED_LOCKSTEP_NONE )
        return lockstep;
    return g_app && g_app->world ? (int)(g_app->world->cycle / APP_SERVER_TICK_LOGIC_CYCLES) : 0;
}

int
PluginDrive_ServerTickOfCycle(int cycle)
{
    /* A client cycle stamped on an entity (the cycle a seq, a spotanim or a
     * face was applied), as the server tick it belongs to: the tick now,
     * less the whole ticks of cycles since. The client's own cycle count
     * and the server's tick drift apart over hundreds of ticks (see
     * lua_drive_tick), so a stamp divided by 30 named a tick the server
     * never ran it on -- her slam's seq read two ticks old on the live seat
     * and current on scriptrun, and a scorer that windows on it answered
     * finite on one lane and infinite on the other (2026-10-09). Rounded,
     * so a stamp from this tick's packets (a frame or two ago) is this tick. */
    int const now_cycle = g_app && g_app->world ? (int)g_app->world->cycle : 0;
    int const ago = now_cycle - cycle;

    /* A stamp newer than now is not a stamp this world made. */
    assert(ago >= 0);
    return PluginDrive_ServerTick() - (ago + APP_SERVER_TICK_LOGIC_CYCLES / 2) / APP_SERVER_TICK_LOGIC_CYCLES;
}

static int
lua_drive_tick(struct lua_State* L)
{
    /* THE SERVER'S OWN TICK where this client has one: the embed world's
     * counter for a leader, the lockstep tick a member is handed with every
     * TICK frame -- the number scriptrun's core->tick is, so a script's F.tick
     * names the same tick-log row on every lane. The client's own clock
     * (world cycle / 30) runs beside the server's and gained a tick on the
     * server every few hundred (a P3 solver's rotation slots were read a
     * tick early live and never on scriptrun, 2026-10-09); it is the answer
     * only for a client with no embed world at all. */
    lua_pushinteger(L, (lua_Integer)PluginDrive_ServerTick());
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
    g_demand_quit_after = 0;
    g_demand_party_size = 0;
    g_demand_party_launch = 0;
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
    lua_pushstring(L, g_demand_start[0] ? g_demand_start : "fresh");
    lua_setfield(L, -2, "start");
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

/* A login name the picker could have made, or a leader named: 1..12 of
 * a-z, 0-9 and `_` (the login form's limit; a save file's stem). */
static int
drive_account_name_valid(char const* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length > 12 )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

/* The Play's `party` table into g_demand_party_* (0), or the refusal (-1).
 * No `party`, or a size of 1: a Play of one. */
static int
drive_play_party(struct lua_State* L, char* reason, size_t reason_capacity)
{
    int names = 0;

    g_demand_party_size = 0;
    g_demand_party_role = 1;
    g_demand_party_launch = 0;
    memset(g_demand_party_names, 0, sizeof(g_demand_party_names));
    memset(g_demand_party_windowed, 0, sizeof(g_demand_party_windowed));
    lua_getfield(L, 1, "party");
    if( lua_isnil(L, -1) )
    {
        lua_pop(L, 1);
        return 0;
    }
    if( !lua_istable(L, -1) )
    {
        lua_pop(L, 1);
        snprintf(reason, reason_capacity, "drive.play: `party` is not a table {role, size, names, launch}");
        return -1;
    }
    lua_getfield(L, -1, "size");
    g_demand_party_size = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 0;
    lua_pop(L, 1);
    lua_getfield(L, -1, "role");
    g_demand_party_role = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 1;
    lua_pop(L, 1);
    lua_getfield(L, -1, "launch");
    g_demand_party_launch = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "windowed");
    if( lua_istable(L, -1) )
    {
        for( int seat = 2; seat <= DRIVE_PARTY_MAX; seat++ )
        {
            lua_rawgeti(L, -1, seat);
            g_demand_party_windowed[seat - 1] = lua_toboolean(L, -1);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, -1, "names");
    if( lua_istable(L, -1) )
    {
        names = (int)lua_rawlen(L, -1);
        for( int seat = 1; seat <= names && seat <= DRIVE_PARTY_MAX; seat++ )
        {
            char const* name;
            lua_rawgeti(L, -1, seat);
            name = lua_tostring(L, -1);
            if( name && strlen(name) < DRIVE_ACCOUNT_MAX )
                snprintf(g_demand_party_names[seat - 1], DRIVE_ACCOUNT_MAX, "%s", name);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
    if( g_demand_party_size <= 1 && !g_demand_party_launch )
    {
        g_demand_party_size = 0;
        return 0;
    }
    if( g_demand_party_size < 2 || g_demand_party_size > DRIVE_PARTY_MAX || g_demand_party_role < 1 ||
        g_demand_party_role > g_demand_party_size )
    {
        snprintf(reason, reason_capacity, "drive.play: party size %d role %d (size 2..%d, role 1..size)",
            g_demand_party_size, g_demand_party_role, DRIVE_PARTY_MAX);
        g_demand_party_size = 0;
        return -1;
    }
    if( g_demand_party_launch && g_demand_party_role != 1 )
    {
        snprintf(reason, reason_capacity, "drive.play: only role 1 (the leader) launches a party");
        g_demand_party_size = 0;
        return -1;
    }
    if( names != 0 && names != g_demand_party_size )
    {
        snprintf(reason, reason_capacity, "drive.play: party.names lists %d account(s) for a party of %d",
            names, g_demand_party_size);
        g_demand_party_size = 0;
        return -1;
    }
    if( names == 0 && !g_demand_party_launch )
    {
        snprintf(reason, reason_capacity, "drive.play: a party member's Play names every raider "
            "(party.names); only a launching leader picks them");
        g_demand_party_size = 0;
        return -1;
    }
    return 0;
}

/* Every raider's account once this Play's own is known: the ones the party
 * named, or -- a launching leader -- its own account then the next free
 * names after it (the picker's rule: no save file, no session dir, not taken
 * by an earlier seat). 0, or -1 with the reason. */
static int
drive_play_party_names(char* reason, size_t reason_capacity)
{
    struct stat info;

    assert(g_demand_party_size > 1);
    if( !g_demand_party_launch )
    {
        if( strcmp(g_demand_party_names[g_demand_party_role - 1], g_demand_account) != 0 )
        {
            snprintf(reason, reason_capacity, "drive.play: party.names[%d] is %s, not this Play's account %s",
                g_demand_party_role, g_demand_party_names[g_demand_party_role - 1], g_demand_account);
            return -1;
        }
        return 0;
    }
    snprintf(g_demand_party_names[0], DRIVE_ACCOUNT_MAX, "%s", g_demand_account);
    for( int seat = 2; seat <= g_demand_party_size; seat++ )
    {
        char candidate[DRIVE_ACCOUNT_MAX];
        int found = 0;

        for( int number = 1; number <= 9999 && !found; number++ )
        {
            char const* save;
            int taken = 0;

            /* <leader>p<seat>, then <leader>p<seat>n<k>: at most 12. */
            if( number == 1 )
                snprintf(candidate, sizeof(candidate), "%.9sp%d", g_demand_account, seat);
            else
                snprintf(candidate, sizeof(candidate), "%.6sp%dn%d", g_demand_account, seat, number);
            if( !drive_account_name_valid(candidate) )
                continue;
            for( int k = 0; k < seat - 1; k++ )
                if( strcmp(g_demand_party_names[k], candidate) == 0 )
                    taken = 1;
            save = ToriRSServer_SavePath(candidate);
            if( !taken && save[0] && stat(save, &info) != 0 )
                found = 1;
        }
        if( !found )
        {
            snprintf(reason, reason_capacity, "drive.play: no free account name for seat %d", seat);
            return -1;
        }
        snprintf(g_demand_party_names[seat - 1], DRIVE_ACCOUNT_MAX, "%s", candidate);
    }
    return 0;
}

/*
 * api.drive.play({id =, source =, fixture =, suite =, title =, legs =,
 *                 start =, reset_fixture =, account =, session =, party =, quit =})
 *   -> "ok", <the account it will play on> | "refused", reason
 *
 * Raid seam37 (client_launch_channel): `account` plays on a given account
 * (a launched member's, whose save its leader wrote), `session` names the run
 * directory, `party` = {role, size, names, launch} makes it a raider's Play
 * (QD_PARTY set before the chunk runs; a launching leader writes every
 * raider's fixture and brings the members up in QD.core_run_test), and
 * `quit = true` exits the client once the Play finishes. A launched member
 * issues its Play itself, from TORIRS_DRIVE_AUTOPLAY (drive_play_json).
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
    lua_getfield(L, 1, "quit");
    g_demand_quit_after = lua_toboolean(L, -1);
    lua_pop(L, 1);
    if( drive_play_field(L, "start", 0, g_demand_start, sizeof(g_demand_start), reason,
            sizeof(reason)) ||
        drive_play_field(L, "reset_fixture", 0, g_demand_reset_fixture,
            sizeof(g_demand_reset_fixture), reason, sizeof(reason)) )
    {
        g_demand_id[0] = '\0';
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    }
    if( g_demand_start[0] && strcmp(g_demand_start, "reset") != 0 &&
        strcmp(g_demand_start, "fresh") != 0 && strcmp(g_demand_start, "as_is") != 0 )
    {
        snprintf(reason, sizeof(reason), "drive.play: start '%s' is not reset, fresh or as_is",
            g_demand_start);
        g_demand_id[0] = '\0';
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    }

    /* Raid seam37: `account` (a member's, named by its leader), `session`
     * (its run directory) and `party`. */
    if( drive_play_party(L, reason, sizeof(reason)) != 0 ||
        drive_play_field(L, "account", 0, g_demand_account, sizeof(g_demand_account), reason,
            sizeof(reason)) ||
        drive_play_field(L, "session", 0, session, sizeof(session), reason, sizeof(reason)) )
    {
        g_demand_id[0] = '\0';
        g_demand_account[0] = '\0';
        g_demand_party_size = 0;
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    }
    g_demand_account_given = g_demand_account[0] != '\0';
    if( g_demand_account_given && !drive_account_name_valid(g_demand_account) )
    {
        snprintf(reason, sizeof(reason), "drive.play: account '%s' is not 1..12 of a-z, 0-9 and _",
            g_demand_account);
        g_demand_id[0] = '\0';
        g_demand_account[0] = '\0';
        g_demand_party_size = 0;
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    }
    if( !g_demand_account_given &&
        drive_play_pick_account(g_demand_id, g_demand_account, sizeof(g_demand_account)) != 0 )
    {
        g_demand_account[0] = '\0';
        g_demand_party_size = 0;
        snprintf(reason, sizeof(reason), "drive.play: no free account name for %s", g_demand_id);
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    }
    if( g_demand_party_size > 1 && drive_play_party_names(reason, sizeof(reason)) != 0 )
    {
        g_demand_account[0] = '\0';
        g_demand_party_size = 0;
        return PluginDrive_PushResult(L, DRIVE_REFUSED, reason);
    }
    /* ABSOLUTE, as run.py's TORIRS_CONTENT_TEST is: App_RequestScreenshot
     * puts a relative capture dir under the plugin prefs' asset directory, so
     * a relative session dir sent every shot to
     * <prefs dir>/plugin_assets/client/build/quest_gate/watch/... (measured,
     * seam24 p1) while the ledger landed here. A party's raiders share one
     * run directory, each in its p<n>/ (t.party.barrier's marks live in the
     * parent, as under run.py). */
    if( !session[0] || session[0] != '/' )
    {
        char cwd[600];
        char given[1100];
        snprintf(given, sizeof(given), "%s", session);
        if( !DRIVE_GETCWD(cwd, sizeof(cwd)) )
            return PluginDrive_PushResult(L, DRIVE_REFUSED, "drive.play: cannot read the working directory");
        if( given[0] )
            snprintf(session, sizeof(session), "%s/%s", cwd, given);
        else if( g_demand_party_size > 1 )
            snprintf(session, sizeof(session), "%s/%s/%s/p%d", cwd, DRIVE_WATCH_ROOT,
                g_demand_party_names[0], g_demand_party_role);
        else
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
    fprintf(stderr, "quest-driver: on demand: play %d: %s: asking for %s and %s; account %s\n",
        g_demand_fetch_serial, g_demand_id, source, g_demand_fixture_path, g_demand_account);
    ToriRS_TaskQueue_Add(g_app->runner.queue,
        CreateTask_PluginScriptRead(source, g_demand_fetch_serial, drive_play_deliver, (void*)(intptr_t)0));
    /* This client's own seat's fixture, and a launching leader's every
     * seat's when the path is per seat ("{seat}"); one file serves them all
     * otherwise. */
    for( int seat = 1; seat <= DRIVE_PARTY_MAX; seat++ )
    {
        int const own_seat = g_demand_party_size > 1 ? g_demand_party_role : 1;
        int const per_seat = strstr(g_demand_fixture_path, "{seat}") != NULL;
        char fixture_path[TORIRS_IOITEM_MAX_PATH];

        if( per_seat ? !(seat == own_seat || (g_demand_party_launch && seat <= g_demand_party_size))
                     : seat != 1 )
            continue;
        drive_play_fixture_path(seat, fixture_path, sizeof(fixture_path));
        g_demand_fixture_state[seat - 1] = DRIVE_FETCH_PENDING;
        ToriRS_TaskQueue_Add(g_app->runner.queue,
            CreateTask_PluginScriptRead(fixture_path, g_demand_fetch_serial, drive_play_deliver,
                (void*)(intptr_t)seat));
    }
    return PluginDrive_PushResult(L, DRIVE_OK, g_demand_account);
}

/* ------------------------------------------------- the launch channel
 *
 * Raid seam37 (client_launch_channel; docs/minigames/raid_loop/
 * SEAM_TRIAGE_2026-10-06j.md). The leader client's embedded IO server keeps
 * the launch service (src/platform/launch_sessions.h); the driver reaches it
 * through the plugin channel, one `launch/<verb>` item per request
 * (task_plugin_io.c CreateTask_PluginLaunch, answered in this process by
 * platform_x_io.c). An item answers on a later frame, so every verb is a
 * request and a ticket:
 *
 *   api.drive.launch_open(body)    -> "ok", ticket, port
 *   api.drive.launch_spawn(body)   -> "ok", ticket
 *   api.drive.launch_command(body) -> "ok", ticket
 *   api.drive.launch_status(body)  -> "ok", ticket
 *   api.drive.launch_close(body)   -> "ok", ticket
 *   api.drive.launch_answer(ticket)-> "timeout", "pending" | "ok", <answer text>
 *                                    | "unsupported", reason | "refused", reason
 *
 * `body` is the request's key=value lines, as a string, or a table of
 * key = value (a string, integer or boolean each; spawn's seat blocks are
 * ordered, so spawn takes a string). launch_open fills pid= (this process),
 * saves= (TORIRSSERVER_SAVES) when absent, and port=0 asks for a free
 * loopback port, returned third. Flat names: tools/quest_gate/
 * check_drive_abi.py reads flat registration arrays (the design's
 * api.drive.launch.open is t.launch.open, raid.lua).
 *
 * The rest of the party half:
 *   api.drive.party_host(port, size) -> "ok", detail | "refused" | "unsupported"
 *   api.drive.party()                -> "ok", {role, size, names, launched,
 *                                       session, seat_token?, frame_locked,
 *                                       mail_answers, mail}
 *   api.drive.quit(code)             -> "ok": the client exits at this frame's end
 * and two knobs of a launched member (the service sets both):
 *   TORIRS_DRIVE_AUTOPLAY=<json>  the Play this client issues once its world
 *                                 is ready (the same table api.drive.play takes)
 *   TORIRS_LAUNCH_SESSION/_SEAT/_SEAT_TOKEN  its mailbox: polled once a tick
 *                                 over the party link (torirs_server_embed.h
 *                                 'M'), its status posted with every poll;
 *                                 `play {json}`, `stop`, `cheat <line>` and
 *                                 `quit` are acted on at the next pump.
 */

enum
{
    DRIVE_LAUNCH_TICKETS = 16,
    DRIVE_LAUNCH_FREE = 0,
    DRIVE_LAUNCH_PENDING,
    DRIVE_LAUNCH_LANDED,
};

struct DriveLaunchTicket
{
    int state;
    int serial;
    char verb[16];
    /* a close's session= (to forget the own session when it lands) */
    char session[64];
    char* answer;
    int answer_size;
};

static struct DriveLaunchTicket g_launch_tickets[DRIVE_LAUNCH_TICKETS];
static int g_launch_serial;

static void
drive_launch_deliver(void* user, int serial, char const* path, void* data, int size)
{
    (void)user;
    (void)path;
    for( int i = 0; i < DRIVE_LAUNCH_TICKETS; i++ )
    {
        struct DriveLaunchTicket* ticket = &g_launch_tickets[i];
        if( ticket->state != DRIVE_LAUNCH_PENDING || ticket->serial != serial )
            continue;
        ticket->state = DRIVE_LAUNCH_LANDED;
        ticket->answer = (char*)data;
        ticket->answer_size = data ? size : 0;
        return;
    }
    free(data); /* a ticket nobody holds any more */
}

/* The body of a launch call: the string at `index`, or the table's key=value
 * lines (string, number or boolean values; others are skipped). Malloc'd,
 * NUL-terminated; *out_size its length. */
static char*
drive_launch_body(struct lua_State* L, int index, int* out_size)
{
    char* body = NULL;
    size_t length = 0;
    size_t capacity = 0;

    assert(out_size);
    if( lua_type(L, index) == LUA_TSTRING )
    {
        char const* text = lua_tolstring(L, index, &length);
        body = (char*)malloc(length + 1);
        assert(body);
        memcpy(body, text, length);
        body[length] = '\0';
        *out_size = (int)length;
        return body;
    }
    luaL_checktype(L, index, LUA_TTABLE);
    capacity = 256;
    body = (char*)malloc(capacity);
    assert(body);
    body[0] = '\0';
    lua_pushnil(L);
    while( lua_next(L, index) != 0 )
    {
        int const value_type = lua_type(L, -1);
        if( lua_type(L, -2) == LUA_TSTRING &&
            (value_type == LUA_TSTRING || value_type == LUA_TNUMBER || value_type == LUA_TBOOLEAN) )
        {
            char const* key = lua_tostring(L, -2);
            char const* value = value_type == LUA_TBOOLEAN ? (lua_toboolean(L, -1) ? "1" : "0")
                                                          : lua_tostring(L, -1);
            size_t const need = length + strlen(key) + strlen(value) + 3;
            if( need > capacity )
            {
                while( capacity < need )
                    capacity *= 2;
                body = (char*)realloc(body, capacity);
                assert(body);
            }
            length += (size_t)snprintf(body + length, capacity - length, "%s=%s\n", key, value);
        }
        lua_pop(L, 1);
    }
    *out_size = (int)length;
    return body;
}

/* Queue `verb` with `body` (taken); "ok", ticket on the stack. */
/* The value of the `key=` line in `size` bytes of `text` into `out`
 * (empty when there is none). */
static void
drive_launch_line_value(char const* text, int size, char const* key, char* out, size_t out_capacity)
{
    size_t const key_length = strlen(key);
    int at = 0;

    assert(text);
    assert(key);
    assert(out);
    assert(out_capacity > 0);
    out[0] = '\0';
    while( at < size )
    {
        int end = at;
        while( end < size && text[end] != '\n' && text[end] != '\0' )
            end++;
        if( (size_t)(end - at) > key_length && strncmp(text + at, key, key_length) == 0 &&
            text[at + (int)key_length] == '=' )
        {
            snprintf(out, out_capacity, "%.*s", end - at - (int)key_length - 1, text + at + key_length + 1);
            return;
        }
        if( end < size && text[end] == '\0' )
            return;
        at = end + 1;
    }
}

/* An `ok` open answer: this process's own launch session from now on. */
static void
drive_launch_remember_open(char const* answer)
{
    assert(answer);
    drive_launch_line_value(answer, (int)strlen(answer), "session", g_launch_own_session,
        sizeof(g_launch_own_session));
    drive_launch_line_value(answer, (int)strlen(answer), "token", g_launch_own_token,
        sizeof(g_launch_own_token));
}

/* An `ok` close of `session`: forget it when it is the own one, and let the
 * party it hosted go (the transport releases it once its members have quit),
 * so this client's next login and next party_host find no party. */
static void
drive_launch_forget_closed(char const* session)
{
    assert(session);
    if( session[0] && strcmp(session, g_launch_own_session) == 0 )
    {
        g_launch_own_session[0] = '\0';
        g_launch_own_token[0] = '\0';
        ToriRSServer_EmbedPartyHostRelease();
    }
}

static int
drive_launch_queue(struct lua_State* L, char const* verb, char* body, int size)
{
    struct DriveLaunchTicket* ticket = NULL;

    assert(verb);
    assert(body);
    for( int i = 0; i < DRIVE_LAUNCH_TICKETS && !ticket; i++ )
        if( g_launch_tickets[i].state == DRIVE_LAUNCH_FREE )
            ticket = &g_launch_tickets[i];
    if( !ticket )
    {
        free(body);
        return PluginDrive_PushResult(L, DRIVE_REFUSED,
            "drive.launch: 16 requests are unanswered or unread (read each with launch_answer)");
    }
    ticket->state = DRIVE_LAUNCH_PENDING;
    ticket->serial = ++g_launch_serial;
    snprintf(ticket->verb, sizeof(ticket->verb), "%s", verb);
    drive_launch_line_value(body, size, "session", ticket->session, sizeof(ticket->session));
    ticket->answer = NULL;
    ticket->answer_size = 0;
    ToriRS_TaskQueue_Add(g_app->runner.queue,
        CreateTask_PluginLaunch(verb, body, size, ticket->serial, drive_launch_deliver, NULL));
    free(body);
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_pushinteger(L, ticket->serial);
    return 2;
}

/* A free loopback port (bind 0, read it back, close): run.py's
 * free_loopback_port. 0 when there is none. */
static int
drive_free_loopback_port(void)
{
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(TORIRS_PLATFORM_WEB)
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int port = 0;
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);

    assert(fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if( bind(fd, (struct sockaddr*)&address, sizeof(address)) == 0 &&
        getsockname(fd, (struct sockaddr*)&address, &length) == 0 )
        port = ntohs(address.sin_port);
    close(fd);
    return port;
#else
    return 0;
#endif
}

static int
lua_drive_launch_open(struct lua_State* L)
{
    int size = 0;
    char* body = drive_launch_body(L, 1, &size);
    char const* saves = getenv("TORIRSSERVER_SAVES");
    char* port_line;
    int port = 0;
    int length;
    int result;

    /* The leader's own pid and saves unless the request names them, and a
     * port when it asks for 0 (or names none). */
    port_line = strstr(body, "port=");
    if( port_line && (port_line == body || port_line[-1] == '\n') )
        port = atoi(port_line + 5);
    if( port <= 0 )
        port = drive_free_loopback_port();
    if( port <= 0 )
    {
        free(body);
        return PluginDrive_PushResult(L, DRIVE_UNSUPPORTED,
            "drive.launch_open: no loopback port (the party link is POSIX only)");
    }
    length = size + 128 + (saves ? (int)strlen(saves) : 0);
    body = (char*)realloc(body, (size_t)length);
    assert(body);
    /* The service reads the FIRST pid=/port=/saves= (launch_sessions.c
     * request_value), so the filled values go first and the request's own
     * pid=/port= lines are dropped. */
    {
        char* filled = (char*)malloc((size_t)length);
        int written;
        assert(filled);
        written = snprintf(filled, (size_t)length, "pid=%d\nport=%d\n", (int)getpid(), port);
        if( !strstr(body, "saves=") && saves && saves[0] )
            written += snprintf(filled + written, (size_t)(length - written), "saves=%s\n", saves);
        /* the request's own lines, minus the port= and pid= the filled
         * values replace */
        {
            char* line = body;
            while( *line )
            {
                char* end = strchr(line, '\n');
                size_t line_length = end ? (size_t)(end - line) : strlen(line);
                if( strncmp(line, "port=", 5) != 0 && strncmp(line, "pid=", 4) != 0 && line_length > 0 )
                    written += snprintf(filled + written, (size_t)(length - written), "%.*s\n",
                        (int)line_length, line);
                line += line_length + (end ? 1 : 0);
            }
        }
        free(body);
        body = filled;
        size = written;
    }
    result = drive_launch_queue(L, "open", body, size);
    if( result == 2 && lua_isinteger(L, -1) )
    {
        lua_pushinteger(L, port);
        return 3;
    }
    return result;
}

static int
drive_launch_verb(struct lua_State* L, char const* verb)
{
    int size = 0;
    char* body = drive_launch_body(L, 1, &size);
    return drive_launch_queue(L, verb, body, size);
}

static int lua_drive_launch_spawn(struct lua_State* L) { return drive_launch_verb(L, "spawn"); }
static int lua_drive_launch_command(struct lua_State* L) { return drive_launch_verb(L, "command"); }
static int lua_drive_launch_status(struct lua_State* L) { return drive_launch_verb(L, "status"); }
static int lua_drive_launch_close(struct lua_State* L) { return drive_launch_verb(L, "close"); }

static int
lua_drive_launch_answer(struct lua_State* L)
{
    int const serial = PluginDrive_ArgInt(L, 1);
    struct DriveLaunchTicket* ticket = NULL;
    enum DriveResult result = DRIVE_OK;
    char const* text;

    for( int i = 0; i < DRIVE_LAUNCH_TICKETS && !ticket; i++ )
        if( g_launch_tickets[i].state != DRIVE_LAUNCH_FREE && g_launch_tickets[i].serial == serial )
            ticket = &g_launch_tickets[i];
    if( !ticket )
        return PluginDrive_PushResult(L, DRIVE_REFUSED, "drive.launch_answer: no such ticket (read once)");
    if( ticket->state == DRIVE_LAUNCH_PENDING )
        return PluginDrive_PushResult(L, DRIVE_TIMEOUT, "pending");
    text = ticket->answer ? ticket->answer : "unsupported: the IO executor does not serve launch items\n";
    if( strncmp(text, "unsupported:", 12) == 0 )
        result = DRIVE_UNSUPPORTED;
    else if( strncmp(text, "ok", 2) != 0 )
        result = DRIVE_REFUSED;
    if( result == DRIVE_OK && strcmp(ticket->verb, "open") == 0 )
        drive_launch_remember_open(text);
    else if( result == DRIVE_OK && strcmp(ticket->verb, "close") == 0 )
        drive_launch_forget_closed(ticket->session);
    lua_pushstring(L, DriveResultName(result));
    lua_pushstring(L, text);
    free(ticket->answer);
    memset(ticket, 0, sizeof(*ticket));
    return 2;
}

/*
 * api.drive.party_host(port, size): host the party at runtime (raid seam37;
 * torirs_server_embed.h). Opens the listen socket now -- a member's dial
 * waits in its backlog -- and the embedded transport attaches it at its next
 * poll: that boundary waits up to TORIRS_EMBED_PARTY_WAIT_S for size - 1
 * seats, then the lock step runs. Call it AFTER launch_spawn: from the attach
 * on this client's frames are held at every boundary until the members are
 * READY, so a launch answer queued after it lands only once they are.
 * A party's frames each pay exactly one logic cycle, so a client that is not
 * frame-locked yet (TORIRS_MAX_FRAMES) is locked here (App_FrameLockEngage):
 * the Scripts tab's everyday client needs no launch flag to host a party.
 * Refused without a world (a member, or before log-in), when this process
 * already hosts (the boot knobs, or a second call), or when the port cannot
 * be bound.
 */
static int
lua_drive_party_host(struct lua_State* L)
{
    int const port = PluginDrive_ArgInt(L, 1);
    int const size = PluginDrive_ArgInt(L, 2);
    char detail[256];
    int listener;

#if defined(_WIN32) || defined(__EMSCRIPTEN__) || defined(TORIRS_PLATFORM_WEB)
    (void)port;
    (void)size;
    (void)detail;
    (void)listener;
    return PluginDrive_PushResult(L, DRIVE_UNSUPPORTED,
        "drive.party_host: the party link is not available on this platform");
#else
    if( port < 1 || port > 65535 || size < 2 || size > DRIVE_PARTY_MAX )
        return PluginDrive_PushResult(L, DRIVE_REFUSED, "drive.party_host: port 1..65535, size 2..4");
    if( !g_embed || !ToriRSServer_EmbedWorld(g_embed) )
        return PluginDrive_PushResult(L, DRIVE_REFUSED,
            "drive.party_host: this client hosts no world (a member, or not logged in)");
    listener = ToriRSServer_EmbedPartyListen(port);
    if( listener < 0 )
    {
        snprintf(detail, sizeof(detail), "drive.party_host: cannot listen on 127.0.0.1:%d", port);
        return PluginDrive_PushResult(L, DRIVE_REFUSED, detail);
    }
    if( ToriRSServer_EmbedPartyHostRequest(listener, size) != 0 )
    {
        close(listener);
        return PluginDrive_PushResult(L, DRIVE_REFUSED,
            "drive.party_host: this client already hosts a party");
    }
    if( !App_FrameLocked() )
    {
        App_FrameLockEngage();
        fprintf(stderr, "quest-driver: party_host: this client is frame-locked from here on (one "
            "logic cycle and 20 ms of world clock a frame)\n");
    }
    snprintf(detail, sizeof(detail), "listening on 127.0.0.1:%d for a party of %d; attached at the "
        "next poll", port, size);
    return PluginDrive_PushResult(L, DRIVE_OK, detail);
#endif
}

/*
 * api.drive.botdrive(agent): this client's embedded world runs the bot
 * runner's drive with `agent` (a shell command, as TORIRS_BOTDRIVE_AGENT
 * takes) from its next tick: the agent decides for every player in the world
 * and the clients only draw it. Ends with the Play that asked
 * (ToriRSServer_EmbedBotDriveRelease), so the next Play is the player's own.
 * "ok", "queued" | "ok", "already driving" (TORIRS_BOTDRIVE_AGENT's, under
 * run.py) | "refused" without a world (a member, or before log-in).
 */
static int
lua_drive_botdrive(struct lua_State* L)
{
    char const* agent = PluginDrive_ArgString(L, 1);

    if( !agent[0] )
        return PluginDrive_PushResult(L, DRIVE_REFUSED, "drive.botdrive: the agent command is empty");
    if( !g_embed || !ToriRSServer_EmbedWorld(g_embed) )
        return PluginDrive_PushResult(L, DRIVE_REFUSED,
            "drive.botdrive: this client hosts no world (a member, or not logged in)");
    if( ToriRSServer_EmbedBotDriveRequest(g_embed, agent) != 0 )
        return PluginDrive_PushResult(L, DRIVE_OK, "already driving");
    fprintf(stderr, "quest-driver: botdrive: the world's next tick starts \"%s\"\n", agent);
    return PluginDrive_PushResult(L, DRIVE_OK, "queued");
}

/* api.drive.party(): who this client is in a party -- the Play's party when
 * it has one, else the launch env (a member before its Play), else a party of
 * one. */
static int
lua_drive_party(struct lua_State* L)
{
    char const* seat_text = getenv("TORIRS_LAUNCH_SEAT");
    char const* session = getenv("TORIRS_LAUNCH_SESSION");
    int answers = 0;
    char const* last = ToriRSServer_EmbedMemberMailLast(&answers);

    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, 0, 11);
    if( g_demand_party_size > 1 )
    {
        lua_pushinteger(L, g_demand_party_role);
        lua_setfield(L, -2, "role");
        lua_pushinteger(L, g_demand_party_size);
        lua_setfield(L, -2, "size");
        lua_createtable(L, g_demand_party_size, 0);
        for( int seat = 1; seat <= g_demand_party_size; seat++ )
        {
            lua_pushstring(L, g_demand_party_names[seat - 1]);
            lua_rawseti(L, -2, seat);
        }
        lua_setfield(L, -2, "names");
        lua_pushboolean(L, g_demand_party_launch);
        lua_setfield(L, -2, "launch");
    }
    else
    {
        lua_pushinteger(L, seat_text && seat_text[0] ? atoi(seat_text) : 1);
        lua_setfield(L, -2, "role");
        lua_pushinteger(L, 1);
        lua_setfield(L, -2, "size");
    }
    lua_pushboolean(L, session && session[0]);
    lua_setfield(L, -2, "launched");
    lua_pushstring(L, session ? session : "");
    lua_setfield(L, -2, "session");
    /* The session this client's own driver opened (a launching leader's
     * Play), for the Scripts tab's PARTY block: "" until one is open. */
    lua_pushstring(L, g_launch_own_session);
    lua_setfield(L, -2, "launch_session");
    lua_pushstring(L, g_launch_own_token);
    lua_setfield(L, -2, "launch_token");
    lua_pushboolean(L, App_FrameLocked());
    lua_setfield(L, -2, "frame_locked");
    lua_pushinteger(L, answers);
    lua_setfield(L, -2, "mail_answers");
    lua_pushstring(L, last);
    lua_setfield(L, -2, "mail");
    return 2;
}

/* api.drive.quit(code): end this client cleanly at the end of this frame
 * (main.c's PluginDrive_Finished branch). A script still running gets the
 * run.unfinished row a stop writes. */
static int
lua_drive_quit(struct lua_State* L)
{
    int const code = PluginDrive_ArgOptInt(L, 1, 0);

    if( g_started && !g_finished )
    {
        drive_ledger_write("run.unfinished", "FAIL", 0, "",
            "run ended without finishing: the client was told to quit (api.drive.quit)");
        drive_ledger_write_summary_exit("none");
        g_finished = 1;
    }
    g_quit_requested = 1;
    g_quit_code = code;
    fprintf(stderr, "quest-driver: quit (exit %d)\n", code);
    return PluginDrive_PushResult(L, DRIVE_OK, "the client exits at the end of this frame");
}

/* ---- a Play from JSON: TORIRS_DRIVE_AUTOPLAY and a `play {json}` command ----
 *
 * The launch service hands a member its Play as JSON (the leader's raid.lua
 * QD.launch._json writes it): objects, arrays, strings, integers, booleans.
 * Parsed straight into the Lua table api.drive.play takes. */

static void
drive_json_space(char const** cursor)
{
    while( **cursor == ' ' || **cursor == '\t' || **cursor == '\n' || **cursor == '\r' )
        (*cursor)++;
}

/* One JSON value at *cursor pushed onto L; 0 on a malformed text (nothing
 * pushed by the failing level). */
static int
drive_json_value(struct lua_State* L, char const** cursor, int depth)
{
    char const* at;

    if( depth > 8 )
        return 0;
    drive_json_space(cursor);
    at = *cursor;
    if( *at == '"' )
    {
        char text[2048];
        int length = 0;
        at++;
        while( *at && *at != '"' )
        {
            char c = *at++;
            if( c == '\\' )
            {
                c = *at++;
                if( c == 'n' )
                    c = '\n';
                else if( c == 't' )
                    c = '\t';
                else if( c == 'u' )
                {
                    /* Only ASCII escapes are written by the leader. */
                    char hex[5] = { 0 };
                    for( int k = 0; k < 4 && *at; k++ )
                        hex[k] = *at++;
                    c = (char)strtol(hex, NULL, 16);
                }
                else if( c == '\0' )
                    return 0;
            }
            if( length + 1 >= (int)sizeof(text) )
                return 0;
            text[length++] = c;
        }
        if( *at != '"' )
            return 0;
        lua_pushlstring(L, text, (size_t)length);
        *cursor = at + 1;
        return 1;
    }
    if( *at == '{' || *at == '[' )
    {
        int const is_object = *at == '{';
        int index = 1;
        *cursor = at + 1;
        lua_newtable(L);
        drive_json_space(cursor);
        if( **cursor == (is_object ? '}' : ']') )
        {
            (*cursor)++;
            return 1;
        }
        for( ;; )
        {
            if( is_object )
            {
                drive_json_space(cursor);
                if( **cursor != '"' || !drive_json_value(L, cursor, depth + 1) )
                {
                    lua_pop(L, 1);
                    return 0;
                }
                drive_json_space(cursor);
                if( **cursor != ':' )
                {
                    lua_pop(L, 2);
                    return 0;
                }
                (*cursor)++;
                if( !drive_json_value(L, cursor, depth + 1) )
                {
                    lua_pop(L, 2);
                    return 0;
                }
                lua_settable(L, -3);
            }
            else
            {
                if( !drive_json_value(L, cursor, depth + 1) )
                {
                    lua_pop(L, 1);
                    return 0;
                }
                lua_rawseti(L, -2, index++);
            }
            drive_json_space(cursor);
            if( **cursor == ',' )
            {
                (*cursor)++;
                continue;
            }
            if( **cursor == (is_object ? '}' : ']') )
            {
                (*cursor)++;
                return 1;
            }
            lua_pop(L, 1);
            return 0;
        }
    }
    if( strncmp(at, "true", 4) == 0 || strncmp(at, "false", 5) == 0 )
    {
        lua_pushboolean(L, at[0] == 't');
        *cursor = at + (at[0] == 't' ? 4 : 5);
        return 1;
    }
    if( strncmp(at, "null", 4) == 0 )
    {
        lua_pushnil(L);
        *cursor = at + 4;
        return 1;
    }
    if( *at == '-' || (*at >= '0' && *at <= '9') )
    {
        char* end = NULL;
        long long const number = strtoll(at, &end, 10);
        if( end == at )
            return 0;
        lua_pushinteger(L, (lua_Integer)number);
        *cursor = end;
        return 1;
    }
    return 0;
}

/* Issue api.drive.play with the Play the JSON names, on a stack of its own.
 * Returns 1 if the Play was accepted; `why` says what happened. */
static int
drive_play_json(struct lua_State* L, char const* json, char const* why)
{
    struct lua_State* thread;
    char const* cursor = json;
    int accepted;

    assert(L);
    assert(json);
    assert(why);
    thread = lua_newthread(L);
    if( !drive_json_value(thread, &cursor, 0) || !lua_istable(thread, -1) )
    {
        fprintf(stderr, "quest-driver: %s: not a JSON object, no Play: %.200s\n", why, json);
        lua_pop(L, 1);
        return 0;
    }
    lua_drive_play(thread);
    accepted = strcmp(lua_tostring(thread, -2), DriveResultName(DRIVE_OK)) == 0;
    fprintf(stderr, "quest-driver: %s: play %s: %s\n", why, accepted ? "accepted" : "refused",
        lua_tostring(thread, -1) ? lua_tostring(thread, -1) : "");
    lua_pop(L, 1);
    return accepted;
}

/* A launched member's side of the channel, once a pump: the AUTOPLAY on the
 * first pump its world is ready for, the commands its leader queued, and its
 * status for the next mail (torirs_server_embed.h 'M'). */
static int g_autoplay_done;

static void
drive_launch_member_pump(struct lua_State* L)
{
    char command[TORIRSSERVER_EMBED_LINK_MAIL_MAX];
    char status[1024];
    char const* autoplay = getenv("TORIRS_DRIVE_AUTOPLAY");
    char const* launched = getenv("TORIRS_LAUNCH_SESSION");
    struct DriveSkillSnapshot hitpoints;
    int have_hitpoints;

    if( !g_autoplay_done && autoplay && autoplay[0] && PluginDrive_OnDemand() && drive_world_ready() &&
        g_demand_state != DRIVE_DEMAND_RUNNING && !g_demand_release_pending )
    {
        g_autoplay_done = 1;
        (void)drive_play_json(L, autoplay, "TORIRS_DRIVE_AUTOPLAY");
    }
    if( !launched || !launched[0] )
        return;
    while( ToriRSServer_EmbedMemberCommandTake(command, (int)sizeof(command)) )
    {
        fprintf(stderr, "quest-driver: launch: the leader says: %.200s\n", command);
        if( strcmp(command, "quit") == 0 )
        {
            g_quit_requested = 1;
            g_quit_code = 0;
        }
        else if( strcmp(command, "stop") == 0 )
        {
            if( g_demand_state == DRIVE_DEMAND_RUNNING && !g_demand_release_pending )
                g_demand_stop_pending = 1;
        }
        else if( strncmp(command, "cheat ", 6) == 0 )
            (void)DriveCore_Cheat(g_app, command + 6);
        else if( strncmp(command, "play ", 5) == 0 )
            (void)drive_play_json(L, command + 5, "launch command");
    }
    have_hitpoints = DriveState_Skill(g_app, 3, &hitpoints) == DRIVE_OK;
    snprintf(status, sizeof(status),
        "state=%s step=%s verdict=%s rows=%d pass=%d fail=%d account=%s hitpoints=%d alive=%d",
        drive_status_state(), g_last_row_step[0] ? g_last_row_step : "-",
        g_last_row_verdict[0] ? g_last_row_verdict : "-", g_ledger_index, g_ledger_pass, g_ledger_fail,
        g_demand_account[0] ? g_demand_account : "-", have_hitpoints ? hitpoints.level : -1,
        have_hitpoints ? hitpoints.level > 0 : 0);
    ToriRSServer_EmbedMemberStatusPost(status);
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

/* ------------------------------------------------------- own swing watch
 *
 * Raid seam48 member_swings_seen.  What a person at the screen sees of
 * their OWN character: the action sequence it starts playing, and when.  A
 * party member has no tick log (the world's one log lives with the leader),
 * so before this the play library COUNTED a phantom swing every
 * weapon-speed ticks of engagement and never re-pressed after the server
 * stopped its swings (seam42 _play_bloat: p3 in reach with no input for 20
 * ticks of a down, twice).
 *
 * Watched once a frame from lua_drive_pump, off the local player's DRAWN
 * action track (animation.primary): a start is the track taking a new seq,
 * or the same seq going back to an earlier frame without its loop counter
 * rising (world_apply_primary_animation's RestartMode.RESET re-send, or the
 * seq ending and being sent again between two pumps).  It is the draw, not
 * the wire: a seq refused by a higher-priority incumbent, or re-sent while
 * still playing under RestartMode.RESETLOOP, shows no start -- exactly what
 * the person sees.  The start cycle is the detecting cycle less the cycles
 * the track already ran in its first frame; a start found past frame 0 (a
 * pump stalled longer than a frame) is stamped at detection.  Reset only by
 * a new process: the reader keys on `starts`, never on an absolute. */
#define DRIVE_OWN_ANIMATION_HISTORY 8

struct DriveOwnAnimationStart
{
    int seq;         /**< the action seq that started */
    int start_cycle; /**< world cycle it started on */
};

struct DriveOwnAnimation
{
    /** The newest DRIVE_OWN_ANIMATION_HISTORY starts, a ring indexed by
     *  start number: a swing and the block seq a hit plays over it inside
     *  one tick are both read by a script that reads once a tick. */
    struct DriveOwnAnimationStart history[DRIVE_OWN_ANIMATION_HISTORY];
    int starts;      /**< how many starts this process has seen */
    int drawn;       /**< the track's seq at the last watch; -1 none */
    int frame;       /**< the track's frame at the last watch */
    int cycle;       /**< the track's per-frame accumulator at the last watch */
    int loop;        /**< the track's loop counter at the last watch */
};

static struct DriveOwnAnimation g_own_animation = { .drawn = -1 };

/* The local player's entity-pool index, the way drive_pointer_local_player
 * finds it: the entity sync's slot for local_pid (2047 before the server
 * named one).  -1 when this client has not placed itself yet. */
static int
drive_local_player_index(void)
{
    int local_idx = -1;

    assert(g_app);
    if( !RS_EntitySync_FindPlayer(&g_app->esync,
            g_app->esync.local_pid >= 0 ? g_app->esync.local_pid : 2047, &local_idx, NULL) )
        return -1;
    return local_idx;
}

static void
drive_own_animation_watch(void)
{
    struct WorldEntity_Player const* player;
    struct WorldEntityFacet_AnimationStep const* track;
    int idx;
    int drawn;
    int restarted;

    assert(g_app);
    if( !g_app->world )
        return; /* before login: nothing is drawn yet */
    idx = drive_local_player_index();
    if( idx < 0 )
        return;
    player = World_EntityPoolGet(&g_app->world->entities.player, idx);
    if( !player )
        return;
    track = &player->animation.primary;
    /* world_cycle.c's anim_step_active: 0xFFFF and 0 both mean "no action
     * track" (a fresh entity's zeroed track is 0). */
    drawn = (track->anim_id == (uint16_t)-1 || track->anim_id == 0) ? -1 : (int)track->anim_id;
    restarted = drawn >= 0 && drawn == g_own_animation.drawn && track->loop <= g_own_animation.loop &&
                (track->frame < g_own_animation.frame ||
                    (track->frame == g_own_animation.frame && track->cycle < g_own_animation.cycle));
    if( drawn >= 0 && (drawn != g_own_animation.drawn || restarted) )
    {
        struct DriveOwnAnimationStart* start =
            &g_own_animation.history[g_own_animation.starts % DRIVE_OWN_ANIMATION_HISTORY];
        start->seq = drawn;
        start->start_cycle = g_app->world->cycle - (track->frame == 0 ? (int)track->cycle : 0);
        g_own_animation.starts++;
    }
    g_own_animation.drawn = drawn;
    g_own_animation.frame = track->frame;
    g_own_animation.cycle = track->cycle;
    g_own_animation.loop = track->loop;
}

/* The `me` row's own-animation fields: seq and seq_tick (the newest start,
 * -1 before the first; the tick in api_drive.tick's client ticks),
 * seq_starts (how many starts this process has seen) and seq_history (the
 * newest starts, oldest first, as {n, seq, tick, age}; n counts from 1, age
 * is the world cycles since the start -- the reader puts a start on its own
 * tick axis by age, not by flooring two cycles that straddle a boundary). */
static void
drive_push_own_animation(struct lua_State* L)
{
    int first;
    int k;
    int row = 0;

    assert(L);
    assert(g_app);
    assert(g_app->world);
    if( g_own_animation.starts > 0 )
    {
        struct DriveOwnAnimationStart const* newest =
            &g_own_animation.history[(g_own_animation.starts - 1) % DRIVE_OWN_ANIMATION_HISTORY];
        lua_pushinteger(L, newest->seq);
        lua_setfield(L, -2, "seq");
        lua_pushinteger(L, (lua_Integer)(newest->start_cycle / APP_SERVER_TICK_LOGIC_CYCLES));
        lua_setfield(L, -2, "seq_tick");
    }
    else
    {
        lua_pushinteger(L, -1);
        lua_setfield(L, -2, "seq");
        lua_pushinteger(L, -1);
        lua_setfield(L, -2, "seq_tick");
    }
    lua_pushinteger(L, g_own_animation.starts);
    lua_setfield(L, -2, "seq_starts");
    first = g_own_animation.starts - DRIVE_OWN_ANIMATION_HISTORY;
    if( first < 0 )
        first = 0;
    lua_createtable(L, g_own_animation.starts - first, 0);
    for( k = first; k < g_own_animation.starts; k++ )
    {
        struct DriveOwnAnimationStart const* start =
            &g_own_animation.history[k % DRIVE_OWN_ANIMATION_HISTORY];
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, k + 1);
        lua_setfield(L, -2, "n");
        lua_pushinteger(L, start->seq);
        lua_setfield(L, -2, "seq");
        lua_pushinteger(L, (lua_Integer)(start->start_cycle / APP_SERVER_TICK_LOGIC_CYCLES));
        lua_setfield(L, -2, "tick");
        lua_pushinteger(L, (lua_Integer)(g_app->world->cycle - start->start_cycle));
        lua_setfield(L, -2, "age");
        lua_rawseti(L, -2, ++row);
    }
    lua_setfield(L, -2, "seq_history");
}

/* api_drive.players() -> result, rows: every player in THIS client's entity
 * pool -- the raiders it can see -- as {name, x, z, level, pid, me, anim}, the
 * `me` row also {seq, seq_tick, seq_starts, seq_history} (raid seam48,
 * drive_push_own_animation). The tile
 * is the world tile (scene base + grid), `me` marks the local player. A read
 * of what the client was sent, so it works on a party member as on the
 * leader; t.party.players (raid.lua) filters it by radius. */
static int
lua_drive_players(struct lua_State* L)
{
    struct World_EntityPool* pool;
    int i;
    int n = 0;
    int local_idx;

    assert(g_app);
    if( !g_app->world )
        return PluginDrive_PushResult(L, DRIVE_NOT_FOUND, NULL);
    local_idx = drive_local_player_index();
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
        /* server_x/server_z: the server's whole tile (route[0] while a route
         * is in flight, the tile the player will stand on at the tick's end),
         * where x/z is the stepping tile the figure is drawn on. Scriptrun's
         * rows carry the same field, so a planner reads one name on both
         * lanes (Verzik P3 live: raiders and tornadoes read a tile behind). */
        lua_pushinteger(L, g_app->world->_base_tile_x +
                               (player->pathing.route_length > 0 ? player->pathing.route_x[0]
                                                                  : player->grid_position.x));
        lua_setfield(L, -2, "server_x");
        lua_pushinteger(L, g_app->world->_base_tile_z +
                               (player->pathing.route_length > 0 ? player->pathing.route_z[0]
                                                                  : player->grid_position.z));
        lua_setfield(L, -2, "server_z");
        lua_pushinteger(L, player->grid_position.level);
        lua_setfield(L, -2, "level");
        lua_pushinteger(L, player->server_pid);
        lua_setfield(L, -2, "pid");
        lua_pushboolean(L, i == local_idx);
        lua_setfield(L, -2, "me");
        /* Raid seam48: the action seq this player's model is drawing (-1
         * none), and on the `me` row what its own screen saw it START
         * (g_own_animation): seq, the client tick it started on and how many
         * starts this process has seen (a reader keys a new swing on it). */
        lua_pushinteger(L, (player->animation.primary.anim_id == (uint16_t)-1 ||
                               player->animation.primary.anim_id == 0)
                               ? -1
                               : (lua_Integer)player->animation.primary.anim_id);
        lua_setfield(L, -2, "anim");
        if( i == local_idx )
            drive_push_own_animation(L);
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
    {"launch_open", lua_drive_launch_open},
    {"launch_spawn", lua_drive_launch_spawn},
    {"launch_command", lua_drive_launch_command},
    {"launch_status", lua_drive_launch_status},
    {"launch_close", lua_drive_launch_close},
    {"launch_answer", lua_drive_launch_answer},
    {"party_host", lua_drive_party_host},
    {"botdrive", lua_drive_botdrive},
    {"party", lua_drive_party},
    {"quit", lua_drive_quit},
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
    /* One flat `api.drive`, assembled from nine files. Order is registration
     * order only; the names are disjoint and the inventory test proves it. */
    lua_newtable(L);
    PluginDriveCore_RegisterLua(L, script);
    PluginDriveState_RegisterLua(L, script);
    PluginDriveChat_RegisterLua(L, script);
    PluginDriveRead_RegisterLua(L, script);
    PluginDrivePointer_RegisterLua(L, script);
    PluginDriveUi_RegisterLua(L, script);
    PluginDriveTicklog_RegisterLua(L, script);
    PluginDriveLos_RegisterLua(L, script);
    PluginDriveRecord_RegisterLua(L, script);
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
