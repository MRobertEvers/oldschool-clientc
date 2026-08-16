#include "platform_x_io_js5_cache.h"
#include <assert.h>

#include "js5/js5_rscache.h"
#include "platform/sockstream.h"

#include <dat2disk.h>

#include <stdlib.h>
#include <string.h>

struct PlatformXIOJs5Cache
{
    struct Js5RscacheStorage storage;
    struct Js5Client* client;
    uint32_t* failed_groups;
    size_t failed_group_count;
    size_t failed_group_capacity;
    int adapter_failed;
};

static void
js5_cache_record_group_failure(
    struct PlatformXIOJs5Cache* cache,
    int archive,
    int group)
{
    uint32_t key = ((uint32_t)archive << 16u) | (uint32_t)group;

    for( size_t i = 0; i < cache->failed_group_count; i++ )
        if( cache->failed_groups[i] == key )
            return;
    if( cache->failed_group_count == cache->failed_group_capacity )
    {
        size_t capacity = cache->failed_group_capacity ? cache->failed_group_capacity * 2u : 16u;
        uint32_t* groups = realloc(cache->failed_groups, capacity * sizeof(*groups));
        assert(groups);
        cache->failed_groups = groups;
        cache->failed_group_capacity = capacity;
    }
    cache->failed_groups[cache->failed_group_count++] = key;
}

struct PlatformXIOJs5Cache*
PlatformXIOJs5Cache_New(
    struct RSCache_Dat2Disk* disk,
    const struct Js5Config* config)
{
    struct PlatformXIOJs5Cache* cache;
    struct Js5Config effective;
    struct Js5StorageOps storage;

    assert(disk);
    assert(config);
    if( sockstream_init() != 0 )
        return NULL;

    cache = calloc(1u, sizeof(*cache));
    assert(cache);
    if( !Js5RscacheStorageInit(&cache->storage, disk, disk->directory) )
    {
        free(cache);
        return NULL;
    }
    storage = Js5RscacheStorageOperations();

    /*
     * The platform cache is generic, so populate every physical table the
     * rscache container can address. Missing master entries are ordinary and
     * therefore optional; every present entry is scanned, and — when the
     * config asks for it — filled in the background. An executor demand
     * promotes its group to the urgent lane either way, so a cache with the
     * background fill off still answers every read; it just stops at what was
     * asked for. See Js5Config::background_fill.
     */
    effective = *config;
    for( int archive = 0; archive < RSCACHE_DAT2_DISK_TABLE_CAPACITY; archive++ )
        Js5ArchiveSelectionSet(
            &effective.archives, archive, true, config->background_fill, true);

    cache->client = Js5ClientNew(&effective, NULL, NULL, &storage, &cache->storage);
    if( !cache->client )
    {
        Js5RscacheStorageClear(&cache->storage);
        free(cache);
        return NULL;
    }
    return cache;
}

void
PlatformXIOJs5Cache_Free(struct PlatformXIOJs5Cache* cache)
{
    if( !cache )
        return;
    Js5ClientFree(cache->client);
    Js5RscacheStorageClear(&cache->storage);
    /* Do not call sockstream_cleanup(): the game socket shares this process. */
    free(cache->failed_groups);
    free(cache);
}

int
PlatformXIOJs5Cache_Tick(
    struct PlatformXIOJs5Cache* cache,
    uint64_t now_ms)
{
    struct Js5Completion completion;
    int result;

    assert(cache);
    result = Js5ClientTick(cache->client, now_ms);

    /* Readiness lives in the client state. Drain notifications here so a full
     * background cache fill does not retain one heap node per group. */
    while( Js5ClientNextCompletion(cache->client, &completion) )
    {
        if( completion.kind == JS5_COMPLETION_ERROR && completion.archive < 255u )
            js5_cache_record_group_failure(cache, completion.archive, completion.group);
    }
    return cache->adapter_failed ? -1 : result;
}

enum Js5RequestResult
PlatformXIOJs5Cache_RequestGroup(
    struct PlatformXIOJs5Cache* cache,
    int archive,
    int group)
{
    assert(cache);
    return Js5ClientRequestGroup(cache->client, archive, group, JS5_PRIORITY_URGENT);
}

bool
PlatformXIOJs5Cache_GroupReady(
    const struct PlatformXIOJs5Cache* cache,
    int archive,
    int group)
{
    return cache && Js5ClientGroupReady(cache->client, archive, group);
}

bool
PlatformXIOJs5Cache_GroupFailed(
    const struct PlatformXIOJs5Cache* cache,
    int archive,
    int group)
{
    uint32_t key;

    if( archive < 0 || archive >= 255 || group < 0 || group > UINT16_MAX )
        return false;
    assert(cache);
    key = ((uint32_t)archive << 16u) | (uint32_t)group;
    for( size_t i = 0; i < cache->failed_group_count; i++ )
        if( cache->failed_groups[i] == key )
            return true;
    return false;
}

bool
PlatformXIOJs5Cache_MetadataReady(const struct PlatformXIOJs5Cache* cache)
{
    return cache && Js5ClientMetadataReady(cache->client);
}

enum Js5Error
PlatformXIOJs5Cache_LastError(const struct PlatformXIOJs5Cache* cache)
{
    assert(cache);
    return cache->adapter_failed ? JS5_ERROR_NO_MEMORY : Js5ClientLastError(cache->client);
}

void
PlatformXIOJs5Cache_GetProgress(
    const struct PlatformXIOJs5Cache* cache,
    struct Js5Progress* progress)
{
    assert(progress);
    if( !cache )
    {
        memset(progress, 0, sizeof(*progress));
        progress->last_error = JS5_ERROR_ARGUMENT;
        return;
    }
    Js5ClientGetProgress(cache->client, progress);
    if( cache->adapter_failed )
    {
        progress->state = JS5_STATE_FAILED;
        progress->last_error = JS5_ERROR_NO_MEMORY;
    }
}
