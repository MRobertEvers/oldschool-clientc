#include "android_platform.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <sys/stat.h>

#include <errno.h>
#include <fstream>
#include <jni.h>
#include <sstream>
#include <unistd.h>

// External C includes from scene_tile_test.cpp
extern "C" {
#include "bmp.h"
#include "graphics/render.h"
#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/frustrum_cullmap.h"
#include "osrs/scene.h"
#include "osrs/scene_tile.h"
#include "osrs/tables/config_floortype.h"
#include "osrs/tables/config_idk.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/config_object.h"
#include "osrs/tables/config_sequence.h"
#include "osrs/tables/configs.h"
#include "osrs/tables/sprites.h"
#include "osrs/tables/textures.h"
#include "osrs/world.h"
#include "osrs/xtea_config.h"
#include "screen.h"
#include "shared_tables.h"
}

#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ImGui headers
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#define LOG_TAG "AndroidPlatform"
#define LOGI(...)                                                                                  \
    printf("[INFO] " __VA_ARGS__);                                                                 \
    printf("\n")
#define LOGE(...)                                                                                  \
    printf("[ERROR] " __VA_ARGS__);                                                                \
    printf("\n")

// Constants from scene_tile_test.cpp
#define GUI_WINDOW_WIDTH 400
#define GUI_WINDOW_HEIGHT 600

// External variables from scene_tile_test.cpp
extern "C" int g_sin_table[2048];
extern "C" int g_cos_table[2048];
extern "C" int g_tan_table[2048];
extern "C" int g_hsl16_to_rgb_table[65536];

// Global variables from scene_tile_test.cpp
int g_blit_buffer[SCREEN_WIDTH * SCREEN_HEIGHT] = { 0 };

// Static screen vertices arrays
static int g_screen_vertices_x[20];
static int g_screen_vertices_y[20];
static int g_screen_vertices_z[20];
static int g_ortho_vertices_x[20];
static int g_ortho_vertices_y[20];
static int g_ortho_vertices_z[20];

// Utility functions from scene_tile_test.cpp
static inline int
min(int a, int b)
{
    return a < b ? a : b;
}
static inline int
max(int a, int b)
{
    return a > b ? a : b;
}

// Structs are now defined in android_platform.h


// Game free function from scene_tile_test.cpp
void
game_free(struct Game* game)
{
    if( game->ops )
        free(game->ops);
    if( game->textures_cache )
        textures_cache_free(game->textures_cache);
    if( game->scene )
        scene_free(game->scene);
    if( game->scene_locs )
        free(game->scene_locs);
    if( game->scene_textures )
        scene_textures_free(game->scene_textures);
    if( game->map_locs )
        free(game->map_locs);
    if( game->sprite_packs )
        sprite_pack_free(game->sprite_packs);
    if( game->textures )
        free(game->textures);
    if( game->tiles )
        free(game->tiles);
}

