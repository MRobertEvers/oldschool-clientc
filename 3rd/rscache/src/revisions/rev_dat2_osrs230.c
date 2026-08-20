#include "revisions.h"

#include "datatypes/dat2_config_sequence.h"

/*
 * OSRS revision 230 (`cache.osrs230`, manifest_osrs230.ini).
 *
 * Reference: rsprot's osrs-230 tree for the protocol side, and the mock server
 * under src/torirsserver/torirsserver which mirrors the Kronos client.
 *
 * Layout notes that follow from `revision = 230` alone, via each datatype's
 * RSCache_*Flags:
 *   - loc  : >= 220 payloads (opcode 93 is sound fades, 95/96 carry a byte)
 *   - npc  : >= 210 head icons (bitfield, not a single sprite index)
 *   - component: pre-237 field widths, so type-6 model ids stay u16
 *   - map locs: still XTEA-encrypted (gate is OldSchool >= 237)
 *   - seq opcode 18 debugName / spotanim opcode 9 debugName (rev 230 ops)
 */

struct RSCache
RSCache_ProfileDat2Osrs230(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_OLDSCHOOL;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 230;
    cache.quirks = RSCACHE_QUIRK_NONE;

    /* Stated rather than derived: this is the binding the brief asks for — the
     * revision names the codec it uses. The derivation in
     * RSCache_Dat2ConfigSequenceCodecVersion would reach the same answer from
     * revision 230, but pinning it here means a future change to that rule cannot
     * silently re-point an already-verified revision. */
    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_V3;

    return cache;
}
