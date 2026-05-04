#ifndef LUA_PLATFORM_H
#define LUA_PLATFORM_H

#include "osrs/lua_sidecar/lua_gametypes.h"

struct LuaGameScript
{
    char name[64];
    struct LuaGameType* args;
};

struct LuaGameScript*
LuaGameScript_New();

void
LuaGameScript_Free(struct LuaGameScript* script);

char*
LuaGameScript_GetName(struct LuaGameScript* game_script);

struct LuaGameType*
LuaGameScript_GetArgs(struct LuaGameScript* game_script);

#endif