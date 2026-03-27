#ifndef TORI_RS_FRAME_U_C
#define TORI_RS_FRAME_U_C

#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/collision_map.h"
#include "osrs/dash_utils.h"
#include "osrs/isaac.h"
#include "osrs/minimap.h"
#include "osrs/minimenu.h"
#include "osrs/packetout.h"
#include "osrs/revconfig/static_ui.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/world_options.h"
#include "tori_rs.h"
#include "tori_rs_render.h"

#ifdef TORI_DEBUG_MINIMAP_FRAME
#include "graphics/dash_minimap.h"

#include <stdint.h>
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

struct FrameRenderLoadKeyPtr
{
    uint64_t key;
};

static uint64_t
model_cache_key_u64(const struct Scene2Element* element)
{
    if( !element )
        return 0;
    return ((uint64_t)(uint32_t)element->id << 24) | ((uint64_t)element->active_anim_id << 8) |
           (uint64_t)element->active_frame;
}

static void
queue_scene_element_load_from_event(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    struct Scene2Element* element)
{
    (void)game;
    uint64_t model_key = model_cache_key_u64(element);
    LibToriRS_RenderCommandBufferAddCommand(
        render_command_buffer,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_MODEL_LOAD,
            ._model_load = {
                .model = element->dash_model,
                .model_key = model_key,
                .model_id = -1,
            },
        });
}

static void
queue_texture_load_from_event(
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    int texture_id,
    struct DashTexture* texture_nullable)
{
    LibToriRS_RenderCommandBufferAddCommand(
        render_command_buffer,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_TEXTURE_LOAD,
            ._texture_load = {
                .texture_id = texture_id,
                .texture_nullable = texture_nullable,
            },
        });
}

static void
queue_sprite_load_from_event(
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    int element_id,
    struct DashSprite* sprite)
{
    LibToriRS_RenderCommandBufferAddCommand(
        render_command_buffer,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_SPRITE_LOAD,
            ._sprite_load = {
                .element_id = element_id,
                .sprite = sprite,
            },
        });
}

static void
queue_sprite_unload_from_event(
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    int element_id,
    struct DashSprite* sprite)
{
    LibToriRS_RenderCommandBufferAddCommand(
        render_command_buffer,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_SPRITE_UNLOAD,
            ._sprite_load = {
                .element_id = element_id,
                .sprite = sprite,
            },
        });
}

static void
queue_font_load_from_event(
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    int font_id,
    struct DashPixFont* font)
{
    LibToriRS_RenderCommandBufferAddCommand(
        render_command_buffer,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_FONT_LOAD,
            ._font_load = {
                .font_id = font_id,
                .font = font,
            },
        });
}

static void
queue_sprite_draw_from_event(
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    int element_id,
    struct DashSprite* sprite,
    int x,
    int y,
    int rotation_r2pi2048)
{
    struct ToriRSRenderCommand command = {
        .kind = TORIRS_GFX_SPRITE_DRAW,
        ._sprite_draw = {
            .sprite = sprite,
            .x = x,
            .y = y,
            .rotation_r2pi2048 = rotation_r2pi2048,
            .src_x = 0,
            .src_y = 0,
            .src_w = 0,
            .src_h = 0,
            .blit_dest = TORIRS_SPRITE_BLIT_FRAME,
        },
    };
    (void)element_id;
    LibToriRS_RenderCommandBufferAddCommand(render_command_buffer, command);
}

static void
queue_static_ui_minimap_draws(
    struct GGame* game,
    struct StaticUIComponent* component)
{
    struct Minimap* mm = NULL;
    if( game->world && game->world->minimap )
        mm = game->world->minimap;
    else if( game->sys_minimap )
        mm = game->sys_minimap;
    if( !mm )
        return;

    int elem_id = game->minimap_static_uiscene_element_id;
    if( elem_id < 0 )
        return;
    struct UISceneElement* element = uiscene_element_at(game->ui_scene, elem_id);
    if( !element || !element->dash_sprites || !element->dash_sprites[0] )
        return;

    struct DashSprite* static_sprite = element->dash_sprites[0];

    int camera_tile_x = game->camera_world_x / 128;
    int camera_tile_z = game->camera_world_z / 128;
    int radius = 25;
    int sw_x = camera_tile_x - radius;
    int sw_z = camera_tile_z - radius;
    int ne_x = camera_tile_x + radius;
    int ne_z = camera_tile_z + radius;
    if( sw_x < 0 )
        sw_x = 0;
    if( sw_z < 0 )
        sw_z = 0;
    if( ne_x > mm->width )
        ne_x = mm->width;
    if( ne_z > mm->height )
        ne_z = mm->height;

