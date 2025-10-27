#include "platform_impl_osx_sdl2_renderer_opengl3.h"

extern "C" {
#include "graphics/render.h"
#include "libgame.u.h"
}
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

// Use regular OpenGL headers on macOS/iOS for development
#include <OpenGL/gl.h>
#include <OpenGL/gl3.h>

#include <SDL.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_screen_vertices_x[20];
static int g_screen_vertices_y[20];
static int g_screen_vertices_z[20];
static int g_ortho_vertices_x[20];
static int g_ortho_vertices_y[20];
static int g_ortho_vertices_z[20];

static void
render_imgui(struct Renderer* renderer, struct Game* game)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Info window
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);

    ImGui::Begin("Info");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    Uint64 frequency = SDL_GetPerformanceFrequency();
    // ImGui::Text(
    //     "Render Time: %.3f ms/frame",
    //     (double)(game->end_time - game->start_time) * 1000.0 / (double)frequency);
    // ImGui::Text(
    //     "Average Render Time: %.3f ms/frame, %.3f ms/frame, %.3f ms/frame",
    //     (double)(game->frame_time_sum / game->frame_count) * 1000.0 / (double)frequency,
    //     (double)(game->painters_time_sum / game->frame_count) * 1000.0 / (double)frequency,
    //     (double)(game->texture_upload_time_sum / game->frame_count) * 1000.0 /
    //     (double)frequency);
    // ImGui::Text("Mouse (x, y): %d, %d", game->mouse_x, game->mouse_y);

    // ImGui::Text("Hover model: %d, %d", game->hover_model, game->hover_loc_yaw);
    // ImGui::Text(
    //     "Hover loc: %d, %d, %d", game->hover_loc_x, game->hover_loc_y, game->hover_loc_level);

    // Camera position with copy button
    char camera_pos_text[256];
    snprintf(
        camera_pos_text,
        sizeof(camera_pos_text),
        "Camera (x, y, z): %d, %d, %d : %d, %d",
        game->camera_world_x,
        game->camera_world_y,
        game->camera_world_z,
        game->camera_world_x / 128,
        game->camera_world_y / 128);
    ImGui::Text("%s", camera_pos_text);
    ImGui::SameLine();
    if( ImGui::SmallButton("Copy##pos") )
    {
        ImGui::SetClipboardText(camera_pos_text);
    }

    // Camera rotation with copy button
    char camera_rot_text[256];
    snprintf(
        camera_rot_text,
        sizeof(camera_rot_text),
        "Camera (pitch, yaw, roll): %d, %d, %d",

        game->camera_pitch,
        game->camera_yaw,
        game->camera_roll);
    ImGui::Text("%s", camera_rot_text);
    ImGui::SameLine();
    if( ImGui::SmallButton("Copy##rot") )
    {
        ImGui::SetClipboardText(camera_rot_text);
    }

    // Add camera speed slider
    ImGui::Separator();
    ImGui::Text("Camera Controls");
    ImGui::SliderInt("FOV", &game->camera_fov, 64, 768, "%d");
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static void
load_static_scene(struct Renderer* renderer, struct Game* game)
{
    // This function builds a static scene buffer containing all scene geometry
    // It's called once when the scene is loaded

    printf("Building static scene buffer...\n");

    // Start building the static scene
    pix3dgl_scene_static_begin(renderer->pix3dgl);

    // Load all scene tiles first
    for( int i = 0; i < game->scene->scene_tiles_length; i++ )
    {
        struct SceneTile* scene_tile = &game->scene->scene_tiles[i];

        // Load tile textures
        if( scene_tile->face_texture_ids != NULL && scene_tile->face_count > 0 )
        {
            for( int face = 0; face < scene_tile->face_count; face++ )
            {
                if( scene_tile->face_texture_ids[face] != -1 )
                {
                    pix3dgl_load_texture(
                        renderer->pix3dgl,
                        scene_tile->face_texture_ids[face],
                        game->textures_cache,
                        game->cache);
                }
            }
        }

        // Add tile geometry directly to static scene buffer
        pix3dgl_scene_static_add_tile(
            renderer->pix3dgl,
            scene_tile->vertex_x,
            scene_tile->vertex_y,
            scene_tile->vertex_z,
            scene_tile->vertex_count,
            scene_tile->faces_a,
            scene_tile->faces_b,
            scene_tile->faces_c,
            scene_tile->face_count,
            scene_tile->face_texture_ids,
            scene_tile->face_color_hsl_a,
            scene_tile->face_color_hsl_b,
            scene_tile->face_color_hsl_c);
    }

    // Add all scene models directly to static scene buffer
    for( int i = 0; i < game->scene->models_length; i++ )
    {
        struct SceneModel* scene_model = &game->scene->models[i];

        if( !scene_model->model )
            continue;

        // Store the scene model index for later use
        scene_model->scene_model_idx = i;

        // Load model textures (still needed for rendering)
        if( scene_model->model->face_textures )
        {
            for( int face = 0; face < scene_model->model->face_count; face++ )
            {
                if( scene_model->model->face_textures[face] != -1 )
                {
                    pix3dgl_load_texture(
                        renderer->pix3dgl,
                        scene_model->model->face_textures[face],
                        game->textures_cache,
                        game->cache);
                }
            }
        }

        // Calculate world position
        float position_x = scene_model->region_x + scene_model->offset_x;
        float position_y = scene_model->region_height + scene_model->offset_height;
        float position_z = scene_model->region_z + scene_model->offset_z;
        float yaw_radians = (scene_model->yaw * 2.0f * M_PI) / 2048.0f;

        // Add model geometry directly to static scene buffer (no individual model loading)
        pix3dgl_scene_static_add_model_raw(
            renderer->pix3dgl,
            scene_model->model->vertices_x,
            scene_model->model->vertices_y,
            scene_model->model->vertices_z,
            scene_model->model->face_indices_a,
            scene_model->model->face_indices_b,
            scene_model->model->face_indices_c,
            scene_model->model->face_count,
            scene_model->model->face_textures,
            scene_model->model->face_texture_coords,
            scene_model->model->textured_p_coordinate,
            scene_model->model->textured_m_coordinate,
            scene_model->model->textured_n_coordinate,
            scene_model->lighting->face_colors_hsl_a,
            scene_model->lighting->face_colors_hsl_b,
            scene_model->lighting->face_colors_hsl_c,
            scene_model->model->face_infos,
            position_x,
            position_y,
            position_z,
            yaw_radians);
    }

    // Finalize the static scene - uploads to GPU
    pix3dgl_scene_static_end(renderer->pix3dgl);

    printf("Static scene buffer built successfully\n");
}

