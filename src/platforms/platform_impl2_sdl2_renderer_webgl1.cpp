#ifdef __EMSCRIPTEN__

#include "platforms/platform_impl2_sdl2_renderer_webgl1.h"

#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"

#define TORIRS_NK_SDL_GLES2_IMPLEMENTATION
#include "nuklear/backends/sdl_opengles2/nuklear_torirs_sdl_gles2.h"

#include <cstdio>

extern "C" {
#include "graphics/dash.h"
#include "osrs/game.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

static struct nk_context* s_webgl1_nk = nullptr;
static Uint64 s_webgl1_ui_prev_perf = 0;

Platform2_SDL2_Renderer_WebGL1*
PlatformImpl2_SDL2_Renderer_WebGL1_New(
    int width,
    int height)
{
    Platform2_SDL2_Renderer_WebGL1* renderer = new Platform2_SDL2_Renderer_WebGL1();
    renderer->width = width;
    renderer->height = height;
    return renderer;
}

void
PlatformImpl2_SDL2_Renderer_WebGL1_Free(Platform2_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer )
        return;
    if( renderer->platform && renderer->platform->window && renderer->gl_context )
    {
        if( SDL_GL_MakeCurrent(renderer->platform->window, renderer->gl_context) == 0 )
        {
            if( s_webgl1_nk )
            {
                torirs_nk_ui_clear_active();
                torirs_nk_gles2_shutdown();
                s_webgl1_nk = nullptr;
            }
        }
    }
    if( renderer->trspk )
        TRSPK_WebGL1_Shutdown(renderer->trspk);
    renderer->trspk = nullptr;
    if( renderer->gl_context )
        SDL_GL_DeleteContext(renderer->gl_context);
    renderer->gl_context = nullptr;
    delete renderer;
}

bool
PlatformImpl2_SDL2_Renderer_WebGL1_Init(
    Platform2_SDL2_Renderer_WebGL1* renderer,
    struct Platform2_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;
    renderer->platform = platform;
    renderer->gl_context = SDL_GL_CreateContext(platform->window);
    if( !renderer->gl_context )
        return false;
    SDL_GL_MakeCurrent(platform->window, renderer->gl_context);
    SDL_GL_GetDrawableSize(platform->window, &renderer->width, &renderer->height);
    renderer->trspk =
        TRSPK_WebGL1_InitWithCurrentContext((uint32_t)renderer->width, (uint32_t)renderer->height);
    if( !renderer->trspk || !renderer->trspk->ready )
    {
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = nullptr;
        return false;
    }

    s_webgl1_nk = torirs_nk_gles2_init(platform->window);
    if( !s_webgl1_nk )
    {
        fprintf(stderr, "WebGL1: Nuklear GLES2 init failed\n");
        TRSPK_WebGL1_Shutdown(renderer->trspk);
        renderer->trspk = nullptr;
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = nullptr;
        return false;
    }
    {
        struct nk_font_atlas* atlas = nullptr;
        torirs_nk_gles2_font_stash_begin(&atlas);
        nk_font_atlas_add_default(atlas, 13.0f * platform->display_scale, nullptr);
        torirs_nk_gles2_font_stash_end();
    }
    torirs_nk_ui_set_active(s_webgl1_nk, torirs_nk_gles2_handle_event, torirs_nk_gles2_handle_grab);
    s_webgl1_ui_prev_perf = SDL_GetPerformanceCounter();

    renderer->gl_ready = true;
    return true;
}

