#include "trspk_lru_model_cache.h"

#include "trspk_resource_cache.h"
#include "trspk_vertex_buffer.h"

#include <stdlib.h>
#include <string.h>

static uint64_t
trspk_lru_key(
    TRSPK_ModelId model_id,
    uint8_t seg,
    uint16_t frame)
{
    return (uint64_t)model_id | ((uint64_t)seg << 16) | ((uint64_t)frame << 24);
}

typedef struct TRSPK_LruEntry
{
    uint64_t key;
    bool in_use;
    uint64_t touch;
    TRSPK_VertexBuffer mesh;
} TRSPK_LruEntry;

struct TRSPK_LruModelCache
{
    uint32_t capacity;
    uint32_t used;
    uint64_t clock;
    TRSPK_VertexFormat vertex_format;
    TRSPK_LruEntry* entries;
};

TRSPK_LruModelCache*
trspk_lru_model_cache_create(
    uint32_t capacity,
    TRSPK_VertexFormat vertex_format)
{
    if( capacity == 0u || vertex_format == TRSPK_VERTEX_FORMAT_NONE )
        return NULL;
    TRSPK_LruModelCache* c = (TRSPK_LruModelCache*)calloc(1u, sizeof(TRSPK_LruModelCache));
    if( !c )
        return NULL;
    c->capacity = capacity;
    c->vertex_format = vertex_format;
    c->entries = (TRSPK_LruEntry*)calloc(capacity, sizeof(TRSPK_LruEntry));
    if( !c->entries )
    {
        free(c);
        return NULL;
    }
    return c;
}

TRSPK_VertexFormat
trspk_lru_model_cache_vertex_format(const TRSPK_LruModelCache* cache)
{
    return cache ? cache->vertex_format : TRSPK_VERTEX_FORMAT_NONE;
}

void
trspk_lru_model_cache_destroy(TRSPK_LruModelCache* cache)
{
    if( !cache )
        return;
    trspk_lru_model_cache_reset(cache);
    free(cache->entries);
    free(cache);
}

void
trspk_lru_model_cache_reset(TRSPK_LruModelCache* cache)
{
    if( !cache || !cache->entries )
        return;
    for( uint32_t i = 0; i < cache->capacity; ++i )
    {
        if( cache->entries[i].in_use )
            trspk_vertex_buffer_free(&cache->entries[i].mesh);
    }
    memset(cache->entries, 0, (size_t)cache->capacity * sizeof(TRSPK_LruEntry));
    cache->used = 0u;
    cache->clock = 0u;
}

void
trspk_lru_model_cache_evict_model_id(
    TRSPK_LruModelCache* cache,
    TRSPK_ModelId model_id)
{
    if( !cache || !cache->entries )
        return;
    for( uint32_t i = 0; i < cache->capacity; ++i )
    {
        if( !cache->entries[i].in_use )
            continue;
        if( (cache->entries[i].key & 0xFFFFu) != (uint64_t)model_id )
            continue;
        trspk_vertex_buffer_free(&cache->entries[i].mesh);
        cache->entries[i].in_use = false;
        cache->entries[i].key = 0u;
        if( cache->used > 0u )
            cache->used -= 1u;
    }
}

const TRSPK_VertexBuffer*
trspk_lru_model_cache_get(
    TRSPK_LruModelCache* cache,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index)
{
    if( !cache || !cache->entries || cache->vertex_format == TRSPK_VERTEX_FORMAT_NONE )
        return NULL;
    const uint64_t want = trspk_lru_key(model_id, gpu_segment_slot, frame_index);
    for( uint32_t i = 0; i < cache->capacity; ++i )
    {
        if( !cache->entries[i].in_use || cache->entries[i].key != want )
            continue;
        cache->entries[i].touch = ++cache->clock;
        return &cache->entries[i].mesh;
    }
    return NULL;
}

