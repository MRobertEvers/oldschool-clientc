#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 242 (`cache.osrs242`, OpenRS2 #2757, 2026-11-06). Fetched by
 * tools/fetch_cache_osrs242.sh. The repo's default cache.
 */

struct RSCache
RSCache_ProfileDat2Osrs242(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 242;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
