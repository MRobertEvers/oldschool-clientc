#ifndef TORI_RS_FRAME_U_C
#define TORI_RS_FRAME_U_C

#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/collision_map.h"
#include "osrs/dash_utils.h"
#include "osrs/isaac.h"
#include "osrs/minimenu.h"
#include "osrs/packetout.h"
#include "tori_rs.h"
#include "tori_rs_render.h"

#include <stdbool.h>
#include <stdio.h>

void
LibToriRS_FrameBegin(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    game->at_painters_command_index = 0;
    game->at_render_command_index = 0;

    game->tile_clicked_x = -1;
    game->tile_clicked_z = -1;
    game->tile_clicked_level = -1;
    game->hovered_scene_element = NULL;
    game->hovered_scene2_element = NULL;

    game->camera->pitch = game->camera_pitch;
    game->camera->yaw = game->camera_yaw;
    game->camera->roll = game->camera_roll;

    LibToriRS_RenderCommandBufferReset(render_command_buffer);

    if( game->world )
    {
        painter_paint(
            game->world->painter,
            game->sys_painter_buffer,
            game->camera_world_x / 128,
            game->camera_world_z / 128,
            game->camera_world_y / 240);
    }
}

bool
LibToriRS_FrameNextCommand(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    struct ToriRSRenderCommand* command)
{
    memset(command, 0, sizeof(*command));

    struct DashPosition position = { 0 };
    // struct SceneElement* element = NULL;
    // struct SceneTerrainTile* tile_model = NULL;

    struct Scene2Element* element = NULL;

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
            element = scene2_element_at(game->world->scene2, cmd->_entity._bf_entity);
            if( !element || !element->dash_model )
                continue;
            memcpy(&position, element->dash_position, sizeof(struct DashPosition));

            position.x = position.x - game->camera_world_x;
            position.y = position.y - game->camera_world_y;
            position.z = position.z - game->camera_world_z;

            // if( element->dash_model && element->animation && element->animation->frame_count > 0
            // )
            // {
            //     struct SceneAnimation* anim = element->animation;
            //     int pi = anim->frame_index;
            //     int si = anim->frame_index_secondary;
            //     if( anim->dash_frames_secondary && anim->walkmerge && pi < anim->frame_count &&
            //         si >= 0 && si < anim->frame_count_secondary )
            //     {
            //         dashmodel_animate_mask(
            //             scene_element_model(game->scene, cmd->_entity._bf_entity),
            //             anim->dash_frames[pi],
            //             anim->dash_frames_secondary[si],
            //             anim->dash_framemap,
            //             anim->walkmerge);
            //     }
            //     else
            //     {
            //         dashmodel_animate(
            //             scene_element_model(game->scene, cmd->_entity._bf_entity),
            //             anim->dash_frames[pi],
            //             anim->dash_framemap);
            //     }
            // }

            int cull = dash3d_project_model(
                game->sys_dash, element->dash_model, &position, game->view_port, game->camera);
            if( cull != DASHCULL_VISIBLE )
                break;

            /* Client.ts: detect interactable loc, NPC, or player; check mouse hover. Last hit wins.
             */
            bool is_hovered = false;
            int mouse_vp_x = game->mouse_x - game->viewport_offset_x;
            int mouse_vp_y = game->mouse_y - game->viewport_offset_y;
            if( game->view_port && mouse_vp_x >= 0 && mouse_vp_x < game->view_port->width &&
                mouse_vp_y >= 0 && mouse_vp_y < game->view_port->height &&
                dash3d_projected_model_contains(
                    game->sys_dash, element->dash_model, game->view_port, mouse_vp_x, mouse_vp_y) )
            {
                game->hovered_scene2_element = element;
                is_hovered = true;
            }

            *command = (struct ToriRSRenderCommand) {
                    .kind = TORIRS_GFX_MODEL_DRAW,
                    ._model_draw = {
                        .model = element->dash_model,
                        .is_hovered = is_hovered,
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

            struct MapBuildTileEntity* tile_entity = NULL;
            tile_entity = world_tile_entity_at(game->world, sx, sz, slevel);
            if( !tile_entity || tile_entity->scene_element.element_id == -1 )
                break;

            element = scene2_element_at(game->world->scene2, tile_entity->scene_element.element_id);
            if( !element || !element->dash_model )
                break;

            memcpy(&position, element->dash_position, sizeof(struct DashPosition));

            position.x = position.x - game->camera_world_x;
            position.y = position.y - game->camera_world_y;
            position.z = position.z - game->camera_world_z;

            // tile_model = scene_terrain_tile_at(game->scene->terrain, sx, sz, slevel);
            // if( !tile_model || !tile_model->dash_model )
            //     break;

            // position.x = sx * 128 - game->camera_world_x;
            // position.z = sz * 128 - game->camera_world_z;
            // position.y = -game->camera_world_y;

            // int cull = dash3d_project_model(
            //     game->sys_dash, tile_model->dash_model, &position, game->view_port,
            //     game->camera);
            // if( cull != DASHCULL_VISIBLE )
            //     continue;

            // /* Client.ts: click(mouseX-8, mouseY-11); draw tests
            // pointInsideTriangle(World3D.mouseX,
            //  * World3D.mouseY). Only record tile when we have a click (taking input).
            //  * Only acknowledge 3D clicks within the viewport bounds (graphics3d_width x height).
            //  */
            // int click_vp_x = game->mouse_clicked_x - game->viewport_offset_x - 8;
            // int click_vp_y = game->mouse_clicked_y - game->viewport_offset_y - 11;
            // bool in_viewport =
            //     game->view_port && game->mouse_clicked_x >= game->viewport_offset_x &&
            //     game->mouse_clicked_x < game->viewport_offset_x + game->view_port->width &&
            //     game->mouse_clicked_y >= game->viewport_offset_y &&
            //     game->mouse_clicked_y < game->viewport_offset_y + game->view_port->height;
            // if( in_viewport && dash3d_projected_model_contains(
            //                        game->sys_dash,
            //                        tile_model->dash_model,
            //                        game->view_port,
            //                        click_vp_x,
            //                        click_vp_y) )
            // {
            //     game->tile_clicked_x = sx;
            //     game->tile_clicked_z = sz;
            //     game->tile_clicked_level = slevel;
            // }

            int cull = dash3d_project_model(
                game->sys_dash, element->dash_model, &position, game->view_port, game->camera);
            if( cull != DASHCULL_VISIBLE )
                break;

            bool is_hovered = false;
            int mouse_vp_x = game->mouse_x - game->viewport_offset_x;
            int mouse_vp_y = game->mouse_y - game->viewport_offset_y;
            if( game->view_port && mouse_vp_x >= 0 && mouse_vp_x < game->view_port->width &&
                mouse_vp_y >= 0 && mouse_vp_y < game->view_port->height &&
                dash3d_projected_model_contains(
                    game->sys_dash, element->dash_model, game->view_port, mouse_vp_x, mouse_vp_y) )
            {
                game->hovered_scene2_element = element;
                is_hovered = true;
            }

            *command = (struct ToriRSRenderCommand) {
                    .kind = TORIRS_GFX_MODEL_DRAW,
                    ._model_draw = {
                        .model = element->dash_model,
                        .position = position,
                        .is_hovered = is_hovered,
                    },
                };
        }
        break;
        default:
            break;
        }
    }

    // if( game->tile_clicked_x != -1 && game->tile_clicked_z != -1 &&
    //     command->kind == TORIRS_GFX_NONE && !game->interface_consumed_click )
    // {
    //     tile_model = scene_terrain_tile_at(
    //         game->scene->terrain,
    //         game->tile_clicked_x,
    //         game->tile_clicked_z,
    //         game->tile_clicked_level);
    //     if( !tile_model || !tile_model->dash_model )
    //         goto skip_highlight;

    //     position.x = game->tile_clicked_x * 128 - game->camera_world_x;
    //     position.z = game->tile_clicked_z * 128 - game->camera_world_z;
    //     position.y = -game->camera_world_y;

    //     int cull = dash3d_project_model(
    //         game->sys_dash, tile_model->dash_model, &position, game->view_port, game->camera);
    //     assert(cull == DASHCULL_VISIBLE);
    //     if( cull != DASHCULL_VISIBLE )
    //         goto skip_highlight;

    //     /* Client.ts: click stores mouse; draw sets clickTileX/Z. Copy to clicked_tile for next
    //     tick
    //      * tryMove (updateGame). */
    //     if( game->mouse_clicked )
    //     {
    //         game->clicked_tile_x = game->tile_clicked_x;
    //         game->clicked_tile_z = game->tile_clicked_z;
    //         game->clicked_tile_valid = 1;
    //     }
    //     game->tile_clicked_x = -1;
    //     game->tile_clicked_z = -1;
    //     game->tile_clicked_level = -1;
    //     *command = (struct ToriRSRenderCommand) {
    //         .kind = TORIRS_GFX_MODEL_DRAW_HIGHLIGHT,
    //         ._model_draw = {
    //             .model = tile_model->dash_model,
    //             .position = position,
    //         },
    //     };
    // }
