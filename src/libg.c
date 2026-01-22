#include "libg.h"

#include "graphics/dash.h"
#include "graphics/lighting.h"
#include "osrs/dashlib.h"
#include "osrs/gameproto.h"
#include "osrs/gametask.h"
#include "osrs/gio.h"
#include "osrs/grender.h"
#include "osrs/packetbuffer.h"
#include "osrs/packetin.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/tables_dat/config_versionlist_mapsquare.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
// clang-format off
#include "osrs/rscache/cache.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/gameproto_packets_write.u.c"
// clang-format on

#define CACHE_PATH "../cache"
#define CACHE_DAT_PATH "../cache254"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static struct DashModel*
dashmodel_new_from_cache(
    struct Cache* cache,
    int model_id)
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
    dash3d_calculate_bounds_cylinder(
        dashmodel->bounds_cylinder,
        model->vertex_count,
        model->vertices_x,
        model->vertices_y,
        model->vertices_z);
    return dashmodel;
}

struct GGame*
libg_game_new(struct GIOQueue* io)
{
    struct GGame* game = malloc(sizeof(struct GGame));
    memset(game, 0, sizeof(struct GGame));

    dash_init();

    game->io = io;
    game->running = true;

    // x =
    // 7616
    // z =
    // 8100

    game->camera_world_x = 4237;
    game->camera_world_y = -730;
    game->camera_world_z = 3672;
    game->camera_yaw = 0;
    game->camera_pitch = 128;
    game->camera_roll = 0;
    game->camera_pitch = 128;
    game->camera_yaw = 768;
    game->camera_roll = 0;
    game->camera_fov = 512;
    game->camera_movement_speed = 70;
    game->camera_rotation_speed = 20;
    game->cc = 100000;
    game->latched = false;

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
    game->position->y = 1000;
    game->position->z = 500;
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

    struct CacheDat* cache_dat = cache_dat_new(CACHE_DAT_PATH);
    struct CacheDatArchive* archive = cache_dat_archive_new_load(cache_dat, CACHE_DAT_MODELS, 0);
    assert(archive);

    struct CacheModel* datmodel = model_new_from_archive(archive, 0);
    struct DashModel* dashmodel = dashmodel_new_from_cache_model(datmodel);
    game->model = dashmodel;

    dashmodel->normals = dashmodel_normals_new(game->model->vertex_count, game->model->face_count);
    calculate_vertex_normals(
        dashmodel->normals->lighting_vertex_normals,
        dashmodel->normals->lighting_face_normals,
        game->model->vertex_count,
        game->model->face_indices_a,
        game->model->face_indices_b,
        game->model->face_indices_c,
        game->model->vertices_x,
        game->model->vertices_y,
        game->model->vertices_z,
        game->model->face_count);

    struct DashModelLighting* lighting = dashmodel_lighting_new(game->model->face_count);

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
        dashmodel->face_colors,
        dashmodel->face_alphas,
        dashmodel->face_textures,
        dashmodel->face_infos,
        64,
        768,
        -50,
        -10,
        -50);

    archive = cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_VERSION_LIST);

    struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(archive);

    char* file_data = NULL;
    int file_data_size = 0;
    int name_hash = archive_name_hash_dat("map_index");
    for( int i = 0; i < filelist->file_count; i++ )
    {
        if( filelist->file_name_hashes[i] == name_hash )
        {
            printf("Found map index at index %d\n", i);
            file_data = filelist->files[i];
            file_data_size = filelist->file_sizes[i];
            break;
        }
    }

    assert(file_data);

    struct CacheMapSquares* map_squares = cache_map_squares_new_decode(file_data, file_data_size);
    assert(map_squares);

    for( int i = 0; i < map_squares->squares_count; i++ )
    {
        struct MapSquareCoord coord = { 0 };
        cache_map_square_coord(&coord, map_squares->squares[i].map_id);
        printf(
            "Map square %d: (%d, %d), %d, %d\n",
            i,
            coord.map_x,
            coord.map_z,
            map_squares->squares[i].terrain_archive_id,
            map_squares->squares[i].loc_archive_id);
    }

    cache_map_squares_free(map_squares);

    char data[1024];

    gametask_new_init_io(game, game->io);
    // gametask_new_init_scene_dat(game, 48, 48, 51, 51);
    // gametask_new_init_scene(game, 40, 40, 41, 41);
    gametask_new_init_scene(game, 23, 54, 25, 54);

    // struct PacketBuffer packetbuffer;
    // packetbuffer_init(&packetbuffer, GAMEPROTO_REVISION_LC254);

    // gameproto_packet_write_maprebuild8_z16_x16(
    //     data, PKTIN_LC254_REBUILD_NORMAL, sizeof(data), 50 * 8, 50 * 8);

    // packetbuffer_read(&packetbuffer, data, sizeof(data));
    // assert(packetbuffer_ready(&packetbuffer));

    // gameproto_process(
    //     game,
    //     GAMEPROTO_REVISION_LC254,
    //     packetbuffer.packet_type,
    //     packetbuffer.data,
    //     packetbuffer.data_size);

    // packetbuffer_reset(&packetbuffer);

    return game;
}

