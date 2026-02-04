#include "gio_cache_dat.h"

#include "filepack.h"
#include "gio_assets.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/xtea_config.h"
#include "rscache/filelist.h"
#include "rscache/tables/configs.h"
#include "rscache/tables/maps.h"
#include "rscache/tables_dat/animframe.h"
#include "rscache/tables_dat/config_versionlist_mapsquare.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_PATH "../cache254"

struct FileBuffer*
filebuffer_new(
    char* data,
    int data_size)
{
    struct FileBuffer* filebuffer = malloc(sizeof(struct FileBuffer));
    filebuffer->data = data;
    filebuffer->data_size = data_size;
    return filebuffer;
}

void
filebuffer_free(struct FileBuffer* filebuffer)
{
    if( !filebuffer )
        return;
    free(filebuffer->data);
    free(filebuffer);
}

struct CacheDat*
gioqb_cache_dat_new(void)
{
    struct CacheDat* cache_dat = cache_dat_new_from_directory(CACHE_PATH);
    if( !cache_dat )
    {
        printf("Failed to load cache from directory: %s\n", CACHE_PATH);
        return NULL;
    }

    return cache_dat;
}

struct CacheDatArchive*
gioqb_cache_dat_texture_new_load(struct CacheDat* cache_dat)
{
    return NULL;
}

struct CacheDatArchive*
gioqb_cache_dat_map_terrain_new_load(
    struct CacheDat* cache_dat,
    int chunk_x,
    int chunk_z)
{
    int map_id = cache_map_square_id(chunk_x, chunk_z);

    struct CacheMapSquare* map_square = NULL;

    for( int i = 0; i < cache_dat->map_squares->squares_count; i++ )
    {
        if( cache_dat->map_squares->squares[i].map_id == map_id )
        {
            map_square = &cache_dat->map_squares->squares[i];
            break;
        }
    }

    if( !map_square )
    {
        printf("Failed to load map terrain %d, %d\n", chunk_x, chunk_z);
        return NULL;
    }

    return cache_dat_archive_new_load(cache_dat, CACHE_DAT_MAPS, map_square->terrain_archive_id);
}

struct CacheDatArchive* //
gioqb_cache_dat_models_new_load(
    struct CacheDat* cache_dat,
    int model_id)
{
    return cache_dat_archive_new_load(cache_dat, CACHE_DAT_MODELS, model_id);
}

struct CacheDatArchive* //
gioqb_cache_dat_sound_new_load(
    struct CacheDat* cache_dat,
    int sound_id)
{
    return cache_dat_archive_new_load(cache_dat, CACHE_DAT_SOUNDS, sound_id);
}

struct CacheDatArchive*
gioqb_cache_dat_map_scenery_new_load(
    struct CacheDat* cache_dat,
    int chunk_x,
    int chunk_z)
{
    int map_id = cache_map_square_id(chunk_x, chunk_z);

    struct CacheMapSquare* map_square = NULL;

    for( int i = 0; i < cache_dat->map_squares->squares_count; i++ )
    {
        if( cache_dat->map_squares->squares[i].map_id == map_id )
        {
            map_square = &cache_dat->map_squares->squares[i];
            break;
        }
    }

    if( !map_square )
    {
        printf("Failed to load map scenery %d, %d\n", chunk_x, chunk_z);
        return NULL;
    }

    return cache_dat_archive_new_load(cache_dat, CACHE_DAT_MAPS, map_square->loc_archive_id);
}

struct CacheDatArchive*
gioqb_cache_dat_config_texture_sprites_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* config_archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_TEXTURES);
    if( !config_archive )
    {
        printf("Failed to load config textures\n");
        return NULL;
    }

    return config_archive;
}

struct CacheDatArchive*
gioqb_cache_dat_config_media_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* config_archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_MEDIA_2D_GRAPHICS);
    if( !config_archive )
    {
        printf("Failed to load config media\n");
        return NULL;
    }

    return config_archive;
}

struct CacheDatArchive* //
gioqb_cache_dat_config_title_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* config_archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_TITLE_AND_FONTS);
    if( !config_archive )
    {
        printf("Failed to load config title\n");
        return NULL;
    }

    return config_archive;
}

struct CacheDatArchive*
gioqb_cache_dat_animbaseframes_new_load(
    struct CacheDat* cache_dat,
    int animbaseframes_id)
{
    struct CacheDatArchive* archive = NULL;
    archive = cache_dat_archive_new_load(cache_dat, CACHE_DAT_ANIMATIONS, animbaseframes_id);
    if( !archive )
    {
        // This failure is expected. The loader just asks for all the archives.
        // printf("Failed to load animbaseframes archive\n");
        return NULL;
    }

    return archive;
}

