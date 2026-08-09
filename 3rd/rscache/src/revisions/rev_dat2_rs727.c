#include "revisions.h"

#include "../datatypes/dat2_config_npc.h"
#include "../datatypes/dat2_config_obj.h"

/*
 * RuneScape 2, rev 727 — the last pre-EoC branch (`cache.rs727_preeoc`).
 *
 * game=rs2 epoch=dat2 revision=727. It shares the 643 branch's container and
 * geometry layout, so those codec pins are the same; what moved is the npc and
 * obj *config* streams.
 *
 * ## What changed since 643, and how it was settled
 *
 * The opcode tables come from rsmv (`~/Documents/git_repos/rsmv/src/opcodes/`),
 * which states every RS2/RS3 config opcode with `buildnr` gates. The gates that
 * bite at 727:
 *
 *   - npc opcodes 0x01 / 0x3C hold **varuint** model ids from build 669, and
 *     every obj model field does from build 670. A varuint is two bytes when the
 *     top bit of the first is clear and four when it is set, so under the 643
 *     u16 reading a model list is the right length only by accident and every
 *     field after it lands wrong.
 *   - obj opcodes 0x17 / 0x19 dropped their trailing type byte at build 502.
 *   - npc 0x6A / 0x76 and obj 0x2A / 0x2B carry different structures than the
 *     643 opcodes sharing those numbers, and about thirty opcodes per type have
 *     no 643 counterpart at all.
 *
 * Same opcode number, different structure — so each type gets a codec version
 * rather than a flag widening the 643 body, which is the rule dat2_config_loc.h
 * states and the reason the 643 loc/flo pins exist.
 *
 * ## What the pins are checked against
 *
 * A config record ends with opcode 0 at exactly its file length, so a wrong
 * payload width anywhere makes a record miss its terminator. Over
 * `cache.rs727_preeoc`: **24,803 of 24,803** obj records and **15,632 of
 * 15,661** npc records consume exactly. The 29 that do not are one npc family
 * (ids 8729-9065) using opcodes 0xCC-0xDF that no available reference
 * documents; they stop at that opcode with `_consumed` short, which is the
 * signal every caller already checks. See EXCEPTIONS.md B23.
 *
 * Decode only: there is no 727 encoder, so this profile must not be used to
 * write a cache. EXCEPTIONS.md B23 records that.
 */

struct RSCache
RSCache_ProfileDat2Rs727(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_RS2;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 727;
    cache.quirks = RSCACHE_QUIRK_NONE;

    /* Shared with 643 — the RS2 branch's structural differences from OldSchool,
     * none of which moved between the two revisions. */
    cache.codec[RSCACHE_TYPE_LOC] = RSCACHE_CODEC_LOC_RS2;
    cache.codec[RSCACHE_TYPE_OVERLAY] = RSCACHE_CODEC_FLO_RS2;
    cache.codec[RSCACHE_TYPE_UNDERLAY] = RSCACHE_CODEC_FLO_RS2;
    cache.codec[RSCACHE_TYPE_FRAME] = RSCACHE_CODEC_FRAME_V2;
    cache.codec[RSCACHE_TYPE_FRAMEMAP] = RSCACHE_CODEC_FRAMEMAP_V3;

    /* New at this branch. Both would also be derived from revision >= 669/670,
     * but stating them keeps the revision the only thing a reader has to trust:
     * the profile says which stream it speaks. */
    cache.codec[RSCACHE_TYPE_NPC] = RSCACHE_CODEC_NPC_RS2_BUILD669;
    cache.codec[RSCACHE_TYPE_OBJ] = RSCACHE_CODEC_OBJ_RS2_BUILD670;

    return cache;
}
