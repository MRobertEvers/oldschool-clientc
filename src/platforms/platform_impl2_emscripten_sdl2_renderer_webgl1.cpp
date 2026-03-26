#include "platform_impl2_emscripten_sdl2_renderer_webgl1.h"

#include "graphics/dash.h"
#include "tori_rs_render.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <GLES2/gl2.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <emscripten.h>

struct GLViewportRect
{
    int x;
    int y;
    int width;
    int height;
};

struct LogicalViewportRect
{
    int x;
    int y;
    int width;
    int height;
};

static LogicalViewportRect
compute_logical_viewport_rect(
    int window_width,
    int window_height,
    const struct GGame* game)
{
    LogicalViewportRect rect = { 0, 0, window_width, window_height };
    if( window_width <= 0 || window_height <= 0 || !game || !game->view_port )
        return rect;

    int x = game->viewport_offset_x;
    int y = game->viewport_offset_y;
    int w = game->view_port->width;
    int h = game->view_port->height;
    if( w <= 0 || h <= 0 )
        return rect;

    if( x < 0 )
        x = 0;
    if( y < 0 )
        y = 0;
    if( x >= window_width || y >= window_height )
        return rect;
    if( x + w > window_width )
        w = window_width - x;
    if( y + h > window_height )
        h = window_height - y;
    if( w <= 0 || h <= 0 )
        return rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    return rect;
}

static GLViewportRect
compute_world_viewport_rect(
    int framebuffer_width,
    int framebuffer_height,
    int window_width,
    int window_height,
    const LogicalViewportRect& logical_rect)
{
    GLViewportRect rect = { 0, 0, framebuffer_width, framebuffer_height };
    if( framebuffer_width <= 0 || framebuffer_height <= 0 || window_width <= 0 ||
        window_height <= 0 )
        return rect;

    const double scale_x =
        (window_width > 0) ? ((double)framebuffer_width / (double)window_width) : 1.0;
    const double scale_y =
        (window_height > 0) ? ((double)framebuffer_height / (double)window_height) : 1.0;

    int scaled_x = (int)lround((double)logical_rect.x * scale_x);
    int scaled_top_y = (int)lround((double)logical_rect.y * scale_y);
    int scaled_w = (int)lround((double)logical_rect.width * scale_x);
    int scaled_h = (int)lround((double)logical_rect.height * scale_y);

    int clamped_x = scaled_x < 0 ? 0 : scaled_x;
    int clamped_top_y = scaled_top_y < 0 ? 0 : scaled_top_y;
    if( clamped_x >= framebuffer_width || clamped_top_y >= framebuffer_height )
        return rect;

    int clamped_w = scaled_w;
    int clamped_h = scaled_h;
    if( clamped_x + clamped_w > framebuffer_width )
        clamped_w = framebuffer_width - clamped_x;
    if( clamped_top_y + clamped_h > framebuffer_height )
        clamped_h = framebuffer_height - clamped_top_y;
    if( clamped_w <= 0 || clamped_h <= 0 )
        return rect;

    rect.x = clamped_x;
    rect.y = framebuffer_height - (clamped_top_y + clamped_h);
    rect.width = clamped_w;
    rect.height = clamped_h;
    return rect;
}

static float
yaw_to_radians(int yaw_r2pi2048)
{
    return (yaw_r2pi2048 * 2.0f * 3.14159265358979323846f) / 2048.0f;
}

static uintptr_t
model_gpu_cache_key(const struct DashModel* model)
{
    if( !model )
        return 0;
#if UINTPTR_MAX == 0xffffffffu
    uintptr_t key = 2166136261u;
#else
    uintptr_t key = 1469598103934665603ull;
#endif
    const uintptr_t fnv_prime = (uintptr_t)16777619u;
    auto mix_word = [&](uintptr_t word) {
        key ^= word;
        key *= fnv_prime;
    };

    mix_word((uintptr_t)model->vertices_x);
    mix_word((uintptr_t)model->face_indices_a);
    mix_word((uintptr_t)model->face_indices_b);
    mix_word((uintptr_t)model->face_indices_c);
    mix_word((uintptr_t)model->face_count);

    // Animated models are deformed in-place, so pointer-based keys are not enough:
    // hash current vertex contents to get a distinct GPU mesh per keyframe pose.
    const bool is_animated = model->original_vertices_x && model->original_vertices_y &&
                             model->original_vertices_z && model->vertex_count > 0;
    if( is_animated )
    {
        for( int i = 0; i < model->vertex_count; ++i )
        {
            mix_word((uintptr_t)(uint32_t)model->vertices_x[i]);
            mix_word((uintptr_t)(uint32_t)model->vertices_y[i]);
            mix_word((uintptr_t)(uint32_t)model->vertices_z[i]);
        }
    }
    return key;
}

