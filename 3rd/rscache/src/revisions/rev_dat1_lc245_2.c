#include "revisions.h"

/*
 * Revision 245.2 — the other old-generation profile the client ships
 * (`cache254`, RevConfig rev_245_2).
 *
 * Layout-identical to 254 for every datatype this library decodes; the
 * differences between the two are in the network protocol, not the cache. Kept
 * as its own profile anyway so that when a cache-side difference does turn up it
 * has an obvious home, and so `--rev lc245_2` maps to something rather than
 * silently borrowing 254's identity.
 */

struct RSCache
RSCache_ProfileDat1Lc245_2(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.container = RSCACHE_CONTAINER_DAT1;
    cache.epoch = RSCACHE_EPOCH_DAT1_CLASSIC;
    cache.version = 245;

    return cache;
}
