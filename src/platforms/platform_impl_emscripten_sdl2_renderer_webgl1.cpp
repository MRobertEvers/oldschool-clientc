#include "platform_impl_emscripten_sdl2_renderer_webgl1.h"

extern "C" {
#include "graphics/render.h"
#include "libgame.u.h"
#include "osrs/anim.h"
}
#include "imgui.h"
#include "imgui_impl_opengl3.h" // ImGui supports ES2/WebGL1 via OpenGL3 backend
#include "imgui_impl_sdl2.h"

// Emscripten with WebGL1 (OpenGL ES 2.0)
#include <GLES2/gl2.h>
#include <emscripten/html5.h>

#include <SDL.h>
#include <emscripten.h>
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

static void
render_imgui(struct RendererEmscripten_SDL2WebGL1* renderer, struct Game* game)
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
    ImGui::SliderInt("Move Speed", &game->camera_movement_speed, 1, 100, "%d");
    ImGui::SliderInt("Look Speed", &game->camera_rotation_speed, 1, 100, "%d");
    ImGui::End();

    // Render ImGui windows first
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Now draw virtual joysticks directly over the 3D scene using a new ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Static state to track which joystick (if any) has captured input
    static int captured_joystick = -1; // -1 = none, 0 = move, 1 = look
    static bool was_mouse_down = false;

    // Get screen dimensions
    ImVec2 screen_size = ImGui::GetIO().DisplaySize;
    float joystick_radius = 80.0f;
    float margin = 30.0f;

    // Check mouse state
    bool is_mouse_down = ImGui::IsMouseDown(0);
    ImVec2 mouse_pos = ImGui::GetIO().MousePos;

    // Reset capture when mouse is released
    if( !is_mouse_down && was_mouse_down )
    {
        captured_joystick = -1;
    }
    was_mouse_down = is_mouse_down;

    // Virtual Joystick helper function with capture support
    auto draw_virtual_joystick = [&](ImVec2 center,
                                     float outer_radius,
                                     ImVec2& out_delta,
                                     const char* label,
                                     bool& is_touching,
                                     int joystick_id) -> void {
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();

        // Draw outer circle (joystick boundary)
        draw_list->AddCircleFilled(center, outer_radius, IM_COL32(40, 40, 40, 180));
        draw_list->AddCircle(center, outer_radius, IM_COL32(150, 150, 150, 220), 32, 3.0f);

        // Check if mouse/touch is in the joystick area
        float dx = mouse_pos.x - center.x;
        float dy = mouse_pos.y - center.y;
        float dist = sqrtf(dx * dx + dy * dy);

        is_touching = false;
        ImVec2 stick_offset(0, 0);

        // Capture logic: start capture if mouse pressed inside joystick and nothing else captured
        if( is_mouse_down && dist < outer_radius && captured_joystick == -1 )
        {
            captured_joystick = joystick_id;
        }

        // Process input if this joystick is captured or mouse is inside and nothing is captured
        if( is_mouse_down && (captured_joystick == joystick_id) )
        {
            is_touching = true;
            // Clamp to radius
            if( dist > outer_radius * 0.85f )
            {
                float scale = (outer_radius * 0.85f) / dist;
                stick_offset.x = dx * scale;
                stick_offset.y = dy * scale;
            }
            else
            {
                stick_offset.x = dx;
                stick_offset.y = dy;
            }
        }

        // Draw inner circle (stick position)
        ImVec2 stick_pos = ImVec2(center.x + stick_offset.x, center.y + stick_offset.y);
        float inner_radius = outer_radius * 0.35f;
        draw_list->AddCircleFilled(
            stick_pos,
            inner_radius,
            is_touching ? IM_COL32(80, 160, 255, 255) : IM_COL32(100, 100, 100, 200));
        draw_list->AddCircle(stick_pos, inner_radius, IM_COL32(200, 200, 200, 255), 32, 2.5f);

        // Draw center cross
        float cross_size = 8.0f;
        draw_list->AddLine(
            ImVec2(center.x - cross_size, center.y),
            ImVec2(center.x + cross_size, center.y),
            IM_COL32(120, 120, 120, 180),
            1.5f);
        draw_list->AddLine(
            ImVec2(center.x, center.y - cross_size),
            ImVec2(center.x, center.y + cross_size),
            IM_COL32(120, 120, 120, 180),
            1.5f);

        // Normalize output to -1 to 1 range
        if( is_touching && dist > 1.0f )
        {
            out_delta.x = stick_offset.x / (outer_radius * 0.85f);
            out_delta.y = stick_offset.y / (outer_radius * 0.85f);
        }
        else
        {
            out_delta.x = 0;
            out_delta.y = 0;
        }

        // Draw label with background for better visibility
        ImVec2 label_size = ImGui::CalcTextSize(label);
        ImVec2 label_pos = ImVec2(center.x - label_size.x * 0.5f, center.y - outer_radius - 25);
        ImVec2 label_bg_min = ImVec2(label_pos.x - 5, label_pos.y - 2);
        ImVec2 label_bg_max =
            ImVec2(label_pos.x + label_size.x + 5, label_pos.y + label_size.y + 2);
        draw_list->AddRectFilled(label_bg_min, label_bg_max, IM_COL32(0, 0, 0, 150), 4.0f);
        draw_list->AddText(label_pos, IM_COL32(255, 255, 255, 255), label);
    };

    // Helper function to draw holdable buttons
    auto draw_hold_button = [&](ImVec2 pos, ImVec2 size, const char* label, int button_id) -> bool {
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();

        ImVec2 min = pos;
        ImVec2 max = ImVec2(pos.x + size.x, pos.y + size.y);

        // Check if mouse is over button
        bool is_hovered = mouse_pos.x >= min.x && mouse_pos.x <= max.x && mouse_pos.y >= min.y &&
                          mouse_pos.y <= max.y;

        // Check if button is being held
        bool is_held = false;
        if( is_mouse_down && is_hovered && captured_joystick == -1 )
        {
            captured_joystick = button_id;
        }

        if( is_mouse_down && captured_joystick == button_id )
        {
            is_held = true;
        }

        // Draw button
        ImU32 bg_color = is_held      ? IM_COL32(60, 120, 200, 220)
                         : is_hovered ? IM_COL32(80, 80, 80, 200)
                                      : IM_COL32(50, 50, 50, 180);
        ImU32 border_color = IM_COL32(150, 150, 150, 220);

        draw_list->AddRectFilled(min, max, bg_color, 8.0f);
        draw_list->AddRect(min, max, border_color, 8.0f, 0, 2.5f);

        // Draw text
        ImVec2 text_size = ImGui::CalcTextSize(label);
        ImVec2 text_pos =
            ImVec2(pos.x + (size.x - text_size.x) * 0.5f, pos.y + (size.y - text_size.y) * 0.5f);
        draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), label);

        return is_held;
    };

    // Get delta time for framerate-independent movement
    float delta_time = ImGui::GetIO().DeltaTime;

    // Vertical movement buttons - above move joystick
    float button_width = 70.0f;
    float button_height = 45.0f;
    float button_spacing = 8.0f;
    float buttons_y = screen_size.y - margin - joystick_radius * 2 - 60;
    float buttons_center_x = margin + joystick_radius - (button_width + button_spacing / 2);

    ImVec2 up_button_pos(buttons_center_x, buttons_y);
    ImVec2 down_button_pos(buttons_center_x + button_width + button_spacing, buttons_y);
    ImVec2 button_size(button_width, button_height);

    bool up_held = draw_hold_button(up_button_pos, button_size, "Up", 2);
    bool down_held = draw_hold_button(down_button_pos, button_size, "Down", 3);

    // Apply vertical movement from buttons (framerate independent)
    if( up_held )
    {
        game->camera_world_y -= (int)(game->camera_movement_speed * 2 * delta_time * 60.0f);
    }
    if( down_held )
    {
        game->camera_world_y += (int)(game->camera_movement_speed * 2 * delta_time * 60.0f);
    }

    // Movement joystick - bottom left
    ImVec2 move_joystick_center(margin + joystick_radius, screen_size.y - margin - joystick_radius);
    ImVec2 move_delta(0, 0);
    bool move_touching = false;
    draw_virtual_joystick(
        move_joystick_center, joystick_radius, move_delta, "Move", move_touching, 0);

    // Look joystick - bottom right
    ImVec2 look_joystick_center(
        screen_size.x - margin - joystick_radius, screen_size.y - margin - joystick_radius);
    ImVec2 look_delta(0, 0);
    bool look_touching = false;
    draw_virtual_joystick(
        look_joystick_center, joystick_radius, look_delta, "Look", look_touching, 1);

    // Apply movement joystick input (framerate independent)
    if( move_delta.x != 0 || move_delta.y != 0 )
    {
        // Convert yaw to radians for sin/cos (yaw is in 0-2047 range)
        float yaw_radians = (game->camera_yaw * 2.0f * M_PI) / 2048.0f;

        // Joystick deltas: negative y = forward, positive y = backward
        //                  positive x = right, negative x = left
        // Invert y so that forward is positive
        float forward_component = -move_delta.y;
        float strafe_component = move_delta.x;

        // Match the movement from libgame.c:
        // Forward (W): x -= sin, z += cos
        // Backward (S): x += sin, z -= cos
        // Strafe Right (D): x += cos, z += sin
        // Strafe Left (A): x -= cos, z -= sin

        int dx =
            (int)((-sinf(yaw_radians) * forward_component + cosf(yaw_radians) * strafe_component) *
                  game->camera_movement_speed * delta_time * 60.0f);
        int dz =
            (int)((cosf(yaw_radians) * forward_component + sinf(yaw_radians) * strafe_component) *
                  game->camera_movement_speed * delta_time * 60.0f);

        game->camera_world_x += dx;
        game->camera_world_z += dz;
    }

    // Apply look joystick input (framerate independent)
    if( look_delta.x != 0 || look_delta.y != 0 )
    {
        // Yaw (horizontal looking - X axis) - inverted for natural feel
        game->camera_yaw -= (int)(look_delta.x * game->camera_rotation_speed * delta_time * 60.0f);
        if( game->camera_yaw < 0 )
            game->camera_yaw += 2048;
        if( game->camera_yaw >= 2048 )
            game->camera_yaw -= 2048;

        // Pitch (vertical looking - Y axis) - inverted for natural feel
        game->camera_pitch +=
            (int)(look_delta.y * game->camera_rotation_speed * delta_time * 60.0f);
        if( game->camera_pitch < 0 )
            game->camera_pitch += 2048;
        if( game->camera_pitch >= 2048 )
            game->camera_pitch -= 2048;
    }

    // Render the joysticks overlay
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static void
load_static_scene(struct RendererEmscripten_SDL2WebGL1* renderer, struct Game* game)
{
    // This function builds a static scene buffer containing all scene geometry
    // It's called once when the scene is loaded

    printf("load_static_scene called\n");

    // Check if Pix3DGL is initialized
    if( !renderer->pix3dgl )
    {
        printf("Pix3DGL not initialized\n");
        return;
    }

    // Check if we have a valid scene to load
    if( !game->scene || !game->scene->scene_tiles )
    {
        printf("Scene not ready for static loading\n");
        return;
    }

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
                        game->textures_cache,
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
render_scene(struct RendererEmscripten_SDL2WebGL1* renderer, struct Game* game)
{
    return;
    // OPTIMIZATION: Track camera state to avoid unnecessary sorting every frame
    static int last_cam_x = -99999;
    static int last_cam_y = -99999;
    static int last_cam_z = -99999;
    static float last_cam_yaw = -99999.0f;
    static float last_cam_pitch = -99999.0f;
    static int frame_count = 0;

    // Performance tracking
    double perf_frequency = emscripten_get_now(); // ms since page load
    double frame_start = perf_frequency;

    frame_count++;

    // Update animation clock for texture animations at 50fps (20ms per frame)
    // Quantize time to 50fps intervals for game-accurate animation
    double current_time_ms = emscripten_get_now();
    Uint32 frame_ticks = ((Uint32)current_time_ms / 20) * 20; // Round down to nearest 20ms interval
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
        double face_order_start = emscripten_get_now();

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
                int screen_width = renderer->soft3d_pixel_buffer_nullable ? renderer->soft3d_width
                                                                          : renderer->width;
                int screen_height = renderer->soft3d_pixel_buffer_nullable ? renderer->soft3d_height
                                                                           : renderer->height;
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
                    screen_width,
                    screen_height,
                    50); // near_plane_z

                // Collect face indices in render order
                static std::vector<int> face_order;
                face_order.clear();

                if( renderer->soft3d_pixel_buffer_nullable )
                {
                    while( iter_render_model_next(&iter_model) )
                    {
                        face_order.push_back(iter_model.value_face);

                        model_draw_face(
                            renderer->soft3d_pixel_buffer_nullable,
                            iter_model.value_face,
                            scene_model->model->face_infos,
                            scene_model->model->face_indices_a,
                            scene_model->model->face_indices_b,
                            scene_model->model->face_indices_c,
                            scene_model->model->face_count,
                            iter_model.screen_vertices_x,
                            iter_model.screen_vertices_y,
                            iter_model.screen_vertices_z,
                            iter_model.ortho_vertices_x,
                            iter_model.ortho_vertices_y,
                            iter_model.ortho_vertices_z,
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
                            renderer->soft3d_width / 2,
                            renderer->soft3d_height / 2,
                            50,
                            renderer->soft3d_width,
                            renderer->soft3d_height,
                            game->camera_fov,
                            game->textures_cache);
                    }
                }
                else
                {
                    while( iter_render_model_next(&iter_model) )
                    {
                        face_order.push_back(iter_model.value_face);
                    }
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
                    if( renderer->soft3d_pixel_buffer_nullable )
                    {
                        render_scene_tile(
                            renderer->soft3d_pixel_buffer_nullable,
                            g_screen_vertices_x,
                            g_screen_vertices_y,
                            g_screen_vertices_z,
                            g_ortho_vertices_x,
                            g_ortho_vertices_y,
                            g_ortho_vertices_z,
                            renderer->soft3d_width,
                            renderer->soft3d_height,
                            // Had to use 100 here because of the scale, near plane z was resulting
                            // in triangles extremely close to the camera.
                            50,
                            game->camera_world_x,
                            game->camera_world_y,
                            game->camera_world_z,
                            game->camera_pitch,
                            game->camera_yaw,
                            game->camera_roll,
                            game->camera_fov,
                            scene_tile,
                            game->textures_cache,
                            NULL);
                    }
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

        // Update last known camera position
        last_cam_x = game->camera_world_x;
        last_cam_y = game->camera_world_y;
        last_cam_z = game->camera_world_z;
        last_cam_yaw = game->camera_yaw;
        last_cam_pitch = game->camera_pitch;

        double face_order_end = emscripten_get_now();
        renderer->face_order_time_ms = face_order_end - face_order_start;
    }
    else
    {
        // Camera didn't move - reuse last frame's face ordering (FAST!)
        renderer->face_order_time_ms = 0.0; // No face order computation this frame
    }

    // Draw the static scene buffer (reuses cached index buffer if camera didn't move!)
    double render_start = emscripten_get_now();
    pix3dgl_scene_static_draw(renderer->pix3dgl);
    double render_end = emscripten_get_now();
    renderer->render_time_ms = render_end - render_start;

    pix3dgl_end_frame(renderer->pix3dgl);
}

