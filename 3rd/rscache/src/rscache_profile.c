#include "rscache_profile.h"

#include "dat2disk.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct RSCache
RSCache_ProfileZero(void)
{
    struct RSCache cache;
    memset(&cache, 0, sizeof(cache));

    cache.game = RSCACHE_GAME_UNSET;
    cache.epoch = RSCACHE_EPOCH_UNSET;
    cache.revision = RSCACHE_REVISION_UNKNOWN;
    cache.quirks = RSCACHE_QUIRK_NONE;

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
    assert(cache);
    assert(type >= 0 && type < RSCACHE_TYPE_COUNT);
    cache->group_revision[type] = archive_revision;
}

int32_t
RSCache_GroupRevision(
    const struct RSCache* cache,
    enum RSCache_Type type)
{
    assert(cache);
    assert(type >= 0 && type < RSCACHE_TYPE_COUNT);
    return cache->group_revision[type];
}

static bool
revision_at_least_in_game(
    const struct RSCache* cache,
    enum RSCache_Type type,
    int required_game,
    int game_rev,
    int32_t archive_rev_threshold,
    bool default_when_unknown)
{
    assert(cache);

    /*
     * A declared game revision is the authoritative answer — it came from the
     * manifest rather than from interpreting a counter — but only for a cache
     * in the lineage `game_rev` is numbered in.
     *
     * The game test is load-bearing, not defensive. dat1 revisions run to 254
     * in a 2004-era sequence; OldSchool restarted from 1 and is now in the
     * 230s; RS2 is continuous past 643. Without the guard a dat1 rev-254
     * profile satisfies *every* OldSchool threshold in this library — 210,
     * 220, 226, 237 — and each one silently switches a decoder to a field
     * layout that postdates it by nearly twenty years (D16).
     */
    if( cache->game == required_game && cache->revision != RSCACHE_REVISION_UNKNOWN )
        return cache->revision >= game_rev;

    /* Otherwise fall back to the reference client's own gate for this group. */
    int32_t group_revision = RSCache_GroupRevision(cache, type);
    if( group_revision != RSCACHE_GROUP_REVISION_UNKNOWN &&
        archive_rev_threshold != RSCACHE_GROUP_REVISION_UNKNOWN )
        return group_revision >= archive_rev_threshold;

    return default_when_unknown;
}

bool
RSCache_RevisionAtLeastOsrs(
    const struct RSCache* cache,
    enum RSCache_Type type,
    int game_rev,
    int32_t archive_rev_threshold,
    bool default_when_unknown)
{
    assert(cache);
    return revision_at_least_in_game(
        cache, type, RSCACHE_GAME_OLDSCHOOL, game_rev, archive_rev_threshold, default_when_unknown);
}

bool
RSCache_RevisionAtLeastRs2(
    const struct RSCache* cache,
    enum RSCache_Type type,
    int game_rev,
    int32_t archive_rev_threshold,
    bool default_when_unknown)
{
    assert(cache);
    return revision_at_least_in_game(
        cache, type, RSCACHE_GAME_RS2, game_rev, archive_rev_threshold, default_when_unknown);
}

struct RSCache_RecordAddress
RSCache_RecordAddressFor(
    const struct RSCache* cache,
    enum RSCache_Type type)
{
    struct RSCache_RecordAddress addr = { 0 };
    addr.table = RSCACHE_DAT2_TABLE_CONFIGS;
    addr.group = -1;
    addr.group_shift = 0;
    addr.file_mask = 0;

    if( RSCache_IsRs2Dat2(cache) )
    {
        /* Which table each type lives in per void's Index.kt, and the shard width per its
         * DefinitionDecoder subclasses. Only the types a world render needs are mapped so
         * far. `table` stays logical — whoever holds the open cache turns it into an id. */
        switch( type )
        {
        case RSCACHE_TYPE_LOC:
            addr.table = RSCACHE_DAT2_TABLE_LOC;
            addr.group_shift = 8;
            addr.file_mask = 0xFF;
            return addr;
        case RSCACHE_TYPE_NPC:
            addr.table = RSCACHE_DAT2_TABLE_NPC;
            addr.group_shift = 7;
            addr.file_mask = 0x7F;
            return addr;
        case RSCACHE_TYPE_OBJ:
            addr.table = RSCACHE_DAT2_TABLE_OBJ;
            addr.group_shift = 8;
            addr.file_mask = 0xFF;
            return addr;
        case RSCACHE_TYPE_SEQUENCE:
            addr.table = RSCACHE_DAT2_TABLE_SEQ;
            addr.group_shift = 7;
            addr.file_mask = 0x7F;
            return addr;
        case RSCACHE_TYPE_SPOTANIM:
            addr.table = RSCACHE_DAT2_TABLE_SPOTANIM;
            addr.group_shift = 8;
            addr.file_mask = 0xFF;
            return addr;
        case RSCACHE_TYPE_ENUM:
            addr.table = RSCACHE_DAT2_TABLE_ENUM;
            addr.group_shift = 8;
            addr.file_mask = 0xFF;
            return addr;
        case RSCACHE_TYPE_VARBIT:
            /* The widest shard of the lot — 1024 files per group (void's
             * VarBitDecoder: `id ushr 10` / `id and 0x3ff`). */
            addr.table = RSCACHE_DAT2_TABLE_VARBIT;
            addr.group_shift = 10;
            addr.file_mask = 0x3FF;
            return addr;
        default:
            break;
        }
    }

