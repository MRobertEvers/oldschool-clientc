#ifndef TORI_RS_FRAME_U_C
#define TORI_RS_FRAME_U_C

#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/collision_map.h"
#include "osrs/dash_utils.h"
#include "osrs/game.h"
#include "osrs/interface.h"
#include "osrs/interface_state.h"
#include "osrs/isaac.h"
#include "osrs/minimap.h"
#include "osrs/minimenu.h"
#include "osrs/packetout.h"
#include "osrs/revconfig/static_ui.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/world_options.h"
#include "tori_rs.h"
#include "tori_rs_render.h"

#ifdef TORI_DEBUG_MINIMAP_FRAME
#include "graphics/dash_minimap.h"

#include <stdint.h>
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
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
    int src_bb_x = 0;
    int src_bb_y = 0;
    int src_bb_w = 0;
    int src_bb_h = 0;
    int src_anchor_x = sprite->crop_width >> 1;
    int src_anchor_y = sprite->crop_height >> 1;
    if( sprite->crop_width > 0 && sprite->crop_height > 0 )
    {
        src_bb_x = sprite->crop_x;
        src_bb_y = sprite->crop_y;
        src_bb_w = sprite->crop_width;
        src_bb_h = sprite->crop_height;
        if( !src_anchor_x && !src_anchor_y )
        {
            src_anchor_x = sprite->crop_x + (sprite->crop_width >> 1);
            src_anchor_y = sprite->crop_y + (sprite->crop_height >> 1);
        }
    }

    struct ToriRSRenderCommand command = {
        .kind = TORIRS_GFX_SPRITE_DRAW,
        ._sprite_draw = {
            .sprite = sprite,
            .dst_bb_x = x,
            .dst_bb_y = y,
            .src_anchor_x = src_anchor_x,
            .src_anchor_y = src_anchor_y,
            .rotation_r2pi2048 = rotation_r2pi2048,
            .src_bb_x = src_bb_x,
            .src_bb_y = src_bb_y,
            .src_bb_w = src_bb_w,
            .src_bb_h = src_bb_h,
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

    int anchor_x = 0;
    int anchor_y = 0;

    camera_tile_x = game->camera_world_x / 128;
    camera_tile_z = game->camera_world_z / 128;

    anchor_x = camera_tile_x * (static_sprite->width / 104);
    anchor_y = static_sprite->height - camera_tile_z * (static_sprite->height / 104);

    LibToriRS_RenderCommandBufferAddCommand(
        game->uiscene_queued_commands,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_SPRITE_DRAW,
            ._sprite_draw = {
                .sprite = static_sprite,
                .dst_anchor_x = component->position.anchor_x,
                .dst_anchor_y = component->position.anchor_y,
                .dst_bb_x = component->position.x,
                .dst_bb_y = component->position.y,
                .dst_bb_w = component->position.width,
                .dst_bb_h = component->position.height,
                .rotated = true,
                .rotation_r2pi2048 = (( game->camera_yaw)& 0x7ff),
                .src_bb_x = 0,
                .src_bb_y = 0,
                .src_bb_w = static_sprite->crop_width,
                .src_bb_h = static_sprite->crop_height,
                .src_anchor_x = anchor_x,
                .src_anchor_y = anchor_y,
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
            // dot = game->sprite_mapdot0;
            break;
        case MINIMAP_LOC_TYPE_NPC:
            // dot = game->sprite_mapdot1;
            break;
        case MINIMAP_LOC_TYPE_OBJECT:
            // dot = game->sprite_mapdot2;
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
            game->uiscene_queued_commands,
            (struct ToriRSRenderCommand){
                .kind = TORIRS_GFX_SPRITE_DRAW,
                ._sprite_draw = {
                    .sprite = dot,
                    .dst_bb_x = dot_x,
                    .dst_bb_y = dot_y,
                    .dst_bb_w = dot->width,
                    .dst_bb_h = dot->height,
                    .rotation_r2pi2048 = 0,
                    .src_bb_x = 0,
                    .src_bb_y = 0,
                    .src_bb_w = 0,
                    .src_bb_h = 0,
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

    game->interface_consumed_click = 0;

    game->tile_clicked_x = -1;
    game->tile_clicked_z = -1;
    game->tile_clicked_level = -1;

    game->camera->pitch = game->camera_pitch;
    game->camera->yaw = game->camera_yaw;
    game->camera->roll = game->camera_roll;

    game->uiscene_idx = 0;

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

struct UIStep
{
    bool done;
};

static struct DashSprite*
uiscene_redstone_sprite_for_tab(
    struct GGame* game,
    int tab)
{
    if( !game->ui_scene || tab < 0 || tab > 13 )
        return NULL;
    if( tab <= 6 )
    {
        const char* n = NULL;
        switch( tab )
        {
        case 0:
            n = "redstone1";
            break;
        case 1:
            n = "redstone2";
            break;
        case 2:
            n = "redstone2";
            break;
        case 3:
            n = "redstone3";
            break;
        case 4:
            n = "redstone2h";
            break;
        case 5:
            n = "redstone2h";
            break;
        case 6:
            n = "redstone1h";
            break;
        default:
            break;
        }
        return n ? uiscene_sprite_by_name(game->ui_scene, n, 0) : NULL;
    }
    else
    {
        const char* n = NULL;
        switch( tab )
        {
        case 7:
            n = "redstone1v";
            break;
        case 8:
            n = "redstone2v";
            break;
        case 9:
            n = "redstone2v";
            break;
        case 10:
            n = "redstone3v";
            break;
        case 11:
            n = "redstone2hv";
            break;
        case 12:
            n = "redstone2hv";
            break;
        case 13:
            n = "redstone1hv";
            break;
        default:
            break;
        }
        return n ? uiscene_sprite_by_name(game->ui_scene, n, 0) : NULL;
    }
}

static void
uielem_tab_redstones_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct UIStep* step)
{
    assert(component->type == UIELEM_TAB_REDSTONES);
    step->done = true;
    if( !game->iface || !game->ui_scene )
        return;
    if( game->iface->sidebar_interface_id != -1 )
        return;

    int tab = game->iface->selected_tab;
    struct DashSprite* sp = uiscene_redstone_sprite_for_tab(game, tab);
    if( !sp )
        return;

    int bind_top_x = component->position.x;
    int bind_top_y = component->position.y;
    int bind_bot_x = component->hitbox_x;
    int bind_bot_y = component->hitbox_y;
    int rx = 0, ry = 0;
    if( tab >= 0 && tab <= 6 )
    {
        static const int ox[7] = { 22, 54, 82, 110, 153, 181, 209 };
        static const int oy[7] = { 10, 8, 8, 8, 8, 8, 9 };
        rx = ox[tab];
        ry = oy[tab];
        queue_sprite_draw_from_event(
            game->uiscene_queued_commands, -1, sp, bind_top_x + rx, bind_top_y + ry, 0);
    }
    else if( tab >= 7 && tab <= 13 )
    {
        static const int ox[7] = { 42, 74, 102, 130, 173, 201, 229 };
        static const int oy[7] = { 0, 0, 0, 1, 0, 0, 0 };
        int i = tab - 7;
        rx = ox[i];
        ry = oy[i];
        queue_sprite_draw_from_event(
            game->uiscene_queued_commands, -1, sp, bind_bot_x + rx, bind_bot_y + ry, 0);
    }

    static const struct
    {
        int x, y, w, h;
    } k_tab_hit[14] = {
        { 541, 169, 26, 24 },
        { 566, 168, 26, 24 },
        { 594, 168, 28, 24 },
        { 631, 172, 30, 24 },
        { 669, 169, 26, 24 },
        { 696, 171, 28, 24 },
        { 724, 173, 28, 24 },
        { 570, 468, 26, 24 },
        { 598, 469, 28, 24 },
        { 633, 470, 30, 24 },
        { 670, 468, 28, 24 },
        { 697, 468, 28, 24 },
        { 722, 468, 28, 24 },
        { 748, 468, 26, 24 },
    };

    if( game->mouse_clicked && !game->interface_consumed_click )
    {
        int cx = game->mouse_clicked_x;
        int cy = game->mouse_clicked_y;
        for( int i = 0; i < 14; i++ )
        {
            int hx = k_tab_hit[i].x;
            int hy = k_tab_hit[i].y;
            int hw = k_tab_hit[i].w;
            int hh = k_tab_hit[i].h;
            if( cx >= hx && cx < hx + hw && cy >= hy && cy < hy + hh )
            {
                game->iface->selected_tab = i;
                game->interface_consumed_click = 1;
                break;
            }
        }
    }
}

static void
uielem_builtin_sidebar_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct UIStep* step)
{
    assert(component->type == UIELEM_BUILTIN_SIDEBAR);
    step->done = true;
    if( !game->iface || !game->buildcachedat || !game->sys_dash || !game->iface_view_port )
        return;
    if( game->iface->sidebar_interface_id != -1 )
        return;

    int tab = game->iface->selected_tab;
    if( tab < 0 || tab > 13 )
        return;
    int root_id = game->iface->tab_interface_id[tab];
    if( root_id < 0 )
        return;

    struct CacheDatConfigComponent* root =
        buildcachedat_get_component(game->buildcachedat, root_id);
    if( !root || root->type != COMPONENT_TYPE_LAYER )
        return;

    int w = component->position.width;
    int h = component->position.height;
    if( w <= 0 )
        w = 190;
    if( h <= 0 )
        h = 261;

    int sbx = component->position.x;
    int sby = component->position.y;

    // if( game->iface_sidebar_sprite )
    // {
    //     LibToriRS_RenderCommandBufferAddCommand(
    //         game->uiscene_queued_commands,
    //         (struct ToriRSRenderCommand){
    //             .kind = TORIRS_GFX_SPRITE_UNLOAD,
    //             ._sprite_load = { .element_id = -1, .sprite = game->iface_sidebar_sprite },
    //     });
    // }

    size_t pix_count = (size_t)w * (size_t)h;
    uint32_t* px = (uint32_t*)malloc(pix_count * sizeof(uint32_t));
    if( !px )
        return;
    memset(px, 0, pix_count * sizeof(uint32_t));

    struct DashViewPort saved_vp = *game->iface_view_port;
    dash2d_set_bounds(game->iface_view_port, 0, 0, w, h);
    game->iface_view_port->width = w;
    game->iface_view_port->height = h;
    game->iface_view_port->stride = w;
    game->iface_view_port->x_center = w / 2;
    game->iface_view_port->y_center = h / 2;

    game->iface->current_hovered_interface_id = -1;
    // (void)interface_find_hovered_interface_id(game, root, sbx, sby, game->mouse_x,
    // game->mouse_y);

    interface_draw_component_layer(game, root, 0, 0, 0, (int*)px, w);

    *game->iface_view_port = saved_vp;

    // if( game->iface_sidebar_sprite )
    //     dashsprite_free(game->iface_sidebar_sprite);
    // game->iface_sidebar_sprite = dashsprite_new_from_argb_owned(px, w, h);
    // if( !game->iface_sidebar_sprite )
    //     return;

    // LibToriRS_RenderCommandBufferAddCommand(
    //     game->uiscene_queued_commands,
    //     (struct ToriRSRenderCommand){
    //         .kind = TORIRS_GFX_SPRITE_LOAD,
    //         ._sprite_load = { .element_id = -1, .sprite = game->iface_sidebar_sprite },
    // });

    // LibToriRS_RenderCommandBufferAddCommand(
    //     game->uiscene_queued_commands,
    //     (struct ToriRSRenderCommand){
    //         .kind = TORIRS_GFX_SPRITE_DRAW,
    //         ._sprite_draw = {
    //             .sprite = game->iface_sidebar_sprite,
    //             .dst_bb_x = sbx,
    //             .dst_bb_y = sby,
    //             .dst_bb_w = w,
    //             .dst_bb_h = h,
    //             .rotation_r2pi2048 = 0,
    //             .src_bb_x = 0,
    //             .src_bb_y = 0,
    //             .src_bb_w = 0,
    //             .src_bb_h = 0,
    //         },
    //     });
}

static void
uielem_sprite_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct UIStep* step)
{
    assert(component->type == UIELEM_SPRITE);

    struct UISceneElement* element = uiscene_element_at(game->ui_scene, component->scene_id);
    if( !element )
        return;

    struct DashSprite* sprite = element->dash_sprites[component->atlas_index];
    if( !sprite )
        return;

    queue_sprite_draw_from_event(
        game->uiscene_queued_commands,
        component->scene_id,
        sprite,
        component->position.x,
        component->position.y,
        0);

    step->done = true;
}

static void
uielem_world_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct UIStep* step,
    bool project_models)
{
    assert(component->type == UIELEM_WORLD);

    struct DashPosition position = { 0 };
    struct ToriRSRenderCommand command = { 0 };
    struct Scene2Element* scene_element = NULL;
    struct UISceneElement* element = uiscene_element_at(game->ui_scene, component->scene_id);
    if( !element )
        return;

    if( game->at_painters_command_index >= game->sys_painter_buffer->command_count )
    {
        step->done = true;
        return;
    }

    step->done = false;

    struct PaintersElementCommand* cmd =
        &game->sys_painter_buffer->commands[game->at_painters_command_index];

    game->at_painters_command_index++;

    switch( cmd->_bf_kind )
    {
    case PNTR_CMD_ELEMENT:
    {
        scene_element = scene2_element_at(game->world->scene2, cmd->_entity._bf_entity);
        if( !scene_element || !scene_element->dash_model )
            return;
        memcpy(&position, scene_element->dash_position, sizeof(struct DashPosition));

        position.x = position.x - game->camera_world_x;
        position.y = position.y - game->camera_world_y;
        position.z = position.z - game->camera_world_z;

        int cull = DASHCULL_VISIBLE;
        // if( project_models )
        // {
        cull = dash3d_project_model(
            game->sys_dash, scene_element->dash_model, &position, game->view_port, game->camera);
        // if( cull == DASHCULL_VISIBLE &&
        //     entity_interactable(game->world, scene_element->parent_entity_id) && game->view_port
        //     )
        // {
        //     int cvx = game->mouse_x - game->viewport_offset_x;
        //     int cvy = game->mouse_y - game->viewport_offset_y;
        //     if( cvx >= 0 && cvy >= 0 && cvx < game->view_port->width &&
        //         cvy < game->view_port->height )
        //     {
        //         struct DashAABB* aabb = dash3d_projected_model_aabb(game->sys_dash);

        //         if( cvx >= aabb->min_screen_x && cvx <= aabb->max_screen_x &&
        //             cvy >= aabb->min_screen_y && cvy <= aabb->max_screen_y &&
        //             dash3d_projected_model_contains(
        //                 game->sys_dash, scene_element->dash_model, game->view_port, cvx, cvy) )
        //         {
        //         }
        //     }
        // }
        // }
        // else
        // {
        //     cull = dash3d_cull_fast(
        //         game->sys_dash,
        //         scene_element->dash_model,
        //         &position,
        //         game->view_port,
        //         game->camera);
        //     if( cull == DASHCULL_VISIBLE )
        //         cull = dash3d_cull_aabb(
        //             game->sys_dash,
        //             scene_element->dash_model,
        //             &position,
        //             game->view_port,
        //             game->camera);
        //     if( cull == DASHCULL_VISIBLE && game->view_port &&
        //         entity_interactable(game->world, scene_element->parent_entity_id) )
        //     {
        //         int cursor_vp_x = game->mouse_x - game->viewport_offset_x;
        //         int cursor_vp_y = game->mouse_y - game->viewport_offset_y;
        //         bool cursor_in_viewport = cursor_vp_x >= 0 && cursor_vp_y >= 0 &&
        //                                   cursor_vp_x < game->view_port->width &&
        //                                   cursor_vp_y < game->view_port->height;
        //         struct DashAABB* aabb = dash3d_projected_model_aabb(game->sys_dash);
        //         bool cursor_in_aabb =
        //             cursor_vp_x >= aabb->min_screen_x && cursor_vp_x <= aabb->max_screen_x &&
        //             cursor_vp_y >= aabb->min_screen_y && cursor_vp_y <= aabb->max_screen_y;
        //         if( cursor_in_viewport && cursor_in_aabb )
        //         {
        //             dash3d_project_raw(
        //                 game->sys_dash,
        //                 scene_element->dash_model,
        //                 &position,
        //                 game->view_port,
        //                 game->camera);
        //             if( dash3d_projected_model_contains(
        //                     game->sys_dash,
        //                     scene_element->dash_model,
        //                     game->view_port,
        //                     cursor_vp_x,
        //                     cursor_vp_y) )
        //             {
        //                 game->hovered_interactible_entity_uid = scene_element->parent_entity_id;
        //             }
        //         }
        //     }
        // }
        if( cull != DASHCULL_VISIBLE )
            break;

        entity_animate(game->world, scene_element->parent_entity_id);

        command.kind = TORIRS_GFX_MODEL_DRAW;
        command._model_draw.model = scene_element->dash_model;
        command._model_draw.model_key = model_cache_key_u64(scene_element);
        command._model_draw.model_id = -1;
        memcpy(&command._model_draw.position, &position, sizeof(struct DashPosition));
        LibToriRS_RenderCommandBufferAddCommand(game->uiscene_queued_commands, command);
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

        scene_element =
            scene2_element_at(game->world->scene2, tile_entity->scene_element.element_id);
        if( !scene_element || !scene_element->dash_model )
            break;

        memcpy(&position, scene_element->dash_position, sizeof(struct DashPosition));

        position.x = position.x - game->camera_world_x;
        position.y = position.y - game->camera_world_y;
        position.z = position.z - game->camera_world_z;

        int cull = dash3d_project_model(
            game->sys_dash, scene_element->dash_model, &position, game->view_port, game->camera);
        if( cull != DASHCULL_VISIBLE )
            break;

        command.kind = TORIRS_GFX_MODEL_DRAW;
        command._model_draw.model = scene_element->dash_model;
        command._model_draw.model_key = model_cache_key_u64(scene_element);
        command._model_draw.model_id = -1;
        memcpy(&command._model_draw.position, &position, sizeof(struct DashPosition));
        LibToriRS_RenderCommandBufferAddCommand(game->uiscene_queued_commands, command);
    }
    break;
    default:
        break;
    }
}

static void
uielem_minimap_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct UIStep* step)
{
    assert(component->type == UIELEM_MINIMAP);

    struct UISceneElement* element = uiscene_element_at(game->ui_scene, component->scene_id);
    if( !element )
        return;

    queue_static_ui_minimap_draws(game, component);
    step->done = true;
}

static void
uielem_compass_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct UIStep* step)
{
    assert(component->type == UIELEM_COMPASS);

    struct UISceneElement* element = uiscene_element_at(game->ui_scene, component->scene_id);
    if( !element )
        return;

    struct DashSprite* sprite = element->dash_sprites[component->atlas_index];
    if( !sprite )
        return;

    struct ToriRSRenderCommand command;
    command.kind = TORIRS_GFX_SPRITE_DRAW;
    command._sprite_draw.rotated = true;
    command._sprite_draw.sprite = sprite;
    command._sprite_draw.dst_bb_x = component->position.x;
    command._sprite_draw.dst_bb_y = component->position.y;
    command._sprite_draw.dst_bb_w = component->position.width;
    command._sprite_draw.dst_bb_h = component->position.height;
    command._sprite_draw.dst_anchor_x = component->position.anchor_x;
    command._sprite_draw.dst_anchor_y = component->position.anchor_y;
    command._sprite_draw.src_bb_x = sprite->crop_x;
    command._sprite_draw.src_bb_y = sprite->crop_y;
    command._sprite_draw.src_bb_w = sprite->crop_width;
    command._sprite_draw.src_bb_h = sprite->crop_height;
    command._sprite_draw.src_anchor_x = sprite->crop_width >> 1;
    command._sprite_draw.src_anchor_y = sprite->crop_height >> 1;
    command._sprite_draw.rotation_r2pi2048 = ((game->camera_yaw) & 0x7ff);

    LibToriRS_RenderCommandBufferAddCommand(game->uiscene_queued_commands, command);

    step->done = true;
}

