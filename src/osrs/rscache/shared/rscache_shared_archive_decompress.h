#ifndef RSCACHE_RSCACHESHARED_ARCHIVEDECOMPRESS_H
#define RSCACHE_RSCACHESHARED_ARCHIVEDECOMPRESS_H

#include "rscache_shared_archive.h"

#include <stdint.h>

bool
RSCacheShared_ArchiveDecompress(struct RSCacheShared_ArchiveBuffer* archive);
// Decrypt and decompress the archive data using the provided XTEA key
// If xtea_key_nullable is NULL, the data will not be decrypted
bool
RSCacheShared_ArchiveDecryptDecompress(
    struct RSCacheShared_ArchiveBuffer* archive,
    uint32_t* xtea_key_nullable);

bool
RSCacheShared_ArchiveDecompressDat(struct RSCacheShared_ArchiveBuffer* archive);

#endif