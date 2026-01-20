#include "filepack.h"
#include "gio_assets.h"
#include "gio_cache.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/xtea_config.h"
#include "rscache/tables/configs.h"
#include "rscache/tables/maps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_PATH "../cache254"

struct CacheDat*
gioqb_cache_dat_new(void)
{
    struct CacheDat* cache_dat = cache_dat_new(CACHE_PATH);
    if( !cache_dat )
    {
        printf("Failed to load cache from directory: %s\n", CACHE_PATH);
        return NULL;
    }

    return cache_dat;
}

struct CacheDatArchive*
gioqb_cache_dat_model_new_load(
    struct CacheDat* cache_dat,
    int model_id)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_MODELS, model_id);
    if( !archive )
    {
        printf("Failed to load archive for model %d\n", model_id);
        return NULL;
    }

    return archive;
}

struct CacheDatArchive*
gioqb_cache_dat_texture_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* archive = cache_dat_archive_new_load(cache_dat, CACHE_DAT_TEXTURES, 0);
    if( !archive )
    {
        printf("Failed to load archive for texture definitions\n");
        return NULL;
    }

    return archive;
}

struct CacheDatArchive*
gioqb_cache_dat_spritepack_new_load(
    struct CacheDat* cache_dat,
    int spritepack_id)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_SPRITES, spritepack_id);
    if( !archive )
    {
        printf("Failed to load archive for spritepack %d\n", spritepack_id);
        return NULL;
    }

    return archive;
}

struct CacheDatArchive*
gioqb_cache_dat_map_scenery_new_load(
    struct CacheDat* cache_dat,
    int chunk_mapx,
    int chunk_mapz)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_MAPS, chunk_mapx, chunk_mapz);
    if( !archive )
    {
        printf("Failed to load archive for map scenery %d, %d\n", chunk_mapx, chunk_mapz);
        return NULL;
    }

    return archive;
}

struct CacheDatArchive*
gioqb_cache_dat_map_terrain_new_load(
    struct CacheDat* cache_dat,
    int chunk_x,
    int chunk_z)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_MAPS, chunk_x, chunk_z);
    if( !archive )
    {
        printf("Failed to load archive for map terrain %d, %d\n", chunk_x, chunk_z);
        return NULL;
    }

    return archive;
}

struct CacheDatArchive*
gioqb_cache_dat_config_scenery_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_LOCS);
    if( !archive )
    {
        printf("Failed to load archive for config scenery\n");
        return NULL;
    }

    return archive;
}

struct CacheDatArchive*
gioqb_cache_dat_config_underlay_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_UNDERLAY);
    if( !archive )
    {
        printf("Failed to load archive for config underlay\n");
        return NULL;
    }

    return archive;
}

struct CacheDatArchive*
gioqb_cache_dat_config_overlay_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_OVERLAY);
    if( !archive )
    {
        printf("Failed to load archive for config overlay\n");
        return NULL;
    }

    return archive;
}

struct CacheDatArchive* //
gioqb_cache_dat_config_sequences_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_SEQUENCE);
    if( !archive )
    {
        printf("Failed to load archive for config sequences\n");
        return NULL;
    }

    return archive;
}

struct CacheDatArchive* //
gioqb_cache_dat_animation_new_load(
    struct CacheDat* cache_dat,
    int animation_aka_archive_id)
{
    if( animation_aka_archive_id == 8402 )
    {
        printf("animation_aka_archive_id: %d\n", animation_aka_archive_id);
    }
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_ANIMATIONS, animation_aka_archive_id);
    if( !archive )
    {
        printf("Failed to load archive for animation %d\n", animation_aka_archive_id);
        return NULL;
    }

    return archive;
}

struct CacheDatArchive* //
gioqb_cache_dat_framemap_new_load(
    struct CacheDat* cache_dat,
    int framemap_id)
{
    return NULL;
    // struct CacheDatArchive* archive =
    //     cache_dat_archive_new_load(cache_dat, CACHE_DAT_SKELETONS, framemap_id);
    // if( !archive )
    // {
    //     printf("Failed to load archive for framemap %d\n", framemap_id);
    //     return NULL;
    // }

    // return archive;
}

