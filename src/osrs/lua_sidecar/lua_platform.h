#ifndef LUA_PLATFORM_H
#define LUA_PLATFORM_H

#include "osrs/lua_sidecar/lua_gametypes.h"

struct LuaGameScript
{
    char name[64];
    struct LuaGameType* args;
};

#endif