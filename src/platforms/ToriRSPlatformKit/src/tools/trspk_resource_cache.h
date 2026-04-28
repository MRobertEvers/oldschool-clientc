#ifndef TORIRS_PLATFORM_KIT_TRSPK_RESOURCE_CACHE_H
#define TORIRS_PLATFORM_KIT_TRSPK_RESOURCE_CACHE_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TRSPK_ResourceCache TRSPK_ResourceCache;
typedef struct TRSPK_LruModelCache TRSPK_LruModelCache;

/**
 * @param model_lru_capacity 0 = no CPU mesh LRU; else max cooked-model entries.
 * @param model_lru_vertex_format layout for LRU entries when capacity > 0; must not be NONE.
 *        Ignored when model_lru_capacity is 0.
 */
TRSPK_ResourceCache*
trspk_resource_cache_create(
    uint32_t model_lru_capacity,
    TRSPK_VertexFormat model_lru_vertex_format);
void
trspk_resource_cache_destroy(TRSPK_ResourceCache* cache);
void
trspk_resource_cache_reset(TRSPK_ResourceCache* cache);

bool
trspk_resource_cache_set_model_pose(
    TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    uint32_t pose_index,
    const TRSPK_ModelPose* pose);

const TRSPK_ModelPose*
trspk_resource_cache_get_model_pose(
    const TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    uint32_t pose_index);

uint32_t
trspk_resource_cache_allocate_pose_slot(
    TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    int gpu_segment_slot,
    int frame_index);

const TRSPK_ModelPose*
trspk_resource_cache_get_pose_for_draw(
    const TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    bool use_animation,
    int scene_animation_index,
    int frame_index);

void
trspk_resource_cache_clear_model(
    TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id);

void
trspk_resource_cache_set_model_bake(
    TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    const TRSPK_BakeTransform* bake);

const TRSPK_BakeTransform*
trspk_resource_cache_get_model_bake(
    const TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id);

void
trspk_resource_cache_batch_begin(
    TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id);

void
trspk_resource_cache_batch_set_resource(
    TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id,
    TRSPK_GPUHandle vbo,
    TRSPK_GPUHandle ebo);

void
trspk_resource_cache_batch_set_chunks(
    TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id,
    const TRSPK_GPUHandle* vbos,
    const TRSPK_GPUHandle* ebos,
    uint32_t chunk_count);

const TRSPK_BatchResource*
trspk_resource_cache_batch_get(
    const TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id);

TRSPK_BatchResource
trspk_resource_cache_batch_clear(
    TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id);

bool
trspk_resource_cache_init_atlas(
    TRSPK_ResourceCache* cache,
    uint32_t width,
    uint32_t height);

void
trspk_resource_cache_unload_atlas(TRSPK_ResourceCache* cache);

bool
trspk_resource_cache_load_texture_128(
    TRSPK_ResourceCache* cache,
    TRSPK_TextureId texture_id,
    const uint8_t* rgba_128x128,
    float anim_u,
    float anim_v,
    bool opaque);

const uint8_t*
trspk_resource_cache_get_atlas_pixels(const TRSPK_ResourceCache* cache);

uint32_t
trspk_resource_cache_get_atlas_pixel_size(const TRSPK_ResourceCache* 
cache);

uint32_t
trspk_resource_cache_get_atlas_width(const TRSPK_ResourceCache* cache);

uint32_t
trspk_resource_cache_get_atlas_height(const TRSPK_ResourceCache* cache);

void
trspk_resource_cache_set_atlas_tile(
    TRSPK_ResourceCache* cache,
    TRSPK_TextureId tex_id,
    const TRSPK_AtlasTile* tile);
const TRSPK_AtlasTile*
trspk_resource_cache_get_atlas_tile(
    const TRSPK_ResourceCache* cache,
    TRSPK_TextureId tex_id);
const TRSPK_AtlasTile*
trspk_resource_cache_get_all_tiles(const TRSPK_ResourceCache* cache);

TRSPK_LruModelCache*
trspk_resource_cache_lru_model_cache(TRSPK_ResourceCache* cache);

#ifdef __cplusplus
}
#endif

#endif
