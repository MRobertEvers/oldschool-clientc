extern "C" {
#include "graphics/render.h"
#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/frustrum_cullmap.h"
#include "osrs/scene.h"
#include "osrs/scene_cache.h"
#include "osrs/scene_tile.h"
#include "osrs/tables/config_floortype.h"
#include "osrs/tables/config_idk.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/config_object.h"
#include "osrs/tables/config_sequence.h"
#include "osrs/tables/configs.h"
#include "osrs/tables/sprites.h"
#include "osrs/tables/textures.h"
#include "osrs/xtea_config.h"
#include "screen.h"
#include "shared_tables.h"
}

#include "osrs/pix3dgl.h"

#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Emscripten compatibility
#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>

#include <emscripten.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Emscripten global variables - must be declared early for function visibility
#ifdef __EMSCRIPTEN__
bool g_using_webgl2 = false; // Track WebGL version globally
#endif

// OpenGL ES2 headers for rendering
#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(__ANDROID__)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(__APPLE__)
// Use regular OpenGL headers on macOS/iOS for development
#include <OpenGL/gl.h>
#include <OpenGL/gl3.h>
#define GL_GLEXT_PROTOTYPES
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

// SDL2 main handling
#ifdef main
#undef main
#endif

// #define LUMBRIDGE_KITCHEN_TILE_1 14815
#define LUMBRIDGE_KITCHEN_TILE_1 14815
#define LUMBRIDGE_KITCHEN_TILE_2 14816
#define LUMBRIDGE_BRICK_WALL 638
#define CACHE_PATH "../cache"

// ImGui headers
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

extern "C" int g_sin_table[2048];
extern "C" int g_cos_table[2048];
extern "C" int g_tan_table[2048];

extern "C" int g_blit_buffer[SCREEN_WIDTH * SCREEN_HEIGHT] = { 0 };

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

    int model_id;
    struct SceneModel* scene_model;

    uint64_t start_time;
    uint64_t end_time;

    uint64_t frame_count;
    uint64_t frame_time_sum;

    uint64_t projection_time_sum;

    struct CacheTexture* textures;
    int* texture_ids;
    int texture_count;

    struct Scene* scene;

    struct SceneOp* ops;
    int op_count;

    int max_render_ops;
    int manual_render_ops;

    struct Cache* cache;
    struct TexturesCache* textures_cache;

    // Add Pix3DGL for OpenGL rendering
    struct Pix3DGL* pix3dgl;
};

struct Pix3D
{
    int* pixel_buffer;
    int width;
    int height;

    int center_x;
    int center_y;

    bool clipped;

    int screen_vertices_x[4096];
    int screen_vertices_y[4096];
    int screen_vertices_z[4096];
    int ortho_vertices_x[4096];
    int ortho_vertices_y[4096];
    int ortho_vertices_z[4096];

    int depth_to_face_count[1600];
    int depth_to_face_buckets[1600][512];

    uint64_t deob_sum;
    uint64_t s4_sum;
    uint64_t bs4_sum;
    uint64_t bary_bs4_sum;

    uint64_t texture_sum;
    uint64_t texture_blerp_sum;
    uint64_t texture_count;

    int deob_count;
    int s4_count;
    int bs4_count;
    int bary_bs4_count;
    int projection_count;
} _Pix3D;

void
game_free(struct Game* game)
{
    if( game->ops )
        free(game->ops);
    if( game->textures_cache )
        textures_cache_free(game->textures_cache);
    if( game->scene )
        scene_free(game->scene);
    if( game->textures )
        free(game->textures);
    if( game->pix3dgl )
        pix3dgl_cleanup(game->pix3dgl);
}

struct PlatformSDL2
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SDL_GLContext gl_context; // Add OpenGL context
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

