#include "tori_rs.h"

#include "graphics/dash.h"
#include "graphics/lighting.h"
#include "osrs/buildcachedat.h"
#include "osrs/dashlib.h"
#include "osrs/gameproto.h"
#include "osrs/gameproto_process.h"
#include "osrs/gametask.h"
#include "osrs/gio.h"
#include "osrs/packetbuffer.h"
#include "osrs/packetin.h"
#include "osrs/query_engine.h"
#include "osrs/query_executor_dat.h"
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

    game->netin = ringbuf_new(4096);
    game->netout = ringbuf_new(4096);

    game->login = malloc(sizeof(struct LCLogin));
    memset(game->login, 0, sizeof(struct LCLogin));
    lclogin_init(game->login, 245, NULL, false);
    lclogin_load_rsa_public_key_from_env(game->login);
    lclogin_start(game->login, "asdf2", "a", false);

    game->packet_buffer = malloc(sizeof(struct PacketBuffer));
    memset(game->packet_buffer, 0, sizeof(struct PacketBuffer));

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

    game->buildcachedat = buildcachedat_new();

    gametask_new_init_io(game, game->io);
    gametask_new_init_scene_dat(game, 50, 50, 51, 51);

    //     {
    // #define MAPXZR(x, z) ((x) << 8 | (z))
    //         int regions[2] = {
    //             MAPXZR(48, 48),
    //             MAPXZR(48, 49),
    //         };

    //         struct QEQuery* q = query_engine_qnew();
    //         query_engine_qpush_op(q, QEDAT_DT_MAPS_SCENERY, QE_FN_0, QE_STORE_SET_0);
    //         query_engine_qpush_argx(q, regions, 2);
    //         query_engine_qpush_op(q, QEDAT_DT_CONFIG_LOCIDS, QE_FN_FROM_0, QE_STORE_DISCARD);
    //         query_engine_qpush_op(q, QEDAT_DT_MAPS_TERRAIN, QE_FN_0, QE_STORE_DISCARD);

    //         gametask_new_query(game, q);
    //     }

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
    struct ToriRSRenderCommandBuffer* render_command_buffer,
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
    struct ToriRSRenderCommandBuffer* render_command_buffer)
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
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    struct GameTask* task = NULL;
    struct GIOMessage message = { 0 };

    if( input->quit )
    {
        game->running = false;
        return;
    }

    gameproto_process(game, game->io);

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

        game->cycle++;

        if( game->player_walk_animation && game->player_walk_animation->frame_count > 0 )
        {
            game->player_walk_animation->cycle++;
            if( game->player_walk_animation->cycle >=
                game->player_walk_animation
                    ->frame_lengths[game->player_walk_animation->frame_index] )
            {
                game->player_walk_animation->cycle = 0;
                game->player_walk_animation->frame_index++;
                if( game->player_walk_animation->frame_index >=
                    game->player_walk_animation->frame_count )
                {
                    game->player_walk_animation->frame_index = 0;
                }
            }
        }

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
                if( xdiff == 0 )
                {
                    if( zdiff > 0 )
                    {
                        game->player_draw_yaw = 1024;
                    }
                    else
                    {
                        game->player_draw_yaw = 0;
                    }
                }
                else if( zdiff == 0 )
                {
                    if( xdiff > 0 )
                    {
                        game->player_draw_yaw = 1536;
                    }
                    else
                    {
                        game->player_draw_yaw = 512;
                    }
                }

                else if( xdiff > 0 && zdiff > 0 )
                {
                    game->player_draw_yaw = 1024 + 256;
                }
                else if( xdiff > 0 && zdiff < 0 )
                {
                    game->player_draw_yaw = 1536 + 256;
                }
                else if( xdiff < 0 && zdiff > 0 )
                {
                    game->player_draw_yaw = 512 + 256;
                }
                else if( xdiff < 0 && zdiff < 0 )
                {
                    game->player_draw_yaw = 256;
                }
            }
            else
            {
                game->player_state = 0;
            }
        }
    }

    // tori_rs_render_command_buffer_reset(render_command_buffer);
    // struct ToriRSRenderCommand command = {
    //     .kind = TORIRS_GFX_MODEL_DRAW,
    //     ._model_draw = {
    //         .model = game->model,
    //         .position = *game->position,
    //     },
    // };
    // tori_rs_render_command_buffer_add_command(render_command_buffer, command);

    return;
}

bool
LibToriRS_GameIsRunning(struct GGame* game)
{
    return game->running;
}

void
LibToriRS_FrameBegin(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !game->scene || !game->sys_painter )
        return;

    game->at_painters_command_index = 0;
    game->at_render_command_index = 0;

    game->camera->pitch = game->camera_pitch;
    game->camera->yaw = game->camera_yaw;
    game->camera->roll = game->camera_roll;

    tori_rs_render_command_buffer_reset(render_command_buffer);

    if( game->sys_painter )
        painter_paint(
            game->sys_painter,
            game->sys_painter_buffer,
            game->camera_world_x / 128,
            game->camera_world_z / 128,
            game->camera_world_y / 240);
}

