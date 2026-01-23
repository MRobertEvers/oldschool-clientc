#include "platform_impl_osx_sdl2_renderer_opengl3.h"

extern "C" {
#include "graphics/render.h"
#include "libgame.u.h"
#include "osrs/anim.h"
}
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

// Use regular OpenGL headers on macOS/iOS for development
#include <OpenGL/gl.h>
#include <OpenGL/gl3.h>

#include <SDL.h>
#include <stdio.h>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_screen_vertices_x[20];
static int g_screen_vertices_y[20];
static int g_screen_vertices_z[20];
static int g_ortho_vertices_x[20];
static int g_ortho_vertices_y[20];
static int g_ortho_vertices_z[20];

// Include C implementation of convex hull
#include "graphics/convex_hull.u.c"
#include "graphics/triangle_contains.u.c"

// Draw convex hull overlay using ImGui draw list (works with modern OpenGL)
static void
draw_convex_hull_overlay(struct RendererOSX_SDL2OpenGL3* renderer)
{
    if( !renderer->convex_hull_vertices || renderer->convex_hull_vertex_count < 3 )
    {
        return;
    }

    // Get ImGui's background draw list (renders behind ImGui windows)
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    // Convert vertices to ImVec2 format
    ImVector<ImVec2> points;
    for( int i = 0; i < renderer->convex_hull_vertex_count; i++ )
    {
        points.push_back(ImVec2(
            renderer->convex_hull_vertices[i * 2 + 0], renderer->convex_hull_vertices[i * 2 + 1]));
    }

    // Draw filled convex polygon with transparency (yellow, 30% opacity)
    draw_list->AddConvexPolyFilled(
        points.Data, points.Size, IM_COL32(255, 255, 0, 76)); // 76 = 30% of 255

    // Draw outline (yellow, 80% opacity)
    draw_list->AddPolyline(
        points.Data,
        points.Size,
        IM_COL32(255, 255, 0, 204), // 204 = 80% of 255
        ImDrawFlags_Closed,
        2.0f);

    printf("DEBUG: Convex hull drawn using ImGui\n");
}

static void
render_imgui(
    struct RendererOSX_SDL2OpenGL3* renderer,
    struct Game* game)
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

    // Performance metrics
    ImGui::Separator();
    ImGui::Text("Performance Metrics");
    ImGui::Text("Face Order Compute: %.3f ms", renderer->face_order_time_ms);
    ImGui::Text("Render Time: %.3f ms", renderer->render_time_ms);
    ImGui::Text(
        "Total Frame Time: %.3f ms", renderer->face_order_time_ms + renderer->render_time_ms);

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

    ImGui::Text("Camera tile: %d, %d", game->camera_world_x / 128, game->camera_world_y / 128);

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

    // Draw convex hull overlay BEFORE ImGui::Render()
    draw_convex_hull_overlay(renderer);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static void
load_static_scene(
    struct RendererOSX_SDL2OpenGL3* renderer,
    struct Game* game)
{
    // This function builds a static scene buffer containing all scene geometry
    // It's called once when the scene is loaded

    printf("Building static scene buffer...\n");

