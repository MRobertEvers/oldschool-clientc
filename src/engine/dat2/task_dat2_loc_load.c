#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_location_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2LocLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int loc_id;
};

static int
Task_Dat2LocLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2LocLoad* task = (struct Task_Dat2LocLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_Dat2ConfigLoc* rscache_loc = NULL;
    struct ToriRS_Location* torirs_loc = NULL;

    PT_BEGIN(&task->pt);

    /* The whole group decodes on first touch; later tasks skip the archive
     * load entirely instead of re-decompressing it per id. */
    if( !dat2_buildcache_loc_get(task->bc, task->loc_id) )
    {
        RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_LOCS);
        PT_YIELD(&task->pt);

        archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_LOCS);
        if( !archive )
        {
            fprintf(stderr, "Failed to decode dat2 loc config group for loc %d\n", task->loc_id);
            PT_EXIT(&task->pt);
        }

        dat2_buildcache_locs_init_from_archive(task->bc, archive, NULL, 0);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    rscache_loc = dat2_buildcache_loc_get(task->bc, task->loc_id);
    if( !rscache_loc )
    {
        fprintf(stderr, "Failed to load dat2 loc %d\n", task->loc_id);
        PT_EXIT(&task->pt);
    }

    torirs_loc = ToriRS_LocationFromRSCacheDat2(task->loc_id, rscache_loc);
    if( !torirs_loc )
    {
        fprintf(stderr, "Failed to convert dat2 loc %d\n", task->loc_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_LocationAdd(&task->bc->base, task->loc_id, torirs_loc);

    PT_END(&task->pt);
}

static void
Task_Dat2LocLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2LocLoad_VTable = {
    .run = Task_Dat2LocLoad_Run,
    .free = Task_Dat2LocLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2LocLoad(
    struct CacheProvider* provider,
    int loc_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2LocLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_LocationHas(provider, loc_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2LocLoad_VTable;
    strcpy(task->task.name, "Dat2LocLoad");
    task->bc = dat2_buildcache;
    task->loc_id = loc_id;
    PT_INIT(&task->pt);
    return &task->task;
}
