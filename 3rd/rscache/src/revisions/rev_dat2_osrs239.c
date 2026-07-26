#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 239 (`cache.osrs239`, manifest_osrs239.ini).
 *
 * Past every gate from 231..238, plus:
 *   - obj opcode 160 stackable = 2
 *   - type-6 component model ids are i32 (gate 237)
 *   - map (lX_Z) archives are stored plain; no xteas.json is shipped (gate 237)
 */

struct RSCache
RSCache_ProfileDat2Osrs239(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 239;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