extern "C" struct RendererEmscripten_SDL2WebGL1*
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_New(int width, int height)
{
    struct RendererEmscripten_SDL2WebGL1* renderer =
        (struct RendererEmscripten_SDL2WebGL1*)malloc(sizeof(struct RendererEmscripten_SDL2WebGL1));
    memset(renderer, 0, sizeof(struct RendererEmscripten_SDL2WebGL1));

    renderer->width = width;
    renderer->height = height;

    renderer->op_capacity = 10900;
    renderer->ops = (struct SceneOp*)malloc(renderer->op_capacity * sizeof(struct SceneOp));
    memset(renderer->ops, 0, renderer->op_capacity * sizeof(struct SceneOp));

    renderer->op_count = 0;

    return renderer;
}

extern "C" void
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Free(struct RendererEmscripten_SDL2WebGL1* renderer)
{
    free(renderer);
}

extern "C" bool
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Init(
    struct RendererEmscripten_SDL2WebGL1* renderer, struct Platform* platform)
{
    renderer->platform = platform;

    // Create WebGL context directly using Emscripten API (like model_viewerfx.cpp)
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.enableExtensionsByDefault = 1;
    attrs.alpha = 1;
    attrs.depth = 1;
    attrs.stencil = 0;
    attrs.antialias = 0;
    attrs.premultipliedAlpha = 0;
    attrs.preserveDrawingBuffer = 0;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_DEFAULT;
    attrs.failIfMajorPerformanceCaveat = 0;

