#include "platform_impl_emscripten_sdl2_renderer_webgl1.h"

extern "C" {
#include "graphics/render.h"
#include "libgame.u.h"
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

        // Add model geometry directly to static scene buffer (no individual model loading)
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

    // Finalize the static scene - uploads to GPU
    pix3dgl_scene_static_load_end(renderer->pix3dgl);

    printf("Static scene buffer built successfully\n");
}

static void
render_scene(struct Renderer* renderer, struct Game* game)
{
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

    // Depth testing disabled - using painter's algorithm for draw order
    glDisable(GL_DEPTH_TEST);

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

                // Compute face order for this model using iter_render_model_init
                struct IterRenderModel iter_model;
                iter_render_model_init(
                    &iter_model,
                    scene_model,
                    iter_render_scene_ops.value.yaw,
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

                while( iter_render_model_next(&iter_model) )
                {
                    face_order.push_back(iter_model.value_face);
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
        printf("Frame %d: Reusing cached face order\n", frame_count);
        renderer->face_order_time_ms = 0.0; // No face order computation this frame
    }

    // Draw the static scene buffer (reuses cached index buffer if camera didn't move!)
    double render_start = emscripten_get_now();
    pix3dgl_scene_static_draw(renderer->pix3dgl);
    double render_end = emscripten_get_now();
    renderer->render_time_ms = render_end - render_start;

    pix3dgl_end_frame(renderer->pix3dgl);
}

extern "C" struct Renderer*
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_New(int width, int height)
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

extern "C" void
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Free(struct Renderer* renderer)
{
    free(renderer);
}

extern "C" bool
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Init(
    struct Renderer* renderer, struct Platform* platform)
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

    return true;
}

extern "C" void
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Shutdown(struct Renderer* renderer)
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(renderer->gl_context);
}

extern "C" void
PlatformImpl_Emscripten_SDL2_Renderer_WebGL1_Render(
    struct Renderer* renderer, struct Game* game, struct GameGfxOpList* gfx_op_list)
{
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

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
