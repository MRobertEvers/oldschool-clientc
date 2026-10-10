/*
 * --scriptrun: quest-driver test scripts run INSIDE the server, at CPU speed,
 * by sessionless bots that perceive only what a client would be sent.
 *
 *   torirsserver --scriptrun <test.lua> [--bots N] [--name NAME] [--ticks N]
 *                [--script-root DIR] [--session DIR] [--quiet]
 *
 * THE BOT. One sessionless player per seat (as the bot runner makes them,
 * torirs_server_botrun.c), each with
 *   - a perceived world (scriptrun/scriptrun_core.c): every payload the server
 *     addresses to it (ToriRSServer.packet_sink) decoded with the client's own
 *     parser and entity decoders into the client's own world model;
 *   - a Lua state running the quest driver's parts -- the very list the client
 *     plugin loads (plugin/torirs_plugin_drive_parts.h) -- with api.drive bound
 *     here: reads answer from the perceived world, inputs become the packets a
 *     client sends and go through ToriRSServer_WorldHandle, the one door every
 *     client packet takes.
 * Nothing a bot reads comes from server state: a verb whose only source would
 * be the server answers "unsupported", as it does on a client with no embedded
 * server. Screen verbs (pointer, menus, camera, shots) answer "unsupported".
 * The server's own tick log is the exception, as it is on the client: the
 * raid harness grades from it (t.ticklog), it never feeds a decision.
 *
 * THE CLOCK. No delay between ticks. Each tick: every bot's await is
 * re-tested and its script resumed, inputs land through the packet door, then
 * the world ticks and its packets reach the bots. A script acting on tick n's
 * packets reaches the server on tick n + 1, as a client's click would.
 */

#include "torirs_server.h"
#include "torirs_server_boot.h"
#include "torirs_server_content.h"
#include "torirs_server_wire.h"
#include "torirs_server_scene.h"
#include "torirs_server_save.h"
#include "engine/world_builder/collision_map.h"
#include "features/features.h"
#include <rscache.h>

#include "game/rs_entity_sync.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "plugin/torirs_drive_pick.h"
#include "plugin/torirs_plugin_drive_parts.h"
#include "scriptrun/scriptrun_core.h"
#include "world/entity_npc.h"
#include "world/entity_player.h"
#include "world/world.h"

/* Through the repo root (see the Makefile note on --scriptrun). */
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "plugin/torirs_drive_plan_lua.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>

enum
{
    SCRIPTRUN_BOTS_MAX = 8,
    SCRIPTRUN_BARRIERS = 256,
};

enum ScriptBotState
{
    SCRIPTBOT_START = 0,
    SCRIPTBOT_AWAIT,
    SCRIPTBOT_DONE,
};

struct ScriptRun;

/* One input packet waiting for the tick boundary. */
struct ScriptrunInput
{
    int name;
    int len;
    uint8_t payload[256];
};

enum
{
    SCRIPTRUN_INPUTS = 64,
    /* The side panel a fresh login shows (the stone order in TAB_NAMES). */
    SCRIPTRUN_TAB_INVENTORY = 3,
    SCRIPTRUN_TAB_EQUIPMENT = 4,
    SCRIPTRUN_TAB_PRAYER = 5,
    SCRIPTRUN_TAB_MAGIC = 6,
};

struct ScriptBot
{
    struct ScriptRun* run;
    int index;
    char name[32];
    struct ToriRSServerPlayer* player;
    struct ScriptrunCore* core;
    lua_State* L;
    lua_State* co;
    int co_ref;
    enum ScriptBotState state;
    int await_level_ref;
    int await_kind;      /* an event kind, or -1 for any (with a match) */
    /* A resume sent this step, stamped once the coroutine yields: the
     * client's flush raises resume_answered after the verb has armed its
     * await (app_cs2_flush.c), never inside the verb. */
    int resume_stamp_pending;
    int resume_stamp_component;
    /* Inputs sent this step, handed to the server at the next tick (a
     * client's packets reach the server only at a boundary, and what the
     * server says back arrives only after that tick). */
    struct ScriptrunInput inputs[SCRIPTRUN_INPUTS];
    int input_count;
    /* The side panel showing: client-local, set by tab(); a panel that is
     * not showing is display-hidden to the client's dispatcher. */
    int tab;
    int await_match_ref; /* LUA_NOREF, or a 1-arg predicate over the event */
    /* The last event serial the pump has read: the client's g_event_cursor
     * (torirs_plugin_drive.c). One cursor for the bot, never one per await:
     * arming an await does not move it (lua_drive_await). */
    int event_cursor;
    int await_deadline;
    char await_note[128];
    int finished;
    int exit_code;
    int rows, pass, fail;
    int errored;
};

struct ScriptRun
{
    struct ToriRSServer* srv;
    struct GameProtoRevTable const* rev;
    struct ScriptBot bots[SCRIPTRUN_BOTS_MAX];
    int count;
    char const* test_path;
    char const* script_root;
    char const* session_dir;
    /* --fixture: the save each seat logs in on ({seat} = 1-based seat). */
    char const* fixture;
    char* test_source;
    char* barriers[SCRIPTRUN_BARRIERS];
    int barrier_ticks[SCRIPTRUN_BARRIERS];
    int barrier_count;
    int quiet;
    FILE* ledger;
    struct RSCache_Dat2Disk* cache_disk;
    int check_collision;
    int checked_zone_x[SCRIPTRUN_BOTS_MAX], checked_zone_z[SCRIPTRUN_BOTS_MAX];
    int check_due[SCRIPTRUN_BOTS_MAX]; /* tick a scene's check runs, 0 none */
};

/* ------------------------------------------------------------- helpers */

static char*
read_file(
    char const* path,
    long* out_len)
{
    FILE* f = fopen(path, "rb");
    long len;
    char* buf;
    size_t got;

    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)len + 1);
    assert(buf);
    got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    if( out_len )
        *out_len = (long)got;
    return buf;
}

static void
put2(uint8_t* out, int v)
{
    out[0] = (uint8_t)((v >> 8) & 0xff);
    out[1] = (uint8_t)(v & 0xff);
}

static void
put4(uint8_t* out, int v)
{
    out[0] = (uint8_t)((v >> 24) & 0xff);
    out[1] = (uint8_t)((v >> 16) & 0xff);
    out[2] = (uint8_t)((v >> 8) & 0xff);
    out[3] = (uint8_t)(v & 0xff);
}

static int64_t
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* The client answers a scene -- the login's and every rebuild's -- with
 * MAP_BUILD_COMPLETE once it has built it; the bot's scene is its perceived
 * world, built the moment the REBUILD is decoded, so it answers at once. The
 * login burst (ToriRSServer_WorldLoginFinish) waits on this. */
static void
scene_ack(struct ToriRSServerPlayer* player)
{
    if( player->login_scene_pending || player->rebuild_scene_pending )
    {
        player->world->active_player = player;
        ToriRSServer_WorldHandle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
    }
}

static struct ScriptBot*
bot_of(lua_State* L)
{
    struct ScriptBot* bot = (struct ScriptBot*)lua_touserdata(L, lua_upvalueindex(1));
    assert(bot);
    return bot;
}

static int
push_result(
    lua_State* L,
    char const* result,
    char const* detail)
{
    lua_pushstring(L, result);
    if( detail )
        lua_pushstring(L, detail);
    else
        lua_pushnil(L);
    return 2;
}

static int
panel_showing(
    struct ScriptBot const* bot,
    int component);

/* Queue one input for the next tick boundary. */
static void
handle(
    struct ScriptBot* bot,
    int name,
    uint8_t const* payload,
    int len)
{
    struct ScriptrunInput* in;

    assert(len >= 0);
    assert(len <= (int)sizeof(in->payload));
    assert(bot->input_count < SCRIPTRUN_INPUTS);
    in = &bot->inputs[bot->input_count++];
    in->name = name;
    in->len = len;
    if( len > 0 )
        memcpy(in->payload, payload, (size_t)len);
}

/* The boundary: every bot's queued inputs, in send order, into the server
 * just before its tick runs (where a client's packets land). The answers go
 * out through the sink now, but no bot steps again until the tick is over. */
static void
flush_inputs(struct ScriptBot* bot)
{
    for( int i = 0; i < bot->input_count; i++ )
    {
        struct ScriptrunInput const* in = &bot->inputs[i];
        scene_ack(bot->player);
        bot->run->srv->active_player = bot->player;
        ToriRSServer_WorldHandle(bot->player, in->name, in->len > 0 ? in->payload : NULL, in->len);
    }
    bot->input_count = 0;
}

static int
cheb(int ax, int az, int bx, int bz)
{
    int dx = ax > bx ? ax - bx : bx - ax;
    int dz = az > bz ? az - bz : bz - az;
    return dx > dz ? dx : dz;
}

/* -------------------------------------------------- scheduler verbs */

static int
d_tick(lua_State* L)
{
    lua_pushinteger(L, bot_of(L)->core->tick);
    return 1;
}

static int
d_server_tick(lua_State* L)
{
    lua_pushstring(L, "ok");
    lua_pushinteger(L, bot_of(L)->core->tick);
    return 2;
}

static int
d_settled(lua_State* L)
{
    lua_pushboolean(L, 1);
    return 1;
}

/* app/app_plugin_drive_events.c's names, in enum ScriptrunEventKind order. */
static char const* const EVENT_NAMES[SCRIPTRUN_EV_KIND_COUNT] = {
    "none", "sub_opened", "sub_mounted", "sub_closed", "chat_opened", "slot_mounted",
    "resume_answered", "varp_changed", "inv_changed", "obj_added", "obj_removed", "map_flag",
    "chat_message", "server_tick", "npc_spawn", "npc_despawn", "npc_retype", "inv_packet",
    "npc_seq", "npc_face",
};

static int
event_kind(char const* name)
{
    for( int k = 1; k < SCRIPTRUN_EV_KIND_COUNT; k++ )
        if( strcmp(EVENT_NAMES[k], name) == 0 )
            return k;
    return -1;
}

static void
push_event(
    lua_State* L,
    struct ScriptrunEvent const* ev)
{
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, ev->serial);
    lua_setfield(L, -2, "serial");
    lua_pushstring(L, EVENT_NAMES[ev->kind]);
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

/* TORIRS_DRIVE_AWAIT_TRACE=1: the client's await trace line for line
 * (torirs_plugin_drive.c drive_await_trace), prefixed with the bot. */
static void
await_trace(
    struct ScriptBot const* bot,
    char const* how,
    char const* note)
{
    static int on = -1;
    if( on < 0 )
    {
        char const* e = getenv("TORIRS_DRIVE_AWAIT_TRACE");
        on = e && e[0] && e[0] != '0';
    }
    if( on )
        fprintf(stderr, "[%s] await-trace t%d %s '%s'\n", bot->name, bot->core->tick, how, note ? note : "");
}

/* await(descriptor, deadline): a `level` predicate re-tested each tick, and/or
 * an `event` kind whose events since the call are offered to `match`, exactly
 * as the client's scheduler does (plugin/torirs_plugin_drive.c). Deadlines are
 * server ticks. */
static int
d_await(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int deadline;
    int kind = -1; /* the client's: no `event` offers every kind to `match` */
    int has_match;
    int level_ref = LUA_NOREF;
    int match_ref = LUA_NOREF;
    char note[128] = "await";

    luaL_checktype(L, 1, LUA_TTABLE);
    deadline = (int)luaL_optinteger(L, 2, 0);
    lua_getfield(L, 1, "note");
    if( lua_isstring(L, -1) )
        snprintf(note, sizeof(note), "%s", lua_tostring(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, 1, "event");
    if( lua_isstring(L, -1) )
    {
        kind = event_kind(lua_tostring(L, -1));
        if( kind < 0 )
        {
            char detail[160];
            snprintf(detail, sizeof(detail), "scriptrun: no '%s' event here", lua_tostring(L, -1));
            lua_pop(L, 1);
            return push_result(L, "unsupported", detail);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "match");
    has_match = lua_isfunction(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "level");
    if( lua_isfunction(L, -1) )
    {
        lua_pushvalue(L, -1);
        if( lua_pcall(L, 0, 1, 0) != LUA_OK )
            return lua_error(L);
        if( lua_toboolean(L, -1) )
        {
            lua_pop(L, 2);
            await_trace(bot, "level-now", note);
            return push_result(L, "ok", NULL);
        }
        lua_pop(L, 1);
        if( deadline > 0 && lua_isyieldable(L) )
            level_ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pops the level fn */
        else
            lua_pop(L, 1);
    }
    else
    {
        lua_pop(L, 1);
        if( !has_match )
            return luaL_error(L, "drive.await '%s': descriptor needs a level or a match predicate", note);
    }
    if( deadline <= 0 )
    {
        if( level_ref != LUA_NOREF )
            luaL_unref(L, LUA_REGISTRYINDEX, level_ref);
        return push_result(L, "timeout", note);
    }
    if( !lua_isyieldable(L) )
    {
        if( level_ref != LUA_NOREF )
            luaL_unref(L, LUA_REGISTRYINDEX, level_ref);
        return luaL_error(L, "drive.await '%s' would suspend inside an await predicate", note);
    }
    if( has_match )
    {
        lua_getfield(L, 1, "match");
        match_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    bot->await_level_ref = level_ref;
    bot->await_match_ref = match_ref;
    bot->await_kind = kind;
    bot->await_deadline = bot->core->tick + deadline;
    snprintf(bot->await_note, sizeof(bot->await_note), "%s", note);
    bot->state = SCRIPTBOT_AWAIT;
    await_trace(bot, "arm", note);
    return lua_yield(L, 0);
}

static int
d_report(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    if( !bot->run->quiet )
        fprintf(stderr, "[%s t%d] %s\n", bot->name, bot->core->tick, luaL_optstring(L, 1, ""));
    return 0;
}

static int
d_ledger(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    char const* step;
    char const* verdict;
    char const* detail;
    char row[32];

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "step");
    lua_getfield(L, 1, "verdict");
    lua_getfield(L, 1, "detail");
    step = lua_tostring(L, -3);
    verdict = lua_tostring(L, -2);
    detail = lua_tostring(L, -1);
    bot->rows++;
    if( verdict && strcmp(verdict, "PASS") == 0 )
        bot->pass++;
    else if( verdict && strcmp(verdict, "FAIL") == 0 )
        bot->fail++;
    fprintf(stdout, "[%s t%d] %-6s %s  %s\n", bot->name, bot->core->tick, verdict ? verdict : "?",
            step ? step : "?", detail ? detail : "");
    fflush(stdout);
    if( bot->run->ledger )
    {
        fprintf(bot->run->ledger, "%s\t%d\t%s\t%s\t%s\n", bot->name, bot->core->tick,
                step ? step : "", verdict ? verdict : "", detail ? detail : "");
        fflush(bot->run->ledger);
    }
    lua_pop(L, 3);
    snprintf(row, sizeof(row), "row %d", bot->rows);
    return push_result(L, "ok", row);
}

static int
d_finish(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    bot->finished = 1;
    bot->exit_code = (int)luaL_optinteger(L, 1, 0);
    return push_result(L, "ok", "finish");
}

static int
d_session(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    lua_newtable(L);
    lua_pushstring(L, bot->run->session_dir ? bot->run->session_dir : ".");
    lua_setfield(L, -2, "dir");
    lua_pushstring(L, bot->run->test_path);
    lua_setfield(L, -2, "script");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "on_demand");
    return 1;
}

static int
d_status(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    lua_pushstring(L, "ok");
    lua_newtable(L);
    lua_pushstring(L, bot->finished ? "finished" : "running");
    lua_setfield(L, -2, "state");
    lua_pushinteger(L, bot->rows);
    lua_setfield(L, -2, "rows");
    lua_pushinteger(L, bot->pass);
    lua_setfield(L, -2, "pass");
    lua_pushinteger(L, bot->fail);
    lua_setfield(L, -2, "fail");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "on_demand");
    return 2;
}

