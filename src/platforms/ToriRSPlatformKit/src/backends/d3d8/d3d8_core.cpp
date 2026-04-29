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

/** Matches legacy d3d8_reset_device depth surface (D16, no MSAA). */
static bool
trspk_d3d8_create_depth_stencil_surface(TRSPK_D3D8Renderer* r)
{
    if( !r || !r->com_device )
        return false;
    if( r->depth_stencil_surf )
    {
        reinterpret_cast<IDirect3DSurface8*>((uintptr_t)r->depth_stencil_surf)->Release();
        r->depth_stencil_surf = 0u;
    }
    auto* dev = reinterpret_cast<IDirect3DDevice8*>((uintptr_t)r->com_device);
    const UINT w = r->width > 0u ? r->width : 1u;
    const UINT h = r->height > 0u ? r->height : 1u;
    IDirect3DSurface8* ds = nullptr;
    const HRESULT hr =
        dev->CreateDepthStencilSurface(w, h, D3DFMT_D16, D3DMULTISAMPLE_NONE, &ds);
    if( FAILED(hr) || !ds )
        return false;
    r->depth_stencil_surf = (GLuint)(uintptr_t)ds;
    return true;
}

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
    r->width = pp.BackBufferWidth;
    r->height = pp.BackBufferHeight;

    if( !trspk_d3d8_create_depth_stencil_surface(r) )
    {
        dev->Release();
        r->com_device = 0u;
        reinterpret_cast<IDirect3D8*>((uintptr_t)r->com_d3d)->Release();
        r->com_d3d = 0u;
        return false;
    }

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
    pp.BackBufferWidth = r->width > 0u ? r->width : 1u;
    pp.BackBufferHeight = r->height > 0u ? r->height : 1u;

    HRESULT hr = dev->Reset(&pp);
    if( FAILED(hr) )
        return false;
    if( !trspk_d3d8_create_depth_stencil_surface(r) )
        return false;
    if( !trspk_d3d8_create_dynamic_index_ring(r) )
        return false;
    trspk_d3d8_cache_init_atlas(r, TRSPK_ATLAS_DIMENSION, TRSPK_ATLAS_DIMENSION);
    r->device_lost = false;
    return true;
}

void
TRSPK_D3D8_FrameBegin(TRSPK_D3D8Renderer* r)
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
    if( !dev )
        return;

    dev->BeginScene();
    IDirect3DSurface8* bb = nullptr;
    if( SUCCEEDED(dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb )
    {
        IDirect3DSurface8* ds =
            r->depth_stencil_surf
                ? reinterpret_cast<IDirect3DSurface8*>((uintptr_t)r->depth_stencil_surf)
                : nullptr;
        dev->SetRenderTarget(bb, ds);
        bb->Release();
    }
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    D3DVIEWPORT8 vp;
    ZeroMemory(&vp, sizeof(vp));
    vp.X = 0;
    vp.Y = 0;
    vp.Width = r->width > 0u ? r->width : 1u;
    vp.Height = r->height > 0u ? r->height : 1u;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    dev->SetViewport(&vp);
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
        D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
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
    if( r->depth_stencil_surf )
    {
        reinterpret_cast<IDirect3DSurface8*>((uintptr_t)r->depth_stencil_surf)->Release();
        r->depth_stencil_surf = 0u;
    }
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
