#include "platform_x_cache.h"

#include "../ioqueue/libtorirs_ioqueue.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_xtea_config.h"
#include "platform_x/cachelib.h"
#include "platform_x/cachelib_platform.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct LibToriPlatformX_Cache
{
    struct LibToriRS_Instance* instance;

    struct RSCacheDat2DiskLib* cache;
};

struct LibToriPlatformX_Cache*
LibToriPlatformX_CacheNew(
    struct LibToriRS_Instance* instance,
    int mode,
    char const* directory)
{
    struct LibToriPlatformX_Cache* cache = malloc(sizeof(struct LibToriPlatformX_Cache));
    if( !cache )
        return NULL;

    cache->instance = instance;
    cache->cache = cachelib_new(mode);
    if( !cache->cache )
        return NULL;

    if( cachelib_platform_init(cache->cache, directory) != 1 )
        return NULL;

    return cache;
}

void
LibToriPlatformX_CacheFree(struct LibToriPlatformX_Cache* cache)
{
    if( !cache )
        return;
    free(cache);
}

int
LibToriPlatformX_GetMode(struct LibToriPlatformX_Cache* cache)
{
    if( !cache )
        return -1;
    return cachelib_get_mode(cache->cache);
}

int
LibToriPlatformX_CacheLoadIO(
    struct LibToriPlatformX_Cache* cache,
    struct LibToriRS_IOQueue* io_queue)
{
    if( !cache || !io_queue )
        return -1;

    struct CacheLib_IORequest request;
    void* data = NULL;
    struct LibToriRS_IOQueueItem* io_item = NULL;
    for( int i = 0; i < io_queue->count; i++ )
    {
        io_item = &io_queue->items[i];
        if( io_item->status == TORIRSIO_STAT_DONE )
            continue;

        if( io_item->kind == TORIRSIO_KIND_REFERENCE_TABLE )
        {
            struct RSCacheDat2Disk* cache_dat2 = cachelib_dat2_disk(cache->cache);
            if( !cache_dat2 )
            {
                io_item->error_code = -1;
                io_item->status = TORIRSIO_STAT_DONE;
                continue;
            }

            data = RSCacheDat2Disk_ArchiveNewReferenceTableLoad(
                cache_dat2, io_item->u.reference_table.table_id);
            if( data )
            {
                io_item->data = data;
                io_item->error_code = 0;
                io_item->status = TORIRSIO_STAT_DONE;
            }
            else
            {
                io_item->error_code = -1;
                io_item->status = TORIRSIO_STAT_DONE;
            }
            continue;
        }

        if( io_item->kind != TORIRSIO_KIND_CACHE )
            continue;

        request.table_id = io_item->u.cache.table_id;
        request.archive_id = io_item->u.cache.archive_id;
        request.flags = io_item->u.cache.flags;

        data = cachelib_platform_load_io(cache->cache, &request);

        if( data )
        {
            io_item->data = data;
            io_item->error_code = 0;
            io_item->status = TORIRSIO_STAT_DONE;
        }
        else
        {
            io_item->error_code = -1;
            io_item->status = TORIRSIO_STAT_DONE;
        }
    }

    return io_queue->count;
}