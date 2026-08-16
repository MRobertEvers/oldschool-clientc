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

struct LuaGameType*
LuaUI_load_rs_components(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaUI_resolve_inv_sprites(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaUI_parse_revconfig(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaUI_get_revconfig_inv_obj_ids(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaUI_load_revconfig_inventories(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaUI_load_revconfig_ui(
    struct GGame* game,
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

#endif
