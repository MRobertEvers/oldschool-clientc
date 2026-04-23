#include "platform_impl2_emscripten_native_renderer_soft3d.h"

extern "C" {
#include "graphics/dash.h"
#include "osrs/game.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EM_JS(
    void,
    emscripten_native_canvas_put_rgba,
    (const uint8_t* rgba_ptr,
     int w,
     int h,
     int dst_x,
     int dst_y),
    {
        var canvas = document.getElementById('canvas');
        if( !canvas || !rgba_ptr || w <= 0 || h <= 0 )
            return;
        var ctx = canvas.getContext('2d');
        if( !ctx )
            return;
        var len = w * h * 4;
        var copy = new Uint8ClampedArray(Module.HEAPU8.buffer, rgba_ptr, len);
        var data = new Uint8ClampedArray(copy);
        var imageData = new ImageData(data, w, h);
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        ctx.putImageData(imageData, dst_x, dst_y);
    });

static void
argb_buffer_to_rgba_bytes(
    const int* src,
    int pixel_count,
    uint8_t* dst_rgba)
{
    for( int i = 0; i < pixel_count; i++ )
    {
        uint32_t p = (uint32_t)src[i];
        dst_rgba[i * 4 + 0] = (uint8_t)((p >> 16) & 0xffu);
        dst_rgba[i * 4 + 1] = (uint8_t)((p >> 8) & 0xffu);
        dst_rgba[i * 4 + 2] = (uint8_t)(p & 0xffu);
        dst_rgba[i * 4 + 3] = (uint8_t)((p >> 24) & 0xffu);
    }
}

struct Platform2_Emscripten_Native_Renderer_Soft3D*
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_New(
    int width,
    int height,
    int max_width,
    int max_height)
{
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer =
        (struct Platform2_Emscripten_Native_Renderer_Soft3D*)malloc(
            sizeof(struct Platform2_Emscripten_Native_Renderer_Soft3D));
    memset(renderer, 0, sizeof(struct Platform2_Emscripten_Native_Renderer_Soft3D));

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

    renderer->rgba_buffer = (uint8_t*)malloc(pixel_count * 4u);
    if( !renderer->rgba_buffer )
    {
        printf("Failed to allocate rgba buffer\n");
        free(renderer->pixel_buffer);
        free(renderer);
        return NULL;
    }

    renderer->dash_offset_x = 0;
    renderer->dash_offset_y = 0;

    return renderer;
}

void
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_Free(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer)
{
    if( !renderer )
        return;
    free(renderer->pixel_buffer);
    free(renderer->rgba_buffer);
    free(renderer);
}

bool
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_Init(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer,
    struct Platform2_Emscripten_Native* platform)
{
    if( !renderer || !platform )
        return false;
    renderer->platform = platform;
    return true;
}

