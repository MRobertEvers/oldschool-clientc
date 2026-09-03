#include "plugin/torirs_plugin_lua.h"

#include "plugin/torirs_plugin_host.h"
#include "platform/platform_memory.h"

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
#include "log/torirs_log.h"

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
/*
 * Config items one script may declare. The schema has to live in C for the
 * life of the plugin -- the host holds it by pointer -- so it is copied into
 * the fixed arrays below rather than left on the Lua heap, and this is what
 * sizes them: 32 * 4 * PLUGIN_LUA_STR_MAX is ~12 KB of strings per script,
 * ~196 KB across PLUGIN_LUA_MAX_SCRIPTS.
 *
 * Sized against a real port: RuneLite's Ground Items plugin, which
 * script/plugins/ground_items.lua follows, carries 33 settings. At the 16 this
 * used to be, that port silently lost ten of them and then failed at the first
 * read of one, from inside a draw handler, several files from the declaration
 * that was dropped. A script that declares more than this is now refused
 * outright (PluginLua_AddScript), and must stay under TORIRS_PLUGIN_CONFIG_MAX
 * as well -- the store the host keeps the VALUES in, which also has to hold
 * whatever stray keys an older ini left behind.
 */
#define PLUGIN_LUA_MAX_CONFIG 32
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
    /* Empty when the script declared none, which the host reads as "derive one
     * from the name" -- so this is not seeded with the name here. */
    char title[TORIRS_PLUGIN_TITLE_MAX];
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
    /*
     * WHICH surface is open, because two of the draw verbs are legal on only
     * one of them. draw.tile and draw.hull name something in the scene, and
     * the host ASSERTS when they are called on a screen surface -- an abort is
     * the right answer to a C plugin's contract violation and the wrong one to
     * a typo in a script, so the binding refuses them here instead.
     */
    enum LuaSurface
    {
        LUA_SURFACE_NONE = 0,
        LUA_SURFACE_WORLD,
        LUA_SURFACE_CANVAS,
        LUA_SURFACE_PANEL
    } cur_surface_kind;
    /** Inside on_chrome: chrome.paint and chrome.ops are legal. */
    int cur_in_chrome;
    struct ToriRS_PluginEvMenuBuild* cur_menu;

    size_t mem_used;
    bool alive;

    /**
     * The script's text, kept so it can be re-executed.
     *
     * Reload is why: tearing the lua_State down and calling init again
     * re-subscribes the SAME function references, so without the source the
     * file's text is never run a second time and a "reload" changes nothing.
     * Held on the C heap rather than in the per-script Lua arena, because the
     * arena is exactly what a reload throws away.
     */
    char* source;
    int source_len;
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
    TORIRS_LOG("plugin: lua script '%s' disabled: %s\n", script->name, msg);
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

static void
lua_push_loc(lua_State* L, struct ToriRS_PluginLocSnap const* l)
{
    lua_createtable(L, 0, 11);
#define SETI(k, v)                                                                            \
    do                                                                                        \
    {                                                                                         \
        lua_pushinteger(L, (lua_Integer)(v));                                                 \
        lua_setfield(L, -2, (k));                                                             \
    } while( 0 )
    SETI("loc_id", l->loc_id);
    SETI("tile_x", l->tile_x);
    SETI("tile_z", l->tile_z);
    SETI("level", l->level);
    SETI("size_x", l->size_x);
    SETI("size_z", l->size_z);
    SETI("shape", l->shape);
    SETI("angle", l->angle);
    SETI("element_id", l->element_id);
    SETI("visible_ops", l->visible_ops);
#undef SETI
    lua_pushboolean(L, l->interactive != 0);
    lua_setfield(L, -2, "interactive");
    lua_pushstring(L, l->name);
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

    TORIRS_LOG("[%s]", script->name);
    for( int i = 1; i <= argc; i++ )
    {
        size_t len = 0;
        char const* s = luaL_tolstring(L, i, &len);
        TORIRS_LOG(" %s", s);
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
lua_api_frame_work_us(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushinteger(L, (lua_Integer)g_api->frame_work_us(script->cur_ctx));
    return 1;
}

static int
lua_api_memory_bytes(lua_State* L)
{
    uint64_t bytes = 0;

    (void)lua_upvalue_script(L);
    PlatformMemory_FootprintBytes(&bytes);
    lua_pushinteger(L, (lua_Integer)bytes);
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

/* A table or nil, so `if api.hover_entity() then` reads the miss. */
static int
lua_api_hover_entity(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    struct ToriRS_PluginHoverEntity hit;

    if( !g_api->hover_entity(script->cur_ctx, &hit) )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 5);
    lua_pushinteger(L, hit.kind);
    lua_setfield(L, -2, "kind");
    lua_pushinteger(L, hit.element_id);
    lua_setfield(L, -2, "element_id");
    lua_pushinteger(L, hit.tile_x);
    lua_setfield(L, -2, "tile_x");
    lua_pushinteger(L, hit.tile_z);
    lua_setfield(L, -2, "tile_z");
    lua_pushinteger(L, hit.level);
    lua_setfield(L, -2, "level");
    return 1;
}

static int
lua_api_notify(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->notify(script->cur_ctx, luaL_tolstring(L, 1, NULL));
    return 0;
}

static int
lua_api_varbit(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushinteger(L, g_api->varbit(script->cur_ctx, (int)luaL_checkinteger(L, 1)));
    return 1;
}

static int
lua_api_varp(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushinteger(L, g_api->varp(script->cur_ctx, (int)luaL_checkinteger(L, 1)));
    return 1;
}

static int
lua_api_setting_color(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const varp_id = (int)luaL_checkinteger(L, 1);
    uint32_t const fallback = (uint32_t)luaL_optinteger(L, 2, 0);
    lua_pushinteger(L, (lua_Integer)g_api->setting_color(script->cur_ctx, varp_id, fallback));
    return 1;
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

/* ------------------------------------------------------- the window tab
 *
 * `api.window.*`, a thin forward of the win_* verbs. Kinds arrive as STRINGS
 * ("checkbox", "input") rather than as numeric constants, because a script that
 * has to be handed a table of enum values in order to name a checkbox is a
 * script whose api leaked the C header into it -- and a typo'd string can be
 * reported by name, where a wrong integer cannot.
 */

static int
lua_window_kind_from_name(char const* name)
{
    if( !name )
        return -1;
    if( strcmp(name, "label") == 0 )
        return TORIRS_PLUGIN_W_LABEL;
    if( strcmp(name, "checkbox") == 0 )
        return TORIRS_PLUGIN_W_CHECKBOX;
    if( strcmp(name, "input") == 0 )
        return TORIRS_PLUGIN_W_INPUT;
    if( strcmp(name, "dropdown") == 0 )
        return TORIRS_PLUGIN_W_DROPDOWN;
    if( strcmp(name, "button") == 0 )
        return TORIRS_PLUGIN_W_BUTTON;
    if( strcmp(name, "separator") == 0 )
        return TORIRS_PLUGIN_W_SEPARATOR;
    if( strcmp(name, "section") == 0 )
        return TORIRS_PLUGIN_W_SECTION;
    if( strcmp(name, "paragraph") == 0 )
        return TORIRS_PLUGIN_W_PARAGRAPH;
    if( strcmp(name, "key_value") == 0 )
        return TORIRS_PLUGIN_W_KEY_VALUE;
    if( strcmp(name, "toggle") == 0 )
        return TORIRS_PLUGIN_W_TOGGLE;
    if( strcmp(name, "textarea") == 0 )
        return TORIRS_PLUGIN_W_TEXTAREA;
    if( strcmp(name, "list_row") == 0 )
        return TORIRS_PLUGIN_W_LIST_ROW;
    if( strcmp(name, "image") == 0 )
        return TORIRS_PLUGIN_W_IMAGE;
    if( strcmp(name, "progress") == 0 )
        return TORIRS_PLUGIN_W_PROGRESS;
    if( strcmp(name, "error") == 0 )
        return TORIRS_PLUGIN_W_ERROR;
    if( strcmp(name, "custom") == 0 )
        return TORIRS_PLUGIN_W_CUSTOM;
    return -1;
}


/* ------------------------------------------------------------------ layout */

/*
 * The layout region a call is about, carried as a SECOND upvalue.
 *
 * `api.layout.safe_gamechrome.reserve(...)` and `api.layout.viewport.rect()` are the same
 * two C functions closed over different regions, which is what lets the
 * scripted surface be per-region names -- the shape the C side reads as an
 * argument -- without a function per region per verb.
 */
static int
lua_layout_slot(lua_State* L)
{
    return (int)lua_tointeger(L, lua_upvalueindex(2));
}

/** The named edge, or -1. */
static int
lua_layout_edge_from_name(char const* name)
{
    if( !name )
        return -1;
    if( strcmp(name, "left") == 0 )
        return TORIRS_PLUGIN_EDGE_LEFT;
    if( strcmp(name, "right") == 0 )
        return TORIRS_PLUGIN_EDGE_RIGHT;
    if( strcmp(name, "top") == 0 )
        return TORIRS_PLUGIN_EDGE_TOP;
    if( strcmp(name, "bottom") == 0 )
        return TORIRS_PLUGIN_EDGE_BOTTOM;
    return -1;
}

/**
 * `rect([member])` -> `{x=,y=,w=,h=}`, or nil.
 *
 * With no argument, the region as a whole. With one, that MEMBER of it -- the
 * role's own numbering, so `chat_buttons.rect(3)` is the report abuse button
 * and not the fourth chat button this cache happened to build. Nil for a
 * region (or a member) this gameframe does not have, which is an answer: a
 * caller draws nothing rather than guessing a box.
 */
static int
lua_layout_rect(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const slot = lua_layout_slot(L);
    int const want_member = !lua_isnoneornil(L, 1);
    int const member = want_member ? (int)luaL_checkinteger(L, 1) : -1;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int got;

    got = want_member ? g_api->slot_member_rect(script->cur_ctx, slot, member, &x, &y, &w, &h)
                      : g_api->slot_rect(script->cur_ctx, slot, &x, &y, &w, &h);
    if( !got )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, w);
    lua_setfield(L, -2, "w");
    lua_pushinteger(L, h);
    lua_setfield(L, -2, "h");
    return 1;
}

static int
lua_layout_reserve(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* edge_name = luaL_checkstring(L, 1);
    int const px = (int)luaL_checkinteger(L, 2);
    int const edge = lua_layout_edge_from_name(edge_name);

    if( edge < 0 )
        return luaL_error(
            L, "reserve: '%s' is not an edge; use left, right, top or bottom",
            edge_name);
    lua_pushboolean(
        L, g_api->layout_reserve(script->cur_ctx, lua_layout_slot(L), edge, px));
    return 1;
}

/** Give back every edge of this region this script had taken. */
static int
lua_layout_release(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const slot = lua_layout_slot(L);

    for( int edge = 0; edge < TORIRS_PLUGIN_EDGE_COUNT; edge++ )
        g_api->layout_reserve(script->cur_ctx, slot, edge, 0);
    return 0;
}

/**
 * `replace(rect)` on a PLACEABLE region: state where the frame puts it.
 *
 * The exclusive verb, and the host enforces the rest of what that means --
 * legal only for the plugin that owns the frame and only inside its layout
 * handler. A script calling it anywhere else is told so rather than quietly
 * doing nothing.
 */
static int
lua_layout_replace(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const slot = lua_layout_slot(L);
    int x;
    int y;
    int w;
    int h;

    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "x");
    x = (int)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);
    lua_getfield(L, 1, "y");
    y = (int)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);
    lua_getfield(L, 1, "w");
    w = (int)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);
    lua_getfield(L, 1, "h");
    h = (int)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);

    lua_pushboolean(L, g_api->layout_slot(script->cur_ctx, slot, x, y, w, h));
    return 1;
}

