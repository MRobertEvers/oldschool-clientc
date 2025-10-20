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

void
scenegfx_scene_load_map(
    struct Game* game, int chunk_x, int chunk_y, struct GameGfxOpList* gfx_op_list)
{
    struct GameGfxOp gfx_op;

    game->scene = scene_new_from_map(game->cache, chunk_x, chunk_y);

    for( int i = 0; i < game->scene->models_length; i++ )
    {
        struct SceneModel* scene_model = &game->scene->models[i];
        if( scene_model->model )
        {
            gfx_op.kind = GAME_GFX_OP_SCENE_MODEL_LOAD;
            gfx_op._scene_model_load.scene_model_idx = i;
            gfx_op._scene_model_load.frame_id = 0;

            game_gfx_op_list_push(gfx_op_list, &gfx_op);
        }
    }

    for( int i = 0; i < game->scene->scene_tiles_length; i++ )
    {
        struct SceneTile* scene_tile = &game->scene->scene_tiles[i];
        if( scene_tile->face_count > 0 )
        {
            gfx_op.kind = GAME_GFX_OP_SCENE_TILE_LOAD;
            gfx_op._scene_tile_load.scene_tile_idx = i;
            game_gfx_op_list_push(gfx_op_list, &gfx_op);
        }
    }
}

struct GameGfxOpList*
game_gfx_op_list_new(int capacity_hint)
{
    struct GameGfxOpList* list = (struct GameGfxOpList*)malloc(sizeof(struct GameGfxOpList));
    list->ops = (struct GameGfxOp*)malloc(capacity_hint * sizeof(struct GameGfxOp));
    list->op_count = 0;
    list->op_capacity = capacity_hint;
    return list;
}

void
game_gfx_op_list_free(struct GameGfxOpList* list)
{
    free(list->ops);
    free(list);
}

void
game_gfx_op_list_reset(struct GameGfxOpList* list)
{
    list->op_count = 0;
}

int
game_gfx_op_list_push(struct GameGfxOpList* list, struct GameGfxOp* op)
{
    if( list->op_count >= list->op_capacity )
    {
        list->op_capacity *= 2;
        list->ops =
            (struct GameGfxOp*)realloc(list->ops, list->op_capacity * sizeof(struct GameGfxOp));
    }
    memcpy(&list->ops[list->op_count], op, sizeof(struct GameGfxOp));
    list->op_count++;
    return 0;
}

struct Game*
game_new(int flags, struct GameGfxOpList* gfx_op_list)
{
    struct Game* game = (struct Game*)malloc(sizeof(struct Game));
    memset(game, 0, sizeof(struct Game));

    game->running = true;

    game->camera_yaw = 0;
    game->camera_pitch = 128;
    game->camera_roll = 0;
    game->camera_fov = 512;
    game->camera_world_x = 0;
    game->camera_world_y = -500;
    game->camera_world_z = -500;

    game->camera_movement_speed = 20;
    game->camera_rotation_speed = 20;

    game->cache = NULL;

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
    game->frustrum_cullmap = frustrum_cullmap_new_nocull(25);
    game->textures_cache = textures_cache_new(cache);

    /**
     * Init
     *
     */
    // struct ModelCache* model_cache = NULL;
    // struct CacheModel* model = NULL;
    // model_cache = model_cache_new();

    // model = model_cache_checkout(model_cache, cache, 14816);

    scenegfx_scene_load_map(game, 50, 50, gfx_op_list);

    // game->scene_model = scene_model_new_lit_from_model(model, 0);

    // game->textures_cache = textures_cache_new(cache);

    // if( !game->scene_model->bounds_cylinder )
    // {
    //     game->scene_model->bounds_cylinder =
    //         (struct BoundsCylinder*)malloc(sizeof(struct BoundsCylinder));
    //     *game->scene_model->bounds_cylinder = calculate_bounds_cylinder(
    //         game->scene_model->model->vertex_count,
    //         game->scene_model->model->vertices_x,
    //         game->scene_model->model->vertices_y,
    //         game->scene_model->model->vertices_z);
    // }

    return game;
}

void
game_free(struct Game* game)
{
    free(game);
}

void
game_step_main_loop(struct Game* game, struct GameInput* input, struct GameGfxOpList* gfx_op_list)
{
    const int target_input_fps = 50;
    const float time_delta_step = 1.0f / target_input_fps;

    int time_quanta = 0;
    while( input->time_delta_accumulator_seconds > time_delta_step )
    {
        time_quanta++;
        input->time_delta_accumulator_seconds -= time_delta_step;
    }

    for( int i = 0; i < time_quanta; i++ )
    {
        if( input->w_pressed )
        {
            game->camera_world_x -=
                (g_sin_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
            game->camera_world_z +=
                (g_cos_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
        }

        if( input->s_pressed )
        {
            game->camera_world_x +=
                (g_sin_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
            game->camera_world_z -=
                (g_cos_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
        }

        if( input->a_pressed )
        {
            game->camera_world_x -=
                (g_cos_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
            game->camera_world_z -=
                (g_sin_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
        }

        if( input->d_pressed )
        {
            game->camera_world_x +=
                (g_cos_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
            game->camera_world_z +=
                (g_sin_table[game->camera_yaw] * game->camera_movement_speed) >> 16;
        }

        if( input->r_pressed )
        {
            game->camera_world_y -= game->camera_movement_speed;
        }
        if( input->f_pressed )
        {
            game->camera_world_y += game->camera_movement_speed;
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
    }

    struct GameGfxOp gfx_op = {
        .kind = GAME_GFX_OP_SCENE_DRAW,
    };
    game_gfx_op_list_push(gfx_op_list, &gfx_op);

    return;
}

bool
game_is_running(struct Game* game)
{
    return game->running;
}