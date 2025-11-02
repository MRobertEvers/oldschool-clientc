#include "platform_impl_osx_sdl2_renderer_soft3d.h"

extern "C" {
#include "graphics/render.h"
#include "libgame.u.h"
}
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <SDL.h>
#include <stdio.h>

static int g_screen_vertices_x[20];
static int g_screen_vertices_y[20];
static int g_screen_vertices_z[20];
static int g_ortho_vertices_x[20];
static int g_ortho_vertices_y[20];
static int g_ortho_vertices_z[20];

static void
render_imgui(struct Renderer* renderer, struct Game* game)
{
    ImGui_ImplSDLRenderer2_NewFrame();
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
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer->renderer);
}

static void
render_scene(struct Renderer* renderer, struct Game* game)
{
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
        if( iter_render_scene_ops.value.tile_nullable_ )
        {
            render_scene_tile(
                renderer->pixel_buffer,
                g_screen_vertices_x,
                g_screen_vertices_y,
                g_screen_vertices_z,
                g_ortho_vertices_x,
                g_ortho_vertices_y,
                g_ortho_vertices_z,
                renderer->width,
                renderer->height,
                // Had to use 100 here because of the scale, near plane z was resulting in triangles
                // extremely close to the camera.
                50,
                game->camera_world_x,
                game->camera_world_y,
                game->camera_world_z,
                game->camera_pitch,
                game->camera_yaw,
                game->camera_roll,
                game->camera_fov,
                iter_render_scene_ops.value.tile_nullable_,
                game->textures_cache,
                NULL);
        }

        if( iter_render_scene_ops.value.model_nullable_ )
        {
            scene_model = iter_render_scene_ops.value.model_nullable_;
            if( !scene_model->model )
                continue;

            int yaw_adjust = 0;
            iter_render_model_init(
                &iter_render_model,
                scene_model,
                // TODO: For wall decor, this should probably be set on the model->yaw rather than
                // on the op.
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
            int model_intersected = 0;
            while( iter_render_model_next(&iter_render_model) )
            {
                int face = iter_render_model.value_face;

                // bool is_in_bb = false;
                // if( game->mouse_x > 0 && game->mouse_x >= iter_model.aabb_min_screen_x &&
                //     game->mouse_x <= iter_model.aabb_max_screen_x &&
                //     game->mouse_y >= iter_model.aabb_min_screen_y &&
                //     game->mouse_y <= iter_model.aabb_max_screen_y )
                // {
                //     is_in_bb = true;
                // }

                // if( !model_intersected && is_in_bb )
                // {
                //     // Get face vertex indices
                //     int face_a = iter.value.model_nullable_->model->face_indices_a[face];
                //     int face_b = iter.value.model_nullable_->model->face_indices_b[face];
                //     int face_c = iter.value.model_nullable_->model->face_indices_c[face];

                //     // Get screen coordinates of the triangle vertices
                //     int x1 = iter_model.screen_vertices_x[face_a] + SCREEN_WIDTH / 2;
                //     int y1 = iter_model.screen_vertices_y[face_a] + SCREEN_HEIGHT / 2;
                //     int x2 = iter_model.screen_vertices_x[face_b] + SCREEN_WIDTH / 2;
                //     int y2 = iter_model.screen_vertices_y[face_b] + SCREEN_HEIGHT / 2;
                //     int x3 = iter_model.screen_vertices_x[face_c] + SCREEN_WIDTH / 2;
                //     int y3 = iter_model.screen_vertices_y[face_c] + SCREEN_HEIGHT / 2;

                //     // Check if mouse is inside the triangle using barycentric coordinates
                //     bool mouse_in_triangle = false;
                //     if( x1 != -5000 && x2 != -5000 && x3 != -5000 )
                //     { // Skip clipped triangles
                //         int denominator = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
                //         if( denominator != 0 )
                //         {
                //             float a =
                //                 ((y2 - y3) * (game->mouse_x - x3) + (x3 - x2) * (game->mouse_y -
                //                 y3)) / (float)denominator;
                //             float b =
                //                 ((y3 - y1) * (game->mouse_x - x3) + (x1 - x3) * (game->mouse_y -
                //                 y3)) / (float)denominator;
                //             float c = 1 - a - b;
                //             mouse_in_triangle = (a >= 0 && b >= 0 && c >= 0);
                //         }
                //     }

                //     if( mouse_in_triangle )
                //     {
                //         game->hover_model = iter_model.model->model_id;
                //         game->hover_loc_x = iter_model.model->_chunk_pos_x;
                //         game->hover_loc_y = iter_model.model->_chunk_pos_y;
                //         game->hover_loc_level = iter_model.model->_chunk_pos_level;
                //         game->hover_loc_yaw = iter_model.model->yaw;

                //         last_model_hit_model = iter.value.model_nullable_;
                //         last_model_hit_yaw = iter.value.model_nullable_->yaw + iter.value.yaw;

                //         model_intersected = true;
                //     }a
                // }
                // Only draw the face if mouse is inside the triangle
                model_draw_face(
                    renderer->pixel_buffer,
                    face,
                    scene_model->model->face_infos,
                    scene_model->model->face_indices_a,
                    scene_model->model->face_indices_b,
                    scene_model->model->face_indices_c,
                    scene_model->model->face_count,
                    iter_render_model.screen_vertices_x,
                    iter_render_model.screen_vertices_y,
                    iter_render_model.screen_vertices_z,
                    iter_render_model.ortho_vertices_x,
                    iter_render_model.ortho_vertices_y,
                    iter_render_model.ortho_vertices_z,
                    scene_model->model->vertex_count,
                    scene_model->model->face_textures,
                    scene_model->model->face_texture_coords,
                    scene_model->model->textured_face_count,
                    scene_model->model->textured_p_coordinate,
                    scene_model->model->textured_m_coordinate,
                    scene_model->model->textured_n_coordinate,
                    scene_model->model->textured_face_count,
                    scene_model->lighting->face_colors_hsl_a,
                    scene_model->lighting->face_colors_hsl_b,
                    scene_model->lighting->face_colors_hsl_c,
                    scene_model->model->face_alphas,
                    renderer->width / 2,
                    renderer->height / 2,
                    50,
                    renderer->width,
                    renderer->height,
                    game->camera_fov,
                    game->textures_cache);
            }

            // render_scene_model(
            //     pixel_buffer,
            //     SCREEN_WIDTH,
            //     SCREEN_HEIGHT,
            //     // Had to use 100 here because of the scale, near plane z was resulting in
            //     // extremely close to the camera.
            //     100,
            //     0,
            //     game->camera_x,
            //     game->camera_y,
            //     game->camera_z,
            //     game->camera_pitch,
            //     game->camera_yaw,
            //     game->camera_roll,
            //     game->camera_fov,
            //     iter.value.model_nullable_,
            //     game->textures_cache);
        }
    }

    // int scene_x = game->scene_model->region_x - game->camera_world_x;
    // int scene_y = game->scene_model->region_height - game->camera_world_y;
    // int scene_z = game->scene_model->region_z - game->camera_world_z;

    // struct AABB aabb;
    // render_model_frame(
    //     renderer->pixel_buffer,
    //     renderer->width,
    //     renderer->height,
    //     50,
    //     0,
    //     game->scene_model->yaw,
    //     0,
    //     scene_x,
    //     scene_y,
    //     scene_z,
    //     game->camera_pitch,
    //     game->camera_yaw,
    //     game->camera_roll,
    //     game->camera_fov,
    //     &aabb,
    //     game->scene_model->model,
    //     game->scene_model->lighting,
    //     game->scene_model->bounds_cylinder,
    //     NULL,
    //     NULL,
    //     NULL,
    //     game->textures_cache);
}

