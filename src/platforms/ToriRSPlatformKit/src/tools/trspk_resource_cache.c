#include "trspk_resource_cache.h"

#include "../../include/ToriRSPlatformKit/trspk_math.h"
#include "trspk_lru_model_cache.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct TRSPK_ModelData
{
    TRSPK_ModelPose poses[TRSPK_MAX_POSES_PER_MODEL];
    uint32_t pose_count;
    uint32_t animation_offsets[3];
    TRSPK_BakeTransform model_bake;
    bool has_model_bake;
} TRSPK_ModelData;

struct TRSPK_ResourceCache
{
    TRSPK_ModelData models[TRSPK_MAX_MODELS];
    TRSPK_BatchResource batches[TRSPK_MAX_BATCHES];
    TRSPK_AtlasTile atlas_tiles[TRSPK_MAX_TEXTURES];
    bool atlas_tile_valid[TRSPK_MAX_TEXTURES];
    uint8_t* atlas_pixels;
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t atlas_pixel_size;
    uint32_t lru_capacity;
    TRSPK_VertexFormat lru_vertex_format;
    TRSPK_LruModelCache* lru_model;
};

static bool
trspk_valid_model_id(TRSPK_ModelId model_id)
{
    return (uint32_t)model_id < TRSPK_MAX_MODELS;
}

static bool
trspk_valid_texture_id(TRSPK_TextureId texture_id)
{
    return (uint32_t)texture_id < TRSPK_MAX_TEXTURES;
}

TRSPK_ResourceCache*
trspk_resource_cache_create(
    uint32_t model_lru_capacity,
    TRSPK_VertexFormat model_lru_vertex_format)
{
    TRSPK_ResourceCache* cache = (TRSPK_ResourceCache*)calloc(1u, sizeof(TRSPK_ResourceCache));
    if( !cache )
        return NULL;
    cache->lru_capacity = model_lru_capacity;
    cache->lru_vertex_format = model_lru_vertex_format;
    if( model_lru_capacity > 0u )
    {
        cache->lru_model =
            trspk_lru_model_cache_create(model_lru_capacity, model_lru_vertex_format);
        if( !cache->lru_model )
            cache->lru_capacity = 0u;
    }
    trspk_resource_cache_reset(cache);
    return cache;
}

void
trspk_resource_cache_destroy(TRSPK_ResourceCache* cache)
{
    if( !cache )
        return;
    trspk_lru_model_cache_destroy(cache->lru_model);
    cache->lru_model = NULL;
    free(cache->atlas_pixels);
    free(cache);
}

void
trspk_resource_cache_reset(TRSPK_ResourceCache* cache)
{
    if( !cache )
        return;
    free(cache->atlas_pixels);
    cache->atlas_pixels = NULL;
    if( cache->lru_model )
        trspk_lru_model_cache_reset(cache->lru_model);
    {
        const uint32_t lcap = cache->lru_capacity;
        const TRSPK_VertexFormat lfmt = cache->lru_vertex_format;
        TRSPK_LruModelCache* lru = cache->lru_model;
        memset(cache, 0, sizeof(*cache));
        cache->lru_model = lru;
        cache->lru_capacity = lcap;
        cache->lru_vertex_format = lfmt;
    }
    for( uint32_t i = 0; i < TRSPK_MAX_MODELS; ++i )
        cache->models[i].model_bake = trspk_bake_transform_identity();
}

