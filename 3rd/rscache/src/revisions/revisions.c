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
RSCache_ProfileForIdentity(
    int game,
    int epoch,
    int revision,
    uint32_t quirks)
{
    struct RSCache profile = RSCache_ProfileZero();
    profile.game = game;
    profile.epoch = epoch;
    profile.revision = revision;
    profile.quirks = quirks;

    /* Exact (game, epoch, revision) match borrows codec pins only. Identity
     * fields and quirks stay as the caller stated them. */
    if( revision != RSCACHE_REVISION_UNKNOWN )
    {
        for( int i = 0; i < REVISION_COUNT; i++ )
        {
            struct RSCache candidate = REVISIONS[i].profile();
            if( candidate.game == game && candidate.epoch == epoch &&
                candidate.revision == revision )
            {
                for( int t = 0; t < RSCACHE_TYPE_COUNT; t++ )
                    profile.codec[t] = candidate.codec[t];
                break;
            }
        }
    }

    return profile;
}
