#include "revisions.h"

#include <string.h>

/*
 * The revision registry.
 *
 * A flat table built once, in the spirit of rsprot's ProtRepository: name in,
 * profile out, no allocation and no hashing. Keeping it in one place means the
 * set of supported revisions is greppable, and adding one is a single row.
 *
 * Names match the vocabulary already used by --rev and by the manifests'
 * [net:boot] rev= field, so a caller can hand either straight through.
 */

struct revision_entry
{
    const char* name;
    struct RSCache (*profile)(void);
};

static const struct revision_entry REVISIONS[] = {
    /* dat1 */
    { "lc254", RSCache_ProfileDat1Lc254 },
    { "lc245_2", RSCache_ProfileDat1Lc245_2 },
    /* dat2 */
    { "osrs184", RSCache_ProfileDat2Osrs184Kronos },
    { "kronos", RSCache_ProfileDat2Osrs184Kronos },
    { "osrs230", RSCache_ProfileDat2Osrs230 },
    { "osrs233", RSCache_ProfileDat2Osrs233 },
    /* The xrsps server serves a stock rev-233 cache; the two names differ only
     * in transport, which is not a cache concern. */
    { "xrsps233", RSCache_ProfileDat2Osrs233 },
    { "osrs239", RSCache_ProfileDat2Osrs239 },
    { "643", RSCache_ProfileDat2Rs643 },
    { "rs643", RSCache_ProfileDat2Rs643 },
};

#define REVISION_COUNT ((int)(sizeof(REVISIONS) / sizeof(REVISIONS[0])))

bool
RSCache_ProfileByName(
    const char* name,
    struct RSCache* out)
{
    if( !name || !out )
        return false;

    for( int i = 0; i < REVISION_COUNT; i++ )
    {
        if( strcmp(REVISIONS[i].name, name) == 0 )
        {
            *out = REVISIONS[i].profile();
            return true;
        }
    }
    return false;
}

struct RSCache
RSCache_ProfileForContainerRevision(
    int container,
    int game_revision)
{
    /* Exact match on both axes first. */
    for( int i = 0; i < REVISION_COUNT; i++ )
    {
        struct RSCache candidate = REVISIONS[i].profile();
        if( candidate.container == container && candidate.version == game_revision )
            return candidate;
    }

    /* No declared profile for this revision. Fall back to the closest declared
     * one at or below it in the same container, so the caller still gets the
     * right container, epoch and string terminator — the things that would
     * corrupt a decode outright — and only loses the fine-grained field gates,
     * which the revision itself can still drive. */
    struct RSCache best = RSCache_ProfileZero();
    bool found = false;
    for( int i = 0; i < REVISION_COUNT; i++ )
    {
        struct RSCache candidate = REVISIONS[i].profile();
        if( candidate.container != container )
            continue;
        if( candidate.version == RSCACHE_REVISION_UNKNOWN )
            continue;
        if( candidate.version > game_revision )
            continue;
        if( !found || candidate.version > best.version )
        {
            best = candidate;
            found = true;
        }
    }

    if( !found )
    {
        /* Newer than nothing we know, or a container with no declared profile.
         * Start from zero and state what the caller told us. */
        best = RSCache_ProfileZero();
        best.container = container;
        best.epoch = container == RSCACHE_CONTAINER_DAT1 ? RSCACHE_EPOCH_DAT1_CLASSIC
                                                         : RSCACHE_EPOCH_OSRS;
    }

    /* Carry the caller's revision through even when it matched no row: it is
     * more specific than the profile we borrowed, and every era gate keys off
     * it. Drop codec pins from the borrowed profile, which were verified against
     * that revision rather than this one. */
    if( game_revision != RSCACHE_REVISION_UNKNOWN && best.version != game_revision )
    {
        best.version = game_revision;
        for( int i = 0; i < RSCACHE_TYPE_COUNT; i++ )
            best.codec[i] = RSCACHE_CODEC_AUTO;
    }

    return best;
}
