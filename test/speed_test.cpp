#include "inc.c"
extern "C" {
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

#ifdef __APPLE__
#include <mach/clock.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

// ImGui headers
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

struct Game
{
    int model_id;

    struct CacheModel* model;
    struct Cache* cache;
    struct TexturesCache* textures_cache;
    struct SceneModel* scene_model;

    int camera_x;
    int camera_y;
    int camera_z;

    int camera_pitch;
    int camera_yaw;
    int camera_roll;

    int64_t scene_render_start;
    int64_t scene_render_end;
};

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
    platform->renderer = SDL_CreateRenderer(
        platform->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

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
        NULL,
        NULL,
        NULL,
        textures_cache);
}

static void
render_imgui(struct PlatformSDL2* platform, struct Game* game)
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

    mach_timebase_info_data_t info;

    mach_timebase_info(&info);

    int64_t elapsed = game->scene_render_end - game->scene_render_start;

    elapsed = (elapsed)*info.numer / info.denom;
    elapsed = elapsed / 1000;
    double seconds = ((double)elapsed) / 1000.0;
    ImGui::Text("Render Time: %.3f ms/frame", seconds);
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

static void
render_model(struct PlatformSDL2* platform, struct Game* game)
{
    memset(platform->pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
    int64_t start_time = mach_absolute_time();
    game->scene_render_start = start_time;
    render_scene_model(
        platform->pixel_buffer,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        50,
        0,
        game->camera_x,
        game->camera_z,
        game->camera_y,
        game->camera_pitch,
        game->camera_yaw,
        game->camera_roll,
        512,
        game->scene_model,
        game->textures_cache);
    game->scene_render_end = mach_absolute_time();

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        platform->pixel_buffer,
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
    if( SDL_LockTexture(platform->texture, NULL, (void**)&pix_write, &_pitch_unused) < 0 )
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
    SDL_UnlockTexture(platform->texture);

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

    SDL_RenderCopy(platform->renderer, platform->texture, NULL, &dst_rect);

    SDL_FreeSurface(surface);
}

int
main(int argc, char** argv)
{
    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();

    static const int oak_model_id = 1571;

    Game game = { 0 };
    game.camera_x = 0;
    game.camera_y = 0;
    game.camera_z = 1000;

    PlatformSDL2 platform = { 0 };
    platform_sdl2_init(&platform);

    struct Cache* cache = cache_new_from_directory(CACHE_PATH);

    struct CacheModel* model = model_new_from_cache(cache, oak_model_id);

    struct SceneModel scene_model = { 0 };
    scene_model.model = model;
    scene_model.light_ambient = 64;
    scene_model.light_contrast = 0;
    scene_model.region_x = 0;
    scene_model.region_z = 400;
    scene_model.region_height = 0;
    game.scene_model = &scene_model;

    scene_model_normals_new(&scene_model);
    scene_model_lighting_new(&scene_model, scene_model.normals);

    struct TexturesCache* textures_cache = textures_cache_new(cache);
    game.textures_cache = textures_cache;

    int up_pressed = 0;
    int down_pressed = 0;
    int left_pressed = 0;
    int right_pressed = 0;

    bool quit = false;
    while( !quit )
    {
        SDL_Event event;
        while( SDL_PollEvent(&event) )
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if( event.type == SDL_QUIT )
            {
                quit = true;
            }
            if( event.type == SDL_KEYDOWN )
            {
                if( event.key.keysym.sym == SDLK_ESCAPE )
                {
                    quit = true;
                }
                if( event.key.keysym.sym == SDLK_UP )
                {
                    up_pressed = 1;
                }
                if( event.key.keysym.sym == SDLK_DOWN )
                {
                    down_pressed = 1;
                }
                if( event.key.keysym.sym == SDLK_LEFT )
                {
                    left_pressed = 1;
                }
                if( event.key.keysym.sym == SDLK_RIGHT )
                {
                    right_pressed = 1;
                }
            }
            if( event.type == SDL_KEYUP )
            {
                if( event.key.keysym.sym == SDLK_UP )
                {
                    up_pressed = 0;
                }
            }
            if( event.type == SDL_KEYUP )
            {
                if( event.key.keysym.sym == SDLK_DOWN )
                {
                    down_pressed = 0;
                }
            }
            if( event.type == SDL_KEYUP )
            {
                if( event.key.keysym.sym == SDLK_LEFT )
                {
                    left_pressed = 0;
                }
            }
            if( event.type == SDL_KEYUP )
            {
                if( event.key.keysym.sym == SDLK_RIGHT )
                {
                    right_pressed = 0;
                }
            }
        }

        int speed = 10;

        if( up_pressed )
        {
            game.camera_pitch = (game.camera_pitch - speed + 2048) % 2048;
        }

        if( down_pressed )
        {
            game.camera_pitch = (game.camera_pitch + speed) % 2048;
        }

        if( left_pressed )
        {
            game.camera_yaw = (game.camera_yaw - speed + 2048) % 2048;
        }

        if( right_pressed )
        {
            game.camera_yaw = (game.camera_yaw + speed) % 2048;
        }

        SDL_RenderClear(platform.renderer);

        render_model(&platform, &game);
        render_imgui(&platform, &game);

        SDL_RenderPresent(platform.renderer);

        SDL_Delay(16);
    }

    return 0;
}