static void
render_imgui_overlay(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 220), ImGuiCond_FirstUseEver);
    ImGui::Begin("WebGL1 Debug");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    ImGui::Text(
        "Camera: %d %d %d", game->camera_world_x, game->camera_world_y, game->camera_world_z);
    ImGui::Text("Mouse: %d %d", game->mouse_x, game->mouse_y);
    if( game->view_port )
    {
        ImGui::Separator();
        int w = game->view_port->width;
        int h = game->view_port->height;
        bool changed = ImGui::InputInt("World viewport W", &w);
        changed |= ImGui::InputInt("World viewport H", &h);
        if( changed )
        {
            int mw = renderer->max_width > 0 ? renderer->max_width : 4096;
            int mh = renderer->max_height > 0 ? renderer->max_height : 4096;
            if( w > mw )
                w = mw;
            if( h > mh )
                h = mh;
            LibToriRS_GameSetWorldViewportSize(game, w, h);
        }
    }
    ImGui::Text("Loaded model keys: %zu", renderer->loaded_model_keys.size());
    ImGui::Text("Loaded scene keys: %zu", renderer->loaded_scene_element_keys.size());
    ImGui::Text("Loaded textures: %zu", renderer->loaded_texture_ids.size());
    ImGui::End();

    ImGui::Render();
    glDisable(GL_DEPTH_TEST);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    pix3dgl_restore_gl_state_after_imgui(renderer->pix3dgl);
}

struct LetterboxRect
{
    int x, y, w, h;
};

/* Returns a centered rect that fits game_w×game_h inside canvas_w×canvas_h
 * while preserving the game's aspect ratio (letterbox / pillarbox). */
static LetterboxRect
compute_letterbox_rect(int canvas_w, int canvas_h, int game_w, int game_h)
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

static void
sync_canvas_size(struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer->platform )
        return;

    Platform2_Emscripten_SDL2_SyncCanvasCssSize(
        renderer->platform, renderer->platform->current_game);

    int new_w = renderer->platform->drawable_width;
    int new_h = renderer->platform->drawable_height;
    if( new_w <= 0 || new_h <= 0 )
        return;

    if( new_w != renderer->width || new_h != renderer->height )
    {
        renderer->width = new_w;
        renderer->height = new_h;
        glViewport(0, 0, renderer->width, renderer->height);
    }
}