/** `api.layout.top_level.replace([opts])` -- own the whole gameframe. */
static int
lua_layout_top_level_replace(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int canvas = TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW;
    int w = 0;
    int h = 0;

    if( lua_istable(L, 1) )
    {
        lua_getfield(L, 1, "w");
        w = (int)luaL_optinteger(L, -1, 0);
        lua_pop(L, 1);
        lua_getfield(L, 1, "h");
        h = (int)luaL_optinteger(L, -1, 0);
        lua_pop(L, 1);
        if( w > 0 && h > 0 )
            canvas = TORIRS_PLUGIN_CANVAS_FIXED;
    }
    lua_pushboolean(L, g_api->layout_claim(script->cur_ctx, canvas, w, h));
    return 1;
}

static int
lua_layout_top_level_release(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->layout_release(script->cur_ctx);
    return 0;
}

static int
lua_layout_top_level_rect(lua_State* L)
{
    return lua_layout_rect(L);
}

static int
lua_layout_revision(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushinteger(L, g_api->layout_revision(script->cur_ctx));
    return 1;
}

/* -------------------------------------------------------------- roles */

/*
 * The semantic role a call is about, carried as a SECOND upvalue, exactly as a
 * layout region is.
 *
 * Same argument, one level further out: a role is a thing a script talks about
 * repeatedly, and a string argument repeated at every call site is a typo
 * waiting to be a silent no-op. `api.role("report_button")` binds the name
 * once and a misspelling of the VERB is then a nil index at the point of the
 * mistake. The name itself cannot be checked at bind time and deliberately is
 * not: the vocabulary is open, and a role this revision has not bound is a
 * legitimate answer rather than an error -- the same script has to run on the
 * lane whose profile names it and on the one whose profile does not.
 */
static char const*
lua_role_name(lua_State* L)
{
    return lua_tostring(L, lua_upvalueindex(2));
}

/** `rect()` -> `{x=,y=,w=,h=}`, or nil for a role this revision has not bound. */
static int
lua_role_rect(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    if( !g_api->role_rect(script->cur_ctx, lua_role_name(L), &x, &y, &w, &h) )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, w);
    lua_setfield(L, -2, "w");
    lua_pushinteger(L, h);
    lua_setfield(L, -2, "h");
    return 1;
}

static int
lua_role_visible(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushboolean(L, g_api->role_visible(script->cur_ctx, lua_role_name(L)));
    return 1;
}

/**
 * `replace([enabled])` -- suppress the native role while plugin chrome stands
 * in its place. The claim is persistent host state, not a per-frame drawing,
 * so scripts reconcile it at start/config changes and explicitly release it
 * when their mode changes. Omitted means true, matching the ordinary
 * `role.replace()` spelling.
 */
static int
lua_role_replace(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int enabled = 1;

    if( !lua_isnoneornil(L, 1) )
    {
        luaL_checktype(L, 1, LUA_TBOOLEAN);
        enabled = lua_toboolean(L, 1);
    }
    lua_pushboolean(
        L, g_api->role_replace(script->cur_ctx, lua_role_name(L), enabled));
    return 1;
}

/**
 * `anchor()` -- attach the rest of this canvas subscriber's declarations to
 * the role's local paint boundary. The host clears the stamp when the
 * subscriber returns, so a script cannot accidentally retarget the next
 * plugin's drawing.
 */
static int
lua_role_anchor(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);

    /* api.role.anchor(role [, "before"|"after"]) -- after unless said. */
    int place = TORIRS_PLUGIN_ANCHOR_AFTER;
    if( lua_isstring(L, 2) && strcmp(lua_tostring(L, 2), "before") == 0 )
        place = TORIRS_PLUGIN_ANCHOR_BEFORE;
    lua_pushboolean(L, g_api->role_anchor(script->cur_ctx, lua_role_name(L), place));
    return 1;
}

/** `click([op])` -- op defaults to 0, the classic unnumbered button. */
static int
lua_role_click(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushboolean(
        L, g_api->role_click(script->cur_ctx, lua_role_name(L), (int)luaL_optinteger(L, 1, 0)));
    return 1;
}

/** `id()` -- the component id right now, or nil. Do not keep it; @see role_id. */
static int
lua_role_id(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const id = g_api->role_id(script->cur_ctx, lua_role_name(L));

    if( id < 0 )
        lua_pushnil(L);
    else
        lua_pushinteger(L, id);
    return 1;
}

/**
 * `api.role(name)` -> the verb table for that role.
 *
 * A constructor rather than a pre-built table per name, because the vocabulary
 * is OPEN -- a profile may name anything, and there is no list here to build
 * from. The tables are cached (upvalue 2) so a script that calls this in a
 * frame handler is not allocating one per frame; a script that hoists it is
 * doing the same thing, only visibly.
 */
/* -- chrome and entities: the claim tier ---------------------------------
 *
 * `api.chrome.*` and `api.entity.*`, flat verbs taking the part name, because
 * a claim is stated once at start and the name is not repeated at every
 * frame the way a region's is. Scopes are a table of strings or a single
 * string: {"appearance", "hitbox"}.
 */

static int
lua_scopes_arg(lua_State* L, int idx)
{
    static const struct
    {
        char const* name;
        int bit;
    } SCOPES[] = {
        { "position", TORIRS_PLUGIN_CHROME_SCOPE_POSITION },
        { "appearance", TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE },
        { "hitbox", TORIRS_PLUGIN_CHROME_SCOPE_HITBOX },
        { "all", TORIRS_PLUGIN_CHROME_SCOPE_ALL },
    };
    int mask = 0;

    if( lua_isnoneornil(L, idx) )
        return TORIRS_PLUGIN_CHROME_SCOPE_ALL;
    if( lua_isinteger(L, idx) )
        return (int)lua_tointeger(L, idx) & TORIRS_PLUGIN_CHROME_SCOPE_ALL;

    lua_Integer const n = lua_istable(L, idx) ? luaL_len(L, idx) : 1;
    for( lua_Integer i = 1; i <= n; i++ )
    {
        char const* name;
        int found = 0;
        if( lua_istable(L, idx) )
            lua_rawgeti(L, idx, i);
        else
            lua_pushvalue(L, idx);
        name = lua_tostring(L, -1);
        for( size_t s = 0; name && s < sizeof(SCOPES) / sizeof(SCOPES[0]); s++ )
            if( strcmp(name, SCOPES[s].name) == 0 )
            {
                mask |= SCOPES[s].bit;
                found = 1;
            }
        lua_pop(L, 1);
        if( !found )
            return luaL_error(L, "unknown scope '%s'", name ? name : "?");
    }
    return mask;
}

static void
lua_push_scopes(lua_State* L, int mask)
{
    lua_createtable(L, 0, 3);
    lua_pushboolean(L, (mask & TORIRS_PLUGIN_CHROME_SCOPE_POSITION) != 0);
    lua_setfield(L, -2, "position");
    lua_pushboolean(L, (mask & TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE) != 0);
    lua_setfield(L, -2, "appearance");
    lua_pushboolean(L, (mask & TORIRS_PLUGIN_CHROME_SCOPE_HITBOX) != 0);
    lua_setfield(L, -2, "hitbox");
}

/** A ChromePart from a table {x,y,w,h, idle=,hover=,active=,active_hover=,
 *  disabled=, label_x=, label_y=}. Missing art is -1. */
static void
lua_part_arg(lua_State* L, int idx, struct ToriRS_PluginChromePart* out)
{
    static char const* const STATE_KEY[TORIRS_PLUGIN_CHROME_STATE_COUNT] = {
        "idle", "hover", "active", "active_hover", "disabled",
    };
    memset(out, 0, sizeof(*out));
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_STATE_COUNT; i++ )
        out->art[i] = -1;
    if( !lua_istable(L, idx) )
        return;
#define LUA_PART_INT(field, key)                                                   \
    do                                                                             \
    {                                                                              \
        lua_getfield(L, idx, key);                                                 \
        out->field = (int)luaL_optinteger(L, -1, out->field);                      \
        lua_pop(L, 1);                                                             \
    } while( 0 )
    LUA_PART_INT(x, "x");
    LUA_PART_INT(y, "y");
    LUA_PART_INT(w, "w");
    LUA_PART_INT(h, "h");
    LUA_PART_INT(label_x, "label_x");
    LUA_PART_INT(label_y, "label_y");
#undef LUA_PART_INT
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_STATE_COUNT; i++ )
    {
        lua_getfield(L, idx, STATE_KEY[i]);
        out->art[i] = (int)luaL_optinteger(L, -1, -1);
        lua_pop(L, 1);
    }
}

