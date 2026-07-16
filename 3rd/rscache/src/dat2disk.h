#ifndef RSCACHE_DAT2DISK_H
#define RSCACHE_DAT2DISK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct RSCache_ReferenceTable;

struct RSCache_Dat2DiskSectorHeader
{
    int part_no;
    int next_sector_no;
    int index_id;
    int archive_id;
};

struct RSCache_Dat2DiskIndexRecord
{
    int idx_file_id;
    int archive_idx;
    int sector;
    int length;
};

struct RSCache_Dat2DiskArchive
{
    char* data;
    int data_size;
    int archive_id;
    int table_id;
    int revision;
    int file_count;
};

enum RSCache_Dat2DiskTable
{
    RSCACHE_DAT2_DISK_TABLE_ANIMATIONS = 0,
    RSCACHE_DAT2_DISK_TABLE_SKELETONS = 1,
    RSCACHE_DAT2_DISK_TABLE_CONFIGS = 2,
    RSCACHE_DAT2_DISK_TABLE_INTERFACES = 3,
    RSCACHE_DAT2_DISK_TABLE_SOUND_EFFECTS = 4,
    RSCACHE_DAT2_DISK_TABLE_MAPS = 5,
    RSCACHE_DAT2_DISK_TABLE_MUSIC_TRACKS = 6,
    RSCACHE_DAT2_DISK_TABLE_MODELS = 7,
    RSCACHE_DAT2_DISK_TABLE_SPRITES = 8,
    RSCACHE_DAT2_DISK_TABLE_TEXTURES = 9,
    RSCACHE_DAT2_DISK_TABLE_BINARY = 10,
    RSCACHE_DAT2_DISK_TABLE_MUSIC_JINGLES = 11,
    RSCACHE_DAT2_DISK_TABLE_CLIENTSCRIPT = 12,
    RSCACHE_DAT2_DISK_TABLE_FONTS = 13,
    RSCACHE_DAT2_DISK_TABLE_MUSIC_SAMPLES = 14,
    RSCACHE_DAT2_DISK_TABLE_MUSIC_PATCHES = 15,
    RSCACHE_DAT2_DISK_TABLE_WORLDMAP_GEOGRAPHY = 18,
    RSCACHE_DAT2_DISK_TABLE_WORLDMAP = 19,
    RSCACHE_DAT2_DISK_TABLE_WORLDMAP_GROUND = 20,
    RSCACHE_DAT2_DISK_TABLE_DBTABLE_INDEX = 21,
    RSCACHE_DAT2_DISK_TABLE_ANIMAYAS = 22,
    RSCACHE_DAT2_DISK_TABLE_GAMEVALS = 24,
    RSCACHE_DAT2_DISK_TABLE_COUNT,
};

#define RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID 255

struct RSCache_Dat2Disk
{
    char* directory;
    struct RSCache_ReferenceTable* tables[RSCACHE_DAT2_DISK_TABLE_COUNT];
    FILE* dat2_file;
};

bool
RSCache_Dat2DiskIsValidTableId(int table_id);

struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewFromDirectory(char const* directory);
struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewUninitialized(void);
void
RSCache_Dat2DiskFree(struct RSCache_Dat2Disk* disk);

struct RSCache_Dat2DiskArchive*
RSCache_Dat2DiskArchiveNewReferenceTableLoad(
    struct RSCache_Dat2Disk* disk,
    int table_id);
struct RSCache_Dat2DiskArchive*
RSCache_Dat2DiskArchiveNewLoad(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id);
struct RSCache_Dat2DiskArchive*
RSCache_Dat2DiskArchiveNewLoadDecrypted(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id,
    uint32_t* xtea_key_nullable);
void
RSCache_Dat2DiskArchiveInitMetadata(
    struct RSCache_Dat2Disk* disk,
    struct RSCache_Dat2DiskArchive* archive);
void
RSCache_Dat2DiskArchiveInitMetadataFromTable(
    struct RSCache_ReferenceTable* table,
    struct RSCache_Dat2DiskArchive* archive);
void
RSCache_Dat2DiskArchiveFree(struct RSCache_Dat2DiskArchive* archive);

uint32_t*
RSCache_Dat2DiskArchiveXteaKey(
    struct RSCache_Dat2Disk* disk,
    int table_id,
    int archive_id);

int
RSCache_Dat2DiskDat2FileReadArchive(
    FILE* dat2_file,
    int idx_file_id,
    int archive_id,
    int sector,
    int length,
    struct RSCache_Dat2DiskArchive* archive);

int
RSCache_Dat2DiskDatFileReadArchive(
    FILE* dat_file,
    int index_id,
    int archive_id,
    int start_sector,
    int length_bytes,
    struct RSCache_Dat2DiskArchive* archive);

int
RSCache_Dat2DiskDat2FileAppendArchive(
    FILE* file,
    int index_id,
    int archive_id,
    uint8_t* data,
    int data_size);

int
RSCache_Dat2DiskIndexFileReadRecord(
    FILE* file,
    int entry_idx,
    struct RSCache_Dat2DiskIndexRecord* record);

int
RSCache_Dat2DiskIndexFileWriteRecord(
    FILE* file,
    int entry_idx,
    struct RSCache_Dat2DiskIndexRecord* record);

#endif
