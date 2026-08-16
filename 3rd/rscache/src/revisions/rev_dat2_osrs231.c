#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 231.
 *
 * Cache-side delta vs 230:
 *   - npc opcode 126 footprintSize (u16), with post-decode default
 *     footprintSize = (int)(0.4f * size * 128) when unset
 *   - clientscript PUSH_NULL (63) already took a 1-byte operand
 */

struct RSCache
RSCache_ProfileDat2Osrs231(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 231;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