/* ------------------------------------------------------------ symbols */

static int
pack_kind(char const* kind)
{
    static struct
    {
        char const* name;
        int kind;
    } const KINDS[] = {
        { "npc", TORIRSSERVER_PACK_NPC },
        { "obj", TORIRSSERVER_PACK_OBJ },
        { "loc", TORIRSSERVER_PACK_LOC },
        { "seq", TORIRSSERVER_PACK_SEQ },
        { "spotanim", TORIRSSERVER_PACK_SPOTANIM },
        { "inv", TORIRSSERVER_PACK_INV },
        { "varp", TORIRSSERVER_PACK_VARP },
        { "varbit", TORIRSSERVER_PACK_VARBIT },
        { "interface", TORIRSSERVER_PACK_INTERFACE },
        { "component", TORIRSSERVER_PACK_COMPONENT },
        { "stat", TORIRSSERVER_PACK_STAT },
    };
    for( size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); i++ )
        if( strcmp(KINDS[i].name, kind) == 0 )
            return KINDS[i].kind;
    return -1;
}

static int
d_symbol(lua_State* L)
{
    char const* kind = luaL_checkstring(L, 1);
    char const* name = luaL_checkstring(L, 2);
    int pk = pack_kind(kind);
    int id;

    if( pk < 0 )
        return luaL_error(L, "drive.symbol: unknown kind '%s'", kind);
    id = ToriRSServer_ContentSymbol((enum ToriRSServerPackKind)pk, name);
    if( id < 0 )
    {
        lua_pushstring(L, "not_found");
        lua_pushnil(L);
        return 2;
    }
    lua_pushstring(L, "ok");
    lua_pushinteger(L, id);
    return 2;
}

static int
d_symbol_name(lua_State* L)
{
    char const* kind = luaL_checkstring(L, 1);
    int id = (int)luaL_checkinteger(L, 2);
    int pk = pack_kind(kind);
    char const* name;

    if( pk < 0 )
        return luaL_error(L, "drive.symbol_name: unknown kind '%s'", kind);
    name = ToriRSServer_ContentSymbolName((enum ToriRSServerPackKind)pk, id);
    if( !name || !name[0] )
    {
        lua_pushstring(L, "not_found");
        lua_pushnil(L);
        return 2;
    }
    lua_pushstring(L, "ok");
    lua_pushstring(L, name);
    return 2;
}

static int
d_component(lua_State* L)
{
    char const* symbol = luaL_checkstring(L, 1);
    int sub = (int)luaL_optinteger(L, 2, -1);
    int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, symbol);

    if( id < 0 )
    {
        lua_pushstring(L, "not_found");
        lua_pushnil(L);
        return 2;
    }
    lua_pushstring(L, "ok");
    /* A slot of an if_setevents component (a store's items, a list's rows)
     * travels in the id's high bits, as the client's child widget carries its
     * own sub: d_if_click sends it, where it used to send 0xffff and the
     * server's last_slot read -1 (the ToB supply chest bought nothing, relay
     * rl7). With no sub the id is the component's, as before. */
    lua_pushinteger(L, sub >= 0 ? ((lua_Integer)id | ((lua_Integer)(sub + 1) << 32)) : (lua_Integer)id);
    return 2;
}

/* --------------------------------------------------------------- me */

static int
d_player_tile(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int x, z, level;

    if( !ScriptrunCore_LocalTile(bot->core, &x, &z, &level) )
        return push_result(L, "not_found", "not placed yet");
    lua_pushstring(L, "ok");
    lua_newtable(L);
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, z);
    lua_setfield(L, -2, "z");
    lua_pushinteger(L, level);
    lua_setfield(L, -2, "level");
    return 2;
}

static struct WorldEntity_Player*
local_player(struct ScriptBot* bot)
{
    int world_idx;
    int pid = bot->core->esync->local_pid >= 0 ? bot->core->esync->local_pid : 2047;

    if( !RS_EntitySync_FindPlayer(bot->core->esync, pid, &world_idx, NULL) )
        return NULL;
    return World_EntityPoolGet(&bot->core->world->entities.player, world_idx);
}

static int
d_player_idle(lua_State* L)
{
    struct WorldEntity_Player* me = local_player(bot_of(L));
    lua_pushstring(L, "ok");
    lua_pushboolean(L, !me || me->pathing.route_length == 0);
    return 2;
}

/* The XP table: the level a client derives from the experience it was sent. */
static int
level_for_xp(int xp_tenths_or_xp)
{
    int xp = xp_tenths_or_xp;
    int points = 0;
    for( int lvl = 1; lvl < 99; lvl++ )
    {
        int threshold;
        points += (int)((double)lvl + 300.0 * pow(2.0, (double)lvl / 7.0));
        threshold = points / 4;
        if( xp < threshold )
            return lvl;
    }
    return 99;
}

static int
d_skill(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int stat = (int)luaL_checkinteger(L, 1);

    if( stat < 0 || stat >= SCRIPTRUN_STATS )
        return push_result(L, "not_found", NULL);
    lua_pushstring(L, "ok");
    lua_newtable(L);
    lua_pushinteger(L, bot->core->stat_level[stat]);
    lua_setfield(L, -2, "level");
    lua_pushinteger(L, level_for_xp(bot->core->stat_xp[stat]));
    lua_setfield(L, -2, "base_level");
    lua_pushinteger(L, bot->core->stat_xp[stat]);
    lua_setfield(L, -2, "experience");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "stated");
    return 2;
}

static int
d_varp(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int id = (int)luaL_checkinteger(L, 1);

    if( id < 0 || id >= SCRIPTRUN_VARPS )
        return push_result(L, "not_found", NULL);
    lua_pushstring(L, "ok");
    lua_pushinteger(L, bot->core->varps[id]);
    return 2;
}

static int
d_varbit(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int id = (int)luaL_checkinteger(L, 1);
    int value;

    if( !ScriptrunCore_Varbit(bot->core, id, &value) )
        return push_result(L, "not_found", NULL);
    lua_pushstring(L, "ok");
    lua_pushinteger(L, value);
    return 2;
}

/* ------------------------------------------------------- containers */

/* The client's container ids are inv ids (93 backpack, 94 worn). */
static int
d_inv_count(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int inv_id = (int)luaL_checkinteger(L, 1);
    int obj_id = (int)luaL_checkinteger(L, 2);
    struct ScriptrunInv const* inv = ScriptrunCore_Inv(bot->core, inv_id);
    int total = 0;

    if( !inv )
        return push_result(L, "not_found", NULL);
    for( int s = 0; s < inv->size; s++ )
        if( inv->obj[s] == obj_id )
            total += inv->count[s];
    lua_pushstring(L, "ok");
    lua_pushinteger(L, total);
    return 2;
}

static int
d_inv_slot(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int inv_id = (int)luaL_checkinteger(L, 1);
    int slot = (int)luaL_checkinteger(L, 2);
    struct ScriptrunInv const* inv = ScriptrunCore_Inv(bot->core, inv_id);

    if( !inv || slot < 0 || slot >= SCRIPTRUN_INV_SLOTS )
        return push_result(L, "not_found", NULL);
    lua_pushstring(L, "ok");
    lua_newtable(L);
    lua_pushinteger(L, inv->obj[slot]);
    lua_setfield(L, -2, "obj_id");
    lua_pushinteger(L, inv->obj[slot] >= 0 ? inv->count[slot] : 0);
    lua_setfield(L, -2, "count");
    return 2;
}

static int
d_inv_capacity(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int inv_id = (int)luaL_checkinteger(L, 1);
    struct ScriptrunInv const* inv = ScriptrunCore_Inv(bot->core, inv_id);

    if( !inv )
        return push_result(L, "not_found", NULL);
    lua_pushstring(L, "ok");
    lua_pushinteger(L, inv->size);
    return 2;
}

/* ------------------------------------------------------------ world */

static void
push_npc_row(
    lua_State* L,
    struct ScriptBot* bot,
    struct WorldEntity_NPC const* npc)
{
    struct World* world = bot->core->world;
    int base_x = world->_base_tile_x;
    int base_z = world->_base_tile_z;
    int cycle = (int)world->cycle;
    /* The authoritative tile: routeX[0] (see scriptrun_core.h on why this
     * reads the route, not the per-cycle draw tile). */
    int tx = base_x + (npc->pathing.route_length > 0 ? npc->pathing.route_x[0]
                                                     : npc->grid_position.x);
    int tz = base_z + (npc->pathing.route_length > 0 ? npc->pathing.route_z[0]
                                                     : npc->grid_position.z);
    /* The newest SEQUENCE op until its length runs out (the client's
     * per-cycle step ends the track there): the same rule the live client's
     * row reads (torirs_plugin_drive_ui.c drive_ui_fill_npc_state). */
    int anim = (npc->seq_sent_id >= 0 && npc->seq_sent_id != 65535) ? npc->seq_sent_id : -1;
    int len;

    if( anim >= 0 )
    {
        len = ToriRSServer_SeqLengthCycles(anim);
        if( len > 0 && cycle - npc->seq_sent_cycle >= len )
            anim = -1;
    }

    lua_newtable(L);
    lua_pushinteger(L, npc->server_slot);
    lua_setfield(L, -2, "slot");
    lua_pushinteger(L, npc->npc_id);
    lua_setfield(L, -2, "npc_id");
    lua_pushinteger(L, npc->base_npc_id);
    lua_setfield(L, -2, "base_npc_id");
    lua_pushinteger(L, tx);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, tz);
    lua_setfield(L, -2, "z");
    lua_pushinteger(L, tx);
    lua_setfield(L, -2, "server_x");
    lua_pushinteger(L, tz);
    lua_setfield(L, -2, "server_z");
    lua_pushinteger(L, npc->grid_position.level);
    lua_setfield(L, -2, "level");
    lua_pushinteger(L, npc->element_id);
    lua_setfield(L, -2, "element_id");
    if( npc->combat.healthbar_type >= 0 )
    {
        int width = ToriRSServer_HealthbarWidth(npc->combat.healthbar_type);
        lua_pushinteger(L, npc->combat.healthbar_end_fill);
        lua_setfield(L, -2, "health_ratio");
        lua_pushinteger(L, width > 0 ? width : 30);
        lua_setfield(L, -2, "health_scale");
        lua_pushboolean(L, npc->combat.healthbar_end_cycle > cycle);
        lua_setfield(L, -2, "health_active");
    }
    else
    {
        lua_pushinteger(L, -1);
        lua_setfield(L, -2, "health_ratio");
        lua_pushinteger(L, -1);
        lua_setfield(L, -2, "health_scale");
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "health_active");
    }
    {
        int hit_damage = -1, hit_cycle = 0;
        for( int i = 0; i < WORLD_ENTITY_DAMAGE_SLOTS; i++ )
        {
            if( npc->combat.damage_start_cycles[i] > cycle || npc->combat.damage_cycles[i] <= cycle )
                continue;
            if( npc->combat.damage_start_cycles[i] < hit_cycle )
                continue;
            hit_cycle = npc->combat.damage_start_cycles[i];
            hit_damage = (int)npc->combat.damage_values[i];
        }
        lua_pushinteger(L, hit_damage);
        lua_setfield(L, -2, "hit_damage");
        lua_pushinteger(L, hit_cycle);
        lua_setfield(L, -2, "hit_cycle");
    }
    lua_pushstring(L, npc->name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, npc->chat.timer > 0 ? npc->chat.message : "");
    lua_setfield(L, -2, "overhead");
    lua_pushinteger(L, npc->chat.timer > 0 ? npc->chat.timer : 0);
    lua_setfield(L, -2, "overhead_timer");
    lua_pushinteger(L, anim);
    lua_setfield(L, -2, "anim_id");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "anim_frame");
    lua_pushinteger(L, npc->spotanim_packet_id);
    lua_setfield(L, -2, "spotanim_id");
    lua_pushinteger(L, npc->seq_sent_id);
    lua_setfield(L, -2, "seq_id");
    lua_pushinteger(L, npc->seq_sent_id >= 0 ? npc->seq_sent_cycle / SCRIPTRUN_CYCLES_PER_TICK : -1);
    lua_setfield(L, -2, "seq_tick");
    lua_pushinteger(L, npc->spotanim_sent_id);
    lua_setfield(L, -2, "spotanim_sent_id");
    lua_pushinteger(L, npc->spotanim_sent_id >= 0
                           ? npc->spotanim_sent_cycle / SCRIPTRUN_CYCLES_PER_TICK
                           : -1);
    lua_setfield(L, -2, "spotanim_tick");
    lua_pushinteger(L, npc->facing.entity_id);
    lua_setfield(L, -2, "facing");
    if( npc->face_sent_x != 0 || npc->face_sent_z != 0 )
    {
        lua_pushinteger(L, npc->face_sent_x >> 1);
        lua_setfield(L, -2, "face_x");
        lua_pushinteger(L, npc->face_sent_z >> 1);
        lua_setfield(L, -2, "face_z");
        lua_pushinteger(L, npc->face_sent_cycle / SCRIPTRUN_CYCLES_PER_TICK);
        lua_setfield(L, -2, "face_tick");
    }
    else
    {
        lua_pushinteger(L, -1);
        lua_setfield(L, -2, "face_x");
        lua_pushinteger(L, -1);
        lua_setfield(L, -2, "face_z");
        lua_pushinteger(L, -1);
        lua_setfield(L, -2, "face_tick");
    }
    lua_pushinteger(L, npc->size > 0 ? npc->size : 1);
    lua_setfield(L, -2, "size");
    lua_pushinteger(L, -1);
    lua_setfield(L, -2, "pose_anim");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "pose_frame");
    lua_pushstring(L, "");
    lua_setfield(L, -2, "pose_kind");
    lua_pushinteger(L, npc->idle_animations.readyanim);
    lua_setfield(L, -2, "ready_anim");
    lua_pushinteger(L, npc->idle_animations.walkanim);
    lua_setfield(L, -2, "walk_anim");
    lua_pushinteger(L, npc->idle_animations.turnanim);
    lua_setfield(L, -2, "turn_anim");
    lua_pushinteger(L, npc->idle_animations.runanim);
    lua_setfield(L, -2, "run_anim");
}

