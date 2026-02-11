#ifndef TORI_RS_FRAME_U_C
#define TORI_RS_FRAME_U_C

#include "graphics/dash.h"
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

    game->tile_clicked_x = -1;
    game->tile_clicked_z = -1;
    game->tile_clicked_level = -1;
    game->hovered_scene_element = NULL;

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
    struct SceneTerrainTile* tile_model = NULL;

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
                struct SceneAnimation* anim = element->animation;
                int pi = anim->frame_index;
                int si = anim->frame_index_secondary;
                if( anim->dash_frames_secondary && anim->walkmerge && pi < anim->frame_count &&
                    si >= 0 && si < anim->frame_count_secondary )
                {
                    dashmodel_animate_mask(
                        scene_element_model(game->scene, cmd->_entity._bf_entity),
                        anim->dash_frames[pi],
                        anim->dash_frames_secondary[si],
                        anim->dash_framemap,
                        anim->walkmerge);
                }
                else
                {
                    dashmodel_animate(
                        scene_element_model(game->scene, cmd->_entity._bf_entity),
                        anim->dash_frames[pi],
                        anim->dash_framemap);
                }
            }

            int cull = dash3d_project_model(
                game->sys_dash,
                scene_element_model(game->scene, cmd->_entity._bf_entity),
                &position,
                game->view_port,
                game->camera);
            if( cull != DASHCULL_VISIBLE )
                break;

            /* Client.ts: detect interactable loc, NPC, or player; check mouse hover. Last hit wins. */
            bool is_interactable =
                element->interactable || element->entity_kind == 1 || element->entity_kind == 2;
            if( is_interactable && game->view_port &&
                game->mouse_x >= 0 && game->mouse_x < game->view_port->width &&
                game->mouse_y >= 0 && game->mouse_y < game->view_port->height &&
                dash3d_projected_model_contains(
                    game->sys_dash,
                    element->dash_model,
                    game->view_port,
                    game->mouse_x,
                    game->mouse_y) )
            {
                game->hovered_scene_element = element;
            }

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

            /* Client.ts: click(mouseX-8, mouseY-11); draw tests pointInsideTriangle(World3D.mouseX,
             * World3D.mouseY). Only record tile when we have a click (taking input).
             * Only acknowledge 3D clicks within the viewport bounds (graphics3d_width x height). */
            int click_x = game->mouse_clicked_x - 8;
            int click_y = game->mouse_clicked_y - 11;
            bool in_viewport = game->view_port &&
                game->mouse_clicked_x >= 0 && game->mouse_clicked_x < game->view_port->width &&
                game->mouse_clicked_y >= 0 && game->mouse_clicked_y < game->view_port->height;
            if( in_viewport && dash3d_projected_model_contains(
                    game->sys_dash, tile_model->dash_model, game->view_port, click_x, click_y) )
            {
                game->tile_clicked_x = sx;
                game->tile_clicked_z = sz;
                game->tile_clicked_level = slevel;
            }

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

    if( game->tile_clicked_x != -1 && game->tile_clicked_z != -1 &&
        command->kind == TORIRS_GFX_NONE && !game->interface_consumed_click )
    {
        tile_model = scene_terrain_tile_at(
            game->scene->terrain,
            game->tile_clicked_x,
            game->tile_clicked_z,
            game->tile_clicked_level);
        if( !tile_model || !tile_model->dash_model )
            goto skip_highlight;

        position.x = game->tile_clicked_x * 128 - game->camera_world_x;
        position.z = game->tile_clicked_z * 128 - game->camera_world_z;
        position.y = -game->camera_world_y;

        int cull = dash3d_project_model(
            game->sys_dash, tile_model->dash_model, &position, game->view_port, game->camera);
        assert(cull == DASHCULL_VISIBLE);
        if( cull != DASHCULL_VISIBLE )
            goto skip_highlight;

        /* Client.ts: click stores mouse; draw sets clickTileX/Z. Copy to clicked_tile for next tick
         * tryMove (updateGame). */
        if( game->mouse_clicked )
        {
            game->clicked_tile_x = game->tile_clicked_x;
            game->clicked_tile_z = game->tile_clicked_z;
            game->clicked_tile_valid = 1;
        }
        game->tile_clicked_x = -1;
        game->tile_clicked_z = -1;
        game->tile_clicked_level = -1;
        *command = (struct ToriRSRenderCommand) {
            .kind = TORIRS_GFX_MODEL_DRAW_HIGHLIGHT,
            ._model_draw = {
                .model = tile_model->dash_model,
                .position = position,
            },
        };
    }
skip_highlight:;

    return command->kind != TORIRS_GFX_NONE;
}

/* Client.ts ClientProt.MOVE_GAMECLICK = 182 (index 255) */
#define MOVE_GAMECLICK_OPCODE 182

