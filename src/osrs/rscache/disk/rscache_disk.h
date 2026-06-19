#ifndef RSCACHE_RSCACHEDISK_H
#define RSCACHE_RSCACHEDISK_H

#include "../shared/rscache_shared_archive.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int
RSCacheDisk_Dat2FileReadArchive(
    FILE* file,
    int index_id,
    int archive_id,
    int start_sector,
    int length_bytes,
    struct RSCacheShared_ArchiveBuffer* archive);

int
RSCacheDisk_DatFileReadArchive(
    FILE* file,
    int index_id,
    int archive_id,
    int start_sector,
    int length_bytes,
    struct RSCacheShared_ArchiveBuffer* archive);

int
RSCacheDisk_Dat2FileAppendArchive(
    FILE* file,
    int index_id,
    int archive_id,
    uint8_t* data,
    int data_size);

struct RSCacheDisk_IndexRecord
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

int
RSCacheDisk_IndexFileReadRecord(
    FILE* file,
    int entry_idx,
    struct RSCacheDisk_IndexRecord* record);

int
RSCacheDisk_IndexFileWriteRecord(
    FILE* file,
    int entry_idx,
    struct RSCacheDisk_IndexRecord* record);

#endif