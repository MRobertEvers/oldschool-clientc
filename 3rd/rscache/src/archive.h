#ifndef RSCACHE_ARCHIVE_H
#define RSCACHE_ARCHIVE_H

#include <stdbool.h>
#include <stdint.h>

struct RSCache_Dat2DiskArchive;

enum RSCache_ArchiveFormat
{
    RSCACHE_ARCHIVE_FORMAT_DAT2 = 0,
    RSCACHE_ARCHIVE_FORMAT_DAT = 1,
    RSCACHE_ARCHIVE_FORMAT_DAT_MULTIFILE = 2,
};

bool
RSCache_ArchiveDecryptDecompress(
    struct RSCache_Dat2DiskArchive* archive,
    uint32_t* xtea_key_nullable);

bool
RSCache_ArchiveDecompressDat(
    struct RSCache_Dat2DiskArchive* archive,
    enum RSCache_ArchiveFormat format);

int
RSCache_ArchiveNameHashDat(const char* name);

int
RSCache_ArchiveNameHashDat2(char* name);

#endif