    // Start building the static scene
    pix3dgl_scene_static_load_begin(renderer->pix3dgl);

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
                        game->scene->textures_cache,
                        game->cache);
                }
            }
        }

        // Add tile geometry directly to static scene buffer with tile index
        pix3dgl_scene_static_load_tile(
            renderer->pix3dgl,
            i, // Pass tile index for tracking
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
                        game->scene->textures_cache,
                        game->cache);
                }
            }
        }

        // Calculate world position
        float position_x = scene_model->region_x + scene_model->offset_x;
        float position_y = scene_model->region_height + scene_model->offset_height;
        float position_z = scene_model->region_z + scene_model->offset_z;
        float yaw_radians = (scene_model->yaw * 2.0f * M_PI) / 2048.0f;

        // Check if model has animation sequence
        if( scene_model->sequence && scene_model->frames && scene_model->frame_count > 0 )
        {
            // Model has animation - create keyframes

            // Allocate temporary buffers for animated vertices and colors
            int vertex_count = scene_model->model->vertex_count;
            int face_count = scene_model->model->face_count;

            int* anim_vertices_x = (int*)malloc(sizeof(int) * vertex_count);
            int* anim_vertices_y = (int*)malloc(sizeof(int) * vertex_count);
            int* anim_vertices_z = (int*)malloc(sizeof(int) * vertex_count);
            int* anim_face_alphas =
                scene_model->model->face_alphas ? (int*)malloc(sizeof(int) * face_count) : NULL;

            // Begin loading animated model with all keyframes
            pix3dgl_scene_static_load_animated_model_begin(
                renderer->pix3dgl, scene_model->scene_model_idx, scene_model->frame_count);

            // Generate and upload each keyframe
            for( int frame_idx = 0; frame_idx < scene_model->frame_count; frame_idx++ )
            {
                // Reset to original vertices
                memcpy(
                    anim_vertices_x, scene_model->original_vertices_x, sizeof(int) * vertex_count);
                memcpy(
                    anim_vertices_y, scene_model->original_vertices_y, sizeof(int) * vertex_count);
                memcpy(
                    anim_vertices_z, scene_model->original_vertices_z, sizeof(int) * vertex_count);

                if( anim_face_alphas && scene_model->original_face_alphas )
                {
                    memcpy(
                        anim_face_alphas,
                        scene_model->original_face_alphas,
                        sizeof(int) * face_count);
                }

                // Apply animation frame transform
                anim_frame_apply(
                    scene_model->frames[frame_idx],
                    scene_model->framemap,
                    anim_vertices_x,
                    anim_vertices_y,
                    anim_vertices_z,
                    anim_face_alphas,
                    scene_model->vertex_bones ? scene_model->vertex_bones->bones_count : 0,
                    scene_model->vertex_bones ? scene_model->vertex_bones->bones : NULL,
                    scene_model->vertex_bones ? scene_model->vertex_bones->bones_sizes : NULL,
                    scene_model->face_bones ? scene_model->face_bones->bones_count : 0,
                    scene_model->face_bones ? scene_model->face_bones->bones : NULL,
                    scene_model->face_bones ? scene_model->face_bones->bones_sizes : NULL);

                // Upload this keyframe
                pix3dgl_scene_static_load_animated_model_keyframe(
                    renderer->pix3dgl,
                    scene_model->scene_model_idx,
                    frame_idx,
                    anim_vertices_x,
                    anim_vertices_y,
                    anim_vertices_z,
                    scene_model->model->face_indices_a,
                    scene_model->model->face_indices_b,
                    scene_model->model->face_indices_c,
                    face_count,
                    scene_model->model->face_textures,
                    scene_model->model->face_texture_coords,
                    scene_model->model->textured_p_coordinate,
                    scene_model->model->textured_m_coordinate,
                    scene_model->model->textured_n_coordinate,
                    scene_model->lighting->face_colors_hsl_a,
                    scene_model->lighting->face_colors_hsl_b,
                    scene_model->lighting->face_colors_hsl_c,
                    scene_model->model->face_infos,
                    anim_face_alphas,
                    position_x,
                    position_y,
                    position_z,
                    yaw_radians);
            }

            memcpy(anim_vertices_x, scene_model->original_vertices_x, sizeof(int) * vertex_count);
            memcpy(anim_vertices_y, scene_model->original_vertices_y, sizeof(int) * vertex_count);
            memcpy(anim_vertices_z, scene_model->original_vertices_z, sizeof(int) * vertex_count);

            if( anim_face_alphas && scene_model->original_face_alphas )
            {
                memcpy(
                    anim_face_alphas, scene_model->original_face_alphas, sizeof(int) * face_count);
            }

            // Finalize animated model with frame timing data
            pix3dgl_scene_static_load_animated_model_end(
                renderer->pix3dgl,
                scene_model->scene_model_idx,
                scene_model->sequence->frame_lengths,
                scene_model->frame_count);

            // Free temporary buffers
            free(anim_vertices_x);
            free(anim_vertices_y);
            free(anim_vertices_z);
            if( anim_face_alphas )
                free(anim_face_alphas);
        }
        else
        {
            // Static model - load normally
            pix3dgl_scene_static_load_model(
                renderer->pix3dgl,
                scene_model->scene_model_idx,
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
                scene_model->model->face_alphas,
                position_x,
                position_y,
                position_z,
                yaw_radians);
        }
    }

    // Finalize the static scene - uploads to GPU
    pix3dgl_scene_static_load_end(renderer->pix3dgl);

    printf("Static scene buffer built successfully\n");
}