// Platform initialization function from scene_tile_test.cpp
static bool
platform_sdl2_init(struct PlatformSDL2* platform)
{
    if( SDL_Init(SDL_INIT_VIDEO) < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    // Create window at the target resolution
    platform->window = SDL_CreateWindow(
        "Scene Tile Test",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_RESIZABLE);

    if( !platform->window )
    {
        printf("Window creation failed: %s\n", SDL_GetError());
        return false;
    }

    // Create renderer
    platform->renderer = SDL_CreateRenderer(platform->window, -1, SDL_RENDERER_ACCELERATED);

    if( !platform->renderer )
    {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        return false;
    }

    // Create texture for game rendering
    platform->texture = SDL_CreateTexture(
        platform->renderer,
        SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT);

    if( !platform->texture )
    {
        printf("Texture creation failed: %s\n", SDL_GetError());
        return false;
    }

    // Allocate pixel buffer
    platform->pixel_buffer = (int*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
    if( !platform->pixel_buffer )
    {
        printf("Failed to allocate pixel buffer\n");
        return false;
    }
    memset(platform->pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

    // Get initial window size
    SDL_GetWindowSize(platform->window, &platform->window_width, &platform->window_height);
    SDL_GetRendererOutputSize(
        platform->renderer, &platform->drawable_width, &platform->drawable_height);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if( !ImGui_ImplSDL2_InitForSDLRenderer(platform->window, platform->renderer) )
    {
        printf("ImGui SDL2 init failed\n");
        return false;
    }
    if( !ImGui_ImplSDLRenderer2_Init(platform->renderer) )
    {
        printf("ImGui Renderer init failed\n");
        return false;
    }

    return true;
}

// Mouse coordinate transformation function from scene_tile_test.cpp
static void
transform_mouse_coordinates(
    int window_mouse_x,
    int window_mouse_y,
    int* game_mouse_x,
    int* game_mouse_y,
    struct PlatformSDL2* platform)
{
    // Calculate the same scaling transformation as the rendering
    float src_aspect = (float)SCREEN_WIDTH / SCREEN_HEIGHT;
    float dst_aspect = (float)platform->drawable_width / platform->drawable_height;

    int dst_x, dst_y, dst_w, dst_h;

    if( src_aspect > dst_aspect )
    {
        // Source is wider - fit to width
        dst_w = platform->drawable_width;
        dst_h = (int)(platform->drawable_width / src_aspect);
        dst_x = 0;
        dst_y = (platform->drawable_height - dst_h) / 2;
    }
    else
    {
        // Source is taller - fit to height
        dst_h = platform->drawable_height;
        dst_w = (int)(platform->drawable_height * src_aspect);
        dst_y = 0;
        dst_x = (platform->drawable_width - dst_w) / 2;
    }

    // Transform window coordinates to game coordinates
    // Account for the offset and scaling of the game rendering area
    if( window_mouse_x < dst_x || window_mouse_x >= dst_x + dst_w || window_mouse_y < dst_y ||
        window_mouse_y >= dst_y + dst_h )
    {
        // Mouse is outside the game rendering area
        *game_mouse_x = -1;
        *game_mouse_y = -1;
    }
    else
    {
        // Transform from window coordinates to game coordinates
        float relative_x = (float)(window_mouse_x - dst_x) / dst_w;
        float relative_y = (float)(window_mouse_y - dst_y) / dst_h;

        *game_mouse_x = (int)(relative_x * SCREEN_WIDTH);
        *game_mouse_y = (int)(relative_y * SCREEN_HEIGHT);
    }
}

// Outline effect function from scene_tile_test.cpp
static void
apply_outline_effect(
    int* pixel_buffer,
    int* source_buffer,
    int width,
    int height,
    int bound_min_x,
    int bound_max_x,
    int bound_min_y,
    int bound_max_y)
{
    for( int y = bound_min_y; y < bound_max_y; y++ )
    {
        if( y < 1 || y >= height - 1 )
            continue;

        for( int x = bound_min_x; x < bound_max_x; x++ )
        {
            if( x < 1 || x >= width - 1 )
                continue;

            int pixel = source_buffer[y * width + x];
            if( pixel != 0 )
            {
                // Check if any neighboring pixel is empty (0)
                if( source_buffer[(y - 1) * width + x] == 0 ||
                    source_buffer[(y + 1) * width + x] == 0 ||
                    source_buffer[y * width + (x - 1)] == 0 ||
                    source_buffer[y * width + (x + 1)] == 0 )
                {
                    pixel_buffer[y * width + x] = 0xFFFFFF; // White outline
                }
            }
        }
    }
}

// Render scene model function from scene_tile_test.cpp
static void
render_scene_model(
    int* pixel_buffer,
    int width,
    int height,
    int near_plane_z,
    int yaw,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int camera_roll,
    int fov,
    struct AABB* aabb,
    struct SceneModel* model,
    struct TexturesCache* textures_cache)
{
    int scene_x = model->region_x - camera_x;
    int scene_y = model->region_height - camera_y;
    int scene_z = model->region_z - camera_z;

    if( !model->bounds_cylinder )
    {
        model->bounds_cylinder = (struct BoundsCylinder*)malloc(sizeof(struct BoundsCylinder));
        *model->bounds_cylinder = calculate_bounds_cylinder(
            model->model->vertex_count,
            model->model->vertices_x,
            model->model->vertices_y,
            model->model->vertices_z);
    }

    yaw %= 2048;

    scene_x += model->offset_x;
    scene_y += model->offset_height;
    scene_z += model->offset_z;

    if( model->model == NULL )
        return;

    render_model_frame(
        pixel_buffer,
        width,
        height,
        near_plane_z,
        0,
        yaw,
        0,
        scene_x,
        scene_y,
        scene_z,
        camera_pitch,
        camera_yaw,
        camera_roll,
        fov,
        aabb,
        model->model,
        model->lighting,
        model->bounds_cylinder,
        NULL,
        NULL,
        NULL,
        textures_cache);
}

// ImGui rendering function from scene_tile_test.cpp
static void
game_render_imgui(struct Game* game, struct PlatformSDL2* platform)
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
    ImGui::Text(
        "Render Time: %.3f ms/frame",
        (double)(game->end_time - game->start_time) * 1000.0 / (double)frequency);
    ImGui::Text(
        "Average Render Time: %.3f ms/frame",
        (double)(game->frame_time_sum / game->frame_count) * 1000.0 / (double)frequency);
    ImGui::Text("Mouse (x, y): %d, %d", game->mouse_x, game->mouse_y);

    ImGui::Text("Hover model: %d, %d", game->hover_model, game->hover_loc_yaw);
    ImGui::Text(
        "Hover loc: %d, %d, %d", game->hover_loc_x, game->hover_loc_y, game->hover_loc_level);

    // Camera position with copy button
    char camera_pos_text[256];
    snprintf(
        camera_pos_text,
        sizeof(camera_pos_text),
        "Camera (x, y, z): %d, %d, %d : %d, %d",
        game->camera_x,
        game->camera_y,
        game->camera_z,
        game->camera_x / 128,
        game->camera_y / 128);
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
        "Camera (pitch, yaw, roll, fake_pitch): %d, %d, %d, %d",
        game->camera_pitch,
        game->camera_yaw,
        game->camera_roll,
        game->fake_pitch);
    ImGui::Text("%s", camera_rot_text);
    ImGui::SameLine();
    if( ImGui::SmallButton("Copy##rot") )
    {
        ImGui::SetClipboardText(camera_rot_text);
    }

    // Add camera speed slider
    ImGui::Separator();
    ImGui::Text("Camera Controls");
    ImGui::SliderInt("Movement Speed", &game->camera_speed, 10, 200, "%d");
    if( ImGui::IsItemHovered() )
    {
        ImGui::SetTooltip("Adjusts how fast the camera moves when using WASD keys");
    }
    ImGui::SliderInt("FOV", &game->camera_fov, 64, 768, "%d");
    if( ImGui::IsItemHovered() )
    {
        ImGui::SetTooltip("Adjusts the camera field of view");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), platform->renderer);
}

