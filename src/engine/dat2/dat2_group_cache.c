#include "dat2_group_cache.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <rscache.h>

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
};

struct Dat2GroupCache*
Dat2GroupCache_New(size_t budget_bytes)
{
    struct Dat2GroupCache* cache = calloc(1, sizeof(*cache));
    if( !cache )
        return NULL;
    cache->budget = budget_bytes;
    return cache;
}

static void
dat2_group_cache_release(struct Dat2GroupCacheSlot* slot, size_t* bytes)
{
    if( !slot->occupied )
        return;
    if( slot->group.filelist )
        RSCache_FileListFree(slot->group.filelist);
    free(slot->group.file_ids);
    assert(*bytes >= slot->group.bytes);
    *bytes -= slot->group.bytes;
    memset(slot, 0, sizeof(*slot));
}

void
Dat2GroupCache_Free(struct Dat2GroupCache* cache)
{
    if( !cache )
        return;
    Dat2GroupCache_Clear(cache);
    free(cache);
}

void
Dat2GroupCache_Clear(struct Dat2GroupCache* cache)
{
    if( !cache )
        return;
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

    if( !cache )
        return NULL;
    slot = dat2_group_cache_find(cache, table, group);
    if( !slot )
        return NULL;
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

    if( !filelist )
        return 0;
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
    int* file_ids = NULL;
    size_t bytes;

    if( !cache || !archive )
        return NULL;

    slot = dat2_group_cache_find(cache, table, group);
    if( slot )
    {
        slot->last_used = ++cache->clock;
        return &slot->group;
    }

    filelist = RSCache_FileListNewFromDecode(
        archive->data, archive->data_size, archive->file_count);
    if( !filelist )
        return NULL;

    if( archive->file_ids && archive->file_count > 0 )
    {
        file_ids = malloc((size_t)archive->file_count * sizeof(*file_ids));
        if( !file_ids )
        {
            RSCache_FileListFree(filelist);
            return NULL;
        }
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
        RSCache_FileListFree(filelist);
        free(file_ids);
        return NULL;
    }

    slot->occupied = 1;
    slot->last_used = ++cache->clock;
    slot->group.table = table;
    slot->group.group = group;
    slot->group.filelist = filelist;
    slot->group.file_ids = file_ids;
    slot->group.file_count = archive->file_count;
    slot->group.revision = archive->revision;
    slot->group.bytes = bytes;
    cache->bytes += bytes;

    return &slot->group;
}

int
Dat2Group_IndexOf(
    struct Dat2Group const* group,
    int file_id)
{
    if( !group || !group->filelist )
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