static void
lua_push_part(lua_State* L, struct ToriRS_PluginChromePart const* part)
{
    static char const* const STATE_KEY[TORIRS_PLUGIN_CHROME_STATE_COUNT] = {
        "idle", "hover", "active", "active_hover", "disabled",
    };
    static char const* const SOURCE_NAME[] = { "none", "lane", "frame", "added" };
    lua_createtable(L, 0, 12);
    lua_pushinteger(L, part->x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, part->y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, part->w);
    lua_setfield(L, -2, "w");
    lua_pushinteger(L, part->h);
    lua_setfield(L, -2, "h");
    lua_pushinteger(L, part->label_x);
    lua_setfield(L, -2, "label_x");
    lua_pushinteger(L, part->label_y);
    lua_setfield(L, -2, "label_y");
    for( int i = 0; i < TORIRS_PLUGIN_CHROME_STATE_COUNT; i++ )
        if( part->art[i] >= 0 )
        {
            lua_pushinteger(L, part->art[i]);
            lua_setfield(L, -2, STATE_KEY[i]);
        }
    lua_pushstring(L, part->source >= 0 && part->source < 4 ? SOURCE_NAME[part->source] : "none");
    lua_setfield(L, -2, "source");
}

/** api.chrome.claim(part [, scopes]) -> held-scopes table, or nil for a part
 *  this revision has not got. */
static int
lua_chrome_claim(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* part = luaL_checkstring(L, 1);
    int const scopes = lua_scopes_arg(L, 2);
    int const got = g_api->chrome_claim(script->cur_ctx, part, scopes, 1);
    if( got < 0 )
        lua_pushnil(L);
    else
        lua_push_scopes(L, got);
    return 1;
}

/** api.chrome.release(part [, scopes]) */
static int
lua_chrome_release(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* part = luaL_checkstring(L, 1);
    g_api->chrome_claim(script->cur_ctx, part, lua_scopes_arg(L, 2), 0);
    return 0;
}

/** api.chrome.add(part, anchor [, initial]) -> held-scopes table, or nil. */
static int
lua_chrome_add(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* part = luaL_checkstring(L, 1);
    char const* anchor = luaL_checkstring(L, 2);
    struct ToriRS_PluginChromePart initial;
    int got;
    lua_part_arg(L, 3, &initial);
    /* api.chrome.add(part, anchor [, initial [, "before"|"after"]]) */
    got = g_api->chrome_add(
        script->cur_ctx,
        part,
        anchor,
        lua_isstring(L, 4) && strcmp(lua_tostring(L, 4), "before") == 0
            ? TORIRS_PLUGIN_ANCHOR_BEFORE
            : TORIRS_PLUGIN_ANCHOR_AFTER,
        lua_istable(L, 3) ? &initial : NULL);
    if( got < 0 )
        lua_pushnil(L);
    else
        lua_push_scopes(L, got);
    return 1;
}

/** api.chrome.owner(part [, scope]) -> title or nil */
static int
lua_chrome_owner(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* part = luaL_checkstring(L, 1);
    int scope = lua_isnoneornil(L, 2) ? TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE : lua_scopes_arg(L, 2);
    char const* who = g_api->chrome_owner(script->cur_ctx, part, scope);
    if( who )
        lua_pushstring(L, who);
    else
        lua_pushnil(L);
    return 1;
}

/** api.chrome.claimed(part [, scopes]) -> scopes table held by others */
static int
lua_chrome_claimed(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* part = luaL_checkstring(L, 1);
    lua_push_scopes(L, g_api->chrome_claimed(script->cur_ctx, part, lua_scopes_arg(L, 2)));
    return 1;
}

/** api.chrome.part(part) -> part table or nil */
static int
lua_chrome_part(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* part = luaL_checkstring(L, 1);
    struct ToriRS_PluginChromePart out;
    if( !g_api->chrome_part(script->cur_ctx, part, &out) )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_push_part(L, &out);
    return 1;
}

/** api.chrome.paint(part, {x,y,w,h, idle=..}) -- inside on_chrome only. */
static int
lua_chrome_paint(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* part = luaL_checkstring(L, 1);
    struct ToriRS_PluginChromePart art;
    if( !script->cur_in_chrome )
        return luaL_error(L, "chrome.paint is only legal inside on_chrome");
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_part_arg(L, 2, &art);
    lua_pushboolean(L, g_api->chrome_paint(script->cur_ctx, part, &art));
    return 1;
}

/** Read a string-or-list of ops at `idx` into `ops`. */
static int
lua_ops_arg(lua_State* L, int idx, char const** ops)
{
    int op_count = 0;
    if( lua_isstring(L, idx) )
        ops[op_count++] = lua_tostring(L, idx);
    else if( lua_istable(L, idx) )
    {
        lua_Integer const n = luaL_len(L, idx);
        for( lua_Integer i = 1; i <= n && op_count < TORIRS_PLUGIN_REGION_OPS_MAX; i++ )
        {
            lua_rawgeti(L, idx, i);
            if( lua_type(L, -1) == LUA_TSTRING )
                ops[op_count++] = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
    }
    else if( !lua_isnoneornil(L, idx) )
        return luaL_error(L, "ops must be a string or a list of strings");
    return op_count;
}

/** api.chrome.ops(part, ops [, tag]) -- inside on_chrome only. */
static int
lua_chrome_ops(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* part = luaL_checkstring(L, 1);
    char const* ops[TORIRS_PLUGIN_REGION_OPS_MAX];
    int op_count;
    if( !script->cur_in_chrome )
        return luaL_error(L, "chrome.ops is only legal inside on_chrome");
    op_count = lua_ops_arg(L, 2, ops);
    lua_pushboolean(
        L, g_api->chrome_ops(script->cur_ctx, part, ops, op_count, (uint32_t)luaL_optinteger(L, 3, 0)));
    return 1;
}

/** api.chrome.state(part, "idle"|"hover"|"active"|"active_hover"|"disabled") */
static int
lua_chrome_state(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* part = luaL_checkstring(L, 1);
    static char const* const NAMES[] = { "idle", "hover", "active", "active_hover", "disabled", NULL };
    int const state = luaL_checkoption(L, 2, "idle", NAMES);
    lua_pushboolean(L, g_api->chrome_state(script->cur_ctx, part, state));
    return 1;
}

/** api.entity.part(kind, a [, b, c, d]) -> name. kind is "npc"|"player"|"loc"|"obj". */
static int
lua_entity_part(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    static char const* const KINDS[] = { "npc", "player", "loc", "obj", NULL };
    int const kind = luaL_checkoption(L, 1, NULL, KINDS) + 1;
    char buf[TORIRS_PLUGIN_ROLE_NAME_MAX];
    char const* name = g_api->entity_part(
        script->cur_ctx,
        kind,
        (int)luaL_checkinteger(L, 2),
        (int)luaL_optinteger(L, 3, 0),
        (int)luaL_optinteger(L, 4, 0),
        (int)luaL_optinteger(L, 5, 0),
        buf,
        (int)sizeof(buf));
    if( name )
        lua_pushstring(L, name);
    else
        lua_pushnil(L);
    return 1;
}

/** api.entity.look(part, {hull=true, rgb=0xRRGGBB, fill=alpha, shape="bounds"|"mesh"}) */
static int
lua_entity_look(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* part = luaL_checkstring(L, 1);
    struct ToriRS_PluginEntityLook look;
    static char const* const SHAPES[] = { "bounds", "mesh", NULL };

    luaL_checktype(L, 2, LUA_TTABLE);
    memset(&look, 0, sizeof(look));
    lua_getfield(L, 2, "hull");
    look.hull = lua_isnoneornil(L, -1) ? 1 : lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "rgb");
    look.rgb = (uint32_t)luaL_optinteger(L, -1, 0xFFFFFF);
    lua_pop(L, 1);
    lua_getfield(L, 2, "fill");
    look.fill_alpha = (int)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);
    lua_getfield(L, 2, "shape");
    look.shape = luaL_checkoption(L, -1, "bounds", SHAPES) == 0 ? TORIRS_PLUGIN_HULL_BOUNDS
                                                               : TORIRS_PLUGIN_HULL_MESH;
    lua_pop(L, 1);
    lua_pushboolean(L, g_api->entity_look(script->cur_ctx, part, &look));
    return 1;
}

/** api.entity.ops(part, "append"|"replace"|"none", ops [, tag]) */
static int
lua_entity_ops(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* part = luaL_checkstring(L, 1);
    static char const* const MODES[] = { "append", "replace", "none", NULL };
    int const mode = luaL_checkoption(L, 2, "append", MODES);
    char const* ops[TORIRS_PLUGIN_REGION_OPS_MAX];
    int const op_count = lua_ops_arg(L, 3, ops);
    lua_pushboolean(
        L,
        g_api->entity_ops(
            script->cur_ctx, part, mode, ops, op_count, (uint32_t)luaL_optinteger(L, 4, 0)));
    return 1;
}

static int
lua_api_role(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* name = luaL_checkstring(L, 1);
    static const struct
    {
        char const* name;
        lua_CFunction fn;
    } VERBS[] = {
        { "rect", lua_role_rect },
        { "visible", lua_role_visible },
        { "replace", lua_role_replace },
        { "anchor", lua_role_anchor },
        { "click", lua_role_click },
        { "id", lua_role_id },
    };

    lua_pushvalue(L, lua_upvalueindex(2));
    lua_getfield(L, -1, name);
    if( lua_istable(L, -1) )
        return 1;
    lua_pop(L, 1);

    lua_createtable(L, 0, (int)(sizeof(VERBS) / sizeof(VERBS[0])));
    for( size_t v = 0; v < sizeof(VERBS) / sizeof(VERBS[0]); v++ )
    {
        lua_pushlightuserdata(L, script);
        lua_pushstring(L, name);
        lua_pushcclosure(L, VERBS[v].fn, 2);
        lua_setfield(L, -2, VERBS[v].name);
    }
    /* Into the cache, and left on the stack as the return value. */
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name);
    return 1;
}

