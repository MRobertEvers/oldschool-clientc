#include "platform_impl2_osx_sdl2_renderer_opengl3.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

#include <cmath>
#include <cstdio>

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
    struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer,
    struct GGame* game)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 170), ImGuiCond_FirstUseEver);
    ImGui::Begin("OpenGL3 Debug");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    ImGui::Text(
        "Camera: %d %d %d", game->camera_world_x, game->camera_world_y, game->camera_world_z);
    ImGui::Text("Mouse: %d %d", game->mouse_x, game->mouse_y);
    ImGui::Text("Loaded model keys: %zu", renderer->loaded_model_keys.size());
    ImGui::Text("Loaded scene keys: %zu", renderer->loaded_scene_element_keys.size());
    ImGui::Text("Loaded textures: %zu", renderer->loaded_texture_ids.size());
    ImGui::End();

    ImGui::Render();
    glDisable(GL_DEPTH_TEST);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static void
sync_drawable_size(struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer)
{
    if( !renderer || !renderer->platform || !renderer->platform->window )
        return;

    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(renderer->platform->window, &drawable_width, &drawable_height);
    if( drawable_width <= 0 || drawable_height <= 0 )
        return;

    renderer->width = drawable_width;
    renderer->height = drawable_height;
}

struct Platform2_OSX_SDL2_Renderer_OpenGL3*
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_New(
    int width,
    int height)
{
    auto* renderer = new Platform2_OSX_SDL2_Renderer_OpenGL3();
    renderer->gl_context = NULL;
    renderer->platform = NULL;
    renderer->width = width;
    renderer->height = height;
    renderer->gl_context_ready = false;
    renderer->pix3dgl = NULL;
    renderer->next_model_index = 1;
    return renderer;
}

