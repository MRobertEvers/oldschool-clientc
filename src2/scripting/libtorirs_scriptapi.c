#include "libtorirs_scriptapi.h"

#include "../ioqueue/libtorirs_ioqueue.h"
#include "src/osrs/rscache/cache_dat.h"
#include "src/osrs/rscache/tables_dat/configs_dat.h"

#include <stdio.h>

void
LibToriRS_ScriptAPI_Dat1_ConfigFileFetch(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_ConfigFileFetch\n");
    if( !instance )
        return;
    struct LibToriRS_IOQueue* io_queue = LibToriRS_GetIOQueue(instance);
    if( !io_queue )
        return;

    struct LibToriRS_IOQueueItem item = { 0 };
    item.table_id = CACHE_DAT_CONFIGS;
    item.archive_id = CONFIG_DAT_CONFIGS;
    item.flags = 0;
    if( !LibToriRS_IOQueuePopWrite(io_queue, &item) )
        return;
}

void
LibToriRS_ScriptAPI_Dat1_ConfigFileLoad(struct LibToriRS_Instance* instance)
{
    printf("LibToriRS_ScriptAPI_Dat1_ConfigFileLoad\n");
    if( !instance )
        return;
    struct LibToriRS_IOQueue* io_queue = LibToriRS_GetIOQueue(instance);
    if( !io_queue )
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
}