    // Force WebGL1 for maximum compatibility
    attrs.majorVersion = 1;
    attrs.minorVersion = 0;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_create_context("#canvas", &attrs);

    if( context < 0 )
    {
        printf("Failed to create WebGL1 context!\n");
        return false;
    }

    // Make the context current
    if( emscripten_webgl_make_context_current(context) != EMSCRIPTEN_RESULT_SUCCESS )
    {
        printf("Failed to make WebGL context current!\n");
        return false;
    }

    // Set canvas size
    emscripten_set_canvas_element_size("#canvas", renderer->width, renderer->height);

    renderer->gl_context = (SDL_GLContext)context;

    printf("WebGL1 context created successfully\n");

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

    // ImGui supports WebGL1/ES2 via the OpenGL3 backend with ES2 shaders
    ImGui_ImplOpenGL3_Init("#version 100"); // GLSL ES 1.00 for WebGL1

    // Initialize pix3dgl (uses pix3dgl_opengles2.cpp implementation)
    renderer->pix3dgl = pix3dgl_new();
    if( !renderer->pix3dgl )
    {
        printf("Failed to initialize Pix3DGL\n");
        return false;
    }

    return true;
}

bool
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_InitSoft3D(
    struct RendererEmscripten_SDL2WebGL1* renderer,
    int max_width,
    int max_height,
    const char* canvas_id)
{
    renderer->soft3d_max_width = max_width;
    renderer->soft3d_max_height = max_height;

    strncpy(renderer->soft3d_canvas_id, canvas_id, sizeof(renderer->soft3d_canvas_id));

    renderer->soft3d_pixel_buffer_nullable = (int*)malloc(max_width * max_height * sizeof(int));
    if( !renderer->soft3d_pixel_buffer_nullable )
    {
        printf("Failed to allocate soft3d pixel buffer\n");
        return false;
    }
    memset(renderer->soft3d_pixel_buffer_nullable, 0, max_width * max_height * sizeof(int));

    renderer->soft3d_width = max_width;
    renderer->soft3d_height = max_height;

    // Simple approach: Just use Canvas 2D API via JavaScript
    // Emscripten will let us blit pixels directly to canvas via EM_ASM
    printf("Initialized soft3d pixel buffer (%dx%d)\n", max_width, max_height);

    return true;
}

