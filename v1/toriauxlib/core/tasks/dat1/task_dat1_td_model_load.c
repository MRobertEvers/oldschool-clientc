#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat1_buildcache.h"
#include "core/tapi/tapi_dat1.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/td/toriauxlibtd.h"

#include <stdio.h>
#include <stdlib.h>

struct Task_Dat1TdModelLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibTD* tdx;
    int model_id;
};

static int
Task_Dat1TdModelLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1TdModelLoad* task =
        LibToriRS_container_of(base, struct Task_Dat1TdModelLoad, base);
    struct RSCacheDat2A_Model* model;

    PT_BEGIN(&task->pt);

    IO_REQUEST(ctx, 0, TAPIDat1_FetchModel(ctx, task->model_id));
    PT_YIELD(&task->pt);

    model = TAPIDat1_DecodeModel(ctx, 0);
    if( !model )
    {
        fprintf(stderr, "Task_Dat1TdModelLoad: failed to decode model %d\n", task->model_id);
        PT_EXIT(&task->pt);
    }

    dat1_buildcache_model_add(dat1(ToriAuxLibTD_C(task->tdx)), task->model_id, model);
    ToriAuxLibTD_SubmitModelFromDat1(task->tdx, task->model_id);
    ToriAuxLibTD_Model(task->tdx, task->model_id);

    PT_END(&task->pt);
}

static void
Task_Dat1TdModelLoad_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat1_td_model_load_vtable = {
    .run_fn = Task_Dat1TdModelLoad_Run,
    .free_fn = Task_Dat1TdModelLoad_Free,
};

struct LibToriRS_Task*
Task_Dat1TdModelLoad_New(
    struct ToriAuxLibTD* tdx,
    int model_id)
{
    struct Task_Dat1TdModelLoad* task = calloc(1, sizeof(struct Task_Dat1TdModelLoad));
    if( !task )
        return NULL;
    task->base.vtable = &g_task_dat1_td_model_load_vtable;
    task->tdx = tdx;
    task->model_id = model_id;
    PT_INIT(&task->pt);
    return &task->base;
}
