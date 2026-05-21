#include "libtorirs_scriptapi.h"

#include "../ioqueue/libtorirs_ioqueue.h"
#include "../libtorirs_internal.h"
#include "buildcache/dat1_buildcache.h"
#include "gamecache/gamecache.h"
#include "src/osrs/dash_utils.h"
#include "platforms/platform_x/cachelib_client.h"
#include "src/osrs/rscache/cache_dat.h"
#include "src/osrs/rscache/filelist.h"
#include "src/osrs/rscache/tables/model.h"
#include "src/osrs/rscache/tables_dat/configs_dat.h"

// clang-format off
#include "src/osrs/_light_model_default.u.c"
// clang-format on

#include <stdio.h>

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

void
LibToriRS_ScriptAPI_Dat1_ConfigFileLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_ConfigFileLoad\n");
    if( !instance )
        return;

    struct LibToriRS_IOQueueItem item = { 0 };
    if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        return;
    if( item.table_id != CACHE_DAT_CONFIGS )
        return;
    if( item.archive_id != CONFIG_DAT_CONFIGS )
        return;
    if( item.flags != 0 )
        return;

    struct CacheDatArchive* archive = item.data;

    printf("CacheDatArchive: %p\n", archive);

    struct FileListDat* filelist_dat = filelist_dat_new_from_cache_dat_archive(archive);
    if( !filelist_dat )
        return;

    dat1_buildcache_set_fromconfigtable_config_jagfile(instance->dat1_buildcache, filelist_dat);

    cache_dat_archive_free(archive);
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

    gamecache_dashmodel_add(instance->gamecache, model_id, dash);
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

void
LibToriRS_ScriptAPI_Dat1_ModelLoad(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_ModelLoad\n");
    if( !instance )
        return;

    struct LibToriRS_IOQueueItem item = { 0 };
    if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        return;
    if( item.table_id != CACHE_DAT_MODELS )
        return;
    if( item.flags != 0 )
        return;

    struct CacheDatArchive* archive = item.data;
    if( !archive )
        return;

    int model_id = item.archive_id;

    struct CacheModel* model = model_new_from_archive(archive, model_id);
    if( !model )
        return;

    dat1_buildcache_model_add(instance->dat1_buildcache, model_id, model);
    cache_dat_archive_free(archive);
}

void
LibToriRS_ScriptAPI_Game_ModelViewer_Init(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Game_ModelViewer_Init\n");
    if( !instance )
        return;

    instance->model_viewer = game_modelviewer_new();
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

    struct DashModel* model = gamecache_dashmodel_get(instance->gamecache, model_id);
    if( !model )
        return;

    _light_model_default(model, 0, 0);

    game_modelviewer_set_model(instance->model_viewer, model);
}