#include "engine/dat1/dat1_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/torirs_spotanimtype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

struct Task_Dat1SpotanimLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int spotanim_id;
};

static int
Task_Dat1SpotanimLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1SpotanimLoad* task = (struct Task_Dat1SpotanimLoad*)task_base;
    struct RSCache_Dat1ConfigSpotanimList* list = NULL;
    struct ToriRS_Spotanimtype* torirs_spotanim = NULL;

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_config_jagfile(task->bc) )
    {
        struct RSCache_FileListDat* config_jagfile = NULL;

        RSCache_IO_Dat1ConfigJagfileLoad(io, 0);
        PT_YIELD(&task->pt);

        config_jagfile = RSCache_IO_Dat1ConfigJagfileDecode(io, 0);
        if( !config_jagfile )
        {
            TORIRS_ERR("Failed to decode dat1 config jagfile for spotanim %d\n",
                task->spotanim_id);
            PT_EXIT(&task->pt);
        }

        dat1_buildcache_set_config_jagfile(task->bc, config_jagfile);
    }

    list = dat1_buildcache_get_spotanim_list(task->bc);
    if( !list || task->spotanim_id < 0 || task->spotanim_id >= list->spotanims_count )
    {
        TORIRS_ERR("Failed to load dat1 spotanim %d\n", task->spotanim_id);
        PT_EXIT(&task->pt);
    }

    torirs_spotanim = ToriRS_SpotanimtypeFromRSCacheDat1(
        task->spotanim_id, &list->spotanims[task->spotanim_id]);
    if( !torirs_spotanim )
    {
        TORIRS_ERR("Failed to convert dat1 spotanim %d\n", task->spotanim_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_SpotanimtypeAdd(&task->bc->base, task->spotanim_id, torirs_spotanim);

    PT_END(&task->pt);
}

static void
Task_Dat1SpotanimLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1SpotanimLoad_VTable = {
    .run = Task_Dat1SpotanimLoad_Run,
    .free = Task_Dat1SpotanimLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat1SpotanimLoad(
    struct CacheProvider* provider,
    int spotanim_id)
{
    struct Dat1BuildCache* dat1_buildcache;
    struct Task_Dat1SpotanimLoad* task;

    assert(provider);

    dat1_buildcache = (struct Dat1BuildCache*)provider;
    if( CacheProvider_SpotanimtypeHas(provider, spotanim_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1SpotanimLoad_VTable;
    strcpy(task->task.name, "Dat1SpotanimLoad");
    task->bc = dat1_buildcache;
    task->spotanim_id = spotanim_id;
    PT_INIT(&task->pt);
    return &task->task;
}
