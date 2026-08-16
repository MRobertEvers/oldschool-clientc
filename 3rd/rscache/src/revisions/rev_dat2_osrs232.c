#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 232.
 *
 * Cache-side deltas vs 231:
 *   - model V2/V3 gain an optional faceZOffsets section (gate byte + faceCount
 *     signed bytes)
 *   - loc opcode 91 sound-distance-fade curve (already under OSRS_220 gate)
 *   - seq opcode 16 verticalOffset (already in SEQUENCE_V3)
 *   - sprites: non-zero palette index is opaque even when FLAG_ALPHA is set
 */

struct RSCache
RSCache_ProfileDat2Osrs232(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 232;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