extern "C" void
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Shutdown(
    struct RendererEmscripten_SDL2WebGL1* renderer)
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(renderer->gl_context);
}

extern "C" void
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Render(
    struct RendererEmscripten_SDL2WebGL1* renderer,
    struct Game* game,
    struct GameGfxOpList* gfx_op_list)
{
    // Handle canvas resize: sync canvas resolution with CSS size
    double css_width, css_height;
    emscripten_get_element_css_size("#canvas", &css_width, &css_height);

    // Clear soft3d pixel buffer if it exists
    if( renderer->soft3d_pixel_buffer_nullable )
    {
        for( int i = 0; i < renderer->soft3d_height; i++ )
        {
            memset(
                &renderer->soft3d_pixel_buffer_nullable[i * renderer->soft3d_width],
                0,
                renderer->soft3d_width * sizeof(int));
        }
    }

    int new_width = (int)css_width;
    int new_height = (int)css_height;

    // Only update if size changed
    if( new_width != renderer->width || new_height != renderer->height )
    {
        renderer->width = new_width;
        renderer->height = new_height;
        emscripten_set_canvas_element_size("#canvas", new_width, new_height);

        // Update WebGL viewport to match new canvas size
        glViewport(0, 0, new_width, new_height);

        // Also update SDL window size so ImGui can pick up the correct dimensions
        if( renderer->platform && renderer->platform->window )
        {
            SDL_SetWindowSize(renderer->platform->window, new_width, new_height);
        }

        printf("Canvas resized to %dx%d\n", new_width, new_height);
    }

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

