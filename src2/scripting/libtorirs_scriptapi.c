#include "libtorirs_scriptapi.h"

#include "../ioqueue/libtorirs_ioqueue.h"
#include "../libtorirs_internal.h"
#include "buildcache/dat1_buildcache.h"
#include "gamecache/gamecache.h"
#include "platforms/platform_x/cachelib_client.h"
#include "src/osrs/dash_utils.h"
#include "src/osrs/rscache/cache_dat.h"
#include "src/osrs/rscache/filelist.h"
#include "src/osrs/rscache/tables/model.h"
#include "src/osrs/rscache/tables_dat/config_textures.h"
#include "src/osrs/rscache/tables_dat/configs_dat.h"
#include "src/osrs/texture.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_types.h"

#include <stdio.h>
#include <stdlib.h>

#define DAT1_TEXTURE_COUNT 50

void
LibToriRS_ScriptAPI_Dat1_ConfigFileFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_ConfigFileFetch\n");
    if( !instance )
        return;

    struct CacheLib_IORequest request;
    cachelib_dat1_config_file_fetch(&request);

    struct LibToriRS_IOQueueItem item = { 0 };
    item.table_id = request.table_id;
    item.archive_id = request.archive_id;
    item.flags = request.flags;

    if( !LibToriRS_IOQueuePopWrite(io_queue, &item) )
        return;
}

bool
LibToriRS_ScriptAPI_Dat1_ConfigFileLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_ConfigFileLoad\n");
    if( !instance )
        return false;

    struct LibToriRS_IOQueueItem item = { 0 };
    if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        return false;
    if( item.status != TORIRSIO_RESOLVED )
        return false;
    if( item.table_id != CACHE_DAT_CONFIGS )
        return false;
    if( item.archive_id != CONFIG_DAT_CONFIGS )
        return false;
    if( item.flags != 0 )
        return false;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return false;

    printf("CacheDatArchive: %p\n", archive);

    struct FileListDat* filelist_dat = filelist_dat_new_from_cache_dat_archive(archive);
    if( !filelist_dat )
        return false;

    dat1_buildcache_set_fromconfigtable_config_jagfile(instance->dat1_buildcache, filelist_dat);

    cache_dat_archive_free(archive);
    return true;
}

void
LibToriRS_ScriptAPI_Dat1_TexturesFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_TexturesFetch\n");
    if( !instance )
        return;

    struct CacheLib_IORequest request;
    cachelib_dat1_textures_archive_fetch(&request);

    struct LibToriRS_IOQueueItem item = { 0 };
    item.table_id = request.table_id;
    item.archive_id = request.archive_id;
    item.flags = request.flags;

    if( !LibToriRS_IOQueuePopWrite(io_queue, &item) )
        return;
}

bool
LibToriRS_ScriptAPI_Dat1_TexturesLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_TexturesLoad\n");
    if( !instance )
        return false;
    if( !instance->model_viewer || !instance->model_viewer->context )
        return false;

    struct LibToriRS_IOQueueItem item = { 0 };
    if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        return false;
    if( item.status != TORIRSIO_RESOLVED )
        return false;
    if( item.table_id != CACHE_DAT_CONFIGS )
        return false;
    if( item.archive_id != CONFIG_DAT_TEXTURES )
        return false;
    if( item.flags != 0 )
        return false;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return false;

    struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(archive);
    cache_dat_archive_free(archive);
    if( !filelist )
        return false;

    struct ToriDraw_TextureMap* texture_map = &instance->model_viewer->context->texture_map;

    for( int i = 0; i < DAT1_TEXTURE_COUNT; i++ )
    {
        struct CacheDatTexture* cache_texture =
            cache_dat_texture_new_from_filelist_dat(filelist, i, 0);
        if( !cache_texture )
        {
            printf("cache_dat_texture_new_from_filelist_dat failed for texture %d\n", i);
            assert(false);
            continue;
        }

        int animation_direction = TEXANIM_DIRECTION_NONE;
        int animation_speed = 0;
        if( i == 17 || i == 24 )
        {
            animation_direction = TEXANIM_DIRECTION_V_DOWN;
            animation_speed = 2;
        }

        if( i == 8 )
        {
            printf("cache_texture: %p\n", cache_texture);
        }
        struct DashTexture* dash_texture =
            texture_new_from_texture_sprite(
                cache_texture, animation_direction, animation_speed, false, false);
        cache_dat_texture_free(cache_texture);
        if( !dash_texture )
        {
            printf("texture_new_from_texture_sprite failed for texture %d\n", i);
            assert(false);
            continue;
        }

        struct ToriDraw_Texture* toridraw_texture = malloc(sizeof(struct ToriDraw_Texture));
        if( !toridraw_texture )
        {
            texture_free(dash_texture);
            printf("malloc failed for texture %d\n", i);
            assert(false);
            continue;
        }

        toridraw_texture->texels = dash_texture->texels;
        toridraw_texture->width = dash_texture->width;
        toridraw_texture->height = dash_texture->height;
        toridraw_texture->opaque = dash_texture->opaque;

        dash_texture->texels = NULL;
        texture_free(dash_texture);

        texture_map->textures[i] = toridraw_texture;
    }

    texture_map->count = DAT1_TEXTURE_COUNT;
    filelist_dat_free(filelist);
    return true;
}

