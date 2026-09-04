#include "plugin/torirs_plugin_lua.h"

#include "plugin/torirs_plugin_host.h"
#include "plugin/torirs_plugin_v2.h"
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
 * Native API-v2 Lua runtime host.
 *
 * A Lua file is registered as an ordinary V2 plugin, not as a subscriber
 * hidden behind a compatibility layer. Every callback resolves its VM from the stable
 * id supplied by api.core.plugin_id().  That matters during reload: host
 * state and configuration survive while the VM, functions, and static Lua
 * descriptors are rebuilt from the retained source bytes.
 *
 * The Lua surface mirrors the C hierarchy exactly.  There are no flat
 * api.log/api.role/api.window aliases.  Callback-scoped builders are separate
 * Lua objects, just as they are separate C objects, so retaining one cannot
 * retain a native pointer past the callback.
 */

#define PLUGIN_LUA_MAX_SCRIPTS 16
#define PLUGIN_LUA_MAX_CONFIG 32
#define PLUGIN_LUA_MAX_CONTRIBUTIONS 16
#define PLUGIN_LUA_MAX_FRAMES 32
#define PLUGIN_LUA_STR_MAX 96
#define PLUGIN_LUA_STEP_BUDGET 400000
#define PLUGIN_LUA_MEM_CAP_BYTES (16u * 1024u * 1024u)
#define PLUGIN_LUA_INTERNAL_RESERVE_BYTES (4u * 1024u * 1024u)
#define PLUGIN_LUA_HARD_MEM_CAP_BYTES \
    (PLUGIN_LUA_MEM_CAP_BYTES + PLUGIN_LUA_INTERNAL_RESERVE_BYTES)
#define PLUGIN_LUA_OPTIONS_MAX 128
#define PLUGIN_LUA_LOOKUP_CAPACITY 32
#define PLUGIN_LUA_CALLBACK_DEPTH_MAX 16
_Static_assert(
    PLUGIN_LUA_LOOKUP_CAPACITY >= 2 * PLUGIN_LUA_MAX_SCRIPTS &&
        (PLUGIN_LUA_LOOKUP_CAPACITY & (PLUGIN_LUA_LOOKUP_CAPACITY - 1)) == 0,
    "Lua id lookup must remain a half-full power-of-two table");

enum LuaHandler
{
    LUA_ON_START = 0,
    LUA_ON_STOP,
    LUA_ON_FRAME_START,
    LUA_ON_LOGIC_TICK,
    LUA_ON_SERVER_TICK,
    LUA_ON_WORLD_LOADED,
    LUA_ON_SCREEN_CHANGED,
    LUA_ON_NPC_SPAWN,
    LUA_ON_NPC_RETYPE,
    LUA_ON_NPC_DESPAWN,
    LUA_ON_ITEM_SPAWN,
    LUA_ON_ITEM_CHANGED,
    LUA_ON_ITEM_DESPAWN,
    LUA_ON_CONFIG_CHANGED,
    LUA_ON_ASSET,
    LUA_ON_CHAT_MESSAGE,
    LUA_ON_GAME_EVENT,
    LUA_ON_KEY,
    LUA_ON_MENU_BUILD,
    LUA_ON_MENU_SELECT,
    LUA_ON_DRAW_WORLD,
    LUA_ON_DRAW_CANVAS,
    LUA_ON_UI_BUILD,
    LUA_ON_UI_ACTION,
    LUA_ON_UI_DRAW,
    LUA_ON_PLACEMENT_CHANGED,
    LUA_ON_UI_NODE_DRAW,
    LUA_ON_UI_NODE_ACTION,
    LUA_ON_CANVAS_ACTION,
    LUA_ON_UI_LAYOUT,
    LUA_HANDLER_COUNT
};

static char const* const LUA_HANDLER_NAMES[LUA_HANDLER_COUNT] = {
    "on_start",
    "on_stop",
    "on_frame_start",
    "on_logic_tick",
    "on_server_tick",
    "on_world_loaded",
    "on_screen_changed",
    "on_npc_spawn",
    "on_npc_retype",
    "on_npc_despawn",
    "on_item_spawn",
    "on_item_changed",
    "on_item_despawn",
    "on_config_changed",
    "on_asset",
    "on_chat_message",
    "on_game_event",
    "on_key",
    "on_menu_build",
    "on_menu_select",
    "on_draw_world",
    "on_draw_canvas",
    "on_ui_build",
    "on_ui_action",
    "on_ui_draw",
    "on_placement_changed",
    "on_ui_node_draw",
    "on_ui_node_action",
    "on_canvas_action",
    "on_ui_layout",
};

struct LuaContributionStorage
{
    char node[TORIRS_UI_NAME_MAX];
    char parent[TORIRS_UI_NAME_MAX];
    char label[TORIRS_UI_LABEL_MAX];
    char action[TORIRS_UI_ACTION_MAX];
    char actions[TORIRS_UI_NAMED_ACTIONS_MAX][TORIRS_UI_ACTION_MAX];
};

struct LuaCallbackScope
{
    struct ToriRS_ApiV2* api;
    struct ToriRS_DrawBuilder* draw;
    struct ToriRS_PanelBuilder* panel;
    struct ToriRS_FrameBuilder* frame;
    struct ToriRS_MenuBuildEvent* menu;
    size_t memory_limit;
};

struct LuaScript
{
    lua_State* L;
    struct ToriRS_PluginHost* host;
    int plugin_index;
    bool alive;
    bool reload_failed;
    char pending_disable[160];

    int table_ref;
    int api_ref;
    int draw_ref;
    int panel_builder_ref;
    int frame_builder_ref;
    int handler_ref[LUA_HANDLER_COUNT];
    int frame_build_ref[PLUGIN_LUA_MAX_FRAMES];
    int frame_draw_ref[PLUGIN_LUA_MAX_FRAMES];

    char name[TORIRS_PLUGIN_NAME_MAX];
    char title[TORIRS_PLUGIN_TITLE_MAX];
    char version[24];
    struct ToriRS_PluginDefV2 def;
    struct ToriRS_ConfigSchema config_schema;
    struct ToriRS_ConfigItem config[PLUGIN_LUA_MAX_CONFIG + 1];
    char cfg_str[PLUGIN_LUA_MAX_CONFIG][4][PLUGIN_LUA_STR_MAX];
    int config_count;
    struct ToriRS_UiContribution contributions[PLUGIN_LUA_MAX_CONTRIBUTIONS + 1];
    struct LuaContributionStorage contribution_strings[PLUGIN_LUA_MAX_CONTRIBUTIONS];
    int contribution_count;
    struct ToriRS_FrameOffer frames[PLUGIN_LUA_MAX_FRAMES + 1];
    char frame_ids[PLUGIN_LUA_MAX_FRAMES][TORIRS_PLUGIN_FRAME_ID_MAX];
    char frame_titles[PLUGIN_LUA_MAX_FRAMES][TORIRS_PLUGIN_TITLE_MAX];
    int frame_count;

    /* Callback-scoped native values. Lua closures only read these while the
     * corresponding callback is armed. */
    struct ToriRS_ApiV2* cur_api;
    struct ToriRS_DrawBuilder* cur_draw;
    struct ToriRS_PanelBuilder* cur_panel;
    struct ToriRS_FrameBuilder* cur_frame;
    struct ToriRS_MenuBuildEvent* cur_menu;
    struct LuaCallbackScope callback_scopes[PLUGIN_LUA_CALLBACK_DEPTH_MAX];
    int callback_depth;

    size_t mem_used;
    size_t memory_limit;
    char* source;
    int source_len;
};

struct LuaFn
{
    char const* name;
    lua_CFunction fn;
};

static struct LuaScript g_scripts[PLUGIN_LUA_MAX_SCRIPTS];
static int g_script_count;
/* Open-addressed id -> script index. Zero is empty, stored values are index+1.
 * With at most 16 scripts in 32 slots, callback lookup stays O(1) without
 * retaining or trusting the opaque V2 callback state. */
static uint8_t g_script_lookup[PLUGIN_LUA_LOOKUP_CAPACITY];

/* --------------------------------------------------------------- plumbing */

static struct LuaScript*
lua_upvalue_script(lua_State* L)
{
    return (struct LuaScript*)lua_touserdata(L, lua_upvalueindex(1));
}

static struct ToriRS_ApiV2*
lua_current_api(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    if( !script || !script->cur_api )
        luaL_error(L, "plugin API used outside a callback");
    return script->cur_api;
}

static struct LuaScript*
lua_script_for_api(struct ToriRS_ApiV2* api)
{
    char const* id;
    uint32_t hash = 2166136261u;
    if( !api || !api->core.plugin_id )
        return NULL;
    id = api->core.plugin_id(api);
    if( !id )
        return NULL;
    for( char const* at = id; *at; at++ )
    {
        hash ^= (unsigned char)*at;
        hash *= 16777619u;
    }
    for( int probe = 0; probe < PLUGIN_LUA_LOOKUP_CAPACITY; probe++ )
    {
        int const slot = (int)((hash + (uint32_t)probe) &
                               (PLUGIN_LUA_LOOKUP_CAPACITY - 1u));
        int const encoded = g_script_lookup[slot];
        if( encoded == 0 ) return NULL;
        if( strcmp(g_scripts[encoded - 1].name, id) == 0 )
            return &g_scripts[encoded - 1];
    }
    return NULL;
}

static void
lua_script_lookup_insert(int index)
{
    char const* id = g_scripts[index].name;
    uint32_t hash = 2166136261u;
    assert(index >= 0 && index < PLUGIN_LUA_MAX_SCRIPTS);
    for( char const* at = id; *at; at++ )
    {
        hash ^= (unsigned char)*at;
        hash *= 16777619u;
    }
    for( int probe = 0; probe < PLUGIN_LUA_LOOKUP_CAPACITY; probe++ )
    {
        int const slot = (int)((hash + (uint32_t)probe) &
                               (PLUGIN_LUA_LOOKUP_CAPACITY - 1u));
        if( g_script_lookup[slot] == 0 )
        {
            g_script_lookup[slot] = (uint8_t)(index + 1);
            return;
        }
    }
    assert(!"Lua script lookup table unexpectedly full");
}

static void*
lua_script_alloc(void* userdata, void* pointer, size_t old_size, size_t new_size)
{
    struct LuaScript* script = userdata;
    size_t const was = pointer ? old_size : 0;

    if( new_size == 0 )
    {
        if( pointer )
        {
            assert(script->mem_used >= old_size);
            script->mem_used -= old_size;
        }
        free(pointer);
        return NULL;
    }
    if( script->mem_used - was + new_size > script->memory_limit )
        return NULL;
    pointer = realloc(pointer, new_size);
    if( pointer )
        script->mem_used = script->mem_used - was + new_size;
    return pointer;
}

