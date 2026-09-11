#include "dat2_group_cache.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rscache.h>
#include "log/torirs_log.h"

/*
 * Slots are scanned linearly. Groups are read at task granularity — a few
 * hundred times across a boot, not per frame — so a scan over this many entries
 * costs nothing next to the split it avoids, and it keeps eviction to one pass.
 */
#define DAT2_GROUP_CACHE_SLOTS 48

struct Dat2GroupCacheSlot
{
    struct Dat2Group group;
    uint64_t last_used;
    int occupied;
};

struct Dat2GroupCache
{
    struct Dat2GroupCacheSlot slots[DAT2_GROUP_CACHE_SLOTS];
    size_t bytes;
    size_t budget;
    uint64_t clock;

    /* Census, dumped by DAT2_GROUP_CACHE_CENSUS=1. A budget is only worth
     * lowering if the misses it buys are cheap, and the miss count is the only
     * way to tell an eviction that was reclaimed from one that gets re-split on
     * the very next lookup. */
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    size_t evicted_bytes;
    size_t peak_bytes;
};

struct Dat2GroupCache*
Dat2GroupCache_New(size_t budget_bytes)
{
    struct Dat2GroupCache* cache = calloc(1, sizeof(*cache));
    assert(cache);
    cache->budget = budget_bytes;
    return cache;
}

static void
dat2_group_cache_release(struct Dat2GroupCacheSlot* slot, size_t* bytes)
{
    if( !slot->occupied )
        return;
    RSCache_FileListFreeShared(slot->group.filelist, slot->group.blob);
    free(slot->group.file_ids);
    assert(*bytes >= slot->group.bytes);
    *bytes -= slot->group.bytes;
    memset(slot, 0, sizeof(*slot));
}

static void
dat2_group_cache_census(struct Dat2GroupCache const* cache)
{
    uint64_t lookups;

    assert(cache);
    lookups = cache->hits + cache->misses;
    TORIRS_REPORT("[dat2_group_cache] budget=%.2f MB peak=%.2f MB live=%.2f MB\n"
        "[dat2_group_cache] lookups=%llu hits=%llu misses=%llu hit=%.1f%%\n"
        "[dat2_group_cache] evictions=%llu evicted=%.2f MB\n",
        cache->budget / (1024.0 * 1024.0),
        cache->peak_bytes / (1024.0 * 1024.0),
        cache->bytes / (1024.0 * 1024.0),
        (unsigned long long)lookups,
        (unsigned long long)cache->hits,
        (unsigned long long)cache->misses,
        lookups ? 100.0 * (double)cache->hits / (double)lookups : 0.0,
        (unsigned long long)cache->evictions,
        cache->evicted_bytes / (1024.0 * 1024.0));

    for( int i = 0; i < DAT2_GROUP_CACHE_SLOTS; i++ )
    {
        struct Dat2GroupCacheSlot const* slot = &cache->slots[i];
        if( !slot->occupied )
            continue;
        TORIRS_REPORT("[dat2_group_cache]   resident table=%d group=%d files=%d %.2f MB\n",
            slot->group.table,
            slot->group.group,
            slot->group.file_count,
            slot->group.bytes / (1024.0 * 1024.0));
    }
}

void
Dat2GroupCache_Free(struct Dat2GroupCache* cache)
{
    if( !cache )
        return;
    if( getenv("DAT2_GROUP_CACHE_CENSUS") )
        dat2_group_cache_census(cache);
    Dat2GroupCache_Clear(cache);
    free(cache);
}

void
Dat2GroupCache_Clear(struct Dat2GroupCache* cache)
{
    assert(cache);
    for( int i = 0; i < DAT2_GROUP_CACHE_SLOTS; i++ )
        dat2_group_cache_release(&cache->slots[i], &cache->bytes);
    assert(cache->bytes == 0);
}

size_t
Dat2GroupCache_Bytes(struct Dat2GroupCache const* cache)
{
    return cache ? cache->bytes : 0;
}

static struct Dat2GroupCacheSlot*
dat2_group_cache_find(
    struct Dat2GroupCache* cache,
    int table,
    int group)
{
    for( int i = 0; i < DAT2_GROUP_CACHE_SLOTS; i++ )
    {
        struct Dat2GroupCacheSlot* slot = &cache->slots[i];
        if( slot->occupied && slot->group.table == table
            && slot->group.group == group )
            return slot;
    }
    return NULL;
}

