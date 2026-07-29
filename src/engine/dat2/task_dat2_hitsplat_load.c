#include "engine/dat2/dat2_tasks.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_hitsplat.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Load every hitsplat type's sprite id, once, at boot.
 *
 * Same shape and the same reasons as Task_Dat2VarbitLoad: whole-group and
 * eager rather than per-id and lazy, because the consumer reads it inside the
 * per-frame overlay build, where there is nowhere to yield to a load.
 *
 * The group is small — 78 records in cache.osrs230, a few hundred bytes each —
 * so this is a single group read, not a walk. Unlike varbits, hitsplats have no
 * sharded RS2 form to handle: the type predates that layout entirely, and a
 * cache without the group simply keeps drawing bare numbers.
 */

struct Task_Dat2HitsplatLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    struct RS_Hitsplats* hitsplats;
    /* The sprite-preload walk. Both the cursor and the table have to outlive a
     * PT_YIELD, so neither can be a local. */
    int preload_index;
};

static int
Task_Dat2HitsplatLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2HitsplatLoad* task = (struct Task_Dat2HitsplatLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    int* sprite_ids = NULL;
    int count = 0;
    int decoded = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_HITSPLAT);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_HITSPLAT);
    if( !archive )
    {
        /* Pre-OldSchool caches keep hitmarks in a named sprite archive instead,
         * which the static-sprite path already handles. Absent is normal. */
        PT_EXIT(&task->pt);
    }

    filelist =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(stderr, "hitsplat: failed to split the config group\n");
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    /* Ids are sparse, so size from the largest rather than from the count. */
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

    sprite_ids = malloc((size_t)count * sizeof(*sprite_ids));
    if( !sprite_ids )
    {
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }
    /* -1 is "no sprite", and it is a real state rather than a hole: a quarter
     * of cache.osrs230's records genuinely carry no opcode 5. */
    for( int i = 0; i < count; i++ )
        sprite_ids[i] = -1;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive->file_ids[i];
        struct RSCache_Dat2ConfigHitsplat entry;

        if( id < 0 || id >= count || filelist->file_sizes[i] <= 0 )
            continue;
        memset(&entry, 0, sizeof(entry));
        RSCache_Dat2ConfigHitsplatDecodeInplace(
            &entry, filelist->files[i], filelist->file_sizes[i]);
        if( entry._consumed != filelist->file_sizes[i] )
            fprintf(stderr, "hitsplat %d: decode consumed %d of %d bytes\n", id,
                    entry._consumed, filelist->file_sizes[i]);
        sprite_ids[id] = entry.sprite_id;
        decoded++;
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    if( !RS_Hitsplats_SetTypes(task->hitsplats, sprite_ids, count) )
    {
        free(sprite_ids);
        PT_EXIT(&task->pt);
    }
    printf("hitsplat load: %d types (%d records)\n", count, decoded);

    /*
     * Bring the sprites themselves into residence.
     *
     * `UITreeSceneBridge_EnsureSprite` binds a *resident* sprite into the
     * scene; it does not load one, because it is called from the per-frame
     * overlay build where there is nowhere to yield to. So knowing the ids is
     * not enough — something has to have asked for them, and nothing else ever
     * will. This is the same shape as the texture-wants registry: an
     * event-driven loader only ever has what somebody requested.
     *
     * Fifty-odd small sprites at boot, once. Loading them lazily on the first
     * hit would mean the first hit of a session draws no splat, which is
     * precisely the bug this is fixing.
     */
    for( task->preload_index = 0; task->preload_index < task->hitsplats->count;
         task->preload_index++ )
    {
        if( task->hitsplats->sprite_ids[task->preload_index] < 0 )
            continue;
        /* The _IF form skips a NULL child, which is what CreateTask_SpriteLoad
         * returns for a sprite already resident — and it clears its own child
         * pointer afterwards, which is what makes it safe inside a loop. */
        TASK_AWAITEX_IF(
            &task->pt,
            io,
            CreateTask_SpriteLoad(&task->bc->base,
                                  task->hitsplats->sprite_ids[task->preload_index]));
    }

    PT_END(&task->pt);
}

static void
Task_Dat2HitsplatLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2HitsplatLoad_VTable = {
    .run = Task_Dat2HitsplatLoad_Run,
    .free = Task_Dat2HitsplatLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2HitsplatLoad(
    struct CacheProvider* provider,
    struct RS_Hitsplats* hitsplats)
{
    assert(provider);
    assert(hitsplats);

    struct Task_Dat2HitsplatLoad* task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2HitsplatLoad_VTable;
    strcpy(task->task.name, "Dat2HitsplatLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->hitsplats = hitsplats;
    PT_INIT(&task->pt);
    return &task->task;
}