static void
lua_step_hook(lua_State* L, lua_Debug* debug)
{
    (void)debug;
    luaL_error(L, "instruction budget exhausted (%d)", PLUGIN_LUA_STEP_BUDGET);
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

static void lua_script_defer_disable(struct LuaScript* script, char const* message);
static bool lua_script_flush_disable(struct LuaScript* script, struct ToriRS_ApiV2* api);

static bool
lua_callback_scope_push(struct LuaScript* script, struct ToriRS_ApiV2* api)
{
    struct LuaCallbackScope* saved;
    if( script->callback_depth == 0 && script->mem_used > PLUGIN_LUA_MEM_CAP_BYTES )
    {
        /* Event marshalling happens before Lua's protected handler call. User
         * allocations stop at 16 MiB; this full collection and 4 MiB reserve
         * guarantee bounded host-built event/context tables cannot hit the
         * script cap on that unprotected path. A script that retained earlier
         * event tables past the user cap is refused before another is built. */
        (void)lua_gc(script->L, LUA_GCCOLLECT);
        if( script->mem_used > PLUGIN_LUA_MEM_CAP_BYTES )
        {
            lua_script_defer_disable(script, "retained Lua values exhausted the callback reserve");
            (void)lua_script_flush_disable(script, api);
            return false;
        }
    }
    if( script->callback_depth >= PLUGIN_LUA_CALLBACK_DEPTH_MAX )
        return false;
    saved = &script->callback_scopes[script->callback_depth++];
    saved->api = script->cur_api;
    saved->draw = script->cur_draw;
    saved->panel = script->cur_panel;
    saved->frame = script->cur_frame;
    saved->menu = script->cur_menu;
    saved->memory_limit = script->memory_limit;
    script->memory_limit = PLUGIN_LUA_HARD_MEM_CAP_BYTES;
    script->cur_api = api;
    script->cur_draw = NULL;
    script->cur_panel = NULL;
    script->cur_frame = NULL;
    script->cur_menu = NULL;
    return true;
}

static void
lua_callback_scope_pop(struct LuaScript* script)
{
    struct LuaCallbackScope const* saved;
    assert(script->callback_depth > 0);
    saved = &script->callback_scopes[--script->callback_depth];
    script->cur_api = saved->api;
    script->cur_draw = saved->draw;
    script->cur_panel = saved->panel;
    script->cur_frame = saved->frame;
    script->cur_menu = saved->menu;
    script->memory_limit = saved->memory_limit;
}

static int
lua_callback_pcall(struct LuaScript* script, int arguments, int results)
{
    bool const outermost = script->callback_depth == 1;
    int status;
    assert(script->callback_depth > 0);
    /* One budget covers the complete nested dispatch. A synchronous config
     * notification may enter Lua again, but it cannot reset the counter or
     * disarm the outer handler when it returns. */
    if( outermost ) lua_arm_budget(script);
    script->memory_limit = PLUGIN_LUA_MEM_CAP_BYTES;
    status = lua_pcall(script->L, arguments, results, 0);
    if( outermost ) lua_disarm_budget(script);
    lua_callback_scope_pop(script);
    return status;
}

static void
lua_script_defer_disable(struct LuaScript* script, char const* message)
{
    assert(script);
    if( !script->pending_disable[0] )
        snprintf(script->pending_disable, sizeof(script->pending_disable), "%s",
            message ? message : "error");
}

static bool
lua_script_flush_disable(struct LuaScript* script, struct ToriRS_ApiV2* api)
{
    char message[sizeof(script->pending_disable)];
    if( script->callback_depth != 0 || !script->pending_disable[0] ) return false;
    snprintf(message, sizeof(message), "%s", script->pending_disable);
    script->pending_disable[0] = '\0';
    TORIRS_ERR("plugin: Lua script '%s' disabled: %s\n", script->name, message);
    if( api && api->client && api->client->disable_self )
        api->client->disable_self(api, message);
    else if( script->plugin_index >= 0 )
    {
        PluginHost_SetError(script->host, script->plugin_index, message);
        PluginHost_SetEnabled(script->host, script->plugin_index, false);
    }
    return true;
}

static void
lua_script_fault(
    struct LuaScript* script,
    struct ToriRS_ApiV2* api,
    char const* where,
    char const* error)
{
    char message[160];
    snprintf(message, sizeof(message), "%s: %s", where, error ? error : "error");
    lua_script_defer_disable(script, message);
    (void)lua_script_flush_disable(script, api);
}

static void
lua_register_functions(lua_State* L, struct LuaScript* script, struct LuaFn const* fns)
{
    lua_newtable(L);
    for( ; fns->name; fns++ )
    {
        lua_pushlightuserdata(L, script);
        lua_pushcclosure(L, fns->fn, 1);
        lua_setfield(L, -2, fns->name);
    }
}

static void
lua_raw_getfield(lua_State* L, int index, char const* key)
{
    index = lua_absindex(L, index);
    lua_pushstring(L, key);
    (void)lua_rawget(L, index);
}

static void
lua_push_rect(lua_State* L, struct ToriRS_Rect rect)
{
    lua_createtable(L, 0, 6);
#define RECT_FIELD(name, value)                                                          \
    do                                                                                   \
    {                                                                                    \
        lua_pushinteger(L, (value));                                                     \
        lua_setfield(L, -2, (name));                                                     \
    } while( 0 )
    RECT_FIELD("x", rect.x);
    RECT_FIELD("y", rect.y);
    RECT_FIELD("width", rect.width);
    RECT_FIELD("height", rect.height);
    RECT_FIELD("w", rect.width);
    RECT_FIELD("h", rect.height);
#undef RECT_FIELD
}

static int
lua_table_int(lua_State* L, int index, char const* key, int fallback)
{
    int out;
    index = lua_absindex(L, index);
    lua_raw_getfield(L, index, key);
    out = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : fallback;
    lua_pop(L, 1);
    return out;
}

static bool
lua_table_bool(lua_State* L, int index, char const* key, bool fallback)
{
    bool out;
    index = lua_absindex(L, index);
    lua_raw_getfield(L, index, key);
    out = lua_isboolean(L, -1) ? lua_toboolean(L, -1) != 0 : fallback;
    lua_pop(L, 1);
    return out;
}

static char const*
lua_table_string(lua_State* L, int index, char const* key)
{
    char const* out;
    index = lua_absindex(L, index);
    lua_raw_getfield(L, index, key);
    out = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
    lua_pop(L, 1);
    return out;
}

static bool
lua_string_fits(char const* text, size_t capacity)
{
    return !text || strlen(text) < capacity;
}

static bool
lua_config_key_valid(char const* key)
{
    if( !key || !key[0] ) return false;
    for( ; *key; key++ )
        if( (*key < 'a' || *key > 'z') && (*key < '0' || *key > '9') && *key != '_' )
            return false;
    return true;
}

static struct ToriRS_Rect
lua_check_rect(lua_State* L, int index)
{
    struct ToriRS_Rect out;
    luaL_checktype(L, index, LUA_TTABLE);
    out.x = lua_table_int(L, index, "x", 0);
    out.y = lua_table_int(L, index, "y", 0);
    out.width = lua_table_int(L, index, "width", lua_table_int(L, index, "w", 0));
    out.height = lua_table_int(L, index, "height", lua_table_int(L, index, "h", 0));
    return out;
}

static uint32_t
lua_color_arg(lua_State* L, int index)
{
    if( lua_isinteger(L, index) )
        return (uint32_t)lua_tointeger(L, index);
    char const* text = luaL_checkstring(L, index);
    unsigned value;
    if( text[0] == '#' && strlen(text) == 7 && sscanf(text + 1, "%x", &value) == 1 )
        return (uint32_t)value;
    return (uint32_t)luaL_error(L, "colour must be 0xRRGGBB or '#RRGGBB'");
}

static char const*
lua_result_name(enum ToriRS_Result result)
{
    static char const* const NAMES[] = {
        "ok", "not_found", "pending", "unsupported", "conflict", "budget", "invalid", "error"
    };
    return result >= TORIRS_RESULT_OK && result <= TORIRS_RESULT_ERROR ? NAMES[result] : "error";
}

static char const*
lua_asset_state_name(enum ToriRS_AssetState state)
{
    static char const* const NAMES[] = {
        "pending", "ready", "missing", "invalid", "budget", "error"
    };
    return state >= TORIRS_ASSET_PENDING && state <= TORIRS_ASSET_ERROR ? NAMES[state] : "error";
}

static void
lua_push_result(lua_State* L, enum ToriRS_Result result)
{
    lua_pushboolean(L, result == TORIRS_RESULT_OK);
    lua_pushstring(L, lua_result_name(result));
}

static int
lua_enum_integer(lua_State* L, int index, int minimum, int maximum, char const* what)
{
    int const value = (int)luaL_checkinteger(L, index);
    if( value < minimum || value > maximum )
        return luaL_error(L, "%s must be between %d and %d", what, minimum, maximum);
    return value;
}

static int
lua_area_from_arg(lua_State* L, int index)
{
    char const* name;
    if( lua_isinteger(L, index) )
        return lua_enum_integer(L, index, TORIRS_AREA_PLATFORM_SAFE, TORIRS_AREA_RAW_VIEWPORT,
            "placement area");
    name = luaL_checkstring(L, index);
    if( strcmp(name, "platform_safe") == 0 ) return TORIRS_AREA_PLATFORM_SAFE;
    if( strcmp(name, "frame_build") == 0 ) return TORIRS_AREA_FRAME_BUILD;
    if( strcmp(name, "overlay_safe") == 0 ) return TORIRS_AREA_OVERLAY_SAFE;
    if( strcmp(name, "raw_viewport") == 0 ) return TORIRS_AREA_RAW_VIEWPORT;
    return luaL_error(L, "unknown placement area '%s'", name);
}

static int
lua_anchor_from_arg(lua_State* L, int index)
{
    char const* name;
    static char const* const NAMES[] = {
        "top-left", "top", "top-right", "left", "center", "right",
        "bottom-left", "bottom", "bottom-right"
    };
    if( lua_isinteger(L, index) )
        return lua_enum_integer(L, index, TORIRS_ANCHOR_TOP_LEFT, TORIRS_ANCHOR_BOTTOM_RIGHT,
            "anchor");
    name = luaL_checkstring(L, index);
    for( int i = 0; i < 9; i++ )
        if( strcmp(name, NAMES[i]) == 0 ) return i;
    return luaL_error(L, "unknown anchor '%s'", name);
}

static int
lua_edge_from_arg(lua_State* L, int index)
{
    char const* name;
    if( lua_isinteger(L, index) )
        return lua_enum_integer(L, index, TORIRS_EDGE_TOP, TORIRS_EDGE_LEFT, "edge");
    name = luaL_checkstring(L, index);
    if( strcmp(name, "top") == 0 ) return TORIRS_EDGE_TOP;
    if( strcmp(name, "right") == 0 ) return TORIRS_EDGE_RIGHT;
    if( strcmp(name, "bottom") == 0 ) return TORIRS_EDGE_BOTTOM;
    if( strcmp(name, "left") == 0 ) return TORIRS_EDGE_LEFT;
    return luaL_error(L, "unknown edge '%s'", name);
}

static uint32_t
lua_facets_from_arg(lua_State* L, int index)
{
    uint32_t facets = 0;
    if( lua_isinteger(L, index) )
    {
        uint32_t const value = (uint32_t)lua_tointeger(L, index);
        if( value == 0 || (value & ~TORIRS_UI_FACET_ALL) != 0 )
            luaL_error(L, "UI facets must be a non-empty subset of 0x7");
        return value;
    }
    if( lua_type(L, index) == LUA_TSTRING )
    {
        char const* name = lua_tostring(L, index);
        if( strcmp(name, "bounds") == 0 ) return TORIRS_UI_FACET_BOUNDS;
        if( strcmp(name, "appearance") == 0 ) return TORIRS_UI_FACET_APPEARANCE;
        if( strcmp(name, "actions") == 0 ) return TORIRS_UI_FACET_ACTIONS;
        if( strcmp(name, "all") == 0 ) return TORIRS_UI_FACET_ALL;
        luaL_error(L, "unknown UI facet '%s'", name);
    }
    luaL_checktype(L, index, LUA_TTABLE);
    for( lua_Integer i = 1, n = (lua_Integer)lua_rawlen(L, index); i <= n; i++ )
    {
        lua_rawgeti(L, index, i);
        if( lua_type(L, -1) == LUA_TTABLE )
            return (uint32_t)luaL_error(L, "UI facets must be a flat array");
        facets |= lua_facets_from_arg(L, -1);
        lua_pop(L, 1);
    }
    if( facets == 0 )
        luaL_error(L, "UI facets must not be empty");
    return facets;
}

static struct ToriRS_ImageRef lua_image_arg(lua_State* L, int index)
{
    struct ToriRS_ImageRef ref = { (int)luaL_checkinteger(L, index) };
    return ref;
}
static struct ToriRS_ModelRef lua_model_arg(lua_State* L, int index)
{
    struct ToriRS_ModelRef ref = { (int)luaL_checkinteger(L, index) };
    return ref;
}
static struct ToriRS_MeshRef lua_mesh_arg(lua_State* L, int index)
{
    struct ToriRS_MeshRef ref = { (int)luaL_checkinteger(L, index) };
    return ref;
}
static struct ToriRS_SceneInstanceRef lua_instance_arg(lua_State* L, int index)
{
    struct ToriRS_SceneInstanceRef ref = { (int)luaL_checkinteger(L, index) };
    return ref;
}
static struct ToriRS_UiNodeRef lua_ui_ref_arg(lua_State* L, int index)
{
    struct ToriRS_UiNodeRef ref = { (uint32_t)luaL_checkinteger(L, index) };
    return ref;
}

static int lua_surface_from_arg(lua_State* L, int index);

/* ------------------------------------------------------ snapshot -> table */

static void
lua_push_player(lua_State* L, struct ToriRS_PlayerSnapshot const* player)
{
    lua_createtable(L, 0, 13);
#define FIELD(name, value)                                                               \
    do { lua_pushinteger(L, (lua_Integer)(value)); lua_setfield(L, -2, (name)); } while( 0 )
    FIELD("true_x", player->true_x); FIELD("true_z", player->true_z);
    FIELD("level", player->level); FIELD("fine_x", player->fine_x);
    FIELD("fine_z", player->fine_z); FIELD("dest_x", player->dest_x);
    FIELD("dest_z", player->dest_z); FIELD("flag_x", player->flag_x);
    FIELD("flag_z", player->flag_z); FIELD("server_pid", player->server_pid);
    FIELD("element_id", player->element_id); FIELD("combat_level", player->combat_level);
#undef FIELD
    lua_pushstring(L, player->name); lua_setfield(L, -2, "name");
}

static void
lua_push_npc(lua_State* L, struct ToriRS_NpcSnapshot const* npc)
{
    lua_createtable(L, 0, 15);
#define FIELD(name, value)                                                               \
    do { lua_pushinteger(L, (lua_Integer)(value)); lua_setfield(L, -2, (name)); } while( 0 )
    FIELD("server_slot", npc->server_slot); FIELD("npc_id", npc->npc_id);
    FIELD("base_npc_id", npc->base_npc_id); FIELD("combat_level", npc->combat_level);
    FIELD("size", npc->size); FIELD("true_x", npc->true_x); FIELD("true_z", npc->true_z);
    FIELD("level", npc->level); FIELD("fine_x", npc->fine_x); FIELD("fine_z", npc->fine_z);
    FIELD("element_id", npc->element_id); FIELD("visible_ops", npc->visible_ops);
    FIELD("health_ratio", npc->health_ratio); FIELD("health_scale", npc->health_scale);
#undef FIELD
    lua_pushstring(L, npc->name); lua_setfield(L, -2, "name");
}

static void
lua_push_obj(lua_State* L, struct ToriRS_GroundItemSnapshot const* item)
{
    lua_createtable(L, 0, 9);
#define FIELD(name, value)                                                               \
    do { lua_pushinteger(L, (lua_Integer)(value)); lua_setfield(L, -2, (name)); } while( 0 )
    FIELD("obj_id", item->obj_id); FIELD("count", item->count); FIELD("cost", item->cost);
    FIELD("tile_x", item->tile_x); FIELD("tile_z", item->tile_z); FIELD("level", item->level);
    FIELD("element_id", item->element_id);
    FIELD("value", (int64_t)item->cost * (int64_t)item->count);
#undef FIELD
    lua_pushstring(L, item->name); lua_setfield(L, -2, "name");
}

static void
lua_push_loc(lua_State* L, struct ToriRS_ScenerySnapshot const* loc)
{
    lua_createtable(L, 0, 13);
#define FIELD(name, value)                                                               \
    do { lua_pushinteger(L, (lua_Integer)(value)); lua_setfield(L, -2, (name)); } while( 0 )
    FIELD("loc_id", loc->loc_id); FIELD("tile_x", loc->tile_x); FIELD("tile_z", loc->tile_z);
    FIELD("level", loc->level); FIELD("size_x", loc->size_x); FIELD("size_z", loc->size_z);
    FIELD("shape", loc->shape); FIELD("angle", loc->angle); FIELD("element_id", loc->element_id);
    FIELD("visible_ops", loc->visible_ops);
#undef FIELD
    lua_pushboolean(L, loc->interactive != 0); lua_setfield(L, -2, "interactive");
    lua_pushstring(L, loc->name); lua_setfield(L, -2, "name");
}

/* --------------------------------------------------------------- api.core */

static int lua_core_log(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    struct ToriRS_ApiV2* api = lua_current_api(L);
    char message[512];
    size_t used = 0;
    message[0] = '\0';
    for( int i = 1; i <= lua_gettop(L) && used + 2 < sizeof(message); i++ )
    {
        size_t length;
        char const* text = luaL_tolstring(L, i, &length);
        int const wrote = snprintf(message + used, sizeof(message) - used, "%s%.*s",
            i == 1 ? "" : " ", (int)length, text);
        lua_pop(L, 1);
        if( wrote < 0 ) break;
        used += (size_t)wrote < sizeof(message) - used ? (size_t)wrote : sizeof(message) - used - 1;
    }
    (void)script;
    api->core.log(api, "%s", message);
    return 0;
}
static int lua_core_notify(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); a->core.notify(a,luaL_checkstring(L,1)); return 0; }
static int lua_core_screen(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); lua_pushinteger(L,a->core.screen(a)); return 1; }
static int lua_core_frame_ms(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); lua_pushinteger(L,(lua_Integer)a->core.frame_ms(a)); return 1; }
static int lua_core_frame_work_us(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); lua_pushinteger(L,(lua_Integer)a->core.frame_work_us(a)); return 1; }
static int lua_core_lane(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L); struct ToriRS_LaneInfo lane;
    if( !a->core.lane(a,&lane) ) { lua_pushnil(L); return 1; }
    lua_createtable(L,0,3);
    lua_pushinteger(L,lane.game); lua_setfield(L,-2,"game");
    lua_pushinteger(L,lane.epoch); lua_setfield(L,-2,"epoch");
    lua_pushinteger(L,lane.revision); lua_setfield(L,-2,"revision");
    return 1;
}
static int lua_core_capability(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); lua_pushboolean(L,a->core.capability(a,luaL_checkstring(L,1))); return 1; }
static int lua_core_plugin_id(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); lua_pushstring(L,a->core.plugin_id(a)); return 1; }

/* ------------------------------------------------------------- api.config */

static struct ToriRS_ConfigItem const*
lua_config_item(struct LuaScript const* script, char const* key)
{
    for( int i = 0; i < script->config_count; i++ )
        if( strcmp(script->config[i].key, key) == 0 ) return &script->config[i];
    return NULL;
}

static int lua_config_has(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); lua_pushboolean(L,a->config.has(a,luaL_checkstring(L,1))); return 1; }
static int lua_config_get_bool(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); bool v; if(!a->config.get_bool(a,luaL_checkstring(L,1),&v)){lua_pushnil(L);return 1;} lua_pushboolean(L,v);return 1; }
static int lua_config_get_int(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); int v; if(!a->config.get_int(a,luaL_checkstring(L,1),&v)){lua_pushnil(L);return 1;} lua_pushinteger(L,v);return 1; }
static int lua_config_get_color(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); uint32_t v; if(!a->config.get_color(a,luaL_checkstring(L,1),&v)){lua_pushnil(L);return 1;} lua_pushinteger(L,v);return 1; }
static int lua_config_get_string(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); char const* v; if(!a->config.get_string(a,luaL_checkstring(L,1),&v)){lua_pushnil(L);return 1;} lua_pushstring(L,v);return 1; }
static int lua_config_set(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L); char value[TORIRS_PLUGIN_CONFIG_VALUE_MAX];
    if( lua_isboolean(L,2) ) snprintf(value,sizeof(value),"%d",lua_toboolean(L,2)?1:0);
    else snprintf(value,sizeof(value),"%s",luaL_tolstring(L,2,NULL));
    lua_push_result(L,a->config.set(a,luaL_checkstring(L,1),value)); return 2;
}

static int
lua_config_index(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    struct ToriRS_ApiV2* api = script->cur_api;
    char const* key = luaL_checkstring(L, 2);
    struct ToriRS_ConfigItem const* item = lua_config_item(script, key);
    if( !api ) return luaL_error(L, "plugin config used outside a callback");
    if( !item ) { lua_pushnil(L); return 1; }
    switch( item->type )
    {
    case TORIRS_CONFIG_BOOL: { bool v; if(api->config.get_bool(api,key,&v)){lua_pushboolean(L,v);return 1;} break; }
    case TORIRS_CONFIG_INT: { int v; if(api->config.get_int(api,key,&v)){lua_pushinteger(L,v);return 1;} break; }
    case TORIRS_CONFIG_COLOR: { uint32_t v; if(api->config.get_color(api,key,&v)){lua_pushinteger(L,v);return 1;} break; }
    default: { char const* v; if(api->config.get_string(api,key,&v)){lua_pushstring(L,v);return 1;} break; }
    }
    lua_pushnil(L); return 1;
}

static int lua_config_newindex(lua_State* L)
{
    (void)lua_upvalue_script(L);
    return luaL_error(L,"configuration is read-only; call api.config.set(key, value)");
}

/* -------------------------------------------------------------- api.world */

static int lua_world_local_player(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); struct ToriRS_PlayerSnapshot v; if(!a->world.local_player(a,&v)){lua_pushnil(L);return 1;} lua_push_player(L,&v);return 1; }
static int lua_world_npc_next(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); struct ToriRS_NpcSnapshot v; int n=a->world.npc_next(a,(int)luaL_optinteger(L,1,-1),&v); if(n<0){lua_pushnil(L);return 1;} lua_pushinteger(L,n);lua_push_npc(L,&v);return 2; }
static int lua_world_npc_by_slot(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); struct ToriRS_NpcSnapshot v; if(!a->world.npc_by_slot(a,(int)luaL_checkinteger(L,1),&v)){lua_pushnil(L);return 1;}lua_push_npc(L,&v);return 1; }
static int lua_world_player_next(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); struct ToriRS_PlayerSnapshot v; int n=a->world.player_next(a,(int)luaL_optinteger(L,1,-1),&v);if(n<0){lua_pushnil(L);return 1;}lua_pushinteger(L,n);lua_push_player(L,&v);return 2; }
static int lua_world_item_next(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); struct ToriRS_GroundItemSnapshot v; int n=a->world.item_next(a,(int)luaL_optinteger(L,1,-1),&v);if(n<0){lua_pushnil(L);return 1;}lua_pushinteger(L,n);lua_push_obj(L,&v);return 2; }
static int lua_world_scenery_next(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); struct ToriRS_ScenerySnapshot v; int n=a->world.scenery_next(a,(int)luaL_optinteger(L,1,-1),&v);if(n<0){lua_pushnil(L);return 1;}lua_pushinteger(L,n);lua_push_loc(L,&v);return 2; }

/* -------------------------------------------------------------- api.input */

static int lua_input_key_held(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L); int key=lua_type(L,1)==LUA_TSTRING?PluginLua_KeyCodeFromName(lua_tostring(L,1)):(int)luaL_checkinteger(L,1);lua_pushboolean(L,key>=0&&a->input.key_held(a,key));return 1; }
static int lua_input_pointer(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);int x,y;if(!a->input.pointer(a,&x,&y)){lua_pushnil(L);return 1;}lua_pushinteger(L,x);lua_pushinteger(L,y);return 2; }
static int lua_input_hover_tile(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);int x,z,l;if(!a->input.hover_tile(a,&x,&z,&l)){lua_pushnil(L);return 1;}lua_pushinteger(L,x);lua_pushinteger(L,z);lua_pushinteger(L,l);return 3; }
static int lua_input_hover_entity(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_HoverTarget v;
    if(!a->input.hover_entity(a,&v)){lua_pushnil(L);return 1;}lua_createtable(L,0,5);
#define F(k,x) lua_pushinteger(L,(x));lua_setfield(L,-2,(k))
    F("kind",v.kind);F("element_id",v.element_id);F("tile_x",v.tile_x);F("tile_z",v.tile_z);F("level",v.level);
#undef F
    return 1;
}
static int lua_input_text_input(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->input.text_input(a,lua_toboolean(L,1)!=0);return 0; }
static int lua_input_chat_focus(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->input.chat_focus(a,lua_toboolean(L,1)!=0);return 0; }

/* ---------------------------------------------------------------- api.ui */

static int lua_ui_ref(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_UiNodeRef r=a->ui.ref(a,luaL_checkstring(L,1));if(!r.value)lua_pushnil(L);else lua_pushinteger(L,r.value);return 1; }
static int lua_ui_info(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_UiNodeInfo v;
    memset(&v,0,sizeof(v));v.struct_size=sizeof(v);
    if(!a->ui.info(a,lua_ui_ref_arg(L,1),&v)){lua_pushnil(L);return 1;}
    lua_createtable(L,0,14);lua_push_rect(L,v.bounds);lua_setfield(L,-2,"bounds");
    lua_pushinteger(L,v.available_facets);lua_setfield(L,-2,"available_facets");
    lua_pushboolean(L,v.visible);lua_setfield(L,-2,"visible");lua_pushboolean(L,v.enabled);lua_setfield(L,-2,"enabled");lua_pushboolean(L,v.active);lua_setfield(L,-2,"active");
    if(v.parent.value)lua_pushinteger(L,v.parent.value);else lua_pushnil(L);lua_setfield(L,-2,"parent");
    lua_pushinteger(L,v.anchor);lua_setfield(L,-2,"anchor");lua_pushinteger(L,v.paint_order);lua_setfield(L,-2,"paint_order");lua_pushinteger(L,v.clip);lua_setfield(L,-2,"clip");
    lua_pushstring(L,v.label);lua_setfield(L,-2,"label");lua_pushinteger(L,v.label_x);lua_setfield(L,-2,"label_x");lua_pushinteger(L,v.label_y);lua_setfield(L,-2,"label_y");
    lua_push_rect(L,v.hit_rect);lua_setfield(L,-2,"hit_rect");
    lua_createtable(L,(int)v.action_count,0);for(uint32_t i=0;i<v.action_count;i++){lua_pushstring(L,v.actions[i]);lua_rawseti(L,-2,(lua_Integer)i+1);}lua_setfield(L,-2,"actions");
    lua_createtable(L,TORIRS_UI_VISUAL_STATE_COUNT,0);for(int i=0;i<TORIRS_UI_VISUAL_STATE_COUNT;i++){if(v.state_images[i].value)lua_pushinteger(L,v.state_images[i].value);else lua_pushnil(L);lua_rawseti(L,-2,i+1);}lua_setfield(L,-2,"state_images");
    return 1;
}
static int lua_ui_invoke(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushboolean(L,a->ui.invoke(a,lua_ui_ref_arg(L,1),luaL_checkstring(L,2)));return 1; }
static int lua_ui_contribution_info(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_UiContributionInfo v;memset(&v,0,sizeof(v));v.struct_size=sizeof(v);
    if(!a->ui.contribution_info(a,luaL_checkstring(L,1),lua_facets_from_arg(L,2),&v)){lua_pushnil(L);return 1;}
    lua_createtable(L,0,3);lua_pushinteger(L,v.state);lua_setfield(L,-2,"state");lua_pushinteger(L,v.active_facets);lua_setfield(L,-2,"active_facets");lua_pushstring(L,v.conflict_plugin);lua_setfield(L,-2,"conflict_plugin");return 1;
}

