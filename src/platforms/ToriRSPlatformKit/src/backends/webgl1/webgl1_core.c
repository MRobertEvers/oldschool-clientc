#include "trspk_webgl1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

static TRSPK_WebGL1Renderer*
trspk_webgl1_alloc(
    uint32_t width,
    uint32_t height)
{
    TRSPK_WebGL1Renderer* r = (TRSPK_WebGL1Renderer*)calloc(1u, sizeof(TRSPK_WebGL1Renderer));
    if( !r )
        return NULL;
    r->width = width;
    r->height = height;
    r->window_width = width;
    r->window_height = height;
    r->pass_logical_rect.x = 0;
    r->pass_logical_rect.y = 0;
    r->pass_logical_rect.width = (int32_t)width;
    r->pass_logical_rect.height = (int32_t)height;
    r->pass_gl_rect = r->pass_logical_rect;
    r->cache = trspk_resource_cache_create(512u, TRSPK_VERTEX_FORMAT_WEBGL1_ARRAY);
    r->batch_staging = trspk_batch16_create(65535u, 65535u, TRSPK_VERTEX_FORMAT_WEBGL1);
    if( !trspk_webgl1_dynamic_init(r) || !r->cache || !r->batch_staging )
    {
        trspk_webgl1_dynamic_shutdown(r);
        trspk_resource_cache_destroy(r->cache);
        trspk_batch16_destroy(r->batch_staging);
        free(r);
        return NULL;
    }
    return r;
}

static void
trspk_webgl1_init_current_gl(TRSPK_WebGL1Renderer* r)
{
    if( !r )
        return;
#ifdef __EMSCRIPTEN__
    glViewport(0, 0, (GLsizei)r->width, (GLsizei)r->height);
    r->prog_world3d = trspk_webgl1_create_world_program(&r->world_locs);
    glGenBuffers(1, &r->dynamic_ibo);
    trspk_webgl1_cache_init_atlas(r, TRSPK_ATLAS_DIMENSION, TRSPK_ATLAS_DIMENSION);
    r->ready = (r->prog_world3d != 0u && r->dynamic_ibo != 0u);
#else
    r->ready = false;
#endif
}

TRSPK_WebGL1Renderer*
TRSPK_WebGL1_Init(
    const char* canvas_id,
    uint32_t width,
    uint32_t height)
{
    TRSPK_WebGL1Renderer* r = trspk_webgl1_alloc(width, height);
    if( !r )
        return NULL;
#ifdef __EMSCRIPTEN__
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = false;
    attrs.depth = true;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context(canvas_id, &attrs);
    emscripten_webgl_make_context_current(ctx);
    trspk_webgl1_init_current_gl(r);
#else
    (void)canvas_id;
    r->ready = false;
#endif
    return r;
}

TRSPK_WebGL1Renderer*
TRSPK_WebGL1_InitWithCurrentContext(
    uint32_t width,
    uint32_t height)
{
    TRSPK_WebGL1Renderer* r = trspk_webgl1_alloc(width, height);
    if( !r )
        return NULL;
    trspk_webgl1_init_current_gl(r);
    return r;
}

void
TRSPK_WebGL1_Shutdown(TRSPK_WebGL1Renderer* r)
{
    if( !r )
        return;
    trspk_webgl1_pass_free_pending_dynamic_uploads(r);
    free(r->pass_state.pending_dynamic_uploads);
    r->pass_state.pending_dynamic_uploads = NULL;
    r->pass_state.pending_dynamic_upload_cap = 0u;
#ifdef __EMSCRIPTEN__
    if( r->prog_world3d )
        glDeleteProgram(r->prog_world3d);
    if( r->dynamic_ibo )
        glDeleteBuffers(1, &r->dynamic_ibo);
    if( r->atlas_texture )
        glDeleteTextures(1, &r->atlas_texture);
    for( uint32_t i = 0; i < r->chunk_count; ++i )
    {
        if( r->batch_chunk_vbos[i] )
            glDeleteBuffers(1, &r->batch_chunk_vbos[i]);
        if( r->batch_chunk_ebos[i] )
            glDeleteBuffers(1, &r->batch_chunk_ebos[i]);
    }
#endif
    for( uint32_t i = 0; i < TRSPK_MAX_WEBGL1_CHUNKS; ++i )
        free(r->pass_state.chunk_index_pools[i]);
    free(r->pass_state.subdraws);
    trspk_webgl1_dynamic_shutdown(r);
    trspk_resource_cache_destroy(r->cache);
    trspk_batch16_destroy(r->batch_staging);
    free(r);
}

void
TRSPK_WebGL1_Resize(
    TRSPK_WebGL1Renderer* r,
    uint32_t width,
    uint32_t height)
{
    if( !r )
        return;
    r->width = width;
    r->height = height;
#ifdef __EMSCRIPTEN__
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
#endif
}

void
TRSPK_WebGL1_SetWindowSize(
    TRSPK_WebGL1Renderer* r,
    uint32_t width,
    uint32_t height)
{
    if( !r )
        return;
    r->window_width = width;
    r->window_height = height;
}

void
TRSPK_WebGL1_FrameBegin(TRSPK_WebGL1Renderer* r)
{
    if( !r )
        return;
    r->pass_state.uniform_pass_subslot = 0u;
    r->diag_frame_submitted_draws = 0u;
    r->diag_frame_pass_subdraws = 0u;
    r->diag_frame_merge_break_chunk = 0u;
    r->diag_frame_merge_break_vbo = 0u;
    r->diag_frame_merge_break_pool_gap = 0u;
    r->diag_frame_merge_break_next_invalid = 0u;
    r->diag_frame_merge_outer_skips = 0u;
#ifdef __EMSCRIPTEN__
    glViewport(0, 0, (GLsizei)r->width, (GLsizei)r->height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
}

void
TRSPK_WebGL1_FrameEnd(TRSPK_WebGL1Renderer* r)
{
    (void)r;
}

TRSPK_ResourceCache*
TRSPK_WebGL1_GetCache(TRSPK_WebGL1Renderer* renderer)
{
    return renderer ? renderer->cache : NULL;
}

TRSPK_Batch16*
TRSPK_WebGL1_GetBatchStaging(TRSPK_WebGL1Renderer* renderer)
{
    return renderer ? renderer->batch_staging : NULL;
}
