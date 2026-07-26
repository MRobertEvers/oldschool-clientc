#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 236.
 *
 * Cache-side deltas vs 235:
 *   - npc opcodes 130 idleAnimRestart, 147 zbuf=false
 *   - loc opcode 96 raise (already under OSRS_220 gate)
 */

struct RSCache
RSCache_ProfileDat2Osrs236(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 236;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
