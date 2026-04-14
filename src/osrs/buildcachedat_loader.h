#ifndef BUILD_CACHE_DAT_LOADER_H
#define BUILD_CACHE_DAT_LOADER_H

#include "game.h"
#include "osrs/buildcachedat.h"

void
buildcachedat_loader_set_2d_media_jagfile(
    struct BuildCacheDat* buildcachedat,
    int data_size,
    void* data);

void
buildcachedat_loader_set_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    int data_size,
    void* data);

/** Load varp/varbit from config jagfile into game. Call as soon as config is loaded
 * so varp packets can be applied before scene finalize. */
void
buildcachedat_loader_init_varp_varbit(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game);

void
buildcachedat_loader_set_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    int data_size,
    void* data);

void
buildcachedat_loader_map_terrain_cache_add_mapid(
    struct BuildCacheDat* buildcachedat,
    int map_id,
    int data_size,
    void* data);

void
buildcachedat_loader_map_terrain_cache_add(
    struct BuildCacheDat* buildcachedat,
    int param_a,
    int param_b,
    int data_size,
    void* data);

void
buildcachedat_loader_map_scenery_cache_add_mapid(
    struct BuildCacheDat* buildcachedat,
    int map_id,
    int data_size,
    void* data);

void
buildcachedat_loader_map_scenery_cache_add(
    struct BuildCacheDat* buildcachedat,
    int param_a,
    int param_b,
    int data_size,
    void* data);

void
buildcachedat_loader_init_floortypes_from_config_jagfile(struct BuildCacheDat* buildcachedat);

void
buildcachedat_loader_init_scenery_configs_from_config_jagfile(struct BuildCacheDat* buildcachedat);

int
buildcachedat_loader_get_all_scenery_locs(
    struct BuildCacheDat* buildcachedat,
    int** loc_ids_out,
    int** chunk_x_out,
    int** chunk_z_out);

int
buildcachedat_loader_get_scenery_model_ids(
    struct BuildCacheDat* buildcachedat,
    int loc_id,
    int** model_ids_out);

/** Flat sorted list of unique model IDs referenced by all cached scenery placements
 * (same flattening rules as buildcachedat_loader_get_scenery_model_ids per loc).
 * Caller must free(*model_ids_out). Returns 0 and NULL on empty / error. */
int
buildcachedat_loader_get_all_unique_scenery_model_ids(
    struct BuildCacheDat* buildcachedat,
    int** model_ids_out);

void
buildcachedat_loader_model_cache_add(
    struct BuildCacheDat* buildcachedat,
    int model_id,
    int data_size,
    void* data);

struct Scene2;
struct UIScene;

void
buildcachedat_loader_cache_textures(
    struct BuildCacheDat* buildcachedat,
    struct Scene2* scene2,
    int data_size,
    void* data);

void
buildcachedat_loader_sequences_init_from_config_jagfile(struct BuildCacheDat* buildcachedat);

int
buildcachedat_loader_get_animbaseframes_count_from_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat);

void
buildcachedat_loader_animbaseframes_cache_add(
    struct BuildCacheDat* buildcachedat,
    int animbaseframes_id,
    int data_size,
    void* data);

void
buildcachedat_loader_cache_media(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int data_size,
    void* data);

void
buildcachedat_loader_cache_title(
    struct BuildCacheDat* buildcachedat,
    struct UIScene* ui_scene,
    int data_size,
    void* data);

void
buildcachedat_loader_init_idkits_from_config_jagfile(struct BuildCacheDat* buildcachedat);

void
buildcachedat_loader_init_objects_from_config_jagfile(struct BuildCacheDat* buildcachedat);

void
buildcachedat_loader_load_interfaces(
    struct BuildCacheDat* buildcachedat,
    void* data,
    int data_size);

/** Load sprites for all component graphics (graphic, activeGraphic, invSlotGraphic) from
 * game->media_filelist and register them. No-op if media_filelist is NULL. */
void
buildcachedat_loader_load_component_sprites_from_media(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game);

void
buildcachedat_loader_finalize_scene(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z);

void
buildcachedat_loader_finalize_scene_centerzone(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int zonex,
    int zonez,
    int size);

/** Set up a fresh World for the chunked (slow-path) rebuild.
 *  Frees any existing world and creates a new one.  Jagfile lifecycle is
 *  left entirely to the caller: the config jagfile must remain valid through
 *  the full chunk loop so per-chunk init_scenery_configs calls can decode
 *  loc configs; the caller is responsible for clearing jagfiles at the right
 *  points.  After this, drive the build with
 *  world_rebuild_centerzone_begin / _chunk / _end. */
void
buildcachedat_loader_prepare_scene_centerzone(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game);

void
buildcachedat_loader_scenery_config_load_mapchunk_from_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz);

int
buildcachedat_loader_scenery_config_get_model_ids_mapchunk(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz,
    int** model_ids_out);

int
buildcachedat_loader_scenery_config_get_animbaseframes_ids_mapchunk(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz,
    int** frame_ids_out);

#endif