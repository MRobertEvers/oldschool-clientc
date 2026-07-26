#ifndef RSCACHE_TOOLS_TRANSCODE_H
#define RSCACHE_TOOLS_TRANSCODE_H

#include "rscache.h"

#include <stdint.h>

/**
 * Convert a dat1 AnimBase into a dat2 Framemap (V1 layout).
 * Caller frees with RSCache_Dat2FramemapFree.
 */
struct RSCache_Dat2Framemap*
tool_transcode_animbase_to_framemap(
    const struct RSCache_Dat1AnimBase* base,
    int framemap_id);

/**
 * Convert a dat2 Framemap into a dat1 AnimBase.
 * Drops transform_actor / masks / tail with a warning line when present.
 * Caller frees via the AnimBaseFrames free path (or free fields manually).
 */
struct RSCache_Dat1AnimBase*
tool_transcode_framemap_to_animbase(
    const struct RSCache_Dat2Framemap* fm,
    char*** warnings,
    int* warning_count);

/**
 * Re-encode a model to OB2 for dat1 destinations. Returns freshly encoded
 * bytes (caller frees). Warns when source format is not OB2.
 */
uint8_t*
tool_transcode_model_to_ob2(
    const struct RSCache_Model* model,
    const struct RSCache_ModelProvenance* prov,
    uint32_t* out_size,
    char*** warnings,
    int* warning_count);

#endif
