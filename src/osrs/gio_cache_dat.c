#include "gio_cache_dat.h"

#include "filepack.h"
#include "osrs/rscache/dat1disk/rscache_dat1disk.h"
#include "osrs/rscache/shared/rscache_shared_xtea_config.h"
#include "osrs/rscache/shared/rscache_shared_file_list.h"
#include "osrs/rscache/dat2a/rscache_dat2a_configs.h"
#include "osrs/rscache/dat2a/rscache_dat2a_maps.h"
#include "osrs/rscache/dat1a/rscache_dat1a_anim_frame.h"
#include "osrs/rscache/dat1a/rscache_dat1a_version_list_mapsquare.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CACHE_PATH
#define CACHE_PATH "../cache254"
#endif

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

struct RSCacheDat1Disk*
gioqb_cache_dat_new(void)
{
    struct RSCacheDat1Disk* cache_dat = RSCacheDat1Disk_NewFromDirectory(CACHE_PATH);
    if( !cache_dat )
    {
        printf("Failed to load cache from directory: %s\n", CACHE_PATH);
        return NULL;
    }

    return cache_dat;
}

struct RSCacheDat1Disk_Archive*
gioqb_cache_dat_texture_new_load(struct RSCacheDat1Disk* cache_dat)
{
    return NULL;
}

struct RSCacheDat1Disk_Archive*
gioqb_cache_dat_map_terrain_new_load(
    struct RSCacheDat1Disk* cache_dat,
    int chunk_x,
    int chunk_z)
{
    int map_id = RSCacheDat1A_MapSquareId(chunk_x, chunk_z);

    struct RSCacheDat1A_MapSquare* map_square = NULL;

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

    return RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Maps, map_square->terrain_archive_id);
}

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_models_new_load(
    struct RSCacheDat1Disk* cache_dat,
    int model_id)
{
    if( model_id == 2373 )
    {
        printf("Loading model %d\n", model_id);
    }
    return RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Models, model_id);
}

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_sound_new_load(
    struct RSCacheDat1Disk* cache_dat,
    int sound_id)
{
    return RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Sounds, sound_id);
}

struct RSCacheDat1Disk_Archive*
gioqb_cache_dat_map_scenery_new_load(
    struct RSCacheDat1Disk* cache_dat,
    int chunk_x,
    int chunk_z)
{
    int map_id = RSCacheDat1A_MapSquareId(chunk_x, chunk_z);

    struct RSCacheDat1A_MapSquare* map_square = NULL;

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

    return RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Maps, map_square->loc_archive_id);
}

struct RSCacheDat1Disk_Archive*
gioqb_cache_dat_config_texture_sprites_new_load(struct RSCacheDat1Disk* cache_dat)
{
    struct RSCacheDat1Disk_Archive* config_archive =
        RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_Textures);
    if( !config_archive )
    {
        printf("Failed to load config textures\n");
        return NULL;
    }

    return config_archive;
}

struct RSCacheDat1Disk_Archive*
gioqb_cache_dat_config_media_new_load(struct RSCacheDat1Disk* cache_dat)
{
    struct RSCacheDat1Disk_Archive* config_archive =
        RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_Media2dGraphics);
    if( !config_archive )
    {
        printf("Failed to load config media\n");
        return NULL;
    }

    return config_archive;
}

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_config_title_new_load(struct RSCacheDat1Disk* cache_dat)
{
    struct RSCacheDat1Disk_Archive* config_archive =
        RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_TitleAndFonts);
    if( !config_archive )
    {
        printf("Failed to load config title\n");
        return NULL;
    }

    return config_archive;
}

struct RSCacheDat1Disk_Archive*
gioqb_cache_dat_animbaseframes_new_load(
    struct RSCacheDat1Disk* cache_dat,
    int animbaseframes_id)
{
    struct RSCacheDat1Disk_Archive* archive = NULL;
    archive = RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Animations, animbaseframes_id);
    if( !archive )
    {
        // This failure is expected. The loader just asks for all the archives.
        // printf("Failed to load animbaseframes archive\n");
        return NULL;
    }

    return archive;
}

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_config_configs_new_load(struct RSCacheDat1Disk* cache_dat)
{
    struct RSCacheDat1Disk_Archive* config_archive =
        RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_Configs);
    if( !config_archive )
    {
        printf("Failed to load config configs\n");
        return NULL;
    }

    return config_archive;
}

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_config_interfaces_new_load(struct RSCacheDat1Disk* cache_dat)
{
    struct RSCacheDat1Disk_Archive* config_archive =
        RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_Interfaces);
    if( !config_archive )
    {
        printf("Failed to load config interfaces\n");
        return NULL;
    }

    return config_archive;
}

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_config_media_2d_graphics_new_load(struct RSCacheDat1Disk* cache_dat)
{
    struct RSCacheDat1Disk_Archive* config_archive =
        RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_Media2dGraphics);
    if( !config_archive )
    {
        printf("Failed to load config media 2d graphics\n");
        return NULL;
    }

    return config_archive;
}

struct RSCacheDat1Disk_Archive* //
gioqb_cache_dat_config_version_list_new_load(struct RSCacheDat1Disk* cache_dat)
{
    struct RSCacheDat1Disk_Archive* config_archive =
        RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_VersionList);
    if( !config_archive )
    {
        printf("Failed to load config version list\n");
        return NULL;
    }

    return config_archive;
}

struct RSCacheDat1Disk_Archive*
gioqb_cache_dat_config_textures_new_load(struct RSCacheDat1Disk* cache_dat)
{
    struct RSCacheDat1Disk_Archive* config_archive =
        RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_Textures);
    if( !config_archive )
    {
        printf("Failed to load config textures\n");
        return NULL;
    }

    return config_archive;
}

struct RSCacheDat1Disk_Archive*
gioqb_cache_dat_config_chat_system_new_load(struct RSCacheDat1Disk* cache_dat)
{
    struct RSCacheDat1Disk_Archive* config_archive =
        RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_ChatSystem);
    if( !config_archive )
    {
        printf("Failed to load config chat system\n");
        return NULL;
    }

    return config_archive;
}

struct RSCacheDat1Disk_Archive*
gioqb_cache_dat_config_sound_effects_new_load(struct RSCacheDat1Disk* cache_dat)
{
    struct RSCacheDat1Disk_Archive* config_archive =
        RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_SoundEffects);
    if( !config_archive )
    {
        printf("Failed to load config sound effects\n");
        return NULL;
    }

    return config_archive;
}
