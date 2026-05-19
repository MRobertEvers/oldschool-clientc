#include "platform_x_cache.h"

#include "../ioqueue/libtorirs_ioqueue.h"
#include "platform_x/cachelib_platform.h"
#include "src/osrs/rscache/cache.h"
#include "src/osrs/rscache/cache_dat.h"
#include "src/osrs/rscache/xtea_config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct LibToriPlatformX_Cache
{
    struct LibToriRS_Instance* instance;

    struct CacheLib* cache;
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
        request.table_id = io_item->table_id;
        request.archive_id = io_item->archive_id;
        request.flags = io_item->flags;

        data = cachelib_platform_load_io(cache->cache, &request);

        if( !data )
            return -1;

        io_item->data = data;
        io_item->is_resolved = true;
    }
    return 0;
}