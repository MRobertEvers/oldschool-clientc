#ifndef LUA_GAME_H
#define LUA_GAME_H

#include "lua_gametypes.h"
#include "osrs/game.h"

struct LuaGameType*
LuaGame_build_scene(
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaGame_build_scene_centerzone(
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaGame_exec_pkt_player_info(
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaGame_exec_pkt_npc_info(
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaGame_exec_pkt_if_settab(
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaGame_exec_pkt_update_inv_full(
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaGame_get_inv_obj_ids(
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaGame_load_interfaces(
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaGame_load_component_sprites(
    struct GGame* game,
    struct LuaGameType* args);

/** Unique model ids for `COMPONENT_TYPE_MODEL` with `modelType == 1` (for Lua cache load). */
struct LuaGameType*
LuaGame_get_interface_model_ids(
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