static int
lua_window_request(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* title = luaL_optstring(L, 1, script->name);
    lua_pushboolean(L, g_api->win_request(script->cur_ctx, title));
    return 1;
}

static int
lua_window_widget(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* kind_name = luaL_checkstring(L, 1);
    char const* id = luaL_checkstring(L, 2);
    char const* label = luaL_optstring(L, 3, NULL);
    int const kind = lua_window_kind_from_name(kind_name);

    /* Named, not numbered: the message says which word was wrong. */
    if( kind < 0 )
        return luaL_error(
            L,
            "window.widget: unknown kind '%s' (label, checkbox, input, dropdown, button, "
            "separator)",
            kind_name);

    lua_pushboolean(L, g_api->win_widget(script->cur_ctx, kind, id, label));
    return 1;
}

static int
lua_window_set_text(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* id = luaL_checkstring(L, 1);
    char const* text = luaL_tolstring(L, 2, NULL);
    lua_pushboolean(L, g_api->win_set_text(script->cur_ctx, id, text));
    return 1;
}

static int
lua_window_set_checked(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* id = luaL_checkstring(L, 1);
    lua_pushboolean(L, g_api->win_set_checked(script->cur_ctx, id, lua_toboolean(L, 2) != 0));
    return 1;
}

static int
lua_window_set_options(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* id = luaL_checkstring(L, 1);
    char const* choices = luaL_checkstring(L, 2);
    /* 1-based in Lua, as every index a script writes is; -1 stays "none". */
    int const selected = (int)luaL_optinteger(L, 3, 0) - 1;
    lua_pushboolean(L, g_api->win_set_options(script->cur_ctx, id, choices, selected));
    return 1;
}

static int
lua_window_clear(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->win_clear(script->cur_ctx);
    return 0;
}

/* ------------------------------------------------ application panel ---- */

