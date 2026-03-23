#ifndef TORI_RS_FRAME_U_C
#define TORI_RS_FRAME_U_C

#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/collision_map.h"
#include "osrs/dash_utils.h"
#include "osrs/isaac.h"
#include "osrs/minimenu.h"
#include "osrs/packetout.h"
#include "osrs/world_options.h"
#include "tori_rs.h"
#include "tori_rs_render.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

struct FrameRenderLoadKeyInt
{
    int key;
};

struct FrameRenderLoadKeyPtr
{
    uintptr_t key;
};

static struct DashMap*
new_frame_render_load_map(
    size_t key_size,
    size_t entry_size,
    int capacity)
{
    struct DashMapConfig config = {
        .buffer = malloc((size_t)capacity * entry_size),
        .buffer_size = (size_t)capacity * entry_size,
        .key_size = key_size,
        .entry_size = entry_size,
    };
    return dashmap_new(&config, 0);
}

static bool
mark_model_load_seen(
    struct GGame* game,
    uintptr_t model_key)
{
    if( model_key == 0 )
        return false;
    if( !game->render_loaded_model_keys )
        game->render_loaded_model_keys =
            new_frame_render_load_map(sizeof(uintptr_t), sizeof(struct FrameRenderLoadKeyPtr), 8192);
    if( dashmap_search(game->render_loaded_model_keys, &model_key, DASHMAP_FIND) )
        return false;
    struct FrameRenderLoadKeyPtr* entry =
        dashmap_search(game->render_loaded_model_keys, &model_key, DASHMAP_INSERT);
    if( !entry )
        return false;
    entry->key = model_key;
    return true;
}

static bool
mark_texture_load_seen(
    struct GGame* game,
    int texture_id)
{
    if( texture_id < 0 )
        return false;
    if( !game->render_loaded_texture_ids )
        game->render_loaded_texture_ids =
            new_frame_render_load_map(sizeof(int), sizeof(struct FrameRenderLoadKeyInt), 1024);
    if( dashmap_search(game->render_loaded_texture_ids, &texture_id, DASHMAP_FIND) )
        return false;
    struct FrameRenderLoadKeyInt* entry =
        dashmap_search(game->render_loaded_texture_ids, &texture_id, DASHMAP_INSERT);
    if( !entry )
        return false;
    entry->key = texture_id;
    return true;
}

static bool
mark_scene_element_load_seen(
    struct GGame* game,
    uintptr_t element_key)
{
    if( element_key == 0 )
        return false;
    if( !game->render_loaded_scene_element_keys )
        game->render_loaded_scene_element_keys =
            new_frame_render_load_map(sizeof(uintptr_t), sizeof(struct FrameRenderLoadKeyPtr), 8192);
    if( dashmap_search(game->render_loaded_scene_element_keys, &element_key, DASHMAP_FIND) )
        return false;
    struct FrameRenderLoadKeyPtr* entry =
        dashmap_search(game->render_loaded_scene_element_keys, &element_key, DASHMAP_INSERT);
    if( !entry )
        return false;
    entry->key = element_key;
    return true;
}

static bool
queue_model_load_if_needed(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    struct DashModel* model,
    int model_id)
{
    uintptr_t model_key = (uintptr_t)model;
    if( !mark_model_load_seen(game, model_key) )
        return false;
    LibToriRS_RenderCommandBufferAddCommand(
        render_command_buffer,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_MODEL_LOAD,
            ._model_load = {
                .model = model,
                .model_key = model_key,
                .model_id = model_id,
            },
        });
    return true;
}

static bool
queue_texture_load_if_needed(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    int texture_id,
    struct DashTexture* texture_nullable)
{
    if( !mark_texture_load_seen(game, texture_id) )
        return false;
    LibToriRS_RenderCommandBufferAddCommand(
        render_command_buffer,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_TEXTURE_LOAD,
            ._texture_load = {
                .texture_id = texture_id,
                .texture_nullable = texture_nullable,
            },
        });
    return true;
}

