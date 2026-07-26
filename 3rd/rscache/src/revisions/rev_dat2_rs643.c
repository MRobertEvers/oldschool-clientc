#include "revisions.h"

/*
 * The 643 / RuneScape-2 branch (`cache.643`).
 *
 * game=rs2 epoch=dat2 revision=643. All four fields are significant: revision
 * is safely comparable against RS2 thresholds (474 for procedural textures,
 * 537/555/582/629 for their flag ladder) because RSCache_RevisionAtLeastRs2
 * requires game=rs2, and OldSchool thresholds never see it because
 * RSCache_RevisionAtLeastOsrs requires game=oldschool.
 *
 * The layout differences RSCache_IsRs2Dat2 selects, all in the interface
 * component decoder:
 *   - type 5 blocks carry a trailing colour int and order their flips H,V
 *     (OSRS omits the colour and orders them V,H)
 *   - type 6 blocks carry aShort49 + aBoolean411, with each size-override short
 *     gated on its own mode (OSRS carries both whenever either mode is dynamic)
 */

struct RSCache
RSCache_ProfileDat2Rs643(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_RS2;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 643;
    cache.quirks = RSCACHE_QUIRK_NONE;

    /*
     * Explicit codec pins. This is the rsprot-style part: the revision *declares* which
     * codec it speaks rather than leaving every datatype to sniff the game. Both of these
     * are structural differences from OSRS, not field-level ones:
     *
 *   loc  — opcodes 1 and 5 invert the model-list nesting (one shape owning a list of
 *          models, rather than each model naming its shape).
 *   flo  — overlay opcode 3 *conflicts*: a u16 texture here, a bare flag in the older
 *          reading.
 *   frame — RS >= 610: leading unused byte before the framemap id, and ORIGIN/
 *           TRANSLATE / ROTATE transforms stored at higher precision (>>2 / >>4 on
 *           read). Without this pin, idle frames fail to decode and NPCs stay in
 *           bind pose.
 */
    cache.codec[RSCACHE_TYPE_LOC] = RSCACHE_CODEC_LOC_RS2;
    cache.codec[RSCACHE_TYPE_OVERLAY] = RSCACHE_CODEC_FLO_RS2;
    cache.codec[RSCACHE_TYPE_UNDERLAY] = RSCACHE_CODEC_FLO_RS2;
    /* Animation frames: leading unused byte + higher-precision transforms (rev 610+). */
    cache.codec[RSCACHE_TYPE_FRAME] = RSCACHE_CODEC_FRAME_V2;

    return cache;
}
