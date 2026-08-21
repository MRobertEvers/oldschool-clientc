#include "plugin/torirs_plugin_lua.h"

#include "plugin/torirs_plugin_host.h"

/* The key enum, for PluginLua_KeyCodeFromName. The only engine header this
 * file needs: a script names a key ("shift"), and one side of that lookup has
 * to be the real enum or the two drift. torirs_plugin.h stays engine-free,
 * and torirs_plugin_bridge.u.c static-asserts the contract constants against
 * these same values. */
#include "input/torirs_input.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The Lua adapter.
 *
 * One C plugin that hosts N scripts. To the host each script is a full plugin
 * in its own right -- its own ctx, its own config section, its own panel row,
 * its own enable flag -- which is what makes "a script" and "a C plugin" the
 * same kind of thing everywhere else in the system. The engine never learns
 * that a VM exists: everything here is written against torirs_plugin.h, the
 * same header a C plugin uses.
 *
 * Isolation is per script, not per adapter:
 *
 *   - One lua_State each. Unloading is closing a state, there are no shared
 *     globals to leak between scripts, and memory is accounted per script.
 *   - A counting allocator with a hard cap. Past the cap the allocator returns
 *     NULL, which Lua turns into an ordinary catchable error.
 *   - An instruction budget re-armed before every dispatch, so a script that
 *     spins cannot take the frame with it.
 *   - A sandboxed library set. `io`, `os`, `package` and `debug` are not
 *     opened -- and, because this file provides its own openlibs instead of
 *     linking linit.c, liolib/loslib/loadlib are not even compiled into the
 *     binary. That is simultaneously the security boundary, the tree's
 *     "IO goes through the queue, never fopen" rule, and what makes the web
 *     lane work.
 *
 * A script that errors is disabled through the same path the settings panel
 * uses, with the message parked on its panel row. The client keeps running: a
 * plugin bug must never be a client bug.
 */

#define PLUGIN_LUA_MAX_SCRIPTS 16
#define PLUGIN_LUA_MAX_CONFIG 16
#define PLUGIN_LUA_STR_MAX 96
/* Re-armed before every dispatch. Generous for real handlers -- the entity
 * highlighter's draw pass is a few hundred -- and fatal to a runaway loop. */
#define PLUGIN_LUA_STEP_BUDGET 400000
#define PLUGIN_LUA_MEM_CAP_BYTES (16 * 1024 * 1024)

struct LuaScript;

/* Passed as a subscription's userdata so one trampoline can serve every
 * script and every event without a function per combination. */
struct LuaBinding
{
    struct LuaScript* script;
    int event;
};

struct LuaScript
{
    lua_State* L;
    struct ToriRS_PluginHost* host;
    /* Host index of the plugin THIS script is. -1 until registered. */
    int plugin_index;

    /* Registry refs: the table the script returned, and one per handler. */
    int table_ref;
    int handler_ref[TORIRS_PLUGIN_EV_COUNT];
    /* The persistent `api` table handed to every handler. */
    int api_ref;
    /* The persistent `draw` table, valid only during EV_DRAW_WORLD. */
    int draw_ref;
    /* The persistent `menu` table. Built once, reused every dispatch: the
     * hover pass rebuilds the minimenu on EVERY frame, so anything allocated
     * per dispatch here is allocated fifty times a second. */
    int menu_ref;

    struct LuaBinding bindings[TORIRS_PLUGIN_EV_COUNT];

    char name[TORIRS_PLUGIN_NAME_MAX];
    char version[24];
    struct ToriRS_PluginDef def;
    struct ToriRS_PluginConfigItem config[PLUGIN_LUA_MAX_CONFIG + 1];
    /* Backing store for the schema's strings: key, label, default, choices. */
    char cfg_str[PLUGIN_LUA_MAX_CONFIG][4][PLUGIN_LUA_STR_MAX];
    int config_count;

    /* Live only inside a dispatch, so the Lua-side closures can reach what the
     * C event carried without the script being handed a raw pointer. */
    struct ToriRS_PluginCtx* cur_ctx;
    void* cur_surface;
    struct ToriRS_PluginEvMenuBuild* cur_menu;

    size_t mem_used;
    bool alive;
};

static struct LuaScript g_scripts[PLUGIN_LUA_MAX_SCRIPTS];
static int g_script_count;
static struct ToriRS_PluginApi const* g_api;
static struct ToriRS_PluginHost* g_host;

/* --------------------------------------------------------------- plumbing */

static struct LuaScript*
lua_script_for_ctx(struct ToriRS_PluginCtx* ctx)
{
    int const index = PluginHost_CtxIndex(ctx);
    for( int i = 0; i < g_script_count; i++ )
    {
        if( g_scripts[i].alive && g_scripts[i].plugin_index == index )
            return &g_scripts[i];
    }
    return NULL;
}

/*
 * A per-script arena, so one script cannot starve another or the client.
 *
 * Returning NULL at the cap is deliberate: Lua raises a catchable memory
 * error, the pcall around the handler catches it, and the script is disabled
 * with a message. Aborting here -- the tree's usual answer to an allocation
 * failure -- would let a plugin bug take the client down, which is exactly the
 * outcome this layer exists to prevent. Allocation failures in the HOST are
 * still asserts; this cap is a script exhausting its own budget, which is a
 * different thing from the machine being out of memory.
 */
static void*
lua_script_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    struct LuaScript* script = (struct LuaScript*)ud;

    if( nsize == 0 )
    {
        if( ptr )
        {
            assert(script->mem_used >= osize);
            script->mem_used -= osize;
        }
        free(ptr);
        return NULL;
    }

    size_t const was = ptr ? osize : 0;
    if( script->mem_used - was + nsize > PLUGIN_LUA_MEM_CAP_BYTES )
        return NULL;

    void* out = realloc(ptr, nsize);
    if( !out )
        return NULL;
    script->mem_used = script->mem_used - was + nsize;
    return out;
}

static void
lua_step_hook(lua_State* L, lua_Debug* ar)
{
    (void)ar;
    luaL_error(
        L,
        "instruction budget exhausted (%d): a handler must return within the "
        "frame it was called on",
        PLUGIN_LUA_STEP_BUDGET);
}

static void
lua_arm_budget(struct LuaScript* script)
{
    lua_sethook(script->L, lua_step_hook, LUA_MASKCOUNT, PLUGIN_LUA_STEP_BUDGET);
}

static void
lua_disarm_budget(struct LuaScript* script)
{
    lua_sethook(script->L, NULL, 0, 0);
}

/* Take the script out of the frame and say why. The panel shows the message;
 * re-enabling from there re-runs the script from its last loaded source. */