static int
d_npcs(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int radius = (int)luaL_optinteger(L, 1, 0); /* <= 0: all (drive_ui_within_radius) */
    struct World_EntityPool* pool = &bot->core->world->entities.npc;
    int px = 0, pz = 0, plevel = 0;
    int have = ScriptrunCore_LocalTile(bot->core, &px, &pz, &plevel);
    struct
    {
        int idx;
        int dist;
    } found[256];
    int n = 0;

    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC const* npc = World_EntityPoolGet(pool, i);
        int tx, tz, d;
        if( !npc || npc->server_slot < 0 || n >= 256 )
            continue;
        tx = bot->core->world->_base_tile_x +
             (npc->pathing.route_length > 0 ? npc->pathing.route_x[0] : npc->grid_position.x);
        tz = bot->core->world->_base_tile_z +
             (npc->pathing.route_length > 0 ? npc->pathing.route_z[0] : npc->grid_position.z);
        d = have ? cheb(tx, tz, px, pz) : 0;
        if( have && radius > 0 && d > radius )
            continue;
        found[n].idx = i;
        found[n].dist = d;
        n++;
    }
    /* Nearest first (insertion sort: a room holds a handful). */
    for( int a = 1; a < n; a++ )
    {
        int b = a;
        while( b > 0 && found[b - 1].dist > found[b].dist )
        {
            int ti = found[b].idx, td = found[b].dist;
            found[b] = found[b - 1];
            found[b - 1].idx = ti;
            found[b - 1].dist = td;
            b--;
        }
    }
    lua_pushstring(L, "ok");
    lua_createtable(L, n, 0);
    for( int k = 0; k < n; k++ )
    {
        push_npc_row(L, bot, World_EntityPoolGet(pool, found[k].idx));
        lua_rawseti(L, -2, k + 1);
    }
    return 2;
}

static int
d_players(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct World* world = bot->core->world;
    struct World_EntityPool* pool = &world->entities.player;
    struct WorldEntity_Player* me = local_player(bot);
    int n = 0;

    lua_pushstring(L, "ok");
    lua_newtable(L);
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player const* p = World_EntityPoolGet(pool, i);
        int anim;
        if( !p )
            continue;
        /* The newest SEQUENCE op until its length runs out: the same rule
         * the live client's row reads (torirs_plugin_drive.c
         * drive_player_anim). The world here is never stepped, so the track
         * itself would never end. */
        anim = p->seq_sent_id;
        if( anim >= 0 )
        {
            int len = ToriRSServer_SeqLengthCycles(anim);
            if( len > 0 && (int)world->cycle - p->seq_sent_cycle >= len )
                anim = -1;
        }
        lua_newtable(L);
        lua_pushstring(L, p->name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, world->_base_tile_x +
                               (p->pathing.route_length > 0 ? p->pathing.route_x[0] : p->grid_position.x));
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, world->_base_tile_z +
                               (p->pathing.route_length > 0 ? p->pathing.route_z[0] : p->grid_position.z));
        lua_setfield(L, -2, "z");
        /* the server's whole tile, the same reading as `x`/`z` here; the
         * client's rows carry the stepping tile in x/z and the server tile
         * under this name, so a bot reads server_x on both lanes */
        lua_pushinteger(L, world->_base_tile_x +
                               (p->pathing.route_length > 0 ? p->pathing.route_x[0] : p->grid_position.x));
        lua_setfield(L, -2, "server_x");
        lua_pushinteger(L, world->_base_tile_z +
                               (p->pathing.route_length > 0 ? p->pathing.route_z[0] : p->grid_position.z));
        lua_setfield(L, -2, "server_z");
        lua_pushinteger(L, p->grid_position.level);
        lua_setfield(L, -2, "level");
        lua_pushinteger(L, p->server_pid);
        lua_setfield(L, -2, "pid");
        lua_pushboolean(L, p == me);
        lua_setfield(L, -2, "me");
        lua_pushinteger(L, anim);
        lua_setfield(L, -2, "anim");
        if( p == me )
        {
            struct ScriptrunCore const* c = bot->core;
            int last = c->own_seq_count - 1;
            lua_pushinteger(L, last >= 0 ? c->own_seq[last] : -1);
            lua_setfield(L, -2, "seq");
            lua_pushinteger(L, last >= 0 ? c->own_seq_tick[last] * SCRIPTRUN_CYCLES_PER_TICK : -1);
            lua_setfield(L, -2, "seq_tick");
            lua_pushinteger(L, c->own_seq_starts);
            lua_setfield(L, -2, "seq_starts");
            lua_newtable(L);
            for( int h = 0; h < c->own_seq_count; h++ )
            {
                lua_newtable(L);
                lua_pushinteger(L, c->own_seq_n[h]);
                lua_setfield(L, -2, "n");
                lua_pushinteger(L, c->own_seq[h]);
                lua_setfield(L, -2, "seq");
                lua_pushinteger(L, c->own_seq_tick[h] * SCRIPTRUN_CYCLES_PER_TICK);
                lua_setfield(L, -2, "tick");
                lua_pushinteger(L, (c->tick - c->own_seq_tick[h]) * SCRIPTRUN_CYCLES_PER_TICK);
                lua_setfield(L, -2, "age");
                lua_rawseti(L, -2, h + 1);
            }
            lua_setfield(L, -2, "seq_history");
        }
        lua_rawseti(L, -2, ++n);
    }
    return 2;
}

static int
d_projectiles(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ScriptrunCore* c = bot->core;
    int radius = (int)luaL_optinteger(L, 1, 0); /* <= 0: all (drive_ui_within_radius) */
    int px = 0, pz = 0, plevel = 0;
    int have = ScriptrunCore_LocalTile(c, &px, &pz, &plevel);
    int now = (int)c->world->cycle;
    int n = 0;

    lua_pushstring(L, "ok");
    lua_newtable(L);
    for( int i = 0; i < c->projectile_count; i++ )
    {
        struct ScriptrunProjectile const* p = &c->projectiles[i];
        int elapsed = now - p->launch_cycle;
        if( have && radius > 0 && cheb(px, pz, p->dst_x, p->dst_z) > radius )
            continue;
        lua_newtable(L);
        lua_pushinteger(L, -1);
        lua_setfield(L, -2, "seq");
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "seq_frame");
        lua_pushinteger(L, p->spotanim);
        lua_setfield(L, -2, "spotanim_id");
        lua_pushinteger(L, p->src_x);
        lua_setfield(L, -2, "src_x");
        lua_pushinteger(L, p->src_z);
        lua_setfield(L, -2, "src_z");
        lua_pushinteger(L, p->dst_x);
        lua_setfield(L, -2, "dst_x");
        lua_pushinteger(L, p->dst_z);
        lua_setfield(L, -2, "dst_z");
        lua_pushinteger(L, p->level);
        lua_setfield(L, -2, "level");
        lua_pushinteger(L, p->target);
        lua_setfield(L, -2, "target");
        lua_pushinteger(L, p->target > 0 ? p->target - 1 : -1);
        lua_setfield(L, -2, "target_npc_slot");
        lua_pushboolean(L, elapsed >= p->start_delay);
        lua_setfield(L, -2, "launched");
        lua_pushinteger(L, p->end_delay - elapsed > 0 ? p->end_delay - elapsed : 0);
        lua_setfield(L, -2, "cycles_left");
        /* the flight as the server sent it (DriveProjectileRow.duration) */
        lua_pushinteger(L, p->end_delay);
        lua_setfield(L, -2, "duration");
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "element_id");
        lua_rawseti(L, -2, ++n);
    }
    return 2;
}

static int
d_spotanims(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ScriptrunCore* c = bot->core;
    int radius = (int)luaL_optinteger(L, 1, 0); /* <= 0: all (drive_ui_within_radius) */
    int px = 0, pz = 0, plevel = 0;
    int have = ScriptrunCore_LocalTile(c, &px, &pz, &plevel);
    int now = (int)c->world->cycle;
    int n = 0;

    lua_pushstring(L, "ok");
    lua_newtable(L);
    for( int i = 0; i < c->map_anim_count; i++ )
    {
        struct ScriptrunMapAnim const* a = &c->map_anims[i];
        int left;
        if( have && radius > 0 && cheb(px, pz, a->x, a->z) > radius )
            continue;
        /* The core retires a floor graphic 40 ticks after it lands (the
         * server keeps no spotanim config to read its sequence length from). */
        left = 40 * SCRIPTRUN_CYCLES_PER_TICK - (now - a->cycle);
        lua_newtable(L);
        lua_pushinteger(L, -1);
        lua_setfield(L, -2, "seq");
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "seq_frame");
        lua_pushinteger(L, a->spotanim);
        lua_setfield(L, -2, "spotanim_id");
        lua_pushinteger(L, a->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, a->z);
        lua_setfield(L, -2, "z");
        lua_pushinteger(L, a->level);
        lua_setfield(L, -2, "level");
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "active");
        lua_pushinteger(L, left > 0 ? left : 0);
        lua_setfield(L, -2, "cycles_left");
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "element_id");
        lua_rawseti(L, -2, ++n);
    }
    return 2;
}

static int
d_objs(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ScriptrunCore* c = bot->core;
    int radius = (int)luaL_optinteger(L, 1, 0); /* <= 0: all (drive_ui_within_radius) */
    int px = 0, pz = 0, plevel = 0;
    int have = ScriptrunCore_LocalTile(c, &px, &pz, &plevel);
    int n = 0;

    lua_pushstring(L, "ok");
    lua_newtable(L);
    for( int i = 0; i < c->obj_count; i++ )
    {
        struct ScriptrunObj const* o = &c->objs[i];
        if( have && radius > 0 && cheb(px, pz, o->x, o->z) > radius )
            continue;
        lua_newtable(L);
        lua_pushinteger(L, o->obj_id);
        lua_setfield(L, -2, "obj_id");
        lua_pushinteger(L, o->count);
        lua_setfield(L, -2, "count");
        lua_pushinteger(L, o->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, o->z);
        lua_setfield(L, -2, "z");
        lua_pushinteger(L, o->level);
        lua_setfield(L, -2, "level");
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "element_id");
        lua_rawseti(L, -2, ++n);
    }
    return 2;
}

static int
d_locs(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ScriptrunCore* c = bot->core;
    int radius = (int)luaL_optinteger(L, 1, 0); /* <= 0: all (drive_ui_within_radius) */
    int px = 0, pz = 0, plevel = 0;
    int have = ScriptrunCore_LocalTile(c, &px, &pz, &plevel);
    int n = 0;

    lua_pushstring(L, "ok");
    lua_newtable(L);
    for( int i = 0; i < c->loc_count; i++ )
    {
        struct ScriptrunLoc const* l = &c->locs[i];
        if( l->loc_id < 0 || (have && radius > 0 && cheb(px, pz, l->x, l->z) > radius) )
            continue;
        lua_newtable(L);
        lua_pushinteger(L, l->loc_id);
        lua_setfield(L, -2, "loc_id");
        lua_pushinteger(L, l->loc_id);
        lua_setfield(L, -2, "resolved_loc_id");
        lua_pushinteger(L, l->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, l->z);
        lua_setfield(L, -2, "z");
        lua_pushinteger(L, l->level);
        lua_setfield(L, -2, "level");
        lua_pushinteger(L, l->shape);
        lua_setfield(L, -2, "shape");
        lua_pushinteger(L, l->angle);
        lua_setfield(L, -2, "angle");
        /* The copy's name for world_op's pin: its slot in the core's loc
         * list, + 1 (stable for the session; 0 is no copy). The client names
         * a copy by its scene element; this lane has none. */
        lua_pushinteger(L, i + 1);
        lua_setfield(L, -2, "element_id");
        lua_rawseti(L, -2, ++n);
    }
    return 2;
}

static int
d_empty_list(lua_State* L)
{
    lua_pushstring(L, "ok");
    lua_newtable(L);
    return 2;
}

/* --------------------------------------------------------- messages */

static int
d_messages(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ScriptrunCore* c = bot->core;
    int count = (int)luaL_optinteger(L, 1, 20);
    int n = 0;

    lua_pushstring(L, "ok");
    lua_newtable(L);
    for( int s = c->message_serial; s > 0 && n < count && s > c->message_serial - SCRIPTRUN_MESSAGES;
         s-- )
    {
        struct ScriptrunMessage const* m = &c->messages[s % SCRIPTRUN_MESSAGES];
        lua_newtable(L);
        lua_pushinteger(L, m->type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, m->serial);
        lua_setfield(L, -2, "serial");
        lua_pushstring(L, m->name);
        lua_setfield(L, -2, "sender");
        lua_pushstring(L, m->text);
        lua_setfield(L, -2, "text");
        lua_rawseti(L, -2, ++n);
    }
    return 2;
}

static int
d_message_serial(lua_State* L)
{
    lua_pushstring(L, "ok");
    lua_pushinteger(L, bot_of(L)->core->message_serial);
    return 2;
}

/* ----------------------------------------------------------- inputs */

static int
d_move_to(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    uint8_t payload[5];

    payload[0] = 0;
    put2(payload + 1, (int)luaL_checkinteger(L, 1));
    put2(payload + 3, (int)luaL_checkinteger(L, 2));
    handle(bot, PKTOUT_NAME_MOVE_GAMECLICK, payload, 5);
    return push_result(L, "ok", "walk sent");
}

/* The pressed row, as the client's _press_row answers it. */
static int
push_press(
    lua_State* L,
    char const* row_text,
    int element_id)
{
    lua_pushstring(L, "ok");
    lua_newtable(L);
    lua_pushstring(L, row_text ? row_text : "");
    lua_setfield(L, -2, "row_text");
    lua_pushinteger(L, element_id);
    lua_setfield(L, -2, "element_id");
    return 2;
}

/* world_op(kind, type_id, option [, element_id]): the client's
 * DrivePointer_WorldOp -- the op on an instance of a CONTENT TYPE, `no_row`
 * when its config offers no such op -- sent as the packet the row sends.
 * The client takes the first copy it finds; this takes the nearest, or the
 * named copy when the caller pins one (an attack's reach_element). Answers
 * the pressed row ({row_text, element_id}) where the client answers nothing:
 * the callers that press through the menu read it. */
