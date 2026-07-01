#include "task_dat2_anim_io.h"

#include "core/tapi/tapi_dat2.h"
#include "ioqueue/libtorirs_io.h"
#include "osrs/rscache/dat2a/dat2a_animaya.h"
#include "osrs/rscache/dat2a/dat2a_frame.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/task_dat2_io.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

static void
task_dat2_anim_reset_skeletal_state(struct Task_Dat2AnimResolve* task)
{
    task->skeletal_maya_id = -1;
    task->skeletal_seq_id = -1;
    task->skeletal_animaya_aid = -1;
    task->skeletal_base_id = -1;
}

static void
task_dat2_anim_reset_load_state(struct Task_Dat2AnimResolve* task)
{
    task->current_aid = -1;
    task->pending_framemap_count = 0;
    task->pending_framemap_index = 0;
    if( task->held_idx0 )
    {
        RSCacheDat2Disk_ArchiveFree(task->held_idx0);
        task->held_idx0 = NULL;
    }
}

void
Task_Dat2AnimResolve_Init(
    struct Task_Dat2AnimResolve* task,
    struct ToriAuxLibCache* c,
    struct Dat2BuildCache* bc)
{
    PT_INIT(&task->thread);
    task->c = c;
    task->bc = bc;
    task->seq_count = 0;
    task->seq_index = 0;
    task->archive_index = 0;
    task->held_idx0 = NULL;
    task_dat2_anim_reset_load_state(task);
    task_dat2_anim_reset_skeletal_state(task);
    dat2_anim_archive_set_init(&task->aset, 0);
}

void
Task_Dat2AnimResolve_SetArchiveSet(
    struct Task_Dat2AnimResolve* task,
    const struct Dat2AnimArchiveSet* aset)
{
    dat2_anim_archive_set_free(&task->aset);
    assert(task && aset && aset->ids);
    if( aset->count <= 0 )
    {
        dat2_anim_archive_set_init(&task->aset, 0);
        return;
    }

    dat2_anim_archive_set_init(&task->aset, aset->count);
    for( int i = 0; i < aset->count; i++ )
        dat2_anim_archive_set_add(&task->aset, aset->ids[i]);
}

void
Task_Dat2AnimResolve_AddSequenceId(
    struct Task_Dat2AnimResolve* task,
    int seq_id)
{
    assert(task);
    assert(seq_id >= 0);
    assert(task->seq_count < TASK_DAT2_ANIM_RESOLVE_MAX_SEQS);
    task->seq_ids[task->seq_count++] = seq_id;
}

void
Task_Dat2AnimResolve_Destroy(struct Task_Dat2AnimResolve* task)
{
    if( !task )
        return;
    task_dat2_anim_reset_load_state(task);
    task_dat2_anim_reset_skeletal_state(task);
    dat2_anim_archive_set_free(&task->aset);
}

int
Task_Dat2AnimResolve_Run(
    struct Task_Dat2AnimResolve* task,
    struct LibToriRS_IOContext* ctx)
{
    struct RSCacheDat2A_ConfigSequence* seq = NULL;
    struct RSCacheDat2Disk_Archive* animaya_arch = NULL;
    struct RSCacheDat2Disk_Archive* skel_base_arch = NULL;
    struct RSCacheDat2Disk_Archive* skel = NULL;
    struct RSCacheShared_FileList* fl = NULL;
    struct RSCacheDat2Disk_ReferenceTable* animations_table = NULL;
    struct RSCacheDat2Disk_ReferenceTable* animaya_table = NULL;
    struct RSCacheDat2A_AnimMaya* maya = NULL;
    bool loaded = false;
    int fmid = -1;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
#pragma clang diagnostic ignored "-Wsometimes-uninitialized"
#endif

    PT_BEGIN(&task->thread);

    assert(task->c);
    assert(task->bc);