static bool
queue_scene_element_load_if_needed(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    struct Scene2Element* element)
{
    uintptr_t element_key = (uintptr_t)element;
    if( !mark_scene_element_load_seen(game, element_key) )
        return false;
    queue_model_load_if_needed(game, render_command_buffer, element->dash_model, -1);
    LibToriRS_RenderCommandBufferAddCommand(
        render_command_buffer,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_SCENE_ELEMENT_LOAD,
            ._scene_element_load = {
                .scene_element_id = element->id,
                .parent_entity_id = element->parent_entity_id,
                .scene_element_key = element_key,
                .model = element->dash_model,
            },
        });
    return true;
}

static void
queue_static_load_commands(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !game->buildcachedat || !game->world )
        return;

    if( game->render_load_generation_emitted == game->render_load_generation )
        return;

    uintptr_t world_key = (uintptr_t)game->world;
    if( game->render_load_world_key != world_key )
    {
        if( game->render_loaded_model_keys )
            dashmap_free(game->render_loaded_model_keys);
        if( game->render_loaded_scene_element_keys )
            dashmap_free(game->render_loaded_scene_element_keys);
        game->render_loaded_model_keys =
            new_frame_render_load_map(sizeof(uintptr_t), sizeof(struct FrameRenderLoadKeyPtr), 8192);
        game->render_loaded_scene_element_keys =
            new_frame_render_load_map(sizeof(uintptr_t), sizeof(struct FrameRenderLoadKeyPtr), 8192);
        game->render_load_world_key = world_key;
    }

    struct DashMapIter* texture_iter = buildcachedat_iter_new_textures(game->buildcachedat);
    int texture_id = -1;
    struct DashTexture* texture = NULL;
    while( (texture = buildcachedat_iter_next_texture_id(texture_iter, &texture_id)) != NULL )
        queue_texture_load_if_needed(game, render_command_buffer, texture_id, texture);
    dashmap_iter_free(texture_iter);

    if( game->world->scene2 )
    {
        struct Scene2Element* element = game->world->scene2->active_list;
        while( element )
        {
            if( element->active && element->dash_model )
                queue_scene_element_load_if_needed(game, render_command_buffer, element);
            element = element->next;
        }
    }

    game->render_load_generation_emitted = game->render_load_generation;
}

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

    world_pickset_reset(&game->pickset);

    LibToriRS_RenderCommandBufferReset(render_command_buffer);
    queue_static_load_commands(game, render_command_buffer);

    if( game->world && game->world->painter && game->sys_painter_buffer )
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
entity_map_build_loc_entity_animate(
    struct World* world,
    int map_build_loc_entity_id)
{
    struct MapBuildLocEntity* map_build_loc_entity =
        &world->map_build_loc_entities[map_build_loc_entity_id];

    struct EntityAnimation* animation = NULL;
    struct Scene2Element* scene_element = NULL;

    if( map_build_loc_entity->scene_element.element_id != -1 )
    {
        animation = &map_build_loc_entity->animation;
        scene_element =
            scene2_element_at(world->scene2, map_build_loc_entity->scene_element.element_id);

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
    }

    if( map_build_loc_entity->scene_element_two.element_id != -1 )
    {
        animation = &map_build_loc_entity->animation_two;
        scene_element =
            scene2_element_at(world->scene2, map_build_loc_entity->scene_element_two.element_id);

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
        entity_map_build_loc_entity_animate(world, entity_id_from_uid(entity_uid));
        break;
    case ENTITY_KIND_MAP_BUILD_TILE:

    default:
        return;
    }
}

static bool
entity_interactable(
    struct World* world,
    int entity_id)
{
    switch( entity_kind_from_uid(entity_id) )
    {
    case ENTITY_KIND_PLAYER:
        return true;
    case ENTITY_KIND_NPC:
        return true;
    case ENTITY_KIND_MAP_BUILD_LOC:
    {
        struct MapBuildLocEntity* map_build_loc_entity =
            &world->map_build_loc_entities[entity_id_from_uid(entity_id)];
        return map_build_loc_entity->interactable;
    }
    }
    return false;
}

struct EntityCoords
{
    int x;
    int z;
};