static void
lua_script_fault(struct LuaScript* script, char const* what)
{
    char msg[160];
    snprintf(msg, sizeof(msg), "%s", what ? what : "error");
    fprintf(stderr, "plugin: lua script '%s' disabled: %s\n", script->name, msg);
    if( script->plugin_index >= 0 )
    {
        PluginHost_SetError(script->host, script->plugin_index, msg);
        PluginHost_SetEnabled(script->host, script->plugin_index, false);
    }
}

/* ------------------------------------------------------ snapshot -> table */

static void
lua_push_player(lua_State* L, struct ToriRS_PluginPlayerSnap const* p)
{
    lua_createtable(L, 0, 12);
#define SETI(k, v)                                                                            \
    do                                                                                        \
    {                                                                                         \
        lua_pushinteger(L, (lua_Integer)(v));                                                 \
        lua_setfield(L, -2, (k));                                                             \
    } while( 0 )
    SETI("true_x", p->true_x);
    SETI("true_z", p->true_z);
    SETI("level", p->level);
    SETI("fine_x", p->fine_x);
    SETI("fine_z", p->fine_z);
    SETI("dest_x", p->dest_x);
    SETI("dest_z", p->dest_z);
    SETI("flag_x", p->flag_x);
    SETI("flag_z", p->flag_z);
    SETI("server_pid", p->server_pid);
    SETI("element_id", p->element_id);
    SETI("combat_level", p->combat_level);
#undef SETI
    lua_pushstring(L, p->name);
    lua_setfield(L, -2, "name");
}

static void
lua_push_npc(lua_State* L, struct ToriRS_PluginNpcSnap const* n)
{
    lua_createtable(L, 0, 12);
#define SETI(k, v)                                                                            \
    do                                                                                        \
    {                                                                                         \
        lua_pushinteger(L, (lua_Integer)(v));                                                 \
        lua_setfield(L, -2, (k));                                                             \
    } while( 0 )
    SETI("server_slot", n->server_slot);
    SETI("npc_id", n->npc_id);
    SETI("base_npc_id", n->base_npc_id);
    SETI("combat_level", n->combat_level);
    SETI("size", n->size);
    SETI("true_x", n->true_x);
    SETI("true_z", n->true_z);
    SETI("level", n->level);
    SETI("fine_x", n->fine_x);
    SETI("fine_z", n->fine_z);
    SETI("element_id", n->element_id);
    SETI("visible_ops", n->visible_ops);
#undef SETI
    lua_pushstring(L, n->name);
    lua_setfield(L, -2, "name");
}

static void
lua_push_obj(lua_State* L, struct ToriRS_PluginObjSnap const* o)
{
    lua_createtable(L, 0, 8);
#define SETI(k, v)                                                                            \
    do                                                                                        \
    {                                                                                         \
        lua_pushinteger(L, (lua_Integer)(v));                                                 \
        lua_setfield(L, -2, (k));                                                             \
    } while( 0 )
    SETI("obj_id", o->obj_id);
    SETI("count", o->count);
    SETI("cost", o->cost);
    SETI("tile_x", o->tile_x);
    SETI("tile_z", o->tile_z);
    SETI("level", o->level);
    SETI("element_id", o->element_id);
    /* Convenience, because every consumer computes it: a stack of 200 arrows
     * is worth 200x the arrow's cost, and getting that product wrong is the
     * one arithmetic mistake a value-thresholding plugin can make. */
    SETI("value", (int64_t)o->cost * (int64_t)o->count);
#undef SETI
    lua_pushstring(L, o->name);
    lua_setfield(L, -2, "name");
}

/* ------------------------------------------------------------- api tables */

static struct LuaScript*
lua_upvalue_script(lua_State* L)
{
    return (struct LuaScript*)lua_touserdata(L, lua_upvalueindex(1));
}

static int
lua_api_log(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const argc = lua_gettop(L);

    fprintf(stderr, "[%s]", script->name);
    for( int i = 1; i <= argc; i++ )
    {
        size_t len = 0;
        char const* s = luaL_tolstring(L, i, &len);
        fprintf(stderr, " %s", s);
        lua_pop(L, 1);
    }
    fputc('\n', stderr);
    return 0;
}

static int
lua_api_world_cycle(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushinteger(L, g_api->world_cycle(script->cur_ctx));
    return 1;
}

static int
lua_api_frame_ms(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushinteger(L, (lua_Integer)g_api->frame_ms(script->cur_ctx));
    return 1;
}

static int
lua_api_local_player(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    struct ToriRS_PluginPlayerSnap snap;

    if( !g_api->local_player(script->cur_ctx, &snap) )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_push_player(L, &snap);
    return 1;
}

/*
 * `for npc in api.npcs() do` -- one value per step, not the (control, value)
 * pair ipairs hands out.
 *
 * The host's iterator token is carried as the closure's second upvalue rather
 * than as the loop's visible control variable, so a script never sees a cursor
 * it has no use for and cannot accidentally reuse one across frames (the pool
 * it indexes is rebuilt as entities come and go).
 */
static int
lua_npc_iter(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    struct ToriRS_PluginNpcSnap snap;
    int const prev = (int)lua_tointeger(L, lua_upvalueindex(2));
    int const next = g_api->npc_next(script->cur_ctx, prev, &snap);

    if( next < 0 )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, next);
    lua_replace(L, lua_upvalueindex(2));
    lua_push_npc(L, &snap);
    return 1;
}

static int
lua_api_npcs(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushlightuserdata(L, script);
    lua_pushinteger(L, -1);
    lua_pushcclosure(L, lua_npc_iter, 2);
    return 1;
}

/* `for player in api.players() do`; see lua_npc_iter for the upvalue cursor. */
static int
lua_player_iter(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    struct ToriRS_PluginPlayerSnap snap;
    int const prev = (int)lua_tointeger(L, lua_upvalueindex(2));
    int const next = g_api->player_next(script->cur_ctx, prev, &snap);

    if( next < 0 )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, next);
    lua_replace(L, lua_upvalueindex(2));
    lua_push_player(L, &snap);
    return 1;
}

static int
lua_api_players(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushlightuserdata(L, script);
    lua_pushinteger(L, -1);
    lua_pushcclosure(L, lua_player_iter, 2);
    return 1;
}

static int
lua_api_npc_by_slot(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    struct ToriRS_PluginNpcSnap snap;
    int const slot = (int)luaL_checkinteger(L, 1);

    if( !g_api->npc_by_slot(script->cur_ctx, slot, &snap) )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_push_npc(L, &snap);
    return 1;
}

static int
lua_api_key_held(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int code;

    if( lua_type(L, 1) == LUA_TSTRING )
        code = PluginLua_KeyCodeFromName(lua_tostring(L, 1));
    else
        code = (int)luaL_checkinteger(L, 1);

    lua_pushboolean(L, code >= 0 && g_api->key_held(script->cur_ctx, code));
    return 1;
}

/* Three returns or one nil, the same shape as project(): a script that says
 * `local x, z, level = api.hover_tile()` reads a miss as x == nil, and never
 * as tile 0. */
