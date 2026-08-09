#include "../../../src2/core/tapi/tapi_dat2.h"
#include "../../../src2/ioqueue/libtorirs_io.h"
#include "../async_cache_tasks.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_AsyncCacheDat2_Model_Load
{
    struct LibToriRS_Task base;
    struct pt pt;
    int id;
    struct CacheDat2* cachedat2;
};

static int
Task_AsyncCacheDat2_Model_Load_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_AsyncCacheDat2_Model_Load* task =
        LibToriRS_container_of(base, struct Task_AsyncCacheDat2_Model_Load, base);

    struct RSCacheDat2A_Model* model;

    PT_BEGIN(&task->pt);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchModel(ctx, task->id));
    PT_YIELD(&task->pt);
    model = TAPIDat2_DecodeModel(ctx, 0);

    CacheDat2_Model_Add(task->cachedat2, task->id, model);

    PT_END(&task->pt);
}

static void
Task_AsyncCacheDat2_Model_Load_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_vtable = {
    .run_fn = Task_AsyncCacheDat2_Model_Load_Run,
    .free_fn = Task_AsyncCacheDat2_Model_Load_Free,
};

struct LibToriRS_Task*
Task_AsyncCacheDat2_Model_Load_New(
    int id,
    struct CacheDat2* cachedat2)
{
    struct Task_AsyncCacheDat2_Model_Load* task =
        malloc(sizeof(struct Task_AsyncCacheDat2_Model_Load));
    memset(task, 0, sizeof(struct Task_AsyncCacheDat2_Model_Load));
    task->base.vtable = &g_vtable;
    task->id = id;
    task->cachedat2 = cachedat2;
    PT_INIT(&task->pt);
    return &task->base;
}
