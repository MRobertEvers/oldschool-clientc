#include "gio_cache.h"

#include "filepack.h"
#include "gio_assets.h"
#include "osrs/rscache/xtea_config.h"
#include "rscache/tables/configs.h"
#include "rscache/tables/maps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_PATH "../cache"

struct Cache*
gioqb_cache_new(void)
{
    printf("Loading XTEA keys from: %s/xteas.json\n", CACHE_PATH);
    // printf("Loading XTEA keys from: ../cache/xteas.json\n");
    int xtea_keys_count = xtea_config_load_keys(CACHE_PATH "/xteas.json");
    if( xtea_keys_count == -1 )
    {
        printf("Failed to load xtea keys from: %s/xteas.json\n", CACHE_PATH);
        printf("Make sure the xteas.json file exists in the cache directory\n");
        assert(false && "Failed to load xtea keys");
    }
    printf("Loaded %d XTEA keys successfully\n", xtea_keys_count);

    struct Cache* cache = cache_new_from_directory(CACHE_PATH);
    if( !cache )
    {
        printf("Failed to load cache from directory: %s\n", CACHE_PATH);
        return NULL;
    }

    return cache;
}

struct CacheArchive*
gioqb_cache_model_new_load(
    struct Cache* cache,
    int model_id)
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
gioqb_cache_texture_new_load(struct Cache* cache)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_TEXTURES, 0);
    if( !archive )
    {
        printf("Failed to load archive for texture definitions\n");
        return NULL;
    }

    return archive;
}

struct CacheArchive*
gioqb_cache_spritepack_new_load(
    struct Cache* cache,
    int spritepack_id)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_SPRITES, spritepack_id);
    if( !archive )
    {
        printf("Failed to load archive for spritepack %d\n", spritepack_id);
        return NULL;
    }

    return archive;
}

struct CacheArchive*
gioqb_cache_map_scenery_new_load(
    struct Cache* cache,
    int chunk_mapx,
    int chunk_mapz)
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
gioqb_cache_map_terrain_new_load(
    struct Cache* cache,
    int chunk_x,
    int chunk_z)
{
    struct CacheArchive* archive = map_terrain_archive_new_load(cache, chunk_x, chunk_z);
    if( !archive )
    {
        printf("Failed to load archive for map terrain %d, %d\n", chunk_x, chunk_z);
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

struct CacheArchive* //
gioqb_cache_config_sequences_new_load(struct Cache* cache)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_SEQUENCE);
    if( !archive )
    {
        printf("Failed to load archive for config sequences\n");
        return NULL;
    }

    return archive;
}

struct CacheArchive* //
gioqb_cache_animation_new_load(
    struct Cache* cache,
    int animation_aka_archive_id)
{
    if( animation_aka_archive_id == 8402 )
    {
        printf("animation_aka_archive_id: %d\n", animation_aka_archive_id);
    }
    struct CacheArchive* archive =
        cache_archive_new_load(cache, CACHE_ANIMATIONS, animation_aka_archive_id);
    if( !archive )
    {
        printf("Failed to load archive for animation %d\n", animation_aka_archive_id);
        return NULL;
    }

    return archive;
}

struct CacheArchive* //
gioqb_cache_framemap_new_load(
    struct Cache* cache,
    int framemap_id)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_SKELETONS, framemap_id);
    if( !archive )
    {
        printf("Failed to load archive for framemap %d\n", framemap_id);
        return NULL;
    }

    return archive;
}

void
gioqb_cache_fullfill(
    struct GIOQueue* io,
    struct Cache* cache,
    struct GIOMessage* message)
{
    struct CacheArchive* archive = NULL;
    struct FilePack* filepack = NULL;

    bool is_filepack = false;
    bool null_ok = false;
    void* data = NULL;
    int data_size = 0;

    int out_param_a = 0;
    int out_param_b = 0;

    switch( message->command )
    {
    case ASSET_MODELS:
        archive = gioqb_cache_model_new_load(cache, message->param_a);
        out_param_b = message->param_a;
        break;
    case ASSET_TEXTURES:
        archive = gioqb_cache_texture_new_load(cache);

        is_filepack = true;
        filepack = filepack_new(cache, archive);
        break;
    case ASSET_SPRITEPACKS:
        archive = gioqb_cache_spritepack_new_load(cache, message->param_a);
        out_param_b = message->param_a;
        break;
    case ASSET_MAP_SCENERY:
        archive = gioqb_cache_map_scenery_new_load(cache, message->param_a, message->param_b);
        out_param_b = (message->param_a << 16) | message->param_b;
        break;
    case ASSET_MAP_TERRAIN:
        archive = gioqb_cache_map_terrain_new_load(cache, message->param_a, message->param_b);
        out_param_b = (message->param_a << 16) | message->param_b;
        break;
    case ASSET_CONFIG_SCENERY:
        archive = gioqb_cache_config_scenery_new_load(cache);

        is_filepack = true;
        filepack = filepack_new(cache, archive);
        break;
    case ASSET_CONFIG_UNDERLAY:
        archive = gioqb_cache_config_underlay_new_load(cache);

        is_filepack = true;
        filepack = filepack_new(cache, archive);
        break;
    case ASSET_CONFIG_OVERLAY:
        archive = gioqb_cache_config_overlay_new_load(cache);

        is_filepack = true;
        filepack = filepack_new(cache, archive);
        break;
    case ASSET_CONFIG_SEQUENCES:
        archive = gioqb_cache_config_sequences_new_load(cache);

        is_filepack = true;
        filepack = filepack_new(cache, archive);
        break;
    case ASSET_ANIMATION:
        archive = gioqb_cache_animation_new_load(cache, message->param_a);
        out_param_b = message->param_a;

        is_filepack = true;
        filepack = filepack_new(cache, archive);
        break;
    case ASSET_FRAMEMAP:
        archive = gioqb_cache_framemap_new_load(cache, message->param_a);
        out_param_b = message->param_a;
        break;
    default:
        printf("Unknown asset command: %d\n", message->command);
        assert(false && "Unknown asset command");
        break;
    }

    if( is_filepack )
    {
        if( !filepack && null_ok )
        {
            assert(false && "Failed to load filepack");
        }

        data = filepack->data;
        data_size = filepack->data_size;

        filepack->data = NULL;
        filepack->data_size = 0;
        filepack_free(filepack);
        filepack = NULL;

        cache_archive_free(archive);
        archive = NULL;
    }
    else
    {
        if( !archive && !null_ok )
        {
            assert(false && "Failed to load archive");
        }

        data = archive->data;
        data_size = archive->data_size;

        archive->data = NULL;
        archive->data_size = 0;
        cache_archive_free(archive);
        archive = NULL;
    }

    gioqb_mark_done(
        io, message->message_id, message->command, out_param_a, out_param_b, data, data_size);
}
