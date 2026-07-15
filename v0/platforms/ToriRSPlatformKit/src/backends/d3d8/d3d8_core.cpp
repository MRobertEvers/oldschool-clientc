/**
 * TRSPK Direct3D 8 device bootstrap: HAL, HWND swap chain, Reset hooks (aligned with legacy Win32 d3d8).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d8.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../include/ToriRSPlatformKit/trspk_types.h"

#include "d3d8_internal.h"
#include "d3d8_vertex.h"
#include "trspk_d3d8.h"

#if defined(TORIRS_D3D8_STATIC_LINK)
extern "C" IDirect3D8* WINAPI
Direct3DCreate8(UINT SDKVersion);
#endif

#ifndef D3D_SDK_VERSION
#define D3D_SDK_VERSION 220
#endif

typedef IDirect3D8*(WINAPI* PFN_Direct3DCreate8)(UINT);

extern "C" {

void*
trspk_d3d8_internal_device(TRSPK_D3D8Renderer* r)
{
    return r ? (void*)(uintptr_t)r->com_device : nullptr;
}

void*
trspk_d3d8_internal_d3d(TRSPK_D3D8Renderer* r)
{
    return r ? (void*)(uintptr_t)r->com_d3d : nullptr;
}

} /* extern "C" */

extern "C" bool
trspk_d3d8_dynamic_init(TRSPK_D3D8Renderer* r);

extern "C" void
trspk_d3d8_dynamic_shutdown(TRSPK_D3D8Renderer* r);

extern "C" void
trspk_d3d8_pass_free_pending_dynamic_uploads(TRSPK_D3D8Renderer* r);

extern "C" void
trspk_d3d8_cache_init_atlas(
    TRSPK_D3D8Renderer* r,
    uint32_t width,
    uint32_t height);

static void
trspk_d3d8_release_gpu_defaults(TRSPK_D3D8Renderer* r);

static bool
trspk_d3d8_create_dynamic_index_ring(TRSPK_D3D8Renderer* r);

static void
trspk_d3d8_release_scene_render_targets(TRSPK_D3D8Renderer* r)
{
    if( !r )
        return;
    if( r->scene_rt_surf )
    {
        reinterpret_cast<IDirect3DSurface8*>((uintptr_t)r->scene_rt_surf)->Release();
        r->scene_rt_surf = 0u;
    }
    if( r->scene_rt_tex )
    {
        reinterpret_cast<IDirect3DTexture8*>((uintptr_t)r->scene_rt_tex)->Release();
        r->scene_rt_tex = 0u;
    }
    if( r->depth_stencil_surf )
    {
        reinterpret_cast<IDirect3DSurface8*>((uintptr_t)r->depth_stencil_surf)->Release();
        r->depth_stencil_surf = 0u;
    }
}

/** Legacy d3d8_reset_device: scene color RT + D16 depth at renderer buffer size. */
static bool
trspk_d3d8_create_scene_render_targets(TRSPK_D3D8Renderer* r)
{
    if( !r || !r->com_device )
        return false;
    trspk_d3d8_release_scene_render_targets(r);
    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
    const UINT w = r->width > 0u ? r->width : 1u;
    const UINT h = r->height > 0u ? r->height : 1u;
    IDirect3DTexture8* tex = nullptr;
    HRESULT hr = dev->CreateTexture(
        w, h, 1u, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &tex);
    if( FAILED(hr) || !tex )
        return false;
    IDirect3DSurface8* surf = nullptr;
    hr = tex->GetSurfaceLevel(0u, &surf);
    if( FAILED(hr) || !surf )
    {
        tex->Release();
        return false;
    }
    IDirect3DSurface8* ds = nullptr;
    hr = dev->CreateDepthStencilSurface(w, h, D3DFMT_D16, D3DMULTISAMPLE_NONE, &ds);
    if( FAILED(hr) || !ds )
    {
        surf->Release();
        tex->Release();
        return false;
    }
    r->scene_rt_tex = (GLuint)(uintptr_t)tex;
    r->scene_rt_surf = (GLuint)(uintptr_t)surf;
    r->depth_stencil_surf = (GLuint)(uintptr_t)ds;
    return true;
}