static void
render_scene(
    struct RendererOSX_SDL2OpenGL3* renderer,
    struct Game* game)
{
    // OPTIMIZATION: Track camera state to avoid unnecessary sorting every frame
    static int last_cam_x = -99999;
    static int last_cam_y = -99999;
    static int last_cam_z = -99999;
    static float last_cam_yaw = -99999.0f;
    static float last_cam_pitch = -99999.0f;
    static int frame_count = 0;

    // Performance tracking
    Uint64 perf_frequency = SDL_GetPerformanceFrequency();

    frame_count++;

    // Update animation clock for texture animations at 50fps (20ms per frame)
    // Quantize time to 50fps intervals for game-accurate animation
    Uint32 current_ticks = SDL_GetTicks();
    Uint32 frame_ticks = (current_ticks / 20) * 20; // Round down to nearest 20ms interval
    float animation_clock = frame_ticks / 1000.0f;
    pix3dgl_set_animation_clock(renderer->pix3dgl, animation_clock);

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

    // Check if camera moved significantly (thresholds: 128 units = 1 tile, 12 degrees)
    int dx = game->camera_world_x - last_cam_x;
    int dy = game->camera_world_y - last_cam_y;
    int dz = game->camera_world_z - last_cam_z;
    float angle_diff =
        fabsf(game->camera_yaw - last_cam_yaw) + fabsf(game->camera_pitch - last_cam_pitch);

    bool camera_moved =
        (dx > 12 || dy > 12 || dz > 12 ||
         angle_diff > 0.2094f); // 128*128 = 16384, 12° = 0.2094 rad
    camera_moved = true;

