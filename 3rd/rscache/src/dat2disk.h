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
    int* file_ids;
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

/**
 * There are many tables in the Dat2 cache format.
 *
 * Table 255 serves as a special table.
 * Table 255 contains archive metadata for the archives stored in
 * the other tables.
 * The metadata includes, file names, file ids, and CRC information.
 * The archive ids in table 255 correspond to the table ids of the other tables.
 * For example, table 2 corresponds to archive 2 of table 255.
 * The archives in table 255 are a binary blob that is parsed into a list of archive metadata.
 * So the metadata for the archives of table 2 are stored in a list
 * of archive metadata in archive 2 of table 255.
 * You can find archive metadata by searching through the list of archive metadata and looking for
 * the archive id that matches the archive id of the archive in table 2.
 *
 * Otherwise, Table 255 behaves like the other tables.
 *
 * Each table stores Archives. Archives can be a single blob,
 * or a multi-file archive. Archives can be compressed or uncompressed.
 *
 * The compression mode of the archive is stored WITH the archive.
 * All other information about the archive is stored in the table 255.
 *
 * Archives that contain multiple files are stored in a multi-file format, called a "FileList" here.
 * Note: For "dat", LostCity calls this a "JagFile", we call it a "FileListDat", which is slightly
 * different from "FileList".
 */

/**
 * Table id of the special reference table archives (see comment above).
 * Reference table N (archive_id=N, table_id=RSCACHE_DAT2_DISK_REFERENCE_TABLE_ID)
 * holds the RSCache_ReferenceTable metadata for table N. It is fetched
 * through the exact same archive-load path as any other table/archive pair, so
 * callers without a live RSCache_Dat2Disk handle (e.g. an IO queue consumer) can
 * request it like any other cache archive.
 */
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
