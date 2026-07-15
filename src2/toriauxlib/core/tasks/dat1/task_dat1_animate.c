#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat1_buildcache.h"
#include "core/tapi/tapi_dat1.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <assert.h>
#include <stdlib.h>

struct Task_Dat1Animate
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* c;
    int anim_id;
};

static int
Task_Dat1Animate_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1Animate* task = LibToriRS_container_of(base, struct Task_Dat1Animate, base);
    struct Dat1BuildCache* dat1_bc = dat1(task->c);
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);

    PT_BEGIN(&task->pt);

    assert(task->c && dat1_bc && core);

    if( task->anim_id != -1 && !ToriAuxLibCore_SequenceGet(core, task->anim_id) )
    {
        IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
        PT_YIELD(&task->pt);

        if( !TAPIDat1_DecodeConfigJagfile(ctx, 0) )
            PT_EXIT(&task->pt);

        dat1_buildcache_sequences_init_from_config_jagfile(dat1_bc);
        ToriAuxLibCache_SubmitAllSequencesFromDat1(task->c);
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->pt);
}

static void
Task_Dat1Animate_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat1_animate_vtable = {
    .run_fn = Task_Dat1Animate_Run,
    .free_fn = Task_Dat1Animate_Free,
};

struct LibToriRS_Task*
Task_Dat1Animate_New(
    struct ToriAuxLibCache* c,
    int anim_id)
{
    struct Task_Dat1Animate* task = calloc(1, sizeof(struct Task_Dat1Animate));
    if( !task )
        return NULL;
    task->base.vtable = &g_task_dat1_animate_vtable;
    task->c = c;
    task->anim_id = anim_id;
    PT_INIT(&task->pt);
    return &task->base;
}
