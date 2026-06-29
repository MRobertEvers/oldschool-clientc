#ifndef TORIAUXLIBC_T_RUNESCAPE_DAT1_PLAYER_ADD_H
#define TORIAUXLIBC_T_RUNESCAPE_DAT1_PLAYER_ADD_H

#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../libtorirs.h"
#include "../../runescape/appearance.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "toriauxlib/c/dat1io.h"
#include "toriauxlib/c/toriauxlibc.h"
#include "toriauxlib/c/toriauxlibc_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toridraw/toridraw_map.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TASK_DAT1_PLAYER_IO_BATCH 64

struct Task_Dat1PlayerAdd
{
    struct pt thread;
    struct ToriAuxLibC* c;
    int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT];
    int readyanim;
    int walkanim;
    int turnanim;
    int runanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
    struct LibToriRS_IOBatch io_batch;
    int model_ids[256];
    int model_count;
    int model_index;
};

static bool
task_dat1_player_sequence_missing(
    struct ToriAuxLibC* c,
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l)
{
    int const anims[] = {
        readyanim, walkanim, turnanim, runanim, walkanim_b, walkanim_r, walkanim_l
    };
    struct ToriAuxLibCore* core = ToriAuxLibC_Core(c);

    for( int i = 0; i < (int)(sizeof(anims) / sizeof(anims[0])); i++ )
    {
        if( anims[i] != -1 && !ToriAuxLibCore_SequenceGet(core, anims[i]) )
            return true;
    }

    return false;
}

struct Task_Dat1PlayerAdd*
Task_Dat1PlayerAdd_New(
    struct ToriAuxLibC* c,
    const int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT],
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l)
{
    struct Task_Dat1PlayerAdd* task = calloc(1, sizeof(struct Task_Dat1PlayerAdd));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->c = c;
    if( appearance )
        memcpy(task->appearance, appearance, sizeof(task->appearance));
    task->readyanim = readyanim;
    task->walkanim = walkanim;
    task->turnanim = turnanim;
    task->runanim = runanim;
    task->walkanim_b = walkanim_b;
    task->walkanim_r = walkanim_r;
    task->walkanim_l = walkanim_l;
    return task;
}

void
Task_Dat1PlayerAdd_Free(struct Task_Dat1PlayerAdd* task)
{
    free(task);
}

int
Task_Dat1PlayerAdd_Run(
    struct Task_Dat1PlayerAdd* task,
    struct LibToriRS_IOContext* ctx)
{
    struct Dat1BuildCache* dat1_bc = dat1(task->c);
    struct ToriAuxLibCore* core = ToriAuxLibC_Core(task->c);

    PT_BEGIN(&task->thread);

    assert(task->c && dat1_bc && core);

    if( ToriDraw_MapCount(dat1_bc->idk_hmap) == 0 )
    {
        IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
        PT_YIELD(&task->thread);

        {
            struct RSCacheShared_FileListDat* config_jag = TAPIDat1_DecodeConfigJagfile(ctx, 0);
            if( config_jag )
                dat1_buildcache_set_fromconfigtable_config_jagfile(dat1_bc, config_jag);
        }
        LibToriRS_IOQueueClear(ctx->io);

        if( dat1_bc->fromconfigtable_config_jagfile )
        {
            dat1_buildcache_idks_init_from_config_jagfile(dat1_bc);
            dat1_buildcache_objs_init_from_config_jagfile(dat1_bc);
        }
    }

    task->model_count = runescape_appearance_collect_model_ids(
        dat1_bc, task->appearance, task->model_ids, 256);

    for( task->model_index = 0; task->model_index < task->model_count; )
    {
        LibToriRS_IOBatchReset(&task->io_batch);
        int batch_end = task->model_index + TASK_DAT1_PLAYER_IO_BATCH;
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

    if( !ToriAuxLibCore_ModelHas(core, RUNESCAPE_PLAYER_PLACEHOLDER_MODEL_ID) )
    {
        struct RSCacheDat2A_Model* model;
        IO_REQUEST(ctx, 0, TAPIDat1_FetchModel(ctx, RUNESCAPE_PLAYER_PLACEHOLDER_MODEL_ID));
        PT_YIELD(&task->thread);

        model = TAPIDat1_DecodeModel(ctx, 0);
        if( model )
        {
            dat1_buildcache_model_add(dat1_bc, RUNESCAPE_PLAYER_PLACEHOLDER_MODEL_ID, model);
            ToriAuxLibC_SubmitModelFromDat1(task->c, RUNESCAPE_PLAYER_PLACEHOLDER_MODEL_ID);
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    if( task_dat1_player_sequence_missing(
            task->c,
            task->readyanim,
            task->walkanim,
            task->turnanim,
            task->runanim,
            task->walkanim_b,
            task->walkanim_r,
            task->walkanim_l) )
    {
        IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
        PT_YIELD(&task->thread);

        if( !TAPIDat1_DecodeConfigJagfile(ctx, 0) )
        {
            PT_EXIT(&task->thread);
        }

        dat1_buildcache_sequences_init_from_config_jagfile(dat1_bc);
        ToriAuxLibC_SubmitAllSequencesFromDat1(task->c);
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->thread);
}

#endif
