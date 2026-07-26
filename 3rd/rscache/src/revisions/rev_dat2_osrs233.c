#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 233 — the xrsps target (manifest_xrsps.ini).
 *
 * Cache-side identical to 230 for every datatype currently decoded; the two
 * differ in the network protocol and transport (233 is served over a plaintext
 * WebSocket with no JS5 handshake). Declared separately so `revision=233`
 * resolves to a real profile, and so a cache-side difference has a home when one
 * surfaces. Map locs are still XTEA-encrypted (gate is OldSchool >= 237).
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
