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

// #define LUMBRIDGE_KITCHEN_TILE_1 14815
#define LUMBRIDGE_KITCHEN_TILE_1 14815
#define LUMBRIDGE_BRICK_WALL 638
#define CACHE_PATH "../cache"

// ImGui headers
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

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

int
pix3d_set_gamma(int rgb, double gamma)
{
    double r = (double)(rgb >> 16) / 256.0;
    double g = (double)(rgb >> 8 & 0xff) / 256.0;
    double b = (double)(rgb & 0xff) / 256.0;
    double powR = pow(r, gamma);
    double powG = pow(g, gamma);
    double powB = pow(b, gamma);
    int intR = (int)(powR * 256.0);
    int intG = (int)(powG * 256.0);
    int intB = (int)(powB * 256.0);
    return (intR << 16) + (intG << 8) + intB;
}

void
pix3d_set_brightness(int* palette, double brightness)
{
    double random_brightness = brightness;
    int offset = 0;
    for( int y = 0; y < 512; y++ )
    {
        double hue = (double)(y / 8) / 64.0 + 0.0078125;
        double saturation = (double)(y & 0x7) / 8.0 + 0.0625;
        for( int x = 0; x < 128; x++ )
        {
            double lightness = (double)x / 128.0;
            double r = lightness;
            double g = lightness;
            double b = lightness;
            if( saturation != 0.0 )
            {
                double q;
                if( lightness < 0.5 )
                {
                    q = lightness * (saturation + 1.0);
                }
                else
                {
                    q = lightness + saturation - lightness * saturation;
                }
                double p = lightness * 2.0 - q;
                double t = hue + 0.3333333333333333;
                if( t > 1.0 )
                {
                    t--;
                }
                double d11 = hue - 0.3333333333333333;
                if( d11 < 0.0 )
                {
                    d11++;
                }
                if( t * 6.0 < 1.0 )
                {
                    r = p + (q - p) * 6.0 * t;
                }
                else if( t * 2.0 < 1.0 )
                {
                    r = q;
                }
                else if( t * 3.0 < 2.0 )
                {
                    r = p + (q - p) * (0.6666666666666666 - t) * 6.0;
                }
                else
                {
                    r = p;
                }
                if( hue * 6.0 < 1.0 )
                {
                    g = p + (q - p) * 6.0 * hue;
                }
                else if( hue * 2.0 < 1.0 )
                {
                    g = q;
                }
                else if( hue * 3.0 < 2.0 )
                {
                    g = p + (q - p) * (0.6666666666666666 - hue) * 6.0;
                }
                else
                {
                    g = p;
                }
                if( d11 * 6.0 < 1.0 )
                {
                    b = p + (q - p) * 6.0 * d11;
                }
                else if( d11 * 2.0 < 1.0 )
                {
                    b = q;
                }
                else if( d11 * 3.0 < 2.0 )
                {
                    b = p + (q - p) * (0.6666666666666666 - d11) * 6.0;
                }
                else
                {
                    b = p;
                }
            }
            int intR = (int)(r * 256.0);
            int intG = (int)(g * 256.0);
            int intB = (int)(b * 256.0);
            int rgb = (intR << 16) + (intG << 8) + intB;
            int rgbAdjusted = pix3d_set_gamma(rgb, random_brightness);
            palette[offset++] = rgbAdjusted;
        }
    }
}

#include <stdio.h>

void
init_hsl16_to_rgb_table()
{
    // 0 and 128 are both black.
    pix3d_set_brightness(g_hsl16_to_rgb_table, 0.8);
}

