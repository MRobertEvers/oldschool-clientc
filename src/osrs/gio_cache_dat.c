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

struct FileBuffer*
gioqb_cache_dat_config_flotype_file_load(struct CacheDat* cache_dat)
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

struct FileBuffer* //
gioqb_cache_dat_config_seq_file_new_load(struct CacheDat* cache_dat)
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

    int file_data_idx = filelist_dat_find_file_by_name(filelist, "seq.dat");
    if( file_data_idx == -1 )
    {
        printf("Failed to find seq.dat in filelist\n");
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

struct FileListDatIndexed*
gioqb_cache_dat_config_scenery_fileidx_new_load(struct CacheDat* cache_dat)
{
    struct FileBuffer* filebuffer = NULL;
    struct FileListDatIndexed* filelist_indexed = NULL;
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

    int file_index_idx = filelist_dat_find_file_by_name(filelist, "loc.idx");
    if( file_index_idx == -1 )
    {
        printf("Failed to find loc.idx in filelist\n");
        filelist_dat_free(filelist);
        cache_dat_archive_free(config_archive);
        return NULL;
    }

    int file_data_idx = filelist_dat_find_file_by_name(filelist, "loc.dat");
    if( file_data_idx == -1 )
    {
        printf("Failed to find loc.dat in filelist\n");
        filelist_dat_free(filelist);
        cache_dat_archive_free(config_archive);
        return NULL;
    }

    filelist_indexed = filelist_dat_indexed_new_from_decode(
        filelist->files[file_index_idx],
        filelist->file_sizes[file_index_idx],
        filelist->files[file_data_idx],
        filelist->file_sizes[file_data_idx]);
    if( !filelist_indexed )
    {
        printf("Failed to load filelist indexed\n");
        filelist_dat_indexed_free(filelist_indexed);
        cache_dat_archive_free(config_archive);
        return NULL;
    }

    return filelist_indexed;
}

struct FileBuffer* //
gioqb_cache_dat_config_versionlist_animindex_new_load(struct CacheDat* cache_dat)
{
    struct FileBuffer* filebuffer = NULL;
    struct FileListDat* filelist = NULL;
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_CONFIGS);
    if( !archive )
    {
        printf("Failed to load config underlay\n");
        return NULL;
    }

    archive = cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_VERSION_LIST);

    filelist = filelist_dat_new_from_cache_dat_archive(archive);

    int file_data_size = 0;
    int name_hash;
    char* file_data = NULL;

    name_hash = archive_name_hash_dat("anim_index");
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( filelist->file_name_hashes[i] == name_hash )
        {
            file_data = filelist->files[i];
            file_data_size = filelist->file_sizes[i];
            break;
        }
    }

    assert(file_data && "Failed to find anim index");

    void* filebuffer_data = malloc(file_data_size);
    if( !filebuffer_data )
    {
        printf("Failed to allocate memory for file data\n");
        filelist_dat_free(filelist);
        cache_dat_archive_free(archive);
        return NULL;
    }
    memcpy(filebuffer_data, file_data, file_data_size);
    filebuffer = filebuffer_new(filebuffer_data, file_data_size);

    filelist_dat_free(filelist);
    cache_dat_archive_free(archive);

    return filebuffer;
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
    struct FileListDatIndexed* filelist_indexed = NULL;

    if( message->command == ASSET_DAT_MAP_TERRAIN )
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
    else if( message->command == ASSET_DAT_CONFIG_FILE_FLOORTYPE )
    {
        filebuffer = gioqb_cache_dat_config_flotype_file_load(cache_dat);
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
    else if( message->command == ASSET_DAT_CONFIG_FILE_SEQ )
    {
        filebuffer = gioqb_cache_dat_config_seq_file_new_load(cache_dat);
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
    else if( message->command == ASSET_DAT_MAP_SCENERY )
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
    else if( message->command == ASSET_DAT_CONFIG_TEXTURE_SPRITES )
    {
        archive = gioqb_cache_dat_config_texture_sprites_new_load(cache_dat);
        gioqb_mark_done(
            io, message->message_id, message->command, 0, 0, archive->data, archive->data_size);
        archive->data = NULL;
        archive->data_size = 0;
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_DAT_CONFIG_MEDIA_2D_GRAPHICS )
    {
        archive = gioqb_cache_dat_config_media_new_load(cache_dat);
        gioqb_mark_done(
            io, message->message_id, message->command, 0, 0, archive->data, archive->data_size);
        archive->data = NULL;
        archive->data_size = 0;
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_DAT_CONFIG_TITLE )
    {
        archive = gioqb_cache_dat_config_title_new_load(cache_dat);
        gioqb_mark_done(
            io, message->message_id, message->command, 0, 0, archive->data, archive->data_size);
        archive->data = NULL;
        archive->data_size = 0;
        cache_dat_archive_free(archive);
        archive = NULL;
    }
    else if( message->command == ASSET_DAT_CONFIG_FILEIDX_SCENERY )
    {
        filelist_indexed = gioqb_cache_dat_config_scenery_fileidx_new_load(cache_dat);
        filepack = filepack_new_from_filelist_dat_indexed(filelist_indexed);
        gioqb_mark_done(
            io, message->message_id, message->command, 0, 0, filepack->data, filepack->data_size);
        filepack->data = NULL;
        filepack->data_size = 0;
        filepack_free(filepack);
        filepack = NULL;
        filelist_dat_indexed_free(filelist_indexed);
        filelist_indexed = NULL;
    }
    else if( message->command == ASSET_DAT_MODELS )
    {
        archive = gioqb_cache_dat_models_new_load(cache_dat, message->param_a);
        assert(archive && "Failed to load models archive");
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
    else if( message->command == ASSET_DAT_ANIMBASEFRAMES )
    {
        archive = gioqb_cache_dat_animbaseframes_new_load(cache_dat, message->param_a);
        // NULL is OK here!
        if( archive )
        {
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
            gioqb_mark_done(io, message->message_id, message->command, 0, 0, NULL, 0);
        }
    }
    else if( message->command == ASSET_DAT_CONFIG_VERSIONLIST_ANIMINDEX )
    {
        filebuffer = gioqb_cache_dat_config_versionlist_animindex_new_load(cache_dat);
        assert(filebuffer && "Failed to load anim index");
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
    else if( message->command == ASSET_DAT_SOUND )
    {
        archive = gioqb_cache_dat_sound_new_load(cache_dat, message->param_a);
        assert(archive && "Failed to load sound archive");
        gioqb_mark_done(
            io,
            message->message_id,
            message->command,
            0,
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