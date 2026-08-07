#ifndef SRC_JS5_JS5_RSCACHE_H
#define SRC_JS5_JS5_RSCACHE_H

#include "js5.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RSCache_Dat2Disk;

/*
 * Storage adapter for an executor-owned sparse dat2 cache. The disk remains
 * owned by the executor and must outlive this adapter and its Js5Client.
 */
struct Js5RscacheStorage
{
    struct RSCache_Dat2Disk* disk;
    char* directory;
};

bool
Js5RscacheStorageInit(
    struct Js5RscacheStorage* storage,
    struct RSCache_Dat2Disk* disk,
    const char* directory);

void
Js5RscacheStorageClear(struct Js5RscacheStorage* storage);

struct Js5StorageOps
Js5RscacheStorageOperations(void);

#ifdef __cplusplus
}
#endif

#endif
