#ifndef PLATFORM_X_LUAHOST_H
#define PLATFORM_X_LUAHOST_H

#include "../libtorirs.h"
#include "../scripting/libtorirs_scripting.h"
#include "3rd/lua/lua.h"
#include "platform_x_lua_internal.h"

#include <assert.h>

int
LibToriPlatformX_LuaHost_Print(lua_State* L)
{
    const char* str = lua_tostring(L, 1);
    printf("%s\n", str);
    return 0;
}

/**
 * upvalue(1): LibToriPlatformX_Lua
 * Platform.GetIOQueue
 *   returns: io_queue: LibToriRS_IOQueue
 */
int
LibToriPlatformX_LuaHost_Platform_GetIOQueue(lua_State* L)
{
    struct LibToriPlatformX_Lua* lua =
        (struct LibToriPlatformX_Lua*)lua_touserdata(L, lua_upvalueindex(1));
    if( !lua )
        return 0;

    struct LibToriRS_IOQueue* io_queue = LibToriRS_GetIOQueue(lua->instance);
    lua_pushlightuserdata(L, io_queue);
    return 1;
}

/**
 * upvalue(1): LibToriPlatformX_Lua
 * Platform.LoadIO
 *   arg(1): io_queue: LibToriRS_IOQueue
 */
int
LibToriPlatformX_LuaHost_Platform_LoadIO(lua_State* L)
{
    struct LibToriPlatformX_Lua* lua =
        (struct LibToriPlatformX_Lua*)lua_touserdata(L, lua_upvalueindex(1));
    if( !lua )
        return 0;

    struct LibToriRS_IOQueue* io_queue = (struct LibToriRS_IOQueue*)lua_touserdata(L, 1);

    for( int i = 0; i < io_queue->count; i++ )
    {
        struct LibToriRS_IOQueueItem* io_item = &io_queue->items[i];
        void* data = cachelib_platform_load_io(lua->cache, io_item);
        if( !data )
            return 0;
        io_item->data = data;
        io_item->is_resolved = true;
    }

    return 0;
}

int
LibToriPlatformX_LuaHost_Game_Dat1_ConfigFileFetch(lua_State* L)
{
    struct LibToriRS_Instance* instance =
        (struct LibToriRS_Instance*)lua_touserdata(L, lua_upvalueindex(1));
    if( !instance )
        return 0;

    struct LibToriRS_IOQueue* io_queue = (struct LibToriRS_IOQueue*)lua_touserdata(L, 1);

    LibToriRS_ScriptAPI_Dat1_ConfigFileFetch(instance, io_queue);

    return 0;
}

int
LibToriPlatformX_LuaHost_Game_Dat1_ConfigFileLoad(lua_State* L)
{
    struct LibToriRS_Instance* instance =
        (struct LibToriRS_Instance*)lua_touserdata(L, lua_upvalueindex(1));
    if( !instance )
        return 0;

    struct LibToriRS_IOQueue* io_queue = (struct LibToriRS_IOQueue*)lua_touserdata(L, 1);

    LibToriRS_ScriptAPI_Dat1_ConfigFileLoad(instance, io_queue);

    return 0;
}

int
LibToriPlatformX_LuaHost_Game_Dat1_ModelFetch(lua_State* L)
{
    struct LibToriRS_Instance* instance =
        (struct LibToriRS_Instance*)lua_touserdata(L, lua_upvalueindex(1));
    if( !instance )
        return 0;

    struct LibToriRS_IOQueue* io_queue = (struct LibToriRS_IOQueue*)lua_touserdata(L, 1);
    int model_id = luaL_checkinteger(L, 2);

    LibToriRS_ScriptAPI_Dat1_ModelFetch(instance, model_id, io_queue);

    return 0;
}

int
LibToriPlatformX_LuaHost_Game_Dat1_ModelLoad(lua_State* L)
{
    struct LibToriRS_Instance* instance =
        (struct LibToriRS_Instance*)lua_touserdata(L, lua_upvalueindex(1));
    if( !instance )
        return 0;

    struct LibToriRS_IOQueue* io_queue = (struct LibToriRS_IOQueue*)lua_touserdata(L, 1);
    LibToriRS_ScriptAPI_Dat1_ModelLoad(instance, io_queue);

    return 0;
}

int
LibToriPlatformX_LuaHost_Game_Dat1_SubmitGameCacheModel(lua_State* L)
{
    struct LibToriRS_Instance* instance =
        (struct LibToriRS_Instance*)lua_touserdata(L, lua_upvalueindex(1));
    if( !instance )
        return 0;

    int model_id = luaL_checkinteger(L, 1);

    LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel(instance, model_id);

    return 0;
}

int
LibToriPlatformX_LuaHost_Game_ModelViewer_Init(lua_State* L)
{
    struct LibToriRS_Instance* instance =
        (struct LibToriRS_Instance*)lua_touserdata(L, lua_upvalueindex(1));
    if( !instance )
        return 0;

    LibToriRS_ScriptAPI_Game_ModelViewer_Init(instance);

    return 0;
}

int
LibToriPlatformX_LuaHost_Game_ModelViewer_RenderModel(lua_State* L)
{
    struct LibToriRS_Instance* instance =
        (struct LibToriRS_Instance*)lua_touserdata(L, lua_upvalueindex(1));
    if( !instance )
        return 0;

    int model_id = luaL_checkinteger(L, 1);

    LibToriRS_ScriptAPI_Game_ModelViewer_RenderModel(instance, model_id);

    return 0;
}

static inline void
lua_bind_function_to_platform(
    lua_State* L,
    struct LibToriPlatformX_Lua* platform_x_lua,
    const char* name,
    void (*fn)(lua_State* L))
{
    lua_pushlightuserdata(L, platform_x_lua);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

void
LibToriPlatformX_LuaHost_BindToPlatform(
    lua_State* L,
    struct LibToriPlatformX_Lua* lua)
{
    lua_newtable(L);
    lua_bind_function_to_platform(
        L, lua, "GetIOQueue", LibToriPlatformX_LuaHost_Platform_GetIOQueue);
    lua_setglobal(L, "Platform");

    lua_newtable(L);
    lua_bind_function_to_platform(
        L, lua, "Dat1_ConfigFileFetch", LibToriPlatformX_LuaHost_Game_Dat1_ConfigFileFetch);
    lua_bind_function_to_platform(
        L, lua, "Dat1_ConfigFileLoad", LibToriPlatformX_LuaHost_Game_Dat1_ConfigFileLoad);
    lua_bind_function_to_platform(
        L, lua, "Dat1_ModelFetch", LibToriPlatformX_LuaHost_Game_Dat1_ModelFetch);
    lua_bind_function_to_platform(
        L, lua, "Dat1_ModelLoad", LibToriPlatformX_LuaHost_Game_Dat1_ModelLoad);
    lua_bind_function_to_platform(
        L,
        lua,
        "Dat1_SubmitGameCacheModel",
        LibToriPlatformX_LuaHost_Game_Dat1_SubmitGameCacheModel);
    lua_bind_function_to_platform(
        L, lua, "ModelViewer_Init", LibToriPlatformX_LuaHost_Game_ModelViewer_Init);
    lua_bind_function_to_platform(
        L, lua, "ModelViewer_RenderModel", LibToriPlatformX_LuaHost_Game_ModelViewer_RenderModel);
    lua_setglobal(L, "Game");
}

#endif