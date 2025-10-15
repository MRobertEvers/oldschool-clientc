#include "libgame.h"

#include "libinput.h"
#include "osrs/cache.h"
#include "osrs/scene.h"
#include "osrs/scene_cache.h"
#include "osrs/xtea_config.h"
#include "shared_tables.h"
#define CACHE_PATH "../cache"

#include "libgame.u.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Game*
game_new(void)
{
    struct Game* game = (struct Game*)malloc(sizeof(struct Game));
    memset(game, 0, sizeof(struct Game));

    game->running = true;

    game->camera_yaw = 0;
    game->camera_pitch = 128;
    game->camera_roll = 0;
    game->camera_fov = 512;
    game->camera_world_x = 0;
    game->camera_world_y = -50;
    game->camera_world_z = -500;

    game->camera_movement_speed = 20;
    game->camera_rotation_speed = 20;

    game->cache = NULL;
    game->scene_model = NULL;

    return game;
}

void
game_free(struct Game* game)
{
    free(game);
}

bool
game_init(struct Game* game)
{
    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();
    init_reciprocal16();

    printf("Loading XTEA keys from: %s/xteas.json\n", CACHE_PATH);
    int xtea_keys_count = xtea_config_load_keys(CACHE_PATH "/xteas.json");
    if( xtea_keys_count == -1 )
    {
        printf("Failed to load xtea keys from: %s/xteas.json\n", CACHE_PATH);

        return 0;
    }
    printf("Loaded %d XTEA keys successfully\n", xtea_keys_count);

    printf("Loading cache from directory: %s\n", CACHE_PATH);
    struct Cache* cache = cache_new_from_directory(CACHE_PATH);
    if( !cache )
    {
        printf("Failed to load cache from directory: %s\n", CACHE_PATH);

        return 0;
    }
    printf("Cache loaded successfully\n");

    game->cache = cache;

    /**
     * Init
     *
     */
    struct ModelCache* model_cache = NULL;
    struct CacheModel* model = NULL;
    model_cache = model_cache_new();

    model = model_cache_checkout(model_cache, cache, 14816);
    game->scene_model = scene_model_new_lit_from_model(model, 0);

    game->textures_cache = textures_cache_new(cache);

    if( !game->scene_model->bounds_cylinder )
    {
        game->scene_model->bounds_cylinder =
            (struct BoundsCylinder*)malloc(sizeof(struct BoundsCylinder));
        *game->scene_model->bounds_cylinder = calculate_bounds_cylinder(
            game->scene_model->model->vertex_count,
            game->scene_model->model->vertices_x,
            game->scene_model->model->vertices_y,
            game->scene_model->model->vertices_z);
    }

    return true;
}

void
game_process_input(struct Game* game, struct GameInput* input)
{
    if( input->w_pressed )
    {
        game->camera_world_x += (g_sin_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
        game->camera_world_y -= (g_cos_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
    }

    if( input->a_pressed )
    {
        game->camera_world_x += (g_cos_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
        game->camera_world_y += (g_sin_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
    }

    if( input->s_pressed )
    {
        game->camera_world_x -= (g_sin_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
        game->camera_world_y += (g_cos_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
    }

    if( input->d_pressed )
    {
        game->camera_world_x -= (g_cos_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
        game->camera_world_y -= (g_sin_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
    }

    if( input->r_pressed )
    {
        game->camera_world_z -= game->camera_movement_speed;
    }
    if( input->f_pressed )
    {
        game->camera_world_z += game->camera_movement_speed;
    }

    if( input->up_pressed )
    {
        game->camera_pitch = (game->camera_pitch + game->camera_rotation_speed) % 2048;
    }
    if( input->down_pressed )
    {
        game->camera_pitch = (game->camera_pitch - game->camera_rotation_speed + 2048) % 2048;
    }

    if( input->left_pressed )
    {
        game->camera_yaw = (game->camera_yaw + game->camera_rotation_speed) % 2048;
    }
    if( input->right_pressed )
    {
        game->camera_yaw = (game->camera_yaw - game->camera_rotation_speed + 2048) % 2048;
    }

    if( input->quit )
    {
        game->running = false;
    }

    return;
}

void
game_step_main_loop(struct Game* game)
{
    return;
}

bool
game_is_running(struct Game* game)
{
    return game->running;
}