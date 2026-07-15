#include "../../../include/ToriRSPlatformKit/trspk_math.h"
#include "trspk_webgl1.h"

#include <stdlib.h>
#include <string.h>

#ifdef TRSPK_WEBGL1_VERBOSE_MERGE
#include <stdio.h>
#endif

TRSPK_Rect
trspk_webgl1_full_window_logical_rect(const TRSPK_WebGL1Renderer* r)
{
    int32_t w = 1;
    int32_t h = 1;
    if( r && r->window_width > 0u && r->window_height > 0u )
    {
        w = (int32_t)r->window_width;
        h = (int32_t)r->window_height;
    }
    else if( r && r->width > 0u && r->height > 0u )
    {
        w = (int32_t)r->width;
        h = (int32_t)r->height;
    }
    TRSPK_Rect rect = { 0, 0, w, h };
    if( rect.width <= 0 )
        rect.width = 1;
    if( rect.height <= 0 )
        rect.height = 1;
    return rect;
}

#ifdef __EMSCRIPTEN__
/** GL lower-left full drawable (0,0,fbW,fbH) for viewport/scissor when mapping fails. */
static TRSPK_Rect
trspk_webgl1_full_drawable_gl_rect(const TRSPK_WebGL1Renderer* r)
{
    TRSPK_Rect rect = { 0, 0, r ? (int32_t)r->width : 1, r ? (int32_t)r->height : 1 };
    if( rect.width <= 0 )
        rect.width = 1;
    if( rect.height <= 0 )
        rect.height = 1;
    return rect;
}

static TRSPK_Rect
trspk_webgl1_compute_gl_viewport_rect(
    const TRSPK_WebGL1Renderer* r,
    const TRSPK_Rect* logical)
{
    TRSPK_Rect rect = trspk_webgl1_full_drawable_gl_rect(r);
    if( !r || !logical )
        return rect;
    trspk_logical_rect_to_gl_viewport_pixels(
        &rect, r->width, r->height, r->window_width, r->window_height, logical);
    return rect;
}

static void
trspk_webgl1_apply_full_scissor(TRSPK_WebGL1Renderer* r)
{
    const TRSPK_Rect full = trspk_webgl1_full_drawable_gl_rect(r);
    glScissor(full.x, full.y, full.width, full.height);
}

static void
trspk_webgl1_apply_logical_scissor(
    TRSPK_WebGL1Renderer* r,
    const TRSPK_Rect* logical)
{
    /* gr is OpenGL lower-left; same as opengl3_clamped_gl_scissor (no second Y flip). */
    TRSPK_Rect gr = trspk_webgl1_compute_gl_viewport_rect(r, logical);
    GLint sx = (GLint)gr.x;
    GLint sy = (GLint)gr.y;
    GLint sw = (GLint)gr.width;
    GLint sh = (GLint)gr.height;

    if( sx < 0 )
    {
        sw += sx;
        sx = 0;
    }
    if( sy < 0 )
    {
        sh += sy;
        sy = 0;
    }
    if( sx + sw > (GLint)r->width )
        sw = (GLint)r->width - sx;
    if( sy + sh > (GLint)r->height )
        sh = (GLint)r->height - sy;
    if( sw <= 0 || sh <= 0 )
    {
        trspk_webgl1_apply_full_scissor(r);
        return;
    }

    glScissor(sx, sy, sw, sh);
}

static void
trspk_webgl1_apply_pass_viewport(TRSPK_WebGL1Renderer* r)
{
    if( !r )
        return;
    r->pass_gl_rect = trspk_webgl1_compute_gl_viewport_rect(r, &r->pass_logical_rect);
    glViewport(r->pass_gl_rect.x, r->pass_gl_rect.y, r->pass_gl_rect.width, r->pass_gl_rect.height);
    glEnable(GL_SCISSOR_TEST);
    trspk_webgl1_apply_logical_scissor(r, &r->pass_logical_rect);
}
#endif

static bool
trspk_webgl1_reserve_subdraws(
    TRSPK_WebGL1PassState* pass,
    uint32_t need)
{
    if( need <= pass->subdraw_cap )
        return true;
    uint32_t cap = pass->subdraw_cap ? pass->subdraw_cap : 32u;
    while( cap < need )
        cap *= 2u;
    TRSPK_WebGL1SubDraw* n =
        (TRSPK_WebGL1SubDraw*)realloc(pass->subdraws, cap * sizeof(TRSPK_WebGL1SubDraw));
    if( !n )
        return false;
    pass->subdraws = n;
    pass->subdraw_cap = cap;
    return true;
}

