#ifndef TORIAUXLIB_TASK_RUNESCAPE_DAT1_PROJECTILE_ADD_H
#define TORIAUXLIB_TASK_RUNESCAPE_DAT1_PROJECTILE_ADD_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "../../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "core/tapi/tapi_dat1.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <assert.h>
#include <stdlib.h>

struct Task_Dat1ProjectileAdd
{
    struct pt thread;
    struct ToriAuxLibCache* c;
    int model_id;
    int anim_id;
};

struct Task_Dat1ProjectileAdd*
Task_Dat1ProjectileAdd_New(
    struct ToriAuxLibCache* c,
    int model_id,
    int anim_id)
{
    struct Task_Dat1ProjectileAdd* task = calloc(1, sizeof(struct Task_Dat1ProjectileAdd));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->c = c;
    task->model_id = model_id;
    task->anim_id = anim_id;
    return task;
}

void
Task_Dat1ProjectileAdd_Free(struct Task_Dat1ProjectileAdd* task)
{
    free(task);
}

int
Task_Dat1ProjectileAdd_Run(
    struct Task_Dat1ProjectileAdd* task,
    struct LibToriRS_IOContext* ctx)
{
    struct Dat1BuildCache* dat1_bc = dat1(task->c);
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);

    PT_BEGIN(&task->thread);

    assert(task->c && dat1_bc && core);

    if( !ToriAuxLibCore_ModelHas(core, task->model_id) )
    {
        struct RSCacheDat2A_Model* model;
        IO_REQUEST(ctx, 0, TAPIDat1_FetchModel(ctx, task->model_id));
        PT_YIELD(&task->thread);

        model = TAPIDat1_DecodeModel(ctx, 0);
        if( !model )
            PT_EXIT(&task->thread);

        dat1_buildcache_model_add(dat1_bc, task->model_id, model);
        ToriAuxLibCache_SubmitModelFromDat1(task->c, task->model_id);
        LibToriRS_IOQueueClear(ctx->io);
    }

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