#ifdef __EMSCRIPTEN__
    // For Emscripten, create a visible window that matches the canvas size
    platform->window = SDL_CreateWindow(
        "Model Viewer FX",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_SHOWN); // Remove OpenGL flag - use direct WebGL

    if( !platform->window )
    {
        printf("Window creation failed: %s\n", SDL_GetError());
        return false;
    }

    // Create WebGL context directly (this approach worked before)
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

    // Force WebGL1 only for maximum compatibility
    g_using_webgl2 = false;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context;

    // Create WebGL1 context directly
    attrs.majorVersion = 1;
    attrs.minorVersion = 0;
    context = emscripten_webgl_create_context("#canvas", &attrs);

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

    if( SCREEN_WIDTH > 0 && SCREEN_HEIGHT > 0 )
    {
        emscripten_set_canvas_element_size("#canvas", SCREEN_WIDTH, SCREEN_HEIGHT);
    }
    else
    {
        printf("ERROR: Invalid screen dimensions, using fallback 800x600\n");
        emscripten_set_canvas_element_size("#canvas", 800, 600);
    }

    platform->gl_context = (SDL_GLContext)context;

    // Skip SDL renderer - use WebGL for everything including ImGui
    platform->renderer = nullptr;

    // Initialize ImGui with OpenGL3 backend for WebGL1
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

    // Set display size explicitly for Emscripten
    io.DisplaySize = ImVec2((float)SCREEN_WIDTH, (float)SCREEN_HEIGHT);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    // Store initial dimensions in platform struct
    platform->window_width = SCREEN_WIDTH;
    platform->window_height = SCREEN_HEIGHT;
    platform->drawable_width = SCREEN_WIDTH;
    platform->drawable_height = SCREEN_HEIGHT;

    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends - cast context like main.cpp does
    if( !ImGui_ImplSDL2_InitForOpenGL(platform->window, (void*)platform->gl_context) )
    {
        printf("Failed to initialize ImGui SDL2 backend\n");
        return false;
    }
    if( !ImGui_ImplOpenGL3_Init("#version 100") )
    { // For WebGL1 / GLES2
        printf("Failed to initialize ImGui OpenGL3 backend\n");
        return false;
    }
    printf("ImGui initialized with OpenGL3 backend for WebGL1\n");

    // Enable SDL events like main.cpp does
    SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEBUTTONUP, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEWHEEL, SDL_ENABLE);
    SDL_EventState(SDL_KEYDOWN, SDL_ENABLE);
    SDL_EventState(SDL_KEYUP, SDL_ENABLE);
    SDL_EventState(SDL_TEXTINPUT, SDL_ENABLE);

