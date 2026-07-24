#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 233 — the xrsps target (manifest_xrsps.ini).
 *
 * Cache-side identical to 230 for every datatype currently decoded; the two
 * differ in the network protocol and transport (233 is served over a plaintext
 * WebSocket with no JS5 handshake). Declared separately so `client_version=233`
 * resolves to a real profile, and so a cache-side difference has a home when one
 * surfaces.
 */

struct RSCache
RSCache_ProfileDat2Osrs233(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.container = RSCACHE_CONTAINER_DAT2;
    cache.epoch = RSCACHE_EPOCH_OSRS;
    cache.version = 233;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
