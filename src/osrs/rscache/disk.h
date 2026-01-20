#ifndef DISK_H
#define DISK_H

#include "archive.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int
disk_dat2file_read_archive(
    FILE* file,
    int index_id,
    int archive_id,
    int start_sector,
    int length_bytes,
    struct ArchiveBuffer* archive);

int
disk_datfile_read_archive(
    FILE* file,
    int index_id,
    int archive_id,
    int start_sector,
    int length_bytes,
    struct ArchiveBuffer* archive);

int
disk_dat2file_append_archive(
    FILE* file,
    int index_id,
    int archive_id,
    uint8_t* data,
    int data_size);

struct IndexRecord
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
disk_indexfile_read_record(
    FILE* file,
    int entry_idx,
    struct IndexRecord* record);

int
disk_indexfile_write_record(
    FILE* file,
    int entry_idx,
    struct IndexRecord* record);

#endif