#include "cache_dat.h"

#include "archive.h"
#include "disk.h"
#include "filelist.h"
#include "tables_dat/animframe.h"
#include "tables_dat/config_versionlist_mapsquare.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_FILE_NAME_ROOT "main_file_cache"

static FILE*
fopen_dat(char const* cache_directory)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.dat", cache_directory, CACHE_FILE_NAME_ROOT);
    return fopen(path, "rb+");
}

static FILE*
fopen_index(
    char const* cache_directory,
    int table_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.idx%d", cache_directory, CACHE_FILE_NAME_ROOT, table_id);
    return fopen(path, "rb+");
}

static int
read_index(
    struct IndexRecord* record,
    char const* cache_directory,
    int table_id,
    int entry_idx)
{
    FILE* index_file = fopen_index(cache_directory, table_id);
    if( !index_file )
        return -1;

    if( disk_indexfile_read_record(index_file, entry_idx, record) != 0 )
    {
        goto error;
    }

    record->idx_file_id = table_id;

    fclose(index_file);
    return 0;

error:;
    fclose(index_file);
    return -1;
}

struct CacheDat*
cache_dat_new(char const* directory)
{
    struct CacheDatArchive* archive = NULL;
    struct FileListDat* filelist = NULL;
    char* file_data = NULL;

    struct CacheDat* cache_dat = malloc(sizeof(struct CacheDat));
    if( cache_dat == NULL )
        return NULL;
    cache_dat->directory = strdup(directory);
    cache_dat->_dat_file = fopen_dat(cache_dat->directory);
    if( cache_dat->_dat_file == NULL )
    {
        printf("Failed to open dat file\n");
        free(cache_dat);
        return NULL;
    }

    archive = cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_VERSION_LIST);

    filelist = filelist_dat_new_from_cache_dat_archive(archive);

    int file_data_size = 0;
    int name_hash = archive_name_hash_dat("map_index");
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( filelist->file_name_hashes[i] == name_hash )
        {
            file_data = filelist->files[i];
            file_data_size = filelist->file_sizes[i];
            break;
        }
    }

    cache_dat->map_squares = cache_map_squares_new_decode(file_data, file_data_size);

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

    // int archive_count = file_data_size / 2;

    // cache_dat->animbaseframes = malloc(archive_count * sizeof(struct CacheDatAnimBaseFrames*));
    // cache_dat->animbaseframes_count = archive_count;
    // for( int i = 0; i < archive_count; i++ )
    // {
    //     archive = cache_dat_archive_new_load(cache_dat, CACHE_DAT_ANIMATIONS, i);
    //     if( !archive )
    //         continue;
    //     cache_dat->animbaseframes[i] =
    //         cache_dat_animbaseframes_new_decode(archive->data, archive->data_size);
    //     cache_dat_archive_free(archive);
    //     archive = NULL;
    // }

    return cache_dat;
}

void
cache_dat_free(struct CacheDat* cache_dat)
{
    if( cache_dat == NULL )
        return;
    if( cache_dat->_dat_file )
        fclose(cache_dat->_dat_file);
    free(cache_dat->directory);
    free(cache_dat);
}

struct CacheDatArchive*
cache_dat_archive_new_load(
    struct CacheDat* cache_dat,
    int table_id,
    int archive_id)
{
    struct CacheInetPayload* payload = NULL;
    struct ArchiveBuffer dat2_archive = { 0 };
    struct CacheDatArchive* archive = malloc(sizeof(struct CacheDatArchive));
    memset(archive, 0, sizeof(struct CacheDatArchive));

    if( table_id == CACHE_DAT_CONFIGS )
        dat2_archive.format = ARCHIVE_FORMAT_DAT_MULTIFILE;
    else
        dat2_archive.format = ARCHIVE_FORMAT_DAT;

    // 2. Consult the index for table_id. Table_id=2 is idx2
    //  - Read the entry "archive_id" in idx2. archive_id is the slot in the idx2 file..
    //  - Load the archive specified in the entry from .dat2
    //  - Decompress the archive if necessary
    //  - Use information from the reference table to load the files, etc.

    // TODO: Read archive_id or archive_slot?
    struct IndexRecord index_record = { 0 };
    read_index(&index_record, cache_dat->directory, table_id, archive_id);

    int res = disk_datfile_read_archive(
        cache_dat->_dat_file,
        index_record.idx_file_id,
        index_record.archive_idx,
        index_record.sector,
        index_record.length,
        &dat2_archive);
    if( res != 0 )
    {
        printf("Failed to read dat2 archive for table %d\n", table_id);
        goto error;
    }

    if( dat2_archive.format == ARCHIVE_FORMAT_DAT )
    {
        if( !archive_decompress_dat(&dat2_archive) )
        {
            printf("Failed to decompress dat2 archive for table %d\n", table_id);
            goto error;
        }
    }

    archive->data = dat2_archive.data;
    archive->data_size = dat2_archive.data_size;
    archive->archive_id = archive_id;
    archive->table_id = table_id;
    archive->format = dat2_archive.format;

    return archive;

error:;
    if( archive->data )
        free(archive->data);
    free(archive);
    return NULL;
}

void
cache_dat_archive_free(struct CacheDatArchive* archive)
{
    if( archive->data )
        free(archive->data);
    free(archive);
}