    // OPTIMIZATION: Only recompute face order when camera moves significantly
    if( camera_moved || frame_count == 1 )
    {
        Uint64 face_order_start = SDL_GetPerformanceCounter();

        // Compute scene ops to determine proper draw order (painter's algorithm)
        renderer->op_count = render_scene_compute_ops(
            renderer->ops,
            renderer->op_capacity,
            game->camera_world_x,
            game->camera_world_y,
            game->camera_world_z,
            game->scene,
            NULL);

        // Structure to track unified draw order (models and tiles interleaved)
        struct DrawItem
        {
            bool is_tile;
            int index; // scene_model_idx or scene_tile_idx
        };

        static std::vector<DrawItem> unified_draw_order;
        unified_draw_order.clear();

        // START BATCH MODE - defers index buffer rebuild until end
        pix3dgl_scene_static_begin_face_order_batch(renderer->pix3dgl);

        struct IterRenderSceneOps iter_render_scene_ops;
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
        // Reset last hovered model
        renderer->last_hovered_model = NULL;
        while( iter_render_scene_ops_next(&iter_render_scene_ops) )
        {
            if( iter_render_scene_ops.value.model_nullable_ )
            {
                struct SceneModel* scene_model = iter_render_scene_ops.value.model_nullable_;

                // Safety check: ensure model has valid data
                if( !scene_model->model || scene_model->model->face_count == 0 )
                {
                    continue;
                }

                // Add model to unified draw order
                unified_draw_order.push_back({ false, scene_model->scene_model_idx });

                // For animated models, query the GPU-side animation frame and apply it
                // This ensures CPU and GPU are always using the exact same animation state
                int anim_frame = pix3dgl_scene_static_get_model_animation_frame(
                    renderer->pix3dgl, scene_model->scene_model_idx);

                if( anim_frame >= 0 )
                {
                    // This is an animated model - apply the current GPU-side animation frame
                    // Copy original vertices to model
                    memcpy(
                        scene_model->model->vertices_x,
                        scene_model->original_vertices_x,
                        sizeof(int) * scene_model->model->vertex_count);
                    memcpy(
                        scene_model->model->vertices_y,
                        scene_model->original_vertices_y,
                        sizeof(int) * scene_model->model->vertex_count);
                    memcpy(
                        scene_model->model->vertices_z,
                        scene_model->original_vertices_z,
                        sizeof(int) * scene_model->model->vertex_count);
                    if( scene_model->model->face_alphas && scene_model->original_face_alphas )
                    {
                        memcpy(
                            scene_model->model->face_alphas,
                            scene_model->original_face_alphas,
                            sizeof(int) * scene_model->model->face_count);
                    }

                    // Apply the GPU's current animation frame
                    anim_frame_apply(
                        scene_model->frames[anim_frame],
                        scene_model->framemap,
                        scene_model->model->vertices_x,
                        scene_model->model->vertices_y,
                        scene_model->model->vertices_z,
                        scene_model->model->face_alphas,
                        scene_model->vertex_bones ? scene_model->vertex_bones->bones_count : 0,
                        scene_model->vertex_bones ? scene_model->vertex_bones->bones : NULL,
                        scene_model->vertex_bones ? scene_model->vertex_bones->bones_sizes : NULL,
                        scene_model->face_bones ? scene_model->face_bones->bones_count : 0,
                        scene_model->face_bones ? scene_model->face_bones->bones : NULL,
                        scene_model->face_bones ? scene_model->face_bones->bones_sizes : NULL);
                }

                // Compute face order for this model using iter_render_model_init
                struct IterRenderModel iter_model;
                iter_render_model_init(
                    &iter_model,
                    scene_model,
                    0,
                    game->camera_world_x,
                    game->camera_world_y,
                    game->camera_world_z,
                    game->camera_pitch,
                    game->camera_yaw,
                    0,   // camera_roll
                    512, // fov
                    renderer->width,
                    renderer->height,
                    50); // near_plane_z

                // Collect face indices in render order
                static std::vector<int> face_order;
                face_order.clear();

                bool model_intersected = false;
                bool clipped = false;
                while( iter_render_model_next(&iter_model) )
                {
                    face_order.push_back(iter_model.value_face);

                    int face = iter_model.value_face;
                    bool is_in_bb = false;
                    if( !clipped && renderer->mouse_x > 0 &&
                        renderer->mouse_x >= iter_model.aabb_min_screen_x &&
                        renderer->mouse_x <= iter_model.aabb_max_screen_x &&
                        renderer->mouse_y >= iter_model.aabb_min_screen_y &&
                        renderer->mouse_y <= iter_model.aabb_max_screen_y )
                    {
                        is_in_bb = true;
                    }

                    if( !clipped && is_in_bb )
                    {
                        // Get face vertex indices
                        int face_a = scene_model->model->face_indices_a[face];
                        int face_b = scene_model->model->face_indices_b[face];
                        int face_c = scene_model->model->face_indices_c[face];

                        // Get screen coordinates of the triangle vertices
                        int x1 = iter_model.screen_vertices_x[face_a];
                        int x2 = iter_model.screen_vertices_x[face_b];
                        int x3 = iter_model.screen_vertices_x[face_c];
                        int y1 = iter_model.screen_vertices_y[face_a];
                        int y2 = iter_model.screen_vertices_y[face_b];
                        int y3 = iter_model.screen_vertices_y[face_c];

                        // Check if mouse is inside the triangle using barycentric coordinates
                        if( x1 != -5000 && x2 != -5000 && x3 != -5000 )
                        { // Skip clipped triangles
                            x1 += renderer->width / 2;
                            x2 += renderer->width / 2;
                            x3 += renderer->width / 2;

                            y1 += renderer->height / 2;
                            y2 += renderer->height / 2;
                            y3 += renderer->height / 2;

                            bool mouse_in_triangle = triangle_contains_point(
                                x1, y1, x2, y2, x3, y3, renderer->mouse_x, renderer->mouse_y);
                            if( mouse_in_triangle )
                                model_intersected = true;
                        }
                        else
                        {
                            clipped = true;
                        }
                    }
                }

                if( !clipped && model_intersected )
                {
                    renderer->last_hovered_model = scene_model;
                }

                // Set the face order for this model (batched - doesn't trigger rebuild yet!)
                if( !face_order.empty() )
                {
                    pix3dgl_scene_static_set_model_face_order(
                        renderer->pix3dgl,
                        scene_model->scene_model_idx,
                        face_order.data(),
                        face_order.size());
                }
            }
            else if( iter_render_scene_ops.value.tile_nullable_ )
            {
                struct SceneTile* scene_tile = iter_render_scene_ops.value.tile_nullable_;

                // Determine tile index from scene_tiles array
                int tile_idx = scene_tile - game->scene->scene_tiles;

                // Safety check: ensure tile index is valid
                if( tile_idx >= 0 && tile_idx < game->scene->scene_tiles_length )
                {
                    // Add tile to unified draw order
                    unified_draw_order.push_back({ true, tile_idx });

                    // For now, tiles use their default face order
                    // TODO: Implement per-face depth sorting for tiles if needed
                }
            }
        }

        // END BATCH MODE - triggers single index buffer rebuild for ALL models
        pix3dgl_scene_static_end_face_order_batch(renderer->pix3dgl);

        // Set the unified draw order (models and tiles interleaved)
        if( !unified_draw_order.empty() )
        {
            pix3dgl_scene_static_set_unified_draw_order(
                renderer->pix3dgl,
                reinterpret_cast<bool*>(&unified_draw_order[0].is_tile),
                reinterpret_cast<int*>(&unified_draw_order[0].index),
                unified_draw_order.size(),
                sizeof(DrawItem));
        }

        // ============= HOVER DETECTION AND CONVEX HULL COMPUTATION =============

        // If we have a hovered model, compute its convex hull
        if( renderer->last_hovered_model )
        {
            printf(
                "Computing convex hull for hovered model ID: %d\n",
                renderer->last_hovered_model->model->_id);

            // Collect vertices only from visible faces
            std::vector<float> vertex_x;
            std::vector<float> vertex_y;
            std::vector<bool> vertex_used(renderer->last_hovered_model->model->vertex_count, false);

            // Initialize render model iterator for the hovered model
            struct IterRenderModel iter_hull;
            iter_render_model_init(
                &iter_hull,
                renderer->last_hovered_model,
                0,
                game->camera_world_x,
                game->camera_world_y,
                game->camera_world_z,
                game->camera_pitch,
                game->camera_yaw,
                0,   // camera_roll
                512, // fov
                renderer->width,
                renderer->height,
                50); // near_plane_z

            // Iterate through all visible faces and collect their vertices
            while( iter_render_model_next(&iter_hull) )
            {
                int face = iter_hull.value_face;

                // Check if face is invisible based on face_infos and colors
                // Type 2 faces are hidden (face_infos & 0x3 == 2)
                int* face_infos = renderer->last_hovered_model->model->face_infos;
                if( face_infos )
                {
                    int face_type = face_infos[face] & 0x3;
                    if( face_type == 2 )
                        continue; // Hidden face
                }

                // Faces with color_c == -2 are invisible
                int* colors_c = renderer->last_hovered_model->lighting->face_colors_hsl_c;
                if( colors_c && colors_c[face] == -2 )
                    continue; // Hidden face

                // Get face vertex indices
                int face_a = renderer->last_hovered_model->model->face_indices_a[face];
                int face_b = renderer->last_hovered_model->model->face_indices_b[face];
                int face_c = renderer->last_hovered_model->model->face_indices_c[face];

                // Get screen coordinates
                int x1 = iter_hull.screen_vertices_x[face_a] + renderer->width / 2;
                int y1 = iter_hull.screen_vertices_y[face_a] + renderer->height / 2;
                int x2 = iter_hull.screen_vertices_x[face_b] + renderer->width / 2;
                int y2 = iter_hull.screen_vertices_y[face_b] + renderer->height / 2;
                int x3 = iter_hull.screen_vertices_x[face_c] + renderer->width / 2;
                int y3 = iter_hull.screen_vertices_y[face_c] + renderer->height / 2;

                // Skip clipped faces
                if( x1 == -5000 || x2 == -5000 || x3 == -5000 )
                    continue;

                // Mark vertices as used and add to list if not already added
                if( !vertex_used[face_a] )
                {
                    vertex_used[face_a] = true;
                    vertex_x.push_back((float)x1);
                    vertex_y.push_back((float)y1);
                }
                if( !vertex_used[face_b] )
                {
                    vertex_used[face_b] = true;
                    vertex_x.push_back((float)x2);
                    vertex_y.push_back((float)y2);
                }
                if( !vertex_used[face_c] )
                {
                    vertex_used[face_c] = true;
                    vertex_x.push_back((float)x3);
                    vertex_y.push_back((float)y3);
                }
            }

            // Compute convex hull from visible vertices only
            size_t num_vertices = vertex_x.size();
            if( num_vertices > 0 )
            {
                // Allocate output arrays for convex hull
                float* hull_x = (float*)malloc(sizeof(float) * num_vertices);
                float* hull_y = (float*)malloc(sizeof(float) * num_vertices);

                // Call C version of compute_convex_hull
                size_t hull_size = compute_convex_hull(
                    vertex_x.data(), vertex_y.data(), num_vertices, hull_x, hull_y);

                // Store convex hull for rendering
                if( hull_size > 0 )
                {
                    printf("Convex hull computed with %zu vertices:\n", hull_size);
                    for( size_t i = 0; i < hull_size; i++ )
                    {
                        printf("  Vertex %zu: (%.2f, %.2f)\n", i, hull_x[i], hull_y[i]);
                    }

                    // Allocate and store vertices for rendering
                    if( renderer->convex_hull_vertices )
                    {
                        free(renderer->convex_hull_vertices);
                    }
                    renderer->convex_hull_vertex_count = hull_size;
                    renderer->convex_hull_vertices = (float*)malloc(sizeof(float) * 2 * hull_size);
                    for( size_t i = 0; i < hull_size; i++ )
                    {
                        renderer->convex_hull_vertices[i * 2 + 0] = hull_x[i];
                        renderer->convex_hull_vertices[i * 2 + 1] = hull_y[i];
                    }
                }
                else
                {
                    printf("Convex hull is empty\n");
                    // Clear convex hull
                    if( renderer->convex_hull_vertices )
                    {
                        free(renderer->convex_hull_vertices);
                        renderer->convex_hull_vertices = NULL;
                    }
                    renderer->convex_hull_vertex_count = 0;
                }

                free(hull_x);
                free(hull_y);
            }
            else
            {
                printf("No vertices to compute convex hull\n");
                // Clear convex hull
                if( renderer->convex_hull_vertices )
                {
                    free(renderer->convex_hull_vertices);
                    renderer->convex_hull_vertices = NULL;
                }
                renderer->convex_hull_vertex_count = 0;
            }
        }
        else
        {
            // No hovered model - clear convex hull
            if( renderer->convex_hull_vertices )
            {
                free(renderer->convex_hull_vertices);
                renderer->convex_hull_vertices = NULL;
            }
            renderer->convex_hull_vertex_count = 0;
        }

        // Update last known camera position
        last_cam_x = game->camera_world_x;
        last_cam_y = game->camera_world_y;
        last_cam_z = game->camera_world_z;
        last_cam_yaw = game->camera_yaw;
        last_cam_pitch = game->camera_pitch;

        Uint64 face_order_end = SDL_GetPerformanceCounter();
        renderer->face_order_time_ms =
            (double)(face_order_end - face_order_start) * 1000.0 / (double)perf_frequency;
    }
    else
    {
        // Camera didn't move - reuse last frame's face ordering (FAST!)
        renderer->face_order_time_ms = 0.0; // No face order computation this frame
    }

