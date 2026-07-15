#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_dat2.h"
#include "osrs/rscache/dat2a/dat2a_animaya.h"
#include "osrs/rscache/dat2a/dat2a_frame.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/dat2/dat2_anim_load.h"
#include "toriauxlib/core/tasks/dat2/task_dat2_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TASK_DAT2_ANIM_RESOLVE_MAX_SEQS 16
#define TASK_DAT2_ANIM_MAX_PENDING_FRAMEMAPS 32

#define TASK_DAT2_ANIM_FATAL(...)                                                                  \
    do                                                                                             \
    {                                                                                              \
        fprintf(stderr, "Task_Dat2AnimResolve: ");                                                 \
        fprintf(stderr, __VA_ARGS__);                                                              \
        abort();                                                                                   \
    } while( 0 )

struct Task_Dat2AnimResolve
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* c;
    struct Dat2BuildCache* bc;
    struct Dat2AnimArchiveSet aset;
    int seq_ids[TASK_DAT2_ANIM_RESOLVE_MAX_SEQS];
    int seq_count;
    int seq_index;
    int archive_index;
    int pending_framemap_ids[TASK_DAT2_ANIM_MAX_PENDING_FRAMEMAPS];
    int pending_framemap_count;
    int pending_framemap_index;
    struct RSCacheDat2Disk_Archive* held_idx0;
    int skeletal_maya_id;
    int skeletal_seq_id;
    int skeletal_animaya_aid;
    int skeletal_base_id;
    int current_aid;
};

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

static void
task_dat2_anim_resolve_build_archive_set(
    struct Task_Dat2AnimResolve* task)
{
    struct Dat2AnimArchiveSet tmp;

    dat2_anim_archive_set_init(&tmp, 32);
    assert(tmp.ids && "npc anim archive set alloc failed");

    for( int i = 0; i < task->seq_count; i++ )
    {
        struct RSCacheDat2A_ConfigSequence* seq =
            dat2_buildcache_sequence_get(task->bc, task->seq_ids[i]);
        if( seq )
            dat2_anim_set_add_sequence_archives(&tmp, seq);
    }

    dat2_anim_archive_set_free(&task->aset);
    task->aset = tmp;
}

static int
Task_Dat2AnimResolve_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2AnimResolve* task =
        LibToriRS_container_of(base, struct Task_Dat2AnimResolve, base);
    struct RSCacheDat2A_ConfigSequence* seq = NULL;
    struct RSCacheDat2Disk_Archive* animaya_arch = NULL;
    struct RSCacheDat2Disk_Archive* skel_base_arch = NULL;
    struct RSCacheDat2Disk_Archive* skel = NULL;
    struct RSCacheShared_FileList* fl = NULL;
    struct RSCacheDat2Disk_ReferenceTable* animations_table = NULL;
    struct RSCacheDat2Disk_ReferenceTable* animaya_table = NULL;
    struct RSCacheDat2A_AnimMaya* maya = NULL;
    int fmid = -1;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
