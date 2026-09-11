#include "js5_rscache.h"
#include <assert.h>

#include <dat2disk.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int
js5_rscache_read_raw(
    void* user,
    int archive,
    int group,
    uint8_t** data,
    size_t* size)
{
    struct Js5RscacheStorage* storage = (struct Js5RscacheStorage*)user;
    if( !storage || !storage->disk )
        return JS5_STORAGE_ERROR;
    assert(data);
    assert(size);
    *data = NULL;
    *size = 0u;
    struct RSCache_Dat2DiskArchive* loaded =
        RSCache_Dat2DiskArchiveNewLoadRaw(storage->disk, archive, group);
    if( !loaded )
        return JS5_STORAGE_MISSING;
    if( loaded->data_size < 0 || (loaded->data_size > 0 && !loaded->data) )
    {
        RSCache_Dat2DiskArchiveFree(loaded);
        return JS5_STORAGE_ERROR;
    }
    *data = (uint8_t*)loaded->data;
    *size = (size_t)loaded->data_size;
    loaded->data = NULL;
    loaded->data_size = 0;
    RSCache_Dat2DiskArchiveFree(loaded);
    return JS5_STORAGE_OK;
}

static void
js5_rscache_release_raw(void* user, uint8_t* data)
{
    (void)user;
    free(data);
}

static int
js5_rscache_install_reference(
    void* user,
    int archive,
    const uint8_t* container,
    size_t size)
{
    struct Js5RscacheStorage* storage = (struct Js5RscacheStorage*)user;
    if( !storage || !storage->disk || size > INT_MAX )
        return JS5_STORAGE_ERROR;
    assert(container);
    return RSCache_Dat2DiskInstallReferenceTableRaw(
               storage->disk, archive, container, (int)size)
               ? JS5_STORAGE_OK
               : JS5_STORAGE_ERROR;
}

static int
js5_rscache_activate_reference(
    void* user,
    int archive,
    const uint8_t* container,
    size_t size)
{
    struct Js5RscacheStorage* storage = (struct Js5RscacheStorage*)user;
    if( !storage || !storage->disk || !container || archive < 0 ||
        archive >= RSCACHE_DAT2_DISK_TABLE_CAPACITY || size > INT_MAX )
        return JS5_STORAGE_ERROR;
    if( RSCache_Dat2DiskReferenceTable(storage->disk, archive) )
        return JS5_STORAGE_OK;
    /*
     * A reference can survive an interrupted cache creation while idxN did
     * not. Dat2Disk deliberately skips such a table at open. JS5 has already
     * validated this container against the master, so publishing it here is
     * safe; the helper also recreates the missing idxN sentinel.
     */
    return RSCache_Dat2DiskInstallReferenceTableRaw(
               storage->disk, archive, container, (int)size)
               ? JS5_STORAGE_OK
               : JS5_STORAGE_ERROR;
}

static int
js5_rscache_install_group(
    void* user,
    int archive,
    int group,
    const uint8_t* container,
    size_t size,
    uint32_t version)
{
    struct Js5RscacheStorage* storage = (struct Js5RscacheStorage*)user;
    if( !storage || !storage->disk || size > (size_t)INT_MAX - 4u )
        return JS5_STORAGE_ERROR;
    assert(container);
    uint8_t* stored = (uint8_t*)malloc(size + 4u);
    assert(stored);
    memcpy(stored, container, size);
    stored[size] = (uint8_t)(version >> 24u);
    stored[size + 1u] = (uint8_t)(version >> 16u);
    stored[size + 2u] = (uint8_t)(version >> 8u);
    stored[size + 3u] = (uint8_t)version;
    /* Through the disk, not its directory: a store-backed cache (the browser's
     * IndexedDB one) has no path to write to, and routing by handle is what
     * lets the same adapter serve both backings. */
    int result = RSCache_Dat2DiskWriteArchiveTo(
        storage->disk, archive, group, stored, (int)(size + 4u));
    free(stored);
    return result == 0 ? JS5_STORAGE_OK : JS5_STORAGE_ERROR;
}

bool
Js5RscacheStorageInit(
    struct Js5RscacheStorage* storage,
    struct RSCache_Dat2Disk* disk,
    const char* directory)
{
    assert(directory);
    if( !directory[0] )
        return false;
    assert(storage);
    assert(disk);
    size_t length = strlen(directory) + 1u;
    char* copy = (char*)malloc(length);
    assert(copy);
    memcpy(copy, directory, length);
    storage->disk = disk;
    storage->directory = copy;
    return true;
}

void
Js5RscacheStorageClear(struct Js5RscacheStorage* storage)
{
    assert(storage);
    free(storage->directory);
    storage->directory = NULL;
    storage->disk = NULL;
}

struct Js5StorageOps
Js5RscacheStorageOperations(void)
{
    struct Js5StorageOps operations;
    memset(&operations, 0, sizeof(operations));
    operations.read_raw = js5_rscache_read_raw;
    operations.release_raw = js5_rscache_release_raw;
    operations.install_reference = js5_rscache_install_reference;
    operations.activate_reference = js5_rscache_activate_reference;
    operations.install_group = js5_rscache_install_group;
    return operations;
}
