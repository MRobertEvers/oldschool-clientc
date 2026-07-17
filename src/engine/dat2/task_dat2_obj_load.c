#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_objtype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2ObjLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int obj_id;
};

static int
Task_Dat2ObjLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2ObjLoad* task = (struct Task_Dat2ObjLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_Dat2ConfigObj* rscache_obj = NULL;
    struct ToriRS_Objtype* torirs_obj = NULL;
    int wanted_id = task->obj_id;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_OBJECT);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_OBJECT);
    if( !archive )
    {
        fprintf(stderr, "Failed to decode dat2 object config group for obj %d\n", task->obj_id);
        PT_EXIT(&task->pt);
    }

    dat2_buildcache_objects_init_from_archive(task->bc, archive, &wanted_id, 1);
    RSCache_Dat2DiskArchiveFree(archive);

    rscache_obj = dat2_buildcache_object_get(task->bc, task->obj_id);
    if( !rscache_obj )
    {
        fprintf(stderr, "Failed to load dat2 obj %d\n", task->obj_id);
        PT_EXIT(&task->pt);
    }

    torirs_obj = ToriRS_ObjtypeFromRSCacheDat2(task->obj_id, rscache_obj);
    if( !torirs_obj )
    {
        fprintf(stderr, "Failed to convert dat2 obj %d\n", task->obj_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_ObjtypeAdd(&task->bc->base, task->obj_id, torirs_obj);

    PT_END(&task->pt);
}

static void
Task_Dat2ObjLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2ObjLoad_VTable = {
    .run = Task_Dat2ObjLoad_Run,
    .free = Task_Dat2ObjLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2ObjLoad(
    struct CacheProvider* provider,
    int obj_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2ObjLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_ObjtypeHas(provider, obj_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2ObjLoad_VTable;
    strcpy(task->task.name, "Dat2ObjLoad");
    task->bc = dat2_buildcache;
    task->obj_id = obj_id;
    PT_INIT(&task->pt);
    return &task->task;
}