/* api_drive.cast_npc(npc_id, spell_component, [element_id]) -> result
 *
 * A spell cast on an npc: what the client does with the spell's "Cast" row
 * armed (drive_pointer_spell_arm) and the npc's "Cast <spell> -> <npc>" row
 * pressed, which leaves as OPNPCT (the npc's slot, the spell's component).
 * The spellbook is a side panel: a spell on a display-hidden spellbook is
 * refused as the client refuses it (app_minimenu_ui_pick_live_reason). The
 * copy is the named element when one is given, else the nearest, as
 * d_world_op. Rune and level checks are the server's ([apnpct,...]). */
static int
d_cast_npc(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ScriptrunCore* c = bot->core;
    int type_id = (int)luaL_checkinteger(L, 1);
    int component = (int)luaL_checkinteger(L, 2);
    int want_element = (int)luaL_optinteger(L, 3, -1);
    struct World_EntityPool* pool = &c->world->entities.npc;
    struct WorldEntity_NPC const* best = NULL;
    int best_d = 1 << 30;
    int best_tx = 0, best_tz = 0;
    int px = 0, pz = 0, plevel = 0;
    uint8_t payload[8];

    if( !panel_showing(bot, component) )
        return push_result(L, "refused", "the spell's panel is display-hidden");
    if( !ScriptrunCore_LocalTile(c, &px, &pz, &plevel) )
        return push_result(L, "not_found", "not placed yet");
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC const* npc = World_EntityPoolGet(pool, i);
        int tx, tz, d;
        if( !npc || npc->server_slot < 0 || (npc->npc_id != type_id && npc->base_npc_id != type_id) )
            continue;
        if( want_element >= 0 && npc->element_id != want_element )
            continue;
        tx = c->world->_base_tile_x + npc->grid_position.x;
        tz = c->world->_base_tile_z + npc->grid_position.z;
        d = DrivePick_Distance(tx, tz, npc->grid_position.level, px, pz, plevel);
        if( DrivePick_Better(best != NULL, d, tx, tz, npc->server_slot, best_d, best_tx, best_tz,
                             best ? best->server_slot : 0) )
        {
            best_d = d;
            best_tx = tx;
            best_tz = tz;
            best = npc;
        }
    }
    if( !best )
        return push_result(L, "not_found", want_element >= 0 ? "that npc copy is not in view"
                                                              : "no such npc in view");
    put2(payload, best->server_slot);
    put4(payload + 2, component);
    handle(bot, PKTOUT_NAME_OPNPCT, payload, 6);
    return push_result(L, "ok", NULL);
}

static int
d_world_op(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ScriptrunCore* c = bot->core;
    char const* kind = luaL_checkstring(L, 1);
    int type_id = (int)luaL_checkinteger(L, 2);
    int op = (int)luaL_checkinteger(L, 3);
    int want_element = (int)luaL_optinteger(L, 4, -1);
    int px = 0, pz = 0, plevel = 0;
    uint8_t payload[8];

    if( op <= 0 )
        return push_result(L, "unsupported", "scriptrun: Examine needs a client");
    if( op > 5 )
        return push_result(L, "no_row", "op above 5");
    if( !ScriptrunCore_LocalTile(c, &px, &pz, &plevel) )
        return push_result(L, "not_found", "not placed yet");

    if( strcmp(kind, "npc") == 0 )
    {
        struct World_EntityPool* pool = &c->world->entities.npc;
        struct WorldEntity_NPC const* best = NULL;
        int best_d = 1 << 30;
        int best_tx = 0, best_tz = 0;
        const struct ToriRSServerNpcInfo* info;
        for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_NPC const* npc = World_EntityPoolGet(pool, i);
            int tx, tz, d;
            if( !npc || npc->server_slot < 0 || (npc->npc_id != type_id && npc->base_npc_id != type_id) )
                continue;
            if( want_element >= 0 && npc->element_id != want_element )
                continue;
            tx = c->world->_base_tile_x + npc->grid_position.x;
            tz = c->world->_base_tile_z + npc->grid_position.z;
            d = DrivePick_Distance(tx, tz, npc->grid_position.level, px, pz, plevel);
            if( DrivePick_Better(best != NULL, d, tx, tz, npc->server_slot, best_d, best_tx, best_tz,
                                 best ? best->server_slot : 0) )
            {
                best_d = d;
                best_tx = tx;
                best_tz = tz;
                best = npc;
            }
        }
        if( !best )
            return push_result(L, "not_found", want_element >= 0 ? "that npc copy is not in view"
                                                                  : "no such npc in view");
        /* The rows are the LIVE record's (the rung the core spawned and
         * re-resolves on every varp change). */
        info = ToriRSServer_NpcInfoRecord(best->npc_id);
        if( !info || !info->ops[op - 1] || !info->ops[op - 1][0] )
            return push_result(L, "no_row", "the npc offers no such op");
        put2(payload, best->server_slot);
        payload[2] = 0;
        handle(bot, PKTOUT_NAME_OPNPC1 + (op - 1), payload, 3);
        return push_press(L, info->ops[op - 1], best->element_id);
    }
    if( strcmp(kind, "loc") == 0 )
    {
        struct ScriptrunLoc const* best = NULL;
        int best_d = 1 << 30;
        char const* row;
        for( int i = 0; i < c->loc_count; i++ )
        {
            struct ScriptrunLoc const* l = &c->locs[i];
            int d;
            if( l->loc_id != type_id )
                continue;
            /* a pinned copy (d_locs' element_id: the list slot + 1) */
            if( want_element > 0 && i + 1 != want_element )
                continue;
            /* Another floor's copy only when this floor has none (a loc's
             * level is its cache level: a bridge's differs). */
            d = DrivePick_Distance(l->x, l->z, l->level, px, pz, plevel);
            if( DrivePick_Better(best != NULL, d, l->x, l->z, 0, best_d, best ? best->x : 0, best ? best->z : 0, 0) )
            {
                best_d = d;
                best = l;
            }
        }
        if( !best )
            return push_result(L, "not_found", "no such loc in view");
        row = ScriptrunCore_LocOpName(c, best->loc_id, op);
        if( !row )
            return push_result(L, "no_row", "the loc offers no such op");
        put2(payload, best->x);
        put2(payload + 2, best->z);
        put2(payload + 4, best->loc_id);
        payload[6] = 0;
        handle(bot, PKTOUT_NAME_OPLOC1 + (op - 1), payload, 7);
        return push_press(L, row, -1);
    }
    if( strcmp(kind, "obj") == 0 )
    {
        struct ScriptrunObj const* best = NULL;
        int best_d = 1 << 30;
        for( int i = 0; i < c->obj_count; i++ )
        {
            struct ScriptrunObj const* o = &c->objs[i];
            int d;
            if( o->obj_id != type_id )
                continue;
            /* Another floor's copy only when this floor has none (a loc's
             * level is its cache level: a bridge's differs). */
            d = DrivePick_Distance(o->x, o->z, o->level, px, pz, plevel);
            if( DrivePick_Better(best != NULL, d, o->x, o->z, 0, best_d, best ? best->x : 0, best ? best->z : 0, 0) )
            {
                best_d = d;
                best = o;
            }
        }
        if( !best )
            return push_result(L, "not_found", "no such object on the ground in view");
        put2(payload, best->x);
        put2(payload + 2, best->z);
        put2(payload + 4, best->obj_id);
        payload[6] = 0;
        handle(bot, PKTOUT_NAME_OPOBJ1 + (op - 1), payload, 7);
        return push_press(L, op == 3 ? "Take" : "", -1);
    }
    return luaL_error(L, "drive.world_op: unknown kind '%s'", kind);
}

/* IF_BUTTONX: g4 component, g2 sub, g2 obj, g1 op (mock239_if_button_decode). */
static void
send_if_buttonx(
    struct ScriptBot* bot,
    int component,
    int sub,
    int obj,
    int op)
{
    uint8_t payload[9];

    put4(payload, component);
    put2(payload + 4, sub);
    put2(payload + 6, obj);
    payload[8] = (uint8_t)op;
    handle(bot, PKTOUT_NAME_IF_BUTTONX, payload, 9);
}

/* A backpack cell's held op: OPHELD1..5 go out as the rev-239 backpack's
 * IF_BUTTONX ops 2, 3, 4, 6, 7 (mock239_if_button_backpack_op). */
static int
d_inv_op(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int component = (int)luaL_checkinteger(L, 1);
    int slot = (int)luaL_checkinteger(L, 2);
    int obj = (int)luaL_checkinteger(L, 3);
    int option = (int)luaL_checkinteger(L, 5);
    static int const IF_OP[6] = { 0, 2, 3, 4, 6, 7 };

    if( option < 1 || option > 5 )
        return push_result(L, "refused", "held op out of range");
    if( !panel_showing(bot, component) )
        return push_result(L, "refused", "the node or an ancestor of it is display-hidden");
    send_if_buttonx(bot, component, slot, obj, IF_OP[option]);
    return push_result(L, "ok", "held op sent");
}

static int
d_if_click(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    lua_Integer handle = luaL_checkinteger(L, 1);
    int component = (int)(handle & 0xffffffff);
    int sub = (int)(handle >> 32) - 1;   /* d_component's slot, -1: none */
    int op = (int)luaL_optinteger(L, 2, 1);

    if( op < 1 || op > 10 )
        return push_result(L, "refused", "op out of range");
    if( !panel_showing(bot, component) )
        return push_result(L, "refused", "the node or an ancestor of it is display-hidden");
    send_if_buttonx(bot, component, sub >= 0 ? sub : 0xffff, 0xffff, op);
    return push_result(L, "ok", "button sent");
}

/* A chatmenu row's id, as option_row hands it out: the rows are clientscript
 * children with no content symbol, and answering one is RESUME_PAUSEBUTTON on
 * chatmenu:options with the row as its sub (chat.rs2 ~p_choice_open's
 * if_addresumebutton). */
enum
{
    OPTION_ROW_BASE = -1000,
};

static int
d_resume(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int component = (int)luaL_checkinteger(L, 1);
    int asked = component;
    int sub = -1;
    uint8_t payload[6];

    if( component <= OPTION_ROW_BASE )
    {
        sub = OPTION_ROW_BASE - component;
        component = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
        if( component < 0 )
            return push_result(L, "not_found", "chatmenu:options");
    }
    put4(payload, component);
    put2(payload + 4, sub);
    /* Pending BEFORE the send: the server handles the input inline and its
     * answer (the next page's IF_OPENSUB) lands inside handle(), clearing it. */
    bot->core->resume_pending = asked;
    handle(bot, PKTOUT_NAME_RESUME_PAUSEBUTTON, payload, 6);
    /* resume_answered: a = the id the caller resumed on, as the client stamps
     * the component its own resume seam fired for -- raised by bot_resume
     * once this coroutine yields. */
    bot->resume_stamp_pending = 1;
    bot->resume_stamp_component = asked;
    return push_result(L, "ok", "resumed");
}

static int
host_component(void)
{
    return ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatbox:chatmodal");
}

/* The side tabs. Selecting one is the client's own business in rev 239 (a
 * stone's clientscript flips a varc; no packet leaves), and every panel is
 * mounted from login, so a scriptrun bot's tab press changes nothing the
 * server or this model can see: `tab` answers ok, `tab_by_name` the standard
 * stone order the client's [tabs] map carries. */
static char const* const TAB_NAMES[] = {
    "combat", "skills", "quest", "inventory", "equipment", "prayer", "magic",
    "clan", "account", "friends", "logout", "settings", "emote", "music",
};

static int
d_tab(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int tab = (int)luaL_checkinteger(L, 1);
    if( tab < 0 || tab >= (int)(sizeof(TAB_NAMES) / sizeof(TAB_NAMES[0])) )
        return push_result(L, "no_row", "no such tab");
    bot->tab = tab;
    return push_result(L, "ok", NULL);
}

/* Is the panel holding `component` showing?  The backpack, the worn tab, the
 * prayer book and the spellbook are side panels: only the selected tab's is displayed, and the
 * client's dispatcher refuses a press on a display-hidden node
 * (app_minimenu.c app_minimenu_ui_pick_live_reason). */
static int
panel_showing(
    struct ScriptBot const* bot,
    int component)
{
    int group = (component >> 16) & 0xffff;
    int inventory = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "inventory");
    int prayer = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "prayerbook");
    int spellbook = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "magic_spellbook");
    int worn = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "wornitems");

    if( group == inventory )
        return bot->tab == SCRIPTRUN_TAB_INVENTORY;
    if( group == prayer )
        return bot->tab == SCRIPTRUN_TAB_PRAYER;
    if( group == spellbook )
        return bot->tab == SCRIPTRUN_TAB_MAGIC;
    if( group == worn )
        return bot->tab == SCRIPTRUN_TAB_EQUIPMENT;
    return 1;
}

static int
d_tab_by_name(lua_State* L)
{
    char const* name = luaL_checkstring(L, 1);
    (void)bot_of(L);
    for( int i = 0; i < (int)(sizeof(TAB_NAMES) / sizeof(TAB_NAMES[0])); i++ )
        if( strcmp(name, TAB_NAMES[i]) == 0 )
        {
            lua_pushstring(L, "ok");
            lua_pushinteger(L, i);
            return 2;
        }
    lua_pushstring(L, "no_row");
    lua_pushnil(L);
    return 2;
}

/* api.drive.route(x, z [, {size, run, from = {x, z}}]): the client's
 * lua_drive_route / DriveUi_Route, over this bot's own collision map (the
 * client's collision_map_route_tiles, the server's own flood), with the
 * client's OSRS click features.  Same answer shape. */
enum
{
    SCRIPTRUN_ROUTE_CAP = 4000,
};

/* api_drive.plan(spec): the raider's beam search over this bot's own
 * collision map (plugin/torirs_drive_plan_lua.h; the client's lua_drive_plan
 * is the same code over the client's map). */
static int
d_plan(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ScriptrunCore* c = bot->core;
    struct World* world = c->world;
    struct CollisionMap* cm;
    int px = 0, pz = 0, level = 0;

    luaL_checktype(L, 1, LUA_TTABLE);
    if( !c->have_collision || !ScriptrunCore_LocalTile(c, &px, &pz, &level) )
    {
        lua_pushstring(L, "not_visible");
        lua_pushnil(L);
        return 2;
    }
    if( level < 0 )
        level = 0;
    if( level >= COLLISION_LEVELS )
        level = COLLISION_LEVELS - 1;
    cm = world->collision_maps[level];
    if( !cm )
    {
        lua_pushstring(L, "no_row");
        lua_pushnil(L);
        return 2;
    }
    return DrivePlanLua(L, 1, cm, world->_base_tile_x, world->_base_tile_z, world->_scene_size, px, pz);
}

