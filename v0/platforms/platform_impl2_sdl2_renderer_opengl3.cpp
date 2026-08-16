#include "platforms/platform_impl2_sdl2_renderer_opengl3.h"

#include "platforms/ToriRSPlatformKit/src/backends/opengl3/opengl3_nk_gl_aliases.h"
#include "platforms/common/torirs_nk_ui_bridge.h"
#include "platforms/common/torirs_nuklear_debug_panel.h"

#define TORIRS_NK_SDL_GL3_IMPLEMENTATION
#include "nuklear/backends/sdl_opengl3/nuklear_torirs_sdl_gl3.h"

#include <cstdio>

extern "C" {
#include "osrs/game.h"
#include "platforms/ToriRSPlatformKit/include/ToriRSPlatformKit/trspk_math.h"
#include "tori_rs.h"
#include "tori_rs_render.h"
}

static constexpr float kTorirsNkGl3DefaultFontPx = 13.0f;

struct Platform2_SDL2_Renderer_OpenGL3*
PlatformImpl2_SDL2_Renderer_OpenGL3_New(
    int width,
    int height)
{
    Platform2_SDL2_Renderer_OpenGL3* renderer = new Platform2_SDL2_Renderer_OpenGL3();
    renderer->width = width;
    renderer->height = height;
    return renderer;
}

void
PlatformImpl2_SDL2_Renderer_OpenGL3_Free(struct Platform2_SDL2_Renderer_OpenGL3* renderer)
{
    if( !renderer )
        return;
    if( renderer->platform && renderer->platform->window && renderer->gl_context )
    {
        if( SDL_GL_MakeCurrent(renderer->platform->window, renderer->gl_context) == 0 )
        {
            if( renderer->nk_ctx )
            {
                torirs_nk_ui_clear_active();
                torirs_nk_gl3_shutdown();
                renderer->nk_ctx = nullptr;
            }
        }
    }
    if( renderer->trspk )
        TRSPK_OpenGL3_Shutdown(renderer->trspk);
    renderer->trspk = nullptr;
    if( renderer->gl_context )
        SDL_GL_DeleteContext(renderer->gl_context);
    renderer->gl_context = nullptr;
    delete renderer;
}

