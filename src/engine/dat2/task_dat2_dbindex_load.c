#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_db.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DBTABLEINDEX (cache table 21): archive id = table id. Decodes every index
 * file in the archive (file 0 = master, file N = column N-1) into one
 * ToriRS_DbTableIndex bundle. A table with no index archive is cached as an
 * empty bundle so callers stop waiting and simply find nothing. */

struct Task_Dat2DbTableIndexLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int table_id;
};

static void
dbindex_add_empty(struct Task_Dat2DbTableIndexLoad* task)
{
    struct ToriRS_DbTableIndex* idx = calloc(1, sizeof(*idx));
    assert(idx);
    idx->table_id = task->table_id;
    CacheProvider_DbTableIndexAdd(&task->bc->base, task->table_id, idx);
}

static int
Task_Dat2DbTableIndexLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2DbTableIndexLoad* task = (struct Task_Dat2DbTableIndexLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct ToriRS_DbTableIndex* idx = NULL;

    PT_BEGIN(&task->pt);

    /* Table 21 holds db indexes in OldSchool and spotanims in RS2, so a branch without
     * the table has no index to find — the same answer as a table with no index archive. */
    if( !RSCache_IO_ProfileHasDat2Table(
            CacheProvider_Profile(&task->bc->base), RSCACHE_DAT2_TABLE_DBTABLE_INDEX) )
    {
        dbindex_add_empty(task);
        PT_EXIT(&task->pt);
    }

    RSCache_IO_Dat2DbTableIndexLoad(io, 0, task->table_id);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2DbTableIndexDecode(io, 0);
    if( !archive || !archive->file_ids || archive->file_count <= 0 )
    {
        /* No index for this table — cache an empty bundle. */
        if( archive )
            RSCache_Dat2DiskArchiveFree(archive);
        dbindex_add_empty(task);
        PT_EXIT(&task->pt);
    }

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist || filelist->file_count <= 0 )
    {
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        dbindex_add_empty(task);
        PT_EXIT(&task->pt);
    }

    idx = calloc(1, sizeof(*idx));
    assert(idx);
    idx->table_id = task->table_id;
    idx->file_count = filelist->file_count;
    idx->files = calloc((size_t)idx->file_count, sizeof(*idx->files));
    idx->file_ids = malloc((size_t)idx->file_count * sizeof(int));
    assert(idx->files && idx->file_ids);

    for( int i = 0; i < filelist->file_count; i++ )
    {
        idx->file_ids[i] = archive->file_ids[i];
        RSCache_Dat2DbIndexFileDecodeInplace(
            &idx->files[i], archive->file_ids[i], filelist->files[i], filelist->file_sizes[i]);
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    CacheProvider_DbTableIndexAdd(&task->bc->base, task->table_id, idx);

    PT_END(&task->pt);
}

static void
Task_Dat2DbTableIndexLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2DbTableIndexLoad_VTable = {
    .run = Task_Dat2DbTableIndexLoad_Run,
    .free = Task_Dat2DbTableIndexLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2DbTableIndexLoad(
    struct CacheProvider* provider,
    int table_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2DbTableIndexLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( table_id < 0 || CacheProvider_DbTableIndexHas(provider, table_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2DbTableIndexLoad_VTable;
    strcpy(task->task.name, "Dat2DbTableIndexLoad");
    task->bc = dat2_buildcache;
    task->table_id = table_id;
    PT_INIT(&task->pt);
    return &task->task;
}
