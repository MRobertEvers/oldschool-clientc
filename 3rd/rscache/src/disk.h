#ifndef RSCACHE_DISK_H
#define RSCACHE_DISK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct RSCache_DiskSectorHeader
{
    int part_no;
    int next_sector_no;
    int index_id;
    int archive_id;
};

struct RSCache_DiskIndexRecord
{
    // This is a sanity check. I.e. if you run off the end of the index's dat2 file,
    // This should match the "table_id" to which the record belongs.
    // i.e. idx2 is table_id 2.
    int idx_file_id;
    int archive_idx;
    int sector;
    // length in bytes as it's stored on disk in the dat2 file.
    int length;
};

struct RSCache_DiskArchive
{
    char* data;
    int data_size;
    int archive_id;
};

enum RSCache_DiskTable
{
    RSCACHE_DISK_TABLE_INTERFACES = 3,
    RSCACHE_DISK_TABLE_MODELS = 7,
};

struct RSCache_Disk
{
    char const* directory;
    FILE* dat2_file;
};

struct RSCache_Disk*
RSCache_DiskNewFromDirectory(char const* directory);
void
RSCache_DiskFree(struct RSCache_Disk* disk);

struct RSCache_DiskArchive*
RSCache_DiskArchiveNewLoad(
    struct RSCache_Disk* disk,
    int table_id,
    int archive_id);
void
RSCache_DiskArchiveFree(struct RSCache_DiskArchive* archive);

int
RSCache_DiskDat2FileReadArchive(
    FILE* dat2_file,
    int idx_file_id,
    int archive_id,
    int sector,
    int length,
    struct RSCache_DiskArchive* archive);

int
RSCache_DiskDatFileReadArchive(
    FILE* dat_file,
    int index_id,
    int archive_id,
    int start_sector,
    int length_bytes,
    struct RSCache_DiskArchive* archive);

int
RSCache_DiskDat2FileAppendArchive(
    FILE* file,
    int index_id,
    int archive_id,
    uint8_t* data,
    int data_size);

int
RSCache_DiskIndexFileReadRecord(
    FILE* file,
    int entry_idx,
    struct RSCache_DiskIndexRecord* record);

int
RSCache_DiskIndexFileWriteRecord(
    FILE* file,
    int entry_idx,
    struct RSCache_DiskIndexRecord* record);

#endif