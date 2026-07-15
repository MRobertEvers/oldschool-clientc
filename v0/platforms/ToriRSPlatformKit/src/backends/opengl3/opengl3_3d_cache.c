#include "opengl3_internal.h"
#include "trspk_opengl3.h"

#include "../../tools/trspk_resource_cache.h"

#include <string.h>

void
trspk_opengl3_cache_init_atlas(
    TRSPK_OpenGL3Renderer* r,
    uint32_t width,
    uint32_t height)
{
    if( !r || !r->cache )
        return;
    trspk_resource_cache_init_atlas(r->cache, width, height);
    trspk_opengl3_cache_refresh_atlas(r);
}

void
trspk_opengl3_cache_load_texture_128(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_TextureId tex_id,
    const uint8_t* rgba_128x128,
    float anim_u,
    float anim_v,
    bool opaque)
{
    if( !r || !r->cache )
        return;
    (void)trspk_resource_cache_load_texture_128(
        r->cache, tex_id, rgba_128x128, anim_u, anim_v, opaque);
}

void
trspk_opengl3_cache_refresh_atlas(TRSPK_OpenGL3Renderer* r)
{
    if( !r || !r->cache )
        return;
    const uint8_t* pixels = trspk_resource_cache_get_atlas_pixels(r->cache);
    const uint32_t width = trspk_resource_cache_get_atlas_width(r->cache);
    const uint32_t height = trspk_resource_cache_get_atlas_height(r->cache);
    if( !pixels || width == 0u || height == 0u )
        return;
    TRSPK_GLuint tex = (TRSPK_GLuint)r->atlas_texture;
    if( tex == 0u )
        trspk_glGenTextures(1, &tex);
    r->atlas_texture = (uint32_t)tex;
    trspk_glBindTexture(GL_TEXTURE_2D, tex);
    trspk_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    trspk_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    trspk_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    trspk_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    trspk_glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        (TRSPK_GLsizei)width,
        (TRSPK_GLsizei)height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels);
}

void
trspk_opengl3_cache_batch_submit(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_BatchId batch_id,
    int usage_hint)
{
    (void)usage_hint;
    if( !r || !r->batch_staging || !r->cache )
        return;
    const void* vertices = NULL;
    const void* indices = NULL;
    uint32_t vertex_bytes = 0u;
    uint32_t index_bytes = 0u;
    trspk_batch32_get_data(r->batch_staging, &vertices, &vertex_bytes, &indices, &index_bytes);
    if( !vertices || !indices || vertex_bytes == 0u || index_bytes == 0u )
        return;

    TRSPK_GLuint vbo = 0u;
    TRSPK_GLuint ebo = 0u;
    trspk_glGenBuffers(1, &vbo);
    trspk_glGenBuffers(1, &ebo);
    trspk_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    trspk_glBufferData(GL_ARRAY_BUFFER, (TRSPK_GLsizeiptr)vertex_bytes, vertices, GL_STATIC_DRAW);
    trspk_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    trspk_glBufferData(
        GL_ELEMENT_ARRAY_BUFFER, (TRSPK_GLsizeiptr)index_bytes, indices, GL_STATIC_DRAW);
    trspk_glBindBuffer(GL_ARRAY_BUFFER, 0u);
    trspk_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0u);

    TRSPK_GPUHandle hvbo = (TRSPK_GPUHandle)(uintptr_t)vbo;
    TRSPK_GPUHandle hebo = (TRSPK_GPUHandle)(uintptr_t)ebo;
    trspk_resource_cache_batch_set_resource(r->cache, batch_id, hvbo, hebo);

    TRSPK_GLuint world_vao = 0u;
    if( r->dynamic_ibo != 0u )
    {
        trspk_glGenVertexArrays(1, &world_vao);
        if( world_vao != 0u )
        {
            trspk_opengl3_world_vao_setup(
                (uint32_t)world_vao, &r->world_locs, vbo, r->dynamic_ibo);
            trspk_resource_cache_batch_set_world_vao(
                r->cache, batch_id, (TRSPK_GPUHandle)(uintptr_t)world_vao);
        }
    }

    const uint32_t count = trspk_batch32_entry_count(r->batch_staging);
    for( uint32_t i = 0; i < count; ++i )
    {
        const TRSPK_Batch32Entry* e = trspk_batch32_get_entry(r->batch_staging, i);
        if( !e )
            continue;
        const uint32_t pose_idx = trspk_resource_cache_allocate_pose_slot(
            r->cache, e->model_id, e->gpu_segment_slot, e->frame_index);
        TRSPK_ModelPose pose = { 0 };
        pose.vbo = hvbo;
        pose.ebo = hebo;
        pose.world_vao = (TRSPK_GPUHandle)(uintptr_t)world_vao;
        pose.vbo_offset = e->vbo_offset;
        pose.ebo_offset = e->ebo_offset;
        pose.element_count = e->element_count;
        pose.batch_id = batch_id;
        pose.chunk_index = 0u;
        pose.valid = true;
        trspk_resource_cache_set_model_pose(r->cache, e->model_id, pose_idx, &pose);
    }
}

void
trspk_opengl3_cache_batch_clear(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_BatchId batch_id)
{
    if( !r || !r->cache )
        return;
    trspk_resource_cache_invalidate_poses_for_batch(r->cache, batch_id);
    TRSPK_BatchResource old = trspk_resource_cache_batch_clear(r->cache, batch_id);
    if( old.world_vao != 0u )
    {
        TRSPK_GLuint wv = (TRSPK_GLuint)(uintptr_t)old.world_vao;
        trspk_glDeleteVertexArrays(1, &wv);
    }
    for( uint32_t i = 0; i < old.chunk_count; ++i )
    {
        TRSPK_GLuint v = (TRSPK_GLuint)(uintptr_t)old.chunk_vbos[i];
        TRSPK_GLuint e = (TRSPK_GLuint)(uintptr_t)old.chunk_ebos[i];
        if( v )
            trspk_glDeleteBuffers(1, &v);
        if( e && e != v )
            trspk_glDeleteBuffers(1, &e);
    }
}

void
trspk_opengl3_cache_unload_model(
    TRSPK_OpenGL3Renderer* r,
    TRSPK_ModelId model_id)
{
    if( r && r->cache )
        trspk_resource_cache_clear_model(r->cache, model_id);
}
