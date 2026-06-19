#include "rscache_dat1disk.h"

#include "../shared/rscache_shared_archive.h"
#include "../disk/rscache_disk.h"
#include "../shared/rscache_shared_file_list.h"
#include "../dat1a/rscache_dat1a_anim_frame.h"
#include "../dat1a/rscache_dat1a_version_list_mapsquare.h"

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
    struct RSCacheDisk_IndexRecord* record,
    char const* cache_directory,
    int table_id,
    int entry_idx)
{
    FILE* index_file = dat1disk_fopen_index(cache_directory, table_id);
    if( !index_file )
        return -1;

    if( RSCacheDisk_IndexFileReadRecord(index_file, entry_idx, record) != 0 )
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

struct RSCacheDat1Disk*
RSCacheDat1Disk_NewFromDirectory(char const* directory)
{
    struct RSCacheDat1Disk_Archive* archive = NULL;
    struct RSCacheShared_FileListDat* filelist = NULL;
    char* file_data = NULL;

    struct RSCacheDat1Disk* cache_dat = malloc(sizeof(struct RSCacheDat1Disk));
    if( cache_dat == NULL )
        return NULL;
    cache_dat->directory = strdup(directory);
    cache_dat->_dat_file = dat1disk_fopen_dat(cache_dat->directory);
    if( cache_dat->_dat_file == NULL )
    {
        printf("Failed to open dat file\n");
        printf("Directory: %s\n", cache_dat->directory);
        free(cache_dat);
        return NULL;
    }

    archive = RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Configs, RSCacheDat1A_ConfigKind_VersionList);

    filelist = RSCacheShared_FileListDatNewFromCacheDatArchive(archive);

    int file_data_size = 0;
    int name_hash = RSCacheShared_ArchiveNameHashDat("map_index");
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( filelist->file_name_hashes[i] == name_hash )
        {
            file_data = filelist->files[i];
            file_data_size = filelist->file_sizes[i];
            break;
        }
    }

    cache_dat->map_squares = RSCacheDat1A_MapSquaresNewDecode(file_data, file_data_size);

    name_hash = RSCacheShared_ArchiveNameHashDat("anim_index");
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

    // cache_dat->animbaseframes = malloc(archive_count * sizeof(struct RSCacheDat1A_AnimBaseFrames*));
    // cache_dat->animbaseframes_count = archive_count;
    // for( int i = 0; i < archive_count; i++ )
    // {
    //     archive = RSCacheDat1Disk_ArchiveNewLoad(cache_dat, RSCacheDat1Disk_Table_Animations, i);
    //     if( !archive )
    //         continue;
    //     cache_dat->animbaseframes[i] =
    //         RSCacheDat1A_AnimBaseFramesNewDecode(archive->data, archive->data_size);
    //     RSCacheDat1Disk_ArchiveFree(archive);
    //     archive = NULL;
    // }

    RSCacheShared_FileListDatFree(filelist);
    RSCacheDat1Disk_ArchiveFree(archive);

    return cache_dat;
}

void
RSCacheDat1Disk_Free(struct RSCacheDat1Disk* cache_dat)
{
    if( cache_dat == NULL )
        return;
    if( cache_dat->_dat_file )
        fclose(cache_dat->_dat_file);
    RSCacheDat1A_MapSquaresFree(cache_dat->map_squares);
    free((void*)cache_dat->directory);
    free(cache_dat);
}

struct RSCacheDat1Disk_Archive*
RSCacheDat1Disk_ArchiveNewLoad(
    struct RSCacheDat1Disk* cache_dat,
    int table_id,
    int archive_id)
{
    struct RSCacheShared_ArchiveBuffer dat2_archive = { 0 };
    struct RSCacheDat1Disk_Archive* archive = malloc(sizeof(struct RSCacheDat1Disk_Archive));
    memset(archive, 0, sizeof(struct RSCacheDat1Disk_Archive));

    if( table_id == RSCacheDat1Disk_Table_Configs )
        dat2_archive.format = RSCacheShared_ArchiveFormat_DatMultifile;
    else
        dat2_archive.format = RSCacheShared_ArchiveFormat_Dat;

    // 2. Consult the index for table_id. Table_id=2 is idx2
    //  - Read the entry "archive_id" in idx2. archive_id is the slot in the idx2 file..
    //  - Load the archive specified in the entry from .dat2
    //  - Decompress the archive if necessary
    //  - Use information from the reference table to load the files, etc.

    // TODO: Read archive_id or archive_slot?
    struct RSCacheDisk_IndexRecord index_record = { 0 };
    dat1disk_read_index(&index_record, cache_dat->directory, table_id, archive_id);

    int res = RSCacheDisk_DatFileReadArchive(
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

    if( dat2_archive.format == RSCacheShared_ArchiveFormat_Dat )
    {
        if( !RSCacheShared_ArchiveDecompressDat(&dat2_archive) )
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
RSCacheDat1Disk_ArchiveFree(struct RSCacheDat1Disk_Archive* archive)
{
    if( !archive )
        return;
    if( archive->data )
        free(archive->data);
    free(archive);
}