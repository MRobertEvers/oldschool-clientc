#ifndef TORIRS_PLATFORM_KIT_TRSPK_LRU_MODEL_CACHE_H
#define TORIRS_PLATFORM_KIT_TRSPK_LRU_MODEL_CACHE_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"
#include "trspk_vertex_buffer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TRSPK_LruModelCache TRSPK_LruModelCache;

TRSPK_LruModelCache*
trspk_lru_model_cache_create(uint32_t capacity);
void
trspk_lru_model_cache_destroy(TRSPK_LruModelCache* cache);
void
trspk_lru_model_cache_reset(TRSPK_LruModelCache* cache);

void
trspk_lru_model_cache_evict_model_id(TRSPK_LruModelCache* cache, TRSPK_ModelId model_id);

const TRSPK_VertexBuffer*
trspk_lru_model_cache_get(
    TRSPK_LruModelCache* cache,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    TRSPK_VertexFormat format);

/** Not valid for format == TRSPK_VERTEX_FORMAT_NONE. */
const TRSPK_VertexBuffer*
trspk_lru_model_cache_get_or_emplace_untextured(
    TRSPK_LruModelCache* cache,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    TRSPK_VertexFormat format,
    const TRSPK_ModelArrays* arrays,
    const TRSPK_BakeTransform* bake);

/** Not valid for format == TRSPK_VERTEX_FORMAT_NONE. */
const TRSPK_VertexBuffer*
trspk_lru_model_cache_get_or_emplace_textured(
    TRSPK_LruModelCache* cache,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    TRSPK_VertexFormat format,
    const TRSPK_ModelArrays* arrays,
    TRSPK_UVMode uv_mode,
    const TRSPK_BakeTransform* bake);

#ifdef __cplusplus
}
#endif

#endif
