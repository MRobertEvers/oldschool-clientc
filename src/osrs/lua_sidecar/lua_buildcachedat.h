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
LuaBuildCacheDat_map_scenery_cache_add(
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
LuaBuildCacheDat_set_2d_media_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_map_terrain_cache_add(
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
LuaBuildCacheDat_model_cache_has(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_animbaseframes_cache_has(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_floortypes_init_from_config_jagfile(
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
LuaBuildCacheDat_model_cache_add(
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
LuaBuildCacheDat_sequences_init_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_animbaseframes_count_from_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_animbaseframes_cache_add(
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
LuaBuildCacheDat_idkits_init_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_objects_init_from_config_jagfile(
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
LuaBuildCacheDat_model_cache_clear(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_has_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_has_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_get_sequence_animbaseframes_ids(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_scenery_config_load_mapchunk_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_scenery_config_get_model_ids_mapchunk(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_scenery_config_get_animbaseframes_ids_mapchunk(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_animbaseframes_cache_clear(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_scenery_config_clear(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_sequences_clear(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_map_scenery_cache_clear(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_map_terrain_cache_clear(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_floortypes_clear(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_objects_clear(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_component_cache_clear(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_clear_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_clear_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

struct LuaGameType*
LuaBuildCacheDat_clear_media_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct LuaGameType* args);

#endif
