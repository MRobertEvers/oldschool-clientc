#include "trspk_webgl1.h"

#include "../../tools/trspk_resource_cache.h"

#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#endif

void
trspk_webgl1_cache_init_atlas(
    TRSPK_WebGL1Renderer* r,
    uint32_t width,
    uint32_t height)
{
    if( !r || !r->cache )
        return;
    trspk_resource_cache_init_atlas(r->cache, width, height);
    r->tiles_dirty = true;
    trspk_webgl1_cache_refresh_atlas(r);
}

void
trspk_webgl1_cache_load_texture_128(
    TRSPK_WebGL1Renderer* r,
    TRSPK_TextureId id,
    const uint8_t* rgba_128x128,
    float anim_u,
    float anim_v,
    bool opaque)
{
    if( !r || !r->cache )
        return;
    if( trspk_resource_cache_load_texture_128(r->cache, id, rgba_128x128, anim_u, anim_v, opaque) )
        r->tiles_dirty = true;
}

void
trspk_webgl1_cache_refresh_atlas(TRSPK_WebGL1Renderer* r)
{
    if( !r || !r->cache )
        return;
#ifdef __EMSCRIPTEN__
    const uint8_t* pixels = trspk_resource_cache_get_atlas_pixels(r->cache);
    const uint32_t width = trspk_resource_cache_get_atlas_width(r->cache);
    const uint32_t height = trspk_resource_cache_get_atlas_height(r->cache);
    if( !pixels || width == 0u || height == 0u )
        return;
    if( r->atlas_texture == 0u )
        glGenTextures(1, &r->atlas_texture);
    glBindTexture(GL_TEXTURE_2D, r->atlas_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        (GLsizei)width,
        (GLsizei)height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels);
#endif
}

bool
trspk_webgl1_cache_tiles_dirty(TRSPK_WebGL1Renderer* r)
{
    return r ? r->tiles_dirty : false;
}

void
trspk_webgl1_cache_clear_tiles_dirty(TRSPK_WebGL1Renderer* r)
{
    if( r )
        r->tiles_dirty = false;
}

void
trspk_webgl1_cache_batch_submit(
    TRSPK_WebGL1Renderer* r,
    TRSPK_BatchId batch_id,
    int usage_hint)
{
    (void)usage_hint;
    if( !r || !r->batch_staging || !r->cache )
        return;
    const uint32_t chunk_count = trspk_batch16_chunk_count(r->batch_staging);
    if( chunk_count > TRSPK_MAX_WEBGL1_CHUNKS )
        return;
#ifdef __EMSCRIPTEN__
    for( uint32_t i = 0; i < r->chunk_count; ++i )
    {
        if( r->batch_chunk_vbos[i] )
            glDeleteBuffers(1, &r->batch_chunk_vbos[i]);
        if( r->batch_chunk_ebos[i] )
            glDeleteBuffers(1, &r->batch_chunk_ebos[i]);
    }
    glGenBuffers((GLsizei)chunk_count, r->batch_chunk_vbos);
    glGenBuffers((GLsizei)chunk_count, r->batch_chunk_ebos);
    for( uint32_t c = 0; c < chunk_count; ++c )
    {
        const void* vertices = NULL;
        const void* indices = NULL;
        uint32_t vertex_bytes = 0u;
        uint32_t index_bytes = 0u;
        trspk_batch16_get_chunk(
            r->batch_staging, c, &vertices, &vertex_bytes, &indices, &index_bytes);
        glBindBuffer(GL_ARRAY_BUFFER, r->batch_chunk_vbos[c]);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertex_bytes, vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->batch_chunk_ebos[c]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)index_bytes, indices, GL_STATIC_DRAW);
    }
#endif
    r->chunk_count = chunk_count;
    TRSPK_GPUHandle hvbos[TRSPK_MAX_WEBGL1_CHUNKS] = { 0 };
    TRSPK_GPUHandle hebos[TRSPK_MAX_WEBGL1_CHUNKS] = { 0 };
    for( uint32_t c = 0; c < chunk_count; ++c )
    {
        hvbos[c] = (TRSPK_GPUHandle)r->batch_chunk_vbos[c];
        hebos[c] = (TRSPK_GPUHandle)r->batch_chunk_ebos[c];
    }
    trspk_resource_cache_batch_set_chunks(r->cache, batch_id, hvbos, hebos, chunk_count);
    const uint32_t entry_count = trspk_batch16_entry_count(r->batch_staging);
    for( uint32_t i = 0; i < entry_count; ++i )
    {
        const TRSPK_Batch16Entry* e = trspk_batch16_get_entry(r->batch_staging, i);
        if( !e )
            continue;
        const uint32_t pose_idx = trspk_resource_cache_allocate_pose_slot(
            r->cache, e->model_id, e->gpu_segment_slot, e->frame_index);
        TRSPK_ModelPose pose = { 0 };
        pose.vbo = hvbos[e->chunk_index];
        pose.ebo = hebos[e->chunk_index];
        pose.vbo_offset = e->vbo_offset;
        pose.ebo_offset = e->ebo_offset;
        pose.element_count = e->element_count;
        pose.batch_id = batch_id;
        pose.chunk_index = e->chunk_index;
        pose.valid = true;
        trspk_resource_cache_set_model_pose(r->cache, e->model_id, pose_idx, &pose);
    }
}

void
trspk_webgl1_cache_batch_clear(
    TRSPK_WebGL1Renderer* r,
    TRSPK_BatchId batch_id)
{
    if( !r || !r->cache )
        return;
    trspk_resource_cache_invalidate_poses_for_batch(r->cache, batch_id);
    TRSPK_BatchResource old = trspk_resource_cache_batch_clear(r->cache, batch_id);
#ifdef __EMSCRIPTEN__
    for( uint32_t i = 0; i < old.chunk_count; ++i )
    {
        GLuint v = (GLuint)old.chunk_vbos[i];
        GLuint e = (GLuint)old.chunk_ebos[i];
        if( v )
            glDeleteBuffers(1, &v);
        if( e )
            glDeleteBuffers(1, &e);
    }
#endif
}

void
trspk_webgl1_cache_unload_model(
    TRSPK_WebGL1Renderer* r,
    TRSPK_ModelId id)
{
    if( r && r->cache )
        trspk_resource_cache_clear_model(r->cache, id);
}
