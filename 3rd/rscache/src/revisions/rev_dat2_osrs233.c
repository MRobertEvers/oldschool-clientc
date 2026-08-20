#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 233 — the xrsps target (manifests/manifest_xrsps.ini).
 *
 * Format-identical to 230 for every datatype this library decodes. Texture
 * codec V2 is selected by RSCache_Dat2TextureCodecVersion at revision >= 233;
 * map locs are still XTEA-encrypted (gate is OldSchool >= 237). Declared
 * separately so `revision=233` resolves to a real profile.
 */

struct RSCache
RSCache_ProfileDat2Osrs233(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 233;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
