#include "platform_x_lua.h"

#include "../libtorirs_internal.h"
#include "../scripting/libtorirs_scriptapi.h"
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "platform_x/cachelib_platform.h"

// clang-format off
#include "platform_x_luahost.u.c"
// clang-format on

#include "platform_x_lua_internal.h"

#include <stdlib.h>

struct LibToriPlatformX_Lua*
LibToriPlatformX_LuaNew(struct LibToriRS_Instance* instance)
{
    struct LibToriPlatformX_Lua* lua = malloc(sizeof(struct LibToriPlatformX_Lua));
    if( !lua )
        return NULL;

    lua->instance = instance;

    lua->L = luaL_newstate();
    if( !lua->L )
    {
        free(lua);
        return NULL;
    }

    luaL_openlibs(lua->L);

    LibToriPlatformX_LuaHost_BindToPlatform(lua->L, lua);

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

int
LibToriPlatformX_LuaCacheIOInit(
    struct LibToriPlatformX_Lua* lua,
    int mode,
    char const* directory)
{
    if( !lua )
        return LIBTORI_PLATFORM_X_LUA_ERROR;

    lua->cache = cachelib_new(mode);
    if( !lua->cache )
        return LIBTORI_PLATFORM_X_LUA_ERROR;

    if( cachelib_platform_init(lua->cache, directory) != 1 )
        return LIBTORI_PLATFORM_X_LUA_ERROR;

    return LIBTORI_PLATFORM_X_LUA_OK;
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

    int rc;
    if( script->is_inline )
    {
        rc = luaL_loadstring(lua->L_coro, script->name);
        if( rc != LUA_OK )
        {
            printf("Lua error: %s\n", lua_tostring(lua->L_coro, -1));
            return LIBTORI_PLATFORM_X_LUA_ERROR;
        }
    }
    else
    {
        rc = luaL_loadfile(lua->L_coro, GetScriptPath(script->name));
        if( rc != LUA_OK )
        {
            printf("Lua error: %s\n", lua_tostring(lua->L_coro, -1));
            return LIBTORI_PLATFORM_X_LUA_ERROR;
        }
    }

    for( int i = 0; i < script->args.count; i++ )
    {
        lua_pushlightuserdata(lua->L_coro, &script->args.values[i]);
    }

    int nres = 0;
    rc = lua_resume(lua->L_coro, lua->L, script->args.count, &nres);
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

    int nres = 0;
    int rc = lua_resume(lua->L_coro, lua->L, 0, &nres);
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
        printf("Lua error: %s\n", lua_tostring(lua->L_coro, -1));
        lua->L_coro = NULL;
        return LIBTORI_PLATFORM_X_LUA_ERROR;
    }
    }
}

int
LibToriPlatformX_LuaReadYieldCount(
    struct LibToriPlatformX_Lua* lua,
    struct LibToriRS_Instance* instance)
{
    if( !lua )
        return LIBTORI_PLATFORM_X_LUA_ERROR;

    return lua_gettop(lua->L_coro);
}

struct LibToriRS_ScriptValue*
LibToriPlatformX_LuaReadYieldValue(
    struct LibToriPlatformX_Lua* lua,
    struct LibToriRS_Instance* instance,
    int index)
{
    if( !lua )
        return NULL;
    return (struct LibToriRS_ScriptValue*)lua_touserdata(lua->L_coro, index);
}