void
init_sin_table()
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(sin((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_sin_table[i] = (int)(sin((double)i * 0.0030679615) * (1 << 16));
}

void
init_cos_table()
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(cos((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_cos_table[i] = (int)(cos((double)i * 0.0030679615) * (1 << 16));
}

void
init_tan_table()
{
    for( int i = 0; i < 2048; i++ )
        g_tan_table[i] = (int)(tan((double)i * 0.0030679615) * (1 << 16));
}

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
    // ImGui::Text("Mouse (x, y): %d, %d", game->mouse_x, game->mouse_y);

    ImGui::Text(
        "texture blerp8 lerp8: %.4f us, %.4f us",
        ((((double)_Pix3D.texture_blerp_sum) / _Pix3D.texture_count) * 1000000.0 /
         (double)frequency),
        ((((double)_Pix3D.texture_sum) / _Pix3D.texture_count) * 1000000.0 / (double)frequency));

    ImGui::Text(
        "S4, deob, bs4, bary_bs4: %.4f, %.4f, %.4f, %.4f us",
        ((((double)_Pix3D.s4_sum) / _Pix3D.s4_count) * 1000000.0 / (double)frequency),
        ((((double)_Pix3D.deob_sum) / _Pix3D.deob_count) * 1000000.0 / (double)frequency),
        ((((double)_Pix3D.bs4_sum) / _Pix3D.bs4_count) * 1000000.0 / (double)frequency),
        ((((double)_Pix3D.bary_bs4_sum) / _Pix3D.bary_bs4_count) * 1000000.0 / (double)frequency));

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
    ImGui::End();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), platform->renderer);
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

extern "C" {
#include "graphics/gouraud_branching.c"
#include "graphics/gouraud_branching_barycentric.c"
#include "graphics/gouraud_deob.c"
}

#include "graphics/gouraud.u.c"

static void
m_draw_face_gouraud(struct SceneModel* model, int face_index)
{
    int ax = _Pix3D.screen_vertices_x[model->model->face_indices_a[face_index]];
    int ay = _Pix3D.screen_vertices_y[model->model->face_indices_a[face_index]];

    int bx = _Pix3D.screen_vertices_x[model->model->face_indices_b[face_index]];
    int by = _Pix3D.screen_vertices_y[model->model->face_indices_b[face_index]];

    int cx = _Pix3D.screen_vertices_x[model->model->face_indices_c[face_index]];
    int cy = _Pix3D.screen_vertices_y[model->model->face_indices_c[face_index]];

    int color_a = model->lighting->face_colors_hsl_a[face_index];
    int color_b = model->lighting->face_colors_hsl_b[face_index];
    int color_c = model->lighting->face_colors_hsl_c[face_index];

    uint64_t deob_start = SDL_GetPerformanceCounter();
    gouraud_deob_draw_triangle(
        _Pix3D.pixel_buffer, ay, by, cy, ax, bx, cx, color_a, color_b, color_c);
    uint64_t deob_end = SDL_GetPerformanceCounter();
    _Pix3D.deob_sum += deob_end - deob_start;
    _Pix3D.deob_count++;

    uint64_t s4_start = SDL_GetPerformanceCounter();
    raster_gouraud_s4(
        _Pix3D.pixel_buffer, //
        _Pix3D.width,
        _Pix3D.height,
        ax,
        bx,
        cx,
        ay,
        by,
        cy,
        color_a,
        color_b,
        color_c);
    uint64_t s4_end = SDL_GetPerformanceCounter();
    _Pix3D.s4_sum += s4_end - s4_start;
    _Pix3D.s4_count++;

    uint64_t bs4_start = SDL_GetPerformanceCounter();
    raster_gouraud_bs4(
        _Pix3D.pixel_buffer, //
        _Pix3D.width,
        _Pix3D.height,
        ax,
        bx,
        cx,
        ay,
        by,
        cy,
        color_a,
        color_b,
        color_c);
    uint64_t bs4_end = SDL_GetPerformanceCounter();
    _Pix3D.bs4_sum += bs4_end - bs4_start;
    _Pix3D.bs4_count++;

    uint64_t bary_bs4_start = SDL_GetPerformanceCounter();
    raster_gouraud_bary_bs4(
        _Pix3D.pixel_buffer, //
        _Pix3D.width,
        _Pix3D.height,
        ax,
        bx,
        cx,
        ay,
        by,
        cy,
        color_a,
        color_b,
        color_c);
    uint64_t bary_bs4_end = SDL_GetPerformanceCounter();
    _Pix3D.bary_bs4_sum += bary_bs4_end - bary_bs4_start;
    _Pix3D.bary_bs4_count++;
}

static inline void
raster_gouraud_bench(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int ax,
    int bx,
    int cx,
    int ay,
    int by,
    int cy,
    int color_a,
    int color_b,
    int color_c,
    int alpha)
{
    if( alpha == 0xFF )
    {
        uint64_t deob_start = SDL_GetPerformanceCounter();
        gouraud_deob_draw_triangle(
            _Pix3D.pixel_buffer, ay, by, cy, ax, bx, cx, color_a, color_b, color_c);
        uint64_t deob_end = SDL_GetPerformanceCounter();
        _Pix3D.deob_sum += deob_end - deob_start;
        _Pix3D.deob_count++;

        uint64_t s4_start = SDL_GetPerformanceCounter();
        raster_gouraud_s4(
            _Pix3D.pixel_buffer, //
            _Pix3D.width,
            _Pix3D.height,
            ax,
            bx,
            cx,
            ay,
            by,
            cy,
            color_a,
            color_b,
            color_c);
        uint64_t s4_end = SDL_GetPerformanceCounter();
        _Pix3D.s4_sum += s4_end - s4_start;
        _Pix3D.s4_count++;

        uint64_t bs4_start = SDL_GetPerformanceCounter();
        raster_gouraud_bs4(
            _Pix3D.pixel_buffer, //
            _Pix3D.width,
            _Pix3D.height,
            ax,
            bx,
            cx,
            ay,
            by,
            cy,
            color_a,
            color_b,
            color_c);
        uint64_t bs4_end = SDL_GetPerformanceCounter();
        _Pix3D.bs4_sum += bs4_end - bs4_start;
        _Pix3D.bs4_count++;

        uint64_t bary_bs4_start = SDL_GetPerformanceCounter();
        raster_gouraud_bary_bs4(
            _Pix3D.pixel_buffer, //
            _Pix3D.width,
            _Pix3D.height,
            ax,
            bx,
            cx,
            ay,
            by,
            cy,
            color_a,
            color_b,
            color_c);
        uint64_t bary_bs4_end = SDL_GetPerformanceCounter();
        _Pix3D.bary_bs4_sum += bary_bs4_end - bary_bs4_start;
        _Pix3D.bary_bs4_count++;
    }
    else
    {
        // raster_gouraud_alpha_s4(
        //     pixel_buffer,
        //     screen_width,
        //     screen_height,
        //     x1,
        //     x2,
        //     x3,
        //     y1,
        //     y2,
        //     y3,
        //     color_a,
        //     color_b,
        //     color_c,
        //     alpha);
    }
}

extern "C" {
#include "graphics/texture.u.c"
#include "graphics/texture_blend_branching.u.c"
}

static inline void
raster_texture_bench(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
    int face,
    int tp_vertex,
    int tm_vertex,
    int tn_vertex,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int* orthographic_vertices_x,
    int* orthographic_vertices_y,
    int* orthographic_vertices_z,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int* texels,
    int texture_size,
    int texture_opaque,
    int near_plane_z,
    int offset_x,
    int offset_y)
{
    int x1 = screen_vertices_x[face_indices_a[face]];
    int x2 = screen_vertices_x[face_indices_b[face]];
    int x3 = screen_vertices_x[face_indices_c[face]];

    int y1 = screen_vertices_y[face_indices_a[face]];
    int y2 = screen_vertices_y[face_indices_b[face]];
    int y3 = screen_vertices_y[face_indices_c[face]];

    int orthographic_x0 = orthographic_vertices_x[tp_vertex];
    int orthographic_y0 = orthographic_vertices_y[tp_vertex];
    int orthographic_z0 = orthographic_vertices_z[tp_vertex];
    int orthographic_x1 = orthographic_vertices_x[tm_vertex];
    int orthographic_y1 = orthographic_vertices_y[tm_vertex];
    int orthographic_z1 = orthographic_vertices_z[tm_vertex];
    int orthographic_x2 = orthographic_vertices_x[tn_vertex];
    int orthographic_y2 = orthographic_vertices_y[tn_vertex];
    int orthographic_z2 = orthographic_vertices_z[tn_vertex];

    int shade_a = colors_a[face];
    int shade_b = colors_b[face];
    int shade_c = colors_c[face];

    assert(shade_a >= 0 && shade_a < 0xFF);
    assert(shade_b >= 0 && shade_b < 0xFF);
    assert(shade_c >= 0 && shade_c < 0xFF);
    _Pix3D.texture_count++;
    //

    int texture_start = SDL_GetPerformanceCounter();

    raster_texture_opaque_blend_lerp8(
        pixel_buffer,
        screen_width,
        screen_height,
        camera_fov,
        x1,
        x2,
        x3,
        y1,
        y2,
        y3,
        orthographic_x0,
        orthographic_x1,
        orthographic_x2,
        orthographic_y0,
        orthographic_y1,
        orthographic_y2,
        orthographic_z0,
        orthographic_z1,
        orthographic_z2,
        shade_a,
        shade_b,
        shade_c,
        texels,
        texture_size);

    int texture_end = SDL_GetPerformanceCounter();
    _Pix3D.texture_sum += texture_end - texture_start;

    int texture_blerp_start = SDL_GetPerformanceCounter();

    raster_texture_opaque_blend_blerp8(
        pixel_buffer,
        screen_width,
        screen_height,
        camera_fov,
        x1,
        x2,
        x3,
        y1,
        y2,
        y3,
        orthographic_x0,
        orthographic_x1,
        orthographic_x2,
        orthographic_y0,
        orthographic_y1,
        orthographic_y2,
        orthographic_z0,
        orthographic_z1,
        orthographic_z2,
        shade_a,
        shade_b,
        shade_c,
        texels,
        texture_size);

    int texture_blerp_end = SDL_GetPerformanceCounter();
    _Pix3D.texture_blerp_sum += texture_blerp_end - texture_blerp_start;
}

void
m_draw_face(
    int* pixel_buffer,
    int face,
    int* face_infos,
    int* face_indices_a,
    int* face_indices_b,
    int* face_indices_c,
    int num_faces,
    int* vertex_x,
    int* vertex_y,
    int* vertex_z,
    int* orthographic_vertex_x_nullable,
    int* orthographic_vertex_y_nullable,
    int* orthographic_vertex_z_nullable,
    int num_vertices,
    int* face_textures,
    int* face_texture_coords,
    int face_texture_coords_length,
    int* face_p_coordinate_nullable,
    int* face_m_coordinate_nullable,
    int* face_n_coordinate_nullable,
    int num_textured_faces,
    int* colors_a,
    int* colors_b,
    int* colors_c,
    int* face_alphas_nullable,
    int offset_x,
    int offset_y,
    int near_plane_z,
    int screen_width,
    int screen_height,
    int camera_fov,
    struct TexturesCache* textures_cache)
{
    struct Texture* texture = NULL;

    // TODO: FaceTYpe is wrong, type 2 is hidden, 3 is black, 0 is gouraud, 1 is flat.
    int type = face_infos ? (face_infos[face] & 0x3) : 0;
    if( type == 2 )
    {
        return;
    }
    assert(type >= 0 && type <= 3);

    int color_a = colors_a[face];
    int color_b = colors_b[face];
    int color_c = colors_c[face];

    if( color_c == -2 )
    {
        return;
    }

    int tp_vertex = -1;
    int tm_vertex = -1;
    int tn_vertex = -1;

    int tp_x = -1;
    int tp_y = -1;
    int tp_z = -1;
    int tm_x = -1;
    int tm_y = -1;
    int tm_z = -1;
    int tn_x = -1;
    int tn_y = -1;
    int tn_z = -1;

    int texture_id = -1;
    int texture_face = -1;

    assert(face < num_faces);

    int alpha = face_alphas_nullable ? (face_alphas_nullable[face]) : 0xFF;

    // Faces with color_c == -2 are not drawn. As far as I can tell, these faces are used for
    // texture PNM coordinates that do not coincide with a visible face.
    // /Users/matthewevers/Documents/git_repos/OS1/src/main/java/jagex3/dash3d/ModelUnlit.java
    // OS1 skips faces with -2.
    if( color_c == -2 )
    {
        // TODO: How to organize this.
        // See here
        // /Users/matthewevers/Documents/git_repos/rs-map-viewer/src/rs/model/ModelData.ts
        // .light

        // and
        // /Users/matthewevers/Documents/git_repos/rs-map-viewer/src/mapviewer/webgl/buffer/SceneBuffer.ts
        // getModelFaces
        return;
        // color_c = 0;
    }

    // TODO: See above comments. alpha overrides colors.
    // if( face_alphas_nullable && face_alphas_nullable[index] < 0 )
    // {
    //     return;
    // }

    int* texels = NULL;
    int texture_size = 0;
    int texture_opaque = true;
    if( face_textures && face_textures[face] != -1 )
    {
        texture_id = face_textures[face];
        // gamma 0.8 is the default in os1
        texture = textures_cache_checkout(textures_cache, NULL, texture_id, 128, 0.8);
        assert(texture != NULL);
        texels = texture->texels;
        texture_size = texture->width;
        texture_opaque = texture->opaque;

        if( color_c == -1 )
            goto textured_flat;
        else
            goto textured;
    }
    else
    {
        // Alpha is a signed byte, but for non-textured
        // faces, we treat it as unsigned.
        // -1 and -2 are reserved. See lighting code.
        if( face_alphas_nullable )
            alpha = 0xFF - (alpha & 0xff);

        if( color_c == -1 )
        {
            type = 1;
        }
        else
        {
            type = 0;
        }
        switch( type )
        {
        case 0:

            // raster_gouraud_bench(
            //     pixel_buffer,
            //     screen_width,
            //     screen_height,
            //     vertex_x[face_indices_a[face]],
            //     vertex_x[face_indices_b[face]],
            //     vertex_x[face_indices_c[face]],
            //     vertex_y[face_indices_a[face]],
            //     vertex_y[face_indices_b[face]],
            //     vertex_y[face_indices_c[face]],
            //     color_a,
            //     color_b,
            //     color_c,
            //     alpha);

            break;
        case 1:
            // Skip triangle if any vertex was clipped

            // raster_face_flat(
            //     pixel_buffer,
            //     face,
            //     face_indices_a,
            //     face_indices_b,
            //     face_indices_c,
            //     vertex_x,
            //     vertex_y,
            //     vertex_z,
            //     orthographic_vertex_x_nullable,
            //     orthographic_vertex_y_nullable,
            //     orthographic_vertex_z_nullable,
            //     colors_a,
            //     face_alphas_nullable,
            //     near_plane_z,
            //     offset_x,
            //     offset_y,
            //     screen_width,
            //     screen_height);

            break;
        case 2:
        textured:;
            assert(face_p_coordinate_nullable != NULL);
            assert(face_m_coordinate_nullable != NULL);
            assert(face_n_coordinate_nullable != NULL);
            assert(orthographic_vertex_x_nullable != NULL);
            assert(orthographic_vertex_y_nullable != NULL);
            assert(orthographic_vertex_z_nullable != NULL);

            if( face_texture_coords && face_texture_coords[face] != -1 )
            {
                texture_face = face_texture_coords[face];

                tp_vertex = face_p_coordinate_nullable[texture_face];
                tm_vertex = face_m_coordinate_nullable[texture_face];
                tn_vertex = face_n_coordinate_nullable[texture_face];
            }
            else
            {
                texture_face = face;
                tp_vertex = face_indices_a[texture_face];
                tm_vertex = face_indices_b[texture_face];
                tn_vertex = face_indices_c[texture_face];
            }
            // texture_id = face_textures[index];
            // texture_face = face_infos[index] >> 2;
            // texture_face = face_texture_coords[index];

            assert(tp_vertex > -1);
            assert(tm_vertex > -1);
            assert(tn_vertex > -1);

            assert(tp_vertex < num_vertices);
            assert(tm_vertex < num_vertices);
            assert(tn_vertex < num_vertices);

            raster_texture_bench(
                pixel_buffer,
                screen_width,
                screen_height,
                camera_fov,
                face,
                tp_vertex,
                tm_vertex,
                tn_vertex,
                face_indices_a,
                face_indices_b,
                face_indices_c,
                vertex_x,
                vertex_y,
                vertex_z,
                orthographic_vertex_x_nullable,
                orthographic_vertex_y_nullable,
                orthographic_vertex_z_nullable,
                colors_a,
                colors_b,
                colors_c,
                texels,
                texture_size,
                texture_opaque,
                near_plane_z,
                offset_x,
                offset_y);
            // raster_face_texture_blend(
            //     pixel_buffer,
            //     screen_width,
            //     screen_height,
            //     camera_fov,
            //     face,
            //     tp_vertex,
            //     tm_vertex,
            //     tn_vertex,
            //     face_indices_a,
            //     face_indices_b,
            //     face_indices_c,
            //     vertex_x,
            //     vertex_y,
            //     vertex_z,
            //     orthographic_vertex_x_nullable,
            //     orthographic_vertex_y_nullable,
            //     orthographic_vertex_z_nullable,
            //     colors_a,
            //     colors_b,
            //     colors_c,
            //     texels,
            //     texture_size,
            //     texture_opaque,
            //     near_plane_z,
            //     offset_x,
            //     offset_y);

            // tp_x = orthographic_vertex_x_nullable[tp_face];
            // tp_y = orthographic_vertex_y_nullable[tp_face];
            // tp_z = orthographic_vertex_z_nullable[tp_face];

            // tm_x = orthographic_vertex_x_nullable[tm_face];
            // tm_y = orthographic_vertex_y_nullable[tm_face];
            // tm_z = orthographic_vertex_z_nullable[tm_face];

            // tn_x = orthographic_vertex_x_nullable[tn_face];
            // tn_y = orthographic_vertex_y_nullable[tn_face];
            // tn_z = orthographic_vertex_z_nullable[tn_face];

            // if( texture->opaque )
            // {
            //     raster_texture_opaque_blend_lerp8(
            //         pixel_buffer,
            //         screen_width,
            //         screen_height,
            //         x1,
            //         x2,
            //         x3,
            //         y1,
            //         y2,
            //         y3,
            //         tp_x,
            //         tm_x,
            //         tn_x,
            //         tp_y,
            //         tm_y,
            //         tn_y,
            //         tp_z,
            //         tm_z,
            //         tn_z,
            //         color_a,
            //         color_b,
            //         color_c,
            //         texels,
            //         128);
            // }
            // else
            // {
            //     raster_texture_transparent_blend_lerp8(
            //         pixel_buffer,
            //         screen_width,
            //         screen_height,
            //         x1,
            //         x2,
            //         x3,
            //         y1,
            //         y2,
            //         y3,
            //         tp_x,
            //         tm_x,
            //         tn_x,
            //         tp_y,
            //         tm_y,
            //         tn_y,
            //         tp_z,
            //         tm_z,
            //         tn_z,
            //         color_a,
            //         color_b,
            //         color_c,
            //         texels,
            //         128);
            // }

            // This will draw a white triangle over the projected texture pnm coords.

            // raster_flat(
            //     pixel_buffer,
            //     screen_width,
            //     screen_height,
            //     tex_x1,
            //     tex_x2,
            //     tex_x3,
            //     tex_y1,
            //     tex_y2,
            //     tex_y3,
            //     color_a);
            break;
        case 3:
        textured_flat:;
            assert(face_p_coordinate_nullable != NULL);
            assert(face_m_coordinate_nullable != NULL);
            assert(face_n_coordinate_nullable != NULL);
            assert(orthographic_vertex_x_nullable != NULL);
            assert(orthographic_vertex_y_nullable != NULL);
            assert(orthographic_vertex_z_nullable != NULL);

            if( face_texture_coords && face_texture_coords[face] != -1 )
            {
                texture_face = face_texture_coords[face];

                tp_vertex = face_p_coordinate_nullable[texture_face];
                tm_vertex = face_m_coordinate_nullable[texture_face];
                tn_vertex = face_n_coordinate_nullable[texture_face];
            }
            else
            {
                texture_face = face;
                tp_vertex = face_indices_a[texture_face];
                tm_vertex = face_indices_b[texture_face];
                tn_vertex = face_indices_c[texture_face];
            }
            // texture_id = face_textures[index];
            // texture_face = face_infos[index] >> 2;
            // texture_face = face_texture_coords[index];

            assert(tp_vertex > -1);
            assert(tm_vertex > -1);
            assert(tn_vertex > -1);

            assert(tp_vertex < num_vertices);
            assert(tm_vertex < num_vertices);
            assert(tn_vertex < num_vertices);

            // raster_face_texture_flat(
            //     pixel_buffer,
            //     screen_width,
            //     screen_height,
            //     camera_fov,
            //     face,
            //     tp_vertex,
            //     tm_vertex,
            //     tn_vertex,
            //     face_indices_a,
            //     face_indices_b,
            //     face_indices_c,
            //     vertex_x,
            //     vertex_y,
            //     vertex_z,
            //     orthographic_vertex_x_nullable,
            //     orthographic_vertex_y_nullable,
            //     orthographic_vertex_z_nullable,
            //     colors_a,
            //     texels,
            //     texture_size,
            //     texture_opaque,
            //     near_plane_z,
            //     offset_x,
            //     offset_y);

            // tp_x = orthographic_vertex_x_nullable[tp_face];
            // tp_y = orthographic_vertex_y_nullable[tp_face];
            // tp_z = orthographic_vertex_z_nullable[tp_face];

            // tm_x = orthographic_vertex_x_nullable[tm_face];
            // tm_y = orthographic_vertex_y_nullable[tm_face];
            // tm_z = orthographic_vertex_z_nullable[tm_face];

            // tn_x = orthographic_vertex_x_nullable[tn_face];
            // tn_y = orthographic_vertex_y_nullable[tn_face];
            // tn_z = orthographic_vertex_z_nullable[tn_face];

            // if( texture->opaque )
            // {
            //     raster_texture_opaque_lerp8(
            //         pixel_buffer,
            //         screen_width,
            //         screen_height,
            //         x1,
            //         x2,
            //         x3,
            //         y1,
            //         y2,
            //         y3,
            //         tp_x,
            //         tm_x,
            //         tn_x,
            //         tp_y,
            //         tm_y,
            //         tn_y,
            //         tp_z,
            //         tm_z,
            //         tn_z,
            //         color_a,
            //         texels,
            //         128);
            // }
            // else
            // {
            //     raster_texture_transparent_lerp8(
            //         pixel_buffer,
            //         screen_width,
            //         screen_height,
            //         x1,
            //         x2,
            //         x3,
            //         y1,
            //         y2,
            //         y3,
            //         tp_x,
            //         tm_x,
            //         tn_x,
            //         tp_y,
            //         tm_y,
            //         tn_y,
            //         tp_z,
            //         tm_z,
            //         tn_z,
            //         color_a,
            //         texels,
            //         128);
            // }

            break;
        }
    }
}

static void
m_paint(struct SceneModel* model, struct Cache* cache, struct TexturesCache* textures_cache)
{
    struct BoundsCylinder* bounds_cylinder = model->bounds_cylinder;

    if( bounds_cylinder->min_z_depth_any_rotation > 1600 )
        return;

    int max_depth = bounds_cylinder->min_z_depth_any_rotation * 2 + 1;
    memset(_Pix3D.depth_to_face_count, 0, max_depth * sizeof(_Pix3D.depth_to_face_count[0]));

    for( int face_index = 0; face_index < model->model->face_count; face_index++ )
    {
        int vertex_a_index = model->model->face_indices_a[face_index];
        int vertex_b_index = model->model->face_indices_b[face_index];
        int vertex_c_index = model->model->face_indices_c[face_index];

        int ax = _Pix3D.screen_vertices_x[vertex_a_index];
        int bx = _Pix3D.screen_vertices_x[vertex_b_index];
        int cx = _Pix3D.screen_vertices_x[vertex_c_index];

        int ay = _Pix3D.screen_vertices_y[vertex_a_index];
        int by = _Pix3D.screen_vertices_y[vertex_b_index];
        int cy = _Pix3D.screen_vertices_y[vertex_c_index];

        if( ax == -5000 || bx == -5000 || cx == -5000 )
            continue;

        int dot_product = (ax - bx) * (cy - by) - (ay - by) * (cx - bx);
        if( dot_product < 0 )
            continue;

        int depth =
            (model->model->vertices_z[vertex_a_index] + model->model->vertices_z[vertex_b_index] +
             model->model->vertices_z[vertex_c_index]) /
                3 +
            bounds_cylinder->min_z_depth_any_rotation;

        if( depth < 0 || depth >= max_depth )
            continue;

        int index_in_depth_bucket = _Pix3D.depth_to_face_count[depth]++;

        _Pix3D.depth_to_face_buckets[depth][index_in_depth_bucket] = face_index;
    }

    for( int depth = max_depth - 1; depth >= 0; depth-- )
    {
        int count = _Pix3D.depth_to_face_count[depth];
        if( count == 0 )
            continue;

        int* face_indices = _Pix3D.depth_to_face_buckets[depth];

        for( int i = 0; i < count; i++ )
        {
            int face_index = face_indices[i];

            m_draw_face(
                _Pix3D.pixel_buffer,
                face_index,
                model->model->face_infos,
                model->model->face_indices_a,
                model->model->face_indices_b,
                model->model->face_indices_c,
                model->model->face_count,
                _Pix3D.screen_vertices_x,
                _Pix3D.screen_vertices_y,
                _Pix3D.screen_vertices_z,
                _Pix3D.ortho_vertices_x,
                _Pix3D.ortho_vertices_y,
                _Pix3D.ortho_vertices_z,
                model->model->vertex_count,
                model->model->face_textures,
                model->model->face_texture_coords,
                model->model->textured_face_count,
                model->model->textured_p_coordinate,
                model->model->textured_m_coordinate,
                model->model->textured_n_coordinate,
                model->model->textured_face_count,
                model->lighting->face_colors_hsl_a,
                model->lighting->face_colors_hsl_b,
                model->lighting->face_colors_hsl_c,
                model->model->face_alphas,
                SCREEN_WIDTH / 2,
                SCREEN_HEIGHT / 2,
                50,
                SCREEN_WIDTH,
                SCREEN_HEIGHT,
                512,
                textures_cache);

            // m_draw_face(model, texel_buffer, texture_opaque, face_index);
        }
    }
}

extern "C" {
#include "../graphics/projection.h"
}

static void
m_draw(
    struct SceneModel* model,
    int model_yaw,
    int pitch,
    int yaw,
    int scene_x,
    int scene_y,
    int scene_z,
    struct Cache* cache,
    struct TexturesCache* textures_cache)
{
    if( !model->bounds_cylinder )
    {
        model->bounds_cylinder = (struct BoundsCylinder*)malloc(sizeof(struct BoundsCylinder));
        *model->bounds_cylinder = calculate_bounds_cylinder(
            model->model->vertex_count,
            model->model->vertices_x,
            model->model->vertices_y,
            model->model->vertices_z);
    }

    struct BoundsCylinder* bounds_cylinder = model->bounds_cylinder;

    int pitch_sin = g_sin_table[pitch];
    int pitch_cos = g_cos_table[pitch];
    int yaw_sin = g_sin_table[yaw];
    int yaw_cos = g_cos_table[yaw];

    int z_center_projected_yaw = yaw_cos * scene_z - yaw_sin * scene_x;
    z_center_projected_yaw >>= 16;
    int z_center_projected_pitch_yaw = pitch_sin * scene_y + pitch_cos * z_center_projected_yaw;
    z_center_projected_pitch_yaw >>= 16;

    int cylinder_radius_projected = bounds_cylinder->radius * pitch_cos >> 16;
    int distance_to_camera = cylinder_radius_projected + z_center_projected_pitch_yaw;
    if( distance_to_camera < 50 || z_center_projected_pitch_yaw > 3500 )
        return;

    // Only check left and right yaw to see if offscreen
    int x_center_projected_yaw = (yaw_sin * scene_z + yaw_cos * scene_x) >> 16;
    int x_center_screen_min = (x_center_projected_yaw - bounds_cylinder->radius) << 9;
    if( x_center_screen_min / distance_to_camera >= _Pix3D.center_x )
        return;

    int x_center_screen_max = (x_center_projected_yaw + bounds_cylinder->radius) << 9;
    if( x_center_screen_max / distance_to_camera <= -_Pix3D.center_x )
        return;

    // TODO: Y

    int model_yaw_sin = g_sin_table[model_yaw];
    int model_yaw_cos = g_cos_table[model_yaw];

    _Pix3D.clipped = false;

    for( int i = 0; i < model->model->vertex_count; i++ )
    {
        // struct ProjectedTriangle projected_triangle = { 0 };
        // project_orthographic_fast(
        //     &projected_triangle,
        //     model->model->vertices_x[i],
        //     model->model->vertices_y[i],
        //     model->model->vertices_z[i],
        //     0,
        //     scene_x,
        //     scene_y,
        //     scene_z,
        //     pitch,
        //     yaw);

        int x = model->model->vertices_x[i];
        int y = model->model->vertices_y[i];
        int z = model->model->vertices_z[i];

        if( model_yaw != 0 )
        {
            int x_projected = (model_yaw_sin * z + model_yaw_cos * x) >> 16;
            int z_projected = (model_yaw_cos * z - model_yaw_sin * x_projected) >> 16;

            x = x_projected;
            z = z_projected;
        }

        x += scene_x;
        y += scene_y;
        z += scene_z;

        int x_scene_rotated = (yaw_sin * z + yaw_cos * x) >> 16;
        int z_scene_rotated_yaw = (yaw_cos * z - yaw_sin * x) >> 16;

        int y_scene_rotated = (pitch_cos * y - pitch_sin * z_scene_rotated_yaw) >> 16;
        int z_scene_rotated_pitch_yaw = (pitch_sin * y + pitch_cos * z_scene_rotated_yaw) >> 16;

        x = x_scene_rotated;
        y = y_scene_rotated;
        z = z_scene_rotated_pitch_yaw;

        _Pix3D.screen_vertices_z[i] = z_scene_rotated_pitch_yaw - z_center_projected_pitch_yaw;
        if( z_scene_rotated_pitch_yaw >= 50 )
        {
            _Pix3D.screen_vertices_x[i] = (x << 9) / z_scene_rotated_pitch_yaw + _Pix3D.center_x;
            _Pix3D.screen_vertices_y[i] = (y << 9) / z_scene_rotated_pitch_yaw + _Pix3D.center_y;

            // struct ProjectedTriangle projected_triangle = { 0 };
            // project_fast(
            //     &projected_triangle,
            //     x,
            //     y,
            //     z,
            //     0,
            //     scene_x,
            //     scene_y,
            //     scene_z,
            //     pitch,
            //     yaw,
            //     512,
            //     50,
            //     _Pix3D.width,
            //     _Pix3D.height);

            // _Pix3D.screen_vertices_x[i] = projected_triangle.x + _Pix3D.center_x;
            // _Pix3D.screen_vertices_y[i] = projected_triangle.y + _Pix3D.center_y;
        }
        else
        {
            _Pix3D.screen_vertices_x[i] = -5000;
            _Pix3D.clipped = true;
        }

        if( model->model->textured_face_count > 0 )
        {
            _Pix3D.ortho_vertices_x[i] = x_scene_rotated;
            _Pix3D.ortho_vertices_y[i] = y_scene_rotated;
            _Pix3D.ortho_vertices_z[i] = z_scene_rotated_pitch_yaw;
        }
    }

    m_paint(model, cache, textures_cache);
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
    int x = camera_x + model->region_x;
    int y = camera_y + model->region_z;
    int z = camera_z + model->region_height;

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
    yaw &= 0x7FF;

    x += model->offset_x;
    y += model->offset_z;
    z += model->offset_height;

    if( model->model == NULL )
        return;

    struct AABB aabb;

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
        &aabb,
        model->model,
        model->lighting,
        model->bounds_cylinder,
        NULL,
        NULL,
        NULL,
        textures_cache);
}

static void
game_render_sdl2(struct Game* game, struct PlatformSDL2* platform)
{
    SDL_Texture* texture = platform->texture;
    SDL_Renderer* renderer = platform->renderer;
    int* pixel_buffer = platform->pixel_buffer;

    // Get the frequency (ticks per second)
    Uint64 frequency = SDL_GetPerformanceFrequency();

    memset(pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

    Uint64 start_ticks = SDL_GetPerformanceCounter();

    for( int x = 0; x < 4; x++ )
    {
        for( int z = 3; z >= 0; z-- )
        {
            int model_x = game->camera_x + x * 128;
            int model_y = game->camera_y + 200 + z * 128;
            int model_z = game->camera_z + 100;

            m_draw(
                game->scene_model,
                0,
                game->camera_pitch,
                game->camera_yaw,
                model_x,
                model_z,
                model_y,
                game->cache,
                game->textures_cache);

            // int model_x = game->camera_x + x * 128;
            // int model_y = game->camera_y + 200 + z * 128;
            // int model_z = game->camera_z + 100;

            // render_scene_model(
            //     pixel_buffer,
            //     SCREEN_WIDTH,
            //     SCREEN_HEIGHT,
            //     // Had to use 100 here because of the scale, near plane z was resulting in
            //     // extremely close to the camera.
            //     50,
            //     0,
            //     model_x,
            //     model_y,
            //     model_z,
            //     game->camera_pitch,
            //     game->camera_yaw,
            //     game->camera_roll,
            //     game->camera_fov,
            //     game->scene_model,
            //     game->textures_cache);
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

#include <iostream>

int
main(int argc, char* argv[])
{
    std::cout << "SDL_main" << std::endl;
    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();

    printf("Loading XTEA keys from: ../cache/xteas.json\n");
    int xtea_keys_count = xtea_config_load_keys("../cache/xteas.json");
    if( xtea_keys_count == -1 )
    {
        printf("Failed to load xtea keys from: ../cache/xteas.json\n");
        printf("Make sure the xteas.json file exists in the cache directory\n");
        return 0;
    }
    printf("Loaded %d XTEA keys successfully\n", xtea_keys_count);

    printf("Loading cache from directory: %s\n", CACHE_PATH);
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
    printf("Cache loaded successfully\n");

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

    game.camera_pitch = 80;
    game.camera_yaw = 1888;
    game.camera_roll = 0;
    game.camera_fov = 512;

    game.camera_pitch = 0;
    game.camera_yaw = 0;
    // Camera (x, y, z): -203, 627, 200 : -1, 4
    game.camera_x = -203;
    game.camera_y = 627;
    game.camera_z = 200;
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
    // game.model_id = LUMBRIDGE_KITCHEN_TILE_1;
    game.model_id = LUMBRIDGE_BRICK_WALL;

    game.textures_cache = textures_cache_new(cache);
    game.cache = cache;

    struct ModelCache* model_cache = model_cache_new();

    {
        struct SceneModel* model = (struct SceneModel*)malloc(sizeof(struct SceneModel));
        memset(model, 0, sizeof(struct SceneModel));

        model->model = model_cache_checkout(model_cache, cache, game.model_id);
        struct CacheModel* cache_model = model->model;

        struct ModelNormals* normals = (struct ModelNormals*)malloc(sizeof(struct ModelNormals));
        memset(normals, 0, sizeof(struct ModelNormals));

        normals->lighting_vertex_normals = (struct LightingNormal*)malloc(
            sizeof(struct LightingNormal) * cache_model->vertex_count);
        memset(
            normals->lighting_vertex_normals,
            0,
            sizeof(struct LightingNormal) * cache_model->vertex_count);
        normals->lighting_face_normals =
            (struct LightingNormal*)malloc(sizeof(struct LightingNormal) * cache_model->face_count);
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

        model->normals = normals;

        struct ModelLighting* lighting =
            (struct ModelLighting*)malloc(sizeof(struct ModelLighting));
        memset(lighting, 0, sizeof(struct ModelLighting));
        lighting->face_colors_hsl_a = (int*)malloc(sizeof(int) * model->model->face_count);
        memset(lighting->face_colors_hsl_a, 0, sizeof(int) * model->model->face_count);
        lighting->face_colors_hsl_b = (int*)malloc(sizeof(int) * model->model->face_count);
        memset(lighting->face_colors_hsl_b, 0, sizeof(int) * model->model->face_count);
        lighting->face_colors_hsl_c = (int*)malloc(sizeof(int) * model->model->face_count);
        memset(lighting->face_colors_hsl_c, 0, sizeof(int) * model->model->face_count);

        model->lighting = lighting;

        int light_ambient = 64;
        int light_attenuation = 768;
        int lightsrc_x = -50;
        int lightsrc_y = -10;
        int lightsrc_z = -50;

        light_ambient += model->light_ambient;
        // 2004Scape multiplies contrast by 5.
        // Later versions do not.
        light_attenuation += model->light_contrast;

        int light_magnitude =
            (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
        int attenuation = (light_attenuation * light_magnitude) >> 8;

        apply_lighting(
            lighting->face_colors_hsl_a,
            lighting->face_colors_hsl_b,
            lighting->face_colors_hsl_c,
            model->aliased_lighting_normals
                ? model->aliased_lighting_normals->lighting_vertex_normals
                : model->normals->lighting_vertex_normals,
            model->normals->lighting_face_normals,
            model->model->face_indices_a,
            model->model->face_indices_b,
            model->model->face_indices_c,
            model->model->face_count,
            model->model->face_colors,
            model->model->face_alphas,
            model->model->face_textures,
            model->model->face_infos,
            light_ambient,
            attenuation,
            lightsrc_x,
            lightsrc_y,
            lightsrc_z);

        model->lighting = lighting;

        game.scene_model = model;
    }

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
                // case SDLK_q:
                //     // Counter Clockwise
                //     game.hover_loc_yaw += 512 * 3;
                //     game.hover_loc_yaw %= 2048;
                //     break;
                // case SDLK_e:
                //     // Clockwise
                //     game.hover_loc_yaw += 512;
                //     game.hover_loc_yaw %= 2048;
                //     break;
                case SDLK_SEMICOLON:
                    // game.show_loc_enabled = !game.show_loc_enabled;
                    break;
                // case SDLK_i:
                //     game.camera_fov += 1;
                //     break;
                // case SDLK_k:
                //     game.camera_fov -= 1;
                //     break;
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
            game.camera_x += (g_sin_table[game.camera_yaw] * speed) >> 16;
            game.camera_y -= (g_cos_table[game.camera_yaw] * speed) >> 16;
            camera_moved = 1;
        }

        if( a_pressed )
        {
            game.camera_x += (g_cos_table[game.camera_yaw] * speed) >> 16;
            game.camera_y += (g_sin_table[game.camera_yaw] * speed) >> 16;
            camera_moved = 1;
        }

        if( s_pressed )
        {
            game.camera_x -= (g_sin_table[game.camera_yaw] * speed) >> 16;
            game.camera_y += (g_cos_table[game.camera_yaw] * speed) >> 16;
            camera_moved = 1;
        }

        if( d_pressed )
        {
            game.camera_x -= (g_cos_table[game.camera_yaw] * speed) >> 16;
            game.camera_y -= (g_sin_table[game.camera_yaw] * speed) >> 16;
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

        if( f_pressed )
        {
            game.camera_z -= speed;
            camera_moved = 1;
        }

        if( r_pressed )
        {
            game.camera_z += speed;
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

        SDL_RenderClear(platform.renderer);
        // Render frame
        game_render_sdl2(&game, &platform);
        game_render_imgui(&game, &platform);

        SDL_RenderPresent(platform.renderer);

        // Calculate frame time and sleep appropriately
        Uint32 frame_end_time = SDL_GetTicks();
        Uint32 frame_time = frame_end_time - frame_start_time;

        if( frame_time < target_frame_time )
        {
            // SDL_Delay(target_frame_time - frame_time);
        }

        last_frame_time = frame_end_time;
    }

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