bool
PlatformImpl2_SDL2_Renderer_OpenGL3_Init(
    struct Platform2_SDL2_Renderer_OpenGL3* renderer,
    struct Platform2_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;
    renderer->platform = platform;

    renderer->gl_context = SDL_GL_CreateContext(platform->window);
    if( !renderer->gl_context )
    {
        fprintf(stderr, "OpenGL3 init failed: could not create context: %s\n", SDL_GetError());
        return false;
    }
    if( SDL_GL_MakeCurrent(platform->window, renderer->gl_context) != 0 )
    {
        fprintf(
            stderr, "OpenGL3 init failed: could not make context current: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = nullptr;
        return false;
    }

    SDL_GL_SetSwapInterval(0);

    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_GL_GetDrawableSize(platform->window, &drawable_w, &drawable_h);
    renderer->width = drawable_w;
    renderer->height = drawable_h;

    renderer->trspk =
        TRSPK_OpenGL3_InitWithCurrentContext((uint32_t)drawable_w, (uint32_t)drawable_h);
    if( !renderer->trspk || !renderer->trspk->ready )
    {
        fprintf(stderr, "OpenGL3 init failed: TRSPK_OpenGL3_InitWithCurrentContext\n");
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = nullptr;
        return false;
    }

    int nk_win_w = 0;
    int nk_win_h = 0;
    SDL_GetWindowSize(platform->window, &nk_win_w, &nk_win_h);
    if( nk_win_w <= 0 || nk_win_h <= 0 )
    {
        nk_win_w = platform->game_screen_width > 0 ? platform->game_screen_width : drawable_w;
        nk_win_h = platform->game_screen_height > 0 ? platform->game_screen_height : drawable_h;
    }

    renderer->nk_ctx = torirs_nk_gl3_init(platform->window);
    if( !renderer->nk_ctx )
    {
        fprintf(stderr, "OpenGL3: Nuklear GL3 init failed\n");
        TRSPK_OpenGL3_Shutdown(renderer->trspk);
        renderer->trspk = nullptr;
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = nullptr;
        return false;
    }
    {
        struct nk_font_atlas* atlas = nullptr;
        torirs_nk_gl3_font_stash_begin(&atlas);
        nk_font_atlas_add_default(
            atlas, kTorirsNkGl3DefaultFontPx * platform->display_scale, nullptr);
        torirs_nk_gl3_font_stash_end();
    }
    torirs_nk_ui_set_active(
        renderer->nk_ctx, torirs_nk_gl3_handle_event, torirs_nk_gl3_handle_grab);
    renderer->nk_ui_prev_perf = SDL_GetPerformanceCounter();

    renderer->gl_ready = true;
    return true;
}

void
PlatformImpl2_SDL2_Renderer_OpenGL3_Render(
    struct Platform2_SDL2_Renderer_OpenGL3* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->gl_ready || !renderer->trspk || !game || !render_command_buffer ||
        !renderer->platform || !renderer->platform->window )
        return;

    struct Platform2_SDL2* platform = renderer->platform;
    if( SDL_GL_MakeCurrent(platform->window, renderer->gl_context) != 0 )
        return;

    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_GL_GetDrawableSize(platform->window, &drawable_w, &drawable_h);
    if( drawable_w != renderer->width || drawable_h != renderer->height )
    {
        renderer->width = drawable_w;
        renderer->height = drawable_h;
        TRSPK_OpenGL3_Resize(renderer->trspk, (uint32_t)drawable_w, (uint32_t)drawable_h);
    }

    SDL_GetWindowSize(platform->window, &platform->window_width, &platform->window_height);
    platform->drawable_width = drawable_w;
    platform->drawable_height = drawable_h;
    {
        const int lw = platform->window_width;
        const int lh = platform->window_height;
        if( lw > 0 && lh > 0 )
        {
            const float sx = (float)drawable_w / (float)lw;
            const float sy = (float)drawable_h / (float)lh;
            platform->display_scale = sx > sy ? sx : sy;
        }
    }

    int win_w = platform->game_screen_width;
    int win_h = platform->game_screen_height;
    if( win_w <= 0 || win_h <= 0 )
        SDL_GetWindowSize(platform->window, &win_w, &win_h);
    if( win_w > 0 && win_h > 0 )
        TRSPK_OpenGL3_SetWindowSize(renderer->trspk, (uint32_t)win_w, (uint32_t)win_h);

    int nk_win_w = 0;
    int nk_win_h = 0;
    SDL_GetWindowSize(platform->window, &nk_win_w, &nk_win_h);
    if( nk_win_w <= 0 || nk_win_h <= 0 )
    {
        nk_win_w = win_w > 0 ? win_w : drawable_w;
        nk_win_h = win_h > 0 ? win_h : drawable_h;
    }

    TRSPK_ResourceCache* cache = TRSPK_OpenGL3_GetCache(renderer->trspk);
    TRSPK_Batch32* staging = TRSPK_OpenGL3_GetBatchStaging(renderer->trspk);
    TRSPK_OpenGL3EventContext events = {};
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

    TRSPK_OpenGL3_FrameBegin(renderer->trspk);

    trspk_glViewport(0, 0, (TRSPK_GLsizei)drawable_w, (TRSPK_GLsizei)drawable_h);
    trspk_glDisable(GL_SCISSOR_TEST);
    trspk_glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    trspk_glClearDepthf(1.0f);
    trspk_glDepthMask((TRSPK_GLboolean)1);
    trspk_glClear((TRSPK_GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    Uint64 const frame_work_t0 = SDL_GetPerformanceCounter();
    LibToriRS_FrameBegin(game, render_command_buffer);

    struct ToriRSRenderCommand cmd = { 0 };
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
    {
        switch( cmd.kind )
        {
        case TORIRS_GFX_RES_MODEL_LOAD:
            trspk_opengl3_event_res_model_load(&events, &cmd);
            break;
        case TORIRS_GFX_RES_MODEL_UNLOAD:
            trspk_opengl3_event_res_model_unload(&events, &cmd);
            break;
        case TORIRS_GFX_RES_TEX_LOAD:
            trspk_opengl3_event_tex_load(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_BEGIN:
            trspk_opengl3_event_batch3d_begin(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_MODEL_ADD:
            trspk_opengl3_event_batch3d_model_add(&events, &cmd);
            break;
        case TORIRS_GFX_RES_ANIM_LOAD:
            trspk_opengl3_event_res_anim_load(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_ANIM_ADD:
            trspk_opengl3_event_batch3d_anim_add(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_END:
            trspk_opengl3_event_batch3d_end(&events, &cmd);
            break;
        case TORIRS_GFX_BATCH3D_CLEAR:
            trspk_opengl3_event_batch3d_clear(&events, &cmd);
            break;
        case TORIRS_GFX_DRAW_MODEL:
            trspk_opengl3_event_draw_model(
                &events, game, &cmd, renderer->facebuffer.indices, TRSPK_FACEBUFFER_INDEX_CAPACITY);
            break;
        case TORIRS_GFX_STATE_BEGIN_3D:
            trspk_opengl3_event_state_begin_3d(&events, &cmd);
            break;
        case TORIRS_GFX_STATE_CLEAR_RECT:
            trspk_opengl3_event_state_clear_rect(&events, &cmd);
            break;
        case TORIRS_GFX_STATE_END_3D:
            trspk_opengl3_event_state_end_3d(&events, game, (double)game->cycle);
            break;
        default:
            break;
        }
    }
    renderer->current_model_batch_id = events.current_batch_id;
    renderer->current_model_batch_active = events.batch_active;

    if( renderer->nk_ctx )
    {
        double const frame_work_avg_ms = torirs_nk_ui_frame_work_avg_ms();
        double const frame_dt = torirs_nk_ui_frame_delta_sec(&renderer->nk_ui_prev_perf);
        TorirsNkDebugPanelParams params = {};
        params.window_title = "Info";
        params.delta_time_sec = frame_dt;
        params.view_w_cap = drawable_w;
        params.view_h_cap = drawable_h;
        params.sdl_window = platform->window;
        params.soft3d = nullptr;
        params.include_soft3d_extras = 0;
        params.include_gpu_frame_stats = 1;
        params.gpu_model_draws = 0u;
        params.gpu_tris = 0u;
        params.gpu_submitted_model_draws = renderer->trspk->diag_frame_submitted_draws;
        params.gpu_pose_invalid_skips = 0u;
        params.gpu_dynamic_index_draws = 0u;
        if( frame_work_avg_ms >= 0.0 )
        {
            params.include_frame_work_timing = 1;
            params.frame_work_avg_ms = frame_work_avg_ms;
        }
        torirs_nk_debug_panel_draw(renderer->nk_ctx, game, &params);
        torirs_nk_ui_after_frame(renderer->nk_ctx);
        trspk_glViewport(0, 0, (TRSPK_GLsizei)drawable_w, (TRSPK_GLsizei)drawable_h);
        torirs_nk_gl3_render(NK_ANTI_ALIASING_ON, 512 * 1024, 128 * 1024);
    }

    Uint64 const frame_work_t1 = SDL_GetPerformanceCounter();
    Uint64 const frame_work_freq = SDL_GetPerformanceFrequency();
    double const frame_work_sec =
        frame_work_freq ? (double)(frame_work_t1 - frame_work_t0) / (double)frame_work_freq : 0.0;
    torirs_nk_ui_frame_work_push_sec(frame_work_sec);

    TRSPK_OpenGL3_FrameEnd(renderer->trspk);
    LibToriRS_FrameEnd(game);

    SDL_GL_SwapWindow(platform->window);
}