#else
    // Set OpenGL attributes for native builds
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    // Create window with OpenGL support
    platform->window = SDL_CreateWindow(
        "Model Viewer FX",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if( !platform->window )
    {
        printf("Window creation failed: %s\n", SDL_GetError());
        return false;
    }

    // Create OpenGL context
    platform->gl_context = SDL_GL_CreateContext(platform->window);
    if( !platform->gl_context )
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
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends - use OpenGL3 for native builds too
    ImGui_ImplSDL2_InitForOpenGL(platform->window, platform->gl_context);
    ImGui_ImplOpenGL3_Init("#version 150"); // OpenGL 3.2 Core for native builds

    // No SDL renderer needed for OpenGL3 backend
    platform->renderer = nullptr;
#endif

    // Remove SDL renderer/texture creation - we're using pure OpenGL
    // This prevents conflicts between OpenGL and SDL renderer

    // We don't need pixel buffer for OpenGL rendering, but keep it for compatibility
    // Allocate pixel buffer (not used for OpenGL but needed for struct compatibility)
    platform->pixel_buffer = (int*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
    if( !platform->pixel_buffer )
    {
        printf("Failed to allocate pixel buffer\n");
        return false;
    }
    memset(platform->pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

    // Get initial window size
    SDL_GetWindowSize(platform->window, &platform->window_width, &platform->window_height);
    // Note: No SDL renderer, so we use window size for drawable size too
    platform->drawable_width = platform->window_width;
    platform->drawable_height = platform->window_height;

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
    /**
     * Platform and renderer specific!
     */
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();

    /**
     * START Common Imgui here!
     */
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

    bool window_open = ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_NoCollapse);

    if( window_open )
    {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text(
            "Camera: (%.1f, %.1f, %.1f)",
            (float)game->camera_x,
            (float)game->camera_y,
            (float)game->camera_z);
        ImGui::Text(
            "Display Size: %.0fx%.0f", ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    }
    ImGui::End();

    ImGui::Render();

    /**
     * END common imgui here!
     */

    /**
     * Platform and renderer specific!
     */
    ImDrawData* draw_data = ImGui::GetDrawData();
    if( draw_data && draw_data->Valid && draw_data->CmdListsCount > 0 )
    {
        // Save OpenGL state before ImGui rendering
        GLboolean depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean blend_enabled = glIsEnabled(GL_BLEND);

        // Set up OpenGL state for ImGui (render on top)
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Render ImGui
        ImGui_ImplOpenGL3_RenderDrawData(draw_data);

        // Restore OpenGL state
        if( depth_test_enabled )
            glEnable(GL_DEPTH_TEST);
        if( !blend_enabled )
            glDisable(GL_BLEND);
    }
}
struct BoundingCylinder
{
    int center_to_top_edge;
    int center_to_bottom_edge;
    int radius;

    // TODO: Name?
    // - Max extent from model origin.
    // - Distance to farthest vertex?
    int min_z_depth_any_rotation;
};

#include <limits.h>

static void
game_render_sdl2(struct Game* game, struct PlatformSDL2* platform)
{
    /**
     * Implement COMMAND handlers here!
     *
     * LOAD_MODEL, CLEAR_MODEL, ETC.
     */
    Uint64 start_ticks = SDL_GetPerformanceCounter();

    if( game->pix3dgl )
    {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDisable(GL_DEPTH_TEST);

        glDisable(GL_CULL_FACE);

        pix3dgl_render_with_camera(
            game->pix3dgl,
            (float)game->camera_x,
            (float)game->camera_y,
            (float)game->camera_z,
            (float)game->camera_pitch,
            (float)game->camera_yaw,
            (float)platform->drawable_width,
            (float)platform->drawable_height);
    }

    Uint64 end_ticks = SDL_GetPerformanceCounter();
    game->start_time = start_ticks;
    game->end_time = end_ticks;

    game->frame_count++;
    game->frame_time_sum += end_ticks - start_ticks;
}

#include <iostream>

#ifdef __EMSCRIPTEN__
// Global game state for Emscripten access
struct Game* g_game = NULL;
struct PlatformSDL2* g_platform = NULL;
bool g_quit = false;

// Global state variables for Emscripten main loop
int g_w_pressed = 0;
int g_a_pressed = 0;
int g_s_pressed = 0;
int g_d_pressed = 0;
int g_up_pressed = 0;
int g_down_pressed = 0;
int g_left_pressed = 0;
int g_right_pressed = 0;
int g_space_pressed = 0;
int g_f_pressed = 0;
int g_r_pressed = 0;
int g_m_pressed = 0;
int g_n_pressed = 0;
int g_i_pressed = 0;
int g_k_pressed = 0;
int g_l_pressed = 0;
int g_j_pressed = 0;
int g_q_pressed = 0;
int g_e_pressed = 0;
int g_comma_pressed = 0;
int g_period_pressed = 0;
int g_speed = 10;

// Handle SDL events for ImGui and input
EM_BOOL
sdl_event_handler(int eventType, const EmscriptenUiEvent* uiEvent, void* userData)
{
    SDL_Event event;
    while( SDL_PollEvent(&event) )
    {
        // Process ImGui events first
        ImGui_ImplSDL2_ProcessEvent(&event);

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
                g_platform->drawable_width = g_platform->window_width;
                g_platform->drawable_height = g_platform->window_height;

                // Update ImGui display size for proper scaling
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize =
                    ImVec2((float)g_platform->drawable_width, (float)g_platform->drawable_height);
            }
        }
        else if( event.type == SDL_MOUSEMOTION )
        {
            transform_mouse_coordinates(
                event.motion.x, event.motion.y, &g_game->mouse_x, &g_game->mouse_y, g_platform);
        }
        else if( event.type == SDL_MOUSEBUTTONDOWN )
        {
            int transformed_x, transformed_y;
            transform_mouse_coordinates(
                event.button.x, event.button.y, &transformed_x, &transformed_y, g_platform);
            g_game->mouse_click_x = transformed_x;
            g_game->mouse_click_y = transformed_y;
            g_game->mouse_click_cycle = 0;
        }
        else if( event.type == SDL_KEYDOWN )
        {
            switch( event.key.keysym.sym )
            {
            case SDLK_ESCAPE:
                g_quit = true;
                break;
            case SDLK_UP:
                g_up_pressed = 1;
                break;
            case SDLK_DOWN:
                g_down_pressed = 1;
                break;
            case SDLK_LEFT:
                g_left_pressed = 1;
                break;
            case SDLK_RIGHT:
                g_right_pressed = 1;
                break;
            case SDLK_s:
                g_s_pressed = 1;
                break;
            case SDLK_w:
                g_w_pressed = 1;
                break;
            case SDLK_d:
                g_d_pressed = 1;
                break;
            case SDLK_a:
                g_a_pressed = 1;
                break;
            case SDLK_r:
                g_r_pressed = 1;
                break;
            case SDLK_f:
                g_f_pressed = 1;
                break;
            case SDLK_SPACE:
                g_space_pressed = 1;
                break;
            case SDLK_m:
                g_m_pressed = 1;
                break;
            case SDLK_n:
                g_n_pressed = 1;
                break;
            case SDLK_i:
                g_i_pressed = 1;
                break;
            case SDLK_k:
                g_k_pressed = 1;
                break;
            case SDLK_l:
                g_l_pressed = 1;
                break;
            case SDLK_j:
                g_j_pressed = 1;
                break;
            case SDLK_q:
                g_q_pressed = 1;
                break;
            case SDLK_e:
                g_e_pressed = 1;
                break;
            case SDLK_PERIOD:
                g_period_pressed = 1;
                break;
            case SDLK_COMMA:
                g_comma_pressed = 1;
                break;
            }
        }
        else if( event.type == SDL_KEYUP )
        {
            switch( event.key.keysym.sym )
            {
            case SDLK_s:
                g_s_pressed = 0;
                break;
            case SDLK_w:
                g_w_pressed = 0;
                break;
            case SDLK_d:
                g_d_pressed = 0;
                break;
            case SDLK_a:
                g_a_pressed = 0;
                break;
            case SDLK_r:
                g_r_pressed = 0;
                break;
            case SDLK_f:
                g_f_pressed = 0;
                break;
            case SDLK_UP:
                g_up_pressed = 0;
                break;
            case SDLK_DOWN:
                g_down_pressed = 0;
                break;
            case SDLK_LEFT:
                g_left_pressed = 0;
                break;
            case SDLK_RIGHT:
                g_right_pressed = 0;
                break;
            case SDLK_SPACE:
                g_space_pressed = 0;
                break;
            case SDLK_m:
                g_m_pressed = 0;
                break;
            case SDLK_n:
                g_n_pressed = 0;
                break;
            case SDLK_i:
                g_i_pressed = 0;
                break;
            case SDLK_k:
                g_k_pressed = 0;
                break;
            case SDLK_l:
                g_l_pressed = 0;
                break;
            case SDLK_j:
                g_j_pressed = 0;
                break;
            case SDLK_PERIOD:
                g_period_pressed = 0;
                break;
            case SDLK_COMMA:
                g_comma_pressed = 0;
                break;
            case SDLK_q:
                g_q_pressed = 0;
                break;
            case SDLK_e:
                g_e_pressed = 0;
                break;
            }
        }
    }
    return EM_TRUE;
}

