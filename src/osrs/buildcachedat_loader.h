#ifndef BUILD_CACHE_DAT_LOADER_H
#define BUILD_CACHE_DAT_LOADER_H

#include "osrs/buildcachedat.h"
#include "game.h"

void
buildcachedat_loader_set_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    int data_size,
    void* data);

void
buildcachedat_loader_set_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    int data_size,
    void* data);

void
buildcachedat_loader_cache_map_terrain(
    struct BuildCacheDat* buildcachedat,
    int param_a,
    int param_b,
    int data_size,
    void* data);

void
buildcachedat_loader_cache_map_scenery(
    struct BuildCacheDat* buildcachedat,
    int param_a,
    int param_b,
    int data_size,
    void* data);

void
buildcachedat_loader_load_floortype(struct BuildCacheDat* buildcachedat);

void
buildcachedat_loader_load_scenery_configs(struct BuildCacheDat* buildcachedat);

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

void
buildcachedat_loader_cache_model(
    struct BuildCacheDat* buildcachedat,
    int model_id,
    int data_size,
    void* data);

void
buildcachedat_loader_load_textures(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int data_size,
    void* data);

void
buildcachedat_loader_load_sequences(struct BuildCacheDat* buildcachedat);

int
buildcachedat_loader_get_animbaseframes_count(struct BuildCacheDat* buildcachedat);

void
buildcachedat_loader_cache_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    int animbaseframes_id,
    int data_size,
    void* data);

void
buildcachedat_loader_load_media(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int data_size,
    void* data);

void
buildcachedat_loader_load_title(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int data_size,
    void* data);

void
buildcachedat_loader_load_idkits(struct BuildCacheDat* buildcachedat);

void
buildcachedat_loader_load_objects(struct BuildCacheDat* buildcachedat);

void
buildcachedat_loader_finalize_scene(
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z);

#endif