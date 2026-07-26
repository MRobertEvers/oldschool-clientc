#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 184, Kronos client flavour (`cache.kronos`).
 *
 * Two things make this profile worth declaring rather than detecting:
 *
 *  1. It is the only profile that sets RSCACHE_QUIRK_KRONOS. The Kronos client
 *     writes loc opcodes 78/79 without the ambient_sound_retain byte a stock
 *     client of the same revision emits. That is a *client build* difference, not
 *     a revision-ordered one, so no revision comparison can imply it — before
 *     this profile existed the loc decoder's KRONOS flag was unreachable, defined
 *     and branched on but never set by any caller.
 *
 *  2. At 184 it predates every modern layout gate, so it exercises the "old"
 *     side of each ladder: sequence v1, pre-220 loc payloads, pre-210 npc head
 *     icons. Its interfaces reference-table revision is 997.
 */

struct RSCache
RSCache_ProfileDat2Osrs184Kronos(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 184;
    cache.quirks = RSCACHE_QUIRK_KRONOS;

    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V1;

    return cache;
}