bool
LibToriRS_FrameNextCommand(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    struct ToriRSRenderCommand* command)
{
    memset(command, 0, sizeof(*command));

    struct DashPosition position = { 0 };
    struct SceneElement* element = NULL;

    if( !game->sys_painter_buffer )
        return false;

    while( command->kind == TORIRS_GFX_NONE )
    {
        if( game->at_painters_command_index >= game->sys_painter_buffer->command_count ||
            game->at_painters_command_index >= game->cc )
        {
            break;
        }

        struct PaintersElementCommand* cmd =
            &game->sys_painter_buffer->commands[game->at_painters_command_index];
        memset(&position, 0, sizeof(struct DashPosition));

        game->at_painters_command_index++;
        switch( cmd->_bf_kind )
        {
        case PNTR_CMD_ELEMENT:
        {
            element = scene_element_at(game->scene->scenery, cmd->_entity._bf_entity);
            if( !element || !element->dash_model )
                continue;
            memcpy(
                &position,
                scene_element_position(game->scene, cmd->_entity._bf_entity),
                sizeof(struct DashPosition));

            position.x = position.x - game->camera_world_x;
            position.y = position.y - game->camera_world_y;
            position.z = position.z - game->camera_world_z;

            if( element->dash_model && element->animation && element->animation->frame_count > 0 )
            {
                dashmodel_animate(
                    scene_element_model(game->scene, cmd->_entity._bf_entity),
                    element->animation->dash_frames[element->animation->frame_index],
                    element->animation->dash_framemap);
            }

            int cull = dash3d_project_model(
                game->sys_dash,
                scene_element_model(game->scene, cmd->_entity._bf_entity),
                &position,
                game->view_port,
                game->camera);
            if( cull != DASHCULL_VISIBLE )
                break;

            // if( (scene_element_interactable(game->scene, cmd->_entity._bf_entity)) )
            // {
            // Adjust mouse coordinates for dash buffer offset
            //     int mouse_x_adjusted = game->mouse_x - renderer->dash_offset_x;
            //     int mouse_y_adjusted = game->mouse_y - renderer->dash_offset_y;
            //     if( dash3d_projected_model_contains(
            //             game->sys_dash,
            //             scene_element_model(game->scene, cmd->_entity._bf_entity),
            //             game->view_port,
            //             mouse_x_adjusted,
            //             mouse_y_adjusted) )
            //     {
            //         // 1637
            //         // interacting_scene_element = cmd->_entity._bf_entity;
            //         // printf(
            //         //     "Interactable: %s\n",
            //         //     scene_element_name(game->scene, cmd->_entity._bf_entity));

            //         // for( struct SceneAction* action = element->actions; action;
            //         //      action = action->next )
            //         // {
            //         //     printf("Action: %s\n", action->action);
            //         // }

            //         // Draw AABB rectangle outline on dash buffer
            //         // AABB coordinates are in screen space relative to viewport center
            //         struct DashAABB* aabb = dash3d_projected_model_aabb(game->sys_dash);

            //         // AABB coordinates are already in viewport space (0 to viewport->width,
            //         0
            //         // to viewport->height)
            //         int db_min_x = aabb->min_screen_x;
            //         int db_min_y = aabb->min_screen_y;
            //         int db_max_x = aabb->max_screen_x;
            //         int db_max_y = aabb->max_screen_y;

            //         // // Draw on dash buffer if it exists
            //         // if( renderer->dash_buffer )
            //         // {
            //         //     // Draw top and bottom horizontal lines
            //         //     for( int x = db_min_x; x <= db_max_x; x++ )
            //         //     {
            //         //         if( x >= 0 && x < renderer->dash_buffer_width )
            //         //         {
            //         //             // Top line
            //         //             if( db_min_y >= 0 && db_min_y <
            //         renderer->dash_buffer_height
            //         //             )
            //         //             {
            //         //                 renderer->dash_buffer
            //         //                     [db_min_y * renderer->dash_buffer_width + x] =
            //         //                     0xFFFFFF;
            //         //             }
            //         //             // Bottom line
            //         //             if( db_max_y >= 0 && db_max_y <
            //         renderer->dash_buffer_height
            //         //             )
            //         //             {
            //         //                 renderer->dash_buffer
            //         //                     [db_max_y * renderer->dash_buffer_width + x] =
            //         //                     0xFFFFFF;
            //         //             }
            //         //         }
            //         //     }

            //         //     // Draw left and right vertical lines
            //         //     for( int y = db_min_y; y <= db_max_y; y++ )
            //         //     {
            //         //         if( y >= 0 && y < renderer->dash_buffer_height )
            //         //         {
            //         //             // Left line
            //         //             if( db_min_x >= 0 && db_min_x <
            //         renderer->dash_buffer_width )
            //         //             {
            //         //                 renderer->dash_buffer
            //         //                     [y * renderer->dash_buffer_width + db_min_x] =
            //         //                     0xFFFFFF;
            //         //             }
            //         //             // Right line
            //         //             if( db_max_x >= 0 && db_max_x <
            //         renderer->dash_buffer_width )
            //         //             {
            //         //                 renderer->dash_buffer
            //         //                     [y * renderer->dash_buffer_width + db_max_x] =
            //         //                     0xFFFFFF;
            //         //             }
            //         //         }
            //         //     }
            //         // }
            //     }
            // }

            *command = (struct ToriRSRenderCommand) {
                    .kind = TORIRS_GFX_MODEL_DRAW,
                    ._model_draw = {
                        .model = element->dash_model,
                        // .position = position,
                    },
                };
            memcpy(&command->_model_draw.position, &position, sizeof(struct DashPosition));

            // dash3d_raster_projected_model(
            //     game->sys_dash,
            //     scene_element_model(game->scene, cmd->_entity._bf_entity),
            //     &position,
            //     game->view_port,
            //     game->camera,
            //     renderer->dash_buffer);
        }
        break;
        case PNTR_CMD_TERRAIN:
        {
            struct SceneTerrainTile* tile_model = NULL;
            int sx = cmd->_terrain._bf_terrain_x;
            int sz = cmd->_terrain._bf_terrain_z;
            int slevel = cmd->_terrain._bf_terrain_y;

            tile_model = scene_terrain_tile_at(game->scene->terrain, sx, sz, slevel);
            if( !tile_model || !tile_model->dash_model )
                break;

            position.x = sx * 128 - game->camera_world_x;
            position.z = sz * 128 - game->camera_world_z;
            position.y = -game->camera_world_y;

            int cull = dash3d_project_model(
                game->sys_dash, tile_model->dash_model, &position, game->view_port, game->camera);
            if( cull != DASHCULL_VISIBLE )
                continue;

            // Check for tile click detection
            if( game->mouse_clicked && tile_model->dash_model )
            {
                // Project the tile model to get its screen bounds

                if( cull == DASHCULL_VISIBLE )
                {
                    // // Adjust mouse coordinates for dash buffer offset
                    // int mouse_x_adjusted = game->mouse_clicked_x - renderer->dash_offset_x;
                    // int mouse_y_adjusted = game->mouse_clicked_y - renderer->dash_offset_y;

                    // // Check if click point is within the tile's projected geometry
                    // if( dash3d_projected_model_contains(
                    //         game->sys_dash,
                    //         tile_model->dash_model,
                    //         game->view_port,
                    //         mouse_x_adjusted,
                    //         mouse_y_adjusted) )
                    // {
                    //     renderer->clicked_tile_x = sx;
                    //     renderer->clicked_tile_z = sz;
                    //     renderer->clicked_tile_level = slevel;
                    // }
                }
            }

            // dash3d_render_model(
            //     game->sys_dash,
            //     tile_model->dash_model,
            //     &position,
            //     game->view_port,
            //     game->camera,
            //     renderer->dash_buffer);
            *command = (struct ToriRSRenderCommand) {
                    .kind = TORIRS_GFX_MODEL_DRAW,
                    ._model_draw = {
                        .model = tile_model->dash_model,
                        .position = position,
                    },
                };
        }
        break;
        default:
            break;
        }
    }

    return command->kind != TORIRS_GFX_NONE;
}

