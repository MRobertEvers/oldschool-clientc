#include "rscache_profile.h"

#include <assert.h>
#include <string.h>

struct RSCache
RSCache_ProfileZero(void)
{
    struct RSCache cache;
    memset(&cache, 0, sizeof(cache));

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.container = RSCACHE_CONTAINER_DAT2;
    cache.epoch = RSCACHE_EPOCH_OSRS;
    cache.version = RSCACHE_REVISION_UNKNOWN;
    cache.quirks = 0u;

    for( int i = 0; i < RSCACHE_TYPE_COUNT; i++ )
    {
        cache.group_revision[i] = RSCACHE_GROUP_REVISION_UNKNOWN;
        cache.codec[i] = RSCACHE_CODEC_AUTO;
    }

    return cache;
}

void
RSCache_ProfileSetGroupRevision(
    struct RSCache* cache,
    enum RSCache_Type type,
    int32_t archive_revision)
{
    if( !cache )
        return;
    assert(type >= 0 && type < RSCACHE_TYPE_COUNT);
    cache->group_revision[type] = archive_revision;
}

int32_t
RSCache_GroupRevision(
    const struct RSCache* cache,
    enum RSCache_Type type)
{
    if( !cache )
        return RSCACHE_GROUP_REVISION_UNKNOWN;
    assert(type >= 0 && type < RSCACHE_TYPE_COUNT);
    return cache->group_revision[type];
}

bool
RSCache_RevisionAtLeastOsrs(
    const struct RSCache* cache,
    enum RSCache_Type type,
    int game_rev,
    int32_t archive_rev_threshold,
    bool default_when_unknown)
{
    if( !cache )
        return default_when_unknown;

    /*
     * A declared game revision is the authoritative answer — it came from the
     * manifest or the handshake rather than from interpreting a counter — but only
     * for a cache in the lineage `game_rev` is numbered in.
     *
     * The epoch test is load-bearing, not defensive. dat1 revisions run to 254 in a
     * 2004-era sequence; OldSchool restarted from 1 and is now in the 230s. Without
     * the guard a dat1 rev-254 profile satisfies *every* threshold in this library —
     * 210, 220, 226, 237 — and each one silently switches a decoder to a field layout
     * that postdates it by nearly twenty years.
     */
    if( cache->epoch == RSCACHE_EPOCH_OSRS && cache->version != RSCACHE_REVISION_UNKNOWN )
        return cache->version >= game_rev;

    /* Otherwise fall back to the reference client's own gate for this group. */
    int32_t group_revision = RSCache_GroupRevision(cache, type);
    if( group_revision != RSCACHE_GROUP_REVISION_UNKNOWN &&
        archive_rev_threshold != RSCACHE_GROUP_REVISION_UNKNOWN )
        return group_revision >= archive_rev_threshold;

    return default_when_unknown;
}