static void
lua_ui_node_arg(lua_State* L, int index, struct ToriRS_UiNode* node)
{
    static char const* const IMAGE_KEYS[TORIRS_UI_VISUAL_STATE_COUNT]={"idle","hover","active","active_hover","disabled"};
    memset(node,0,sizeof(*node));node->struct_size=sizeof(*node);index=lua_absindex(L,index);luaL_checktype(L,index,LUA_TTABLE);
    lua_raw_getfield(L,index,"bounds");if(lua_istable(L,-1))node->bounds=lua_check_rect(L,-1);lua_pop(L,1);
    node->parent=lua_table_string(L,index,"parent");
    lua_raw_getfield(L,index,"anchor");if(!lua_isnil(L,-1))node->anchor=lua_anchor_from_arg(L,-1);lua_pop(L,1);
    node->paint_order=lua_table_int(L,index,"paint_order",TORIRS_UI_PAINT_AFTER_PARENT);
    node->flags=(uint32_t)lua_table_int(L,index,"flags",TORIRS_UI_NODE_VISIBLE|TORIRS_UI_NODE_ENABLED);
    lua_raw_getfield(L,index,"image");if(lua_isinteger(L,-1))node->image.value=(int)lua_tointeger(L,-1);lua_pop(L,1);
    node->label=lua_table_string(L,index,"label");node->action=lua_table_string(L,index,"action");
    node->clip=lua_table_int(L,index,"clip",TORIRS_UI_CLIP_NONE);node->label_x=lua_table_int(L,index,"label_x",0);node->label_y=lua_table_int(L,index,"label_y",0);
    lua_raw_getfield(L,index,"hit_rect");if(lua_istable(L,-1)){node->hit_rect=lua_check_rect(L,-1);node->hit_rect_mode=TORIRS_UI_HIT_RECT_CUSTOM;}lua_pop(L,1);
    lua_raw_getfield(L,index,"state_images");if(lua_istable(L,-1))for(int i=0;i<TORIRS_UI_VISUAL_STATE_COUNT;i++){lua_raw_getfield(L,-1,IMAGE_KEYS[i]);if(lua_isinteger(L,-1)){node->state_images[i].value=(int)lua_tointeger(L,-1);node->state_image_mask|=1u<<i;}lua_pop(L,1);}lua_pop(L,1);
    lua_raw_getfield(L,index,"actions");if(lua_istable(L,-1)){uint32_t n=(uint32_t)lua_rawlen(L,-1);if(n>TORIRS_UI_NAMED_ACTIONS_MAX)luaL_error(L,"UI node has too many actions");node->action_count=n;for(uint32_t i=0;i<n;i++){lua_rawgeti(L,-1,(lua_Integer)i+1);if(lua_type(L,-1)!=LUA_TSTRING)luaL_error(L,"UI node action %d must be a string",(int)i+1);node->actions[i]=lua_tostring(L,-1);lua_pop(L,1);}}lua_pop(L,1);
}

static int lua_ui_update(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_UiNode v;lua_ui_node_arg(L,3,&v);lua_push_result(L,a->ui.update(a,lua_ui_ref_arg(L,1),lua_facets_from_arg(L,2),&v));return 2; }
static int lua_ui_menu_add(lua_State* L) { struct LuaScript* s=lua_upvalue_script(L);struct ToriRS_ApiV2* a=lua_current_api(L);if(!s->cur_menu)return luaL_error(L,"ui.menu_add is only valid in on_menu_build");lua_pushboolean(L,a->ui.menu_add(a,s->cur_menu,luaL_checkstring(L,1),(uint32_t)luaL_checkinteger(L,2)));return 1; }
static int lua_ui_set_enabled(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->ui.set_enabled(a,lua_ui_ref_arg(L,1),lua_toboolean(L,2)!=0));return 2; }

/* ---------------------------------------------------------- api.placement */

static int lua_placement_revision(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,a->placement.revision(a));return 1; }
static int lua_placement_area(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_PlacementAreaRef r=a->placement.area(a,lua_area_from_arg(L,1));if(!r.value)lua_pushnil(L);else lua_pushinteger(L,r.value);return 1; }
static int lua_placement_primary(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_PlacementAreaRef r={(uint32_t)luaL_checkinteger(L,1)};struct ToriRS_Rect v;if(!a->placement.primary(a,r,&v)){lua_pushnil(L);return 1;}lua_push_rect(L,v);return 1; }
static int lua_placement_place(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_Rect v;if(!a->placement.place(a,lua_area_from_arg(L,1),lua_anchor_from_arg(L,2),(int)luaL_checkinteger(L,3),(int)luaL_checkinteger(L,4),(int)luaL_optinteger(L,5,0),&v)){lua_pushnil(L);return 1;}lua_push_rect(L,v);return 1; }
static int lua_placement_rect_next(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_PlacementAreaRef r={(uint32_t)luaL_checkinteger(L,1)};struct ToriRS_Rect v;int n=a->placement.rect_next(a,r,(int)luaL_optinteger(L,2,-1),&v);if(n<0){lua_pushnil(L);return 1;}lua_pushinteger(L,n);lua_push_rect(L,v);return 2; }
static int lua_placement_contains(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_PlacementAreaRef r={(uint32_t)luaL_checkinteger(L,1)};lua_pushboolean(L,a->placement.contains(a,r,lua_check_rect(L,2)));return 1; }
static int lua_placement_reserve(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L);enum ToriRS_PlacementReserveResult result=a->placement.reserve(a,luaL_checkstring(L,1),lua_area_from_arg(L,2),lua_edge_from_arg(L,3),(int)luaL_checkinteger(L,4));
    static char const* const names[]={"ok","no_space","budget","invalid"};lua_pushboolean(L,result==TORIRS_RESERVE_OK);lua_pushstring(L,result>=TORIRS_RESERVE_OK&&result<=TORIRS_RESERVE_INVALID?names[result]:"invalid");return 2;
}
static int lua_placement_reservation_rect(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_Rect v;if(!a->placement.reservation_rect(a,luaL_checkstring(L,1),&v)){lua_pushnil(L);return 1;}lua_push_rect(L,v);return 1; }

/* -------------------------------------------------------------- api.frame */

static int lua_frame_offer_next(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_FrameOfferInfo v;memset(&v,0,sizeof(v));v.struct_size=sizeof(v);int n=a->frame.offer_next(a,(int)luaL_optinteger(L,1,-1),&v);if(n<0){lua_pushnil(L);return 1;}lua_pushinteger(L,n);lua_createtable(L,0,11);
#define FI(k,x) lua_pushinteger(L,(x));lua_setfield(L,-2,(k))
    lua_pushstring(L,v.id);lua_setfield(L,-2,"id");lua_pushstring(L,v.title);lua_setfield(L,-2,"title");lua_pushstring(L,v.provider);lua_setfield(L,-2,"provider");FI("canvas",v.canvas);FI("width",v.width);FI("height",v.height);FI("min_width",v.min_width);FI("min_height",v.min_height);lua_pushboolean(L,v.available);lua_setfield(L,-2,"available");lua_pushstring(L,v.detail);lua_setfield(L,-2,"detail");
#undef FI
    return 2;
}
static int lua_frame_selection(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_FrameSelection v;memset(&v,0,sizeof(v));v.struct_size=sizeof(v);a->frame.selection(a,&v);lua_createtable(L,0,5);lua_pushstring(L,v.requested_id);lua_setfield(L,-2,"requested_id");lua_pushstring(L,v.active_id);lua_setfield(L,-2,"active_id");lua_pushinteger(L,v.status);lua_setfield(L,-2,"status");lua_pushstring(L,v.reason);lua_setfield(L,-2,"reason");lua_pushinteger(L,v.revision);lua_setfield(L,-2,"revision");return 1;
}
static int lua_frame_select(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->frame.select(a,luaL_checkstring(L,1)));return 2; }
static int lua_frame_invalidate(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->frame.invalidate(a);return 0; }
static int lua_frame_surface_native_size(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);int w,h;if(!a->frame.surface_native_size(a,lua_surface_from_arg(L,1),&w,&h)){lua_pushnil(L);return 1;}lua_pushinteger(L,w);lua_pushinteger(L,h);return 2; }

/* --------------------------------------------------------------- api.draw */

static int lua_draw_project(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);int x,y;if(!a->draw.project(a,(int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2),(int)luaL_optinteger(L,3,0),&x,&y)){lua_pushnil(L);return 1;}lua_pushinteger(L,x);lua_pushinteger(L,y);return 2; }
static int lua_draw_element_height(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,a->draw.element_height(a,(int)luaL_checkinteger(L,1)));return 1; }
static int lua_draw_hsl_from_rgb(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,a->draw.hsl_from_rgb(a,lua_color_arg(L,1)));return 1; }
static int lua_draw_hsl_to_rgb(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,a->draw.hsl_to_rgb(a,(int)luaL_checkinteger(L,1)));return 1; }

/* ------------------------------------------------------------- api.assets */

static int lua_assets_request(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushstring(L,lua_asset_state_name(a->assets.request(a,luaL_checkstring(L,1))));return 1; }
static int lua_assets_bytes(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);void const* data;size_t size;if(!a->assets.bytes(a,luaL_checkstring(L,1),&data,&size)){lua_pushnil(L);return 1;}lua_pushlstring(L,data,size);return 1; }
static int lua_assets_save(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);size_t n;char const* data=luaL_checklstring(L,2,&n);lua_push_result(L,a->assets.save(a,luaL_checkstring(L,1),data,n));return 2; }
static int lua_assets_release(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->assets.release(a,luaL_checkstring(L,1));return 0; }
static int lua_assets_image(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_ImageRef r={0};enum ToriRS_AssetState s=a->assets.image(a,luaL_checkstring(L,1),&r);if(r.value)lua_pushinteger(L,r.value);else lua_pushnil(L);lua_pushstring(L,lua_asset_state_name(s));return 2; }
static int lua_assets_image_size(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);int w,h;if(!a->assets.image_size(a,lua_image_arg(L,1),&w,&h)){lua_pushnil(L);return 1;}lua_pushinteger(L,w);lua_pushinteger(L,h);return 2; }
static int lua_assets_image_release(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->assets.image_release(a,lua_image_arg(L,1));return 0; }
static int lua_assets_model(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_ModelRef r={0};enum ToriRS_AssetState s=a->assets.model(a,luaL_checkstring(L,1),&r);if(r.value)lua_pushinteger(L,r.value);else lua_pushnil(L);lua_pushstring(L,lua_asset_state_name(s));return 2; }
static int lua_assets_model_release(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->assets.model_release(a,lua_model_arg(L,1));return 0; }
static int lua_assets_screenshot(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);char path[512];enum ToriRS_Result r=a->assets.screenshot(a,luaL_optstring(L,1,""),luaL_checkstring(L,2),path,sizeof(path));lua_pushboolean(L,r==TORIRS_RESULT_OK);if(r==TORIRS_RESULT_OK)lua_pushstring(L,path);else lua_pushstring(L,lua_result_name(r));return 2; }
static int lua_assets_image_pixels(lua_State* L)
{
    struct ToriRS_ApiV2* api = lua_current_api(L);
    struct ToriRS_ImageRef image = lua_image_arg(L, 1);
    int width;
    int height;
    size_t count;
    size_t got = 0;
    uint32_t* pixels;
    if( !api->assets.image_size(api, image, &width, &height) || width <= 0 || height <= 0 ||
        (size_t)width * (size_t)height > 1024u * 1024u )
    {
        lua_pushnil(L);
        return 1;
    }
    count = (size_t)width * (size_t)height;
    /* Lua owns the temporary, so an allocation error while constructing the
     * result table cannot leak untracked host memory past the longjmp. */
    pixels = lua_newuserdatauv(L, count * sizeof(*pixels), 0);
    if( !api->assets.image_pixels(api, image, pixels, count, &got) )
    {
        lua_pop(L, 1);
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, (int)got, 0);
    for( size_t i = 0; i < got; i++ )
    {
        lua_pushinteger(L, pixels[i]);
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    lua_remove(L, -2);
    return 1;
}
static int lua_assets_image_compose(lua_State* L)
{
    struct ToriRS_ApiV2* api = lua_current_api(L);
    char const* name = luaL_checkstring(L, 1);
    int const width = (int)luaL_checkinteger(L, 2);
    int const height = (int)luaL_checkinteger(L, 3);
    size_t count;
    uint32_t* pixels;
    struct ToriRS_ImageRef ref = { 0 };
    enum ToriRS_AssetState state;
    luaL_checktype(L, 4, LUA_TTABLE);
    if( width <= 0 || height <= 0 ||
        (size_t)width * (size_t)height > 1024u * 1024u )
        return luaL_error(L, "image_compose dimensions must contain 1..1048576 pixels");
    count = (size_t)width * (size_t)height;
    if( lua_rawlen(L, 4) != count )
        return luaL_error(L, "image_compose needs exactly %d pixels", (int)count);
    if( !lua_checkstack(L, 1) )
        return luaL_error(L, "image_compose cannot grow the Lua stack");
    /* Validate before allocating. A script may catch an argument error by
     * calling this C closure indirectly, and that must not leak a host block. */
    for( size_t i = 0; i < count; i++ )
    {
        lua_rawgeti(L, 4, (lua_Integer)i + 1);
        if( !lua_isinteger(L, -1) )
        {
            lua_pop(L, 1);
            return luaL_error(L, "image_compose pixel %d is not an integer", (int)i + 1);
        }
        lua_pop(L, 1);
    }
    pixels = lua_newuserdatauv(L, count * sizeof(*pixels), 0);
    for( size_t i = 0; i < count; i++ )
    {
        lua_rawgeti(L, 4, (lua_Integer)i + 1);
        pixels[i] = (uint32_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    state = api->assets.image_compose(api, name, width, height, pixels, &ref);
    lua_pop(L, 1);
    if( ref.value ) lua_pushinteger(L, ref.value);
    else lua_pushnil(L);
    lua_pushstring(L, lua_asset_state_name(state));
    return 2;
}

/* -------------------------------------------------------------- api.scene */

static int lua_scene_mesh_create(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_MeshRef r={0};enum ToriRS_Result s=a->scene.mesh_create(a,&r);if(r.value)lua_pushinteger(L,r.value);else lua_pushnil(L);lua_pushstring(L,lua_result_name(s));return 2; }
static int lua_scene_mesh_destroy(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->scene.mesh_destroy(a,lua_mesh_arg(L,1));return 0; }
static int lua_scene_mesh_vertex(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->scene.mesh_vertex(a,lua_mesh_arg(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),(int)luaL_checkinteger(L,4)));return 2; }
static int lua_scene_mesh_face(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->scene.mesh_face(a,lua_mesh_arg(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),(int)luaL_checkinteger(L,4),(int)luaL_checkinteger(L,5),(int)luaL_optinteger(L,6,0)));return 2; }
static int lua_scene_instance_create(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_SceneInstanceRef r={0};enum ToriRS_Result s=a->scene.instance_create(a,&r);if(r.value)lua_pushinteger(L,r.value);else lua_pushnil(L);lua_pushstring(L,lua_result_name(s));return 2; }
static int lua_scene_instance_destroy(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->scene.instance_destroy(a,lua_instance_arg(L,1));return 0; }
static int lua_scene_instance_model(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->scene.instance_model(a,lua_instance_arg(L,1),lua_model_arg(L,2)));return 2; }
static int lua_scene_instance_position(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->scene.instance_position(a,lua_instance_arg(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),(int)luaL_checkinteger(L,4),(int)luaL_optinteger(L,5,0),(int)luaL_optinteger(L,6,0)));return 2; }
static int lua_scene_instance_active(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->scene.instance_active(a,lua_instance_arg(L,1),lua_toboolean(L,2)!=0);return 0; }
static int lua_scene_instance_mesh(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->scene.instance_mesh(a,lua_instance_arg(L,1),lua_mesh_arg(L,2)));return 2; }
static int lua_scene_instance_cache_model(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->scene.instance_cache_model(a,lua_instance_arg(L,1),lua_enum_integer(L,2,TORIRS_SCENE_MODEL_CACHE,TORIRS_SCENE_MODEL_SPOTANIM,"scene model kind"),(int)luaL_checkinteger(L,3)));return 2; }
static int lua_scene_instance_recolor(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->scene.instance_recolor(a,lua_instance_arg(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3)));return 2; }
static int lua_scene_instance_clear_recolors(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->scene.instance_clear_recolors(a,lua_instance_arg(L,1));return 0; }
static int lua_scene_instance_animation(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->scene.instance_animation(a,lua_instance_arg(L,1),(int)luaL_checkinteger(L,2),lua_toboolean(L,3)!=0));return 2; }
static int lua_scene_instance_light(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->scene.instance_light(a,lua_instance_arg(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3)));return 2; }
static int lua_scene_instance_ready(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushboolean(L,a->scene.instance_ready(a,lua_instance_arg(L,1)));return 1; }

/* -------------------------------------------------------------- api.panel */