static void
render_scene(struct Renderer* renderer, struct Game* game)
{
    // Begin frame with camera setup
    pix3dgl_begin_frame(
        renderer->pix3dgl,
        game->camera_world_x,
        game->camera_world_y,
        game->camera_world_z,
        game->camera_pitch,
        game->camera_yaw,
        renderer->width,
        renderer->height);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Draw the static scene buffer (contains all scene geometry)
    pix3dgl_scene_static_draw(renderer->pix3dgl);

    pix3dgl_end_frame(renderer->pix3dgl);
}

struct Renderer*
PlatformImpl_OSX_SDL2_Renderer_OpenGL3_New(int width, int height)
{
    struct Renderer* renderer = (struct Renderer*)malloc(sizeof(struct Renderer));
    memset(renderer, 0, sizeof(struct Renderer));

    renderer->pixel_buffer = (int*)malloc(width * height * sizeof(int));
    if( !renderer->pixel_buffer )
    {
        printf("Failed to allocate pixel buffer\n");
        return NULL;
    }
    memset(renderer->pixel_buffer, 0, width * height * sizeof(int));

    renderer->width = width;
    renderer->height = height;

    renderer->op_capacity = 10900;
    renderer->ops = (struct SceneOp*)malloc(renderer->op_capacity * sizeof(struct SceneOp));
    memset(renderer->ops, 0, renderer->op_capacity * sizeof(struct SceneOp));

    renderer->op_count = 0;

    return renderer;
}

void
PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Free(struct Renderer* renderer)
{
    free(renderer);
}

bool
PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Init(struct Renderer* renderer, struct Platform* platform)
{
    renderer->platform = platform;
    // Create OpenGL context
    renderer->gl_context = SDL_GL_CreateContext(platform->window);
    if( !renderer->gl_context )
    {
        printf("OpenGL context creation failed: %s\n", SDL_GetError());
        return false;
    }

    // Enable vsync
    SDL_GL_SetSwapInterval(1);
    printf("OpenGL context created successfully\n");

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if( !ImGui_ImplSDL2_InitForOpenGL(platform->window, renderer->gl_context) )
    {
        printf("ImGui SDL2 init failed\n");
        return false;
    }
    ImGui_ImplOpenGL3_Init("#version 150"); // OpenGL 3.2 Core for native builds

    renderer->pix3dgl = pix3dgl_new();

    return true;
}

void
PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Shutdown(struct Renderer* renderer)
{
    SDL_GL_DeleteContext(renderer->gl_context);
}

