#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat1_buildcache.h"
#include "core/tapi/tapi_dat1.h"
#include "osrs/rscache/dat1a/dat1a_config_npc.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toridraw/toridraw_map.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TASK_DAT1_NPC_IO_BATCH 64

struct Task_Dat1NpcAdd
{
    struct LibToriRS_Task base;
    struct pt pt;
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

static int
Task_Dat1NpcAdd_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1NpcAdd* task = LibToriRS_container_of(base, struct Task_Dat1NpcAdd, base);
    struct Dat1BuildCache* dat1_bc = dat1(task->c);
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);
    struct RSCacheDat1A_ConfigNpc* npc;

    PT_BEGIN(&task->pt);

    assert(task->c && dat1_bc && core);

    if( !dat1_buildcache_npc_get(dat1_bc, task->npc_id) )
    {
        if( !dat1_bc->fromconfigtable_config_jagfile )
        {
            IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
            PT_YIELD(&task->pt);

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
        PT_EXIT(&task->pt);

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

        PT_YIELD(&task->pt);

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
        PT_YIELD(&task->pt);

        if( !TAPIDat1_DecodeConfigJagfile(ctx, 0) )
            PT_EXIT(&task->pt);

        dat1_buildcache_sequences_init_from_config_jagfile(dat1_bc);
        ToriAuxLibCache_SubmitAllSequencesFromDat1(task->c);
        LibToriRS_IOQueueClear(ctx->io);
    }

    npc = dat1_buildcache_npc_get(dat1_bc, task->npc_id);
    if( npc && !ToriAuxLibCore_NpctypeHas(core, task->npc_id) )
    {
        struct ToriAuxLibCore_Npctype* neutral =
            ToriAuxLibCache_NpctypeNewFromDat1ConfigNpc(npc, task->npc_id);
        if( neutral )
            ToriAuxLibCore_NpctypeAdd(core, task->npc_id, neutral);
    }

    PT_END(&task->pt);
}

static void
Task_Dat1NpcAdd_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat1_npc_add_vtable = {
    .run_fn = Task_Dat1NpcAdd_Run,
    .free_fn = Task_Dat1NpcAdd_Free,
};

struct LibToriRS_Task*
Task_Dat1NpcAdd_New(
    struct ToriAuxLibCache* c,
    int npc_id)
{
    struct Task_Dat1NpcAdd* task = calloc(1, sizeof(struct Task_Dat1NpcAdd));
    if( !task )
        return NULL;
    task->base.vtable = &g_task_dat1_npc_add_vtable;
    task->c = c;
    task->npc_id = npc_id;
    PT_INIT(&task->pt);
    return &task->base;
}