void
libg_game_process_input(
    struct GGame* game,
    struct GInput* input)
{
    // IO
    const int target_input_fps = 50;
    const float time_delta_step = 1.0f / target_input_fps;

    int time_quanta = 0;
    while( input->time_delta_accumulator_seconds > time_delta_step )
    {
        time_quanta++;
        input->time_delta_accumulator_seconds -= time_delta_step;
        game->cycles++;
        if( !game->latched )
            game->cc++;
    }

    game->mouse_x = input->mouse_x;
    game->mouse_y = input->mouse_y;

    for( int i = 0; i < time_quanta; i++ )
    {
        if( input->w_pressed )
        {
            game->camera_world_x -=
                (dash_sin(game->camera_yaw) * game->camera_movement_speed) >> 16;
            game->camera_world_z +=
                (dash_cos(game->camera_yaw) * game->camera_movement_speed) >> 16;
        }

        if( input->s_pressed )
        {
            game->camera_world_x +=
                (dash_sin(game->camera_yaw) * game->camera_movement_speed) >> 16;
            game->camera_world_z -=
                (dash_cos(game->camera_yaw) * game->camera_movement_speed) >> 16;
        }

        if( input->a_pressed )
        {
            game->camera_world_x -=
                (dash_cos(game->camera_yaw) * game->camera_movement_speed) >> 16;
            game->camera_world_z -=
                (dash_sin(game->camera_yaw) * game->camera_movement_speed) >> 16;
        }

        if( input->d_pressed )
        {
            game->camera_world_x +=
                (dash_cos(game->camera_yaw) * game->camera_movement_speed) >> 16;
            game->camera_world_z +=
                (dash_sin(game->camera_yaw) * game->camera_movement_speed) >> 16;
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

        if( input->space_pressed )
        {
            game->cc = 0;
        }

        if( input->i_pressed )
        {
            game->latched = !game->latched;
        }

        if( input->j_pressed )
        {
            game->cc += 1;
        }

        if( input->k_pressed )
        {
            game->cc -= 1;
        }

        if( input->l_pressed )
        {
            game->cc += 100;
        }

        if( input->comma_pressed )
        {
            game->cc -= 100;
        }
    }
}

static void
on_completed_task(
    struct GGame* game,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer,
    struct GameTask* task)
{
    switch( task->kind )
    {
    case GAMETASK_KIND_INIT_IO:
        break;
    case GAMETASK_KIND_INIT_SCENE:
        // game->model = gtask_init_scene_value(task->_init_scene);
        break;
    }
}

void
libg_game_step_tasks(
    struct GGame* game,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer)
{
    struct GameTask* task = game->tasks_nullable;
    enum GameTaskStatus status = GAMETASK_STATUS_FAILED;
    while( task )
    {
        status = gametask_step(task);
        if( status != GAMETASK_STATUS_COMPLETED )
            break;

        on_completed_task(game, input, render_command_buffer, task);

        game->tasks_nullable = game->tasks_nullable->next;
        gametask_free(task);
        task = game->tasks_nullable;
    }
}

void
libg_game_step(
    struct GGame* game,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer)
{
    struct GameTask* task = NULL;
    struct GIOMessage message = { 0 };

    if( input->quit )
    {
        game->running = false;
        return;
    }

    libg_game_step_tasks(game, input, render_command_buffer);
    task = game->tasks_nullable;
    if( task && task->status != GAMETASK_STATUS_COMPLETED )
    {
        return;
    }

    libg_game_process_input(game, input);

    dash_animate_textures(game->sys_dash, game->cycles);

    while( game->cycles > 0 )
    {
        game->cycles--;
        if( !game->scene )
            continue;
        for( int i = 0; i < game->scene->scenery->elements_length; i++ )
        {
            struct SceneElement* element = scene_element_at(game->scene->scenery, i);
            if( element->animation )
            {
                element->animation->cycle++;
                if( element->animation->cycle >=
                    element->animation->frame_lengths[element->animation->frame_index] )
                {
                    element->animation->cycle = 0;
                    element->animation->frame_index++;
                    if( element->animation->frame_index >= element->animation->frame_count )
                    {
                        element->animation->frame_index = 0;
                    }
                }
            }
        }
    }

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