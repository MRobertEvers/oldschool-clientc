#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 239 (`cache.osrs239`, manifest_osrs239.ini).
 *
 * Past two independent gates that land at OldSchool 237:
 *   - type-6 component model ids widened from u16 to i32
 *   - map (lX_Z) archives are stored plain; no xteas.json is shipped
 *
 * Those are separate constants on purpose — two unrelated layout changes in one
 * revision is ordinary, but sharing a #define would let a correction to one
 * silently move the other.
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
