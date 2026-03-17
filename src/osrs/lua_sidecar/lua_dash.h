#ifndef LUA_DASH_H
#define LUA_DASH_H

#include "graphics/dash.h"
#include "lua_gametypes.h"

struct BuildCacheDat;
struct LuaGameType;

struct LuaGameType*
LuaDash_load_textures(
    struct DashGraphics* dash,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

bool
LuaDash_CommandHasPrefix(const char* command);

struct LuaGameType*
LuaDash_DispatchCommand(
    struct DashGraphics* dash,
    struct BuildCacheDat* buildcachedat,
    char* command,
    struct LuaGameType* args);

#endif