struct Platform2_Emscripten_SDL2_Renderer_WebGL1*
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_New(
    int width,
    int height,
    int max_width,
    int max_height)
{
    auto* renderer = new Platform2_Emscripten_SDL2_Renderer_WebGL1();
    renderer->gl_context = NULL;
    renderer->platform = NULL;
    renderer->width = width;
    renderer->height = height;
    renderer->max_width = max_width;
    renderer->max_height = max_height;
    renderer->webgl_context_ready = false;
    renderer->pix3dgl = NULL;
    renderer->next_model_index = 1;
    return renderer;
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Free(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer)
{
    if( !renderer )
        return;
    if( renderer->pix3dgl )
    {
        pix3dgl_cleanup(renderer->pix3dgl);
        renderer->pix3dgl = NULL;
    }
    delete renderer;
}

bool
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Init(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer,
    struct Platform2_Emscripten_SDL2* platform)
{
    if( !renderer || !platform )
        return false;

    renderer->platform = platform;

    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha = 1;
    attrs.depth = 1;
    attrs.stencil = 0;
    attrs.antialias = 0;
    attrs.premultipliedAlpha = 0;
    attrs.preserveDrawingBuffer = 0;
    attrs.enableExtensionsByDefault = 1;
    attrs.majorVersion = 1;
    attrs.minorVersion = 0;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if( ctx <= 0 )
    {
        printf("WebGL1 init failed: could not create context\n");
        return false;
    }
    if( emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS )
    {
        printf("WebGL1 init failed: could not make context current\n");
        return false;
    }

    renderer->gl_context = (SDL_GLContext)ctx;
    renderer->webgl_context_ready = true;
    emscripten_set_canvas_element_size("#canvas", renderer->width, renderer->height);
    glViewport(0, 0, renderer->width, renderer->height);
    renderer->pix3dgl = pix3dgl_new(false, true);
    if( !renderer->pix3dgl )
    {
        printf("WebGL1 init failed: Pix3DGL setup failed\n");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if( !ImGui_ImplSDL2_InitForOpenGL(platform->window, renderer->gl_context) )
    {
        printf("ImGui SDL2 init failed for WebGL1\n");
        return false;
    }
    if( !ImGui_ImplOpenGL3_Init("#version 100") )
    {
        printf("ImGui OpenGL3 init failed for WebGL1\n");
        return false;
    }

    return true;
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_WebGL1_Render(
    struct Platform2_Emscripten_SDL2_Renderer_WebGL1* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    static uint64_t s_frame = 0;
    if( !renderer || !renderer->webgl_context_ready || !renderer->pix3dgl || !game ||
        !render_command_buffer )
        return;

    emscripten_webgl_make_context_current((EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)renderer->gl_context);
    sync_canvas_size(renderer);

    glViewport(0, 0, renderer->width, renderer->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Logical window size — same source as OpenGL3 (platform game_screen + SDL fallback).
     * Use for compute_logical_viewport_rect, compute_world_viewport_rect scaling, and
     * pix3dgl_sprite_draw. Drawable size stays renderer->width/height (framebuffer). */
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
    /* Letterbox the fixed output (game_screen_width × game_screen_height) into the
     * canvas (renderer->width × renderer->height), preserving aspect ratio. */
    const LetterboxRect lb =
        compute_letterbox_rect(renderer->width, renderer->height, window_width, window_height);
    /* GL Y origin of the letterbox (GL y=0 is bottom of canvas). */
    const int lb_gl_y = renderer->height - lb.y - lb.h;

    const LogicalViewportRect logical_viewport =
        compute_logical_viewport_rect(window_width, window_height, game);
    /* Scale the world sub-viewport from output space into the letterbox area. */
    const GLViewportRect world_vp_in_lb = compute_world_viewport_rect(
        lb.w, lb.h, window_width, window_height, logical_viewport);
    const GLViewportRect world_viewport = { lb.x + world_vp_in_lb.x,
                                            lb_gl_y + world_vp_in_lb.y,
                                            world_vp_in_lb.width,
                                            world_vp_in_lb.height };
    glViewport(world_viewport.x, world_viewport.y, world_viewport.width, world_viewport.height);

    const float projection_width = (float)logical_viewport.width;
    const float projection_height = (float)logical_viewport.height;

    pix3dgl_set_animation_clock(renderer->pix3dgl, (float)((uint64_t)emscripten_get_now() / 20));
    pix3dgl_begin_3dframe(
        renderer->pix3dgl,
        (float)0,
        (float)0,
        (float)0,
        (float)game->camera_pitch,
        (float)game->camera_yaw,
        projection_width,
        projection_height);

    LibToriRS_FrameBegin(game, render_command_buffer);

    /* Sprite commands must be deferred until after pix3dgl_end_3dframe. Everything else
     * (texture loads, model loads, model draws) is processed inline so that the projection
     * result written into sys_dash by FrameNextCommand (project_models=true) can be consumed
     * immediately by dash3d_prepare_projected_face_order without a second project call. */
    static std::vector<ToriRSRenderCommand> sprite_cmds;
    sprite_cmds.clear();

    {
        struct ToriRSRenderCommand cmd = { 0 };
        while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
        {
            switch( cmd.kind )
            {
            case TORIRS_GFX_TEXTURE_LOAD:
            {
                renderer->loaded_texture_ids.insert(cmd._texture_load.texture_id);
                struct DashTexture* texture = cmd._texture_load.texture_nullable;
                if( texture && texture->texels )
                {
                    pix3dgl_load_texture(
                        renderer->pix3dgl,
                        cmd._texture_load.texture_id,
                        texture->texels,
                        texture->width,
                        texture->height,
                        texture->animation_direction,
                        texture->animation_speed,
                        texture->opaque);
                }
                break;
            }

            case TORIRS_GFX_MODEL_LOAD:
            {
                struct DashModel* model = cmd._model_load.model;
                if( !model || !model->lighting || !model->vertices_x || !model->vertices_y ||
                    !model->vertices_z || !model->face_indices_a || !model->face_indices_b ||
                    !model->face_indices_c || model->face_count <= 0 )
                {
                    break;
                }
                uintptr_t model_key = model_gpu_cache_key(model);
                renderer->loaded_model_keys.insert(model_key);
                if( renderer->model_index_by_key.find(model_key) ==
                    renderer->model_index_by_key.end() )
                {
                    int model_idx = renderer->next_model_index++;
                    renderer->model_index_by_key[model_key] = model_idx;
                    pix3dgl_model_load(
                        renderer->pix3dgl,
                        model_idx,
                        model->vertices_x,
                        model->vertices_y,
                        model->vertices_z,
                        model->face_indices_a,
                        model->face_indices_b,
                        model->face_indices_c,
                        model->face_count,
                        model->face_textures,
                        model->face_texture_coords,
                        model->textured_p_coordinate,
                        model->textured_m_coordinate,
                        model->textured_n_coordinate,
                        model->lighting->face_colors_hsl_a,
                        model->lighting->face_colors_hsl_b,
                        model->lighting->face_colors_hsl_c,
                        model->face_infos,
                        model->face_alphas);
                }
                break;
            }

            case TORIRS_GFX_MODEL_DRAW:
            {
                struct DashModel* model = cmd._model_draw.model;
                if( !model || !model->lighting || !model->vertices_x || !model->vertices_y ||
                    !model->vertices_z || !model->face_indices_a || !model->face_indices_b ||
                    !model->face_indices_c || model->face_count <= 0 )
                {
                    break;
                }

                uintptr_t model_key = model_gpu_cache_key(model);
                auto it = renderer->model_index_by_key.find(model_key);
                if( it == renderer->model_index_by_key.end() )
                {
                    int model_idx = renderer->next_model_index++;
                    renderer->model_index_by_key[model_key] = model_idx;
                    renderer->loaded_model_keys.insert(model_key);
                    pix3dgl_model_load(
                        renderer->pix3dgl,
                        model_idx,
                        model->vertices_x,
                        model->vertices_y,
                        model->vertices_z,
                        model->face_indices_a,
                        model->face_indices_b,
                        model->face_indices_c,
                        model->face_count,
                        model->face_textures,
                        model->face_texture_coords,
                        model->textured_p_coordinate,
                        model->textured_m_coordinate,
                        model->textured_n_coordinate,
                        model->lighting->face_colors_hsl_a,
                        model->lighting->face_colors_hsl_b,
                        model->lighting->face_colors_hsl_c,
                        model->face_infos,
                        model->face_alphas);
                    it = renderer->model_index_by_key.find(model_key);
                }

                /* FrameNextCommand already projected this model (project_models=true).
                 * The sys_dash projection state is still valid — use it directly. */
                struct DashPosition draw_position = cmd._model_draw.position;
                int face_order_count = dash3d_prepare_projected_face_order(
                    game->sys_dash, model, &draw_position, game->view_port, game->camera);
                const int* face_order =
                    dash3d_projected_face_order(game->sys_dash, &face_order_count);
                pix3dgl_model_draw_ordered(
                    renderer->pix3dgl,
                    it->second,
                    (float)draw_position.x,
                    (float)draw_position.y,
                    (float)draw_position.z,
                    yaw_to_radians(draw_position.yaw),
                    face_order,
                    face_order_count);
                break;
            }

            case TORIRS_GFX_SPRITE_LOAD:
            case TORIRS_GFX_SPRITE_UNLOAD:
            case TORIRS_GFX_SPRITE_DRAW:
                sprite_cmds.push_back(cmd);
                break;

            default:
                break;
            }
        }
    }

    glViewport(world_viewport.x, world_viewport.y, world_viewport.width, world_viewport.height);
    pix3dgl_end_3dframe(renderer->pix3dgl);

    /* Sprites are authored in output (game_screen) space; map them into the letterbox
     * area of the canvas so they scale with the 3D world. */
    glViewport(lb.x, lb_gl_y, lb.w, lb.h);

    for( const auto& sc : sprite_cmds )
    {
        if( sc.kind == TORIRS_GFX_SPRITE_LOAD )
        {
            struct DashSprite* sp = sc._sprite_load.sprite;
            if( sp )
                pix3dgl_sprite_load(renderer->pix3dgl, sp);
        }
        else if( sc.kind == TORIRS_GFX_SPRITE_UNLOAD )
        {
            struct DashSprite* sp = sc._sprite_load.sprite;
            if( sp )
                pix3dgl_sprite_unload(renderer->pix3dgl, sp);
        }
    }

    pix3dgl_begin_2dframe(renderer->pix3dgl);
    for( const auto& sc : sprite_cmds )
    {
        if( sc.kind != TORIRS_GFX_SPRITE_DRAW )
            continue;
        struct DashSprite* sp = sc._sprite_draw.sprite;
        if( !sp )
            continue;
        /* Sprite coordinates are authored in logical screen space; match OpenGL3 renderer. */
        pix3dgl_sprite_draw(
            renderer->pix3dgl,
            sp,
            sc._sprite_draw.x,
            sc._sprite_draw.y,
            window_width,
            window_height,
            sc._sprite_draw.rotation_r2pi2048);
    }
    pix3dgl_end_2dframe(renderer->pix3dgl);

    LibToriRS_FrameEnd(game);

    glViewport(0, 0, renderer->width, renderer->height);
    render_imgui_overlay(renderer, game);

    if( renderer->platform && renderer->platform->window )
        SDL_GL_SwapWindow(renderer->platform->window);

    s_frame++;
}
