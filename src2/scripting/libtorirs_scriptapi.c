#include "libtorirs_scriptapi.h"

#include "../ioqueue/libtorirs_ioqueue.h"
#include "../libtorirs_internal.h"
#include "platforms/platform_x/cachelib_client.h"
#include "src/osrs/rscache/cache_dat.h"
#include "src/osrs/rscache/filelist.h"
#include "src/osrs/rscache/tables_dat/configs_dat.h"

#include <stdio.h>

void
LibToriRS_ScriptAPI_Dat1_ConfigFileFetch(
    struct LibToriRS_Instance* instance,
    struct LibToriRS_IOQueue* io_queue)
{
    printf("LibToriRS_ScriptAPI_Dat1_ConfigFileFetch\n");
    if( !instance )
        return;

    struct LibToriRS_IOQueueItem item = { 0 };
    item.table_id = CACHE_DAT_CONFIGS;
    item.archive_id = CONFIG_DAT_CONFIGS;
    item.flags = 0;
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

    struct GameCacheModel* gamecache_model = gamecache_model_new();
    dat1_gamecache_loader_new_gamecache_model_from_cache_model(model, gamecache_model);
    gamecache_model_add(instance->gamecache, model_id, gamecache_model);

    dat1_gamecache_loader_new_gamecache_model_from_cache_model(instance->dat1_buildcache, model_id);
}