bool
trspk_resource_cache_set_model_pose(
    TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    uint32_t pose_index,
    const TRSPK_ModelPose* pose)
{
    if( !cache || !pose || !trspk_valid_model_id(model_id) ||
        pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return false;
    TRSPK_ModelData* model = &cache->models[model_id];
    model->poses[pose_index] = *pose;
    if( model->pose_count <= pose_index )
        model->pose_count = pose_index + 1u;
    return true;
}

void
trspk_resource_cache_invalidate_model_pose(
    TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    uint32_t pose_index)
{
    if( !cache || !trspk_valid_model_id(model_id) || pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return;
    cache->models[model_id].poses[pose_index].valid = false;
}

void
trspk_resource_cache_invalidate_poses_for_batch(
    TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id)
{
    if( !cache || batch_id >= TRSPK_MAX_BATCHES )
        return;
    for( uint32_t mid = 0u; mid < TRSPK_MAX_MODELS; ++mid )
    {
        TRSPK_ModelData* m = &cache->models[mid];
        for( uint32_t pi = 0u; pi < TRSPK_MAX_POSES_PER_MODEL; ++pi )
        {
            TRSPK_ModelPose* p = &m->poses[pi];
            if( !p->valid || p->dynamic )
                continue;
            if( p->batch_id != batch_id )
                continue;
            p->valid = false;
        }
    }
}

const TRSPK_ModelPose*
trspk_resource_cache_get_model_pose(
    const TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    uint32_t pose_index)
{
    if( !cache || !trspk_valid_model_id(model_id) || pose_index >= TRSPK_MAX_POSES_PER_MODEL )
        return NULL;
    const TRSPK_ModelData* model = &cache->models[model_id];
    if( pose_index >= model->pose_count || !model->poses[pose_index].valid )
        return NULL;
    return &model->poses[pose_index];
}

uint32_t
trspk_resource_cache_allocate_pose_slot(
    TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    int gpu_segment_slot,
    int frame_index)
{
    if( !cache || !trspk_valid_model_id(model_id) )
        return 0u;
    TRSPK_ModelData* model = &cache->models[model_id];
    if( gpu_segment_slot == (int)TRSPK_GPU_ANIM_NONE_IDX )
    {
        model->animation_offsets[0] = 0u;
        if( model->pose_count < 1u )
            model->pose_count = 1u;
        return 0u;
    }
    if( frame_index < 0 )
        return 0u;
    if( gpu_segment_slot == (int)TRSPK_GPU_ANIM_PRIMARY_IDX )
    {
        if( model->animation_offsets[1] == 0u )
            model->animation_offsets[1] = model->pose_count < 1u ? 1u : model->pose_count;
        const uint32_t idx = model->animation_offsets[1] + (uint32_t)frame_index;
        if( idx < TRSPK_MAX_POSES_PER_MODEL && model->pose_count <= idx )
            model->pose_count = idx + 1u;
        return idx < TRSPK_MAX_POSES_PER_MODEL ? idx : 0u;
    }
    if( gpu_segment_slot == (int)TRSPK_GPU_ANIM_SECONDARY_IDX )
    {
        if( model->animation_offsets[2] == 0u )
            model->animation_offsets[2] = model->pose_count < 1u ? 1u : model->pose_count;
        const uint32_t idx = model->animation_offsets[2] + (uint32_t)frame_index;
        if( idx < TRSPK_MAX_POSES_PER_MODEL && model->pose_count <= idx )
            model->pose_count = idx + 1u;
        return idx < TRSPK_MAX_POSES_PER_MODEL ? idx : 0u;
    }
    return 0u;
}

const TRSPK_ModelPose*
trspk_resource_cache_get_pose_for_draw(
    const TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    bool use_animation,
    int scene_animation_index,
    int frame_index)
{
    if( !use_animation )
        return trspk_resource_cache_get_model_pose(cache, model_id, 0u);
    if( !cache || !trspk_valid_model_id(model_id) || scene_animation_index < 0 ||
        scene_animation_index > 1 || frame_index < 0 )
        return NULL;
    const TRSPK_ModelData* model = &cache->models[model_id];
    const uint32_t base = model->animation_offsets[(uint32_t)scene_animation_index + 1u];
    return trspk_resource_cache_get_model_pose(cache, model_id, base + (uint32_t)frame_index);
}

void
trspk_resource_cache_clear_model(
    TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id)
{
    if( !cache || !trspk_valid_model_id(model_id) )
        return;
    if( cache->lru_model )
        trspk_lru_model_cache_evict_model_id(cache->lru_model, model_id);
    memset(&cache->models[model_id], 0, sizeof(cache->models[model_id]));
    cache->models[model_id].model_bake = trspk_bake_transform_identity();
}

void
trspk_resource_cache_set_model_bake(
    TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    const TRSPK_BakeTransform* bake)
{
    if( !cache || !bake || !trspk_valid_model_id(model_id) )
        return;
    cache->models[model_id].model_bake = *bake;
    cache->models[model_id].has_model_bake = true;
}

uint32_t
trspk_resource_cache_get_pose_index_for_draw(
    const TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id,
    bool use_animation,
    int scene_animation_index,
    int frame_index)
{
    if( !use_animation )
        return 0u;
    if( !cache || !trspk_valid_model_id(model_id) || scene_animation_index < 0 ||
        scene_animation_index > 1 || frame_index < 0 )
        return UINT32_MAX;
    const TRSPK_ModelData* model = &cache->models[model_id];
    const uint32_t base = model->animation_offsets[(uint32_t)scene_animation_index + 1u];
    return base + (uint32_t)frame_index;
}

const TRSPK_BakeTransform*
trspk_resource_cache_get_model_bake(
    const TRSPK_ResourceCache* cache,
    TRSPK_ModelId model_id)
{
    if( !cache || !trspk_valid_model_id(model_id) || !cache->models[model_id].has_model_bake )
        return NULL;
    return &cache->models[model_id].model_bake;
}

void
trspk_resource_cache_batch_begin(
    TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id)
{
    if( !cache || batch_id >= TRSPK_MAX_BATCHES )
        return;
    memset(&cache->batches[batch_id], 0, sizeof(cache->batches[batch_id]));
    cache->batches[batch_id].valid = true;
}

void
trspk_resource_cache_batch_set_resource(
    TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id,
    TRSPK_GPUHandle vbo,
    TRSPK_GPUHandle ebo)
{
    if( !cache || batch_id >= TRSPK_MAX_BATCHES )
        return;
    cache->batches[batch_id].vbo = vbo;
    cache->batches[batch_id].ebo = ebo;
    cache->batches[batch_id].world_vao = 0u;
    cache->batches[batch_id].chunk_count = 1u;
    cache->batches[batch_id].chunk_vbos[0] = vbo;
    cache->batches[batch_id].chunk_ebos[0] = ebo;
    cache->batches[batch_id].valid = true;
}

void
trspk_resource_cache_batch_set_world_vao(
    TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id,
    TRSPK_GPUHandle world_vao)
{
    if( !cache || batch_id >= TRSPK_MAX_BATCHES )
        return;
    cache->batches[batch_id].world_vao = world_vao;
}

void
trspk_resource_cache_batch_set_chunks(
    TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id,
    const TRSPK_GPUHandle* vbos,
    const TRSPK_GPUHandle* ebos,
    uint32_t chunk_count)
{
    if( !cache || batch_id >= TRSPK_MAX_BATCHES )
        return;
    if( chunk_count > TRSPK_MAX_WEBGL1_CHUNKS )
        chunk_count = TRSPK_MAX_WEBGL1_CHUNKS;
    TRSPK_BatchResource* b = &cache->batches[batch_id];
    memset(b, 0, sizeof(*b));
    b->chunk_count = chunk_count;
    for( uint32_t i = 0; i < chunk_count; ++i )
    {
        b->chunk_vbos[i] = vbos ? vbos[i] : 0u;
        b->chunk_ebos[i] = ebos ? ebos[i] : 0u;
    }
    b->vbo = chunk_count ? b->chunk_vbos[0] : 0u;
    b->ebo = chunk_count ? b->chunk_ebos[0] : 0u;
    b->valid = true;
}

const TRSPK_BatchResource*
trspk_resource_cache_batch_get(
    const TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id)
{
    if( !cache || batch_id >= TRSPK_MAX_BATCHES || !cache->batches[batch_id].valid )
        return NULL;
    return &cache->batches[batch_id];
}

TRSPK_BatchResource
trspk_resource_cache_batch_clear(
    TRSPK_ResourceCache* cache,
    TRSPK_BatchId batch_id)
{
    TRSPK_BatchResource old_resource;
    memset(&old_resource, 0, sizeof(old_resource));
    if( !cache || batch_id >= TRSPK_MAX_BATCHES )
        return old_resource;
    old_resource = cache->batches[batch_id];
    memset(&cache->batches[batch_id], 0, sizeof(cache->batches[batch_id]));
    return old_resource;
}

bool
trspk_resource_cache_init_atlas(
    TRSPK_ResourceCache* cache,
    uint32_t width,
    uint32_t height)
{
    if( !cache || width == 0u || height == 0u )
        return false;
    const uint32_t bytes = width * height * TRSPK_ATLAS_BYTES_PER_PIXEL;
    uint8_t* pixels = (uint8_t*)calloc(1u, bytes);
    if( !pixels )
        return false;
    free(cache->atlas_pixels);
    cache->atlas_pixels = pixels;
    cache->atlas_width = width;
    cache->atlas_height = height;
    cache->atlas_pixel_size = bytes;

    const uint32_t columns = width / TRSPK_TEXTURE_DIMENSION;
    const float du = (float)TRSPK_TEXTURE_DIMENSION / (float)width;
    const float dv = (float)TRSPK_TEXTURE_DIMENSION / (float)height;
    for( uint32_t i = 0; i < TRSPK_MAX_TEXTURES; ++i )
    {
        TRSPK_AtlasTile tile;
        memset(&tile, 0, sizeof(tile));
        if( columns > 0u )
        {
            tile.u0 = (float)(i % columns) * du;
            tile.v0 = (float)(i / columns) * dv;
            tile.du = du;
            tile.dv = dv;
        }
        tile.opaque = 1.0f;
        cache->atlas_tiles[i] = tile;
        cache->atlas_tile_valid[i] = true;
    }
    return true;
}

void
trspk_resource_cache_unload_atlas(TRSPK_ResourceCache* cache)
{
    if( !cache )
        return;
    free(cache->atlas_pixels);
    cache->atlas_pixels = NULL;
    cache->atlas_width = 0u;
    cache->atlas_height = 0u;
    cache->atlas_pixel_size = 0u;
    memset(cache->atlas_tiles, 0, sizeof(cache->atlas_tiles));
    memset(cache->atlas_tile_valid, 0, sizeof(cache->atlas_tile_valid));
}

bool
trspk_resource_cache_load_texture_128(
    TRSPK_ResourceCache* cache,
    TRSPK_TextureId texture_id,
    const uint8_t* rgba_128x128,
    float anim_u,
    float anim_v,
    bool opaque)
{
    if( !cache || !cache->atlas_pixels || !rgba_128x128 || !trspk_valid_texture_id(texture_id) )
        return false;
    const uint32_t columns = cache->atlas_width / TRSPK_TEXTURE_DIMENSION;
    if( columns == 0u )
        return false;
    const uint32_t col = (uint32_t)texture_id % columns;
    const uint32_t row = (uint32_t)texture_id / columns;
    const uint32_t start_x = col * TRSPK_TEXTURE_DIMENSION;
    const uint32_t start_y = row * TRSPK_TEXTURE_DIMENSION;
    const uint32_t atlas_stride = cache->atlas_width * TRSPK_ATLAS_BYTES_PER_PIXEL;
    const uint32_t texture_stride = TRSPK_TEXTURE_DIMENSION * TRSPK_ATLAS_BYTES_PER_PIXEL;
    for( uint32_t y = 0; y < TRSPK_TEXTURE_DIMENSION; ++y )
    {
        const uint32_t dst = (start_y + y) * atlas_stride + start_x * TRSPK_ATLAS_BYTES_PER_PIXEL;
        memcpy(cache->atlas_pixels + dst, rgba_128x128 + y * texture_stride, texture_stride);
    }
    TRSPK_AtlasTile tile = cache->atlas_tiles[texture_id];
    tile.anim_u = anim_u;
    tile.anim_v = anim_v;
    tile.opaque = opaque ? 1.0f : 0.0f;
    cache->atlas_tiles[texture_id] = tile;
    cache->atlas_tile_valid[texture_id] = true;
    return true;
}

const uint8_t*
trspk_resource_cache_get_atlas_pixels(const TRSPK_ResourceCache* cache)
{
    return cache ? cache->atlas_pixels : NULL;
}

uint32_t
trspk_resource_cache_get_atlas_pixel_size(const TRSPK_ResourceCache* cache)
{
    return cache ? cache->atlas_pixel_size : 0u;
}

uint32_t
trspk_resource_cache_get_atlas_width(const TRSPK_ResourceCache* cache)
{
    return cache ? cache->atlas_width : 0u;
}

uint32_t
trspk_resource_cache_get_atlas_height(const TRSPK_ResourceCache* cache)
{
    return cache ? cache->atlas_height : 0u;
}

void
trspk_resource_cache_set_atlas_tile(
    TRSPK_ResourceCache* cache,
    TRSPK_TextureId tex_id,
    const TRSPK_AtlasTile* tile)
{
    if( !cache || !tile || !trspk_valid_texture_id(tex_id) )
        return;
    cache->atlas_tiles[tex_id] = *tile;
    cache->atlas_tile_valid[tex_id] = true;
}

const TRSPK_AtlasTile*
trspk_resource_cache_get_atlas_tile(
    const TRSPK_ResourceCache* cache,
    TRSPK_TextureId tex_id)
{
    if( !cache || !trspk_valid_texture_id(tex_id) || !cache->atlas_tile_valid[tex_id] )
        return NULL;
    return &cache->atlas_tiles[tex_id];
}

const TRSPK_AtlasTile*
trspk_resource_cache_get_all_tiles(const TRSPK_ResourceCache* cache)
{
    return cache ? cache->atlas_tiles : NULL;
}

TRSPK_LruModelCache*
trspk_resource_cache_lru_model_cache(TRSPK_ResourceCache* cache)
{
    return cache ? cache->lru_model : NULL;
}
