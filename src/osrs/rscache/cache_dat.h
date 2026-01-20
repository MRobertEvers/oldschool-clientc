#ifndef CACHE_DAT_H
#define CACHE_DAT_H

#include "archive_decompress.h"
#include "cache.h"

enum CacheDatTable
{
    CACHE_DAT_CONFIGS = 0,
    CACHE_DAT_MODELS = 1,
    CACHE_DAT_ANIMATIONS = 2,
    CACHE_DAT_SOUNDS = 3,
    CACHE_DAT_MAPS = 4,
};

struct CacheDat*
cache_dat_new(char const* path);

void
cache_dat_free(struct CacheDat* cache_dat);

struct CacheDatArchive
{
    char* data;
    int data_size;

    int archive_id;
    int table_id;
    int revision;

    int file_count;

    enum ArchiveFormat format;
};

struct CacheDatArchive*
cache_dat_archive_new_load(
    struct CacheDat* cache_dat,
    int table_id,
    int archive_id);

void
cache_dat_archive_free(struct CacheDatArchive* archive);

#endif