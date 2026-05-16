#ifndef PLATFORM_X_LUAHOST_H
#define PLATFORM_X_LUAHOST_H

#include "../libtorirs.h"
#include "../scripting/libtorirs_scripting.h"
#include "3rd/lua/lua.h"

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

int
LibToriPlatformX_LuaHost_GetIOQueue(lua_State* L)
{
    struct LibToriRS_Instance* instance =
        (struct LibToriRS_Instance*)lua_touserdata(L, lua_upvalueindex(1));
    if( !instance )
        return 0;

    struct LibToriRS_IOQueue* io_queue = instance->io_queue;
    lua_pushlightuserdata(L, io_queue);
    return 1;
}

int
LibToriPlatformX_LuaHost_LoadIO(lua_State* L)
{
    struct LibToriRS_IOQueue* io_queue = (struct LibToriRS_IOQueue*)lua_touserdata(L, 1);

    lua_pushlightuserdata(L, io_queue);

    return lua_yield(L, 1);
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