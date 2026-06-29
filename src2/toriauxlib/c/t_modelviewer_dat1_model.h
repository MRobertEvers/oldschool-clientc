#ifndef TORIAUXLIBCACHE_T_DAT1_MODELVIEWER_MODEL_H
#define TORIAUXLIBCACHE_T_DAT1_MODELVIEWER_MODEL_H

#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "toriauxlib/c/dat1io.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat1ModelLoad
{
    struct pt thread;
    struct ToriAuxLibCache* c;
    int model_id;
};

struct Task_Dat1ModelLoad*
Task_Dat1ModelLoad_New(
    struct ToriAuxLibCache* c,
    int model_id)
{
    struct Task_Dat1ModelLoad* task = malloc(sizeof(struct Task_Dat1ModelLoad));
    assert(task);
    memset(task, 0, sizeof(struct Task_Dat1ModelLoad));
    PT_INIT(&task->thread);
    task->c = c;
    task->model_id = model_id;
    return task;
}

void
Task_Dat1ModelLoad_Free(struct Task_Dat1ModelLoad* task)
{
    if( !task )
        return;
    free(task);
}

int
Task_Dat1ModelLoad_Run(
    struct Task_Dat1ModelLoad* task,
    struct LibToriRS_IOContext* ctx)
{
    struct RSCacheDat2A_Model* model;
    PT_BEGIN(&task->thread);

    IO_REQUEST(ctx, 0, TAPIDat1_FetchModel(ctx, task->model_id));

    PT_YIELD(&task->thread);

    model = TAPIDat1_DecodeModel(ctx, 0);
    if( !model )
    {
        fprintf(stderr, "Failed to decode model\n");
        PT_EXIT(&task->thread);
    }

    dat1_buildcache_model_add(dat1(task->c), task->model_id, model);
    ToriAuxLibCache_SubmitModelFromDat1(task->c, task->model_id);

    PT_END(&task->thread);
}

#endif