static bool
trspk_webgl1_reserve_chunk_indices(
    TRSPK_WebGL1PassState* pass,
    uint32_t chunk,
    uint32_t count)
{
    if( count <= pass->chunk_index_caps[chunk] )
        return true;
    uint32_t cap = pass->chunk_index_caps[chunk] ? pass->chunk_index_caps[chunk] : 4096u;
    while( cap < count )
        cap *= 2u;
    uint16_t* ni = (uint16_t*)realloc(pass->chunk_index_pools[chunk], sizeof(uint16_t) * cap);
    if( !ni )
        return false;
    pass->chunk_index_pools[chunk] = ni;
    pass->chunk_index_caps[chunk] = cap;
    return true;
}

void
trspk_webgl1_draw_begin_3d(
    TRSPK_WebGL1Renderer* r,
    const TRSPK_Rect* viewport)
{
    if( !r )
        return;
    /* Upload any mesh staged outside draw_submit (e.g. RES_MODEL_LOAD) before resetting pools. */
    trspk_webgl1_pass_flush_pending_dynamic_gpu_uploads(r);
    for( uint32_t i = 0; i < TRSPK_MAX_WEBGL1_CHUNKS; ++i )
    {
        r->pass_state.chunk_has_draws[i] = false;
        r->pass_state.chunk_index_counts[i] = 0u;
    }
    r->pass_state.subdraw_count = 0u;
#ifdef __EMSCRIPTEN__
    if( viewport )
    {
        r->pass_logical_rect = *viewport;
        if( r->pass_logical_rect.width <= 0 || r->pass_logical_rect.height <= 0 )
            r->pass_logical_rect = trspk_webgl1_full_window_logical_rect(r);
        trspk_webgl1_apply_pass_viewport(r);
    }
#else
    (void)viewport;
#endif
}

void
trspk_webgl1_draw_add_model(
    TRSPK_WebGL1Renderer* r,
    const TRSPK_ModelPose* pose,
    const uint16_t* sorted_indices,
    uint32_t index_count)
{
    if( !r || !pose || !pose->valid || !sorted_indices || index_count == 0u )
        return;
    const uint32_t chunk = pose->chunk_index;
    if( chunk >= TRSPK_MAX_WEBGL1_CHUNKS )
        return;
    TRSPK_WebGL1PassState* pass = &r->pass_state;
    if( !trspk_webgl1_reserve_subdraws(pass, pass->subdraw_count + 1u) )
        return;
    const uint32_t start = pass->chunk_index_counts[chunk];
    if( !trspk_webgl1_reserve_chunk_indices(pass, chunk, start + index_count) )
        return;
    for( uint32_t i = 0; i < index_count; ++i )
    {
        const uint32_t idx = pose->vbo_offset + (uint32_t)sorted_indices[i];
        if( idx > 0xFFFFu )
            return;
        pass->chunk_index_pools[chunk][start + i] = (uint16_t)idx;
    }
    pass->chunk_index_counts[chunk] += index_count;
    pass->chunk_has_draws[chunk] = true;
    pass->subdraws[pass->subdraw_count].vbo = (GLuint)pose->vbo;
    pass->subdraws[pass->subdraw_count].chunk = (uint8_t)chunk;
    pass->subdraws[pass->subdraw_count].pool_start = start;
    pass->subdraws[pass->subdraw_count].index_count = index_count;
    pass->subdraw_count++;
}

void
trspk_webgl1_draw_submit_3d(
    TRSPK_WebGL1Renderer* r,
    const TRSPK_Mat4* view,
    const TRSPK_Mat4* proj)
{
    if( !r || !view || !proj )
        return;
#ifdef __EMSCRIPTEN__

    trspk_webgl1_pass_flush_pending_dynamic_gpu_uploads(r);

    trspk_webgl1_apply_pass_viewport(r);
    glUseProgram(r->prog_world3d);
    if( r->world_locs.u_modelViewMatrix >= 0 )
        glUniformMatrix4fv(r->world_locs.u_modelViewMatrix, 1, GL_FALSE, view->m);
    if( r->world_locs.u_projectionMatrix >= 0 )
        glUniformMatrix4fv(r->world_locs.u_projectionMatrix, 1, GL_FALSE, proj->m);
    if( r->world_locs.u_clock >= 0 )
        glUniform1f(r->world_locs.u_clock, (float)r->frame_clock);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->atlas_texture);
    if( r->world_locs.s_atlas >= 0 )
        glUniform1i(r->world_locs.s_atlas, 0);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);

    TRSPK_WebGL1PassState* const pass = &r->pass_state;
    r->diag_frame_pass_subdraws += pass->subdraw_count;