static int
d_route(lua_State* L)
{
    static int path_x[SCRIPTRUN_ROUTE_CAP];
    static int path_z[SCRIPTRUN_ROUTE_CAP];
    struct ScriptBot* bot = bot_of(L);
    struct ScriptrunCore* c = bot->core;
    struct World* world = c->world;
    struct ToriRS_FeatureTable const* features = ToriRS_Features_OSRS();
    struct CollisionNearestOpts nearest_opts = { 0 };
    struct CollisionApproach approach = { 0 };
    int x = (int)luaL_checkinteger(L, 1);
    int z = (int)luaL_checkinteger(L, 2);
    int size = 0, run = 0, range = -1, start_x = -1, start_z = -1;
    int px = 0, pz = 0, level = 0;
    int base_x, base_z, src_x, src_z, arrive_x, arrive_z, nearest = 0, steps, per, ticks;
    struct CollisionMap* cm;

    if( lua_gettop(L) >= 3 && !lua_isnil(L, 3) )
    {
        luaL_checktype(L, 3, LUA_TTABLE);
        lua_getfield(L, 3, "from");
        if( !lua_isnil(L, -1) )
        {
            luaL_checktype(L, -1, LUA_TTABLE);
            lua_getfield(L, -1, "x");
            start_x = (int)luaL_checkinteger(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "z");
            start_z = (int)luaL_checkinteger(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        lua_getfield(L, 3, "size");
        if( !lua_isnil(L, -1) )
            size = (int)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 3, "run");
        if( !lua_isnil(L, -1) )
        {
            luaL_checktype(L, -1, LUA_TBOOLEAN);
            run = lua_toboolean(L, -1);
        }
        lua_pop(L, 1);
        lua_getfield(L, 3, "range");
        if( !lua_isnil(L, -1) )
            range = (int)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
    }
    luaL_argcheck(L, size >= 0, 3, "opts.size is an entity size, 0 or more");
    luaL_argcheck(L, range < 0 || size > 0, 3, "opts.range needs opts.size: it is a range to an entity");
    if( !c->have_collision || !ScriptrunCore_LocalTile(c, &px, &pz, &level) )
    {
        lua_pushstring(L, "not_visible");
        lua_pushnil(L);
        return 2;
    }
    if( level < 0 )
        level = 0;
    if( level >= COLLISION_LEVELS )
        level = COLLISION_LEVELS - 1;
    cm = world->collision_maps[level];
    if( !cm )
    {
        lua_pushstring(L, "no_row");
        lua_pushnil(L);
        return 2;
    }
    base_x = world->_base_tile_x;
    base_z = world->_base_tile_z;
    src_x = (start_x >= 0 ? start_x : px) - base_x;
    src_z = (start_z >= 0 ? start_z : pz) - base_z;
    if( src_x < 0 || src_z < 0 || src_x >= world->_scene_size || src_z >= world->_scene_size ||
        x - base_x < 0 || z - base_z < 0 || x - base_x >= world->_scene_size || z - base_z >= world->_scene_size )
    {
        lua_pushstring(L, "refused");
        lua_pushnil(L);
        return 2;
    }
    if( size > 0 )
    {
        collision_approach_from_shape(-2, 0, size, size, 0, 1, &approach);
        nearest_opts.range = features->op_click_nearest_range;
        nearest_opts.max_dist = 100;
        nearest_opts.rank_by_rect_distance = features->nearest_ranks_by_rect_distance;
        nearest_opts.unbounded = 0;
    }
    else
    {
        collision_nearest_opts_from_model(features->ground_click_nearest_model, &nearest_opts);
        nearest_opts.unbounded = features->ground_click_nearest_unbounded;
    }
    arrive_x = x - base_x;
    arrive_z = z - base_z;
    steps = collision_map_route_tiles(cm, src_x, src_z, x - base_x, z - base_z, size > 0 ? &approach : NULL,
                                      &nearest_opts, path_x, path_z, SCRIPTRUN_ROUTE_CAP, &nearest, &arrive_x,
                                      &arrive_z);
    if( steps < 0 )
    {
        lua_pushstring(L, "not_found");
        lua_pushnil(L);
        return 2;
    }
    per = run ? 2 : 1;
    /* a ranged interaction's walk, cut where the server fires it (the
     * client's DriveUi_Route does the same, through the same function) */
    if( range >= 0 && size > 0 )
    {
        steps = collision_route_cut_in_range(cm, src_x, src_z, path_x, path_z, steps, per, x - base_x, z - base_z,
                                             size, range);
        arrive_x = steps > 0 ? path_x[steps - 1] : src_x;
        arrive_z = steps > 0 ? path_z[steps - 1] : src_z;
    }
    ticks = (steps + per - 1) / per;
    lua_pushstring(L, "ok");
    lua_createtable(L, 0, 6);
    lua_createtable(L, steps, 0);
    for( int i = 0; i < steps; i++ )
    {
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, path_x[i] + base_x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, path_z[i] + base_z);
        lua_setfield(L, -2, "z");
        lua_pushboolean(L, run && (i % 2) == 1);
        lua_setfield(L, -2, "run");
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "tiles");
    lua_createtable(L, ticks, 0);
    for( int i = 0; i < ticks; i++ )
    {
        int k = (i + 1) * per - 1;
        if( k > steps - 1 )
            k = steps - 1;
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, path_x[k] + base_x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, path_z[k] + base_z);
        lua_setfield(L, -2, "z");
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "ticks");
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, arrive_x + base_x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, arrive_z + base_z);
    lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "arrive");
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, start_x >= 0 ? start_x : px);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, start_x >= 0 ? start_z : pz);
    lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "from");
    lua_pushboolean(L, nearest != 0);
    lua_setfield(L, -2, "nearest");
    lua_pushboolean(L, run != 0);
    lua_setfield(L, -2, "run");
    return 2;
}

static int
d_modal_group(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int host = host_component();
    int group = host >= 0 ? ScriptrunCore_MountedAt(bot->core, host) : -1;

    if( group < 0 )
        return push_result(L, "not_found", "nothing under chat_modal_host");
    lua_pushstring(L, "ok");
    lua_pushinteger(L, group);
    return 2;
}

static int
d_pause_pending(lua_State* L)
{
    lua_pushstring(L, "ok");
    lua_pushinteger(L, bot_of(L)->core->resume_pending);
    return 2;
}

static int
d_meslayer_mode(lua_State* L)
{
    (void)bot_of(L);
    lua_pushstring(L, "ok");
    lua_pushinteger(L, 0);
    return 2;
}

/* The continue seams are pause buttons of the mounted page: armed while the
 * page is up. */
static int
d_click_armed(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int component = (int)luaL_checkinteger(L, 1);

    lua_pushstring(L, "ok");
    lua_pushboolean(L, ScriptrunCore_InterfaceOpen(bot->core, (component >> 16) & 0xffff));
    return 2;
}

static int
d_widget_own_hidden(lua_State* L)
{
    (void)bot_of(L);
    (void)luaL_checkinteger(L, 1);
    lua_pushstring(L, "ok");
    lua_pushboolean(L, 0);
    return 2;
}

/* chatmenu's title and rows: the clientscript chatbox_multi_init's two
 * strings -- the header, then the rows joined with '|' -- which is what the
 * client's CS2 builds the rows from. */
static int
d_options(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ScriptrunCore const* c = bot->core;
    int iface = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INTERFACE, "chatmenu");
    char rows[SCRIPTRUN_TEXT];
    int n = 0;

    if( iface < 0 || !ScriptrunCore_InterfaceOpen(c, iface) || c->last_script.serial == 0 )
        return push_result(L, "not_found", "no chatmenu up");
    lua_pushstring(L, "ok");
    lua_newtable(L);
    lua_pushstring(L, c->last_script.str_args[0]);
    lua_setfield(L, -2, "title");
    lua_newtable(L);
    snprintf(rows, sizeof(rows), "%s", c->last_script.str_args[1]);
    for( char* save = NULL, *tok = strtok_r(rows, "|", &save); tok; tok = strtok_r(NULL, "|", &save) )
    {
        lua_pushstring(L, tok);
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "rows");
    return 2;
}

static int
d_option_row(lua_State* L)
{
    int row = (int)luaL_checkinteger(L, 1);
    (void)bot_of(L);
    if( row < 1 || row > 5 )
        return luaL_error(L, "drive.option_row: row must be 1..5");
    lua_pushstring(L, "ok");
    lua_pushinteger(L, OPTION_ROW_BASE - row);
    return 2;
}

static int
d_close_modal(lua_State* L)
{
    handle(bot_of(L), PKTOUT_NAME_CLOSE_MODAL, NULL, 0);
    return push_result(L, "ok", "closed");
}

/* A typed ::command, as the client sends it: CLIENT_CHEAT, a newline-ended
 * string (torirs_server_world.c handle_cheat). Its answer is the game message
 * the bot is sent, which QD.cheat awaits. */
static int
d_cheat(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    char const* text = luaL_checkstring(L, 1);
    uint8_t payload[200];
    int len;

    while( *text == ':' )
        text++;
    len = snprintf((char*)payload, sizeof(payload), "%s\n", text);
    if( len <= 0 || len >= (int)sizeof(payload) )
        return push_result(L, "refused", "cheat text too long");
    handle(bot, PKTOUT_NAME_CLIENT_CHEAT, payload, len);
    return push_result(L, "ok", "sent");
}

/* --------------------------------------------------------- interfaces */

static int
d_widget_presented(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int component = (int)luaL_checkinteger(L, 1);

    lua_pushstring(L, "ok");
    lua_pushboolean(L, ScriptrunCore_InterfaceOpen(bot->core, (component >> 16) & 0xffff) &&
                           panel_showing(bot, component));
    return 2;
}

static int
d_group_present(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    int group = (int)luaL_checkinteger(L, 1);

    lua_pushstring(L, "ok");
    lua_pushboolean(L, ScriptrunCore_InterfaceOpen(bot->core, group));
    return 2;
}

static int
d_widget_text(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    char const* text = ScriptrunCore_Text(bot->core, (int)luaL_checkinteger(L, 1));

    /* The client: "" if it has none. */
    lua_pushstring(L, "ok");
    lua_pushstring(L, text ? text : "");
    return 2;
}

/* ------------------------------------------------------------- party */

static int
d_party(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ScriptRun* run = bot->run;

    lua_pushstring(L, "ok");
    lua_newtable(L);
    lua_pushinteger(L, bot->index + 1);
    lua_setfield(L, -2, "role");
    lua_pushinteger(L, run->count);
    lua_setfield(L, -2, "size");
    lua_newtable(L);
    for( int i = 0; i < run->count; i++ )
    {
        lua_pushstring(L, run->bots[i].name);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "names");
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "frame_locked");
    return 2;
}

static int
d_barrier_mark(lua_State* L)
{
    struct ScriptRun* run = bot_of(L)->run;
    char const* file = luaL_checkstring(L, 1);

    for( int i = 0; i < run->barrier_count; i++ )
        if( strcmp(run->barriers[i], file) == 0 )
            return push_result(L, "ok", NULL);
    assert(run->barrier_count < SCRIPTRUN_BARRIERS);
    run->barriers[run->barrier_count] = strdup(file);
    assert(run->barriers[run->barrier_count]);
    /* STAMPED WITH THE TICK, honoured from the next tick on -- the live
     * lane's rule (torirs_plugin_drive.c lua_drive_barrier_present: a mark
     * carries its writer's lockstep tick and is present only to a reader a
     * boundary later). Before this a mark counted at once, so a party whose
     * members were already waiting passed a barrier in the tick the leader
     * marked it, one tick before the live party did, and every player
     * action after it sat one tick earlier than the live lane's against
     * Verzik's unchanged clock (2026-10-09: the two tick logs differed by
     * exactly that tick from the first consume on). */
    run->barrier_ticks[run->barrier_count] = bot_of(L)->core->tick;
    run->barrier_count++;
    return push_result(L, "ok", NULL);
}

static int
d_barrier_present(lua_State* L)
{
    struct ScriptRun* run = bot_of(L)->run;
    char const* file = luaL_checkstring(L, 1);

    for( int i = 0; i < run->barrier_count; i++ )
        if( strcmp(run->barriers[i], file) == 0 )
            return push_result(L, run->barrier_ticks[i] < bot_of(L)->core->tick ? "ok" : "not_found", NULL);
    return push_result(L, "not_found", NULL);
}

/* ------------------------------------------------- the server's log */

static int
d_ticklog_start(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ToriRSServer* srv = bot->run->srv;

    /* The client's: <session>/ticklog.tsv when the run has a session
     * (torirs_plugin_drive_ticklog.c lua_drive_ticklog_start). */
    if( !ToriRSServer_TicklogEnabled(srv) )
    {
        char path[1024];
        path[0] = '\0';
        if( bot->run->session_dir )
            snprintf(path, sizeof(path), "%s/ticklog.tsv", bot->run->session_dir);
        ToriRSServer_TicklogEnable(srv, path[0] ? path : NULL);
    }
    lua_pushstring(L, "ok");
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)ToriRSServer_TicklogStartTick());
    lua_setfield(L, -2, "start_tick");
    lua_pushinteger(L, (lua_Integer)srv->tick);
    lua_setfield(L, -2, "tick");
    lua_pushinteger(L, (lua_Integer)ToriRSServer_TicklogCount());
    lua_setfield(L, -2, "serial");
    lua_pushstring(L, bot->run->session_dir ? bot->run->session_dir : "");
    lua_setfield(L, -2, "path");
    return 2;
}

/* A row as torirs_plugin_drive_ticklog.c drive_ticklog_push_row builds it. */
static void
push_ticklog_row(
    lua_State* L,
    struct ToriRSServerTicklogRow const* row)
{
    lua_createtable(L, 0, 11);
    lua_pushinteger(L, (lua_Integer)row->serial);
    lua_setfield(L, -2, "serial");
    lua_pushinteger(L, (lua_Integer)row->tick);
    lua_setfield(L, -2, "tick");
    lua_pushstring(L, ToriRSServer_TicklogKindName(row->kind));
    lua_setfield(L, -2, "kind");
    lua_pushinteger(L, (lua_Integer)row->a);
    lua_setfield(L, -2, "a");
    lua_pushinteger(L, (lua_Integer)row->b);
    lua_setfield(L, -2, "b");
    lua_pushinteger(L, (lua_Integer)row->c);
    lua_setfield(L, -2, "c");
    lua_pushinteger(L, (lua_Integer)row->d);
    lua_setfield(L, -2, "d");
    lua_pushinteger(L, (lua_Integer)row->e);
    lua_setfield(L, -2, "e");
    lua_pushinteger(L, (lua_Integer)row->f);
    lua_setfield(L, -2, "f");
    lua_pushinteger(L, (lua_Integer)row->g);
    lua_setfield(L, -2, "g");
    if( row->label[0] )
    {
        lua_pushstring(L, row->label);
        lua_setfield(L, -2, "label");
    }
}

