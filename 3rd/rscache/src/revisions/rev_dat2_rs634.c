#include "revisions.h"

#include "../datatypes/dat2_config_obj.h"
#include "../datatypes/dat2_config_sequence.h"

/*
 * RuneScape 2, rev 634 — the "void" 634 branch (`cache.void634`).
 *
 * game=rs2 epoch=dat2 revision=634. It sits between the two RS2 profiles that
 * already existed, and takes half its pins from each:
 *
 *   530 <= 558 < 634 < 643 <= 727
 *
 * Every pin below was measured against `cache.void634` rather than assumed,
 * using exact consumption as the gate — a config record ends with opcode 0 at
 * exactly its file length, so a wrong payload width anywhere makes a record miss
 * its terminator. Per type, the whole cache, best profile per row in bold:
 *
 *   type      records   rs558      rs643      rs727      **this**
 *   npc        13,984   100.0%     100.0%      72.6%     100.0%
 *   obj        20,178    99.4%      22.5%      76.8%     100.0%
 *   loc        57,203    32.2%     100.0%      75.8%     100.0%
 *   seq        15,237   100.0%      70.4%     100.0%     100.0%
 *   spotanim    2,964   100.0%     100.0%      55.9%     100.0%
 *
 * No single existing profile reads this cache: 643 desyncs every fifth obj
 * record and 558 desyncs two locs in three. That is what the row of pins is for.
 *
 * ## Where each pin comes from
 *
 *   loc       643's `RSCACHE_CODEC_LOC_RS2`. The 530 body reads 18,401 of
 *             57,203 locs; this reads all of them.
 *   obj       Its own codec, `RSCACHE_CODEC_OBJ_RS2_634`, transcribed from the
 *             rev-634 client's item decoder (Class213.method1566): the 530 table
 *             plus opcodes 18, 132 and 134. The 530 body has never heard of
 *             opcode 132 and stops there on 125 records; the 670 body reads
 *             model ids as varuint, which is not true until build 670, and
 *             misaligns 4,687. See obj_decode_op_rs2_634.
 *   seq       558's `RSCACHE_CODEC_SEQUENCE_RS2_727`, for the same reason it is
 *             pinned there: the payload-free opcodes 15/16/18 exist here and the
 *             530/643 body rejects them, stranding 4,516 records at their first
 *             opcode.
 *   npc       Deliberately derived. 634 is below build 669, so the base RS2 npc
 *             body applies and reads every record exactly; pinning
 *             `NPC_RS2_BUILD669` here would be the 727 reading and costs 27%.
 *   spotanim  Deliberately derived, likewise: 100% as it stands, 55.9% under
 *             727's pin.
 *   frame     `FRAME_V2`, as 643: the threshold is 610 and 634 is past it.
 *   framemap  `FRAMEMAP_V3`, as every RS2 profile from 530 up.
 *   flo       `FLO_RS2`, the RS2 lineage's overlay/underlay reading.
 *
 * ## Decode only
 *
 * There is no 634 encoder, exactly as for 727 (EXCEPTIONS.md B23): the obj
 * encoder emits the OldSchool opcode shape whatever the profile says, so this
 * profile must not be used to write a cache.
 *
 * ## Two profiles, because "void634" is a build and not a revision
 *
 * `cache.void634` is a private-server repack whose map locs were rewritten in
 * plaintext, so it ships no xteas.json. That is a property of how the cache was
 * built, not of revision 634 — a stock 634 cache is XTEA-keyed like every RS2
 * dat2 cache from 414 up — so it rides as a quirk on a second profile rather
 * than being baked into the revision. `RSCache_ProfileDat2Rs634` is the
 * revision; `RSCache_ProfileDat2Void634` is that cache.
 */

struct RSCache
RSCache_ProfileDat2Rs634(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_RS2;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 634;
    cache.quirks = RSCACHE_QUIRK_NONE;

    /* Shared with 643 — the RS2 branch's structural differences from OldSchool. */
    cache.codec[RSCACHE_TYPE_LOC] = RSCACHE_CODEC_LOC_RS2;
    cache.codec[RSCACHE_TYPE_OVERLAY] = RSCACHE_CODEC_FLO_RS2;
    cache.codec[RSCACHE_TYPE_UNDERLAY] = RSCACHE_CODEC_FLO_RS2;
    /* Animation frames: leading unused byte + higher-precision transforms (610+). */
    cache.codec[RSCACHE_TYPE_FRAME] = RSCACHE_CODEC_FRAME_V2;
    /* Skeletons / SeqBase: transform_actor + masks (530+). */
    cache.codec[RSCACHE_TYPE_FRAMEMAP] = RSCACHE_CODEC_FRAMEMAP_V3;

    /* Not 643's: rev 634 sequences use the payload-free 15/16/18 flags. */
    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_RS2_727;
    /* The 634 client's own item table: 530's plus opcodes 18, 132 and 134. */
    cache.codec[RSCACHE_TYPE_OBJ] = RSCACHE_CODEC_OBJ_RS2_634;

    /* NPC and SPOTANIM are deliberately derived; see the header comment. */
    return cache;
}

struct RSCache
RSCache_ProfileDat2Void634(void)
{
    struct RSCache cache = RSCache_ProfileDat2Rs634();

    /* Map locs pre-decrypted at repack time; there are no keys to want. */
    cache.quirks |= RSCACHE_QUIRK_VOID_RS634_NO_XTEAS;

    return cache;
}