    for( task->seq_index = 0; task->seq_index < task->seq_count; task->seq_index++ )
    {
        seq = dat2_buildcache_sequence_get(task->bc, task->seq_ids[task->seq_index]);
        task->skeletal_seq_id = task->seq_ids[task->seq_index];
        if( !seq )
            continue;

        task->skeletal_maya_id = seq->anim_maya_id;
        if( task->skeletal_maya_id < 0 ||
            dat2_buildcache_skeletal_has(task->bc, task->skeletal_maya_id) )
            continue;

        task->skeletal_animaya_aid = task->skeletal_maya_id >> 16;
        task->skeletal_base_id = -1;

        DAT2_ENSURE_REFERENCE_TABLE(
            ctx, &task->thread, task->bc, RSCacheDat2Disk_Table_Animayas);

        if( !dat2_buildcache_reference_table_has(task->bc, RSCacheDat2Disk_Table_Animayas) )
        {
            fprintf(
                stderr,
                "Task_Dat2AnimResolve: Animayas reference table missing for "
                "maya_id=%d animaya_aid=%d seq_id=%d\n",
                task->skeletal_maya_id,
                task->skeletal_animaya_aid,
                task->skeletal_seq_id);
            continue;
        }

        IO_REQUEST(
            ctx,
            0,
            TAPIDat2_FetchArchive(
                ctx, RSCacheDat2Disk_Table_Animayas, task->skeletal_animaya_aid));
        PT_YIELD(&task->thread);

        animaya_arch = TAPIDat2_DecodeArchive(
            ctx,
            0,
            RSCacheDat2Disk_Table_Animayas,
            task->skeletal_animaya_aid);
        if( !animaya_arch )
        {
            fprintf(
                stderr,
                "Task_Dat2AnimResolve: failed to load animaya table=%d archive=%d "
                "maya_id=%d seq_id=%d\n",
                RSCacheDat2Disk_Table_Animayas,
                task->skeletal_animaya_aid,
                task->skeletal_maya_id,
                task->skeletal_seq_id);
            LibToriRS_IOQueueClear(ctx->io);
            continue;
        }

        animaya_table =
            dat2_buildcache_reference_table_get(task->bc, RSCacheDat2Disk_Table_Animayas);
        maya = RSCacheDat2A_AnimMayaNewFromArchive(
            animaya_table, animaya_arch, task->skeletal_maya_id);
        RSCacheDat2Disk_ArchiveFree(animaya_arch);
        animaya_arch = NULL;

        if( !maya )
        {
            fprintf(
                stderr,
                "Task_Dat2AnimResolve: failed to decode animaya maya_id=%d "
                "animaya_aid=%d seq_id=%d\n",
                task->skeletal_maya_id,
                task->skeletal_animaya_aid,
                task->skeletal_seq_id);
            LibToriRS_IOQueueClear(ctx->io);
            continue;
        }

        dat2_buildcache_skeletal_add(task->bc, task->skeletal_maya_id, maya);

        if( maya->base_id >= 0 &&
            !dat2_buildcache_skeletal_base_has(task->bc, maya->base_id) )
        {
            task->skeletal_base_id = maya->base_id;
            LibToriRS_IOQueueClear(ctx->io);
            IO_REQUEST(
                ctx,
                1,
                TAPIDat2_FetchArchive(
                    ctx, RSCacheDat2Disk_Table_Skeletons, task->skeletal_base_id));
            PT_YIELD(&task->thread);

            skel_base_arch = TAPIDat2_DecodeArchive(
                ctx, 1, RSCacheDat2Disk_Table_Skeletons, task->skeletal_base_id);
            if( skel_base_arch )
            {
                dat2_buildcache_skeletal_base_add_from_archive(
                    task->bc, task->skeletal_base_id, skel_base_arch);
                RSCacheDat2Disk_ArchiveFree(skel_base_arch);
                skel_base_arch = NULL;
            }
            else
            {
                fprintf(
                    stderr,
                    "Task_Dat2AnimResolve: failed to load skeletal base table=%d "
                    "archive=%d maya_id=%d seq_id=%d\n",
                    RSCacheDat2Disk_Table_Skeletons,
                    task->skeletal_base_id,
                    task->skeletal_maya_id,
                    task->skeletal_seq_id);
            }
            LibToriRS_IOQueueClear(ctx->io);
        }
        else
        {
            LibToriRS_IOQueueClear(ctx->io);
        }
    }

