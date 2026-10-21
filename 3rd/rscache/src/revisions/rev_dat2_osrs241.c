#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 241 (`cache.osrs241`, OpenRS2 #2746, 2026-10-20 — the
 * Guthixian Archive release). Fetched by tools/fetch_cache_osrs241.sh.
 *
 * Same gates as 239. `cachepack verify --rev osrs241` against cache.osrs241 is
 * what proves no config opcode moved in 240..241; a new one shows up there as
 * an unknown opcode, not here as a guess.
 */

struct RSCache
RSCache_ProfileDat2Osrs241(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 241;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
