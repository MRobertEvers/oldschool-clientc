#include "engine/dat2/dat2_tasks.h"
#include "mem/torirs_mem.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "world/wev.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Load every WorldEntityConfig (sailing boat) record, once, at boot.
 *
 * Same shape and the same reasons as Task_Dat2HitsplatLoad: whole-group and
 * eager, because the consumer is the WORLDENTITY_INFO packet apply and the
 * per-frame interpolator, neither of which has anywhere to yield to a load.
 * The group is tiny — 14 records, 506 bytes total in cache.osrs239.
 *
 * Archive 72 exists only in newer OldSchool caches (223's has none); an
 * absent group leaves the table empty and no boat ever spawns -- the
 * pre-sailing world, not an error. Absent is decided from the configs
 * reference table BEFORE the group is asked for: a disk read of a missing
 * group fails quietly, but a JS5 server answers one by closing the
 * connection, and the browser's reconnect re-sends it forever.
 */

struct Task_Dat2WevConfigLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct WevConfigTable* table;
};

static int
Task_Dat2WevConfigLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat2WevConfigLoad* task = (struct Task_Dat2WevConfigLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct WevConfig* entries = NULL;
    int count = 0;
    int decoded = 0;

    PT_BEGIN(&task->pt);

    /* One fetch of the configs table for every loader that wants it
     * (dat2_buildcache.h, reference_table_loading). */
    while( !dat2_buildcache_reference_table_has(task->bc, RSCACHE_DAT2_TABLE_CONFIGS) )
    {
        if( task->bc->reference_table_loading[RSCACHE_DAT2_TABLE_CONFIGS] )
        {
            task->task.blocked = 1;
            PT_YIELD(&task->pt);
            continue;
        }
        task->bc->reference_table_loading[RSCACHE_DAT2_TABLE_CONFIGS] = 1;
        RSCache_IO_Dat2ReferenceTableLoad(io, 0, RSCACHE_DAT2_TABLE_CONFIGS);
        PT_YIELD(&task->pt);

        {
            struct RSCache_ReferenceTable* configs = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
            task->bc->reference_table_loading[RSCACHE_DAT2_TABLE_CONFIGS] = 0;
            if( !configs )
            {
                fprintf(stderr, "wevconfig: no configs reference table\n");
                PT_EXIT(&task->pt);
            }
            dat2_buildcache_reference_table_add(task->bc, RSCACHE_DAT2_TABLE_CONFIGS, configs);
        }
    }
    if( !RSCache_ReferenceTableHasArchive(
            dat2_buildcache_reference_table_get(task->bc, RSCACHE_DAT2_TABLE_CONFIGS),
            RSCACHE_DAT2_CONFIG_KIND_WORLDENTITY) )
        PT_EXIT(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_WORLDENTITY);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_WORLDENTITY);
    if( !archive )
    {
        /* Listed but unreadable. The table stays empty and WORLDENTITY_INFO
         * spawn asserts loudly if a server sends one anyway. */
        PT_EXIT(&task->pt);
    }

    filelist =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(stderr, "wevconfig: failed to split config group 72\n");
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    /* Ids are sparse (cache.osrs239 runs 1..14 with no 0), so size from the
     * largest rather than from the count. */
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( archive->file_ids[i] + 1 > count )
            count = archive->file_ids[i] + 1;
    }
    if( count <= 0 )
    {
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    /* ASSET, like the configs' strings: WevConfigTable_Free releases it. */
    entries = ToriMem_Alloc(ToriMem_Assets(), (size_t)count * sizeof(*entries));
    /* Holes read back as id == -1, which is what WevConfigTable_Has tests. */
    for( int i = 0; i < count; i++ )
        WevConfig_Init(&entries[i], -1);

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];

        if( id < 0 || id >= count || filelist->file_sizes[i] <= 0 )
            continue;
        /* A duplicate file id re-decodes into the same entry; Decode Inits
         * (memsets) the struct, which would leak the first decode's heap
         * strings. Every entry was Init'd above, so this is free(NULL) on
         * the normal path. */
        WevConfig_FreeContents(&entries[id]);
        if( !WevConfig_Decode(
                &entries[id],
                id,
                (uint8_t const*)filelist->files[i],
                filelist->file_sizes[i]) )
            fprintf(
                stderr,
                "wevconfig %d: decode stopped at %d of %d bytes\n",
                id,
                entries[id]._consumed,
                filelist->file_sizes[i]);
        decoded++;
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    WevConfigTable_Set(task->table, entries, count);
    printf("wevconfig load: %d types (%d records)\n", count, decoded);

    PT_END(&task->pt);
}

static void
Task_Dat2WevConfigLoad_Free(struct ToriRS_Task* task_base)
{
    ToriMem_Free(ToriMem_Assets(), task_base);
}

static struct ToriRS_TaskVTable Task_Dat2WevConfigLoad_VTable = {
    .run = Task_Dat2WevConfigLoad_Run,
    .free = Task_Dat2WevConfigLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2WevConfigLoad(
    struct CacheProvider* provider,
    struct WevConfigTable* table)
{
    assert(provider);
    assert(table);

    struct Task_Dat2WevConfigLoad* task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2WevConfigLoad_VTable;
    strcpy(task->task.name, "Dat2WevConfigLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->table = table;
    PT_INIT(&task->pt);
    return &task->task;
}
