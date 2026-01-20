#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <stdbool.h>

enum ArchiveFormat
{
    ARCHIVE_FORMAT_DAT2 = 0,
    ARCHIVE_FORMAT_DAT = 1,
};

/**
 * The .dat2 file is a concatenation of archives.
 * An archive may be a single data blob, or contain multiple files.
 *
 * On disk, the archives may be compressed.
 */
struct ArchiveBuffer
{
    enum ArchiveFormat format;
    char* data;
    int data_size;

    int archive_id;
};

int
archive_name_hash(char* name);

#endif