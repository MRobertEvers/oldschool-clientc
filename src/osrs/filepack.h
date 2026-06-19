#ifndef FILEPACK_H
#define FILEPACK_H

#include "osrs/rscache/dat2disk/rscache_dat2disk.h"
#include "osrs/rscache/dat1disk/rscache_dat1disk.h"
#include "osrs/rscache/shared/rscache_shared_file_list.h"

struct FilePack
{
    void* data;
    int data_size;
};

struct FilePack*
filepack_new(
    struct RSCacheDat2Disk* cache,
    struct RSCacheDat2Disk_Archive* archive);

struct FilePack*
filepack_new_from_filelist_dat_indexed(struct RSCacheShared_FileListDatIndexed* filelist_indexed);

void
filepack_free(struct FilePack* filepack);

struct FileMetadata
{
    int flags;
    int revision;
    int file_count;
    int archive_id;
    int table_id;

    void* filelist_reference_ptr_;
    int filelist_reference_size;

    void* data_ptr_;
    int data_size;
};

void
filepack_metadata(
    struct FilePack* filepack,
    struct FileMetadata* out);

#endif