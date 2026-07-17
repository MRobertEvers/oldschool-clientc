#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_idk_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2IdkLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int idk_id;
};

static int
Task_Dat2IdkLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2IdkLoad* task = (struct Task_Dat2IdkLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_Dat2ConfigIdk* rscache_idk = NULL;
    struct ToriRS_Idk* torirs_idk = NULL;
    int wanted_id = task->idk_id;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_IDENTKIT);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_IDENTKIT);
    if( !archive )
    {
        fprintf(stderr, "Failed to decode dat2 identkit config group for idk %d\n", task->idk_id);
        PT_EXIT(&task->pt);
    }

    dat2_buildcache_identkits_init_from_archive(task->bc, archive, &wanted_id, 1);
    RSCache_Dat2DiskArchiveFree(archive);

    rscache_idk = dat2_buildcache_identkit_get(task->bc, task->idk_id);
    if( !rscache_idk )
    {
        fprintf(stderr, "Failed to load dat2 idk %d\n", task->idk_id);
        PT_EXIT(&task->pt);
    }

    torirs_idk = ToriRS_IdkFromRSCacheDat2(task->idk_id, rscache_idk);
    if( !torirs_idk )
    {
        fprintf(stderr, "Failed to convert dat2 idk %d\n", task->idk_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_IdkAdd(&task->bc->base, task->idk_id, torirs_idk);

    PT_END(&task->pt);
}

static void
Task_Dat2IdkLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2IdkLoad_VTable = {
    .run = Task_Dat2IdkLoad_Run,
    .free = Task_Dat2IdkLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2IdkLoad(
    struct CacheProvider* provider,
    int idk_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2IdkLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_IdkHas(provider, idk_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2IdkLoad_VTable;
    strcpy(task->task.name, "Dat2IdkLoad");
    task->bc = dat2_buildcache;
    task->idk_id = idk_id;
    PT_INIT(&task->pt);
    return &task->task;
}
