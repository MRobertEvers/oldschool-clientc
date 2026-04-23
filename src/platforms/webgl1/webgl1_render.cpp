#include "platforms/webgl1/webgl1_internal.h"

#include "nuklear/backends/sdl_opengles2/nuklear_torirs_sdl_gles2.h"

#include <SDL.h>
#include <emscripten/html5.h>

struct LetterboxRect
{
    int x, y, w, h;
};

static LetterboxRect
compute_letterbox_rect(
    int canvas_w,
    int canvas_h,
    int game_w,
    int game_h)
{
    LetterboxRect r = { 0, 0, canvas_w, canvas_h };
    if( canvas_w <= 0 || canvas_h <= 0 || game_w <= 0 || game_h <= 0 )
        return r;
    const float ca = (float)canvas_w / (float)canvas_h;
    const float ga = (float)game_w / (float)game_h;
    if( ga > ca )
    {
        r.w = canvas_w;
        r.h = (int)((float)canvas_w / ga);
        r.x = 0;
        r.y = (canvas_h - r.h) / 2;
    }
    else
    {
        r.h = canvas_h;
        r.w = (int)((float)canvas_h * ga);
        r.y = 0;
        r.x = (canvas_w - r.w) / 2;
    }
    return r;
}

void
PlatformImpl2_SDL2_Renderer_WebGL1_Render(
    struct Platform2_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->webgl_context_ready || !game || !render_command_buffer )
        return;

    emscripten_webgl_make_context_current((EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)renderer->gl_context);
    sync_canvas_size(renderer);

    glViewport(0, 0, renderer->width, renderer->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int window_width = renderer->platform ? renderer->platform->game_screen_width : 0;
    int window_height = renderer->platform ? renderer->platform->game_screen_height : 0;
    if( window_width <= 0 || window_height <= 0 )
    {
        if( renderer->platform && renderer->platform->window )
            SDL_GetWindowSize(renderer->platform->window, &window_width, &window_height);
    }
    if( window_width <= 0 || window_height <= 0 )
    {
        window_width = renderer->width;
        window_height = renderer->height;
    }

    const LetterboxRect lb =
        compute_letterbox_rect(renderer->width, renderer->height, window_width, window_height);
    const int lb_gl_y = renderer->height - lb.y - lb.h;

    const LogicalViewportRect logical_viewport =
        wg1_compute_logical_viewport_rect(window_width, window_height, game);

    const float projection_width = (float)logical_viewport.width;
    const float projection_height = (float)logical_viewport.height;

    LibToriRS_FrameBegin(game, render_command_buffer);

    BufferedFaceOrder bfo3d_accum;
    BufferedSprite2D bsp2d_accum;
    BufferedFont2D bft2d_accum;
    Buffered2DOrder b2d_order_accum;

    WebGL1RenderCtx ctx = {};
    ctx.renderer = renderer;
    ctx.game = game;
    ctx.win_width = window_width;
    ctx.win_height = window_height;
    ctx.logical_vp = logical_viewport;
    ctx.ui_gl_vp = wg1_compute_gl_world_viewport_rect(
        renderer->width, renderer->height, window_width, window_height, logical_viewport);
    ctx.lb_x = lb.x;
    ctx.lb_gl_y = lb_gl_y;
    ctx.lb_w = lb.w;
    ctx.lb_h = lb.h;
    ctx.bfo3d = &bfo3d_accum;
    ctx.bsp2d = &bsp2d_accum;
    ctx.bft2d = &bft2d_accum;
    ctx.b2d_order = &b2d_order_accum;

    wg1_matrices_for_frame(
        game,
        projection_width,
        projection_height,
        ctx.world_uniforms,
        ctx.world_uniforms + 16);

    {
        struct ToriRSRenderCommand cmd = { 0 };
        while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
        {
            switch( cmd.kind )
            {
            case TORIRS_GFX_BEGIN_3D:
                wg1_frame_event_begin_3d(&ctx, &cmd);
                break;
            case TORIRS_GFX_END_3D:
                wg1_frame_event_end_3d(&ctx, &cmd);
                break;
            case TORIRS_GFX_BEGIN_2D:
                wg1_frame_event_begin_2d(&ctx, &cmd);
                break;
            case TORIRS_GFX_END_2D:
                wg1_frame_event_end_2d(&ctx, &cmd);
                break;
            case TORIRS_GFX_CLEAR_RECT:
                wg1_frame_event_clear_rect(&ctx, &cmd);
                break;
            case TORIRS_GFX_TEXTURE_LOAD:
                wg1_frame_event_texture_load(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH_TEXTURE_LOAD_START:
                wg1_frame_event_batch_texture_load_start(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH_TEXTURE_LOAD_END:
                wg1_frame_event_batch_texture_load_end(&ctx, &cmd);
                break;
            case TORIRS_GFX_MODEL_LOAD:
                wg1_frame_event_model_load(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH3D_MODEL_LOAD:
                wg1_frame_event_model_batched_load(&ctx, &cmd);
                break;
            case TORIRS_GFX_MODEL_UNLOAD:
                wg1_frame_event_model_unload(&ctx, &cmd);
                break;
            case TORIRS_GFX_MODEL_ANIMATION_LOAD:
            case TORIRS_GFX_BATCH3D_MODEL_ANIMATION_LOAD:
                wg1_frame_event_model_animation_load(&ctx, &cmd);
                break;
            case TORIRS_GFX_MODEL_DRAW:
                wg1_frame_event_model_draw(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH3D_LOAD_START:
                wg1_frame_event_batch_model_load_start(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH3D_LOAD_END:
                wg1_frame_event_batch_model_load_end(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH3D_CLEAR:
                wg1_frame_event_batch_model_clear(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH3D_VERTEX_ARRAY_LOAD:
                wg1_frame_event_batch_vertex_array_load(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH3D_FACE_ARRAY_LOAD:
                wg1_frame_event_batch_face_array_load(&ctx, &cmd);
                break;
            case TORIRS_GFX_SPRITE_LOAD:
                wg1_frame_event_sprite_load(&ctx, &cmd);
                break;
            case TORIRS_GFX_SPRITE_UNLOAD:
                wg1_frame_event_sprite_unload(&ctx, &cmd);
                break;
            case TORIRS_GFX_SPRITE_DRAW:
                wg1_frame_event_sprite_draw(&ctx, &cmd);
                break;
            case TORIRS_GFX_FONT_LOAD:
                wg1_frame_event_font_load(&ctx, &cmd);
                break;
            case TORIRS_GFX_FONT_DRAW:
                wg1_frame_event_font_draw(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH_SPRITE_LOAD_START:
                wg1_frame_event_batch_sprite_load_start(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH_SPRITE_LOAD_END:
                wg1_frame_event_batch_sprite_load_end(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH_FONT_LOAD_START:
                wg1_frame_event_batch_font_load_start(&ctx, &cmd);
                break;
            case TORIRS_GFX_BATCH_FONT_LOAD_END:
                wg1_frame_event_batch_font_load_end(&ctx, &cmd);
                break;
            default:
                break;
            }
        }
    }

    wg1_flush_2d(&ctx);
    if( ctx.bfo3d )
        wg1_flush_3d(&ctx, ctx.bfo3d);

    LibToriRS_FrameEnd(game);

    glViewport(0, 0, renderer->width, renderer->height);
    if( s_webgl1_nk )
    {
        double const dt = torirs_nk_ui_frame_delta_sec(&s_webgl1_ui_prev_perf);
        int mw = renderer->max_width > 0 ? renderer->max_width : 4096;
        int mh = renderer->max_height > 0 ? renderer->max_height : 4096;
        TorirsNkDebugPanelParams params = {};
        params.window_title = "WebGL1 Debug";
        params.delta_time_sec = dt;
        params.view_w_cap = mw;
        params.view_h_cap = mh;
        params.include_soft3d_extras = 0;
        params.include_load_counts = 1;
        params.loaded_models = wg1_count_loaded_model_slots(renderer->model_cache);
        params.loaded_scenes = 0;
        params.loaded_textures = renderer->texture_id_ever_loaded_count;
        torirs_nk_debug_panel_draw(s_webgl1_nk, game, &params);
        torirs_nk_ui_after_frame(s_webgl1_nk);
        glDisable(GL_DEPTH_TEST);
        torirs_nk_gles2_render(NK_ANTI_ALIASING_ON, 512 * 1024, 128 * 1024);
        wg1_restore_gl_state_after_ui(renderer);
    }

    if( renderer->platform && renderer->platform->window )
        SDL_GL_SwapWindow(renderer->platform->window);
}
