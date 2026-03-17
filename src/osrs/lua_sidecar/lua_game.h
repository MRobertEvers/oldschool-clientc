#ifndef LUA_GAME_H
#define LUA_GAME_H

#include "lua_gametypes.h"
#include "osrs/game.h"

struct LuaGameType*
LuaGame_build_scene(
    struct GGame* game,
    struct LuaGameType* args);

bool
LuaGame_CommandHasPrefix(const char* command);

struct LuaGameType*
LuaGame_DispatchCommand(
    struct GGame* game,
    char* command,
    struct LuaGameType* args);

#endif