static int lua_panel_request(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_PanelDescriptor d;memset(&d,0,sizeof(d));luaL_checktype(L,1,LUA_TTABLE);d.icon_asset=lua_table_string(L,1,"icon_asset");d.preferred_width=lua_table_int(L,1,"preferred_width",0);lua_push_result(L,a->panel.request(a,&d));return 2;
}
static int lua_panel_invalidate(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->panel.invalidate(a);return 0; }
static int lua_panel_attention(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->panel.attention(a,lua_toboolean(L,1)!=0);return 0; }
static int lua_panel_set_text(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->panel.set_text(a,luaL_checkstring(L,1),luaL_tolstring(L,2,NULL)));return 2; }
static int lua_panel_set_value(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);int v=lua_isboolean(L,2)?(lua_toboolean(L,2)?1:0):(int)luaL_checkinteger(L,2);lua_push_result(L,a->panel.set_value(a,luaL_checkstring(L,1),v));return 2; }
static int lua_panel_set_height(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_push_result(L,a->panel.set_height(a,luaL_checkstring(L,1),(int)luaL_checkinteger(L,2)));return 2; }

static int
lua_select_options_arg(
    lua_State* L,
    int index,
    struct ToriRS_SelectOption* options,
    int capacity)
{
    int count;
    index=lua_absindex(L,index);luaL_checktype(L,index,LUA_TTABLE);count=(int)lua_rawlen(L,index);
    if(count>capacity)luaL_error(L,"select has %d options; limit is %d",count,capacity);
    for(int i=0;i<count;i++)
    {
        lua_rawgeti(L,index,i+1);luaL_checktype(L,-1,LUA_TTABLE);memset(&options[i],0,sizeof(options[i]));options[i].struct_size=sizeof(options[i]);
        options[i].value=lua_table_string(L,-1,"value");options[i].label=lua_table_string(L,-1,"label");options[i].enabled=lua_table_bool(L,-1,"enabled",true);options[i].detail=lua_table_string(L,-1,"detail");
        if(!options[i].value||!options[i].label)luaL_error(L,"select option %d requires value and label",i+1);
        lua_pop(L,1);
    }
    return count;
}

static int lua_panel_set_options(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_SelectOption options[PLUGIN_LUA_OPTIONS_MAX];int n=lua_select_options_arg(L,3,options,PLUGIN_LUA_OPTIONS_MAX);lua_push_result(L,a->panel.set_options(a,luaL_checkstring(L,1),luaL_checkstring(L,2),options,n));return 2;
}
static int lua_panel_redraw(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);a->panel.redraw(a,luaL_checkstring(L,1));return 0; }

/* -------------------------------------------------------------- api.cache */

static int lua_cache_frame_root(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,a->cache.frame_root(a));return 1; }
static int lua_cache_varbit(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,a->cache.varbit(a,(int)luaL_checkinteger(L,1)));return 1; }
static int lua_cache_varp(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,a->cache.varp(a,(int)luaL_checkinteger(L,1)));return 1; }
static int lua_cache_component_rect(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_Rect v;if(!a->cache.component_rect(a,(int)luaL_checkinteger(L,1),&v)){lua_pushnil(L);return 1;}lua_push_rect(L,v);return 1; }
static int lua_cache_invoke(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushboolean(L,a->cache.invoke(a,(int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2)));return 1; }
static int lua_cache_named_id(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);int id;if(!a->cache.named_id(a,luaL_checkstring(L,1),luaL_checkstring(L,2),&id)){lua_pushnil(L);return 1;}lua_pushinteger(L,id);return 1; }
static int lua_cache_tab_active(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,a->cache.tab_active(a));return 1; }
static int lua_cache_tab_enabled(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushboolean(L,a->cache.tab_enabled(a,(int)luaL_checkinteger(L,1)));return 1; }
static int lua_cache_tab_select(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushboolean(L,a->cache.tab_select(a,(int)luaL_checkinteger(L,1)));return 1; }

/* ------------------------------------------------------------- api.client */

static struct ToriRS_ClientApiV2 const* lua_client(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);if(!a->client)luaL_error(L,"client module is unavailable on this API minor version");return a->client; }
static int lua_client_display_get(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_ClientApiV2 const* c=lua_client(L);int v,lo,hi;if(!c->display_get(a,(int)luaL_checkinteger(L,1),&v,&lo,&hi)){lua_pushnil(L);return 1;}lua_pushinteger(L,v);lua_pushinteger(L,lo);lua_pushinteger(L,hi);return 3; }
static int lua_client_display_set(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_ClientApiV2 const* c=lua_client(L);lua_push_result(L,c->display_set(a,(int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2)));return 2; }
static int lua_client_feature_next(lua_State* L)
{
    struct ToriRS_ApiV2* api = lua_current_api(L);
    struct ToriRS_ClientApiV2 const* client = lua_client(L);
    struct ToriRS_FeatureInfo feature;
    int const next = client->feature_next(api, (int)luaL_optinteger(L, 1, -1), &feature);
    if( next < 0 ) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, next);
    lua_createtable(L, 0, 12);
    lua_pushstring(L, feature.key); lua_setfield(L, -2, "key");
    lua_pushstring(L, feature.label); lua_setfield(L, -2, "label");
    lua_pushstring(L, feature.section); lua_setfield(L, -2, "section");
    lua_pushinteger(L, feature.kind); lua_setfield(L, -2, "kind");
    lua_pushinteger(L, feature.value); lua_setfield(L, -2, "value");
    lua_pushinteger(L, feature.min); lua_setfield(L, -2, "min");
    lua_pushinteger(L, feature.max); lua_setfield(L, -2, "max");
    lua_pushstring(L, feature.choices); lua_setfield(L, -2, "choices");
    lua_createtable(L, feature.value_count, 0);
    for( int i = 0; i < feature.value_count; i++ )
    {
        lua_pushinteger(L, feature.values[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "values");
    lua_pushinteger(L, feature.value_count); lua_setfield(L, -2, "value_count");
    lua_pushboolean(L, feature.is_default != 0); lua_setfield(L, -2, "is_default");
    return 2;
}
static int lua_client_feature_get(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_ClientApiV2 const* c=lua_client(L);int v;if(!c->feature_get(a,luaL_checkstring(L,1),&v)){lua_pushnil(L);return 1;}lua_pushinteger(L,v);return 1; }
static int lua_client_feature_set(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_ClientApiV2 const* c=lua_client(L);lua_push_result(L,c->feature_set(a,luaL_checkstring(L,1),(int)luaL_checkinteger(L,2)));return 2; }
static int lua_client_world_cycle(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,lua_client(L)->world_cycle(a));return 1; }
static int lua_client_datestamp(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);char out[64];if(!lua_client(L)->datestamp(a,out,sizeof(out))){lua_pushnil(L);return 1;}lua_pushstring(L,out);return 1; }
static int lua_client_setting_color(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,lua_client(L)->setting_color(a,(int)luaL_checkinteger(L,1),(uint32_t)luaL_optinteger(L,2,0)));return 1; }
static int lua_client_memory_bytes(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,(lua_Integer)lua_client(L)->memory_bytes(a));return 1; }
static int
lua_client_disable_self(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    (void)lua_current_api(L);
    lua_script_defer_disable(script, luaL_checkstring(L, 1));
    /* Unwind this Lua handler before native teardown resets its API table.
     * Nested dispatch records the request and the outermost callback flushes
     * it after every active Lua frame has returned. */
    return luaL_error(L, "plugin requested disable_self");
}

/* --------------------------------------------------------------- api.game */

static struct ToriRS_GameApiV2 const* lua_game(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);if(!a->game)luaL_error(L,"game module is unavailable on this API minor version");return a->game; }
static int lua_game_skill(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_SkillSnapshot v;memset(&v,0,sizeof(v));v.struct_size=sizeof(v);if(!lua_game(L)->skill(a,(int)luaL_checkinteger(L,1),&v)){lua_pushnil(L);return 1;}lua_createtable(L,0,8);lua_pushinteger(L,v.index);lua_setfield(L,-2,"index");lua_pushstring(L,v.name);lua_setfield(L,-2,"name");lua_pushinteger(L,v.current_level);lua_setfield(L,-2,"current_level");lua_pushinteger(L,v.base_level);lua_setfield(L,-2,"base_level");lua_pushinteger(L,v.xp);lua_setfield(L,-2,"xp");lua_pushinteger(L,v.level_xp);lua_setfield(L,-2,"level_xp");lua_pushinteger(L,v.next_level_xp);lua_setfield(L,-2,"next_level_xp");return 1;
}
static int lua_game_run_energy(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,lua_game(L)->run_energy(a));return 1; }
static int lua_game_inventory_size(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,lua_game(L)->inventory_size(a,lua_enum_integer(L,1,TORIRS_INVENTORY_BACKPACK,TORIRS_INVENTORY_BANK,"inventory")));return 1; }
static int lua_game_inventory_slot(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);int id,count;if(!lua_game(L)->inventory_slot(a,lua_enum_integer(L,1,TORIRS_INVENTORY_BACKPACK,TORIRS_INVENTORY_BANK,"inventory"),(int)luaL_checkinteger(L,2),&id,&count)){lua_pushnil(L);return 1;}lua_pushinteger(L,id);lua_pushinteger(L,count);return 2; }
static int lua_game_item_info(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_ItemInfo v;if(!lua_game(L)->item_info(a,(int)luaL_checkinteger(L,1),&v)){lua_pushnil(L);return 1;}lua_createtable(L,0,16);lua_pushinteger(L,v.obj_id);lua_setfield(L,-2,"obj_id");lua_pushstring(L,v.name);lua_setfield(L,-2,"name");lua_pushinteger(L,v.cost);lua_setfield(L,-2,"cost");lua_pushboolean(L,v.stackable);lua_setfield(L,-2,"stackable");lua_pushinteger(L,v.cert_link);lua_setfield(L,-2,"cert_link");lua_pushinteger(L,v.wearpos);lua_setfield(L,-2,"wearpos");lua_pushinteger(L,v.wearpos2);lua_setfield(L,-2,"wearpos2");lua_pushinteger(L,v.wearpos3);lua_setfield(L,-2,"wearpos3");lua_pushboolean(L,v.has_bonuses);lua_setfield(L,-2,"has_bonuses");lua_createtable(L,TORIRS_EQUIPMENT_BONUS_COUNT,0);for(int i=0;i<TORIRS_EQUIPMENT_BONUS_COUNT;i++){lua_pushinteger(L,v.bonus[i]);lua_rawseti(L,-2,i+1);}lua_setfield(L,-2,"bonuses");lua_pushinteger(L,v.attack_rate);lua_setfield(L,-2,"attack_rate");lua_pushinteger(L,v.ranged_strength);lua_setfield(L,-2,"ranged_strength");return 1;
}
static int lua_game_item_image(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_ImageRef ref={0};int style=lua_isnoneornil(L,3)?TORIRS_ITEM_ICON_BORDERED:lua_enum_integer(L,3,TORIRS_ITEM_ICON_PLAIN,TORIRS_ITEM_ICON_SELECTED,"item icon style");enum ToriRS_AssetState s=lua_game(L)->item_image(a,(int)luaL_checkinteger(L,1),(int)luaL_optinteger(L,2,1),style,&ref);if(ref.value)lua_pushinteger(L,ref.value);else lua_pushnil(L);lua_pushstring(L,lua_asset_state_name(s));return 2; }
static void lua_push_highlight(lua_State* L,struct ToriRS_HighlightItem const* v)
{
    lua_createtable(L,0,15);
#define HI(k,x) lua_pushinteger(L,(lua_Integer)(x));lua_setfield(L,-2,(k))
    HI("kind",v->kind);HI("element_id",v->element_id);HI("tile_x",v->tile_x);HI("tile_z",v->tile_z);HI("level",v->level);HI("size_x",v->size_x);HI("size_z",v->size_z);HI("rgb",v->rgb);HI("opacity",v->opacity);HI("outline_width",v->outline_width);HI("flags",v->flags);HI("fine_x",v->fine_x);HI("fine_z",v->fine_z);HI("overhead_height",v->overhead_height);
#undef HI
    lua_pushstring(L,v->name);lua_setfield(L,-2,"name");
}
static int lua_game_highlight_next(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_HighlightItem v;int n=lua_game(L)->highlight_next(a,(int)luaL_optinteger(L,1,-1),&v);if(n<0){lua_pushnil(L);return 1;}lua_pushinteger(L,n);lua_push_highlight(L,&v);return 2; }
static int lua_game_loot_source_next(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_LootSource v;int n=lua_game(L)->loot_source_next(a,(int)luaL_optinteger(L,1,-1),&v);if(n<0){lua_pushnil(L);return 1;}lua_pushinteger(L,n);lua_createtable(L,0,4);lua_pushinteger(L,v.id);lua_setfield(L,-2,"id");lua_pushstring(L,v.name);lua_setfield(L,-2,"name");lua_pushinteger(L,v.row_count);lua_setfield(L,-2,"row_count");lua_pushinteger(L,v.kill_count);lua_setfield(L,-2,"kill_count");return 2; }
static int lua_game_loot_row_next(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_LootRow v;int n=lua_game(L)->loot_row_next(a,(int)luaL_checkinteger(L,1),(int)luaL_optinteger(L,2,-1),&v);if(n<0){lua_pushnil(L);return 1;}lua_pushinteger(L,n);lua_createtable(L,0,3);lua_pushinteger(L,v.obj_id);lua_setfield(L,-2,"obj_id");lua_pushinteger(L,v.quantity);lua_setfield(L,-2,"quantity");lua_pushinteger(L,v.value);lua_setfield(L,-2,"value");return 2; }
static int lua_game_entity_part(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);char out[128];int kind=lua_enum_integer(L,1,TORIRS_ENTITY_NPC,TORIRS_ENTITY_OBJ,"entity kind");char const* p=lua_game(L)->entity_part(a,kind,(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),(int)luaL_checkinteger(L,4),(int)luaL_checkinteger(L,5),out,sizeof(out));if(p)lua_pushstring(L,p);else lua_pushnil(L);return 1; }
static int lua_game_entity_look(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);struct ToriRS_EntityAppearance v;luaL_checktype(L,2,LUA_TTABLE);v.hull=lua_table_bool(L,2,"hull",false);v.rgb=(uint32_t)lua_table_int(L,2,"rgb",0);v.fill_alpha=lua_table_int(L,2,"fill_alpha",0);v.shape=lua_table_int(L,2,"shape",TORIRS_HULL_BOUNDS);if(v.shape<TORIRS_HULL_BOUNDS||v.shape>TORIRS_HULL_MESH)return luaL_error(L,"entity hull shape is invalid");lua_push_result(L,lua_game(L)->entity_look(a,luaL_checkstring(L,1),&v));return 2; }
static int lua_game_entity_ops(lua_State* L)
{
    struct ToriRS_ApiV2* a=lua_current_api(L);char const* ops[8];int mode=lua_enum_integer(L,2,TORIRS_ENTITY_OPS_APPEND,TORIRS_ENTITY_OPS_NONE,"entity operation mode");luaL_checktype(L,3,LUA_TTABLE);int n=(int)lua_rawlen(L,3);if(n>8)return luaL_error(L,"too many entity operations");for(int i=0;i<n;i++){lua_rawgeti(L,3,i+1);ops[i]=luaL_checkstring(L,-1);lua_pop(L,1);}lua_push_result(L,lua_game(L)->entity_ops(a,luaL_checkstring(L,1),mode,ops,n,(uint32_t)luaL_optinteger(L,4,0)));return 2;
}
static int lua_game_loot_revision(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushinteger(L,(lua_Integer)lua_game(L)->loot_revision(a));return 1; }
static int lua_game_loot_source_clear(lua_State* L) { struct ToriRS_ApiV2* a=lua_current_api(L);lua_pushboolean(L,lua_game(L)->loot_source_clear(a,(int)luaL_checkinteger(L,1)));return 1; }

/* ----------------------------------------------------- draw builder object */

static struct ToriRS_DrawBuilder* lua_draw_builder(lua_State* L)
{
    struct LuaScript* script=lua_upvalue_script(L);if(!script||!script->cur_draw)return (void*)(intptr_t)luaL_error(L,"draw builder used outside a draw callback");return script->cur_draw;
}
static int lua_builder_rect(lua_State* L) { struct ToriRS_DrawBuilder* d=lua_draw_builder(L);d->rect(d,(struct ToriRS_Rect){(int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),(int)luaL_checkinteger(L,4)},lua_color_arg(L,5),(int)luaL_optinteger(L,6,255));return 0; }
static int lua_builder_line(lua_State* L) { struct ToriRS_DrawBuilder* d=lua_draw_builder(L);d->line(d,(int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),(int)luaL_checkinteger(L,4),lua_color_arg(L,5),(int)luaL_optinteger(L,6,255));return 0; }
static int lua_builder_text(lua_State* L) { struct ToriRS_DrawBuilder* d=lua_draw_builder(L);d->text(d,(int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2),luaL_checkstring(L,3),lua_isnoneornil(L,4)?0xffffffu:lua_color_arg(L,4));return 0; }
static int lua_builder_image(lua_State* L) { struct ToriRS_DrawBuilder* d=lua_draw_builder(L);d->image(d,lua_image_arg(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),(int)luaL_optinteger(L,4,255));return 0; }
static int lua_builder_world_tile(lua_State* L) { struct ToriRS_DrawBuilder* d=lua_draw_builder(L);uint32_t fill=lua_color_arg(L,4);uint32_t outline=lua_isnoneornil(L,5)?fill:lua_color_arg(L,5);lua_push_result(L,d->world_tile(d,(int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),fill,outline,(int)luaL_optinteger(L,6,0)));return 2; }
static int lua_builder_world_hull(lua_State* L) { struct ToriRS_DrawBuilder* d=lua_draw_builder(L);int shape=TORIRS_HULL_BOUNDS;if(lua_type(L,4)==LUA_TSTRING){char const*name=lua_tostring(L,4);if(strcmp(name,"mesh")==0)shape=TORIRS_HULL_MESH;else if(strcmp(name,"bounds")!=0)return luaL_error(L,"unknown hull shape '%s'",name);}else if(!lua_isnoneornil(L,4))shape=lua_enum_integer(L,4,TORIRS_HULL_BOUNDS,TORIRS_HULL_MESH,"hull shape");lua_push_result(L,d->world_hull(d,(int)luaL_checkinteger(L,1),lua_color_arg(L,2),(int)luaL_optinteger(L,3,0),shape));return 2; }
static int lua_builder_action_region(lua_State* L) { struct ToriRS_DrawBuilder* d=lua_draw_builder(L);lua_push_result(L,d->action_region(d,lua_check_rect(L,1),luaL_checkstring(L,2)));return 2; }
static int lua_builder_image_clip(lua_State* L) { struct ToriRS_DrawBuilder* d=lua_draw_builder(L);d->image_clip(d,lua_image_arg(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),lua_check_rect(L,4),(int)luaL_optinteger(L,5,255));return 0; }
static int lua_builder_action_region_id(lua_State* L) { struct ToriRS_DrawBuilder* d=lua_draw_builder(L);lua_push_result(L,d->action_region_id(d,lua_check_rect(L,1),luaL_checkstring(L,2),(uint32_t)luaL_checkinteger(L,3)));return 2; }
static int lua_builder_context(lua_State* L) { struct ToriRS_DrawBuilder* d=lua_draw_builder(L);struct ToriRS_DrawContext v;memset(&v,0,sizeof(v));v.struct_size=sizeof(v);if(!d->context(d,&v)){lua_pushnil(L);return 1;}lua_createtable(L,0,2);lua_push_rect(L,v.bounds);lua_setfield(L,-2,"bounds");lua_push_rect(L,v.clip);lua_setfield(L,-2,"clip");return 1; }

