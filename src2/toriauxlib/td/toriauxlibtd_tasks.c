#include "toriauxlib/td/toriauxlibtd_tasks.h"

#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "toriauxlib/c/dat1io.h"
#include "toriauxlib/c/toriauxlibc_submit.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat1a/dat1a_anim_frame.h"
#include "osrs/rscache/dat1a/dat1a_config_textures.h"
#include "osrs/rscache/dat1a/dat1a_configs_dat.h"
#include "platforms/platform_x/cachelib_client.h"
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
    struct Task_ToriAuxLibTD_ModelLoad* task = calloc(1, sizeof(struct Task_ToriAuxLibTD_ModelLoad));
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
    int decoded_model_id;

    PT_BEGIN(&task->thread);

    dat1io_model_fetch(ctx, task->model_id);
    PT_YIELD(&task->thread);

    decoded_model_id = -1;
    model = dat1io_model_decode(ctx, &decoded_model_id);
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
    struct Task_ToriAuxLibTD_TexturesLoad* task = (struct Task_ToriAuxLibTD_TexturesLoad*)task_state;
    struct LibToriRS_IOQueueItem item;
    struct RSCacheDat1Disk_Archive* archive;
    struct RSCacheShared_FileListDat* filelist;

    PT_BEGIN(&task->thread);

    {
        struct RSCacheDat2DiskLib_IORequest request;
        cachelib_dat1_textures_archive_fetch(&request);
        LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
    }
    PT_YIELD(&task->thread);

    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        PT_EXIT(&task->thread);
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        PT_EXIT(&task->thread);
    if( item.u.cache.table_id != RSCacheDat1Disk_Table_Configs ||
        item.u.cache.archive_id != RSCacheDat1A_ConfigKind_Textures )
        PT_EXIT(&task->thread);

    archive = item.data;
    if( !archive )
        PT_EXIT(&task->thread);

    filelist = RSCacheShared_FileListDatNewFromCacheDatArchive(archive);
    RSCacheDat1Disk_ArchiveFree(archive);
    if( !filelist )
        PT_EXIT(&task->thread);

    for( int i = 0; i < DAT1_TEXTURE_COUNT; i++ )
    {
        struct RSCacheDat1A_ConfigTexture* cache_texture =
            RSCacheDat1A_ConfigTextureNewFromFilelistDat(filelist, i, 0);
        if( !cache_texture )
            continue;

        int animation_direction;
        int animation_speed;
        tdx_texture_anim_params(i, &animation_direction, &animation_speed);

        struct ToriAuxLibCore_Texture* gc_texture = ToriAuxLibC_TextureNewFromCacheDatTexture(
            cache_texture, animation_direction, animation_speed);
        RSCacheDat1A_ConfigTextureFree(cache_texture);
        if( !gc_texture )
            continue;

        ToriAuxLibC_SubmitTexture(ToriAuxLibTD_C(task->tdx), i, gc_texture);
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
    int pending_fetches;
    int pending_decodes;
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
    struct Task_ToriAuxLibTD_AnimationsLoad* task = (struct Task_ToriAuxLibTD_AnimationsLoad*)task_state;
    struct Dat1BuildCache* dat1_bc = dat1(ToriAuxLibTD_C(task->tdx));
    PT_BEGIN(&task->thread);

    if( !dat1_bc->fromconfigtable_config_jagfile || !dat1_bc->versionlist_jagfile )
    {
        dat1io_config_jagfile_fetch(ctx);
        dat1io_versionlist_jagfile_fetch(ctx);
        PT_YIELD(&task->thread);

        {
            struct RSCacheShared_FileListDat* config_jag = dat1io_config_jagfile_decode(ctx);
            struct RSCacheShared_FileListDat* versionlist_jag = dat1io_versionlist_jagfile_decode(ctx);
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
        task->pending_fetches = 0;
        int batch_end = task->anim_index + TDX_ANIM_IO_BATCH;
        if( batch_end > task->anim_count )
            batch_end = task->anim_count;

        for( ; task->anim_index < batch_end; task->anim_index++ )
        {
            if( !dat1_buildcache_animbaseframes_has(dat1_bc, task->anim_index) )
            {
                dat1io_animations_fetch(ctx, task->anim_index);
                task->pending_fetches++;
            }
        }

        if( task->pending_fetches == 0 )
            continue;

        PT_YIELD(&task->thread);

        for( task->pending_decodes = 0; task->pending_decodes < task->pending_fetches;
             task->pending_decodes++ )
        {
            struct RSCacheDat1A_AnimBaseFrames* abf = NULL;
            int anim_id = dat1io_animations_decode(ctx, &abf);
            if( anim_id >= 0 && abf )
            {
                dat1_buildcache_animbaseframes_add(dat1_bc, anim_id, abf);
                ToriAuxLibC_SubmitAnimationFromDat1(ToriAuxLibTD_C(task->tdx), anim_id);
                ToriAuxLibTD_Animation(task->tdx, anim_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    dat1_buildcache_sequences_init_from_config_jagfile(dat1_bc);
    ToriAuxLibC_SubmitAllSequencesFromDat1(ToriAuxLibTD_C(task->tdx));

    PT_END(&task->thread);
}
