#ifndef TORIAUXLIB_TASK_RUNESCAPE_DAT2_ANIMATE_H
#define TORIAUXLIB_TASK_RUNESCAPE_DAT2_ANIMATE_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "../../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "core/tapi/tapi_dat2.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/task_runescape_dat2_anim_load.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <assert.h>
#include <stdlib.h>

struct Task_Dat2Animate
{
    struct pt thread;
    struct ToriAuxLibCache* c;
    int anim_id;
};

struct Task_Dat2Animate*
Task_Dat2Animate_New(
    struct ToriAuxLibCache* c,
    int anim_id)
{
    struct Task_Dat2Animate* task = calloc(1, sizeof(struct Task_Dat2Animate));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->c = c;
    task->anim_id = anim_id;
    return task;
}

void
Task_Dat2Animate_Free(struct Task_Dat2Animate* task)
{
    free(task);
}

int
Task_Dat2Animate_Run(
    struct Task_Dat2Animate* task,
    struct LibToriRS_IOContext* ctx)
{
    struct Dat2BuildCache* dat2_bc = dat2(task->c);
    struct RSCacheDat2Disk* cache_disk = ToriAuxLibCache_Dat2Disk(task->c);
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);
    struct RSCacheDat2Disk_Archive* sequence_archive = NULL;
    int seq_id = task->anim_id;

    PT_BEGIN(&task->thread);

    assert(task->c && dat2_bc && core);

    if( seq_id != -1 && !ToriAuxLibCore_SequenceGet(core, seq_id) )
    {
        sequence_archive = NULL;

        IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Sequence));
        PT_YIELD(&task->thread);

        sequence_archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Sequence);
        if( !sequence_archive || !cache_disk )
        {
            if( sequence_archive )
                RSCacheDat2Disk_ArchiveFree(sequence_archive);
            PT_EXIT(&task->thread);
        }

        dat2_buildcache_sequence_load_from_archive(dat2_bc, cache_disk, sequence_archive, seq_id);

        struct RSCacheDat2A_ConfigSequence* seq = dat2_buildcache_sequence_get(dat2_bc, seq_id);
        if( seq )
        {
            struct Dat2AnimArchiveSet aset;
            dat2_anim_archive_set_init(&aset, 32);
            if( aset.ids )
            {
                dat2_anim_set_add_sequence_archives(&aset, seq);
                dat2_anim_cache_sequence_skeletal(dat2_bc, cache_disk, seq);
                dat2_anim_submit_archive_set(task->c, dat2_bc, cache_disk, &aset);
                dat2_anim_archive_set_free(&aset);
            }

            dat2_anim_submit_sequence_skeletal(task->c, dat2_bc, seq);
            ToriAuxLibCache_SubmitSequenceFromDat2(task->c, seq_id);
        }

        RSCacheDat2Disk_ArchiveFree(sequence_archive);
        sequence_archive = NULL;
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->thread);
}

#endif