static int
lua_panel_request(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    struct ToriRS_PluginPanelDesc desc;
    char icon[TORIRS_PLUGIN_ASSET_NAME_MAX];

    memset(&desc, 0, sizeof(desc));
    icon[0] = '\0';
    if( lua_istable(L, 1) )
    {
        lua_getfield(L, 1, "icon");
        if( lua_type(L, -1) == LUA_TSTRING )
            snprintf(icon, sizeof(icon), "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 1, "icon_asset");
        if( !icon[0] && lua_type(L, -1) == LUA_TSTRING )
            snprintf(icon, sizeof(icon), "%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 1, "preferred_width");
        if( lua_isinteger(L, -1) )
            desc.preferred_width = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    else if( !lua_isnoneornil(L, 1) )
        /*
         * The bare-string form only ever meant a TITLE, and a page has none:
         * the rail entry is named by the plugin's own manifest title, which no
         * page it registers can rename. Reading the string as anything else
         * would quietly give an old script a different registration than the
         * one it wrote. @see struct ToriRS_PluginPanelDesc.
         */
        return luaL_error(
            L,
            "panel.request: pass a table -- a page carries no title of its own, "
            "so the string form has no meaning");

    desc.icon_asset = icon[0] ? icon : NULL;
    lua_pushboolean(L, g_api->panel_request(script->cur_ctx, &desc));
    return 1;
}

static int
lua_panel_widget(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* kind_name = luaL_checkstring(L, 1);
    char const* id = luaL_checkstring(L, 2);
    char const* label = luaL_optstring(L, 3, NULL);
    int const kind = lua_window_kind_from_name(kind_name);

    if( kind < 0 )
        return luaL_error(L, "panel.widget: unknown kind '%s'", kind_name);
    lua_pushboolean(L, g_api->panel_widget(script->cur_ctx, kind, id, label));
    return 1;
}

static int
lua_panel_set_text(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* id = luaL_checkstring(L, 1);
    char const* value = luaL_tolstring(L, 2, NULL);
    lua_pushboolean(L, g_api->panel_set_text(script->cur_ctx, id, value));
    return 1;
}

static int
lua_panel_set_value(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* id = luaL_checkstring(L, 1);
    int value;

    if( lua_isboolean(L, 2) )
        value = lua_toboolean(L, 2) ? 1 : 0;
    else
        value = (int)luaL_checkinteger(L, 2);
    lua_pushboolean(L, g_api->panel_set_value(script->cur_ctx, id, value));
    return 1;
}

static int
lua_panel_set_height(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* id = luaL_checkstring(L, 1);
    int const height = (int)luaL_checkinteger(L, 2);

    lua_pushboolean(
        L, g_api->panel_set_height(script->cur_ctx, id, height));
    return 1;
}

static int
lua_panel_set_options(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* id = luaL_checkstring(L, 1);
    char const* choices = luaL_checkstring(L, 2);
    int const selected = (int)luaL_optinteger(L, 3, 0) - 1;
    lua_pushboolean(L, g_api->panel_set_options(script->cur_ctx, id, choices, selected));
    return 1;
}

static int
lua_panel_set_attention(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushboolean(
        L,
        g_api->panel_set_attention(script->cur_ctx, lua_toboolean(L, 1) != 0));
    return 1;
}

static int
lua_panel_clear(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->panel_clear(script->cur_ctx);
    return 0;
}

static int
lua_panel_invalidate(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->panel_invalidate(script->cur_ctx, luaL_checkstring(L, 1));
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

/*
 * draw.tile(x, z, level [, colour [, fill_colour, fill_opacity]])
 *
 * The fill's colour comes before its opacity, and the opacity is CHECKED
 * rather than defaulted: a script written against the old (colour, opacity)
 * pair would otherwise read its opacity as a fill colour and quietly draw no
 * wash at all. Asking for a fill means saying what colour it is.
 */
static int
lua_draw_tile(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const tx = (int)luaL_checkinteger(L, 1);
    int const tz = (int)luaL_checkinteger(L, 2);
    int const level = (int)luaL_checkinteger(L, 3);
    uint32_t const rgb = lua_check_color(L, 4);
    int const want_fill = !lua_isnoneornil(L, 5);
    uint32_t const fill_rgb = want_fill ? lua_check_color(L, 5) : rgb;
    int const fill = want_fill ? (int)luaL_checkinteger(L, 6) : 0;

    if( script->cur_surface_kind != LUA_SURFACE_WORLD )
        return luaL_error(
            L,
            "%s names something in the scene, so it is only legal inside "
            "on_draw_world",
            "draw.tile");
    g_api->draw_tile(
        script->cur_ctx, script->cur_surface, tx, tz, level, rgb, fill_rgb, fill);
    return 0;
}

/*
 * Hull shape by name.
 *
 * A name rather than the enum's number because the number would have to be
 * exported into every script's globals to be usable, and a script that got it
 * wrong would silently draw the other shape. An unknown name raises instead,
 * which is what makes a typo a message naming the shapes rather than a
 * highlight that quietly stays square.
 */
static int
lua_check_hull_shape(lua_State* L, int idx)
{
    char const* name;

    if( lua_isnoneornil(L, idx) )
        return TORIRS_PLUGIN_HULL_BOUNDS;

    name = luaL_checkstring(L, idx);
    if( strcmp(name, "bounds") == 0 )
        return TORIRS_PLUGIN_HULL_BOUNDS;
    if( strcmp(name, "mesh") == 0 )
        return TORIRS_PLUGIN_HULL_MESH;
    return luaL_error(L, "unknown hull shape '%s' (want \"bounds\" or \"mesh\")", name);
}

static int
lua_draw_hull(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const element_id = (int)luaL_checkinteger(L, 1);
    uint32_t const rgb = lua_check_color(L, 2);
    int const fill = (int)luaL_optinteger(L, 3, 0);
    int const shape = lua_check_hull_shape(L, 4);

    if( script->cur_surface_kind != LUA_SURFACE_WORLD )
        return luaL_error(
            L,
            "%s names something in the scene, so it is only legal inside "
            "on_draw_world",
            "draw.hull");
    g_api->draw_hull(script->cur_ctx, script->cur_surface, element_id, rgb, fill, shape);
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
        return luaL_error(
            L, "draw calls are only legal inside on_draw_world / on_draw_canvas");
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
        return luaL_error(
            L, "draw calls are only legal inside on_draw_world / on_draw_canvas");
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
        return luaL_error(
            L, "draw calls are only legal inside on_draw_world / on_draw_canvas");
    g_api->draw_rect(script->cur_ctx, script->cur_surface, x, y, w, h, rgb, fill);
    return 0;
}

/*
 * draw.image(handle, x, y [, trans [, clip_x, clip_y, clip_w, clip_h]])
 *
 * `trans` is the reference's sense and not the fill alpha the other verbs
 * take: 0 is opaque and 255 is invisible, matching every sprite blit in the
 * client. The clip is an extra rectangle to cut the blit to, in the surface's
 * own coordinates -- what a METER is made of, and the reason it is an argument
 * rather than a second verb.
 *
 * An image whose read has not landed draws nothing, which is the ordinary
 * state for the first frames after image_load and not an error.
 */
static int
lua_draw_image(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const image = (int)luaL_checkinteger(L, 1);
    int const x = (int)luaL_checkinteger(L, 2);
    int const y = (int)luaL_checkinteger(L, 3);
    int const trans = (int)luaL_optinteger(L, 4, 0);
    int const clip_x = (int)luaL_optinteger(L, 5, 0);
    int const clip_y = (int)luaL_optinteger(L, 6, 0);
    int const clip_w = (int)luaL_optinteger(L, 7, 0);
    int const clip_h = (int)luaL_optinteger(L, 8, 0);

    if( !script->cur_surface )
        return luaL_error(
            L, "draw calls are only legal inside on_draw_world / on_draw_canvas");
    g_api->draw_image(
        script->cur_ctx, script->cur_surface, image, x, y, clip_x, clip_y, clip_w, clip_h, trans);
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

/* `for loc in api.locs() do`; same cursor shape as the other two. */
static int
lua_loc_iter(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    struct ToriRS_PluginLocSnap snap;
    int const prev = (int)lua_tointeger(L, lua_upvalueindex(2));
    int const next = g_api->loc_next(script->cur_ctx, prev, &snap);

    if( next < 0 )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, next);
    lua_replace(L, lua_upvalueindex(2));
    lua_push_loc(L, &snap);
    return 1;
}

static int
lua_api_locs(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    lua_pushlightuserdata(L, script);
    lua_pushinteger(L, -1);
    lua_pushcclosure(L, lua_loc_iter, 2);
    return 1;
}

/* -- images --
 *
 * A PICTURE the plugin ships, out of the same asset folder api.asset_load
 * reads: a PNG the host decodes and hands back a handle for, which draw.image
 * blits. The bytes route of api.asset_data is deliberately not it -- a script
 * holding a PNG as a string would have to decode it in Lua to draw a single
 * pixel, and the host already has the decoder every C plugin uses.
 *
 * ASYNCHRONOUS, exactly like the C half: the read goes on the IO queue, so
 * image_size answers nil and draw.image draws nothing for the first frames
 * after a load. A script lays out against image_size and skips a frame rather
 * than waiting for anything.
 */

static int
lua_api_image_load(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* name = luaL_checkstring(L, 1);
    int const handle = g_api->image_load(script->cur_ctx, name);

    /* -1 is a refusal the script can do something about -- a bad name, or its
     * image budget -- so it comes back as nil rather than as a handle that
     * silently draws nothing forever. */
    if( handle < 0 )
        lua_pushnil(L);
    else
        lua_pushinteger(L, handle);
    return 1;
}

/** `w, h`, or nil while the read has not landed. */
static int
lua_api_image_size(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const image = (int)luaL_checkinteger(L, 1);
    int w = 0;
    int h = 0;

    if( !g_api->image_size(script->cur_ctx, image, &w, &h) )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

static int
lua_api_image_release(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->image_release(script->cur_ctx, (int)luaL_checkinteger(L, 1));
    return 0;
}

/* -- the interface's own widgets -- */

/**
 * `component_rect(id)` -> `{x=,y=,w=,h=}`, or nil.
 *
 * The read half of a component id, for the buttons the gameframe's ROLES do
 * not cover: a cache frame's chat filters are the interface's own widgets, so
 * api.layout.chat_buttons has no members to number and the id is the only
 * handle there is. `id` is `(interface << 16) | component`, the same number
 * the wire uses. Nil for a component this cache does not have or an interface
 * that is not open, which is an answer -- a caller draws nothing rather than
 * guessing a box.
 */
static int
lua_api_component_rect(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const component_id = (int)luaL_checkinteger(L, 1);
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    if( !g_api->component_rect(script->cur_ctx, component_id, &x, &y, &w, &h) )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);
    lua_setfield(L, -2, "y");
    lua_pushinteger(L, w);
    lua_setfield(L, -2, "w");
    lua_pushinteger(L, h);
    lua_setfield(L, -2, "h");
    return 1;
}

/* -- the pointer -- */

/** `x, y` in canvas coordinates, or nil when the client has no pointer. */
static int
lua_api_mouse_pos(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int x = 0;
    int y = 0;

    if( !g_api->mouse_pos(script->cur_ctx, &x, &y) )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    return 2;
}

/*
 * hit_region(x, y, w, h [, ops [, tag]])
 *
 * Claim a box of the canvas so what was drawn there can be clicked. `ops` is
 * one verb or a list of them: the first is the mouseover line and the LEFT
 * click, all of them are rows in the right-click menu. Omitted (or an empty
 * list) claims the pointer without offering anything, which is how a plugin
 * stops a click falling through to the world behind its own art.
 *
 * Declared with the drawing and never once at start, because the box comes
 * from where the frame put things THIS frame. @see
 * ToriRS_PluginApi::hit_region.
 */
static int
lua_api_hit_region(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const x = (int)luaL_checkinteger(L, 1);
    int const y = (int)luaL_checkinteger(L, 2);
    int const w = (int)luaL_checkinteger(L, 3);
    int const h = (int)luaL_checkinteger(L, 4);
    uint32_t const tag = (uint32_t)luaL_optinteger(L, 6, 0);
    char const* ops[TORIRS_PLUGIN_REGION_OPS_MAX];
    int op_count = 0;

    /* The world surface has no canvas coordinates to express a box in, and the
     * host asserts on one declared there. A script gets a message. */
    if( script->cur_surface_kind != LUA_SURFACE_CANVAS )
        return luaL_error(L, "hit_region is only legal inside on_draw_canvas");

    if( lua_isstring(L, 5) )
        ops[op_count++] = lua_tostring(L, 5);
    else if( lua_istable(L, 5) )
    {
        lua_Integer const n = luaL_len(L, 5);
        for( lua_Integer i = 1; i <= n && op_count < TORIRS_PLUGIN_REGION_OPS_MAX; i++ )
        {
            lua_rawgeti(L, 5, i);
            /* A STRING and not "stringable": lua_tostring on a number
             * converts the stack slot in place, and that copy dies with the
             * pop below while the pointer would live on in `ops`. */
            if( lua_type(L, -1) == LUA_TSTRING )
                /* Kept alive by the table, which is on the stack for the whole
                 * call -- the host copies each string into the region. */
                ops[op_count++] = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
    }
    else if( !lua_isnoneornil(L, 5) )
        return luaL_error(L, "hit_region: ops must be a string or a list of strings");

    lua_pushboolean(
        L,
        g_api->hit_region(
            script->cur_ctx, script->cur_surface, x, y, w, h, ops, op_count, tag));
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

/* -- screenshots --
 *
 * api.screenshot(name [, dir]) -> ok, path. The destination is the SECOND
 * argument and optional, because the common call is `api.screenshot(filename)`
 * and the directory is a config value a plugin passes through unchanged when
 * it has one -- `api.screenshot(name, api.config.destination)` reads correctly
 * even when the key is empty, which is what the default has to be for the
 * browser lane.
 *
 * The second return is where the file lands, which a script cannot work out
 * from what it passed in: the relative case resolves against a folder only the
 * engine knows. nil when the capture was refused, so `if ok then` and
 * `if path then` say the same thing. */
static int
lua_api_screenshot(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char const* name = luaL_checkstring(L, 1);
    char const* dir = luaL_optstring(L, 2, NULL);
    char path[TORIRS_PLUGIN_SCREENSHOT_PATH_MAX];
    int const ok = g_api->screenshot(script->cur_ctx, dir, name, path, (int)sizeof(path));

    lua_pushboolean(L, ok);
    if( ok )
        lua_pushstring(L, path);
    else
        lua_pushnil(L);
    return 2;
}

static int
lua_api_datestamp(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    char stamp[32];

    if( !g_api->datestamp(script->cur_ctx, stamp, (int)sizeof(stamp)) )
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, stamp);
    return 1;
}

/* -- shipped models -- */

static int
lua_api_model_load(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const handle = g_api->model_load(script->cur_ctx, luaL_checkstring(L, 1));

    if( handle < 0 )
        lua_pushnil(L);
    else
        lua_pushinteger(L, handle);
    return 1;
}

/* -- authored meshes -- */

/* nil rather than -1 wherever a budget can refuse, for the reason
 * lua_api_object_create gives: `if not m then` is the shape the script is
 * already writing around every other allocation here. */
static int
lua_api_mesh_create(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const handle = g_api->mesh_create(script->cur_ctx);

    if( handle < 0 )
        lua_pushnil(L);
    else
        lua_pushinteger(L, handle);
    return 1;
}

static int
lua_api_mesh_destroy(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->mesh_destroy(script->cur_ctx, (int)luaL_checkinteger(L, 1));
    return 0;
}

static int
lua_api_mesh_clear(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    g_api->mesh_clear(script->cur_ctx, (int)luaL_checkinteger(L, 1));
    return 0;
}

static int
lua_api_mesh_vertex(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const index = g_api->mesh_vertex(
        script->cur_ctx,
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3),
        (int)luaL_checkinteger(L, 4));

    if( index < 0 )
        lua_pushnil(L);
    else
        lua_pushinteger(L, index);
    return 1;
}

static int
lua_api_mesh_face(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    int const alpha = (int)luaL_optinteger(L, 6, 0);
    int index;

    /* Checked here rather than left to the host's assert: a transparency out
     * of range is a script's arithmetic going wrong, and a script error names
     * the line it went wrong on where an abort names the C frame under it. */
    if( alpha < 0 || alpha > TORIRS_PLUGIN_MESH_ALPHA_MAX )
        return luaL_error(
            L, "mesh_face: alpha must be 0..%d, got %d", TORIRS_PLUGIN_MESH_ALPHA_MAX, alpha);

    index = g_api->mesh_face(
        script->cur_ctx,
        (int)luaL_checkinteger(L, 1),
        (int)luaL_checkinteger(L, 2),
        (int)luaL_checkinteger(L, 3),
        (int)luaL_checkinteger(L, 4),
        (int)luaL_checkinteger(L, 5),
        alpha);

    if( index < 0 )
        lua_pushnil(L);
    else
        lua_pushinteger(L, index);
    return 1;
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
    enum ToriRS_PluginModelSource source;

    if( strcmp(kind, "model") == 0 )
        source = TORIRS_PLUGIN_MODEL_CACHE;
    else if( strcmp(kind, "spotanim") == 0 )
        source = TORIRS_PLUGIN_MODEL_SPOTANIM;
    else if( strcmp(kind, "mesh") == 0 )
        source = TORIRS_PLUGIN_MODEL_MESH;
    else if( strcmp(kind, "asset") == 0 )
        source = TORIRS_PLUGIN_MODEL_ASSET;
    else
        return luaL_error(
            L,
            "object_model: kind must be \"model\", \"spotanim\", \"mesh\" or "
            "\"asset\", got \"%s\"",
            kind);

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
        { "frame_work_us", lua_api_frame_work_us },
        { "memory_bytes", lua_api_memory_bytes },
        { "local_player", lua_api_local_player },
        { "npcs", lua_api_npcs },
        { "players", lua_api_players },
        { "npc_by_slot", lua_api_npc_by_slot },
        { "objs", lua_api_objs },
        { "locs", lua_api_locs },
        { "key_held", lua_api_key_held },
        { "hover_tile", lua_api_hover_tile },
        { "hover_entity", lua_api_hover_entity },
        { "notify", lua_api_notify },
        { "varbit", lua_api_varbit },
        { "varp", lua_api_varp },
        { "setting_color", lua_api_setting_color },
        { "project", lua_api_project },
        { "cfg_set", lua_api_cfg_set },
        { "image_load", lua_api_image_load },
        { "image_size", lua_api_image_size },
        { "image_release", lua_api_image_release },
        { "mouse_pos", lua_api_mouse_pos },
        { "component_rect", lua_api_component_rect },
        { "hit_region", lua_api_hit_region },
        { "asset_load", lua_api_asset_load },
        { "asset_data", lua_api_asset_data },
        { "asset_save", lua_api_asset_save },
        { "asset_release", lua_api_asset_release },
        { "screenshot", lua_api_screenshot },
        { "datestamp", lua_api_datestamp },
        { "model_load", lua_api_model_load },
        { "mesh_create", lua_api_mesh_create },
        { "mesh_destroy", lua_api_mesh_destroy },
        { "mesh_clear", lua_api_mesh_clear },
        { "mesh_vertex", lua_api_mesh_vertex },
        { "mesh_face", lua_api_mesh_face },
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

    /*
     * api.role: the constructor, closed over its own cache table.
     *
     * Not in FNS above because it needs a second upvalue, and the cache has to
     * be created here rather than lazily so that every call shares one.
     */
    lua_pushlightuserdata(L, script);
    lua_createtable(L, 0, 8);
    lua_pushcclosure(L, lua_api_role, 2);
    lua_setfield(L, -2, "role");

    /* api.chrome and api.entity: the claim tier. */
    {
        static const struct
        {
            char const* name;
            lua_CFunction fn;
        } CHROME[] = {
            { "claim", lua_chrome_claim },     { "release", lua_chrome_release },
            { "add", lua_chrome_add },         { "owner", lua_chrome_owner },
            { "claimed", lua_chrome_claimed }, { "part", lua_chrome_part },
            { "paint", lua_chrome_paint },     { "ops", lua_chrome_ops },
            { "state", lua_chrome_state },
        };
        static const struct
        {
            char const* name;
            lua_CFunction fn;
        } ENTITY[] = {
            { "part", lua_entity_part },
            { "look", lua_entity_look },
            { "ops", lua_entity_ops },
            { "claim", lua_chrome_claim },
            { "release", lua_chrome_release },
            { "owner", lua_chrome_owner },
            { "claimed", lua_chrome_claimed },
        };
        lua_createtable(L, 0, (int)(sizeof(CHROME) / sizeof(CHROME[0])));
        for( size_t v = 0; v < sizeof(CHROME) / sizeof(CHROME[0]); v++ )
        {
            lua_pushlightuserdata(L, script);
            lua_pushcclosure(L, CHROME[v].fn, 1);
            lua_setfield(L, -2, CHROME[v].name);
        }
        lua_setfield(L, -2, "chrome");
        lua_createtable(L, 0, (int)(sizeof(ENTITY) / sizeof(ENTITY[0])));
        for( size_t v = 0; v < sizeof(ENTITY) / sizeof(ENTITY[0]); v++ )
        {
            lua_pushlightuserdata(L, script);
            lua_pushcclosure(L, ENTITY[v].fn, 1);
            lua_setfield(L, -2, ENTITY[v].name);
        }
        lua_setfield(L, -2, "entity");
    }

    /*
     * api.layout: one sub-table per REGION, with the verbs on it.
     *
     * `api.layout.safe_gamechrome.reserve("right", 180)` rather than
     * `api.layout.reserve("safe_gamechrome", "right", 180)`, because a region is a thing
     * a script talks about repeatedly and a string argument repeated at every
     * call site is a typo waiting to be a silent no-op. The region is bound
     * once, into the closure, and a misspelling is then a nil index at the
     * point of the mistake.
     *
     * The verb set is the same everywhere and the host refuses what does not
     * apply: `reserve` on a placeable region, `replace` from a plugin that
     * does not own the frame. Uniform surface, one place that says no.
     */
    {
        static const struct
        {
            char const* name;
            int slot;
        } REGIONS[] = {
            { "viewport", TORIRS_PLUGIN_SLOT_VIEWPORT },
            { "minimap", TORIRS_PLUGIN_SLOT_MINIMAP },
            { "compass", TORIRS_PLUGIN_SLOT_COMPASS },
            { "chat", TORIRS_PLUGIN_SLOT_CHAT },
            { "sidebar", TORIRS_PLUGIN_SLOT_SIDEBAR },
            { "main_modal", TORIRS_PLUGIN_SLOT_MAIN_MODAL },
            /* The name the design note uses for the same region. Both, because
             * "where a modal opens" and "the modal viewport" are the same
             * question asked by people who learned it from different ends. */
            { "modal_viewport", TORIRS_PLUGIN_SLOT_MAIN_MODAL },
            { "chat_buttons", TORIRS_PLUGIN_SLOT_CHAT_BUTTONS },
            { "orbs", TORIRS_PLUGIN_SLOT_ORBS },
            { "canvas", TORIRS_PLUGIN_SLOT_CANVAS },
            { "safe_gamechrome", TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME },
            { "safe_lanechrome", TORIRS_PLUGIN_SLOT_SAFE_LANECHROME },
        };
        static const struct
        {
            char const* name;
            lua_CFunction fn;
        } VERBS[] = {
            { "rect", lua_layout_rect },
            { "reserve", lua_layout_reserve },
            { "release", lua_layout_release },
            { "replace", lua_layout_replace },
        };

        lua_createtable(L, 0, (int)(sizeof(REGIONS) / sizeof(REGIONS[0])) + 2);
        for( size_t r = 0; r < sizeof(REGIONS) / sizeof(REGIONS[0]); r++ )
        {
            lua_createtable(L, 0, (int)(sizeof(VERBS) / sizeof(VERBS[0])));
            for( size_t v = 0; v < sizeof(VERBS) / sizeof(VERBS[0]); v++ )
            {
                lua_pushlightuserdata(L, script);
                lua_pushinteger(L, REGIONS[r].slot);
                lua_pushcclosure(L, VERBS[v].fn, 2);
                lua_setfield(L, -2, VERBS[v].name);
            }
            lua_setfield(L, -2, REGIONS[r].name);
        }

        /* top_level is the frame itself, so its `replace` is the claim. Same
         * grammar, so a script that has learned the region tables has learned
         * this one too. */
        {
            static const struct
            {
                char const* name;
                lua_CFunction fn;
            } TOP[] = {
                { "rect", lua_layout_top_level_rect },
                { "replace", lua_layout_top_level_replace },
                { "release", lua_layout_top_level_release },
            };
            lua_createtable(L, 0, (int)(sizeof(TOP) / sizeof(TOP[0])));
            for( size_t v = 0; v < sizeof(TOP) / sizeof(TOP[0]); v++ )
            {
                lua_pushlightuserdata(L, script);
                lua_pushinteger(L, TORIRS_PLUGIN_SLOT_CANVAS);
                lua_pushcclosure(L, TOP[v].fn, 2);
                lua_setfield(L, -2, TOP[v].name);
            }
            lua_setfield(L, -2, "top_level");
        }

        lua_pushlightuserdata(L, script);
        lua_pushcclosure(L, lua_layout_revision, 1);
        lua_setfield(L, -2, "revision");
        lua_setfield(L, -2, "layout");
    }

    /* api.window: the plugin's tab in the shared window. A sub-table rather
     * than six more api.win_* names, because these only make sense together
     * and a script reading `api.window.widget(...)` is told as much. */
    {
        static const struct
        {
            char const* name;
            lua_CFunction fn;
        } WIN[] = {
            { "request", lua_window_request },
            { "widget", lua_window_widget },
            { "set_text", lua_window_set_text },
            { "set_checked", lua_window_set_checked },
            { "set_options", lua_window_set_options },
            { "clear", lua_window_clear },
        };
        lua_createtable(L, 0, (int)(sizeof(WIN) / sizeof(WIN[0])));
        for( size_t i = 0; i < sizeof(WIN) / sizeof(WIN[0]); i++ )
        {
            lua_pushlightuserdata(L, script);
            lua_pushcclosure(L, WIN[i].fn, 1);
            lua_setfield(L, -2, WIN[i].name);
        }
        lua_setfield(L, -2, "window");
    }

    /* api.panel: an opt-in rail page in the one shared application shell.
     * It deliberately uses the same kind names and result-state vocabulary as
     * api.window, so a script can migrate without learning a platform UI. */
    {
        static const struct
        {
            char const* name;
            lua_CFunction fn;
        } PANEL[] = {
            { "request", lua_panel_request },
            { "widget", lua_panel_widget },
            { "set_text", lua_panel_set_text },
            { "set_value", lua_panel_set_value },
            { "set_height", lua_panel_set_height },
            { "set_options", lua_panel_set_options },
            { "set_attention", lua_panel_set_attention },
            { "clear", lua_panel_clear },
            { "invalidate", lua_panel_invalidate },
        };
        lua_createtable(L, 0, (int)(sizeof(PANEL) / sizeof(PANEL[0])));
        for( size_t i = 0; i < sizeof(PANEL) / sizeof(PANEL[0]); i++ )
        {
            lua_pushlightuserdata(L, script);
            lua_pushcclosure(L, PANEL[i].fn, 1);
            lua_setfield(L, -2, PANEL[i].name);
        }
        lua_setfield(L, -2, "panel");
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
        { "text", lua_draw_text },   { "rect", lua_draw_rect },  { "image", lua_draw_image },
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
    [TORIRS_PLUGIN_EV_DRAW_CANVAS] = "on_draw_canvas",
    [TORIRS_PLUGIN_EV_CANVAS_CLICK] = "on_canvas_click",
    [TORIRS_PLUGIN_EV_LAYOUT_CHANGED] = "on_layout_changed",
    [TORIRS_PLUGIN_EV_CHROME] = "on_chrome",
    [TORIRS_PLUGIN_EV_CONFIG_CHANGED] = "on_config_changed",
    [TORIRS_PLUGIN_EV_OBJ_SPAWN] = "on_obj_spawn",
    [TORIRS_PLUGIN_EV_OBJ_COUNT] = "on_obj_count",
    [TORIRS_PLUGIN_EV_OBJ_DESPAWN] = "on_obj_despawn",
    [TORIRS_PLUGIN_EV_ASSET] = "on_asset",
    [TORIRS_PLUGIN_EV_CHAT_MESSAGE] = "on_chat_message",
    [TORIRS_PLUGIN_EV_GAME_EVENT] = "on_game_event",
    [TORIRS_PLUGIN_EV_UI] = "on_ui",
    [TORIRS_PLUGIN_EV_UI_BUILD] = "on_ui_build",
    [TORIRS_PLUGIN_EV_SETTING] = "on_setting",
    [TORIRS_PLUGIN_EV_SCREEN_CHANGE] = "on_screen_change",
    [TORIRS_PLUGIN_EV_PANEL_BUILD] = "on_panel_build",
    [TORIRS_PLUGIN_EV_PANEL_ACTION] = "on_panel_action",
    [TORIRS_PLUGIN_EV_PANEL_LAYOUT] = "on_panel_layout",
    [TORIRS_PLUGIN_EV_PANEL_DRAW] = "on_panel_draw",
};

/* The screen as a word, because the numeric TORIRS_PLUGIN_SCREEN_* values are
 * a C header a script never sees: `if ev.screen == "game"`, never a bare 30. */
static char const*
lua_screen_name(int screen)
{
    switch( screen )
    {
    case TORIRS_PLUGIN_SCREEN_BOOT:
        return "boot";
    case TORIRS_PLUGIN_SCREEN_TITLE:
        return "title";
    case TORIRS_PLUGIN_SCREEN_CONNECTING:
        return "connecting";
    case TORIRS_PLUGIN_SCREEN_GAME:
        return "game";
    default:
        return "";
    }
}

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
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, (lua_Integer)ev->now_ms);
        lua_setfield(L, -2, "now_ms");
        lua_pushinteger(L, (lua_Integer)ev->drawn_frames);
        lua_setfield(L, -2, "drawn_frames");
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
    case TORIRS_PLUGIN_EV_CHAT_MESSAGE:
    {
        struct ToriRS_PluginEvChat const* ev = payload;
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, ev->type);
        lua_setfield(L, -2, "type");
        lua_pushstring(L, ev->sender);
        lua_setfield(L, -2, "sender");
        lua_pushstring(L, ev->text);
        lua_setfield(L, -2, "text");
        return 1;
    }
    case TORIRS_PLUGIN_EV_GAME_EVENT:
    {
        struct ToriRS_PluginEvGameEvent const* ev = payload;
        lua_createtable(L, 0, 4);
        lua_pushstring(L, ev->kind ? ev->kind : "");
        lua_setfield(L, -2, "kind");
        lua_pushstring(L, ev->subject);
        lua_setfield(L, -2, "subject");
        lua_pushinteger(L, ev->value);
        lua_setfield(L, -2, "value");
        lua_pushstring(L, ev->text);
        lua_setfield(L, -2, "text");
        return 1;
    }
    case TORIRS_PLUGIN_EV_UI:
    {
        struct ToriRS_PluginEvUi const* ev = payload;
        static char const* const ACTION[] = { "activate", "toggle", "text", "pick" };
        lua_createtable(L, 0, 4);
        lua_pushstring(L, ev->widget_id ? ev->widget_id : "");
        lua_setfield(L, -2, "widget");
        /* The action as a word, matching the strings window.widget takes: a
         * script writes `if ev.action == "toggle"`, never a bare number. */
        lua_pushstring(
            L,
            ev->action >= 0 && ev->action < (int)(sizeof(ACTION) / sizeof(ACTION[0]))
                ? ACTION[ev->action]
                : "");
        lua_setfield(L, -2, "action");
        /* PICK indices are 0-based across the contract and 1-based in Lua. */
        lua_pushinteger(
            L, ev->action == TORIRS_PLUGIN_UI_PICK ? ev->value + 1 : ev->value);
        lua_setfield(L, -2, "value");
        lua_pushstring(L, ev->text ? ev->text : "");
        lua_setfield(L, -2, "text");
        /* A toggle's state reads better as a boolean than as 0/1, and a script
         * asking "is it on" should not have to know which it got. */
        lua_pushboolean(L, ev->value != 0);
        lua_setfield(L, -2, "on");
        return 1;
    }
    case TORIRS_PLUGIN_EV_UI_BUILD:
        /* No payload worth pushing: "build your tab" carries nothing but the
         * instruction, and the api the handler needs is its first argument. */
        return 0;
    case TORIRS_PLUGIN_EV_PANEL_BUILD:
    {
        struct ToriRS_PluginEvPanelBuild const* ev = payload;
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, (lua_Integer)ev->selection_generation);
        lua_setfield(L, -2, "generation");
        /* The FACE, as a word rather than a number: a script comparing
         * `ev.view == "settings"` cannot get it silently backwards the way a
         * script comparing against 0 or 1 can. @see enum
         * ToriRS_PluginPanelView. */
        lua_pushstring(
            L,
            ev->view == TORIRS_PLUGIN_PANEL_VIEW_SETTINGS ? "settings" : "page");
        lua_setfield(L, -2, "view");
        return 1;
    }
    case TORIRS_PLUGIN_EV_PANEL_ACTION:
    {
        struct ToriRS_PluginEvPanelAction const* ev = payload;
        static char const* const ACTION[] = {
            "activate", "toggle", "text", "pick", "drag", "scroll", "key"
        };
        lua_createtable(L, 0, 10);
        lua_pushstring(L, ev->id ? ev->id : "");
        lua_setfield(L, -2, "id");
        lua_pushstring(
            L,
            ev->action >= 0 && ev->action < (int)(sizeof(ACTION) / sizeof(ACTION[0]))
                ? ACTION[ev->action]
                : "");
        lua_setfield(L, -2, "action");
        lua_pushinteger(
            L, ev->action == TORIRS_PLUGIN_UI_PICK ? ev->value + 1 : ev->value);
        lua_setfield(L, -2, "value");
        lua_pushboolean(L, ev->value != 0);
        lua_setfield(L, -2, "on");
        lua_pushstring(L, ev->text ? ev->text : "");
        lua_setfield(L, -2, "text");
        lua_pushinteger(L, ev->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, ev->y);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, (lua_Integer)ev->selection_generation);
        lua_setfield(L, -2, "generation");
        lua_pushinteger(L, (lua_Integer)ev->widget_serial);
        lua_setfield(L, -2, "serial");
        lua_pushinteger(L, (lua_Integer)ev->intent_sequence);
        lua_setfield(L, -2, "sequence");
        return 1;
    }
    case TORIRS_PLUGIN_EV_PANEL_LAYOUT:
    {
        struct ToriRS_PluginEvPanelLayout const* ev = payload;
        static char const* const SIZE[] = { "compact", "medium", "expanded" };
        lua_createtable(L, 0, 7);
        lua_pushinteger(L, ev->width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, ev->height);
        lua_setfield(L, -2, "height");
        lua_pushinteger(L, ev->scale_milli);
        lua_setfield(L, -2, "scale_milli");
        lua_pushnumber(L, (lua_Number)ev->scale_milli / 1000.0);
        lua_setfield(L, -2, "scale");
        lua_pushstring(
            L,
            ev->size_class >= TORIRS_PLUGIN_PANEL_COMPACT &&
                    ev->size_class <= TORIRS_PLUGIN_PANEL_EXPANDED
                ? SIZE[ev->size_class]
                : "");
        lua_setfield(L, -2, "size_class");
        lua_pushboolean(L, ev->visible);
        lua_setfield(L, -2, "visible");
        lua_pushboolean(L, ev->game_visible);
        lua_setfield(L, -2, "game_visible");
        lua_pushinteger(L, (lua_Integer)ev->selection_generation);
        lua_setfield(L, -2, "generation");
        return 1;
    }
    case TORIRS_PLUGIN_EV_PANEL_DRAW:
    {
        struct ToriRS_PluginEvPanelDraw const* ev = payload;
        lua_rawgeti(L, LUA_REGISTRYINDEX, script->draw_ref);
        lua_pushstring(L, ev->id ? ev->id : "");
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, ev->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, ev->y);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, ev->width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, ev->height);
        lua_setfield(L, -2, "height");
        lua_pushinteger(L, ev->scale_milli);
        lua_setfield(L, -2, "scale_milli");
        lua_pushinteger(L, (lua_Integer)ev->selection_generation);
        lua_setfield(L, -2, "generation");
        lua_pushinteger(L, (lua_Integer)ev->widget_serial);
        lua_setfield(L, -2, "serial");
        return 1;
    }
    case TORIRS_PLUGIN_EV_SCREEN_CHANGE:
    {
        struct ToriRS_PluginEvScreen const* ev = payload;
        lua_createtable(L, 0, 2);
        lua_pushstring(L, lua_screen_name(ev->screen));
        lua_setfield(L, -2, "screen");
        lua_pushstring(L, lua_screen_name(ev->previous));
        lua_setfield(L, -2, "previous");
        return 1;
    }
    case TORIRS_PLUGIN_EV_SETTING:
    {
        struct ToriRS_PluginEvSetting const* ev = payload;
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, ev->setting_id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, ev->value);
        lua_setfield(L, -2, "value");
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

    /*
     * The same draw table, carrying the canvas it is open over.
     *
     * The size rides on the table rather than arriving as a third argument
     * because a canvas handler needs it on nearly every call -- anything
     * anchored to an edge is `draw.width - something` -- and a second
     * parameter that every handler has to accept in order to use is a
     * parameter every handler gets wrong once.
     */
    case TORIRS_PLUGIN_EV_DRAW_CANVAS:
    {
        struct ToriRS_PluginEvDrawCanvas const* ev = payload;
        lua_rawgeti(L, LUA_REGISTRYINDEX, script->draw_ref);
        lua_pushinteger(L, ev->width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, ev->height);
        lua_setfield(L, -2, "height");
        return 1;
    }

    case TORIRS_PLUGIN_EV_CHROME:
    {
        struct ToriRS_PluginEvLayout const* ev = payload;
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, ev->width);
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, ev->height);
        lua_setfield(L, -2, "height");
        return 1;
    }
    case TORIRS_PLUGIN_EV_CANVAS_CLICK:
    {
        struct ToriRS_PluginEvCanvasClick const* ev = payload;
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, (lua_Integer)ev->tag);
        lua_setfield(L, -2, "tag");
        lua_pushinteger(L, ev->op);
        lua_setfield(L, -2, "op");
        lua_pushinteger(L, ev->x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, ev->y);
        lua_setfield(L, -2, "y");
        return 1;
    }

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
    {
        script->cur_surface = ((struct ToriRS_PluginEvDraw*)event)->surface;
        script->cur_surface_kind = LUA_SURFACE_WORLD;
    }
    if( binding->event == TORIRS_PLUGIN_EV_DRAW_CANVAS )
    {
        script->cur_surface = ((struct ToriRS_PluginEvDrawCanvas*)event)->surface;
        script->cur_surface_kind = LUA_SURFACE_CANVAS;
    }
    if( binding->event == TORIRS_PLUGIN_EV_PANEL_DRAW )
    {
        script->cur_surface = ((struct ToriRS_PluginEvPanelDraw*)event)->surface;
        script->cur_surface_kind = LUA_SURFACE_PANEL;
    }
    if( binding->event == TORIRS_PLUGIN_EV_MENU_BUILD )
        script->cur_menu = (struct ToriRS_PluginEvMenuBuild*)event;
    script->cur_in_chrome = binding->event == TORIRS_PLUGIN_EV_CHROME;

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
        script->cur_surface_kind = LUA_SURFACE_NONE;
        script->cur_menu = NULL;
        script->cur_in_chrome = 0;
        lua_script_fault(script, msg);
        return TORIRS_PLUGIN_PASS;
    }

    verdict = lua_read_verdict(script, binding->event, event);
    lua_pop(L, 1);

    script->cur_surface = NULL;
    script->cur_surface_kind = LUA_SURFACE_NONE;
    script->cur_menu = NULL;
    script->cur_in_chrome = 0;
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
    /* The source deliberately OUTLIVES the state: a reload releases the VM and
     * then rebuilds from exactly these bytes. PluginLua_Shutdown frees it. */
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
    /* The same value a "string" holds, edited in a multiline box: a list a
     * plugin expects the user to actually maintain. @see
     * TORIRS_PLUGIN_CFG_TEXT. */
    else if( strcmp(type, "text") == 0 )
        item->type = TORIRS_PLUGIN_CFG_TEXT;
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

    /* CFG_TEXT's box height. 0 means the chrome's own default, which is what a
     * declaration with no opinion gets; the chrome clamps whatever arrives. */
    lua_getfield(L, idx, "rows");
    item->rows = (int)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);
    return true;
}

