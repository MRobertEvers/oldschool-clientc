/*
 * quest-driver: line of sight and the pack, read from the embedded server.
 *
 * Owner: the waves loop's seam `los_and_pack`
 * (docs/minigames/waves_loop/SEAM_TRIAGE_2026-10-03b.md). The Lua half is
 * script/plugins/quest_driver/world.lua (t.world.los, t.npc.pack); the
 * server half is src/torirsserver/torirs_server_los_query.c, which asks the
 * server's own line routines with the arguments its own callers pass.
 *
 * Why the server and not the client: the client has collision for its own
 * pathing, but the question a wave test asks is "would the SERVER have let
 * that ranger fire this tick", and the only answer to that is the routine
 * the server runs. A client-side ray would agree until the day the two maps
 * differ (a loc the server changed and the client has not drawn yet), and
 * that is the day the test is wrong without saying so.
 *
 * Both verbs answer `unsupported` on a socket-server run, which has no world
 * in this process (PluginDrive_EmbedWorld).
 */

#include "plugin/torirs_plugin_drive.h"

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER

#include "plugin/torirs_plugin_lua.h"
#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_content.h"

#include "lauxlib.h"
#include "lua.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One pack read lists at most this many npcs, the NEAREST when more
 * qualify. The Inferno's busiest wave is under twenty; Lumbridge at radius
 * 104 holds more than 256. */
#define DRIVE_LOS_PACK_MAX 512

static struct ToriRSServerPackRow g_drive_los_pack[DRIVE_LOS_PACK_MAX];

/* The first logged-in player: in an embedded quest run, the one being
 * driven (lua_drive_server_npc_slot reads it the same way). -1 when nobody
 * is logged in yet. */
static int
drive_los_driven_pid(struct ToriRSServer* srv)
{
    assert(srv);
    for( int pid = 0; pid < srv->player_count; pid++ )
    {
        if( srv->players[pid].active )
            return pid;
    }
    return -1;
}

static void
drive_los_push_bool(
    struct lua_State* L,
    char const* name,
    int value)
{
    lua_pushboolean(L, value ? 1 : 0);
    lua_setfield(L, -2, name);
}

static void
drive_los_push_int(
    struct lua_State* L,
    char const* name,
    int value)
{
    lua_pushinteger(L, (lua_Integer)value);
    lua_setfield(L, -2, name);
}

/* api.drive.server_los(level, sx, sz, sw, sh, dx, dz, dw, dh)
 *   -> "ok", { line_of_sight, approached, line_of_walk, intersect, in_scene,
 *              gap, src_flags, dst_flags, blockers = {{x, z, flags}...},
 *              blockers_truncated, tick }
 *    | "unsupported", nil
 * Widths and heights below 1 raise: a footprint of nothing is a test bug. */
static int
lua_drive_server_los(struct lua_State* L)
{
    struct ToriRSServer* srv = PluginDrive_EmbedWorld();
    struct ToriRSServerLosReading reading;
    int level = PluginDrive_ArgInt(L, 1);
    int sx = PluginDrive_ArgInt(L, 2);
    int sz = PluginDrive_ArgInt(L, 3);
    int sw = PluginDrive_ArgInt(L, 4);
    int sh = PluginDrive_ArgInt(L, 5);
    int dx = PluginDrive_ArgInt(L, 6);
    int dz = PluginDrive_ArgInt(L, 7);
    int dw = PluginDrive_ArgInt(L, 8);
    int dh = PluginDrive_ArgInt(L, 9);

    if( !srv )
    {
        lua_pushstring(L, DriveResultName(DRIVE_UNSUPPORTED));
        lua_pushnil(L);
        return 2;
    }
    if( sw < 1 || sh < 1 || dw < 1 || dh < 1 )
        return luaL_error(L, "drive.server_los: footprint %dx%d -> %dx%d has a side below 1", sw,
                          sh, dw, dh);
    if( level < 0 || level > 3 )
        return luaL_error(L, "drive.server_los: level %d is out of range", level);
    ToriRSServer_LosQuery(level, sx, sz, sw, sh, dx, dz, dw, dh, &reading);

    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, 0, 12);
    drive_los_push_bool(L, "line_of_sight", reading.line_of_sight);
    drive_los_push_bool(L, "approached", reading.approached);
    drive_los_push_bool(L, "line_of_walk", reading.line_of_walk);
    drive_los_push_bool(L, "intersect", reading.intersect);
    drive_los_push_bool(L, "in_scene", reading.in_scene);
    drive_los_push_int(L, "gap", reading.gap);
    drive_los_push_int(L, "src_flags", reading.src_flags);
    drive_los_push_int(L, "dst_flags", reading.dst_flags);
    drive_los_push_bool(L, "blockers_truncated", reading.blockers_truncated);
    drive_los_push_int(L, "tick", srv->tick);
    lua_createtable(L, reading.blocker_count, 0);
    for( int i = 0; i < reading.blocker_count; i++ )
    {
        lua_createtable(L, 0, 3);
        drive_los_push_int(L, "x", reading.blockers[i].x);
        drive_los_push_int(L, "z", reading.blockers[i].z);
        drive_los_push_int(L, "flags", reading.blockers[i].flags);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "blockers");
    return 2;
}

static char const*
drive_los_target_kind_name(int kind)
{
    if( kind == TORIRSSERVER_PACK_TARGET_PLAYER )
        return "player";
    if( kind == TORIRSSERVER_PACK_TARGET_NPC )
        return "npc";
    return "none";
}

static void
drive_los_push_symbol(
    struct lua_State* L,
    char const* field,
    int type)
{
    char const* name = type >= 0 ? ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_NPC, type)
                                 : NULL;

    if( name )
        lua_pushstring(L, name);
    else
        lua_pushnil(L);
    lua_setfield(L, -2, field);
}

