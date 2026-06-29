#ifndef TORIAUXLIBC_T_RUNESCAPE_DAT2_NPC_ADD_H
#define TORIAUXLIBC_T_RUNESCAPE_DAT2_NPC_ADD_H

#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "osrs/rscache/dat2a/dat2a_config_npctype.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/c/dat2io.h"
#include "toriauxlib/c/toriauxlibc.h"
#include "toriauxlib/c/toriauxlibc_submit.h"
#include "toriauxlib/c/t_runescape_dat2_anim_load.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TASK_DAT2_NPC_IO_BATCH 64

struct Task_Dat2NpcAdd
{
    struct pt thread;
    struct ToriAuxLibC* c;
    int npc_id;
    struct LibToriRS_IOBatch io_batch;
    int model_ids[256];
    int model_count;
    int model_index;
    int npc_anims[7];
};

static bool
task_dat2_npc_sequence_missing(
    struct ToriAuxLibC* c,
    const int* anims,
    int count)
{
    struct ToriAuxLibCore* core = ToriAuxLibC_Core(c);

    for( int i = 0; i < count; i++ )
    {
        if( anims[i] != -1 && !ToriAuxLibCore_SequenceGet(core, anims[i]) )
            return true;
    }

    return false;
}

struct Task_Dat2NpcAdd*
Task_Dat2NpcAdd_New(
    struct ToriAuxLibC* c,
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
    struct RSCacheDat2Disk* cache_disk = ToriAuxLibC_Dat2Disk(task->c);
    struct ToriAuxLibCore* core = ToriAuxLibC_Core(task->c);
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

        npc_archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Npc);
        if( npc_archive && cache_disk )
        {
            dat2_buildcache_npctypes_init_from_archive(
                dat2_bc, cache_disk, npc_archive, wanted_npc_ids, 1);
        }
        if( npc_archive )
            RSCacheDat2Disk_ArchiveFree(npc_archive);
        npc_archive = NULL;
        LibToriRS_IOQueueClear(ctx->io);
    }

    npc = dat2_buildcache_npctype_get(dat2_bc, task->npc_id);
    if( !npc )
        PT_EXIT(&task->thread);

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
        ToriAuxLibC_SubmitModelFromDat2(task->c, task->model_ids[i]);

    if( task_dat2_npc_sequence_missing(task->c, task->npc_anims, 7) )
    {
        IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Sequence));
        PT_YIELD(&task->thread);

        sequence_archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Sequence);
        if( !sequence_archive || !cache_disk )
        {
            if( sequence_archive )
                RSCacheDat2Disk_ArchiveFree(sequence_archive);
            PT_EXIT(&task->thread);
        }

        struct Dat2AnimArchiveSet aset;
        dat2_anim_archive_set_init(&aset, 32);
        if( aset.ids )
        {
            for( int ai = 0; ai < 7; ai++ )
            {
                int seq_id = task->npc_anims[ai];
                if( seq_id == -1 )
                    continue;

                dat2_buildcache_sequence_load_from_archive(
                    dat2_bc, cache_disk, sequence_archive, seq_id);

                struct RSCacheDat2A_ConfigSequence* seq =
                    dat2_buildcache_sequence_get(dat2_bc, seq_id);
                if( !seq )
                    continue;

                dat2_anim_set_add_sequence_archives(&aset, seq);
                dat2_anim_cache_sequence_skeletal(dat2_bc, cache_disk, seq);
            }

            dat2_anim_submit_archive_set(task->c, dat2_bc, cache_disk, &aset);
            dat2_anim_submit_all_skeletal(task->c, dat2_bc);
            dat2_anim_archive_set_free(&aset);
        }

        ToriAuxLibC_SubmitAllSequencesFromDat2(task->c);
        RSCacheDat2Disk_ArchiveFree(sequence_archive);
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->thread);
}

#endif
