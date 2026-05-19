#ifndef PLATFORM_X_CACHELIB_H
#define PLATFORM_X_CACHELIB_H

#include <stdint.h>

struct CacheLib;

struct CacheLib_IORequest
{
    int table_id;
    int archive_id;
    int flags;
};

#define CACHE_MODE_DAT1 0
#define CACHE_MODE_DAT2 1

struct CacheLib*
cachelib_new(int mode);

void
cachelib_free(struct CacheLib* cache);

int
cachelib_get_mode(struct CacheLib* cache);

#endif