// Main rendering function from scene_tile_test.cpp
static void
game_render_sdl2(struct Game* game, struct PlatformSDL2* platform, int deltas)
{
    SDL_Texture* texture = platform->texture;
    SDL_Renderer* renderer = platform->renderer;
    int* pixel_buffer = platform->pixel_buffer;

    // Get the frequency (ticks per second)
    Uint64 frequency = SDL_GetPerformanceFrequency();

    memset(platform->pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
    int render_ops = game->max_render_ops;
    if( !game->manual_render_ops )
    {
        if( game->max_render_ops > game->op_count || game->max_render_ops == 0 )
        {
            if( game->ops )
                free(game->ops);
            game->ops = render_scene_compute_ops(
                game->camera_x, game->camera_y, game->camera_z, game->scene, &game->op_count);
            game->max_render_ops = game->op_count;
            render_ops = game->op_count;
        }
        else
        {
            game->max_render_ops += 10;
        }
    }

    for( int i = 0; i < deltas; i++ )
    {
        struct TexWalk* walk = textures_cache_walk_new(game->textures_cache);

        while( textures_cache_walk_next(walk) )
        {
            texture_animate(walk->texture, 1);
        }

        textures_cache_walk_free(walk);
    }

    Uint64 start_ticks = SDL_GetPerformanceCounter();
    struct IterRenderSceneOps iter;
    struct IterRenderModel iter_model;

    iter_render_scene_ops_init(
        &iter,
        game->frustrum_cullmap,
        game->scene,
        game->ops,
        game->op_count,
        render_ops,
        game->camera_pitch,
        game->camera_yaw,
        game->camera_x / 128,
        game->camera_z / 128);

    int last_model_hit = -1;
    struct SceneModel* last_model_hit_model = NULL;
    int last_model_hit_yaw = 0;
    int min_z = 2;
    int max_z = 4;
    int min_x = 2;
    int max_x = 4;
    while( iter_render_scene_ops_next(&iter) )
    {
        if( iter.value.tile_nullable_ )
        {
            render_scene_tile(
                pixel_buffer,
                g_screen_vertices_x,
                g_screen_vertices_y,
                g_screen_vertices_z,
                g_ortho_vertices_x,
                g_ortho_vertices_y,
                g_ortho_vertices_z,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                // Had to use 100 here because of the scale, near plane z was resulting in triangles
                // extremely close to the camera.
                50,
                game->camera_x,
                game->camera_y,
                game->camera_z,
                game->camera_pitch,
                game->camera_yaw,
                game->camera_roll,
                game->camera_fov,
                iter.value.tile_nullable_,
                game->textures_cache,
                NULL);
        }

        if( iter.value.model_nullable_ )
        {
            if( !iter.value.model_nullable_->model )
                continue;

            // Advance animations
            struct CacheConfigSequence* sequence = iter.value.model_nullable_->sequence;
            if( sequence )
            {
                for( int i = 0; i < deltas; i++ )
                {
                    iter.value.model_nullable_->anim_frame_count += 1;
                    if( iter.value.model_nullable_->anim_frame_count >=
                        sequence->frame_lengths[iter.value.model_nullable_->anim_frame_step] )
                    {
                        iter.value.model_nullable_->anim_frame_count = 0;
                        iter.value.model_nullable_->anim_frame_step += 1;
                        if( iter.value.model_nullable_->anim_frame_step >= sequence->frame_count )
                        {
                            iter.value.model_nullable_->anim_frame_step = 0;
                        }
                    }
                }
                memcpy(
                    iter.value.model_nullable_->model->vertices_x,
                    iter.value.model_nullable_->original_vertices_x,
                    sizeof(int) * iter.value.model_nullable_->model->vertex_count);
                memcpy(
                    iter.value.model_nullable_->model->vertices_y,
                    iter.value.model_nullable_->original_vertices_y,
                    sizeof(int) * iter.value.model_nullable_->model->vertex_count);
                memcpy(
                    iter.value.model_nullable_->model->vertices_z,
                    iter.value.model_nullable_->original_vertices_z,
                    sizeof(int) * iter.value.model_nullable_->model->vertex_count);
                if( iter.value.model_nullable_->model->face_alphas )
                    memcpy(
                        iter.value.model_nullable_->model->face_alphas,
                        iter.value.model_nullable_->original_face_alphas,
                        sizeof(int) * iter.value.model_nullable_->model->face_count);

                anim_frame_apply(
                    iter.value.model_nullable_->frames[iter.value.model_nullable_->anim_frame_step],
                    iter.value.model_nullable_->framemap,
                    iter.value.model_nullable_->model->vertices_x,
                    iter.value.model_nullable_->model->vertices_y,
                    iter.value.model_nullable_->model->vertices_z,
                    iter.value.model_nullable_->model->face_alphas,
                    iter.value.model_nullable_->vertex_bones->bones_count,
                    iter.value.model_nullable_->vertex_bones->bones,
                    iter.value.model_nullable_->vertex_bones->bones_sizes,
                    iter.value.model_nullable_->face_bones
                        ? iter.value.model_nullable_->face_bones->bones_count
                        : 0,
                    iter.value.model_nullable_->face_bones
                        ? iter.value.model_nullable_->face_bones->bones
                        : NULL,
                    iter.value.model_nullable_->face_bones
                        ? iter.value.model_nullable_->face_bones->bones_sizes
                        : NULL);
            }
            int yaw_adjust = iter.value.yaw;
            iter_render_model_init(
                &iter_model,
                iter.value.model_nullable_,
                // TODO: For wall decor, this should probably be set on the model->yaw rather than
                // on the op.
                yaw_adjust,
                game->camera_x,
                game->camera_y,
                game->camera_z,
                game->camera_pitch,
                game->camera_yaw,
                game->camera_roll,
                game->camera_fov,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                50);
            int model_intersected = 0;
            while( iter_render_model_next(&iter_model) )
            {
                int face = iter_model.value_face;

                bool is_in_bb = false;
                if( game->mouse_x > 0 && game->mouse_x >= iter_model.aabb_min_screen_x &&
                    game->mouse_x <= iter_model.aabb_max_screen_x &&
                    game->mouse_y >= iter_model.aabb_min_screen_y &&
                    game->mouse_y <= iter_model.aabb_max_screen_y )
                {
                    is_in_bb = true;
                }

                if( !model_intersected && is_in_bb )
                {
                    // Get face vertex indices
                    int face_a = iter.value.model_nullable_->model->face_indices_a[face];
                    int face_b = iter.value.model_nullable_->model->face_indices_b[face];
                    int face_c = iter.value.model_nullable_->model->face_indices_c[face];

                    // Get screen coordinates of the triangle vertices
                    int x1 = iter_model.screen_vertices_x[face_a] + SCREEN_WIDTH / 2;
                    int y1 = iter_model.screen_vertices_y[face_a] + SCREEN_HEIGHT / 2;
                    int x2 = iter_model.screen_vertices_x[face_b] + SCREEN_WIDTH / 2;
                    int y2 = iter_model.screen_vertices_y[face_b] + SCREEN_HEIGHT / 2;
                    int x3 = iter_model.screen_vertices_x[face_c] + SCREEN_WIDTH / 2;
                    int y3 = iter_model.screen_vertices_y[face_c] + SCREEN_HEIGHT / 2;

                    // Check if mouse is inside the triangle using barycentric coordinates
                    bool mouse_in_triangle = false;
                    if( x1 != -5000 && x2 != -5000 && x3 != -5000 )
                    { // Skip clipped triangles
                        int denominator = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
                        if( denominator != 0 )
                        {
                            float a = ((y2 - y3) * (game->mouse_x - x3) +
                                       (x3 - x2) * (game->mouse_y - y3)) /
                                      (float)denominator;
                            float b = ((y3 - y1) * (game->mouse_x - x3) +
                                       (x1 - x3) * (game->mouse_y - y3)) /
                                      (float)denominator;
                            float c = 1 - a - b;
                            mouse_in_triangle = (a >= 0 && b >= 0 && c >= 0);
                        }
                    }

                    if( mouse_in_triangle )
                    {
                        game->hover_model = iter_model.model->model_id;
                        game->hover_loc_x = iter_model.model->_chunk_pos_x;
                        game->hover_loc_y = iter_model.model->_chunk_pos_y;
                        game->hover_loc_level = iter_model.model->_chunk_pos_level;
                        game->hover_loc_yaw = iter_model.model->yaw;

                        last_model_hit_model = iter.value.model_nullable_;
                        last_model_hit_yaw = iter.value.model_nullable_->yaw + iter.value.yaw;

                        model_intersected = true;
                    }
                }
                // Only draw the face if mouse is inside the triangle
                model_draw_face(
                    pixel_buffer,
                    face,
                    iter.value.model_nullable_->model->face_infos,
                    iter.value.model_nullable_->model->face_indices_a,
                    iter.value.model_nullable_->model->face_indices_b,
                    iter.value.model_nullable_->model->face_indices_c,
                    iter.value.model_nullable_->model->face_count,
                    iter_model.screen_vertices_x,
                    iter_model.screen_vertices_y,
                    iter_model.screen_vertices_z,
                    iter_model.ortho_vertices_x,
                    iter_model.ortho_vertices_y,
                    iter_model.ortho_vertices_z,
                    iter.value.model_nullable_->model->vertex_count,
                    iter.value.model_nullable_->model->face_textures,
                    iter.value.model_nullable_->model->face_texture_coords,
                    iter.value.model_nullable_->model->textured_face_count,
                    iter.value.model_nullable_->model->textured_p_coordinate,
                    iter.value.model_nullable_->model->textured_m_coordinate,
                    iter.value.model_nullable_->model->textured_n_coordinate,
                    iter.value.model_nullable_->model->textured_face_count,
                    iter.value.model_nullable_->lighting->face_colors_hsl_a,
                    iter.value.model_nullable_->lighting->face_colors_hsl_b,
                    iter.value.model_nullable_->lighting->face_colors_hsl_c,
                    iter.value.model_nullable_->model->face_alphas,
                    SCREEN_WIDTH / 2,
                    SCREEN_HEIGHT / 2,
                    50,
                    SCREEN_WIDTH,
                    SCREEN_HEIGHT,
                    game->camera_fov,
                    game->textures_cache);
            }
        }
    }

    if( last_model_hit_model )
    {
        memset(g_blit_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

        int model_id = last_model_hit_model->model_id;
        int model_x = last_model_hit_model->_chunk_pos_x;
        int model_y = last_model_hit_model->_chunk_pos_y;
        int model_z = last_model_hit_model->_chunk_pos_level;

        struct AABB aabb;

        render_scene_model(
            g_blit_buffer,
            SCREEN_WIDTH,
            SCREEN_HEIGHT,
            // Had to use 100 here because of the scale, near plane z was resulting in
            // extremely close to the camera.
            100,
            last_model_hit_yaw,
            game->camera_x,
            game->camera_y,
            game->camera_z,
            game->camera_pitch,
            game->camera_yaw,
            game->camera_roll,
            game->camera_fov,
            &aabb,
            last_model_hit_model,
            game->textures_cache);

        apply_outline_effect(
            pixel_buffer,
            g_blit_buffer,
            SCREEN_WIDTH,
            SCREEN_HEIGHT,
            aabb.min_screen_x,
            aabb.max_screen_x,
            aabb.min_screen_y,
            aabb.max_screen_y);

        // Draw AABB rectangle outline
        for( int x = aabb.min_screen_x; x <= aabb.max_screen_x; x++ )
        {
            if( x >= 0 && x < SCREEN_WIDTH )
            {
                // Top line
                if( aabb.min_screen_y >= 0 && aabb.min_screen_y < SCREEN_HEIGHT )
                {
                    pixel_buffer[aabb.min_screen_y * SCREEN_WIDTH + x] = 0xFFFFFF;
                }
                // Bottom line
                if( aabb.max_screen_y >= 0 && aabb.max_screen_y < SCREEN_HEIGHT )
                {
                    pixel_buffer[aabb.max_screen_y * SCREEN_WIDTH + x] = 0xFFFFFF;
                }
            }
        }

        for( int y = aabb.min_screen_y; y <= aabb.max_screen_y; y++ )
        {
            if( y >= 0 && y < SCREEN_HEIGHT )
            {
                // Left line
                if( aabb.min_screen_x >= 0 && aabb.min_screen_x < SCREEN_WIDTH )
                {
                    pixel_buffer[y * SCREEN_WIDTH + aabb.min_screen_x] = 0xFFFFFF;
                }
                // Right line
                if( aabb.max_screen_x >= 0 && aabb.max_screen_x < SCREEN_WIDTH )
                {
                    pixel_buffer[y * SCREEN_WIDTH + aabb.max_screen_x] = 0xFFFFFF;
                }
            }
        }
    }

    // Draw horizontal line at screen center
    for( int x = 0; x < SCREEN_WIDTH; x++ )
    {
        pixel_buffer[(SCREEN_HEIGHT / 2) * SCREEN_WIDTH + x] = 0xFFFFFF;
    }

    if( game->mouse_click_x != -1 && game->mouse_click_y != -1 )
    {
        if( game->mouse_click_cycle >= 400 )
        {
            game->mouse_click_cycle = 0;
            game->mouse_click_x = -1;
            game->mouse_click_y = -1;
        }
        else
        {
            int offset_x = -8;
            int offset_y = -8;

            // Draw cross pixels at mouse click location
            {
                int cross_width = 16;
                int* cross_pixels = game->image_cross_pixels[game->mouse_click_cycle / 100];
                if( cross_pixels )
                {
                    // Draw 8x8 cross sprite
                    for( int y = 0; y < cross_width; y++ )
                    {
                        for( int x = 0; x < cross_width; x++ )
                        {
                            int pixel = cross_pixels[y * cross_width + x];
                            if( pixel != 0 )
                            {
                                int screen_x = game->mouse_click_x + x + offset_x;
                                int screen_y = game->mouse_click_y + y + offset_y;
                                if( screen_x >= 0 && screen_x < SCREEN_WIDTH && screen_y >= 0 &&
                                    screen_y < SCREEN_HEIGHT )
                                {
                                    pixel_buffer[screen_y * SCREEN_WIDTH + screen_x] = pixel;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Uint64 end_ticks = SDL_GetPerformanceCounter();
    game->start_time = start_ticks;
    game->end_time = end_ticks;

    game->frame_count++;
    game->frame_time_sum += end_ticks - start_ticks;

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        pixel_buffer,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        32,
        SCREEN_WIDTH * sizeof(int),
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000);

    // Copy the pixels into the texture
    int* pix_write = NULL;
    int _pitch_unused = 0;
    if( SDL_LockTexture(texture, NULL, (void**)&pix_write, &_pitch_unused) < 0 )
        return;

    int row_size = SCREEN_WIDTH * sizeof(int);
    int* src_pixels = (int*)surface->pixels;
    for( int src_y = 0; src_y < (SCREEN_HEIGHT); src_y++ )
    {
        // Calculate offset in texture to write a single row of pixels
        int* row = &pix_write[(src_y * SCREEN_WIDTH)];
        // Copy a single row of pixels
        memcpy(row, &src_pixels[(src_y - 0) * SCREEN_WIDTH], row_size);
    }

    // Unlock the texture so that it may be used elsewhere
    SDL_UnlockTexture(texture);

    // Calculate destination rectangle to scale the texture to the current drawable size
    SDL_Rect dst_rect;
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = platform->drawable_width;
    dst_rect.h = platform->drawable_height;

    // Use bilinear scaling when copying texture to renderer
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); // Enable bilinear filtering

    // Calculate aspect ratio preserving dimensions
    float src_aspect = (float)SCREEN_WIDTH / SCREEN_HEIGHT;
    float dst_aspect = (float)platform->drawable_width / platform->drawable_height;

    if( src_aspect > dst_aspect )
    {
        // Source is wider - fit to width
        dst_rect.w = platform->drawable_width;
        dst_rect.h = (int)(platform->drawable_width / src_aspect);
        dst_rect.x = 0;
        dst_rect.y = (platform->drawable_height - dst_rect.h) / 2;
    }
    else
    {
        // Source is taller - fit to height
        dst_rect.h = platform->drawable_height;
        dst_rect.w = (int)(platform->drawable_height * src_aspect);
        dst_rect.y = 0;
        dst_rect.x = (platform->drawable_width - dst_rect.w) / 2;
    }

    SDL_RenderCopy(renderer, texture, NULL, &dst_rect);

    SDL_FreeSurface(surface);
}

// Initialization functions from scene_tile_test.cpp main function
void
AndroidPlatform::initTables()
{
    LOGI("Initializing lookup tables");
    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();
    init_reciprocal16();
    LOGI("Lookup tables initialized");
}

bool
AndroidPlatform::loadSpritePacks()
{
    LOGI("Loading sprite packs");

    if( !m_cache )
    {
        LOGE("Cache not initialized for sprite loading");
        return false;
    }

    int sprite_count = m_cache->tables[CACHE_SPRITES]->archive_count;
    m_game->sprite_packs =
        (struct CacheSpritePack*)malloc(sprite_count * sizeof(struct CacheSpritePack));
    m_game->sprite_ids = (int*)malloc(sprite_count * sizeof(int));
    m_game->sprite_count = sprite_count;

    for( int sprite_index = 0; sprite_index < sprite_count; sprite_index++ )
    {
        struct CacheArchive* archive = cache_archive_new_load(m_cache, CACHE_SPRITES, sprite_index);
        if( !archive )
        {
            LOGI("Failed to load sprite archive %d", sprite_index);
            continue;
        }

        struct ArchiveReference* archives = m_cache->tables[CACHE_SPRITES]->archives;

        if( sprite_index == 299 )
        {
            struct CacheSpritePack* sprite_pack = sprite_pack_new_decode(
                (const unsigned char*)archive->data, archive->data_size, SPRITELOAD_FLAG_NORMALIZE);
            if( sprite_pack )
            {
                m_game->sprite_packs[sprite_index] = *sprite_pack;

                for( int i = 0; i < 8; i++ )
                {
                    int* pixels =
                        sprite_get_pixels(&sprite_pack->sprites[i], sprite_pack->palette, 1);
                    m_game->image_cross_pixels[i] = pixels;
                }
            }
        }

        int file_id = archives[m_cache->tables[CACHE_SPRITES]->ids[sprite_index]].index;
        m_game->sprite_ids[sprite_index] = file_id;

        cache_archive_free(archive);
    }

    LOGI("Sprite packs loaded successfully");
    return true;
}

// PlatformSDL2 cleanup function (from scene_tile_test.cpp cleanup code)
void
platform_sdl2_cleanup(struct PlatformSDL2* platform)
{
    if( platform )
    {
        if( platform->texture )
            SDL_DestroyTexture(platform->texture);
        if( platform->renderer )
            SDL_DestroyRenderer(platform->renderer);
        if( platform->window )
            SDL_DestroyWindow(platform->window);
        if( platform->pixel_buffer )
            free(platform->pixel_buffer);
        SDL_Quit();
    }
}

// External functions from the main project
extern "C" {
void init_hsl16_to_rgb_table();
void init_sin_table();
void init_cos_table();
void init_tan_table();
void init_reciprocal16();

int xtea_config_load_keys(const char* path);
struct Cache* cache_new_from_directory(const char* path);
struct Scene* scene_new_from_map(struct Cache* cache, int x, int y);
struct TexturesCache* textures_cache_new(struct Cache* cache);
struct World* world_new();
struct FrustrumCullmap* frustrum_cullmap_new(int width, int height);
}

AndroidPlatform::AndroidPlatform()
    : m_initialized(false)
    , m_running(false)
    , m_paused(false)
    , m_sdl_platform(nullptr)
    , m_game(nullptr)
    , m_cache(nullptr)
    , m_scene(nullptr)
    , m_textures_cache(nullptr)
    , m_cache_path("/data/data/com.scenetile.test/files/cache")
{
    LOGI("AndroidPlatform constructor");
}

AndroidPlatform::~AndroidPlatform()
{
    LOGI("AndroidPlatform destructor");
    cleanup();
}

bool
AndroidPlatform::init()
{
    LOGI("Initializing AndroidPlatform");

    if( m_initialized )
    {
        LOGI("Already initialized");
        return true;
    }

    // Initialize lookup tables first
    initTables();

    // Initialize SDL2
    if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0 )
    {
        LOGE("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

// Create cache directory
#ifdef _WIN32
    if( _mkdir(m_cache_path.c_str()) != 0 && errno != EEXIST )
    {
        LOGE("Failed to create cache directory: %s", strerror(errno));
        return false;
    }
#else
    if( mkdir(m_cache_path.c_str(), 0755) != 0 && errno != EEXIST )
    {
        LOGE("Failed to create cache directory: %s", strerror(errno));
        return false;
    }
#endif

    // Initialize cache and scene
    if( !initCache() )
    {
        LOGE("Failed to initialize cache");
        return false;
    }

    if( !initScene() )
    {
        LOGE("Failed to initialize scene");
        return false;
    }

    if( !initGame() )
    {
        LOGE("Failed to initialize game");
        return false;
    }

    // Load sprite packs for mouse click animation
    if( !loadSpritePacks() )
    {
        LOGE("Failed to load sprite packs");
        return false;
    }

    m_initialized = true;
    m_running = true;

    LOGI("AndroidPlatform initialization completed successfully");
    return true;
}

void
AndroidPlatform::cleanup()
{
    LOGI("Cleaning up AndroidPlatform");

    if( !m_initialized )
    {
        return;
    }

    cleanupGame();

    if( m_textures_cache )
    {
        // Free textures cache
        // textures_cache_free(m_textures_cache);
        m_textures_cache = nullptr;
    }

    if( m_scene )
    {
        // Free scene
        // scene_free(m_scene);
        m_scene = nullptr;
    }

    if( m_cache )
    {
        // Free cache
        // cache_free(m_cache);
        m_cache = nullptr;
    }

    if( m_sdl_platform )
    {
        delete m_sdl_platform;
        m_sdl_platform = nullptr;
    }

    SDL_Quit();

    m_initialized = false;
    m_running = false;

    LOGI("AndroidPlatform cleanup completed");
}

void
AndroidPlatform::pause()
{
    LOGI("Pausing AndroidPlatform");
    m_paused = true;
}

void
AndroidPlatform::resume()
{
    LOGI("Resuming AndroidPlatform");
    m_paused = false;
}

void
AndroidPlatform::runMainLoop()
{
    LOGI("Starting main loop");

    if( !m_initialized )
    {
        LOGE("Platform not initialized");
        return;
    }

    // Main loop using converted rendering pipeline
    while( m_running && !m_paused )
    {
        processEvents();
        update(1.0f / 60.0f); // 60 FPS

        // Clear renderer
        SDL_RenderClear(m_sdl_platform->renderer);

        // Render frame
        render();

        // Present renderer
        SDL_RenderPresent(m_sdl_platform->renderer);

        // Small delay to prevent excessive CPU usage
        SDL_Delay(16); // ~60 FPS
    }

    LOGI("Main loop ended");
}

bool
AndroidPlatform::initCache()
{
    LOGI("Initializing cache from: %s", m_cache_path.c_str());

    // For now, create a minimal cache structure
    // In a real implementation, you would copy assets from the APK
    // to the cache directory and load them properly

    // Load XTEA keys
    std::string xtea_path = m_cache_path + "/xteas.json";
    int xtea_count = xtea_config_load_keys(xtea_path.c_str());
    if( xtea_count == -1 )
    {
        LOGI("No XTEA keys found, continuing without them");
    }
    else
    {
        LOGI("Loaded %d XTEA keys", xtea_count);
    }

    // Create cache
    m_cache = cache_new_from_directory(m_cache_path.c_str());
    if( !m_cache )
    {
        LOGE("Failed to create cache from directory: %s", m_cache_path.c_str());
        return false;
    }

    LOGI("Cache initialized successfully");
    return true;
}

bool
AndroidPlatform::initScene()
{
    LOGI("Initializing scene");

    if( !m_cache )
    {
        LOGE("Cache not initialized");
        return false;
    }

    // Create scene from map (50, 50 is a default location)
    m_scene = scene_new_from_map(m_cache, 50, 50);
    if( !m_scene )
    {
        LOGE("Failed to create scene from map");
        return false;
    }

    // Create textures cache
    m_textures_cache = textures_cache_new(m_cache);
    if( !m_textures_cache )
    {
        LOGE("Failed to create textures cache");
        return false;
    }

    LOGI("Scene initialized successfully");
    return true;
}

bool
AndroidPlatform::initGame()
{
    LOGI("Initializing game");

    if( !m_scene || !m_textures_cache )
    {
        LOGE("Scene or textures cache not initialized");
        return false;
    }

    // Initialize SDL2 platform wrapper
    m_sdl_platform = new PlatformSDL2();
    if( !platform_sdl2_init(m_sdl_platform) )
    {
        LOGE("Failed to initialize SDL2 platform");
        return false;
    }

    // Initialize game structure
    m_game = new Game();
    memset(m_game, 0, sizeof(Game));

    // Set up basic game state
    m_game->scene = m_scene;
    m_game->textures_cache = m_textures_cache;
    m_game->world = world_new();
    m_game->frustrum_cullmap = frustrum_cullmap_new(40, 50);

    // Set camera position (Lumbridge Kitchen from the original)
    m_game->camera_pitch = 290;
    m_game->camera_yaw = 1538;
    m_game->camera_roll = 0;
    m_game->camera_fov = 512;
    m_game->camera_x = 390;
    m_game->camera_y = -1340;
    m_game->camera_z = 1916;
    m_game->camera_speed = 50;

    // Initialize additional game state from scene_tile_test.cpp
    m_game->show_debug_tiles = 1;
    m_game->player_tile_x = 10;
    m_game->player_tile_y = 10;
    m_game->tile_count = MAP_TILE_COUNT;
    m_game->scene_locs = NULL;
    m_game->show_loc_x = 63;
    m_game->show_loc_y = 63;

    // Initialize image cross pixels for mouse click animation
    m_game->image_cross_pixels = (int**)malloc(8 * sizeof(int*));
    memset(m_game->image_cross_pixels, 0, 8 * sizeof(int*));

    LOGI("Game initialized successfully");
    return true;
}

void
AndroidPlatform::cleanupGame()
{
    LOGI("Cleaning up game");

    if( m_game )
    {
        // Free game resources using the converted function
        game_free(m_game);
        delete m_game;
        m_game = nullptr;
    }

    if( m_sdl_platform )
    {
        platform_sdl2_cleanup(m_sdl_platform);
        delete m_sdl_platform;
        m_sdl_platform = nullptr;
    }
}

void
AndroidPlatform::processEvents()
{
    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        // Process ImGui events first
        ImGui_ImplSDL2_ProcessEvent(&event);

        // Check if ImGui wants to capture keyboard/mouse input
        ImGuiIO& io = ImGui::GetIO();
        bool imgui_wants_keyboard = io.WantCaptureKeyboard;
        bool imgui_wants_mouse = io.WantCaptureMouse;

        switch( event.type )
        {
        case SDL_QUIT:
            m_running = false;
            break;
        case SDL_APP_TERMINATING:
            m_running = false;
            break;
        case SDL_APP_LOWMEMORY:
            LOGI("Low memory warning");
            break;
        case SDL_WINDOWEVENT:
            if( event.window.event == SDL_WINDOWEVENT_RESIZED )
            {
                SDL_GetWindowSize(
                    m_sdl_platform->window,
                    &m_sdl_platform->window_width,
                    &m_sdl_platform->window_height);
                SDL_GetRendererOutputSize(
                    m_sdl_platform->renderer,
                    &m_sdl_platform->drawable_width,
                    &m_sdl_platform->drawable_height);
            }
            break;
        case SDL_MOUSEMOTION:
            if( !imgui_wants_mouse && m_game )
            {
                transform_mouse_coordinates(
                    event.motion.x,
                    event.motion.y,
                    &m_game->mouse_x,
                    &m_game->mouse_y,
                    m_sdl_platform);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if( !imgui_wants_mouse && m_game )
            {
                int transformed_x, transformed_y;
                transform_mouse_coordinates(
                    event.button.x, event.button.y, &transformed_x, &transformed_y, m_sdl_platform);
                m_game->mouse_click_x = transformed_x;
                m_game->mouse_click_y = transformed_y;
                m_game->mouse_click_cycle = 0;
            }
            break;
        case SDL_KEYDOWN:
            if( !imgui_wants_keyboard && m_game )
            {
                switch( event.key.keysym.sym )
                {
                case SDLK_ESCAPE:
                    m_running = false;
                    break;
                case SDLK_UP:
                    m_game->camera_pitch = (m_game->camera_pitch + 10) % 2048;
                    break;
                case SDLK_DOWN:
                    m_game->camera_pitch = (m_game->camera_pitch - 10 + 2048) % 2048;
                    break;
                case SDLK_LEFT:
                    m_game->camera_yaw = (m_game->camera_yaw + 10) % 2048;
                    break;
                case SDLK_RIGHT:
                    m_game->camera_yaw = (m_game->camera_yaw - 10 + 2048) % 2048;
                    break;
                case SDLK_w:
                    m_game->camera_x +=
                        (g_sin_table[m_game->camera_yaw] * m_game->camera_speed) >> 16;
                    m_game->camera_y -=
                        (g_cos_table[m_game->camera_yaw] * m_game->camera_speed) >> 16;
                    break;
                case SDLK_s:
                    m_game->camera_x -=
                        (g_sin_table[m_game->camera_yaw] * m_game->camera_speed) >> 16;
                    m_game->camera_y +=
                        (g_cos_table[m_game->camera_yaw] * m_game->camera_speed) >> 16;
                    break;
                case SDLK_a:
                    m_game->camera_x +=
                        (g_cos_table[m_game->camera_yaw] * m_game->camera_speed) >> 16;
                    m_game->camera_y +=
                        (g_sin_table[m_game->camera_yaw] * m_game->camera_speed) >> 16;
                    break;
                case SDLK_d:
                    m_game->camera_x -=
                        (g_cos_table[m_game->camera_yaw] * m_game->camera_speed) >> 16;
                    m_game->camera_y -=
                        (g_sin_table[m_game->camera_yaw] * m_game->camera_speed) >> 16;
                    break;
                case SDLK_r:
                    m_game->camera_z += m_game->camera_speed;
                    break;
                case SDLK_f:
                    m_game->camera_z -= m_game->camera_speed;
                    break;
                case SDLK_q:
                    m_game->fake_pitch = (m_game->fake_pitch + 10) % 2048;
                    break;
                case SDLK_e:
                    m_game->fake_pitch = (m_game->fake_pitch - 10 + 2048) % 2048;
                    break;
                }
            }
            break;
        default:
            break;
        }
    }
}

void
AndroidPlatform::update(float delta_time)
{
    // Update game logic here
    if( m_game )
    {
        // Update mouse click animation
        m_game->mouse_click_cycle += 20;
    }
}

void
AndroidPlatform::render()
{
    // Render the scene using converted functions
    if( m_sdl_platform && m_game )
    {
        game_render_sdl2(m_game, m_sdl_platform, 1);
        game_render_imgui(m_game, m_sdl_platform);
    }
}
