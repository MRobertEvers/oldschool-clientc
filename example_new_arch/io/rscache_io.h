#ifndef RSCACHE_IO_H
#define RSCACHE_IO_H

#include "asyncio.h"
#include "osrs/rscache/rscache.h"

static inline void
RSCacheIO_Dat2ReferenceTableLoad(
    struct IO* io,
    int table_id)
{
    assert(io != NULL);
    assert(table_id >= 0);
    IO_PushReferenceTable(io, table_id);
}

static inline struct RSCacheDat2Disk_ReferenceTable*
RSCacheIO_Dat2ReferenceTableDecode(
    struct IO* io,
    int slot_id)
{
    assert(io != NULL);
    assert(slot_id >= 0);
    struct RSCacheDat2Disk_ReferenceTable* table = NULL;
    struct IOItem* item = &io->io_slots[slot_id];
    assert(item->kind == IOK_REFERENCE_TABLE);

    table = RSCacheDat2Disk_ReferenceTableNewDecode(IOITEM_DATA(item), IOITEM_DATA_SIZE(item));
    assert(table != NULL);

    IOITEM_FREE_DATA(item);

    return table;
}

static inline void
RSCacheIO_Dat2ModelLoad(
    struct IO* io,
    int model_id)
{
    assert(io != NULL);
    assert(model_id >= 0);
    IO_PushCache(io, 0, RSCacheDat2Disk_Table_Models, model_id, 0);
}

static inline struct RSCacheDat2A_Model*
RSCacheIO_Dat2ModelDecode(
    struct IO* io,
    int slot_id)
{
    assert(io != NULL);
    assert(slot_id >= 0);
    struct RSCacheDat2A_Model* model = NULL;
    struct IOItem* item = &io->io_slots[slot_id];
    assert(item->kind == IOK_CACHE);
    assert(item->u.cache.table_id == RSCacheDat2Disk_Table_Models);

    model = RSCacheDat2A_ModelNewDecode(IOITEM_DATA(item), IOITEM_DATA_SIZE(item));

    assert(model != NULL);

    IOITEM_FREE_DATA(item);

    return model;
}

#endif // RSCACHE_IO_H