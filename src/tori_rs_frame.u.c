#ifndef TORI_RS_FRAME_U_C
#define TORI_RS_FRAME_U_C

#include "osrs/dash_utils.h"
#include "tori_rs.h"
#include "tori_rs_render.h"

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

    LibToriRS_RenderCommandBufferReset(render_command_buffer);

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
                    },
                };
            memcpy(&command->_model_draw.position, &position, sizeof(struct DashPosition));
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

#endif