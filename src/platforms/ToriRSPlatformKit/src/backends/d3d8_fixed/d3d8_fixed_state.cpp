/**
 * D3D8 fixed-function backend — state helpers, frame lifecycle, device helpers.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d8.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "graphics/shared_tables.h"
#include "graphics/uv_pnm.h"
}

#include "d3d8_fixed_internal.h"
#include "d3d8_fixed_state.h"
#include "d3d8_fixed_pass.h"
#include "d3d8_fixed_cache.h"

#include "osrs/game.h"
#include "../../../../platform_impl2_win32_renderer_d3d8.h"

D3D8FixedInternal*
trspk_d3d8_fixed_priv(TRSPK_D3D8FixedRenderer* r)
{
    return r ? static_cast<D3D8FixedInternal*>(r->opaque_internal) : nullptr;
}

void
trspk_d3d8_fixed_log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[d3d8-fixed] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

static const DWORD kFvfWorld = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static const DWORD kFvfUi   = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

static const bool kSkipNuklearOverlayQuad = false;

/* === Matrix utilities ====================================================== */

static void
mat4_mul_colmajor(const float* a, const float* b, float* out)
{
    for( int c = 0; c < 4; ++c )
        for( int r = 0; r < 4; ++r )
        {
            float s = 0.0f;
            for( int k = 0; k < 4; ++k )
                s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

static void
d3d8_remap_projection_z(float* proj_colmajor)
{
    static const float k_clip_z[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
    };
    float tmp[16];
    mat4_mul_colmajor(k_clip_z, proj_colmajor, tmp);
    memcpy(proj_colmajor, tmp, sizeof(tmp));
}

static void
d3d8_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
{
    (void)camera_x;
    (void)camera_y;
    (void)camera_z;
    float cosPitch = cosf(-pitch);
    float sinPitch = sinf(-pitch);
    float cosYaw   = cosf(-yaw);
    float sinYaw   = sinf(-yaw);

    out_matrix[0]  = cosYaw;
    out_matrix[1]  = sinYaw * sinPitch;
    out_matrix[2]  = sinYaw * cosPitch;
    out_matrix[3]  = 0.0f;

    out_matrix[4]  = 0.0f;
    out_matrix[5]  = cosPitch;
    out_matrix[6]  = -sinPitch;
    out_matrix[7]  = 0.0f;

    out_matrix[8]  = -sinYaw;
    out_matrix[9]  = cosYaw * sinPitch;
    out_matrix[10] = cosYaw * cosPitch;
    out_matrix[11] = 0.0f;

    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = 0.0f;
    out_matrix[15] = 1.0f;
}

static void
d3d8_compute_projection_matrix(float* out_matrix, float fov, float screen_width, float screen_height)
{
    float y = 1.0f / tanf(fov * 0.5f);
    float x = y;

    out_matrix[0]  = x * 512.0f / (screen_width / 2.0f);
    out_matrix[1]  = 0.0f;
    out_matrix[2]  = 0.0f;
    out_matrix[3]  = 0.0f;

    out_matrix[4]  = 0.0f;
    out_matrix[5]  = -y * 512.0f / (screen_height / 2.0f);
    out_matrix[6]  = 0.0f;
    out_matrix[7]  = 0.0f;

    out_matrix[8]  = 0.0f;
    out_matrix[9]  = 0.0f;
    out_matrix[10] = 0.0f;
    out_matrix[11] = 1.0f;

    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = -1.0f;
    out_matrix[15] = 0.0f;
}

static void
colmajor_to_d3dmatrix(const float* cm, D3DMATRIX* m)
{
    /* Transpose: column-vector convention → D3D8 row-vector convention. */
    m->_11 = cm[0];  m->_12 = cm[1];  m->_13 = cm[2];  m->_14 = cm[3];
    m->_21 = cm[4];  m->_22 = cm[5];  m->_23 = cm[6];  m->_24 = cm[7];
    m->_31 = cm[8];  m->_32 = cm[9];  m->_33 = cm[10]; m->_34 = cm[11];
    m->_41 = cm[12]; m->_42 = cm[13]; m->_43 = cm[14]; m->_44 = cm[15];
}

/* === Texture helpers (also used by per-id submit path in d3d8_fixed_pass.cpp) = */

float
d3d8_texture_animation_signed(int animation_direction, int animation_speed)
{
    if( animation_direction == 0 )
        return 0.0f;
    float speed = ((float)animation_speed) / 128.0f;
    if( animation_direction == 2 || animation_direction == 4 )
        return speed;
    return -speed;
}

/* === Letterbox dst ========================================================= */

static void
compute_letterbox_dst(int buf_w, int buf_h, int window_w, int window_h, RECT* out_dst)
{
    out_dst->left   = 0;
    out_dst->top    = 0;
    out_dst->right  = buf_w;
    out_dst->bottom = buf_h;
    if( window_w <= 0 || window_h <= 0 )
        return;
    float src_aspect    = (float)buf_w / (float)buf_h;
    float window_aspect = (float)window_w / (float)window_h;
    int   dst_x = 0, dst_y = 0, dst_w = buf_w, dst_h = buf_h;
    if( src_aspect > window_aspect )
    {
        dst_w = window_w;
        dst_h = (int)(window_w / src_aspect);
        dst_x = 0;
        dst_y = (window_h - dst_h) / 2;
    }
    else
    {
        dst_h = window_h;
        dst_w = (int)(window_h * src_aspect);
        dst_y = 0;
        dst_x = (window_w - dst_w) / 2;
    }
    out_dst->left   = dst_x;
    out_dst->top    = dst_y;
    out_dst->right  = dst_x + dst_w;
    out_dst->bottom = dst_y + dst_h;
}

/* === Command log throttle ================================================== */

static constexpr int kD3d8GfxKindSlots = 32;

bool
d3d8_fixed_should_log_cmd(D3D8FixedInternal* p, int kind)
{
    if( !p )
        return false;
    if( kind < 0 || kind >= kD3d8GfxKindSlots )
        return false;
    return !p->trace_first_gfx[(size_t)kind];
}

void
d3d8_fixed_mark_cmd_logged(D3D8FixedInternal* p, int kind)
{
    if( !p )
        return;
    if( kind >= 0 && kind < kD3d8GfxKindSlots )
        p->trace_first_gfx[(size_t)kind] = true;
}

/* === Font flush ============================================================ */

void
D3D8FixedInternal::flush_font()
{
    IDirect3DDevice8* dev = device;
    if( !dev )
        return;
    if( pending_font_verts.empty() || current_font_id < 0 )
    {
        pending_font_verts.clear();
        return;
    }
    auto it = font_atlas_by_id.find(current_font_id);
    if( it == font_atlas_by_id.end() || !it->second )
    {
        pending_font_verts.clear();
        return;
    }
    d3d8_fixed_ensure_pass(this, dev, PASS_2D);
    dev->SetTexture(0, it->second);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    dev->DrawPrimitiveUP(
        D3DPT_TRIANGLELIST,
        (UINT)(pending_font_verts.size() / 3),
        pending_font_verts.data(),
        sizeof(D3D8UiVertex));
    pending_font_verts.clear();
}

/* === 3D pass flush (called before 2D ops) ================================== */

void
D3D8FixedInternal::flush_3d_pass_if_needed()
{
    if( pass_state.subdraws.empty() )
        return;
    if( device )
        d3d8_fixed_submit_pass(this, device);
}

/* === Pass kind setup ======================================================= */

void
d3d8_fixed_ensure_pass(D3D8FixedInternal* priv, IDirect3DDevice8* dev, PassKind want)
{
    if( !priv || !dev )
        return;
    if( priv->current_pass == want )
        return;

    static const DWORD kFvfWorldLocal = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    static const DWORD kFvfUiLocal    = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

    if( want == PASS_3D )
    {
        dev->SetViewport(&priv->frame_vp_3d);
        dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
        dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
        dev->SetVertexShader(kFvfWorldLocal);
    }
    else
    {
        dev->SetViewport(&priv->frame_vp_2d);
        dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetVertexShader(kFvfUiLocal);
    }
    priv->current_pass = want;
}

/* === Render state helper =================================================== */

static void
d3d8_apply_default_render_state(IDirect3DDevice8* dev)
{
    dev->SetRenderState(D3DRS_ZENABLE,          D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,     FALSE);
    dev->SetRenderState(D3DRS_ZFUNC,            D3DCMP_ALWAYS);
    dev->SetRenderState(D3DRS_CULLMODE,         D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING,         FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND,         D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND,        D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,  FALSE);
    dev->SetRenderState(D3DRS_ALPHAREF,         128);
    dev->SetRenderState(D3DRS_ALPHAFUNC,        D3DCMP_GREATEREQUAL);

    dev->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

static void
d3d8_disable_texture_transform(IDirect3DDevice8* dev)
{
    D3DMATRIX id;
    memset(&id, 0, sizeof(id));
    id._11 = id._22 = id._33 = id._44 = 1.0f;
    dev->SetTransform(D3DTS_TEXTURE0, &id);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}

/* === Event handlers (included from d3d8_fixed_events.cpp) ================= */
#include "d3d8_fixed_events.cpp"

/* === Internal teardown ===================================================== */

void
d3d8_fixed_destroy_internal(D3D8FixedInternal* p)
{
    if( !p )
        return;

    /* Submit any pending 3D pass so we don't leak mid-frame. */
    if( !p->pass_state.subdraws.empty() && p->device )
        d3d8_fixed_submit_pass(p, p->device);
    d3d8_fixed_reset_pass(p);

    /* Release per-id world textures (PER_ID mode only). */
    for( auto& kv : p->texture_by_id )
        if( kv.second )
            kv.second->Release();
    p->texture_by_id.clear();
    p->texture_anim_speed_by_id.clear();
    p->texture_opaque_by_id.clear();

    /* Release LRU GPU VBOs (per-instance models). */
    for( auto& kv : p->lru_vbo_by_key )
        if( kv.second )
            kv.second->Release();
    p->lru_vbo_by_key.clear();

    /* Release batch chunk GPU buffers. */
    for( uint32_t c = 0u; c < p->batch_chunk_count; ++c )
    {
        if( p->batch_chunk_vbos[c] )
        {
            p->batch_chunk_vbos[c]->Release();
            p->batch_chunk_vbos[c] = nullptr;
        }
        if( p->batch_chunk_ebos[c] )
        {
            p->batch_chunk_ebos[c]->Release();
            p->batch_chunk_ebos[c] = nullptr;
        }
    }
    p->batch_chunk_count = 0u;

    /* Release atlas texture. */
    if( p->atlas_texture )
    {
        p->atlas_texture->Release();
        p->atlas_texture = nullptr;
    }

    /* Destroy TRSPK infrastructure. */
    if( p->batch_staging )
    {
        trspk_batch16_destroy(p->batch_staging);
        p->batch_staging = nullptr;
    }
    if( p->cache )
    {
        trspk_resource_cache_destroy(p->cache);
        p->cache = nullptr;
    }

    /* Sprite + font maps. */
    for( auto& kv : p->sprite_by_slot )
        if( kv.second )
            kv.second->Release();
    p->sprite_by_slot.clear();
    p->sprite_size_by_slot.clear();

    for( auto& kv : p->font_atlas_by_id )
        if( kv.second )
            kv.second->Release();
    p->font_atlas_by_id.clear();

    /* D3D device resources. */
    if( p->ib_ring )
        p->ib_ring->Release();
    if( p->scene_rt_surf )
        p->scene_rt_surf->Release();
    if( p->scene_rt_tex )
        p->scene_rt_tex->Release();
    if( p->scene_ds )
        p->scene_ds->Release();
    if( p->device )
        p->device->Release();
    if( p->d3d )
        p->d3d->Release();
#if !defined(TORIRS_D3D8_STATIC_LINK)
    if( p->h_dll )
        FreeLibrary(p->h_dll);
#endif
    delete p;
}

/* === Frame lifecycle ======================================================= */

extern "C" void
TRSPK_D3D8Fixed_FrameBegin(
    TRSPK_D3D8FixedRenderer* renderer,
    int view_w,
    int view_h,
    struct GGame* game)
{
    if( !renderer || !game )
        return;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    if( !p || !p->device )
        return;
    IDirect3DDevice8* dev = p->device;

    int vp_w = view_w > 0 ? view_w
                           : (game->view_port ? game->view_port->width : (int)renderer->width);
    int vp_h = view_h > 0 ? view_h
                           : (game->view_port ? game->view_port->height : (int)renderer->height);

    const float pitch_rad = ((float)game->camera_pitch * 2.0f * (float)M_PI) / 2048.0f;
    const float yaw_rad   = ((float)game->camera_yaw   * 2.0f * (float)M_PI) / 2048.0f;

    float view_m[16], proj_m[16];
    d3d8_compute_view_matrix(view_m, 0.0f, 0.0f, 0.0f, pitch_rad, yaw_rad);
    d3d8_compute_projection_matrix(
        proj_m, (90.0f * (float)M_PI) / 180.0f, (float)vp_w, (float)vp_h);
    d3d8_remap_projection_z(proj_m);

    D3DMATRIX d3d_view, d3d_proj;
    colmajor_to_d3dmatrix(view_m, &d3d_view);
    colmajor_to_d3dmatrix(proj_m, &d3d_proj);
    p->frame_view = d3d_view;
    p->frame_proj = d3d_proj;

    p->frame_u_clock = (float)((DWORD)GetTickCount() / 20u);
    p->frame_vp_w    = vp_w;
    p->frame_vp_h    = vp_h;
    renderer->frame_u_clock = p->frame_u_clock;
    renderer->frame_vp_w    = vp_w;
    renderer->frame_vp_h    = vp_h;

    /* Publish frame clock for atlas UV animation baking. */
    p->frame_clock = (double)game->cycle;

    /* Reset per-frame state. */
    d3d8_fixed_reset_pass(p);
    p->ib_ring_write_offset = 0;
    p->current_pass         = PASS_NONE;
    p->debug_model_draws    = 0;
    p->debug_triangles      = 0;
    p->current_font_id      = -1;
    p->pending_font_verts.clear();

    HRESULT hr_rt = dev->SetRenderTarget(p->scene_rt_surf, p->scene_ds);
    if( FAILED(hr_rt) )
        trspk_d3d8_fixed_log(
            "FrameBegin: SetRenderTarget(scene_rt) failed hr=0x%08lX",
            (unsigned long)hr_rt);

    p->frame_vp_2d = { 0, 0, (DWORD)renderer->width, (DWORD)renderer->height, 0.0f, 1.0f };
    DWORD vp3d_x = (DWORD)(renderer->dash_offset_x > 0 ? renderer->dash_offset_x : 0);
    DWORD vp3d_y = (DWORD)(renderer->dash_offset_y > 0 ? renderer->dash_offset_y : 0);
    DWORD vp3d_w = (DWORD)vp_w;
    DWORD vp3d_h = (DWORD)vp_h;
    if( (int)(vp3d_x + vp3d_w) > (int)renderer->width )
        vp3d_w = (DWORD)((int)renderer->width - (int)vp3d_x);
    if( (int)(vp3d_y + vp3d_h) > (int)renderer->height )
        vp3d_h = (DWORD)((int)renderer->height - (int)vp3d_y);
    if( (int)vp3d_w < 1 ) vp3d_w = 1u;
    if( (int)vp3d_h < 1 ) vp3d_h = 1u;
    p->frame_vp_3d = { vp3d_x, vp3d_y, vp3d_w, vp3d_h, 0.0f, 1.0f };

    HRESULT hr_vp = dev->SetViewport(&p->frame_vp_2d);
    if( FAILED(hr_vp) )
        trspk_d3d8_fixed_log(
            "FrameBegin: SetViewport(2d) failed hr=0x%08lX",
            (unsigned long)hr_vp);

    d3d8_apply_default_render_state(dev);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);

    dev->SetViewport(&p->frame_vp_3d);
    dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    dev->SetViewport(&p->frame_vp_2d);

    HRESULT hr_bs = dev->BeginScene();
    if( FAILED(hr_bs) )
        trspk_d3d8_fixed_log(
            "FrameBegin: BeginScene failed hr=0x%08lX",
            (unsigned long)hr_bs);

    dev->SetTransform(D3DTS_VIEW,       &d3d_view);
    dev->SetTransform(D3DTS_PROJECTION, &d3d_proj);
    d3d8_disable_texture_transform(dev);
}

extern "C" void
TRSPK_D3D8Fixed_FlushFont(TRSPK_D3D8FixedRenderer* renderer)
{
    if( !renderer )
        return;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    if( !p )
        return;
    p->flush_font();
}

extern "C" void
TRSPK_D3D8Fixed_FrameEnd(TRSPK_D3D8FixedRenderer* renderer)
{
    if( !renderer )
        return;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    if( !p || !p->device )
        return;
    IDirect3DDevice8* dev = p->device;

    /* Flush any buffered 3D draws that weren't submitted by STATE_END_3D. */
    if( !p->pass_state.subdraws.empty() )
        d3d8_fixed_submit_pass(p, dev);

    HRESULT hr_es = dev->EndScene();
    if( FAILED(hr_es) )
        trspk_d3d8_fixed_log(
            "FrameEnd: EndScene failed hr=0x%08lX",
            (unsigned long)hr_es);

    renderer->diag_frame_submitted_draws = p->debug_model_draws;
    renderer->diag_frame_pass_subdraws   = p->debug_triangles;
}

extern "C" void
TRSPK_D3D8Fixed_Present(
    TRSPK_D3D8FixedRenderer* renderer,
    int window_width,
    int window_height,
    struct Platform2_Win32_Renderer_D3D8* platform)
{
    if( !renderer || !platform )
        return;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    if( !p || !p->device )
        return;
    IDirect3DDevice8* dev = p->device;

    IDirect3DSurface8* bb = nullptr;
    HRESULT hr_gb = dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb);
    if( hr_gb == D3D_OK && bb )
    {
        HRESULT hr_sbb = dev->SetRenderTarget(bb, nullptr);
        if( FAILED(hr_sbb) )
            trspk_d3d8_fixed_log(
                "Present: SetRenderTarget(backbuffer) failed hr=0x%08lX",
                (unsigned long)hr_sbb);
        bb->Release();
    }
    else if( bb )
    {
        bb->Release();
    }
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);

    RECT dst;
    compute_letterbox_dst(
        (int)platform->width,
        (int)platform->height,
        window_width,
        window_height,
        &dst);
    platform->present_dst_x = dst.left;
    platform->present_dst_y = dst.top;
    platform->present_dst_w = (int)(dst.right  - dst.left);
    platform->present_dst_h = (int)(dst.bottom - dst.top);

    HRESULT hr_bs2 = dev->BeginScene();
    if( FAILED(hr_bs2) )
        trspk_d3d8_fixed_log(
            "Present: BeginScene(letterbox) failed hr=0x%08lX",
            (unsigned long)hr_bs2);

    d3d8_fixed_ensure_pass(p, dev, PASS_2D);
    dev->SetTexture(0, p->scene_rt_tex);
    dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    D3DVIEWPORT8 bbvp = {
        0,
        0,
        (DWORD)(window_width  > 0 ? window_width  : 1),
        (DWORD)(window_height > 0 ? window_height : 1),
        0.0f,
        1.0f
    };
    dev->SetViewport(&bbvp);

    float x0 = (float)dst.left;
    float y0 = (float)dst.top;
    float x1 = (float)dst.right;
    float y1 = (float)dst.bottom;
    DWORD wcol = 0xFFFFFFFF;
    D3D8UiVertex qb[6] = {
        { x0, y0, 0.0f, 1.0f, wcol, 0.0f, 0.0f },
        { x1, y0, 0.0f, 1.0f, wcol, 1.0f, 0.0f },
        { x1, y1, 0.0f, 1.0f, wcol, 1.0f, 1.0f },
        { x0, y0, 0.0f, 1.0f, wcol, 0.0f, 0.0f },
        { x1, y1, 0.0f, 1.0f, wcol, 1.0f, 1.0f },
        { x0, y1, 0.0f, 1.0f, wcol, 0.0f, 1.0f },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, qb, sizeof(D3D8UiVertex));
    dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE);

    HRESULT hr_es2 = dev->EndScene();
    if( FAILED(hr_es2) )
        trspk_d3d8_fixed_log(
            "Present: EndScene failed hr=0x%08lX",
            (unsigned long)hr_es2);

    HRESULT hr_pr = dev->Present(nullptr, nullptr, nullptr, nullptr);
    if( FAILED(hr_pr) )
        trspk_d3d8_fixed_log("Present: Present failed hr=0x%08lX", (unsigned long)hr_pr);

    p->frame_count++;
}

extern "C" void
TRSPK_D3D8Fixed_SetTextureMode(
    TRSPK_D3D8FixedRenderer* renderer,
    TRSPK_D3D8FixedTextureMode mode)
{
    if( !renderer )
        return;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    if( !p )
        return;
    p->tex_mode = (mode == TRSPK_D3D8_FIXED_TEX_PER_ID) ? D3D8_FIXED_TEX_PER_ID
                                                          : D3D8_FIXED_TEX_ATLAS;
}
