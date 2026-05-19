#ifndef PLATFORM_X_CACHELIB_INTERNAL_H
#define PLATFORM_X_CACHELIB_INTERNAL_H

#include "cachelib.h"
#include "src/osrs/rscache/cache.h"
#include "src/osrs/rscache/cache_dat.h"

struct CacheLib
{
    int mode;
    union
    {
        struct Cache* cache_dat2;
        struct CacheDat* cache_dat1;
    } u;
};

#endif