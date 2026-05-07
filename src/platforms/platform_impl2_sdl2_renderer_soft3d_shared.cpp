#include "platform_impl2_sdl2_renderer_soft3d_shared.h"

#define NK_SDL_RENDERER_SDL_H <SDL.h>
#define TORIRS_NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklear/backends/sdl_renderer/nuklear_torirs_sdl_renderer.h"
#include "platform_impl2_sdl2.h"
#include "platforms/common/torirs_nk_bench_panel.h"
#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"

static SDL_Window*
soft3d_platform_window(void* platform)
{
    if( !platform )
        return NULL;
    return ((struct Platform2_SDL2*)platform)->window;
}

static float
soft3d_platform_display_scale(void* platform)
{
    if( !platform )
        return 1.0f;
    return ((struct Platform2_SDL2*)platform)->display_scale;
}

extern "C" {
#include "graphics/dash.h"
#include "graphics/raster/deob/pix3d_deob_compat.h"
#include "osrs/game.h"
#include "osrs/gamecache/gamecache.h"
#include "osrs/obj_icon.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/scene2.h"
#include "osrs/ui.h"
#include "osrs/world_option_set.h"
#include "platforms/common/platform_memory.h"
#include "tori_rs.h"
#include "tori_rs_render.h"

/** Client.ts / interface_draw_component_model: TYPE_MODEL NPC/player head uses fixed 128² region
 * centered on widget; Pix3D-style projection via head_model_render_to_region — not game->camera.
 * Returns 1 if drawn. */
static int
soft3d_try_interface_chat_head_raster(
    struct GGame* game,
    struct ToriRSRenderCommand const* cmd,
    int widget_rx,
    int widget_ry,
    int widget_rw,
    int widget_rh,
    int fbw,
    int fbh,
    int* pixel_buffer)
{
    if( !game || !cmd || !pixel_buffer || !game->world || !game->world->scene2 || !cmd->_model_draw.model ||
        cmd->_model_draw.element_id < 0 )
        return 0;

    struct Scene2Element* se =
        scene2_element_at(game->world->scene2, cmd->_model_draw.element_id);
    if( !se || scene2_element_parent_entity_id(se) >= 0 )
        return 0;

    struct UITree* uit = game->ui_root_buffer;
    if( !uit )
        return 0;

    struct StaticUIComponent* ui_comp = NULL;
    for( uint32_t i = 0; i < uit->component_count; i++ )
    {
        struct StaticUIComponent* c = &uit->components[i];
        if( c->type != UIELEM_RS_MODEL )
            continue;
        if( c->u.rs_model.scene2_element_id != cmd->_model_draw.element_id )
            continue;
        ui_comp = c;
        break;
    }
    if( !ui_comp || !game->gamecache )
        return 0;

    struct GameCacheComponent* gcc = gamecache_get_component(game->gamecache, ui_comp->component_id);
    if( !gcc || gcc->type != COMPONENT_TYPE_MODEL )
        return 0;
    if( gcc->modelType != 2 && gcc->modelType != 3 )
        return 0;

    int const region = 128; /* sync with interface_draw_component_model region_size */
    /* Match interface_draw_component_model: center using GameCacheComponent width/height from
     * widget top-left when cache defines them (BEGIN_3D layout size may differ). */
    int cw = gcc->width > 0 ? gcc->width : widget_rw;
    int ch = gcc->height > 0 ? gcc->height : widget_rh;
    int cx          = widget_rx + cw / 2;
    int cy          = widget_ry + ch / 2;
    int rx2         = cx - region / 2;
    int ry2         = cy - region / 2;
    int rw          = region;
    int rh          = region;

    if( rx2 < 0 )
    {
        rw += rx2;
        rx2 = 0;
    }
    if( ry2 < 0 )
    {
        rh += ry2;
        ry2 = 0;
    }
    if( rx2 + rw > fbw )
        rw = fbw - rx2;
    if( ry2 + rh > fbh )
        rh = fbh - ry2;
    if( rw <= 0 || rh <= 0 )
        return 0;

    int zm = ui_comp->u.rs_model.model_zoom;
    if( zm <= 0 )
        zm = 2000;
    int xa = ui_comp->u.rs_model.model_xan & 2047;
    int ya = ui_comp->u.rs_model.model_yan & 2047;

    struct DashModel* head_rw = uitree_chat_head_dashmodel_for_frame(game, gcc, ui_comp);
    if( !head_rw )
        return 0;

    head_model_render_to_region(
        game, head_rw, pixel_buffer, fbw, rx2, ry2, rw, rh, zm, xa, ya);
    dashmodel_free(head_rw);
    return 1;
}
}

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {

/**
 * Alpha-composite a sprite sub-rectangle to pixel_buffer (same RGB blend as
 * dash2d_blit_sprite_alpha). dst_x0,dst_y0 are the destination top-left for the
 * subrect — no extra crop offset (callers pass logical placement, e.g. minimap_emit_dot).
 */
static void
soft3d_blit_sprite_subrect_alpha(
    struct DashSprite* sprite,
    struct DashViewPort* view_port,
    int dst_x0,
    int dst_y0,
    int src_x,
    int src_y,
    int src_w,
    int src_h,
    int blend_alpha,
    int* pixel_buffer)
{
    if( !sprite || !sprite->pixels_argb || !view_port || !pixel_buffer || src_w <= 0 || src_h <= 0 )
        return;
    if( src_x < 0 || src_y < 0 || src_x + src_w > sprite->width || src_y + src_h > sprite->height )
        return;

    uint32_t* src_pixels = sprite->pixels_argb;
    int sw = sprite->width;
    int stride = view_port->stride;
    int cl = view_port->clip_left;
    int ct = view_port->clip_top;
    int cr = view_port->clip_right;
    int cb = view_port->clip_bottom;

    for( int y = 0; y < src_h; y++ )
    {
        for( int x = 0; x < src_w; x++ )
        {
            uint32_t src_pixel = src_pixels[(src_y + y) * sw + (src_x + x)];
            if( src_pixel == 0 )
                continue;

            int dst_x = dst_x0 + x;
            int dst_y = dst_y0 + y;
            if( dst_x < cl || dst_x >= cr || dst_y < ct || dst_y >= cb )
                continue;

            int dst_idx = dst_y * stride + dst_x;
            int dst_pixel = pixel_buffer[dst_idx];

            int src_r = (src_pixel >> 16) & 0xFF;
            int src_g = (src_pixel >> 8) & 0xFF;
            int src_b = src_pixel & 0xFF;

            int dst_r = (dst_pixel >> 16) & 0xFF;
            int dst_g = (dst_pixel >> 8) & 0xFF;
            int dst_b = dst_pixel & 0xFF;

            int out_r = (src_r * blend_alpha + dst_r * (256 - blend_alpha)) >> 8;
            int out_g = (src_g * blend_alpha + dst_g * (256 - blend_alpha)) >> 8;
            int out_b = (src_b * blend_alpha + dst_b * (256 - blend_alpha)) >> 8;

            pixel_buffer[dst_idx] = (out_r << 16) | (out_g << 8) | out_b;
        }
    }
}

} /* extern "C" */

