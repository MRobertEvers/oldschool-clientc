#include "platform_impl2_osx_sdl2_renderer_opengl3.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

#include <cmath>
#include <cstdio>

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

    if( drawable_width != renderer->width || drawable_height != renderer->height )
    {
        renderer->width = drawable_width;
        renderer->height = drawable_height;
        glViewport(0, 0, renderer->width, renderer->height);
    }
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
    renderer->pix3dgl = pix3dgl_new();
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

    float projection_width = (float)renderer->width;
    float projection_height = (float)renderer->height;
    if( game->view_port && game->view_port->width > 0 && game->view_port->height > 0 )
    {
        projection_width = (float)game->view_port->width;
        projection_height = (float)game->view_port->height;
    }

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
    glViewport(0, 0, renderer->width, renderer->height);

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
        else if( cmd->kind == TORIRS_GFX_SCENE_ELEMENT_LOAD )
        {
            uintptr_t scene_key = cmd->_scene_element_load.scene_element_key;
            renderer->loaded_scene_element_keys.insert(scene_key);

            struct DashModel* model = cmd->_scene_element_load.model;
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

        pix3dgl_model_draw(
            renderer->pix3dgl,
            it->second,
            (float)cmd->_model_draw.position.x,
            (float)cmd->_model_draw.position.y,
            (float)cmd->_model_draw.position.z,
            yaw_to_radians(cmd->_model_draw.position.yaw));
    }

    LibToriRS_FrameEnd(game);
    pix3dgl_end_frame(renderer->pix3dgl);
    render_imgui_overlay(renderer, game);
    SDL_GL_SwapWindow(renderer->platform->window);

    s_frame++;
}