struct Renderer*
PlatformImpl_OSX_SDL2_Renderer_Soft3D_New(int width, int height, int max_width, int max_height)
{
    struct Renderer* renderer = (struct Renderer*)malloc(sizeof(struct Renderer));
    memset(renderer, 0, sizeof(struct Renderer));

    renderer->pixel_buffer = (int*)malloc(max_width * max_height * sizeof(int));
    if( !renderer->pixel_buffer )
    {
        printf("Failed to allocate pixel buffer\n");
        return NULL;
    }
    memset(renderer->pixel_buffer, 0, width * height * sizeof(int));

    renderer->width = width;
    renderer->height = height;
    renderer->max_width = max_width;
    renderer->max_height = max_height;

    renderer->op_capacity = 10900;
    renderer->ops = (struct SceneOp*)malloc(renderer->op_capacity * sizeof(struct SceneOp));
    memset(renderer->ops, 0, renderer->op_capacity * sizeof(struct SceneOp));

    renderer->op_count = 0;

    return renderer;
}

void
PlatformImpl_OSX_SDL2_Renderer_Soft3D_Free(struct Renderer* renderer)
{
    free(renderer);
}

bool
PlatformImpl_OSX_SDL2_Renderer_Soft3D_Init(struct Renderer* renderer, struct Platform* platform)
{
    renderer->platform = platform;

    renderer->renderer = SDL_CreateRenderer(platform->window, -1, SDL_RENDERER_ACCELERATED);
    if( !renderer->renderer )
    {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        return false;
    }

    renderer->texture = SDL_CreateTexture(
        renderer->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        renderer->width,
        renderer->height);
    if( !renderer->texture )
    {
        printf("Failed to create texture\n");
        return false;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if( !ImGui_ImplSDL2_InitForSDLRenderer(platform->window, renderer->renderer) )
    {
        printf("ImGui SDL2 init failed\n");
        return false;
    }
    if( !ImGui_ImplSDLRenderer2_Init(renderer->renderer) )
    {
        printf("ImGui Renderer init failed\n");
        return false;
    }

    return true;
}

void
PlatformImpl_OSX_SDL2_Renderer_Soft3D_Shutdown(struct Renderer* renderer)
{
    SDL_DestroyRenderer(renderer->renderer);
    renderer->renderer = NULL;
}

void
PlatformImpl_OSX_SDL2_Renderer_Soft3D_Render(
    struct Renderer* renderer, struct Game* game, struct GameGfxOpList* gfx_op_list)
{
    // Handle window resize: update renderer dimensions up to max size
    int window_width, window_height;
    SDL_GetWindowSize(renderer->platform->window, &window_width, &window_height);

    // Clamp to maximum renderer size (pixel buffer allocation limit)
    int new_width = window_width > renderer->max_width ? renderer->max_width : window_width;
    int new_height = window_height > renderer->max_height ? renderer->max_height : window_height;

    // Only update if size changed
    if( new_width != renderer->width || new_height != renderer->height )
    {
        renderer->width = new_width;
        renderer->height = new_height;

        // Recreate texture with new dimensions
        if( renderer->texture )
        {
            SDL_DestroyTexture(renderer->texture);
        }

        renderer->texture = SDL_CreateTexture(
            renderer->renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            renderer->width,
            renderer->height);

        if( !renderer->texture )
        {
            printf(
                "Failed to recreate texture for new size %dx%d\n",
                renderer->width,
                renderer->height);
        }
        else
        {
            printf(
                "Window resized to %dx%d (max: %dx%d)\n",
                renderer->width,
                renderer->height,
                renderer->max_width,
                renderer->max_height);
        }
    }

    memset(renderer->pixel_buffer, 0, renderer->width * renderer->height * sizeof(int));
    for( int y = 0; y < renderer->height; y++ )
        memset(&renderer->pixel_buffer[y * renderer->width], 0, renderer->width * sizeof(int));

    SDL_RenderClear(renderer->renderer);

    for( int i = 0; i < gfx_op_list->op_count; i++ )
    {
        struct GameGfxOp* gfx_op = &gfx_op_list->ops[i];
        switch( gfx_op->kind )
        {
        case GAME_GFX_OP_SCENE_DRAW:
            render_scene(renderer, game);
            break;
        case GAME_GFX_OP_SCENE_MODEL_LOAD:
            // Noop
            break;
        case GAME_GFX_OP_SCENE_MODEL_UNLOAD:
            // Noop
            break;
        case GAME_GFX_OP_SCENE_MODEL_DRAW:
            // TODO:
            break;
        default:
            break;
        }
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        renderer->pixel_buffer,
        renderer->width,
        renderer->height,
        32,
        renderer->width * sizeof(int),
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000);

    // Copy the pixels into the texture
    int* pix_write = NULL;
    int _pitch_unused = 0;
    if( SDL_LockTexture(renderer->texture, NULL, (void**)&pix_write, &_pitch_unused) < 0 )
        return;

    int row_size = renderer->width * sizeof(int);
    int* src_pixels = (int*)surface->pixels;
    for( int src_y = 0; src_y < (renderer->height); src_y++ )
    {
        // Calculate offset in texture to write a single row of pixels
        int* row = &pix_write[(src_y * renderer->width)];
        // Copy a single row of pixels
        memcpy(row, &src_pixels[(src_y - 0) * renderer->width], row_size);
    }

    // Unlock the texture so that it may be used elsewhere
    SDL_UnlockTexture(renderer->texture);

    // window_width and window_height already retrieved at the top of function
    // Calculate destination rectangle to scale the texture to fill the window
    SDL_Rect dst_rect;
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = window_width;
    dst_rect.h = window_height;

    // Use nearest neighbor (point) filtering when window is larger than rendered size
    // This maintains crisp pixels when scaling up beyond max renderer size
    if( window_width > renderer->width || window_height > renderer->height )
    {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Nearest neighbor (point sampling)
    }
    else
    {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); // Bilinear when scaling down
    }

    // Calculate aspect ratio preserving dimensions
    float src_aspect = (float)renderer->width / (float)renderer->height;
    float window_aspect = (float)window_width / (float)window_height;

    if( src_aspect > window_aspect )
    {
        // Renderer is wider - fit to window width
        dst_rect.w = window_width;
        dst_rect.h = (int)(window_width / src_aspect);
        dst_rect.x = 0;
        dst_rect.y = (window_height - dst_rect.h) / 2;
    }
    else
    {
        // Renderer is taller - fit to window height
        dst_rect.h = window_height;
        dst_rect.w = (int)(window_height * src_aspect);
        dst_rect.y = 0;
        dst_rect.x = (window_width - dst_rect.w) / 2;
    }

    SDL_RenderCopy(renderer->renderer, renderer->texture, NULL, &dst_rect);
    SDL_FreeSurface(surface);

    render_imgui(renderer, game);

    SDL_RenderPresent(renderer->renderer);
}