static struct nk_context* s_soft3d_nk;
static Uint64 s_soft3d_ui_prev_perf;

static void (*s_bench_panel_draw)(
    struct nk_context*,
    struct GGame*) = nullptr;

extern "C" void
torirs_nk_bench_panel_register(
    void (*draw)(
        struct nk_context* nk,
        struct GGame* game))
{
    s_bench_panel_draw = draw;
}

extern "C" void
torirs_nk_bench_panel_unregister(void)
{
    s_bench_panel_draw = nullptr;
}

static void
render_nuklear_overlay(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game)
{
    if( !s_soft3d_nk )
        return;

    double const dt = torirs_nk_ui_frame_delta_sec(&s_soft3d_ui_prev_perf);

    TorirsNkDebugPanelParams params = {};
    params.window_title = "Info";
    params.delta_time_sec = dt;
    params.view_w_cap = renderer->max_width;
    params.view_h_cap = renderer->max_height;
    params.sdl_window = soft3d_platform_window(renderer->platform);
    params.soft3d = renderer;
    params.include_soft3d_extras = 1;

    torirs_nk_debug_panel_draw(s_soft3d_nk, game, &params);
    if( s_bench_panel_draw )
        s_bench_panel_draw(s_soft3d_nk, game);
    torirs_nk_ui_after_frame(s_soft3d_nk);
    torirs_nk_sdlren_render(NK_ANTI_ALIASING_ON);
}

