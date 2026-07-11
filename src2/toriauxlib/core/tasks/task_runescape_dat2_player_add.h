#ifndef TORIAUXLIB_TASK_RUNESCAPE_DAT2_PLAYER_ADD_H
#define TORIAUXLIB_TASK_RUNESCAPE_DAT2_PLAYER_ADD_H

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "../../../libtorirs.h"
#include "../../../runescape/appearance.h"
#include "3rd/minipt.h"
#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_dat2.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/core_task_await.h"
#include "toriauxlib/core/tasks/task_dat2_anim_io.h"
#include "toriauxlib/core/tasks/task_dat2_io.h"
#include "toriauxlib/core/tasks/task_runescape_dat2_anim_load.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TASK_DAT2_PLAYER_IO_BATCH 64

struct Task_Dat2PlayerAdd
{
    struct pt thread;
    struct ToriAuxLibCache* c;
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
    int want_idk_ids[RUNESCAPE_APPEARANCE_SLOT_COUNT];
    int want_idk_count;
    int want_obj_ids[RUNESCAPE_APPEARANCE_SLOT_COUNT];
    int want_obj_count;
    bool need_idk;
    bool need_obj;
    struct Task_Dat2AnimResolve anim_resolve;
    bool anim_resolve_ready;
    struct RSCacheDat2Disk_Archive* sequence_archive;
};

static bool
task_dat2_player_sequence_missing(
    struct ToriAuxLibCache* c,
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
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(c);

    for( int i = 0; i < (int)(sizeof(anims) / sizeof(anims[0])); i++ )
    {
        if( anims[i] != -1 && !ToriAuxLibCore_SequenceGet(core, anims[i]) )
            return true;
    }

    return false;
}