static int
lua_api_hover_tile(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int tile_x = 0;
    int tile_z = 0;
    int level = 0;

    if( !g_api->hover_tile(script->cur_ctx, &tile_x, &tile_z, &level) )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, tile_x);
    lua_pushinteger(L, tile_z);
    lua_pushinteger(L, level);
    return 3;
}

static int
lua_api_project(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int x = 0;
    int y = 0;
    int const fine_x = (int)luaL_checkinteger(L, 1);
    int const fine_z = (int)luaL_checkinteger(L, 2);
    int const height = (int)luaL_optinteger(L, 3, 0);

    if( !g_api->project(script->cur_ctx, fine_x, fine_z, height, &x, &y) )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    return 2;
}

static int
lua_api_cfg_set(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* key = luaL_checkstring(L, 1);
    char buf[TORIRS_PLUGIN_CONFIG_VALUE_MAX];

    if( lua_type(L, 2) == LUA_TBOOLEAN )
        snprintf(buf, sizeof(buf), "%d", lua_toboolean(L, 2) ? 1 : 0);
    else
        snprintf(buf, sizeof(buf), "%s", luaL_tolstring(L, 2, NULL));

    g_api->cfg_set(script->cur_ctx, key, buf);
    return 0;
}

/* Reads go through the schema so a colour arrives as an integer and a bool as
 * a boolean, rather than every script re-parsing "#RRGGBB" by hand. */
static int
lua_config_index(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* key = luaL_checkstring(L, 2);

    for( int i = 0; i < script->config_count; i++ )
    {
        if( strcmp(script->config[i].key, key) != 0 )
            continue;
        switch( script->config[i].type )
        {
        case TORIRS_PLUGIN_CFG_BOOL:
            lua_pushboolean(L, g_api->cfg_bool(script->cur_ctx, key));
            return 1;
        case TORIRS_PLUGIN_CFG_INT:
            lua_pushinteger(L, g_api->cfg_int(script->cur_ctx, key));
            return 1;
        case TORIRS_PLUGIN_CFG_COLOR:
            lua_pushinteger(L, (lua_Integer)g_api->cfg_color(script->cur_ctx, key));
            return 1;
        default:
            lua_pushstring(L, g_api->cfg_str(script->cur_ctx, key));
            return 1;
        }
    }
    /* Reading a key the script never declared is a script bug; nil would hide
     * it behind a plausible default. */
    return luaL_error(L, "config key '%s' was never declared by this plugin", key);
}

static int
lua_config_newindex(lua_State* L)
{
    return luaL_error(
        L, "config is read-only from a handler; use api.cfg_set(key, value) instead");
}

/* ---------------------------------------------------------------- drawing */

static uint32_t
lua_check_color(lua_State* L, int idx)
{
    if( lua_type(L, idx) == LUA_TSTRING )
    {
        char const* s = lua_tostring(L, idx);
        if( s[0] == '#' )
            return (uint32_t)strtoul(s + 1, NULL, 16) & 0xffffffu;
        return (uint32_t)strtoul(s, NULL, 16) & 0xffffffu;
    }
    return (uint32_t)luaL_checkinteger(L, idx) & 0xffffffu;
}

static int
lua_draw_tile(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const tx = (int)luaL_checkinteger(L, 1);
    int const tz = (int)luaL_checkinteger(L, 2);
    int const level = (int)luaL_checkinteger(L, 3);
    uint32_t const rgb = lua_check_color(L, 4);
    int const fill = (int)luaL_optinteger(L, 5, 0);

    if( !script->cur_surface )
        return luaL_error(L, "draw calls are only legal inside on_draw_world");
    g_api->draw_tile(script->cur_ctx, script->cur_surface, tx, tz, level, rgb, fill);
    return 0;
}

static int
lua_draw_hull(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const element_id = (int)luaL_checkinteger(L, 1);
    uint32_t const rgb = lua_check_color(L, 2);
    int const fill = (int)luaL_optinteger(L, 3, 0);

    if( !script->cur_surface )
        return luaL_error(L, "draw calls are only legal inside on_draw_world");
    g_api->draw_hull(script->cur_ctx, script->cur_surface, element_id, rgb, fill);
    return 0;
}

static int
lua_draw_line(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const x0 = (int)luaL_checkinteger(L, 1);
    int const y0 = (int)luaL_checkinteger(L, 2);
    int const x1 = (int)luaL_checkinteger(L, 3);
    int const y1 = (int)luaL_checkinteger(L, 4);
    uint32_t const rgb = lua_check_color(L, 5);

    if( !script->cur_surface )
        return luaL_error(L, "draw calls are only legal inside on_draw_world");
    g_api->draw_line(script->cur_ctx, script->cur_surface, x0, y0, x1, y1, rgb);
    return 0;
}

static int
lua_draw_text(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const x = (int)luaL_checkinteger(L, 1);
    int const y = (int)luaL_checkinteger(L, 2);
    char const* s = luaL_checkstring(L, 3);
    uint32_t const rgb = lua_check_color(L, 4);

    if( !script->cur_surface )
        return luaL_error(L, "draw calls are only legal inside on_draw_world");
    g_api->draw_text(script->cur_ctx, script->cur_surface, x, y, s, rgb);
    return 0;
}

static int
lua_draw_rect(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const x = (int)luaL_checkinteger(L, 1);
    int const y = (int)luaL_checkinteger(L, 2);
    int const w = (int)luaL_checkinteger(L, 3);
    int const h = (int)luaL_checkinteger(L, 4);
    uint32_t const rgb = lua_check_color(L, 5);
    int const fill = (int)luaL_optinteger(L, 6, 0);

    if( !script->cur_surface )
        return luaL_error(L, "draw calls are only legal inside on_draw_world");
    g_api->draw_rect(script->cur_ctx, script->cur_surface, x, y, w, h, rgb, fill);
    return 0;
}

/* ------------------------------------------------------------------- menu */

static int
lua_menu_add(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* text = luaL_checkstring(L, 1);
    uint32_t tag;

    if( !script->cur_menu )
        return luaL_error(L, "menu.add is only legal inside on_menu_build");

    /* The tag is an integer the script chooses, handed straight back on
     * select. Anything richer (a table) would have to outlive the build, and
     * the hover pass rebuilds the menu every frame -- so the contract is a
     * plain integer the script can decode itself. */
    tag = (uint32_t)luaL_optinteger(L, 2, 0);
    lua_pushboolean(L, g_api->menu_add(script->cur_ctx, script->cur_menu, text, tag));
    return 1;
}

/* `for obj in api.objs() do`; see lua_npc_iter for the upvalue cursor. */
static int
lua_obj_iter(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    struct ToriRS_PluginObjSnap snap;
    int const prev = (int)lua_tointeger(L, lua_upvalueindex(2));
    int const next = g_api->obj_next(script->cur_ctx, prev, &snap);

    if( next < 0 )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, next);
    lua_replace(L, lua_upvalueindex(2));
    lua_push_obj(L, &snap);
    return 1;
}

