#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DBROW config group (config kind 38). Same shape as the enum load task: read
 * the whole config group, find the file whose id is the row id, decode it into
 * the rscache DBROW struct, and hand it to the provider. */

struct Task_Dat2DbRowLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int row_id;
};

static int
Task_Dat2DbRowLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2DbRowLoad* task = (struct Task_Dat2DbRowLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct RSCache_Dat2ConfigDbRow* row = NULL;
    int found = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_DBROW);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_DBROW);
    if( !archive )
    {
        fprintf(stderr, "Failed to decode dat2 dbrow config group for row %d\n", task->row_id);
        PT_EXIT(&task->pt);
    }

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(stderr, "Failed to filelist dat2 dbrow group for row %d\n", task->row_id);
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( archive->file_ids[i] != task->row_id )
            continue;
        row = calloc(1, sizeof(*row));
        assert(row);
        row->id = task->row_id;
        RSCache_Dat2ConfigDbRowDecodeInplace(
            row, filelist->files[i], filelist->file_sizes[i]);
        found = 1;
        break;
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    if( !found )
    {
        fprintf(stderr, "Failed to find dat2 dbrow %d in config group\n", task->row_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_DbRowAdd(&task->bc->base, task->row_id, row);

    PT_END(&task->pt);
}

static void
Task_Dat2DbRowLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2DbRowLoad_VTable = {
    .run = Task_Dat2DbRowLoad_Run,
    .free = Task_Dat2DbRowLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2DbRowLoad(
    struct CacheProvider* provider,
    int row_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2DbRowLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( row_id < 0 || CacheProvider_DbRowHas(provider, row_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2DbRowLoad_VTable;
    strcpy(task->task.name, "Dat2DbRowLoad");
    task->bc = dat2_buildcache;
    task->row_id = row_id;
    PT_INIT(&task->pt);
    return &task->task;
}
