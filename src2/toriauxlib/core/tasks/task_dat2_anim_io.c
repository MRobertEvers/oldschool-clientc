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
#include <stdio.h>

static void
task_dat2_anim_reset_load_state(struct Task_Dat2AnimResolve* task)
{
    task->load_step = TASK_DAT2_ANIM_LOAD_IDLE;
    task->current_aid = -1;
    task->pending_framemap_count = 0;
    task->pending_framemap_index = 0;
    task->pending_framemap_fetch_id = -1;
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
    task->phase = TASK_DAT2_ANIM_PHASE_SKELETAL;
    task->held_idx0 = NULL;
    task_dat2_anim_reset_load_state(task);
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

int
Task_Dat2AnimResolve_Run(
    struct Task_Dat2AnimResolve* task,
    struct LibToriRS_IOContext* ctx)
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
#pragma clang diagnostic ignored "-Wsometimes-uninitialized"
#endif

    PT_BEGIN(&task->thread);

    assert(task->c);
    assert(task->bc);

    while( task->phase < TASK_DAT2_ANIM_PHASE_DONE )
    {
        if( task->phase == TASK_DAT2_ANIM_PHASE_SKELETAL )
        {
            if( task->seq_index >= task->seq_count )
            {
                task->phase = TASK_DAT2_ANIM_PHASE_FRAMES_REF_TABLES;
                continue;
            }

            struct RSCacheDat2A_ConfigSequence* seq =
                dat2_buildcache_sequence_get(task->bc, task->seq_ids[task->seq_index]);
            task->seq_index++;

            int maya_id = -1;
            int animaya_aid = -1;
            struct RSCacheDat2Disk_Archive* animaya_arch = NULL;

            if( !seq )
                continue;

            maya_id = seq->anim_maya_id;
            if( maya_id < 0 || dat2_buildcache_skeletal_has(task->bc, maya_id) )
                continue;

            animaya_aid = maya_id >> 16;
            DAT2_ENSURE_REFERENCE_TABLE(
                ctx, &task->thread, task->bc, RSCacheDat2Disk_Table_Animayas);
            if( !dat2_buildcache_reference_table_has(task->bc, RSCacheDat2Disk_Table_Animayas) )
                continue;

            IO_REQUEST(
                ctx, 0, TAPIDat2_FetchArchive(ctx, RSCacheDat2Disk_Table_Animayas, animaya_aid));
            PT_YIELD(&task->thread);

            animaya_aid = maya_id >> 16;
            animaya_arch =
                TAPIDat2_DecodeArchive(ctx, 0, RSCacheDat2Disk_Table_Animayas, animaya_aid);
            if( animaya_arch )
            {
                struct RSCacheDat2Disk_ReferenceTable* animaya_table =
                    dat2_buildcache_reference_table_get(task->bc, RSCacheDat2Disk_Table_Animayas);
                struct RSCacheDat2A_AnimMaya* maya =
                    RSCacheDat2A_AnimMayaNewFromArchive(animaya_table, animaya_arch, maya_id);
                RSCacheDat2Disk_ArchiveFree(animaya_arch);
                if( maya )
                {
                    dat2_buildcache_skeletal_add(task->bc, maya_id, maya);
                    if( maya->base_id >= 0 &&
                        !dat2_buildcache_skeletal_base_has(task->bc, maya->base_id) )
                    {
                        IO_REQUEST(
                            ctx,
                            1,
                            TAPIDat2_FetchArchive(
                                ctx, RSCacheDat2Disk_Table_Skeletons, maya->base_id));
                        PT_YIELD(&task->thread);

                        struct RSCacheDat2Disk_Archive* skel_base_arch = TAPIDat2_DecodeArchive(
                            ctx, 1, RSCacheDat2Disk_Table_Skeletons, maya->base_id);
                        if( skel_base_arch )
                        {
                            dat2_buildcache_skeletal_base_add_from_archive(
                                task->bc, maya->base_id, skel_base_arch);
                            RSCacheDat2Disk_ArchiveFree(skel_base_arch);
                        }
                    }
                }
            }
            LibToriRS_IOQueueClear(ctx->io);
        }
        else if( task->phase == TASK_DAT2_ANIM_PHASE_FRAMES_REF_TABLES )
        {
            DAT2_ENSURE_REFERENCE_TABLE(
                ctx, &task->thread, task->bc, RSCacheDat2Disk_Table_Animations);
            DAT2_ENSURE_REFERENCE_TABLE(
                ctx, &task->thread, task->bc, RSCacheDat2Disk_Table_Skeletons);

            task->archive_index = 0;
            task_dat2_anim_reset_load_state(task);
            task->phase = TASK_DAT2_ANIM_PHASE_FRAMES_LOAD;
        }
        else if( task->phase == TASK_DAT2_ANIM_PHASE_FRAMES_LOAD )
        {
            if( task->load_step == TASK_DAT2_ANIM_LOAD_IDLE )
            {
                if( task->archive_index >= task->aset.count )
                {
                    task->phase = TASK_DAT2_ANIM_PHASE_DONE;
                    continue;
                }

                task->current_aid = task->aset.ids[task->archive_index];
                if( dat2_buildcache_frames_has(task->bc, task->current_aid) )
                {
                    ToriAuxLibCache_SubmitAnimationFromDat2(task->c, task->current_aid);
                    task->archive_index++;
                    continue;
                }

                task->load_step = TASK_DAT2_ANIM_LOAD_FETCH_IDX0;
            }

            if( task->load_step == TASK_DAT2_ANIM_LOAD_FETCH_IDX0 )
            {
                IO_REQUEST(
                    ctx,
                    0,
                    TAPIDat2_FetchArchive(
                        ctx, RSCacheDat2Disk_Table_Animations, task->current_aid));
                PT_YIELD(&task->thread);

                task->load_step = TASK_DAT2_ANIM_LOAD_FETCH_IDX0_DECODE;
                continue;
            }

            if( task->load_step == TASK_DAT2_ANIM_LOAD_FETCH_IDX0_DECODE )
            {
                task->held_idx0 = TAPIDat2_DecodeArchive(
                    ctx, 0, RSCacheDat2Disk_Table_Animations, task->current_aid);
                if( !task->held_idx0 )
                {
                    fprintf(
                        stderr,
                        "Task_Dat2AnimResolve: failed to decode animation archive "
                        "(archive_id=%d)\n",
                        task->current_aid);
                    task->archive_index++;
                    task_dat2_anim_reset_load_state(task);
                    LibToriRS_IOQueueClear(ctx->io);
                    continue;
                }

                struct RSCacheShared_FileList* fl = NULL;
                struct RSCacheDat2Disk_ReferenceTable* animations_table =
                    dat2_buildcache_reference_table_get(
                        task->bc, RSCacheDat2Disk_Table_Animations);
                if( animations_table )
                    RSCacheDat2Disk_ArchiveInitMetadataFromTable(
                        animations_table, task->held_idx0);

                fl = RSCacheShared_FileListNewFromCacheArchive(task->held_idx0);
                task->pending_framemap_count = 0;
                task->pending_framemap_index = 0;
                task->pending_framemap_fetch_id = -1;
                if( fl )
                {
                    task->pending_framemap_count = dat2_buildcache_frames_collect_framemap_ids(
                        fl, task->pending_framemap_ids, TASK_DAT2_ANIM_MAX_PENDING_FRAMEMAPS);
                    RSCacheShared_FileListFree(fl);
                }

                if( task->pending_framemap_count <= 0 )
                    task->load_step = TASK_DAT2_ANIM_LOAD_DECODE;
                else
                    task->load_step = TASK_DAT2_ANIM_LOAD_FETCH_FRAMEMAP;

                LibToriRS_IOQueueClear(ctx->io);
                continue;
            }

            if( task->load_step == TASK_DAT2_ANIM_LOAD_FETCH_FRAMEMAP )
            {
                if( task->pending_framemap_index >= task->pending_framemap_count )
                {
                    task->load_step = TASK_DAT2_ANIM_LOAD_DECODE;
                    continue;
                }

                if( task->pending_framemap_fetch_id < 0 )
                {
                    int fmid = task->pending_framemap_ids[task->pending_framemap_index];
                    if( dat2_buildcache_framemap_get(task->bc, fmid) )
                    {
                        task->pending_framemap_index++;
                        continue;
                    }

                    task->pending_framemap_fetch_id = fmid;
                    IO_REQUEST(
                        ctx, 1, TAPIDat2_FetchArchive(ctx, RSCacheDat2Disk_Table_Skeletons, fmid));
                    PT_YIELD(&task->thread);
                    continue;
                }

                int fmid = task->pending_framemap_fetch_id;
                struct RSCacheDat2Disk_Archive* skel =
                    TAPIDat2_DecodeArchive(ctx, 1, RSCacheDat2Disk_Table_Skeletons, fmid);
                if( skel )
                {
                    dat2_buildcache_framemap_add_from_archive(task->bc, fmid, skel);
                    RSCacheDat2Disk_ArchiveFree(skel);
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
                task->pending_framemap_fetch_id = -1;
                task->pending_framemap_index++;
                LibToriRS_IOQueueClear(ctx->io);
                continue;
            }

            if( task->load_step == TASK_DAT2_ANIM_LOAD_DECODE )
            {
                bool loaded = false;

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

                if( loaded && dat2_buildcache_frames_has(task->bc, task->current_aid) )
                    ToriAuxLibCache_SubmitAnimationFromDat2(task->c, task->current_aid);

                task->archive_index++;
                task_dat2_anim_reset_load_state(task);
                LibToriRS_IOQueueClear(ctx->io);
            }
        }
    }

    PT_END(&task->thread);

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}