/* ---------------------------------------------------- panel builder object */

static struct ToriRS_PanelBuilder* lua_panel_builder(lua_State* L)
{
    struct LuaScript* script=lua_upvalue_script(L);if(!script||!script->cur_panel)return (void*)(intptr_t)luaL_error(L,"panel builder used outside on_ui_build");return script->cur_panel;
}
static int lua_panel_builder_heading(lua_State* L) { struct ToriRS_PanelBuilder* p=lua_panel_builder(L);p->heading(p,luaL_checkstring(L,1));return 0; }
static int lua_panel_builder_paragraph(lua_State* L) { struct ToriRS_PanelBuilder* p=lua_panel_builder(L);p->paragraph(p,luaL_checkstring(L,1));return 0; }
static int lua_panel_builder_toggle(lua_State* L) { struct ToriRS_PanelBuilder* p=lua_panel_builder(L);p->toggle(p,luaL_checkstring(L,1),luaL_checkstring(L,2),lua_toboolean(L,3)!=0);return 0; }
static int lua_panel_builder_select(lua_State* L) { struct ToriRS_PanelBuilder* p=lua_panel_builder(L);struct ToriRS_SelectOption options[PLUGIN_LUA_OPTIONS_MAX];int n=lua_select_options_arg(L,4,options,PLUGIN_LUA_OPTIONS_MAX);p->select(p,luaL_checkstring(L,1),luaL_checkstring(L,2),luaL_checkstring(L,3),options,n);return 0; }
static int lua_panel_builder_button(lua_State* L) { struct ToriRS_PanelBuilder* p=lua_panel_builder(L);p->button(p,luaL_checkstring(L,1),luaL_checkstring(L,2),lua_isnoneornil(L,3)||lua_toboolean(L,3));return 0; }
static int lua_panel_builder_custom(lua_State* L) { struct ToriRS_PanelBuilder* p=lua_panel_builder(L);p->custom(p,luaL_checkstring(L,1),(int)luaL_optinteger(L,2,TORIRS_PANEL_CUSTOM_HEIGHT_DEFAULT));return 0; }
static int lua_panel_builder_label(lua_State* L) { struct ToriRS_PanelBuilder* p=lua_panel_builder(L);p->label(p,luaL_checkstring(L,1),luaL_checkstring(L,2));return 0; }
static int lua_panel_builder_key_value(lua_State* L) { struct ToriRS_PanelBuilder* p=lua_panel_builder(L);p->key_value(p,luaL_checkstring(L,1),luaL_checkstring(L,2),luaL_checkstring(L,3));return 0; }
static int lua_panel_builder_action_row(lua_State* L) { struct ToriRS_PanelBuilder*p=lua_panel_builder(L);p->action_row(p,luaL_checkstring(L,1),luaL_checkstring(L,2),luaL_optstring(L,3,""));return 0; }
static int lua_panel_builder_node(lua_State* L)
{
    struct ToriRS_PanelBuilder* panel = lua_panel_builder(L);
    struct ToriRS_PanelNode node;
    struct ToriRS_SelectOption options[PLUGIN_LUA_OPTIONS_MAX];
    memset(&node, 0, sizeof(node));
    node.struct_size = sizeof(node);
    luaL_checktype(L, 1, LUA_TTABLE);
    node.kind = lua_table_int(L, 1, "kind", TORIRS_PANEL_LABEL);
    node.id = lua_table_string(L, 1, "id");
    node.label = lua_table_string(L, 1, "label");
    node.text = lua_table_string(L, 1, "text");
    node.value = lua_table_int(L, 1, "value", 0);
    node.preferred_height = lua_table_int(L, 1, "preferred_height", 0);
    lua_raw_getfield(L, 1, "options");
    if( lua_istable(L, -1) )
    {
        node.option_count = lua_select_options_arg(
            L, -1, options, PLUGIN_LUA_OPTIONS_MAX);
        node.options = options;
    }
    else if( !lua_isnil(L, -1) )
        return luaL_error(L, "panel node options must be an array");
    lua_pop(L, 1);
    lua_push_result(L, panel->node(panel, &node));
    return 2;
}

/* ---------------------------------------------------- frame builder object */

static struct ToriRS_FrameBuilder*
lua_frame_builder(lua_State* L)
{
    struct LuaScript* script = lua_upvalue_script(L);
    if( !script || !script->cur_frame )
        return (void*)(intptr_t)luaL_error(L, "frame builder used outside an offer build callback");
    return script->cur_frame;
}

static int
lua_surface_from_arg(lua_State* L, int index)
{
    static char const* const NAMES[TORIRS_SURFACE_COUNT] = {
        "viewport", "minimap", "sidebar", "chat", "chat_buttons",
        "modal", "compass", "orbs",
    };
    char const* name;
    if( lua_isinteger(L, index) )
        return lua_enum_integer(L, index, TORIRS_SURFACE_VIEWPORT, TORIRS_SURFACE_COUNT - 1,
            "frame surface");
    name = luaL_checkstring(L, index);
    for( int i = 0; i < TORIRS_SURFACE_COUNT; i++ )
        if( strcmp(name, NAMES[i]) == 0 ) return i;
    return luaL_error(L, "unknown frame surface '%s'", name);
}

static struct ToriRS_ImageRef
lua_optional_image(lua_State* L, int table, char const* field)
{
    struct ToriRS_ImageRef image = { 0 };
    table = lua_absindex(L, table);
    lua_raw_getfield(L, table, field);
    if( lua_isinteger(L, -1) ) image.value = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return image;
}

static int lua_frame_builder_surface(lua_State* L)
{
    struct ToriRS_FrameBuilder* frame = lua_frame_builder(L);
    frame->surface(frame, lua_surface_from_arg(L, 1), lua_check_rect(L, 2));
    return 0;
}
static int lua_frame_builder_surface_member(lua_State* L)
{
    struct ToriRS_FrameBuilder* frame = lua_frame_builder(L);
    frame->surface_member(frame, lua_surface_from_arg(L, 1),
        (int)luaL_checkinteger(L, 2), lua_check_rect(L, 3));
    return 0;
}
static int lua_frame_builder_skin(lua_State* L)
{
    struct ToriRS_FrameBuilder* frame = lua_frame_builder(L);
    struct ToriRS_FrameSkin skin;
    luaL_checktype(L, 2, LUA_TTABLE);
    memset(&skin, 0, sizeof(skin));
    skin.struct_size = sizeof(skin);
    skin.image = lua_optional_image(L, 2, "image");
    skin.mask = lua_optional_image(L, 2, "mask");
    frame->skin(frame, lua_surface_from_arg(L, 1), &skin);
    return 0;
}
static int lua_frame_builder_ui_node(lua_State* L)
{
    struct ToriRS_FrameBuilder* frame = lua_frame_builder(L);
    struct ToriRS_UiNode node;
    lua_ui_node_arg(L, 2, &node);
    frame->ui_node(frame, luaL_checkstring(L, 1), &node);
    return 0;
}
static int lua_frame_builder_scrollbar(lua_State* L)
{
    struct ToriRS_FrameBuilder* frame = lua_frame_builder(L);
    struct ToriRS_FrameScrollbar skin;
    luaL_checktype(L, 1, LUA_TTABLE);
    memset(&skin, 0, sizeof(skin));
    skin.struct_size = sizeof(skin);
    skin.up = lua_optional_image(L, 1, "up");
    skin.down = lua_optional_image(L, 1, "down");
    skin.track = lua_optional_image(L, 1, "track");
    skin.thumb = lua_optional_image(L, 1, "thumb");
    skin.split_thumb = lua_table_bool(L, 1, "split_thumb", false);
    skin.thumb_top = lua_optional_image(L, 1, "thumb_top");
    skin.thumb_middle = lua_optional_image(L, 1, "thumb_middle");
    skin.thumb_bottom = lua_optional_image(L, 1, "thumb_bottom");
    frame->scrollbar(frame, &skin);
    return 0;
}
static int lua_frame_builder_reason(lua_State* L)
{
    struct ToriRS_FrameBuilder* frame = lua_frame_builder(L);
    frame->reason(frame, luaL_checkstring(L, 1));
    return 0;
}
static int lua_frame_builder_surface_overlay(lua_State* L)
{
    struct ToriRS_FrameBuilder* frame = lua_frame_builder(L);
    struct ToriRS_FrameSurfaceOverlay overlay;
    luaL_checktype(L, 2, LUA_TTABLE);
    memset(&overlay, 0, sizeof(overlay));
    overlay.struct_size = sizeof(overlay);
    overlay.image = lua_optional_image(L, 2, "image");
    overlay.x = lua_table_int(L, 2, "x", 0);
    overlay.y = lua_table_int(L, 2, "y", 0);
    overlay.alpha = lua_table_int(L, 2, "alpha", 255);
    frame->surface_overlay(frame, lua_surface_from_arg(L, 1), &overlay);
    return 0;
}

/* Registration arrays are the runtime inventory.  The Python contract test
 * reads these exact arrays and compares them bidirectionally with LuaLS. */
static struct LuaFn const LUA_CORE_FNS[] = {
    {"log",lua_core_log},{"notify",lua_core_notify},{"screen",lua_core_screen},
    {"frame_ms",lua_core_frame_ms},{"frame_work_us",lua_core_frame_work_us},
    {"lane",lua_core_lane},{"capability",lua_core_capability},{"plugin_id",lua_core_plugin_id},{NULL,NULL}
};
static struct LuaFn const LUA_CONFIG_FNS[] = {
    {"has",lua_config_has},{"get_bool",lua_config_get_bool},{"get_int",lua_config_get_int},
    {"get_color",lua_config_get_color},{"get_string",lua_config_get_string},{"set",lua_config_set},{NULL,NULL}
};
static struct LuaFn const LUA_WORLD_FNS[] = {
    {"local_player",lua_world_local_player},{"npc_next",lua_world_npc_next},
    {"npc_by_slot",lua_world_npc_by_slot},{"player_next",lua_world_player_next},
    {"item_next",lua_world_item_next},{"scenery_next",lua_world_scenery_next},{NULL,NULL}
};
static struct LuaFn const LUA_INPUT_FNS[] = {
    {"key_held",lua_input_key_held},{"pointer",lua_input_pointer},{"hover_tile",lua_input_hover_tile},
    {"hover_entity",lua_input_hover_entity},{"text_input",lua_input_text_input},{"chat_focus",lua_input_chat_focus},{NULL,NULL}
};
static struct LuaFn const LUA_UI_FNS[] = {
    {"ref",lua_ui_ref},{"info",lua_ui_info},{"invoke",lua_ui_invoke},
    {"contribution_info",lua_ui_contribution_info},{"update",lua_ui_update},
    {"menu_add",lua_ui_menu_add},{"set_enabled",lua_ui_set_enabled},{NULL,NULL}
};
static struct LuaFn const LUA_PLACEMENT_FNS[] = {
    {"revision",lua_placement_revision},{"area",lua_placement_area},{"primary",lua_placement_primary},
    {"place",lua_placement_place},{"rect_next",lua_placement_rect_next},{"contains",lua_placement_contains},
    {"reserve",lua_placement_reserve},{"reservation_rect",lua_placement_reservation_rect},{NULL,NULL}
};
static struct LuaFn const LUA_FRAME_FNS[] = {
    {"offer_next",lua_frame_offer_next},{"selection",lua_frame_selection},{"select",lua_frame_select},
    {"invalidate",lua_frame_invalidate},{"surface_native_size",lua_frame_surface_native_size},{NULL,NULL}
};
static struct LuaFn const LUA_DRAW_API_FNS[] = {
    {"project",lua_draw_project},{"element_height",lua_draw_element_height},
    {"hsl_from_rgb",lua_draw_hsl_from_rgb},{"hsl_to_rgb",lua_draw_hsl_to_rgb},{NULL,NULL}
};
static struct LuaFn const LUA_ASSETS_FNS[] = {
    {"request",lua_assets_request},{"bytes",lua_assets_bytes},{"save",lua_assets_save},
    {"release",lua_assets_release},{"image",lua_assets_image},{"image_size",lua_assets_image_size},
    {"image_release",lua_assets_image_release},{"model",lua_assets_model},{"model_release",lua_assets_model_release},
    {"screenshot",lua_assets_screenshot},{"image_pixels",lua_assets_image_pixels},
    {"image_compose",lua_assets_image_compose},{NULL,NULL}
};
static struct LuaFn const LUA_SCENE_FNS[] = {
    {"mesh_create",lua_scene_mesh_create},{"mesh_destroy",lua_scene_mesh_destroy},
    {"mesh_vertex",lua_scene_mesh_vertex},{"mesh_face",lua_scene_mesh_face},
    {"instance_create",lua_scene_instance_create},{"instance_destroy",lua_scene_instance_destroy},
    {"instance_model",lua_scene_instance_model},{"instance_position",lua_scene_instance_position},
    {"instance_active",lua_scene_instance_active},{"instance_mesh",lua_scene_instance_mesh},
    {"instance_cache_model",lua_scene_instance_cache_model},{"instance_recolor",lua_scene_instance_recolor},
    {"instance_clear_recolors",lua_scene_instance_clear_recolors},{"instance_animation",lua_scene_instance_animation},
    {"instance_light",lua_scene_instance_light},{"instance_ready",lua_scene_instance_ready},{NULL,NULL}
};
static struct LuaFn const LUA_PANEL_FNS[] = {
    {"request",lua_panel_request},{"invalidate",lua_panel_invalidate},{"attention",lua_panel_attention},
    {"set_text",lua_panel_set_text},{"set_value",lua_panel_set_value},{"set_height",lua_panel_set_height},
    {"set_options",lua_panel_set_options},{"redraw",lua_panel_redraw},{NULL,NULL}
};
static struct LuaFn const LUA_CACHE_FNS[] = {
    {"frame_root",lua_cache_frame_root},{"varbit",lua_cache_varbit},{"varp",lua_cache_varp},
    {"component_rect",lua_cache_component_rect},{"invoke",lua_cache_invoke},{"named_id",lua_cache_named_id},
    {"tab_active",lua_cache_tab_active},{"tab_enabled",lua_cache_tab_enabled},{"tab_select",lua_cache_tab_select},{NULL,NULL}
};
static struct LuaFn const LUA_CLIENT_FNS[] = {
    {"display_get",lua_client_display_get},{"display_set",lua_client_display_set},
    {"feature_next",lua_client_feature_next},{"feature_get",lua_client_feature_get},
    {"feature_set",lua_client_feature_set},{"world_cycle",lua_client_world_cycle},
    {"datestamp",lua_client_datestamp},{"setting_color",lua_client_setting_color},
    {"memory_bytes",lua_client_memory_bytes},{"disable_self",lua_client_disable_self},{NULL,NULL}
};
static struct LuaFn const LUA_GAME_FNS[] = {
    {"skill",lua_game_skill},{"run_energy",lua_game_run_energy},{"inventory_size",lua_game_inventory_size},
    {"inventory_slot",lua_game_inventory_slot},{"item_info",lua_game_item_info},{"item_image",lua_game_item_image},
    {"highlight_next",lua_game_highlight_next},{"loot_source_next",lua_game_loot_source_next},
    {"loot_row_next",lua_game_loot_row_next},{"entity_part",lua_game_entity_part},
    {"entity_look",lua_game_entity_look},{"entity_ops",lua_game_entity_ops},
    {"loot_revision",lua_game_loot_revision},{"loot_source_clear",lua_game_loot_source_clear},{NULL,NULL}
};
static struct LuaFn const LUA_DRAW_BUILDER_FNS[] = {
    {"rect",lua_builder_rect},{"line",lua_builder_line},{"text",lua_builder_text},
    {"image",lua_builder_image},{"world_tile",lua_builder_world_tile},{"world_hull",lua_builder_world_hull},
    {"action_region",lua_builder_action_region},{"image_clip",lua_builder_image_clip},
    {"action_region_id",lua_builder_action_region_id},{"context",lua_builder_context},{NULL,NULL}
};
static struct LuaFn const LUA_PANEL_BUILDER_FNS[] = {
    {"heading",lua_panel_builder_heading},{"paragraph",lua_panel_builder_paragraph},
    {"toggle",lua_panel_builder_toggle},{"select",lua_panel_builder_select},
    {"button",lua_panel_builder_button},{"custom",lua_panel_builder_custom},
    {"label",lua_panel_builder_label},{"key_value",lua_panel_builder_key_value},
    {"action_row",lua_panel_builder_action_row},
    {"node",lua_panel_builder_node},{NULL,NULL}
};
static struct LuaFn const LUA_FRAME_BUILDER_FNS[] = {
    {"surface",lua_frame_builder_surface},{"surface_member",lua_frame_builder_surface_member},
    {"skin",lua_frame_builder_skin},{"ui_node",lua_frame_builder_ui_node},
    {"scrollbar",lua_frame_builder_scrollbar},{"reason",lua_frame_builder_reason},
    {"surface_overlay",lua_frame_builder_surface_overlay},{NULL,NULL}
};

struct LuaModuleRegistration
{
    char const* name;
    struct LuaFn const* functions;
};

static struct LuaModuleRegistration const LUA_API_MODULES[] = {
    {"core",LUA_CORE_FNS},{"config",LUA_CONFIG_FNS},{"world",LUA_WORLD_FNS},
    {"input",LUA_INPUT_FNS},{"ui",LUA_UI_FNS},{"placement",LUA_PLACEMENT_FNS},
    {"frame",LUA_FRAME_FNS},{"draw",LUA_DRAW_API_FNS},{"assets",LUA_ASSETS_FNS},
    {"scene",LUA_SCENE_FNS},{"panel",LUA_PANEL_FNS},{"cache",LUA_CACHE_FNS},
    {"client",LUA_CLIENT_FNS},{"game",LUA_GAME_FNS},{NULL,NULL}
};

