#include "engine/dat1/dat1_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/torirs_objtype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

struct Task_Dat1ObjLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int obj_id;
};

static int
Task_Dat1ObjLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1ObjLoad* task = (struct Task_Dat1ObjLoad*)task_base;
    struct RSCache_Dat1ConfigObj* rscache_obj = NULL;
    struct ToriRS_Objtype* torirs_obj = NULL;

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_config_jagfile(task->bc) )
    {
        struct RSCache_FileListDat* config_jagfile = NULL;

        RSCache_IO_Dat1ConfigJagfileLoad(io, 0);
        PT_YIELD(&task->pt);

        config_jagfile = RSCache_IO_Dat1ConfigJagfileDecode(io, 0);
        if( !config_jagfile )
        {
            TORIRS_ERR("Failed to decode dat1 config jagfile for obj %d\n", task->obj_id);
            PT_EXIT(&task->pt);
        }

        dat1_buildcache_set_config_jagfile(task->bc, config_jagfile);
    }

    rscache_obj = dat1_buildcache_obj_load_from_config_jagfile(task->bc, task->obj_id);
    if( !rscache_obj )
    {
        TORIRS_ERR("Failed to load dat1 obj %d\n", task->obj_id);
        PT_EXIT(&task->pt);
    }

    torirs_obj = ToriRS_ObjtypeFromRSCacheDat1(task->obj_id, rscache_obj);
    if( !torirs_obj )
    {
        TORIRS_ERR("Failed to convert dat1 obj %d\n", task->obj_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_ObjtypeAdd(&task->bc->base, task->obj_id, torirs_obj);

    PT_END(&task->pt);
}

static void
Task_Dat1ObjLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1ObjLoad_VTable = {
    .run = Task_Dat1ObjLoad_Run,
    .free = Task_Dat1ObjLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat1ObjLoad(
    struct CacheProvider* provider,
    int obj_id)
{
    struct Dat1BuildCache* dat1_buildcache;
    struct Task_Dat1ObjLoad* task;

    assert(provider);

    dat1_buildcache = (struct Dat1BuildCache*)provider;
    if( CacheProvider_ObjtypeHas(provider, obj_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1ObjLoad_VTable;
    strcpy(task->task.name, "Dat1ObjLoad");
    task->bc = dat1_buildcache;
    task->obj_id = obj_id;
    PT_INIT(&task->pt);
    return &task->task;
}
