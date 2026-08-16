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

/* DBROW config group (config kind 38). Same shape as the enum load task: read
 * the whole config group through the split-group LRU, find the file whose id is
 * the row id, decode it into the rscache DBROW struct, and hand it to the
 * provider.
 *
 * Going through the LRU rather than decoding the group here matters more for
 * DBROW than for anything else: a script that walks a table asks for one row at
 * a time, and each ask used to decrypt, decompress and re-split the group that
 * holds every row in the cache. */

struct Task_Dat2DbRowLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int row_id;
    /* Borrowed from the buildcache's split-group LRU — never freed here. */
    struct Dat2Group const* group;
};

static int
Task_Dat2DbRowLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2DbRowLoad* task = (struct Task_Dat2DbRowLoad*)task_base;
    struct RSCache_Dat2ConfigDbRow* row = NULL;
    int pos;

    PT_BEGIN(&task->pt);

    DAT2_GROUP_AWAIT(
        &task->pt, io, 0, task->bc->group_cache, RSCACHE_DAT2_TABLE_CONFIGS,
        RSCACHE_DAT2_CONFIG_KIND_DBROW, task->group);

    if( !task->group )
    {
        fprintf(stderr, "Failed to decode dat2 dbrow config group for row %d\n", task->row_id);
        PT_EXIT(&task->pt);
    }

    pos = Dat2Group_IndexOf(task->group, task->row_id);
    if( pos < 0 )
    {
        fprintf(stderr, "Failed to find dat2 dbrow %d in config group\n", task->row_id);
        PT_EXIT(&task->pt);
    }

    row = calloc(1, sizeof(*row));
    assert(row);
    row->id = task->row_id;
    RSCache_Dat2ConfigDbRowDecodeInplace(
        row, task->group->filelist->files[pos], task->group->filelist->file_sizes[pos]);

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
