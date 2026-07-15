#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_dat2.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/dat2/dat2_anim_load.h"
#include "toriauxlib/core/tasks/dat2/task_dat2_io.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

struct LibToriRS_Task*
Task_Dat2AnimResolve_New(
    struct ToriAuxLibCache* c,
    struct Dat2BuildCache* bc,
    const int* seq_ids,
    int seq_count);

struct Task_Dat2Animate
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* c;
    int anim_id;
    struct RSCacheDat2Disk_Archive* sequence_archive;
};

static int
Task_Dat2Animate_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2Animate* task = LibToriRS_container_of(base, struct Task_Dat2Animate, base);
    struct Dat2BuildCache* dat2_bc = dat2(task->c);
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);
    int seq_id = task->anim_id;
    int resolve_seq_ids[1];

    PT_BEGIN(&task->pt);

    assert(task->c && dat2_bc && core);

    if( seq_id != -1 && !ToriAuxLibCore_SequenceGet(core, seq_id) )
    {
        task->sequence_archive = NULL;

        DAT2_ENSURE_CONFIGS_REFERENCE_TABLE(ctx, &task->pt, task->c);

        IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Sequence));
        PT_YIELD(&task->pt);

        task->sequence_archive =
            TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Sequence);
        if( !task->sequence_archive )
            PT_EXIT(&task->pt);

        dat2_buildcache_sequence_load_from_archive(dat2_bc, task->sequence_archive, seq_id);

        struct RSCacheDat2A_ConfigSequence* seq = dat2_buildcache_sequence_get(dat2_bc, seq_id);
        if( seq )
        {
            resolve_seq_ids[0] = seq_id;
            TASK_AWAITEX(
                &task->pt,
                ctx,
                Task_Dat2AnimResolve_New(task->c, dat2_bc, resolve_seq_ids, 1));

            dat2_anim_submit_sequence_skeletal(task->c, dat2_bc, seq);
            ToriAuxLibCache_SubmitSequenceFromDat2(task->c, seq_id);
        }

        if( task->sequence_archive )
        {
            RSCacheDat2Disk_ArchiveFree(task->sequence_archive);
            task->sequence_archive = NULL;
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->pt);
}

static void
Task_Dat2Animate_Free(struct LibToriRS_Task* base)
{
    struct Task_Dat2Animate* task = LibToriRS_container_of(base, struct Task_Dat2Animate, base);

    if( task->sequence_archive )
    {
        RSCacheDat2Disk_ArchiveFree(task->sequence_archive);
        task->sequence_archive = NULL;
    }
    free(task);
}

static struct LibToriRS_TaskVTable g_task_dat2_animate_vtable = {
    .run_fn = Task_Dat2Animate_Run,
    .free_fn = Task_Dat2Animate_Free,
};

struct LibToriRS_Task*
Task_Dat2Animate_New(
    struct ToriAuxLibCache* c,
    int anim_id)
{
    struct Task_Dat2Animate* task = calloc(1, sizeof(struct Task_Dat2Animate));
    if( !task )
        return NULL;

    task->base.vtable = &g_task_dat2_animate_vtable;
    task->c = c;
    task->anim_id = anim_id;
    PT_INIT(&task->pt);
    return &task->base;
}