/* api.drive.ticklog([after_serial], [max], [kind], [slot]): the client's
 * lua_drive_ticklog -- rows in the array part, then next_serial, serial,
 * tick. */
static int
d_ticklog(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ToriRSServer* srv = bot->run->srv;
    int after = (int)luaL_optinteger(L, 1, 0);
    int max = (int)luaL_optinteger(L, 2, 8192);
    int slot = (int)luaL_optinteger(L, 4, -1);
    int kind = -1;
    struct ToriRSServerTicklogRow* rows;
    int count;
    uint32_t next_serial;

    if( !lua_isnoneornil(L, 3) )
    {
        if( lua_type(L, 3) != LUA_TSTRING )
            return luaL_error(L, "drive.ticklog: kind wants a row kind's name or nil");
        kind = ToriRSServer_TicklogKindFromName(lua_tostring(L, 3));
        if( kind < 0 )
            return luaL_error(L, "drive.ticklog: no row kind '%s'", lua_tostring(L, 3));
    }
    if( !ToriRSServer_TicklogEnabled(srv) )
    {
        lua_pushstring(L, "refused");
        lua_pushnil(L);
        return 2;
    }
    if( after < 0 )
        return luaL_error(L, "drive.ticklog: after_serial %d is negative", after);
    if( max <= 0 || max > 8192 )
        max = 8192;
    rows = malloc((size_t)max * sizeof(*rows));
    assert(rows);
    count = ToriRSServer_TicklogReadFiltered((uint32_t)after, kind, slot, rows, max, &next_serial);
    lua_pushstring(L, "ok");
    lua_createtable(L, count, 3);
    for( int i = 0; i < count; i++ )
    {
        push_ticklog_row(L, &rows[i]);
        lua_rawseti(L, -2, i + 1);
    }
    free(rows);
    lua_pushinteger(L, (lua_Integer)next_serial);
    lua_setfield(L, -2, "next_serial");
    lua_pushinteger(L, (lua_Integer)ToriRSServer_TicklogCount());
    lua_setfield(L, -2, "serial");
    lua_pushinteger(L, (lua_Integer)srv->tick);
    lua_setfield(L, -2, "tick");
    return 2;
}

static int
d_ticklog_mark(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    char const* label = luaL_checkstring(L, 1);
    uint32_t serial;

    if( !ToriRSServer_TicklogEnabled(bot->run->srv) )
    {
        lua_pushstring(L, "refused");
        lua_pushnil(L);
        return 2;
    }
    serial = ToriRSServer_TicklogMark(label);
    lua_pushstring(L, "ok");
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, (lua_Integer)serial);
    lua_setfield(L, -2, "serial");
    lua_pushinteger(L, (lua_Integer)bot->run->srv->tick);
    lua_setfield(L, -2, "tick");
    return 2;
}

/* The world slot the tick log keys by, for this bot's client slot. */
static int
d_server_npc_slot(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    struct ToriRSServer* srv = bot->run->srv;
    int client_slot = (int)luaL_checkinteger(L, 1);
    int top = srv->npc_slot_max < TORIRSSERVER_NPC_MAX ? srv->npc_slot_max : TORIRSSERVER_NPC_MAX;

    for( int w = 0; w < top; w++ )
        if( srv->npcs[w].active && ToriRSServer_SlotMapClient(bot->player, w) == client_slot )
        {
            lua_pushstring(L, "ok");
            lua_pushinteger(L, w);
            return 2;
        }
    return push_result(L, "not_found", NULL);
}

static int
d_seq_length(lua_State* L)
{
    int cycles = ToriRSServer_SeqLengthCycles((int)luaL_checkinteger(L, 1));
    if( cycles <= 0 )
        return push_result(L, "not_found", NULL);
    lua_pushstring(L, "ok");
    lua_pushinteger(L, cycles);
    return 2;
}

/* ------------------------------------------------------ unsupported */

static int
d_unsupported(lua_State* L)
{
    char const* name = lua_tostring(L, lua_upvalueindex(2));
    char detail[128];

    snprintf(detail, sizeof(detail), "scriptrun: drive.%s needs a client", name ? name : "?");
    return push_result(L, "unsupported", detail);
}

static int
d_noop(lua_State* L)
{
    (void)L;
    return 0;
}

/* Every verb the client registers (plugin/torirs_plugin_drive*.c). Those not
 * bound above answer "unsupported" rather than being nil, so a part that probes
 * a capability degrades the way it does on a client without it. */
static char const* const CLIENT_VERBS[] = {
    "action_for_slot", "botdrive", "camera", "camera_events", "camera_pose", "camera_state",
    "camera_turn_release", "camera_turn_toward", "click_armed", "events", "forget_varps",
    "inv_arm", "inv_cast", "inv_use_on", "key", "launch_answer", "launch_close",
    "launch_command", "launch_open", "launch_spawn", "launch_status", "loc_copies",
    "loc_variants", "menu_rect", "menu_row_find", "menu_rows", "menu_visible", "meslayer_mode",
    "modal_group", "modal_live", "model_points", "model_pose", "mouse_button", "mouse_move",
    "move_near", "npc_pose", "npc_record", "op_available", "option_row", "options",
    "party_host", "pause_pending", "pick_holds", "pick_point", "play", "player_delayed",
    "plan", "pump", "quit", "render_frame", "render_skip", "route", "screen_position", "server_los",
    "server_npc_pack", "shot", "spell_arm", "start", "stop", "tab", "tab_by_name", "tests",
    "text", "var_content", "varbit_base", "varbit_content", "vessel", "view_attach",
    "view_detach", "view_interact", "view_status", "view_watcher", "widget_at",
    "widget_bounds", "widget_model", "widget_own_hidden", "world_gate",
};

static void
bind(
    lua_State* L,
    struct ScriptBot* bot,
    char const* name,
    lua_CFunction fn)
{
    lua_pushlightuserdata(L, bot);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

static void
push_drive_table(
    lua_State* L,
    struct ScriptBot* bot)
{
    lua_newtable(L);
    for( size_t i = 0; i < sizeof(CLIENT_VERBS) / sizeof(CLIENT_VERBS[0]); i++ )
    {
        lua_pushlightuserdata(L, bot);
        lua_pushstring(L, CLIENT_VERBS[i]);
        lua_pushcclosure(L, d_unsupported, 2);
        lua_setfield(L, -2, CLIENT_VERBS[i]);
    }
    bind(L, bot, "tick", d_tick);
    bind(L, bot, "server_tick", d_server_tick);
    bind(L, bot, "settled", d_settled);
    bind(L, bot, "await", d_await);
    bind(L, bot, "report", d_report);
    bind(L, bot, "ledger", d_ledger);
    bind(L, bot, "finish", d_finish);
    bind(L, bot, "session", d_session);
    bind(L, bot, "status", d_status);
    bind(L, bot, "symbol", d_symbol);
    bind(L, bot, "symbol_name", d_symbol_name);
    bind(L, bot, "component", d_component);
    bind(L, bot, "cheat", d_cheat);
    bind(L, bot, "player_tile", d_player_tile);
    bind(L, bot, "player_idle", d_player_idle);
    bind(L, bot, "skill", d_skill);
    bind(L, bot, "varp", d_varp);
    bind(L, bot, "var_server", d_varp);
    bind(L, bot, "varbit", d_varbit);
    bind(L, bot, "varbit_server", d_varbit);
    bind(L, bot, "inv_count", d_inv_count);
    bind(L, bot, "inv_slot", d_inv_slot);
    bind(L, bot, "inv_capacity", d_inv_capacity);
    bind(L, bot, "npcs", d_npcs);
    bind(L, bot, "players", d_players);
    bind(L, bot, "projectiles", d_projectiles);
    bind(L, bot, "spotanims", d_spotanims);
    bind(L, bot, "objs", d_objs);
    bind(L, bot, "locs", d_locs);
    bind(L, bot, "loc_copies", d_empty_list);
    bind(L, bot, "messages", d_messages);
    bind(L, bot, "message_serial", d_message_serial);
    bind(L, bot, "move_to", d_move_to);
    bind(L, bot, "world_op", d_world_op);
    bind(L, bot, "cast_npc", d_cast_npc);
    bind(L, bot, "inv_op", d_inv_op);
    bind(L, bot, "if_click", d_if_click);
    bind(L, bot, "resume", d_resume);
    bind(L, bot, "close_modal", d_close_modal);
    bind(L, bot, "widget_presented", d_widget_presented);
    bind(L, bot, "group_present", d_group_present);
    bind(L, bot, "widget_text", d_widget_text);
    bind(L, bot, "modal_group", d_modal_group);
    bind(L, bot, "tab", d_tab);
    bind(L, bot, "route", d_route);
    bind(L, bot, "plan", d_plan);
    bind(L, bot, "tab_by_name", d_tab_by_name);
    bind(L, bot, "pause_pending", d_pause_pending);
    bind(L, bot, "meslayer_mode", d_meslayer_mode);
    bind(L, bot, "click_armed", d_click_armed);
    bind(L, bot, "widget_own_hidden", d_widget_own_hidden);
    bind(L, bot, "options", d_options);
    bind(L, bot, "option_row", d_option_row);
    bind(L, bot, "party", d_party);
    bind(L, bot, "barrier_mark", d_barrier_mark);
    bind(L, bot, "barrier_present", d_barrier_present);
    bind(L, bot, "ticklog_start", d_ticklog_start);
    bind(L, bot, "ticklog", d_ticklog);
    bind(L, bot, "ticklog_mark", d_ticklog_mark);
    bind(L, bot, "server_npc_slot", d_server_npc_slot);
    bind(L, bot, "seq_length", d_seq_length);
    bind(L, bot, "pump", d_noop);
    /* A capability a part may branch on: this host has no screen. */
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "scriptrun");
}

static int
core_log(lua_State* L)
{
    struct ScriptBot* bot = bot_of(L);
    if( !bot->run->quiet )
        fprintf(stderr, "[%s] %s\n", bot->name, luaL_optstring(L, 1, ""));
    return 0;
}

static int
core_screen(lua_State* L)
{
    lua_pushinteger(L, 30); /* the gameframe (enum AppScreen) */
    return 1;
}

/* --------------------------------------------------------- Lua state */

/* The bootstrap: what tools/quest_gate/run.py's wrapper does, in short. The
 * setup list's cheats, then run(t), then finish. */
static char const* const BOOTSTRAP =
    "local src, t, path = ...\n"
    "local chunk, err = load(src, '=' .. path, 't')\n"
    "if not chunk then error('test does not compile: ' .. tostring(err)) end\n"
    "local QUEST = chunk()\n"
    "if type(QUEST) ~= 'table' or type(QUEST.run) ~= 'function' then\n"
    "  error('test did not return { run = function(t) ... end }')\n"
    "end\n"
    "t.ticks(2)\n"
    "for _, line in ipairs(QUEST.setup or {}) do\n"
    "  local r, d = t.cheat(line)\n"
    "  if r ~= 'ok' then t.step('setup ' .. line, 'FAIL', tostring(r) .. ' ' .. tostring(d)) end\n"
    "end\n"
    "t.ticks(2)\n"
    "local ok, e = pcall(QUEST.run, t)\n"
    "if not ok then t.step('script-error', 'FAIL', tostring(e)) ; t.finish(1) ; return end\n"
    "t.finish(0)\n";

static int
bot_lua_init(struct ScriptBot* bot)
{
    struct ScriptRun* run = bot->run;
    static luaL_Reg const LIBS[] = {
        { LUA_GNAME, luaopen_base },
        { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string },
        { LUA_MATHLIBNAME, luaopen_math },
        { LUA_UTF8LIBNAME, luaopen_utf8 },
        { NULL, NULL },
    };
    char path[1024];
    char* composed = NULL;
    size_t composed_len = 0;
    lua_State* L;

    L = luaL_newstate();
    assert(L);
    bot->L = L;
    for( luaL_Reg const* lib = LIBS; lib->name; lib++ )
    {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }

    /* The parts, in the client's order, then the plugin file last: the only
     * one with a top-level `return plugin`. */
    for( int i = 0; i <= DRIVE_SCRIPT_PART_COUNT; i++ )
    {
        long len = 0;
        char* src;
        snprintf(path, sizeof(path), "%s/%s", run->script_root,
                 i < DRIVE_SCRIPT_PART_COUNT ? DRIVE_SCRIPT_PARTS[i] : "plugins/quest_driver.lua");
        src = read_file(path, &len);
        if( !src )
        {
            fprintf(stderr, "scriptrun: cannot read %s\n", path);
            free(composed);
            return 0;
        }
        composed = realloc(composed, composed_len + (size_t)len + 2);
        assert(composed);
        memcpy(composed + composed_len, src, (size_t)len);
        composed_len += (size_t)len;
        composed[composed_len++] = '\n';
        free(src);
    }
    if( luaL_loadbuffer(L, composed, composed_len, "=quest-driver") != LUA_OK )
    {
        fprintf(stderr, "scriptrun: the driver does not compile: %s\n", lua_tostring(L, -1));
        free(composed);
        return 0;
    }
    free(composed);
    if( lua_pcall(L, 0, 1, 0) != LUA_OK )
    {
        fprintf(stderr, "scriptrun: the driver did not load: %s\n", lua_tostring(L, -1));
        return 0;
    }
    /* plugin.on_start(api) */
    lua_getfield(L, -1, "on_start");
    lua_newtable(L);
    push_drive_table(L, bot);
    lua_setfield(L, -2, "drive");
    lua_newtable(L);
    bind(L, bot, "log", core_log);
    bind(L, bot, "screen", core_screen);
    lua_setfield(L, -2, "core");
    if( lua_pcall(L, 1, 0, 0) != LUA_OK )
    {
        fprintf(stderr, "scriptrun: on_start failed: %s\n", lua_tostring(L, -1));
        return 0;
    }
    lua_pop(L, 1); /* plugin */

    /* QD_PARTY: what the launcher writes for a party seat
     * (torirs_plugin_drive.c drive_push_party); a solo run sets none. */
    if( run->count > 1 )
    {
        lua_newtable(L);
        lua_pushinteger(L, bot->index + 1);
        lua_setfield(L, -2, "role");
        lua_pushinteger(L, run->count);
        lua_setfield(L, -2, "size");
        lua_createtable(L, run->count, 0);
        for( int seat = 0; seat < run->count; seat++ )
        {
            lua_pushstring(L, run->bots[seat].name);
            lua_rawseti(L, -2, seat + 1);
        }
        lua_setfield(L, -2, "names");
        lua_setglobal(L, "QD_PARTY");
    }

    bot->co = lua_newthread(L);
    bot->co_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if( luaL_loadstring(bot->co, BOOTSTRAP) != LUA_OK )
    {
        fprintf(stderr, "scriptrun: bootstrap: %s\n", lua_tostring(bot->co, -1));
        return 0;
    }
    lua_pushstring(bot->co, run->test_source);
    lua_getglobal(bot->co, "QD_ROOT");
    lua_pushstring(bot->co, run->test_path);
    bot->state = SCRIPTBOT_START;
    bot->await_level_ref = LUA_NOREF;
    bot->await_match_ref = LUA_NOREF;
    bot->await_kind = 0;
    return 1;
}

