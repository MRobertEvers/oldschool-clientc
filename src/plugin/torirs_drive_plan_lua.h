/*
 * The drive API's `plan` verb, one implementation for both lanes: the
 * client's plugin (torirs_plugin_drive_ui.c lua_drive_plan) and the scriptrun
 * server (torirs_server_scriptrun.c d_plan) include this and hand it their
 * own collision map, scene base and player tile. Everything else -- the
 * search itself -- is collision_plan (engine/world_builder/collision_map.c).
 *
 *   api_drive.plan(spec) -> "ok", { path = { {x, z, mid = {x, z}|nil}, ... },
 *                                    lethal, damage, soft, why, expanded }
 *
 * spec (absolute tiles; ticks absolute; every list optional):
 *   from = {x, z}        the tile the plan starts from, default the player's
 *   now = tick           the tick `from` is the end of (required)
 *   h = 12, beam = 32, run = true, move_cost = 0.01
 *   chasers = { {x, z, cost, tier}, ... }           tier "soft"|"damage"|"lethal"
 *   forbid  = { {x, z, t0, t1, cost, tier}, ... }
 *   zones   = { {x, z, size (or x0,z0,x1,z1), lo, hi, t0, t1, cost, tier, require}, ... }
 *   pulls   = { {x, z, size, weight, t0, t1}, ... }
 *   goal    = { x, z, size, side, under_ok, off_side, under, pull }
 *   edge    = { x0, z0, x1, z1, margin, weight }
 *
 * "refused" when `from` is outside the scene. A malformed spec is a Lua
 * argument error: the caller's bug, raised at the call.
 */
#ifndef TORIRS_DRIVE_PLAN_LUA_H
#define TORIRS_DRIVE_PLAN_LUA_H

#include "engine/world_builder/collision_map.h"

#include <assert.h>
#include <string.h>

static int
drive_plan_field_int(lua_State* L, int idx, char const* name, int dflt, int required)
{
    int v;
    lua_getfield(L, idx, name);
    if( lua_isnil(L, -1) )
    {
        lua_pop(L, 1);
        if( required )
            luaL_error(L, "plan: field '%s' is required", name);
        return dflt;
    }
    v = (int)luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    return v;
}

static double
drive_plan_field_num(lua_State* L, int idx, char const* name, double dflt)
{
    double v;
    lua_getfield(L, idx, name);
    if( lua_isnil(L, -1) )
    {
        lua_pop(L, 1);
        return dflt;
    }
    v = luaL_checknumber(L, -1);
    lua_pop(L, 1);
    return v;
}

static int
drive_plan_field_tier(lua_State* L, int idx, int dflt)
{
    char const* s;
    int tier = dflt;
    lua_getfield(L, idx, "tier");
    if( !lua_isnil(L, -1) )
    {
        s = luaL_checkstring(L, -1);
        if( strcmp(s, "soft") == 0 )
            tier = COLLISION_PLAN_SOFT;
        else if( strcmp(s, "damage") == 0 )
            tier = COLLISION_PLAN_DAMAGE;
        else if( strcmp(s, "lethal") == 0 )
            tier = COLLISION_PLAN_LETHAL;
        else
            luaL_error(L, "plan: tier '%s' is not soft, damage or lethal", s);
    }
    lua_pop(L, 1);
    return tier;
}

/* the length of list field `name` at `idx`, the list left on the stack (nil
 * when absent: the caller pops either way) */
static int
drive_plan_list(lua_State* L, int idx, char const* name, int cap)
{
    int n;
    lua_getfield(L, idx, name);
    if( lua_isnil(L, -1) )
        return 0;
    luaL_checktype(L, -1, LUA_TTABLE);
    n = (int)lua_rawlen(L, -1);
    if( n > cap )
        luaL_error(L, "plan: %s has %d entries, the cap is %d", name, n, cap);
    return n;
}

