

#include "cachelib_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RSCacheDat2DiskLib*
cachelib_new(int mode)
{
    struct RSCacheDat2DiskLib* cache = malloc(sizeof(struct RSCacheDat2DiskLib));
    assert(cache);

    memset(cache, 0, sizeof(struct RSCacheDat2DiskLib));
    cache->mode = mode;

    return cache;
}

void
cachelib_free(struct RSCacheDat2DiskLib* cache)
{
    if( !cache )
        return;
    
    // Free the backing cache based on mode
    if( cache->mode == CACHE_MODE_DAT1 && cache->u.cache_dat1 )
    {
        RSCacheDat1Disk_Free(cache->u.cache_dat1);
        cache->u.cache_dat1 = NULL;
    }
    else if( cache->mode == CACHE_MODE_DAT2 && cache->u.cache_dat2 )
    {
        RSCacheDat2Disk_Free(cache->u.cache_dat2);
        cache->u.cache_dat2 = NULL;
    }
    
    free(cache);
}

int
cachelib_get_mode(struct RSCacheDat2DiskLib* cache)
{
    assert(cache);
    return cache->mode;
}

struct RSCacheDat2Disk*
cachelib_dat2_disk(struct RSCacheDat2DiskLib* cache)
{
    assert(cache);
    if( cache->mode != CACHE_MODE_DAT2 )
        return NULL;
    return cache->u.cache_dat2;
}

struct RSCacheDat1Disk*
cachelib_dat1_disk(struct RSCacheDat2DiskLib* cache)
{
    assert(cache);
    if( cache->mode != CACHE_MODE_DAT1 )
        return NULL;
    return cache->u.cache_dat1;
}
