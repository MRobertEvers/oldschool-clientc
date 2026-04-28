#if defined(__APPLE__)

#include "platforms/platform_impl2_sdl2_renderer_metal.h"

#import <QuartzCore/CAMetalLayer.h>

#include <SDL.h>

#include "nuklear/backends/torirs_nk_metal_sdl.h"
#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"

#include <cstdio>

extern "C" {
#include "osrs/game.h"
#include "platforms/ToriRSPlatformKit/include/ToriRSPlatformKit/trspk_math.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

Platform2_SDL2_Renderer_Metal*
PlatformImpl2_SDL2_Renderer_Metal_New(
    int width,
    int height)
{
    Platform2_SDL2_Renderer_Metal* renderer = new Platform2_SDL2_Renderer_Metal();
    renderer->width = width;
    renderer->height = height;
    return renderer;
}

void
PlatformImpl2_SDL2_Renderer_Metal_Free(Platform2_SDL2_Renderer_Metal* renderer)
{
    if( !renderer )
        return;
    if( renderer->nk_ui )
    {
        torirs_nk_ui_clear_active();
        torirs_nk_metal_shutdown(renderer->nk_ui);
        renderer->nk_ui = nullptr;
    }
    if( renderer->trspk )
        TRSPK_Metal_Shutdown(renderer->trspk);
    if( renderer->metal_view )
        SDL_Metal_DestroyView(renderer->metal_view);
    delete renderer;
}

bool
PlatformImpl2_SDL2_Renderer_Metal_Init(
    Platform2_SDL2_Renderer_Metal* renderer,
    struct Platform2_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;
    renderer->platform = platform;
    renderer->metal_view = SDL_Metal_CreateView(platform->window);
    if( !renderer->metal_view )
        return false;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_Metal_GetDrawableSize(platform->window, &drawable_w, &drawable_h);
    renderer->width = drawable_w;
    renderer->height = drawable_h;
    renderer->trspk =
        TRSPK_Metal_Init((__bridge void*)layer, (uint32_t)drawable_w, (uint32_t)drawable_h);
    if( !renderer->trspk )
    {
        SDL_Metal_DestroyView(renderer->metal_view);
        renderer->metal_view = nullptr;
        return false;
    }

    /* Nuklear + SDL input use the same space as SDL_GetMouseState / SDL mouse events:
     * logical window client pixels (see Platform2_SDL2_PollEvents). Do not use
     * game_screen_* here — it can stay fixed while the window is resized. */
    int nk_win_w = 0;
    int nk_win_h = 0;
    SDL_GetWindowSize(platform->window, &nk_win_w, &nk_win_h);
    if( nk_win_w <= 0 || nk_win_h <= 0 )
    {
        nk_win_w = platform->game_screen_width > 0 ? platform->game_screen_width : drawable_w;
        nk_win_h = platform->game_screen_height > 0 ? platform->game_screen_height : drawable_h;
    }

    renderer->nk_ui = torirs_nk_metal_init(
        renderer->trspk->device,
        nk_win_w,
        nk_win_h,
        512u * 1024u,
        128u * 1024u);
    if( !renderer->nk_ui )
    {
        fprintf(stderr, "Metal: Nuklear init failed\n");
        TRSPK_Metal_Shutdown(renderer->trspk);
        renderer->trspk = nullptr;
        SDL_Metal_DestroyView(renderer->metal_view);
        renderer->metal_view = nullptr;
        return false;
    }
    {
        struct nk_font_atlas* atlas = nullptr;
        torirs_nk_metal_font_stash_begin(renderer->nk_ui, &atlas);
        nk_font_atlas_add_default(atlas, 13.0f * platform->display_scale, nullptr);
        torirs_nk_metal_font_stash_end(renderer->nk_ui);
    }
    renderer->nk_metal_last_display_scale = platform->display_scale;
    torirs_nk_ui_set_active(torirs_nk_metal_ctx(renderer->nk_ui), nullptr, nullptr);
    renderer->nk_ui_prev_perf = SDL_GetPerformanceCounter();

    renderer->ready = true;
    return true;
}

void
PlatformImpl2_SDL2_Renderer_Metal_Render(
    Platform2_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->ready || !renderer->trspk || !game || !render_command_buffer )
        return;
    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_Metal_GetDrawableSize(renderer->platform->window, &drawable_w, &drawable_h);
    if( drawable_w != renderer->width || drawable_h != renderer->height )
    {
        renderer->width = drawable_w;
        renderer->height = drawable_h;
        TRSPK_Metal_Resize(renderer->trspk, (uint32_t)drawable_w, (uint32_t)drawable_h);
    }

    SDL_GetWindowSize(renderer->platform->window, &renderer->platform->window_width, &renderer->platform->window_height);
    renderer->platform->drawable_width = drawable_w;
    renderer->platform->drawable_height = drawable_h;
    {
        const int lw = renderer->platform->window_width;
        const int lh = renderer->platform->window_height;
        if( lw > 0 && lh > 0 )
        {
            const float sx = (float)drawable_w / (float)lw;
            const float sy = (float)drawable_h / (float)lh;
            renderer->platform->display_scale = sx > sy ? sx : sy;
        }
    }

    if( renderer->nk_ui
        && (renderer->nk_metal_last_display_scale < 0.0f
            || renderer->nk_metal_last_display_scale != renderer->platform->display_scale) )
    {
        torirs_nk_metal_rebuild_default_font(renderer->nk_ui, 13.0f * renderer->platform->display_scale);
        renderer->nk_metal_last_display_scale = renderer->platform->display_scale;
    }

    int win_w = renderer->platform->game_screen_width;
    int win_h = renderer->platform->game_screen_height;
    if( win_w <= 0 || win_h <= 0 )
        SDL_GetWindowSize(renderer->platform->window, &win_w, &win_h);
    if( win_w > 0 && win_h > 0 )
        TRSPK_Metal_SetWindowSize(renderer->trspk, (uint32_t)win_w, (uint32_t)win_h);

    int nk_win_w = 0;
    int nk_win_h = 0;
    SDL_GetWindowSize(renderer->platform->window, &nk_win_w, &nk_win_h);
    if( nk_win_w <= 0 || nk_win_h <= 0 )
    {
        nk_win_w = win_w > 0 ? win_w : drawable_w;
        nk_win_h = win_h > 0 ? win_h : drawable_h;
    }

    TRSPK_ResourceCache* cache = TRSPK_Metal_GetCache(renderer->trspk);
    TRSPK_Batch32* staging = TRSPK_Metal_GetBatchStaging(renderer->trspk);
    TRSPK_MetalEventContext events = {};
    events.renderer = renderer->trspk;
    events.cache = cache;
    events.staging = staging;
    events.current_batch_id = renderer->current_model_batch_id;
    events.batch_active = renderer->current_model_batch_active;
    events.has_default_pass_logical = false;
    if( win_w > 0 && win_h > 0 && game )
    {
        trspk_pass_logical_from_game(&events.default_pass_logical, win_w, win_h, game);
        events.has_default_pass_logical =
            events.default_pass_logical.width > 0 && events.default_pass_logical.height > 0;
    }
    LibToriRS_FrameBegin(game, render_command_buffer);
    TRSPK_Metal_FrameBegin(renderer->trspk);

    struct ToriRSRenderCommand cmd = { 0 };
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
    {
        switch( cmd.kind )
        {
        case TORIRS_GFX_RES_MODEL_LOAD:
            trspk_metal_event_res_model_load(&events, &cmd);
            break;
        case TORIRS_GFX_RES_MODEL_UNLOAD:
            trspk_metal_event_res_model_unload(&events, &cmd);
            break;
        case TORIRS_GFX_RES_TEX_LOAD:
            trspk_metal_event_tex_load(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_BEGIN:
            trspk_metal_event_batch3d_begin(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_MODEL_ADD:
            trspk_metal_event_batch3d_model_add(&events, &cmd);
            break;
        case TORIRS_GFX_RES_ANIM_LOAD:
            trspk_metal_event_res_anim_load(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_ANIM_ADD:
            trspk_metal_event_batch3d_anim_add(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_END:
            trspk_metal_event_batch3d_end(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_CLEAR:
            trspk_metal_event_batch3d_clear(&events, &cmd);
            break;
        case TORIRS_GFX_DRAW_MODEL:
            trspk_metal_event_draw_model(
                &events, game, &cmd, renderer->facebuffer.indices, TRSPK_FACEBUFFER_INDEX_CAPACITY);
            break;
        case TORIRS_GFX_STATE_BEGIN_3D:
            trspk_metal_event_state_begin_3d(&events, &cmd);
            break;
        case TORIRS_GFX_STATE_CLEAR_RECT:
            trspk_metal_event_state_clear_rect(&events, &cmd);
            break;
        case TORIRS_GFX_STATE_END_3D:
            trspk_metal_event_state_end_3d(&events, game, (double)game->cycle);
            break;
        default:
            break;
        }
    }
    renderer->current_model_batch_id = events.current_batch_id;
    renderer->current_model_batch_active = events.batch_active;

    if( renderer->nk_ui )
        torirs_nk_metal_resize(renderer->nk_ui, nk_win_w, nk_win_h);

    if( renderer->nk_ui && renderer->trspk->pass_state.encoder )
    {
        double const frame_dt = torirs_nk_ui_frame_delta_sec(&renderer->nk_ui_prev_perf);
        TorirsNkDebugPanelParams params = {};
        params.window_title = "Info";
        params.delta_time_sec = frame_dt;
        params.view_w_cap = drawable_w;
        params.view_h_cap = drawable_h;
        params.sdl_window = renderer->platform->window;
        params.soft3d = nullptr;
        params.include_soft3d_extras = 0;
        params.include_gpu_frame_stats = 1;
        params.gpu_model_draws = 0u;
        params.gpu_tris = 0u;
        params.gpu_submitted_model_draws = 0u;
        params.gpu_pose_invalid_skips = 0u;
        params.gpu_dynamic_index_draws = 0u;
        struct nk_context* nk_ctx = torirs_nk_metal_ctx(renderer->nk_ui);
        torirs_nk_debug_panel_draw(nk_ctx, game, &params);
        torirs_nk_ui_after_frame(nk_ctx);
        torirs_nk_metal_render(
            renderer->nk_ui,
            renderer->trspk->pass_state.encoder,
            nk_win_w,
            nk_win_h,
            drawable_w,
            drawable_h,
            NK_ANTI_ALIASING_ON);
    }

    TRSPK_Metal_FrameEnd(renderer->trspk);
    LibToriRS_FrameEnd(game);
}

#endif
