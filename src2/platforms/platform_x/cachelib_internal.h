#ifndef PLATFORM_X_CACHELIB_INTERNAL_H
#define PLATFORM_X_CACHELIB_INTERNAL_H

#include "cachelib.h"
#include "osrs/rscache/dat2disk/rscache_dat2disk.h"
#include "osrs/rscache/dat1disk/rscache_dat1disk.h"

struct RSCacheDat2DiskLib
{
    int mode;
    union
    {
        struct RSCacheDat2Disk* cache_dat2;
        struct RSCacheDat1Disk* cache_dat1;
    } u;
};

#endif