void
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Free(struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer)
{
    if( !renderer )
        return;

    if( renderer->pix3dgl )
    {
        pix3dgl_cleanup(renderer->pix3dgl);
        renderer->pix3dgl = NULL;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if( renderer->gl_context )
    {
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = NULL;
    }
    delete renderer;
}

bool
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Init(
    struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer,
    struct Platform2_OSX_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;

    renderer->platform = platform;
    renderer->gl_context = SDL_GL_CreateContext(platform->window);
    if( !renderer->gl_context )
    {
        printf("OpenGL3 init failed: could not create context: %s\n", SDL_GetError());
        return false;
    }
    if( SDL_GL_MakeCurrent(platform->window, renderer->gl_context) != 0 )
    {
        printf("OpenGL3 init failed: could not make context current: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetSwapInterval(0);
    renderer->gl_context_ready = true;
    sync_drawable_size(renderer);
    renderer->pix3dgl = pix3dgl_new(false, true);
    if( !renderer->pix3dgl )
    {
        printf("OpenGL3 init failed: Pix3DGL setup failed\n");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if( !ImGui_ImplSDL2_InitForOpenGL(platform->window, renderer->gl_context) )
    {
        printf("ImGui SDL2 init failed for OpenGL3\n");
        return false;
    }
#if defined(__APPLE__)
    if( !ImGui_ImplOpenGL3_Init("#version 150") )
#else
    if( !ImGui_ImplOpenGL3_Init("#version 130") )
#endif
    {
        printf("ImGui OpenGL3 init failed for OpenGL3 renderer\n");
        return false;
    }

    return true;
}

void
PlatformImpl2_OSX_SDL2_Renderer_OpenGL3_Render(
    struct Platform2_OSX_SDL2_Renderer_OpenGL3* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    static uint64_t s_frame = 0;
    if( !renderer || !renderer->gl_context_ready || !renderer->pix3dgl || !game ||
        !render_command_buffer || !renderer->platform || !renderer->platform->window )
    {
        return;
    }

    SDL_GL_MakeCurrent(renderer->platform->window, renderer->gl_context);
    sync_drawable_size(renderer);

    glViewport(0, 0, renderer->width, renderer->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int window_width = renderer->platform->game_screen_width;
    int window_height = renderer->platform->game_screen_height;
    if( window_width <= 0 || window_height <= 0 )
        SDL_GetWindowSize(renderer->platform->window, &window_width, &window_height);
    const LogicalViewportRect logical_viewport =
        compute_logical_viewport_rect(window_width, window_height, game);
    const GLViewportRect world_viewport = compute_world_viewport_rect(
        renderer->width, renderer->height, window_width, window_height, logical_viewport);
    glViewport(world_viewport.x, world_viewport.y, world_viewport.width, world_viewport.height);

    const float projection_width = (float)logical_viewport.width;
    const float projection_height = (float)logical_viewport.height;

    pix3dgl_set_animation_clock(renderer->pix3dgl, (float)(SDL_GetTicks64() / 1000.0));
    pix3dgl_begin_frame(
        renderer->pix3dgl,
        (float)0,
        (float)0,
        (float)0,
        (float)game->camera_pitch,
        (float)game->camera_yaw,
        projection_width,
        projection_height);

    LibToriRS_FrameBegin(game, render_command_buffer);

    static std::vector<ToriRSRenderCommand> commands;
    commands.clear();
    {
        struct ToriRSRenderCommand cmd = { 0 };
        while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, false) )
        {
            commands.push_back(cmd);
        }
    }

    int total_commands = (int)commands.size();

    for( int i = 0; i < total_commands; i++ )
    {
        const ToriRSRenderCommand* cmd = &commands[i];
        if( cmd->kind != TORIRS_GFX_TEXTURE_LOAD )
            continue;

        renderer->loaded_texture_ids.insert(cmd->_texture_load.texture_id);
        struct DashTexture* texture = cmd->_texture_load.texture_nullable;
        if( texture && texture->texels )
        {
            pix3dgl_load_texture(
                renderer->pix3dgl,
                cmd->_texture_load.texture_id,
                texture->texels,
                texture->width,
                texture->height,
                texture->animation_direction,
                texture->animation_speed,
                texture->opaque);
        }
    }

    for( int i = 0; i < total_commands; i++ )
    {
        const ToriRSRenderCommand* cmd = &commands[i];
        if( cmd->kind == TORIRS_GFX_MODEL_LOAD )
        {
            struct DashModel* model = cmd->_model_load.model;
            if( !model || !model->lighting || !model->vertices_x || !model->vertices_y ||
                !model->vertices_z || !model->face_indices_a || !model->face_indices_b ||
                !model->face_indices_c || model->face_count <= 0 )
            {
                continue;
            }

            uintptr_t model_key = model_gpu_cache_key(model);
            renderer->loaded_model_keys.insert(model_key);
            if( renderer->model_index_by_key.find(model_key) == renderer->model_index_by_key.end() )
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
        }
    }

    int model_draw_count = 0;
    for( int i = 0; i < total_commands; i++ )
    {
        const ToriRSRenderCommand* cmd = &commands[i];
        if( cmd->kind != TORIRS_GFX_MODEL_DRAW )
            continue;

        model_draw_count++;
        struct DashModel* model = cmd->_model_draw.model;
        if( !model || !model->lighting || !model->vertices_x || !model->vertices_y ||
            !model->vertices_z || !model->face_indices_a || !model->face_indices_b ||
            !model->face_indices_c || model->face_count <= 0 )
        {
            continue;
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

        struct DashPosition draw_position = cmd->_model_draw.position;
        const int cull = dash3d_project_model(
            game->sys_dash, model, &draw_position, game->view_port, game->camera);
        if( cull != DASHCULL_VISIBLE )
            continue;

        int face_order_count = dash3d_prepare_projected_face_order(
            game->sys_dash, model, &draw_position, game->view_port, game->camera);

        const int* face_order = dash3d_projected_face_order(game->sys_dash, &face_order_count);
        pix3dgl_model_draw_ordered(
            renderer->pix3dgl,
            it->second,
            (float)draw_position.x,
            (float)draw_position.y,
            (float)draw_position.z,
            yaw_to_radians(draw_position.yaw),
            face_order,
            face_order_count);
    }

    // Re-assert world viewport right before draw submission in case any state changed.
    glViewport(world_viewport.x, world_viewport.y, world_viewport.width, world_viewport.height);
    LibToriRS_FrameEnd(game);
    pix3dgl_end_frame(renderer->pix3dgl);
    glViewport(0, 0, renderer->width, renderer->height);
    render_imgui_overlay(renderer, game);
    SDL_GL_SwapWindow(renderer->platform->window);

    s_frame++;
}
