#ifndef FILELIST_H
#define FILELIST_H

#include "cache.h"

/**
 * This is part of cachelib
 *
 * filepack is part of gio.h
 */

struct FileList
{
    char** files;
    int* file_sizes;
    int file_count;
};

struct FileList*
filelist_new_from_cache_archive(struct CacheArchive* archive);
struct FileList*
filelist_new_from_decode(
    char* data,
    int data_size,
    int num_files);
void
filelist_free(struct FileList* filelist);

#endif