struct Dat2Group const*
Dat2GroupCache_Get(
    struct Dat2GroupCache* cache,
    int table,
    int group)
{
    struct Dat2GroupCacheSlot* slot;

    assert(cache);
    slot = dat2_group_cache_find(cache, table, group);
    if( !slot )
    {
        cache->misses++;
        return NULL;
    }
    cache->hits++;
    slot->last_used = ++cache->clock;
    return &slot->group;
}

/* Free the least recently used entry. Returns 0 when there was nothing to
 * free, which stops the caller looping forever on an over-budget insert. */
static int
dat2_group_cache_evict_one(struct Dat2GroupCache* cache)
{
    struct Dat2GroupCacheSlot* victim = NULL;

    for( int i = 0; i < DAT2_GROUP_CACHE_SLOTS; i++ )
    {
        struct Dat2GroupCacheSlot* slot = &cache->slots[i];
        if( !slot->occupied )
            continue;
        if( !victim || slot->last_used < victim->last_used )
            victim = slot;
    }
    if( !victim )
        return 0;
    cache->evictions++;
    cache->evicted_bytes += victim->group.bytes;
    dat2_group_cache_release(victim, &cache->bytes);
    return 1;
}

static struct Dat2GroupCacheSlot*
dat2_group_cache_free_slot(struct Dat2GroupCache* cache)
{
    for( int i = 0; i < DAT2_GROUP_CACHE_SLOTS; i++ )
    {
        if( !cache->slots[i].occupied )
            return &cache->slots[i];
    }
    if( !dat2_group_cache_evict_one(cache) )
        return NULL;
    return dat2_group_cache_free_slot(cache);
}

static size_t
dat2_group_filelist_bytes(struct RSCache_FileList const* filelist)
{
    size_t total = sizeof(struct RSCache_FileList);

    assert(filelist);
    total += (size_t)filelist->file_count * (sizeof(char*) + sizeof(int));
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( filelist->file_sizes[i] > 0 )
            total += (size_t)filelist->file_sizes[i];
    }
    return total;
}

struct Dat2Group const*
Dat2GroupCache_Put(
    struct Dat2GroupCache* cache,
    int table,
    int group,
    struct RSCache_Dat2DiskArchive* archive)
{
    struct Dat2GroupCacheSlot* slot;
    struct RSCache_FileList* filelist;
    char* blob = NULL;
    int* file_ids = NULL;
    size_t bytes;

    assert(cache);
    assert(archive);

    slot = dat2_group_cache_find(cache, table, group);
    if( slot )
    {
        slot->last_used = ++cache->clock;
        return &slot->group;
    }

    filelist = RSCache_FileListNewFromDecodeShared(
        archive->data, archive->data_size, archive->file_count, &blob);
    if( !filelist )
        return NULL;

    if( archive->file_ids && archive->file_count > 0 )
    {
        file_ids = malloc((size_t)archive->file_count * sizeof(*file_ids));
        assert(file_ids);
        memcpy(
            file_ids,
            archive->file_ids,
            (size_t)archive->file_count * sizeof(*file_ids));
    }

    bytes = dat2_group_filelist_bytes(filelist)
            + (file_ids ? (size_t)archive->file_count * sizeof(*file_ids) : 0);

    /* Evict until this fits, but never evict past empty: a single group larger
     * than the whole budget is still worth holding for the run of lookups that
     * is about to hit it, and dropping it immediately would mean re-splitting
     * on every one of them. */
    while( cache->bytes + bytes > cache->budget && dat2_group_cache_evict_one(cache) )
        ;

    slot = dat2_group_cache_free_slot(cache);
    if( !slot )
    {
        RSCache_FileListFreeShared(filelist, blob);
        free(file_ids);
        return NULL;
    }

    slot->occupied = 1;
    slot->last_used = ++cache->clock;
    slot->group.table = table;
    slot->group.group = group;
    slot->group.filelist = filelist;
    slot->group.blob = blob;
    slot->group.file_ids = file_ids;
    slot->group.file_count = archive->file_count;
    slot->group.revision = archive->revision;
    slot->group.bytes = bytes;
    cache->bytes += bytes;
    if( cache->bytes > cache->peak_bytes )
        cache->peak_bytes = cache->bytes;

    return &slot->group;
}

int
Dat2Group_IndexOf(
    struct Dat2Group const* group,
    int file_id)
{
    assert(group);
    if( !group->filelist )
        return -1;

    if( !group->file_ids )
    {
        /* No id table: the member id is its index. */
        if( file_id < 0 || file_id >= group->filelist->file_count )
            return -1;
        return file_id;
    }

    for( int i = 0; i < group->file_count; i++ )
    {
        if( group->file_ids[i] == file_id )
            return i < group->filelist->file_count ? i : -1;
    }
    return -1;
}
