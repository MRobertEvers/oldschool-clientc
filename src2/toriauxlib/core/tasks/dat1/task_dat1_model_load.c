#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat1_buildcache.h"
#include "core/tapi/tapi_dat1.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat1ModelLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* c;
    int model_id;
};

static int
Task_Dat1ModelLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1ModelLoad* task =
        LibToriRS_container_of(base, struct Task_Dat1ModelLoad, base);
    struct RSCacheDat2A_Model* model;

    PT_BEGIN(&task->pt);

    IO_REQUEST(ctx, 0, TAPIDat1_FetchModel(ctx, task->model_id));
    PT_YIELD(&task->pt);

    model = TAPIDat1_DecodeModel(ctx, 0);
    if( !model )
    {
        fprintf(stderr, "Failed to decode model\n");
        PT_EXIT(&task->pt);
    }

    dat1_buildcache_model_add(dat1(task->c), task->model_id, model);
    ToriAuxLibCache_SubmitModelFromDat1(task->c, task->model_id);

    PT_END(&task->pt);
}

static void
Task_Dat1ModelLoad_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat1_model_load_vtable = {
    .run_fn = Task_Dat1ModelLoad_Run,
    .free_fn = Task_Dat1ModelLoad_Free,
};

struct LibToriRS_Task*
Task_Dat1ModelLoad_New(
    struct ToriAuxLibCache* c,
    int model_id)
{
    struct Task_Dat1ModelLoad* task = malloc(sizeof(struct Task_Dat1ModelLoad));
    assert(task);
    memset(task, 0, sizeof(struct Task_Dat1ModelLoad));
    task->base.vtable = &g_task_dat1_model_load_vtable;
    task->c = c;
    task->model_id = model_id;
    PT_INIT(&task->pt);
    return &task->base;
}
