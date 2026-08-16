#include "osrs/rscache/dat2disk/dat2disk.h"
#include "platform_x/cachelib_platform.h"

#include <stddef.h>

void*
cachelib_platform_load_io(
    struct RSCacheDat2DiskLib* cache,
    struct CacheLib_IORequest* request)
{
    (void)cache;
    (void)request;
    return NULL;
}

struct RSCacheDat2Disk*
cachelib_dat2_disk(struct RSCacheDat2DiskLib* cache)
{
    (void)cache;
    return NULL;
}

struct RSCacheDat2Disk_Archive*
RSCacheDat2Disk_ArchiveNewReferenceTableLoad(
    struct RSCacheDat2Disk* cache,
    int table_id)
{
    (void)cache;
    (void)table_id;
    return NULL;
}
