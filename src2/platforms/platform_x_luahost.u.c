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

int
LibToriPlatformX_LuaHost_ScriptValueAsLuaInt(lua_State* L)
{
    struct LibToriRS_ScriptValue* value = (struct LibToriRS_ScriptValue*)lua_touserdata(L, 1);
    if( !value )
        return 0;
    assert(value->kind == LIBTORIRS_SCRIPT_VALUE_INT);
    lua_pushinteger(L, value->u.intval.value);
    return 1;
}

int
LibToriPlatformX_LuaHost_ScriptValueAsLuaAny(lua_State* L)
{
    struct LibToriRS_ScriptValue* value = (struct LibToriRS_ScriptValue*)lua_touserdata(L, 1);
    if( !value )
        return 0;
    assert(value->kind == LIBTORIRS_SCRIPT_VALUE_ANY);
    lua_pushlightuserdata(L, value->u.anyval.value);
    return 1;
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

    struct LibToriRS_IOQueue* io_queue = instance->io_queue;

    LibToriRS_ScriptAPI_Dat1_ConfigFileFetch(instance);

    return 0;
}

int
LibToriPlatformX_LuaHost_Game_Dat1_ConfigFileLoad(lua_State* L)
{
    struct LibToriRS_Instance* instance =
        (struct LibToriRS_Instance*)lua_touserdata(L, lua_upvalueindex(1));
    if( !instance )
        return 0;

    struct LibToriRS_IOQueue* io_queue = instance->io_queue;

    LibToriRS_ScriptAPI_Dat1_ConfigFileLoad(instance);

    return 0;
}

#endif