static void
lua_build_api_table(struct LuaScript* script)
{
    lua_State* L=script->L;
    lua_newtable(L);
    for(struct LuaModuleRegistration const* module=LUA_API_MODULES;module->name;module++)
    {
        lua_register_functions(L,script,module->functions);
        if(strcmp(module->name,"config")==0)
        {
            lua_newtable(L);
            lua_pushlightuserdata(L,script);lua_pushcclosure(L,lua_config_index,1);lua_setfield(L,-2,"__index");
            lua_pushlightuserdata(L,script);lua_pushcclosure(L,lua_config_newindex,1);lua_setfield(L,-2,"__newindex");
            lua_setmetatable(L,-2);
        }
        lua_setfield(L,-2,module->name);
    }
    script->api_ref=luaL_ref(L,LUA_REGISTRYINDEX);
    lua_register_functions(L,script,LUA_DRAW_BUILDER_FNS);script->draw_ref=luaL_ref(L,LUA_REGISTRYINDEX);
    lua_register_functions(L,script,LUA_PANEL_BUILDER_FNS);script->panel_builder_ref=luaL_ref(L,LUA_REGISTRYINDEX);
    lua_register_functions(L,script,LUA_FRAME_BUILDER_FNS);script->frame_builder_ref=luaL_ref(L,LUA_REGISTRYINDEX);
}

static void
lua_open_sandbox_libs(lua_State* L)
{
    /* Scripts are trusted local extensions. The instruction hook bounds Lua
     * bytecode, but cannot preempt one long-running C string-pattern call;
     * changing that trust model needs constrained wrappers or a native
     * watchdog rather than pretending the VM hook covers native library work. */
    static luaL_Reg const LIBS[]={
        {LUA_GNAME,luaopen_base},{LUA_TABLIBNAME,luaopen_table},
        {LUA_STRLIBNAME,luaopen_string},{LUA_MATHLIBNAME,luaopen_math},{LUA_UTF8LIBNAME,luaopen_utf8},{NULL,NULL}
    };
    for(luaL_Reg const* lib=LIBS;lib->name;lib++){luaL_requiref(L,lib->name,lib->func,1);lua_pop(L,1);}
    static char const* const REMOVE[]={
        "dofile", "loadfile", "load", "require", "print", "warn",
        /* A hook error must cross the runtime bridge's outer pcall; script-level
         * protected calls could otherwise catch and repeatedly reset it. */
        "pcall", "xpcall",
        /* User-created __gc table finalizers run from lua_close, outside any
         * callback budget. With no metatable access, scripts cannot install
         * one that hangs reload or shutdown. */
        "setmetatable", "getmetatable",
    };
    for(size_t i=0;i<sizeof(REMOVE)/sizeof(REMOVE[0]);i++){lua_pushnil(L);lua_setglobal(L,REMOVE[i]);}
}

/* ---------------------------------------------------------- callback glue */

static bool
lua_call_begin(struct LuaScript* script, struct ToriRS_ApiV2* api, enum LuaHandler handler)
{
    if(!script||!script->alive||script->pending_disable[0]||script->handler_ref[handler]==LUA_NOREF)return false;
    if( !lua_callback_scope_push(script, api) )
    {
        TORIRS_ERR("plugin: Lua callback nesting limit reached in '%s'\n", script->name);
        return false;
    }
    lua_rawgeti(script->L,LUA_REGISTRYINDEX,script->handler_ref[handler]);
    lua_rawgeti(script->L,LUA_REGISTRYINDEX,script->api_ref);
    return true;
}

static enum ToriRS_CallbackResult
lua_call_end(struct LuaScript* script, enum LuaHandler handler, int argument_count, bool verdict)
{
    lua_State* L=script->L;struct ToriRS_ApiV2* api=script->cur_api;enum ToriRS_CallbackResult result=TORIRS_CALLBACK_CONTINUE;
    int status=lua_callback_pcall(script,argument_count,verdict?1:0);
    if(status!=LUA_OK)
    {
        char const* error=lua_tostring(L,-1);char copy[128];snprintf(copy,sizeof(copy),"%s",error?error:"error");lua_pop(L,1);lua_script_fault(script,api,LUA_HANDLER_NAMES[handler],copy);(void)lua_script_flush_disable(script,api);return TORIRS_CALLBACK_CONTINUE;
    }
    if(verdict)
    {
        if(lua_isboolean(L,-1))result=lua_toboolean(L,-1)?TORIRS_CALLBACK_CONSUME:TORIRS_CALLBACK_CONTINUE;
        else if(lua_type(L,-1)==LUA_TSTRING&&strcmp(lua_tostring(L,-1),"consume")==0)result=TORIRS_CALLBACK_CONSUME;
        lua_pop(L,1);
    }
    if( lua_script_flush_disable(script,api) )
        result = TORIRS_CALLBACK_CONTINUE;
    return result;
}

static void lua_push_frame_event(lua_State* L,struct ToriRS_FrameEvent const* e){lua_createtable(L,0,2);lua_pushinteger(L,(lua_Integer)e->now_ms);lua_setfield(L,-2,"now_ms");lua_pushinteger(L,(lua_Integer)e->drawn_frames);lua_setfield(L,-2,"drawn_frames");}
static void lua_push_tick_event(lua_State* L,struct ToriRS_TickEvent const* e){lua_createtable(L,0,1);lua_pushinteger(L,e->cycle);lua_setfield(L,-2,"cycle");}
static void lua_push_world_event(lua_State* L,struct ToriRS_WorldLoadedEvent const* e){lua_createtable(L,0,2);lua_pushinteger(L,e->base_tile_x);lua_setfield(L,-2,"base_tile_x");lua_pushinteger(L,e->base_tile_z);lua_setfield(L,-2,"base_tile_z");}
static char const* lua_screen_name(int screen){switch(screen){case TORIRS_SCREEN_BOOT:return "boot";case TORIRS_SCREEN_TITLE:return "title";case TORIRS_SCREEN_CONNECTING:return "connecting";case TORIRS_SCREEN_GAME:return "game";default:return "unknown";}}
static void lua_push_screen_event(lua_State* L,struct ToriRS_ScreenChangedEvent const* e){lua_createtable(L,0,2);lua_pushstring(L,lua_screen_name(e->screen));lua_setfield(L,-2,"screen");lua_pushstring(L,lua_screen_name(e->previous));lua_setfield(L,-2,"previous");}
static void lua_push_asset_event(lua_State* L,struct ToriRS_AssetEvent const* e){lua_createtable(L,0,3);lua_pushstring(L,e->name?e->name:"");lua_setfield(L,-2,"name");lua_pushinteger(L,e->size);lua_setfield(L,-2,"size");lua_pushboolean(L,e->ok);lua_setfield(L,-2,"ok");}
static void lua_push_chat_event(lua_State* L,struct ToriRS_ChatMessageEvent const* e){lua_createtable(L,0,3);lua_pushinteger(L,e->type);lua_setfield(L,-2,"type");lua_pushstring(L,e->sender);lua_setfield(L,-2,"sender");lua_pushstring(L,e->text);lua_setfield(L,-2,"text");}
static void lua_push_game_event(lua_State* L,struct ToriRS_GameEvent const* e){lua_createtable(L,0,4);lua_pushstring(L,e->kind?e->kind:"");lua_setfield(L,-2,"kind");lua_pushstring(L,e->subject);lua_setfield(L,-2,"subject");lua_pushinteger(L,e->value);lua_setfield(L,-2,"value");lua_pushstring(L,e->text);lua_setfield(L,-2,"text");}
static void lua_push_key_event(lua_State* L,struct ToriRS_KeyEvent const* e){lua_createtable(L,0,3);lua_pushinteger(L,e->key);lua_setfield(L,-2,"key");lua_pushinteger(L,e->ch);lua_setfield(L,-2,"ch");lua_pushboolean(L,e->down);lua_setfield(L,-2,"down");}

static void
lua_push_menu_row(lua_State* L,struct ToriRS_MenuRow const* row)
{
    lua_createtable(L,0,8);lua_pushstring(L,row->text?row->text:"");lua_setfield(L,-2,"text");lua_pushinteger(L,row->action);lua_setfield(L,-2,"action");lua_pushinteger(L,row->pick_kind);lua_setfield(L,-2,"pick_kind");lua_pushinteger(L,row->npc_slot);lua_setfield(L,-2,"npc_slot");lua_pushinteger(L,row->player_pid);lua_setfield(L,-2,"player_pid");lua_pushinteger(L,row->target_id);lua_setfield(L,-2,"target_id");lua_pushinteger(L,row->component_id);lua_setfield(L,-2,"component_id");lua_pushinteger(L,row->slot);lua_setfield(L,-2,"slot");
}
static void lua_push_menu_build_event(lua_State* L,struct ToriRS_MenuBuildEvent const* e){lua_createtable(L,0,2);lua_pushboolean(L,e->hover_pass);lua_setfield(L,-2,"hover_pass");lua_createtable(L,e->row_count,0);for(int i=0;i<e->row_count;i++){lua_push_menu_row(L,&e->rows[i]);lua_rawseti(L,-2,i+1);}lua_setfield(L,-2,"rows");}
static void lua_push_menu_select_event(lua_State* L,struct ToriRS_MenuSelectEvent const* e){lua_createtable(L,0,6);lua_push_menu_row(L,&e->row);lua_setfield(L,-2,"row");lua_pushinteger(L,(lua_Integer)e->plugin_tag);lua_setfield(L,-2,"tag");lua_pushboolean(L,e->owned);lua_setfield(L,-2,"owned");lua_pushinteger(L,e->click_x);lua_setfield(L,-2,"x");lua_pushinteger(L,e->click_y);lua_setfield(L,-2,"y");}

static char const* lua_panel_action_name(int action){static char const* const names[]={"activate","toggle","text","pick","drag","scroll","key"};return action>=0&&action<7?names[action]:"unknown";}
static void lua_push_panel_action(lua_State* L,struct ToriRS_PanelActionEvent const* e){lua_createtable(L,0,10);lua_pushstring(L,e->id?e->id:"");lua_setfield(L,-2,"id");lua_pushstring(L,lua_panel_action_name(e->action));lua_setfield(L,-2,"action");lua_pushinteger(L,e->value);lua_setfield(L,-2,"value");lua_pushboolean(L,e->value!=0);lua_setfield(L,-2,"on");lua_pushstring(L,e->text?e->text:"");lua_setfield(L,-2,"text");lua_pushinteger(L,e->x);lua_setfield(L,-2,"x");lua_pushinteger(L,e->y);lua_setfield(L,-2,"y");lua_pushinteger(L,e->selection_generation);lua_setfield(L,-2,"generation");lua_pushinteger(L,e->widget_serial);lua_setfield(L,-2,"serial");lua_pushinteger(L,(lua_Integer)e->intent_sequence);lua_setfield(L,-2,"sequence");}
static void lua_push_panel_layout(lua_State* L,struct ToriRS_PanelLayoutEvent const* e){static char const* const size[]={"compact","medium","expanded"};lua_createtable(L,0,8);lua_pushinteger(L,e->width);lua_setfield(L,-2,"width");lua_pushinteger(L,e->height);lua_setfield(L,-2,"height");lua_pushinteger(L,e->scale_milli);lua_setfield(L,-2,"scale_milli");lua_pushnumber(L,(lua_Number)e->scale_milli/1000.0);lua_setfield(L,-2,"scale");lua_pushstring(L,e->size_class>=0&&e->size_class<3?size[e->size_class]:"unknown");lua_setfield(L,-2,"size_class");lua_pushboolean(L,e->visible);lua_setfield(L,-2,"visible");lua_pushboolean(L,e->game_visible);lua_setfield(L,-2,"game_visible");lua_pushinteger(L,e->selection_generation);lua_setfield(L,-2,"generation");}

static void
lua_cb_start(struct ToriRS_ApiV2* api, void* state)
{
    struct LuaScript* script = lua_script_for_api(api);
    (void)state;
    if( script && script->reload_failed )
    {
        /* Reload clears the previous run's error before restarting. Refuse
         * here, after that clear, so both ordinary scripts and selected frame
         * providers remain inert and show an actionable current error. */
        if( api->client && api->client->disable_self )
            api->client->disable_self(
                api, "Lua reload changed a static frame offer or failed; see the log");
        return;
    }
    if( lua_call_begin(script, api, LUA_ON_START) )
        (void)lua_call_end(script, LUA_ON_START, 1, false);
}

static void
lua_cb_stop(struct ToriRS_ApiV2* api, void* state)
{
    struct LuaScript* script = lua_script_for_api(api);
    (void)state;
    if( script && script->reload_failed ) return;
    if( lua_call_begin(script, api, LUA_ON_STOP) )
        (void)lua_call_end(script, LUA_ON_STOP, 1, false);
}
#define SIMPLE_EVENT_CB(fn,handler,type,push) static void fn(struct ToriRS_ApiV2*a,void*state,type const*e){(void)state;struct LuaScript*s=lua_script_for_api(a);if(lua_call_begin(s,a,handler)){push(s->L,e);lua_call_end(s,handler,2,false);}}
SIMPLE_EVENT_CB(lua_cb_frame,LUA_ON_FRAME_START,struct ToriRS_FrameEvent,lua_push_frame_event)
SIMPLE_EVENT_CB(lua_cb_logic,LUA_ON_LOGIC_TICK,struct ToriRS_TickEvent,lua_push_tick_event)
SIMPLE_EVENT_CB(lua_cb_server,LUA_ON_SERVER_TICK,struct ToriRS_TickEvent,lua_push_tick_event)
SIMPLE_EVENT_CB(lua_cb_world,LUA_ON_WORLD_LOADED,struct ToriRS_WorldLoadedEvent,lua_push_world_event)
SIMPLE_EVENT_CB(lua_cb_screen,LUA_ON_SCREEN_CHANGED,struct ToriRS_ScreenChangedEvent,lua_push_screen_event)
SIMPLE_EVENT_CB(lua_cb_asset,LUA_ON_ASSET,struct ToriRS_AssetEvent,lua_push_asset_event)
SIMPLE_EVENT_CB(lua_cb_chat,LUA_ON_CHAT_MESSAGE,struct ToriRS_ChatMessageEvent,lua_push_chat_event)
SIMPLE_EVENT_CB(lua_cb_game_event,LUA_ON_GAME_EVENT,struct ToriRS_GameEvent,lua_push_game_event)
#undef SIMPLE_EVENT_CB
#define SNAP_CB(fn,handler,type,push) static void fn(struct ToriRS_ApiV2*a,void*state,type const*e){(void)state;struct LuaScript*s=lua_script_for_api(a);if(lua_call_begin(s,a,handler)){push(s->L,e);lua_call_end(s,handler,2,false);}}
SNAP_CB(lua_cb_npc_spawn,LUA_ON_NPC_SPAWN,struct ToriRS_NpcSnapshot,lua_push_npc)
SNAP_CB(lua_cb_npc_retype,LUA_ON_NPC_RETYPE,struct ToriRS_NpcSnapshot,lua_push_npc)
SNAP_CB(lua_cb_npc_despawn,LUA_ON_NPC_DESPAWN,struct ToriRS_NpcSnapshot,lua_push_npc)
SNAP_CB(lua_cb_item_spawn,LUA_ON_ITEM_SPAWN,struct ToriRS_GroundItemSnapshot,lua_push_obj)
SNAP_CB(lua_cb_item_changed,LUA_ON_ITEM_CHANGED,struct ToriRS_GroundItemSnapshot,lua_push_obj)
SNAP_CB(lua_cb_item_despawn,LUA_ON_ITEM_DESPAWN,struct ToriRS_GroundItemSnapshot,lua_push_obj)
#undef SNAP_CB
static void lua_cb_config(struct ToriRS_ApiV2*a,void*state,char const*key){(void)state;struct LuaScript*s=lua_script_for_api(a);if(lua_call_begin(s,a,LUA_ON_CONFIG_CHANGED)){lua_pushstring(s->L,key?key:"");lua_call_end(s,LUA_ON_CONFIG_CHANGED,2,false);}}
static enum ToriRS_CallbackResult lua_cb_key(struct ToriRS_ApiV2*a,void*state,struct ToriRS_KeyEvent const*e){(void)state;struct LuaScript*s=lua_script_for_api(a);if(!lua_call_begin(s,a,LUA_ON_KEY))return TORIRS_CALLBACK_CONTINUE;lua_push_key_event(s->L,e);return lua_call_end(s,LUA_ON_KEY,2,true);}
static enum ToriRS_CallbackResult lua_cb_menu_build(struct ToriRS_ApiV2*a,void*state,struct ToriRS_MenuBuildEvent*e){(void)state;struct LuaScript*s=lua_script_for_api(a);if(!lua_call_begin(s,a,LUA_ON_MENU_BUILD))return TORIRS_CALLBACK_CONTINUE;s->cur_menu=e;lua_push_menu_build_event(s->L,e);return lua_call_end(s,LUA_ON_MENU_BUILD,2,true);}
static enum ToriRS_CallbackResult lua_cb_menu_select(struct ToriRS_ApiV2*a,void*state,struct ToriRS_MenuSelectEvent const*e){(void)state;struct LuaScript*s=lua_script_for_api(a);if(!lua_call_begin(s,a,LUA_ON_MENU_SELECT))return TORIRS_CALLBACK_CONTINUE;lua_push_menu_select_event(s->L,e);return lua_call_end(s,LUA_ON_MENU_SELECT,2,true);}
static void lua_callback_draw(struct ToriRS_ApiV2*a,enum LuaHandler h,struct ToriRS_DrawBuilder*d){struct LuaScript*s=lua_script_for_api(a);if(lua_call_begin(s,a,h)){s->cur_draw=d;lua_rawgeti(s->L,LUA_REGISTRYINDEX,s->draw_ref);lua_call_end(s,h,2,false);}}
static void lua_cb_draw_world(struct ToriRS_ApiV2*a,void*state,struct ToriRS_DrawBuilder*d){(void)state;lua_callback_draw(a,LUA_ON_DRAW_WORLD,d);}
static void lua_cb_draw_canvas(struct ToriRS_ApiV2*a,void*state,struct ToriRS_DrawBuilder*d){(void)state;lua_callback_draw(a,LUA_ON_DRAW_CANVAS,d);}
static void lua_cb_ui_build(struct ToriRS_ApiV2*a,void*state,struct ToriRS_PanelBuilder*p,int view){(void)state;struct LuaScript*s=lua_script_for_api(a);if(lua_call_begin(s,a,LUA_ON_UI_BUILD)){s->cur_panel=p;lua_rawgeti(s->L,LUA_REGISTRYINDEX,s->panel_builder_ref);lua_pushstring(s->L,view==TORIRS_PANEL_VIEW_SETTINGS?"settings":"page");lua_call_end(s,LUA_ON_UI_BUILD,3,false);}}
static void lua_cb_ui_action(struct ToriRS_ApiV2*a,void*state,struct ToriRS_PanelActionEvent const*e){(void)state;struct LuaScript*s=lua_script_for_api(a);if(lua_call_begin(s,a,LUA_ON_UI_ACTION)){lua_push_panel_action(s->L,e);lua_call_end(s,LUA_ON_UI_ACTION,2,false);}}
static void lua_cb_ui_draw(struct ToriRS_ApiV2*a,void*state,char const*node,struct ToriRS_DrawBuilder*d){(void)state;struct LuaScript*s=lua_script_for_api(a);if(lua_call_begin(s,a,LUA_ON_UI_DRAW)){s->cur_draw=d;lua_pushstring(s->L,node?node:"");lua_rawgeti(s->L,LUA_REGISTRYINDEX,s->draw_ref);lua_call_end(s,LUA_ON_UI_DRAW,3,false);}}
static void lua_cb_placement(struct ToriRS_ApiV2*a,void*state,uint32_t revision){(void)state;struct LuaScript*s=lua_script_for_api(a);if(lua_call_begin(s,a,LUA_ON_PLACEMENT_CHANGED)){lua_pushinteger(s->L,revision);lua_call_end(s,LUA_ON_PLACEMENT_CHANGED,2,false);}}
static void lua_cb_ui_node_draw(struct ToriRS_ApiV2*a,void*state,struct ToriRS_UiNodeRef node,struct ToriRS_DrawBuilder*d){(void)state;struct LuaScript*s=lua_script_for_api(a);if(lua_call_begin(s,a,LUA_ON_UI_NODE_DRAW)){s->cur_draw=d;lua_pushinteger(s->L,node.value);lua_rawgeti(s->L,LUA_REGISTRYINDEX,s->draw_ref);lua_call_end(s,LUA_ON_UI_NODE_DRAW,3,false);}}
static enum ToriRS_CallbackResult lua_cb_ui_node_action(struct ToriRS_ApiV2*a,void*state,struct ToriRS_UiNodeRef node,char const*action){(void)state;struct LuaScript*s=lua_script_for_api(a);if(!lua_call_begin(s,a,LUA_ON_UI_NODE_ACTION))return TORIRS_CALLBACK_CONTINUE;lua_pushinteger(s->L,node.value);lua_pushstring(s->L,action?action:"");return lua_call_end(s,LUA_ON_UI_NODE_ACTION,3,true);}
static enum ToriRS_CallbackResult lua_cb_canvas_action(struct ToriRS_ApiV2*a,void*state,uint32_t id,int operation,int x,int y){(void)state;struct LuaScript*s=lua_script_for_api(a);if(!lua_call_begin(s,a,LUA_ON_CANVAS_ACTION))return TORIRS_CALLBACK_CONTINUE;lua_createtable(s->L,0,4);lua_pushinteger(s->L,id);lua_setfield(s->L,-2,"id");lua_pushinteger(s->L,operation);lua_setfield(s->L,-2,"operation");lua_pushinteger(s->L,x);lua_setfield(s->L,-2,"x");lua_pushinteger(s->L,y);lua_setfield(s->L,-2,"y");return lua_call_end(s,LUA_ON_CANVAS_ACTION,2,true);}
static void lua_cb_ui_layout(struct ToriRS_ApiV2*a,void*state,struct ToriRS_PanelLayoutEvent const*e){(void)state;struct LuaScript*s=lua_script_for_api(a);if(lua_call_begin(s,a,LUA_ON_UI_LAYOUT)){lua_push_panel_layout(s->L,e);lua_call_end(s,LUA_ON_UI_LAYOUT,2,false);}}

