#ifndef RSCACHE_RSCACHESHARED_ARCHIVE_H
#define RSCACHE_RSCACHESHARED_ARCHIVE_H

#include <stdbool.h>

enum RSCacheShared_ArchiveFormat
{
    RSCacheShared_ArchiveFormat_Dat2 = 0,
    // For .dat, the default archives are gzipped blobs.
    RSCacheShared_ArchiveFormat_Dat = 1,
    // For .dat, the Configs index contains multifile archives.
    RSCacheShared_ArchiveFormat_DatMultifile = 2,
};

/**
 * The .dat2 file is a concatenation of archives.
 * An archive may be a single data blob, or contain multiple files.
 *
 * On disk, the archives may be compressed.
 */
struct RSCacheShared_ArchiveBuffer
{
    enum RSCacheShared_ArchiveFormat format;
    char* data;
    int data_size;

    int archive_id;
};

int
RSCacheShared_ArchiveNameHashDat2(char* name);

int
RSCacheShared_ArchiveNameHashDat(const char* name);

#endif