void
PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Render(
    struct Renderer* renderer, struct Game* game, struct GameGfxOpList* gfx_op_list)
{
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);

    glEnable(GL_CULL_FACE);
    // glDisable(GL_DEPTH_TEST);

    // glDisable(GL_CULL_FACE);

    for( int i = 0; i < gfx_op_list->op_count; i++ )
    {
        struct GameGfxOp* gfx_op = &gfx_op_list->ops[i];
        switch( gfx_op->kind )
        {
        case GAME_GFX_OP_SCENE_DRAW:
        {
            render_scene(renderer, game);
        }
        break;
        case GAME_GFX_OP_SCENE_STATIC_LOAD:
        {
            load_static_scene(renderer, game);
        }
        break;
        case GAME_GFX_OP_SCENE_TILE_LOAD:
        {
            struct SceneTile* scene_tile =
                &game->scene->scene_tiles[gfx_op->_scene_tile_load.scene_tile_idx];

            if( scene_tile->face_texture_ids != NULL && scene_tile->face_count > 0 )
            {
                // Load all unique textures used by this model
                for( int face = 0; face < scene_tile->face_count; face++ )
                {
                    if( scene_tile->face_texture_ids[face] != -1 )
                    {
                        int texture_id = scene_tile->face_texture_ids[face];
                        pix3dgl_load_texture(
                            renderer->pix3dgl,
                            scene_tile->face_texture_ids[face],
                            game->textures_cache,
                            game->cache);
                    }
                }
            }

            pix3dgl_tile_load(
                renderer->pix3dgl,
                gfx_op->_scene_tile_load.scene_tile_idx,
                scene_tile->vertex_x,
                scene_tile->vertex_y,
                scene_tile->vertex_z,
                scene_tile->vertex_count,
                scene_tile->faces_a,
                scene_tile->faces_b,
                scene_tile->faces_c,
                scene_tile->face_count,
                scene_tile->valid_faces,
                scene_tile->face_texture_ids,
                scene_tile->face_color_hsl_a,
                scene_tile->face_color_hsl_b,
                scene_tile->face_color_hsl_c);
        }
        break;
        case GAME_GFX_OP_SCENE_TILE_UNLOAD:
        {
        }
        break;
        case GAME_GFX_OP_SCENE_MODEL_LOAD:
        {
            struct SceneModel* scene_model =
                &game->scene->models[gfx_op->_scene_model_load.scene_model_idx];

            // Check if the model has textures
            if( scene_model->model->face_textures != NULL &&
                scene_model->model->textured_face_count > 0 )
            {
                // Load all unique textures used by this model
                for( int face = 0; face < scene_model->model->face_count; face++ )
                {
                    if( scene_model->model->face_textures[face] != -1 )
                    {
                        int texture_id = scene_model->model->face_textures[face];
                        pix3dgl_load_texture(
                            renderer->pix3dgl, texture_id, game->textures_cache, game->cache);
                    }
                }

                // Load as textured model
                pix3dgl_model_load_textured_pnm(
                    renderer->pix3dgl,
                    gfx_op->_scene_model_load.scene_model_idx,
                    scene_model->model->vertices_x,
                    scene_model->model->vertices_y,
                    scene_model->model->vertices_z,
                    scene_model->model->face_indices_a,
                    scene_model->model->face_indices_b,
                    scene_model->model->face_indices_c,
                    scene_model->model->face_count,
                    scene_model->model->face_infos,
                    scene_model->model->face_alphas,
                    scene_model->model->face_textures,
                    scene_model->model->face_texture_coords,
                    scene_model->model->textured_p_coordinate,
                    scene_model->model->textured_m_coordinate,
                    scene_model->model->textured_n_coordinate,
                    scene_model->lighting->face_colors_hsl_a,
                    scene_model->lighting->face_colors_hsl_b,
                    scene_model->lighting->face_colors_hsl_c);
            }
            else
            {
                // Load as regular model without textures
                pix3dgl_model_load(
                    renderer->pix3dgl,
                    gfx_op->_scene_model_load.scene_model_idx,
                    scene_model->model->vertices_x,
                    scene_model->model->vertices_y,
                    scene_model->model->vertices_z,
                    scene_model->model->face_indices_a,
                    scene_model->model->face_indices_b,
                    scene_model->model->face_indices_c,
                    scene_model->model->face_count,
                    scene_model->model->face_alphas,
                    scene_model->lighting->face_colors_hsl_a,
                    scene_model->lighting->face_colors_hsl_b,
                    scene_model->lighting->face_colors_hsl_c);
            }
        }
        break;
        case GAME_GFX_OP_SCENE_MODEL_UNLOAD:
            // Noop
            break;
        case GAME_GFX_OP_SCENE_MODEL_DRAW:
        {
            struct SceneModel* scene_model =
                &game->scene->models[gfx_op->_scene_model_draw.scene_model_idx];
            if( !scene_model->model )
                break;

            // Calculate world position from region coordinates
            float position_x = scene_model->region_x + scene_model->offset_x;
            float position_y = scene_model->region_height + scene_model->offset_height;
            float position_z = scene_model->region_z + scene_model->offset_z;

            // Convert yaw from game units to radians
            float yaw_radians = (scene_model->yaw * 2.0f * M_PI) / 2048.0f;

            // Draw the model
            pix3dgl_model_draw(
                renderer->pix3dgl,
                gfx_op->_scene_model_draw.scene_model_idx,
                position_x,
                position_y,
                position_z,
                yaw_radians);
        }
        break;
        default:
            break;
        }
    }

    render_imgui(renderer, game);

    if( renderer->platform->window )
    {
        SDL_GL_SwapWindow(renderer->platform->window);
    }

    game_gfx_op_list_reset(gfx_op_list);
}