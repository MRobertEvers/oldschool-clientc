/*
 * The cache mounts (engine/cache_mounts.h), against stand-ins for the disks
 * and build caches that record what was opened and freed: a disk is opened
 * once and a failed open mounts nothing, closing frees only what was opened,
 * and the two on-demand answers are given once.
 *
 * Run: make -C src test-cache-mounts
 */

#include "engine/cache_mounts.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Stand-ins, linked in place of rscache and the build caches. Each handle is
 * the address of one of these, so a free can say which one it was given. */
struct RSCache_Dat1Disk
{
    int tag;
};
struct RSCache_Dat2Disk
{
    int tag;
};
struct Dat1BuildCache
{
    int tag;
};
struct Dat2BuildCache
{
    int tag;
};
struct CacheProvider
{
    int tag;
};

static struct RSCache_Dat1Disk g_dat1_disk;
static struct RSCache_Dat2Disk g_dat2_disk;
static struct Dat1BuildCache g_dat1_bc;
static struct Dat2BuildCache g_dat2_bc;
static struct CacheProvider g_dat1_provider;
static struct CacheProvider g_dat2_provider;

/* What happened, in order: one letter per call. */
static char g_log[64];

static void
note(char const* what)
{
    assert(strlen(g_log) + strlen(what) < sizeof(g_log));
    strcat(g_log, what);
}

struct RSCache_Dat1Disk*
RSCache_Dat1DiskNewFromDirectory(char const* path)
{
    note("o1");
    return strcmp(path, "here") == 0 ? &g_dat1_disk : NULL;
}

struct RSCache_Dat2Disk*
RSCache_Dat2DiskNewFromDirectoryLazyTables(char const* directory)
{
    note("o2");
    return strcmp(directory, "here") == 0 ? &g_dat2_disk : NULL;
}

void
RSCache_Dat1DiskFree(struct RSCache_Dat1Disk* cache)
{
    assert(cache == &g_dat1_disk);
    note("f1");
}

void
RSCache_Dat2DiskFree(struct RSCache_Dat2Disk* disk)
{
    assert(disk == &g_dat2_disk);
    note("f2");
}

struct Dat1BuildCache*
dat1_buildcache_new(void)
{
    note("b1");
    return &g_dat1_bc;
}

struct Dat2BuildCache*
dat2_buildcache_new(void)
{
    note("b2");
    return &g_dat2_bc;
}

struct CacheProvider*
dat1_buildcache_as_provider(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache == &g_dat1_bc);
    return &g_dat1_provider;
}

struct CacheProvider*
dat2_buildcache_as_provider(struct Dat2BuildCache* dat2_buildcache)
{
    assert(dat2_buildcache == &g_dat2_bc);
    return &g_dat2_provider;
}

void
dat1_buildcache_free(struct Dat1BuildCache* dat1_buildcache)
{
    assert(dat1_buildcache == &g_dat1_bc);
    note("x1");
}

void
dat2_buildcache_free(struct Dat2BuildCache* dat2_buildcache)
{
    assert(dat2_buildcache == &g_dat2_bc);
    note("x2");
}

static void
reset(struct CacheMounts* mounts)
{
    memset(mounts, 0, sizeof(*mounts));
    g_log[0] = '\0';
}

/* A dat2 boot: its disk and its build cache, and only those freed. */
static void
test_dat2_mount_and_close(void)
{
    struct CacheMounts mounts;

    reset(&mounts);
    assert(CacheMounts_OpenDat2Disk(&mounts, "here") == &g_dat2_disk);
    assert(CacheMounts_Dat2Disk(&mounts) == &g_dat2_disk);
    assert(CacheMounts_Dat1Disk(&mounts) == NULL);
    assert(CacheMounts_OpenDat2BuildCache(&mounts) == &g_dat2_provider);
    CacheMounts_CloseBuildCaches(&mounts);
    CacheMounts_CloseDisks(&mounts);
    assert(strcmp(g_log, "o2b2x2f2") == 0);
    assert(CacheMounts_Dat2Disk(&mounts) == NULL);
    /* Closing again frees nothing twice. */
    CacheMounts_CloseBuildCaches(&mounts);
    CacheMounts_CloseDisks(&mounts);
    assert(strcmp(g_log, "o2b2x2f2") == 0);
}

