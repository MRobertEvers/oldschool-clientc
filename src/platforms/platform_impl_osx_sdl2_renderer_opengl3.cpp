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

    glDisable(GL_DEPTH_TEST);

    struct IterRenderSceneOps iter_render_scene_ops;
    struct IterRenderModel iter_render_model;
    struct SceneModel* scene_model = NULL;

    renderer->op_count = render_scene_compute_ops(
        renderer->ops,
        renderer->op_capacity,
        game->camera_world_x,
        game->camera_world_y,
        game->camera_world_z,
        game->scene,
        NULL);

    iter_render_scene_ops_init(
        &iter_render_scene_ops,
        game->frustrum_cullmap,
        game->scene,
        renderer->ops,
        renderer->op_count,
        renderer->op_count,
        game->camera_pitch,
        game->camera_yaw,
        game->camera_world_x / 128,
        game->camera_world_z / 128);

    while( iter_render_scene_ops_next(&iter_render_scene_ops) )
    {
        // if( iter_render_scene_ops.value.tile_nullable_ )
        // {
        //     render_scene_tile(
        //         renderer->pixel_buffer,
        //         g_screen_vertices_x,
        //         g_screen_vertices_y,
        //         g_screen_vertices_z,
        //         g_ortho_vertices_x,
        //         g_ortho_vertices_y,
        //         g_ortho_vertices_z,
        //         renderer->width,
        //         renderer->height,
        //         // Had to use 100 here because of the scale, near plane z was resulting in
        //         triangles
        //         // extremely close to the camera.
        //         50,
        //         game->camera_world_x,
        //         game->camera_world_y,
        //         game->camera_world_z,
        //         game->camera_pitch,
        //         game->camera_yaw,
        //         game->camera_roll,
        //         game->camera_fov,
        //         iter_render_scene_ops.value.tile_nullable_,
        //         game->textures_cache,
        //         NULL);
        // }

        if( iter_render_scene_ops.value.model_nullable_ )
        {
            scene_model = iter_render_scene_ops.value.model_nullable_;
            if( !scene_model->model )
                continue;

            // Calculate world position from region coordinates
            float position_x = scene_model->region_x + scene_model->offset_x;
            float position_y = scene_model->region_height + scene_model->offset_height;
            float position_z = scene_model->region_z + scene_model->offset_z;

            // Convert yaw from game units to radians
            // Game uses 2048 units per full rotation (2π radians)
            float yaw_radians = (scene_model->yaw * 2.0f * M_PI) / 2048.0f;

            // Initialize the face iterator for painter's algorithm sorting
            int yaw_adjust = 0;
            iter_render_model_init(
                &iter_render_model,
                scene_model,
                yaw_adjust,
                game->camera_world_x,
                game->camera_world_y,
                game->camera_world_z,
                game->camera_pitch,
                game->camera_yaw,
                game->camera_roll,
                game->camera_fov,
                renderer->width,
                renderer->height,
                50);

            // Begin drawing - setup the model once (uniforms, VAO/VBOs)
            pix3dgl_model_begin_draw(
                renderer->pix3dgl,
                scene_model->scene_model_idx,
                position_x,
                position_y,
                position_z,
                yaw_radians);

            // Draw each face in painter's algorithm order with minimal overhead
            while( iter_render_model_next(&iter_render_model) )
            {
                int face = iter_render_model.value_face;
                pix3dgl_model_draw_face_fast(renderer->pix3dgl, face);
            }

            // End drawing - cleanup
            pix3dgl_model_end_draw(renderer->pix3dgl);
        }
    }

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
            render_scene(renderer, game);
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