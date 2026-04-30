#ifndef TORIRS_PLATFORM_KIT_TRSPK_LRU_MODEL_CACHE_H
#define TORIRS_PLATFORM_KIT_TRSPK_LRU_MODEL_CACHE_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"
#include "trspk_vertex_buffer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TRSPK_ResourceCache TRSPK_ResourceCache;

/**
 * CPU-side LRU of cooked meshes. Each hit stores a heap-owned TRSPK_VertexBuffer
 * (status READY, not BATCH_VIEW). Vertex layout is fixed at create time (vertex_format);
 * get_or_emplace builds in that format (TRSPK, WEBGL1, METAL, WEBGL1_SOAOS, METAL_SOAOS).
 */
typedef struct TRSPK_LruModelCache TRSPK_LruModelCache;

TRSPK_LruModelCache*
trspk_lru_model_cache_create(uint32_t capacity, TRSPK_VertexFormat vertex_format);

/** Vertex format passed to create; NONE if cache is invalid. */
TRSPK_VertexFormat
trspk_lru_model_cache_vertex_format(const TRSPK_LruModelCache* cache);
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
    uint16_t frame_index);

const TRSPK_VertexBuffer*
trspk_lru_model_cache_get_or_emplace_untextured(
    TRSPK_LruModelCache* cache,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    const TRSPK_ModelArrays* arrays,
    const TRSPK_BakeTransform* bake,
    float d3d8_v_repeat);

const TRSPK_VertexBuffer*
trspk_lru_model_cache_get_or_emplace_textured(
    TRSPK_LruModelCache* cache,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    const TRSPK_ModelArrays* arrays,
    TRSPK_UVCalculationMode uv_calc_mode,
    const TRSPK_ResourceCache* atlas_tile_meta,
    const TRSPK_BakeTransform* bake,
    float d3d8_v_repeat);

#ifdef __cplusplus
}
#endif

#endif
