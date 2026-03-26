#ifndef LUA_UI_H
#define LUA_UI_H

#include "lua_gametypes.h"

struct BuildCacheDat;
struct GGame;

struct LuaGameType*
LuaUI_load_revconfig(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaUI_load_fonts(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

bool
LuaUI_CommandHasPrefix(const char* command);

struct LuaGameType*
LuaUI_DispatchCommand(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    char* command,
    struct LuaGameType* args);

#endif
