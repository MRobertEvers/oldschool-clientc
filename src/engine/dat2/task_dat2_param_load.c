#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_group_await.h"
#include "engine/torirs_paramtype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

struct Task_Dat2ParamLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int param_id;
    /* Borrowed from the buildcache's split-group LRU — never freed here. */
    struct Dat2Group const* group;
};

static int
Task_Dat2ParamLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2ParamLoad* task = (struct Task_Dat2ParamLoad*)task_base;
    struct RSCache_Dat2ConfigParam entry = { 0 };
    struct ToriRS_ParamType* torirs = NULL;
    int pos = -1;

    PT_BEGIN(&task->pt);

    DAT2_GROUP_AWAIT(
        &task->pt, io, 0, task->bc->group_cache, RSCACHE_DAT2_TABLE_CONFIGS,
        RSCACHE_DAT2_CONFIG_KIND_PARAMS, task->group);

    if( !task->group )
    {
        TORIRS_ERR("Failed to decode dat2 param config group for param %d\n", task->param_id);
        PT_EXIT(&task->pt);
    }

    pos = Dat2Group_IndexOf(task->group, task->param_id);
    if( pos >= 0 )
    {
        entry.id = task->param_id;
        RSCache_Dat2ConfigParamDecodeInplace(
            &entry, task->group->filelist->files[pos], task->group->filelist->file_sizes[pos]);
    }
    else
    {
        /* Not in the config group: cache a default (int) ParamType anyway so the
         * host does not yield-and-reload this id forever. A zeroed entry decodes
         * to an int-typed param with default 0. */
        entry.id = task->param_id;
    }

    torirs = ToriRS_ParamTypeFromRSCacheDat2(task->param_id, &entry);
    if( !torirs )
    {
        RSCache_Dat2ConfigParamFreeInplace(&entry);
        TORIRS_ERR("Failed to convert dat2 param %d\n", task->param_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_ParamAdd(&task->bc->base, task->param_id, torirs);

    PT_END(&task->pt);
}

static void
Task_Dat2ParamLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2ParamLoad_VTable = {
    .run = Task_Dat2ParamLoad_Run,
    .free = Task_Dat2ParamLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2ParamLoad(
    struct CacheProvider* provider,
    int param_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2ParamLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_ParamHas(provider, param_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2ParamLoad_VTable;
    strcpy(task->task.name, "Dat2ParamLoad");
    task->bc = dat2_buildcache;
    task->param_id = param_id;
    PT_INIT(&task->pt);
    return &task->task;
}
