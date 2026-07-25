#include "revisions.h"

/*
 * The 643 / RuneScape-2 branch (`cache.643`).
 *
 * This is the profile that justifies `epoch` existing as a field separate from
 * `version`. 643-era caches number their reference tables in the same
 * small-integer range OSRS used before it moved to unix-timestamp revisions, so
 * the two families are **indistinguishable by revision alone** — a detector
 * looking only at numbers cannot tell a 643 cache from an early OSRS one. The
 * caller has to say which, and this profile is how it says 643.
 *
 * The layout differences the epoch selects, all in the interface component
 * decoder:
 *   - type 5 blocks carry a trailing colour int and order their flips H,V
 *     (OSRS omits the colour and orders them V,H)
 *   - type 6 blocks carry aShort49 + aBoolean411, with each size-override short
 *     gated on its own mode (OSRS carries both whenever either mode is dynamic)
 *
 * `version` is left unknown deliberately: 643 is not on the OSRS revision line,
 * so comparing it against thresholds like "220" would be meaningless. Datatypes
 * therefore fall back to their archive-revision gates for this cache, which is
 * the correct behaviour — and the reason RSCache_RevisionAtLeastOsrs takes an
 * explicit `default_when_unknown` rather than assuming.
 */

struct RSCache
RSCache_ProfileDat2Rs643(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_RS2;
    cache.container = RSCACHE_CONTAINER_DAT2;
    cache.epoch = RSCACHE_EPOCH_643;

    /*
     * Explicit codec pins. This is the rsprot-style part: the revision *declares* which
     * codec it speaks rather than leaving every datatype to sniff the epoch. Both of these
     * are structural differences from OSRS, not field-level ones:
     *
     *   loc  — opcodes 1 and 5 invert the model-list nesting (one shape owning a list of
     *          models, rather than each model naming its shape).
     *   flo  — overlay opcode 3 *conflicts*: a u16 texture here, a bare flag in the older
     *          reading.
     */
    cache.codec[RSCACHE_TYPE_LOC] = RSCACHE_CODEC_LOC_RS2;
    cache.codec[RSCACHE_TYPE_OVERLAY] = RSCACHE_CODEC_FLO_RS2;
    cache.codec[RSCACHE_TYPE_UNDERLAY] = RSCACHE_CODEC_FLO_RS2;
    cache.version = RSCACHE_REVISION_UNKNOWN;

    return cache;
}
