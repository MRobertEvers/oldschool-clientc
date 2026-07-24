#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_objtype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <rscache.h>
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

    PT_BEGIN(&task->pt);

    /* The whole group decodes on first touch; later tasks skip the archive
     * load entirely instead of re-decompressing it per id. */
    if( !dat2_buildcache_object_get(task->bc, task->obj_id) )
    {
        RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_OBJECT);
        PT_YIELD(&task->pt);

        archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_OBJECT);
        if( !archive )
        {
            fprintf(
                stderr, "Failed to decode dat2 object config group for obj %d\n", task->obj_id);
            PT_EXIT(&task->pt);
        }

        dat2_buildcache_objects_init_from_archive(task->bc, archive, NULL, 0);
        RSCache_Dat2DiskArchiveFree(archive);
    }

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

/* --- Bulk load-all: decode every objtype in the config group in one pass. Backs
 * the OC_FIND item-name search, which needs every name resident. The group is a
 * single archive, so this is one decompress plus N cheap conversions. --- */

struct Task_Dat2ObjLoadAll
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
};

static int
Task_Dat2ObjLoadAll_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* cache_io)
{
    struct Task_Dat2ObjLoadAll* task = (struct Task_Dat2ObjLoadAll*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    int idx;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(cache_io, 0, RSCACHE_DAT2_CONFIG_KIND_OBJECT);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(cache_io, 0, RSCACHE_DAT2_CONFIG_KIND_OBJECT);
    if( !archive )
    {
        fprintf(stderr, "Failed to decode dat2 object config group for load-all\n");
        PT_EXIT(&task->pt);
    }

    /* wanted_ids == NULL decodes the whole group into the buildcache. */
    dat2_buildcache_objects_init_from_archive(task->bc, archive, NULL, 0);

    for( idx = 0; idx < archive->file_count; idx++ )
    {
        int obj_id = archive->file_ids[idx];
        struct RSCache_Dat2ConfigObj* rscache_obj;
        struct ToriRS_Objtype* torirs_obj;

        if( CacheProvider_ObjtypeHas(&task->bc->base, obj_id) )
            continue;
        rscache_obj = dat2_buildcache_object_get(task->bc, obj_id);
        if( !rscache_obj )
            continue;
        torirs_obj = ToriRS_ObjtypeFromRSCacheDat2(obj_id, rscache_obj);
        if( !torirs_obj )
            continue;
        CacheProvider_ObjtypeAdd(&task->bc->base, obj_id, torirs_obj);
    }

    RSCache_Dat2DiskArchiveFree(archive);
    task->bc->base.objtypes_all_loaded = true;

    PT_END(&task->pt);
}

static void
Task_Dat2ObjLoadAll_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2ObjLoadAll_VTable = {
    .run = Task_Dat2ObjLoadAll_Run,
    .free = Task_Dat2ObjLoadAll_Free,
};

struct ToriRS_Task*
CreateTask_Dat2ObjLoadAll(struct CacheProvider* provider)
{
    struct Task_Dat2ObjLoadAll* task;

    assert(provider);

    if( provider->objtypes_all_loaded )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2ObjLoadAll_VTable;
    strcpy(task->task.name, "Dat2ObjLoadAll");
    task->bc = (struct Dat2BuildCache*)provider;
    PT_INIT(&task->pt);
    return &task->task;
}
