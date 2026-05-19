#ifndef PLATFORM_X_LUA_INTERNAL_H
#define PLATFORM_X_LUA_INTERNAL_H

#include "3rd/lua/lua.h"

struct LibToriRS_Instance;
struct CacheLib;

struct LibToriPlatformX_Lua
{
    lua_State* L;
    lua_State* L_coro;

    struct CacheLib* cache;
    struct LibToriRS_Instance* instance;
};

#endif