#ifdef TRSPK_WEBGL1_VERBOSE_MERGE
    const uint32_t verbose_pass_subdraws = pass->subdraw_count;
    uint32_t verbose_draws = 0u;
    const uint32_t snap_outer = r->diag_frame_merge_outer_skips;
    const uint32_t snap_chunk = r->diag_frame_merge_break_chunk;
    const uint32_t snap_vbo = r->diag_frame_merge_break_vbo;
    const uint32_t snap_pool = r->diag_frame_merge_break_pool_gap;
    const uint32_t snap_inv = r->diag_frame_merge_break_next_invalid;
#endif

    GLuint last_vbo = 0u;
    uint32_t last_ib_chunk = TRSPK_MAX_WEBGL1_CHUNKS;

    uint32_t run_start = 0u;
    while( run_start < pass->subdraw_count )
    {
        const TRSPK_WebGL1SubDraw* first = &pass->subdraws[run_start];
        const uint32_t chunk = (uint32_t)first->chunk;
        if( chunk >= TRSPK_MAX_WEBGL1_CHUNKS || !pass->chunk_has_draws[chunk] ||
            first->index_count == 0u )
        {
            r->diag_frame_merge_outer_skips++;
            run_start++;
            continue;
        }
        const GLuint vbo_first = first->vbo;
        if( vbo_first == 0u )
        {
            r->diag_frame_merge_outer_skips++;
            run_start++;
            continue;
        }
        if( first->pool_start + first->index_count > pass->chunk_index_counts[chunk] )
        {
            r->diag_frame_merge_outer_skips++;
            run_start++;
            continue;
        }

        uint32_t run_end = run_start + 1u;
        uint32_t run_count = first->index_count;
        while( run_end < pass->subdraw_count )
        {
            const TRSPK_WebGL1SubDraw* next = &pass->subdraws[run_end];
            const uint32_t nc = (uint32_t)next->chunk;
            if( nc >= TRSPK_MAX_WEBGL1_CHUNKS || !pass->chunk_has_draws[nc] ||
                next->index_count == 0u || next->vbo == 0u )
            {
                r->diag_frame_merge_break_next_invalid++;
                break;
            }
            if( next->pool_start + next->index_count > pass->chunk_index_counts[nc] )
            {
                r->diag_frame_merge_break_next_invalid++;
                break;
            }
            if( nc != chunk )
            {
                r->diag_frame_merge_break_chunk++;
                break;
            }
            if( next->vbo != vbo_first )
            {
                r->diag_frame_merge_break_vbo++;
                break;
            }
            if( next->pool_start != first->pool_start + run_count )
            {
                r->diag_frame_merge_break_pool_gap++;
                break;
            }
            run_count += next->index_count;
            run_end++;
        }

        if( last_ib_chunk != chunk )
        {
            const uint32_t chunk_ib_bytes =
                pass->chunk_index_counts[chunk] * (uint32_t)sizeof(uint16_t);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->dynamic_ibo);
            glBufferData(
                GL_ELEMENT_ARRAY_BUFFER,
                (GLsizeiptr)chunk_ib_bytes,
                pass->chunk_index_pools[chunk],
                GL_STREAM_DRAW);
            last_ib_chunk = chunk;
        }

        if( vbo_first != last_vbo )
        {
            glBindBuffer(GL_ARRAY_BUFFER, vbo_first);
            trspk_webgl1_bind_world_attribs(r);
            last_vbo = vbo_first;
        }
        // glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->dynamic_ibo);
        {
            const size_t ibo_byte_off = (size_t)first->pool_start * sizeof(uint16_t);
            glDrawElements(
                GL_TRIANGLES, (GLsizei)run_count, GL_UNSIGNED_SHORT, (void*)ibo_byte_off);
        }
        r->diag_frame_submitted_draws++;

        run_start = run_end;
    }

#endif
    trspk_webgl1_draw_begin_3d(r, NULL);
    r->pass_state.uniform_pass_subslot++;
}

void
trspk_webgl1_draw_clear_rect(
    TRSPK_WebGL1Renderer* r,
    const TRSPK_Rect* rect)
{
    if( !r || !rect )
        return;
#ifdef __EMSCRIPTEN__
    glEnable(GL_SCISSOR_TEST);
    trspk_webgl1_apply_logical_scissor(r, rect);
    glDepthMask(GL_TRUE);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    trspk_webgl1_apply_full_scissor(r);
#endif
}