void
LibToriRS_FrameEnd(struct GGame* game)
{
    if( game->mouse_clicked && game->clicked_tile_valid && !game->interface_consumed_click &&
        GAME_NET_STATE_GAME == game->net_state )
    {
        int dest_x = game->scene_base_tile_x + game->clicked_tile_x;
        int dest_z = game->scene_base_tile_z + game->clicked_tile_z;
        /* Source = route head (Client.ts localPlayer.routeTileX[0], routeTileZ[0]). */
        struct PlayerEntity* pl = &game->players[ACTIVE_PLAYER_SLOT];
        int src_local_x = pl->pathing.route_x[0];
        int src_local_z = pl->pathing.route_z[0];
        int start_world_x = game->scene_base_tile_x + src_local_x;
        int start_world_z = game->scene_base_tile_z + src_local_z;
        if( start_world_x != dest_x || start_world_z != dest_z )
        {
            int dst_local_x = game->clicked_tile_x;
            int dst_local_z = game->clicked_tile_z;

            int path_local_x[25];
            int path_local_z[25];
            int waypoints = 0;
            int have_path = 0;

            if( game->scene && game->scene->collision_maps[0] )
            {
                waypoints = collision_map_bfs_path(
                    game->scene->collision_maps[0],
                    src_local_x,
                    src_local_z,
                    dst_local_x,
                    dst_local_z,
                    path_local_x,
                    path_local_z,
                    25);
                have_path = (waypoints >= 0);
            }

            if( have_path && waypoints > 0 )
            {
                /* Client.ts: bufferSize = Math.min(length, 25); size = bufferSize+bufferSize+3. */
                int buffer_size = waypoints + 1; /* start + waypoints steps */
                if( buffer_size > 25 )
                    buffer_size = 25;
                int steps_to_send = buffer_size - 1; /* waypoint count in packet */
                int payload_size =
                    1 + 2 + 2 + steps_to_send * 2; /* run + startX + startZ + (g1b,g1b) per step */
                if( game->outbound_size + 2 + payload_size <= (int)sizeof(game->outbound_buffer) )
                {
                    uint32_t op = (MOVE_GAMECLICK_OPCODE + isaac_next(game->random_out)) & 0xff;
                    game->outbound_buffer[game->outbound_size++] = (uint8_t)op;
                    game->outbound_buffer[game->outbound_size++] = (uint8_t)payload_size;
                    game->outbound_buffer[game->outbound_size++] = 0; /* run flag (actionKey[5]) */
                    game->outbound_buffer[game->outbound_size++] = (start_world_x >> 8) & 0xff;
                    game->outbound_buffer[game->outbound_size++] = start_world_x & 0xff;
                    game->outbound_buffer[game->outbound_size++] = (start_world_z >> 8) & 0xff;
                    game->outbound_buffer[game->outbound_size++] = start_world_z & 0xff;
                    /* Client.ts: for i=1..bufferSize-1: p1(bfsStepX-startX), p1(bfsStepZ-startZ)
                     * (g1b) */
                    for( int i = 0; i < steps_to_send && i < waypoints; i++ )
                    {
                        int dx = path_local_x[i] - src_local_x;
                        int dz = path_local_z[i] - src_local_z;
                        game->outbound_buffer[game->outbound_size++] = (uint8_t)(int8_t)dx;
                        game->outbound_buffer[game->outbound_size++] = (uint8_t)(int8_t)dz;
                    }

                    game->path_tile_count = waypoints + 1;
                    if( game->path_tile_count > GAME_PATH_TILE_MAX )
                        game->path_tile_count = GAME_PATH_TILE_MAX;
                    game->path_tile_x[0] = src_local_x;
                    game->path_tile_z[0] = src_local_z;
                    for( int i = 0; i < waypoints && i < GAME_PATH_TILE_MAX - 1; i++ )
                    {
                        game->path_tile_x[i + 1] = path_local_x[i];
                        game->path_tile_z[i + 1] = path_local_z[i];
                    }
                    /* Debug: print waypoints (scene-local tile coords). */
                    printf(
                        "[path] waypoints=%d (start + %d steps)\n",
                        game->path_tile_count,
                        waypoints);
                    for( int i = 0; i < game->path_tile_count; i++ )
                        printf(
                            "  [%d] tile=(%d,%d)\n", i, game->path_tile_x[i], game->path_tile_z[i]);
                    game->mouse_clicked = false;
                    game->clicked_tile_valid = 0;
                }
            }
            else
            {
                /* No path (e.g. blocked); consume click anyway (Client.ts clears clickTileX after
                 * tryMove). */
                game->mouse_clicked = false;
                game->clicked_tile_valid = 0;
            }
        }
        else
        {
            game->mouse_clicked = false;
            game->clicked_tile_valid = 0;
        }
    }
    else if( game->clicked_tile_valid )
    {
        game->clicked_tile_valid = 0;
    }
}

#endif