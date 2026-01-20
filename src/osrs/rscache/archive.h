#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <stdbool.h>

enum ArchiveFormat
{
    ARCHIVE_FORMAT_DAT2 = 0,
    // For .dat, the default archives are gzipped blobs.
    ARCHIVE_FORMAT_DAT = 1,
    // For .dat, the Configs index contains multifile archives.
    ARCHIVE_FORMAT_DAT_MULTIFILE = 2,
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
archive_name_hash_dat2(char* name);

int
archive_name_hash_dat(char* name);

#endif