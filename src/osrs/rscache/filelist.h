#ifndef FILELIST_H
#define FILELIST_H

#include "cache.h"
#include "cache_dat.h"

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

struct FileListDat
{
    char** files;
    int* file_sizes;
    int* file_name_hashes;
    int file_count;
};

struct FileListDat*
filelist_dat_new_from_cache_dat_archive(struct CacheDatArchive* archive);

struct FileListDat*
filelist_dat_new_from_decode(
    char* data,
    int data_size);

void
filelist_dat_free(struct FileListDat* filelist);

int
filelist_dat_find_file_by_name(
    struct FileListDat* filelist,
    const char* name);

// ".dat" + ".idx" files inside the config table config archive.
struct FileListDatIndexed
{
    char* data;
    int data_size;

    int* offsets;
    int offset_count;
};

struct FileListDatIndexed*
filelist_dat_indexed_new_from_decode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size);

void
filelist_dat_indexed_free(struct FileListDatIndexed* filelist);

#endif