static void
trspk_d3d8_apply_default_render_state(IDirect3DDevice8* dev)
{
    if( !dev )
        return;
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHAREF, 128u);
    dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);

    dev->SetTextureStageState(0u, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0u, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0u, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0u, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0u, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0u, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

static const DWORD kFvfWorld = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

/** After UI / composite draws: avoid leaving kFvfUi + SELECTARG1 bound (would mis-read mesh VBs). */
static void
trspk_d3d8_restore_world_draw_state(IDirect3DDevice8* dev)
{
    if( !dev )
        return;
    dev->SetPixelShader(0u);
    dev->SetVertexShader(kFvfWorld);
    dev->SetTexture(0u, nullptr);
    dev->SetTexture(1u, nullptr);
    trspk_d3d8_apply_default_render_state(dev);
    dev->SetTextureStageState(1u, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1u, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
}

static void
trspk_d3d8_compute_letterbox_dst(int buf_w, int buf_h, int window_w, int window_h, RECT* out_dst)
{
    out_dst->left = 0;
    out_dst->top = 0;
    out_dst->right = buf_w;
    out_dst->bottom = buf_h;
    if( window_w <= 0 || window_h <= 0 )
        return;
    const float src_aspect = (float)buf_w / (float)buf_h;
    const float window_aspect = (float)window_w / (float)window_h;
    int dst_x = 0;
    int dst_y = 0;
    int dst_w = buf_w;
    int dst_h = buf_h;
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
    out_dst->left = dst_x;
    out_dst->top = dst_y;
    out_dst->right = dst_x + dst_w;
    out_dst->bottom = dst_y + dst_h;
}

struct TRSPK_D3D8UiVertex
{
    float x, y, z, rhw;
    DWORD diffuse;
    float u, v;
};

static const DWORD kFvfUi = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

static TRSPK_D3D8Renderer*
trspk_d3d8_alloc(uint32_t width, uint32_t height)
{
    TRSPK_D3D8Renderer* r = (TRSPK_D3D8Renderer*)calloc(1u, sizeof(TRSPK_D3D8Renderer));
    if( !r )
        return nullptr;
    r->width = width;
    r->height = height;
    r->window_width = width;
    r->window_height = height;
    r->pass_logical_rect.x = 0;
    r->pass_logical_rect.y = 0;
    r->pass_logical_rect.width = (int32_t)width;
    r->pass_logical_rect.height = (int32_t)height;
    r->pass_gl_rect = r->pass_logical_rect;
    r->cache = trspk_resource_cache_create(512u, TRSPK_VERTEX_FORMAT_D3D8_SOAOS);
    r->batch_staging =
        trspk_batch16_create(65535u, 65535u, TRSPK_VERTEX_FORMAT_D3D8);
    if( !trspk_d3d8_dynamic_init(r) || !r->cache || !r->batch_staging )
    {
        trspk_d3d8_dynamic_shutdown(r);
        trspk_resource_cache_destroy(r->cache);
        trspk_batch16_destroy(r->batch_staging);
        free(r);
        return nullptr;
    }
    return r;
}

static bool
trspk_d3d8_create_d3d(TRSPK_D3D8Renderer* r, HWND hwnd)
{
#if defined(TORIRS_D3D8_STATIC_LINK)
    IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
#else
    HMODULE h = LoadLibraryA("d3d8.dll");
    if( !h )
        return false;
    PFN_Direct3DCreate8 fn =
        reinterpret_cast<PFN_Direct3DCreate8>(GetProcAddress(h, "Direct3DCreate8"));
    if( !fn )
        return false;
    IDirect3D8* d3d = fn(D3D_SDK_VERSION);
#endif
    if( !d3d )
        return false;
    r->com_d3d = (uintptr_t)d3d;

    /* Match legacy platform_impl2 Win32 d3d8_create_device (pre-TRSPK): COPY swap, explicit RGBA backbuffer,
     * no PURE device, no auto depth-stencil — depth from CreateDepthStencilSurface (see d3d8_reset_device). */
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_COPY;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.hDeviceWindow = hwnd;
    pp.EnableAutoDepthStencil = FALSE;

    RECT cr;
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;
    if( cw <= 0 )
        cw = 800;
    if( ch <= 0 )
        ch = 600;
    pp.BackBufferWidth = (UINT)cw;
    pp.BackBufferHeight = (UINT)ch;

    D3DCAPS8 caps;
    ZeroMemory(&caps, sizeof(caps));
    DWORD create_flags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    HRESULT capshr = d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
    if( capshr == D3D_OK )
    {
        if( !(caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) )
            create_flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    }

    IDirect3DDevice8* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        create_flags,
        &pp,
        &dev);
    if( FAILED(hr) || !dev )
    {
        d3d->Release();
        r->com_d3d = 0u;
        return false;
    }

    r->com_device = (uintptr_t)dev;
    r->platform_hwnd = hwnd;
    /* Keep r->width / r->height as the scene buffer (Init); swap chain uses client area only. */
    r->window_width = (uint32_t)cw;
    r->window_height = (uint32_t)ch;

    return true;
}

extern "C" {

TRSPK_D3D8Renderer*
TRSPK_D3D8_Init(TRSPK_D3D8_WindowHandle hwnd, uint32_t width, uint32_t height)
{
    if( !hwnd )
        return nullptr;
    TRSPK_D3D8Renderer* r = trspk_d3d8_alloc(width, height);
    if( !r )
        return nullptr;
    if( !trspk_d3d8_create_d3d(r, (HWND)hwnd) )
    {
        trspk_d3d8_dynamic_shutdown(r);
        trspk_resource_cache_destroy(r->cache);
        trspk_batch16_destroy(r->batch_staging);
        free(r);
        return nullptr;
    }

    if( !trspk_d3d8_create_scene_render_targets(r) )
    {
        auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
        auto* d3d = reinterpret_cast<IDirect3D8*>((uintptr_t)r->com_d3d);
        trspk_d3d8_release_gpu_defaults(r);
        if( dev )
            dev->Release();
        if( d3d )
            d3d->Release();
        r->com_device = 0u;
        r->com_d3d = 0u;
        trspk_d3d8_dynamic_shutdown(r);
        trspk_resource_cache_destroy(r->cache);
        trspk_batch16_destroy(r->batch_staging);
        free(r);
        return nullptr;
    }

    if( !trspk_d3d8_create_dynamic_index_ring(r) )
    {
        auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
        auto* d3d = reinterpret_cast<IDirect3D8*>((uintptr_t)r->com_d3d);
        trspk_d3d8_release_gpu_defaults(r);
        if( dev )
            dev->Release();
        if( d3d )
            d3d->Release();
        r->com_device = 0u;
        r->com_d3d = 0u;
        trspk_d3d8_dynamic_shutdown(r);
        trspk_resource_cache_destroy(r->cache);
        trspk_batch16_destroy(r->batch_staging);
        free(r);
        return nullptr;
    }

    trspk_d3d8_cache_init_atlas(r, TRSPK_ATLAS_DIMENSION, TRSPK_ATLAS_DIMENSION);
    {
        auto* idev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
        if( idev )
            trspk_d3d8_apply_default_render_state(idev);
    }
    r->ready = true;
    return r;
}

void
TRSPK_D3D8_Shutdown(TRSPK_D3D8Renderer* r)
{
    if( !r )
        return;
    trspk_d3d8_pass_free_pending_dynamic_uploads(r);
    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
    if( dev )
    {
        trspk_d3d8_release_gpu_defaults(r);
        dev->Release();
    }
    auto* d3d = reinterpret_cast<IDirect3D8*>((uintptr_t)r->com_d3d);
    if( d3d )
        d3d->Release();
    r->com_device = 0u;
    r->com_d3d = 0u;

    for( uint32_t i = 0; i < TRSPK_MAX_WEBGL1_CHUNKS; ++i )
        free(r->pass_state.chunk_index_pools[i]);
    free(r->pass_state.subdraws);
    trspk_d3d8_dynamic_shutdown(r);
    trspk_resource_cache_destroy(r->cache);
    trspk_batch16_destroy(r->batch_staging);
    free(r);
}

void
TRSPK_D3D8_Resize(TRSPK_D3D8Renderer* r, uint32_t width, uint32_t height)
{
    if( !r )
        return;
    r->width = width;
    r->height = height;
    TRSPK_D3D8_TryReset(r);
}

void
TRSPK_D3D8_SetWindowSize(TRSPK_D3D8Renderer* r, uint32_t width, uint32_t height)
{
    if( !r )
        return;
    r->window_width = width;
    r->window_height = height;
}

void
TRSPK_D3D8_NotifyDeviceLost(TRSPK_D3D8Renderer* r)
{
    if( r )
        r->device_lost = true;
}

bool
TRSPK_D3D8_TryReset(TRSPK_D3D8Renderer* r)
{
    if( !r || !r->com_device )
        return false;
    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);

    trspk_d3d8_release_gpu_defaults(r);

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_COPY;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.hDeviceWindow = (HWND)r->platform_hwnd;
    pp.EnableAutoDepthStencil = FALSE;
    RECT cr;
    GetClientRect((HWND)r->platform_hwnd, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;
    if( cw <= 0 )
        cw = 800;
    if( ch <= 0 )
        ch = 600;
    pp.BackBufferWidth = (UINT)cw;
    pp.BackBufferHeight = (UINT)ch;

    HRESULT hr = dev->Reset(&pp);
    if( FAILED(hr) )
        return false;
    r->window_width = (uint32_t)cw;
    r->window_height = (uint32_t)ch;
    if( !trspk_d3d8_create_scene_render_targets(r) )
        return false;
    if( !trspk_d3d8_create_dynamic_index_ring(r) )
        return false;
    trspk_d3d8_cache_init_atlas(r, TRSPK_ATLAS_DIMENSION, TRSPK_ATLAS_DIMENSION);
    r->device_lost = false;
    return true;
}

void
TRSPK_D3D8_FrameBegin(TRSPK_D3D8Renderer* r, int view_w, int view_h, struct GGame* game)
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

    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
    if( !dev || !r->scene_rt_surf || !r->depth_stencil_surf )
        return;

    IDirect3DSurface8* scene_surf =
        reinterpret_cast<IDirect3DSurface8*>((uintptr_t)r->scene_rt_surf);
    IDirect3DSurface8* scene_ds =
        reinterpret_cast<IDirect3DSurface8*>((uintptr_t)r->depth_stencil_surf);
    HRESULT hr_rt = dev->SetRenderTarget(scene_surf, scene_ds);
    if( FAILED(hr_rt) )
        return;

    const UINT scene_w = r->width > 0u ? r->width : 1u;
    const UINT scene_h = r->height > 0u ? r->height : 1u;

    D3DVIEWPORT8 frame_vp_2d;
    ZeroMemory(&frame_vp_2d, sizeof(frame_vp_2d));
    frame_vp_2d.X = 0u;
    frame_vp_2d.Y = 0u;
    frame_vp_2d.Width = scene_w;
    frame_vp_2d.Height = scene_h;
    frame_vp_2d.MinZ = 0.0f;
    frame_vp_2d.MaxZ = 1.0f;
    dev->SetViewport(&frame_vp_2d);

    /* Legacy d3d8_old: SetViewport(2d) then d3d8_apply_default_render_state + LIGHTING off. */
    trspk_d3d8_apply_default_render_state(dev);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);

    int vp_w = view_w > 0 ? view_w : (int)scene_w;
    int vp_h = view_h > 0 ? view_h : (int)scene_h;

    DWORD vp3d_x = (DWORD)(r->dash_offset_x > 0 ? r->dash_offset_x : 0);
    DWORD vp3d_y = (DWORD)(r->dash_offset_y > 0 ? r->dash_offset_y : 0);
    DWORD vp3d_w = (DWORD)vp_w;
    DWORD vp3d_h = (DWORD)vp_h;
    if( (int)(vp3d_x + vp3d_w) > (int)scene_w )
        vp3d_w = (DWORD)((int)scene_w - (int)vp3d_x);
    if( (int)(vp3d_y + vp3d_h) > (int)scene_h )
        vp3d_h = (DWORD)((int)scene_h - (int)vp3d_y);
    if( (int)vp3d_w < 1 )
        vp3d_w = 1u;
    if( (int)vp3d_h < 1 )
        vp3d_h = 1u;

    D3DVIEWPORT8 frame_vp_3d;
    ZeroMemory(&frame_vp_3d, sizeof(frame_vp_3d));
    frame_vp_3d.X = vp3d_x;
    frame_vp_3d.Y = vp3d_y;
    frame_vp_3d.Width = vp3d_w;
    frame_vp_3d.Height = vp3d_h;
    frame_vp_3d.MinZ = 0.0f;
    frame_vp_3d.MaxZ = 1.0f;

    dev->SetViewport(&frame_vp_3d);
    dev->Clear(0u, NULL, D3DCLEAR_ZBUFFER, 0u, 1.0f, 0u);
    dev->SetViewport(&frame_vp_2d);

    dev->BeginScene();
    if( game )
        trspk_d3d8_frame_begin_set_transforms((void*)dev, game, vp_w, vp_h);
}

