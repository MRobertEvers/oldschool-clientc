/*
 * quest-driver: the server's tick, and its tick log, read by a test.
 *
 * Owner: the raid seam (docs/RAID_ORCHESTRATOR.md section 4, rows
 * `player.step_tick` and `tick.log`). The Lua half is
 * script/plugins/quest_driver/ticklog.lua (t.ticklog.*), core.lua (t.tick)
 * and pointer.lua (t.player.step_tick).
 *
 * Why a test needs the SERVER's tick and not the one api_drive.tick answers:
 * api_drive.tick is the client's world cycle divided by 30, a clock that
 * advances at the client's frame pace and starts wherever the client's
 * started. It is the right unit for a deadline. It is the wrong clock for "the
 * step was issued on tick T and resolved on tick T+1", and for a ledger row
 * that says "Maiden swung on ticks 9, 19, 29": those are the numbers the
 * server's own phase order (ToriRSServer_WorldTick) produces, and only
 * `srv->tick` states them.
 *
 * The log itself is the server's (src/torirsserver/torirs_server_ticklog.c);
 * this file only turns it on, reads it back and marks it. Every verb answers
 * `unsupported` on a socket-server run, which has no world in this process
 * (PluginDrive_EmbedWorld).
 */

#include "plugin/torirs_plugin_drive.h"

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER

#include "plugin/torirs_plugin_lua.h"
#include "torirsserver/torirs_server.h"

#include "lauxlib.h"
#include "lua.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Registered from drive_install_modules (torirs_plugin_drive.c), which
 * declares it beside the call: torirs_plugin_drive.h belongs to another
 * owner. */
void PluginDriveTicklog_RegisterLua(struct lua_State* L, void* script);

/* A read returns at most this many rows per call; ticklog.lua pages. */
#define DRIVE_TICKLOG_READ_MAX 8192

/* api.drive.server_tick() -> "ok", tick | "unsupported", nil */
static int
lua_drive_server_tick(struct lua_State* L)
{
    struct ToriRSServer* srv = PluginDrive_EmbedWorld();

    if( !srv )
    {
        lua_pushstring(L, DriveResultName(DRIVE_UNSUPPORTED));
        lua_pushnil(L);
        return 2;
    }
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_pushinteger(L, (lua_Integer)srv->tick);
    return 2;
}

static void
drive_ticklog_push_state(
    struct lua_State* L,
    struct ToriRSServer* srv)
{
    char const* path = ToriRSServer_TicklogPath();

    lua_createtable(L, 0, 4);
    lua_pushinteger(L, (lua_Integer)ToriRSServer_TicklogStartTick());
    lua_setfield(L, -2, "start_tick");
    lua_pushinteger(L, (lua_Integer)srv->tick);
    lua_setfield(L, -2, "tick");
    lua_pushinteger(L, (lua_Integer)ToriRSServer_TicklogCount());
    lua_setfield(L, -2, "serial");
    if( path )
    {
        lua_pushstring(L, path);
        lua_setfield(L, -2, "path");
    }
}

/* api.drive.ticklog_start() -> "ok", {start_tick, tick, serial, path}
 * Idempotent: a second call keeps the rows and answers the same start. The
 * rows also go to <session dir>/ticklog.tsv when the run has one. */
static int
lua_drive_ticklog_start(struct lua_State* L)
{
    struct ToriRSServer* srv = PluginDrive_EmbedWorld();
    char const* dir = DriveCore_SessionDir();
    char path[1024];

    if( !srv )
    {
        lua_pushstring(L, DriveResultName(DRIVE_UNSUPPORTED));
        lua_pushnil(L);
        return 2;
    }
    path[0] = '\0';
    if( dir )
        snprintf(path, sizeof(path), "%s/ticklog.tsv", dir);
    ToriRSServer_TicklogEnable(srv, path[0] ? path : NULL);
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    drive_ticklog_push_state(L, srv);
    return 2;
}

static void
drive_ticklog_push_row(
    struct lua_State* L,
    const struct ToriRSServerTicklogRow* row)
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
    /* The seventh field: HIT_PLAYER's raw (torirs_server.h, RAW DAMAGE),
     * 0 on every other kind. ticklog.lua names it through _RAW[7]. */
    lua_pushinteger(L, (lua_Integer)row->g);
    lua_setfield(L, -2, "g");
    if( row->label[0] )
    {
        lua_pushstring(L, row->label);
        lua_setfield(L, -2, "label");
    }
}

/* api.drive.ticklog([after_serial], [max], [kind], [slot]) -> "ok", {rows...,
 * next_serial, serial, tick} | "refused", nil (the log is off) |
 * "unsupported", nil.
 * Rows are raw {serial, tick, kind, a..g, label}; ticklog.lua names the
 * fields per kind. `kind` (a row kind's name) and `slot` (an npc world slot)
 * filter in C: a Lumbridge run logs ten thousand npc_tile rows in two hundred
 * ticks, and turning each into a Lua table to throw it away spent the
 * driver's whole per-resume instruction budget. `next_serial` is the last
 * serial scanned, matched or not: the serial to pass next time. */
