#include "tori_rs.h"

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
#define CACHE_DAT_PATH "../cache245_2"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct GGame*
LibToriRS_GameNew(
    struct GIOQueue* io,
    int graphics3d_width,
    int graphics3d_height)
{
    struct GGame* game = malloc(sizeof(struct GGame));
    memset(game, 0, sizeof(struct GGame));

    dash_init();

    game->io = io;
    game->running = true;

    game->player_tx = -1;
    game->player_tz = -1;
    game->player_draw_x = -1;
    game->player_draw_z = -1;

    // x =
    // 7616
    // z =
    // 8100
    game->camera_world_x = 0;
    game->camera_world_y = -500;
    game->camera_world_z = 0;

    // Camera (x, y, z): 3939, -800, 12589 : 30, 98
    // Camera (pitch, yaw, roll): 388, 1556, 0

    // Camera (x, y, z): 3790, -780, 4355 : 29, 34
    // Camera (x, y, z): 79, -150, -64 : 0, 0
    game->camera_world_x = 79;
    game->camera_world_y = -150;
    game->camera_world_z = -64;

    // game->camera_world_x = 3939;
    // game->camera_world_y = -800;
    // game->camera_world_z = 12589;
    // Camera (pitch, yaw, roll): 1916, 328, 0
    game->camera_pitch = 1916;
    game->camera_yaw = 328;
    game->camera_roll = 0;
    game->camera_fov = 512;
    game->camera_movement_speed = 70;
    game->camera_rotation_speed = 20;
    game->cc = 100000;
    game->latched = false;

    game->sys_dash = dash_new();

    game->scene_elements = vec_new(sizeof(struct SceneElement), 1024);

    game->position = malloc(sizeof(struct DashPosition));
    memset(game->position, 0, sizeof(struct DashPosition));
    game->view_port = malloc(sizeof(struct DashViewPort));
    memset(game->view_port, 0, sizeof(struct DashViewPort));
    game->camera = malloc(sizeof(struct DashCamera));
    memset(game->camera, 0, sizeof(struct DashCamera));
    game->iface_view_port = malloc(sizeof(struct DashViewPort));
    memset(game->iface_view_port, 0, sizeof(struct DashViewPort));

    game->view_port->stride = graphics3d_width;
    game->view_port->width = graphics3d_width;
    game->view_port->height = graphics3d_height;
    game->view_port->x_center = graphics3d_width / 2;
    game->view_port->y_center = graphics3d_height / 2;

    game->camera->fov_rpi2048 = 512;
    game->camera->near_plane_z = 50;

    gametask_new_init_io(game, game->io);
    gametask_new_init_scene_dat(game, 50, 50, 51, 51);

    return game;
}

void
LibToriRS_GameProcessInput(
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
        if( game->mouse_cycle < 400 && game->mouse_cycle != -1 )
        {
            game->mouse_cycle += 20;
            if( game->mouse_cycle >= 400 )
                game->mouse_cycle = -1;
        }

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

    game->mouse_clicked = false;
    if( input->mouse_clicked )
    {
        game->mouse_clicked = true;
        game->mouse_cycle = 0;
        game->mouse_clicked_x = input->mouse_clicked_x;
        game->mouse_clicked_y = input->mouse_clicked_y;
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
LibToriRS_GameStepTasks(
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
LibToriRS_GameStep(
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

    LibToriRS_GameStepTasks(game, input, render_command_buffer);
    task = game->tasks_nullable;
    if( task && task->status != GAMETASK_STATUS_COMPLETED )
    {
        return;
    }

    LibToriRS_GameProcessInput(game, input);

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

        // Get current time (using tick_ms as fallback, or we can use a platform-specific timer)
        // For now, we'll use a simple approach: track time in game structure
        // The timestamp will be updated by the platform layer
        if( game->player_tx != -1 )
        {
            int player_target_draw_x = game->player_tx * 128;
            int player_target_draw_z = game->player_tz * 128;
            if( player_target_draw_x != game->player_draw_x ||
                player_target_draw_z != game->player_draw_z )
            {
                // Move one tile if enough time has passed

                int xdiff = player_target_draw_x - game->player_draw_x;
                int zdiff = player_target_draw_z - game->player_draw_z;
                int xmove = 0;
                int zmove = 0;
                int pxmove = 0;
                int pzmove = 0;
                // 4 6
                static const int move_speed = 4;
                if( abs(xdiff) > move_speed )
                {
                    xmove = xdiff > 0 ? 1 : -1;
                    pxmove = xmove * move_speed;
                }
                else
                {
                    pxmove = player_target_draw_x - game->player_draw_x;
                }
                if( abs(zdiff) > move_speed )
                {
                    zmove = zdiff > 0 ? 1 : -1;
                    pzmove = zmove * move_speed;
                }
                else
                {
                    pzmove = player_target_draw_z - game->player_draw_z;
                }

                // Update camera world position
                game->player_draw_x += pxmove;
                game->player_draw_z += pzmove;

                if( xdiff >= 2 || zdiff >= 2 )
                {
                    game->player_state = 2;
                }
                else
                {
                    game->player_state = 1;
                }
            }
            else
            {
                game->player_state = 0;
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
LibToriRS_GameIsRunning(struct GGame* game)
{
    return game->running;
}