/* The client's per-resume instruction budget (torirs_plugin_lua.c
 * PLUGIN_LUA_STEP_BUDGET, armed on the coroutine before every resume): a
 * script that does more work than this between two waits dies on the live
 * client, so it must die here too. */
enum
{
    SCRIPTRUN_STEP_BUDGET = 400000,
};

static void
step_budget_hook(
    lua_State* L,
    lua_Debug* debug)
{
    (void)debug;
    luaL_error(L, "instruction budget exhausted (%d)", SCRIPTRUN_STEP_BUDGET);
}

/* Resume with `nargs` already pushed; settle the state from the outcome. */
static void
bot_resume(
    struct ScriptBot* bot,
    int nargs)
{
    int nres = 0;
    int status;

    /* TORIRS_SCRIPTRUN_STEP_BUDGET lowers it, to measure a script's headroom
     * (never raises it past the client's). */
    {
        static int budget = 0;
        if( budget == 0 )
        {
            char const* e = getenv("TORIRS_SCRIPTRUN_STEP_BUDGET");
            budget = e && atoi(e) > 0 && atoi(e) < SCRIPTRUN_STEP_BUDGET ? atoi(e) : SCRIPTRUN_STEP_BUDGET;
        }
        lua_sethook(bot->co, step_budget_hook, LUA_MASKCOUNT, budget);
    }
    status = lua_resume(bot->co, bot->L, nargs, &nres);
    lua_sethook(bot->co, NULL, 0, 0);

    if( bot->resume_stamp_pending )
    {
        bot->resume_stamp_pending = 0;
        ScriptrunCore_Event(bot->core, SCRIPTRUN_EV_RESUME_ANSWERED, bot->resume_stamp_component, 0, 0, 0);
    }
    if( status == LUA_YIELD )
    {
        lua_pop(bot->co, nres);
        return;
    }
    if( status != LUA_OK )
    {
        fprintf(stdout, "[%s t%d] FAIL   script-error  %s\n", bot->name, bot->core->tick,
                lua_tostring(bot->co, -1));
        bot->errored = 1;
        bot->fail++;
        if( !bot->finished )
        {
            bot->finished = 1;
            bot->exit_code = 1;
        }
    }
    bot->state = SCRIPTBOT_DONE;
}

/* Settle the armed await: "ok" when its edge or level came, else "timeout". */
static void
bot_settle(
    struct ScriptBot* bot,
    int satisfied)
{
    if( bot->await_level_ref != LUA_NOREF )
        luaL_unref(bot->L, LUA_REGISTRYINDEX, bot->await_level_ref);
    if( bot->await_match_ref != LUA_NOREF )
        luaL_unref(bot->L, LUA_REGISTRYINDEX, bot->await_match_ref);
    bot->await_level_ref = LUA_NOREF;
    bot->await_match_ref = LUA_NOREF;
    bot->await_kind = 0;
    bot->state = SCRIPTBOT_DONE;
    if( satisfied )
    {
        lua_pushstring(bot->co, "ok");
        lua_pushnil(bot->co);
    }
    else
    {
        lua_pushstring(bot->co, "timeout");
        lua_pushstring(bot->co, bot->await_note);
    }
    bot_resume(bot, 2);
}

/* The client's pump reads at most this many events a frame
 * (drive_pump_once's `struct App_DriveEvent events[32]`). */
enum
{
    SCRIPTRUN_PUMP_BATCH = 32
};

/*
 * ONE PUMP: the client's drive_pump_once (torirs_plugin_drive.c), step for
 * step. Read the next batch past the bot's cursor; offer it in order to the
 * armed await; when an event settles it and the resume arms a NEW await, offer
 * that one the rest of the SAME batch (the client's R3) -- so an event read
 * before the new await was armed can settle it, as it does live. Then the
 * (possibly new) await's level, then its deadline. 1 when it resumed.
 *
 * Before this each await only saw events raised after its own call, so a
 * script that answered a dialogue and then awaited the next server_tick
 * waited one tick longer on scriptrun than live, where that tick's fence sat
 * later in the batch that settled the dialogue: the Maiden leader's first
 * gear switch landed a tick after the live leader's (seed sa, 2026-10-09),
 * and every action after it sat a tick late against her clock.
 */
static int
bot_step_once(struct ScriptBot* bot)
{
    struct ScriptrunCore* c = bot->core;
    int from;
    int batch_end;
    int resumed = 0;

    if( bot->state == SCRIPTBOT_START )
    {
        /* An on-demand Play's cursor starts at the ring's newest entry
         * (drive_demand_begin): what came before is not this script's. */
        bot->event_cursor = c->event_serial;
        bot->state = SCRIPTBOT_DONE; /* until it yields */
        bot_resume(bot, 3);
        return 1;
    }

    from = bot->event_cursor + 1;
    if( from < c->event_serial - SCRIPTRUN_EVENTS + 1 )
        from = c->event_serial - SCRIPTRUN_EVENTS + 1;
    batch_end = c->event_serial;
    if( batch_end - from + 1 > SCRIPTRUN_PUMP_BATCH )
        batch_end = from + SCRIPTRUN_PUMP_BATCH - 1;
    if( batch_end >= from )
        bot->event_cursor = batch_end;

    if( bot->state != SCRIPTBOT_AWAIT )
        return 0;

    /* Predicates run on the main state: they are plain functions. */
    for( int serial = from; serial <= batch_end; serial++ )
    {
        struct ScriptrunEvent const* ev = &c->events[serial % SCRIPTRUN_EVENTS];
        int matched;

        if( bot->state != SCRIPTBOT_AWAIT )
            return resumed;
        if( bot->await_match_ref == LUA_NOREF )
            continue;
        if( bot->await_kind >= 0 && ev->kind != bot->await_kind )
            continue;
        lua_rawgeti(bot->L, LUA_REGISTRYINDEX, bot->await_match_ref);
        push_event(bot->L, ev);
        if( lua_pcall(bot->L, 1, 1, 0) != LUA_OK )
        {
            char const* error = lua_tostring(bot->L, -1);
            fprintf(stdout, "[%s t%d] NOTE   await match error: %s\n", bot->name, c->tick,
                    error ? error : "?");
            lua_pop(bot->L, 1);
            continue;
        }
        matched = lua_toboolean(bot->L, -1);
        lua_pop(bot->L, 1);
        if( matched )
        {
            char how[64];
            snprintf(how, sizeof(how), "event %s %d/%d", EVENT_NAMES[ev->kind], serial - from + 1,
                     batch_end - from + 1);
            await_trace(bot, how, bot->await_note);
            bot_settle(bot, 1);
            resumed = 1;
        }
    }
    if( bot->state != SCRIPTBOT_AWAIT )
        return resumed;

    if( bot->await_level_ref != LUA_NOREF )
    {
        int satisfied = 0;
        lua_rawgeti(bot->L, LUA_REGISTRYINDEX, bot->await_level_ref);
        if( lua_pcall(bot->L, 0, 1, 0) != LUA_OK )
        {
            char const* error = lua_tostring(bot->L, -1);
            fprintf(stdout, "[%s t%d] NOTE   await level error: %s\n", bot->name, c->tick,
                    error ? error : "?");
        }
        else
            satisfied = lua_toboolean(bot->L, -1);
        lua_pop(bot->L, 1);
        if( satisfied )
        {
            await_trace(bot, "level", bot->await_note);
            bot_settle(bot, 1);
            return 1;
        }
    }
    if( c->tick >= bot->await_deadline )
    {
        await_trace(bot, "timeout", bot->await_note);
        bot_settle(bot, 0);
        return 1;
    }
    return resumed;
}

/* The client pumps once a frame and runs several frames to a server tick,
 * so within a tick a bot is pumped again while it resumes, or while events
 * are left past its cursor (more than one batch arrived) -- bounded. */
static void
bot_step(struct ScriptBot* bot)
{
    for( int pass = 0; pass < 32; pass++ )
    {
        int resumed = bot_step_once(bot);
        if( !resumed && bot->event_cursor >= bot->core->event_serial )
            break;
    }
}

/* ----------------------------------------------- the collision check */

/*
 * The bot's collision against the server's, word for word, over every tile
 * both hold. Run once per scene (--check-collision). A mismatch is a tile the
 * bot would route through, or around, differently from the server.
 */
static void
check_collision(
    struct ScriptRun* run,
    struct ScriptBot* bot)
{
    struct ScriptrunCore const* c = bot->core;
    int base_x = c->world->_base_tile_x;
    int base_z = c->world->_base_tile_z;

    if( !c->have_collision )
    {
        fprintf(stdout, "[%s] collision check: no collision built (zone %d,%d)\n", bot->name,
                c->rebuild_zone_x, c->rebuild_zone_z);
        return;
    }
    fprintf(stdout,
            "[%s] collision check: zone %d,%d base %d,%d, %d squares, %d locs placed; server base "
            "%d,%d\n",
            bot->name, c->rebuild_zone_x, c->rebuild_zone_z, base_x, base_z, c->collision_squares,
            c->collision_locs, ToriRSServer_SceneBaseX(), ToriRSServer_SceneBaseZ());
    for( int level = 0; level < COLLISION_LEVELS; level++ )
    {
        int compared = 0, words = 0, walk = 0, shown = 0;
        int bot_blocked = 0, srv_blocked = 0;
        int only_server = 0, only_bot = 0;
        int bits_server[32] = { 0 }, bits_bot[32] = { 0 };

        for( int sx = 0; sx < 104; sx++ )
        {
            for( int sz = 0; sz < 104; sz++ )
            {
                int x = base_x + sx, z = base_z + sz;
                int mine, theirs, my_walk, their_walk;
                if( !ToriRSServer_SceneContains(x, z) )
                    continue;
                mine = ScriptrunCore_CollisionFlags(c, level, x, z);
                theirs = ToriRSServer_SceneTileFlags(level, x, z);
                if( mine < 0 )
                    continue;
                compared++;
                my_walk = (mine & COLL_FLAG_WALK_BLOCKED) != 0;
                their_walk = ToriRSServer_SceneWalkBlocked(level, x, z);
                bot_blocked += my_walk;
                srv_blocked += their_walk;
                if( mine != theirs )
                {
                    words++;
                    for( int b = 0; b < 31; b++ )
                    {
                        int m = (mine >> b) & 1, t = (theirs >> b) & 1;
                        if( t && !m )
                            bits_server[b]++;
                        if( m && !t )
                            bits_bot[b]++;
                    }
                }
                if( my_walk != their_walk )
                {
                    walk++;
                    if( their_walk )
                        only_server++;
                    else
                        only_bot++;
                    if( shown < 6 )
                    {
                        fprintf(stdout, "    level %d tile %d,%d: bot %s (0x%06x) server %s (0x%06x)\n",
                                level, x, z, my_walk ? "BLOCKED" : "open", mine,
                                their_walk ? "BLOCKED" : "open", theirs);
                        /* The bot's locs within 7 tiles, to name the stamp. */
                        for( int li = 0; li < c->loc_count && shown == 0; li++ )
                        {
                            struct ScriptrunLoc const* l = &c->locs[li];
                            if( l->x <= x && l->x > x - 7 && l->z <= z && l->z > z - 7 &&
                                (l->level == level || l->level == level + 1) && l->shape == 10 )
                                fprintf(stdout, "      bot loc %d '%s' at %d,%d,%d shape %d angle %d%s\n",
                                        l->loc_id,
                                        ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_LOC, l->loc_id)
                                            ? ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_LOC, l->loc_id) : "?",
                                        l->x, l->z, l->level, l->shape, l->angle,
                                        l->loc_id < 0 ? " (removed)" : "");
                        }
                        shown++;
                    }
                }
            }
        }
        fprintf(stdout,
                "  level %d: %d tiles compared, walk-blocked bot %d / server %d, %d walk "
                "mismatches (server-only %d, bot-only %d), %d differing collision words\n",
                level, compared, bot_blocked, srv_blocked, walk, only_server, only_bot, words);
        for( int b = 0; b < 31; b++ )
            if( bits_server[b] || bits_bot[b] )
                fprintf(stdout, "    bit 0x%06x: set by server only on %d tiles, by bot only on %d\n",
                        1 << b, bits_server[b], bits_bot[b]);
    }
    fflush(stdout);
    (void)run;
}

/* ------------------------------------------------------------ the feed */

static void
scriptrun_sink(
    struct ToriRSServerPlayer* player,
    int pkt_name,
    const uint8_t* payload,
    int len,
    void* ctx)
{
    struct ScriptRun* run = (struct ScriptRun*)ctx;
    static int debug = -1;

    if( debug < 0 )
    {
        char const* v = getenv("TORIRS_SCRIPTRUN_DEBUG");
        debug = v && *v && *v != '0';
    }
    for( int i = 0; i < run->count; i++ )
    {
        if( run->bots[i].player == player && run->bots[i].core )
        {
            int unhandled = run->bots[i].core->unhandled;
            ScriptrunCore_Packet(run->bots[i].core, pkt_name, payload, len);
            if( debug )
            {
                fprintf(stderr, "scriptrun: %s <- %s (%d bytes)%s", run->bots[i].name,
                        ToriRSServer_WirePktName(pkt_name), len,
                        run->bots[i].core->unhandled != unhandled ? " UNPARSED" : "");
                if( run->bots[i].core->unhandled != unhandled )
                    for( int b = 0; b < len && b < 24; b++ )
                        fprintf(stderr, " %02x", payload[b]);
                fprintf(stderr, "\n");
            }
            return;
        }
    }
}

/* ------------------------------------------------------------ the run */

/* Copy the seat's fixture to the bot's save path. The path is taken as given,
 * else under the script root, else under its tests/raids/fixtures (a test
 * table's own `fixture` field is relative to that). */
