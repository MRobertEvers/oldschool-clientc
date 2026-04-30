/**
 * D3D8 fixed backend — device create/reset/shutdown (from d3d8_old), nk_overlay omitted
 * (platform_impl2 owns Nuklear overlay texture).
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d8.h>
#include <cstdlib>
#include <cstring>

#include "../../tools/trspk_resource_cache.h"
#include "../../tools/trspk_batch16.h"

#include "d3d8_fixed_internal.h"
#include "d3d8_fixed_state.h"
#include "d3d8_fixed_cache.h"
#include "trspk_d3d8_fixed.h"

#if !defined(TORIRS_D3D8_STATIC_LINK)
typedef IDirect3D8*(WINAPI* PFN_Direct3DCreate8)(UINT);
#endif

static void
d3d8_fixed_apply_default_render_state(IDirect3DDevice8* dev)
{
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHAREF, 128);
    dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);

    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

bool
d3d8_fixed_reset_device(TRSPK_D3D8FixedRenderer* ren, D3D8FixedInternal* p, HWND hwnd)
{
    if( !p->device || !hwnd )
    {
        trspk_d3d8_fixed_log(
            "d3d8_fixed_reset_device: abort device=%p hwnd=%p",
            (void*)p->device,
            (void*)hwnd);
        return false;
    }

    if( p->ib_ring )
    {
        p->ib_ring->Release();
        p->ib_ring = nullptr;
    }
    p->ib_ring_size_bytes = 0;
    if( p->scene_rt_surf )
    {
        p->scene_rt_surf->Release();
        p->scene_rt_surf = nullptr;
    }
    if( p->scene_rt_tex )
    {
        p->scene_rt_tex->Release();
        p->scene_rt_tex = nullptr;
    }
    if( p->scene_ds )
    {
        p->scene_ds->Release();
        p->scene_ds = nullptr;
    }

    RECT cr;
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;
    if( cw <= 0 )
        cw = 1;
    if( ch <= 0 )
        ch = 1;
    p->client_w = (UINT)cw;
    p->client_h = (UINT)ch;

    p->pp.BackBufferWidth = p->client_w;
    p->pp.BackBufferHeight = p->client_h;
    HRESULT hr = p->device->Reset(&p->pp);
    if( FAILED(hr) )
    {
        trspk_d3d8_fixed_log("d3d8_fixed_reset_device: device->Reset failed hr=0x%08lX", (unsigned long)hr);
        return false;
    }

    UINT w = (UINT)ren->width;
    UINT h = (UINT)ren->height;
    hr = p->device->CreateTexture(
        w,
        h,
        1,
        D3DUSAGE_RENDERTARGET,
        D3DFMT_X8R8G8B8,
        D3DPOOL_DEFAULT,
        &p->scene_rt_tex);
    if( FAILED(hr) || !p->scene_rt_tex )
    {
        trspk_d3d8_fixed_log(
            "d3d8_fixed_reset_device: CreateTexture(scene_rt) failed hr=0x%08lX tex=%p",
            (unsigned long)hr,
            (void*)p->scene_rt_tex);
        return false;
    }
    hr = p->scene_rt_tex->GetSurfaceLevel(0, &p->scene_rt_surf);
    if( FAILED(hr) )
    {
        trspk_d3d8_fixed_log(
            "d3d8_fixed_reset_device: GetSurfaceLevel(scene_rt) failed hr=0x%08lX",
            (unsigned long)hr);
        return false;
    }
    hr = p->device->CreateDepthStencilSurface(
        w, h, D3DFMT_D16, D3DMULTISAMPLE_NONE, &p->scene_ds);
    if( FAILED(hr) )
    {
        trspk_d3d8_fixed_log(
            "d3d8_fixed_reset_device: CreateDepthStencilSurface failed hr=0x%08lX",
            (unsigned long)hr);
        return false;
    }

    const size_t ib_need = 65536 * sizeof(uint16_t);
    p->ib_ring_size_bytes = ib_need;
    hr = p->device->CreateIndexBuffer(
        (UINT)ib_need,
        D3DUSAGE_DYNAMIC,
        D3DFMT_INDEX16,
        D3DPOOL_DEFAULT,
        &p->ib_ring);
    if( !SUCCEEDED(hr) || !p->ib_ring )
    {
        trspk_d3d8_fixed_log(
            "d3d8_fixed_reset_device: CreateIndexBuffer failed hr=0x%08lX ib=%p",
            (unsigned long)hr,
            (void*)p->ib_ring);
        return false;
    }
    return true;
}

bool
d3d8_fixed_create_device(TRSPK_D3D8FixedRenderer* ren, D3D8FixedInternal* p, HWND hwnd)
{
#if defined(TORIRS_D3D8_STATIC_LINK)
    p->d3d = Direct3DCreate8(D3D_SDK_VERSION);
#else
    p->h_dll = LoadLibraryA("d3d8.dll");
    if( !p->h_dll )
    {
        trspk_d3d8_fixed_log(
            "d3d8_fixed_create_device: LoadLibraryA(d3d8.dll) failed GetLastError=%lu",
            (unsigned long)GetLastError());
        return false;
    }
    PFN_Direct3DCreate8 fn =
        reinterpret_cast<PFN_Direct3DCreate8>(GetProcAddress(p->h_dll, "Direct3DCreate8"));
    if( !fn )
    {
        trspk_d3d8_fixed_log(
            "d3d8_fixed_create_device: GetProcAddress(Direct3DCreate8) failed GetLastError=%lu",
            (unsigned long)GetLastError());
        return false;
    }
    p->d3d = fn(D3D_SDK_VERSION);
#endif
    if( !p->d3d )
    {
        trspk_d3d8_fixed_log("d3d8_fixed_create_device: Direct3DCreate8 returned NULL");
        return false;
    }

    ZeroMemory(&p->pp, sizeof(p->pp));
    p->pp.Windowed = TRUE;
    p->pp.SwapEffect = D3DSWAPEFFECT_COPY;
    p->pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    p->pp.hDeviceWindow = hwnd;
    p->pp.EnableAutoDepthStencil = FALSE;

    RECT cr;
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;
    if( cw <= 0 )
        cw = 800;
    if( ch <= 0 )
        ch = 600;
    p->client_w = (UINT)cw;
    p->client_h = (UINT)ch;
    p->pp.BackBufferWidth = p->client_w;
    p->pp.BackBufferHeight = p->client_h;

    D3DCAPS8 caps;
    ZeroMemory(&caps, sizeof(caps));
    DWORD create_flags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    HRESULT capshr = p->d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
    if( capshr == D3D_OK )
    {
        if( !(caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) )
            create_flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    }

    HRESULT hr = p->d3d->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        create_flags,
        &p->pp,
        &p->device);
    if( FAILED(hr) || !p->device )
    {
        trspk_d3d8_fixed_log(
            "d3d8_fixed_create_device: CreateDevice failed hr=0x%08lX device=%p",
            (unsigned long)hr,
            (void*)p->device);
        return false;
    }

    d3d8_fixed_apply_default_render_state(p->device);
    if( !d3d8_fixed_reset_device(ren, p, hwnd) )
        return false;
    return true;
}

extern "C" {

TRSPK_D3D8FixedRenderer*
TRSPK_D3D8Fixed_Init(TRSPK_D3D8_WindowHandle hwnd, uint32_t width, uint32_t height)
{
    TRSPK_D3D8FixedRenderer* r =
        (TRSPK_D3D8FixedRenderer*)calloc(1, sizeof(TRSPK_D3D8FixedRenderer));
    if( !r )
        return nullptr;
    r->width = width;
    r->height = height;
    r->window_width = width;
    r->window_height = height;
    r->platform_hwnd = hwnd;
    r->dash_offset_x = 0;
    r->dash_offset_y = 0;

    D3D8FixedInternal* p = new D3D8FixedInternal();
    p->current_pass = PASS_NONE;
    p->current_font_id = -1;
    r->opaque_internal = p;

    if( !d3d8_fixed_create_device(r, p, (HWND)hwnd) )
    {
        delete p;
        r->opaque_internal = nullptr;
        free(r);
        return nullptr;
    }

    /* Initialize TRSPK infrastructure. */
    p->cache = trspk_resource_cache_create(512u, TRSPK_VERTEX_FORMAT_D3D8);
    if( !p->cache )
    {
        trspk_d3d8_fixed_log("Init: trspk_resource_cache_create failed");
        d3d8_fixed_destroy_internal(p);
        r->opaque_internal = nullptr;
        free(r);
        return nullptr;
    }
    trspk_resource_cache_init_atlas(p->cache, TRSPK_ATLAS_DIMENSION, TRSPK_ATLAS_DIMENSION);
    d3d8_fixed_cache_refresh_atlas(p, p->device);

    p->batch_staging = trspk_batch16_create(65535u, 65535u, TRSPK_VERTEX_FORMAT_D3D8);
    if( !p->batch_staging )
    {
        trspk_d3d8_fixed_log("Init: trspk_batch16_create failed");
        d3d8_fixed_destroy_internal(p);
        r->opaque_internal = nullptr;
        free(r);
        return nullptr;
    }

    /* Pre-allocate pass-state buffers to avoid frame-time allocations. */
    p->pass_state.ib_scratch.reserve(65536);
    p->pass_state.subdraws.reserve(2048);
    p->pass_state.worlds.reserve(512);

    r->com_device = (uintptr_t)p->device;
    r->com_d3d = (uintptr_t)p->d3d;
    r->ready = true;
    r->device_lost = false;
    return r;
}

