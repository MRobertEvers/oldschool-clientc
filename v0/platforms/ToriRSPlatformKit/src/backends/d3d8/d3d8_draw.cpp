#include "../../../include/ToriRSPlatformKit/trspk_math.h"
#include "d3d8_vertex.h"
#include "osrs/game.h"
#include "trspk_d3d8.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d8.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(_WIN32)
#include <d3d8types.h>

static void
trspk_d3d8_colmajor_to_d3dmatrix(const float* cm, D3DMATRIX* m)
{
    /* Column-vector math (v' = M * v), same as Metal / GL; D3D8 fixed-function uses row-vector
     * (v' = v * M), so transpose when loading (legacy platform_impl2_win32_renderer_d3d8). */
    m->_11 = cm[0];
    m->_12 = cm[1];
    m->_13 = cm[2];
    m->_14 = cm[3];
    m->_21 = cm[4];
    m->_22 = cm[5];
    m->_23 = cm[6];
    m->_24 = cm[7];
    m->_31 = cm[8];
    m->_32 = cm[9];
    m->_33 = cm[10];
    m->_34 = cm[11];
    m->_41 = cm[12];
    m->_42 = cm[13];
    m->_43 = cm[14];
    m->_44 = cm[15];
}
#endif

#ifdef TRSPK_WEBGL1_VERBOSE_MERGE
#include <stdio.h>
#endif