void
LibToriRS_FrameEnd(struct GGame* game)
{}

int
LibToriRS_NetIsReady(struct GGame* game)
{
    return game->scene != NULL;
}

static void
push_packet_lc245_2(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    struct RevPacket_LC245_2_Item* item = malloc(sizeof(struct RevPacket_LC245_2_Item));
    memset(item, 0, sizeof(struct RevPacket_LC245_2_Item));

    item->packet = *packet;
    if( !game->packets_lc245_2 )
    {
        game->packets_lc245_2 = item;
    }
    else
    {
        struct RevPacket_LC245_2_Item* list = game->packets_lc245_2;
        while( list->next_nullable )
        {
            list = list->next_nullable;
        }
        list->next_nullable = item;
    }
}

int
LibToriRS_NetPump(struct GGame* game)
{
    int available = ringbuf_used(game->netin);
    while( available > 0 )
    {
        int ret = 1;
        char contiguous_buffer[64];

        while( ret > 0 )
        {
            switch( game->net_state )
            {
            case GAME_NET_STATE_DISCONNECTED:
                ret = 1;
                game->net_state = GAME_NET_STATE_LOGIN;
                break;
            case GAME_NET_STATE_LOGIN:
            {
                int bytes_needed = lclogin_get_bytes_needed(game->login);
                int total_read = 0;
                do
                {
                    if( bytes_needed <= 0 )
                        break;

                    int to_read;
                    if( bytes_needed > sizeof(contiguous_buffer) )
                        to_read = sizeof(contiguous_buffer);
                    else
                        to_read = bytes_needed;

                    int amount_read = ringbuf_read(game->netin, contiguous_buffer, to_read);

                    ret = lclogin_provide_data(game->login, contiguous_buffer, amount_read);
                    assert(ret == amount_read);
                    bytes_needed -= amount_read;
                    total_read += amount_read;

                    bytes_needed = lclogin_get_bytes_needed(game->login);
                } while( bytes_needed > 0 );

                ret = total_read;
            }
            break;
            case GAME_NET_STATE_GAME:
            {
                ret = ringbuf_read(game->netin, contiguous_buffer, sizeof(contiguous_buffer));
                if( ret <= 0 )
                    break;

                // FILE* f = fopen("contiguous_buffer.bin", "wb");
                // fwrite(contiguous_buffer, 1, ret, f);
                // fclose(f);
                // {
                //     size_t sz = isaac_state_size();
                //     void* buf = malloc(sz);
                //     if( buf && game->login && game->login->random_in )
                //     {
                //         isaac_get_state(game->login->random_in, buf);
                //         FILE* isaac_f = fopen("login_isaac_state.bin", "wb");
                //         if( isaac_f )
                //         {
                //             fwrite(buf, 1, sz, isaac_f);
                //             fclose(isaac_f);
                //         }
                //         free(buf);
                //     }
                // }
                // assert(0);

                int amnt_used = packetbuffer_read(game->packet_buffer, contiguous_buffer, ret);
                if( amnt_used < ret )
                {
                    ringbuf_putback(game->netin, contiguous_buffer + amnt_used, ret - amnt_used);
                }
                if( packetbuffer_ready(game->packet_buffer) )
                {
                    struct RevPacket_LC245_2 packet = { 0 };
                    int res = gameproto_parse_lc245_2(
                        game,
                        packetbuffer_packet_type(game->packet_buffer),
                        packetbuffer_data(game->packet_buffer),
                        packetbuffer_size(game->packet_buffer),
                        &packet);

                    if( res )
                        push_packet_lc245_2(game, &packet);
                    packetbuffer_reset(game->packet_buffer);
                    break;
                }
            }
            break;
            }
        }

        if( ret <= 0 )
            break;
        available = ringbuf_used(game->netin);
    }

    switch( game->net_state )
    {
    case GAME_NET_STATE_LOGIN:
    {
        int result = lclogin_process(game->login);

        if( lclogin_get_state(game->login) == LCLOGIN_STATE_SUCCESS )
        {
            game->net_state = GAME_NET_STATE_GAME;
            packetbuffer_init(
                game->packet_buffer, game->login->random_in, GAMEPROTO_REVISION_LC245_2);
        }
        else if( lclogin_get_state(game->login) == LCLOGIN_STATE_ERROR )
        {
            game->net_state = GAME_NET_STATE_DISCONNECTED;
        }
    }
    break;
    case GAME_NET_STATE_GAME:
        return 0;
    }
    return 0;
}

