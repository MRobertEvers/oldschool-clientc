#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_paramtype_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2ParamLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int param_id;
};

static int
Task_Dat2ParamLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2ParamLoad* task = (struct Task_Dat2ParamLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct RSCache_Dat2ConfigParam entry = { 0 };
    struct ToriRS_ParamType* torirs = NULL;
    int found = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_PARAMS);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_PARAMS);
    if( !archive )
    {
        fprintf(
            stderr, "Failed to decode dat2 param config group for param %d\n", task->param_id);
        PT_EXIT(&task->pt);
    }

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(stderr, "Failed to filelist dat2 param group for param %d\n", task->param_id);
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( archive->file_ids[i] != task->param_id )
            continue;
        entry.id = task->param_id;
        RSCache_Dat2ConfigParamDecodeInplace(
            &entry, filelist->files[i], filelist->file_sizes[i]);
        found = 1;
        break;
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    /* Not in the config group: cache a default (int) ParamType anyway so the
     * host does not yield-and-reload this id forever. A zeroed entry decodes to
     * an int-typed param with default 0. */
    if( !found )
        entry.id = task->param_id;

    torirs = ToriRS_ParamTypeFromRSCacheDat2(task->param_id, &entry);
    if( !torirs )
    {
        RSCache_Dat2ConfigParamFreeInplace(&entry);
        fprintf(stderr, "Failed to convert dat2 param %d\n", task->param_id);
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
