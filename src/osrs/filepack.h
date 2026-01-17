#ifndef FILEPACK_H
#define FILEPACK_H

#include "rscache/cache.h"

struct FilePack
{
    void* data;
    int data_size;
};

struct FilePack*
filepack_new(
    struct Cache* cache,
    struct CacheArchive* archive);

void
filepack_free(struct FilePack* filepack);

struct FileMetadata
{
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