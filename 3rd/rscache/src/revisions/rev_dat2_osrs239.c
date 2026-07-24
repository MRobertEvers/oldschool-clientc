#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 239 (`cache.osrs239`).
 *
 * The first declared revision past the 237 interface change: type-6 component
 * model ids widened from u16 to i32 (if1 reads two of them, if3 one). That
 * follows from `version = 239` through
 * RSCache_Dat2ComponentDecodeRevFromProfile, so no override is needed here —
 * but it does mean this profile and 230's are *not* interchangeable, unlike
 * 230 and 233.
 */

struct RSCache
RSCache_ProfileDat2Osrs239(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.container = RSCACHE_CONTAINER_DAT2;
    cache.epoch = RSCACHE_EPOCH_OSRS;
    cache.version = 239;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
