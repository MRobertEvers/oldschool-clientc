#ifndef RSCACHE_RSCACHESHARED_FILELIST_H
#define RSCACHE_RSCACHESHARED_FILELIST_H

#include "../dat2disk/dat2disk.h"
#include "../dat1disk/dat1disk.h"

/**
 * This is part of cachelib
 *
 * filepack is part of gio.h
 */

struct RSCacheShared_FileList
{
    char** files;
    int* file_sizes;
    int file_count;
};

struct RSCacheShared_FileList*
RSCacheShared_FileListNewFromCacheArchive(struct RSCacheDat2Disk_Archive* archive);

struct RSCacheShared_FileList*
RSCacheShared_FileListNewFromDecode(
    char* data,
    int data_size,
    int num_files);
void
RSCacheShared_FileListFree(struct RSCacheShared_FileList* filelist);

// Jagfile in lostcity

struct RSCacheShared_FileListDat
{
    char** files;
    int* file_sizes;
    int* file_name_hashes;
    int file_count;
};

struct RSCacheShared_FileListDat*
RSCacheShared_FileListDatNewFromCacheDatArchive(struct RSCacheDat1Disk_Archive* archive);

struct RSCacheShared_FileListDat*
RSCacheShared_FileListDatNewFromDecode(
    char* data,
    int data_size);

void
RSCacheShared_FileListDatFree(struct RSCacheShared_FileListDat* filelist);

int
RSCacheShared_FileListDatFindFileByName(
    struct RSCacheShared_FileListDat* filelist,
    const char* name);

// ".dat" + ".idx" files inside the config table config archive.
struct RSCacheShared_FileListDatIndexed
{
    char* data;
    int data_size;

    int* offsets;
    int offset_count;
};

struct RSCacheShared_FileListDatIndexed*
RSCacheShared_FileListDatIndexedNewFromDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size);

void
RSCacheShared_FileListDatIndexedFree(struct RSCacheShared_FileListDatIndexed* filelist);

#endif