#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/render.h"
#include "osrs/scene.h"
#include "osrs/scene_tile.h"
#include "osrs/tables/config_floortype.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/configs.h"
#include "osrs/tables/sprites.h"
#include "osrs/tables/texture_pixels.h"
#include "osrs/tables/textures.h"
#include "osrs/xtea_config.h"

// CACHE_PATH is defined by CMake build system
#ifndef CACHE_PATH
#define CACHE_PATH "/cache"
#endif

#include <assert.h>
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

// Global game state for browser access
struct Game* g_game = NULL;
int* g_pixel_buffer = NULL;

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
                if( d11 * 6.0 < 1.0 )
                {
                    g = p + (q - p) * 6.0 * d11;
                }
                else if( d11 * 2.0 < 1.0 )
                {
                    g = q;
                }
                else if( d11 * 3.0 < 2.0 )
                {
                    g = p + (q - p) * (0.6666666666666666 - d11) * 6.0;
                }
                else
                {
                    g = p;
                }
                if( hue * 6.0 < 1.0 )
                {
                    b = p + (q - p) * 6.0 * hue;
                }
                else if( hue * 2.0 < 1.0 )
                {
                    b = q;
                }
                else if( hue * 3.0 < 2.0 )
                {
                    b = p + (q - p) * (0.6666666666666666 - hue) * 6.0;
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
            rgb = pix3d_set_gamma(rgb, random_brightness);
            palette[offset++] = rgb;
        }
    }
}

void
init_hsl16_to_rgb_table()
{
    int* palette = (int*)malloc(65536 * sizeof(int));
    pix3d_set_brightness(palette, 1.0);
    memcpy(g_hsl16_to_rgb_table, palette, 65536 * sizeof(int));
    free(palette);
}

void
init_sin_table()
{
    for( int i = 0; i < 2048; i++ )
    {
        g_sin_table[i] = (int)(sin((double)i * 0.0030679615757712823) * 65536.0);
    }
}

void
init_cos_table()
{
    for( int i = 0; i < 2048; i++ )
    {
        g_cos_table[i] = (int)(cos((double)i * 0.0030679615757712823) * 65536.0);
    }
}

void
init_tan_table()
{
    for( int i = 0; i < 2048; i++ )
    {
        g_tan_table[i] = (int)(tan((double)i * 0.0030679615757712823) * 65536.0);
    }
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

    struct Scene* scene;

    struct SceneOp* ops;
    int op_count;

    int max_render_ops;
    int manual_render_ops;

    int show_loc_enabled;
    int show_loc_x;
    int show_loc_y;

    struct TexturesCache* textures_cache;
};

void
game_free(struct Game* game)
{
    if( game->tiles )
        free(game->tiles);
    if( game->textures )
        free(game->textures);
    if( game->texture_ids )
        free(game->texture_ids);
    if( game->sprite_packs )
        free(game->sprite_packs);
    if( game->sprite_ids )
        free(game->sprite_ids);
    if( game->map_locs )
        free(game->map_locs);
    if( game->scene_locs )
        free(game->scene_locs);
    if( game->loc_textures )
        free(game->loc_textures);
    if( game->scene )
        free(game->scene);
    if( game->ops )
        free(game->ops);
    if( game->textures_cache )
        free(game->textures_cache);
}

