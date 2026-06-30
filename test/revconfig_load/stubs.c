#include "platform_x/cachelib_platform.h"

#include <stddef.h>

void*
cachelib_platform_load_io(
    struct RSCacheDat2DiskLib* cache,
    struct RSCacheDat2DiskLib_IORequest* request)
{
    (void)cache;
    (void)request;
    return NULL;
}