    // Draw the static scene buffer (reuses cached index buffer if camera didn't move!)
    Uint64 render_start = SDL_GetPerformanceCounter();
    pix3dgl_scene_static_draw(renderer->pix3dgl);
    Uint64 render_end = SDL_GetPerformanceCounter();
    renderer->render_time_ms =
        (double)(render_end - render_start) * 1000.0 / (double)perf_frequency;

    pix3dgl_end_frame(renderer->pix3dgl);
}

struct RendererOSX_SDL2OpenGL3*
PlatformImpl_OSX_SDL2_Renderer_OpenGL3_New(
    int width,
    int height)
{
    struct RendererOSX_SDL2OpenGL3* renderer =
        (struct RendererOSX_SDL2OpenGL3*)malloc(sizeof(struct RendererOSX_SDL2OpenGL3));
    memset(renderer, 0, sizeof(struct RendererOSX_SDL2OpenGL3));

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

    // Initialize mouse tracking
    renderer->mouse_x = 0;
    renderer->mouse_y = 0;
    renderer->last_hovered_model = NULL;
    renderer->convex_hull_vertices = NULL;
    renderer->convex_hull_vertex_count = 0;

    return renderer;
}

void
PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Free(struct RendererOSX_SDL2OpenGL3* renderer)
{
    if( renderer->convex_hull_vertices )
    {
        free(renderer->convex_hull_vertices);
    }
    free(renderer);
}