    return addr;
}

int
RSCache_GameFromName(const char* name)
{
    if( !name )
        return RSCACHE_GAME_UNSET;
    if( strcmp(name, "oldschool") == 0 )
        return RSCACHE_GAME_OLDSCHOOL;
    if( strcmp(name, "rs2") == 0 )
        return RSCACHE_GAME_RS2;
    return RSCACHE_GAME_UNSET;
}

const char*
RSCache_GameName(int game)
{
    switch( game )
    {
    case RSCACHE_GAME_OLDSCHOOL:
        return "oldschool";
    case RSCACHE_GAME_RS2:
        return "rs2";
    default:
        return "unset";
    }
}

int
RSCache_EpochFromName(const char* name)
{
    if( !name )
        return RSCACHE_EPOCH_UNSET;
    if( strcmp(name, "dat1") == 0 )
        return RSCACHE_EPOCH_DAT1;
    if( strcmp(name, "dat2") == 0 )
        return RSCACHE_EPOCH_DAT2;
    return RSCACHE_EPOCH_UNSET;
}

const char*
RSCache_EpochName(int epoch)
{
    switch( epoch )
    {
    case RSCACHE_EPOCH_DAT1:
        return "dat1";
    case RSCACHE_EPOCH_DAT2:
        return "dat2";
    default:
        return "unset";
    }
}

bool
RSCache_QuirksFromList(
    const char* list,
    uint32_t* out_quirks)
{
    assert(out_quirks);
    *out_quirks = RSCACHE_QUIRK_NONE;
    if( !list || list[0] == '\0' )
        return false;

    if( strcmp(list, "none") == 0 )
        return true;

    const char* p = list;
    while( *p )
    {
        while( *p == ' ' || *p == ',' )
            p++;
        if( *p == '\0' )
            break;

        const char* start = p;
        while( *p && *p != ',' && *p != ' ' )
            p++;
        int len = (int)(p - start);

        if( len == 4 && strncmp(start, "none", 4) == 0 )
            continue;
        if( len == 6 && strncmp(start, "kronos", 6) == 0 )
        {
            *out_quirks |= RSCACHE_QUIRK_KRONOS;
            continue;
        }
        if( len == 19 && strncmp(start, "void_rs634_no_xteas", 19) == 0 )
        {
            *out_quirks |= RSCACHE_QUIRK_VOID_RS634_NO_XTEAS;
            continue;
        }
        return false;
    }
    return true;
}

void
RSCache_QuirksName(
    uint32_t quirks,
    char* buf,
    int buf_size)
{
    assert(buf);
    assert(buf_size > 0);
    if( quirks == RSCACHE_QUIRK_NONE )
    {
        snprintf(buf, (size_t)buf_size, "none");
        return;
    }

    static const struct
    {
        uint32_t bit;
        const char* name;
    } known[] = {
        { RSCACHE_QUIRK_KRONOS, "kronos" },
        { RSCACHE_QUIRK_VOID_RS634_NO_XTEAS, "void_rs634_no_xteas" },
    };
    const uint32_t known_mask =
        RSCACHE_QUIRK_KRONOS | RSCACHE_QUIRK_VOID_RS634_NO_XTEAS;
    if( (quirks & ~known_mask) != 0u )
    {
        snprintf(buf, (size_t)buf_size, "0x%x", quirks);
        return;
    }

    buf[0] = '\0';
    size_t used = 0;
    for( size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++ )
    {
        if( (quirks & known[i].bit) == 0u )
            continue;
        int n = snprintf(
            buf + used,
            (size_t)buf_size - used,
            "%s%s",
            used > 0 ? "," : "",
            known[i].name);
        if( n < 0 || (size_t)n >= (size_t)buf_size - used )
            return;
        used += (size_t)n;
    }
}