void
LibToriRS_ScriptAPI_Dat1_ModelCacheAdd(
    struct LibToriRS_Instance* instance,
    int model_id,
    int data_size,
    void* data)
{
    printf("LibToriRS_ScriptAPI_Dat1_ModelCacheAdd\n");
    if( !instance )
        return;
    if( !data )
        return;

    struct CacheModel* model = model_new_decode(data, data_size);
    if( !model )
        return;

    dat1_buildcache_model_add(instance->dat1_buildcache, model_id, model);
}

void
LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel(
    struct LibToriRS_Instance* instance,
    int model_id)
{
    printf("LibToriRS_ScriptAPI_Dat1_SubmitGameCacheModel\n");
    if( !instance )
        return;

    struct CacheModel* model = dat1_buildcache_model_get(instance->dat1_buildcache, model_id);
    if( !model )
        return;

    struct CacheModel* copy = model_new_copy(model);
    if( !copy )
        return;

    struct DashModel* dash = dashmodel_new_from_cache_model(copy);
    model_free(copy);
    if( !dash )
        return;

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = (struct ToriDraw_Model*)(void*)dash,
    };
    gamecache_model_add(instance->gamecache, model_id, hnd);
}

void
LibToriRS_ScriptAPI_Dat1_ModelFetch(
    struct LibToriRS_Instance* instance,
    int model_id,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_ModelFetch\n");
    if( !instance )
        return;

    struct CacheLib_IORequest request;
    cachelib_dat1_model_fetch(model_id, &request);

    struct LibToriRS_IOQueueItem item = { 0 };
    item.table_id = request.table_id;
    item.archive_id = request.archive_id;
    item.flags = request.flags;
    if( !LibToriRS_IOQueuePopWrite(io_queue, &item) )
        return;
}

bool
LibToriRS_ScriptAPI_Dat1_ModelLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_ModelLoad\n");
    if( !instance )
        return false;

    struct LibToriRS_IOQueueItem item = { 0 };
    if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        return false;
    if( item.status != TORIRSIO_RESOLVED )
        return false;
    if( item.table_id != CACHE_DAT_MODELS )
        return false;
    if( item.flags != 0 )
        return false;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return false;

    int model_id = item.archive_id;

    struct CacheModel* model = model_new_from_dat_archive(archive, model_id);
    if( !model )
        return false;

    dat1_buildcache_model_add(instance->dat1_buildcache, model_id, model);
    cache_dat_archive_free(archive);
    return true;
}

void
LibToriRS_ScriptAPI_Game_ModelViewer_Init(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Game_ModelViewer_Init\n");
    if( !instance )
        return;

    instance->model_viewer = game_modelviewer_new(instance->script_queue);
    if( !instance->model_viewer )
        return;
}

void
LibToriRS_ScriptAPI_Game_ModelViewer_RenderModel(
    struct LibToriRS_Instance* instance,
    int model_id)
{
    printf("LibToriRS_ScriptAPI_Game_ModelViewer_RenderModel\n");
    if( !instance )
        return;

    struct ToriDraw_ModelHandle hnd = gamecache_model_get(instance->gamecache, model_id);
    if( hnd.kind != TORIDRAWMK_MODEL )
        return;

    toridraw_light_model_default(hnd, 0, 0);

    game_modelviewer_set_model(instance->model_viewer, model_id, hnd);
}