/* api.drive.server_npc_pack(radius[, only_slot])
 *   -> "ok", { rows = { {slot, client_slot, type, symbol, x, z, level, size,
 *              hitpoints, max_hitpoints, dying, attackrange, mode,
 *              target_kind, target_pid, target_slot, target_client_slot,
 *              target_type, target_symbol, target_via_mode, face_entity,
 *              walk_x, walk_z, anim_seq, anim_tick, sees_player, los_player,
 *              gap_player}... }, tick, pid, player_x, player_z, level }
 *    | "not_found", nil  (nobody is logged in)
 *    | "unsupported", nil
 * Nearest first. `only_slot` (a WORLD slot) reads that npc alone, which is
 * how t.world.los finds an npc's footprint without carrying the area across
 * the Lua boundary. `slot` is the WORLD slot the tick log keys by;
 * `client_slot` is the NPC_INFO slot a t.npc row carries (-1 when this
 * client has no name for it). */
static int
lua_drive_server_npc_pack(struct lua_State* L)
{
    struct ToriRSServer* srv = PluginDrive_EmbedWorld();
    int radius = PluginDrive_ArgInt(L, 1);
    int only_slot = PluginDrive_ArgOptInt(L, 2, -1);
    const struct ToriRSServerPlayer* player;
    int pid;
    int count;

    if( !srv )
    {
        lua_pushstring(L, DriveResultName(DRIVE_UNSUPPORTED));
        lua_pushnil(L);
        return 2;
    }
    if( radius < 0 || radius > 104 )
        return luaL_error(L, "drive.server_npc_pack: radius %d is out of range 0..104", radius);
    pid = drive_los_driven_pid(srv);
    if( pid < 0 )
    {
        lua_pushstring(L, DriveResultName(DRIVE_NOT_FOUND));
        lua_pushnil(L);
        return 2;
    }
    player = &srv->players[pid];
    if( only_slot < -1 || only_slot >= TORIRSSERVER_NPC_MAX )
        return luaL_error(L, "drive.server_npc_pack: slot %d is out of range", only_slot);
    count = ToriRSServer_NpcPackRead(srv, pid, radius, only_slot, g_drive_los_pack,
                                     DRIVE_LOS_PACK_MAX);

    lua_pushstring(L, DriveResultName(DRIVE_OK));
    lua_createtable(L, 0, 6);
    drive_los_push_int(L, "tick", srv->tick);
    drive_los_push_int(L, "pid", pid);
    drive_los_push_int(L, "player_x", player->x);
    drive_los_push_int(L, "player_z", player->z);
    drive_los_push_int(L, "level", player->level);
    lua_createtable(L, count, 0);
    for( int i = 0; i < count; i++ )
    {
        const struct ToriRSServerPackRow* row = &g_drive_los_pack[i];

        lua_createtable(L, 0, 30);
        drive_los_push_int(L, "slot", row->slot);
        drive_los_push_int(L, "client_slot", ToriRSServer_SlotMapClient(player, row->slot));
        drive_los_push_int(L, "type", row->type);
        drive_los_push_symbol(L, "symbol", row->type);
        drive_los_push_int(L, "x", row->x);
        drive_los_push_int(L, "z", row->z);
        drive_los_push_int(L, "level", row->level);
        drive_los_push_int(L, "size", row->size);
        drive_los_push_int(L, "hitpoints", row->hitpoints);
        drive_los_push_int(L, "max_hitpoints", row->max_hitpoints);
        drive_los_push_bool(L, "dying", row->dying);
        drive_los_push_int(L, "attackrange", row->attackrange);
        drive_los_push_int(L, "mode", row->mode);
        lua_pushstring(L, drive_los_target_kind_name(row->target_kind));
        lua_setfield(L, -2, "target_kind");
        drive_los_push_int(L, "target_pid", row->target_pid);
        drive_los_push_int(L, "target_slot", row->target_slot);
        drive_los_push_int(L, "target_client_slot",
                           row->target_slot >= 0
                               ? ToriRSServer_SlotMapClient(player, row->target_slot)
                               : -1);
        drive_los_push_int(L, "target_type", row->target_type);
        drive_los_push_symbol(L, "target_symbol", row->target_type);
        drive_los_push_bool(L, "target_via_mode", row->target_via_mode);
        drive_los_push_int(L, "face_entity", row->face_entity);
        drive_los_push_int(L, "walk_x", row->walk_x);
        drive_los_push_int(L, "walk_z", row->walk_z);
        drive_los_push_int(L, "anim_seq", row->anim_seq);
        drive_los_push_int(L, "anim_tick", row->anim_tick);
        drive_los_push_bool(L, "sees_player", row->sees_player);
        drive_los_push_bool(L, "los_player", row->los_player);
        drive_los_push_int(L, "gap_player", row->gap_player);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "rows");
    return 2;
}

static struct LuaFn const LUA_DRIVE_LOS_FNS[] = {
    {"server_los", lua_drive_server_los},
    {"server_npc_pack", lua_drive_server_npc_pack},
    {NULL, NULL},
};

void
PluginDriveLos_RegisterLua(
    struct lua_State* L,
    void* script)
{
    assert(L);
    assert(script);
    PluginLua_AppendModule(L, script, LUA_DRIVE_LOS_FNS);
}

#else /* !TORIRS_EMBED_SERVER */

/* No embedded server, no driver: nothing here is reachable. ISO C wants a
 * translation unit to declare something. */
typedef int PluginDriveLos_Unused;

#endif /* TORIRS_EMBED_SERVER */
