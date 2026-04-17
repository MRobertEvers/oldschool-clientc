#if defined(_WIN32)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* Include C game/dash headers before platform_impl2_win32*.h so buildcachedat.h does not
 * win the include race and pin dash/cache_dat to C++ linkage (breaks MinGW link vs .c objs). */
extern "C" {
#include "graphics/dash.h"
#include "osrs/game.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

#include "platform_impl2_win32_renderer_gdisoft3d.h"

#include "nuklear/torirs_nuklear.h"
extern "C" {
#include "nuklear/backends/rawfb/nuklear_rawfb.h"
struct nk_context* torirs_rawfb_get_nk_context(struct rawfb_context* rawfb);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
gdisoft3d_setup_bmi(struct Platform2_Win32_Renderer_GDISoft3D* renderer)
{
    BITMAPINFOHEADER* h = &renderer->bmi_header;
    memset(h, 0, sizeof(*h));
    h->biSize = sizeof(BITMAPINFOHEADER);
    h->biWidth = renderer->width;
    h->biHeight = -renderer->height; /* top-down */
    h->biPlanes = 1;
    h->biBitCount = 32;
    h->biCompression = BI_RGB;
}

struct Platform2_Win32_Renderer_GDISoft3D*
PlatformImpl2_Win32_Renderer_GDISoft3D_New(
    int width,
    int height,
    int max_width,
    int max_height)
{
    struct Platform2_Win32_Renderer_GDISoft3D* renderer =
        (struct Platform2_Win32_Renderer_GDISoft3D*)malloc(
            sizeof(struct Platform2_Win32_Renderer_GDISoft3D));
    memset(renderer, 0, sizeof(struct Platform2_Win32_Renderer_GDISoft3D));

    renderer->width = width;
    renderer->height = height;
    renderer->initial_width = width;
    renderer->initial_height = height;
    renderer->max_width = max_width;
    renderer->max_height = max_height;

    size_t const pixel_count = (size_t)width * (size_t)height;
    renderer->pixel_buffer = (int*)malloc(pixel_count * sizeof(int));
    if( !renderer->pixel_buffer )
    {
        printf("Failed to allocate pixel buffer\n");
        free(renderer);
        return NULL;
    }
    memset(renderer->pixel_buffer, 0, pixel_count * sizeof(int));

    renderer->dash_offset_x = 0;
    renderer->dash_offset_y = 0;

    /* First present must clear the bars; nothing has painted them yet. */
    renderer->letterbox_dirty = true;

    if( !QueryPerformanceFrequency(&renderer->nk_qpc_freq) )
        renderer->nk_qpc_freq.QuadPart = 0;

    gdisoft3d_setup_bmi(renderer);

    return renderer;
}

void
PlatformImpl2_Win32_Renderer_GDISoft3D_Free(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer)
{
    if( !renderer )
        return;
    if( renderer->nk_rawfb )
    {
        nk_rawfb_shutdown((struct rawfb_context*)renderer->nk_rawfb);
        renderer->nk_rawfb = NULL;
    }
    if( renderer->platform )
        renderer->platform->nk_ctx_for_input = NULL;
    free(renderer->nk_font_tex_mem);
    renderer->nk_font_tex_mem = NULL;
    free(renderer->pixel_buffer);
    free(renderer);
}

bool
PlatformImpl2_Win32_Renderer_GDISoft3D_Init(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
    struct Platform2_Win32* platform)
{
    if( !renderer || !platform )
        return false;
    renderer->platform = platform;

    static const int FONT_TEX_BYTES = 512 * 512;
    renderer->nk_font_tex_mem = (uint8_t*)malloc((size_t)FONT_TEX_BYTES);
    if( renderer->nk_font_tex_mem && renderer->pixel_buffer )
    {
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
            renderer->pixel_buffer,
            renderer->nk_font_tex_mem,
            (unsigned)renderer->width,
            (unsigned)renderer->height,
            (unsigned)(renderer->width * (int)sizeof(int)),
            pl);
        if( renderer->nk_rawfb )
        {
            struct nk_context* nk =
                torirs_rawfb_get_nk_context((struct rawfb_context*)renderer->nk_rawfb);
            platform->nk_ctx_for_input = (void*)nk;
            if( nk )
                nk_style_hide_cursor(nk);
        }
        else
        {
            free(renderer->nk_font_tex_mem);
            renderer->nk_font_tex_mem = NULL;
        }
    }
    return true;
}

void
PlatformImpl2_Win32_Renderer_GDISoft3D_SetDashOffset(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
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
PlatformImpl2_Win32_Renderer_GDISoft3D_SetDynamicPixelSize(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
    bool dynamic)
{
    if( renderer )
        renderer->pixel_size_dynamic = dynamic;
}

void
PlatformImpl2_Win32_Renderer_GDISoft3D_MarkLetterboxDirty(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer)
{
    if( renderer )
        renderer->letterbox_dirty = true;
}

void
PlatformImpl2_Win32_Renderer_GDISoft3D_SetViewportChangedCallback(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
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
PlatformImpl2_Win32_Renderer_GDISoft3D_PresentToHdc(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
    HDC hdc,
    int client_w,
    int client_h)
{
    if( !renderer || !renderer->pixel_buffer || !hdc || client_w <= 0 || client_h <= 0 )
        return;

    int window_width = client_w;
    int window_height = client_h;

    struct DstRect
    {
        int x, y, w, h;
    } dst_rect = { 0, 0, renderer->width, renderer->height };

    float src_aspect = (float)renderer->width / (float)renderer->height;
    float window_aspect = (float)window_width / (float)window_height;

    if( window_width > 0 && window_height > 0 )
    {
        if( src_aspect > window_aspect )
        {
            dst_rect.w = window_width;
            dst_rect.h = (int)(window_width / src_aspect);
            dst_rect.x = 0;
            dst_rect.y = (window_height - dst_rect.h) / 2;
        }
        else
        {
            dst_rect.h = window_height;
            dst_rect.w = (int)(window_height * src_aspect);
            dst_rect.y = 0;
            dst_rect.x = (window_width - dst_rect.w) / 2;
        }
    }

    renderer->present_dst_x = dst_rect.x;
    renderer->present_dst_y = dst_rect.y;
    renderer->present_dst_w = dst_rect.w;
    renderer->present_dst_h = dst_rect.h;

    /* Letterbox/pillarbox: client area outside dst_rect is not covered by StretchDIBits;
     * WM_ERASEBKGND returns TRUE so the bars stay put across frames. Only re-clear when
     * something (typically WM_SIZE) marked them dirty. */
    if( renderer->letterbox_dirty )
    {
        HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RECT rc;

        if( dst_rect.x > 0 )
        {
            rc.left = 0;
            rc.top = 0;
            rc.right = dst_rect.x;
            rc.bottom = client_h;
            FillRect(hdc, &rc, black);
        }
        if( dst_rect.x + dst_rect.w < client_w )
        {
            rc.left = dst_rect.x + dst_rect.w;
            rc.top = 0;
            rc.right = client_w;
            rc.bottom = client_h;
            FillRect(hdc, &rc, black);
        }
        if( dst_rect.y > 0 )
        {
            rc.left = dst_rect.x;
            rc.top = 0;
            rc.right = dst_rect.x + dst_rect.w;
            rc.bottom = dst_rect.y;
            FillRect(hdc, &rc, black);
        }
        if( dst_rect.y + dst_rect.h < client_h )
        {
            rc.left = dst_rect.x;
            rc.top = dst_rect.y + dst_rect.h;
            rc.right = dst_rect.x + dst_rect.w;
            rc.bottom = client_h;
            FillRect(hdc, &rc, black);
        }

        renderer->letterbox_dirty = false;
    }

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader = renderer->bmi_header;

    StretchDIBits(
        hdc,
        dst_rect.x,
        dst_rect.y,
        dst_rect.w,
        dst_rect.h,
        0,
        0,
        renderer->width,
        renderer->height,
        renderer->pixel_buffer,
        &bi,
        DIB_RGB_COLORS,
        SRCCOPY);
}

void
PlatformImpl2_Win32_Renderer_GDISoft3D_Render(
    struct Platform2_Win32_Renderer_GDISoft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !game || !render_command_buffer || !renderer->pixel_buffer )
        return;

    game->viewport_offset_x = renderer->dash_offset_x;
    game->viewport_offset_y = renderer->dash_offset_y;

    int window_width = 0;
    int window_height = 0;
    if( renderer->platform && renderer->platform->hwnd )
    {
        RECT cr;
        if( GetClientRect((HWND)renderer->platform->hwnd, &cr) )
        {
            window_width = cr.right - cr.left;
            window_height = cr.bottom - cr.top;
        }
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

    struct ToriRSRenderCommand command = { 0 };

    int* vp_pixels = NULL;
    if( game->view_port && renderer->pixel_buffer )
    {
        vp_pixels = &renderer->pixel_buffer
                         [renderer->dash_offset_y * renderer->width + renderer->dash_offset_x];
    }

    LibToriRS_FrameBegin(game, render_command_buffer);
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &command, true) )
    {
        switch( command.kind )
        {
        case TORIRS_GFX_NONE:
            break;
        case TORIRS_GFX_FONT_LOAD:
        case TORIRS_GFX_MODEL_LOAD:
        case TORIRS_GFX_MODEL_UNLOAD:
            break;
        case TORIRS_GFX_SPRITE_LOAD:
        case TORIRS_GFX_SPRITE_UNLOAD:
            break;
        case TORIRS_GFX_TEXTURE_LOAD:
        {
            struct DashTexture* texture = command._texture_load.texture_nullable;
            if( game->sys_dash && texture && texture->texels )
                dash3d_add_texture(game->sys_dash, command._texture_load.texture_id, texture);
        }
        break;
        case TORIRS_GFX_FONT_DRAW:
        {
            struct DashPixFont* f = command._font_draw.font;
            const uint8_t* text = command._font_draw.text;
            if( !f || !text || !renderer->pixel_buffer )
                break;
            const int ox = renderer->dash_offset_x;
            const int oy = renderer->dash_offset_y;
            const int fx = command._font_draw.x + ox;
            const int fy = command._font_draw.y + oy;
            int cl = 0;
            int ct = 0;
            int cr = renderer->width;
            int cb = renderer->height;
            if( game->view_port )
            {
                cl = ox;
                ct = oy;
                cr = ox + game->view_port->width;
                cb = oy + game->view_port->height;
                if( cl < 0 )
                    cl = 0;
                if( ct < 0 )
                    ct = 0;
                if( cr > renderer->width )
                    cr = renderer->width;
                if( cb > renderer->height )
                    cb = renderer->height;
            }
            if( cl < cr && ct < cb )
            {
                dashfont_draw_text_ex_clipped(
                    f,
                    (uint8_t*)text,
                    fx,
                    fy,
                    command._font_draw.color_rgb,
                    renderer->pixel_buffer,
                    renderer->width,
                    cl,
                    ct,
                    cr,
                    cb);
            }
        }
        break;
        case TORIRS_GFX_CLEAR_RECT:
        {
            int cx = command._clear_rect.x;
            int cy = command._clear_rect.y;
            int cw = command._clear_rect.w;
            int ch = command._clear_rect.h;
            int* pb = renderer->pixel_buffer;
            if( cw <= 0 || ch <= 0 || !pb )
                break;
            int rw = renderer->width;
            int rh = renderer->height;
            int x0 = cx < 0 ? 0 : cx;
            int y0 = cy < 0 ? 0 : cy;
            int x1 = cx + cw > rw ? rw : cx + cw;
            int y1 = cy + ch > rh ? rh : cy + ch;
            if( x0 >= x1 || y0 >= y1 )
                break;
            for( int row = y0; row < y1; ++row )
                memset(&pb[row * rw + x0], 0, (size_t)(x1 - x0) * sizeof(int));
        }
        break;
        case TORIRS_GFX_MODEL_DRAW:
            if( vp_pixels )
                dash3d_raster_projected_model(
                    game->sys_dash,
                    command._model_draw.model,
                    &command._model_draw.position,
                    game->view_port,
                    game->camera,
                    vp_pixels,
                    false);
            break;
        case TORIRS_GFX_SPRITE_DRAW:
        {
            struct DashSprite* sp = command._sprite_draw.sprite;
            if( !sp || !sp->pixels_argb )
                break;
            int rot = command._sprite_draw.rotation_r2pi2048;
            int srw = command._sprite_draw.src_bb_w;
            int srh = command._sprite_draw.src_bb_h;
            if( srw <= 0 )
                srw = sp->width;
            if( srh <= 0 )
                srh = sp->height;

            if( !game->sys_dash || !game->iface_view_port || !renderer->pixel_buffer )
                break;
            if( command._sprite_draw.rotated )
            {
                dash2d_blit_rotated_ex(
                    (int*)sp->pixels_argb,
                    sp->width,
                    command._sprite_draw.src_bb_x,
                    command._sprite_draw.src_bb_y,
                    srw,
                    srh,
                    command._sprite_draw.src_anchor_x,
                    command._sprite_draw.src_anchor_y,
                    renderer->pixel_buffer,
                    renderer->width,
                    renderer->height,
                    command._sprite_draw.dst_bb_x,
                    command._sprite_draw.dst_bb_y,
                    command._sprite_draw.dst_bb_w,
                    command._sprite_draw.dst_bb_h,
                    command._sprite_draw.dst_anchor_x,
                    command._sprite_draw.dst_anchor_y,
                    rot);
            }
            else
            {
                dash2d_blit_sprite(
                    game->sys_dash,
                    sp,
                    game->iface_view_port,
                    command._sprite_draw.dst_bb_x,
                    command._sprite_draw.dst_bb_y,
                    renderer->pixel_buffer);
            }
        }
        break;
        case TORIRS_GFX_BEGIN_3D:
        case TORIRS_GFX_END_3D:
        case TORIRS_GFX_BEGIN_2D:
        case TORIRS_GFX_END_2D:
        case TORIRS_GFX_VERTEX_ARRAY_LOAD:
        case TORIRS_GFX_VERTEX_ARRAY_UNLOAD:
        case TORIRS_GFX_FACE_ARRAY_LOAD:
        case TORIRS_GFX_FACE_ARRAY_UNLOAD:
            break;
        default:
            break;
        }
    }
    LibToriRS_FrameEnd(game);

    if( renderer->nk_rawfb )
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
        double fps =
            renderer->nk_dt_smoothed > 1e-9 ? 1.0 / renderer->nk_dt_smoothed : 0.0;

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
            nk_labelf(nk, NK_TEXT_LEFT, "Paint cmd: %d", game->cc);
        }
        nk_end(nk);
        nk_rawfb_render(rawfb, nk_rgba(0, 0, 0, 0), 0);
    }

    game->soft3d_mouse_from_window = true;

    if( renderer->platform && renderer->platform->hwnd )
    {
        HWND hwnd = (HWND)renderer->platform->hwnd;
        HDC hdc = GetDC(hwnd);
        if( hdc )
        {
            RECT cr;
            GetClientRect(hwnd, &cr);
            int cw = cr.right - cr.left;
            int ch = cr.bottom - cr.top;
            PlatformImpl2_Win32_Renderer_GDISoft3D_PresentToHdc(renderer, hdc, cw, ch);
            ReleaseDC(hwnd, hdc);
        }
    }

    game->soft3d_present_dst_x = renderer->present_dst_x;
    game->soft3d_present_dst_y = renderer->present_dst_y;
    game->soft3d_present_dst_w = renderer->present_dst_w;
    game->soft3d_present_dst_h = renderer->present_dst_h;
    game->soft3d_buffer_w = renderer->width;
    game->soft3d_buffer_h = renderer->height;
}

#endif /* _WIN32 */
