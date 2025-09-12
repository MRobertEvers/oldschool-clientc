extern "C" {
#include "bmp.h"
#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/frustrum_cullmap.h"
#include "osrs/render.h"
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

// SDL2 main handling
#ifdef main
#undef main
#endif

#define GUI_WINDOW_WIDTH 400
#define GUI_WINDOW_HEIGHT 600
#define CACHE_PATH "../cache"

// ImGui headers
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

extern "C" int g_sin_table[2048];
extern "C" int g_cos_table[2048];
extern "C" int g_tan_table[2048];

int g_blit_buffer[SCREEN_WIDTH * SCREEN_HEIGHT] = { 0 };

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

//   This tool renders a color palette using jagex's 16-bit HSL, 6 bits
//             for hue, 3 for saturation and 7 for lightness, bitpacked and
//             represented as a short.
extern "C" int g_hsl16_to_rgb_table[65536];

#include <stdio.h>

static void
apply_outline_effect(int* pixel_buffer, int* source_buffer, int width, int height)
{
    for( int y = 1; y < height - 1; y++ )
    {
        for( int x = 1; x < width - 1; x++ )
        {
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

// 4151 is abyssal whip
struct Game
{
    int camera_yaw;
    int camera_pitch;
    int camera_roll;
    int camera_fov;
    int camera_x;
    int camera_y;
    int camera_z;

    int hover_model;
    int hover_loc_x;
    int hover_loc_y;
    int hover_loc_level;
    int hover_loc_yaw;

    int mouse_x;
    int mouse_y;

    int mouse_click_x;
    int mouse_click_y;
    int mouse_click_cycle;

    int player_tile_x;
    int player_tile_y;
    int player_yaw;
    int player_animation_id;
    struct SceneModel* player_model;

    struct FrustrumCullmap* frustrum_cullmap;
    int fake_pitch;

    uint64_t start_time;
    uint64_t end_time;

    uint64_t frame_count;
    uint64_t frame_time_sum;

    struct SceneTile* tiles;
    int tile_count;

    struct SceneTextures* scene_textures;

    struct CacheTexture* textures;
    int* texture_ids;
    int texture_count;

    struct CacheSpritePack* sprite_packs;
    int* sprite_ids;
    int sprite_count;

    struct CacheMapLocs* map_locs;

    struct SceneLocs* scene_locs;
    struct SceneTextures* loc_textures;

    int** image_cross_pixels;
    // THese are of known size.

    struct Scene* scene;
    struct World* world;

    struct SceneOp* ops;
    int op_count;

    int max_render_ops;
    int manual_render_ops;

    int show_loc_enabled;
    int show_loc_x;
    int show_loc_y;

    struct TexturesCache* textures_cache;

    int show_debug_tiles;
    bool show_debug_glow;
    int debug_glow_r;
    int debug_glow_g;
    int debug_glow_b;
    int debug_glow_intensity;
    int debug_glow_radius;
};

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

struct PlatformSDL2
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    int* pixel_buffer;
    int window_width;
    int window_height;
    int drawable_width;
    int drawable_height;
};

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
    ImGui::End();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), platform->renderer);
}

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
    struct SceneModel* model,
    struct TexturesCache* textures_cache)
{
    int x = model->region_x + camera_x;
    int y = model->region_height + camera_y;
    int z = model->region_z + camera_z;

    if( !model->bounds_cylinder )
    {
        model->bounds_cylinder = (struct BoundsCylinder*)malloc(sizeof(struct BoundsCylinder));
        *model->bounds_cylinder = calculate_bounds_cylinder(
            model->model->vertex_count,
            model->model->vertices_x,
            model->model->vertices_y,
            model->model->vertices_z);
    }

    // if( model->mirrored )
    // {
    //     yaw += 1024;
    // }

    // int rotation = model->orientation;
    // while( rotation-- )
    // {
    //     yaw += 1536;
    // }
    yaw %= 2048;

    x += model->offset_x;
    y += model->offset_y;
    z += model->offset_height;

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
        x,
        y,
        z,
        camera_pitch,
        camera_yaw,
        camera_roll,
        fov,
        model->model,
        model->lighting,
        model->bounds_cylinder,
        NULL,
        NULL,
        NULL,
        textures_cache);
}

static int g_screen_vertices_x[20];
static int g_screen_vertices_y[20];
static int g_screen_vertices_z[20];
static int g_ortho_vertices_x[20];
static int g_ortho_vertices_y[20];
static int g_ortho_vertices_z[20];