    int src_x = sw_x * 4;
    int src_y = (mm->height - ne_z + 1) * 4;
    int src_w = (ne_x - sw_x) * 4;
    int src_h = (ne_z - sw_z) * 4;
    if( src_w <= 0 || src_h <= 0 )
        return;

    LibToriRS_RenderCommandBufferAddCommand(
        game->ui_render_command_buffer,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_SPRITE_DRAW,
            ._sprite_draw = {
                .sprite = static_sprite,
                .x = component->position.x,
                .y = component->position.y,
                .rotation_r2pi2048 = game->camera_yaw,
                .src_x = src_x,
                .src_y = src_y,
                .src_w = src_w,
                .src_h = src_h,
                .blit_dest = TORIRS_SPRITE_BLIT_MINIMAP_WINDOW,
            },
        });

    struct MinimapRenderCommandBuffer* dyn = game->minimap_dynamic_commands;
    if( !dyn )
        return;
    minimap_render_dynamic(mm, sw_x, sw_z, ne_x, ne_z, 0, dyn);
    for( int j = 0; j < dyn->count; j++ )
    {
        if( dyn->commands[j].kind != MINIMAP_RENDER_COMMAND_LOC )
            continue;
        int li = dyn->commands[j]._loc.loc_idx;
        struct DashSprite* dot = NULL;
        switch( minimap_loc_type(mm, li) )
        {
        case MINIMAP_LOC_TYPE_PLAYER:
            dot = game->sprite_mapdot0;
            break;
        case MINIMAP_LOC_TYPE_NPC:
            dot = game->sprite_mapdot1;
            break;
        case MINIMAP_LOC_TYPE_OBJECT:
            dot = game->sprite_mapdot2;
            break;
        default:
            break;
        }
        if( !dot )
            continue;
        int lx = mm->locs[li].tile_sx;
        int lz = mm->locs[li].tile_sz;
        int dot_x = (lx - sw_x) * 4 + 2 - (dot->width >> 1);
        int dot_y = (ne_z - lz) * 4 + 2 - (dot->height >> 1);
        LibToriRS_RenderCommandBufferAddCommand(
            game->ui_render_command_buffer,
            (struct ToriRSRenderCommand){
                .kind = TORIRS_GFX_SPRITE_DRAW,
                ._sprite_draw = {
                    .sprite = dot,
                    .x = dot_x,
                    .y = dot_y,
                    .rotation_r2pi2048 = 0,
                    .src_x = 0,
                    .src_y = 0,
                    .src_w = 0,
                    .src_h = 0,
                    .blit_dest = TORIRS_SPRITE_BLIT_MINIMAP_WINDOW,
                },
            });
    }
}

