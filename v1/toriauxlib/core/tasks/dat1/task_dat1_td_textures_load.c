#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat1_buildcache.h"
#include "core/tapi/tapi_dat1.h"
#include "osrs/rscache/dat1a/dat1a_config_textures.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toridraw/toridraw_types.h"

#include <stdlib.h>

struct Task_Dat1TdTexturesLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibTD* tdx;
};

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

static int
Task_Dat1TdTexturesLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1TdTexturesLoad* task =
        LibToriRS_container_of(base, struct Task_Dat1TdTexturesLoad, base);
    struct RSCacheShared_FileListDat* filelist;

    PT_BEGIN(&task->pt);

    IO_REQUEST(ctx, 0, TAPIDat1_FetchTexturesJagfile(ctx));
    PT_YIELD(&task->pt);

    filelist = TAPIDat1_DecodeTexturesJagfile(ctx, 0);
    if( !filelist )
        PT_EXIT(&task->pt);
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
    PT_END(&task->pt);
}

static void
Task_Dat1TdTexturesLoad_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat1_td_textures_load_vtable = {
    .run_fn = Task_Dat1TdTexturesLoad_Run,
    .free_fn = Task_Dat1TdTexturesLoad_Free,
};

struct LibToriRS_Task*
Task_Dat1TdTexturesLoad_New(struct ToriAuxLibTD* tdx)
{
    struct Task_Dat1TdTexturesLoad* task = calloc(1, sizeof(struct Task_Dat1TdTexturesLoad));
    if( !task )
        return NULL;
    task->base.vtable = &g_task_dat1_td_textures_load_vtable;
    task->tdx = tdx;
    PT_INIT(&task->pt);
    return &task->base;
}
