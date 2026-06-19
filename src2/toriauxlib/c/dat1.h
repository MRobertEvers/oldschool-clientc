#ifndef TORIAUXLIBC_DAT1_H
#define TORIAUXLIBC_DAT1_H

#include "../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "toriauxlib/c/dat1io.h"
#include "toriauxlib/c/toriauxlibc.h"
#include "toriauxlib/c/toriauxlibc_submit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat1ModelLoad
{
    struct pt thread;
    struct ToriAuxLibC* c;
    int model_id;
};

struct Task_Dat1ModelLoad*
Task_Dat1ModelLoad_New(
    struct ToriAuxLibC* c,
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
    struct CacheModel* model;
    int decoded_model_id;
    PT_BEGIN(&task->thread);

    dat1io_model_fetch(ctx, task->model_id);

    PT_YIELD(&task->thread);

    decoded_model_id = -1;
    model = dat1io_model_decode(ctx, &decoded_model_id);
    if( !model )
    {
        fprintf(stderr, "Failed to decode model\n");
        PT_EXIT(&task->thread);
    }

    dat1_buildcache_model_add(dat1(task->c), task->model_id, model);
    ToriAuxLibC_SubmitModelFromDat1(task->c, task->model_id);

    PT_END(&task->thread);
}

#endif
