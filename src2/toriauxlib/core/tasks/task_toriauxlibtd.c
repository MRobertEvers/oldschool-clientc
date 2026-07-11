#include "task_toriauxlibtd.h"

#include "../../../ioqueue/libtorirs_ioqueue.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "core/tapi/tapi_dat1.h"
#include "osrs/rscache/dat1a/dat1a_anim_frame.h"
#include "osrs/rscache/dat1a/dat1a_config_textures.h"
#include "osrs/rscache/dat1a/dat1a_configs_dat.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "platforms/platform_x/cachelib_client.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toridraw/toridraw_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TDX_ANIM_IO_BATCH 64

struct Task_ToriAuxLibTD_ModelLoad
{
    struct pt thread;
    struct ToriAuxLibTD* tdx;
    int model_id;
};

struct Task_ToriAuxLibTD_ModelLoad*
Task_ToriAuxLibTD_ModelLoad_New(
    struct ToriAuxLibTD* tdx,
    int model_id)
{
    struct Task_ToriAuxLibTD_ModelLoad* task =
        calloc(1, sizeof(struct Task_ToriAuxLibTD_ModelLoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->tdx = tdx;
    task->model_id = model_id;
    return task;
}

void
Task_ToriAuxLibTD_ModelLoad_Free(struct Task_ToriAuxLibTD_ModelLoad* task)
{
    free(task);
}

int
Task_ToriAuxLibTD_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibTD_ModelLoad* task = (struct Task_ToriAuxLibTD_ModelLoad*)task_state;
    struct RSCacheDat2A_Model* model;

    PT_BEGIN(&task->thread);

    IO_REQUEST(ctx, 0, TAPIDat1_FetchModel(ctx, task->model_id));
    PT_YIELD(&task->thread);

    model = TAPIDat1_DecodeModel(ctx, 0);
    if( !model )
    {
        fprintf(stderr, "Task_ToriAuxLibTD_ModelLoad: failed to decode model %d\n", task->model_id);
        PT_EXIT(&task->thread);
    }

    dat1_buildcache_model_add(dat1(ToriAuxLibTD_C(task->tdx)), task->model_id, model);
    ToriAuxLibTD_SubmitModelFromDat1(task->tdx, task->model_id);
    ToriAuxLibTD_Model(task->tdx, task->model_id);

    PT_END(&task->thread);
}

static void
tdx_texture_anim_params(
    int texture_id,
    int* animation_direction,
    int* animation_speed)
{
    *animation_direction = TORIDRAW_TEXANIM_DIRECTION_NONE;
    *animation_speed = 0;
    if( texture_id == 17 || texture_id == 24 )
    {
        *animation_direction = TORIDRAW_TEXANIM_DIRECTION_V_DOWN;
        *animation_speed = 2;
    }
}

struct Task_ToriAuxLibTD_TexturesLoad
{
    struct pt thread;
    struct ToriAuxLibTD* tdx;
};

struct Task_ToriAuxLibTD_TexturesLoad*
Task_ToriAuxLibTD_TexturesLoad_New(struct ToriAuxLibTD* tdx)
{
    struct Task_ToriAuxLibTD_TexturesLoad* task =
        calloc(1, sizeof(struct Task_ToriAuxLibTD_TexturesLoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->tdx = tdx;
    return task;
}

void
Task_ToriAuxLibTD_TexturesLoad_Free(struct Task_ToriAuxLibTD_TexturesLoad* task)
{
    free(task);
}

int
Task_ToriAuxLibTD_TexturesLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibTD_TexturesLoad* task =
        (struct Task_ToriAuxLibTD_TexturesLoad*)task_state;
    struct RSCacheShared_FileListDat* filelist;

    PT_BEGIN(&task->thread);

    IO_REQUEST(ctx, 0, TAPIDat1_FetchTexturesJagfile(ctx));
    PT_YIELD(&task->thread);

    filelist = TAPIDat1_DecodeTexturesJagfile(ctx, 0);
    if( !filelist )
        PT_EXIT(&task->thread);
    LibToriRS_IOQueueClear(ctx->io);

    for( int i = 0; i < DAT1_TEXTURE_COUNT; i++ )
    {
        struct RSCacheDat1A_ConfigTexture* cache_texture =
            RSCacheDat1A_ConfigTextureNewFromFilelistDat(filelist, i, 0);
        if( !cache_texture )
            continue;

        int animation_direction;
        int animation_speed;
        tdx_texture_anim_params(i, &animation_direction, &animation_speed);

        struct ToriAuxLibCore_Texture* gc_texture = ToriAuxLibCache_TextureNewFromCacheDatTexture(
            cache_texture, animation_direction, animation_speed);
        RSCacheDat1A_ConfigTextureFree(cache_texture);
        if( !gc_texture )
            continue;

        ToriAuxLibCache_SubmitTexture(ToriAuxLibTD_C(task->tdx), i, gc_texture);
        ToriAuxLibTD_Texture(task->tdx, i);
    }

    RSCacheShared_FileListDatFree(filelist);
    PT_END(&task->thread);
}

struct Task_ToriAuxLibTD_AnimationsLoad
{
    struct pt thread;
    struct ToriAuxLibTD* tdx;
    int anim_count;
    int anim_index;
    struct LibToriRS_IOBatch io_batch;
};

struct Task_ToriAuxLibTD_AnimationsLoad*
Task_ToriAuxLibTD_AnimationsLoad_New(struct ToriAuxLibTD* tdx)
{
    struct Task_ToriAuxLibTD_AnimationsLoad* task =
        calloc(1, sizeof(struct Task_ToriAuxLibTD_AnimationsLoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->tdx = tdx;
    return task;
}

void
Task_ToriAuxLibTD_AnimationsLoad_Free(struct Task_ToriAuxLibTD_AnimationsLoad* task)
{
    free(task);
}

int
Task_ToriAuxLibTD_AnimationsLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibTD_AnimationsLoad* task =
        (struct Task_ToriAuxLibTD_AnimationsLoad*)task_state;
    struct Dat1BuildCache* dat1_bc = dat1(ToriAuxLibTD_C(task->tdx));
    PT_BEGIN(&task->thread);

    if( !dat1_bc->fromconfigtable_config_jagfile || !dat1_bc->versionlist_jagfile )
    {
        IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
        IO_REQUEST(ctx, 1, TAPIDat1_FetchVersionlistJagfile(ctx));
        PT_YIELD(&task->thread);

        {
            struct RSCacheShared_FileListDat* config_jag = TAPIDat1_DecodeConfigJagfile(ctx, 0);
            struct RSCacheShared_FileListDat* versionlist_jag =
                TAPIDat1_DecodeVersionlistJagfile(ctx, 1);
            if( config_jag )
                dat1_buildcache_set_fromconfigtable_config_jagfile(dat1_bc, config_jag);
            if( versionlist_jag )
                dat1_buildcache_set_versionlist_jagfile(dat1_bc, versionlist_jag);
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    task->anim_count = dat1_buildcache_get_animbaseframes_count_from_versionlist_jagfile(dat1_bc);
    for( task->anim_index = 0; task->anim_index < task->anim_count; )
    {
        LibToriRS_IOBatchReset(&task->io_batch);
        int batch_end = task->anim_index + TDX_ANIM_IO_BATCH;
        if( batch_end > task->anim_count )
            batch_end = task->anim_count;

        for( ; task->anim_index < batch_end; task->anim_index++ )
        {
            if( !dat1_buildcache_animbaseframes_has(dat1_bc, task->anim_index) )
            {
                int slot = LibToriRS_IOBatchAdd(&task->io_batch, 0);
                IO_REQUEST(ctx, slot, TAPIDat1_FetchAnimations(ctx, task->anim_index));
            }
        }

        if( LibToriRS_IOBatchEmpty(&task->io_batch) )
            continue;

        PT_YIELD(&task->thread);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            struct RSCacheDat1A_AnimBaseFrames* abf = NULL;
            int anim_id = TAPIDat1_DecodeAnimations(ctx, i, &abf);
            if( anim_id >= 0 && abf )
            {
                dat1_buildcache_animbaseframes_add(dat1_bc, anim_id, abf);
                ToriAuxLibCache_SubmitAnimationFromDat1(ToriAuxLibTD_C(task->tdx), anim_id);
                ToriAuxLibTD_Animation(task->tdx, anim_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    dat1_buildcache_sequences_init_from_config_jagfile(dat1_bc);
    ToriAuxLibCache_SubmitAllSequencesFromDat1(ToriAuxLibTD_C(task->tdx));

    PT_END(&task->thread);
}
