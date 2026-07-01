#include "task_dat2_anim_io.h"

#include "core/tapi/tapi_dat2.h"
#include "ioqueue/libtorirs_io.h"
#include "osrs/rscache/dat2a/dat2a_animaya.h"
#include "osrs/rscache/dat2a/dat2a_frame.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/task_dat2_io.h"

enum Task_Dat2AnimResolve_Phase
{
    TASK_DAT2_ANIM_PHASE_SKELETAL = 0,
    TASK_DAT2_ANIM_PHASE_FRAMES = 1,
    TASK_DAT2_ANIM_PHASE_DONE = 2,
};

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
    dat2_anim_archive_set_init(&task->aset, 0);
}

void
Task_Dat2AnimResolve_SetArchiveSet(
    struct Task_Dat2AnimResolve* task,
    const struct Dat2AnimArchiveSet* aset)
{
    dat2_anim_archive_set_free(&task->aset);
    if( !task || !aset || !aset->ids || aset->count <= 0 )
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
    if( !task || seq_id < 0 || task->seq_count >= TASK_DAT2_ANIM_RESOLVE_MAX_SEQS )
        return;
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

    if( !task->c || !task->bc )
        PT_EXIT(&task->thread);

    while( task->phase < TASK_DAT2_ANIM_PHASE_DONE )
    {
        if( task->phase == TASK_DAT2_ANIM_PHASE_SKELETAL )
        {
            if( task->seq_index >= task->seq_count )
            {
                task->phase = TASK_DAT2_ANIM_PHASE_FRAMES;
                task->archive_index = 0;
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
                    dat2_buildcache_skeletal_add(task->bc, maya_id, maya);
            }
            LibToriRS_IOQueueClear(ctx->io);
        }
        else if( task->phase == TASK_DAT2_ANIM_PHASE_FRAMES )
        {
            if( task->archive_index >= task->aset.count )
            {
                task->phase = TASK_DAT2_ANIM_PHASE_DONE;
                continue;
            }

            int aid = -1;
            struct RSCacheDat2Disk_Archive* idx0 = NULL;
            struct RSCacheShared_FileList* fl = NULL;
            int fmid = -1;
            struct RSCacheDat2Disk_Archive* skel = NULL;

            aid = task->aset.ids[task->archive_index++];

            if( !dat2_buildcache_frames_has(task->bc, aid) )
            {
                DAT2_ENSURE_REFERENCE_TABLE(
                    ctx, &task->thread, task->bc, RSCacheDat2Disk_Table_Animations);
                DAT2_ENSURE_REFERENCE_TABLE(
                    ctx, &task->thread, task->bc, RSCacheDat2Disk_Table_Skeletons);

                IO_REQUEST(
                    ctx, 0, TAPIDat2_FetchArchive(ctx, RSCacheDat2Disk_Table_Animations, aid));
                PT_YIELD(&task->thread);

                aid = task->aset.ids[task->archive_index - 1];
                idx0 = TAPIDat2_DecodeArchive(ctx, 0, RSCacheDat2Disk_Table_Animations, aid);
                if( idx0 )
                {
                    fl = RSCacheShared_FileListNewFromCacheArchive(idx0);
                    if( fl && fl->file_count > 0 && fl->files[0] )
                    {
                        fmid = RSCacheDat2A_FrameFramemapIdFromFile(
                            fl->files[0], fl->file_sizes[0]);
                        if( fmid >= 0 && !dat2_buildcache_framemap_get(task->bc, fmid) )
                        {
                            IO_REQUEST(
                                ctx,
                                1,
                                TAPIDat2_FetchArchive(
                                    ctx, RSCacheDat2Disk_Table_Skeletons, fmid));
                            PT_YIELD(&task->thread);

                            fmid = RSCacheDat2A_FrameFramemapIdFromFile(
                                fl->files[0], fl->file_sizes[0]);
                            skel = TAPIDat2_DecodeArchive(
                                ctx, 1, RSCacheDat2Disk_Table_Skeletons, fmid);
                            if( skel )
                            {
                                dat2_buildcache_framemap_add_from_archive(task->bc, fmid, skel);
                                RSCacheDat2Disk_ArchiveFree(skel);
                                skel = NULL;
                            }
                        }
                    }
                    RSCacheShared_FileListFree(fl);
                    fl = NULL;
                    dat2_buildcache_frames_add_from_fetched_archive(task->bc, aid, idx0);
                    RSCacheDat2Disk_ArchiveFree(idx0);
                    idx0 = NULL;
                }
                LibToriRS_IOQueueClear(ctx->io);
            }

            ToriAuxLibCache_SubmitAnimationFromDat2(task->c, aid);
        }
    }

    PT_END(&task->thread);

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}
