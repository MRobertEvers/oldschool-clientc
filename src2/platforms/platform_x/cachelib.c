

#include "cachelib_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CacheLib*
cachelib_new(int mode)
{
    struct CacheLib* cache = malloc(sizeof(struct CacheLib));
    assert(cache);

    memset(cache, 0, sizeof(struct CacheLib));
    cache->mode = mode;

    return cache;
}

void
cachelib_free(struct CacheLib* cache)
{
    if( !cache )
        return;
    
    // Free the backing cache based on mode
    if( cache->mode == CACHE_MODE_DAT1 && cache->u.cache_dat1 )
    {
        cache_dat_free(cache->u.cache_dat1);
        cache->u.cache_dat1 = NULL;
    }
    else if( cache->mode == CACHE_MODE_DAT2 && cache->u.cache_dat2 )
    {
        cache_free(cache->u.cache_dat2);
        cache->u.cache_dat2 = NULL;
    }
    
    free(cache);
}

int
cachelib_get_mode(struct CacheLib* cache)
{
    if( !cache )
        return -1;
    return cache->mode;
}