struct Task_Dat2PlayerAdd*
Task_Dat2PlayerAdd_New(
    struct ToriAuxLibCache* c,
    const int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT],
    int readyanim,
    int walkanim,
    int turnanim,
    int runanim,
    int walkanim_b,
    int walkanim_r,
    int walkanim_l)
{
    struct Task_Dat2PlayerAdd* task = calloc(1, sizeof(struct Task_Dat2PlayerAdd));
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
Task_Dat2PlayerAdd_Free(struct Task_Dat2PlayerAdd* task)
{
    if( !task )
        return;
    Task_Dat2AnimResolve_Destroy(&task->anim_resolve);
    if( task->sequence_archive )
    {
        RSCacheDat2Disk_ArchiveFree(task->sequence_archive);
        task->sequence_archive = NULL;
    }
    free(task);
}

int
Task_Dat2PlayerAdd_Run(
    struct Task_Dat2PlayerAdd* task,
    struct LibToriRS_IOContext* ctx)
{
    struct Dat2BuildCache* dat2_bc = dat2(task->c);
    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->c);
    struct RSCacheDat2Disk_Archive* identkit_archive = NULL;
    struct RSCacheDat2Disk_Archive* object_archive = NULL;
    int const player_anims[] = {
        task->readyanim,  task->walkanim,   task->turnanim,   task->runanim,
        task->walkanim_b, task->walkanim_r, task->walkanim_l,
    };
    int player_anim_count = (int)(sizeof(player_anims) / sizeof(player_anims[0]));

    PT_BEGIN(&task->thread);

    assert(task->c && dat2_bc && core);

    runescape_appearance_collect_config_ids(
        task->appearance,
        task->want_idk_ids,
        &task->want_idk_count,
        RUNESCAPE_APPEARANCE_SLOT_COUNT,
        task->want_obj_ids,
        &task->want_obj_count,
        RUNESCAPE_APPEARANCE_SLOT_COUNT);

    task->need_idk = false;
    for( int i = 0; i < task->want_idk_count; i++ )
    {
        if( !dat2_buildcache_identkit_get(dat2_bc, task->want_idk_ids[i]) )
        {
            task->need_idk = true;
            break;
        }
    }

    task->need_obj = false;
    for( int i = 0; i < task->want_obj_count; i++ )
    {
        if( !dat2_buildcache_object_get(dat2_bc, task->want_obj_ids[i]) )
        {
            task->need_obj = true;
            break;
        }
    }

    if( task->need_idk || task->need_obj )
    {
        int io_slot = 0;

        if( task->need_idk )
            IO_REQUEST(
                ctx, io_slot++, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Identkit));
        if( task->need_obj )
            IO_REQUEST(
                ctx, io_slot++, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Object));
        PT_YIELD(&task->thread);

        DAT2_ENSURE_CONFIGS_REFERENCE_TABLE(ctx, &task->thread, task->c);

        io_slot = 0;
        if( task->need_idk )
        {
            identkit_archive =
                TAPIDat2_DecodeConfigGroup(ctx, io_slot++, RSCacheDat2A_ConfigKind_Identkit);
            if( identkit_archive )
            {
                dat2_buildcache_identkits_init_from_archive(
                    dat2_bc, identkit_archive, task->want_idk_ids, task->want_idk_count);
            }
            if( identkit_archive )
                RSCacheDat2Disk_ArchiveFree(identkit_archive);
            identkit_archive = NULL;
        }
        if( task->need_obj )
        {
            object_archive =
                TAPIDat2_DecodeConfigGroup(ctx, io_slot++, RSCacheDat2A_ConfigKind_Object);
            if( object_archive )
            {
                dat2_buildcache_objects_init_from_archive(
                    dat2_bc, object_archive, task->want_obj_ids, task->want_obj_count);
            }
            if( object_archive )
                RSCacheDat2Disk_ArchiveFree(object_archive);
            object_archive = NULL;
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    task->model_count = runescape_appearance_collect_model_ids_dat2(
        dat2_bc, task->appearance, task->model_ids, 256);

    for( task->model_index = 0; task->model_index < task->model_count; )
    {
        LibToriRS_IOBatchReset(&task->io_batch);
        int batch_end = task->model_index + TASK_DAT2_PLAYER_IO_BATCH;
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

    if( !ToriAuxLibCore_ModelHas(core, RUNESCAPE_PLAYER_PLACEHOLDER_MODEL_ID) )
    {
        struct RSCacheDat2A_Model* model;
        IO_REQUEST(ctx, 0, TAPIDat2_FetchModel(ctx, RUNESCAPE_PLAYER_PLACEHOLDER_MODEL_ID));
        PT_YIELD(&task->thread);

        model = TAPIDat2_DecodeModel(ctx, 0);
        if( model )
        {
            dat2_buildcache_model_add(dat2_bc, RUNESCAPE_PLAYER_PLACEHOLDER_MODEL_ID, model);
            ToriAuxLibCache_SubmitModelFromDat2(task->c, RUNESCAPE_PLAYER_PLACEHOLDER_MODEL_ID);
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    if( task_dat2_player_sequence_missing(
            task->c,
            task->readyanim,
            task->walkanim,
            task->turnanim,
            task->runanim,
            task->walkanim_b,
            task->walkanim_r,
            task->walkanim_l) )
    {
        task->sequence_archive = NULL;

        IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Sequence));
        PT_YIELD(&task->thread);

        task->sequence_archive =
            TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Sequence);
        if( !task->sequence_archive )
            PT_EXIT(&task->thread);

        DAT2_ENSURE_CONFIGS_REFERENCE_TABLE(ctx, &task->thread, task->c);

        if( !task->anim_resolve_ready )
        {
            struct Dat2AnimArchiveSet aset;

            Task_Dat2AnimResolve_Init(&task->anim_resolve, task->c, dat2_bc);
            dat2_anim_archive_set_init(&aset, 32);
            if( aset.ids )
            {
                for( int ai = 0; ai < player_anim_count; ai++ )
                {
                    int seq_id = player_anims[ai];
                    if( seq_id == -1 )
                        continue;

                    dat2_buildcache_sequence_load_from_archive(
                        dat2_bc, task->sequence_archive, seq_id);

                    struct RSCacheDat2A_ConfigSequence* seq =
                        dat2_buildcache_sequence_get(dat2_bc, seq_id);
                    if( !seq )
                        continue;

                    dat2_anim_set_add_sequence_archives(&aset, seq);
                    Task_Dat2AnimResolve_AddSequenceId(&task->anim_resolve, seq_id);
                }

                Task_Dat2AnimResolve_SetArchiveSet(&task->anim_resolve, &aset);
                dat2_anim_archive_set_free(&aset);
                task->anim_resolve_ready = true;
            }
        }

        if( task->anim_resolve_ready )
        {
            TASK_AWAIT(&task->thread, Task_Dat2AnimResolve_Run(&task->anim_resolve, ctx));
            Task_Dat2AnimResolve_Destroy(&task->anim_resolve);
            task->anim_resolve_ready = false;
            dat2_anim_submit_all_skeletal(task->c, dat2_bc);
        }

        ToriAuxLibCache_SubmitAllSequencesFromDat2(task->c);
        if( task->sequence_archive )
        {
            RSCacheDat2Disk_ArchiveFree(task->sequence_archive);
            task->sequence_archive = NULL;
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->thread);
}

#endif