void
TRSPK_D3D8_FrameEnd(TRSPK_D3D8Renderer* r)
{
    if( !r || !r->com_device )
        return;
    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
    dev->EndScene();
}

void
TRSPK_D3D8_Present(TRSPK_D3D8Renderer* r)
{
    if( !r || !r->com_device )
        return;
    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
    IDirect3DTexture8* scene_tex =
        r->scene_rt_tex ? reinterpret_cast<IDirect3DTexture8*>((uintptr_t)r->scene_rt_tex) :
                          nullptr;
    if( !scene_tex )
    {
        dev->Present(NULL, NULL, NULL, NULL);
        return;
    }

    IDirect3DSurface8* bb = nullptr;
    if( FAILED(dev->GetBackBuffer(0u, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb )
    {
        dev->Present(NULL, NULL, NULL, NULL);
        return;
    }
    HRESULT hr_sbb = dev->SetRenderTarget(bb, nullptr);
    bb->Release();
    if( FAILED(hr_sbb) )
    {
        dev->Present(NULL, NULL, NULL, NULL);
        return;
    }

    dev->Clear(0u, NULL, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0u);

    RECT cr;
    int win_w = (int)r->window_width;
    int win_h = (int)r->window_height;
    if( r->platform_hwnd && GetClientRect((HWND)r->platform_hwnd, &cr) )
    {
        const int cw = cr.right - cr.left;
        const int ch = cr.bottom - cr.top;
        if( cw > 0 && ch > 0 )
        {
            win_w = cw;
            win_h = ch;
        }
    }

    RECT dst;
    trspk_d3d8_compute_letterbox_dst(
        (int)r->width, (int)r->height, win_w, win_h, &dst);

    dev->BeginScene();

    D3DVIEWPORT8 bbvp;
    ZeroMemory(&bbvp, sizeof(bbvp));
    bbvp.X = 0u;
    bbvp.Y = 0u;
    bbvp.Width = (DWORD)(win_w > 0 ? win_w : 1);
    bbvp.Height = (DWORD)(win_h > 0 ? win_h : 1);
    bbvp.MinZ = 0.0f;
    bbvp.MaxZ = 1.0f;
    dev->SetViewport(&bbvp);

    dev->SetVertexShader(kFvfUi);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetTextureStageState(0u, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0u, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0u, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0u, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetTexture(0u, scene_tex);
    dev->SetTexture(1u, nullptr);

    const float x0 = (float)dst.left;
    const float y0 = (float)dst.top;
    const float x1 = (float)dst.right;
    const float y1 = (float)dst.bottom;
    const DWORD wcol = 0xFFFFFFFFu;
    TRSPK_D3D8UiVertex qb[6] = {
        { x0, y0, 0.0f, 1.0f, wcol, 0.0f, 0.0f },
        { x1, y0, 0.0f, 1.0f, wcol, 1.0f, 0.0f },
        { x1, y1, 0.0f, 1.0f, wcol, 1.0f, 1.0f },
        { x0, y0, 0.0f, 1.0f, wcol, 0.0f, 0.0f },
        { x1, y1, 0.0f, 1.0f, wcol, 1.0f, 1.0f },
        { x0, y1, 0.0f, 1.0f, wcol, 0.0f, 1.0f },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2u, qb, sizeof(TRSPK_D3D8UiVertex));

    /* Legacy letterbox tail: POINT filters + MODULATE only (not full restore_world). */
    dev->SetTextureStageState(0u, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0u, D3DTSS_MINFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0u, D3DTSS_COLOROP, D3DTOP_MODULATE);

    dev->EndScene();
    dev->Present(NULL, NULL, NULL, NULL);
}

void*
TRSPK_D3D8_GetDevice(TRSPK_D3D8Renderer* r)
{
    return r ? (void*)(uintptr_t)r->com_device : nullptr;
}

TRSPK_ResourceCache*
TRSPK_D3D8_GetCache(TRSPK_D3D8Renderer* renderer)
{
    return renderer ? renderer->cache : NULL;
}

TRSPK_Batch16*
TRSPK_D3D8_GetBatchStaging(TRSPK_D3D8Renderer* renderer)
{
    return renderer ? renderer->batch_staging : NULL;
}

} /* extern "C" */

static bool
trspk_d3d8_create_dynamic_index_ring(TRSPK_D3D8Renderer* r)
{
    if( !r || !r->com_device )
        return false;
    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
    const UINT bytes = (UINT)(65536u * sizeof(uint16_t));
    IDirect3DIndexBuffer8* ib = nullptr;
    HRESULT hr = dev->CreateIndexBuffer(
        bytes,
        D3DUSAGE_DYNAMIC,
        D3DFMT_INDEX16,
        D3DPOOL_DEFAULT,
        &ib);
    if( FAILED(hr) || !ib )
        return false;
    r->dynamic_ibo = (GLuint)(uintptr_t)ib;
    return true;
}

static void
trspk_d3d8_release_gpu_defaults(TRSPK_D3D8Renderer* r)
{
    if( !r || !r->com_device )
        return;
    trspk_d3d8_release_scene_render_targets(r);
    if( r->dynamic_ibo )
    {
        reinterpret_cast<IDirect3DIndexBuffer8*>((uintptr_t)r->dynamic_ibo)->Release();
        r->dynamic_ibo = 0u;
    }
    if( r->atlas_texture )
    {
        reinterpret_cast<IDirect3DTexture8*>((uintptr_t)r->atlas_texture)->Release();
        r->atlas_texture = 0u;
    }
    for( uint32_t i = 0; i < TRSPK_MAX_WEBGL1_CHUNKS; ++i )
    {
        if( r->batch_chunk_vbos[i] )
        {
            reinterpret_cast<IDirect3DVertexBuffer8*>((uintptr_t)r->batch_chunk_vbos[i])->Release();
            r->batch_chunk_vbos[i] = 0u;
        }
        if( r->batch_chunk_ebos[i] )
        {
            reinterpret_cast<IDirect3DIndexBuffer8*>((uintptr_t)r->batch_chunk_ebos[i])->Release();
            r->batch_chunk_ebos[i] = 0u;
        }
        if( r->dynamic_npc_vbos[i] )
        {
            reinterpret_cast<IDirect3DVertexBuffer8*>((uintptr_t)r->dynamic_npc_vbos[i])->Release();
            r->dynamic_npc_vbos[i] = 0u;
        }
        if( r->dynamic_npc_ebos[i] )
        {
            reinterpret_cast<IDirect3DIndexBuffer8*>((uintptr_t)r->dynamic_npc_ebos[i])->Release();
            r->dynamic_npc_ebos[i] = 0u;
        }
        if( r->dynamic_projectile_vbos[i] )
        {
            reinterpret_cast<IDirect3DVertexBuffer8*>((uintptr_t)r->dynamic_projectile_vbos[i])
                ->Release();
            r->dynamic_projectile_vbos[i] = 0u;
        }
        if( r->dynamic_projectile_ebos[i] )
        {
            reinterpret_cast<IDirect3DIndexBuffer8*>((uintptr_t)r->dynamic_projectile_ebos[i])
                ->Release();
            r->dynamic_projectile_ebos[i] = 0u;
        }
    }
    r->chunk_count = 0u;
}
