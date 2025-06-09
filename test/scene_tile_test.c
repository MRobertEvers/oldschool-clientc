#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/render.h"
#include "osrs/scene_tile.h"
#include "osrs/tables/config_floortype.h"
#include "osrs/tables/configs.h"
#include "osrs/xtea_config.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

int g_sin_table[2048];
int g_cos_table[2048];
int g_tan_table[2048];

//   This tool renders a color palette using jagex's 16-bit HSL, 6 bits
//             for hue, 3 for saturation and 7 for lightness, bitpacked and
//             represented as a short.
int g_hsl16_to_rgb_table[65536];

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

void
init_hsl16_to_rgb_table()
{
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

struct Game
{
    int camera_yaw;
    int camera_pitch;
    int camera_roll;
    int camera_fov;
    int camera_x;
    int camera_y;
    int camera_z;

    struct SceneTile* tiles;
    int tile_count;
};

struct PlatformSDL2
{
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    int* pixel_buffer;
};

static bool
platform_sdl2_init(struct PlatformSDL2* platform)
{
    int init = SDL_INIT_VIDEO;

    int res = SDL_Init(init);
    if( res < 0 )
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Scene Tile Test",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_ALLOW_HIGHDPI);
    platform->window = window;

    SDL_Renderer* renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    platform->renderer = renderer;

    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Create texture
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT);
    platform->texture = texture;

    platform->pixel_buffer = (int*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
    memset(platform->pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

    return true;
}

static void
game_render_sdl2(struct Game* game, struct PlatformSDL2* platform)
{
    SDL_Texture* texture = platform->texture;
    SDL_Renderer* renderer = platform->renderer;
    int* pixel_buffer = platform->pixel_buffer;
    memset(pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

    render_scene_tiles(
        pixel_buffer,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        50,
        game->camera_x,
        game->camera_y,
        game->camera_z,
        game->camera_pitch,
        game->camera_yaw,
        game->camera_roll,
        game->camera_fov,
        game->tiles,
        game->tile_count);

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
    printf(
        "Camera: x=%d y=%d z=%d pitch=%d yaw=%d\n",
        game->camera_x,
        game->camera_y,
        game->camera_z,
        game->camera_pitch,
        game->camera_yaw);

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
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    SDL_FreeSurface(surface);
}

int
main()
{
    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();

    int xtea_keys_count = xtea_config_load_keys("../cache/xteas.json");
    if( xtea_keys_count == -1 )
    {
        printf("Failed to load xtea keys\n");
        return 1;
    }

    struct Cache* cache = cache_new_from_directory("../cache");
    if( !cache )
    {
        printf("Failed to load cache\n");
        return 1;
    }

    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_UNDERLAY);
    if( !archive )
    {
        printf("Failed to load underlay archive\n");
        return 1;
    }

    struct FileList* filelist = filelist_new_from_cache_archive(archive);

    int underlay_count = filelist->file_count;
    int* underlay_ids = (int*)malloc(underlay_count * sizeof(int));
    struct Underlay* underlays = (struct Underlay*)malloc(underlay_count * sizeof(struct Underlay));
    for( int i = 0; i < underlay_count; i++ )
    {
        struct Underlay* underlay = &underlays[i];

        struct ArchiveReference* archives = cache->tables[CACHE_CONFIGS]->archives;

        config_floortype_underlay_decode_inplace(
            underlay, filelist->files[i], filelist->file_sizes[i]);

        int file_id =
            archives[cache->tables[CACHE_CONFIGS]->ids[CONFIG_UNDERLAY]].children.files[i].id;
        underlay_ids[i] = file_id;
    }

    filelist_free(filelist);
    cache_archive_free(archive);

    archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_OVERLAY);
    if( !archive )
    {
        printf("Failed to load overlay archive\n");
        return 1;
    }

    filelist = filelist_new_from_cache_archive(archive);

    int overlay_count = filelist->file_count;
    int* overlay_ids = (int*)malloc(overlay_count * sizeof(int));
    struct Overlay* overlays = (struct Overlay*)malloc(overlay_count * sizeof(struct Overlay));
    for( int i = 0; i < overlay_count; i++ )
    {
        struct Overlay* overlay = &overlays[i];

        struct ArchiveReference* archives = cache->tables[CACHE_CONFIGS]->archives;

        config_floortype_overlay_decode_inplace(
            overlay, filelist->files[i], filelist->file_sizes[i]);
        int file_id =
            archives[cache->tables[CACHE_CONFIGS]->ids[CONFIG_OVERLAY]].children.files[i].id;
        overlay_ids[i] = file_id;
    }

    filelist_free(filelist);
    cache_archive_free(archive);

    struct MapTerrain* map_terrain = map_terrain_new_from_cache(cache, 50, 50);
    if( !map_terrain )
    {
        printf("Failed to load map terrain\n");
        return 1;
    }

    struct SceneTile* tiles = scene_tiles_new_from_map_terrain(
        map_terrain, overlays, overlay_ids, overlay_count, underlays, underlay_ids, underlay_count);

    // Initialize SDL
    struct PlatformSDL2 platform = { 0 };
    if( !platform_sdl2_init(&platform) )
    {
        printf("Failed to initialize SDL\n");
        return 1;
    }

    // Initialize game state
    struct Game game = { 0 };
    game.camera_x = 3242;
    game.camera_y = -26;
    game.camera_z = 3202;
    game.camera_pitch = 105;
    game.camera_yaw = 284;
    game.camera_fov = 512; // Default FOV
    game.tiles = tiles;
    game.tile_count = MAP_TILE_COUNT;

    int w_pressed = 0;
    int a_pressed = 0;
    int s_pressed = 0;
    int d_pressed = 0;

    int up_pressed = 0;
    int down_pressed = 0;
    int left_pressed = 0;
    int right_pressed = 0;

    int f_pressed = 0;
    int r_pressed = 0;

    bool quit = false;
    int speed = 200;
    SDL_Event event;
    while( !quit )
    {
        while( SDL_PollEvent(&event) )
        {
            if( event.type == SDL_QUIT )
            {
                quit = true;
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
                case SDLK_q:
                    game.camera_roll = (game.camera_roll - 10 + 2048) % 2048;
                    break;
                case SDLK_e:
                    game.camera_roll = (game.camera_roll + 10) % 2048;
                    break;
                case SDLK_i:
                    game.camera_fov += 1;
                    break;
                case SDLK_k:
                    game.camera_fov -= 1;
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
                }
            }
        }

        if( w_pressed )
        {
            game.camera_x -= (g_sin_table[game.camera_yaw] * speed) >> 16;
            game.camera_y -= (g_cos_table[game.camera_yaw] * speed) >> 16;
        }

        if( a_pressed )
        {
            game.camera_x += (g_cos_table[game.camera_yaw] * speed) >> 16;
            game.camera_y -= (g_sin_table[game.camera_yaw] * speed) >> 16;
        }

        if( s_pressed )
        {
            game.camera_x += (g_sin_table[game.camera_yaw] * speed) >> 16;
            game.camera_y += (g_cos_table[game.camera_yaw] * speed) >> 16;
        }

        if( d_pressed )
        {
            game.camera_x -= (g_cos_table[game.camera_yaw] * speed) >> 16;
            game.camera_y += (g_sin_table[game.camera_yaw] * speed) >> 16;
        }

        if( up_pressed )
        {
            game.camera_pitch = (game.camera_pitch + 10) % 2048;
        }

        if( left_pressed )
        {
            game.camera_yaw = (game.camera_yaw - 10 + 2048) % 2048;
        }

        if( right_pressed )
        {
            game.camera_yaw = (game.camera_yaw + 10) % 2048;
        }

        if( down_pressed )
        {
            game.camera_pitch = (game.camera_pitch - 10 + 2048) % 2048;
        }

        if( f_pressed )
        {
            game.camera_z -= speed;
        }

        if( r_pressed )
        {
            game.camera_z += speed;
        }

        // Render frame
        game_render_sdl2(&game, &platform);

        // Cap at 30 FPS
        SDL_Delay(1000 / 30);
    }

    // Cleanup
    SDL_DestroyTexture(platform.texture);
    SDL_DestroyRenderer(platform.renderer);
    SDL_DestroyWindow(platform.window);
    free(platform.pixel_buffer);
    SDL_Quit();

    // Free game resources
    free_tiles(tiles, game.tile_count);
    free(overlay_ids);
    free(underlays);
    free(overlays);
    cache_free(cache);

    return 0;
}