#pragma clang diagnostic ignored "-Wsometimes-uninitialized"
#endif

    PT_BEGIN(&task->pt);

    assert(task->c);
    assert(task->bc);

    for( task->seq_index = 0; task->seq_index < task->seq_count; task->seq_index++ )
    {
        seq = dat2_buildcache_sequence_get(task->bc, task->seq_ids[task->seq_index]);
        task->skeletal_seq_id = task->seq_ids[task->seq_index];
        if( !seq )
        {
            TASK_DAT2_ANIM_FATAL(
                "sequence missing from buildcache (seq_id=%d)\n", task->skeletal_seq_id);
        }

        task->skeletal_maya_id = seq->anim_maya_id;
        if( task->skeletal_maya_id < 0 ||
            dat2_buildcache_skeletal_has(task->bc, task->skeletal_maya_id) )
            continue;

        task->skeletal_animaya_aid = task->skeletal_maya_id >> 16;
        task->skeletal_base_id = -1;

        DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->pt, task->bc, RSCacheDat2Disk_Table_Animayas);

        if( !dat2_buildcache_reference_table_has(task->bc, RSCacheDat2Disk_Table_Animayas) )
        {
            TASK_DAT2_ANIM_FATAL(
                "Animayas reference table missing for maya_id=%d animaya_aid=%d seq_id=%d\n",
                task->skeletal_maya_id,
                task->skeletal_animaya_aid,
                task->skeletal_seq_id);
        }

        IO_REQUEST(
            ctx,
            0,
            TAPIDat2_FetchArchive(ctx, RSCacheDat2Disk_Table_Animayas, task->skeletal_animaya_aid));
        PT_YIELD(&task->pt);

        animaya_arch = TAPIDat2_DecodeArchive(
            ctx, 0, RSCacheDat2Disk_Table_Animayas, task->skeletal_animaya_aid);
        if( !animaya_arch )
        {
            TASK_DAT2_ANIM_FATAL(
                "failed to load animaya table=%d archive=%d maya_id=%d seq_id=%d\n",
                RSCacheDat2Disk_Table_Animayas,
                task->skeletal_animaya_aid,
                task->skeletal_maya_id,
                task->skeletal_seq_id);
        }

        animaya_table =
            dat2_buildcache_reference_table_get(task->bc, RSCacheDat2Disk_Table_Animayas);
        if( !animaya_table )
        {
            TASK_DAT2_ANIM_FATAL(
                "Animayas reference table missing during decode maya_id=%d seq_id=%d\n",
                task->skeletal_maya_id,
                task->skeletal_seq_id);
        }
        maya = RSCacheDat2A_AnimMayaNewFromArchive(
            animaya_table, animaya_arch, task->skeletal_maya_id);
        RSCacheDat2Disk_ArchiveFree(animaya_arch);
        animaya_arch = NULL;

        if( !maya )
        {
            TASK_DAT2_ANIM_FATAL(
                "failed to decode animaya maya_id=%d animaya_aid=%d seq_id=%d\n",
                task->skeletal_maya_id,
                task->skeletal_animaya_aid,
                task->skeletal_seq_id);
        }

        dat2_buildcache_skeletal_add(task->bc, task->skeletal_maya_id, maya);

        if( maya->base_id >= 0 && !dat2_buildcache_skeletal_base_has(task->bc, maya->base_id) )
        {
            task->skeletal_base_id = maya->base_id;
            LibToriRS_IOQueueClear(ctx->io);
            IO_REQUEST(
                ctx,
                1,
                TAPIDat2_FetchArchive(
                    ctx, RSCacheDat2Disk_Table_Skeletons, task->skeletal_base_id));
            PT_YIELD(&task->pt);

            skel_base_arch = TAPIDat2_DecodeArchive(
                ctx, 1, RSCacheDat2Disk_Table_Skeletons, task->skeletal_base_id);
            if( !skel_base_arch )
            {
                TASK_DAT2_ANIM_FATAL(
                    "failed to load skeletal base table=%d archive=%d maya_id=%d seq_id=%d\n",
                    RSCacheDat2Disk_Table_Skeletons,
                    task->skeletal_base_id,
                    task->skeletal_maya_id,
                    task->skeletal_seq_id);
            }
            dat2_buildcache_skeletal_base_add_from_archive(
                task->bc, task->skeletal_base_id, skel_base_arch);
            RSCacheDat2Disk_ArchiveFree(skel_base_arch);
            skel_base_arch = NULL;
            LibToriRS_IOQueueClear(ctx->io);
        }
        else
        {
            LibToriRS_IOQueueClear(ctx->io);
        }
    }

    DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->pt, task->bc, RSCacheDat2Disk_Table_Animations);
    DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->pt, task->bc, RSCacheDat2Disk_Table_Skeletons);

    for( task->archive_index = 0; task->archive_index < task->aset.count; task->archive_index++ )
    {
        task->current_aid = task->aset.ids[task->archive_index];
        if( dat2_buildcache_frames_has(task->bc, task->current_aid) )
        {
            ToriAuxLibCache_SubmitAnimationFromDat2(task->c, task->current_aid);
            continue;
        }

        IO_REQUEST(
            ctx,
            0,
            TAPIDat2_FetchArchive(ctx, RSCacheDat2Disk_Table_Animations, task->current_aid));
        PT_YIELD(&task->pt);

        task->held_idx0 =
            TAPIDat2_DecodeArchive(ctx, 0, RSCacheDat2Disk_Table_Animations, task->current_aid);
        if( !task->held_idx0 )
        {
            TASK_DAT2_ANIM_FATAL(
                "failed to decode animation archive (archive_id=%d)\n", task->current_aid);
        }

        animations_table =
            dat2_buildcache_reference_table_get(task->bc, RSCacheDat2Disk_Table_Animations);
        if( !animations_table )
        {
            TASK_DAT2_ANIM_FATAL(
                "Animations reference table missing (archive_id=%d)\n", task->current_aid);
        }
        RSCacheDat2Disk_ArchiveInitMetadataFromTable(animations_table, task->held_idx0);

        fl = RSCacheShared_FileListNewFromCacheArchive(task->held_idx0);
        task->pending_framemap_count = 0;
        task->pending_framemap_index = 0;
        if( !fl )
        {
            TASK_DAT2_ANIM_FATAL(
                "failed to build file list from animation archive (archive_id=%d)\n",
                task->current_aid);
        }
        task->pending_framemap_count = dat2_buildcache_frames_collect_framemap_ids(
            fl, task->pending_framemap_ids, TASK_DAT2_ANIM_MAX_PENDING_FRAMEMAPS);
        RSCacheShared_FileListFree(fl);
        fl = NULL;

        LibToriRS_IOQueueClear(ctx->io);

        for( task->pending_framemap_index = 0;
             task->pending_framemap_index < task->pending_framemap_count;
             task->pending_framemap_index++ )
        {
            fmid = task->pending_framemap_ids[task->pending_framemap_index];
            if( dat2_buildcache_framemap_get(task->bc, fmid) )
                continue;

            IO_REQUEST(ctx, 1, TAPIDat2_FetchArchive(ctx, RSCacheDat2Disk_Table_Skeletons, fmid));
            PT_YIELD(&task->pt);

            fmid = task->pending_framemap_ids[task->pending_framemap_index];
            skel = TAPIDat2_DecodeArchive(ctx, 1, RSCacheDat2Disk_Table_Skeletons, fmid);
            if( !skel )
            {
                TASK_DAT2_ANIM_FATAL(
                    "failed to decode framemap (archive_id=%d framemap_id=%d)\n",
                    task->current_aid,
                    fmid);
            }
            dat2_buildcache_framemap_add_from_archive(task->bc, fmid, skel);
            RSCacheDat2Disk_ArchiveFree(skel);
            skel = NULL;
            LibToriRS_IOQueueClear(ctx->io);
        }

        if( !dat2_buildcache_frames_add_from_fetched_archive(
                task->bc, task->current_aid, task->held_idx0) )
        {
            TASK_DAT2_ANIM_FATAL(
                "frame decode failed (archive_id=%d pending_framemaps=%d)\n",
                task->current_aid,
                task->pending_framemap_count);
        }
        if( !dat2_buildcache_frames_has(task->bc, task->current_aid) )
        {
            TASK_DAT2_ANIM_FATAL(
                "frames missing before submit after decode (archive_id=%d)\n", task->current_aid);
        }
        ToriAuxLibCache_SubmitAnimationFromDat2(task->c, task->current_aid);

        task_dat2_anim_reset_load_state(task);
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->pt);

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