/* A dat1 boot, the same way round. */
static void
test_dat1_mount_and_close(void)
{
    struct CacheMounts mounts;

    reset(&mounts);
    assert(CacheMounts_OpenDat1Disk(&mounts, "here") == &g_dat1_disk);
    assert(CacheMounts_Dat1Disk(&mounts) == &g_dat1_disk);
    assert(CacheMounts_Dat2Disk(&mounts) == NULL);
    assert(CacheMounts_OpenDat1BuildCache(&mounts) == &g_dat1_provider);
    CacheMounts_CloseBuildCaches(&mounts);
    CacheMounts_CloseDisks(&mounts);
    assert(strcmp(g_log, "o1b1x1f1") == 0);
}

/* No cache in the directory: nothing is mounted and nothing is freed. */
static void
test_missing_cache_mounts_nothing(void)
{
    struct CacheMounts mounts;

    reset(&mounts);
    assert(CacheMounts_OpenDat1Disk(&mounts, "nowhere") == NULL);
    assert(CacheMounts_Dat1Disk(&mounts) == NULL);
    reset(&mounts);
    assert(CacheMounts_OpenDat2Disk(&mounts, "nowhere") == NULL);
    assert(CacheMounts_Dat2Disk(&mounts) == NULL);
    CacheMounts_CloseBuildCaches(&mounts);
    CacheMounts_CloseDisks(&mounts);
    assert(strcmp(g_log, "o2") == 0);
}

/* On demand: no disk at all; the build cache alone is freed. */
static void
test_on_demand_has_no_disk(void)
{
    struct CacheMounts mounts;

    reset(&mounts);
    assert(!CacheMounts_OnDemand(&mounts));
    CacheMounts_SetOnDemand(&mounts);
    assert(CacheMounts_OnDemand(&mounts));
    assert(CacheMounts_OpenDat1BuildCache(&mounts) == &g_dat1_provider);
    CacheMounts_CloseBuildCaches(&mounts);
    CacheMounts_CloseDisks(&mounts);
    assert(strcmp(g_log, "b1x1") == 0);
}

/* The checksums come from the server only when it is the cache's and nobody
 * stated others; then every login refreshes them. */
static void
test_jag_crc_from_on_demand(void)
{
    struct CacheMounts mounts;

    reset(&mounts);
    assert(!CacheMounts_ArmJagCrcFromOnDemand(&mounts, 0));
    assert(!CacheMounts_JagCrcFromOnDemand(&mounts));

    reset(&mounts);
    CacheMounts_SetOnDemand(&mounts);
    assert(!CacheMounts_ArmJagCrcFromOnDemand(&mounts, 1));
    assert(!CacheMounts_JagCrcFromOnDemand(&mounts));
    assert(CacheMounts_ArmJagCrcFromOnDemand(&mounts, 0));
    assert(CacheMounts_JagCrcFromOnDemand(&mounts));
}

/* The prefetch passes ride the first bake that asks, on demand only; a
 * later bake (the post-login rebake) gets none. */
static void
test_prefetch_first_bake_only(void)
{
    struct CacheMounts mounts;

    reset(&mounts);
    assert(!CacheMounts_TakeDat1Prefetch(&mounts));
    CacheMounts_SetOnDemand(&mounts);
    assert(!CacheMounts_TakeDat1Prefetch(&mounts));

    reset(&mounts);
    CacheMounts_SetOnDemand(&mounts);
    assert(CacheMounts_TakeDat1Prefetch(&mounts));
    assert(!CacheMounts_TakeDat1Prefetch(&mounts));
    assert(!CacheMounts_TakeDat1Prefetch(&mounts));
}

int
main(void)
{
    test_dat2_mount_and_close();
    test_dat1_mount_and_close();
    test_missing_cache_mounts_nothing();
    test_on_demand_has_no_disk();
    test_jag_crc_from_on_demand();
    test_prefetch_first_bake_only();
    printf("cache_mounts_test: ok\n");
    return 0;
}
