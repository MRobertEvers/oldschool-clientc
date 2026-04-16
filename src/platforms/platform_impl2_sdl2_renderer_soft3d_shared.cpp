#include "platform_impl2_sdl2_renderer_soft3d_shared.h"

#include "nuklear/torirs_nk_config.h"
#include "nuklear.h"

#define NK_SDL_RENDERER_SDL_H <SDL.h>
#define TORIRS_NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklear/backends/sdl_renderer/nuklear_torirs_sdl_renderer.h"

#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"

#ifdef __EMSCRIPTEN__
#include "platform_impl2_emscripten_sdl2.h"
#else
#include "platform_impl2_osx_sdl2.h"
#endif

static SDL_Window*
soft3d_platform_window(void* platform)
{
    if( !platform )
        return NULL;
#ifdef __EMSCRIPTEN__
    return ((struct Platform2_Emscripten_SDL2*)platform)->window;
#else
    return ((struct Platform2_OSX_SDL2*)platform)->window;
#endif
}

static float
soft3d_platform_display_scale(void* platform)
{
    if( !platform )
        return 1.0f;
#ifdef __EMSCRIPTEN__
    return ((struct Platform2_Emscripten_SDL2*)platform)->display_scale;
#else
    return ((struct Platform2_OSX_SDL2*)platform)->display_scale;
#endif
}

extern "C" {
#include "graphics/dash.h"
#include "osrs/game.h"
#include "osrs/world_option_set.h"
#include "platforms/common/platform_memory.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static struct nk_context* s_soft3d_nk;
static Uint64 s_soft3d_ui_prev_perf;

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
    torirs_nk_ui_after_frame(s_soft3d_nk);
    torirs_nk_sdlren_render(NK_ANTI_ALIASING_ON);
}

struct Platform2_SDL2_Renderer_Soft3D*
PlatformImpl2_SDL2_Renderer_Soft3D_New(
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

    return renderer;
}

void
PlatformImpl2_SDL2_Renderer_Soft3D_Shutdown(struct Platform2_SDL2_Renderer_Soft3D* renderer)
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
PlatformImpl2_SDL2_Renderer_Soft3D_Free(struct Platform2_SDL2_Renderer_Soft3D* renderer)
{
    if( !renderer )
        return;
    PlatformImpl2_SDL2_Renderer_Soft3D_Shutdown(renderer);
    free(renderer->pixel_buffer);
    free(renderer);
}

bool
PlatformImpl2_SDL2_Renderer_Soft3D_Init(
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

    torirs_nk_ui_set_active(s_soft3d_nk, torirs_nk_sdlren_handle_event, torirs_nk_sdlren_handle_grab);
    s_soft3d_ui_prev_perf = SDL_GetPerformanceCounter();

    return true;
}

void
PlatformImpl2_SDL2_Renderer_Soft3D_SetDashOffset(
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
PlatformImpl2_SDL2_Renderer_Soft3D_Render(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
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
        game->view_port->x_center = game->view_port->width / 2;
        game->view_port->y_center = game->view_port->height / 2;
        game->view_port->stride = renderer->width;
    }

    for( int y = 0; y < renderer->height; y++ )
        memset(&renderer->pixel_buffer[y * renderer->width], 0x00, renderer->width * sizeof(int));

    struct ToriRSRenderCommand command = { 0 };

    int* vp_pixels = NULL;
    if( game->view_port && renderer->pixel_buffer )
    {
        vp_pixels = &renderer->pixel_buffer
                         [renderer->dash_offset_y * renderer->width + renderer->dash_offset_x];
    }

    static std::vector<ToriRSRenderCommand> deferred_font_draws;
    deferred_font_draws.clear();

    /* Readme "FrameStart" — timing covers FrameBegin + command drain + FrameEnd. */
    Uint64 const soft3d_perf_freq = SDL_GetPerformanceFrequency();
    Uint64 const soft3d_t_frame_start = SDL_GetPerformanceCounter();

    LibToriRS_FrameBegin(game, render_command_buffer);
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &command, true) )
    {
        switch( command.kind )
        {
        case TORIRS_GFX_FONT_LOAD:
        case TORIRS_GFX_MODEL_LOAD:
            break;
        case TORIRS_GFX_TEXTURE_LOAD:
        {
            struct DashTexture* texture = command._texture_load.texture_nullable;
            if( game->sys_dash && texture && texture->texels )
                dash3d_add_texture(game->sys_dash, command._texture_load.texture_id, texture);
        }
        break;
        case TORIRS_GFX_FONT_DRAW:
            deferred_font_draws.push_back(command);
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
        default:
            break;
        }
    }
    LibToriRS_FrameEnd(game);

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

    // for( const auto& fc : deferred_font_draws )
    // {
    //     struct DashPixFont* f = fc._font_draw.font;
    //     if( f && fc._font_draw.text && renderer->pixel_buffer )
    //         dashfont_draw_text_ex(
    //             f,
    //             (uint8_t*)fc._font_draw.text,
    //             fc._font_draw.x,
    //             fc._font_draw.y,
    //             fc._font_draw.color_rgb,
    //             renderer->pixel_buffer,
    //             renderer->width);
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
PlatformImpl2_SDL2_Renderer_Soft3D_SetDynamicPixelSize(
    struct Platform2_SDL2_Renderer_Soft3D* renderer,
    bool dynamic)
{
    renderer->pixel_size_dynamic = dynamic;
}

void
PlatformImpl2_SDL2_Renderer_Soft3D_SetViewportChangedCallback(
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
