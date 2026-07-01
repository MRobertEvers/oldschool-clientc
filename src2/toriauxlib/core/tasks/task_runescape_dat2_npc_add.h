#ifndef TORIAUXLIB_TASK_RUNESCAPE_DAT2_NPC_ADD_H
#define TORIAUXLIB_TASK_RUNESCAPE_DAT2_NPC_ADD_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "../../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_config_npctype.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/core_task_await.h"
#include "toriauxlib/core/tasks/task_dat2_anim_io.h"
#include "toriauxlib/core/tasks/task_dat2_io.h"
#include "toriauxlib/core/tasks/task_runescape_dat2_anim_load.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TASK_DAT2_NPC_IO_BATCH 64

struct Task_Dat2NpcAdd
{
    struct pt thread;
    struct ToriAuxLibCache* c;
    int npc_id;
    struct LibToriRS_IOBatch io_batch;
    int model_ids[256];
    int model_count;
    int model_index;
    int npc_anims[7];
};

static bool
task_dat2_npc_sequence_missing(
    struct ToriAuxLibCache* c,
    const int* anims,
    int count)
{
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(c);

    for( int i = 0; i < count; i++ )
    {
        if( anims[i] != -1 && !ToriAuxLibCore_SequenceGet(core, anims[i]) )
            return true;
    }

    return false;
}

static void
task_dat2_npc_assert_classic_sequence_frames(
    struct Dat2BuildCache* dat2_bc,
    struct RSCacheDat2A_ConfigSequence* seq)
{
    assert(dat2_bc && seq);

    if( seq->anim_maya_id >= 0 && seq->frame_count == 0 )
        return;

    for( int fi = 0; fi < seq->frame_count; fi++ )
    {
        int aid = (seq->frame_ids[fi] >> 16) & 0xFFFF;
        if( aid < 0 )
            continue;
        assert(dat2_buildcache_frames_has(dat2_bc, aid) &&
               "npc classic sequence missing frame archive after IO");
    }
}

static void
task_dat2_npc_submit_sequence_assets(
    struct Task_Dat2NpcAdd* task,
    struct Dat2BuildCache* dat2_bc,
    struct ToriAuxLibCore* core,
    int seq_id,
    struct RSCacheDat2A_ConfigSequence* seq)
{
    assert(task && dat2_bc && core && seq);

    if( seq->anim_maya_id >= 0 && seq->frame_count == 0 )
    {
        if( dat2_buildcache_skeletal_has(dat2_bc, seq->anim_maya_id) )
            dat2_anim_submit_sequence_skeletal(task->c, dat2_bc, seq);
        else
        {
            fprintf(
                stderr,
                "Task_Dat2NpcAdd: npc_id=%d seq_id=%d anim_maya_id=%d animaya_aid=%d not loaded; "
                "NPC will spawn without this skeletal anim\n",
                task->npc_id,
                seq_id,
                seq->anim_maya_id,
                seq->anim_maya_id >> 16);
        }
    }
    else
        task_dat2_npc_assert_classic_sequence_frames(dat2_bc, seq);

    ToriAuxLibCache_SubmitSequenceFromDat2(task->c, seq_id);
    assert(ToriAuxLibCore_SequenceGet(core, seq_id) &&
           "npc sequence not in core after submit");
}

struct Task_Dat2NpcAdd*
Task_Dat2NpcAdd_New(
    struct ToriAuxLibCache* c,
    int npc_id)
{
    struct Task_Dat2NpcAdd* task = calloc(1, sizeof(struct Task_Dat2NpcAdd));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->c = c;
    task->npc_id = npc_id;
    return task;
}

void
Task_Dat2NpcAdd_Free(struct Task_Dat2NpcAdd* task)
{
    free(task);
}

