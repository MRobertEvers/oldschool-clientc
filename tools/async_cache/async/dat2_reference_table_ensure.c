#include "../../../src2/core/tapi/tapi_dat2.h"
#include "../../../src2/ioqueue/libtorirs_io.h"
#include "../async_cache_tasks.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_AsyncCacheDat2_ReferenceTable_Ensure
{
    struct LibToriRS_Task base;
    struct pt pt;
    int table_id;
    struct CacheDat2* cachedat2;
    void* user;
    ReferenceTableLoadCallback callback;
};

static void
Task_AsyncCacheDat2_ReferenceTable_Ensure_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static int
Task_AsyncCacheDat2_ReferenceTable_Ensure_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_AsyncCacheDat2_ReferenceTable_Ensure* task;
    struct RSCacheDat2Disk_ReferenceTable* reference_table;

    task = LibToriRS_container_of(base, struct Task_AsyncCacheDat2_ReferenceTable_Ensure, base);

    PT_BEGIN(&task->pt);

    reference_table = CacheDat2_ReferenceTable_Get(task->cachedat2, task->table_id);
    if( !reference_table )
    {
        IO_REQUEST(ctx, 0, TAPIDat2_FetchReferenceTable(ctx, task->table_id));
        PT_YIELD(&task->pt);
        reference_table = TAPIDat2_DecodeReferenceTable(ctx, 0, task->table_id);

        CacheDat2_ReferenceTable_Add(task->cachedat2, task->table_id, reference_table);
        LibToriRS_IOQueueClear(ctx->io);
    }

    task->callback(task->user, reference_table);

    PT_END(&task->pt);
}

static struct LibToriRS_TaskVTable g_vtable = {
    .run_fn = Task_AsyncCacheDat2_ReferenceTable_Ensure_Run,
    .free_fn = Task_AsyncCacheDat2_ReferenceTable_Ensure_Free,
};

struct LibToriRS_Task*
Task_AsyncCacheDat2_ReferenceTable_Ensure_New(
    int table_id,
    struct CacheDat2* cachedat2,
    void* user,
    ReferenceTableLoadCallback callback)
{
    struct Task_AsyncCacheDat2_ReferenceTable_Ensure* task =
        malloc(sizeof(struct Task_AsyncCacheDat2_ReferenceTable_Ensure));
    memset(task, 0, sizeof(struct Task_AsyncCacheDat2_ReferenceTable_Ensure));
    task->base.vtable = &g_vtable;

    task->table_id = table_id;
    task->cachedat2 = cachedat2;
    task->user = user;
    task->callback = callback;
    PT_INIT(&task->pt);
    return &task->base;
}
