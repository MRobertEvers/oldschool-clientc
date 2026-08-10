#include "revisions.h"

#include "../datatypes/dat2_config_obj.h"
#include "../datatypes/dat2_config_sequence.h"

/*
 * RuneScape 2, rev 530 — the January 2009 source cache for the marked
 * 2009scape Summoning lane.
 *
 * The boundary declarations here are the important part.  Revision 530 is the
 * first FRAMEMAP_V3 build, but it is still a FRAME_V1 build (FRAME_V2 starts at
 * 610).  In particular, do not copy rs643's explicit FRAME_V2 pin: doing so
 * consumes a byte which is not present and shifts every transform in the
 * archive.
 *
 * Sequence and obj are different streams from both contemporary OldSchool and
 * the later RS2 branches, so this profile pins their exact rev-530 codecs.
 */

struct RSCache
RSCache_ProfileDat2Rs530(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_RS2;
    cache.epoch = RSCACHE_EPOCH_DAT2;
    cache.revision = 530;
    cache.quirks = RSCACHE_QUIRK_NONE;

    cache.codec[RSCACHE_TYPE_LOC] = RSCACHE_CODEC_LOC_RS2_530;
    cache.codec[RSCACHE_TYPE_OVERLAY] = RSCACHE_CODEC_FLO_RS2;
    cache.codec[RSCACHE_TYPE_UNDERLAY] = RSCACHE_CODEC_FLO_RS2;
    cache.codec[RSCACHE_TYPE_FRAMEMAP] = RSCACHE_CODEC_FRAMEMAP_V3;
    cache.codec[RSCACHE_TYPE_SEQUENCE] = RSCACHE_CODEC_SEQUENCE_RS2_530;
    cache.codec[RSCACHE_TYPE_OBJ] = RSCACHE_CODEC_OBJ_RS2_530;

    /* FRAME is deliberately derived: rev 530 is FRAME_V1, below threshold 610. */
    return cache;
}