void
LibToriRS_NetConnect(
    struct GGame* game,
    char* username,
    char* password)
{
    game->net_state = GAME_NET_STATE_LOGIN;
    lclogin_start(game->login, username, password, false);
}

void
LibToriRS_NetRecv(
    struct GGame* game,
    uint8_t* data,
    int data_size)
{
    int ret = ringbuf_write(game->netin, data, data_size);
    assert(ret == RINGBUF_OK);
}

void
LibToriRS_NetDisconnected(struct GGame* game)
{
    ringbuf_clear(game->netin);
    ringbuf_clear(game->netout);
    game->net_state = GAME_NET_STATE_DISCONNECTED;
}

int
LibToriRS_NetGetOutgoing(
    struct GGame* game,
    uint8_t* buffer,
    int buffer_size)
{
    switch( game->net_state )
    {
    case GAME_NET_STATE_DISCONNECTED:
        return 0;
    case GAME_NET_STATE_LOGIN:
    {
        const uint8_t* data = NULL;
        int outgoing_size = lclogin_get_data_to_send(game->login, &data);
        if( data )
        {
            assert(outgoing_size <= buffer_size);
            memcpy(buffer, data, outgoing_size);
        }
        lclogin_mark_data_sent(game->login, outgoing_size);
        return outgoing_size;
    }
    case GAME_NET_STATE_GAME:
        return 0;
    }
    return 0;
}