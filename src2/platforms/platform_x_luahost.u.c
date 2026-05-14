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

#endif