static int
lua_frame_offer_index(struct LuaScript const* script, char const* offer_id)
{
    if( !offer_id ) return -1;
    for( int i = 0; i < script->frame_count; i++ )
        if( strcmp(script->frames[i].id, offer_id) == 0 ) return i;
    return -1;
}

static void
lua_push_frame_build_context(lua_State* L, struct ToriRS_FrameBuildContext const* context)
{
    lua_createtable(L, 0, 5);
    lua_pushstring(L, context->offer_id ? context->offer_id : "");
    lua_setfield(L, -2, "offer_id");
    lua_pushstring(L,
        context->canvas == TORIRS_FRAME_CANVAS_FIXED ? "fixed" : "window");
    lua_setfield(L, -2, "canvas");
    lua_push_rect(L, context->logical_canvas);
    lua_setfield(L, -2, "logical_canvas");
    if( context->available.value ) lua_pushinteger(L, context->available.value);
    else lua_pushnil(L);
    lua_setfield(L, -2, "available");
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, context->lane.game); lua_setfield(L, -2, "game");
    lua_pushinteger(L, context->lane.epoch); lua_setfield(L, -2, "epoch");
    lua_pushinteger(L, context->lane.revision); lua_setfield(L, -2, "revision");
    lua_setfield(L, -2, "lane");
}

static enum ToriRS_FrameBuildResult
lua_frame_build_result(lua_State* L, int index)
{
    char const* name;
    if( lua_isinteger(L, index) )
    {
        int result = (int)lua_tointeger(L, index);
        if( result >= TORIRS_FRAME_READY && result <= TORIRS_FRAME_ERROR )
            return (enum ToriRS_FrameBuildResult)result;
        return TORIRS_FRAME_ERROR;
    }
    name = lua_tostring(L, index);
    if( name && strcmp(name, "ready") == 0 ) return TORIRS_FRAME_READY;
    if( name && strcmp(name, "pending") == 0 ) return TORIRS_FRAME_PENDING;
    if( name && strcmp(name, "unsupported") == 0 ) return TORIRS_FRAME_UNSUPPORTED;
    return TORIRS_FRAME_ERROR;
}

static enum ToriRS_FrameBuildResult
lua_frame_offer_build(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_FrameBuilder* frame,
    struct ToriRS_FrameBuildContext const* context)
{
    struct LuaScript* script = lua_script_for_api(api);
    int offer;
    int status;
    enum ToriRS_FrameBuildResult result;
    (void)state;
    if( !script || !script->alive || script->reload_failed || !context )
        return TORIRS_FRAME_ERROR;
    offer = lua_frame_offer_index(script, context->offer_id);
    if( offer < 0 || script->frame_build_ref[offer] == LUA_NOREF )
        return TORIRS_FRAME_ERROR;

    if( !lua_callback_scope_push(script, api) )
        return TORIRS_FRAME_ERROR;
    script->cur_frame = frame;
    lua_rawgeti(script->L, LUA_REGISTRYINDEX, script->frame_build_ref[offer]);
    lua_rawgeti(script->L, LUA_REGISTRYINDEX, script->api_ref);
    lua_rawgeti(script->L, LUA_REGISTRYINDEX, script->frame_builder_ref);
    lua_push_frame_build_context(script->L, context);
    status = lua_callback_pcall(script, 3, 1);
    if( status != LUA_OK )
    {
        char error[128];
        char const* text = lua_tostring(script->L, -1);
        snprintf(error, sizeof(error), "%s", text ? text : "error");
        lua_pop(script->L, 1);
        lua_script_fault(script, api, "frame.build", error);
        return TORIRS_FRAME_ERROR;
    }
    result = lua_frame_build_result(script->L, -1);
    lua_pop(script->L, 1);
    if( lua_script_flush_disable(script, api) )
        return TORIRS_FRAME_ERROR;
    return result;
}

static int
lua_selected_frame_index(struct LuaScript* script, struct ToriRS_ApiV2* api)
{
    struct ToriRS_FrameSelection selection;
    char canonical[TORIRS_PLUGIN_FRAME_ID_MAX];
    memset(&selection, 0, sizeof(selection));
    selection.struct_size = sizeof(selection);
    api->frame.selection(api, &selection);
    for( int i = 0; i < script->frame_count; i++ )
    {
        snprintf(canonical, sizeof(canonical), "%s/%s", script->name, script->frames[i].id);
        if( strcmp(selection.active_id, canonical) == 0 ) return i;
    }
    return -1;
}

static void
lua_frame_offer_draw(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_DrawBuilder* draw)
{
    struct LuaScript* script = lua_script_for_api(api);
    int offer;
    int status;
    (void)state;
    if( !script || !script->alive || script->reload_failed ) return;
    offer = lua_selected_frame_index(script, api);
    if( offer < 0 || script->frame_draw_ref[offer] == LUA_NOREF ) return;
    if( !lua_callback_scope_push(script, api) )
        return;
    script->cur_draw = draw;
    lua_rawgeti(script->L, LUA_REGISTRYINDEX, script->frame_draw_ref[offer]);
    lua_rawgeti(script->L, LUA_REGISTRYINDEX, script->api_ref);
    lua_rawgeti(script->L, LUA_REGISTRYINDEX, script->draw_ref);
    status = lua_callback_pcall(script, 2, 0);
    if( status != LUA_OK )
    {
        char error[128];
        char const* text = lua_tostring(script->L, -1);
        snprintf(error, sizeof(error), "%s", text ? text : "error");
        lua_pop(script->L, 1);
        lua_script_fault(script, api, "frame.draw", error);
    }
    (void)lua_script_flush_disable(script, api);
}

/* ------------------------------------------------------- script lifecycle */

static void
lua_script_release(struct LuaScript* script)
{
    if( script->L )
        lua_close(script->L);
    script->L = NULL;
    script->alive = false;
    script->cur_api = NULL;
    script->cur_draw = NULL;
    script->cur_panel = NULL;
    script->cur_frame = NULL;
    script->cur_menu = NULL;
    script->callback_depth = 0;
    memset(script->callback_scopes, 0, sizeof(script->callback_scopes));
}