struct Platform2_SDL2_Renderer_Soft3D*
PlatformImpl2_SDL2_Renderer_Soft3DShared_New(
    int width,
    int height,
    int max_width,
    int max_height)
{
    struct Platform2_SDL2_Renderer_Soft3D* renderer =
        (struct Platform2_SDL2_Renderer_Soft3D*)malloc(
            sizeof(struct Platform2_SDL2_Renderer_Soft3D));
    memset(renderer, 0, sizeof(struct Platform2_SDL2_Renderer_Soft3D));

    renderer->width = width;
    renderer->height = height;
    renderer->initial_width = width;
    renderer->initial_height = height;
    renderer->max_width = max_width;
    renderer->max_height = max_height;

    /* All rendering uses stride `width` and height `height` (see clear loop, vp_pixels, blits).
     * max_* only clamp viewport/window scaling — not the CPU buffer layout. */
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
    renderer->soft3d_iface_clip_stack_top = -1;

    return renderer;
}

void
PlatformImpl2_SDL2_Renderer_Soft3DShared_Shutdown(struct Platform2_SDL2_Renderer_Soft3D* renderer)
{
    if( !renderer )
        return;
    if( s_soft3d_nk )
    {
        torirs_nk_ui_clear_active();
        torirs_nk_sdlren_shutdown();
        s_soft3d_nk = NULL;
    }
    if( renderer->texture )
    {
        SDL_DestroyTexture(renderer->texture);
        renderer->texture = NULL;
    }
    if( renderer->renderer )
    {
        SDL_DestroyRenderer(renderer->renderer);
        renderer->renderer = NULL;
    }
}

void
PlatformImpl2_SDL2_Renderer_Soft3DShared_Free(struct Platform2_SDL2_Renderer_Soft3D* renderer)
{
    if( !renderer )
        return;
    PlatformImpl2_SDL2_Renderer_Soft3DShared_Shutdown(renderer);
    free(renderer->pixel_buffer);
    free(renderer);
}

bool
PlatformImpl2_SDL2_Renderer_Soft3DShared_Init(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    void* platform)
{
    renderer->platform = platform;

    SDL_Window* win = soft3d_platform_window(platform);
    renderer->renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if( !renderer->renderer )
    {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        return false;
    }

    renderer->texture = SDL_CreateTexture(
        renderer->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        renderer->width,
        renderer->height);
    if( !renderer->texture )
    {
        printf("Failed to create texture\n");
        SDL_DestroyRenderer(renderer->renderer);
        renderer->renderer = NULL;
        return false;
    }

    s_soft3d_nk = torirs_nk_sdlren_init(win, renderer->renderer);
    if( !s_soft3d_nk )
    {
        printf("Nuklear SDL renderer init failed\n");
        SDL_DestroyTexture(renderer->texture);
        renderer->texture = NULL;
        SDL_DestroyRenderer(renderer->renderer);
        renderer->renderer = NULL;
        return false;
    }

    {
        struct nk_font_atlas* atlas = NULL;
        torirs_nk_sdlren_font_stash_begin(&atlas);
        nk_font_atlas_add_default(atlas, 13.0f * soft3d_platform_display_scale(platform), NULL);
        torirs_nk_sdlren_font_stash_end();
    }

    torirs_nk_ui_set_active(
        s_soft3d_nk, torirs_nk_sdlren_handle_event, torirs_nk_sdlren_handle_grab);
    s_soft3d_ui_prev_perf = SDL_GetPerformanceCounter();

    return true;
}