void
PlatformImpl2_SDL2_Renderer_WebGL1_Render(
    Platform2_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->gl_ready || !renderer->trspk || !game || !render_command_buffer )
        return;
    SDL_GL_MakeCurrent(renderer->platform->window, renderer->gl_context);
    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_GL_GetDrawableSize(renderer->platform->window, &drawable_w, &drawable_h);
    if( drawable_w != renderer->width || drawable_h != renderer->height )
    {
        renderer->width = drawable_w;
        renderer->height = drawable_h;
        TRSPK_WebGL1_Resize(renderer->trspk, (uint32_t)drawable_w, (uint32_t)drawable_h);
    }
    int win_width = renderer->platform->game_screen_width;
    int win_height = renderer->platform->game_screen_height;
    if( win_width <= 0 || win_height <= 0 )
        SDL_GetWindowSize(renderer->platform->window, &win_width, &win_height);
    if( win_width > 0 && win_height > 0 )
        TRSPK_WebGL1_SetWindowSize(renderer->trspk, (uint32_t)win_width, (uint32_t)win_height);

    TRSPK_ResourceCache* cache = TRSPK_WebGL1_GetCache(renderer->trspk);
    TRSPK_Batch16* staging = TRSPK_WebGL1_GetBatchStaging(renderer->trspk);
    TRSPK_WebGL1EventContext events = {};
    events.renderer = renderer->trspk;
    events.cache = cache;
    events.staging = staging;
    events.current_batch_id = renderer->current_model_batch_id;
    events.batch_active = renderer->current_model_batch_active;
    if( win_width > 0 && win_height > 0 && game )
    {
        trspk_webgl1_compute_default_pass_logical(
            &events.default_pass_logical, win_width, win_height, game);
        events.has_default_pass_logical =
            events.default_pass_logical.width > 0 && events.default_pass_logical.height > 0;
    }
    else
    {
        events.has_default_pass_logical = false;
    }

    renderer->diag_frame_model_draw_cmds = 0u;
    renderer->diag_frame_pose_invalid_skips = 0u;
    renderer->diag_frame_submitted_model_draws = 0u;
    LibToriRS_FrameBegin(game, render_command_buffer);
    TRSPK_WebGL1_FrameBegin(renderer->trspk);

    struct ToriRSRenderCommand cmd = { 0 };
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
    {
        switch( cmd.kind )
        {
        case TORIRS_GFX_RES_MODEL_LOAD:
            trspk_webgl1_event_res_model_load(&events, &cmd);
            break;
        case TORIRS_GFX_RES_MODEL_UNLOAD:
            trspk_webgl1_event_res_model_unload(&events, &cmd);
            break;
        case TORIRS_GFX_RES_TEX_LOAD:
            trspk_webgl1_event_tex_load(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_BEGIN:
            trspk_webgl1_event_batch3d_begin(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_MODEL_ADD:
            trspk_webgl1_event_batch3d_model_add(&events, &cmd);
            break;
        case TORIRS_GFX_RES_ANIM_LOAD:
            if( cmd._animation_load.usage_hint == TORIRS_USAGE_SCENERY )
                trspk_webgl1_event_batch3d_anim_add(&events, &cmd);
            else
                trspk_webgl1_event_res_anim_load(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_ANIM_ADD:
            trspk_webgl1_event_batch3d_anim_add(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_END:
            trspk_webgl1_event_batch3d_end(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_CLEAR:
            trspk_webgl1_event_batch3d_clear(&events, &cmd);
            break;
        case TORIRS_GFX_DRAW_MODEL:
            trspk_webgl1_event_draw_model(
                &events, game, &cmd, renderer->facebuffer.indices, TRSPK_FACEBUFFER_INDEX_CAPACITY);
            break;
        case TORIRS_GFX_STATE_BEGIN_3D:
            trspk_webgl1_event_state_begin_3d(&events, &cmd);
            break;
        case TORIRS_GFX_STATE_CLEAR_RECT:
            trspk_webgl1_event_state_clear_rect(&events, &cmd);
            break;
        case TORIRS_GFX_STATE_END_3D:
            trspk_webgl1_event_state_end_3d(&events, game, (double)game->cycle);
            break;
        default:
            break;
        }
    }
    renderer->current_model_batch_id = events.current_batch_id;
    renderer->current_model_batch_active = events.batch_active;
    renderer->diag_frame_model_draw_cmds = events.diag_frame_model_draw_cmds;
    renderer->diag_frame_pose_invalid_skips = events.diag_frame_pose_invalid_skips;
    renderer->diag_frame_submitted_model_draws = events.diag_frame_submitted_model_draws;
    TRSPK_WebGL1_FrameEnd(renderer->trspk);

    if( s_webgl1_nk )
    {
        double const dt = torirs_nk_ui_frame_delta_sec(&s_webgl1_ui_prev_perf);
        TorirsNkDebugPanelParams params = {};
        params.window_title = "Info";
        params.delta_time_sec = dt;
        params.view_w_cap = drawable_w;
        params.view_h_cap = drawable_h;
        params.sdl_window = renderer->platform->window;
        params.soft3d = nullptr;
        params.include_soft3d_extras = 0;
        params.include_gpu_frame_stats = 1;
        params.gpu_model_draws = renderer->diag_frame_model_draw_cmds;
        params.gpu_tris = 0u;
        params.gpu_submitted_model_draws = renderer->diag_frame_submitted_model_draws;
        params.gpu_pose_invalid_skips = renderer->diag_frame_pose_invalid_skips;
        params.gpu_dynamic_index_draws = 0u;
        torirs_nk_debug_panel_draw(s_webgl1_nk, game, &params);
        torirs_nk_ui_after_frame(s_webgl1_nk);
        torirs_nk_gles2_render(NK_ANTI_ALIASING_ON, 512 * 1024, 128 * 1024);
    }

    SDL_GL_SwapWindow(renderer->platform->window);
    LibToriRS_FrameEnd(game);
}

#endif
