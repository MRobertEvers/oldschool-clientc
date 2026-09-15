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
#include "log/torirs_log.h"

struct Task_Dat2FlotypeLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int flo_id;
};

static int
Task_Dat2FlotypeLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2FlotypeLoad* task = (struct Task_Dat2FlotypeLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_Dat2ConfigOverlay* rscache_overlay = NULL;
    struct ToriRS_Flotype* torirs_flotype = NULL;

    PT_BEGIN(&task->pt);

    /* The whole group decodes on first touch; later tasks skip the archive
     * load entirely instead of re-decompressing it per id. */
    if( !dat2_buildcache_overlay_get(task->bc, task->flo_id) )
    {
        RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_OVERLAY);
        PT_YIELD(&task->pt);

        archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_OVERLAY);
        if( !archive )
        {
            TORIRS_ERR("Failed to decode dat2 overlay config group for flo %d\n", task->flo_id);
            PT_EXIT(&task->pt);
        }

        dat2_buildcache_overlays_init_from_archive(task->bc, archive, NULL, 0);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    rscache_overlay = dat2_buildcache_overlay_get(task->bc, task->flo_id);
    if( !rscache_overlay )
    {
        TORIRS_ERR("Failed to load dat2 overlay %d\n", task->flo_id);
        PT_EXIT(&task->pt);
    }

    torirs_flotype = ToriRS_FlotypeFromRSCacheOverlay(task->flo_id, rscache_overlay);
    if( !torirs_flotype )
    {
        TORIRS_ERR("Failed to convert dat2 overlay %d\n", task->flo_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_FlotypeAdd(&task->bc->base, task->flo_id, torirs_flotype);

    PT_END(&task->pt);
}

static void
Task_Dat2FlotypeLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2FlotypeLoad_VTable = {
    .run = Task_Dat2FlotypeLoad_Run,
    .free = Task_Dat2FlotypeLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2FlotypeLoad(
    struct CacheProvider* provider,
    int flo_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2FlotypeLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_FlotypeHas(provider, flo_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2FlotypeLoad_VTable;
    strcpy(task->task.name, "Dat2FlotypeLoad");
    task->bc = dat2_buildcache;
    task->flo_id = flo_id;
    PT_INIT(&task->pt);
    return &task->task;
}