void
PlatformImpl2_SDL2_Renderer_Soft3DShared_SetDashOffset(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
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
PlatformImpl2_SDL2_Renderer_Soft3DShared_Render(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    pix3d_deob_compat_dbg_tick();
    game->viewport_offset_x = renderer->dash_offset_x;
    game->viewport_offset_y = renderer->dash_offset_y;

    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(soft3d_platform_window(renderer->platform), &window_width, &window_height);

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
    Uint64 const soft3d_perf_freq = SDL_GetPerformanceFrequency();
    Uint64 const soft3d_t_frame_start = SDL_GetPerformanceCounter();

    int iface_saved_clip_l = 0;
    int iface_saved_clip_t = 0;
    int iface_saved_clip_r = 0;
    int iface_saved_clip_b = 0;
    if( game->iface_view_port )
    {
        iface_saved_clip_l = game->iface_view_port->clip_left;
        iface_saved_clip_t = game->iface_view_port->clip_top;
        iface_saved_clip_r = game->iface_view_port->clip_right;
        iface_saved_clip_b = game->iface_view_port->clip_bottom;
    }
    renderer->soft3d_iface_clip_stack_top = -1;

    /* BEGIN_3D / END_3D stack: GPU backends apply dst rect per pass; soft3d previously ignored
     * these and always rasterized with game->view_port + world pixel slice. Mirror the rect so
     * interface RS_MODEL (chat heads) raster into the widget. See Client.ts drawInterface
     * TYPE_MODEL. */
    int soft3d_ui3d_top = -1;
    int soft3d_ui3d_stack_x[8];
    int soft3d_ui3d_stack_y[8];
    int soft3d_ui3d_stack_w[8];
    int soft3d_ui3d_stack_h[8];

    LibToriRS_FrameBegin(game, render_command_buffer);
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &command, true) )
    {
        switch( command.kind )
        {
        case TORIRS_GFX_RES_FONT_LOAD:
        case TORIRS_GFX_RES_MODEL_LOAD:
        case TORIRS_GFX_RES_MODEL_UNLOAD:
            break;
        case TORIRS_GFX_RES_TEX_LOAD:
        {
            struct DashTexture* texture = command._texture_load.texture_nullable;
            if( game->sys_dash && texture && texture->texels )
                dash3d_add_texture(game->sys_dash, command._texture_load.texture_id, texture);
        }
        break;
        case TORIRS_GFX_DRAW_FONT:
        {
            struct DashPixFont* f = command._font_draw.font;
            const uint8_t* text = command._font_draw.text;
            if( !f || !text || !renderer->pixel_buffer )
                break;
            const int fx = command._font_draw.x;
            const int fy = command._font_draw.y;
            int cl = 0;
            int ct = 0;
            int cr = renderer->width;
            int cb = renderer->height;
            if( game->iface_view_port )
            {
                struct DashViewPort* vp = game->iface_view_port;
                cl = vp->clip_left;
                ct = vp->clip_top;
                cr = vp->clip_right;
                cb = vp->clip_bottom;
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
                if( command._font_draw.shadowed )
                {
                    dashfont_draw_text_clipped_taggable(
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
                        cb,
                        true);
                }
                else
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
        }
        break;
        case TORIRS_GFX_STATE_CLEAR_RECT:
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
        case TORIRS_GFX_DRAW_MODEL:
            if( command._model_draw.use_animation && command._model_draw.model &&
                command._model_draw.animation_frame )
            {
                dashmodel_animate(
                    command._model_draw.model,
                    command._model_draw.animation_frame,
                    command._model_draw.animation_framemap);
            }
            if( !renderer->pixel_buffer || !game->sys_dash )
                break;

            {
                int fbw = renderer->width;
                int fbh = renderer->height;
                int rx = 0;
                int ry = 0;
                int rwid = 0;
                int rhgt = 0;
                int have_ui_rect = 0;
                if( soft3d_ui3d_top >= 0 )
                {
                    rx = soft3d_ui3d_stack_x[soft3d_ui3d_top];
                    ry = soft3d_ui3d_stack_y[soft3d_ui3d_top];
                    rwid = soft3d_ui3d_stack_w[soft3d_ui3d_top];
                    rhgt = soft3d_ui3d_stack_h[soft3d_ui3d_top];
                    if( rx < 0 )
                    {
                        rwid += rx;
                        rx = 0;
                    }
                    if( ry < 0 )
                    {
                        rhgt += ry;
                        ry = 0;
                    }
                    if( rx + rwid > fbw )
                        rwid = fbw - rx;
                    if( ry + rhgt > fbh )
                        rhgt = fbh - ry;
                    have_ui_rect = rwid > 0 && rhgt > 0;
                }

                if( have_ui_rect &&
                    soft3d_try_interface_chat_head_raster(
                        game, &command, rx, ry, rwid, rhgt, fbw, fbh, renderer->pixel_buffer) )
                {
                    break;
                }
            }

            if( !game->camera )
                break;

            if( renderer->pixel_buffer && game->sys_dash )
            {
                struct DashPosition draw_pos = command._model_draw.position;
                struct DashViewPort* vp_use = game->view_port;
                int* dst_pixels = vp_pixels;
                if( soft3d_ui3d_top >= 0 )
                {
                    int rx = soft3d_ui3d_stack_x[soft3d_ui3d_top];
                    int ry = soft3d_ui3d_stack_y[soft3d_ui3d_top];
                    int rwid = soft3d_ui3d_stack_w[soft3d_ui3d_top];
                    int rhgt = soft3d_ui3d_stack_h[soft3d_ui3d_top];
                    int fbw = renderer->width;
                    int fbh = renderer->height;
                    if( rx < 0 )
                    {
                        rwid += rx;
                        rx = 0;
                    }
                    if( ry < 0 )
                    {
                        rhgt += ry;
                        ry = 0;
                    }
                    if( rx + rwid > fbw )
                        rwid = fbw - rx;
                    if( ry + rhgt > fbh )
                        rhgt = fbh - ry;
                    if( rwid > 0 && rhgt > 0 )
                    {
                        static struct DashViewPort soft3d_ui_vp;
                        soft3d_ui_vp.width = rwid;
                        soft3d_ui_vp.height = rhgt;
                        soft3d_ui_vp.clip_left = 0;
                        soft3d_ui_vp.clip_top = 0;
                        soft3d_ui_vp.clip_right = rwid;
                        soft3d_ui_vp.clip_bottom = rhgt;
                        soft3d_ui_vp.x_center = rwid / 2;
                        soft3d_ui_vp.y_center = rhgt / 2;
                        soft3d_ui_vp.stride = fbw;
                        vp_use = &soft3d_ui_vp;
                        dst_pixels = renderer->pixel_buffer + ry * fbw + rx;
                        if( game->world && game->world->scene2 &&
                            command._model_draw.element_id >= 0 )
                        {
                            struct Scene2Element* se = scene2_element_at(
                                game->world->scene2, command._model_draw.element_id);
                            if( se && scene2_element_parent_entity_id(se) < 0 )
                            {
                                draw_pos.x += game->camera_world_x;
                                draw_pos.y += game->camera_world_y;
                                draw_pos.z += game->camera_world_z;
                            }
                        }
                    }
                    else
                    {
                        dst_pixels = NULL;
                    }
                }
                else if( vp_pixels )
                {
                    dst_pixels = vp_pixels;
                }
                else
                {
                    dst_pixels = NULL;
                }
                if( dst_pixels && vp_use )
                    dash3d_raster_projected_model(
                        game->sys_dash,
                        command._model_draw.model,
                        &draw_pos,
                        vp_use,
                        game->camera,
                        dst_pixels,
                        false);
            }
            break;
        case TORIRS_GFX_DRAW_SPRITE:
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

            struct DashViewPort* vp = game->iface_view_port;

            /* Feature 5: per-sprite clip override. Push clip bounds if requested. */
            bool had_per_clip = false;
            int saved_l = 0, saved_t = 0, saved_r = 0, saved_b = 0;
            if( command._sprite_draw.clip_w > 0 )
            {
                saved_l = vp->clip_left;
                saved_t = vp->clip_top;
                saved_r = vp->clip_right;
                saved_b = vp->clip_bottom;
                had_per_clip = true;
                int cx = command._sprite_draw.clip_x;
                int cy = command._sprite_draw.clip_y;
                int cxr = cx + command._sprite_draw.clip_w;
                int cyb = cy + command._sprite_draw.clip_h;
                dash2d_set_bounds(
                    vp,
                    cx > saved_l ? cx : saved_l,
                    cy > saved_t ? cy : saved_t,
                    cxr < saved_r ? cxr : saved_r,
                    cyb < saved_b ? cyb : saved_b);
            }

            /* Feature 4: masked sprite — apply mask alpha to temp copy before blit. */
            struct DashSprite* mask_sp = command._sprite_draw.mask_sprite;
            bool has_mask =
                (mask_sp && mask_sp->pixels_argb && command._sprite_draw.mask_element_id >= 0);

            /* Build a temporary masked pixel buffer if needed. */
            uint32_t* masked_pixels = nullptr;
            struct DashSprite masked_sprite_tmp = {};
            if( has_mask )
            {
                int pw = sp->width;
                int ph = sp->height;
                masked_pixels = (uint32_t*)malloc((size_t)(pw * ph) * sizeof(uint32_t));
                if( masked_pixels )
                {
                    /* Copy source pixels, masking alpha from mask sprite. */
                    int mw = mask_sp->width;
                    int mh = mask_sp->height;
                    for( int py = 0; py < ph; py++ )
                    {
                        for( int px = 0; px < pw; px++ )
                        {
                            uint32_t src_pix = ((uint32_t*)sp->pixels_argb)[py * pw + px];
                            uint32_t mask_alpha = 0xff;
                            if( px < mw && py < mh )
                            {
                                uint32_t mp = ((uint32_t*)mask_sp->pixels_argb)[py * mw + px];
                                mask_alpha = (mp >> 24) & 0xff;
                            }
                            uint32_t orig_alpha = (src_pix >> 24) & 0xff;
                            uint32_t new_alpha = (orig_alpha * mask_alpha) / 255u;
                            masked_pixels[py * pw + px] =
                                (src_pix & 0x00ffffffu) | (new_alpha << 24);
                        }
                    }
                    /* Wire a temporary DashSprite pointing at masked pixels. */
                    masked_sprite_tmp = *sp;
                    masked_sprite_tmp.pixels_argb = masked_pixels;
                    sp = &masked_sprite_tmp;
                }
            }

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
                int ix = command._sprite_draw.src_bb_x;
                int iy = command._sprite_draw.src_bb_y;
                bool const subrect = command._sprite_draw.src_bb_w > 0 &&
                                     command._sprite_draw.src_bb_h > 0 && ix >= 0 && iy >= 0 &&
                                     ix + srw <= sp->width && iy + srh <= sp->height;

                /* Feature 8: alpha mode 1 = blend, 0 = opaque.
                 * When src_bb is set (minimap dots, cropped UI sprites), dash2d_blit_sprite_alpha
                 * is wrong: it ignores src_bb and shifts dst by crop twice vs subrect placement.
                 * Use subrect paths so dst_bb is the on-screen top-left of the drawn subrect. */
                if( subrect )
                {
                    if( command._sprite_draw.alpha_mode == 1 )
                    {
                        soft3d_blit_sprite_subrect_alpha(
                            sp,
                            vp,
                            command._sprite_draw.dst_bb_x,
                            command._sprite_draw.dst_bb_y,
                            ix,
                            iy,
                            srw,
                            srh,
                            torirs_sprite_draw_blend_u8(&command),
                            renderer->pixel_buffer);
                    }
                    else
                    {
                        dash2d_blit_sprite_subrect(
                            game->sys_dash,
                            sp,
                            vp,
                            command._sprite_draw.dst_bb_x - sp->crop_x,
                            command._sprite_draw.dst_bb_y - sp->crop_y,
                            ix,
                            iy,
                            srw,
                            srh,
                            renderer->pixel_buffer);
                    }
                }
                else if( command._sprite_draw.alpha_mode == 1 )
                {
                    dash2d_blit_sprite_alpha(
                        game->sys_dash,
                        sp,
                        vp,
                        command._sprite_draw.dst_bb_x,
                        command._sprite_draw.dst_bb_y,
                        torirs_sprite_draw_blend_u8(&command),
                        renderer->pixel_buffer);
                }
                else
                {
                    dash2d_blit_sprite(
                        game->sys_dash,
                        sp,
                        vp,
                        command._sprite_draw.dst_bb_x,
                        command._sprite_draw.dst_bb_y,
                        renderer->pixel_buffer);
                }
            }

            if( masked_pixels )
                free(masked_pixels);

            /* Restore per-sprite clip. */
            if( had_per_clip )
                dash2d_set_bounds(vp, saved_l, saved_t, saved_r, saved_b);
        }
        break;
        case TORIRS_GFX_STATE_PUSH_CLIP:
        {
            struct DashViewPort* vp = game->iface_view_port;
            if( !vp || renderer->soft3d_iface_clip_stack_top + 1 >= PLATFORM_SOFT3D_CLIP_STACK )
                break;
            int x = command._push_clip.x;
            int y = command._push_clip.y;
            int w = command._push_clip.w;
            int h = command._push_clip.h;
            int xr = x + w;
            int yb = y + h;
            int nl = x > vp->clip_left ? x : vp->clip_left;
            int nt = y > vp->clip_top ? y : vp->clip_top;
            int nr = xr < vp->clip_right ? xr : vp->clip_right;
            int nb = yb < vp->clip_bottom ? yb : vp->clip_bottom;
            int i = ++renderer->soft3d_iface_clip_stack_top;
            renderer->soft3d_iface_clip_stack_l[i] = vp->clip_left;
            renderer->soft3d_iface_clip_stack_t[i] = vp->clip_top;
            renderer->soft3d_iface_clip_stack_r[i] = vp->clip_right;
            renderer->soft3d_iface_clip_stack_b[i] = vp->clip_bottom;
            dash2d_set_bounds(vp, nl, nt, nr, nb);
        }
        break;
        case TORIRS_GFX_STATE_POP_CLIP:
        {
            struct DashViewPort* vp = game->iface_view_port;
            if( !vp || renderer->soft3d_iface_clip_stack_top < 0 )
                break;
            int i = renderer->soft3d_iface_clip_stack_top--;
            dash2d_set_bounds(
                vp,
                renderer->soft3d_iface_clip_stack_l[i],
                renderer->soft3d_iface_clip_stack_t[i],
                renderer->soft3d_iface_clip_stack_r[i],
                renderer->soft3d_iface_clip_stack_b[i]);
        }
        break;
        case TORIRS_GFX_DRAW_RECT:
        {
            struct DashViewPort* vp = game->iface_view_port;
            int* pb = renderer->pixel_buffer;
            if( !vp || !pb )
                break;
            int x = command._rect_draw.x;
            int y = command._rect_draw.y;
            int w = command._rect_draw.w;
            int h = command._rect_draw.h;
            int rgb = command._rect_draw.color_rgb;
            int alpha = command._rect_draw.alpha;
            uint8_t fill = command._rect_draw.fill;
            if( w <= 0 || h <= 0 )
                break;
            int stride = renderer->width;
            int x0 = x < vp->clip_left ? vp->clip_left : x;
            int y0 = y < vp->clip_top ? vp->clip_top : y;
            int x1 = x + w > vp->clip_right ? vp->clip_right : x + w;
            int y1 = y + h > vp->clip_bottom ? vp->clip_bottom : y + h;
            if( x0 >= x1 || y0 >= y1 )
                break;
            if( fill )
            {
                /* Match WebGL: only 1..254 blends; 0 and >=255 are opaque replacement. */
                if( alpha > 0 && alpha < 255 )
                {
                    int r = (rgb >> 16) & 0xFF;
                    int g = (rgb >> 8) & 0xFF;
                    int b = rgb & 0xFF;
                    for( int py = y0; py < y1; py++ )
                        for( int px = x0; px < x1; px++ )
                        {
                            int idx = py * stride + px;
                            int existing = pb[idx];
                            int er = (existing >> 16) & 0xFF, eg = (existing >> 8) & 0xFF,
                                eb = existing & 0xFF;
                            int nr = (r * (256 - alpha) + er * alpha) >> 8;
                            int ng = (g * (256 - alpha) + eg * alpha) >> 8;
                            int nb = (b * (256 - alpha) + eb * alpha) >> 8;
                            pb[idx] = (nr << 16) | (ng << 8) | nb;
                        }
                }
                else
                {
                    for( int py = y0; py < y1; py++ )
                        for( int px = x0; px < x1; px++ )
                            pb[py * stride + px] = rgb;
                }
            }
            else
            {
                if( y >= vp->clip_top && y < vp->clip_bottom )
                    for( int px = x0; px < x1; px++ )
                        pb[y * stride + px] = rgb;
                if( y + h - 1 >= vp->clip_top && y + h - 1 < vp->clip_bottom )
                    for( int px = x0; px < x1; px++ )
                        pb[(y + h - 1) * stride + px] = rgb;
                if( x >= vp->clip_left && x < vp->clip_right )
                    for( int py = y0; py < y1; py++ )
                        pb[py * stride + x] = rgb;
                if( x + w - 1 >= vp->clip_left && x + w - 1 < vp->clip_right )
                    for( int py = y0; py < y1; py++ )
                        pb[py * stride + (x + w - 1)] = rgb;
            }
        }
        break;
        case TORIRS_GFX_DRAW_LINE:
        {
            struct DashViewPort* vp = game->iface_view_port;
            int* pb = renderer->pixel_buffer;
            if( !vp || !pb )
                break;
            int x0 = command._line_draw.x0;
            int y0 = command._line_draw.y0;
            int x1 = command._line_draw.x1;
            int y1 = command._line_draw.y1;
            int rgb = command._line_draw.color_rgb;
            ui_draw_line_clipped(
                pb,
                renderer->width,
                renderer->width,
                renderer->height,
                vp->clip_left,
                vp->clip_top,
                vp->clip_right,
                vp->clip_bottom,
                x0,
                y0,
                x1,
                y1,
                rgb);
        }
        break;
        case TORIRS_GFX_STATE_BEGIN_3D:
            if( command._begin_3d.w > 0 && command._begin_3d.h > 0 && soft3d_ui3d_top + 1 < 8 )
            {
                ++soft3d_ui3d_top;
                soft3d_ui3d_stack_x[soft3d_ui3d_top] = command._begin_3d.x;
                soft3d_ui3d_stack_y[soft3d_ui3d_top] = command._begin_3d.y;
                soft3d_ui3d_stack_w[soft3d_ui3d_top] = command._begin_3d.w;
                soft3d_ui3d_stack_h[soft3d_ui3d_top] = command._begin_3d.h;
            }
            break;
        case TORIRS_GFX_STATE_END_3D:
            if( soft3d_ui3d_top >= 0 )
                --soft3d_ui3d_top;
            break;
        case TORIRS_GFX_STATE_BEGIN_2D:
        case TORIRS_GFX_STATE_END_2D:
            break;
        default:
            break;
        }
    }
    LibToriRS_FrameEnd(game);
    if( game->iface_view_port )
    {
        dash2d_set_bounds(
            game->iface_view_port,
            iface_saved_clip_l,
            iface_saved_clip_t,
            iface_saved_clip_r,
            iface_saved_clip_b);
    }

    /* Reflect tile target for debug HUD; movement consumes tile_clicked in LibToriRS_GameStep.
     * Do not clear game->tile_clicked here — FrameBegin resets each frame; clearing here prevented
     * the next GameStep from seeing clicks produced at FrameEnd. */
    if( game->tile_clicked_x >= 0 )
        renderer->clicked_tile_x = game->tile_clicked_x;
    else
        renderer->clicked_tile_x = -1;
    if( game->tile_clicked_z >= 0 )
        renderer->clicked_tile_z = game->tile_clicked_z;
    else
        renderer->clicked_tile_z = -1;
    renderer->clicked_tile_level = game->tile_clicked_level;

    {
        Uint64 const soft3d_t_after = SDL_GetPerformanceCounter();
        renderer->last_raster_ms =
            (double)(soft3d_t_after - soft3d_t_frame_start) * 1000.0 / (double)soft3d_perf_freq;
    }

    // {
    //     Uint64 const soft3d_t_after = SDL_GetPerformanceCounter();
    //     double const soft3d_ms =
    //         (double)(soft3d_t_after - soft3d_t_frame_start) * 1000.0 / (double)soft3d_perf_freq;
    //     static double s_soft3d_frame_ms_sum = 0.0;
    //     static int s_soft3d_frame_count = 0;
    //     s_soft3d_frame_ms_sum += soft3d_ms;
    //     s_soft3d_frame_count++;
    //     if( s_soft3d_frame_count >= 30 )
    //     {
    //         printf(
    //             "[soft3d] LibToriRS_FrameBegin..FrameEnd avg (30 frames): %.3f ms\n",
    //             s_soft3d_frame_ms_sum / 30.0);
    //         s_soft3d_frame_ms_sum = 0.0;
    //         s_soft3d_frame_count = 0;
    //     }
    // }

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        renderer->pixel_buffer,
        renderer->width,
        renderer->height,
        32,
        renderer->width * sizeof(int),
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000);

    int* pix_write = NULL;
    int texture_pitch = 0;
    if( SDL_LockTexture(renderer->texture, NULL, (void**)&pix_write, &texture_pitch) < 0 )
    {
        SDL_FreeSurface(surface);
        return;
    }

    int* src_pixels = (int*)surface->pixels;
    int texture_w = texture_pitch / sizeof(int);

    /* Streaming texture must be cleared: soft buffer uses 0 for transparent/empty. */
    memset(pix_write, 0, (size_t)texture_pitch * (size_t)renderer->height);

    for( int src_y = 0; src_y < (renderer->height); src_y++ )
    {
        int* row = &pix_write[(src_y * texture_w)];
        memcpy(row, &src_pixels[src_y * renderer->width], (size_t)renderer->width * sizeof(int));
    }

    SDL_UnlockTexture(renderer->texture);

    SDL_Rect dst_rect;
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = renderer->width;
    dst_rect.h = renderer->height;

    if( window_width > renderer->width || window_height > renderer->height )
    {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    }
    else
    {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    }

    float src_aspect = (float)renderer->width / (float)renderer->height;
    float window_aspect = (float)window_width / (float)window_height;

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

#ifdef __EMSCRIPTEN__
    game->soft3d_mouse_from_window = false;
#else
    game->soft3d_mouse_from_window = true;
#endif
    game->soft3d_present_dst_x = dst_rect.x;
    game->soft3d_present_dst_y = dst_rect.y;
    game->soft3d_present_dst_w = dst_rect.w;
    game->soft3d_present_dst_h = dst_rect.h;
    game->soft3d_buffer_w = renderer->width;
    game->soft3d_buffer_h = renderer->height;

    SDL_RenderCopy(renderer->renderer, renderer->texture, NULL, &dst_rect);
    SDL_FreeSurface(surface);

    render_nuklear_overlay(renderer, game);

    SDL_RenderPresent(renderer->renderer);
}

void
PlatformImpl2_SDL2_Renderer_Soft3DShared_SetDynamicPixelSize(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    bool dynamic)
{
    renderer->pixel_size_dynamic = dynamic;
}

void
PlatformImpl2_SDL2_Renderer_Soft3DShared_SetViewportChangedCallback(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    void (*callback)(
        struct GGame* game,
        int new_width,
        int new_height,
        void* userdata),
    void* userdata)
{
    renderer->on_viewport_changed = callback;
    renderer->on_viewport_changed_userdata = userdata;
}
