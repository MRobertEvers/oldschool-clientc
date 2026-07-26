#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 237 — the large format break.
 *
 * Cache-side deltas vs 236:
 *   - EntityOps (sub-ops / conditional ops) on npc, loc, obj
 *   - 32-bit model id opcodes alongside the existing u16 forms
 *   - params opcode 249 type byte 2 = 8-byte long
 *   - clientscript 16-byte trailer with long locals/args + LCONST
 *   - component type-6 model ids already gated at 237 in dat2_component
 *   - map locs stored plain (no XTEA) — see RSCache_MapLocsNeedXtea
 */

struct RSCache
RSCache_ProfileDat2Osrs237(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 237;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
