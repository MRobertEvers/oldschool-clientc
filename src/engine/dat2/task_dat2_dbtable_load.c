#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DBTABLE config group (config kind 39). Same shape as the DBROW load task: read
 * the whole config group, find the file whose id is the table id, decode it into
 * the rscache DBTABLE struct, and hand it to the provider.
 *
 * A DBROW lists only the columns it sets. Everything about a column a row does
 * *not* list — its arity, its field types and its default values — is stated
 * here and nowhere else, so DB_GETFIELD cannot answer for a missing column
 * without this record. */

struct Task_Dat2DbTableLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int table_id;
};

static int
Task_Dat2DbTableLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2DbTableLoad* task = (struct Task_Dat2DbTableLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct RSCache_Dat2ConfigDbTable* table = NULL;
    int found = 0;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ConfigGroupLoad(io, 0, RSCACHE_DAT2_CONFIG_KIND_DBTABLE);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2ConfigGroupDecode(io, 0, RSCACHE_DAT2_CONFIG_KIND_DBTABLE);
    if( !archive )
    {
        fprintf(
            stderr, "Failed to decode dat2 dbtable config group for table %d\n", task->table_id);
        PT_EXIT(&task->pt);
    }

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist || !archive->file_ids )
    {
        fprintf(stderr, "Failed to filelist dat2 dbtable group for table %d\n", task->table_id);
        RSCache_FileListFree(filelist);
        RSCache_Dat2DiskArchiveFree(archive);
        PT_EXIT(&task->pt);
    }

    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( archive->file_ids[i] != task->table_id )
            continue;
        table = calloc(1, sizeof(*table));
        assert(table);
        table->id = task->table_id;
        RSCache_Dat2ConfigDbTableDecodeInplace(
            table, filelist->files[i], filelist->file_sizes[i]);
        found = 1;
        break;
    }

    RSCache_FileListFree(filelist);
    RSCache_Dat2DiskArchiveFree(archive);

    if( !found )
    {
        fprintf(stderr, "Failed to find dat2 dbtable %d in config group\n", task->table_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_DbTableAdd(&task->bc->base, task->table_id, table);

    PT_END(&task->pt);
}

static void
Task_Dat2DbTableLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2DbTableLoad_VTable = {
    .run = Task_Dat2DbTableLoad_Run,
    .free = Task_Dat2DbTableLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2DbTableLoad(
    struct CacheProvider* provider,
    int table_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2DbTableLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( table_id < 0 || CacheProvider_DbTableHas(provider, table_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2DbTableLoad_VTable;
    strcpy(task->task.name, "Dat2DbTableLoad");
    task->bc = dat2_buildcache;
    task->table_id = table_id;
    PT_INIT(&task->pt);
    return &task->task;
}