void
gioqb_cache_dat_fullfill(
    struct GIOQueue* io,
    struct CacheDat* cache_dat,
    struct GIOMessage* message)
{
    struct CacheDatArchive* archive = NULL;
    struct FilePack* filepack = NULL;

    if( message->command == ASSET_MODELS )
    {
        archive = gioqb_cache_dat_model_new_load(cache_dat, message->param_a);
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            archive->revision,
            message->param_a,
            archive->data,
            archive->data_size);
        archive->data = NULL;
        archive->data_size = 0;
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_TEXTURES )
    {
        archive = gioqb_cache_dat_texture_new_load(cache_dat);
        assert(archive && "Failed to load texture archive");
        filepack = filepack_new(cache_dat, archive);
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            archive->revision,
            0,
            filepack->data,
            filepack->data_size);
        filepack->data = NULL;
        filepack->data_size = 0;
        filepack_free(filepack);
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_SPRITEPACKS )
    {
        archive = gioqb_cache_dat_spritepack_new_load(cache_dat, message->param_a);
        assert(archive && "Failed to load spritepack archive");
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            archive->revision,
            message->param_a,
            archive->data,
            archive->data_size);
        archive->data = NULL;
        archive->data_size = 0;
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_MAP_SCENERY )
    {
        archive =
            gioqb_cache_dat_map_scenery_new_load(cache_dat, message->param_a, message->param_b);

        int param_b_mapxz = (message->param_a << 16) | message->param_b;
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            archive->revision,
            param_b_mapxz,
            archive->data,
            archive->data_size);
        archive->data = NULL;
        archive->data_size = 0;
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_MAP_TERRAIN )
    {
        archive =
            gioqb_cache_dat_map_terrain_new_load(cache_dat, message->param_a, message->param_b);

        int param_b_mapxz = (message->param_a << 16) | message->param_b;
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            archive->revision,
            param_b_mapxz,
            archive->data,
            archive->data_size);
        archive->data = NULL;
        archive->data_size = 0;
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_CONFIG_SCENERY )
    {
        archive = gioqb_cache_dat_config_scenery_new_load(cache_dat);
        filepack = filepack_new(cache_dat, archive);
        assert(filepack && "Failed to create filepack");
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            archive->revision,
            0,
            filepack->data,
            filepack->data_size);
        filepack->data = NULL;
        filepack->data_size = 0;
        filepack_free(filepack);
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_CONFIG_UNDERLAY )
    {
        archive = gioqb_cache_dat_config_underlay_new_load(cache_dat);
        filepack = filepack_new(cache_dat, archive);
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            archive->revision,
            0,
            filepack->data,
            filepack->data_size);
        filepack->data = NULL;
        filepack->data_size = 0;
        filepack_free(filepack);
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_CONFIG_OVERLAY )
    {
        archive = gioqb_cache_dat_config_overlay_new_load(cache_dat);
        filepack = filepack_new(cache_dat, archive);
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            archive->revision,
            0,
            filepack->data,
            filepack->data_size);
        filepack->data = NULL;
        filepack->data_size = 0;
        filepack_free(filepack);
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_CONFIG_SEQUENCES )
    {
        archive = gioqb_cache_dat_config_sequences_new_load(cache_dat);
        filepack = filepack_new(cache_dat, archive);
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            archive->revision,
            0,
            filepack->data,
            filepack->data_size);
        filepack->data = NULL;
        filepack->data_size = 0;
        filepack_free(filepack);
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_ANIMATION )
    {
        archive = gioqb_cache_dat_animation_new_load(cache_dat, message->param_a);
        assert(archive && "Failed to load animation archive");
        filepack = filepack_new(cache_dat, archive);
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            archive->revision,
            message->param_a,
            filepack->data,
            filepack->data_size);
        filepack->data = NULL;
        filepack->data_size = 0;
        filepack_free(filepack);
        cache_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_FRAMEMAP )
    {
        archive = gioqb_cache_dat_framemap_new_load(cache_dat, message->param_a);
        assert(archive && "Failed to load framemap archive");
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            archive->revision,
            message->param_a,
            archive->data,
            archive->data_size);
        archive->data = NULL;
        archive->data_size = 0;
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else
    {
        printf("Unknown asset command: %d\n", message->command);
        assert(false && "Unknown asset command");
    }
}