void
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_SetDashOffset(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer,
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
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_SetDynamicPixelSize(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer,
    bool dynamic)
{
    if( renderer )
        renderer->pixel_size_dynamic = dynamic;
}

void
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_SetViewportChangedCallback(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer,
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
PlatformImpl2_Emscripten_Native_Renderer_Soft3D_Render(
    struct Platform2_Emscripten_Native_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !game || !render_command_buffer || !renderer->pixel_buffer ||
        !renderer->rgba_buffer )
        return;

    game->viewport_offset_x = renderer->dash_offset_x;
    game->viewport_offset_y = renderer->dash_offset_y;

    int window_width = 0;
    int window_height = 0;
    if( renderer->platform )
    {
        window_width = renderer->platform->window_width;
        window_height = renderer->platform->window_height;
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
        game->view_port->stride = renderer->width;

        const int ox = renderer->dash_offset_x;
        const int oy = renderer->dash_offset_y;
        const int max_vp_w = renderer->width - ox;
        const int max_vp_h = renderer->height - oy;
        if( max_vp_w > 0 && max_vp_h > 0 )
        {
            if( game->view_port->width > max_vp_w )
                game->view_port->width = max_vp_w;
            if( game->view_port->height > max_vp_h )
                game->view_port->height = max_vp_h;

            if( game->view_port->clip_left < 0 )
                game->view_port->clip_left = 0;
            if( game->view_port->clip_top < 0 )
                game->view_port->clip_top = 0;
            if( game->view_port->clip_right > max_vp_w )
                game->view_port->clip_right = max_vp_w;
            if( game->view_port->clip_bottom > max_vp_h )
                game->view_port->clip_bottom = max_vp_h;
            if( game->view_port->clip_right > game->view_port->width )
                game->view_port->clip_right = game->view_port->width;
            if( game->view_port->clip_bottom > game->view_port->height )
                game->view_port->clip_bottom = game->view_port->height;
            if( game->view_port->clip_left >= game->view_port->clip_right )
            {
                game->view_port->clip_left = 0;
                game->view_port->clip_right = game->view_port->width;
            }
            if( game->view_port->clip_top >= game->view_port->clip_bottom )
            {
                game->view_port->clip_top = 0;
                game->view_port->clip_bottom = game->view_port->height;
            }
        }

        game->view_port->x_center = game->view_port->width / 2;
        game->view_port->y_center = game->view_port->height / 2;
    }

    struct ToriRSRenderCommand command = { 0 };

    int* vp_pixels = NULL;
    if( game->view_port && renderer->pixel_buffer )
    {
        vp_pixels = &renderer->pixel_buffer
                         [renderer->dash_offset_y * renderer->width + renderer->dash_offset_x];
    }

    /* Readme "FrameStart" — timing covers FrameBegin + command drain + FrameEnd. */
    double const soft3d_t_frame_start_ms = emscripten_get_now();

    LibToriRS_FrameBegin(game, render_command_buffer);
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &command, true) )
    {
        switch( command.kind )
        {
        case TORIRS_GFX_FONT_LOAD:
        case TORIRS_GFX_MODEL_LOAD:
        case TORIRS_GFX_BATCH3D_MODEL_LOAD:
        case TORIRS_GFX_MODEL_UNLOAD:
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
        case TORIRS_GFX_BATCH3D_LOAD_START:
        case TORIRS_GFX_BATCH3D_LOAD_END:
        case TORIRS_GFX_BATCH3D_CLEAR:
        case TORIRS_GFX_BATCH3D_VERTEX_ARRAY_LOAD:
        case TORIRS_GFX_BATCH3D_FACE_ARRAY_LOAD:
        case TORIRS_GFX_MODEL_ANIMATION_LOAD:
        case TORIRS_GFX_BATCH3D_MODEL_ANIMATION_LOAD:
        case TORIRS_GFX_BATCH_TEXTURE_LOAD_START:
        case TORIRS_GFX_BATCH_TEXTURE_LOAD_END:
        case TORIRS_GFX_BATCH_SPRITE_LOAD_START:
        case TORIRS_GFX_BATCH_SPRITE_LOAD_END:
        case TORIRS_GFX_BATCH_FONT_LOAD_START:
        case TORIRS_GFX_BATCH_FONT_LOAD_END:
            break;
        default:
            break;
        }
    }
    LibToriRS_FrameEnd(game);

    {
        double const soft3d_ms = emscripten_get_now() - soft3d_t_frame_start_ms;
        static double s_soft3d_frame_ms_sum = 0.0;
        static int s_soft3d_frame_count = 0;
        s_soft3d_frame_ms_sum += soft3d_ms;
        s_soft3d_frame_count++;
        if( s_soft3d_frame_count >= 30 )
        {
            printf(
                "[soft3d_native] LibToriRS_FrameBegin..FrameEnd avg (30 frames): %.3f ms\n",
                s_soft3d_frame_ms_sum / 30.0);
            s_soft3d_frame_ms_sum = 0.0;
            s_soft3d_frame_count = 0;
        }
    }

    struct DstRect
    {
        int x, y, w, h;
    } dst_rect = { 0, 0, renderer->width, renderer->height };

    if( window_width > renderer->width || window_height > renderer->height )
    {
        /* Neighbor scaling hint not applicable to 2D canvas putImageData; keep letterbox math. */
    }

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

    game->soft3d_mouse_from_window = false;
    game->soft3d_present_dst_x = dst_rect.x;
    game->soft3d_present_dst_y = dst_rect.y;
    game->soft3d_present_dst_w = dst_rect.w;
    game->soft3d_present_dst_h = dst_rect.h;
    game->soft3d_buffer_w = renderer->width;
    game->soft3d_buffer_h = renderer->height;

    const int pixel_count = renderer->width * renderer->height;
    argb_buffer_to_rgba_bytes(renderer->pixel_buffer, pixel_count, renderer->rgba_buffer);
    emscripten_native_canvas_put_rgba(
        renderer->rgba_buffer, renderer->width, renderer->height, dst_rect.x, dst_rect.y);
}
