#ifndef TORIAUXLIB_TASK_DAT2_MODELVIEWER_MODEL_H
#define TORIAUXLIB_TASK_DAT2_MODELVIEWER_MODEL_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "../../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_dat2.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2ModelLoad
{
    struct pt thread;
    struct ToriAuxLibCache* c;
    int model_id;
};

struct Task_Dat2ModelLoad*
Task_Dat2ModelLoad_New(
    struct ToriAuxLibCache* c,
    int model_id)
{
    struct Task_Dat2ModelLoad* task = malloc(sizeof(struct Task_Dat2ModelLoad));
    assert(task);
    memset(task, 0, sizeof(struct Task_Dat2ModelLoad));
    PT_INIT(&task->thread);
    task->c = c;
    task->model_id = model_id;
    return task;
}

void
Task_Dat2ModelLoad_Free(struct Task_Dat2ModelLoad* task)
{
    if( !task )
        return;
    free(task);
}

int
Task_Dat2ModelLoad_Run(
    struct Task_Dat2ModelLoad* task,
    struct LibToriRS_IOContext* ctx)
{
    struct RSCacheDat2A_Model* model;
    PT_BEGIN(&task->thread);

    IO_REQUEST(ctx, 0, TAPIDat2_FetchModel(ctx, task->model_id));

    PT_YIELD(&task->thread);

    model = TAPIDat2_DecodeModel(ctx, 0);
    if( !model )
    {
        fprintf(stderr, "Failed to decode dat2 model\n");
        PT_EXIT(&task->thread);
    }

    dat2_buildcache_model_add(dat2(task->c), task->model_id, model);
    ToriAuxLibCache_SubmitModelFromDat2(task->c, task->model_id);

    PT_END(&task->thread);
}

#endif