static int
lua_api_objs(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushlightuserdata(L, script);
    lua_pushinteger(L, -1);
    lua_pushcclosure(L, lua_obj_iter, 2);
    return 1;
}

/* -- assets --
 *
 * Bytes cross as Lua STRINGS, which is the right carrier and not a shortcut: a
 * Lua string is a counted byte array that may contain NULs, so a binary asset
 * survives the round trip, and string.byte / string.sub are already the tools
 * for reading one. */

static int
lua_api_asset_load(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushboolean(L, g_api->asset_load(script->cur_ctx, luaL_checkstring(L, 1)));
    return 1;
}

static int
lua_api_asset_data(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int size = 0;
    void const* data = g_api->asset_data(script->cur_ctx, luaL_checkstring(L, 1), &size);

    if( !data )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, (char const*)data, (size_t)size);
    return 1;
}

static int
lua_api_asset_save(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* name = luaL_checkstring(L, 1);
    size_t size = 0;
    char const* data = luaL_checklstring(L, 2, &size);

    lua_pushboolean(L, g_api->asset_save(script->cur_ctx, name, data, (int)size));
    return 1;
}

static int
lua_api_asset_release(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->asset_release(script->cur_ctx, luaL_checkstring(L, 1));
    return 0;
}

/* -- world objects -- */

static int
lua_api_object_create(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const handle = g_api->object_create(script->cur_ctx);

    /* nil rather than -1 on refusal: a budget that has run out is something a
     * script has to notice, and `if not obj then` is the shape it will already
     * be writing for every other allocation here. */
    if( handle < 0 )
        lua_pushnil(L);
    else
        lua_pushinteger(L, handle);
    return 1;
}

static int
lua_api_object_destroy(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->object_destroy(script->cur_ctx, (int)luaL_checkinteger(L, 1));
    return 0;
}

static int
lua_api_object_model(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const handle = (int)luaL_checkinteger(L, 1);
    int const id = (int)luaL_checkinteger(L, 2);
    char const* kind = luaL_optstring(L, 3, "model");
    enum ToriRS_PluginModelSource source =
        strcmp(kind, "spotanim") == 0 ? TORIRS_PLUGIN_MODEL_SPOTANIM : TORIRS_PLUGIN_MODEL_CACHE;

    if( strcmp(kind, "spotanim") != 0 && strcmp(kind, "model") != 0 )
        return luaL_error(L, "object_model: kind must be \"model\" or \"spotanim\", got \"%s\"", kind);

    g_api->object_set_model(script->cur_ctx, handle, source, id);
    return 0;
}

static int
lua_api_object_recolor(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->object_recolor(
        script->cur_ctx,
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3));
    return 0;
}

static int
lua_api_object_clear_recolors(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->object_clear_recolors(script->cur_ctx, (int)luaL_checkinteger(L, 1));
    return 0;
}

static int
lua_api_object_anim(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->object_set_anim(
        script->cur_ctx,
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        lua_isnoneornil(L, 3) ? 1 : lua_toboolean(L, 3));
    return 0;
}

static int
lua_api_object_light(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->object_set_light(
        script->cur_ctx,
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3));
    return 0;
}

static int
lua_api_object_position(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->object_set_position(
        script->cur_ctx,
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3),
        (int)luaL_checkinteger(L, 4),
        (int)luaL_optinteger(L, 5, 0),
        (int)luaL_optinteger(L, 6, 0));
    return 0;
}

static int
lua_api_object_active(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->object_set_active(
        script->cur_ctx, (int)luaL_checkinteger(L, 1), lua_toboolean(L, 2));
    return 0;
}

static int
lua_api_object_ready(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushboolean(L, g_api->object_ready(script->cur_ctx, (int)luaL_checkinteger(L, 1)));
    return 1;
}

/* -- colour -- */

static int
lua_api_hsl(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushinteger(L, g_api->hsl_from_rgb(script->cur_ctx, (uint32_t)luaL_checkinteger(L, 1)));
    return 1;
}

static int
lua_api_rgb(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushinteger(
        L, (lua_Integer)g_api->hsl_to_rgb(script->cur_ctx, (int)luaL_checkinteger(L, 1)));
    return 1;
}

/* Pack / unpack a packed HSL, so a script can shift a colour's luminance the
 * way the reference's own beam does without re-deriving the bit layout. */
static int
lua_api_hsl_pack(lua_State* L)
{
    int const h = (int)luaL_checkinteger(L, 1);
    int const s = (int)luaL_checkinteger(L, 2);
    int const l = (int)luaL_checkinteger(L, 3);
    (void)lua_upvalue_script(L);
    lua_pushinteger(L, ((h & 63) << 10) | ((s & 7) << 7) | (l & 127));
    return 1;
}

static int
lua_api_hsl_unpack(lua_State* L)
{
    int const hsl = (int)luaL_checkinteger(L, 1);
    (void)lua_upvalue_script(L);
    lua_pushinteger(L, (hsl >> 10) & 63);
    lua_pushinteger(L, (hsl >> 7) & 7);
    lua_pushinteger(L, hsl & 127);
    return 3;
}

/* -------------------------------------------------------------- sandboxing */

/*
 * Our own openlibs.
 *
 * linit.c is deliberately not compiled: its table names every luaopen_*, so
 * linking it would pull liolib (fopen), loslib (system/getenv) and loadlib
 * (dlopen) into the binary whether or not they were ever opened. Naming the
 * five we want here keeps them out of the link entirely, which is what the web
 * lane needs and what makes "scripts cannot touch the filesystem" a property
 * of the build rather than a promise.
 */
static void
lua_open_sandbox_libs(lua_State* L)
{
    static const luaL_Reg LIBS[] = {
        { LUA_GNAME, luaopen_base },
        { LUA_STRLIBNAME, luaopen_string },
        { LUA_TABLIBNAME, luaopen_table },
        { LUA_MATHLIBNAME, luaopen_math },
        { LUA_UTF8LIBNAME, luaopen_utf8 },
    };

    for( size_t i = 0; i < sizeof(LIBS) / sizeof(LIBS[0]); i++ )
    {
        luaL_requiref(L, LIBS[i].name, LIBS[i].func, 1);
        lua_pop(L, 1);
    }

    /*
     * The base library still carries the loaders. They reach lauxlib's
     * fopen-backed reader, which is the one hole the library selection above
     * does not close, so they are removed by name. Scripts are handed to
     * PluginLua_AddScript as bytes that already came through the IO queue --
     * there is no second way in, and there must not be.
     */
    static char const* const REMOVE[] = { "dofile", "loadfile", "load", "require", "print" };
    for( size_t i = 0; i < sizeof(REMOVE) / sizeof(REMOVE[0]); i++ )
    {
        lua_pushnil(L);
        lua_setglobal(L, REMOVE[i]);
    }
}

