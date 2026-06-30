#ifndef TORIAUXLIB_TASK_RUNESCAPE_DAT1_NPC_ADD_H
#define TORIAUXLIB_TASK_RUNESCAPE_DAT1_NPC_ADD_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "../../../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "osrs/rscache/dat1a/dat1a_config_npc.h"
#include "core/tapi/tapi_dat1.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toridraw/toridraw_map.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TASK_DAT1_NPC_IO_BATCH 64

struct Task_Dat1NpcAdd
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
task_dat1_npc_sequence_missing(
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

struct Task_Dat1NpcAdd*
Task_Dat1NpcAdd_New(
    struct ToriAuxLibCache* c,
    int npc_id)
{
    struct Task_Dat1NpcAdd* task = calloc(1, sizeof(struct Task_Dat1NpcAdd));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->c = c;
    task->npc_id = npc_id;
    return task;
}

void
Task_Dat1NpcAdd_Free(struct Task_Dat1NpcAdd* task)
{
    free(task);
}

int
Task_Dat1NpcAdd_Run(
    struct Task_Dat1NpcAdd* task,
    struct LibToriRS_IOContext* ctx)
{
    struct Dat1BuildCache* dat1_bc = dat1(task->c);
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);
    struct RSCacheDat1A_ConfigNpc* npc;

    PT_BEGIN(&task->thread);

    assert(task->c && dat1_bc && core);

    if( !dat1_buildcache_npc_get(dat1_bc, task->npc_id) )
    {
        if( !dat1_bc->fromconfigtable_config_jagfile )
        {
            IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
            PT_YIELD(&task->thread);

            {
                struct RSCacheShared_FileListDat* config_jag = TAPIDat1_DecodeConfigJagfile(ctx, 0);
                if( config_jag )
                    dat1_buildcache_set_fromconfigtable_config_jagfile(dat1_bc, config_jag);
            }
            LibToriRS_IOQueueClear(ctx->io);
        }

        if( dat1_bc->fromconfigtable_config_jagfile )
            dat1_buildcache_npc_load_from_config_jagfile(dat1_bc, task->npc_id);
    }

    npc = dat1_buildcache_npc_get(dat1_bc, task->npc_id);
    if( !npc )
        PT_EXIT(&task->thread);

    task->npc_anims[0] = npc->readyanim;
    task->npc_anims[1] = npc->walkanim;
    task->npc_anims[2] = -1;
    task->npc_anims[3] = -1;
    task->npc_anims[4] = npc->walkanim_b;
    task->npc_anims[5] = npc->walkanim_r;
    task->npc_anims[6] = npc->walkanim_l;

    task->model_count = 0;
    for( int i = 0; i < npc->models_count && task->model_count < 256; i++ )
        task->model_ids[task->model_count++] = npc->models[i];

    for( task->model_index = 0; task->model_index < task->model_count; )
    {
        LibToriRS_IOBatchReset(&task->io_batch);
        int batch_end = task->model_index + TASK_DAT1_NPC_IO_BATCH;
        if( batch_end > task->model_count )
            batch_end = task->model_count;

        for( ; task->model_index < batch_end; task->model_index++ )
        {
            int model_id = task->model_ids[task->model_index];
            if( !dat1_buildcache_model_get(dat1_bc, model_id) )
            {
                int slot = LibToriRS_IOBatchAdd(&task->io_batch, model_id);
                IO_REQUEST(ctx, slot, TAPIDat1_FetchModel(ctx, model_id));
            }
        }

        if( LibToriRS_IOBatchEmpty(&task->io_batch) )
            continue;

        PT_YIELD(&task->thread);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            struct RSCacheDat2A_Model* model = TAPIDat1_DecodeModel(ctx, i);
            if( model )
            {
                int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
                dat1_buildcache_model_add(dat1_bc, model_id, model);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    for( int i = 0; i < task->model_count; i++ )
        ToriAuxLibCache_SubmitModelFromDat1(task->c, task->model_ids[i]);

    if( task_dat1_npc_sequence_missing(task->c, task->npc_anims, 7) )
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
