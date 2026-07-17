#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_model_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2ModelLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int model_id;
};

static int
Task_Dat2ModelLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2ModelLoad* task = (struct Task_Dat2ModelLoad*)task_base;
    struct RSCache_Model* rscache_model = NULL;
    struct RSCache_Model* model_copy = NULL;
    struct ToriRS_Model* torirs_model = NULL;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ModelLoad(io, 0, task->model_id);
    PT_YIELD(&task->pt);

    rscache_model = RSCache_IO_Dat2ModelDecode(io, 0);
    if( !rscache_model )
    {
        fprintf(stderr, "Failed to decode dat2 model %d\n", task->model_id);
        PT_EXIT(&task->pt);
    }

    dat2_buildcache_model_add(task->bc, task->model_id, rscache_model);

    model_copy = RSCache_ModelNewCopy(rscache_model);
    assert(model_copy);
    torirs_model = ToriRS_ModelFromRSCache(model_copy);
    RSCache_ModelFree(model_copy);
    CacheProvider_ModelAdd(&task->bc->base, task->model_id, torirs_model);

    PT_END(&task->pt);
}

static void
Task_Dat2ModelLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2ModelLoad_VTable = {
    .run = Task_Dat2ModelLoad_Run,
    .free = Task_Dat2ModelLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2ModelLoad(
    struct CacheProvider* provider,
    int model_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2ModelLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_ModelHas(provider, model_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2ModelLoad_VTable;
    strcpy(task->task.name, "Dat2ModelLoad");
    task->bc = dat2_buildcache;
    task->model_id = model_id;
    PT_INIT(&task->pt);
    return &task->task;
}
