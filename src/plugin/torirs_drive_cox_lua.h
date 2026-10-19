/*
 * Chambers of Xeric drive verbs, one implementation for both lanes: the
 * client's plugin (torirs_plugin_drive_ui.c) and the scriptrun server
 * (torirs_server_scriptrun.c) include this and hand it their own collision
 * map and scene base, the way torirs_drive_plan_lua.h's verbs are shared.
 *
 *   api_drive.npc_stall(x, z, size, tx, tz, steps)
 *     -> "ok", { x, z, stalled, steps }
 *
 * A size-`size` npc whose south-west tile is (x, z) closing on (tx, tz) the
 * way the server's pursuit does (ToriRSServer_WorldNpcWalkToApproach, and
 * content's `npc_approach`): each step, the reference's naive path to a tile
 * beside the target (collision_map_naive_path), then npc_take_step toward it
 * -- the diagonal, else the X leg, else the Z leg, each as
 * collision_map_can_travel_typed allows (NORMAL; other npcs are not on the
 * client's map) -- for at most `steps` steps: where it ends, whether it
 * stopped because no leg was open (`stalled`), and the steps taken. Standing
 * beside the target, edge-on (the melee approach), is arriving, not a stall.
 * Absolute tiles in and out. The Skeletal Mystics' corner trap
 * (cox2_mystics.lua) asks it where a melee mystic closing on a tile is stopped
 * by an obstacle. (It used to steer the south-west tile onto (tx, tz), the
 * line content's old `npc_walk` took, which is not the pursuit's.)
 *
 * "not_visible" when (x, z) is not on the map. A malformed call is a Lua
 * argument error.
 */
#ifndef TORIRS_DRIVE_COX_LUA_H
#define TORIRS_DRIVE_COX_LUA_H

#include "engine/world_builder/collision_map.h"

#include <assert.h>

static int
drive_cox_sign(int v)
{
    return v > 0 ? 1 : (v < 0 ? -1 : 0);
}

static int
DriveNpcStallLua(lua_State* L, int idx, struct CollisionMap* cm, int base_x, int base_z)
{
    int x, z, size, tx, tz, steps, taken = 0, stalled = 0;
    unsigned rng = 0x5eed1234u;
    struct CollisionApproach approach = { 0 };

    assert(cm);
    x = (int)luaL_checkinteger(L, idx) - base_x;
    z = (int)luaL_checkinteger(L, idx + 1) - base_z;
    size = (int)luaL_checkinteger(L, idx + 2);
    tx = (int)luaL_checkinteger(L, idx + 3) - base_x;
    tz = (int)luaL_checkinteger(L, idx + 4) - base_z;
    steps = (int)luaL_checkinteger(L, idx + 5);
    luaL_argcheck(L, size >= 1 && size <= 5, idx + 2, "npc_stall: size is 1..5");
    luaL_argcheck(L, steps >= 0 && steps <= 64, idx + 5, "npc_stall: steps is 0..64");
    if( x < 1 || z < 1 || x + size + 1 >= cm->size_x || z + size + 1 >= cm->size_z )
    {
        lua_pushstring(L, "not_visible");
        lua_pushnil(L);
        return 2;
    }
    approach.kind = COLL_APPROACH_RECT_ADJACENT;
    approach.loc_width = 1;
    approach.loc_length = 1;
    approach.mover_size = size;
    while( taken < steps )
    {
        int wx, wz, dx, dz;
        int sx = 0, sz = 0;

        if( collision_map_reached(cm, x, z, tx, tz, &approach) )
            break;
        if( !collision_map_naive_path(cm, x, z, tx, tz, size, size, 1, 1, 0, &rng, &wx, &wz) )
        {
            stalled = 1;
            break;
        }
        dx = drive_cox_sign(wx - x);
        dz = drive_cox_sign(wz - z);
        /* the naive path handing back the npc's own tile is the reference's
         * "nowhere to go" (ToriRSServer_WorldNpcWalkTo): it stands */
        if( dx == 0 && dz == 0 )
        {
            stalled = 1;
            break;
        }
        /* the next tile must stay on the map with the footprint's margin */
        if( x + dx < 1 || z + dz < 1 || x + dx + size + 1 >= cm->size_x || z + dz + size + 1 >= cm->size_z )
        {
            stalled = 1;
            break;
        }
        if( dx != 0 && dz != 0 &&
            collision_map_can_travel_typed(cm, x, z, dx, dz, size, 0, COLL_TYPE_NORMAL) )
        {
            sx = dx;
            sz = dz;
        }
        else if( dx != 0 && collision_map_can_travel_typed(cm, x, z, dx, 0, size, 0, COLL_TYPE_NORMAL) )
        {
            sx = dx;
        }
        else if( dz != 0 && collision_map_can_travel_typed(cm, x, z, 0, dz, size, 0, COLL_TYPE_NORMAL) )
        {
            sz = dz;
        }
        else
        {
            stalled = 1;
            break;
        }
        x += sx;
        z += sz;
        taken++;
    }
    lua_pushstring(L, "ok");
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, x + base_x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, z + base_z);
    lua_setfield(L, -2, "z");
    lua_pushboolean(L, stalled);
    lua_setfield(L, -2, "stalled");
    lua_pushinteger(L, taken);
    lua_setfield(L, -2, "steps");
    return 2;
}

/* api_drive.npc_sees(x, z, size, tx, tz) -> "ok", seen
 *
 * Whether a size-`size` npc whose south-west tile is (x, z) sees tile
 * (tx, tz) by the planner's watcher rule (collision_plan_watcher_sees: under
 * the footprint, or line of sight from its near column / near row) on the
 * lane's collision map -- the same test aplan prices its watchers with. The
 * Skeletal Mystics' fight tile (cox2_mystics.lua) is one only its target sees.
 * "not_visible" when the footprint is not on the map. */
static int
DriveNpcSeesLua(lua_State* L, int idx, struct CollisionMap* cm, int base_x, int base_z)
{
    int x, z, size, tx, tz;

    assert(cm);
    x = (int)luaL_checkinteger(L, idx) - base_x;
    z = (int)luaL_checkinteger(L, idx + 1) - base_z;
    size = (int)luaL_checkinteger(L, idx + 2);
    tx = (int)luaL_checkinteger(L, idx + 3) - base_x;
    tz = (int)luaL_checkinteger(L, idx + 4) - base_z;
    luaL_argcheck(L, size >= 1 && size <= 5, idx + 2, "npc_sees: size is 1..5");
    if( x < 0 || z < 0 || x + size > cm->size_x || z + size > cm->size_z || tx < 0 || tz < 0 ||
        tx >= cm->size_x || tz >= cm->size_z )
    {
        lua_pushstring(L, "not_visible");
        lua_pushnil(L);
        return 2;
    }
    lua_pushstring(L, "ok");
    lua_pushboolean(L, collision_plan_watcher_sees(cm, x, z, size, tx, tz));
    return 2;
}

#endif
