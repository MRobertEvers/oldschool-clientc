#include "libg.h"

#include "osrs/cache.h"
#include "osrs/gio.h"
#include "osrs/grender.h"
#include "osrs/sceneload.u.c"
#include "shared_tables.h"

#define CACHE_PATH "../cache"

#include <stdlib.h>
#include <string.h>

struct GGame*
libg_game_new(struct GIOQueue* io)
{
    struct GGame* game = malloc(sizeof(struct GGame));
    memset(game, 0, sizeof(struct GGame));

    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();
    init_reciprocal16();

    game->io = io;
    game->running = true;
    game->camera_world_x = 0;
    game->camera_world_y = -100;
    game->camera_world_z = -500;
    game->camera_yaw = 0;
    game->camera_pitch = 128;
    game->camera_roll = 0;
    game->camera_fov = 512;
    game->camera_movement_speed = 20;
    game->camera_rotation_speed = 20;

    game->cache = cache_new_from_directory(CACHE_PATH);

    game->model = model_new_from_cache(game->cache, 0);
    game->normals = model_normals_new(game->model->vertex_count, game->model->face_count);
    calculate_vertex_normals(
        game->normals->lighting_vertex_normals,
        game->normals->lighting_face_normals,
        game->model->vertex_count,
        game->model->face_indices_a,
        game->model->face_indices_b,
        game->model->face_indices_c,
        game->model->vertices_x,
        game->model->vertices_y,
        game->model->vertices_z,
        game->model->face_count);

    struct ModelLighting* lighting = model_lighting_new(game->model->face_count);

    game->lighting = lighting;

    int light_ambient = 64;
    int light_attenuation = 768;
    int lightsrc_x = -50;
    int lightsrc_y = -10;
    int lightsrc_z = -50;

    {
        light_ambient += 0;
        // 2004Scape multiplies contrast by 5.
        // Later versions do not.
        light_attenuation += 0;
    }

    int light_magnitude =
        (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
    int attenuation = (light_attenuation * light_magnitude) >> 8;

    apply_lighting(
        lighting->face_colors_hsl_a,
        lighting->face_colors_hsl_b,
        lighting->face_colors_hsl_c,
        game->normals->lighting_vertex_normals,
        game->normals->lighting_face_normals,
        game->model->face_indices_a,
        game->model->face_indices_b,
        game->model->face_indices_c,
        game->model->face_count,
        game->model->face_colors,
        game->model->face_alphas,
        game->model->face_textures,
        game->model->face_infos,
        light_ambient,
        attenuation,
        lightsrc_x,
        lightsrc_y,
        lightsrc_z);

    game->bounds_cylinder = (struct BoundsCylinder*)malloc(sizeof(struct BoundsCylinder));
    *game->bounds_cylinder = calculate_bounds_cylinder(
        game->model->vertex_count,
        game->model->vertices_x,
        game->model->vertices_y,
        game->model->vertices_z);

    game->tasks_nullable = gtask_new_init_io(game->io);
    game->tasks_nullable->next = gtask_new_init_scene(game->io, 50, 50);

    return game;
}

void
libg_game_free(struct GGame* game)
{
    gioq_free(game->io);
    free(game);
}

void
libg_game_process_input(struct GGame* game, struct GInput* input)
{
    // IO
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
}

static void
on_completed_task(
    struct GGame* game,
    struct GIOQueue* queue,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer,
    struct GTask* task)
{
    switch( task->kind )
    {
    case GTASK_KIND_INIT_IO:
        break;
    case GTASK_KIND_INIT_SCENE:
        // game->model = gtask_init_scene_value(task->_init_scene);
        break;
    }
}

void
libg_game_step_tasks(
    struct GGame* game,
    struct GIOQueue* queue,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer)
{
    struct GTask* task = game->tasks_nullable;
    enum GTaskStatus status = GTASK_STATUS_FAILED;
    while( task )
    {
        status = gtask_step(task);
        if( status != GTASK_STATUS_COMPLETED )
            break;

        on_completed_task(game, queue, input, render_command_buffer, task);

        game->tasks_nullable = game->tasks_nullable->next;
        gtask_free(task);
        task = game->tasks_nullable;
    }
}

void
libg_game_step(
    struct GGame* game,
    struct GIOQueue* queue,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer)
{
    struct GTask* task = NULL;
    struct GIOMessage message = { 0 };

    if( input->quit )
    {
        game->running = false;
        return;
    }

    libg_game_step_tasks(game, queue, input, render_command_buffer);
    task = game->tasks_nullable;
    if( task && task->status != GTASK_STATUS_COMPLETED )
    {
        printf("Tasks Inflight: %d\n", task->status);
        return;
    }

    libg_game_process_input(game, input);

    grendercb_reset(render_command_buffer);
    struct GRenderCommand command = {
        .kind = GRENDER_CMD_MODEL_DRAW,
    };
    grendercb_add_command(render_command_buffer, command);

    return;
}

bool
libg_game_is_running(struct GGame* game)
{
    return game->running;
}