static int
fixture_install(
    struct ScriptRun const* run,
    int seat,
    char const* bot_name,
    char const* script_root)
{
    char pattern[512];
    char path[1024];
    char const* dest = ToriRSServer_SavePath(bot_name);
    FILE* in = NULL;
    FILE* out;
    char* at;

    snprintf(pattern, sizeof(pattern), "%s", run->fixture);
    at = strstr(pattern, "{seat}");
    if( at )
    {
        char tail[512];
        snprintf(tail, sizeof(tail), "%s", at + 6);
        snprintf(at, sizeof(pattern) - (size_t)(at - pattern), "%d%s", seat, tail);
    }
    for( int i = 0; i < 3 && !in; i++ )
    {
        if( i == 0 )
            snprintf(path, sizeof(path), "%s", pattern);
        else if( i == 1 )
            snprintf(path, sizeof(path), "%s/%s", script_root, pattern);
        else
            snprintf(path, sizeof(path), "%s/tests/raids/fixtures/%s", script_root, pattern);
        in = fopen(path, "rb");
    }
    if( !in )
    {
        fprintf(stderr, "scriptrun: fixture %s not found (as given, under %s, or its tests/raids/fixtures)\n",
                pattern, script_root);
        return 0;
    }
    assert(dest && *dest);
    out = fopen(dest, "wb");
    if( !out )
    {
        fclose(in);
        fprintf(stderr, "scriptrun: cannot write %s\n", dest);
        return 0;
    }
    /* tools/quest_gate/run.py write_session_fixture: the first `name = ...`
     * line becomes the seat's account, or every seat logs in under the
     * fixture's one name and no raider can find another by name. */
    {
        int renamed = 0;
        char line[1024];
        while( fgets(line, sizeof(line), in) )
        {
            if( !renamed && strncmp(line, "name", 4) == 0 )
            {
                char const* rest = line + 4;
                while( *rest == ' ' || *rest == '\t' )
                    rest++;
                if( *rest == '=' )
                {
                    fprintf(out, "name = %s\n", bot_name);
                    renamed = 1;
                    continue;
                }
            }
            fputs(line, out);
        }
        fclose(in);
        fclose(out);
        if( !renamed )
        {
            fprintf(stderr, "scriptrun: fixture %s has no `name = ...` line to rewrite\n", path);
            return 0;
        }
    }
    fprintf(stderr, "scriptrun: %s logs in on %s\n", bot_name, path);
    return 1;
}

int
ToriRSServer_ScriptRun(
    struct ToriRSServer* srv,
    const struct ToriRSServerBootConfig* config,
    int argc,
    char** argv)
{
    static struct ScriptRun run;
    char const* name = "srun";
    int bots = 1;
    int ticks_max = 6000;
    int64_t started;
    int ticks_run = 0;
    int status = 0;

    assert(srv);
    assert(config);
    memset(&run, 0, sizeof(run));
    run.srv = srv;
    run.script_root = "script";
    if( argc < 3 )
    {
        fprintf(stderr, "usage: torirsserver --scriptrun <test.lua> [--bots N] [--name NAME] "
                        "[--ticks N] [--script-root DIR] [--session DIR] [--quiet]\n");
        return 2;
    }
    run.test_path = argv[2];
    for( int i = 3; i < argc; i++ )
    {
        if( strcmp(argv[i], "--bots") == 0 && i + 1 < argc )
            bots = atoi(argv[++i]);
        else if( strcmp(argv[i], "--name") == 0 && i + 1 < argc )
            name = argv[++i];
        else if( strcmp(argv[i], "--ticks") == 0 && i + 1 < argc )
            ticks_max = atoi(argv[++i]);
        else if( strcmp(argv[i], "--script-root") == 0 && i + 1 < argc )
            run.script_root = argv[++i];
        else if( strcmp(argv[i], "--session") == 0 && i + 1 < argc )
            run.session_dir = argv[++i];
        else if( strcmp(argv[i], "--fixture") == 0 && i + 1 < argc )
            run.fixture = argv[++i];
        else if( strcmp(argv[i], "--quiet") == 0 )
            run.quiet = 1;
        else if( strcmp(argv[i], "--check-collision") == 0 )
            run.check_collision = 1;
    }
    if( bots < 1 || bots > SCRIPTRUN_BOTS_MAX )
    {
        fprintf(stderr, "scriptrun: --bots must be 1..%d\n", SCRIPTRUN_BOTS_MAX);
        return 2;
    }
    run.test_source = read_file(run.test_path, NULL);
    if( !run.test_source )
    {
        fprintf(stderr, "scriptrun: cannot read %s\n", run.test_path);
        return 2;
    }
    /* The bots decode revision 239 (the content's own revision), so the world
     * writes 239's bytes whatever the standalone server's default wire is. */
    run.rev = GameProtoRev_OSRS239();
    assert(run.rev);
    srv->wire = ToriRSServer_WireByName("osrs239");
    assert(srv->wire);

    /* The client's own cache, for the map files its scene is built from. The
     * map keys (xteas.json) are rscache-global and the server's scene build
     * has loaded them already. */
    {
        struct RSCache profile = RSCache_ProfileZero();
        char alt[1024];
        run.cache_disk = RSCache_Dat2DiskNewFromDirectory(config->cache_dir);
        if( !run.cache_disk )
        {
            snprintf(alt, sizeof(alt), "../%s", config->cache_dir);
            run.cache_disk = RSCache_Dat2DiskNewFromDirectory(alt);
        }
        if( run.cache_disk )
        {
            profile.game = RSCACHE_GAME_OLDSCHOOL;
            profile.epoch = RSCACHE_EPOCH_DAT2;
            profile.revision = TORIRSSERVER_CACHE_REVISION;
            RSCache_Dat2DiskSetProfile(run.cache_disk, &profile);
        }
        else
            fprintf(stderr, "scriptrun: no cache at %s: the bots have no collision\n",
                    config->cache_dir);
    }
    if( run.session_dir )
    {
        char ledger_path[1100];
        snprintf(ledger_path, sizeof(ledger_path), "%s/ledger.tsv", run.session_dir);
        run.ledger = fopen(ledger_path, "w");
    }

    /* The fixture: the real runner logs each seat in on the test's save
     * (tools/raid_gate/run.py). The saves live in the session, never in the
     * shared `saves`, so a run neither reads nor writes anyone's account. */
    if( run.fixture )
    {
        static char saves[1024];
        if( !run.session_dir )
        {
            fprintf(stderr, "scriptrun: --fixture needs --session (the saves live there)\n");
            status = 2;
            goto out;
        }
        snprintf(saves, sizeof(saves), "%s/saves", run.session_dir);
        mkdir(saves, 0755);
        setenv("TORIRSSERVER_SAVES", saves, 1);
    }

    /* The feed first: the login burst is the first thing a client decodes. */
    srv->packet_sink = scriptrun_sink;
    srv->packet_sink_ctx = &run;

    /*
     * THE GATE'S WORLD, so the same --name is the same run on both lanes
     * (the owner, 2026-10-09: "Given the same seeds, you should expect the
     * scriptrun and the live client runner's ticklog to be identical"):
     *
     * - the bots are named as tools/quest_gate/run.py party_accounts names a
     *   party's accounts -- the run name's save-file stem, at most nine
     *   characters, then _p<seat> -- because a player's random stream is
     *   keyed by its name (ToriRSServer_WorldPlayerRandom);
     * - the npc run seed is the first account's name, as run.py exports it
     *   (TORIRSSERVER_RUN_NAME), read by ToriRSServer_WorldInit;
     * - the world is built as the embed builds it (torirs_server_embed.c):
     *   the global npc population included, so every npc slot the raid's
     *   spawns take is the slot the live world hands out.
     * Before this the scriptrun world was never WorldInit'd: no run seed
     * (the legacy tile-and-life streams), ten npcs where the live world had
     * a thousand, and bots named <name><seat>.
     */
    {
        static char stem[10];
        static char leader[32];
        int n = 0;
        for( char const* at = name; *at && n < 9; at++ )
        {
            unsigned char c = (unsigned char)*at;
            if( isalnum(c) )
                stem[n++] = (char)tolower(c);
            else if( c == '_' || c == '-' || c == ' ' )
                stem[n++] = '_';
        }
        stem[n] = '\0';
        snprintf(leader, sizeof(leader), "%s_p1", stem);
        setenv("TORIRSSERVER_RUN_NAME", leader, 1);
        ToriRSServer_WorldInit(srv, ToriRSServer_BootZone(config->home_x),
                               ToriRSServer_BootZone(config->home_z));
        for( int i = 0; i < bots; i++ )
            snprintf(run.bots[i].name, sizeof(run.bots[i].name), "%s_p%d", stem, i + 1);
    }

    for( int i = 0; i < bots; i++ )
    {
        struct ScriptBot* bot = &run.bots[i];
        struct ToriRSServerPlayer* player;

        bot->run = &run;
        bot->index = i;
        bot->tab = SCRIPTRUN_TAB_INVENTORY;
        bot->core = ScriptrunCore_New(run.rev);
        bot->core->cache_disk = run.cache_disk;
        run.count = i + 1;
        player = ToriRSServer_WorldAddPlayer(srv, NULL);
        assert(player);
        bot->player = player;
        /* What the login response tells a client, before anything else: its
         * slot in the GPI namespace (torirs_server_wire.h). */
        ScriptrunCore_SetLocalIndex(bot->core, ToriRSServer_WirePlayerIndex(player->pid));
        ToriRSServer_WorldPlayerInit(player);
        ToriRSServer_WorldSetDisplayName(player, bot->name);
        if( run.fixture && !fixture_install(&run, i + 1, bot->name, run.script_root) )
        {
            status = 2;
            goto out;
        }
        srv->active_player = player;
        ToriRSServer_WorldLogin(player);
        scene_ack(player);
        fprintf(stderr, "scriptrun: %s is pid %d at %d,%d\n", bot->name, player->pid, player->x,
                player->z);
    }
    for( int i = 0; i < run.count; i++ )
    {
        if( !bot_lua_init(&run.bots[i]) )
        {
            status = 2;
            goto out;
        }
    }

    started = now_ms();
    for( int t = 0; t < ticks_max; t++ )
    {
        int live = 0;

        for( int i = 0; i < run.count; i++ )
        {
            struct ScriptrunCore* c = run.bots[i].core;
            ScriptrunCore_BeginTick(c, (int)srv->tick);
            /* A scene's check waits a few ticks: its zone loc packets (runtime
             * spawns, doors) follow the rebuild. */
            if( run.check_collision && c->have_scene &&
                (c->rebuild_zone_x != run.checked_zone_x[i] ||
                 c->rebuild_zone_z != run.checked_zone_z[i]) )
            {
                run.checked_zone_x[i] = c->rebuild_zone_x;
                run.checked_zone_z[i] = c->rebuild_zone_z;
                run.check_due[i] = c->tick + 3;
            }
            if( run.check_due[i] && c->tick >= run.check_due[i] )
            {
                run.check_due[i] = 0;
                check_collision(&run, &run.bots[i]);
            }
        }
        for( int i = 0; i < run.count; i++ )
        {
            struct ScriptBot* bot = &run.bots[i];
            scene_ack(bot->player);
            if( !bot->finished )
                bot_step(bot);
            if( !bot->finished && bot->state != SCRIPTBOT_DONE )
                live++;
        }
        if( live == 0 )
            break;
        /* A party Play ends with its leader (the Scripts tab's Stop ends the
         * members' clients): a member parked on a barrier the leader will
         * never reach would otherwise run out its whole deadline. */
        if( run.count > 1 && (run.bots[0].finished || run.bots[0].state == SCRIPTBOT_DONE) )
        {
            fprintf(stderr, "scriptrun: the leader %s ended at tick %d; the party ends with it\n",
                    run.bots[0].name, run.bots[0].core->tick);
            break;
        }
        for( int i = 0; i < run.count; i++ )
            flush_inputs(&run.bots[i]);
        ToriRSServer_WorldTick(srv);
        ticks_run++;
    }
    if( getenv("TORIRS_SCRIPTRUN_DEBUG") )
    {
        for( int i = 0; i < run.count; i++ )
        {
            struct ScriptrunCore* c = run.bots[i].core;
            struct World_EntityPool* pp = &c->world->entities.player;
            struct World_EntityPool* np = &c->world->entities.npc;
            fprintf(stderr, "scriptrun: %s (server truth %d,%d) perceives: scene %d zone %d,%d base %d,%d local_pid %d "
                            "players %d npcs %d msgs %d invs:",
                    run.bots[i].name, run.bots[i].player->x, run.bots[i].player->z,
                    c->have_scene, c->rebuild_zone_x, c->rebuild_zone_z,
                    c->world->_base_tile_x, c->world->_base_tile_z, c->esync->local_pid, pp->count,
                    np->count, c->message_serial);
            for( int k = 0; k < SCRIPTRUN_INVS; k++ )
                if( c->invs[k].inv_id >= 0 )
                    fprintf(stderr, " %d(size %d)", c->invs[k].inv_id, c->invs[k].size);
            fprintf(stderr, "\n");
            for( int k = World_EntityPoolHead(pp); k != WORLD_ENTITY_NIL; k = World_EntityPoolNext(pp, k) )
            {
                struct WorldEntity_Player* p = World_EntityPoolGet(pp, k);
                fprintf(stderr, "  player pid %d '%s' route %d,%d len %d grid %d,%d\n", p->server_pid,
                        p->name, p->pathing.route_x[0], p->pathing.route_z[0], p->pathing.route_length,
                        p->grid_position.x, p->grid_position.z);
            }
            for( int m = c->message_serial; m > 0 && m > c->message_serial - 6; m-- )
                fprintf(stderr, "  msg %s\n", c->messages[m % SCRIPTRUN_MESSAGES].text);
        }
    }
    {
        int64_t ms = now_ms() - started;
        int failed = 0;
        for( int i = 0; i < run.count; i++ )
        {
            struct ScriptBot* bot = &run.bots[i];
            if( !bot->finished )
                fprintf(stdout, "[%s] UNFINISHED (await '%s' at tick %d)\n", bot->name,
                        bot->await_note, bot->core->tick);
            fprintf(stdout, "[%s] rows %d pass %d fail %d exit %d packets %d\n", bot->name,
                    bot->rows, bot->pass, bot->fail, bot->exit_code, bot->core->packets);
            if( !bot->finished || bot->fail > 0 || bot->exit_code != 0 )
                failed = 1;
        }
        fprintf(stdout, "SCRIPTRUN %s: %d ticks in %lld ms (%.0f ticks/s, %.1fx real time)\n",
                failed ? "FAIL" : "PASS", ticks_run, (long long)ms,
                ms > 0 ? ticks_run * 1000.0 / (double)ms : 0.0,
                ms > 0 ? ticks_run * 600.0 / (double)ms : 0.0);
        status = failed ? 1 : 0;
    }
out:
    srv->packet_sink = NULL;
    srv->packet_sink_ctx = NULL;
    for( int i = 0; i < run.count; i++ )
    {
        if( run.bots[i].L )
            lua_close(run.bots[i].L);
        ScriptrunCore_Free(run.bots[i].core);
    }
    for( int i = 0; i < run.barrier_count; i++ )
        free(run.barriers[i]);
    free(run.test_source);
    if( run.ledger )
        fclose(run.ledger);
    if( run.cache_disk )
        RSCache_Dat2DiskFree(run.cache_disk);
    return status;
}