bool
PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Init(
    struct RendererOSX_SDL2OpenGL3* renderer,
    struct Platform* platform)
{
    renderer->platform = platform;
    // Create OpenGL context
    renderer->gl_context = SDL_GL_CreateContext(platform->window);
    if( !renderer->gl_context )
    {
        printf("OpenGL context creation failed: %s\n", SDL_GetError());
        return false;
    }

    // Disable vsync for maximum performance
    SDL_GL_SetSwapInterval(0);
    printf("OpenGL context created successfully (VSync disabled for max FPS)\n");

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
PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Shutdown(struct RendererOSX_SDL2OpenGL3* renderer)
{
    SDL_GL_DeleteContext(renderer->gl_context);
}

void
PlatformImpl_OSX_SDL2_Renderer_OpenGL3_Render(
    struct RendererOSX_SDL2OpenGL3* renderer,
    struct Game* game,
    struct GameGfxOpList* gfx_op_list)
{
    // Handle window resize: update renderer dimensions
    int window_width, window_height;
    SDL_GetWindowSize(renderer->platform->window, &window_width, &window_height);

    // Only update if size changed
    if( window_width != renderer->width || window_height != renderer->height )
    {
        renderer->width = window_width;
        renderer->height = window_height;
    }

    // Update mouse position from SDL
    SDL_GetMouseState(&renderer->mouse_x, &renderer->mouse_y);

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
        }
        break;
        case GAME_GFX_OP_SCENE_TILE_UNLOAD:
        {
        }
        break;
        case GAME_GFX_OP_SCENE_MODEL_LOAD:
            break;
        case GAME_GFX_OP_SCENE_MODEL_UNLOAD:
            // Noop
            break;
        case GAME_GFX_OP_SCENE_MODEL_DRAW:
        {
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