skip_highlight:;

    return command->kind != TORIRS_GFX_NONE;
}

/* Client.ts ClientProt.MOVE_GAMECLICK = 182 (index 255) */
#define MOVE_GAMECLICK_OPCODE 182

void
LibToriRS_FrameEnd(struct GGame* game)
{
    /* Client.ts: if mouse outside menu bounds, menuVisible = false. Close minimenu when mouse
     * moves off the menu. */
    if( game->menu_visible && game->menu_area == 0 && game->view_port )
    {
        int mouse_vp_x = game->mouse_x - game->viewport_offset_x;
        int mouse_vp_y = game->mouse_y - game->viewport_offset_y;
        if( mouse_vp_x < game->menu_x - 10 || mouse_vp_x > game->menu_x + game->menu_width + 10 ||
            mouse_vp_y < game->menu_y - 10 || mouse_vp_y > game->menu_y + game->menu_height + 10 )
        {
            game->menu_visible = 0;
        }
    }

    /* Client.ts crossMode: yellow (1) when tile clicked, red (2) when viewport clicked but not
     * tile. No cross when clicking on 2D interface. */
    if( game->mouse_clicked && game->view_port )
    {
        int vp_ox = game->viewport_offset_x;
        int vp_oy = game->viewport_offset_y;
        int in_viewport = game->mouse_clicked_x >= vp_ox &&
                          game->mouse_clicked_x < vp_ox + game->view_port->width &&
                          game->mouse_clicked_y >= vp_oy &&
                          game->mouse_clicked_y < vp_oy + game->view_port->height;
        if( game->interface_consumed_click || !in_viewport )
        {
            game->mouse_cycle = -1;
            game->cross_mode = 0;
        }
        else
        {
            game->cross_mode = game->clicked_tile_valid ? 1 : 2;
            game->cross_x = game->mouse_clicked_x - vp_ox;
            game->cross_y = game->mouse_clicked_y - vp_oy;
        }
    }

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

bool
LibToriRS_FindTileAtViewport(
    struct GGame* game,
    int vp_x,
    int vp_y,
    int* out_tile_x,
    int* out_tile_z,
    int* out_level)
{
    if( !game->sys_painter_buffer || !game->sys_dash || !game->view_port || !game->camera ||
        !out_tile_x || !out_tile_z || !out_level )
        return false;

    int click_x = vp_x - 8;
    int click_y = vp_y - 11;

    for( int i = 0; i < game->sys_painter_buffer->command_count && i < game->cc; i++ )
    {
        struct PaintersElementCommand* cmd = &game->sys_painter_buffer->commands[i];
        if( cmd->_bf_kind != PNTR_CMD_TERRAIN )
            continue;

        int sx = cmd->_terrain._bf_terrain_x;
        int sz = cmd->_terrain._bf_terrain_z;
        int slevel = cmd->_terrain._bf_terrain_y;
    }
    return false;
}

#endif