static bool
lua_read_config_item(struct LuaScript* script, lua_State* L, int table, int slot)
{
    struct ToriRS_ConfigItem* item = &script->config[slot];
    char* key = script->cfg_str[slot][0];
    char* label = script->cfg_str[slot][1];
    char* default_value = script->cfg_str[slot][2];
    char* choices = script->cfg_str[slot][3];
    char const* type;

    table = lua_absindex(L, table);
    memset(item, 0, sizeof(*item));

    lua_raw_getfield(L, table, "key");
    if( lua_type(L, -1) != LUA_TSTRING || !lua_tostring(L, -1)[0] )
    {
        lua_pop(L, 1);
        return false;
    }
    if( !lua_string_fits(lua_tostring(L, -1), PLUGIN_LUA_STR_MAX) ||
        !lua_config_key_valid(lua_tostring(L, -1)) )
    {
        lua_pop(L, 1);
        return false;
    }
    snprintf(key, PLUGIN_LUA_STR_MAX, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    item->key = key;

    lua_raw_getfield(L, table, "type");
    type = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "string";
    if( strcmp(type, "bool") == 0 ) item->type = TORIRS_CONFIG_BOOL;
    else if( strcmp(type, "int") == 0 ) item->type = TORIRS_CONFIG_INT;
    else if( strcmp(type, "color") == 0 || strcmp(type, "colour") == 0 ) item->type = TORIRS_CONFIG_COLOR;
    else if( strcmp(type, "enum") == 0 ) item->type = TORIRS_CONFIG_ENUM;
    else if( strcmp(type, "text") == 0 ) item->type = TORIRS_CONFIG_TEXT;
    else if( strcmp(type, "string") == 0 ) item->type = TORIRS_CONFIG_STRING;
    else
    {
        TORIRS_ERR("plugin: Lua config '%s' has unknown type '%s'\n", key, type);
        lua_pop(L, 1);
        return false;
    }
    lua_pop(L, 1);

    lua_raw_getfield(L, table, "label");
    if( lua_type(L, -1) == LUA_TSTRING )
    {
        if( !lua_string_fits(lua_tostring(L, -1), PLUGIN_LUA_STR_MAX) )
        {
            lua_pop(L, 1);
            return false;
        }
        snprintf(label, PLUGIN_LUA_STR_MAX, "%s", lua_tostring(L, -1));
        item->label = label;
    }
    lua_pop(L, 1);

    lua_raw_getfield(L, table, "default");
    if( lua_isboolean(L, -1) )
        snprintf(default_value, PLUGIN_LUA_STR_MAX, "%d", lua_toboolean(L, -1) ? 1 : 0);
    else if( lua_type(L, -1) == LUA_TSTRING || lua_isnumber(L, -1) )
    {
        if( !lua_string_fits(lua_tostring(L, -1), PLUGIN_LUA_STR_MAX) )
        {
            lua_pop(L, 1);
            return false;
        }
        snprintf(default_value, PLUGIN_LUA_STR_MAX, "%s", lua_tostring(L, -1));
    }
    else
        default_value[0] = '\0';
    lua_pop(L, 1);
    item->default_value = default_value;

    item->min = lua_table_int(L, table, "min", 0);
    item->max = lua_table_int(L, table, "max", 0);
    item->rows = lua_table_int(L, table, "rows", 0);
    lua_raw_getfield(L, table, "choices");
    if( lua_type(L, -1) == LUA_TSTRING )
    {
        if( !lua_string_fits(lua_tostring(L, -1), PLUGIN_LUA_STR_MAX) )
        {
            lua_pop(L, 1);
            return false;
        }
        snprintf(choices, PLUGIN_LUA_STR_MAX, "%s", lua_tostring(L, -1));
        item->choices = choices;
    }
    lua_pop(L, 1);
    return true;
}

static int
lua_ui_mode_from_value(lua_State* L, int index)
{
    char const* mode;
    if( lua_isinteger(L, index) )
        return lua_enum_integer(L, index, TORIRS_UI_MODIFY, TORIRS_UI_REPLACE_OR_PROVIDE,
            "UI contribution mode");
    mode = luaL_checkstring(L, index);
    if( strcmp(mode, "modify") == 0 ) return TORIRS_UI_MODIFY;
    if( strcmp(mode, "provide_if_missing") == 0 ) return TORIRS_UI_PROVIDE_IF_MISSING;
    if( strcmp(mode, "replace_or_provide") == 0 ) return TORIRS_UI_REPLACE_OR_PROVIDE;
    return luaL_error(L, "unknown UI contribution mode '%s'", mode);
}

static void
lua_copy_contribution_strings(
    struct LuaContributionStorage* strings,
    struct ToriRS_UiContribution* contribution)
{
    struct ToriRS_UiNode* node = &contribution->value;
    snprintf(strings->node, sizeof(strings->node), "%s", contribution->node);
    contribution->node = strings->node;
    if( node->parent )
    {
        snprintf(strings->parent, sizeof(strings->parent), "%s", node->parent);
        node->parent = strings->parent;
    }
    if( node->label )
    {
        snprintf(strings->label, sizeof(strings->label), "%s", node->label);
        node->label = strings->label;
    }
    if( node->action )
    {
        snprintf(strings->action, sizeof(strings->action), "%s", node->action);
        node->action = strings->action;
    }
    for( uint32_t i = 0; i < node->action_count; i++ )
    {
        snprintf(strings->actions[i], sizeof(strings->actions[i]), "%s", node->actions[i]);
        node->actions[i] = strings->actions[i];
    }
}

static bool
lua_read_contribution(struct LuaScript* script, lua_State* L, int table, int slot)
{
    struct ToriRS_UiContribution* contribution = &script->contributions[slot];
    char const* node;

    table = lua_absindex(L, table);
    memset(contribution, 0, sizeof(*contribution));
    contribution->struct_size = sizeof(*contribution);
    lua_raw_getfield(L, table, "node");
    node = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
    if( !node || !node[0] )
    {
        lua_pop(L, 1);
        return false;
    }
    if( !lua_string_fits(node, sizeof(script->contribution_strings[slot].node)) )
    {
        lua_pop(L, 1);
        return false;
    }
    contribution->node = node;
    lua_pop(L, 1);

    lua_raw_getfield(L, table, "mode");
    contribution->mode = lua_isnil(L, -1) ? TORIRS_UI_MODIFY : lua_ui_mode_from_value(L, -1);
    lua_pop(L, 1);
    lua_raw_getfield(L, table, "facets");
    contribution->facets = lua_isnil(L, -1) ? TORIRS_UI_FACET_ALL : lua_facets_from_arg(L, -1);
    lua_pop(L, 1);
    if( contribution->facets == 0 )
        return false;

    lua_raw_getfield(L, table, "value");
    if( !lua_istable(L, -1) )
    {
        lua_pop(L, 1);
        return false;
    }
    lua_ui_node_arg(L, -1, &contribution->value);
    lua_pop(L, 1);
    if( !lua_string_fits(contribution->value.parent,
            sizeof(script->contribution_strings[slot].parent)) ||
        !lua_string_fits(contribution->value.label,
            sizeof(script->contribution_strings[slot].label)) ||
        !lua_string_fits(contribution->value.action,
            sizeof(script->contribution_strings[slot].action)) )
        return false;
    for( uint32_t i = 0; i < contribution->value.action_count; i++ )
        if( !lua_string_fits(contribution->value.actions[i],
                sizeof(script->contribution_strings[slot].actions[i])) )
            return false;
    lua_copy_contribution_strings(
        &script->contribution_strings[slot], contribution);
    return true;
}

static bool
lua_read_frame_offer(struct LuaScript* script, lua_State* L, int table, int slot)
{
    struct ToriRS_FrameOffer* offer = &script->frames[slot];
    char const* id;
    char const* title;
    char const* canvas;

    table = lua_absindex(L, table);
    memset(offer, 0, sizeof(*offer));
    offer->struct_size = sizeof(*offer);
    id = lua_table_string(L, table, "id");
    title = lua_table_string(L, table, "title");
    canvas = lua_table_string(L, table, "canvas");
    if( !id || !id[0] || !title || !title[0] || !canvas ||
        !lua_string_fits(id, sizeof(script->frame_ids[slot])) ||
        !lua_string_fits(title, sizeof(script->frame_titles[slot])) )
        return false;
    snprintf(script->frame_ids[slot], sizeof(script->frame_ids[slot]), "%s", id);
    snprintf(script->frame_titles[slot], sizeof(script->frame_titles[slot]), "%s", title);
    offer->id = script->frame_ids[slot];
    offer->title = script->frame_titles[slot];
    if( strcmp(canvas, "fixed") == 0 ) offer->canvas = TORIRS_FRAME_CANVAS_FIXED;
    else if( strcmp(canvas, "window") == 0 ) offer->canvas = TORIRS_FRAME_CANVAS_WINDOW;
    else return false;
    offer->width = lua_table_int(L, table, "width", 0);
    offer->height = lua_table_int(L, table, "height", 0);
    offer->min_width = lua_table_int(L, table, "min_width", 0);
    offer->min_height = lua_table_int(L, table, "min_height", 0);

    lua_raw_getfield(L, table, "build");
    if( !lua_isfunction(L, -1) )
    {
        lua_pop(L, 1);
        return false;
    }
    script->frame_build_ref[slot] = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_raw_getfield(L, table, "draw");
    if( lua_isfunction(L, -1) )
        script->frame_draw_ref[slot] = luaL_ref(L, LUA_REGISTRYINDEX);
    else if( !lua_isnil(L, -1) )
    {
        lua_pop(L, 1);
        return false;
    }
    else
        lua_pop(L, 1);
    offer->build = lua_frame_offer_build;
    /* Always install the generic draw bridge so a source reload may add or
     * remove an offer's draw function without changing the host's static
     * callback shape. A missing Lua function is an O(1) return. */
    offer->draw = lua_frame_offer_draw;
    return true;
}

static void
lua_definition_callbacks(struct ToriRS_PluginCallbacks* callbacks)
{
    memset(callbacks, 0, sizeof(*callbacks));
    callbacks->struct_size = sizeof(*callbacks);
    callbacks->on_start = lua_cb_start;
    callbacks->on_stop = lua_cb_stop;
    callbacks->on_frame_start = lua_cb_frame;
    callbacks->on_logic_tick = lua_cb_logic;
    callbacks->on_server_tick = lua_cb_server;
    callbacks->on_world_loaded = lua_cb_world;
    callbacks->on_screen_changed = lua_cb_screen;
    callbacks->on_npc_spawn = lua_cb_npc_spawn;
    callbacks->on_npc_retype = lua_cb_npc_retype;
    callbacks->on_npc_despawn = lua_cb_npc_despawn;
    callbacks->on_item_spawn = lua_cb_item_spawn;
    callbacks->on_item_changed = lua_cb_item_changed;
    callbacks->on_item_despawn = lua_cb_item_despawn;
    callbacks->on_config_changed = lua_cb_config;
    callbacks->on_asset = lua_cb_asset;
    callbacks->on_chat_message = lua_cb_chat;
    callbacks->on_game_event = lua_cb_game_event;
    callbacks->on_key = lua_cb_key;
    callbacks->on_menu_build = lua_cb_menu_build;
    callbacks->on_menu_select = lua_cb_menu_select;
    callbacks->on_draw_world = lua_cb_draw_world;
    callbacks->on_draw_canvas = lua_cb_draw_canvas;
    callbacks->on_ui_build = lua_cb_ui_build;
    callbacks->on_ui_action = lua_cb_ui_action;
    callbacks->on_ui_draw = lua_cb_ui_draw;
    callbacks->on_placement_changed = lua_cb_placement;
    callbacks->on_ui_node_draw = lua_cb_ui_node_draw;
    callbacks->on_ui_node_action = lua_cb_ui_node_action;
    callbacks->on_canvas_action = lua_cb_canvas_action;
    callbacks->on_ui_layout = lua_cb_ui_layout;
}

/* Runs only through lua_pcall. Descriptor tables are inert data (all reads
 * below are raw), but conversion errors and the runtime's own registry/table
 * allocations still need the same protected memory boundary as user code. */
static int
lua_parse_definition(lua_State* L)
{
    void* allocator_user = NULL;
    struct LuaScript* script;
    char const* id;

    (void)lua_getallocf(L, &allocator_user);
    script = allocator_user;
    assert(script);
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_raw_getfield(L, 1, "id");
    id = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
    if( !id || !id[0] )
        return luaL_error(L, "plugin table must declare a non-empty V2 id");
    if( strcmp(id, script->name) != 0 )
        return luaL_error(L, "plugin id '%s' does not match manifest id '%s'", id, script->name);
    lua_pop(L, 1);

    lua_raw_getfield(L, 1, "title");
    if( lua_type(L, -1) == LUA_TSTRING &&
        !lua_string_fits(lua_tostring(L, -1), sizeof(script->title)) )
        return luaL_error(L, "plugin title is too long");
    snprintf(script->title, sizeof(script->title), "%s",
        lua_type(L, -1) == LUA_TSTRING && lua_tostring(L, -1)[0]
            ? lua_tostring(L, -1) : script->name);
    lua_pop(L, 1);
    lua_raw_getfield(L, 1, "version");
    if( lua_type(L, -1) == LUA_TSTRING &&
        !lua_string_fits(lua_tostring(L, -1), sizeof(script->version)) )
        return luaL_error(L, "plugin version is too long");
    snprintf(script->version, sizeof(script->version), "%s",
        lua_type(L, -1) == LUA_TSTRING && lua_tostring(L, -1)[0]
            ? lua_tostring(L, -1) : "0");
    lua_pop(L, 1);

    lua_raw_getfield(L, 1, "config");
    if( lua_istable(L, -1) )
    {
        int const count = (int)lua_rawlen(L, -1);
        if( count > PLUGIN_LUA_MAX_CONFIG )
            return luaL_error(L, "declares %d config items; limit is %d",
                count, PLUGIN_LUA_MAX_CONFIG);
        for( int i = 0; i < count; i++ )
        {
            lua_rawgeti(L, -1, i + 1);
            if( !lua_istable(L, -1) || !lua_read_config_item(script, L, -1, i) )
                return luaL_error(L, "invalid config item at %d", i + 1);
            lua_pop(L, 1);
            script->config_count++;
        }
    }
    else if( !lua_isnil(L, -1) )
        return luaL_error(L, "config must be an array");
    lua_pop(L, 1);

    lua_raw_getfield(L, 1, "frames");
    if( lua_istable(L, -1) )
    {
        int const count = (int)lua_rawlen(L, -1);
        if( count > PLUGIN_LUA_MAX_FRAMES )
            return luaL_error(L, "declares %d frame offers; limit is %d",
                count, PLUGIN_LUA_MAX_FRAMES);
        for( int i = 0; i < count; i++ )
        {
            lua_rawgeti(L, -1, i + 1);
            if( !lua_istable(L, -1) || !lua_read_frame_offer(script, L, -1, i) )
                return luaL_error(L, "invalid frame offer at %d", i + 1);
            lua_pop(L, 1);
            script->frame_count++;
        }
    }
    else if( !lua_isnil(L, -1) )
        return luaL_error(L, "frames must be an array");
    lua_pop(L, 1);

    lua_raw_getfield(L, 1, "ui_contributions");
    if( lua_istable(L, -1) )
    {
        int const count = (int)lua_rawlen(L, -1);
        if( count > PLUGIN_LUA_MAX_CONTRIBUTIONS )
            return luaL_error(L, "declares %d UI contributions; limit is %d",
                count, PLUGIN_LUA_MAX_CONTRIBUTIONS);
        for( int i = 0; i < count; i++ )
        {
            lua_rawgeti(L, -1, i + 1);
            if( !lua_istable(L, -1) || !lua_read_contribution(script, L, -1, i) )
                return luaL_error(L, "invalid UI contribution at %d", i + 1);
            lua_pop(L, 1);
            script->contribution_count++;
        }
    }
    else if( !lua_isnil(L, -1) )
        return luaL_error(L, "ui_contributions must be an array");
    lua_pop(L, 1);

    for( int i = 0; i < LUA_HANDLER_COUNT; i++ )
    {
        lua_raw_getfield(L, 1, LUA_HANDLER_NAMES[i]);
        if( lua_isfunction(L, -1) )
            script->handler_ref[i] = luaL_ref(L, LUA_REGISTRYINDEX);
        else if( !lua_isnil(L, -1) )
            return luaL_error(L, "%s must be a function", LUA_HANDLER_NAMES[i]);
        else
            lua_pop(L, 1);
    }
    lua_pushvalue(L, 1);
    script->table_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    memset(&script->def, 0, sizeof(script->def));
    script->config_schema.struct_size = sizeof(script->config_schema);
    script->config_schema.items = script->config;
    script->def.struct_size = sizeof(script->def);
    script->def.id = script->name;
    script->def.title = script->title;
    script->def.version = script->version;
    /* These addresses are copied into the host once. Keep the stable empty
     * schema/array published so a reload may add its first item/contribution. */
    script->def.config = &script->config_schema;
    script->def.frames = script->frame_count ? script->frames : NULL;
    script->def.ui_contributions = script->contributions;
    script->def.event_priority = lua_table_int(L, 1, "event_priority", 0);
    script->def.draw_order = lua_table_int(L, 1, "draw_order", 0);
    lua_definition_callbacks(&script->def.callbacks);
    return 0;
}

static bool
lua_script_build(
    struct LuaScript* script,
    char const* fallback_name,
    char const* source,
    int source_len)
{
    lua_State* L;
    char chunk[TORIRS_PLUGIN_NAME_MAX + 2];
    int parser_ref;

    assert(script && fallback_name && source && source_len > 0);
    script->table_ref = LUA_NOREF;
    script->api_ref = LUA_NOREF;
    script->draw_ref = LUA_NOREF;
    script->panel_builder_ref = LUA_NOREF;
    script->frame_builder_ref = LUA_NOREF;
    for( int i = 0; i < LUA_HANDLER_COUNT; i++ )
        script->handler_ref[i] = LUA_NOREF;
    for( int i = 0; i < PLUGIN_LUA_MAX_FRAMES; i++ )
    {
        script->frame_build_ref[i] = LUA_NOREF;
        script->frame_draw_ref[i] = LUA_NOREF;
    }
    script->config_count = 0;
    script->contribution_count = 0;
    script->frame_count = 0;
    memset(script->config, 0, sizeof(script->config));
    memset(script->contributions, 0, sizeof(script->contributions));
    memset(script->contribution_strings, 0, sizeof(script->contribution_strings));
    memset(script->frames, 0, sizeof(script->frames));
    memset(script->frame_ids, 0, sizeof(script->frame_ids));
    memset(script->frame_titles, 0, sizeof(script->frame_titles));
    script->mem_used = 0;
    script->memory_limit = PLUGIN_LUA_HARD_MEM_CAP_BYTES;
    if( script->plugin_index < 0 )
        snprintf(script->name, sizeof(script->name), "%s", fallback_name);
    else if( strcmp(script->name, fallback_name) != 0 )
        return false;

    L = lua_newstate(lua_script_alloc, script, 0);
    if( !L )
    {
        TORIRS_ERR("plugin: Lua state for '%s' would not start\n", fallback_name);
        return false;
    }
    script->L = L;
    script->alive = true;
    lua_open_sandbox_libs(L);
    /* Reserve all runtime closures before user code can consume its arena,
     * and create the parser closure while the state is still empty. */
    lua_build_api_table(script);
    lua_pushcfunction(L, lua_parse_definition);
    parser_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    snprintf(chunk, sizeof(chunk), "@%s", fallback_name);
    script->memory_limit = PLUGIN_LUA_MEM_CAP_BYTES;
    lua_arm_budget(script);
    if( luaL_loadbuffer(L, source, (size_t)source_len, chunk) != LUA_OK ||
        lua_pcall(L, 0, 1, 0) != LUA_OK )
    {
        char const* error = lua_tostring(L, -1);
        TORIRS_ERR("plugin: Lua script '%s' failed to load: %s\n", fallback_name, error ? error : "?");
        lua_disarm_budget(script);
        lua_script_release(script);
        return false;
    }
    lua_disarm_budget(script);
    script->memory_limit = PLUGIN_LUA_HARD_MEM_CAP_BYTES;
    if( !lua_istable(L, -1) )
    {
        TORIRS_ERR("plugin: Lua script '%s' must return a plugin table\n", fallback_name);
        lua_script_release(script);
        return false;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, parser_ref);
    lua_insert(L, -2);
    lua_arm_budget(script);
    if( lua_pcall(L, 1, 0, 0) != LUA_OK )
    {
        char const* error = lua_tostring(L, -1);
        TORIRS_ERR("plugin: Lua script '%s' has an invalid V2 definition: %s\n",
            fallback_name, error ? error : "?");
        lua_disarm_budget(script);
        lua_script_release(script);
        return false;
    }
    lua_disarm_budget(script);
    luaL_unref(L, LUA_REGISTRYINDEX, parser_ref);
    return true;

}

struct LuaFrameSignature
{
    char id[TORIRS_PLUGIN_FRAME_ID_MAX];
    char title[TORIRS_PLUGIN_TITLE_MAX];
    int canvas;
    int width;
    int height;
    int min_width;
    int min_height;
};

static void
lua_frame_signatures(
    struct LuaScript const* script,
    struct LuaFrameSignature out[PLUGIN_LUA_MAX_FRAMES])
{
    memset(out, 0, sizeof(*out) * PLUGIN_LUA_MAX_FRAMES);
    for( int i = 0; i < script->frame_count; i++ )
    {
        snprintf(out[i].id, sizeof(out[i].id), "%s", script->frames[i].id);
        snprintf(out[i].title, sizeof(out[i].title), "%s", script->frames[i].title);
        out[i].canvas = script->frames[i].canvas;
        out[i].width = script->frames[i].width;
        out[i].height = script->frames[i].height;
        out[i].min_width = script->frames[i].min_width;
        out[i].min_height = script->frames[i].min_height;
    }
}

static bool
lua_frame_signatures_equal(
    struct LuaFrameSignature const before[PLUGIN_LUA_MAX_FRAMES],
    int before_count,
    struct LuaScript const* script)
{
    if( before_count != script->frame_count ) return false;
    for( int i = 0; i < before_count; i++ )
    {
        struct ToriRS_FrameOffer const* after = &script->frames[i];
        if( strcmp(before[i].id, after->id) != 0 ||
            strcmp(before[i].title, after->title) != 0 ||
            before[i].canvas != after->canvas ||
            before[i].width != after->width || before[i].height != after->height ||
            before[i].min_width != after->min_width ||
            before[i].min_height != after->min_height )
            return false;
    }
    return true;
}

static void
lua_frame_signatures_restore(
    struct LuaScript* script,
    struct LuaFrameSignature const saved[PLUGIN_LUA_MAX_FRAMES],
    int count)
{
    memset(script->frames, 0, sizeof(script->frames));
    memset(script->frame_ids, 0, sizeof(script->frame_ids));
    memset(script->frame_titles, 0, sizeof(script->frame_titles));
    script->frame_count = count;
    for( int i = 0; i < count; i++ )
    {
        struct ToriRS_FrameOffer* offer = &script->frames[i];
        snprintf(script->frame_ids[i], sizeof(script->frame_ids[i]), "%s", saved[i].id);
        snprintf(script->frame_titles[i], sizeof(script->frame_titles[i]), "%s", saved[i].title);
        offer->struct_size = sizeof(*offer);
        offer->id = script->frame_ids[i];
        offer->title = script->frame_titles[i];
        offer->canvas = saved[i].canvas;
        offer->width = saved[i].width;
        offer->height = saved[i].height;
        offer->min_width = saved[i].min_width;
        offer->min_height = saved[i].min_height;
        offer->build = lua_frame_offer_build;
        offer->draw = lua_frame_offer_draw;
    }
}

static void
lua_script_reload(struct ToriRS_PluginHost* host, int plugin_index, void* userdata)
{
    struct LuaScript* script = userdata;
    char stable_name[TORIRS_PLUGIN_NAME_MAX];
    struct LuaFrameSignature frames_before[PLUGIN_LUA_MAX_FRAMES];
    int frame_count_before;
    assert(script && host == script->host && plugin_index == script->plugin_index);
    (void)host;
    (void)plugin_index;
    frame_count_before = script->frame_count;
    snprintf(stable_name, sizeof(stable_name), "%s", script->name);
    lua_frame_signatures(script, frames_before);
    lua_script_release(script);
    if( !lua_script_build(script, stable_name, script->source, script->source_len) )
    {
        lua_frame_signatures_restore(script, frames_before, frame_count_before);
        memset(script->config, 0, sizeof(script->config));
        memset(script->contributions, 0, sizeof(script->contributions));
        script->config_count = 0;
        script->contribution_count = 0;
        script->reload_failed = true;
        return;
    }
    if( !lua_frame_signatures_equal(frames_before, frame_count_before, script) )
    {
        TORIRS_ERR(
            "plugin: Lua reload for '%s' changed static frame ids, titles, canvas, "
            "or constraints; restart the client to publish a new catalogue\n",
            script->name);
        /* The host catalogue is immutable after registration. Keep the new VM
         * only so the runtime host can close it normally, but do not start or
         * dispatch it. on_start uses disable_self after Reload clears the old
         * error, which also marks a frame provider unavailable. */
        script->reload_failed = true;
        script->contributions[0].node = NULL;
        lua_frame_signatures_restore(script, frames_before, frame_count_before);
        return;
    }
    script->reload_failed = false;
}

int
PluginLua_AddScript(
    struct ToriRS_PluginHost* host,
    char const* name,
    char const* source,
    int source_len)
{
    struct LuaScript* script;
    int index;

    if( !host || !name || !name[0] || strlen(name) >= TORIRS_PLUGIN_NAME_MAX ||
        !source || source_len <= 0 )
    {
        TORIRS_ERR("plugin: invalid or empty Lua source refused\n");
        return -1;
    }
    if( g_script_count >= PLUGIN_LUA_MAX_SCRIPTS )
    {
        TORIRS_ERR("plugin: Lua script table full, refusing '%s'\n", name);
        return -1;
    }
    script = &g_scripts[g_script_count];
    memset(script, 0, sizeof(*script));
    script->host = host;
    script->plugin_index = -1;
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
    index = PluginHost_RegisterV2(host, &script->def);
    if( index < 0 )
    {
        lua_script_release(script);
        free(script->source);
        script->source = NULL;
        return -1;
    }
    script->plugin_index = index;
    PluginHost_SetReloadHandler(host, index, lua_script_reload, script);
    g_script_count++;
    lua_script_lookup_insert(g_script_count - 1);
    return index;
}

int
PluginLua_KeyCodeFromName(char const* name)
{
    static struct { char const* name; int code; } const KEYS[] = {
        {"shift",TORIRS_KEY_SHIFT},{"ctrl",TORIRS_KEY_CTRL},
        {"space",TORIRS_KEY_SPACE},{"tab",TORIRS_KEY_TAB},
        {"escape",TORIRS_KEY_ESCAPE},
    };
    if( !name ) return -1;
    for( size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++ )
        if( strcmp(KEYS[i].name, name) == 0 ) return KEYS[i].code;
    return -1;
}

#if defined(TORIRS_PLUGIN_LUA_TESTING)
bool
PluginLua_TestReplaceSource(int plugin_index, char const* source, int source_len)
{
    if( !source || source_len <= 0 ) return false;
    for( int i = 0; i < g_script_count; i++ )
    {
        struct LuaScript* script = &g_scripts[i];
        if( script->plugin_index != plugin_index ) continue;
        char* replacement = realloc(script->source, (size_t)source_len);
        if( !replacement ) return false;
        memcpy(replacement, source, (size_t)source_len);
        script->source = replacement;
        script->source_len = source_len;
        return true;
    }
    return false;
}
#endif

void
PluginLua_Shutdown(void)
{
    for( int i = 0; i < g_script_count; i++ )
    {
        lua_script_release(&g_scripts[i]);
        free(g_scripts[i].source);
        g_scripts[i].source = NULL;
        g_scripts[i].source_len = 0;
    }
    g_script_count = 0;
    memset(g_script_lookup, 0, sizeof(g_script_lookup));
}

static void
lua_runtime_stop(struct ToriRS_ApiV2* api, void* state)
{
    (void)api;
    (void)state;
    PluginLua_Shutdown();
}

struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_LUA = {
    .struct_size = sizeof(struct ToriRS_PluginDefV2),
    .id = "lua",
    .title = "Lua Scripting",
    .version = "2.0.0",
    .flags = TORIRS_PLUGIN_V2_RUNTIME_HOST | TORIRS_PLUGIN_V2_HIDDEN,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_stop = lua_runtime_stop,
    },
};