static int
DrivePlanLua(
    lua_State* L,
    int spec_idx,
    struct CollisionMap* cm,
    int base_x,
    int base_z,
    int scene_size,
    int player_x,
    int player_z)
{
    static struct CollisionPlanSpec spec;
    static struct CollisionPlanResult res;
    int from_x = player_x, from_z = player_z;
    int n, i, k;

    assert(cm);
    luaL_checktype(L, spec_idx, LUA_TTABLE);
    memset(&spec, 0, sizeof(spec));
    lua_getfield(L, spec_idx, "from");
    if( !lua_isnil(L, -1) )
    {
        luaL_checktype(L, -1, LUA_TTABLE);
        from_x = drive_plan_field_int(L, lua_gettop(L), "x", 0, 1);
        from_z = drive_plan_field_int(L, lua_gettop(L), "z", 0, 1);
    }
    lua_pop(L, 1);
    spec.src_x = from_x - base_x;
    spec.src_z = from_z - base_z;
    if( spec.src_x < 0 || spec.src_z < 0 || spec.src_x >= scene_size || spec.src_z >= scene_size ||
        spec.src_x >= cm->size_x || spec.src_z >= cm->size_z )
    {
        lua_pushstring(L, "refused");
        lua_pushnil(L);
        return 2;
    }
    spec.now = drive_plan_field_int(L, spec_idx, "now", 0, 1);
    spec.h = drive_plan_field_int(L, spec_idx, "h", 12, 0);
    spec.beam = drive_plan_field_int(L, spec_idx, "beam", 32, 0);
    luaL_argcheck(L, spec.h >= 1 && spec.h <= COLLISION_PLAN_H_MAX, spec_idx, "h is 1..16");
    luaL_argcheck(L, spec.beam >= 1 && spec.beam <= COLLISION_PLAN_BEAM_MAX, spec_idx, "beam is 1..64");
    lua_getfield(L, spec_idx, "run");
    spec.steps_per_tick = lua_isnil(L, -1) ? 2 : (lua_toboolean(L, -1) ? 2 : 1);
    lua_pop(L, 1);
    spec.move_cost = drive_plan_field_num(L, spec_idx, "move_cost", 0.01);

    n = drive_plan_list(L, spec_idx, "chasers", COLLISION_PLAN_CHASERS_MAX);
    for( i = 1; i <= n; i++ )
    {
        struct CollisionPlanChaser* c = &spec.chasers[i - 1];
        lua_rawgeti(L, -1, i);
        k = lua_gettop(L);
        c->x = drive_plan_field_int(L, k, "x", 0, 1) - base_x;
        c->z = drive_plan_field_int(L, k, "z", 0, 1) - base_z;
        c->cost = drive_plan_field_num(L, k, "cost", 1.0);
        c->tier = drive_plan_field_tier(L, k, COLLISION_PLAN_DAMAGE);
        lua_pop(L, 1);
    }
    spec.n_chasers = n;
    lua_pop(L, 1);

    n = drive_plan_list(L, spec_idx, "forbid", COLLISION_PLAN_FORBID_MAX);
    for( i = 1; i <= n; i++ )
    {
        struct CollisionPlanForbid* f = &spec.forbid[i - 1];
        lua_rawgeti(L, -1, i);
        k = lua_gettop(L);
        f->x = drive_plan_field_int(L, k, "x", 0, 1) - base_x;
        f->z = drive_plan_field_int(L, k, "z", 0, 1) - base_z;
        f->t0 = drive_plan_field_int(L, k, "t0", -1, 0);
        f->t1 = drive_plan_field_int(L, k, "t1", 1 << 30, 0);
        f->cost = drive_plan_field_num(L, k, "cost", 1.0);
        f->tier = drive_plan_field_tier(L, k, COLLISION_PLAN_LETHAL);
        lua_pop(L, 1);
    }
    spec.n_forbid = n;
    lua_pop(L, 1);

    n = drive_plan_list(L, spec_idx, "zones", COLLISION_PLAN_ZONES_MAX);
    for( i = 1; i <= n; i++ )
    {
        struct CollisionPlanZone* zn = &spec.zones[i - 1];
        int size;
        lua_rawgeti(L, -1, i);
        k = lua_gettop(L);
        lua_getfield(L, k, "size");
        size = lua_isnil(L, -1) ? 0 : (int)luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        if( size > 0 )
        {
            zn->x0 = drive_plan_field_int(L, k, "x", 0, 1) - base_x;
            zn->z0 = drive_plan_field_int(L, k, "z", 0, 1) - base_z;
            zn->x1 = zn->x0 + size - 1;
            zn->z1 = zn->z0 + size - 1;
        }
        else
        {
            zn->x0 = drive_plan_field_int(L, k, "x0", 0, 1) - base_x;
            zn->z0 = drive_plan_field_int(L, k, "z0", 0, 1) - base_z;
            zn->x1 = drive_plan_field_int(L, k, "x1", 0, 1) - base_x;
            zn->z1 = drive_plan_field_int(L, k, "z1", 0, 1) - base_z;
        }
        zn->lo = drive_plan_field_int(L, k, "lo", 0, 0);
        zn->hi = drive_plan_field_int(L, k, "hi", 1 << 20, 0);
        zn->t0 = drive_plan_field_int(L, k, "t0", -1, 0);
        zn->t1 = drive_plan_field_int(L, k, "t1", 1 << 30, 0);
        zn->cost = drive_plan_field_num(L, k, "cost", 1.0);
        zn->tier = drive_plan_field_tier(L, k, COLLISION_PLAN_LETHAL);
        lua_getfield(L, k, "require");
        zn->require = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_pop(L, 1);
    }
    spec.n_zones = n;
    lua_pop(L, 1);

    n = drive_plan_list(L, spec_idx, "pulls", COLLISION_PLAN_PULLS_MAX);
    for( i = 1; i <= n; i++ )
    {
        struct CollisionPlanPull* p = &spec.pulls[i - 1];
        lua_rawgeti(L, -1, i);
        k = lua_gettop(L);
        p->x = drive_plan_field_int(L, k, "x", 0, 1) - base_x;
        p->z = drive_plan_field_int(L, k, "z", 0, 1) - base_z;
        p->size = drive_plan_field_int(L, k, "size", 1, 0);
        p->weight = drive_plan_field_num(L, k, "weight", 1.0);
        p->t0 = drive_plan_field_int(L, k, "t0", -1, 0);
        p->t1 = drive_plan_field_int(L, k, "t1", 1 << 30, 0);
        lua_pop(L, 1);
    }
    spec.n_pulls = n;
    lua_pop(L, 1);

    lua_getfield(L, spec_idx, "goal");
    if( !lua_isnil(L, -1) )
    {
        struct CollisionPlanGoal* g = &spec.goal;
        luaL_checktype(L, -1, LUA_TTABLE);
        k = lua_gettop(L);
        g->present = 1;
        g->x = drive_plan_field_int(L, k, "x", 0, 1) - base_x;
        g->z = drive_plan_field_int(L, k, "z", 0, 1) - base_z;
        g->size = drive_plan_field_int(L, k, "size", 1, 0);
        g->side = drive_plan_field_int(L, k, "side", -1, 0);
        lua_getfield(L, k, "under_ok");
        g->under_ok = lua_toboolean(L, -1);
        lua_pop(L, 1);
        g->off_side = drive_plan_field_num(L, k, "off_side", 0.25);
        g->under = drive_plan_field_num(L, k, "under", 0.5);
        g->pull = drive_plan_field_num(L, k, "pull", 1.0);
    }
    lua_pop(L, 1);

    lua_getfield(L, spec_idx, "edge");
    if( !lua_isnil(L, -1) )
    {
        struct CollisionPlanEdge* e = &spec.edge;
        luaL_checktype(L, -1, LUA_TTABLE);
        k = lua_gettop(L);
        e->present = 1;
        e->x0 = drive_plan_field_int(L, k, "x0", 0, 1) - base_x;
        e->z0 = drive_plan_field_int(L, k, "z0", 0, 1) - base_z;
        e->x1 = drive_plan_field_int(L, k, "x1", 0, 1) - base_x;
        e->z1 = drive_plan_field_int(L, k, "z1", 0, 1) - base_z;
        e->margin = drive_plan_field_int(L, k, "margin", 3, 0);
        e->weight = drive_plan_field_num(L, k, "weight", 1.0);
    }
    lua_pop(L, 1);

    collision_plan(cm, &spec, &res);

    lua_pushstring(L, "ok");
    lua_createtable(L, 0, 6);
    lua_createtable(L, res.n, 0);
    for( i = 0; i < res.n; i++ )
    {
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, res.path_x[i] + base_x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, res.path_z[i] + base_z);
        lua_setfield(L, -2, "z");
        if( i == 0 && res.mid_x >= 0 )
        {
            lua_createtable(L, 0, 2);
            lua_pushinteger(L, res.mid_x + base_x);
            lua_setfield(L, -2, "x");
            lua_pushinteger(L, res.mid_z + base_z);
            lua_setfield(L, -2, "z");
            lua_setfield(L, -2, "mid");
        }
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "path");
    lua_pushnumber(L, res.lethal);
    lua_setfield(L, -2, "lethal");
    lua_pushnumber(L, res.damage);
    lua_setfield(L, -2, "damage");
    lua_pushnumber(L, res.soft);
    lua_setfield(L, -2, "soft");
    lua_pushinteger(L, res.expanded);
    lua_setfield(L, -2, "expanded");
    if( res.why_kind )
    {
        static char const* const kinds[] = { "", "forbid", "zone", "chaser" };
        lua_pushfstring(L, "%s[%d] k%d", kinds[res.why_kind], res.why_index + 1, res.why_k);
        lua_setfield(L, -2, "why");
    }
    return 2;
}

#endif