int
Task_Dat2NpcAdd_Run(
    struct Task_Dat2NpcAdd* task,
    struct LibToriRS_IOContext* ctx)
{
    struct Dat2BuildCache* dat2_bc = dat2(task->c);
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);
    struct RSCacheDat2Disk_Archive* npc_archive = NULL;
    struct RSCacheDat2Disk_Archive* sequence_archive = NULL;
    struct RSCacheDat2A_ConfigNpctype* npc;
    int const wanted_npc_ids[] = { task->npc_id };

    PT_BEGIN(&task->thread);

    assert(task->c && dat2_bc && core);

    if( !dat2_buildcache_npctype_get(dat2_bc, task->npc_id) )
    {
        IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Npc));
        PT_YIELD(&task->thread);

        DAT2_ENSURE_CONFIGS_REFERENCE_TABLE(ctx, &task->thread, task->c);

        npc_archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Npc);
        if( npc_archive )
        {
            dat2_buildcache_npctypes_init_from_archive(
                dat2_bc, npc_archive, wanted_npc_ids, 1);
        }
        if( npc_archive )
            RSCacheDat2Disk_ArchiveFree(npc_archive);
        npc_archive = NULL;
        LibToriRS_IOQueueClear(ctx->io);
    }

    npc = dat2_buildcache_npctype_get(dat2_bc, task->npc_id);
    assert(npc && "npctype missing after IO");

    task->npc_anims[0] = npc->standing_animation;
    task->npc_anims[1] = npc->walking_animation;
    task->npc_anims[2] = npc->rotate180_animation;
    task->npc_anims[3] = npc->run_animation;
    task->npc_anims[4] = npc->idle_rotate_left_animation;
    task->npc_anims[5] = npc->rotate_right_animation;
    task->npc_anims[6] = npc->rotate_left_animation;

    task->model_count = 0;
    for( int i = 0; i < npc->models_count && task->model_count < 256; i++ )
        task->model_ids[task->model_count++] = npc->models[i];

    for( task->model_index = 0; task->model_index < task->model_count; )
    {
        LibToriRS_IOBatchReset(&task->io_batch);
        int batch_end = task->model_index + TASK_DAT2_NPC_IO_BATCH;
        if( batch_end > task->model_count )
            batch_end = task->model_count;

        for( ; task->model_index < batch_end; task->model_index++ )
        {
            int model_id = task->model_ids[task->model_index];
            if( !dat2_buildcache_model_get(dat2_bc, model_id) )
            {
                int slot = LibToriRS_IOBatchAdd(&task->io_batch, model_id);
                IO_REQUEST(ctx, slot, TAPIDat2_FetchModel(ctx, model_id));
            }
        }

        if( LibToriRS_IOBatchEmpty(&task->io_batch) )
            continue;

        PT_YIELD(&task->thread);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            struct RSCacheDat2A_Model* model = TAPIDat2_DecodeModel(ctx, i);
            if( model )
            {
                int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
                dat2_buildcache_model_add(dat2_bc, model_id, model);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    for( int i = 0; i < task->model_count; i++ )
    {
        int model_id = task->model_ids[i];
        assert(dat2_buildcache_model_get(dat2_bc, model_id) && "npc model missing after IO");
        ToriAuxLibCache_SubmitModelFromDat2(task->c, model_id);
        assert(ToriAuxLibCore_ModelHas(core, model_id) && "npc model not in core after submit");
    }

    if( task_dat2_npc_sequence_missing(task->c, task->npc_anims, 7) )
    {
        IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Sequence));
        PT_YIELD(&task->thread);

        DAT2_ENSURE_CONFIGS_REFERENCE_TABLE(ctx, &task->thread, task->c);

        sequence_archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Sequence);
        assert(sequence_archive && "npc sequence config group missing after IO");

        struct Dat2AnimArchiveSet aset;
        struct Task_Dat2AnimResolve anim_resolve;
        dat2_anim_archive_set_init(&aset, 32);
        Task_Dat2AnimResolve_Init(&anim_resolve, task->c, dat2_bc);
        assert(aset.ids && "npc anim archive set alloc failed");

        for( int ai = 0; ai < 7; ai++ )
        {
            int seq_id = task->npc_anims[ai];
            struct RSCacheDat2A_ConfigSequence* seq;

            if( seq_id == -1 )
                continue;

            dat2_buildcache_sequence_load_from_archive(dat2_bc, sequence_archive, seq_id);

            seq = dat2_buildcache_sequence_get(dat2_bc, seq_id);
            assert(seq && "npc sequence missing after IO");

            dat2_anim_set_add_sequence_archives(&aset, seq);
            Task_Dat2AnimResolve_AddSequenceId(&anim_resolve, seq_id);
        }

        Task_Dat2AnimResolve_SetArchiveSet(&anim_resolve, &aset);
        TASK_AWAIT(&task->thread, Task_Dat2AnimResolve_Run(&anim_resolve, ctx));

        for( int ai = 0; ai < 7; ai++ )
        {
            int seq_id = task->npc_anims[ai];
            struct RSCacheDat2A_ConfigSequence* seq;

            if( seq_id == -1 )
                continue;

            seq = dat2_buildcache_sequence_get(dat2_bc, seq_id);
            assert(seq && "npc sequence missing after anim resolve");

            task_dat2_npc_submit_sequence_assets(task, dat2_bc, core, seq_id, seq);
        }

        dat2_anim_archive_set_free(&aset);
        RSCacheDat2Disk_ArchiveFree(sequence_archive);
        LibToriRS_IOQueueClear(ctx->io);
    }

    npc = dat2_buildcache_npctype_get(dat2_bc, task->npc_id);
    assert(npc && "npctype missing after IO");
    if( !ToriAuxLibCore_NpctypeHas(core, task->npc_id) )
    {
        struct ToriAuxLibCore_Npctype* neutral =
            ToriAuxLibCache_NpctypeNewFromDat2ConfigNpctype(npc, task->npc_id);
        assert(neutral && "npc npctype core conversion failed");
        ToriAuxLibCore_NpctypeAdd(core, task->npc_id, neutral);
    }

    PT_END(&task->thread);
}

#endif