struct CacheDatArchive* //
gioqb_cache_dat_config_configs_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* config_archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_CONFIGS);
    if( !config_archive )
    {
        printf("Failed to load config configs\n");
        return NULL;
    }

    return config_archive;
}

struct CacheDatArchive* //
gioqb_cache_dat_config_interfaces_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* config_archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_INTERFACES);
    if( !config_archive )
    {
        printf("Failed to load config interfaces\n");
        return NULL;
    }

    return config_archive;
}

struct CacheDatArchive* //
gioqb_cache_dat_config_media_2d_graphics_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* config_archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_MEDIA_2D_GRAPHICS);
    if( !config_archive )
    {
        printf("Failed to load config media 2d graphics\n");
        return NULL;
    }

    return config_archive;
}

struct CacheDatArchive* //
gioqb_cache_dat_config_version_list_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* config_archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_VERSION_LIST);
    if( !config_archive )
    {
        printf("Failed to load config version list\n");
        return NULL;
    }

    return config_archive;
}

struct CacheDatArchive*
gioqb_cache_dat_config_textures_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* config_archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_TEXTURES);
    if( !config_archive )
    {
        printf("Failed to load config textures\n");
        return NULL;
    }

    return config_archive;
}

struct CacheDatArchive*
gioqb_cache_dat_config_chat_system_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* config_archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_CHAT_SYSTEM);
    if( !config_archive )
    {
        printf("Failed to load config chat system\n");
        return NULL;
    }

    return config_archive;
}

struct CacheDatArchive*
gioqb_cache_dat_config_sound_effects_new_load(struct CacheDat* cache_dat)
{
    struct CacheDatArchive* config_archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_SOUND_EFFECTS);
    if( !config_archive )
    {
        printf("Failed to load config sound effects\n");
        return NULL;
    }

    return config_archive;
}

void
gioqb_cache_dat_fullfill(
    struct GIOQueue* io,
    struct CacheDat* cache_dat,
    struct GIOMessage* message)
{
    struct CacheDatArchive* archive = NULL;
    void* data = NULL;
    int data_size = 0;
    bool null_ok = false;
    int out_param_a = 0;
    int out_param_b = 0;

    switch( message->command )
    {
    case ASSET_DAT_MAP_TERRAIN:
        archive =
            gioqb_cache_dat_map_terrain_new_load(cache_dat, message->param_a, message->param_b);
        out_param_b = (message->param_a << 16) | message->param_b;
        break;
    case ASSET_DAT_MAP_SCENERY:
        archive =
            gioqb_cache_dat_map_scenery_new_load(cache_dat, message->param_a, message->param_b);
        out_param_b = (message->param_a << 16) | message->param_b;
        break;
    case ASSET_DAT_MODELS:
        archive = gioqb_cache_dat_models_new_load(cache_dat, message->param_a);
        out_param_b = message->param_a;
        break;
    case ASSET_DAT_ANIMBASEFRAMES:
        archive = gioqb_cache_dat_animbaseframes_new_load(cache_dat, message->param_a);
        out_param_b = message->param_a;
        null_ok = true;
        break;
    case ASSET_DAT_SOUND:
        assert(false && "ASSET_DAT_SOUND not implemented");
        break;
    case ASSET_DAT_CONFIG_TITLE_AND_FONTS:
        archive = gioqb_cache_dat_config_title_new_load(cache_dat);
        break;
    case ASSET_DAT_CONFIG_CONFIGS:
        archive = gioqb_cache_dat_config_configs_new_load(cache_dat);
        break;
    case ASSET_DAT_CONFIG_INTERFACES:
        archive = gioqb_cache_dat_config_interfaces_new_load(cache_dat);
        break;
    case ASSET_DAT_CONFIG_MEDIA_2D_GRAPHICS:
        archive = gioqb_cache_dat_config_media_2d_graphics_new_load(cache_dat);
        break;
    case ASSET_DAT_CONFIG_VERSION_LIST:
        archive = gioqb_cache_dat_config_version_list_new_load(cache_dat);
        break;
    case ASSET_DAT_CONFIG_TEXTURES:
        archive = gioqb_cache_dat_config_textures_new_load(cache_dat);
        break;
    case ASSET_DAT_CONFIG_CHAT_SYSTEM:
        archive = gioqb_cache_dat_config_chat_system_new_load(cache_dat);
        break;
    case ASSET_DAT_CONFIG_SOUND_EFFECTS:
        assert(false && "ASSET_DAT_CONFIG_SOUND_EFFECTS not implemented");
        break;
    }

    if( archive )
    {
        data = archive->data;
        data_size = archive->data_size;

        archive->data = NULL;
        archive->data_size = 0;
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( !null_ok )
    {
        assert(false && "Failed to load archive");
    }

    gioqb_mark_done(
        io, message->message_id, message->command, out_param_a, out_param_b, data, data_size);
}