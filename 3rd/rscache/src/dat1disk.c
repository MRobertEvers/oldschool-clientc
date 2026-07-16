#include "dat1disk.h"

#include "archive.h"
#include "datatypes/mapsquares.h"
#include "dat2disk.h"
#include "filelist.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_FILE_NAME_ROOT "main_file_cache"

static FILE*
dat1disk_fopen_dat(char const* cache_directory)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.dat", cache_directory, CACHE_FILE_NAME_ROOT);
    return fopen(path, "rb+");
}

static FILE*
dat1disk_fopen_index(
    char const* cache_directory,
    int table_id)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.idx%d", cache_directory, CACHE_FILE_NAME_ROOT, table_id);
    return fopen(path, "rb+");
}

static int
dat1disk_read_index(
    struct RSCache_Dat2DiskIndexRecord* record,
    char const* cache_directory,
    int table_id,
    int entry_idx)
{
    FILE* index_file = dat1disk_fopen_index(cache_directory, table_id);
    if( !index_file )
        return -1;

    if( RSCache_Dat2DiskIndexFileReadRecord(index_file, entry_idx, record) != 0 )
    {
        fclose(index_file);
        return -1;
    }

    record->idx_file_id = table_id;
    fclose(index_file);
    return 0;
}

struct RSCache_Dat1Disk*
RSCache_Dat1DiskNewFromDirectory(char const* directory)
{
    struct RSCache_Dat1DiskArchive* archive = NULL;
    struct RSCache_FileListDat* filelist = NULL;
    char* file_data = NULL;

    struct RSCache_Dat1Disk* cache = malloc(sizeof(struct RSCache_Dat1Disk));
    if( !cache )
        return NULL;
    memset(cache, 0, sizeof(struct RSCache_Dat1Disk));

    cache->directory = strdup(directory);
    if( !cache->directory )
    {
        free(cache);
        return NULL;
    }

    cache->dat_file = dat1disk_fopen_dat(cache->directory);
    if( !cache->dat_file )
    {
        printf("Failed to open dat file\n");
        printf("Directory: %s\n", cache->directory);
        free(cache->directory);
        free(cache);
        return NULL;
    }

    archive = RSCache_Dat1DiskArchiveNewLoad(
        cache,
        RSCACHE_DAT1_DISK_TABLE_CONFIGS,
        RSCACHE_DAT1_CONFIG_VERSION_LIST);
    if( !archive )
        goto error;

    filelist = RSCache_FileListDatNewFromDecode(archive->data, archive->data_size);

    int file_data_size = 0;
    int name_hash = RSCache_ArchiveNameHashDat("map_index");
    if( filelist )
    {
        for( int i = 0; i < filelist->file_count; i++ )
        {
            if( filelist->file_name_hashes[i] == name_hash )
            {
                file_data = filelist->files[i];
                file_data_size = filelist->file_sizes[i];
                break;
            }
        }
    }

    cache->map_squares = RSCache_MapSquaresNewDecode(file_data, file_data_size);

    if( filelist )
        RSCache_FileListDatFree(filelist);
    RSCache_Dat1DiskArchiveFree(archive);

    return cache;

error:
    if( cache->dat_file )
        fclose(cache->dat_file);
    free(cache->directory);
    free(cache);
    return NULL;
}

void
RSCache_Dat1DiskFree(struct RSCache_Dat1Disk* cache)
{
    if( !cache )
        return;

    if( cache->dat_file )
        fclose(cache->dat_file);

    RSCache_MapSquaresFree(cache->map_squares);
    free(cache->directory);
    free(cache);
}

struct RSCache_Dat1DiskArchive*
RSCache_Dat1DiskArchiveNewLoad(
    struct RSCache_Dat1Disk* cache,
    int table_id,
    int archive_id)
{
    assert(cache);

    struct RSCache_Dat1DiskArchive* archive = malloc(sizeof(struct RSCache_Dat1DiskArchive));
    if( !archive )
        return NULL;
    memset(archive, 0, sizeof(struct RSCache_Dat1DiskArchive));

    enum RSCache_ArchiveFormat format = table_id == RSCACHE_DAT1_DISK_TABLE_CONFIGS
        ? RSCACHE_ARCHIVE_FORMAT_DAT_MULTIFILE
        : RSCACHE_ARCHIVE_FORMAT_DAT;

    struct RSCache_Dat2DiskArchive raw = { 0 };
    struct RSCache_Dat2DiskIndexRecord index_record = { 0 };
    dat1disk_read_index(&index_record, cache->directory, table_id, archive_id);

    if( RSCache_Dat2DiskDatFileReadArchive(
            cache->dat_file,
            index_record.idx_file_id,
            index_record.archive_idx,
            index_record.sector,
            index_record.length,
            &raw) != 0 )
    {
        printf("Failed to read dat archive for table %d\n", table_id);
        goto error;
    }

    if( format == RSCACHE_ARCHIVE_FORMAT_DAT )
    {
        if( !RSCache_ArchiveDecompressDat(&raw, format) )
        {
            printf("Failed to decompress dat archive for table %d\n", table_id);
            goto error;
        }
    }

    archive->data = raw.data;
    archive->data_size = raw.data_size;
    archive->archive_id = archive_id;
    archive->table_id = table_id;
    archive->format = format;
    return archive;

error:
    if( raw.data )
        free(raw.data);
    free(archive);
    return NULL;
}

void
RSCache_Dat1DiskArchiveFree(struct RSCache_Dat1DiskArchive* archive)
{
    if( !archive )
        return;

    if( archive->data )
        free(archive->data);

    free(archive);
}
