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

/** Heap allocator bytes in use (platform_get_memory_info), as MB float. */
struct LuaGameType*
LuaGame_get_heap_usage_mb(
    struct GGame* game,
    struct LuaGameType* args);

/** Chunked (slow-path) world rebuild: begin -- prepare world and allocate build structures. */
struct LuaGameType*
LuaGame_rebuild_centerzone_begin(
    struct GGame* game,
    struct LuaGameType* args);

/** Chunked rebuild: process a single map chunk (mapx, mapz); clears buildcache after. */
struct LuaGameType*
LuaGame_rebuild_centerzone_chunk(
    struct GGame* game,
    struct LuaGameType* args);

/** Chunked rebuild: finalize terrain/lighting, rebuild minimap, clear buildcache. */
struct LuaGameType*
LuaGame_rebuild_centerzone_end(
    struct GGame* game,
    struct LuaGameType* args);

/** Convenience: full slow rebuild in one call (all chunks assumed loaded; uses begin/chunk/end). */
struct LuaGameType*
LuaGame_rebuild_centerzone_slow(
    struct GGame* game,
    struct LuaGameType* args);

#endif