extern "C" {

static void
trspk_d3d8_mat4_mul_colmajor(const float* a, const float* b, float* out)
{
    float r[16];
    for( int col = 0; col < 4; ++col )
    {
        for( int row = 0; row < 4; ++row )
        {
            r[col * 4 + row] = a[0 * 4 + row] * b[col * 4 + 0] + a[1 * 4 + row] * b[col * 4 + 1] +
                               a[2 * 4 + row] * b[col * 4 + 2] + a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

void
trspk_d3d8_remap_projection_opengl_to_d3d8_z(float* proj_colmajor)
{
    if( !proj_colmajor )
        return;
    static const float k_clip_z[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
    };
    float tmp[16];
    trspk_d3d8_mat4_mul_colmajor(k_clip_z, proj_colmajor, tmp);
    memcpy(proj_colmajor, tmp, sizeof(tmp));
}

TRSPK_Rect
trspk_d3d8_full_window_logical_rect(const TRSPK_D3D8Renderer* r)
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

#if defined(_WIN32)
static TRSPK_Rect
trspk_d3d8_full_drawable_gl_rect(const TRSPK_D3D8Renderer* r)
{
    TRSPK_Rect rect = { 0, 0, r ? (int32_t)r->width : 1, r ? (int32_t)r->height : 1 };
    if( rect.width <= 0 )
        rect.width = 1;
    if( rect.height <= 0 )
        rect.height = 1;
    return rect;
}

static TRSPK_Rect
trspk_d3d8_compute_gl_viewport_rect(
    const TRSPK_D3D8Renderer* r,
    const TRSPK_Rect* logical)
{
    TRSPK_Rect rect = trspk_d3d8_full_drawable_gl_rect(r);
    if( !r || !logical )
        return rect;
    /* Offscreen scene RT: logical rects are in scene pixels (legacy frame_vp_*). Use win==fb
     * so sx=sy=1; r->window_width is only for letterbox in TRSPK_D3D8_Present, not viewport scale. */
    const uint32_t fbw = r->width > 0u ? r->width : 1u;
    const uint32_t fbh = r->height > 0u ? r->height : 1u;
    trspk_logical_rect_to_gl_viewport_pixels(&rect, fbw, fbh, fbw, fbh, logical);
    return rect;
}

/**
 * `gl_rect` is from trspk_logical_rect_to_gl_viewport_pixels: **OpenGL viewport convention**
 * (origin lower-left, Y increases upward). D3D8 D3DVIEWPORT8 uses **top-left** origin (Y down).
 * Mapping: vp.Y = fb_h - gl_rect.y - gl_rect.height (same as legacy CLEAR_RECT when logical is
 * top-left: trspk_math converts to GL lower-left first; this step is the single required flip).
 */
static void
trspk_d3d8_fill_viewport8_from_gl_lower_left_rect(
    D3DVIEWPORT8* vp,
    int32_t framebuffer_h,
    const TRSPK_Rect* gl_rect)
{
    if( !vp || !gl_rect )
        return;
    const int32_t fh = framebuffer_h > 0 ? framebuffer_h : 1;
    const DWORD vx = (DWORD)(gl_rect->x >= 0 ? gl_rect->x : 0);
    const DWORD vw = (DWORD)(gl_rect->width > 0 ? gl_rect->width : 1);
    const DWORD vh = (DWORD)(gl_rect->height > 0 ? gl_rect->height : 1);
    const DWORD vy = (DWORD)((int32_t)fh - gl_rect->y - (int32_t)vh);
    ZeroMemory(vp, sizeof(*vp));
    vp->X = vx;
    vp->Y = vy;
    vp->Width = vw;
    vp->Height = vh;
    vp->MinZ = 0.0f;
    vp->MaxZ = 1.0f;
}

static void
trspk_d3d8_apply_pass_viewport(TRSPK_D3D8Renderer* r)
{
    if( !r || !r->com_device )
        return;
    r->pass_gl_rect = trspk_d3d8_compute_gl_viewport_rect(r, &r->pass_logical_rect);
    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
    const int32_t fh = (int32_t)(r->height > 0u ? r->height : 1u);
    D3DVIEWPORT8 vp;
    trspk_d3d8_fill_viewport8_from_gl_lower_left_rect(&vp, fh, &r->pass_gl_rect);
    dev->SetViewport(&vp);
}
#endif

static bool
trspk_d3d8_reserve_subdraws(
    TRSPK_D3D8PassState* pass,
    uint32_t need)
{
    if( need <= pass->subdraw_cap )
        return true;
    uint32_t cap = pass->subdraw_cap ? pass->subdraw_cap : 32u;
    while( cap < need )
        cap *= 2u;
    TRSPK_D3D8SubDraw* n =
        (TRSPK_D3D8SubDraw*)realloc(pass->subdraws, cap * sizeof(TRSPK_D3D8SubDraw));
    if( !n )
        return false;
    pass->subdraws = n;
    pass->subdraw_cap = cap;
    return true;
}

static bool
trspk_d3d8_reserve_chunk_indices(
    TRSPK_D3D8PassState* pass,
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
trspk_d3d8_draw_begin_3d(TRSPK_D3D8Renderer* r, const TRSPK_Rect* viewport)
{
    if( !r )
        return;
    trspk_d3d8_pass_flush_pending_dynamic_gpu_uploads(r);
    for( uint32_t i = 0; i < TRSPK_MAX_WEBGL1_CHUNKS; ++i )
    {
        r->pass_state.chunk_has_draws[i] = false;
        r->pass_state.chunk_index_counts[i] = 0u;
    }
    r->pass_state.subdraw_count = 0u;
#if defined(_WIN32)
    if( viewport )
    {
        r->pass_logical_rect = *viewport;
        if( r->pass_logical_rect.width <= 0 || r->pass_logical_rect.height <= 0 )
            r->pass_logical_rect = trspk_d3d8_full_window_logical_rect(r);
        trspk_d3d8_apply_pass_viewport(r);
    }
#else
    (void)viewport;
#endif
}

void
trspk_d3d8_draw_add_model(
    TRSPK_D3D8Renderer* r,
    const TRSPK_ModelPose* pose,
    const uint16_t* sorted_indices,
    uint32_t index_count)
{
    if( !r || !pose || !pose->valid || !sorted_indices || index_count == 0u )
        return;
    const uint32_t chunk = pose->chunk_index;
    if( chunk >= TRSPK_MAX_WEBGL1_CHUNKS )
        return;
    TRSPK_D3D8PassState* pass = &r->pass_state;
    if( !trspk_d3d8_reserve_subdraws(pass, pass->subdraw_count + 1u) )
        return;
    const uint32_t start = pass->chunk_index_counts[chunk];
    if( !trspk_d3d8_reserve_chunk_indices(pass, chunk, start + index_count) )
        return;
    for( uint32_t i = 0; i < index_count; ++i )
    {
        const uint32_t local = (uint32_t)sorted_indices[i];
        if( local > 0xFFFFu )
            return;
        /* Per-model-local indices; IDirect3DDevice8::SetIndices BaseVertexIndex = vbo_offset
         * (legacy 9c62ec2d platform_impl2_win32_renderer_d3d8 MODEL_DRAW). */
        pass->chunk_index_pools[chunk][start + i] = (uint16_t)local;
    }
    pass->chunk_index_counts[chunk] += index_count;
    pass->chunk_has_draws[chunk] = true;
    pass->subdraws[pass->subdraw_count].vbo = (GLuint)pose->vbo;
    pass->subdraws[pass->subdraw_count].vbo_offset = pose->vbo_offset;
    pass->subdraws[pass->subdraw_count].chunk = (uint8_t)chunk;
    pass->subdraws[pass->subdraw_count].pool_start = start;
    pass->subdraws[pass->subdraw_count].index_count = index_count;
    pass->subdraw_count++;
}

void
trspk_d3d8_draw_submit_3d_ex(
    TRSPK_D3D8Renderer* r,
    const TRSPK_Mat4* view,
    const TRSPK_Mat4* proj,
    bool reset_pass_after)
{
    /* Win32 path: single atlas + MODULATE (see trspk_d3d8_fvf_bake). Legacy d3d8_old binds one
     * IDirect3DTexture8 per raw tex id, toggles COLOROP (MODULATE vs SELECTARG2) and optional
     * D3DTS_TEXTURE0 scroll per run. TRSPK bakes atlas UVs + untextured sentinel at atlas origin
     * (trspk_d3d8_cache_refresh_atlas) so MODULATE(TEX0, DIFFUSE) matches diffuse-only for
     * untextured faces. Legacy toggled ALPHATEST per non-opaque texture; atlas path keeps
     * ALPHATEST off and relies on texture alpha + SRCALPHA blending (same default state stack). */
    if( !r || !view || !proj )
        return;
#if defined(_WIN32)
    if( !r->com_device )
    {
        if( reset_pass_after )
        {
            trspk_d3d8_draw_begin_3d(r, NULL);
            r->pass_state.uniform_pass_subslot++;
        }
        return;
    }
    trspk_d3d8_pass_flush_pending_dynamic_gpu_uploads(r);
    trspk_d3d8_apply_pass_viewport(r);

    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);

    static const DWORD kFvfWorld = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    dev->SetVertexShader(kFvfWorld);
    dev->SetPixelShader(0u);

    D3DMATRIX d3d_view;
    D3DMATRIX d3d_proj;
    D3DMATRIX d3d_world;
    trspk_d3d8_colmajor_to_d3dmatrix(view->m, &d3d_view);
    trspk_d3d8_colmajor_to_d3dmatrix(proj->m, &d3d_proj);
    ZeroMemory(&d3d_world, sizeof(d3d_world));
    d3d_world._11 = d3d_world._22 = d3d_world._33 = d3d_world._44 = 1.0f;
    dev->SetTransform(D3DTS_WORLD, &d3d_world);
    dev->SetTransform(D3DTS_VIEW, &d3d_view);
    dev->SetTransform(D3DTS_PROJECTION, &d3d_proj);

    D3DMATRIX idtex;
    ZeroMemory(&idtex, sizeof(idtex));
    idtex._11 = idtex._22 = idtex._33 = idtex._44 = 1.0f;
    dev->SetTransform(D3DTS_TEXTURE0, &idtex);
    dev->SetTextureStageState(0u, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

    IDirect3DTexture8* atlas =
        r->atlas_texture ? reinterpret_cast<IDirect3DTexture8*>((uintptr_t)r->atlas_texture) :
                           nullptr;
    dev->SetTexture(0u, atlas);
    dev->SetTexture(1u, nullptr);
    dev->SetTextureStageState(1u, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1u, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    /* Match legacy 9c62ec2d d3d8_apply_default_render_state + d3d8_ensure_pass(PASS_3D). */
    dev->SetTextureStageState(0u, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0u, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0u, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0u, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0u, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0u, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0u, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0u, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0u, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetTextureStageState(0u, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHAREF, 128u);
    dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);

    const UINT stride = (UINT)sizeof(TRSPK_VertexD3D8);

    TRSPK_D3D8PassState* const pass = &r->pass_state;
    r->diag_frame_pass_subdraws += pass->subdraw_count;

    GLuint last_vbo = 0u;
    uint32_t last_ib_chunk = TRSPK_MAX_WEBGL1_CHUNKS;

    uint32_t run_start = 0u;
    while( run_start < pass->subdraw_count )
    {
        const TRSPK_D3D8SubDraw* first = &pass->subdraws[run_start];
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
            const TRSPK_D3D8SubDraw* next = &pass->subdraws[run_end];
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
            if( next->vbo_offset != first->vbo_offset )
            {
                r->diag_frame_merge_break_vbo++;
                break;
            }
            run_count += next->index_count;
            run_end++;
        }

        if( last_ib_chunk != chunk )
        {
            const uint32_t chunk_ib_bytes =
                pass->chunk_index_counts[chunk] * (uint32_t)sizeof(uint16_t);
            auto* ib =
                reinterpret_cast<IDirect3DIndexBuffer8*>((uintptr_t)r->dynamic_ibo);
            BYTE* dst = nullptr;
            /* D3DLOCK_DISCARD: discard entire dynamic buffer (65536 indices); copy used prefix. */
            const UINT ib_full_bytes = (UINT)(65536u * sizeof(uint16_t));
            if( SUCCEEDED(ib->Lock(0u, ib_full_bytes, &dst, D3DLOCK_DISCARD)) && dst )
            {
                memcpy(dst, pass->chunk_index_pools[chunk], (size_t)chunk_ib_bytes);
                ib->Unlock();
            }
            last_ib_chunk = chunk;
        }

        if( vbo_first != last_vbo )
        {
            auto* vb =
                reinterpret_cast<IDirect3DVertexBuffer8*>((uintptr_t)vbo_first);
            dev->SetStreamSource(0u, vb, stride);
            last_vbo = vbo_first;
        }

        auto* ib =
            reinterpret_cast<IDirect3DIndexBuffer8*>((uintptr_t)r->dynamic_ibo);
        /* Legacy MODEL_DRAW: SetIndices(ib_ring, vertex_index_base), model-local indices. */
        dev->SetIndices(ib, (UINT)first->vbo_offset);

        uint16_t max_local = 0u;
        const uint16_t* pool = pass->chunk_index_pools[chunk];
        for( uint32_t i = 0u; i < run_count; ++i )
        {
            const uint16_t ix = pool[first->pool_start + i];
            if( ix > max_local )
                max_local = ix;
        }
        const UINT num_vertices = (UINT)max_local + 1u;

        dev->DrawIndexedPrimitive(
            D3DPT_TRIANGLELIST,
            0u,
            num_vertices,
            (UINT)first->pool_start,
            (UINT)(run_count / 3u));
        r->diag_frame_submitted_draws++;

        run_start = run_end;
    }

#endif

    if( reset_pass_after )
    {
        trspk_d3d8_draw_begin_3d(r, NULL);
        r->pass_state.uniform_pass_subslot++;
    }
}

void
trspk_d3d8_draw_submit_3d(
    TRSPK_D3D8Renderer* r,
    const TRSPK_Mat4* view,
    const TRSPK_Mat4* proj)
{
    trspk_d3d8_draw_submit_3d_ex(r, view, proj, true);
}

void
trspk_d3d8_draw_clear_rect(TRSPK_D3D8Renderer* r, const TRSPK_Rect* rect)
{
    if( !r || !rect || !r->com_device )
        return;
#if defined(_WIN32)
    if( rect->width <= 0 || rect->height <= 0 )
        return;
    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
    TRSPK_Rect gl;
    const uint32_t fbw = r->width > 0u ? r->width : 1u;
    const uint32_t fbh = r->height > 0u ? r->height : 1u;
    trspk_logical_rect_to_gl_viewport_pixels(&gl, fbw, fbh, fbw, fbh, rect);
    if( gl.width <= 0 || gl.height <= 0 )
        return;
    const int32_t fh = (int32_t)(r->height > 0u ? r->height : 1u);

    D3DVIEWPORT8 saved;
    ZeroMemory(&saved, sizeof(saved));
    dev->GetViewport(&saved);
    D3DVIEWPORT8 cvp;
    trspk_d3d8_fill_viewport8_from_gl_lower_left_rect(&cvp, fh, &gl);
    dev->SetViewport(&cvp);
    dev->Clear(0u, NULL, D3DCLEAR_TARGET, 0x00000000u, 1.0f, 0u);
    dev->SetViewport(&saved);
#endif
}

void
trspk_d3d8_fill_view_proj_for_pass(
    TRSPK_D3D8Renderer* r,
    struct GGame* game,
    TRSPK_Mat4* out_view,
    TRSPK_Mat4* out_proj)
{
    if( !r || !game || !out_view || !out_proj )
        return;
    trspk_d3d8_compute_view_matrix(
        out_view->m,
        0.0f,
        0.0f,
        0.0f,
        trspk_dash_yaw_to_radians(game->camera_pitch),
        trspk_dash_yaw_to_radians(game->camera_yaw));
    /* Legacy d3d8_old uses game->view_port for projection size each frame; pass_logical_rect
     * only refines the viewport rect (BEGIN_3D), not the lens aspect when view_port is valid. */
    float projection_width;
    float projection_height;
    if( game->view_port && game->view_port->width > 0 && game->view_port->height > 0 )
    {
        projection_width = (float)game->view_port->width;
        projection_height = (float)game->view_port->height;
    }
    else if( r->pass_logical_rect.width > 0 && r->pass_logical_rect.height > 0 )
    {
        projection_width = (float)r->pass_logical_rect.width;
        projection_height = (float)r->pass_logical_rect.height;
    }
    else
    {
        projection_width = (float)r->width;
        projection_height = (float)r->height;
    }
    trspk_d3d8_compute_projection_matrix(
        out_proj->m, (90.0f * TRSPK_PI) / 180.0f, projection_width, projection_height);
    trspk_d3d8_remap_projection_opengl_to_d3d8_z(out_proj->m);
}

#if defined(_WIN32)
void
trspk_d3d8_frame_begin_set_transforms(void* device, struct GGame* game, int proj_w, int proj_h)
{
    if( !device || !game )
        return;
    auto* dev = reinterpret_cast<IDirect3DDevice8*>(device);
    TRSPK_Mat4 view, proj;
    const float pw = proj_w > 0 ? (float)proj_w : 1.0f;
    const float ph = proj_h > 0 ? (float)proj_h : 1.0f;
    trspk_d3d8_compute_view_matrix(
        view.m,
        0.0f,
        0.0f,
        0.0f,
        trspk_dash_yaw_to_radians(game->camera_pitch),
        trspk_dash_yaw_to_radians(game->camera_yaw));
    trspk_d3d8_compute_projection_matrix(proj.m, (90.0f * TRSPK_PI) / 180.0f, pw, ph);
    trspk_d3d8_remap_projection_opengl_to_d3d8_z(proj.m);
    D3DMATRIX d3d_view, d3d_proj;
    trspk_d3d8_colmajor_to_d3dmatrix(view.m, &d3d_view);
    trspk_d3d8_colmajor_to_d3dmatrix(proj.m, &d3d_proj);
    /* Legacy d3d8_old: VIEW/PROJECTION + tex0 disable only; WORLD set per MODEL_DRAW. */
    dev->SetTransform(D3DTS_VIEW, &d3d_view);
    dev->SetTransform(D3DTS_PROJECTION, &d3d_proj);
    D3DMATRIX idtex;
    ZeroMemory(&idtex, sizeof(idtex));
    idtex._11 = idtex._22 = idtex._33 = idtex._44 = 1.0f;
    dev->SetTransform(D3DTS_TEXTURE0, &idtex);
    dev->SetTextureStageState(0u, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}
#endif

void
trspk_d3d8_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
{
    /* Translation block (rows 12–14): legacy d3d8_old passes camera 0,0,0 so view has no
     * translation; trspk_d3d8_fill_view_proj_for_pass / frame_begin_set_transforms use 0,0,0. */
    float cosPitch = cosf(-pitch);
    float sinPitch = sinf(-pitch);
    float cosYaw = cosf(-yaw);
    float sinYaw = sinf(-yaw);

    out_matrix[0] = cosYaw;
    out_matrix[1] = sinYaw * sinPitch;
    out_matrix[2] = sinYaw * cosPitch;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 0.0f;
    out_matrix[5] = cosPitch;
    out_matrix[6] = -sinPitch;
    out_matrix[7] = 0.0f;
    out_matrix[8] = -sinYaw;
    out_matrix[9] = cosYaw * sinPitch;
    out_matrix[10] = cosYaw * cosPitch;
    out_matrix[11] = 0.0f;
    out_matrix[12] = -camera_x * cosYaw + camera_z * sinYaw;
    out_matrix[13] =
        -camera_x * sinYaw * sinPitch - camera_y * cosPitch - camera_z * cosYaw * sinPitch;
    out_matrix[14] =
        -camera_x * sinYaw * cosPitch + camera_y * sinPitch - camera_z * cosYaw * cosPitch;
    out_matrix[15] = 1.0f;
}

void
trspk_d3d8_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height)
{
    float y = 1.0f / tanf(fov * 0.5f);
    float x = y;
    out_matrix[0] = x * 512.0f / (screen_width / 2.0f);
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 0.0f;
    out_matrix[5] = -y * 512.0f / (screen_height / 2.0f);
    out_matrix[6] = 0.0f;
    out_matrix[7] = 0.0f;
    out_matrix[8] = 0.0f;
    out_matrix[9] = 0.0f;
    out_matrix[10] = 0.0f;
    out_matrix[11] = 1.0f;
    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = -1.0f;
    out_matrix[15] = 0.0f;
}

} /* extern "C" */