static const TRSPK_VertexBuffer*
trspk_lru_model_cache_get_or_emplace_impl(
    TRSPK_LruModelCache* cache,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    const TRSPK_ModelArrays* arrays,
    bool textured,
    TRSPK_UVCalculationMode uv_calc_mode,
    const TRSPK_ResourceCache* atlas_tile_meta,
    const TRSPK_BakeTransform* bake)
{
    if( !cache || !arrays || arrays->face_count == 0u || !cache->entries ||
        cache->vertex_format == TRSPK_VERTEX_FORMAT_NONE )
        return NULL;
    const TRSPK_VertexFormat format = cache->vertex_format;
    const uint64_t want = trspk_lru_key(model_id, gpu_segment_slot, frame_index);
    uint32_t found = cache->capacity;
    uint32_t min_i = 0u;
    uint64_t min_touch = 0u;
    bool have_min = false;
    for( uint32_t i = 0; i < cache->capacity; ++i )
    {
        if( !cache->entries[i].in_use )
            continue;
        if( cache->entries[i].key == want )
        {
            found = i;
            break;
        }
        if( !have_min || cache->entries[i].touch < min_touch )
        {
            have_min = true;
            min_touch = cache->entries[i].touch;
            min_i = i;
        }
    }
    if( found < cache->capacity )
    {
        cache->entries[found].touch = ++cache->clock;
        return &cache->entries[found].mesh;
    }

    const uint32_t corner_count = arrays->face_count * 3u;
    TRSPK_VertexBuffer built = { 0 };
    if( !trspk_vertex_buffer_allocate_mesh(&built, corner_count, corner_count, format) )
        return NULL;

    bool build_ok;
    if( textured )
        build_ok = trspk_vertex_buffer_write_textured(
            arrays->vertex_count,
            arrays->face_count,
            arrays->vertices_x,
            arrays->vertices_y,
            arrays->vertices_z,
            arrays->faces_a,
            arrays->faces_b,
            arrays->faces_c,
            arrays->faces_a_color_hsl16,
            arrays->faces_b_color_hsl16,
            arrays->faces_c_color_hsl16,
            arrays->faces_textures,
            arrays->textured_faces,
            arrays->textured_faces_a,
            arrays->textured_faces_b,
            arrays->textured_faces_c,
            arrays->face_alphas,
            arrays->face_infos,
            uv_calc_mode,
            atlas_tile_meta,
            bake,
            &built);
    else
        build_ok = trspk_vertex_buffer_write(
            arrays->vertex_count,
            arrays->face_count,
            arrays->vertices_x,
            arrays->vertices_y,
            arrays->vertices_z,
            arrays->faces_a,
            arrays->faces_b,
            arrays->faces_c,
            arrays->faces_a_color_hsl16,
            arrays->faces_b_color_hsl16,
            arrays->faces_c_color_hsl16,
            arrays->face_alphas,
            arrays->face_infos,
            bake,
            &built);
    if( !build_ok )
    {
        trspk_vertex_buffer_free(&built);
        return NULL;
    }
    const bool has_indices = (built.index_format == TRSPK_INDEX_FORMAT_U32)
                                 ? (built.indices.as_u32 != NULL)
                                 : (built.indices.as_u16 != NULL);
    if( !has_indices || !trspk_vertex_buffer_has_vertex_payload(&built) )
    {
        trspk_vertex_buffer_free(&built);
        return NULL;
    }

    uint32_t slot;
    bool from_empty = false;
    if( cache->used < cache->capacity )
    {
        slot = cache->capacity;
        for( uint32_t i = 0; i < cache->capacity; ++i )
        {
            if( !cache->entries[i].in_use )
            {
                from_empty = true;
                slot = i;
                break;
            }
        }
        if( slot == cache->capacity )
        {
            trspk_vertex_buffer_free(&built);
            return NULL;
        }
    }
    else
    {
        if( !have_min )
        {
            trspk_vertex_buffer_free(&built);
            return NULL;
        }
        trspk_vertex_buffer_free(&cache->entries[min_i].mesh);
        cache->entries[min_i].in_use = false;
        slot = min_i;
    }

    if( from_empty )
        cache->used += 1u;
    TRSPK_LruEntry* e = &cache->entries[slot];
    e->in_use = true;
    e->key = want;
    e->touch = ++cache->clock;
    e->mesh = built;
    return &e->mesh;
}

const TRSPK_VertexBuffer*
trspk_lru_model_cache_get_or_emplace_untextured(
    TRSPK_LruModelCache* cache,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    const TRSPK_ModelArrays* arrays,
    const TRSPK_BakeTransform* bake)
{
    return trspk_lru_model_cache_get_or_emplace_impl(
        cache,
        model_id,
        gpu_segment_slot,
        frame_index,
        arrays,
        false,
        TRSPK_UV_CALC_TEXTURED_FACE_ARRAY,
        NULL,
        bake);
}

const TRSPK_VertexBuffer*
trspk_lru_model_cache_get_or_emplace_textured(
    TRSPK_LruModelCache* cache,
    TRSPK_ModelId model_id,
    uint8_t gpu_segment_slot,
    uint16_t frame_index,
    const TRSPK_ModelArrays* arrays,
    TRSPK_UVCalculationMode uv_calc_mode,
    const TRSPK_ResourceCache* atlas_tile_meta,
    const TRSPK_BakeTransform* bake)
{
    return trspk_lru_model_cache_get_or_emplace_impl(
        cache,
        model_id,
        gpu_segment_slot,
        frame_index,
        arrays,
        true,
        uv_calc_mode,
        atlas_tile_meta,
        bake);
}
