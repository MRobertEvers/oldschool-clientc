#ifndef DISK_H
#define DISK_H

#include "osrs/archive.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int disk_dat2file_read_archive(
    FILE* file,
    int index_id,
    int archive_id,
    int start_sector,
    int length_bytes,
    struct Dat2Archive* archive);

int disk_dat2file_append_archive(
    FILE* file, int index_id, int archive_id, uint8_t* data, int data_size);

#endif