/* ------------------------------------------------------------- api table */

static void
lua_build_api_table(struct LuaScript* script)
{
    lua_State* L = script->L;
    static const struct
    {
        char const* name;
        lua_CFunction fn;
    } FNS[] = {
        { "log", lua_api_log },
        { "world_cycle", lua_api_world_cycle },
        { "frame_ms", lua_api_frame_ms },
        { "local_player", lua_api_local_player },
        { "npcs", lua_api_npcs },
        { "players", lua_api_players },
        { "npc_by_slot", lua_api_npc_by_slot },
        { "objs", lua_api_objs },
        { "key_held", lua_api_key_held },
        { "hover_tile", lua_api_hover_tile },
        { "project", lua_api_project },
        { "cfg_set", lua_api_cfg_set },
        { "asset_load", lua_api_asset_load },
        { "asset_data", lua_api_asset_data },
        { "asset_save", lua_api_asset_save },
        { "asset_release", lua_api_asset_release },
        { "object_create", lua_api_object_create },
        { "object_destroy", lua_api_object_destroy },
        { "object_model", lua_api_object_model },
        { "object_recolor", lua_api_object_recolor },
        { "object_clear_recolors", lua_api_object_clear_recolors },
        { "object_anim", lua_api_object_anim },
        { "object_light", lua_api_object_light },
        { "object_position", lua_api_object_position },
        { "object_active", lua_api_object_active },
        { "object_ready", lua_api_object_ready },
        { "hsl", lua_api_hsl },
        { "rgb", lua_api_rgb },
        { "hsl_pack", lua_api_hsl_pack },
        { "hsl_unpack", lua_api_hsl_unpack },
    };

    lua_createtable(L, 0, (int)(sizeof(FNS) / sizeof(FNS[0])) + 1);
    for( size_t i = 0; i < sizeof(FNS) / sizeof(FNS[0]); i++ )
    {
        lua_pushlightuserdata(L, script);
        lua_pushcclosure(L, FNS[i].fn, 1);
        lua_setfield(L, -2, FNS[i].name);
    }

    /* api.config: a proxy, so panel edits are visible on the next event
     * without the script reloading or caching anything. */
    lua_createtable(L, 0, 0);
    lua_createtable(L, 0, 2);
    lua_pushlightuserdata(L, script);
    lua_pushcclosure(L, lua_config_index, 1);
    lua_setfield(L, -2, "__index");
    lua_pushlightuserdata(L, script);
    lua_pushcclosure(L, lua_config_newindex, 1);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "config");

    script->api_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* The draw table. Its functions check cur_surface, so holding a reference
     * to it outside on_draw_world buys a script nothing. */
    static const struct
    {
        char const* name;
        lua_CFunction fn;
    } DRAW[] = {
        { "tile", lua_draw_tile },   { "hull", lua_draw_hull }, { "line", lua_draw_line },
        { "text", lua_draw_text },   { "rect", lua_draw_rect },
    };
    lua_createtable(L, 0, (int)(sizeof(DRAW) / sizeof(DRAW[0])));
    for( size_t i = 0; i < sizeof(DRAW) / sizeof(DRAW[0]); i++ )
    {
        lua_pushlightuserdata(L, script);
        lua_pushcclosure(L, DRAW[i].fn, 1);
        lua_setfield(L, -2, DRAW[i].name);
    }
    script->draw_ref = luaL_ref(L, LUA_REGISTRYINDEX);
}

/*
 * `menu.rows`, built on first read rather than on dispatch.
 *
 * The minimenu is rebuilt every frame to compose the mouseover line, so
 * EV_MENU_BUILD fires ~50 times a second whether or not anything wants it.
 * Materialising the rows eagerly cost one table per row per frame per
 * subscribed script -- measured at ~61us a frame on app_run's p50 with a
 * single script whose handler does nothing but `if menu.hover_pass then
 * return end`. The early-out saved nothing, because the work had already
 * happened before the handler was called.
 *
 * Lazily, that same handler pays one boolean read. A handler that does want
 * the rows pays exactly what it did before, and only once: the array is
 * rawset into the table, so the metamethod does not fire again for the rest
 * of this dispatch.
 */
static int
lua_menu_rows_index(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    struct ToriRS_PluginEvMenuBuild const* ev = script->cur_menu;
    char const* key = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : NULL;

    if( !key || strcmp(key, "rows") != 0 || !ev )
    {
        lua_pushnil(L);
        return 1;
    }

    lua_createtable(L, ev->row_count, 0);
    for( int i = 0; i < ev->row_count; i++ )
    {
        lua_createtable(L, 0, 6);
        lua_pushstring(L, ev->rows[i].text ? ev->rows[i].text : "");
        lua_setfield(L, -2, "text");
        lua_pushinteger(L, ev->rows[i].action);
        lua_setfield(L, -2, "action");
        lua_pushinteger(L, ev->rows[i].pick_kind);
        lua_setfield(L, -2, "pick_kind");
        lua_pushinteger(L, ev->rows[i].npc_slot);
        lua_setfield(L, -2, "npc_slot");
        lua_pushinteger(L, ev->rows[i].player_pid);
        lua_setfield(L, -2, "player_pid");
        lua_pushinteger(L, ev->rows[i].target_id);
        lua_setfield(L, -2, "target_id");
        lua_rawseti(L, -2, i + 1);
    }

    /* Cache it on the table itself, so __index is not consulted again. The
     * dispatch clears it before the next handler runs. */
    lua_pushvalue(L, -1);
    lua_setfield(L, 1, "rows");
    return 1;
}

/* The `menu` table handed to on_menu_build: `add` and the metatable are built
 * once here; only `hover_pass` is rewritten per dispatch. */
static void
lua_build_menu(struct LuaScript* script)
{
    lua_State* L = script->L;

    lua_createtable(L, 0, 3);
    lua_pushlightuserdata(L, script);
    lua_pushcclosure(L, lua_menu_add, 1);
    lua_setfield(L, -2, "add");

    lua_createtable(L, 0, 1);
    lua_pushlightuserdata(L, script);
    lua_pushcclosure(L, lua_menu_rows_index, 1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);

    script->menu_ref = luaL_ref(L, LUA_REGISTRYINDEX);
}

/* ------------------------------------------------------------- dispatch */

/* The handler names the adapter looks for. Absent names are simply not
 * subscribed, so a script pays nothing for events it does not use -- the
 * @Subscribe of this system, discovered rather than declared. */
