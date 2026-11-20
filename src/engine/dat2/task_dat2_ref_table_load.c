#include "engine/dat2/dat2_tasks.h"
#include "mem/torirs_mem.h"

#include "engine/asset_requests.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * ASSET_KIND_REF_TABLE's loader: one dat2 reference table, fetched and
 * decoded once while every task that needs it awaits it.
 *
 * It used to be a flag on the buildcache, set by whichever map square got
 * there first and cleared when its read came back. The square that set it
 * read the table inside its own task, so a square freed mid-read left the
 * flag set and its siblings waiting on it forever (Task_Dat2MapLoad_Free
 * never cleared it). The read is its own task now, and a task freed before
 * it ends reports itself abandoned: its awaiters wake, find the table still
 * missing and the entry wanted again, and ask once more.
 */

struct Task_Dat2RefTableLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int table_id;
    int ended;
};

static int
Task_Dat2RefTableLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IOBatch* io)
{
    struct Task_Dat2RefTableLoad* task = (struct Task_Dat2RefTableLoad*)task_base;
    struct RSCache_ReferenceTable* table = NULL;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2ReferenceTableLoad(io, 0, task->table_id);
    PT_YIELD(&task->pt);

    table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
    if( table )
        dat2_buildcache_reference_table_add(task->bc, task->table_id, table);
    else
        TORIRS_ERR("Failed to load reference table %d\n", task->table_id);
    task->ended = 1;
    Assets_Finished(
        task->bc->assets,
        AssetKey_Make(ASSET_KIND_REF_TABLE, task->table_id, 0),
        dat2_buildcache_reference_table_has(task->bc, task->table_id));

    PT_END(&task->pt);
}

static void
Task_Dat2RefTableLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2RefTableLoad* task = (struct Task_Dat2RefTableLoad*)task_base;

    if( !task->ended )
        Assets_Abandoned(task->bc->assets, AssetKey_Make(ASSET_KIND_REF_TABLE, task->table_id, 0));
    ToriMem_Free(ToriMem_Assets(), task);
}

static struct ToriRS_TaskVTable Task_Dat2RefTableLoad_VTable = {
    .run = Task_Dat2RefTableLoad_Run,
    .free = Task_Dat2RefTableLoad_Free,
};

static enum AssetStart
ref_table_start(
    void* data,
    struct Assets* assets,
    struct AssetKey key)
{
    struct Dat2BuildCache* bc = data;
    struct Task_Dat2RefTableLoad* task;

    (void)assets;
    assert(key.id >= 0);
    assert(key.id < RSCACHE_DAT2_TABLE_COUNT);
    if( dat2_buildcache_reference_table_has(bc, key.id) )
        return ASSET_START_HERE;
    assert(bc->base.asset_queue && "a reference table loads on the provider's asset queue");
    task = ToriMem_Calloc(ToriMem_Assets(), 1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2RefTableLoad_VTable;
    strcpy(task->task.name, "Dat2RefTableLoad");
    task->bc = bc;
    task->table_id = key.id;
    PT_INIT(&task->pt);
    ToriRS_TaskQueue_Add(bc->base.asset_queue, &task->task);
    return ASSET_START_LOADING;
}

static bool
ref_table_resident(
    void* data,
    struct AssetKey key)
{
    return dat2_buildcache_reference_table_has((struct Dat2BuildCache*)data, key.id);
}

void
Dat2BuildCache_AttachAssets(
    struct Dat2BuildCache* bc,
    struct Assets* assets)
{
    struct AssetLoader loader;

    assert(bc);
    assert(assets);
    loader.start = ref_table_start;
    loader.resident = ref_table_resident;
    loader.data = bc;
    Assets_SetLoader(assets, ASSET_KIND_REF_TABLE, &loader);
    bc->assets = assets;
}