static void
entity_coords_from_element(
    struct World* world,
    int entity_uid,
    struct EntityCoords* coords)
{
    switch( entity_kind_from_uid(entity_uid) )
    {
    case ENTITY_KIND_PLAYER:
    {
        struct PlayerEntity* player = &world->players[entity_id_from_uid(entity_uid)];
        coords->x = player->pathing.route_x[0];
        coords->z = player->pathing.route_z[0];
    }
    break;
    case ENTITY_KIND_NPC:
    {
        struct NPCEntity* npc = &world->npcs[entity_id_from_uid(entity_uid)];
        coords->x = npc->pathing.route_x[0];
        coords->z = npc->pathing.route_z[0];
    }
    break;
    case ENTITY_KIND_MAP_BUILD_LOC:
    {
        struct MapBuildLocEntity* map_build_loc_entity =
            &world->map_build_loc_entities[entity_id_from_uid(entity_uid)];
        coords->x = map_build_loc_entity->scene_coord.sx;
        coords->z = map_build_loc_entity->scene_coord.sz;
    }
    break;
    }
}

bool
LibToriRS_FrameNextCommand(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    struct ToriRSRenderCommand* command)
{
    assert(game && render_command_buffer && command);
    memset(command, 0, sizeof(*command));
    if( !game->sys_painter_buffer || !game->sys_painter )
        return false;

    if( game->at_render_command_index < LibToriRS_RenderCommandBufferCount(render_command_buffer) )
    {
        *command =
            *LibToriRS_RenderCommandBufferAt(render_command_buffer, game->at_render_command_index);
        game->at_render_command_index++;
        return true;
    }

    struct DashPosition position = { 0 };
    struct EntityCoords coords = { 0 };
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
            bool queued_load = queue_scene_element_load_if_needed(game, render_command_buffer, element);
            memcpy(&position, element->dash_position, sizeof(struct DashPosition));

            position.x = position.x - game->camera_world_x;
            position.y = position.y - game->camera_world_y;
            position.z = position.z - game->camera_world_z;

            int cull = dash3d_project_model(
                game->sys_dash, element->dash_model, &position, game->view_port, game->camera);
            if( cull != DASHCULL_VISIBLE )
                break;

            if( queued_load )
                break;

            entity_animate(game->world, element->parent_entity_id);

            if( entity_interactable(game->world, element->parent_entity_id) )
            {
                int vp_ox = game->viewport_offset_x;
                int vp_oy = game->viewport_offset_y;
                /* Hover: add to pickset when cursor is over model (tooltip + options). */
                int cursor_vp_x = game->mouse_x - vp_ox;
                int cursor_vp_y = game->mouse_y - vp_oy;
                if( cursor_vp_x >= 0 && cursor_vp_y >= 0 && cursor_vp_x < game->view_port->width &&
                    cursor_vp_y < game->view_port->height &&
                    dash3d_projected_model_contains(
                        game->sys_dash,
                        element->dash_model,
                        game->view_port,
                        cursor_vp_x,
                        cursor_vp_y) )
                {
                    entity_coords_from_element(game->world, element->parent_entity_id, &coords);
                    world_pickset_add(
                        &game->pickset,
                        coords.x,
                        coords.z,
                        entity_kind_from_uid(element->parent_entity_id),
                        entity_id_from_uid(element->parent_entity_id));
                }
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

            struct MapBuildTileEntity* tile_entity = NULL;
            tile_entity = world_tile_entity_at(game->world, sx, sz, slevel);
            if( !tile_entity || tile_entity->scene_element.element_id == -1 )
                break;

            element = scene2_element_at(game->world->scene2, tile_entity->scene_element.element_id);
            if( !element || !element->dash_model )
                break;
            bool queued_load = queue_scene_element_load_if_needed(game, render_command_buffer, element);

            memcpy(&position, element->dash_position, sizeof(struct DashPosition));

            position.x = position.x - game->camera_world_x;
            position.y = position.y - game->camera_world_y;
            position.z = position.z - game->camera_world_z;

            int cull = dash3d_project_model(
                game->sys_dash, element->dash_model, &position, game->view_port, game->camera);
            if( cull != DASHCULL_VISIBLE )
                break;

            if( queued_load )
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
    /* Build optionset from pickset for tooltip and context menu (Client.ts menuOption /
     * drawTooltip). */
    if( game->world )
    {
        game->option_set.option_count = 0;
        world_options_add_pickset_options(game->world, &game->pickset, &game->option_set);
    }

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