static void
game_render_sdl2(struct Game* game, struct PlatformSDL2* platform)
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

    struct TexWalk* walk = textures_cache_walk_new(game->textures_cache);

    while( textures_cache_walk_next(walk) )
    {
        texture_animate(walk->texture, 1);
    }

    textures_cache_walk_free(walk);
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
    int min_z = 1;
    int max_z = 1;
    int min_x = 1;
    int max_x = 1;
    while( iter_render_scene_ops_next(&iter) )
    {
        if( iter.value.z < min_z || iter.value.z > max_z || iter.value.x < min_x ||
            iter.value.x > max_x )
            continue;
        if( iter.value.tile_nullable_ )
        {
            render_scene_tile(
                g_screen_vertices_x,
                g_screen_vertices_y,
                g_screen_vertices_z,
                g_ortho_vertices_x,
                g_ortho_vertices_y,
                g_ortho_vertices_z,
                pixel_buffer,
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
            // if( game->hover_loc_x == iter.value.model_nullable_->_chunk_pos_x &&
            //     game->hover_loc_y == iter.value.model_nullable_->_chunk_pos_y &&
            //     game->hover_loc_level == iter.value.model_nullable_->_chunk_pos_level &&
            //     iter.value.model_nullable_->model_id == game->hover_model )
            // {
            //     yaw_adjust += game->hover_loc_yaw;
            // }

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

                if( !model_intersected )
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
                // model_draw_face(
                //     pixel_buffer,
                //     face,
                //     iter.value.model_nullable_->model->face_infos,
                //     iter.value.model_nullable_->model->face_indices_a,
                //     iter.value.model_nullable_->model->face_indices_b,
                //     iter.value.model_nullable_->model->face_indices_c,
                //     iter.value.model_nullable_->model->face_count,
                //     iter_model.screen_vertices_x,
                //     iter_model.screen_vertices_y,
                //     iter_model.screen_vertices_z,
                //     iter_model.ortho_vertices_x,
                //     iter_model.ortho_vertices_y,
                //     iter_model.ortho_vertices_z,
                //     iter.value.model_nullable_->model->vertex_count,
                //     iter.value.model_nullable_->model->face_textures,
                //     iter.value.model_nullable_->model->face_texture_coords,
                //     iter.value.model_nullable_->model->textured_face_count,
                //     iter.value.model_nullable_->model->textured_p_coordinate,
                //     iter.value.model_nullable_->model->textured_m_coordinate,
                //     iter.value.model_nullable_->model->textured_n_coordinate,
                //     iter.value.model_nullable_->model->textured_face_count,
                //     iter.value.model_nullable_->lighting->face_colors_hsl_a,
                //     iter.value.model_nullable_->lighting->face_colors_hsl_b,
                //     iter.value.model_nullable_->lighting->face_colors_hsl_c,
                //     iter.value.model_nullable_->model->face_alphas,
                //     SCREEN_WIDTH / 2,
                //     SCREEN_HEIGHT / 2,
                //     SCREEN_WIDTH,
                //     SCREEN_HEIGHT,
                //     game->textures_cache);
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

    if( last_model_hit_model )
    {
        memset(g_blit_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

        int model_id = last_model_hit_model->model_id;
        int model_x = last_model_hit_model->_chunk_pos_x;
        int model_y = last_model_hit_model->_chunk_pos_y;
        int model_z = last_model_hit_model->_chunk_pos_level;

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
            last_model_hit_model,
            game->textures_cache);

        apply_outline_effect(pixel_buffer, g_blit_buffer, SCREEN_WIDTH, SCREEN_HEIGHT);
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
                // if( game->mouse_click_cycle / 100 == 0 )
                // {
                //     offset_x = -10;
                //     offset_y = -7;
                // }
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

    // render_scene_ops(
    //     game->ops,
    //     game->op_count,
    //     0,
    //     game->max_render_ops,
    //     pixel_buffer,
    //     SCREEN_WIDTH,
    //     SCREEN_HEIGHT,
    //     // Had to use 100 here because of the scale, near plane z was resulting in triangles
    //     // extremely close to the camera.
    //     100,
    //     game->camera_x,
    //     game->camera_y,
    //     game->camera_z,
    //     game->camera_pitch,
    //     game->camera_yaw,
    //     game->camera_roll,
    //     game->camera_fov,
    //     game->scene,
    //     game->textures_cache);

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

    // Draw debug text for camera position and rotation
    // printf(
    //     "Camera: x=%d y=%d z=%d pitch=%d yaw=%d\n",
    //     game->camera_x,
    //     game->camera_y,
    //     game->camera_z,
    //     game->camera_pitch,
    //     game->camera_yaw);

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

#include <iostream>

// Emscripten compatibility
#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>

#include <emscripten.h>

// Global game state for Emscripten access
struct Game* g_game = NULL;
struct PlatformSDL2* g_platform = NULL;
bool g_quit = false;

// Emscripten main loop function
void
emscripten_main_loop()
{
    if( g_quit || !g_game || !g_platform )
    {
        return;
    }

    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        ImGui_ImplSDL2_ProcessEvent(&event);

        // Check if ImGui wants to capture keyboard/mouse input
        ImGuiIO& io = ImGui::GetIO();
        bool imgui_wants_keyboard = io.WantCaptureKeyboard;
        bool imgui_wants_mouse = io.WantCaptureMouse;

        if( event.type == SDL_QUIT )
        {
            g_quit = true;
        }
        else if( event.type == SDL_WINDOWEVENT )
        {
            if( event.window.event == SDL_WINDOWEVENT_RESIZED )
            {
                SDL_GetWindowSize(
                    g_platform->window, &g_platform->window_width, &g_platform->window_height);
                SDL_GetRendererOutputSize(
                    g_platform->renderer,
                    &g_platform->drawable_width,
                    &g_platform->drawable_height);
            }
        }
        else if( event.type == SDL_MOUSEMOTION && !imgui_wants_mouse )
        {
            transform_mouse_coordinates(
                event.motion.x, event.motion.y, &g_game->mouse_x, &g_game->mouse_y, g_platform);
        }
        else if( event.type == SDL_MOUSEBUTTONDOWN && !imgui_wants_mouse )
        {
            int transformed_x, transformed_y;
            transform_mouse_coordinates(
                event.button.x, event.button.y, &transformed_x, &transformed_y, g_platform);
            g_game->mouse_click_x = transformed_x;
            g_game->mouse_click_y = transformed_y;
            g_game->mouse_click_cycle = 0;
        }
        else if( event.type == SDL_KEYDOWN && !imgui_wants_keyboard )
        {
            switch( event.key.keysym.sym )
            {
            case SDLK_ESCAPE:
                g_quit = true;
                break;
            case SDLK_UP:
                g_game->camera_pitch = (g_game->camera_pitch + 10) % 2048;
                break;
            case SDLK_DOWN:
                g_game->camera_pitch = (g_game->camera_pitch - 10 + 2048) % 2048;
                break;
            case SDLK_LEFT:
                g_game->camera_yaw = (g_game->camera_yaw + 10) % 2048;
                break;
            case SDLK_RIGHT:
                g_game->camera_yaw = (g_game->camera_yaw - 10 + 2048) % 2048;
                break;
            case SDLK_w:
                g_game->camera_x += (g_sin_table[g_game->camera_yaw] * 200) >> 16;
                g_game->camera_y -= (g_cos_table[g_game->camera_yaw] * 200) >> 16;
                break;
            case SDLK_s:
                g_game->camera_x -= (g_sin_table[g_game->camera_yaw] * 200) >> 16;
                g_game->camera_y += (g_cos_table[g_game->camera_yaw] * 200) >> 16;
                break;
            case SDLK_a:
                g_game->camera_x += (g_cos_table[g_game->camera_yaw] * 200) >> 16;
                g_game->camera_y += (g_sin_table[g_game->camera_yaw] * 200) >> 16;
                break;
            case SDLK_d:
                g_game->camera_x -= (g_cos_table[g_game->camera_yaw] * 200) >> 16;
                g_game->camera_y -= (g_sin_table[g_game->camera_yaw] * 200) >> 16;
                break;
            case SDLK_r:
                g_game->camera_z += 200;
                break;
            case SDLK_f:
                g_game->camera_z -= 200;
                break;
            case SDLK_q:
                g_game->fake_pitch = (g_game->fake_pitch + 10) % 2048;
                break;
            case SDLK_e:
                g_game->fake_pitch = (g_game->fake_pitch - 10 + 2048) % 2048;
                break;
            }
        }
    }

    // Update mouse click animation
    g_game->mouse_click_cycle += 20;

    // Render frame
    SDL_RenderClear(g_platform->renderer);
    game_render_sdl2(g_game, g_platform);
    game_render_imgui(g_game, g_platform);
    SDL_RenderPresent(g_platform->renderer);
}

// Exported functions for JavaScript
extern "C" {
EMSCRIPTEN_KEEPALIVE
void
set_camera_position(int x, int y, int z)
{
    if( g_game )
    {
        g_game->camera_x = x;
        g_game->camera_y = y;
        g_game->camera_z = z;
    }
}

EMSCRIPTEN_KEEPALIVE
void
set_camera_rotation(int pitch, int yaw, int roll)
{
    if( g_game )
    {
        g_game->camera_pitch = pitch;
        g_game->camera_yaw = yaw;
        g_game->camera_roll = roll;
    }
}

EMSCRIPTEN_KEEPALIVE
void
set_camera_fov(int fov)
{
    if( g_game )
    {
        g_game->camera_fov = fov;
    }
}

EMSCRIPTEN_KEEPALIVE
int*
get_pixel_buffer()
{
    return g_platform ? g_platform->pixel_buffer : nullptr;
}

EMSCRIPTEN_KEEPALIVE
int
get_screen_width()
{
    return SCREEN_WIDTH;
}

EMSCRIPTEN_KEEPALIVE
int
get_screen_height()
{
    return SCREEN_HEIGHT;
}

EMSCRIPTEN_KEEPALIVE
void
render_frame()
{
    if( g_game && g_platform )
    {
        game_render_sdl2(g_game, g_platform);
    }
}
}
#endif

int
main(int argc, char* argv[])
{
    std::cout << "SDL_main" << std::endl;
    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();
    init_reciprocal16();

    printf("Loading XTEA keys from: ../cache/xteas.json\n");
    int xtea_keys_count = xtea_config_load_keys("../cache/xteas.json");
    if( xtea_keys_count == -1 )
    {
        printf("Failed to load xtea keys from: ../cache/xteas.json\n");
        printf("Make sure the xteas.json file exists in the cache directory\n");
        return 1;
    }
    printf("Loaded %d XTEA keys successfully\n", xtea_keys_count);

    printf("Loading cache from directory: %s\n", CACHE_PATH);
    struct Cache* cache = cache_new_from_directory(CACHE_PATH);
    if( !cache )
    {
        printf("Failed to load cache from directory2: %s\n", CACHE_PATH);
        printf("Make sure the cache directory exists and contains the required files:\n");
        printf("  - main_file_cache.dat2\n");
        printf("  - main_file_cache.idx0 through main_file_cache.idx255\n");
        return 1;
    }
    printf("Cache loaded successfully\n");

    struct CacheArchive* archive = NULL;
    struct FileList* filelist = NULL;

    /**
     * Config/Underlay
     */

    // archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_UNDERLAY);
    // if( !archive )
    // {
    //     printf("Failed to load underlay archive\n");
    //     return 1;
    // }

    // filelist = filelist_new_from_cache_archive(archive);

    // int underlay_count = filelist->file_count;
    // int* underlay_ids = (int*)malloc(underlay_count * sizeof(int));
    // struct CacheConfigUnderlay* underlays = (struct
    // CacheConfigUnderlay*)malloc(underlay_count * sizeof(struct Underlay)); for( int i = 0; i
    // < underlay_count; i++ )
    // {
    //     struct CacheConfigUnderlay* underlay = &underlays[i];

    //     struct ArchiveReference* archives = cache->tables[CACHE_CONFIGS]->archives;

    //     config_floortype_underlay_decode_inplace(
    //         underlay, filelist->files[i], filelist->file_sizes[i]);

    //     int file_id =
    //         archives[cache->tables[CACHE_CONFIGS]->ids[CONFIG_UNDERLAY]].children.files[i].id;
    //     underlay_ids[i] = file_id;
    // }

    // filelist_free(filelist);
    // cache_archive_free(archive);

    // /**
    //  * Config/Overlay
    //  */

    // archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_OVERLAY);
    // if( !archive )
    // {
    //     printf("Failed to load overlay archive\n");
    //     return 1;
    // }

    // filelist = filelist_new_from_cache_archive(archive);

    // int overlay_count = filelist->file_count;
    // int* overlay_ids = (int*)malloc(overlay_count * sizeof(int));
    // struct CacheConfigOverlay* overlays = (struct CacheConfigOverlay*)malloc(overlay_count *
    // sizeof(struct CacheOverlay)); for( int i = 0; i < overlay_count; i++ )
    // {
    //     struct CacheConfigOverlay* overlay = &overlays[i];

    //     struct ArchiveReference* archives = cache->tables[CACHE_CONFIGS]->archives;

    //     config_floortype_overlay_decode_inplace(
    //         overlay, filelist->files[i], filelist->file_sizes[i]);
    //     int file_id =
    //         archives[cache->tables[CACHE_CONFIGS]->ids[CONFIG_OVERLAY]].children.files[i].id;
    //     overlay_ids[i] = file_id;
    // }

    // filelist_free(filelist);
    // cache_archive_free(archive);

    /**
     * Textures
     */

    // archive = cache_archive_new_load(cache, CACHE_TEXTURES, 0);
    // if( !archive )
    // {
    //     printf("Failed to load textures archive\n");
    //     return 1;
    // }

    // filelist = filelist_new_from_cache_archive(archive);

    // int texture_definitions_count = filelist->file_count;
    // struct CacheTexture* texture_definitions =
    //     (struct CacheTexture*)malloc(texture_definitions_count * sizeof(struct CacheTexture));
    // memset(texture_definitions, 0, texture_definitions_count * sizeof(struct CacheTexture));
    // int* texture_ids = (int*)malloc(texture_definitions_count * sizeof(int));
    // for( int i = 0; i < texture_definitions_count; i++ )
    // {
    //     struct CacheTexture* texture_definition = &texture_definitions[i];

    //     struct ArchiveReference* archives = cache->tables[CACHE_TEXTURES]->archives;

    //     texture_definition = texture_definition_decode_inplace(
    //         texture_definition, (const unsigned char*)filelist->files[i],
    //         filelist->file_sizes[i]);
    //     assert(texture_definition != NULL);
    //     int file_id = archives[cache->tables[CACHE_TEXTURES]->ids[0]].children.files[i].id;
    //     texture_ids[i] = file_id;

    //     if( file_id == 60 )
    //     {
    //         // 1648
    //         printf("heightmap\n");
    //     }
    //     if( file_id == 8 )
    //     {
    //         // sprite 455
    //         printf("grass\n");
    //     }
    //     if( file_id == 7 )
    //     {
    //         // sprite 1648
    //         printf("bank booth\n");
    //     }
    // }

    // filelist_free(filelist);
    // cache_archive_free(archive);

    // /**
    //  * Sprites
    //  */

    // Initialize game state
    struct Game game = { 0 };
    game.world = world_new();
    game.image_cross_pixels = (int**)malloc(8 * sizeof(int*));

    int sprite_count = cache->tables[CACHE_SPRITES]->archive_count;
    struct CacheSpritePack* sprite_packs =
        (struct CacheSpritePack*)malloc(sprite_count * sizeof(struct CacheSpritePack));
    int* sprite_ids = (int*)malloc(sprite_count * sizeof(int));

    for( int sprite_index = 0; sprite_index < sprite_count; sprite_index++ )
    {
        archive = cache_archive_new_load(cache, CACHE_SPRITES, sprite_index);
        if( !archive )
        {
            printf("Failed to load sprites archive\n");
            return 1;
        }

        struct ArchiveReference* archives = cache->tables[CACHE_SPRITES]->archives;

        if( sprite_index == 455 )
        {
            int iiii = 0;
        }
        if( sprite_index == 299 )
        {
            struct CacheSpritePack* sprite_pack = sprite_pack_new_decode(
                (const unsigned char*)archive->data, archive->data_size, SPRITELOAD_FLAG_NORMALIZE);
            if( !sprite_pack )
            {
                printf("Failed to load sprites pack\n");
                return 1;
            }

            sprite_packs[sprite_index] = *sprite_pack;

            for( int i = 0; i < 8; i++ )
            {
                int* pixels = sprite_get_pixels(&sprite_pack->sprites[i], sprite_pack->palette, 1);
                game.image_cross_pixels[i] = pixels;
            }
        }
        // DO NOT FREE
        // sprite_pack_free(sprite_pack);
        // for( int ind = 0; ind < sprite_pack->count; ind++ )
        // {
        //     int* pixels = sprite_get_pixels(&sprite_pack->sprites[ind], sprite_pack->palette, 1);

        //     if( !pixels )
        //         return 1;

        //     char filename[100];
        //     sprintf(filename, "sprite_%d_%d.bmp", sprite_index, ind);

        //     // Replace the existing BMP writing code with:
        //     write_bmp_file(
        //         filename,
        //         pixels,
        //         sprite_pack->sprites[ind].width,
        //         sprite_pack->sprites[ind].height);
        //     free(pixels);
        // }
        // return 0;

        int file_id = archives[cache->tables[CACHE_SPRITES]->ids[sprite_index]].index;

        sprite_ids[sprite_index] = file_id;

        cache_archive_free(archive);
    }

    /**
     * Decode textures from sprites
     */

    // struct CacheTexture* texture_definition = &texture_definitions[0];
    // int* pixels = texture_pixels_new_from_definition(
    //     texture_definition, 128, sprite_packs, sprite_ids, sprite_count, 1.0);
    // write_bmp_file("texture.bmp", pixels, 128, 128);
    // free(pixels);

    // struct CacheMapTerrain* map_terrain = map_terrain_new_from_cache(cache, 50, 50);
    // if( !map_terrain )
    // {
    //     printf("Failed to load map terrain\n");
    //     return 1;
    // }

    // struct CacheMapLocs* map_locs = map_locs_new_from_cache(cache, 50, 50);
    // if( !map_locs )
    // {
    //     printf("Failed to load map locs\n");
    //     return 1;
    // }

    // assert(map_locs->locs != NULL);

    /**
     * Config/Locs
     */

    // int* locs_to_decode = (int*)malloc(map_locs->locs_count * sizeof(int));
    // for( int i = 0; i < map_locs->locs_count; i++ )
    // {
    //     locs_to_decode[i] = map_locs->locs[i].id;
    // }

    // archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_LOCS);
    // if( !archive )
    // {
    //     printf("Failed to load locs archive\n");
    //     return 1;
    // }

    // filelist = filelist_new_from_cache_archive(archive);

    // // int locs_count = filelist->file_count;
    // int locs_count = map_locs->locs_count;
    // int* locs_ids = (int*)malloc(locs_count * sizeof(int));
    // struct CacheConfigLocation* locs = (struct CacheConfigLocation*)malloc(locs_count *
    // sizeof(struct CacheConfigLocation)); memset(locs, 0, locs_count * sizeof(struct
    // CacheConfigLocation)); for( int i = 0; i < locs_count; i++ )
    // {
    //     int loc_id = locs_to_decode[i];
    //     struct CacheConfigLocation* loc = &locs[i];

    //     struct ArchiveReference* cfg = cache->tables[CACHE_CONFIGS]->archives;

    //     decode_loc(loc, filelist->files[loc_id], filelist->file_sizes[loc_id]);

    //     if( loc->offset_x != 0 )
    //     {
    //         printf("Offset x: %d\n", loc->offset_x);
    //     }
    //     if( loc->offset_y != 0 )
    //     {
    //         printf("Offset y: %d\n", loc->offset_y);
    //     }

    //     int file_id =
    //     cfg[cache->tables[CACHE_CONFIGS]->ids[CONFIG_LOCS]].children.files[loc_id].id;
    //     locs_ids[i] = file_id;
    // }

    // filelist_free(filelist);
    // cache_archive_free(archive);

    /**
     * Prepare Scene
     */

    // struct SceneTile* tiles = scene_tiles_new_from_map_terrain(
    //     map_terrain, overlays, overlay_ids, overlay_count, underlays, underlay_ids,
    //     underlay_count);

    // struct SceneTextures* textures = scene_textures_new_from_tiles(
    //     tiles,
    //     MAP_TILE_COUNT,
    //     sprite_packs,
    //     sprite_ids,
    //     sprite_count,
    //     texture_definitions,
    //     texture_ids,
    //     texture_definitions_count);

    // struct SceneLocs* scene_locs = scene_locs_new_from_map_locs(map_terrain, map_locs,
    // cache);

    struct Scene* scene = scene_new_from_map(cache, 50, 50);

    // Initialize SDL
    struct PlatformSDL2 platform = { 0 };
    if( !platform_sdl2_init(&platform) )
    {
        printf("Failed to initialize SDL\n");
        return 1;
    }

    game.show_debug_tiles = 1;

    // game.camera_yaw = 0;
    // game.camera_pitch = 0;
    // game.camera_roll = 0;
    // game.camera_fov = 512;
    // game.camera_x = 0;
    // game.camera_y = -240;
    // game.camera_z = 0;
    game.camera_pitch = 370;
    game.camera_yaw = 1358;
    game.camera_roll = 0;
    game.camera_fov = 512;
    game.camera_x = 239;
    game.camera_y = -340;
    game.camera_z = 192;

    game.player_tile_x = 10;
    game.player_tile_y = 10;

    // game.camera_x = -2576;
    // game.camera_y = -3015;
    // game.camera_z = 2000;
    // game.camera_pitch = 405;
    // game.camera_yaw = 1536;
    // game.camera_roll = 0;
    game.camera_fov = 512; // Default FOV
    // game.tiles = tiles;
    game.tile_count = MAP_TILE_COUNT;
    // game.scene_textures = textures;

    // game.sprite_packs = sprite_packs;
    // game.sprite_ids = sprite_ids;
    // game.sprite_count = sprite_count;
    // game.textures = texture_definitions;
    // game.texture_ids = texture_ids;
    // game.texture_count = texture_definitions_count;
    game.scene_locs = NULL;

    game.scene = scene;

    game.textures_cache = textures_cache_new(cache);

    game.show_loc_x = 63;
    game.show_loc_y = 63;

    struct ModelCache* model_cache = model_cache_new();
    struct SceneModel* player_model = NULL;

    {
        struct SceneModel* scene_model = (struct SceneModel*)malloc(sizeof(struct SceneModel));
        memset(scene_model, 0, sizeof(struct SceneModel));

        struct CacheConfigIdkTable* config_idk_table = config_idk_table_new(cache);
        if( !config_idk_table )
        {
            printf("Failed to load config idk table\n");
            return 0;
        }

        struct CacheConfigSequenceTable* config_sequence_table = config_sequence_table_new(cache);
        assert(config_sequence_table);

        struct CacheConfigLocationTable* config_locs_table = config_locs_table_new(cache);
        if( !config_locs_table )
        {
            printf("Failed to load config locs table\n");
            return 0;
        }

        struct CacheConfigObjectTable* config_object_table = config_object_table_new(cache);
        if( !config_object_table )
        {
            printf("Failed to load config object table\n");
            return 0;
        }

        // 4,274  6,282 292 256 289 298 266
        int idk_ids[12] = { 0, 0, 0, 0, 274, 0, 282, 292, 256, 289, 298, 266 };
        int parts__models_ids[10] = { 0 };
        int parts__models_count = 0;

        for( int i = 0; i < 12; i++ )
        {
            int idk_id = idk_ids[i];
            if( idk_id >= 256 && idk_id < 512 )
            {
                idk_id -= 256;
            }
            else
                continue;
            struct CacheConfigIdk* idk = config_idk_table_get(config_idk_table, idk_id);
            if( idk )
            {
                assert(idk->model_ids_count == 1);
                parts__models_ids[parts__models_count++] = idk->model_ids[0];
            }
        }

        struct CacheModel* models[12] = { 0 };
        for( int i = 0; i < parts__models_count; i++ )
        {
            struct CacheModel* model =
                model_cache_checkout(model_cache, cache, parts__models_ids[i]);
            if( model )
            {
                models[i] = model;
            }
            else
            {
                assert(false);
            }
        }

        struct CacheModel* merged_model = model_new_merge(models, parts__models_count);

        scene_model->model = merged_model;

        struct CacheConfigSequence* sequence = NULL;

        sequence = config_sequence_table_get_new(config_sequence_table, 819);
        if( sequence )
        {
            scene_model->sequence = sequence;

            if( scene_model->model->vertex_bone_map )
                scene_model->vertex_bones = modelbones_new_decode(
                    scene_model->model->vertex_bone_map, scene_model->model->vertex_count);
            if( scene_model->model->face_bone_map )
                scene_model->face_bones = modelbones_new_decode(
                    scene_model->model->face_bone_map, scene_model->model->face_count);

            scene_model->original_vertices_x =
                (int*)malloc(sizeof(int) * scene_model->model->vertex_count);
            scene_model->original_vertices_y =
                (int*)malloc(sizeof(int) * scene_model->model->vertex_count);
            scene_model->original_vertices_z =
                (int*)malloc(sizeof(int) * scene_model->model->vertex_count);

            memcpy(
                scene_model->original_vertices_x,
                scene_model->model->vertices_x,
                sizeof(int) * scene_model->model->vertex_count);
            memcpy(
                scene_model->original_vertices_y,
                scene_model->model->vertices_y,
                sizeof(int) * scene_model->model->vertex_count);
            memcpy(
                scene_model->original_vertices_z,
                scene_model->model->vertices_z,
                sizeof(int) * scene_model->model->vertex_count);

            if( scene_model->model->face_alphas )
            {
                scene_model->original_face_alphas =
                    (int*)malloc(sizeof(int) * scene_model->model->face_count);
                memcpy(
                    scene_model->original_face_alphas,
                    scene_model->model->face_alphas,
                    sizeof(int) * scene_model->model->face_count);
            }

            assert(scene_model->frames == NULL);
            scene_model->frames =
                (struct CacheFrame**)malloc(sizeof(struct CacheFrame*) * sequence->frame_count);
            memset(scene_model->frames, 0, sizeof(struct CacheFrame*) * sequence->frame_count);

            int frame_id = sequence->frame_ids[0];
            int frame_archive_id = (frame_id >> 16) & 0xFFFF;
            // Get the frame definition ID from the second 2 bytes of the sequence frame ID The
            //     first 2 bytes are the sequence ID,
            //     the second 2 bytes are the frame archive ID

            struct CacheArchive* frame_archive =
                cache_archive_new_load(cache, CACHE_ANIMATIONS, frame_archive_id);
            struct FileList* frame_filelist = filelist_new_from_cache_archive(frame_archive);
            for( int i = 0; i < sequence->frame_count; i++ )
            {
                assert(((sequence->frame_ids[i] >> 16) & 0xFFFF) == frame_archive_id);
                // assert(i < frame_filelist->file_count);

                int frame_id = sequence->frame_ids[i];
                int frame_archive_id = (frame_id >> 16) & 0xFFFF;
                int frame_file_id = frame_id & 0xFFFF;

                assert(frame_file_id > 0);
                assert(frame_file_id - 1 < frame_filelist->file_count);

                char* frame_data = frame_filelist->files[frame_file_id - 1];
                int frame_data_size = frame_filelist->file_sizes[frame_file_id - 1];
                int framemap_id = framemap_id_from_frame_archive(frame_data, frame_data_size);

                if( !scene_model->framemap )
                {
                    scene_model->framemap = framemap_new_from_cache(cache, framemap_id);
                }

                struct CacheFrame* frame =
                    frame_new_decode2(frame_id, scene_model->framemap, frame_data, frame_data_size);

                scene_model->frames[scene_model->frame_count++] = frame;
            }

            cache_archive_free(frame_archive);
            frame_archive = NULL;
            filelist_free(frame_filelist);
            frame_filelist = NULL;

            struct CacheModel* cache_model = scene_model->model;

            struct ModelNormals* normals =
                (struct ModelNormals*)malloc(sizeof(struct ModelNormals));
            memset(normals, 0, sizeof(struct ModelNormals));

            normals->lighting_vertex_normals = (struct LightingNormal*)malloc(
                sizeof(struct LightingNormal) * cache_model->vertex_count);
            memset(
                normals->lighting_vertex_normals,
                0,
                sizeof(struct LightingNormal) * cache_model->vertex_count);
            normals->lighting_face_normals = (struct LightingNormal*)malloc(
                sizeof(struct LightingNormal) * cache_model->face_count);
            memset(
                normals->lighting_face_normals,
                0,
                sizeof(struct LightingNormal) * cache_model->face_count);

            normals->lighting_vertex_normals_count = cache_model->vertex_count;
            normals->lighting_face_normals_count = cache_model->face_count;

            calculate_vertex_normals(
                normals->lighting_vertex_normals,
                normals->lighting_face_normals,
                cache_model->vertex_count,
                cache_model->face_indices_a,
                cache_model->face_indices_b,
                cache_model->face_indices_c,
                cache_model->vertices_x,
                cache_model->vertices_y,
                cache_model->vertices_z,
                cache_model->face_count);

            scene_model->normals = normals;

            struct ModelLighting* lighting =
                (struct ModelLighting*)malloc(sizeof(struct ModelLighting));
            memset(lighting, 0, sizeof(struct ModelLighting));
            lighting->face_colors_hsl_a =
                (int*)malloc(sizeof(int) * scene_model->model->face_count);
            memset(lighting->face_colors_hsl_a, 0, sizeof(int) * scene_model->model->face_count);
            lighting->face_colors_hsl_b =
                (int*)malloc(sizeof(int) * scene_model->model->face_count);
            memset(lighting->face_colors_hsl_b, 0, sizeof(int) * scene_model->model->face_count);
            lighting->face_colors_hsl_c =
                (int*)malloc(sizeof(int) * scene_model->model->face_count);
            memset(lighting->face_colors_hsl_c, 0, sizeof(int) * scene_model->model->face_count);

            scene_model->lighting = lighting;

            int light_ambient = 64;
            int light_attenuation = 768;
            int lightsrc_x = -50;
            int lightsrc_y = -10;
            int lightsrc_z = -50;

            light_ambient += scene_model->light_ambient;
            // 2004Scape multiplies contrast by 5.
            // Later versions do not.
            light_attenuation += scene_model->light_contrast;

            int light_magnitude = (int)sqrt(
                lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
            int attenuation = (light_attenuation * light_magnitude) >> 8;

            apply_lighting(
                lighting->face_colors_hsl_a,
                lighting->face_colors_hsl_b,
                lighting->face_colors_hsl_c,
                scene_model->aliased_lighting_normals
                    ? scene_model->aliased_lighting_normals->lighting_vertex_normals
                    : scene_model->normals->lighting_vertex_normals,
                scene_model->normals->lighting_face_normals,
                scene_model->model->face_indices_a,
                scene_model->model->face_indices_b,
                scene_model->model->face_indices_c,
                scene_model->model->face_count,
                scene_model->model->face_colors,
                scene_model->model->face_alphas,
                scene_model->model->face_textures,
                scene_model->model->face_infos,
                light_ambient,
                attenuation,
                lightsrc_x,
                lightsrc_y,
                lightsrc_z);

            scene_model->lighting = lighting;

            player_model = scene_model;
        }

        world_player_entity_new_add(game.world, 0, 0, 0, player_model);
    }

    game.frustrum_cullmap = frustrum_cullmap_new(40, 131072); // 65536 = 90° FOV

    int w_pressed = 0;
    int a_pressed = 0;
    int s_pressed = 0;
    int d_pressed = 0;

    int up_pressed = 0;
    int down_pressed = 0;
    int left_pressed = 0;
    int right_pressed = 0;
    int space_pressed = 0;

    int f_pressed = 0;
    int r_pressed = 0;

    int m_pressed = 0;
    int n_pressed = 0;
    int i_pressed = 0;
    int k_pressed = 0;
    int l_pressed = 0;
    int j_pressed = 0;
    int q_pressed = 0;
    int e_pressed = 0;

    int comma_pressed = 0;
    int period_pressed = 0;

    bool quit = false;
    int speed = 50;
    SDL_Event event;

    // Frame timing variables
    Uint32 last_frame_time = SDL_GetTicks();
    const int target_fps = 50;
    const int target_frame_time = 1000 / target_fps;

#ifdef __EMSCRIPTEN__
    // Set global pointers for Emscripten
    g_game = &game;
    g_platform = &platform;
    g_quit = false;

    // Use Emscripten's main loop
    emscripten_set_main_loop(emscripten_main_loop, 0, 1);
#else
    // Traditional SDL main loop for native builds
    while( !quit )
    {
        Uint32 frame_start_time = SDL_GetTicks();

        while( SDL_PollEvent(&event) )
        {
            ImGui_ImplSDL2_ProcessEvent(&event);

            // Check if ImGui wants to capture keyboard/mouse input
            ImGuiIO& io = ImGui::GetIO();
            bool imgui_wants_keyboard = io.WantCaptureKeyboard;
            bool imgui_wants_mouse = io.WantCaptureMouse;

            if( event.type == SDL_QUIT )
            {
                quit = true;
            }
            else if( event.type == SDL_WINDOWEVENT )
            {
                if( event.window.event == SDL_WINDOWEVENT_RESIZED )
                {
                    SDL_GetWindowSize(
                        platform.window, &platform.window_width, &platform.window_height);
                    SDL_GetRendererOutputSize(
                        platform.renderer, &platform.drawable_width, &platform.drawable_height);
                }
            }
            else if( event.type == SDL_MOUSEMOTION && !imgui_wants_mouse )
            {
                transform_mouse_coordinates(
                    event.motion.x, event.motion.y, &game.mouse_x, &game.mouse_y, &platform);
            }
            else if( event.type == SDL_MOUSEBUTTONDOWN && !imgui_wants_mouse )
            {
                int transformed_x, transformed_y;
                transform_mouse_coordinates(
                    event.button.x, event.button.y, &transformed_x, &transformed_y, &platform);
                game.mouse_click_x = transformed_x;
                game.mouse_click_y = transformed_y;
                game.mouse_click_cycle = 0;
            }
            else if( event.type == SDL_KEYDOWN && !imgui_wants_keyboard )
            {
                switch( event.key.keysym.sym )
                {
                case SDLK_ESCAPE:
                    quit = true;
                    break;
                case SDLK_UP:
                    up_pressed = 1;
                    break;
                case SDLK_DOWN:
                    down_pressed = 1;
                    break;
                case SDLK_LEFT:
                    left_pressed = 1;
                    break;
                case SDLK_RIGHT:
                    right_pressed = 1;
                    break;
                case SDLK_s:
                    s_pressed = 1;
                    break;
                case SDLK_w:
                    w_pressed = 1;
                    break;
                case SDLK_d:
                    d_pressed = 1;
                    break;
                case SDLK_a:
                    a_pressed = 1;
                    break;
                case SDLK_r:
                    r_pressed = 1;
                    break;
                case SDLK_f:
                    f_pressed = 1;
                    break;
                case SDLK_SEMICOLON:
                    game.show_loc_enabled = !game.show_loc_enabled;
                    break;
                case SDLK_SPACE:
                    space_pressed = 1;
                    break;
                case SDLK_m:
                    m_pressed = 1;
                    break;
                case SDLK_n:
                    n_pressed = 1;
                    break;
                case SDLK_i:
                    i_pressed = 1;
                    break;
                case SDLK_k:
                    k_pressed = 1;
                    break;
                case SDLK_l:
                    l_pressed = 1;
                    break;
                case SDLK_j:
                    j_pressed = 1;
                    break;
                case SDLK_q:
                    q_pressed = 1;
                    break;
                case SDLK_e:
                    e_pressed = 1;
                    break;
                case SDLK_PERIOD:
                    period_pressed = 1;
                    break;
                case SDLK_COMMA:
                    comma_pressed = 1;
                    break;
                }
            }
            else if( event.type == SDL_KEYUP && !imgui_wants_keyboard )
            {
                switch( event.key.keysym.sym )
                {
                case SDLK_s:
                    s_pressed = 0;
                    break;
                case SDLK_w:
                    w_pressed = 0;
                    break;
                case SDLK_d:
                    d_pressed = 0;
                    break;
                case SDLK_a:
                    a_pressed = 0;
                    break;
                case SDLK_r:
                    r_pressed = 0;
                    break;
                case SDLK_f:
                    f_pressed = 0;
                    break;
                case SDLK_UP:
                    up_pressed = 0;
                    break;
                case SDLK_DOWN:
                    down_pressed = 0;
                    break;
                case SDLK_LEFT:
                    left_pressed = 0;
                    break;
                case SDLK_RIGHT:
                    right_pressed = 0;
                    break;
                case SDLK_SPACE:
                    space_pressed = 0;
                    break;
                case SDLK_m:
                    m_pressed = 0;
                    break;
                case SDLK_n:
                    n_pressed = 0;
                    break;
                case SDLK_i:
                    i_pressed = 0;
                    break;
                case SDLK_k:
                    k_pressed = 0;
                    break;
                case SDLK_l:
                    l_pressed = 0;
                    break;
                case SDLK_j:
                    j_pressed = 0;
                    break;
                case SDLK_PERIOD:
                    period_pressed = 0;
                    break;
                case SDLK_COMMA:
                    comma_pressed = 0;
                    break;
                case SDLK_q:
                    q_pressed = 0;
                    break;
                case SDLK_e:
                    e_pressed = 0;
                    break;
                }
            }
        }

        int camera_moved = 0;
        if( w_pressed )
        {
            game.camera_x -= (g_sin_table[game.camera_yaw] * speed) >> 16;
            game.camera_z += (g_cos_table[game.camera_yaw] * speed) >> 16;
            camera_moved = 1;
        }

        if( a_pressed )
        {
            game.camera_x -= (g_cos_table[game.camera_yaw] * speed) >> 16;
            game.camera_z -= (g_sin_table[game.camera_yaw] * speed) >> 16;
            camera_moved = 1;
        }

        if( s_pressed )
        {
            game.camera_x += (g_sin_table[game.camera_yaw] * speed) >> 16;
            game.camera_z -= (g_cos_table[game.camera_yaw] * speed) >> 16;
            camera_moved = 1;
        }

        if( d_pressed )
        {
            game.camera_x += (g_cos_table[game.camera_yaw] * speed) >> 16;
            game.camera_z += (g_sin_table[game.camera_yaw] * speed) >> 16;
            camera_moved = 1;
        }

        if( q_pressed )
        {
            game.fake_pitch = (game.fake_pitch + 10) % 2048;
        }

        if( e_pressed )
        {
            game.fake_pitch = (game.fake_pitch - 10 + 2048) % 2048;
        }

        if( up_pressed )
        {
            game.camera_pitch = (game.camera_pitch + 10) % 2048;
            camera_moved = 1;
        }

        if( left_pressed )
        {
            game.camera_yaw = (game.camera_yaw + 10) % 2048;
            camera_moved = 1;
        }

        if( right_pressed )
        {
            game.camera_yaw = (game.camera_yaw - 10 + 2048) % 2048;
            camera_moved = 1;
        }

        if( down_pressed )
        {
            game.camera_pitch = (game.camera_pitch - 10 + 2048) % 2048;
            camera_moved = 1;
        }

        if( f_pressed )
        {
            game.camera_y += speed;
            camera_moved = 1;
        }

        if( r_pressed )
        {
            game.camera_y -= speed;
            camera_moved = 1;
        }

        if( i_pressed )
        {
            game.player_tile_y += 1;
        }
        if( k_pressed )
        {
            game.player_tile_y -= 1;
        }
        if( l_pressed )
        {
            game.player_tile_x += 1;
        }
        if( j_pressed )
        {
            game.player_tile_x -= 1;
        }

        if( camera_moved )
        {
            memset(platform.pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
        }

        if( space_pressed )
        {
            if( game.ops )
                free(game.ops);
            game.ops = render_scene_compute_ops(
                game.camera_x, game.camera_y, game.camera_z, game.scene, &game.op_count);
            memset(platform.pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
            game.max_render_ops = 1;
            game.manual_render_ops = 0;
        }

        if( comma_pressed )
        {
            game.manual_render_ops = 1;
            game.max_render_ops -= 1;
            if( game.max_render_ops < 0 )
                game.max_render_ops = 0;
        }
        if( period_pressed )
        {
            game.manual_render_ops = 1;
            game.max_render_ops += 1;
            if( game.max_render_ops > game.op_count )
                game.max_render_ops = game.op_count;
        }

        game.mouse_click_cycle += 20;

        player_model->anim_frame_count += 1;
        if( player_model->anim_frame_count >=
            player_model->sequence->frame_lengths[player_model->anim_frame_step] )
        {
            player_model->anim_frame_count = 0;
            player_model->anim_frame_step += 1;
            if( player_model->anim_frame_step >= player_model->sequence->frame_count )
            {
                player_model->anim_frame_step = 0;
            }
        }

        world_begin_scene_frame(game.world, game.scene);

        SDL_RenderClear(platform.renderer);
        // Render frame
        game_render_sdl2(&game, &platform);
        game_render_imgui(&game, &platform);

        SDL_RenderPresent(platform.renderer);

        world_end_scene_frame(game.world, game.scene);

        // Calculate frame time and sleep appropriately
        Uint32 frame_end_time = SDL_GetTicks();
        Uint32 frame_time = frame_end_time - frame_start_time;

        if( frame_time < target_frame_time )
        {
            SDL_Delay(target_frame_time - frame_time);
        }

        last_frame_time = frame_end_time;
    }
#endif

    // Cleanup
    SDL_DestroyTexture(platform.texture);
    SDL_DestroyRenderer(platform.renderer);
    SDL_DestroyWindow(platform.window);
    free(platform.pixel_buffer);
    SDL_Quit();

    game_free(&game);

    // Free game resources
    // free_tiles(tiles, game.tile_count);
    // free(overlay_ids);
    // free(underlays);
    // free(overlays);
    cache_free(cache);

    return 0;
}