// Global frame counter for debugging
static int g_frame_count = 0;

// Main loop function
EM_BOOL
loop(double time, void* userData)
{
    g_frame_count++;

    if( g_quit || !g_game || !g_platform )
    {
        return EM_FALSE;
    }

    // Handle SDL events for ImGui
    sdl_event_handler(0, nullptr, nullptr);

    int camera_moved = 0;

    // Convert yaw from 2048-based system to radians for proper trigonometry
    float yaw_radians = (g_game->camera_yaw * 2.0f * M_PI) / 2048.0f;

    // Camera movement logic
    if( g_w_pressed ) // Forward
    {
        g_game->camera_x -= (int)(sin(yaw_radians) * g_speed);
        g_game->camera_z += (int)(cos(yaw_radians) * g_speed);
        camera_moved = 1;
    }

    if( g_s_pressed ) // Backward
    {
        g_game->camera_x += (int)(sin(yaw_radians) * g_speed);
        g_game->camera_z -= (int)(cos(yaw_radians) * g_speed);
        camera_moved = 1;
    }

    if( g_a_pressed ) // Strafe left
    {
        g_game->camera_x -= (int)(cos(yaw_radians) * g_speed);
        g_game->camera_z -= (int)(sin(yaw_radians) * g_speed);
        camera_moved = 1;
    }

    if( g_d_pressed ) // Strafe right
    {
        g_game->camera_x += (int)(cos(yaw_radians) * g_speed);
        g_game->camera_z += (int)(sin(yaw_radians) * g_speed);
        camera_moved = 1;
    }

    int angle_delta = 20;
    if( g_up_pressed )
    {
        g_game->camera_pitch = (g_game->camera_pitch + angle_delta) % 2048;
        camera_moved = 1;
    }

    if( g_left_pressed )
    {
        g_game->camera_yaw = (g_game->camera_yaw + angle_delta) % 2048;
        camera_moved = 1;
    }

    if( g_right_pressed )
    {
        g_game->camera_yaw = (g_game->camera_yaw - angle_delta + 2048) % 2048;
        camera_moved = 1;
    }

    if( g_down_pressed )
    {
        g_game->camera_pitch = (g_game->camera_pitch - angle_delta + 2048) % 2048;
        camera_moved = 1;
    }

    if( g_f_pressed ) // Move down (decrease Y - fall)
    {
        g_game->camera_y += g_speed;
        camera_moved = 1;
    }

    if( g_r_pressed ) // Move up (increase Y - rise)
    {
        g_game->camera_y -= g_speed;
        camera_moved = 1;
    }

    if( g_comma_pressed )
    {
        g_game->manual_render_ops = 1;
        g_game->max_render_ops -= 1;
        if( g_game->max_render_ops < 0 )
            g_game->max_render_ops = 0;
    }
    if( g_period_pressed )
    {
        g_game->manual_render_ops = 1;
        g_game->max_render_ops += 1;
        if( g_game->max_render_ops > g_game->op_count )
            g_game->max_render_ops = g_game->op_count;
    }

    // Set viewport to canvas size
    glViewport(0, 0, g_platform->drawable_width, g_platform->drawable_height);

    // Clear the canvas
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f); // Dark blue background
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    game_render_sdl2(g_game, g_platform);
    game_render_imgui(g_game, g_platform);

    // Swap buffers
    if( g_platform && g_platform->window )
    {
        SDL_GL_SwapWindow(g_platform->window);
    }

    return EM_TRUE;
}
#endif

