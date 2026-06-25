#ifndef TORIAUXLIBC_DAT2_H
#define TORIAUXLIBC_DAT2_H

#include "../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "toriauxlib/c/dat2io.h"
#include "toriauxlib/c/toriauxlibc.h"
#include "toriauxlib/c/toriauxlibc_submit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2ModelLoad
{
    struct pt thread;
    struct ToriAuxLibC* c;
    int model_id;
};

struct Task_Dat2ModelLoad*
Task_Dat2ModelLoad_New(
    struct ToriAuxLibC* c,
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
    int decoded_model_id;
    PT_BEGIN(&task->thread);

    dat2io_model_fetch(ctx, task->model_id);

    PT_YIELD(&task->thread);

    decoded_model_id = -1;
    model = dat2io_model_decode(ctx, &decoded_model_id);
    if( !model )
    {
        fprintf(stderr, "Failed to decode dat2 model\n");
        PT_EXIT(&task->thread);
    }

    dat2_buildcache_model_add(dat2(task->c), task->model_id, model);
    ToriAuxLibC_SubmitModelFromDat2(task->c, task->model_id);

    PT_END(&task->thread);
}

#endif
