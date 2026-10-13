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
 *   zones   = { {x, z, size (or x0,z0,x1,z1), lo, hi, t0, t1, cost, tier, require, sight}, ... }
 *   pulls   = { {x, z, size, weight, t0, t1}, ... }
 *   watchers = { {x, z, size, t0, t1, cost, tier}, ... }  seen by the near-edge rule
 *   goal    = { x, z, size, side, under_ok, off_side, under, pull }
 *   edge    = { x0, z0, x1, z1, margin, weight }
 *
 * "refused" when `from` is outside the scene. A malformed spec is a Lua
 * argument error: the caller's bug, raised at the call.
 *
 * DriveActionPlanLua, below, is the v2 verb `aplan` (action_plan.c), bound
 * the same way on both lanes (lua_drive_aplan / d_aplan).
 */
#ifndef TORIRS_DRIVE_PLAN_LUA_H
#define TORIRS_DRIVE_PLAN_LUA_H

#include "engine/world_builder/action_plan.h"
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

/* zones = { {x, z, size (or x0,z0,x1,z1), lo, hi, t0, t1, cost, tier, require,
 * sight}, ... } into `zones`; returns the count. Shared by plan and aplan. */
static int
drive_plan_read_zones(lua_State* L, int spec_idx, struct CollisionPlanZone* zones, int cap, int base_x, int base_z)
{
    int n, i, k;
    n = drive_plan_list(L, spec_idx, "zones", cap);
    for( i = 1; i <= n; i++ )
    {
        struct CollisionPlanZone* zn = &zones[i - 1];
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
        lua_getfield(L, k, "sight");
        zn->sight = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return n;
}

/* pulls = { {x, z, size, weight, t0, t1}, ... } into `pulls`; returns the count */
static int
drive_plan_read_pulls(lua_State* L, int spec_idx, struct CollisionPlanPull* pulls, int cap, int base_x, int base_z)
{
    int n, i, k;
    n = drive_plan_list(L, spec_idx, "pulls", cap);
    for( i = 1; i <= n; i++ )
    {
        struct CollisionPlanPull* p = &pulls[i - 1];
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
    lua_pop(L, 1);
    return n;
}

/* goal = { x, z, size, side, under_ok, off_side, under, pull } (absent: not present) */
static void
drive_plan_read_goal(lua_State* L, int spec_idx, struct CollisionPlanGoal* g, int base_x, int base_z)
{
    int k;
    lua_getfield(L, spec_idx, "goal");
    if( !lua_isnil(L, -1) )
    {
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
}

/* edge = { x0, z0, x1, z1, margin, weight } (absent: not present) */
static void
drive_plan_read_edge(lua_State* L, int spec_idx, struct CollisionPlanEdge* e, int base_x, int base_z)
{
    int k;
    lua_getfield(L, spec_idx, "edge");
    if( !lua_isnil(L, -1) )
    {
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

    spec.n_zones = drive_plan_read_zones(L, spec_idx, spec.zones, COLLISION_PLAN_ZONES_MAX, base_x, base_z);
    spec.n_pulls = drive_plan_read_pulls(L, spec_idx, spec.pulls, COLLISION_PLAN_PULLS_MAX, base_x, base_z);

    n = drive_plan_list(L, spec_idx, "watchers", COLLISION_PLAN_WATCHERS_MAX);
    for( i = 1; i <= n; i++ )
    {
        struct CollisionPlanWatcher* w = &spec.watchers[i - 1];
        lua_rawgeti(L, -1, i);
        k = lua_gettop(L);
        w->x = drive_plan_field_int(L, k, "x", 0, 1) - base_x;
        w->z = drive_plan_field_int(L, k, "z", 0, 1) - base_z;
        w->size = drive_plan_field_int(L, k, "size", 1, 0);
        w->t0 = drive_plan_field_int(L, k, "t0", -1, 0);
        w->t1 = drive_plan_field_int(L, k, "t1", 1 << 30, 0);
        w->cost = drive_plan_field_num(L, k, "cost", 1.0);
        w->tier = drive_plan_field_tier(L, k, COLLISION_PLAN_LETHAL);
        luaL_argcheck(L, w->size >= 1 && w->x >= 0 && w->z >= 0 && w->x + w->size <= cm->size_x
                && w->z + w->size <= cm->size_z, spec_idx, "a watcher's footprint is outside the scene");
        lua_pop(L, 1);
    }
    spec.n_watchers = n;
    lua_pop(L, 1);

    drive_plan_read_goal(L, spec_idx, &spec.goal, base_x, base_z);
    drive_plan_read_edge(L, spec_idx, &spec.edge, base_x, base_z);

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
        static char const* const kinds[] = { "", "forbid", "zone", "chaser", "watcher" };
        lua_pushfstring(L, "%s[%d] k%d", kinds[res.why_kind], res.why_index + 1, res.why_k);
        lua_setfield(L, -2, "why");
    }
    return 2;
}

/* ----------------------------------------------------------------------
 * api_drive.aplan(spec): the ToB v2 action planner (action_plan.c; spec
 * and model in action_plan.h and TOB2_DESIGN.md "PLAN"). Absolute tiles
 * and ticks in, scene-local inside.
 *
 *   -> "ok", { path = { {x, z, mid = {x, z}|nil, act = "stay"|"walk"|"attack",
 *                        tgt = i|nil, swing = true|nil, refused = true|nil},
 *                      ... },                                     step k = 1..h
 *              risk (= risk_rule + risk_hp), risk_rule, risk_hp, loss, soft,
 *              value, hp_worst, why = "hit[3] k2"|nil (the first charge of
 *              either), why_rule, why_hp (the first of each),
 *              expanded, routes, steps, clicks, cost }
 *
 * spec (lists optional; defaults in brackets, no bracket = required):
 *   from = {x, z} [the player's tile], now, h [12], beam [32], run [true],
 *   hp, inter = i (1-based target) [nil], swing_ready [0: ready],
 *   stunned_until [0: not stunned], click_cost [0.05], move_cost [0.01],
 *   targets = { {size [1], path = { {x, z}, ... } (k = 0..; non-empty),
 *                range [1], speed, value [0], t_ok0 [-inf], t_ok1 [+inf],
 *                refuse_under_way [true], swings_max [0 = unlimited],
 *                path_alt = { {x, z}, ... } [none] (the npc's other possible
 *                  step per k, same layout as path: the WORSE chase is kept),
 *                path_alts = { { {x, z}, ... }, ... } [none] (more of them;
 *                  path_alt + path_alts <= 9 in all, the WORST chase kept),
 *                swing_costs = { {x0, z0, x1, z1 | x, z, size [1], t0, t1 [t0],
 *                                 dmg_exp [0], dmg_max [dmg_exp], stun [0],
 *                                 forbid [false]}, ... } (<= 16)} }
 *     [t_ok0, t_ok1] gates a PRESS; refuse_under_way = false lets an attack
 *     already under way keep chasing and swinging outside it. A swing cost
 *     is charged like a hit when a swing on the target goes out at t in
 *     [t0, t1] from a tile in its rect.
 *   hits    = { {x0, z0, x1, z1 | x, z, size [1], t0, t1 [t0],
 *                sample "end"|"pass" ["end"], dmg_exp [0], dmg_max [dmg_exp],
 *                stun [0], forbid [false]} }
 *   marks   = { {t_mark (>= now), r [0], t0, t1 [t0], sample, dmg_exp,
 *                dmg_max, stun, forbid} }
 *   held    = { {t, ends_attack [false], swing_delay [0], heal [0]} }
 *   chasers = { {x, z, dmg_exp (required unless forbid), dmg_max [dmg_exp],
 *                forbid [false]} }   a forbid touch is risk_rule, not damage
 *   watchers = { {x, z, size [1], t0, t1 [t0], dmg_exp [0], dmg_max [dmg_exp],
 *                 stun [0], forbid [false]} }  (<= 128) seen by plan's near-edge
 *                 rule, charged on the tick-end tile; footprint inside the scene
 *   zones, pulls, goal, edge  as api_drive.plan (zone tiers: lethal -> risk,
 *                             damage -> loss, soft -> soft; costs in hp)
 *
 * "refused" when `from` is outside the scene; a malformed spec (missing
 * required field, over a cap, a target footprint off the scene, a mark
 * before now, inter past the targets) is a Lua argument error.
 * ---------------------------------------------------------------------- */

static int
drive_aplan_sample(lua_State* L, int idx)
{
    char const* s;
    int v = ACTION_PLAN_SAMPLE_END;
    lua_getfield(L, idx, "sample");
    if( !lua_isnil(L, -1) )
    {
        s = luaL_checkstring(L, -1);
        if( strcmp(s, "end") == 0 )
            v = ACTION_PLAN_SAMPLE_END;
        else if( strcmp(s, "pass") == 0 )
            v = ACTION_PLAN_SAMPLE_PASS;
        else
            luaL_error(L, "aplan: sample '%s' is not end or pass", s);
    }
    lua_pop(L, 1);
    return v;
}

static int
drive_aplan_bool(lua_State* L, int idx, char const* name)
{
    int v;
    lua_getfield(L, idx, name);
    v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

/* a why as "kind[i] kN" ("swingcost[i.j] kN"), or nil for none */
static void
drive_aplan_push_why(lua_State* L, int kind, int index, int k)
{
    static char const* const kinds[] = { "", "hit", "mark", "chaser", "zone", "watcher" };
    if( kind == ACTION_PLAN_WHY_NONE )
        lua_pushnil(L);
    else if( kind == ACTION_PLAN_WHY_SWING_COST )
        lua_pushfstring(L, "swingcost[%d.%d] k%d", index / ACTION_PLAN_SWING_COSTS_MAX + 1,
                        index % ACTION_PLAN_SWING_COSTS_MAX + 1, k);
    else
    {
        assert(kind > 0);
        assert(kind < (int)(sizeof(kinds) / sizeof(kinds[0])));
        lua_pushfstring(L, "%s[%d] k%d", kinds[kind], index + 1, k);
    }
}

/* the footprint path list on top of the stack as target tg's next
 * alternative (`which` 0 = path_alt, else path_alts[which]) */
static void
drive_aplan_read_alt(lua_State* L, struct ActionPlanTarget* tg, int ti, int which, struct CollisionMap* cm,
                     int base_x, int base_z)
{
    int a = tg->n_alts;
    int pn, p;
    if( !lua_istable(L, -1) )
        luaL_error(L, "aplan: targets[%d] alternative %d is not a list", ti, which);
    if( a >= ACTION_PLAN_ALTS_MAX )
        luaL_error(L, "aplan: targets[%d] has more than %d alternatives (path_alt + path_alts)", ti,
                   ACTION_PLAN_ALTS_MAX);
    pn = (int)lua_rawlen(L, -1);
    if( pn < 1 || pn > ACTION_PLAN_H_MAX + 1 )
        luaL_error(L, "aplan: targets[%d] alternative %d has %d entries, 1..%d", ti, which, pn, ACTION_PLAN_H_MAX + 1);
    for( p = 1; p <= pn; p++ )
    {
        int e, px, pz;
        lua_rawgeti(L, -1, p);
        luaL_checktype(L, -1, LUA_TTABLE);
        e = lua_gettop(L);
        px = drive_plan_field_int(L, e, "x", 0, 1) - base_x;
        pz = drive_plan_field_int(L, e, "z", 0, 1) - base_z;
        if( px < 0 || pz < 0 || px + tg->size > cm->size_x || pz + tg->size > cm->size_z )
            luaL_error(L, "aplan: targets[%d] alternative %d [%d] is outside the scene", ti, which, p);
        tg->alt_x[a][p - 1] = px;
        tg->alt_z[a][p - 1] = pz;
        lua_pop(L, 1);
    }
    tg->alt_n[a] = pn;
    tg->n_alts = a + 1;
}

static int
DriveActionPlanLua(
    lua_State* L,
    int spec_idx,
    struct CollisionMap* cm,
    int base_x,
    int base_z,
    int scene_size,
    int player_x,
    int player_z)
{
    static struct ActionPlanSpec spec;
    static struct ActionPlanResult res;
    int from_x = player_x, from_z = player_z;
    int n, i, k, p;

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
    luaL_argcheck(L, spec.h >= 1 && spec.h <= ACTION_PLAN_H_MAX, spec_idx, "aplan: h is 1..16");
    luaL_argcheck(L, spec.beam >= 1 && spec.beam <= ACTION_PLAN_BEAM_MAX, spec_idx, "aplan: beam is 1..64");
    lua_getfield(L, spec_idx, "run");
    spec.run = lua_isnil(L, -1) ? 1 : lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, spec_idx, "hp");
    if( lua_isnil(L, -1) )
        luaL_error(L, "aplan: field 'hp' is required");
    spec.hp = luaL_checknumber(L, -1);
    lua_pop(L, 1);
    spec.inter = drive_plan_field_int(L, spec_idx, "inter", 0, 0) - 1;
    spec.swing_ready = drive_plan_field_int(L, spec_idx, "swing_ready", 0, 0);
    spec.stunned_until = drive_plan_field_int(L, spec_idx, "stunned_until", 0, 0);
    spec.click_cost = drive_plan_field_num(L, spec_idx, "click_cost", 0.05);
    spec.move_cost = drive_plan_field_num(L, spec_idx, "move_cost", 0.01);

    n = drive_plan_list(L, spec_idx, "targets", ACTION_PLAN_TARGETS_MAX);
    for( i = 1; i <= n; i++ )
    {
        struct ActionPlanTarget* tg = &spec.targets[i - 1];
        int pn;
        lua_rawgeti(L, -1, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        k = lua_gettop(L);
        tg->size = drive_plan_field_int(L, k, "size", 1, 0);
        tg->range = drive_plan_field_int(L, k, "range", 1, 0);
        tg->speed = drive_plan_field_int(L, k, "speed", 0, 1);
        tg->value = drive_plan_field_num(L, k, "value", 0.0);
        tg->t_ok0 = drive_plan_field_int(L, k, "t_ok0", -(1 << 30), 0);
        tg->t_ok1 = drive_plan_field_int(L, k, "t_ok1", 1 << 30, 0);
        tg->swings_max = drive_plan_field_int(L, k, "swings_max", 0, 0);
        if( tg->swings_max < 0 )
            luaL_error(L, "aplan: targets[%d].swings_max is negative", i);
        lua_getfield(L, k, "refuse_under_way");
        tg->refuse_under_way = lua_isnil(L, -1) ? 1 : lua_toboolean(L, -1);
        lua_pop(L, 1);
        {
            int ns = drive_plan_list(L, k, "swing_costs", ACTION_PLAN_SWING_COSTS_MAX);
            int j;
            for( j = 1; j <= ns; j++ )
            {
                struct ActionPlanSwingCost* sc = &tg->swing_costs[j - 1];
                int e, rect;
                lua_rawgeti(L, -1, j);
                luaL_checktype(L, -1, LUA_TTABLE);
                e = lua_gettop(L);
                lua_getfield(L, e, "x0");
                rect = !lua_isnil(L, -1);
                lua_pop(L, 1);
                if( rect )
                {
                    sc->x0 = drive_plan_field_int(L, e, "x0", 0, 1) - base_x;
                    sc->z0 = drive_plan_field_int(L, e, "z0", 0, 1) - base_z;
                    sc->x1 = drive_plan_field_int(L, e, "x1", 0, 1) - base_x;
                    sc->z1 = drive_plan_field_int(L, e, "z1", 0, 1) - base_z;
                }
                else
                {
                    int size = drive_plan_field_int(L, e, "size", 1, 0);
                    sc->x0 = drive_plan_field_int(L, e, "x", 0, 1) - base_x;
                    sc->z0 = drive_plan_field_int(L, e, "z", 0, 1) - base_z;
                    sc->x1 = sc->x0 + size - 1;
                    sc->z1 = sc->z0 + size - 1;
                }
                sc->t0 = drive_plan_field_int(L, e, "t0", 0, 1);
                sc->t1 = drive_plan_field_int(L, e, "t1", sc->t0, 0);
                sc->dmg_exp = drive_plan_field_num(L, e, "dmg_exp", 0.0);
                sc->dmg_max = drive_plan_field_num(L, e, "dmg_max", sc->dmg_exp);
                sc->stun = drive_plan_field_int(L, e, "stun", 0, 0);
                sc->forbid = drive_aplan_bool(L, e, "forbid");
                lua_pop(L, 1);
            }
            tg->n_swing_costs = ns;
            lua_pop(L, 1);
        }
        if( tg->size < 1 || tg->range < 1 || tg->speed < 1 )
            luaL_error(L, "aplan: targets[%d] needs size, range and speed >= 1", i);
        lua_getfield(L, k, "path");
        if( !lua_istable(L, -1) )
            luaL_error(L, "aplan: targets[%d].path is required", i);
        pn = (int)lua_rawlen(L, -1);
        if( pn < 1 || pn > ACTION_PLAN_H_MAX + 1 )
            luaL_error(L, "aplan: targets[%d].path has %d entries, 1..%d", i, pn, ACTION_PLAN_H_MAX + 1);
        for( p = 1; p <= pn; p++ )
        {
            int e, px, pz;
            lua_rawgeti(L, -1, p);
            luaL_checktype(L, -1, LUA_TTABLE);
            e = lua_gettop(L);
            px = drive_plan_field_int(L, e, "x", 0, 1) - base_x;
            pz = drive_plan_field_int(L, e, "z", 0, 1) - base_z;
            if( px < 0 || pz < 0 || px + tg->size > cm->size_x || pz + tg->size > cm->size_z )
                luaL_error(L, "aplan: targets[%d].path[%d] is outside the scene", i, p);
            tg->path_x[p - 1] = px;
            tg->path_z[p - 1] = pz;
            lua_pop(L, 1);
        }
        tg->path_n = pn;
        lua_pop(L, 1); /* path */
        /* path_alt (one alternative) and path_alts (a list of them), both
         * into the same up-to-9 alternatives */
        lua_getfield(L, k, "path_alt");
        if( !lua_isnil(L, -1) )
            drive_aplan_read_alt(L, tg, i, 0, cm, base_x, base_z);
        lua_pop(L, 1); /* path_alt */
        lua_getfield(L, k, "path_alts");
        if( !lua_isnil(L, -1) )
        {
            int na;
            if( !lua_istable(L, -1) )
                luaL_error(L, "aplan: targets[%d].path_alts is not a list", i);
            na = (int)lua_rawlen(L, -1);
            for( p = 1; p <= na; p++ )
            {
                lua_rawgeti(L, -1, p);
                drive_aplan_read_alt(L, tg, i, p, cm, base_x, base_z);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1); /* path_alts */
        lua_pop(L, 1); /* target */
    }
    spec.n_targets = n;
    lua_pop(L, 1);
    if( spec.inter < -1 || spec.inter >= spec.n_targets )
        luaL_error(L, "aplan: inter %d names no target (%d given)", spec.inter + 1, spec.n_targets);

    n = drive_plan_list(L, spec_idx, "hits", ACTION_PLAN_HITS_MAX);
    for( i = 1; i <= n; i++ )
    {
        struct ActionPlanHit* hit = &spec.hits[i - 1];
        int size;
        lua_rawgeti(L, -1, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        k = lua_gettop(L);
        lua_getfield(L, k, "x0");
        size = lua_isnil(L, -1);
        lua_pop(L, 1);
        if( size )
        {
            size = drive_plan_field_int(L, k, "size", 1, 0);
            hit->x0 = drive_plan_field_int(L, k, "x", 0, 1) - base_x;
            hit->z0 = drive_plan_field_int(L, k, "z", 0, 1) - base_z;
            hit->x1 = hit->x0 + size - 1;
            hit->z1 = hit->z0 + size - 1;
        }
        else
        {
            hit->x0 = drive_plan_field_int(L, k, "x0", 0, 1) - base_x;
            hit->z0 = drive_plan_field_int(L, k, "z0", 0, 1) - base_z;
            hit->x1 = drive_plan_field_int(L, k, "x1", 0, 1) - base_x;
            hit->z1 = drive_plan_field_int(L, k, "z1", 0, 1) - base_z;
        }
        hit->t0 = drive_plan_field_int(L, k, "t0", 0, 1);
        hit->t1 = drive_plan_field_int(L, k, "t1", hit->t0, 0);
        hit->sample = drive_aplan_sample(L, k);
        hit->dmg_exp = drive_plan_field_num(L, k, "dmg_exp", 0.0);
        hit->dmg_max = drive_plan_field_num(L, k, "dmg_max", hit->dmg_exp);
        hit->stun = drive_plan_field_int(L, k, "stun", 0, 0);
        hit->forbid = drive_aplan_bool(L, k, "forbid");
        lua_pop(L, 1);
    }
    spec.n_hits = n;
    lua_pop(L, 1);

    n = drive_plan_list(L, spec_idx, "marks", ACTION_PLAN_MARKS_MAX);
    for( i = 1; i <= n; i++ )
    {
        struct ActionPlanMark* mk = &spec.marks[i - 1];
        lua_rawgeti(L, -1, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        k = lua_gettop(L);
        mk->t_mark = drive_plan_field_int(L, k, "t_mark", 0, 1);
        mk->r = drive_plan_field_int(L, k, "r", 0, 0);
        mk->t0 = drive_plan_field_int(L, k, "t0", 0, 1);
        mk->t1 = drive_plan_field_int(L, k, "t1", mk->t0, 0);
        mk->sample = drive_aplan_sample(L, k);
        mk->dmg_exp = drive_plan_field_num(L, k, "dmg_exp", 0.0);
        mk->dmg_max = drive_plan_field_num(L, k, "dmg_max", mk->dmg_exp);
        mk->stun = drive_plan_field_int(L, k, "stun", 0, 0);
        mk->forbid = drive_aplan_bool(L, k, "forbid");
        if( mk->t_mark < spec.now )
            luaL_error(L, "aplan: marks[%d].t_mark %d is before now %d (a past mark is a hit)", i, mk->t_mark, spec.now);
        if( mk->r < 0 )
            luaL_error(L, "aplan: marks[%d].r is negative", i);
        lua_pop(L, 1);
    }
    spec.n_marks = n;
    lua_pop(L, 1);

    n = drive_plan_list(L, spec_idx, "held", ACTION_PLAN_HELD_MAX);
    for( i = 1; i <= n; i++ )
    {
        struct ActionPlanHeld* hd = &spec.held[i - 1];
        lua_rawgeti(L, -1, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        k = lua_gettop(L);
        hd->t = drive_plan_field_int(L, k, "t", 0, 1);
        hd->ends_attack = drive_aplan_bool(L, k, "ends_attack");
        hd->swing_delay = drive_plan_field_int(L, k, "swing_delay", 0, 0);
        hd->heal = drive_plan_field_num(L, k, "heal", 0.0);
        lua_pop(L, 1);
    }
    spec.n_held = n;
    lua_pop(L, 1);

    n = drive_plan_list(L, spec_idx, "chasers", ACTION_PLAN_CHASERS_MAX);
    for( i = 1; i <= n; i++ )
    {
        struct ActionPlanChaser* c = &spec.chasers[i - 1];
        lua_rawgeti(L, -1, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        k = lua_gettop(L);
        c->x = drive_plan_field_int(L, k, "x", 0, 1) - base_x;
        c->z = drive_plan_field_int(L, k, "z", 0, 1) - base_z;
        c->forbid = drive_aplan_bool(L, k, "forbid");
        lua_getfield(L, k, "dmg_exp");
        if( lua_isnil(L, -1) && !c->forbid )
            luaL_error(L, "aplan: chasers[%d].dmg_exp is required (or forbid = true)", i);
        lua_pop(L, 1);
        c->dmg_exp = drive_plan_field_num(L, k, "dmg_exp", 0.0);
        c->dmg_max = drive_plan_field_num(L, k, "dmg_max", c->dmg_exp);
        lua_pop(L, 1);
    }
    spec.n_chasers = n;
    lua_pop(L, 1);

    n = drive_plan_list(L, spec_idx, "watchers", ACTION_PLAN_WATCHERS_MAX);
    for( i = 1; i <= n; i++ )
    {
        struct ActionPlanWatcher* w = &spec.watchers[i - 1];
        lua_rawgeti(L, -1, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        k = lua_gettop(L);
        w->x = drive_plan_field_int(L, k, "x", 0, 1) - base_x;
        w->z = drive_plan_field_int(L, k, "z", 0, 1) - base_z;
        w->size = drive_plan_field_int(L, k, "size", 1, 0);
        w->t0 = drive_plan_field_int(L, k, "t0", 0, 1);
        w->t1 = drive_plan_field_int(L, k, "t1", w->t0, 0);
        w->dmg_exp = drive_plan_field_num(L, k, "dmg_exp", 0.0);
        w->dmg_max = drive_plan_field_num(L, k, "dmg_max", w->dmg_exp);
        w->stun = drive_plan_field_int(L, k, "stun", 0, 0);
        w->forbid = drive_aplan_bool(L, k, "forbid");
        if( w->size < 1 || w->x < 0 || w->z < 0 || w->x + w->size > cm->size_x || w->z + w->size > cm->size_z )
            luaL_error(L, "aplan: watchers[%d]'s footprint is outside the scene", i);
        lua_pop(L, 1);
    }
    spec.n_watchers = n;
    lua_pop(L, 1);

    spec.n_zones = drive_plan_read_zones(L, spec_idx, spec.zones, ACTION_PLAN_ZONES_MAX, base_x, base_z);
    spec.n_pulls = drive_plan_read_pulls(L, spec_idx, spec.pulls, ACTION_PLAN_PULLS_MAX, base_x, base_z);
    drive_plan_read_goal(L, spec_idx, &spec.goal, base_x, base_z);
    drive_plan_read_edge(L, spec_idx, &spec.edge, base_x, base_z);

    action_plan(cm, &spec, &res);

    lua_pushstring(L, "ok");
    lua_createtable(L, 0, 11);
    lua_createtable(L, res.n, 0);
    for( i = 0; i < res.n; i++ )
    {
        static char const* const acts[] = { "stay", "walk", "attack" };
        lua_createtable(L, 0, 6);
        lua_pushinteger(L, res.path_x[i] + base_x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, res.path_z[i] + base_z);
        lua_setfield(L, -2, "z");
        if( res.mid_x[i] >= 0 )
        {
            lua_createtable(L, 0, 2);
            lua_pushinteger(L, res.mid_x[i] + base_x);
            lua_setfield(L, -2, "x");
            lua_pushinteger(L, res.mid_z[i] + base_z);
            lua_setfield(L, -2, "z");
            lua_setfield(L, -2, "mid");
        }
        assert(res.act[i] >= ACTION_PLAN_STAY);
        assert(res.act[i] <= ACTION_PLAN_ATTACK);
        lua_pushstring(L, acts[res.act[i]]);
        lua_setfield(L, -2, "act");
        if( res.act[i] == ACTION_PLAN_ATTACK )
        {
            lua_pushinteger(L, res.tgt[i] + 1);
            lua_setfield(L, -2, "tgt");
        }
        if( res.swing[i] )
        {
            lua_pushboolean(L, 1);
            lua_setfield(L, -2, "swing");
        }
        if( res.refused[i] )
        {
            lua_pushboolean(L, 1);
            lua_setfield(L, -2, "refused");
        }
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "path");
    lua_pushnumber(L, res.risk);
    lua_setfield(L, -2, "risk");
    lua_pushnumber(L, res.risk_rule);
    lua_setfield(L, -2, "risk_rule");
    lua_pushnumber(L, res.risk_hp);
    lua_setfield(L, -2, "risk_hp");
    drive_aplan_push_why(L, res.whyr_kind, res.whyr_index, res.whyr_k);
    lua_setfield(L, -2, "why_rule");
    drive_aplan_push_why(L, res.whyh_kind, res.whyh_index, res.whyh_k);
    lua_setfield(L, -2, "why_hp");
    lua_pushnumber(L, res.loss);
    lua_setfield(L, -2, "loss");
    lua_pushnumber(L, res.soft);
    lua_setfield(L, -2, "soft");
    lua_pushnumber(L, res.value);
    lua_setfield(L, -2, "value");
    lua_pushnumber(L, res.hp_worst);
    lua_setfield(L, -2, "hp_worst");
    lua_pushnumber(L, res.cost);
    lua_setfield(L, -2, "cost");
    lua_pushinteger(L, res.steps);
    lua_setfield(L, -2, "steps");
    lua_pushinteger(L, res.clicks);
    lua_setfield(L, -2, "clicks");
    lua_pushinteger(L, res.expanded);
    lua_setfield(L, -2, "expanded");
    lua_pushinteger(L, res.routes);
    lua_setfield(L, -2, "routes");
    drive_aplan_push_why(L, res.why_kind, res.why_index, res.why_k);
    lua_setfield(L, -2, "why");
    return 2;
}

/* api_drive.meet_rank(spec): Verzik P3's ball meeting-tile rank
 * (action_plan.h meet_rank), pure geometry, the same on both lanes
 * (lua_drive_meet_rank / d_meet_rank). Absolute tiles.
 *
 *   spec = { t = {x, z}, n = {x, z} | nil, her = {x, z, size} | nil,
 *            arena = {x0, z0, x1, z1}, ticks, max_level = 4, r = 12,
 *            webs = { {x, z}, ... }, crabs = { {x, z}, ... } }
 *   -> "ok", { {x, z, k1, k2}, ... } (best first), level (0: none), cells
 */
static int
DriveMeetRankLua(lua_State* L, int spec_idx)
{
    static struct MeetRankSpec spec;
    static struct MeetRankTile out[MEET_RANK_OUT_MAX];
    int n_out = 0, cells = 0, level, i, n, k;
    memset(&spec, 0, sizeof(spec));
    luaL_checktype(L, spec_idx, LUA_TTABLE);
    lua_getfield(L, spec_idx, "t");
    luaL_checktype(L, -1, LUA_TTABLE);
    k = lua_gettop(L);
    spec.t_x = drive_plan_field_int(L, k, "x", 0, 1);
    spec.t_z = drive_plan_field_int(L, k, "z", 0, 1);
    lua_pop(L, 1);
    lua_getfield(L, spec_idx, "n");
    if( !lua_isnil(L, -1) )
    {
        k = lua_gettop(L);
        spec.has_n = 1;
        spec.n_x = drive_plan_field_int(L, k, "x", 0, 1);
        spec.n_z = drive_plan_field_int(L, k, "z", 0, 1);
    }
    lua_pop(L, 1);
    lua_getfield(L, spec_idx, "her");
    if( !lua_isnil(L, -1) )
    {
        k = lua_gettop(L);
        spec.has_her = 1;
        spec.her_x = drive_plan_field_int(L, k, "x", 0, 1);
        spec.her_z = drive_plan_field_int(L, k, "z", 0, 1);
        spec.her_size = drive_plan_field_int(L, k, "size", 0, 1);
    }
    lua_pop(L, 1);
    lua_getfield(L, spec_idx, "arena");
    luaL_checktype(L, -1, LUA_TTABLE);
    k = lua_gettop(L);
    spec.ax0 = drive_plan_field_int(L, k, "x0", 0, 1);
    spec.az0 = drive_plan_field_int(L, k, "z0", 0, 1);
    spec.ax1 = drive_plan_field_int(L, k, "x1", 0, 1);
    spec.az1 = drive_plan_field_int(L, k, "z1", 0, 1);
    lua_pop(L, 1);
    spec.ticks = drive_plan_field_int(L, spec_idx, "ticks", 0, 1);
    spec.max_level = drive_plan_field_int(L, spec_idx, "max_level", 4, 0);
    spec.r = drive_plan_field_int(L, spec_idx, "r", 12, 0);
    luaL_argcheck(L, spec.ticks >= 1, spec_idx, "meet_rank: ticks >= 1");
    luaL_argcheck(L, spec.max_level >= 1 && spec.max_level <= 4, spec_idx, "meet_rank: max_level is 1..4");
    luaL_argcheck(L, spec.r >= 1 && spec.r <= MEET_RANK_R_MAX, spec_idx, "meet_rank: r is 1..16");
    luaL_argcheck(L, !spec.has_her || spec.her_size >= 1, spec_idx, "meet_rank: her.size >= 1");
    lua_getfield(L, spec_idx, "webs");
    if( !lua_isnil(L, -1) )
    {
        luaL_checktype(L, -1, LUA_TTABLE);
        n = (int)lua_rawlen(L, -1);
        if( n > MEET_RANK_WEBS_MAX )
            luaL_error(L, "meet_rank: more than %d webs", MEET_RANK_WEBS_MAX);
        for( i = 1; i <= n; i++ )
        {
            lua_rawgeti(L, -1, i);
            luaL_checktype(L, -1, LUA_TTABLE);
            k = lua_gettop(L);
            spec.web_x[i - 1] = drive_plan_field_int(L, k, "x", 0, 1);
            spec.web_z[i - 1] = drive_plan_field_int(L, k, "z", 0, 1);
            lua_pop(L, 1);
        }
        spec.n_webs = n;
    }
    lua_pop(L, 1);
    lua_getfield(L, spec_idx, "crabs");
    if( !lua_isnil(L, -1) )
    {
        luaL_checktype(L, -1, LUA_TTABLE);
        n = (int)lua_rawlen(L, -1);
        if( n > MEET_RANK_CRABS_MAX )
            luaL_error(L, "meet_rank: more than %d crabs", MEET_RANK_CRABS_MAX);
        for( i = 1; i <= n; i++ )
        {
            lua_rawgeti(L, -1, i);
            luaL_checktype(L, -1, LUA_TTABLE);
            k = lua_gettop(L);
            spec.crab_x[i - 1] = drive_plan_field_int(L, k, "x", 0, 1);
            spec.crab_z[i - 1] = drive_plan_field_int(L, k, "z", 0, 1);
            lua_pop(L, 1);
        }
        spec.n_crabs = n;
    }
    lua_pop(L, 1);
    level = meet_rank(&spec, out, &n_out, &cells);
    lua_pushstring(L, "ok");
    lua_createtable(L, n_out, 0);
    for( i = 0; i < n_out; i++ )
    {
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, out[i].x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, out[i].z);
        lua_setfield(L, -2, "z");
        lua_pushinteger(L, out[i].k1);
        lua_setfield(L, -2, "k1");
        lua_pushinteger(L, out[i].k2);
        lua_setfield(L, -2, "k2");
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushinteger(L, level);
    lua_pushinteger(L, cells);
    return 4;
}

/* ---------------------------------------------------------------------
 * The relay's heavy Lua, in C (action_plan.h; owner 2026-10-13). Each verb
 * takes plain tiles and answers what its Lua original answered.
 * ------------------------------------------------------------------ */

/* read list[i].x / .z (1-based, the list at `idx`) into xs/zs, at most max */
static int
drive_tiles_read(lua_State* L, int idx, const char* what, int max, int* xs, int* zs)
{
    int n, i, k;
    luaL_checktype(L, idx, LUA_TTABLE);
    n = (int)lua_rawlen(L, idx);
    if( n > max )
        luaL_error(L, "%s: more than %d entries", what, max);
    for( i = 1; i <= n; i++ )
    {
        lua_rawgeti(L, idx, i);
        luaL_checktype(L, -1, LUA_TTABLE);
        k = lua_gettop(L);
        xs[i - 1] = drive_plan_field_int(L, k, "x", 0, 1);
        zs[i - 1] = drive_plan_field_int(L, k, "z", 0, 1);
        lua_pop(L, 1);
    }
    return n;
}

/* api_drive.yellow_chains(pools, n, hop, top, nodes) -> "ok",
 * { {i, j, k}, ... } (1-based pool indices) and the hop, or "ok", nil */
static int
DriveYellowChainsLua(lua_State* L)
{
    static int px[YELLOW_POOLS_MAX], pz[YELLOW_POOLS_MAX];
    static int out[YELLOW_CHAINS_MAX][3];
    int np = drive_tiles_read(L, 1, "yellow_chains pools", YELLOW_POOLS_MAX, px, pz);
    int n = (int)luaL_checkinteger(L, 2);
    int hop0 = (int)luaL_checkinteger(L, 3);
    int top = (int)luaL_checkinteger(L, 4);
    int nodes = (int)luaL_checkinteger(L, 5);
    int hop = 0, i, k;
    luaL_argcheck(L, n >= 1 && n <= YELLOW_CHAINS_MAX, 2, "yellow_chains: n is 1..8");
    lua_pushstring(L, "ok");
    if( !yellow_chains(px, pz, np, n, hop0, top, nodes, out, &hop) )
    {
        lua_pushnil(L);
        return 2;
    }
    lua_createtable(L, n, 0);
    for( i = 0; i < n; i++ )
    {
        lua_createtable(L, 3, 0);
        for( k = 0; k < 3; k++ )
        {
            lua_pushinteger(L, out[i][k] + 1);
            lua_rawseti(L, -2, k + 1);
        }
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushinteger(L, hop);
    return 3;
}

/* api_drive.yellow_match(raiders, firsts, thirds) -> "ok", { {chain, rev}, ... }
 * (1-based chain index per raider, rev true = run from its third pool), or nil */
static int
DriveYellowMatchLua(lua_State* L)
{
    static int rx[YELLOW_CHAINS_MAX], rz[YELLOW_CHAINS_MAX];
    static int c1x[YELLOW_CHAINS_MAX], c1z[YELLOW_CHAINS_MAX], c3x[YELLOW_CHAINS_MAX], c3z[YELLOW_CHAINS_MAX];
    static int pick[YELLOW_CHAINS_MAX], rev[YELLOW_CHAINS_MAX];
    int nr = drive_tiles_read(L, 1, "yellow_match raiders", YELLOW_CHAINS_MAX, rx, rz);
    int nc = drive_tiles_read(L, 2, "yellow_match firsts", YELLOW_CHAINS_MAX, c1x, c1z);
    int nc3 = drive_tiles_read(L, 3, "yellow_match thirds", YELLOW_CHAINS_MAX, c3x, c3z);
    int j;
    luaL_argcheck(L, nc == nc3, 3, "yellow_match: firsts and thirds differ in length");
    lua_pushstring(L, "ok");
    if( !yellow_match(rx, rz, nr, c1x, c1z, c3x, c3z, nc, pick, rev) )
    {
        lua_pushnil(L);
        return 2;
    }
    lua_createtable(L, nr, 0);
    for( j = 0; j < nr; j++ )
    {
        lua_createtable(L, 2, 0);
        lua_pushinteger(L, pick[j] + 1);
        lua_rawseti(L, -2, 1);
        lua_pushboolean(L, rev[j]);
        lua_rawseti(L, -2, 2);
        lua_rawseti(L, -2, j + 1);
    }
    return 2;
}

/* api_drive.tiles_nearest(from, tiles, max_d, cap) -> "ok", { i, ... } (1-based
 * indices into tiles: within Chebyshev max_d, nearest first, then x, z) */
static int
DriveTilesNearestLua(lua_State* L)
{
    static int tx[8192], tz[8192], out[8192];
    int fx, fz, n, max_d, cap, k, i;
    luaL_checktype(L, 1, LUA_TTABLE);
    fx = drive_plan_field_int(L, 1, "x", 0, 1);
    fz = drive_plan_field_int(L, 1, "z", 0, 1);
    n = drive_tiles_read(L, 2, "tiles_nearest tiles", 8192, tx, tz);
    max_d = (int)luaL_checkinteger(L, 3);
    cap = (int)luaL_checkinteger(L, 4);
    luaL_argcheck(L, cap >= 0, 4, "tiles_nearest: cap >= 0");
    if( cap > n )
        cap = n;
    k = tiles_nearest(fx, fz, tx, tz, n, max_d, cap, out);
    lua_pushstring(L, "ok");
    lua_createtable(L, k, 0);
    for( i = 0; i < k; i++ )
    {
        lua_pushinteger(L, out[i] + 1);
        lua_rawseti(L, -2, i + 1);
    }
    return 2;
}

/* api_drive.bloat_first_bad(spec, x, z, at) -> "ok", lethal_t, damage_t (0 =
 * none in at+1 .. at+hold_long). spec (absolute tiles): lap = {{x,z},...},
 * corner = {bool,...}, size, pos, branches = {{dir, speed}, ...} (the first
 * lethal), tick, down_at|nil, rise, hold_long, hands = {{x,z,t0,t1},...},
 * stomp = {x, z, t, before}|nil. Over the lane's collision map (the flies'
 * sight is api_drive.plan's watcher rule). */
static int
DriveBloatFirstBadLua(lua_State* L, int idx, struct CollisionMap* cm, int base_x, int base_z)
{
    static struct BloatHoldSpec s;
    int x, z, at, i, k, n, lt = 0, dt = 0;
    memset(&s, 0, sizeof(s));
    luaL_checktype(L, idx, LUA_TTABLE);
    s.n_lap = 0;
    lua_getfield(L, idx, "lap");
    {
        static int lx[BLOAT_LAP_MAX], lz[BLOAT_LAP_MAX];
        s.n_lap = drive_tiles_read(L, lua_gettop(L), "bloat_first_bad lap", BLOAT_LAP_MAX, lx, lz);
        for( i = 0; i < s.n_lap; i++ )
        {
            s.lap_x[i] = lx[i] - base_x;
            s.lap_z[i] = lz[i] - base_z;
        }
    }
    lua_pop(L, 1);
    luaL_argcheck(L, s.n_lap >= 1, idx, "bloat_first_bad: an empty lap");
    lua_getfield(L, idx, "corner");
    luaL_checktype(L, -1, LUA_TTABLE);
    for( i = 0; i < s.n_lap; i++ )
    {
        lua_rawgeti(L, -1, i + 1);
        s.corner[i] = (unsigned char)lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    s.size = drive_plan_field_int(L, idx, "size", 0, 1);
    s.pos = drive_plan_field_int(L, idx, "pos", 0, 1);
    luaL_argcheck(L, s.pos >= 1 && s.pos <= s.n_lap, idx, "bloat_first_bad: pos is off the lap");
    lua_getfield(L, idx, "branches");
    luaL_checktype(L, -1, LUA_TTABLE);
    n = (int)lua_rawlen(L, -1);
    luaL_argcheck(L, n >= 1 && n <= BLOAT_BRANCHES_MAX, idx, "bloat_first_bad: 1..4 branches");
    for( i = 1; i <= n; i++ )
    {
        lua_rawgeti(L, -1, i);
        k = lua_gettop(L);
        s.br_dir[i - 1] = drive_plan_field_int(L, k, "dir", 0, 1);
        s.br_speed[i - 1] = drive_plan_field_int(L, k, "speed", 0, 1);
        lua_pop(L, 1);
    }
    s.n_br = n;
    lua_pop(L, 1);
    s.tick = drive_plan_field_int(L, idx, "tick", 0, 1);
    lua_getfield(L, idx, "down_at");
    if( !lua_isnil(L, -1) )
    {
        s.has_down = 1;
        s.down_at = (int)luaL_checkinteger(L, -1);
    }
    lua_pop(L, 1);
    s.rise = drive_plan_field_int(L, idx, "rise", 0, 1);
    s.hold_long = drive_plan_field_int(L, idx, "hold_long", 0, 1);
    lua_getfield(L, idx, "hands");
    if( !lua_isnil(L, -1) )
    {
        luaL_checktype(L, -1, LUA_TTABLE);
        n = (int)lua_rawlen(L, -1);
        if( n > BLOAT_HANDS_MAX )
            luaL_error(L, "bloat_first_bad: more than %d hands", BLOAT_HANDS_MAX);
        for( i = 1; i <= n; i++ )
        {
            lua_rawgeti(L, -1, i);
            k = lua_gettop(L);
            s.hand_x[i - 1] = drive_plan_field_int(L, k, "x", 0, 1) - base_x;
            s.hand_z[i - 1] = drive_plan_field_int(L, k, "z", 0, 1) - base_z;
            s.hand_t0[i - 1] = drive_plan_field_int(L, k, "t0", 0, 1);
            s.hand_t1[i - 1] = drive_plan_field_int(L, k, "t1", 0, 1);
            lua_pop(L, 1);
        }
        s.n_hands = n;
    }
    lua_pop(L, 1);
    lua_getfield(L, idx, "stomp");
    if( !lua_isnil(L, -1) )
    {
        k = lua_gettop(L);
        s.has_stomp = 1;
        s.stomp_x = drive_plan_field_int(L, k, "x", 0, 1) - base_x;
        s.stomp_z = drive_plan_field_int(L, k, "z", 0, 1) - base_z;
        s.stomp_t = drive_plan_field_int(L, k, "t", 0, 1);
        s.stomp_before = drive_plan_field_int(L, k, "before", 0, 1);
    }
    lua_pop(L, 1);
    /* every footprint casts from inside the scene (api_drive.plan's rule) */
    for( i = 0; i < s.n_lap; i++ )
        if( s.lap_x[i] < 0 || s.lap_z[i] < 0 || s.lap_x[i] + s.size > cm->size_x || s.lap_z[i] + s.size > cm->size_z )
            luaL_error(L, "bloat_first_bad: lap[%d] is outside the scene", i + 1);
    if( s.has_stomp &&
        (s.stomp_x < 0 || s.stomp_z < 0 || s.stomp_x + s.size > cm->size_x || s.stomp_z + s.size > cm->size_z) )
        luaL_error(L, "bloat_first_bad: the stomp is outside the scene");
    x = (int)luaL_checkinteger(L, idx + 1) - base_x;
    z = (int)luaL_checkinteger(L, idx + 2) - base_z;
    at = (int)luaL_checkinteger(L, idx + 3);
    bloat_first_bad(cm, &s, x, z, at, &lt, &dt);
    lua_pushstring(L, "ok");
    lua_pushinteger(L, lt);
    lua_pushinteger(L, dt);
    return 3;
}

/* api_drive.sote_guard(spec) -> "ok", { {x, z, t, kind}, ... } (kind 0 maybe,
 * 1 ball, 2 ric, 3 pair). spec: me = {x,z}, r, max, fixed = {{t, grey}},
 * mov = {{tk, red, a, o, x, z}} (red: a = T, x/z = src; else a = lv, x/z =
 * the victim), hyp = {{tk, lv, o, x, z}} */
static int
DriveSoteGuardLua(lua_State* L)
{
    static struct SoteGuardSpec s;
    static int ox[SOTE_OUT_MAX], oz[SOTE_OUT_MAX], ot[SOTE_OUT_MAX], ok[SOTE_OUT_MAX];
    int i, k, n;
    memset(&s, 0, sizeof(s));
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "me");
    luaL_checktype(L, -1, LUA_TTABLE);
    k = lua_gettop(L);
    s.mx = drive_plan_field_int(L, k, "x", 0, 1);
    s.mz = drive_plan_field_int(L, k, "z", 0, 1);
    lua_pop(L, 1);
    s.r = drive_plan_field_int(L, 1, "r", 0, 1);
    s.max = drive_plan_field_int(L, 1, "max", 0, 1);
    luaL_argcheck(L, s.max >= 0 && s.max <= SOTE_OUT_MAX, 1, "sote_guard: max is 0..512");
    lua_getfield(L, 1, "fixed");
    luaL_checktype(L, -1, LUA_TTABLE);
    n = (int)lua_rawlen(L, -1);
    if( n > SOTE_FIXED_MAX )
        luaL_error(L, "sote_guard: more than %d fixed", SOTE_FIXED_MAX);
    for( i = 1; i <= n; i++ )
    {
        lua_rawgeti(L, -1, i);
        k = lua_gettop(L);
        s.fixed_t[i - 1] = drive_plan_field_int(L, k, "t", 0, 1);
        lua_getfield(L, k, "grey");
        s.fixed_grey[i - 1] = (unsigned char)lua_toboolean(L, -1);
        lua_pop(L, 2);
    }
    s.n_fixed = n;
    lua_pop(L, 1);
    lua_getfield(L, 1, "mov");
    luaL_checktype(L, -1, LUA_TTABLE);
    n = (int)lua_rawlen(L, -1);
    if( n > SOTE_MOV_MAX )
        luaL_error(L, "sote_guard: more than %d mov", SOTE_MOV_MAX);
    for( i = 1; i <= n; i++ )
    {
        lua_rawgeti(L, -1, i);
        k = lua_gettop(L);
        s.mov_tk[i - 1] = drive_plan_field_int(L, k, "tk", 0, 1);
        lua_getfield(L, k, "red");
        s.mov_red[i - 1] = (unsigned char)lua_toboolean(L, -1);
        lua_pop(L, 1);
        s.mov_a[i - 1] = drive_plan_field_int(L, k, "a", 0, 1);
        s.mov_o[i - 1] = drive_plan_field_int(L, k, "o", 0, 0);
        s.mov_x[i - 1] = drive_plan_field_int(L, k, "x", 0, 1);
        s.mov_z[i - 1] = drive_plan_field_int(L, k, "z", 0, 1);
        lua_pop(L, 1);
    }
    s.n_mov = n;
    lua_pop(L, 1);
    lua_getfield(L, 1, "hyp");
    luaL_checktype(L, -1, LUA_TTABLE);
    n = (int)lua_rawlen(L, -1);
    if( n > SOTE_HYP_MAX )
        luaL_error(L, "sote_guard: more than %d hyp", SOTE_HYP_MAX);
    for( i = 1; i <= n; i++ )
    {
        lua_rawgeti(L, -1, i);
        k = lua_gettop(L);
        s.hyp_tk[i - 1] = drive_plan_field_int(L, k, "tk", 0, 1);
        s.hyp_lv[i - 1] = drive_plan_field_int(L, k, "lv", 0, 1);
        s.hyp_o[i - 1] = drive_plan_field_int(L, k, "o", 0, 1);
        s.hyp_x[i - 1] = drive_plan_field_int(L, k, "x", 0, 1);
        s.hyp_z[i - 1] = drive_plan_field_int(L, k, "z", 0, 1);
        lua_pop(L, 1);
    }
    s.n_hyp = n;
    lua_pop(L, 1);
    n = sote_guard(&s, ox, oz, ot, ok);
    lua_pushstring(L, "ok");
    lua_createtable(L, n, 0);
    for( i = 0; i < n; i++ )
    {
        lua_createtable(L, 4, 0);
        lua_pushinteger(L, ox[i]);
        lua_rawseti(L, -2, 1);
        lua_pushinteger(L, oz[i]);
        lua_rawseti(L, -2, 2);
        lua_pushinteger(L, ot[i]);
        lua_rawseti(L, -2, 3);
        lua_pushinteger(L, ok[i]);
        lua_rawseti(L, -2, 4);
        lua_rawseti(L, -2, i + 1);
    }
    return 2;
}

#endif