int
main(int argc, char* argv[])
{
    std::cout << "SDL_main" << std::endl;

    init_hsl16_to_rgb_table();
    init_reciprocal16();

    init_sin_table();
    init_cos_table();
    init_tan_table();

    int xtea_keys_count = xtea_config_load_keys("../cache/xteas.json");
    if( xtea_keys_count == -1 )
    {
        printf("Failed to load xtea keys from: ../cache/xteas.json\n");
        printf("Make sure the xteas.json file exists in the cache directory\n");
        return 0;
    }

    fflush(stdout);
    struct Cache* cache = cache_new_from_directory("../cache");
    if( !cache )
    {
        printf("Failed to load cache from directory: %s\n", CACHE_PATH);
        printf("Make sure the cache directory exists and contains the required files:\n");
        printf("  - main_file_cache.dat2\n");
        printf("  - main_file_cache.idx0 through main_file_cache.idx255\n");
        return 1;
    }

    struct PlatformSDL2 platform = { 0 };

    if( !platform_sdl2_init(&platform) )
    {
        printf("Failed to initialize SDL\n");
        return 1;
    }

    memset(&_Pix3D, 0, sizeof(_Pix3D));
    _Pix3D.width = SCREEN_WIDTH;
    _Pix3D.height = SCREEN_HEIGHT;
    _Pix3D.center_x = SCREEN_WIDTH / 2;
    _Pix3D.center_y = SCREEN_HEIGHT / 2;
    _Pix3D.pixel_buffer = platform.pixel_buffer;

    struct Game game = { 0 };

    // Simple camera setup - position camera to look at model at origin
    // mera: pos(-350.0, -500.0, 31.0) pitch=1.779 yaw=1.853
    // Camera: pos(136.0, -90.0, -8.0) pitch=0.611 yaw=1.543
    game.camera_yaw = 1.543 * 2048 / M_PI / 2;   // No rotation left/right
    game.camera_pitch = 0.611 * 2048 / M_PI / 2; // No rotation up/down
    game.camera_roll = 0;                        // No roll
    game.camera_fov = 512;                       // Standard FOV
    game.camera_x = 136;                         // Center X (model is around 0)
    game.camera_y = -90;                         // Slightly above model (model Y is -3 to 0)
    game.camera_z = -8;                          // Camera back from model (model Z is -64 to 64)
    // // Simple camera setup - looking straight ahead
    // game.camera_yaw = 0;   // No rotation left/right
    // game.camera_pitch = 0; // No rotation up/down
    // game.camera_roll = 0;  // No roll
    // game.camera_fov = 512; // Standard FOV
    // game.camera_x = 0;     // Center X
    // game.camera_y = 0;     // Center Y
    // game.camera_z = 0;     // Camera at origin

    // game.camera_x = 170;
    // game.camera_y = 360;
    // game.camera_z = 200;

    // game.camera_x = -2576;
    // game.camera_y = -3015;
    // game.camera_z = 2000;
    // game.camera_pitch = 405;
    // game.camera_yaw = 1536;
    // game.camera_roll = 0;
    game.camera_fov = 512; // Default FOV
    game.model_id = LUMBRIDGE_KITCHEN_TILE_2;
    // game.model_id = LUMBRIDGE_BRICK_WALL;

    game.textures_cache = textures_cache_new(cache);
    game.cache = cache;

    // Initialize Pix3DGL for OpenGL rendering
    game.pix3dgl = pix3dgl_new();

    struct ModelCache* model_cache = model_cache_new();

    struct CacheModel* model = model_cache_checkout(model_cache, cache, game.model_id);
    game.scene_model = scene_model_new_lit_from_model(model, 0);

    // Load the model into Pix3DGL

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
    int speed = 10;
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

    printf("Setting up Emscripten main loop...\n");

    // Register SDL event handlers
    SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEBUTTONUP, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEWHEEL, SDL_ENABLE);
    SDL_EventState(SDL_KEYDOWN, SDL_ENABLE);
    SDL_EventState(SDL_KEYUP, SDL_ENABLE);
    SDL_EventState(SDL_TEXTINPUT, SDL_ENABLE);

    // Use Emscripten's animation frame loop like main.cpp does
    emscripten_set_main_loop_timing(EM_TIMING_RAF, 0);

    // Instead of animation frame loop, try a simple setTimeout approach
    emscripten_set_main_loop([]() { loop(0.0, nullptr); }, 0, 1); // 60 FPS, simulate infinite