    // Render soft3d buffer if it exists - blit to a separate canvas
    if( renderer->soft3d_pixel_buffer_nullable )
    {
        // Use Emscripten's EM_ASM to blit pixel buffer to a separate soft3d canvas
        // clang-format off
        EM_ASM(
            {
                var width = $0;
                var height = $1;
                var pixelPtr = $2;
                var canvasIdPtr = $3;

                // Convert C string pointer to JavaScript string
                var canvasId = UTF8ToString(canvasIdPtr);

                // Get the soft3d canvas (already exists in the HTML)
                var soft3dCanvas = document.getElementById(canvasId);
                if( !soft3dCanvas )
                {
                    console.error('soft3d-canvas not found in DOM!', canvasId);
                    return;
                }
                
                // Show the container if it's hidden
                var container = document.getElementById('soft3d-container');
                if (container && container.style.display === 'none') {
                    container.style.display = 'block';
                }


                // Update canvas size if it changed
                if (soft3dCanvas.width !== width || soft3dCanvas.height !== height) {
                    soft3dCanvas.width = width;
                    soft3dCanvas.height = height;
                }

                var ctx = soft3dCanvas.getContext('2d');
                if (!ctx) return;

                // Create or reuse ImageData
                if (!Module._soft3dImageData || Module._soft3dImageData.width !== width ||
                    Module._soft3dImageData.height !== height)
                {
                    Module._soft3dImageData = ctx.createImageData(width, height);
                }

                var imageData = Module._soft3dImageData;
                var data = imageData.data;

                // Copy pixel data from WASM memory (ARGB8888) to ImageData (RGBA)
                var srcView = new Uint32Array(HEAPU8.buffer, pixelPtr, width * height);
                
                var nonZeroCount = 0;
                for (var i = 0; i < width * height; i++)
                {
                    var pixel = srcView[i];
                    if (pixel !== 0) nonZeroCount++;
                    
                    var idx = i * 4;
                    // Convert ARGB to RGBA (force alpha to 255 if it's 0)
                    data[idx + 0] = (pixel >> 16) & 0xFF; // R
                    data[idx + 1] = (pixel >> 8) & 0xFF;  // G
                    data[idx + 2] = (pixel >> 0) & 0xFF;  // B
                    var alpha = (pixel >> 24) & 0xFF;
                    data[idx + 3] = alpha === 0 ? 255 : alpha; // A (default to opaque if 0)
                }


                // Put the image data on the soft3d canvas
                ctx.putImageData(imageData, 0, 0);
            },
            renderer->soft3d_width,
            renderer->soft3d_height,
            renderer->soft3d_pixel_buffer_nullable,
            renderer->soft3d_canvas_id);
        // clang-format on
    }

    render_imgui(renderer, game);

    if( renderer->platform->window )
    {
        SDL_GL_SwapWindow(renderer->platform->window);
    }

    game_gfx_op_list_reset(gfx_op_list);
}