static char const* const LUA_HANDLER_NAME[TORIRS_PLUGIN_EV_COUNT] = {
    [TORIRS_PLUGIN_EV_START] = "on_start",
    [TORIRS_PLUGIN_EV_STOP] = "on_stop",
    [TORIRS_PLUGIN_EV_FRAME_START] = "on_frame",
    [TORIRS_PLUGIN_EV_LOGIC_TICK] = "on_logic_tick",
    [TORIRS_PLUGIN_EV_SERVER_TICK] = "on_server_tick",
    [TORIRS_PLUGIN_EV_WORLD_LOADED] = "on_world_loaded",
    [TORIRS_PLUGIN_EV_NPC_SPAWN] = "on_npc_spawn",
    [TORIRS_PLUGIN_EV_NPC_RETYPE] = "on_npc_retype",
    [TORIRS_PLUGIN_EV_NPC_DESPAWN] = "on_npc_despawn",
    [TORIRS_PLUGIN_EV_PACKET_IN] = "on_packet_in",
    [TORIRS_PLUGIN_EV_PACKET_OUT] = "on_packet_out",
    [TORIRS_PLUGIN_EV_KEY] = "on_key",
    [TORIRS_PLUGIN_EV_MENU_BUILD] = "on_menu_build",
    [TORIRS_PLUGIN_EV_MENU_SELECT] = "on_menu_select",
    [TORIRS_PLUGIN_EV_DRAW_WORLD] = "on_draw_world",
    [TORIRS_PLUGIN_EV_CONFIG_CHANGED] = "on_config_changed",
    [TORIRS_PLUGIN_EV_OBJ_SPAWN] = "on_obj_spawn",
    [TORIRS_PLUGIN_EV_OBJ_COUNT] = "on_obj_count",
    [TORIRS_PLUGIN_EV_OBJ_DESPAWN] = "on_obj_despawn",
    [TORIRS_PLUGIN_EV_ASSET] = "on_asset",
};

/* Push the event's second argument. Returns the number of arguments pushed. */
static int
lua_push_event_arg(struct LuaScript* script, int event, void* payload)
{
    lua_State* L = script->L;

    switch( event )
    {
    case TORIRS_PLUGIN_EV_START:
    case TORIRS_PLUGIN_EV_STOP:
        return 0;

    case TORIRS_PLUGIN_EV_FRAME_START:
    {
        struct ToriRS_PluginEvFrame const* ev = payload;
        lua_createtable(L, 0, 1);
        lua_pushinteger(L, (lua_Integer)ev->now_ms);
        lua_setfield(L, -2, "now_ms");
        return 1;
    }
    case TORIRS_PLUGIN_EV_LOGIC_TICK:
    case TORIRS_PLUGIN_EV_SERVER_TICK:
    {
        struct ToriRS_PluginEvTick const* ev = payload;
        lua_createtable(L, 0, 1);
        lua_pushinteger(L, ev->cycle);
        lua_setfield(L, -2, "cycle");
        return 1;
    }
    case TORIRS_PLUGIN_EV_WORLD_LOADED:
    {
        struct ToriRS_PluginEvWorld const* ev = payload;
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, ev->base_tile_x);
        lua_setfield(L, -2, "base_tile_x");
        lua_pushinteger(L, ev->base_tile_z);
        lua_setfield(L, -2, "base_tile_z");
        return 1;
    }
    case TORIRS_PLUGIN_EV_NPC_SPAWN:
    case TORIRS_PLUGIN_EV_NPC_RETYPE:
    case TORIRS_PLUGIN_EV_NPC_DESPAWN:
    {
        struct ToriRS_PluginEvNpc const* ev = payload;
        lua_push_npc(L, &ev->npc);
        return 1;
    }
    case TORIRS_PLUGIN_EV_OBJ_SPAWN:
    case TORIRS_PLUGIN_EV_OBJ_COUNT:
    case TORIRS_PLUGIN_EV_OBJ_DESPAWN:
    {
        struct ToriRS_PluginEvObj const* ev = payload;
        lua_push_obj(L, &ev->obj);
        return 1;
    }
    case TORIRS_PLUGIN_EV_ASSET:
    {
        struct ToriRS_PluginEvAsset const* ev = payload;
        lua_createtable(L, 0, 3);
        lua_pushstring(L, ev->name ? ev->name : "");
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, ev->size);
        lua_setfield(L, -2, "size");
        lua_pushboolean(L, ev->ok);
        lua_setfield(L, -2, "ok");
        return 1;
    }
    case TORIRS_PLUGIN_EV_PACKET_IN:
    {
        struct ToriRS_PluginEvPacketIn const* ev = payload;
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, ev->name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, ev->size);
        lua_setfield(L, -2, "size");
        return 1;
    }
    case TORIRS_PLUGIN_EV_PACKET_OUT:
    {
        struct ToriRS_PluginEvPacketOut const* ev = payload;
        lua_createtable(L, 0, 1);
        lua_pushstring(L, ev->builder ? ev->builder : "");
        lua_setfield(L, -2, "builder");
        return 1;
    }
    case TORIRS_PLUGIN_EV_KEY:
    {
        struct ToriRS_PluginEvKey const* ev = payload;
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, ev->key);
        lua_setfield(L, -2, "key");
        lua_pushinteger(L, ev->ch);
        lua_setfield(L, -2, "ch");
        lua_pushboolean(L, ev->down);
        lua_setfield(L, -2, "down");
        return 1;
    }
    case TORIRS_PLUGIN_EV_MENU_BUILD:
    {
        struct ToriRS_PluginEvMenuBuild* ev = payload;

        lua_rawgeti(L, LUA_REGISTRYINDEX, script->menu_ref);
        lua_pushboolean(L, ev->hover_pass);
        lua_setfield(L, -2, "hover_pass");
        /* Drop last dispatch's cached rows so __index rebuilds them -- but
         * only if this handler actually reads them. See lua_menu_rows_index. */
        lua_pushnil(L);
        lua_setfield(L, -2, "rows");
        return 1;
    }
    case TORIRS_PLUGIN_EV_MENU_SELECT:
    {
        struct ToriRS_PluginEvMenuSelect const* ev = payload;
        lua_createtable(L, 0, 5);
        lua_pushinteger(L, (lua_Integer)ev->plugin_tag);
        lua_setfield(L, -2, "tag");
        lua_pushboolean(L, ev->owned);
        lua_setfield(L, -2, "owned");
        lua_pushinteger(L, ev->click_x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, ev->click_y);
        lua_setfield(L, -2, "y");
        lua_createtable(L, 0, 6);
        lua_pushstring(L, ev->row.text ? ev->row.text : "");
        lua_setfield(L, -2, "text");
        lua_pushinteger(L, ev->row.action);
        lua_setfield(L, -2, "action");
        lua_pushinteger(L, ev->row.pick_kind);
        lua_setfield(L, -2, "pick_kind");
        lua_pushinteger(L, ev->row.npc_slot);
        lua_setfield(L, -2, "npc_slot");
        lua_pushinteger(L, ev->row.player_pid);
        lua_setfield(L, -2, "player_pid");
        lua_pushinteger(L, ev->row.target_id);
        lua_setfield(L, -2, "target_id");
        lua_setfield(L, -2, "row");
        return 1;
    }
    case TORIRS_PLUGIN_EV_DRAW_WORLD:
        lua_rawgeti(L, LUA_REGISTRYINDEX, script->draw_ref);
        return 1;

    case TORIRS_PLUGIN_EV_CONFIG_CHANGED:
    {
        struct ToriRS_PluginEvConfig const* ev = payload;
        lua_createtable(L, 0, 1);
        lua_pushstring(L, ev->key ? ev->key : "");
        lua_setfield(L, -2, "key");
        return 1;
    }
    default:
        return 0;
    }
}

