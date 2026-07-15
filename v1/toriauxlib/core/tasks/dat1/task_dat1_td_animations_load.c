#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat1_buildcache.h"
#include "core/tapi/tapi_dat1.h"
#include "osrs/rscache/dat1a/dat1a_anim_frame.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/td/toriauxlibtd.h"

#include <stdlib.h>

#define TDX_ANIM_IO_BATCH 64

struct Task_Dat1TdAnimationsLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibTD* tdx;
    int anim_count;
    int anim_index;
    struct LibToriRS_IOBatch io_batch;
};

static int
Task_Dat1TdAnimationsLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1TdAnimationsLoad* task =
        LibToriRS_container_of(base, struct Task_Dat1TdAnimationsLoad, base);
    struct Dat1BuildCache* dat1_bc = dat1(ToriAuxLibTD_C(task->tdx));

    PT_BEGIN(&task->pt);

    if( !dat1_bc->fromconfigtable_config_jagfile || !dat1_bc->versionlist_jagfile )
    {
        IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
        IO_REQUEST(ctx, 1, TAPIDat1_FetchVersionlistJagfile(ctx));
        PT_YIELD(&task->pt);

        {
            struct RSCacheShared_FileListDat* config_jag = TAPIDat1_DecodeConfigJagfile(ctx, 0);
            struct RSCacheShared_FileListDat* versionlist_jag =
                TAPIDat1_DecodeVersionlistJagfile(ctx, 1);
            if( config_jag )
                dat1_buildcache_set_fromconfigtable_config_jagfile(dat1_bc, config_jag);
            if( versionlist_jag )
                dat1_buildcache_set_versionlist_jagfile(dat1_bc, versionlist_jag);
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    task->anim_count = dat1_buildcache_get_animbaseframes_count_from_versionlist_jagfile(dat1_bc);
    for( task->anim_index = 0; task->anim_index < task->anim_count; )
    {
        LibToriRS_IOBatchReset(&task->io_batch);
        int batch_end = task->anim_index + TDX_ANIM_IO_BATCH;
        if( batch_end > task->anim_count )
            batch_end = task->anim_count;

        for( ; task->anim_index < batch_end; task->anim_index++ )
        {
            if( !dat1_buildcache_animbaseframes_has(dat1_bc, task->anim_index) )
            {
                int slot = LibToriRS_IOBatchAdd(&task->io_batch, 0);
                IO_REQUEST(ctx, slot, TAPIDat1_FetchAnimations(ctx, task->anim_index));
            }
        }

        if( LibToriRS_IOBatchEmpty(&task->io_batch) )
            continue;

        PT_YIELD(&task->pt);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            struct RSCacheDat1A_AnimBaseFrames* abf = NULL;
            int anim_id = TAPIDat1_DecodeAnimations(ctx, i, &abf);
            if( anim_id >= 0 && abf )
            {
                dat1_buildcache_animbaseframes_add(dat1_bc, anim_id, abf);
                ToriAuxLibCache_SubmitAnimationFromDat1(ToriAuxLibTD_C(task->tdx), anim_id);
                ToriAuxLibTD_Animation(task->tdx, anim_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    dat1_buildcache_sequences_init_from_config_jagfile(dat1_bc);
    ToriAuxLibCache_SubmitAllSequencesFromDat1(ToriAuxLibTD_C(task->tdx));

    PT_END(&task->pt);
}

static void
Task_Dat1TdAnimationsLoad_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat1_td_animations_load_vtable = {
    .run_fn = Task_Dat1TdAnimationsLoad_Run,
    .free_fn = Task_Dat1TdAnimationsLoad_Free,
};

struct LibToriRS_Task*
Task_Dat1TdAnimationsLoad_New(struct ToriAuxLibTD* tdx)
{
    struct Task_Dat1TdAnimationsLoad* task = calloc(1, sizeof(struct Task_Dat1TdAnimationsLoad));
    if( !task )
        return NULL;
    task->base.vtable = &g_task_dat1_td_animations_load_vtable;
    task->tdx = tdx;
    PT_INIT(&task->pt);
    return &task->base;
}
