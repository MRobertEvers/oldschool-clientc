#include "toridrawx_tasks.h"

#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "gamecache/dat1io.h"
#include "gamecache/gamecache_submit.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/animframe.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "platforms/platform_x/cachelib_client.h"
#include "toridraw/toridraw_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TDX_ANIM_IO_BATCH 64

struct Task_ToriDrawX_ModelLoad
{
    struct pt thread;
    struct ToriDrawX* tdx;
    int model_id;
};

struct Task_ToriDrawX_ModelLoad*
Task_ToriDrawX_ModelLoad_New(
    struct ToriDrawX* tdx,
    int model_id)
{
    struct Task_ToriDrawX_ModelLoad* task = calloc(1, sizeof(struct Task_ToriDrawX_ModelLoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->tdx = tdx;
    task->model_id = model_id;
    return task;
}

void
Task_ToriDrawX_ModelLoad_Free(struct Task_ToriDrawX_ModelLoad* task)
{
    free(task);
}

int
Task_ToriDrawX_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriDrawX_ModelLoad* task = (struct Task_ToriDrawX_ModelLoad*)task_state;
    struct CacheModel* model;
    int decoded_model_id;

    PT_BEGIN(&task->thread);

    dat1io_model_fetch(ctx, task->model_id);
    PT_YIELD(&task->thread);

    decoded_model_id = -1;
    model = dat1io_model_decode(ctx, &decoded_model_id);
    if( !model )
    {
        fprintf(stderr, "Task_ToriDrawX_ModelLoad: failed to decode model %d\n", task->model_id);
        PT_EXIT(&task->thread);
    }

    dat1_buildcache_model_add(dat1(ToriDrawX_GamecacheL(task->tdx)), task->model_id, model);
    ToriDrawX_SubmitModelFromDat1(task->tdx, task->model_id);
    ToriDrawX_Model(task->tdx, task->model_id);

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

struct Task_ToriDrawX_TexturesLoad
{
    struct pt thread;
    struct ToriDrawX* tdx;
};

struct Task_ToriDrawX_TexturesLoad*
Task_ToriDrawX_TexturesLoad_New(struct ToriDrawX* tdx)
{
    struct Task_ToriDrawX_TexturesLoad* task =
        calloc(1, sizeof(struct Task_ToriDrawX_TexturesLoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->tdx = tdx;
    return task;
}

void
Task_ToriDrawX_TexturesLoad_Free(struct Task_ToriDrawX_TexturesLoad* task)
{
    free(task);
}

int
Task_ToriDrawX_TexturesLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriDrawX_TexturesLoad* task = (struct Task_ToriDrawX_TexturesLoad*)task_state;
    struct LibToriRS_IOQueueItem item;
    struct CacheDatArchive* archive;
    struct FileListDat* filelist;

    PT_BEGIN(&task->thread);

    {
        struct CacheLib_IORequest request;
        cachelib_dat1_textures_archive_fetch(&request);
        LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
    }
    PT_YIELD(&task->thread);

    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        PT_EXIT(&task->thread);
    if( item.kind != TORIRSIO_KIND_CACHE || item.status != TORIRSIO_STAT_DONE ||
        item.error_code != 0 )
        PT_EXIT(&task->thread);
    if( item.u.cache.table_id != CACHE_DAT_CONFIGS ||
        item.u.cache.archive_id != CONFIG_DAT_TEXTURES )
        PT_EXIT(&task->thread);

    archive = item.data;
    if( !archive )
        PT_EXIT(&task->thread);

    filelist = filelist_dat_new_from_cache_dat_archive(archive);
    cache_dat_archive_free(archive);
    if( !filelist )
        PT_EXIT(&task->thread);

    for( int i = 0; i < DAT1_TEXTURE_COUNT; i++ )
    {
        struct CacheDatTexture* cache_texture =
            cache_dat_texture_new_from_filelist_dat(filelist, i, 0);
        if( !cache_texture )
            continue;

        int animation_direction;
        int animation_speed;
        tdx_texture_anim_params(i, &animation_direction, &animation_speed);

        struct GameCache_Texture* gc_texture = gamecache_texture_new_from_cache_dat_texture(
            cache_texture, animation_direction, animation_speed);
        cache_dat_texture_free(cache_texture);
        if( !gc_texture )
            continue;

        gamecache_submit_texture(ToriDrawX_Gamecache(task->tdx), i, gc_texture);
        ToriDrawX_Texture(task->tdx, i);
    }

    filelist_dat_free(filelist);
    PT_END(&task->thread);
}

struct Task_ToriDrawX_AnimationsLoad
{
    struct pt thread;
    struct ToriDrawX* tdx;
    int anim_count;
    int anim_index;
    int pending_fetches;
    int pending_decodes;
};

struct Task_ToriDrawX_AnimationsLoad*
Task_ToriDrawX_AnimationsLoad_New(struct ToriDrawX* tdx)
{
    struct Task_ToriDrawX_AnimationsLoad* task =
        calloc(1, sizeof(struct Task_ToriDrawX_AnimationsLoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->tdx = tdx;
    return task;
}

void
Task_ToriDrawX_AnimationsLoad_Free(struct Task_ToriDrawX_AnimationsLoad* task)
{
    free(task);
}

int
Task_ToriDrawX_AnimationsLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriDrawX_AnimationsLoad* task = (struct Task_ToriDrawX_AnimationsLoad*)task_state;
    struct Dat1BuildCache* dat1_bc = dat1(ToriDrawX_GamecacheL(task->tdx));
    struct GameCache* gc = ToriDrawX_Gamecache(task->tdx);

    PT_BEGIN(&task->thread);

    if( !dat1_bc->fromconfigtable_config_jagfile || !dat1_bc->versionlist_jagfile )
    {
        dat1io_config_jagfile_fetch(ctx);
        dat1io_versionlist_jagfile_fetch(ctx);
        PT_YIELD(&task->thread);

        {
            struct FileListDat* config_jag = dat1io_config_jagfile_decode(ctx);
            struct FileListDat* versionlist_jag = dat1io_versionlist_jagfile_decode(ctx);
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
            struct CacheDatAnimBaseFrames* abf = NULL;
            int anim_id = dat1io_animations_decode(ctx, &abf);
            if( anim_id >= 0 && abf )
            {
                dat1_buildcache_animbaseframes_add(dat1_bc, anim_id, abf);
                gamecache_submit_animation_from_dat1(gc, dat1_bc, anim_id);
                ToriDrawX_Animation(task->tdx, anim_id);
            }
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    dat1_buildcache_sequences_init_from_config_jagfile(dat1_bc);
    gamecache_submit_all_sequences_from_dat1(gc, dat1_bc);

    PT_END(&task->thread);
}
