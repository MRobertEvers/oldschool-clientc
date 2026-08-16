#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 235.
 *
 * Cache-side deltas vs 234:
 *   - npc opcodes 145 canHideForOverlap, 146 overlapTintHSL
 *   - loc opcode 95 soundVisibility (already under OSRS_220 gate)
 *   - container trailer revision may be 4 bytes when enough remain
 */

struct RSCache
RSCache_ProfileDat2Osrs235(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 235;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
