#include "engine/cache_mounts.h"

#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat2/dat2_buildcache.h"

#include <rscache.h>

#include <assert.h>
#include <stddef.h>

struct RSCache_Dat1Disk*
CacheMounts_OpenDat1Disk(
    struct CacheMounts* mounts,
    char const* directory)
{
    assert(mounts);
    assert(directory);
    assert(!mounts->dat1_disk);
    mounts->dat1_disk = RSCache_Dat1DiskNewFromDirectory(directory);
    return mounts->dat1_disk;
}

struct RSCache_Dat2Disk*
CacheMounts_OpenDat2Disk(
    struct CacheMounts* mounts,
    char const* directory)
{
    assert(mounts);
    assert(directory);
    assert(!mounts->dat2_disk);
    /* Lazy: the client reads maybe two thirds of the tables a cache ships,
     * and decoding the rest at open cost several MB it never looked at. */
    mounts->dat2_disk = RSCache_Dat2DiskNewFromDirectoryLazyTables(directory);
    return mounts->dat2_disk;
}

void
CacheMounts_SetOnDemand(struct CacheMounts* mounts)
{
    assert(mounts);
    mounts->on_demand = 1;
}

int
CacheMounts_OnDemand(struct CacheMounts const* mounts)
{
    assert(mounts);
    return mounts->on_demand;
}

struct RSCache_Dat1Disk*
CacheMounts_Dat1Disk(struct CacheMounts const* mounts)
{
    assert(mounts);
    return mounts->dat1_disk;
}

struct RSCache_Dat2Disk*
CacheMounts_Dat2Disk(struct CacheMounts const* mounts)
{
    assert(mounts);
    return mounts->dat2_disk;
}

int
CacheMounts_ArmJagCrcFromOnDemand(
    struct CacheMounts* mounts,
    int stated)
{
    assert(mounts);
    if( !mounts->on_demand || stated )
        return 0;
    mounts->jag_crc_from_ondemand = 1;
    return 1;
}

int
CacheMounts_JagCrcFromOnDemand(struct CacheMounts const* mounts)
{
    assert(mounts);
    return mounts->jag_crc_from_ondemand;
}

int
CacheMounts_TakeDat1Prefetch(struct CacheMounts* mounts)
{
    int want;

    assert(mounts);
    want = mounts->on_demand && !mounts->dat1_prefetch_queued;
    mounts->dat1_prefetch_queued = 1;
    return want;
}

struct CacheProvider*
CacheMounts_OpenDat1BuildCache(struct CacheMounts* mounts)
{
    assert(mounts);
    assert(!mounts->dat1_bc);
    mounts->dat1_bc = dat1_buildcache_new();
    return dat1_buildcache_as_provider(mounts->dat1_bc);
}

struct CacheProvider*
CacheMounts_OpenDat2BuildCache(struct CacheMounts* mounts)
{
    assert(mounts);
    assert(!mounts->dat2_bc);
    mounts->dat2_bc = dat2_buildcache_new();
    return dat2_buildcache_as_provider(mounts->dat2_bc);
}

void
CacheMounts_CloseBuildCaches(struct CacheMounts* mounts)
{
    assert(mounts);
    /* Only the one matching the configured format was ever created; both
     * frees assert on NULL, so the unused side must not be handed to them. */
    if( mounts->dat2_bc )
        dat2_buildcache_free(mounts->dat2_bc);
    if( mounts->dat1_bc )
        dat1_buildcache_free(mounts->dat1_bc);
    mounts->dat2_bc = NULL;
    mounts->dat1_bc = NULL;
}

void
CacheMounts_CloseDisks(struct CacheMounts* mounts)
{
    assert(mounts);
    if( mounts->dat2_disk )
        RSCache_Dat2DiskFree(mounts->dat2_disk);
    if( mounts->dat1_disk )
        RSCache_Dat1DiskFree(mounts->dat1_disk);
    mounts->dat2_disk = NULL;
    mounts->dat1_disk = NULL;
}
