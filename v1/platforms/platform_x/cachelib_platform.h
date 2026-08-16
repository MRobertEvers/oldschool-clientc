#ifndef PLATFORM_X_CACHELIB_PLATFORM_H
#define PLATFORM_X_CACHELIB_PLATFORM_H

#include "cachelib.h"

int
cachelib_platform_init(
    struct RSCacheDat2DiskLib* cache,
    char const* directory);

void*
cachelib_platform_load_io(
    struct RSCacheDat2DiskLib* cache,
    struct CacheLib_IORequest* request);

#endif