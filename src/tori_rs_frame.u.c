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

static void
entity_player_animate(
    struct World* world,
    int player_entity_id)
{
    struct PlayerEntity* player = &world->players[player_entity_id];
    struct EntityAnimation* animation = &player->animation;
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, player->scene_element2.element_id);
    if( !scene_element )
        return;

    if( animation->primary_anim.anim_id != -1 && scene_element->primary_frames.count > 0 )
    {
        int frame = animation->primary_anim.frame;
        if( frame >= 0 && frame < scene_element->primary_frames.count )
        {
            dashmodel_animate(
                scene_element->dash_model,
                scene_element->primary_frames.frames[frame],
                scene_element->dash_framemap);
        }
    }
    else if( animation->secondary_anim.anim_id != -1 && scene_element->secondary_frames.count > 0 )
    {
        int frame = animation->secondary_anim.frame;
        if( frame >= 0 && frame < scene_element->secondary_frames.count )
        {
            dashmodel_animate(
                scene_element->dash_model,
                scene_element->secondary_frames.frames[frame],
                scene_element->dash_framemap);
        }
    }
}

static void
entity_npc_animate(
    struct World* world,
    int npc_entity_id)
{
    struct NPCEntity* npc = &world->npcs[npc_entity_id];
    struct EntityAnimation* animation = &npc->animation;
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, npc->scene_element2.element_id);
    if( !scene_element )
        return;

    if( animation->primary_anim.anim_id != -1 && scene_element->primary_frames.count > 0 )
    {
        int frame = animation->primary_anim.frame;
        if( frame >= 0 && frame < scene_element->primary_frames.count )
        {
            dashmodel_animate(
                scene_element->dash_model,
                scene_element->primary_frames.frames[frame],
                scene_element->dash_framemap);
        }
    }
    else if( animation->secondary_anim.anim_id != -1 && scene_element->secondary_frames.count > 0 )
    {
        int frame = animation->secondary_anim.frame;
        if( frame >= 0 && frame < scene_element->secondary_frames.count )
        {
            dashmodel_animate(
                scene_element->dash_model,
                scene_element->secondary_frames.frames[frame],
                scene_element->dash_framemap);
        }
    }
}

static void
entity_animate(
    struct World* world,
    int entity_uid)
{
    switch( entity_kind_from_uid(entity_uid) )
    {
    case ENTITY_KIND_PLAYER:
        entity_player_animate(world, entity_id_from_uid(entity_uid));
        break;
    case ENTITY_KIND_NPC:
        entity_npc_animate(world, entity_id_from_uid(entity_uid));
        break;
    case ENTITY_KIND_MAP_BUILD_LOC:

    case ENTITY_KIND_MAP_BUILD_TILE:

    default:
        return;
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

            int cull = dash3d_project_model(
                game->sys_dash, element->dash_model, &position, game->view_port, game->camera);
            if( cull != DASHCULL_VISIBLE )
                break;

            entity_animate(game->world, element->parent_entity_id);

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

            int cull = dash3d_project_model(
                game->sys_dash, element->dash_model, &position, game->view_port, game->camera);
            if( cull != DASHCULL_VISIBLE )
                break;

            /* If tile was clicked this frame, record it (last hit wins). */
            if( game->mouse_clicked && game->view_port )
            {
                int vp_ox = game->viewport_offset_x;
                int vp_oy = game->viewport_offset_y;
                int click_vp_x = game->mouse_clicked_x - vp_ox;
                int click_vp_y = game->mouse_clicked_y - vp_oy;
                if( click_vp_x >= 0 && click_vp_x < game->view_port->width && click_vp_y >= 0 &&
                    click_vp_y < game->view_port->height &&
                    dash3d_projected_model_contains(
                        game->sys_dash,
                        element->dash_model,
                        game->view_port,
                        click_vp_x,
                        click_vp_y) )
                {
                    game->tile_clicked_x = sx;
                    game->tile_clicked_z = sz;
                    game->tile_clicked_level = slevel;
                }
            }

            *command = (struct ToriRSRenderCommand) {
                    .kind = TORIRS_GFX_MODEL_DRAW,
                    ._model_draw = {
                        .model = element->dash_model,
                        .position = position,
                    },
                };
        }
        break;
        default:
            break;
        }
    }

    /* Copy recorded tile click to clicked_tile for FrameEnd (tryMove / MOVE_OPCLICK). */
    if( command->kind == TORIRS_GFX_NONE && game->tile_clicked_x != -1 &&
        game->tile_clicked_z != -1 && game->mouse_clicked && !game->interface_consumed_click )
    {
        game->clicked_tile_x = game->tile_clicked_x;
        game->clicked_tile_z = game->tile_clicked_z;
        game->clicked_tile_valid = 1;
    }

    return command->kind != TORIRS_GFX_NONE;
}

/* Client.ts ClientProt.MOVE_GAMECLICK = 182 (index 255) */
#define MOVE_GAMECLICK_OPCODE 182

void
LibToriRS_FrameEnd(struct GGame* game)
{
    if( game->mouse_clicked )
    {
        if( game->interface_consumed_click )
        {
            game->mouse_cycle = -1;
            game->cross_mode = 0;
        }
        else
        {
            game->cross_mode = game->clicked_tile_valid ? 1 : 2;
            game->cross_x = game->mouse_clicked_x;
            game->cross_y = game->mouse_clicked_y;
        }
    }

    if( game->mouse_clicked && game->clicked_tile_valid && !game->interface_consumed_click &&
        GAME_NET_STATE_GAME == game->net_state && game->world && game->world->collision_map )
    {
        int dest_x = game->scene_base_tile_x + game->clicked_tile_x;
        int dest_z = game->scene_base_tile_z + game->clicked_tile_z;
        /* Source = route head (Client.ts localPlayer.routeTileX[0], routeTileZ[0]). */
        struct PlayerEntity* pl = &game->world->players[ACTIVE_PLAYER_SLOT];
        int src_local_x = pl->pathing.route_x[0];
        int src_local_z = pl->pathing.route_z[0];
        int start_world_x = game->scene_base_tile_x + src_local_x;
        int start_world_z = game->scene_base_tile_z + src_local_z;
        if( start_world_x != dest_x || start_world_z != dest_z )
        {
            int dst_local_x = game->clicked_tile_x;
            int dst_local_z = game->clicked_tile_z;
            send_move_opclick_to(
                game,
                game->world->collision_map,
                src_local_x,
                src_local_z,
                dst_local_x,
                dst_local_z);
        }
        game->mouse_clicked = false;
        game->clicked_tile_valid = 0;
    }
    else if( game->clicked_tile_valid )
    {
        game->clicked_tile_valid = 0;
    }
}

#endif