#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 234.
 *
 * Cache-side deltas vs 233:
 *   - npc opcode 129 unknown1 (payload-free flag)
 *   - loc opcode 94 unknown1 (payload-free flag under OSRS; RS2 still uses it
 *     as contour_ground_type = 4)
 *   - seq opcode 19 soundsCrossWorldView
 */

struct RSCache
RSCache_ProfileDat2Osrs234(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 234;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