/*
 * Build a script's VM from its text: a fresh state, the chunk run, the table it
 * returned read for a name, a version, a config schema and handlers.
 *
 * Extracted from PluginLua_AddScript so reload can run exactly the same path.
 * Anything a reload did differently from a first load would be a difference
 * that only shows up after a reload, which is the worst place for one.
 *
 * `name` seeds the identity; a script that names itself overrides it. On
 * failure the script is left released and `false` is returned.
 */
static void lua_script_plugin_reload(struct ToriRS_PluginCtx* ctx);

static bool
lua_script_build(struct LuaScript* script, char const* name, char const* source, int source_len)
{
    lua_State* L;
    char chunk[TORIRS_PLUGIN_NAME_MAX + 2];

    assert(script);
    assert(name);
    assert(source);
    assert(source_len > 0);

    /* Every ref is dead with the old state; re-seed them so a failure halfway
     * through does not leave a stale index pointing into a closed registry. */
    script->table_ref = LUA_NOREF;
    script->api_ref = LUA_NOREF;
    script->draw_ref = LUA_NOREF;
    script->menu_ref = LUA_NOREF;
    for( int i = 0; i < TORIRS_PLUGIN_EV_COUNT; i++ )
        script->handler_ref[i] = LUA_NOREF;
    script->config_count = 0;
    script->mem_used = 0;
    snprintf(script->name, sizeof(script->name), "%s", name);
    script->title[0] = '\0';

    L = lua_newstate(lua_script_alloc, script, 0);
    if( !L )
    {
        TORIRS_LOG("plugin: lua state for '%s' would not start\n", name);
        return false;
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
        TORIRS_ERR("plugin: lua script '%s' failed to load: %s\n", name, err ? err : "?");
        lua_disarm_budget(script);
        lua_script_release(script);
        return false;
    }
    lua_disarm_budget(script);

    if( !lua_istable(L, -1) )
    {
        TORIRS_LOG("plugin: lua script '%s' must return its plugin table (got %s)\n",
            name,
            luaL_typename(L, -1));
        lua_script_release(script);
        return false;
    }

    /* A script may name itself; the file name is the fallback so a plugin's
     * identity -- and therefore its ini section -- never depends on where the
     * file happened to live. */
    lua_getfield(L, -1, "name");
    if( lua_type(L, -1) == LUA_TSTRING )
        snprintf(script->name, sizeof(script->name), "%s", lua_tostring(L, -1));
    lua_pop(L, 1);

    /* `title` is what the settings roster shows. A script that declares none
     * gets one derived from its name by the host, so the panel never prints a
     * bare id -- but the script is the only thing that knows whether "Ground
     * Items" or something else is the right words for it. */
    lua_getfield(L, -1, "title");
    if( lua_type(L, -1) == LUA_TSTRING )
        snprintf(script->title, sizeof(script->title), "%s", lua_tostring(L, -1));
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
        /* Refused rather than truncated, for the reason PLUGIN_LUA_MAX_CONFIG
         * gives: the seventeenth item used to vanish here and surface as a
         * "was never declared" error thrown out of a handler much later, with
         * nothing connecting the two. */
        if( n > PLUGIN_LUA_MAX_CONFIG )
        {
            TORIRS_ERR("plugin: lua script '%s' declares %d config items; the limit "
                "is %d. Refusing it rather than dropping the last %d in "
                "silence.\n",
                name,
                (int)n,
                PLUGIN_LUA_MAX_CONFIG,
                (int)n - PLUGIN_LUA_MAX_CONFIG);
            lua_script_release(script);
            return false;
        }
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

    /* Rewritten every build, not only the first: a reloaded script may have
     * gained or lost config keys, and the host rereads the def through the same
     * pointer after calling `reload`. */
    script->def.name = script->name;
    script->def.title = script->title[0] ? script->title : NULL;
    script->def.version = script->version;
    script->def.priority = 0;
    script->def.config = script->config_count > 0 ? script->config : NULL;
    script->def.init = lua_script_plugin_init;
    script->def.shutdown = NULL;
    script->def.reload = lua_script_plugin_reload;
    return true;
}

