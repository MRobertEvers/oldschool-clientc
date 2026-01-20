#include "gio_cache_dat.h"

#include "filepack.h"
#include "gio_assets.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/xtea_config.h"
#include "rscache/filelist.h"
#include "rscache/tables/configs.h"
#include "rscache/tables/maps.h"
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
    struct CacheDat* cache_dat = cache_dat_new(CACHE_PATH);
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

struct FileBuffer*
gioqb_cache_dat_flotype_load(struct CacheDat* cache_dat)
{
    struct FileBuffer* filebuffer = NULL;
    struct FileListDat* filelist = NULL;
    struct CacheDatArchive* config_archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_CONFIGS);
    if( !config_archive )
    {
        printf("Failed to load config underlay\n");
        return NULL;
    }

    filelist = filelist_dat_new_from_cache_dat_archive(config_archive);
    if( !filelist )
    {
        printf("Failed to load filelist underlay\n");
        cache_dat_archive_free(config_archive);
        return NULL;
    }

    int file_data_idx = filelist_dat_find_file_by_name(filelist, "flo.dat");
    if( file_data_idx == -1 )
    {
        printf("Failed to find flo.dat in filelist\n");
        filelist_dat_free(filelist);
        cache_dat_archive_free(config_archive);
        return NULL;
    }

    int file_size = filelist->file_sizes[file_data_idx];
    void* file_data = malloc(file_size);
    if( !file_data )
    {
        printf("Failed to allocate memory for file data\n");
        filelist_dat_free(filelist);
        cache_dat_archive_free(config_archive);
        return NULL;
    }
    memcpy(file_data, filelist->files[file_data_idx], file_size);

    filebuffer = filebuffer_new(file_data, file_size);

    filelist_dat_free(filelist);
    cache_dat_archive_free(config_archive);

    return filebuffer;
}

struct CacheDatArchive*
gioqb_cache_dat_texture_sprites_new_load(struct CacheDat* cache_dat)
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

void
gioqb_cache_dat_fullfill(
    struct GIOQueue* io,
    struct CacheDat* cache_dat,
    struct GIOMessage* message)
{
    struct CacheDatArchive* archive = NULL;
    struct FilePack* filepack = NULL;
    struct FileBuffer* filebuffer = NULL;
    struct FileListDat* filelist = NULL;

    if( message->command == ASSET_MAP_TERRAIN )
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
    else if( message->command == ASSET_CONFIG_FLOORTYPE )
    {
        filebuffer = gioqb_cache_dat_flotype_load(cache_dat);
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            0,
            0,
            filebuffer->data,
            filebuffer->data_size);
        filebuffer->data = NULL;
        filebuffer->data_size = 0;
        filebuffer_free(filebuffer);
        filebuffer = NULL;
    }
    else if( message->command == ASSET_TEXTURE_SPRITE )
    {
        archive = gioqb_cache_dat_texture_sprites_new_load(cache_dat);
        gioqb_mark_done(
            io, message->message_id, message->command, 0, 0, archive->data, archive->data_size);
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