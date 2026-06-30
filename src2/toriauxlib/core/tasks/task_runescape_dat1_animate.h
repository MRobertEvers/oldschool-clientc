#ifndef TORIAUXLIB_TASK_RUNESCAPE_DAT1_ANIMATE_H
#define TORIAUXLIB_TASK_RUNESCAPE_DAT1_ANIMATE_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "../../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "core/tapi/tapi_dat1.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <assert.h>
#include <stdlib.h>

struct Task_Dat1Animate
{
    struct pt thread;
    struct ToriAuxLibCache* c;
    int anim_id;
};

struct Task_Dat1Animate*
Task_Dat1Animate_New(
    struct ToriAuxLibCache* c,
    int anim_id)
{
    struct Task_Dat1Animate* task = calloc(1, sizeof(struct Task_Dat1Animate));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->c = c;
    task->anim_id = anim_id;
    return task;
}

void
Task_Dat1Animate_Free(struct Task_Dat1Animate* task)
{
    free(task);
}

int
Task_Dat1Animate_Run(
    struct Task_Dat1Animate* task,
    struct LibToriRS_IOContext* ctx)
{
    struct Dat1BuildCache* dat1_bc = dat1(task->c);
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);

    PT_BEGIN(&task->thread);

    assert(task->c && dat1_bc && core);

    if( task->anim_id != -1 && !ToriAuxLibCore_SequenceGet(core, task->anim_id) )
    {
        IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
        PT_YIELD(&task->thread);

        if( !TAPIDat1_DecodeConfigJagfile(ctx, 0) )
            PT_EXIT(&task->thread);

        dat1_buildcache_sequences_init_from_config_jagfile(dat1_bc);
        ToriAuxLibCache_SubmitAllSequencesFromDat1(task->c);
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->thread);
}

#endif
