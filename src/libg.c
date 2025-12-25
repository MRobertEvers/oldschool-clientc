#include "libg.h"

#include "graphics/dash.h"
#include "graphics/lighting.h"
#include "osrs/dashlib.h"
#include "osrs/gio.h"
#include "osrs/grender.h"
#include "shared_tables.h"

// clang-format off
#include "osrs/cache.h"
#include "osrs/tables/model.h"
// clang-format on

#define CACHE_PATH "../cache"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static struct DashModel*
dashmodel_new_from_cache(struct Cache* cache, int model_id)
{
    struct CacheModel* model = model_new_from_cache(cache, model_id);
    if( !model )
    {
        printf("Failed to load model %d\n", model_id);
        return NULL;
    }

    struct DashModel* dashmodel = malloc(sizeof(struct DashModel));
    memset(dashmodel, 0, sizeof(struct DashModel));
    dashmodel->vertex_count = model->vertex_count;

    dashmodel->vertices_x = model->vertices_x;
    dashmodel->vertices_y = model->vertices_y;
    dashmodel->vertices_z = model->vertices_z;

    dashmodel->face_count = model->face_count;
    dashmodel->face_indices_a = model->face_indices_a;
    dashmodel->face_indices_b = model->face_indices_b;
    dashmodel->face_indices_c = model->face_indices_c;
    dashmodel->face_alphas = model->face_alphas;
    dashmodel->face_infos = model->face_infos;
    dashmodel->face_priorities = model->face_priorities;
    dashmodel->textured_face_count = model->textured_face_count;
    dashmodel->textured_p_coordinate = model->textured_p_coordinate;
    dashmodel->textured_m_coordinate = model->textured_m_coordinate;
    dashmodel->textured_n_coordinate = model->textured_n_coordinate;
    dashmodel->face_textures = model->face_textures;
    dashmodel->face_texture_coords = model->face_texture_coords;

    dashmodel->normals = dashmodel_normals_new(model->vertex_count, model->face_count);

    calculate_vertex_normals(
        dashmodel->normals->lighting_vertex_normals,
        dashmodel->normals->lighting_face_normals,
        dashmodel->vertex_count,
        dashmodel->face_indices_a,
        dashmodel->face_indices_b,
        dashmodel->face_indices_c,
        dashmodel->vertices_x,
        dashmodel->vertices_y,
        dashmodel->vertices_z,
        dashmodel->face_count);

    struct DashModelLighting* lighting =
        (struct DashModelLighting*)dashmodel_lighting_new(dashmodel->face_count);
    dashmodel->lighting = lighting;

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
        dashmodel->normals->lighting_vertex_normals,
        dashmodel->normals->lighting_face_normals,
        dashmodel->face_indices_a,
        dashmodel->face_indices_b,
        dashmodel->face_indices_c,
        dashmodel->face_count,
        model->face_colors,
        dashmodel->face_alphas,
        dashmodel->face_textures,
        dashmodel->face_infos,
        light_ambient,
        attenuation,
        lightsrc_x,
        lightsrc_y,
        lightsrc_z);

    dashmodel->bounds_cylinder =
        (struct DashBoundsCylinder*)malloc(sizeof(struct DashBoundsCylinder));
    *dashmodel->bounds_cylinder = dash3d_calculate_bounds_cylinder(
        model->vertex_count, model->vertices_x, model->vertices_y, model->vertices_z);
    return dashmodel;
}

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

    // x =
    // 7616
    // z =
    // 8100

    game->camera_world_x = 7616;
    game->camera_world_y = -500;
    game->camera_world_z = 8000;
    game->camera_yaw = 0;
    game->camera_pitch = 128;
    game->camera_roll = 0;
    game->camera_fov = 512;
    game->camera_movement_speed = 20;
    game->camera_rotation_speed = 20;

    game->sys_dash = dash_new();

    game->scene_elements = vec_new(sizeof(struct SceneElement), 1024);
    game->entity_dashmodels = vec_new(sizeof(int), 1024);
    game->entity_painters = vec_new(sizeof(int), 1024);

    struct Cache* cache = cache_new_from_directory(CACHE_PATH);
    if( !cache )
    {
        printf("Failed to load cache from directory: %s\n", CACHE_PATH);
        return NULL;
    }
    struct CacheModel* model = model_new_from_cache(cache, 0);
    game->model = dashmodel_new_from_cache(cache, 0);
    if( !game->model )
    {
        printf("Failed to load model %d\n", 0);
        return NULL;
    }

    game->position = malloc(sizeof(struct DashPosition));
    memset(game->position, 0, sizeof(struct DashPosition));
    game->view_port = malloc(sizeof(struct DashViewPort));
    memset(game->view_port, 0, sizeof(struct DashViewPort));
    game->camera = malloc(sizeof(struct DashCamera));
    memset(game->camera, 0, sizeof(struct DashCamera));

    game->position->x = 0;
    game->position->y = 0;
    game->position->z = 200;
    game->position->pitch = 0;
    game->position->yaw = 0;
    game->position->roll = 0;

    game->view_port->stride = 1024;
    game->view_port->width = 1024;
    game->view_port->height = 768;
    game->view_port->x_center = 512;
    game->view_port->y_center = 384;

    game->camera->fov_rpi2048 = 512;
    game->camera->near_plane_z = 50;

    // game->model = model_new_from_cache(game->cache, 0);
    // game->normals = model_normals_new(game->model->vertex_count, game->model->face_count);
    // calculate_vertex_normals(
    //     game->normals->lighting_vertex_normals,
    //     game->normals->lighting_face_normals,
    //     game->model->vertex_count,
    //     game->model->face_indices_a,
    //     game->model->face_indices_b,
    //     game->model->face_indices_c,
    //     game->model->vertices_x,
    //     game->model->vertices_y,
    //     game->model->vertices_z,
    //     game->model->face_count);

    // struct ModelLighting* lighting = model_lighting_new(game->model->face_count);

    // game->lighting = lighting;

    // int light_ambient = 64;
    // int light_attenuation = 768;
    // int lightsrc_x = -50;
    // int lightsrc_y = -10;
    // int lightsrc_z = -50;

    // {
    //     light_ambient += 0;
    //     // 2004Scape multiplies contrast by 5.
    //     // Later versions do not.
    //     light_attenuation += 0;
    // }

    // int light_magnitude =
    //     (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
    // int attenuation = (light_attenuation * light_magnitude) >> 8;

    // apply_lighting(
    //     lighting->face_colors_hsl_a,
    //     lighting->face_colors_hsl_b,
    //     lighting->face_colors_hsl_c,
    //     game->normals->lighting_vertex_normals,
    //     game->normals->lighting_face_normals,
    //     game->model->face_indices_a,
    //     game->model->face_indices_b,
    //     game->model->face_indices_c,
    //     game->model->face_count,
    //     game->model->face_colors,
    //     game->model->face_alphas,
    //     game->model->face_textures,
    //     game->model->face_infos,
    //     light_ambient,
    //     attenuation,
    //     lightsrc_x,
    //     lightsrc_y,
    //     lightsrc_z);

    // game->bounds_cylinder = (struct BoundsCylinder*)malloc(sizeof(struct BoundsCylinder));
    // *game->bounds_cylinder = calculate_bounds_cylinder(
    //     game->model->vertex_count,
    //     game->model->vertices_x,
    //     game->model->vertices_y,
    //     game->model->vertices_z);

    game->tasks_nullable = gtask_new_init_io(game->io);
    game->tasks_nullable->next = gtask_new_init_scene(game, 50 * 64, 50 * 64);

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
    struct GGame* game, struct GInput* input, struct GRenderCommandBuffer* render_command_buffer)
{
    struct GTask* task = game->tasks_nullable;
    enum GTaskStatus status = GTASK_STATUS_FAILED;
    while( task )
    {
        status = gtask_step(task);
        if( status != GTASK_STATUS_COMPLETED )
            break;

        on_completed_task(game, input, render_command_buffer, task);

        game->tasks_nullable = game->tasks_nullable->next;
        gtask_free(task);
        task = game->tasks_nullable;
    }
}

void
libg_game_step(
    struct GGame* game, struct GInput* input, struct GRenderCommandBuffer* render_command_buffer)
{
    struct GTask* task = NULL;
    struct GIOMessage message = { 0 };

    if( input->quit )
    {
        game->running = false;
        return;
    }

    libg_game_step_tasks(game, input, render_command_buffer);
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