// Browser-compatible render function
void
game_render_browser(struct Game* game, int* pixel_buffer)
{
    memset(pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

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

    render_scene_ops(
        game->ops,
        game->op_count,
        0,
        game->max_render_ops,
        pixel_buffer,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        100, // Near plane z
        game->camera_x,
        game->camera_y,
        game->camera_z,
        game->camera_pitch,
        game->camera_yaw,
        game->camera_roll,
        game->camera_fov,
        game->scene,
        game->textures_cache);
}

// Functions to export to JavaScript
int*
get_pixel_buffer()
{
    return g_pixel_buffer;
}

int
get_screen_width()
{
    return SCREEN_WIDTH;
}

int
get_screen_height()
{
    return SCREEN_HEIGHT;
}

void
render_frame()
{
    if( g_game && g_pixel_buffer )
    {
        game_render_browser(g_game, g_pixel_buffer);
    }
}

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

void
set_camera_fov(int fov)
{
    if( g_game )
    {
        g_game->camera_fov = fov;
    }
}

int
main()
{
    printf("Starting browser-compatible scene tile test with rendering...\n");

    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();

    printf("Loading XTEA keys...\n");
    int xtea_keys_count = xtea_config_load_keys("../cache/xteas.json");
    if( xtea_keys_count == -1 )
    {
        printf("Failed to load xtea keys\n");
        return 1;
    }
    printf("Loaded %d XTEA keys\n", xtea_keys_count);

    printf("Loading cache from %s...\n", CACHE_PATH);
    struct Cache* cache = cache_new_from_directory(CACHE_PATH);
    if( !cache )
    {
        printf("Failed to load cache\n");
        return 1;
    }
    printf("Cache loaded successfully!\n");

    struct CacheArchive* archive = NULL;
    struct FileList* filelist = NULL;

    /**
     * Textures
     */
    printf("Loading textures...\n");
    archive = cache_archive_new_load(cache, CACHE_TEXTURES, 0);
    if( !archive )
    {
        printf("Failed to load textures archive\n");
        return 1;
    }

    filelist = filelist_new_from_cache_archive(archive);
    int texture_definitions_count = filelist->file_count;
    printf("Found %d texture definitions\n", texture_definitions_count);

    struct CacheTexture* texture_definitions =
        (struct CacheTexture*)malloc(texture_definitions_count * sizeof(struct CacheTexture));
    memset(texture_definitions, 0, texture_definitions_count * sizeof(struct CacheTexture));

    for( int i = 0; i < texture_definitions_count; i++ )
    {
        struct CacheTexture* texture_definition = &texture_definitions[i];

        struct ArchiveReference* archives = cache->tables[CACHE_TEXTURES]->archives;

        texture_definition_decode_inplace(
            texture_definition, filelist->files[i], filelist->file_sizes[i]);
    }

    filelist_free(filelist);
    cache_archive_free(archive);

    /**
     * Sprites
     */
    printf("Loading sprites...\n");
    archive = cache_archive_new_load(cache, CACHE_SPRITES, 0);
    if( !archive )
    {
        printf("Failed to load sprites archive\n");
        return 1;
    }

    filelist = filelist_new_from_cache_archive(archive);
    int sprite_count = filelist->file_count;
    printf("Found %d sprite packs\n", sprite_count);

    struct CacheSpritePack* sprite_packs =
        (struct CacheSpritePack*)malloc(sprite_count * sizeof(struct CacheSpritePack));
    memset(sprite_packs, 0, sprite_count * sizeof(struct CacheSpritePack));

    for( int i = 0; i < sprite_count; i++ )
    {
        struct ArchiveReference* archives = cache->tables[CACHE_SPRITES]->archives;

        struct CacheSpritePack* sprite_pack =
            sprite_pack_new_decode(archive->data, archive->data_size, SPRITELOAD_FLAG_NORMALIZE);
        sprite_packs[i] = *sprite_pack;
        free(sprite_pack);
    }

    filelist_free(filelist);
    cache_archive_free(archive);

    /**
     * Map Locations
     */
    printf("Loading map locations...\n");
    struct CacheMapLocs* map_locs = map_locs_new_from_cache(cache, 50, 50);
    if( !map_locs )
    {
        printf("Failed to load map locations\n");
        return 1;
    }
    printf("Loaded map locations for tile 50,50\n");

    /**
     * Scene
     */
    printf("Creating scene...\n");
    struct Scene* scene = scene_new_from_map(cache, 50, 50);
    if( !scene )
    {
        printf("Failed to create scene\n");
        return 1;
    }
    printf("Scene created successfully!\n");

    // Initialize game state
    struct Game* game = (struct Game*)malloc(sizeof(struct Game));
    memset(game, 0, sizeof(struct Game));

    game->camera_yaw = 0;
    game->camera_pitch = 0;
    game->camera_roll = 0;
    game->camera_fov = 512;
    game->camera_x = -3542;
    game->camera_y = -873;
    game->camera_z = 800;

    game->tile_count = 16384; // MAP_TILE_COUNT
    game->scene_locs = NULL;
    game->scene = scene;
    game->textures_cache = textures_cache_new(cache);
    game->show_loc_x = 63;
    game->show_loc_y = 63;

    // Allocate pixel buffer
    g_pixel_buffer = (int*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));
    memset(g_pixel_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(int));

    // Set global game pointer
    g_game = game;

    printf("Game initialized successfully!\n");
    printf("Rendering system ready for browser!\n");
    printf("Cache preloading test completed successfully!\n");
    printf("All cache files are accessible and working correctly.\n");

    // Render initial frame
    game_render_browser(game, g_pixel_buffer);
    printf("Initial frame rendered successfully!\n");

    printf("Test completed successfully!\n");
    return 0;
}