/* "consume" / true from a handler is the verdict; on a packet event that is
 * also what sets `drop`, so one word means one thing everywhere. */
static enum ToriRS_PluginVerdict
lua_read_verdict(struct LuaScript* script, int event, void* payload)
{
    lua_State* L = script->L;
    bool consume = false;

    if( lua_isboolean(L, -1) )
        consume = lua_toboolean(L, -1);
    else if( lua_type(L, -1) == LUA_TSTRING )
    {
        char const* s = lua_tostring(L, -1);
        consume = (strcmp(s, "consume") == 0 || strcmp(s, "drop") == 0);
    }
    if( !consume )
        return TORIRS_PLUGIN_PASS;

    if( event == TORIRS_PLUGIN_EV_PACKET_IN )
        ((struct ToriRS_PluginEvPacketIn*)payload)->drop = true;
    else if( event == TORIRS_PLUGIN_EV_PACKET_OUT )
        ((struct ToriRS_PluginEvPacketOut*)payload)->drop = true;
    return TORIRS_PLUGIN_CONSUME;
}

static enum ToriRS_PluginVerdict
lua_trampoline(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    struct LuaBinding* binding = (struct LuaBinding*)userdata;
    struct LuaScript* script;
    lua_State* L;
    int args;
    enum ToriRS_PluginVerdict verdict = TORIRS_PLUGIN_PASS;

    assert(binding);
    script = binding->script;
    assert(script);
    if( !script->alive || script->handler_ref[binding->event] == LUA_NOREF )
        return TORIRS_PLUGIN_PASS;

    L = script->L;
    script->cur_ctx = ctx;
    if( binding->event == TORIRS_PLUGIN_EV_DRAW_WORLD )
        script->cur_surface = ((struct ToriRS_PluginEvDraw*)event)->surface;
    if( binding->event == TORIRS_PLUGIN_EV_MENU_BUILD )
        script->cur_menu = (struct ToriRS_PluginEvMenuBuild*)event;

    lua_rawgeti(L, LUA_REGISTRYINDEX, script->handler_ref[binding->event]);
    lua_rawgeti(L, LUA_REGISTRYINDEX, script->api_ref);
    args = 1 + lua_push_event_arg(script, binding->event, event);

    lua_arm_budget(script);
    int const status = lua_pcall(L, args, 1, 0);
    lua_disarm_budget(script);

    if( status != LUA_OK )
    {
        char const* err = lua_tostring(L, -1);
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: %s", LUA_HANDLER_NAME[binding->event], err ? err : "?");
        lua_pop(L, 1);
        /* Clear the window before disabling: the fault path re-enters the host,
         * and a stale surface would outlive the frame that owned it. */
        script->cur_surface = NULL;
        script->cur_menu = NULL;
        lua_script_fault(script, msg);
        return TORIRS_PLUGIN_PASS;
    }

    verdict = lua_read_verdict(script, binding->event, event);
    lua_pop(L, 1);

    script->cur_surface = NULL;
    script->cur_menu = NULL;
    return verdict;
}

/* ------------------------------------------------------- script lifecycle */

/* The shared init for every script plugin: which script it is arrives through
 * the ctx, since a def cannot carry userdata. */
static void
lua_script_plugin_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    struct LuaScript* script;

    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    script = lua_script_for_ctx(ctx);
    assert(script);

    for( int ev = 0; ev < TORIRS_PLUGIN_EV_COUNT; ev++ )
    {
        if( script->handler_ref[ev] == LUA_NOREF )
            continue;
        script->bindings[ev].script = script;
        script->bindings[ev].event = ev;
        api->subscribe(
            ctx, (enum ToriRS_PluginEvent)ev, lua_trampoline, &script->bindings[ev]);
    }
}

static void
lua_script_release(struct LuaScript* script)
{
    if( !script->alive )
        return;
    if( script->L )
        lua_close(script->L);
    script->L = NULL;
    script->alive = false;
}

/* Copy one declared config item into the script's own storage. The host holds
 * the schema by pointer for the life of the plugin, so it cannot live on the
 * Lua heap. */
static bool
lua_read_config_item(struct LuaScript* script, lua_State* L, int idx, int slot)
{
    struct ToriRS_PluginConfigItem* item = &script->config[slot];
    char* const key = script->cfg_str[slot][0];
    char* const label = script->cfg_str[slot][1];
    char* const def = script->cfg_str[slot][2];
    char* const choices = script->cfg_str[slot][3];

    memset(item, 0, sizeof(*item));

    lua_getfield(L, idx, "key");
    if( lua_type(L, -1) != LUA_TSTRING )
    {
        lua_pop(L, 1);
        return false;
    }
    snprintf(key, PLUGIN_LUA_STR_MAX, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    item->key = key;

    lua_getfield(L, idx, "type");
    char const* type = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "string";
    if( strcmp(type, "bool") == 0 )
        item->type = TORIRS_PLUGIN_CFG_BOOL;
    else if( strcmp(type, "int") == 0 )
        item->type = TORIRS_PLUGIN_CFG_INT;
    else if( strcmp(type, "color") == 0 || strcmp(type, "colour") == 0 )
        item->type = TORIRS_PLUGIN_CFG_COLOR;
    else if( strcmp(type, "enum") == 0 )
        item->type = TORIRS_PLUGIN_CFG_ENUM;
    else
        item->type = TORIRS_PLUGIN_CFG_STRING;
    lua_pop(L, 1);

    lua_getfield(L, idx, "label");
    if( lua_type(L, -1) == LUA_TSTRING )
    {
        snprintf(label, PLUGIN_LUA_STR_MAX, "%s", lua_tostring(L, -1));
        item->label = label;
    }
    lua_pop(L, 1);

    lua_getfield(L, idx, "default");
    if( lua_type(L, -1) == LUA_TBOOLEAN )
        snprintf(def, PLUGIN_LUA_STR_MAX, "%d", lua_toboolean(L, -1) ? 1 : 0);
    else if( lua_type(L, -1) != LUA_TNIL )
        snprintf(def, PLUGIN_LUA_STR_MAX, "%s", luaL_tolstring(L, -1, NULL)), lua_pop(L, 1);
    else
        def[0] = '\0';
    lua_pop(L, 1);
    item->default_value = def;

    lua_getfield(L, idx, "min");
    item->min = (int)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);
    lua_getfield(L, idx, "max");
    item->max = (int)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);

    lua_getfield(L, idx, "choices");
    if( lua_type(L, -1) == LUA_TSTRING )
    {
        snprintf(choices, PLUGIN_LUA_STR_MAX, "%s", lua_tostring(L, -1));
        item->choices = choices;
    }
    lua_pop(L, 1);
    return true;
}

