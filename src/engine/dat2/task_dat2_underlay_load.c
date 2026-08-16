#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_flotype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2UnderlayLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int underlay_id;
};

static int
Task_Dat2UnderlayLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2UnderlayLoad* task = (struct Task_Dat2UnderlayLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_Dat2ConfigUnderlay* rscache_underlay = NULL;
    struct ToriRS_Flotype* torirs_underlay = NULL;

    PT_BEGIN(&task->pt);

    /* The whole group decodes on first touch; later tasks skip the archive
     * load entirely instead of re-decompressing it per id. */
    if( !dat2_buildcache_underlay_get(task->bc, task->underlay_id) )
    {
        RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_UNDERLAY);
        PT_YIELD(&task->pt);

        archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_UNDERLAY);
        if( !archive )
        {
            fprintf(
                stderr,
                "Failed to decode dat2 underlay config group for underlay %d\n",
                task->underlay_id);
            PT_EXIT(&task->pt);
        }

        dat2_buildcache_underlays_init_from_archive(task->bc, archive, NULL, 0);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    rscache_underlay = dat2_buildcache_underlay_get(task->bc, task->underlay_id);
    if( !rscache_underlay )
    {
        fprintf(stderr, "Failed to load dat2 underlay %d\n", task->underlay_id);
        PT_EXIT(&task->pt);
    }

    torirs_underlay = ToriRS_FlotypeFromRSCacheUnderlay(task->underlay_id, rscache_underlay);
    if( !torirs_underlay )
    {
        fprintf(stderr, "Failed to convert dat2 underlay %d\n", task->underlay_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_UnderlayAdd(&task->bc->base, task->underlay_id, torirs_underlay);

    PT_END(&task->pt);
}

static void
Task_Dat2UnderlayLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2UnderlayLoad_VTable = {
    .run = Task_Dat2UnderlayLoad_Run,
    .free = Task_Dat2UnderlayLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2UnderlayLoad(
    struct CacheProvider* provider,
    int underlay_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2UnderlayLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_UnderlayHas(provider, underlay_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2UnderlayLoad_VTable;
    strcpy(task->task.name, "Dat2UnderlayLoad");
    task->bc = dat2_buildcache;
    task->underlay_id = underlay_id;
    PT_INIT(&task->pt);
    return &task->task;
}