/*
 * The def's `reload` hook: throw the VM away and build a new one from the text
 * this script was loaded with.
 *
 * A failed rebuild leaves the plugin released and disabled rather than half
 * alive, and says so where the user is looking -- the window's roster shows a
 * plugin's last fault beside its switch. Reloading a file that no longer
 * compiles has to be visible: a plugin that silently stopped after a Save
 * would read as the Save having broken it.
 */
static void
lua_script_plugin_reload(struct ToriRS_PluginCtx* ctx)
{
    struct LuaScript* script = lua_script_for_ctx(ctx);

    assert(ctx);
    if( !script || !script->source )
        return;

    lua_script_release(script);
    if( !lua_script_build(script, script->name, script->source, script->source_len) )
    {
        PluginHost_SetError(script->host, script->plugin_index, "reload failed; see the log");
        PluginHost_SetEnabled(script->host, script->plugin_index, false);
    }
}

int
PluginLua_AddScript(
    struct ToriRS_PluginHost* host,
    char const* name,
    char const* source,
    int source_len)
{
    struct LuaScript* script;

    assert(host);
    assert(name);
    assert(source);
    assert(source_len > 0);

    if( g_script_count >= PLUGIN_LUA_MAX_SCRIPTS )
    {
        TORIRS_ERR("plugin: lua script table full, refusing '%s'\n", name);
        return -1;
    }

    script = &g_scripts[g_script_count];
    memset(script, 0, sizeof(*script));
    script->host = host;
    script->plugin_index = -1;

    /* The text is kept for the life of the script so a reload can re-run it.
     * On the C heap, deliberately: the per-script Lua arena is exactly what a
     * reload throws away. */
    script->source = malloc((size_t)source_len);
    assert(script->source);
    memcpy(script->source, source, (size_t)source_len);
    script->source_len = source_len;

    if( !lua_script_build(script, name, source, source_len) )
    {
        free(script->source);
        script->source = NULL;
        return -1;
    }

    script->plugin_index = PluginHost_Register(host, &script->def);
    if( script->plugin_index < 0 )
    {
        lua_script_release(script);
        free(script->source);
        script->source = NULL;
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
    {
        lua_script_release(&g_scripts[i]);
        /* Released separately from the state, because it outlives one: a
         * reload closes the VM and rebuilds from these bytes, so only the
         * final teardown may free them. */
        free(g_scripts[i].source);
        g_scripts[i].source = NULL;
        g_scripts[i].source_len = 0;
    }
    g_script_count = 0;
}

/* ---------------------------------------------------------- the adapter */

static void
lua_adapter_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    (void)ctx;
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
 * enable flag and one place to report a load failure. It subscribes to nothing
 * -- the scripts it hosts do that for themselves.
 *
 * `adapter` keeps it out of the settings roster while it is working. It used
 * to have a row there too, and that row was noise: it sat among the scripts it
 * runs, looking like a peer of them, with nothing a user does to it. The flag
 * is a PRESENTATION fact and nothing else -- to the host this is a plugin like
 * any other, which is the property the whole design rests on.
 */
struct ToriRS_PluginDef const TORIRS_PLUGIN_LUA = {
    .name = "lua",
    /* Only ever seen when this adapter is broken or switched off, which is
     * exactly when the reader needs to be told it is the scripting layer they
     * are looking at rather than a plugin called "lua". */
    .title = "Lua Scripting",
    .version = "1.0.0",
    .priority = 0,
    .config = NULL,
    .adapter = true,
    .init = lua_adapter_init,
    .shutdown = lua_adapter_shutdown,
};