#else

    // Traditional SDL main loop for native builds
    while( !quit )
    {
        Uint32 frame_start_time = SDL_GetTicks();

        while( SDL_PollEvent(&event) )
        {
            // Process ImGui events first
            ImGui_ImplSDL2_ProcessEvent(&event);

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
                    // Since we don't have SDL renderer, drawable size = window size
                    platform.drawable_width = platform.window_width;
                    platform.drawable_height = platform.window_height;

                    // Update ImGui display size for proper scaling
                    ImGuiIO& io = ImGui::GetIO();
                    io.DisplaySize =
                        ImVec2((float)platform.drawable_width, (float)platform.drawable_height);
                }
            }
            else if( event.type == SDL_MOUSEMOTION )
            {
                transform_mouse_coordinates(
                    event.motion.x, event.motion.y, &game.mouse_x, &game.mouse_y, &platform);
            }
            else if( event.type == SDL_MOUSEBUTTONDOWN )
            {
                int transformed_x, transformed_y;
                transform_mouse_coordinates(
                    event.button.x, event.button.y, &transformed_x, &transformed_y, &platform);
                game.mouse_click_x = transformed_x;
                game.mouse_click_y = transformed_y;
                game.mouse_click_cycle = 0;
            }
            else if( event.type == SDL_KEYDOWN )
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
            else if( event.type == SDL_KEYUP )
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

        // Convert yaw from 2048-based system to radians for proper trigonometry
        float yaw_radians = (game.camera_yaw * 2.0f * M_PI) / 2048.0f;

        // Yaw is CCW around the Y axis.
        // Yaw is 0 when aligned +Z
        // Pitch is CCW around the X axis.
        if( w_pressed ) // Forward
        {
            // Move forward in the direction the camera is facing
            // For CCW yaw around Y-axis: forward is +sin(yaw) in X, +cos(yaw) in Z
            game.camera_x -= (int)(sin(yaw_radians) * speed);
            game.camera_z += (int)(cos(yaw_radians) * speed);
            camera_moved = 1;
        }

        if( s_pressed ) // Backward
        {
            // Move backward opposite to camera facing direction
            game.camera_x += (int)(sin(yaw_radians) * speed);
            game.camera_z -= (int)(cos(yaw_radians) * speed);
            camera_moved = 1;
        }

        if( a_pressed ) // Strafe left
        {
            // Strafe left: perpendicular to forward direction
            // Left is forward rotated -90 degrees: -cos(yaw) in X, +sin(yaw) in Z
            game.camera_x -= (int)(cos(yaw_radians) * speed);
            game.camera_z -= (int)(sin(yaw_radians) * speed);
            camera_moved = 1;
        }

        if( d_pressed ) // Strafe right
        {
            // Strafe right: opposite of left
            game.camera_x += (int)(cos(yaw_radians) * speed);
            game.camera_z += (int)(sin(yaw_radians) * speed);
            camera_moved = 1;
        }

        if( q_pressed )
        {
            // game.fake_pitch = (game.fake_pitch + 10) % 2048;
        }

        if( e_pressed )
        {
            // game.fake_pitch = (game.fake_pitch - 10 + 2048) % 2048;
        }

        int angle_delta = 20;
        if( up_pressed )
        {
            game.camera_pitch = (game.camera_pitch + angle_delta) % 2048;
            camera_moved = 1;
        }

        // Yaw is CCW around the Y axis.
        // Pitch is CCW around the X axis.
        if( left_pressed )
        {
            game.camera_yaw = (game.camera_yaw + angle_delta) % 2048;
            camera_moved = 1;
        }

        if( right_pressed )
        {
            game.camera_yaw = (game.camera_yaw - angle_delta + 2048) % 2048;
            camera_moved = 1;
        }

        if( down_pressed )
        {
            game.camera_pitch = (game.camera_pitch - angle_delta + 2048) % 2048;
            camera_moved = 1;
        }

        if( f_pressed ) // Move down (decrease Y - fall)
        {
            game.camera_y += speed;
            camera_moved = 1;
        }

        if( r_pressed ) // Move up (increase Y - rise)
        {
            game.camera_y -= speed;
            camera_moved = 1;
        }

        if( i_pressed )
        {
            // game.player_tile_y += 1;
        }
        if( k_pressed )
        {
            // game.player_tile_y -= 1;
        }
        if( l_pressed )
        {
            // game.player_tile_x += 1;
            // game.show_loc_x += 1;
            // printf("Show loc: %d, %d\n", game.show_loc_x, game.show_loc_y);
        }
        if( j_pressed )
        {
            // game.player_tile_x -= 1;

            // game.show_loc_x -= 1;
            // printf("Show loc: %d, %d\n", game.show_loc_x, game.show_loc_y);
            // int loc_id =
            //     game.scene->grid_tiles[MAP_TILE_COORD(game.show_loc_x, game.show_loc_y,
            //     0)].locs[0];
            // if( loc_id != 0 )
            // {
            //     // struct SceneLoc* loc = &game.scene->locs->locs[loc_id];
            //     // printf(
            //     //     "Loc: %s, %d, %d\n",
            //     //     loc->__loc.name,
            //     //     loc->__loc._file_id,
            //     //     loc->model_ids ? loc->model_ids[0] : -1);
            // }
        }

        if( camera_moved )
        {
            memset(platform.pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
        }

        if( space_pressed )
        {
            // if( game.ops )
            //     free(game.ops);
            // game.ops = render_scene_compute_ops(
            //     game.camera_x, game.camera_y, game.camera_z, game.scene, &game.op_count);
            // memset(platform.pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
            // game.max_render_ops = 1;
            // game.manual_render_ops = 0;
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

        // Render frame
        game_render_sdl2(&game, &platform);
        game_render_imgui(&game, &platform);

        // Swap buffers after both 3D rendering and ImGui are complete
        SDL_GL_SwapWindow(platform.window);

        // Calculate frame time and sleep appropriately
        Uint32 frame_end_time = SDL_GetTicks();
        Uint32 frame_time = frame_end_time - frame_start_time;

        if( frame_time < target_frame_time )
        {
            // SDL_Delay(target_frame_time - frame_time);
        }

        last_frame_time = frame_end_time;
    }

    printf("Cleanup ImGui\n");
    // Cleanup ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    // Cleanup
    if( platform.gl_context )
    {
        SDL_GL_DeleteContext(platform.gl_context);
    }
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
#endif

    return 0;
}