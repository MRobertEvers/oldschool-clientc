#include "platforms/platform_impl2_sdl2_renderer_opengl3.h"

#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_render.h"
#include "tori_rs_render.h"

#include <SDL.h>
#include <cstdio>

struct Platform2_SDL2_Renderer_OpenGL3*
PlatformImpl2_SDL2_Renderer_OpenGL3_New(
    int width,
    int height)
{
    (void)width;
    (void)height;
    return new Platform2_SDL2_Renderer_OpenGL3();
}

void
PlatformImpl2_SDL2_Renderer_OpenGL3_Free(struct Platform2_SDL2_Renderer_OpenGL3* renderer)
{
    if( !renderer )
        return;

    if( renderer->platform && renderer->platform->window && renderer->gl_context )
    {
        SDL_GL_MakeCurrent(renderer->platform->window, renderer->gl_context);
        opengl3_gl_destroy_programs(renderer);
        opengl3_atlas_resources_shutdown(renderer);
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = nullptr;
    }

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
    renderer->gl_ready = true;
    opengl3_sync_drawable_size(renderer);
    glViewport(0, 0, renderer->width, renderer->height);

    if( !opengl3_gl_create_programs(renderer) )
    {
        fprintf(stderr, "OpenGL3 init failed: shader programs\n");
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = nullptr;
        renderer->gl_ready = false;
        return false;
    }

    opengl3_atlas_resources_init(renderer);

    return true;
}

void
PlatformImpl2_SDL2_Renderer_OpenGL3_Render(
    struct Platform2_SDL2_Renderer_OpenGL3* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->gl_ready || !game || !render_command_buffer ||
        !renderer->platform || !renderer->platform->window )
        return;

    struct Platform2_SDL2* platform = renderer->platform;
    if( SDL_GL_MakeCurrent(platform->window, renderer->gl_context) != 0 )
        return;

    opengl3_sync_drawable_size(renderer);

    renderer->diag_frame_model_draw_cmds = 0u;
    renderer->diag_frame_pose_invalid_skips = 0u;
    renderer->diag_frame_submitted_model_draws = 0u;
    renderer->diag_frame_dynamic_index_draws = 0u;

    renderer->pass.uniform_pass_subslot = 0u;

    glViewport(0, 0, renderer->width, renderer->height);
    opengl3_gl_bind_default_world_gl_state();
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int win_width = platform->game_screen_width;
    int win_height = platform->game_screen_height;
    if( win_width <= 0 || win_height <= 0 )
        SDL_GetWindowSize(platform->window, &win_width, &win_height);

    const LogicalViewportRect logical_vp =
        opengl3_compute_logical_viewport_rect(win_width, win_height, game);

    LibToriRS_FrameBegin(game, render_command_buffer);

    renderer->pass.win_width = win_width;
    renderer->pass.win_height = win_height;
    renderer->pass.clear_rect_slot = 0;

    OpenGL3RenderCtx ctx = {};
    ctx.renderer = renderer;
    ctx.game = game;

    opengl3_render_dispatch_commands(&ctx, render_command_buffer, &logical_vp);

    LibToriRS_FrameEnd(game);

    glViewport(0, 0, renderer->width, renderer->height);
    SDL_GL_SwapWindow(platform->window);
}