    DAT2_ENSURE_REFERENCE_TABLE(
        ctx, &task->thread, task->bc, RSCacheDat2Disk_Table_Animations);
    DAT2_ENSURE_REFERENCE_TABLE(
        ctx, &task->thread, task->bc, RSCacheDat2Disk_Table_Skeletons);

    for( task->archive_index = 0; task->archive_index < task->aset.count; task->archive_index++ )
    {
        task->current_aid = task->aset.ids[task->archive_index];
        if( dat2_buildcache_frames_has(task->bc, task->current_aid) )
        {
            assert(
                dat2_buildcache_frames_has(task->bc, task->current_aid) &&
                "frames missing before submit");
            ToriAuxLibCache_SubmitAnimationFromDat2(task->c, task->current_aid);
            continue;
        }

        IO_REQUEST(
            ctx,
            0,
            TAPIDat2_FetchArchive(
                ctx, RSCacheDat2Disk_Table_Animations, task->current_aid));
        PT_YIELD(&task->thread);

        task->held_idx0 = TAPIDat2_DecodeArchive(
            ctx, 0, RSCacheDat2Disk_Table_Animations, task->current_aid);
        if( !task->held_idx0 )
        {
            fprintf(
                stderr,
                "Task_Dat2AnimResolve: failed to decode animation archive "
                "(archive_id=%d)\n",
                task->current_aid);
            LibToriRS_IOQueueClear(ctx->io);
            continue;
        }

        animations_table =
            dat2_buildcache_reference_table_get(task->bc, RSCacheDat2Disk_Table_Animations);
        if( animations_table )
            RSCacheDat2Disk_ArchiveInitMetadataFromTable(animations_table, task->held_idx0);

        fl = RSCacheShared_FileListNewFromCacheArchive(task->held_idx0);
        task->pending_framemap_count = 0;
        task->pending_framemap_index = 0;
        if( fl )
        {
            task->pending_framemap_count = dat2_buildcache_frames_collect_framemap_ids(
                fl, task->pending_framemap_ids, TASK_DAT2_ANIM_MAX_PENDING_FRAMEMAPS);
            RSCacheShared_FileListFree(fl);
            fl = NULL;
        }

        LibToriRS_IOQueueClear(ctx->io);

        for( task->pending_framemap_index = 0;
             task->pending_framemap_index < task->pending_framemap_count;
             task->pending_framemap_index++ )
        {
            fmid = task->pending_framemap_ids[task->pending_framemap_index];
            if( dat2_buildcache_framemap_get(task->bc, fmid) )
                continue;

            IO_REQUEST(
                ctx, 1, TAPIDat2_FetchArchive(ctx, RSCacheDat2Disk_Table_Skeletons, fmid));
            PT_YIELD(&task->thread);

            fmid = task->pending_framemap_ids[task->pending_framemap_index];
            skel = TAPIDat2_DecodeArchive(ctx, 1, RSCacheDat2Disk_Table_Skeletons, fmid);
            if( skel )
            {
                dat2_buildcache_framemap_add_from_archive(task->bc, fmid, skel);
                RSCacheDat2Disk_ArchiveFree(skel);
                skel = NULL;
            }
            else
            {
                fprintf(
                    stderr,
                    "Task_Dat2AnimResolve: failed to decode framemap "
                    "(archive_id=%d framemap_id=%d)\n",
                    task->current_aid,
                    fmid);
            }
            LibToriRS_IOQueueClear(ctx->io);
        }

        loaded = false;
        if( task->held_idx0 )
        {
            loaded = dat2_buildcache_frames_add_from_fetched_archive(
                task->bc, task->current_aid, task->held_idx0);
            if( !loaded )
            {
                fprintf(
                    stderr,
                    "Task_Dat2AnimResolve: frame decode failed "
                    "(archive_id=%d pending_framemaps=%d)\n",
                    task->current_aid,
                    task->pending_framemap_count);
            }
        }

        if( loaded )
        {
            assert(
                dat2_buildcache_frames_has(task->bc, task->current_aid) &&
                "frames missing before submit after decode");
            ToriAuxLibCache_SubmitAnimationFromDat2(task->c, task->current_aid);
        }

        task_dat2_anim_reset_load_state(task);
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->thread);

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}
