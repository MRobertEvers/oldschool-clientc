#ifndef PLATFORM_X_CACHELIB_H
#define PLATFORM_X_CACHELIB_H

#include <stdint.h>

struct RSCacheDat2DiskLib;

struct RSCacheDat2DiskLib_IORequest
{
    int table_id;
    int archive_id;
    int flags;
};

#define CACHE_MODE_DAT1 0
#define CACHE_MODE_DAT2 1

struct RSCacheDat2DiskLib*
cachelib_new(int mode);

void
cachelib_free(struct RSCacheDat2DiskLib* cache);

int
cachelib_get_mode(struct RSCacheDat2DiskLib* cache);

#endif