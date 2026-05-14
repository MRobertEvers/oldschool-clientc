#include "platform_x_lua.h"

#include "../libtorirs_internal.h"
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"

// clang-format off
#include "platform_x_luahost.u.c"
// clang-format on

#include <stdlib.h>

struct LibToriPlatformX_Lua
{
    lua_State* L;
    lua_State* L_coro;
};

struct LibToriPlatformX_Lua*
LibToriPlatformX_LuaNew(void)
{
    struct LibToriPlatformX_Lua* lua = malloc(sizeof(struct LibToriPlatformX_Lua));
    if( !lua )
        return NULL;

    lua->L = luaL_newstate();
    if( !lua->L )
    {
        free(lua);
        return NULL;
    }
    luaL_openlibs(lua->L);
    lua_pushcfunction(lua->L, LibToriPlatformX_LuaHost_Print);
    lua_setglobal(lua->L, "print_chost");

    lua_newtable(lua->L);
    lua_pushcfunction(lua->L, LibToriPlatformX_LuaHost_ScriptValueAsLuaInt);
    lua_setfield(lua->L, -2, "ScriptValueAsLuaInt");
    lua_pushcfunction(lua->L, LibToriPlatformX_LuaHost_ScriptValueAsLuaAny);
    lua_setfield(lua->L, -2, "ScriptValueAsLuaAny");
    lua_pushcfunction(lua->L, LibToriPlatformX_LuaHost_Print);
    lua_setfield(lua->L, -2, "Print");
    lua_setglobal(lua->L, "Host");

    return lua;
}

void
LibToriPlatformX_LuaFree(struct LibToriPlatformX_Lua* lua)
{
    if( lua )
        return;

    free(lua);
    lua = NULL;
}

#define LUA_SCRIPTS_DIR "../../revs/scripts"
static char g_script_path[256];

static char*
GetScriptPath(const char* script_name)
{
    snprintf(g_script_path, sizeof(g_script_path), "%s/%s", LUA_SCRIPTS_DIR, script_name);
    return g_script_path;
}

int
LibToriPlatformX_LuaRun(
    struct LibToriPlatformX_Lua* lua,
    struct LibToriRS_Instance* instance)
{
    if( !lua )
        return LIBTORI_PLATFORM_X_LUA_ERROR;

    struct LibToriRS_Script* script = LibToriRS_ScriptQueuePop(instance->script_queue);
    if( !script )
        return LIBTORI_PLATFORM_X_LUA_ERROR;

    lua->L_coro = lua_newthread(lua->L);

    luaL_loadfile(lua->L_coro, GetScriptPath(script->name));

    int nres = 0;
    int rc = lua_resume(lua->L_coro, lua->L, 0, &nres);
    switch( rc )
    {
    case LUA_OK:
        return LIBTORI_PLATFORM_X_LUA_OK;
    case LUA_YIELD:
        return LIBTORI_PLATFORM_X_LUA_YIELDED;
    default:
        printf("Lua error: %s\n", lua_tostring(lua->L_coro, -1));
        return LIBTORI_PLATFORM_X_LUA_ERROR;
    }
}

int
LibToriPlatformX_LuaContinue(
    struct LibToriPlatformX_Lua* lua,
    struct LibToriRS_Instance* instance)
{
    if( !lua )
        return LIBTORI_PLATFORM_X_LUA_ERROR;

    lua_pushinteger(lua->L_coro, 99);

    int nres = 0;
    int rc = lua_resume(lua->L_coro, lua->L, 1, &nres);
    switch( rc )
    {
    case LUA_OK:
    {
        lua->L_coro = NULL;
        return LIBTORI_PLATFORM_X_LUA_OK;
    }
    case LUA_YIELD:
        return LIBTORI_PLATFORM_X_LUA_YIELDED;
    default:
    {
        lua->L_coro = NULL;
        return LIBTORI_PLATFORM_X_LUA_ERROR;
    }
    }
}