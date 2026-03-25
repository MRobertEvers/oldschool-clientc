#ifndef LUA_UI_H
#define LUA_UI_H

#include "lua_gametypes.h"

struct BuildCacheDat;
struct GGame;

bool
LuaUI_CommandHasPrefix(const char* command);

struct LuaGameType*
LuaUI_DispatchCommand(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    char* command,
    struct LuaGameType* args);

#endif

