#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/torirs_worldmap_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2MapElementLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int element_id;
};

static int
Task_Dat2MapElementLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2MapElementLoad* task = (struct Task_Dat2MapElementLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct RSCache_MapElement entry = { 0 };
    struct ToriRS_MapElement* torirs = NULL;
    int found = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_AREA);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_AREA);
    if( !archive )
    {
        fprintf(
            stderr,
            "Failed to decode dat2 map element config group for element %d\n",
            task->element_id);
        PT_EXIT(&task->pt);
    }

    filelist =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(
            stderr,
            "Failed to filelist dat2 map element group for element %d\n",
            task->element_id);
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( archive->file_ids[i] != task->element_id )
            continue;
        entry.id = task->element_id;
        RSCache_MapElementDecodeInplace(&entry, filelist->files[i], filelist->file_sizes[i]);
        found = 1;
        break;
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    if( !found )
    {
        fprintf(stderr, "Failed to find dat2 map element %d in config group\n", task->element_id);
        PT_EXIT(&task->pt);
    }

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