static void
queue_static_load_commands(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !game->buildcachedat || !game->world )
        return;

    struct Scene2* scene2 = game->world->scene2;

    /* Do not gate this whole function on scene2 events: static UI sprite draws must be
     * repopulated every frame, and buildcache/ui event buffers are independent of scene2.
     * When buffers are empty the pop loops are no-ops. */

    struct BuildCacheDatEvent cache_event = { 0 };
    while( buildcachedat_eventbuffer_pop(game->buildcachedat, &cache_event) )
    {
        if( cache_event.type != BUILDCACHEDAT_EVENT_TEXTURE_ADDED )
            continue;
        struct DashTexture* texture =
            buildcachedat_get_texture(game->buildcachedat, cache_event.texture_id);
        if( !texture )
            continue;
        if( scene2 )
            scene2_texture_add(scene2, cache_event.texture_id, texture);
        else
            queue_texture_load_from_event(render_command_buffer, cache_event.texture_id, texture);
    }

    if( scene2 )
    {
        struct Scene2Event scene_event = { 0 };
        while( scene2_eventbuffer_pop(scene2, &scene_event) )
        {
            if( scene_event.type == SCENE2_EVENT_TEXTURE_LOADED )
            {
                struct DashTexture* texture = scene2_texture_get(scene2, scene_event.texture_id);
                queue_texture_load_from_event(
                    render_command_buffer, scene_event.texture_id, texture);
                continue;
            }
            if( scene_event.element_id < 0 || scene_event.element_id >= scene2->elements_count )
                continue;
            if( scene_event.type != SCENE2_EVENT_ELEMENT_ACQUIRED &&
                scene_event.type != SCENE2_EVENT_MODEL_CHANGED )
            {
                continue;
            }
            struct Scene2Element* element = scene2_element_at(scene2, scene_event.element_id);
            if( !element->active || !element->dash_model )
                continue;
            if( element->parent_entity_id != scene_event.parent_entity_id )
                continue;
            queue_scene_element_load_from_event(game, render_command_buffer, element);
        }
    }

    if( game->ui_scene )
    {
        struct UISceneEvent ui_event = { 0 };
        while( uiscene_eventbuffer_pop(game->ui_scene, &ui_event) )
        {
            if( ui_event.type == UISCENE_EVENT_ELEMENT_ACQUIRED )
            {
                struct UISceneElement* element =
                    uiscene_element_at(game->ui_scene, ui_event.element_id);
                if( !element || !element->dash_sprites )
                    continue;
                queue_sprite_load_from_event(
                    render_command_buffer, ui_event.element_id, element->dash_sprites);
            }
            else if( ui_event.type == UISCENE_EVENT_ELEMENT_RELEASED )
            {
                queue_sprite_unload_from_event(render_command_buffer, ui_event.element_id, NULL);
            }
            else if( ui_event.type == UISCENE_EVENT_FONT_ADDED )
            {
                struct DashPixFont* font = uiscene_font_get(game->ui_scene, ui_event.font_id);
                if( font )
                    queue_font_load_from_event(render_command_buffer, ui_event.font_id, font);
            }
        }

        LibToriRS_RenderCommandBufferReset(game->ui_render_command_buffer);
        for( int i = 0; i < game->static_ui->component_count; i++ )
        {
            struct StaticUIComponent* component = &game->static_ui->components[i];
            // printf("component->type: %s\n", static_ui_component_type_str(component->type));
            if( component->type == UIELEM_SPRITE )
            {
                struct UISceneElement* element =
                    uiscene_element_at(game->ui_scene, component->scene_id);
                if( !element || !element->dash_sprites )
                    continue;
                struct DashSprite* sprite = element->dash_sprites[component->atlas_index];
                if( !sprite )
                    continue;
                /* Only emit SPRITE_DRAW; SPRITE_LOAD is handled once via ELEMENT_ACQUIRED.
                 * The renderer does a lazy upload on cache miss as a fallback. */
                queue_sprite_draw_from_event(
                    game->ui_render_command_buffer,
                    component->scene_id,
                    sprite,
                    component->position.x,
                    component->position.y,
                    0);
            }
            else if( component->type == UIELEM_MINIMAP )
            {
                queue_static_ui_minimap_draws(game, component);
            }
        }
    }
}

