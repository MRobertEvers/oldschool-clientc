#ifndef LUA_BUILDCACHEDAT_H
#define LUA_BUILDCACHEDAT_H

struct BuildCacheDat;
struct GGame;
struct LuaGameType;

#include <stdbool.h>

/** All functions take (struct BuildCacheDat*, struct LuaGameType* args).
 * args is typically a VarTypeArray of positional arguments.
 * For functions needing GGame, args[0] is userdata(GGame*).
 * Functions that return values return a malloc'd LuaGameType* (caller must LuaGameType_Free).
 * Functions that return void return NULL. */

struct LuaGameType*
LuaBuildCacheDat_DispatchCommand(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    char* command,
    struct LuaGameType* args);

bool
LuaBuildCacheDat_CommandHasPrefix(char* command);

struct LuaGameType*
LuaBuildCacheDat_cache_map_scenery(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_set_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_init_varp_varbit_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_set_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_cache_map_terrain(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_has_map_terrain(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_has_map_scenery(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_has_model(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_has_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_init_floortypes_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_init_scenery_configs_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_all_scenery_locs(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_scenery_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_all_unique_scenery_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_npc_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_npc_head_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_idk_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_idk_head_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_obj_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_obj_head_model_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_obj(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_player_appearance_ids_from_packet(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_npc_ids_from_packet(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_cache_model(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_load_interfaces(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_load_component_sprites_from_media(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_cache_textures(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_init_sequences_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_animbaseframes_count_from_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_cache_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_cache_media(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_cache_title(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_init_idkits_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_init_objects_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_finalize_scene(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_clear(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_clear_map_chunks(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_clear_component_cache(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

#endif
