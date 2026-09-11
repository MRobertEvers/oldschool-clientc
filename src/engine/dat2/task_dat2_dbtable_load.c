#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/dat2/dat2_group_await.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/* DBTABLE config group (config kind 39). Same shape as the DBROW load task: read
 * the whole config group through the split-group LRU, find the file whose id is
 * the table id, decode it into the rscache DBTABLE struct, and hand it to the
 * provider.
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
    /* Borrowed from the buildcache's split-group LRU — never freed here. */
    struct Dat2Group const* group;
};

static int
Task_Dat2DbTableLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2DbTableLoad* task = (struct Task_Dat2DbTableLoad*)task_base;
    struct RSCache_Dat2ConfigDbTable* table = NULL;
    int pos;

    PT_BEGIN(&task->pt);

    DAT2_GROUP_AWAIT(
        &task->pt, io, 0, task->bc->group_cache, RSCACHE_DAT2_TABLE_CONFIGS,
        RSCACHE_DAT2_CONFIG_KIND_DBTABLE, task->group);

    if( !task->group )
    {
        TORIRS_ERR("Failed to decode dat2 dbtable config group for table %d\n", task->table_id);
        PT_EXIT(&task->pt);
    }

    pos = Dat2Group_IndexOf(task->group, task->table_id);
    if( pos < 0 )
    {
        TORIRS_ERR("Failed to find dat2 dbtable %d in config group\n", task->table_id);
        PT_EXIT(&task->pt);
    }

    table = calloc(1, sizeof(*table));
    assert(table);
    table->id = task->table_id;
    RSCache_Dat2ConfigDbTableDecodeInplace(
        table, task->group->filelist->files[pos], task->group->filelist->file_sizes[pos]);

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
