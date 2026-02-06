#include "osrs/buildcache_loader.h"

#include "graphics/dashmap.h"
#include "osrs/buildcache.h"
#include "osrs/cache_utils.h"
#include "osrs/configmap.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/framemap.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables/sprites.h"

void
buildcache_loader_add_map_scenery(
    struct BuildCache* buildcache,
    int mapx,
    int mapz,
    int data_size,
    void* data)
{
    struct CacheMapLocs* locs = map_locs_new_from_decode(data, data_size);
    locs->_chunk_mapx = mapx;
    locs->_chunk_mapz = mapz;
    buildcache_add_map_scenery(buildcache, mapx, mapz, locs);
}

void
buildcache_loader_add_config_scenery(
    struct BuildCache* buildcache,
    struct GGame* game,
    int data_size,
    void* data,
    const int* ids,
    int ids_size)
{
    if( game->init_scenery_configmap )
        configmap_free(game->init_scenery_configmap);
    game->init_scenery_configmap = configmap_new_from_filepack(data, data_size, (int*)ids, ids_size);
    int id = 0;
    struct DashMapIter* iter = dashmap_iter_new(game->init_scenery_configmap);
    struct CacheConfigLocation* config_loc = NULL;
    while( (config_loc = (struct CacheConfigLocation*)configmap_iter_next(iter, &id)) )
    {
        buildcache_add_config_location(buildcache, id, config_loc);
    }
    dashmap_iter_free(iter);
}

void
buildcache_loader_add_model(
    struct BuildCache* buildcache,
    int model_id,
    int data_size,
    void* data)
{
    struct CacheModel* model = model_new_decode(data, data_size);
    buildcache_add_model(buildcache, model_id, model);
}

void
buildcache_loader_add_map_terrain(
    struct BuildCache* buildcache,
    int mapx,
    int mapz,
    int data_size,
    void* data)
{
    struct CacheMapTerrain* terrain =
        map_terrain_new_from_decode(data, data_size, mapx, mapz);
    buildcache_add_map_terrain(buildcache, mapx, mapz, terrain);
}

void
buildcache_loader_add_config_underlay(
    struct BuildCache* buildcache,
    int data_size,
    void* data)
{
    struct DashMap* configmap = configmap_new_from_filepack(data, data_size, NULL, 0);
    struct DashMapIter* iter = dashmap_iter_new(configmap);
    int id = 0;
    struct CacheConfigUnderlay* underlay = NULL;
    while( (underlay = (struct CacheConfigUnderlay*)configmap_iter_next(iter, &id)) )
    {
        buildcache_add_config_underlay(buildcache, id, underlay);
    }
    dashmap_iter_free(iter);
}

void
buildcache_loader_add_config_overlay(
    struct BuildCache* buildcache,
    int data_size,
    void* data)
{
    struct DashMap* configmap = configmap_new_from_filepack(data, data_size, NULL, 0);
    struct DashMapIter* iter = dashmap_iter_new(configmap);
    int id = 0;
    struct CacheConfigOverlay* overlay = NULL;
    while( (overlay = (struct CacheConfigOverlay*)configmap_iter_next(iter, &id)) )
    {
        buildcache_add_config_overlay(buildcache, id, overlay);
    }
    dashmap_iter_free(iter);
}

void
buildcache_loader_add_texture_definitions(
    struct BuildCache* buildcache,
    struct GGame* game,
    int data_size,
    void* data,
    const int* ids,
    int ids_size)
{
    (void)buildcache;
    if( game->init_texture_definitions_configmap )
        configmap_free(game->init_texture_definitions_configmap);
    game->init_texture_definitions_configmap =
        configmap_new_from_filepack(data, data_size, (int*)ids, ids_size);
}

void
buildcache_loader_add_spritepack(
    struct BuildCache* buildcache,
    int id,
    int data_size,
    void* data)
{
    struct CacheSpritePack* spritepack =
        sprite_pack_new_decode(data, data_size, SPRITELOAD_FLAG_NORMALIZE);
    buildcache_add_spritepack(buildcache, id, spritepack);
}

void
buildcache_loader_add_config_sequences(
    struct BuildCache* buildcache,
    struct GGame* game,
    int data_size,
    void* data,
    const int* ids,
    int ids_size)
{
    if( game->init_sequences_configmap )
        configmap_free(game->init_sequences_configmap);
    game->init_sequences_configmap =
        configmap_new_from_filepack(data, data_size, (int*)ids, ids_size);
    struct DashMapIter* iter = dashmap_iter_new(game->init_sequences_configmap);
    int seq_id = 0;
    struct CacheConfigSequence* seq = NULL;
    while( (seq = (struct CacheConfigSequence*)configmap_iter_next(iter, &seq_id)) )
    {
        buildcache_add_config_sequence(buildcache, seq_id, seq);
    }
    dashmap_iter_free(iter);
}

void
buildcache_loader_add_frame_blob(
    struct BuildCache* buildcache,
    int id,
    int data_size,
    void* data)
{
    struct CacheFrameBlob* blob =
        (struct CacheFrameBlob*)cu_filelist_new_from_filepack(data, data_size);
    buildcache_add_frame_blob(buildcache, id, blob);
}

void
buildcache_loader_add_framemap(
    struct BuildCache* buildcache,
    int id,
    int data_size,
    void* data)
{
    struct CacheFramemap* framemap = framemap_new_decode2(id, data, data_size);
    buildcache_add_framemap(buildcache, id, framemap);
}
