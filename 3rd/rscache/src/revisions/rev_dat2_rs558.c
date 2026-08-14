#include "revisions.h"

#include "../datatypes/dat2_config_obj.h"
#include "../datatypes/dat2_config_sequence.h"

/*
 * RuneScape 2, rev 558 — the 2009-12-09 source cache for the Ancient Curses
 * lane (`cache.rs558_20091209_ancientcurses`), captured six days after The
 * Temple at Senntisten shipped the curses book on 2009-12-03.
 *
 * 558 sits inside the 530 lineage for geometry:
 *
 *   530 <= 558 < 610
 *
 * so it is a FRAMEMAP_V3 build that is still a FRAME_V1 build. Do not copy
 * rs643's explicit FRAME_V2 pin. FRAME_V2 begins at 610; pinning it here would
 * consume a leading byte which is not present and shift every transform in the
 * archive — the trap that burned the rev-530 recon (docs/SUMMONING_PORT.md).
 * FRAME is therefore left derived, as in rev_dat2_rs530.c.
 *
 * Every pin below was measured against this cache rather than assumed, using
 * short decodes (a record whose decode stopped before its payload ended) as the
 * decode gate. Per-type, rev558 vs the rev530 cache as control:
 *
 *   type        rs558 short decodes   rs530 control
 *   spotanim              0                 0
 *   obj                   0                 0
 *   loc                   0                 0
 *   npc                   0                 0
 *   seq              *1389*                 0        <- 530 codec is wrong here
 *
 * SEQUENCE is the one pin that does NOT come from 530. Sequences carry the
 * payload-free opcodes 15/16/18, which the rev-530 codec rejects as unknown:
 * every affected record bailed at its first opcode ("consumed 1 of 101 bytes").
 * Those flags are what RSCACHE_CODEC_SEQUENCE_RS2_727 was written to accept, and
 * under it all 12593 sequences decode to completion — 0 short decodes. That is
 * the evidence the stream is being read correctly and not merely skipped past:
 * had opcode 15 carried a payload here, stepping over it would have misaligned
 * the remainder and stranded records on unknown opcodes.
 *
 * The sequence *encoder* still reports ~11% `differ` on round-trip. That is the
 * RS2 sequence encoder's own gap, not this pin: the same encoder against its
 * home revision (rs727 on cache.rs727_preeoc) reports 51% differ and a non-zero
 * `lost-here`, where rev558 reports `lost-here` 0. Import needs decode, and
 * decode is exact.
 *
 * Known-unsupported types at this revision, none of them on the curses path
 * (which needs spotanim, seq, model, framemap, sprite, interface and synth):
 *
 *   hitsplat   1654/1654 short — fails identically on the rev530 control
 *              (1431/1431), so it is pre-existing to the RS2 branch, not 558.
 *   varc       1043/1043 short — rev530 decodes its own 632 records cleanly,
 *              so the varc stream genuinely changed between 530 and 558.
 *   healthbar   155/155  short — the rev530 cache holds no healthbar records,
 *              so there is no control and the shape is simply unmeasured.
 *
 * These are read-only concerns: nothing packs *into* a 558 cache, so an
 * unsupported decode here cannot corrupt anything. Fix them if a later lane
 * needs them; do not assume the zero-record control above was a pass.
 */

struct RSCache
RSCache_ProfileDat2Rs558(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_RS2;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 558;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_LOC] = RSCACHE_CODEC_LOC_RS2_530;
    cache.codec[RSCACHE_TYPE_OVERLAY] = RSCACHE_CODEC_FLO_RS2;
    cache.codec[RSCACHE_TYPE_UNDERLAY] = RSCACHE_CODEC_FLO_RS2;
    cache.codec[RSCACHE_TYPE_FRAMEMAP] = RSCACHE_CODEC_FRAMEMAP_V3;
    /* Not 530's: rev 558 sequences use the payload-free 15/16/18 flags. */
    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_RS2_727;
    cache.codec[RSCACHE_TYPE_OBJ] = RSCACHE_CODEC_OBJ_RS2_530;

    /* FRAME is deliberately derived: rev 558 is FRAME_V1, below threshold 610. */
    return cache;
}