void
LibToriRS_FrameBegin(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    game->at_painters_command_index = 0;
    game->at_render_command_index = 0;
    game->at_ui_render_command_index = 0;

    game->tile_clicked_x = -1;
    game->tile_clicked_z = -1;
    game->tile_clicked_level = -1;

    game->hovered_interactible_entity_uid = -1;
    game->frame_hover_font_draw_done = false;

    game->camera->pitch = game->camera_pitch;
    game->camera->yaw = game->camera_yaw;
    game->camera->roll = game->camera_roll;

    world_pickset_reset(&game->pickset);

    LibToriRS_RenderCommandBufferReset(render_command_buffer);
    if( game->minimap_dynamic_commands )
        minimap_commands_reset(game->minimap_dynamic_commands);
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
        scene_element->active_anim_id = animation->primary_anim.anim_id;
        scene_element->active_frame = (uint8_t)frame;
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
        scene_element->active_anim_id = animation->secondary_anim.anim_id;
        scene_element->active_frame = (uint8_t)frame;
        if( frame >= 0 && frame < scene_element->secondary_frames.count )
        {
            dashmodel_animate(
                scene_element->dash_model,
                scene_element->secondary_frames.frames[frame],
                scene_element->dash_framemap);
        }
    }
    else
    {
        scene_element->active_anim_id = 0;
        scene_element->active_frame = 0;
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
        scene_element->active_anim_id = animation->primary_anim.anim_id;
        scene_element->active_frame = (uint8_t)frame;
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
        scene_element->active_anim_id = animation->secondary_anim.anim_id;
        scene_element->active_frame = (uint8_t)frame;
        if( frame >= 0 && frame < scene_element->secondary_frames.count )
        {
            dashmodel_animate(
                scene_element->dash_model,
                scene_element->secondary_frames.frames[frame],
                scene_element->dash_framemap);
        }
    }
    else
    {
        scene_element->active_anim_id = 0;
        scene_element->active_frame = 0;
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
            scene_element->active_anim_id = animation->primary_anim.anim_id;
            scene_element->active_frame = (uint8_t)frame;
            if( frame >= 0 && frame < scene_element->primary_frames.count )
            {
                dashmodel_animate(
                    scene_element->dash_model,
                    scene_element->primary_frames.frames[frame],
                    scene_element->dash_framemap);
            }
        }
        else
        {
            scene_element->active_anim_id = 0;
            scene_element->active_frame = 0;
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
            scene_element->active_anim_id = animation->primary_anim.anim_id;
            scene_element->active_frame = (uint8_t)frame;
            if( frame >= 0 && frame < scene_element->primary_frames.count )
            {
                dashmodel_animate(
                    scene_element->dash_model,
                    scene_element->primary_frames.frames[frame],
                    scene_element->dash_framemap);
            }
        }
        else
        {
            scene_element->active_anim_id = 0;
            scene_element->active_frame = 0;
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
    struct ToriRSRenderCommand* command,
    bool project_models)
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
            memcpy(&position, element->dash_position, sizeof(struct DashPosition));

            position.x = position.x - game->camera_world_x;
            position.y = position.y - game->camera_world_y;
            position.z = position.z - game->camera_world_z;

            int cull = DASHCULL_VISIBLE;
            if( project_models )
            {
                cull = dash3d_project_model(
                    game->sys_dash, element->dash_model, &position, game->view_port, game->camera);
                if( cull == DASHCULL_VISIBLE &&
                    entity_interactable(game->world, element->parent_entity_id) && game->view_port )
                {
                    int cvx = game->mouse_x - game->viewport_offset_x;
                    int cvy = game->mouse_y - game->viewport_offset_y;
                    if( cvx >= 0 && cvy >= 0 && cvx < game->view_port->width &&
                        cvy < game->view_port->height )
                    {
                        struct DashAABB* aabb = dash3d_projected_model_aabb(game->sys_dash);

                        if( cvx >= aabb->min_screen_x && cvx <= aabb->max_screen_x &&
                            cvy >= aabb->min_screen_y && cvy <= aabb->max_screen_y &&
                            dash3d_projected_model_contains(
                                game->sys_dash, element->dash_model, game->view_port, cvx, cvy) )
                        {
                            game->hovered_interactible_entity_uid = element->parent_entity_id;
                        }
                    }
                }
            }
            else
            {
                cull = dash3d_cull_fast(
                    game->sys_dash, element->dash_model, &position, game->view_port, game->camera);
                if( cull == DASHCULL_VISIBLE )
                    cull = dash3d_cull_aabb(
                        game->sys_dash,
                        element->dash_model,
                        &position,
                        game->view_port,
                        game->camera);
                if( cull == DASHCULL_VISIBLE && game->view_port &&
                    entity_interactable(game->world, element->parent_entity_id) )
                {
                    int cursor_vp_x = game->mouse_x - game->viewport_offset_x;
                    int cursor_vp_y = game->mouse_y - game->viewport_offset_y;
                    bool cursor_in_viewport = cursor_vp_x >= 0 && cursor_vp_y >= 0 &&
                                              cursor_vp_x < game->view_port->width &&
                                              cursor_vp_y < game->view_port->height;
                    struct DashAABB* aabb = dash3d_projected_model_aabb(game->sys_dash);
                    bool cursor_in_aabb =
                        cursor_vp_x >= aabb->min_screen_x && cursor_vp_x <= aabb->max_screen_x &&
                        cursor_vp_y >= aabb->min_screen_y && cursor_vp_y <= aabb->max_screen_y;
                    if( cursor_in_viewport && cursor_in_aabb )
                    {
                        dash3d_project_raw(
                            game->sys_dash,
                            element->dash_model,
                            &position,
                            game->view_port,
                            game->camera);
                        if( dash3d_projected_model_contains(
                                game->sys_dash,
                                element->dash_model,
                                game->view_port,
                                cursor_vp_x,
                                cursor_vp_y) )
                        {
                            game->hovered_interactible_entity_uid = element->parent_entity_id;
                        }
                    }
                }
            }
            if( cull != DASHCULL_VISIBLE )
                break;

            entity_animate(game->world, element->parent_entity_id);

            // if( entity_interactable(game->world, element->parent_entity_id) )
            // {
            //     int vp_ox = game->viewport_offset_x;
            //     int vp_oy = game->viewport_offset_y;
            //     /* Hover: add to pickset when cursor is over model (tooltip + options). */
            //     int cursor_vp_x = game->mouse_x - vp_ox;
            //     int cursor_vp_y = game->mouse_y - vp_oy;
            //     if( cursor_vp_x >= 0 && cursor_vp_y >= 0 && cursor_vp_x <
            //     game->view_port->width
            //     &&
            //         cursor_vp_y < game->view_port->height &&
            //         dash3d_projected_model_contains(
            //             game->sys_dash,
            //             element->dash_model,
            //             game->view_port,
            //             cursor_vp_x,
            //             cursor_vp_y) )
            //     {
            //         entity_coords_from_element(game->world, element->parent_entity_id,
            //         &coords); world_pickset_add(
            //             &game->pickset,
            //             coords.x,
            //             coords.z,
            //             entity_kind_from_uid(element->parent_entity_id),
            //             entity_id_from_uid(element->parent_entity_id));
            //     }
            // }

            *command = (struct ToriRSRenderCommand) {
                    .kind = TORIRS_GFX_MODEL_DRAW,
                    ._model_draw = {
                        .model = element->dash_model,
                        .model_key = model_cache_key_u64(element),
                        .model_id = -1,
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
            // if( game->mouse_clicked && game->view_port )
            // {
            //     int vp_ox = game->viewport_offset_x;
            //     int vp_oy = game->viewport_offset_y;
            //     int click_vp_x = game->mouse_clicked_x - vp_ox;
            //     int click_vp_y = game->mouse_clicked_y - vp_oy;
            //     if( click_vp_x >= 0 && click_vp_x < game->view_port->width && click_vp_y >= 0
            //     &&
            //         click_vp_y < game->view_port->height &&
            //         dash3d_projected_model_contains(
            //             game->sys_dash,
            //             element->dash_model,
            //             game->view_port,
            //             click_vp_x,
            //             click_vp_y) )
            //     {
            //         game->tile_clicked_x = sx;
            //         game->tile_clicked_z = sz;
            //         game->tile_clicked_level = slevel;
            //     }
            // }

            *command = (struct ToriRSRenderCommand) {
                    .kind = TORIRS_GFX_MODEL_DRAW,
                    ._model_draw = {
                        .model = element->dash_model,
                        .position = position,
                        .model_key = model_cache_key_u64(element),
                        .model_id = -1,
                    },
                };
        }
        break;
        default:
            break;
        }
    }

    if( command->kind == TORIRS_GFX_NONE && game->ui_render_command_buffer &&
        game->at_ui_render_command_index <
            LibToriRS_RenderCommandBufferCount(game->ui_render_command_buffer) )
    {
        *command = *LibToriRS_RenderCommandBufferAt(
            game->ui_render_command_buffer, game->at_ui_render_command_index);
        game->at_ui_render_command_index++;
    }

    /* Copy recorded tile click to clicked_tile for FrameEnd (tryMove / MOVE_OPCLICK). */
    if( command->kind == TORIRS_GFX_NONE && game->tile_clicked_x != -1 &&
        game->tile_clicked_z != -1 && game->mouse_clicked && !game->interface_consumed_click )
    {
        game->clicked_tile_x = game->tile_clicked_x;
        game->clicked_tile_z = game->tile_clicked_z;
        game->clicked_tile_valid = 1;
    }

    /* Emit a placeholder FONT_DRAW when a hovered interactible entity was detected this frame.
     * This is the last command yielded before returning false. */
    if( command->kind == TORIRS_GFX_NONE && game->hovered_interactible_entity_uid != -1 &&
        !game->frame_hover_font_draw_done )
    {
        game->frame_hover_font_draw_done = true;
        struct DashPixFont* font = NULL;
        if( game->ui_scene )
        {
            int b12_id = uiscene_font_find_id(game->ui_scene, "b12");
            if( b12_id >= 0 )
                font = uiscene_font_get(game->ui_scene, b12_id);
            if( !font && game->ui_scene->font_count > 0 )
                font = uiscene_font_get(game->ui_scene, 0);
        }
        if( !font )
            font = game->pixfont_b12;
        if( font )
        {
            *command = (struct ToriRSRenderCommand){
                .kind = TORIRS_GFX_FONT_DRAW,
                ._font_draw = {
                    .font = font,
                    .text = (const uint8_t*)"Hello what is this?",
                    .x = game->mouse_x,
                    .y = game->mouse_y,
                    .color_rgb = 0xFFFFFF,
                },
            };
            return true;
        }
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