static void
Task_Dat2AnimResolve_Free(struct LibToriRS_Task* base)
{
    struct Task_Dat2AnimResolve* task =
        LibToriRS_container_of(base, struct Task_Dat2AnimResolve, base);
    task_dat2_anim_reset_load_state(task);
    task_dat2_anim_reset_skeletal_state(task);
    dat2_anim_archive_set_free(&task->aset);
    free(task);
}

static struct LibToriRS_TaskVTable g_task_dat2_anim_resolve_vtable = {
    .run_fn = Task_Dat2AnimResolve_Run,
    .free_fn = Task_Dat2AnimResolve_Free,
};

struct LibToriRS_Task*
Task_Dat2AnimResolve_New(
    struct ToriAuxLibCache* c,
    struct Dat2BuildCache* bc,
    const int* seq_ids,
    int seq_count)
{
    struct Task_Dat2AnimResolve* task;

    assert(c);
    assert(bc);
    assert(seq_ids);
    assert(seq_count > 0);
    assert(seq_count <= TASK_DAT2_ANIM_RESOLVE_MAX_SEQS);

    task = calloc(1, sizeof(struct Task_Dat2AnimResolve));
    if( !task )
        return NULL;

    task->base.vtable = &g_task_dat2_anim_resolve_vtable;
    task->c = c;
    task->bc = bc;
    task->seq_count = seq_count;
    memcpy(task->seq_ids, seq_ids, (size_t)seq_count * sizeof(int));
    PT_INIT(&task->pt);
    task_dat2_anim_reset_load_state(task);
    task_dat2_anim_reset_skeletal_state(task);
    dat2_anim_archive_set_init(&task->aset, 0);
    task_dat2_anim_resolve_build_archive_set(task);
    return &task->base;
}
