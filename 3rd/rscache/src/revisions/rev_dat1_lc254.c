#include "revisions.h"

/*
 * LostCity revision 254 — the old-generation reference the client boots by
 * default (`cache254.lostcity`, manifest_rs254.ini).
 *
 * Jagfile container: config records live in ".dat"/".idx" pairs inside the
 * config jagfile rather than in JS5 groups, strings are newline terminated
 * rather than NUL terminated, and ids are narrow. All of that follows from
 * epoch=dat1 below, so no per-datatype override is needed.
 *
 * game=rs2: dat1 classic and RS2/main are one continuous lineage across the
 * container change. revision=254 is comparable with revision=643.
 */

struct RSCache
RSCache_ProfileDat1Lc254(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_RS2;
    cache.epoch = RSCACHE_EPOCH_DAT1;
    cache.revision = 254;
    cache.quirks = RSCACHE_QUIRK_NONE;

    return cache;
}