int
PluginLua_AddScript(
    struct ToriRS_PluginHost* host,
    char const* name,
    char const* source,
    int source_len)
{
    struct LuaScript* script;
    lua_State* L;
    char chunk[TORIRS_PLUGIN_NAME_MAX + 2];

    assert(host);
    assert(name);
    assert(source);
    assert(source_len > 0);

    if( g_script_count >= PLUGIN_LUA_MAX_SCRIPTS )
    {
        fprintf(stderr, "plugin: lua script table full, refusing '%s'\n", name);
        return -1;
    }

    script = &g_scripts[g_script_count];
    memset(script, 0, sizeof(*script));
    script->host = host;
    script->plugin_index = -1;
    script->table_ref = LUA_NOREF;
    script->api_ref = LUA_NOREF;
    script->draw_ref = LUA_NOREF;
    script->menu_ref = LUA_NOREF;
    for( int i = 0; i < TORIRS_PLUGIN_EV_COUNT; i++ )
        script->handler_ref[i] = LUA_NOREF;
    snprintf(script->name, sizeof(script->name), "%s", name);

    L = lua_newstate(lua_script_alloc, script, 0);
    if( !L )
    {
        fprintf(stderr, "plugin: lua state for '%s' would not start\n", name);
        return -1;
    }
    script->L = L;
    script->alive = true;
    lua_open_sandbox_libs(L);

    snprintf(chunk, sizeof(chunk), "@%s", name);
    lua_arm_budget(script);
    if( luaL_loadbuffer(L, source, (size_t)source_len, chunk) != LUA_OK ||
        lua_pcall(L, 0, 1, 0) != LUA_OK )
    {
        char const* err = lua_tostring(L, -1);
        fprintf(stderr, "plugin: lua script '%s' failed to load: %s\n", name, err ? err : "?");
        lua_disarm_budget(script);
        lua_script_release(script);
        return -1;
    }
    lua_disarm_budget(script);

    if( !lua_istable(L, -1) )
    {
        fprintf(
            stderr,
            "plugin: lua script '%s' must return its plugin table (got %s)\n",
            name,
            luaL_typename(L, -1));
        lua_script_release(script);
        return -1;
    }

    /* A script may name itself; the file name is the fallback so a plugin's
     * identity -- and therefore its ini section -- never depends on where the
     * file happened to live. */
    lua_getfield(L, -1, "name");
    if( lua_type(L, -1) == LUA_TSTRING )
        snprintf(script->name, sizeof(script->name), "%s", lua_tostring(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, -1, "version");
    snprintf(
        script->version,
        sizeof(script->version),
        "%s",
        lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "0");
    lua_pop(L, 1);

    lua_getfield(L, -1, "config");
    if( lua_istable(L, -1) )
    {
        lua_Integer const n = luaL_len(L, -1);
        for( lua_Integer i = 1; i <= n && script->config_count < PLUGIN_LUA_MAX_CONFIG; i++ )
        {
            lua_rawgeti(L, -1, i);
            if( lua_istable(L, -1) &&
                lua_read_config_item(script, L, lua_gettop(L), script->config_count) )
                script->config_count++;
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    memset(&script->config[script->config_count], 0, sizeof(script->config[0]));

    for( int ev = 0; ev < TORIRS_PLUGIN_EV_COUNT; ev++ )
    {
        if( !LUA_HANDLER_NAME[ev] )
            continue;
        lua_getfield(L, -1, LUA_HANDLER_NAME[ev]);
        if( lua_isfunction(L, -1) )
            script->handler_ref[ev] = luaL_ref(L, LUA_REGISTRYINDEX);
        else
            lua_pop(L, 1);
    }

    script->table_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_build_api_table(script);
    lua_build_menu(script);

    script->def.name = script->name;
    script->def.version = script->version;
    script->def.priority = 0;
    script->def.config = script->config_count > 0 ? script->config : NULL;
    script->def.init = lua_script_plugin_init;
    script->def.shutdown = NULL;

    script->plugin_index = PluginHost_Register(host, &script->def);
    if( script->plugin_index < 0 )
    {
        lua_script_release(script);
        return -1;
    }

    g_script_count++;
    return script->plugin_index;
}

int
PluginLua_KeyCodeFromName(char const* name)
{
    /* The handful a plugin realistically gates on. Anything else is passed as
     * an integer -- the contract is enum LibToriRS_KeyCode either way, and
     * mirroring the whole table here would be a second copy to drift. */
    static const struct
    {
        char const* name;
        int code;
    } KEYS[] = {
        { "shift", TORIRS_PLUGIN_KEY_SHIFT }, { "ctrl", TORIRS_PLUGIN_KEY_CTRL },
        { "space", TORIRS_PLUGIN_KEY_SPACE },
        { "tab", TORIRS_PLUGIN_KEY_TAB },     { "escape", TORIRS_PLUGIN_KEY_ESCAPE },
    };

    if( !name )
        return -1;
    for( size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++ )
    {
        if( strcmp(KEYS[i].name, name) == 0 )
            return KEYS[i].code;
    }
    return -1;
}

void
PluginLua_Shutdown(void)
{
    for( int i = 0; i < g_script_count; i++ )
        lua_script_release(&g_scripts[i]);
    g_script_count = 0;
}

/* ---------------------------------------------------------- the adapter */

static void
lua_adapter_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);
    g_api = api;
}

static void
lua_adapter_shutdown(struct ToriRS_PluginCtx* ctx)
{
    (void)ctx;
    PluginLua_Shutdown();
}

void
PluginLua_Bind(struct ToriRS_PluginHost* host)
{
    assert(host);
    g_host = host;
    /* The api table is fetched rather than waited for: scripts can be added
     * before the adapter's own init runs, and every one of them needs it. */
    g_api = PluginHost_Api(host);
}

/*
 * The adapter is itself a plugin so that the whole scripting layer has one
 * enable flag, one panel row and one place to report a load failure. It
 * subscribes to nothing -- the scripts it hosts do that for themselves.
 */
struct ToriRS_PluginDef const TORIRS_PLUGIN_LUA = {
    .name = "lua",
    .version = "1.0.0",
    .priority = 0,
    .config = NULL,
    .init = lua_adapter_init,
    .shutdown = lua_adapter_shutdown,
};