static int
lua_drive_ticklog(struct lua_State* L)
{
    struct ToriRSServer* srv = PluginDrive_EmbedWorld();
    int after = PluginDrive_ArgOptInt(L, 1, 0);
    int max = PluginDrive_ArgOptInt(L, 2, DRIVE_TICKLOG_READ_MAX);
    int slot = PluginDrive_ArgOptInt(L, 4, -1);
    int kind = -1;
    struct ToriRSServerTicklogRow* rows;
    int count;
    uint32_t next_serial;

    /* Not luaL_checkstring: the argument is optional, and check_drive_abi
     * reads a luaL_check* index as the caller's required floor. */
    if( !lua_isnoneornil(L, 3) )
    {
        if( lua_type(L, 3) != LUA_TSTRING )
            return luaL_error(L, "drive.ticklog: kind wants a row kind's name or nil");
        kind = ToriRSServer_TicklogKindFromName(lua_tostring(L, 3));
        if( kind < 0 )
            return luaL_error(L, "drive.ticklog: no row kind '%s'", lua_tostring(L, 3));
    }

    if( !srv )
    {
        lua_pushstring(L, DriveResultName(DRIVE_UNSUPPORTED));
        lua_pushnil(L);
        return 2;
    }
    if( !ToriRSServer_TicklogEnabled(srv) )
    {
        lua_pushstring(L, DriveResultName(DRIVE_REFUSED));
        lua_pushnil(L);
        return 2;
    }
    if( after < 0 )
        return luaL_error(L, "drive.ticklog: after_serial %d is negative", after);
    if( max <= 0 || max > DRIVE_TICKLOG_READ_MAX )
        max = DRIVE_TICKLOG_READ_MAX;
    rows = malloc((size_t)max * sizeof(*rows));
    assert(rows);
    count = ToriRSServer_TicklogReadFiltered((uint32_t)after, kind, slot, rows, max,
                                             &next_serial);
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, count, 3);
    for( int i = 0; i < count; i++ )
    {
        drive_ticklog_push_row(L, &rows[i]);
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

/* api.drive.ticklog_mark(label) -> "ok", {serial, tick} | "refused", nil */
static int
lua_drive_ticklog_mark(struct lua_State* L)
{
    struct ToriRSServer* srv = PluginDrive_EmbedWorld();
    char const* label = PluginDrive_ArgString(L, 1);
    uint32_t serial;

    if( !srv )
    {
        lua_pushstring(L, DriveResultName(DRIVE_UNSUPPORTED));
        lua_pushnil(L);
        return 2;
    }
    if( !ToriRSServer_TicklogEnabled(srv) )
    {
        lua_pushstring(L, DriveResultName(DRIVE_REFUSED));
        lua_pushnil(L);
        return 2;
    }
    serial = ToriRSServer_TicklogMark(label);
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, (lua_Integer)serial);
    lua_setfield(L, -2, "serial");
    lua_pushinteger(L, (lua_Integer)srv->tick);
    lua_setfield(L, -2, "tick");
    return 2;
}

/* api.drive.server_npc_slot(client_slot) -> "ok", world slot | "not_found",
 * nil | "unsupported", nil.
 *
 * The npc index a client row carries (t.npc.nearest's `slot`) is NOT the
 * server's npc slot: NPC_INFO names each npc by a per-client slot
 * (struct ToriRSServerPlayerSlotMap), so a world pool larger than the wire
 * field never aliases. Every tick-log row is keyed by the WORLD slot, so a
 * test that found its goblin on the client asks this for the slot the log
 * knows it by. Read through the first logged-in player, which in an
 * embedded quest run is the one being driven. `not_found` once the client
 * has no name for it any more (the npc despawned from this client), so ask
 * before the fight, not after the death. */
static int
lua_drive_server_npc_slot(struct lua_State* L)
{
    struct ToriRSServer* srv = PluginDrive_EmbedWorld();
    int client_slot = PluginDrive_ArgInt(L, 1);
    int world_slot = -1;

    if( !srv )
    {
        lua_pushstring(L, DriveResultName(DRIVE_UNSUPPORTED));
        lua_pushnil(L);
        return 2;
    }
    if( client_slot < 0 || client_slot >= TORIRSSERVER_CLIENT_NPC_SLOTS )
        return luaL_error(L, "drive.server_npc_slot: client slot %d is out of range",
                          client_slot);
    for( int pid = 0; pid < srv->player_count; pid++ )
    {
        if( !srv->players[pid].active )
            continue;
        world_slot = ToriRSServer_SlotMapWorld(&srv->players[pid], client_slot);
        break;
    }
    if( world_slot < 0 )
    {
        lua_pushstring(L, DriveResultName(DRIVE_NOT_FOUND));
        lua_pushnil(L);
        return 2;
    }
    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_pushinteger(L, (lua_Integer)world_slot);
    return 2;
}

static struct LuaFn const LUA_DRIVE_TICKLOG_FNS[] = {
    {"server_npc_slot", lua_drive_server_npc_slot},
    {"server_tick", lua_drive_server_tick},
    {"ticklog_start", lua_drive_ticklog_start},
    {"ticklog", lua_drive_ticklog},
    {"ticklog_mark", lua_drive_ticklog_mark},
    {NULL, NULL},
};

void
PluginDriveTicklog_RegisterLua(struct lua_State* L, void* script)
{
    assert(L);
    assert(script);
    PluginLua_AppendModule(L, script, LUA_DRIVE_TICKLOG_FNS);
}

#else /* !TORIRS_EMBED_SERVER */

/* No embedded server, no driver: nothing here is reachable. ISO C wants a
 * translation unit to declare something. */
typedef int PluginDriveTicklog_Unused;

#endif /* TORIRS_EMBED_SERVER */
