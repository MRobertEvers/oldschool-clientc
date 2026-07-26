#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 238.
 *
 * Cache-side deltas vs 237:
 *   - obj opcode 15 tradeable=false; opcode 65 means geTradeable
 *   - worldmap MapSquare/Zone no longer carry groupId/fileId
 *   - worldmap details/compositemap looked up by archive id when unnamed
 */

struct RSCache
RSCache_ProfileDat2Osrs238(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 238;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
