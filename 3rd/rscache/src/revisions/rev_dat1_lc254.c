#include "revisions.h"

/*
 * LostCity revision 254 — the old-generation reference the client boots by
 * default (`cache254.lostcity`, manifest_rs254.ini).
 *
 * Jagfile container: config records live in ".dat"/".idx" pairs inside the
 * config jagfile rather than in JS5 groups, strings are newline terminated
 * rather than NUL terminated, and ids are narrow. All of that follows from the
 * container and epoch below, so no per-datatype override is needed.
 */

struct RSCache
RSCache_ProfileDat1Lc254(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.container = RSCACHE_CONTAINER_DAT1;
    cache.epoch = RSCACHE_EPOCH_DAT1_CLASSIC;
    cache.version = 254;

    return cache;
}
