#include "engine/dat2/dat2_tasks.h"

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
 * Archive 72 exists only from OldSchool 239 on; every earlier cache simply
 * does not have it, so an absent group leaves the table empty and no boat
 * ever spawns — the pre-sailing world, not an error.
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
    struct ToriRS_IO* io)
{
    struct Task_Dat2WevConfigLoad* task = (struct Task_Dat2WevConfigLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct WevConfig* entries = NULL;
    int count = 0;
    int decoded = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_WORLDENTITY);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_WORLDENTITY);
    if( !archive )
    {
        /* Pre-sailing cache (anything before OldSchool 239): no archive 72,
         * no boats. The table stays empty and WORLDENTITY_INFO spawn asserts
         * loudly if a server sends one anyway. */
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

    entries = malloc((size_t)count * sizeof(*entries));
    assert(entries);
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
    free(task_base);
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

    struct Task_Dat2WevConfigLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2WevConfigLoad_VTable;
    strcpy(task->task.name, "Dat2WevConfigLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->table = table;
    PT_INIT(&task->pt);
    return &task->task;
}
