#ifndef RSCACHE_ARCHIVE_H
#define RSCACHE_ARCHIVE_H

#include <stdbool.h>
#include <stdint.h>

struct RSCache_DiskArchive;

bool
RSCache_ArchiveDecryptDecompress(
    struct RSCache_DiskArchive* archive,
    uint32_t* xtea_key_nullable);

#endif
