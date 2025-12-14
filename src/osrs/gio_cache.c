#include "gio_cache.h"

#include "tables/configs.h"
#include "tables/maps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_PATH "../cache"

struct Cache*
gioqb_cache_new(void)
{
    struct Cache* cache = cache_new_from_directory(CACHE_PATH);
    if( !cache )
    {
        printf("Failed to load cache from directory: %s\n", CACHE_PATH);
        return NULL;
    }

    return cache;
}

struct CacheArchive*
gioqb_cache_model_new_load(struct Cache* cache, int model_id)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_MODELS, model_id);
    if( !archive )
    {
        printf("Failed to load archive for model %d\n", model_id);
        return NULL;
    }

    return archive;
}

struct CacheArchive*
gioqb_cache_texture_new_load(struct Cache* cache, int texture_id)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_TEXTURES, texture_id);
    if( !archive )
    {
        printf("Failed to load archive for texture %d\n", texture_id);
        return NULL;
    }

    return archive;
}

struct CacheArchive*
gioqb_cache_map_scenery_new_load(struct Cache* cache, int chunk_mapx, int chunk_mapz)
{
    struct CacheArchive* archive = map_locs_archive_new_load(cache, chunk_mapx, chunk_mapz);
    if( !archive )
    {
        printf("Failed to load archive for map scenery %d, %d\n", chunk_mapx, chunk_mapz);
        return NULL;
    }

    return archive;
}

struct CacheArchive*
gioqb_cache_config_scenery_new_load(struct Cache* cache)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_LOCS);
    if( !archive )
    {
        printf("Failed to load archive for config scenery\n");
        return NULL;
    }

    return archive;
}

struct CacheArchive*
gioqb_cache_config_underlay_new_load(struct Cache* cache)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_UNDERLAY);
    if( !archive )
    {
        printf("Failed to load archive for config underlay\n");
        return NULL;
    }

    return archive;
}

struct CacheArchive*
gioqb_cache_config_overlay_new_load(struct Cache* cache)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_OVERLAY);
    if( !archive )
    {
        printf("Failed to load archive for config overlay\n");
        return NULL;
    }

    return archive;
}