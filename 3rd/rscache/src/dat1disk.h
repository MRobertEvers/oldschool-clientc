#ifndef RSCACHE_DAT1DISK_H
#define RSCACHE_DAT1DISK_H

#include "archive.h"

#include <stdio.h>

enum RSCache_Dat1DiskTable
{
    RSCACHE_DAT1_DISK_TABLE_CONFIGS = 0,
    RSCACHE_DAT1_DISK_TABLE_MODELS = 1,
    RSCACHE_DAT1_DISK_TABLE_ANIMATIONS = 2,
    RSCACHE_DAT1_DISK_TABLE_SOUNDS = 3,
    RSCACHE_DAT1_DISK_TABLE_MAPS = 4,
};

enum RSCache_Dat1ConfigKind
{
    RSCACHE_DAT1_CONFIG_TITLE_AND_FONTS = 1,
    RSCACHE_DAT1_CONFIG_CONFIGS = 2,
    RSCACHE_DAT1_CONFIG_INTERFACES = 3,
    RSCACHE_DAT1_CONFIG_MEDIA_2D = 4,
    RSCACHE_DAT1_CONFIG_VERSION_LIST = 5,
    RSCACHE_DAT1_CONFIG_TEXTURES = 6,
    RSCACHE_DAT1_CONFIG_CHAT_SYSTEM = 7,
    RSCACHE_DAT1_CONFIG_SOUND_EFFECTS = 8,
};

struct RSCache_MapSquares;

struct RSCache_Dat1Disk
{
    char* directory;
    FILE* dat_file;
    struct RSCache_MapSquares* map_squares;
};

struct RSCache_Dat1Disk*
RSCache_Dat1DiskNewFromDirectory(char const* path);

void
RSCache_Dat1DiskFree(struct RSCache_Dat1Disk* cache);

struct RSCache_Dat1DiskArchive
{
    char* data;
    int data_size;
    int archive_id;
    int table_id;
    int revision;
    int file_count;
    enum RSCache_ArchiveFormat format;
};

struct RSCache_Dat1DiskArchive*
RSCache_Dat1DiskArchiveNewLoad(
    struct RSCache_Dat1Disk* cache,
    int table_id,
    int archive_id);

void
RSCache_Dat1DiskArchiveFree(struct RSCache_Dat1DiskArchive* archive);

#endif
