#include "opengl3_internal.h"
#include "trspk_opengl3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TRSPK_OpenGL3Renderer*
trspk_opengl3_alloc(
    uint32_t width,
    uint32_t height)
{
    TRSPK_OpenGL3Renderer* r = (TRSPK_OpenGL3Renderer*)calloc(1u, sizeof(TRSPK_OpenGL3Renderer));
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
    r->cache = trspk_resource_cache_create(512u, TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS);
    r->batch_staging = trspk_batch32_create(4096u, 12288u, TRSPK_VERTEX_FORMAT_WEBGL1);
    r->pass_state.index_capacity = TRSPK_OPENGL3_DYNAMIC_INDEX_CAPACITY;
    r->pass_state.index_pool =
        (uint32_t*)calloc(r->pass_state.index_capacity, sizeof(uint32_t));
    if( !trspk_opengl3_dynamic_init(r) || !r->cache || !r->batch_staging || !r->pass_state.index_pool )
    {
        trspk_opengl3_dynamic_shutdown(r);
        trspk_resource_cache_destroy(r->cache);
        trspk_batch32_destroy(r->batch_staging);
        free(r->pass_state.index_pool);
        free(r);
        return NULL;
    }
    return r;
}

TRSPK_OpenGL3Renderer*
TRSPK_OpenGL3_InitWithCurrentContext(
    uint32_t width,
    uint32_t height)
{
    if( !trspk_opengl3_load_gl_procs() )
        return NULL;
    TRSPK_OpenGL3Renderer* r = trspk_opengl3_alloc(width, height);
    if( !r )
        return NULL;

    r->prog_world3d = trspk_opengl3_create_world_program(&r->world_locs);
    if( r->prog_world3d == 0u )
    {
        TRSPK_OpenGL3_Shutdown(r);
        return NULL;
    }
    trspk_glUseProgram((TRSPK_GLuint)r->prog_world3d);
    if( r->world_locs.s_atlas >= 0 )
        trspk_glUniform1i((TRSPK_GLint)r->world_locs.s_atlas, 0);

    TRSPK_GLuint ibo = 0u;
    trspk_glGenBuffers(1, &ibo);
    r->dynamic_ibo = (uint32_t)ibo;

    trspk_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    trspk_glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        (TRSPK_GLsizeiptr)(TRSPK_OPENGL3_DYNAMIC_INDEX_CAPACITY * sizeof(uint32_t)),
        NULL,
        GL_DYNAMIC_DRAW);
    trspk_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0u);

    TRSPK_GLuint vao_npc = 0u;
    TRSPK_GLuint vao_proj = 0u;
    trspk_glGenVertexArrays(1, &vao_npc);
    trspk_glGenVertexArrays(1, &vao_proj);
    r->vao_dynamic_npc = (uint32_t)vao_npc;
    r->vao_dynamic_projectile = (uint32_t)vao_proj;
    if( r->dynamic_npc_vbo != 0u && r->dynamic_ibo != 0u && vao_npc != 0u )
        trspk_opengl3_world_vao_setup(
            r->vao_dynamic_npc, &r->world_locs, r->dynamic_npc_vbo, r->dynamic_ibo);
    if( r->dynamic_projectile_vbo != 0u && r->dynamic_ibo != 0u && vao_proj != 0u )
        trspk_opengl3_world_vao_setup(
            r->vao_dynamic_projectile,
            &r->world_locs,
            r->dynamic_projectile_vbo,
            r->dynamic_ibo);

    trspk_glBindVertexArray(0u);

    trspk_opengl3_cache_init_atlas(r, TRSPK_ATLAS_DIMENSION, TRSPK_ATLAS_DIMENSION);

    r->ready = (r->prog_world3d != 0u && r->dynamic_ibo != 0u && r->vao_dynamic_npc != 0u &&
                r->vao_dynamic_projectile != 0u);
    if( !r->ready )
    {
        TRSPK_OpenGL3_Shutdown(r);
        return NULL;
    }
    return r;
}

void
TRSPK_OpenGL3_Shutdown(TRSPK_OpenGL3Renderer* r)
{
    if( !r )
        return;
    if( r->prog_world3d )
        trspk_glDeleteProgram((TRSPK_GLuint)r->prog_world3d);
    if( r->vao_dynamic_npc )
    {
        TRSPK_GLuint v = (TRSPK_GLuint)r->vao_dynamic_npc;
        trspk_glDeleteVertexArrays(1, &v);
    }
    if( r->vao_dynamic_projectile )
    {
        TRSPK_GLuint v = (TRSPK_GLuint)r->vao_dynamic_projectile;
        trspk_glDeleteVertexArrays(1, &v);
    }
    if( r->dynamic_ibo )
    {
        TRSPK_GLuint b = (TRSPK_GLuint)r->dynamic_ibo;
        trspk_glDeleteBuffers(1, &b);
    }
    if( r->atlas_texture )
    {
        TRSPK_GLuint t = (TRSPK_GLuint)r->atlas_texture;
        trspk_glDeleteTextures(1, &t);
    }
    free(r->pass_state.index_pool);
    free(r->pass_state.subdraws);
    trspk_opengl3_dynamic_shutdown(r);
    trspk_resource_cache_destroy(r->cache);
    trspk_batch32_destroy(r->batch_staging);
    free(r);
}

void
TRSPK_OpenGL3_Resize(
    TRSPK_OpenGL3Renderer* r,
    uint32_t width,
    uint32_t height)
{
    if( !r )
        return;
    r->width = width;
    r->height = height;
}

void
TRSPK_OpenGL3_SetWindowSize(
    TRSPK_OpenGL3Renderer* r,
    uint32_t window_width,
    uint32_t window_height)
{
    if( !r )
        return;
    r->window_width = window_width;
    r->window_height = window_height;
}

void
TRSPK_OpenGL3_FrameBegin(TRSPK_OpenGL3Renderer* r)
{
    if( !r || !r->ready )
        return;
    r->pass_state.uniform_pass_subslot = 0u;
    r->pass_state.index_upload_offset = 0u;
    r->pass_state.index_count = 0u;
    r->pass_state.subdraw_count = 0u;
    r->diag_frame_submitted_draws = 0u;
}

void
TRSPK_OpenGL3_FrameEnd(TRSPK_OpenGL3Renderer* r)
{
    (void)r;
}

TRSPK_ResourceCache*
TRSPK_OpenGL3_GetCache(TRSPK_OpenGL3Renderer* renderer)
{
    return renderer ? renderer->cache : NULL;
}

TRSPK_Batch32*
TRSPK_OpenGL3_GetBatchStaging(TRSPK_OpenGL3Renderer* renderer)
{
    return renderer ? renderer->batch_staging : NULL;
}