bool
LibToriRS_FrameNextCommand(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    struct ToriRSRenderCommand* command,
    bool project_models)
{
    struct StaticUIComponent* component = NULL;
    struct ToriRSRenderCommand* cmd = NULL;
    struct UIStep step = { 0 };
    while( true )
    {
        if( game->uiscene_idx >= game->ui_scene_buffer->component_count )
            return false;

        if( game->uiscene_command_idx < game->uiscene_queued_commands->command_count )
        {
            cmd = LibToriRS_RenderCommandBufferAt(
                game->uiscene_queued_commands, game->uiscene_command_idx);
            memcpy(command, cmd, sizeof(struct ToriRSRenderCommand));
            game->uiscene_command_idx++;

            return true;
        }
        else if( game->step_done )
        {
            game->uiscene_idx++;
            memset(&step, 0, sizeof(step));

            LibToriRS_RenderCommandBufferReset(game->uiscene_queued_commands);
            game->uiscene_command_idx = 0;
            game->step_done = false;
            continue;
        }

        step.done = true;

        component = &game->ui_scene_buffer->components[game->uiscene_idx];

        int component_command_count = 0;
        switch( component->type )
        {
        case UIELEM_SPRITE:
            uielem_sprite_step(game, component, &step);
            break;
        case UIELEM_WORLD:
            uielem_world_step(game, component, &step, project_models);
            break;
        case UIELEM_MINIMAP:
            uielem_minimap_step(game, component, &step);
            break;
        case UIELEM_COMPASS:
            uielem_compass_step(game, component, &step);
            break;
        case UIELEM_TAB_REDSTONES:
            uielem_tab_redstones_step(game, component, &step);
            break;
        case UIELEM_BUILTIN_SIDEBAR:
            uielem_builtin_sidebar_step(game, component, &step);
            break;
        default:
            break;
        }

        game->step_done = step.done;
    }

    return false;
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
}

#endif