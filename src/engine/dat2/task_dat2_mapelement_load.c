#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_group_await.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/torirs_worldmap_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

struct Task_Dat2MapElementLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int element_id;
    /* Borrowed from the buildcache's split-group LRU — never freed here. */
    struct Dat2Group const* group;
};

static int
Task_Dat2MapElementLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2MapElementLoad* task = (struct Task_Dat2MapElementLoad*)task_base;
    struct RSCache_MapElement entry = { 0 };
    struct ToriRS_MapElement* torirs = NULL;
    int pos;

    PT_BEGIN(&task->pt);

    DAT2_GROUP_AWAIT(
        &task->pt, io, 0, task->bc->group_cache, RSCACHE_DAT2_TABLE_CONFIGS,
        RSCACHE_DAT2_CONFIG_KIND_AREA, task->group);

    if( !task->group )
    {
        TORIRS_ERR("Failed to decode dat2 map element config group for element %d\n",
            task->element_id);
        PT_EXIT(&task->pt);
    }

    pos = Dat2Group_IndexOf(task->group, task->element_id);
    if( pos < 0 )
    {
        TORIRS_ERR("Failed to find dat2 map element %d in config group\n", task->element_id);
        PT_EXIT(&task->pt);
    }

    entry.id = task->element_id;
    RSCache_MapElementDecodeInplace(
        &entry, task->group->filelist->files[pos], task->group->filelist->file_sizes[pos]);

    torirs = ToriRS_MapElementFromRSCacheDat2(task->element_id, &entry);
    RSCache_MapElementFreeInplace(&entry);

    CacheProvider_MapElementAdd(&task->bc->base, task->element_id, torirs);

    PT_END(&task->pt);
}

static void
Task_Dat2MapElementLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2MapElementLoad_VTable = {
    .run = Task_Dat2MapElementLoad_Run,
    .free = Task_Dat2MapElementLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2MapElementLoad(
    struct CacheProvider* provider,
    int element_id)
{
    struct Task_Dat2MapElementLoad* task;

    assert(provider);

    if( element_id < 0 || CacheProvider_MapElementHas(provider, element_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2MapElementLoad_VTable;
    strcpy(task->task.name, "Dat2MapElementLoad");
    task->bc = (struct Dat2BuildCache*)provider;
    task->element_id = element_id;
    PT_INIT(&task->pt);
    return &task->task;
}