void
TRSPK_D3D8Fixed_Shutdown(TRSPK_D3D8FixedRenderer* renderer)
{
    if( !renderer )
        return;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    if( p )
        d3d8_fixed_destroy_internal(p);
    renderer->opaque_internal = nullptr;
    renderer->com_device = 0;
    renderer->com_d3d = 0;
    renderer->ready = false;
    free(renderer);
}

bool
TRSPK_D3D8Fixed_TryReset(TRSPK_D3D8FixedRenderer* renderer)
{
    if( !renderer )
        return false;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    if( !p || !p->device )
        return false;
    bool ok = d3d8_fixed_reset_device(renderer, p, (HWND)renderer->platform_hwnd);
    if( ok )
        renderer->device_lost = false;
    return ok;
}

void
TRSPK_D3D8Fixed_NotifyDeviceLost(TRSPK_D3D8FixedRenderer* renderer)
{
    if( renderer )
        renderer->device_lost = true;
}

void
TRSPK_D3D8Fixed_Resize(TRSPK_D3D8FixedRenderer* renderer, uint32_t width, uint32_t height)
{
    if( !renderer )
        return;
    renderer->width = width;
    renderer->height = height;
}

void
TRSPK_D3D8Fixed_SetWindowSize(TRSPK_D3D8FixedRenderer* renderer, uint32_t width, uint32_t height)
{
    if( !renderer )
        return;
    renderer->window_width = width;
    renderer->window_height = height;
}

void*
TRSPK_D3D8Fixed_GetDevice(TRSPK_D3D8FixedRenderer* renderer)
{
    if( !renderer )
        return nullptr;
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(renderer);
    return p ? (void*)p->device : nullptr;
}

} /* extern "C" */
