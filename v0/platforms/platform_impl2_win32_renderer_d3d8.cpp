#if defined(_WIN32) && defined(TORIRS_HAS_D3D8)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d8.h>
#include <windows.h>

extern "C" {
#include "graphics/dash.h"
#include "osrs/game.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

#include "nuklear/torirs_nuklear.h"
#include "platform_impl2_win32_renderer_d3d8.h"
extern "C" {
#include "nuklear/backends/rawfb/nuklear_rawfb.h"
struct nk_context*
torirs_rawfb_get_nk_context(struct rawfb_context* rawfb);
}

namespace
{

static void
d3d8_log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[d3d8] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

struct D3D8UiVertex
{
    float x, y, z, rhw;
    DWORD diffuse;
    float u, v;
};

static const DWORD kFvfUi = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

/** Match legacy d3d8_old: overlay on so FPS/debug panel matches working renderer. */
static const bool kSkipNuklearOverlayQuad = false;

static void
compute_letterbox_dst(int buf_w, int buf_h, int window_w, int window_h, RECT* out_dst)
{
    out_dst->left = 0;
    out_dst->top = 0;
    out_dst->right = buf_w;
    out_dst->bottom = buf_h;
    if( window_w <= 0 || window_h <= 0 )
        return;
    float src_aspect = (float)buf_w / (float)buf_h;
    float window_aspect = (float)window_w / (float)window_h;
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

static void
d3d8_release_nk_overlay_tex(void** tex)
{
    if( tex && *tex )
    {
        ((IDirect3DTexture8*)(*tex))->Release();
        *tex = nullptr;
    }
}

static bool
d3d8_recreate_nk_overlay_texture(
    struct Platform2_Win32_Renderer_D3D8* ren,
    IDirect3DDevice8* dev)
{
    if( !ren || !dev || ren->width <= 0 || ren->height <= 0 )
        return false;
    d3d8_release_nk_overlay_tex(&ren->nk_overlay_tex);
    IDirect3DTexture8* tex = nullptr;
    /* Legacy d3d8_reset_device nk_overlay: DYNAMIC + DEFAULT pool. */
    HRESULT hr = dev->CreateTexture(
        (UINT)ren->width,
        (UINT)ren->height,
        1,
        D3DUSAGE_DYNAMIC,
        D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,
        &tex);
    if( FAILED(hr) || !tex )
    {
        d3d8_log(
            "nk overlay CreateTexture failed hr=0x%08lX %dx%d",
            (unsigned long)hr,
            ren->width,
            ren->height);
        return false;
    }
    ren->nk_overlay_tex = tex;
    return true;
}

/** Fixed-function UI pass for Nuklear quad (world shaders may be bound earlier in the frame). */
static void
d3d8_apply_ui_pass_for_nk(IDirect3DDevice8* dev, unsigned rw, unsigned rh)
{
    dev->SetPixelShader(0);
    dev->SetVertexShader(kFvfUi);
    D3DVIEWPORT8 vp = { 0, 0, rw, rh, 0.0f, 1.0f };
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
}

static bool
d3d8_init_or_resize_nk_rawfb(struct Platform2_Win32_Renderer_D3D8* renderer)
{
    if( renderer->nk_rawfb )
    {
        nk_rawfb_shutdown((struct rawfb_context*)renderer->nk_rawfb);
        renderer->nk_rawfb = nullptr;
    }
    if( !renderer->nk_font_tex_mem || !renderer->nk_pixel_buffer || renderer->width <= 0 ||
        renderer->height <= 0 )
        return false;

    struct rawfb_pl pl;
    pl.bytesPerPixel = 4;
    pl.rshift = 16;
    pl.gshift = 8;
    pl.bshift = 0;
    pl.ashift = 24;
    pl.rloss = 0;
    pl.gloss = 0;
    pl.bloss = 0;
    pl.aloss = 0;
    renderer->nk_rawfb = nk_rawfb_init(
        renderer->nk_pixel_buffer,
        renderer->nk_font_tex_mem,
        (unsigned)renderer->width,
        (unsigned)renderer->height,
        (unsigned)(renderer->width * (int)sizeof(int)),
        pl);
    if( renderer->nk_rawfb && renderer->platform )
    {
        struct nk_context* nk =
            torirs_rawfb_get_nk_context((struct rawfb_context*)renderer->nk_rawfb);
        renderer->platform->nk_ctx_for_input = (void*)nk;
        if( nk )
            nk_style_hide_cursor(nk);
    }
    return renderer->nk_rawfb != nullptr;
}

} /* namespace */

struct Platform2_Win32_Renderer_D3D8*
PlatformImpl2_Win32_Renderer_D3D8_New(
    int width,
    int height,
    int max_width,
    int max_height)
{
    d3d8_log(
        "D3D8_New: enter width=%d height=%d max_width=%d max_height=%d",
        width,
        height,
        max_width,
        max_height);
    struct Platform2_Win32_Renderer_D3D8* r =
        (struct Platform2_Win32_Renderer_D3D8*)calloc(1, sizeof(struct Platform2_Win32_Renderer_D3D8));
    if( !r )
    {
        d3d8_log("D3D8_New: calloc(renderer) failed");
        return nullptr;
    }
    r->width = width;
    r->height = height;
    r->initial_width = width;
    r->initial_height = height;
    r->max_width = max_width;
    r->max_height = max_height;
    r->dash_offset_x = 0;
    r->dash_offset_y = 0;
    if( !QueryPerformanceFrequency(&r->nk_qpc_freq) )
        r->nk_qpc_freq.QuadPart = 0;

    size_t const pc = (size_t)width * (size_t)height;
    r->nk_pixel_buffer = (int*)malloc(pc * sizeof(int));
    if( !r->nk_pixel_buffer )
    {
        d3d8_log("D3D8_New: malloc(nk_pixel_buffer) failed (bytes=%zu)", pc * sizeof(int));
        free(r);
        return nullptr;
    }
    memset(r->nk_pixel_buffer, 0, pc * sizeof(int));

    d3d8_log("D3D8_New: ok renderer=%p", (void*)r);
    return r;
}

void
PlatformImpl2_Win32_Renderer_D3D8_Free(struct Platform2_Win32_Renderer_D3D8* renderer)
{
    if( !renderer )
        return;
    if( renderer->nk_rawfb )
    {
        nk_rawfb_shutdown((struct rawfb_context*)renderer->nk_rawfb);
        renderer->nk_rawfb = nullptr;
    }
    if( renderer->platform )
        renderer->platform->nk_ctx_for_input = nullptr;
    free(renderer->nk_font_tex_mem);
    renderer->nk_font_tex_mem = nullptr;
    d3d8_release_nk_overlay_tex(&renderer->nk_overlay_tex);
    if( renderer->trspk )
    {
        TRSPK_D3D8Fixed_Shutdown(renderer->trspk);
        renderer->trspk = nullptr;
    }
    free(renderer->nk_pixel_buffer);
    free(renderer);
}

bool
PlatformImpl2_Win32_Renderer_D3D8_Init(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    struct Platform2_Win32* platform)
{
    d3d8_log("D3D8_Init: enter renderer=%p platform=%p", (void*)renderer, (void*)platform);
    if( !renderer || !platform || !platform->hwnd )
    {
        d3d8_log("D3D8_Init: abort missing renderer/platform/hwnd");
        return false;
    }
    renderer->platform = platform;
    platform->skip_mouse_transform = 1;

    renderer->trspk = TRSPK_D3D8Fixed_Init(
        (TRSPK_D3D8_WindowHandle)platform->hwnd,
        (uint32_t)renderer->width,
        (uint32_t)renderer->height);
    if( !renderer->trspk || !renderer->trspk->ready )
    {
        d3d8_log("D3D8_Init: TRSPK_D3D8Fixed_Init failed");
        renderer->trspk = nullptr;
        return false;
    }

    renderer->width = (int)renderer->trspk->width;
    renderer->height = (int)renderer->trspk->height;

    size_t const pc = (size_t)renderer->width * (size_t)renderer->height;
    int* npix = (int*)realloc(renderer->nk_pixel_buffer, pc * sizeof(int));
    if( !npix )
    {
        d3d8_log("D3D8_Init: realloc(nk_pixel_buffer) failed");
        TRSPK_D3D8Fixed_Shutdown(renderer->trspk);
        renderer->trspk = nullptr;
        return false;
    }
    renderer->nk_pixel_buffer = npix;
    memset(renderer->nk_pixel_buffer, 0, pc * sizeof(int));

    static const int FONT_TEX_BYTES = 512 * 512;
    renderer->nk_font_tex_mem = (uint8_t*)malloc((size_t)FONT_TEX_BYTES);
    if( renderer->nk_font_tex_mem && renderer->nk_pixel_buffer )
    {
        if( !d3d8_init_or_resize_nk_rawfb(renderer) )
            d3d8_log("D3D8_Init: nk_rawfb_init failed");
        if( !renderer->nk_rawfb )
        {
            free(renderer->nk_font_tex_mem);
            renderer->nk_font_tex_mem = nullptr;
        }
    }

    IDirect3DDevice8* dev = (IDirect3DDevice8*)TRSPK_D3D8Fixed_GetDevice(renderer->trspk);
    if( dev && !d3d8_recreate_nk_overlay_texture(renderer, dev) )
        d3d8_log("D3D8_Init: nk overlay texture unavailable");

    RECT icr;
    if( GetClientRect((HWND)platform->hwnd, &icr) )
    {
        const int iw = icr.right - icr.left;
        const int ih = icr.bottom - icr.top;
        if( iw > 0 && ih > 0 )
        {
            renderer->d3d8_client_w = (uint32_t)iw;
            renderer->d3d8_client_h = (uint32_t)ih;
        }
    }

    d3d8_log("D3D8_Init: complete ok trspk=%p", (void*)renderer->trspk);
    return true;
}

void
PlatformImpl2_Win32_Renderer_D3D8_SetDashOffset(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    int offset_x,
    int offset_y)
{
    if( renderer )
    {
        renderer->dash_offset_x = offset_x;
        renderer->dash_offset_y = offset_y;
    }
}

void
PlatformImpl2_Win32_Renderer_D3D8_SetDynamicPixelSize(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    bool dynamic)
{
    if( renderer )
        renderer->pixel_size_dynamic = dynamic;
}

void
PlatformImpl2_Win32_Renderer_D3D8_MarkResizeDirty(struct Platform2_Win32_Renderer_D3D8* renderer)
{
    if( renderer )
        renderer->resize_dirty = true;
}

void
PlatformImpl2_Win32_Renderer_D3D8_SetViewportChangedCallback(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    void (*callback)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata),
    void* userdata)
{
    if( !renderer )
        return;
    renderer->on_viewport_changed = callback;
    renderer->on_viewport_changed_userdata = userdata;
}

void
PlatformImpl2_Win32_Renderer_D3D8_PresentOrInvalidate(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    HDC hdc,
    int client_w,
    int client_h)
{
    (void)hdc;
    if( !renderer || !renderer->trspk )
        return;
    if( client_w > 0 && client_h > 0 )
    {
        if( (uint32_t)client_w != renderer->d3d8_client_w ||
            (uint32_t)client_h != renderer->d3d8_client_h )
            renderer->resize_dirty = true;
    }
}

void
PlatformImpl2_Win32_Renderer_D3D8_Render(
    struct Platform2_Win32_Renderer_D3D8* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->trspk || !game || !render_command_buffer || !renderer->platform ||
        !renderer->platform->hwnd )
        return;

    HWND hwnd = (HWND)renderer->platform->hwnd;

    game->viewport_offset_x = renderer->dash_offset_x;
    game->viewport_offset_y = renderer->dash_offset_y;

    int window_width = 0;
    int window_height = 0;
    RECT cr;
    if( GetClientRect(hwnd, &cr) )
    {
        window_width = cr.right - cr.left;
        window_height = cr.bottom - cr.top;
    }
    if( window_width <= 0 || window_height <= 0 )
    {
        window_width = renderer->width;
        window_height = renderer->height;
    }

    int new_width = window_width > renderer->max_width ? renderer->max_width : window_width;
    int new_height = window_height > renderer->max_height ? renderer->max_height : window_height;

    if( renderer->initial_view_port_width == 0 && game->view_port && game->view_port->width > 0 )
    {
        renderer->initial_view_port_width = game->view_port->width;
        renderer->initial_view_port_height = game->view_port->height;
    }

    if( game->iface_view_port )
    {
        game->iface_view_port->width = renderer->width;
        game->iface_view_port->height = renderer->height;
        game->iface_view_port->x_center = renderer->width / 2;
        game->iface_view_port->y_center = renderer->height / 2;
        game->iface_view_port->stride = renderer->width;
        game->iface_view_port->clip_left = 0;
        game->iface_view_port->clip_top = 0;
        game->iface_view_port->clip_right = renderer->width;
        game->iface_view_port->clip_bottom = renderer->height;
    }

    if( game->view_port && renderer->initial_view_port_width > 0 && renderer->initial_width > 0 )
    {
        if( renderer->pixel_size_dynamic )
        {
            float scale_x = (float)new_width / (float)renderer->initial_width;
            float scale_y = (float)new_height / (float)renderer->initial_height;
            float scale = scale_x < scale_y ? scale_x : scale_y;
            int new_vp_w = (int)(renderer->initial_view_port_width * scale);
            int new_vp_h = (int)(renderer->initial_view_port_height * scale);
            if( new_vp_w < 1 )
                new_vp_w = 1;
            if( new_vp_h < 1 )
                new_vp_h = 1;
            if( new_vp_w > renderer->max_width )
                new_vp_w = renderer->max_width;
            if( new_vp_h > renderer->max_height )
                new_vp_h = renderer->max_height;

            if( new_vp_w != game->view_port->width || new_vp_h != game->view_port->height )
            {
                LibToriRS_GameSetWorldViewportSize(game, new_vp_w, new_vp_h);
                if( renderer->on_viewport_changed )
                {
                    renderer->on_viewport_changed(
                        game, new_vp_w, new_vp_h, renderer->on_viewport_changed_userdata);
                }
            }
        }
        else
        {
            if( game->view_port->width != renderer->initial_view_port_width ||
                game->view_port->height != renderer->initial_view_port_height )
            {
                LibToriRS_GameSetWorldViewportSize(
                    game, renderer->initial_view_port_width, renderer->initial_view_port_height);
            }
        }
    }

    if( game->view_port )
    {
        game->view_port->x_center = game->view_port->width / 2;
        game->view_port->y_center = game->view_port->height / 2;
        game->view_port->stride = renderer->width;
    }

    IDirect3DDevice8* dev = (IDirect3DDevice8*)TRSPK_D3D8Fixed_GetDevice(renderer->trspk);
    if( !dev )
        return;

    /* Legacy: internal scene buffer (renderer->width/height) is fixed; only swap chain + GPU
     * surfaces follow the HWND client area via d3d8_reset_device / TRSPK_D3D8Fixed_TryReset. */
    const uint32_t cw = window_width > 0 ? (uint32_t)window_width : 0u;
    const uint32_t ch = window_height > 0 ? (uint32_t)window_height : 0u;
    if( renderer->resize_dirty ||
        (cw > 0u && ch > 0u &&
         (cw != renderer->d3d8_client_w || ch != renderer->d3d8_client_h)) )
    {
        if( TRSPK_D3D8Fixed_TryReset(renderer->trspk) )
        {
            if( cw > 0u && ch > 0u )
            {
                renderer->d3d8_client_w = cw;
                renderer->d3d8_client_h = ch;
            }
            if( renderer->nk_font_tex_mem )
                d3d8_init_or_resize_nk_rawfb(renderer);
            d3d8_recreate_nk_overlay_texture(renderer, dev);
        }
        renderer->resize_dirty = false;
    }

    HRESULT coop = dev->TestCooperativeLevel();
    if( coop == D3DERR_DEVICELOST )
        return;
    if( coop == D3DERR_DEVICENOTRESET )
    {
        if( !TRSPK_D3D8Fixed_TryReset(renderer->trspk) )
            return;
        dev = (IDirect3DDevice8*)TRSPK_D3D8Fixed_GetDevice(renderer->trspk);
        if( !dev )
            return;
        d3d8_recreate_nk_overlay_texture(renderer, dev);
    }

    /* Logical viewport / letterbox: client pixels (win_w x win_h); scene RT is renderer size. */
    const int trspk_win_w = window_width > 0 ? window_width : renderer->width;
    const int trspk_win_h = window_height > 0 ? window_height : renderer->height;
    if( trspk_win_w > 0 && trspk_win_h > 0 )
        TRSPK_D3D8Fixed_SetWindowSize(renderer->trspk, (uint32_t)trspk_win_w, (uint32_t)trspk_win_h);

    renderer->trspk->dash_offset_x = renderer->dash_offset_x;
    renderer->trspk->dash_offset_y = renderer->dash_offset_y;

    int frame_vp_w = game && game->view_port ? game->view_port->width : renderer->width;
    int frame_vp_h = game && game->view_port ? game->view_port->height : renderer->height;
    if( frame_vp_w <= 0 )
        frame_vp_w = renderer->width;
    if( frame_vp_h <= 0 )
        frame_vp_h = renderer->height;

    TRSPK_D3D8Fixed_FrameBegin(renderer->trspk, frame_vp_w, frame_vp_h, game);
    LibToriRS_FrameBegin(game, render_command_buffer);

    TRSPK_D3D8Fixed_EventContext ev = {};
    ev.renderer = renderer->trspk;
    ev.game = game;
    ev.platform_renderer = renderer;
    ev.scene_width = renderer->width;
    ev.scene_height = renderer->height;
    ev.window_width = window_width;
    ev.window_height = window_height;
    ev.u_clock = renderer->trspk->frame_u_clock;
    ev.frame_vp_w = renderer->trspk->frame_vp_w;
    ev.frame_vp_h = renderer->trspk->frame_vp_h;

    struct ToriRSRenderCommand cmd = {};
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
    {
        switch( cmd.kind )
        {
        case TORIRS_GFX_RES_MODEL_LOAD:
            trspk_d3d8_fixed_event_res_model_load(&ev, &cmd);
            break;
        case TORIRS_GFX_RES_MODEL_UNLOAD:
            trspk_d3d8_fixed_event_res_model_unload(&ev, &cmd);
            break;
        case TORIRS_GFX_RES_TEX_LOAD:
            trspk_d3d8_fixed_event_tex_load(&ev, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_BEGIN:
            trspk_d3d8_fixed_event_batch3d_begin(&ev, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_MODEL_ADD:
            trspk_d3d8_fixed_event_batch3d_model_add(&ev, &cmd);
            break;
        case TORIRS_GFX_RES_ANIM_LOAD:
            if( cmd._animation_load.usage_hint == TORIRS_USAGE_SCENERY )
                trspk_d3d8_fixed_event_batch3d_anim_add(&ev, &cmd);
            else
                trspk_d3d8_fixed_event_res_anim_load(&ev, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_ANIM_ADD:
            trspk_d3d8_fixed_event_batch3d_anim_add(&ev, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_END:
            trspk_d3d8_fixed_event_batch3d_end(&ev, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_CLEAR:
            trspk_d3d8_fixed_event_batch3d_clear(&ev, &cmd);
            break;
        case TORIRS_GFX_DRAW_MODEL:
            trspk_d3d8_fixed_event_draw_model(&ev, &cmd);
            break;
        case TORIRS_GFX_STATE_BEGIN_3D:
            trspk_d3d8_fixed_event_state_begin_3d(&ev, &cmd);
            break;
        case TORIRS_GFX_STATE_END_3D:
            trspk_d3d8_fixed_event_state_end_3d(&ev, &cmd);
            break;
        case TORIRS_GFX_STATE_BEGIN_2D:
            trspk_d3d8_fixed_event_state_begin_2d(&ev, &cmd);
            break;
        case TORIRS_GFX_STATE_END_2D:
            trspk_d3d8_fixed_event_state_end_2d(&ev, &cmd);
            break;
        case TORIRS_GFX_STATE_CLEAR_RECT:
            trspk_d3d8_fixed_event_state_clear_rect(&ev, &cmd);
            break;
        case TORIRS_GFX_RES_SPRITE_LOAD:
            trspk_d3d8_fixed_event_sprite_load(&ev, &cmd);
            break;
        case TORIRS_GFX_RES_SPRITE_UNLOAD:
            trspk_d3d8_fixed_event_sprite_unload(&ev, &cmd);
            break;
        case TORIRS_GFX_DRAW_SPRITE:
            trspk_d3d8_fixed_event_draw_sprite(&ev, &cmd);
            break;
        case TORIRS_GFX_RES_FONT_LOAD:
            trspk_d3d8_fixed_event_font_load(&ev, &cmd);
            break;
        case TORIRS_GFX_DRAW_FONT:
            trspk_d3d8_fixed_event_draw_font(&ev, &cmd);
            break;
        case TORIRS_GFX_NONE:
            trspk_d3d8_fixed_event_none_or_default(&ev, &cmd);
            break;
        default:
            trspk_d3d8_fixed_event_none_or_default(&ev, &cmd);
            break;
        }
    }

    LibToriRS_FrameEnd(game);

    TRSPK_D3D8Fixed_FlushFont(renderer->trspk);

    if( !kSkipNuklearOverlayQuad && renderer->nk_rawfb && renderer->nk_overlay_tex )
    {
        struct rawfb_context* rawfb = (struct rawfb_context*)renderer->nk_rawfb;
        LARGE_INTEGER now_qpc;
        QueryPerformanceCounter(&now_qpc);
        double dt = 1.0 / 60.0;
        if( renderer->nk_prev_qpc_valid && renderer->nk_qpc_freq.QuadPart > 0 )
        {
            LONGLONG ticks = now_qpc.QuadPart - renderer->nk_prev_qpc.QuadPart;
            dt = (double)ticks / (double)renderer->nk_qpc_freq.QuadPart;
            if( dt < 1e-6 )
                dt = 1e-6;
        }
        renderer->nk_prev_qpc = now_qpc;
        renderer->nk_prev_qpc_valid = 1;

        if( renderer->nk_dt_smoothed <= 0.0 )
            renderer->nk_dt_smoothed = dt;
        else
            renderer->nk_dt_smoothed += 0.1 * (dt - renderer->nk_dt_smoothed);

        double dt_ms = renderer->nk_dt_smoothed * 1000.0;
        double fps = renderer->nk_dt_smoothed > 1e-9 ? 1.0 / renderer->nk_dt_smoothed : 0.0;

        struct nk_context* nk = torirs_rawfb_get_nk_context(rawfb);
        if( nk_begin(
                nk,
                "Info",
                nk_rect(10, 10, 280, 200),
                NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE) )
        {
            nk_layout_row_dynamic(nk, 18, 1);
            nk_labelf(nk, NK_TEXT_LEFT, "%7.2f ms (%6.1f FPS)", dt_ms, fps);
            nk_labelf(nk, NK_TEXT_LEFT, "Buffer: %dx%d", renderer->width, renderer->height);
            nk_labelf(nk, NK_TEXT_LEFT, "Window: %dx%d", window_width, window_height);
            if( game->view_port )
            {
                nk_labelf(
                    nk,
                    NK_TEXT_LEFT,
                    "Viewport: %dx%d",
                    game->view_port->width,
                    game->view_port->height);
            }
            nk_labelf(
                nk,
                NK_TEXT_LEFT,
                "D3D8-fixed GPU tris: %u draws:%u",
                renderer->trspk->diag_frame_pass_subdraws,
                renderer->trspk->diag_frame_submitted_draws);
            nk_labelf(nk, NK_TEXT_LEFT, "Paint cmd: %d", game->cc);
        }
        nk_end(nk);
        nk_rawfb_render(rawfb, nk_rgba(0, 0, 0, 0), 1);

        IDirect3DTexture8* dtex = (IDirect3DTexture8*)renderer->nk_overlay_tex;
        D3DLOCKED_RECT lr;
        HRESULT hr_nk = dtex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD);
        if( hr_nk == D3D_OK )
        {
            const int w = renderer->width;
            const int h = renderer->height;
            const size_t row_bytes = (size_t)w * sizeof(int);
            if( lr.Pitch == (LONG)row_bytes )
                memcpy(lr.pBits, renderer->nk_pixel_buffer, row_bytes * (size_t)h);
            else
            {
                for( int row = 0; row < h; ++row )
                    memcpy(
                        (unsigned char*)lr.pBits + row * lr.Pitch,
                        renderer->nk_pixel_buffer + row * w,
                        row_bytes);
            }
            dtex->UnlockRect(0);
        }
        else if( FAILED(hr_nk) )
            d3d8_log("nk_overlay LockRect failed hr=0x%08lX", (unsigned long)hr_nk);

        d3d8_apply_ui_pass_for_nk(dev, (unsigned)renderer->width, (unsigned)renderer->height);
        dev->SetTexture(0, dtex);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        float rw = (float)renderer->width;
        float rh = (float)renderer->height;
        DWORD amb = 0xFFFFFFFF;
        D3D8UiVertex q[6] = {
            { 0.0f, 0.0f, 0.0f, 1.0f, amb, 0.0f, 0.0f },
            { rw,   0.0f, 0.0f, 1.0f, amb, 1.0f, 0.0f },
            { rw,   rh,   0.0f, 1.0f, amb, 1.0f, 1.0f },
            { 0.0f, 0.0f, 0.0f, 1.0f, amb, 0.0f, 0.0f },
            { rw,   rh,   0.0f, 1.0f, amb, 1.0f, 1.0f },
            { 0.0f, rh,   0.0f, 1.0f, amb, 0.0f, 1.0f },
        };
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q, sizeof(D3D8UiVertex));
    }

    TRSPK_D3D8Fixed_FrameEnd(renderer->trspk);
    TRSPK_D3D8Fixed_Present(renderer->trspk, window_width, window_height, renderer);

    game->soft3d_mouse_from_window = true;
    game->soft3d_present_dst_x = renderer->present_dst_x;
    game->soft3d_present_dst_y = renderer->present_dst_y;
    game->soft3d_present_dst_w = renderer->present_dst_w;
    game->soft3d_present_dst_h = renderer->present_dst_h;
    game->soft3d_buffer_w = renderer->width;
    game->soft3d_buffer_h = renderer->height;
}

#endif /* _WIN32 && TORIRS_HAS_D3D8 */
