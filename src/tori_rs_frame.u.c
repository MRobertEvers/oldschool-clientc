#ifndef TORI_RS_FRAME_U_C
#define TORI_RS_FRAME_U_C

/** Draw radius used when resolving baked cullmap; must be covered by batch_cullmaps.mjs radii. */
#define TORI_RS_PAINTERS_CULLMAP_RADIUS 25

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
#include "osrs/obj_icon.h"
#include "osrs/packetout.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/rs_component_gfx.h"
#include "osrs/rs_component_state.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/scene2.h"
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
#include <string.h>
#include <time.h>

/** Set per `LibToriRS_FrameNextCommand` call for RS model culling. */
static bool s_frame_project_models;

/** Tab sidebar content (RS subtree) only when this tab is selected and no modal owns the sidebar.
 */
static bool
frame_sidebar_tab_active(
    struct GGame* game,
    struct StaticUIComponent* sidebar)
{
    assert(sidebar && sidebar->type == UIELEM_BUILTIN_SIDEBAR);
    if( !game || !game->iface )
        return false;
    if( game->iface->sidebar_interface_id != -1 )
        return false;
    return game->iface->selected_tab == sidebar->u.sidebar.tabno;
}

static bool
frame_uitree_should_descend(
    struct GGame* game,
    struct StaticUIComponent* c)
{
    if( c->first_child < 0 )
        return false;
    if( c->type == UIELEM_BUILTIN_SIDEBAR )
        return frame_sidebar_tab_active(game, c);
    return true;
}

static void
frame_uitree_advance_after_step(
    struct GGame* game,
    int32_t stepped_index)
{
    struct UITree* t = game->ui_root_buffer;
    struct StaticUIComponent* c = &t->components[stepped_index];

    if( frame_uitree_should_descend(game, c) )
    {
        if( game->uitree_stack_top + 1 >= UITREE_TRAVERSAL_STACK_MAX )
        {
            game->uitree_current = -1;
            return;
        }
        game->uitree_stack[++game->uitree_stack_top] = stepped_index;
        game->uitree_current = c->first_child;
        return;
    }
    if( c->next_sibling >= 0 )
    {
        game->uitree_current = c->next_sibling;
        return;
    }
    while( game->uitree_stack_top >= 0 )
    {
        int32_t pidx = game->uitree_stack[game->uitree_stack_top--];
        struct StaticUIComponent* p = &t->components[pidx];
        if( p->next_sibling >= 0 )
        {
            game->uitree_current = p->next_sibling;
            return;
        }
    }
    game->uitree_current = -1;
}

static void
rs_uielem_push_iface_viewport(
    struct GGame* game,
    int w,
    int h,
    struct DashViewPort* out_saved)
{
    *out_saved = *game->iface_view_port;
    dash2d_set_bounds(game->iface_view_port, 0, 0, w, h);
    game->iface_view_port->width = w;
    game->iface_view_port->height = h;
    game->iface_view_port->stride = w;
    game->iface_view_port->x_center = w / 2;
    game->iface_view_port->y_center = h / 2;
}

static void
rs_uielem_pop_iface_viewport(
    struct GGame* game,
    struct DashViewPort* saved)
{
    *game->iface_view_port = *saved;
}

/** Root RS layers from buildcachedat are for modal overlays only (not tab sidebars). */
static bool
rs_root_is_active_modal(
    struct GGame* game,
    int component_id)
{
    if( !game->iface )
        return false;
    return game->iface->sidebar_interface_id == component_id ||
           game->iface->viewport_interface_id == component_id ||
           game->iface->chat_interface_id == component_id;
}

struct FrameRenderLoadKeyPtr
{
    uint64_t key;
};

static uint64_t
model_cache_key_u64(
    struct Scene2* scene2,
    const struct Scene2Element* element)
{
    if( !element || !scene2 )
        return 0;
    int element_id = scene2_element_id((struct Scene2*)scene2, element);
    return ((uint64_t)(uint32_t)element_id << 24) |
           ((uint64_t)scene2_element_active_anim_id(element) << 8) |
           (uint64_t)scene2_element_active_frame(element);
}

static void
queue_scene_element_load_from_event(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    struct Scene2Element* element)
{
    (void)game;
    uint64_t model_key = model_cache_key_u64(game->world->scene2, element);
    LibToriRS_RenderCommandBufferAddCommand(
        render_command_buffer,
        (struct ToriRSRenderCommand){
            .kind = TORIRS_GFX_MODEL_LOAD,
            ._model_load = {
                .model = scene2_element_dash_model(element),
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

    struct UISceneElement* element =
        uiscene_element_at(game->ui_scene, component->u.sprite.scene_id);
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
    minimap_render_dynamic(mm, sw_x, sw_z, ne_x, ne_z, dyn);
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
    if( !game->buildcachedat )
        return;

    /* Textures are registered on game->scene2 during load (same pointer as world->scene2). */
    struct Scene2* scene2 = game->scene2;

    /* Do not gate this whole function on scene2 events: static UI sprite draws must be
     * repopulated every frame, and buildcache/ui event buffers are independent of scene2.
     * When buffers are empty the pop loops are no-ops. */

    if( scene2 )
    {
        struct Scene2Event scene_event = { 0 };
        while( scene2_eventbuffer_pop(scene2, &scene_event) )
        {
            if( scene_event.type == SCENE2_EVENT_TEXTURE_LOADED )
            {
                int tex_id = scene_event.u.texture.texture_id;
                struct DashTexture* texture = scene2_texture_get(scene2, tex_id);
                queue_texture_load_from_event(render_command_buffer, tex_id, texture);
                continue;
            }
            if( scene_event.type == SCENE2_EVENT_VERTEX_ARRAY_ADDED ||
                scene_event.type == SCENE2_EVENT_VERTEX_ARRAY_REMOVED ||
                scene_event.type == SCENE2_EVENT_FACE_ARRAY_ADDED ||
                scene_event.type == SCENE2_EVENT_FACE_ARRAY_REMOVED )
            {
                /* GPU: use u.vertex_array.array or u.face_array.array depending on type. */
                continue;
            }
            if( scene_event.u.element.element_id < 0 ||
                scene_event.u.element.element_id >= scene2_elements_total(scene2) )
                continue;
            if( scene_event.type != SCENE2_EVENT_ELEMENT_ACQUIRED &&
                scene_event.type != SCENE2_EVENT_MODEL_CHANGED )
            {
                continue;
            }
            struct Scene2Element* element =
                scene2_element_at(scene2, scene_event.u.element.element_id);
            if( !element || !scene2_element_is_active(element) ||
                !scene2_element_dash_model(element) )
                continue;
            if( scene2_element_parent_entity_id(element) != scene_event.u.element.parent_entity_id )
                continue;
            queue_scene_element_load_from_event(game, render_command_buffer, element);
        }
        scene2_flush_deferred_array_frees(scene2);
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

    game->uitree_stack_top = -1;
    game->uitree_current = (game->ui_root_buffer && game->ui_root_buffer->root_index >= 0)
                               ? game->ui_root_buffer->root_index
                               : -1;
    game->uiscene_command_idx = 0;
    if( game->uiscene_queued_commands )
        LibToriRS_RenderCommandBufferReset(game->uiscene_queued_commands);

    world_pickset_reset(&game->pickset);

    LibToriRS_RenderCommandBufferReset(render_command_buffer);
    queue_static_load_commands(game, render_command_buffer);

    if( game->world )
    {
        if( game->world->cullmap_screen_width == 0 )
        {
            game->world->cullmap_near_clip_z = game->camera->near_plane_z;
            game->world->cullmap_screen_width = game->view_port->width;
            game->world->cullmap_screen_height = game->view_port->height;
            struct ScriptArgs cullmap_script_args = {
                .tag = SCRIPT_LOAD_CULLMAP,
                .u.load_cullmap = {
                    .viewport_w = game->world->cullmap_screen_width,
                    .viewport_h = game->world->cullmap_screen_height,
                    .fov = game->camera_fov,
                    .draw_radius = TORI_RS_PAINTERS_CULLMAP_RADIUS,
                },
            };
            script_queue_push(&game->script_queue, &cullmap_script_args);
        }
    }

    if( game->world && game->world->painter && game->sys_painter_buffer && game->world->cullmap )
    {
        struct Painter* painter = game->world->painter;
        struct PaintersBuffer* buffer = game->sys_painter_buffer;
        int camera_sx = game->camera_world_x / 128;
        int camera_sz = game->camera_world_z / 128;
        int camera_slevel = game->camera_world_y / 240;

        painter_set_camera_angles(painter, game->camera_pitch, game->camera_yaw);

        static int painter_bench_frames;
        static uint64_t painter_bench_sum_paint_ns;
        static uint64_t painter_bench_sum_paint3_ns;
        static uint64_t painter_bench_sum_paint4_ns;
        struct timespec t0, t1;
        uint64_t dt_paint_ns;
        uint64_t dt_paint3_ns;
        uint64_t dt_paint4_ns;

        if( (rand() & 1) == 0 )
        {
            clock_gettime(CLOCK_MONOTONIC, &t0);
            painter_paint_world3d(painter, buffer, camera_sx, camera_sz, camera_slevel);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            dt_paint_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull +
                          (uint64_t)(t1.tv_nsec - t0.tv_nsec);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            painter_paint_bucket(painter, buffer, camera_sx, camera_sz, camera_slevel);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            dt_paint3_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull +
                           (uint64_t)(t1.tv_nsec - t0.tv_nsec);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            painter_paint4(painter, buffer, camera_sx, camera_sz, camera_slevel);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            dt_paint4_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull +
                           (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        }
        else
        {
            clock_gettime(CLOCK_MONOTONIC, &t0);
            painter_paint_bucket(painter, buffer, camera_sx, camera_sz, camera_slevel);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            dt_paint3_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull +
                           (uint64_t)(t1.tv_nsec - t0.tv_nsec);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            painter_paint_world3d(painter, buffer, camera_sx, camera_sz, camera_slevel);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            dt_paint_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull +
                          (uint64_t)(t1.tv_nsec - t0.tv_nsec);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            painter_paint4(painter, buffer, camera_sx, camera_sz, camera_slevel);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            dt_paint4_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull +
                           (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        }

        painter_bench_sum_paint_ns += dt_paint_ns;
        painter_bench_sum_paint3_ns += dt_paint3_ns;
        painter_bench_sum_paint4_ns += dt_paint4_ns;
        painter_bench_frames++;
        if( painter_bench_frames >= 30 )
        {
            fprintf(
                stderr,
                "painter bench (avg over %d frames): paint_w3d=%.3f ms paint_bucket=%.3f ms "
                "paint4=%.3f ms\n",
                painter_bench_frames,
                (double)painter_bench_sum_paint_ns / (double)painter_bench_frames / 1e6,
                (double)painter_bench_sum_paint3_ns / (double)painter_bench_frames / 1e6,
                (double)painter_bench_sum_paint4_ns / (double)painter_bench_frames / 1e6);
            painter_bench_frames = 0;
            painter_bench_sum_paint_ns = 0;
            painter_bench_sum_paint3_ns = 0;
            painter_bench_sum_paint4_ns = 0;
        }
    }
}

static void
entity_player_animate(
    struct World* world,
    int player_entity_id)
{
    struct PlayerEntity* player = world_player(world, player_entity_id);
    struct EntityAnimation* animation = &player->animation;
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, player->scene_element2.element_id);
    if( !scene_element )
        return;

    struct Scene2Frames* primary = scene2_element_primary_frames(scene_element);
    struct Scene2Frames* secondary = scene2_element_secondary_frames(scene_element);
    struct DashModel* dm = scene2_element_dash_model(scene_element);
    struct DashFramemap* fm = scene2_element_dash_framemap(scene_element);

    if( animation->primary_anim.anim_id != -1 && primary && primary->count > 0 )
    {
        int frame = animation->primary_anim.frame;
        scene2_element_set_active_anim_id(scene_element, animation->primary_anim.anim_id);
        scene2_element_set_active_frame(scene_element, (uint8_t)frame);
        if( frame >= 0 && frame < primary->count )
        {
            dashmodel_animate(dm, primary->frames[frame], fm);
        }
    }
    else if( animation->secondary_anim.anim_id != -1 && secondary && secondary->count > 0 )
    {
        int frame = animation->secondary_anim.frame;
        scene2_element_set_active_anim_id(scene_element, animation->secondary_anim.anim_id);
        scene2_element_set_active_frame(scene_element, (uint8_t)frame);
        if( frame >= 0 && frame < secondary->count )
        {
            dashmodel_animate(dm, secondary->frames[frame], fm);
        }
    }
    else
    {
        scene2_element_set_active_anim_id(scene_element, 0);
        scene2_element_set_active_frame(scene_element, 0);
    }
}

static void
entity_npc_animate(
    struct World* world,
    int npc_entity_id)
{
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
    struct EntityAnimation* animation = &npc->animation;
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, npc->scene_element2.element_id);
    if( !scene_element )
        return;

    struct Scene2Frames* primary = scene2_element_primary_frames(scene_element);
    struct Scene2Frames* secondary = scene2_element_secondary_frames(scene_element);
    struct DashModel* dm = scene2_element_dash_model(scene_element);
    struct DashFramemap* fm = scene2_element_dash_framemap(scene_element);

    if( animation->primary_anim.anim_id != -1 && primary && primary->count > 0 )
    {
        int frame = animation->primary_anim.frame;
        scene2_element_set_active_anim_id(scene_element, animation->primary_anim.anim_id);
        scene2_element_set_active_frame(scene_element, (uint8_t)frame);
        if( frame >= 0 && frame < primary->count )
        {
            dashmodel_animate(dm, primary->frames[frame], fm);
        }
    }
    else if( animation->secondary_anim.anim_id != -1 && secondary && secondary->count > 0 )
    {
        int frame = animation->secondary_anim.frame;
        scene2_element_set_active_anim_id(scene_element, animation->secondary_anim.anim_id);
        scene2_element_set_active_frame(scene_element, (uint8_t)frame);
        if( frame >= 0 && frame < secondary->count )
        {
            dashmodel_animate(dm, secondary->frames[frame], fm);
        }
    }
    else
    {
        scene2_element_set_active_anim_id(scene_element, 0);
        scene2_element_set_active_frame(scene_element, 0);
    }
}

static void
entity_map_build_loc_entity_animate(
    struct World* world,
    int map_build_loc_entity_id)
{
    struct MapBuildLocEntity* map_build_loc_entity =
        world_loc_entity(world, map_build_loc_entity_id);

    struct EntityAnimation* animation = NULL;
    struct Scene2Element* scene_element = NULL;

    if( map_build_loc_entity->scene_element.element_id != -1 )
    {
        animation = &map_build_loc_entity->animation;
        scene_element =
            scene2_element_at(world->scene2, map_build_loc_entity->scene_element.element_id);
        scene2_element_expect(scene_element, "entity_map_build_loc_entity_animate primary");

        struct Scene2Frames* pf = scene2_element_primary_frames(scene_element);
        struct DashModel* dm = scene2_element_dash_model(scene_element);
        struct DashFramemap* fm = scene2_element_dash_framemap(scene_element);

        if( animation->primary_anim.anim_id != -1 && pf && pf->count > 0 )
        {
            int frame = animation->primary_anim.frame;
            scene2_element_set_active_anim_id(scene_element, animation->primary_anim.anim_id);
            scene2_element_set_active_frame(scene_element, (uint8_t)frame);
            if( frame >= 0 && frame < pf->count )
            {
                dashmodel_animate(dm, pf->frames[frame], fm);
            }
        }
        else
        {
            scene2_element_set_active_anim_id(scene_element, 0);
            scene2_element_set_active_frame(scene_element, 0);
        }
    }

    if( map_build_loc_entity->scene_element_two.element_id != -1 )
    {
        animation = &map_build_loc_entity->animation_two;
        scene_element =
            scene2_element_at(world->scene2, map_build_loc_entity->scene_element_two.element_id);
        scene2_element_expect(scene_element, "entity_map_build_loc_entity_animate secondary");

        struct Scene2Frames* pf2 = scene2_element_primary_frames(scene_element);
        struct DashModel* dm2 = scene2_element_dash_model(scene_element);
        struct DashFramemap* fm2 = scene2_element_dash_framemap(scene_element);

        if( animation->primary_anim.anim_id != -1 && pf2 && pf2->count > 0 )
        {
            int frame = animation->primary_anim.frame;
            scene2_element_set_active_anim_id(scene_element, animation->primary_anim.anim_id);
            scene2_element_set_active_frame(scene_element, (uint8_t)frame);
            if( frame >= 0 && frame < pf2->count )
            {
                dashmodel_animate(dm2, pf2->frames[frame], fm2);
            }
        }
        else
        {
            scene2_element_set_active_anim_id(scene_element, 0);
            scene2_element_set_active_frame(scene_element, 0);
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
            world_loc_entity(world, entity_id_from_uid(entity_id));
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
        struct PlayerEntity* player = world_player(world, entity_id_from_uid(entity_uid));
        coords->x = player->pathing.route_x[0];
        coords->z = player->pathing.route_z[0];
    }
    break;
    case ENTITY_KIND_NPC:
    {
        struct NPCEntity* npc = world_npc(world, entity_id_from_uid(entity_uid));
        coords->x = npc->pathing.route_x[0];
        coords->z = npc->pathing.route_z[0];
    }
    break;
    case ENTITY_KIND_MAP_BUILD_LOC:
    {
        struct MapBuildLocEntity* map_build_loc_entity =
            world_loc_entity(world, entity_id_from_uid(entity_uid));
        coords->x = map_build_loc_entity->scene_coord.sx;
        coords->z = map_build_loc_entity->scene_coord.sz;
    }
    break;
    }
}

static bool
uielem_redstone_tab_step(
    struct GGame* game,
    struct StaticUIComponent* component)
{
    assert(component->type == UIELEM_BUILTIN_REDSTONE_TAB);
    if( !game->iface || !game->ui_scene )
        return true;
    if( game->iface->sidebar_interface_id != -1 )
        return true;

    int tabno = component->u.redstone_tab.tabno;
    int x = component->position.x;
    int y = component->position.y;
    int w = component->position.width;
    int h = component->position.height;

    if( game->mouse_clicked && !game->interface_consumed_click )
    {
        int cx = game->mouse_clicked_x;
        int cy = game->mouse_clicked_y;
        if( cx >= x && cx < x + w && cy >= y && cy < y + h )
        {
            game->iface->selected_tab = tabno;
            game->interface_consumed_click = 1;
        }
    }

    bool is_active = (game->iface->selected_tab == tabno);
    int sid =
        is_active ? component->u.redstone_tab.scene_id_active : component->u.redstone_tab.scene_id;
    int ai = is_active ? component->u.redstone_tab.atlas_index_active
                       : component->u.redstone_tab.atlas_index;
    if( sid < 0 )
        return true;

    struct UISceneElement* elem = uiscene_element_at(game->ui_scene, sid);
    if( !elem || !elem->dash_sprites )
        return true;
    struct DashSprite* sp = elem->dash_sprites[ai];
    if( !sp )
        return true;

    int draw_x = x;
    int draw_y = y;
    queue_sprite_draw_from_event(game->uiscene_queued_commands, -1, sp, draw_x, draw_y, 0);

    return true;
}

static bool
uielem_builtin_sidebar_step(
    struct GGame* game,
    struct StaticUIComponent* component)
{
    assert(component->type == UIELEM_BUILTIN_SIDEBAR);
    if( !game || !game->iface )
        return true;

    /* Wrong tab or modal sidebar: emit nothing (traversal also skips first_child subtree). */
    if( !frame_sidebar_tab_active(game, component) )
        return true;

    /* Active tab: panel is drawn by RS child nodes under this builtin. */
    return true;
}

static bool
uielem_sprite_step(
    struct GGame* game,
    struct StaticUIComponent* component)
{
    assert(component->type == UIELEM_BUILTIN_SPRITE);

    struct UISceneElement* element =
        uiscene_element_at(game->ui_scene, component->u.sprite.scene_id);
    if( !element )
        return true;

    struct DashSprite* sprite = element->dash_sprites[component->u.sprite.atlas_index];
    if( !sprite )
        return true;

    queue_sprite_draw_from_event(
        game->uiscene_queued_commands,
        component->u.sprite.scene_id,
        sprite,
        component->position.x,
        component->position.y,
        0);

    return true;
}

static bool
uielem_world_step(
    struct GGame* game,
    struct StaticUIComponent* component)
{
    assert(component->type == UIELEM_BUILTIN_WORLD);

    struct PaintersElementCommand* cmd = NULL;
    struct DashPosition position = { 0 };
    struct ToriRSRenderCommand command = { 0 };
    struct Scene2Element* scene_element = NULL;
    struct UISceneElement* element =
        uiscene_element_at(game->ui_scene, component->u.sprite.scene_id);
    if( !element )
        return true;

    if( game->at_painters_command_index >= game->cc )
    {
        return true;
    }

next:
    if( game->at_painters_command_index >= game->sys_painter_buffer->command_count )
    {
        return true;
    }

    cmd = &game->sys_painter_buffer->commands[game->at_painters_command_index];

    game->at_painters_command_index++;

    switch( cmd->_bf_kind )
    {
    case PNTR_CMD_ELEMENT:
    {
        scene_element = scene2_element_at(game->world->scene2, cmd->_entity._bf_entity);
        if( !scene_element )
            goto next;
        struct DashModel* ent_model = scene2_element_dash_model(scene_element);
        struct DashPosition* ent_pos = scene2_element_dash_position(scene_element);
        if( !ent_model || !ent_pos )
            goto next;
        memcpy(&position, ent_pos, sizeof(struct DashPosition));

        position.x = position.x - game->camera_world_x;
        position.y = position.y - game->camera_world_y;
        position.z = position.z - game->camera_world_z;

        int cull = DASHCULL_VISIBLE;

        cull = dash3d_project_model(
            game->sys_dash, ent_model, &position, game->view_port, game->camera);

        if( cull != DASHCULL_VISIBLE )
            break;

        entity_animate(game->world, scene2_element_parent_entity_id(scene_element));

        command.kind = TORIRS_GFX_MODEL_DRAW;
        command._model_draw.model = ent_model;
        command._model_draw.model_key = model_cache_key_u64(game->world->scene2, scene_element);
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
            goto next;

        scene_element =
            scene2_element_at(game->world->scene2, tile_entity->scene_element.element_id);
        if( !scene_element )
            goto next;
        struct DashModel* tile_model = scene2_element_dash_model(scene_element);
        struct DashPosition* tile_pos = scene2_element_dash_position(scene_element);
        if( !tile_model || !tile_pos )
            goto next;

        memcpy(&position, tile_pos, sizeof(struct DashPosition));

        position.x = position.x - game->camera_world_x;
        position.y = position.y - game->camera_world_y;
        position.z = position.z - game->camera_world_z;

        int cull = dash3d_project_model(
            game->sys_dash, tile_model, &position, game->view_port, game->camera);
        if( cull != DASHCULL_VISIBLE )
            break;

        command.kind = TORIRS_GFX_MODEL_DRAW;
        command._model_draw.model = tile_model;
        command._model_draw.model_key = model_cache_key_u64(game->world->scene2, scene_element);
        command._model_draw.model_id = -1;
        memcpy(&command._model_draw.position, &position, sizeof(struct DashPosition));
        LibToriRS_RenderCommandBufferAddCommand(game->uiscene_queued_commands, command);
    }
    break;
    default:
        break;
    }

    return game->at_painters_command_index >= game->sys_painter_buffer->command_count;
}

static bool
uielem_minimap_step(
    struct GGame* game,
    struct StaticUIComponent* component)
{
    assert(component->type == UIELEM_BUILTIN_MINIMAP);

    struct UISceneElement* element =
        uiscene_element_at(game->ui_scene, component->u.minimap.scene_id);
    if( !element )
        return true;

    queue_static_ui_minimap_draws(game, component);
    return true;
}

static bool
uielem_compass_step(
    struct GGame* game,
    struct StaticUIComponent* component)
{
    assert(component->type == UIELEM_BUILTIN_COMPASS);

    struct UISceneElement* element =
        uiscene_element_at(game->ui_scene, component->u.sprite.scene_id);
    if( !element )
        return true;

    struct DashSprite* sprite = element->dash_sprites[component->u.sprite.atlas_index];
    if( !sprite )
        return true;

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

    return true;
}

static bool
uielem_rs_graphic_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    int cur)
{
    assert(component->type == UIELEM_RS_GRAPHIC);
    return rs_gfx_graphic_step(game, component, game->uiscene_queued_commands, cur);
}

static bool
uielem_rs_text_step(
    struct GGame* game,
    struct StaticUIComponent* component)
{
    assert(component->type == UIELEM_RS_TEXT);
    return rs_gfx_text_step(game, component, game->uiscene_queued_commands);
}

static bool
uielem_rs_inv_step(
    struct GGame* game,
    struct StaticUIComponent* component)
{
    assert(component->type == UIELEM_RS_INV);
    return rs_gfx_inv_step(game, component, game->uiscene_queued_commands);
}

static bool
uielem_rs_layer_step(
    struct GGame* game,
    struct StaticUIComponent* component)
{
    assert(component->type == UIELEM_RS_LAYER);
    return true;
}

static bool
uielem_rs_model_step(
    struct GGame* game,
    struct StaticUIComponent* component)
{
    assert(component->type == UIELEM_RS_MODEL);
    return rs_gfx_model_step(
        game, component, game->uiscene_queued_commands, s_frame_project_models);
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
    bool done = false;
    (void)render_command_buffer;
    s_frame_project_models = project_models;

    while( true )
    {
        if( game->uiscene_command_idx < game->uiscene_queued_commands->command_count )
        {
            cmd = LibToriRS_RenderCommandBufferAt(
                game->uiscene_queued_commands, game->uiscene_command_idx);
            memcpy(command, cmd, sizeof(struct ToriRSRenderCommand));
            game->uiscene_command_idx++;

            return true;
        }

        if( !game->ui_root_buffer || game->uitree_current < 0 )
            return false;

        LibToriRS_RenderCommandBufferReset(game->uiscene_queued_commands);
        game->uiscene_command_idx = 0;

        int32_t cur = game->uitree_current;
        component = &game->ui_root_buffer->components[cur];

        switch( component->type )
        {
        case UIELEM_BUILTIN_SPRITE:
            done = uielem_sprite_step(game, component);
            break;
        case UIELEM_BUILTIN_WORLD:
            done = uielem_world_step(game, component);
            break;
        case UIELEM_BUILTIN_MINIMAP:
            done = uielem_minimap_step(game, component);
            break;
        case UIELEM_BUILTIN_COMPASS:
            done = uielem_compass_step(game, component);
            break;
        case UIELEM_BUILTIN_REDSTONE_TAB:
            done = uielem_redstone_tab_step(game, component);
            break;
        case UIELEM_BUILTIN_SIDEBAR:
            done = uielem_builtin_sidebar_step(game, component);
            break;
        case UIELEM_RS_GRAPHIC:
            done = uielem_rs_graphic_step(game, component, cur);
            break;
        case UIELEM_RS_TEXT:
            done = uielem_rs_text_step(game, component);
            break;
        case UIELEM_RS_LAYER:
            done = uielem_rs_layer_step(game, component);
            break;
        case UIELEM_RS_MODEL:
            done = uielem_rs_model_step(game, component);
            break;
        case UIELEM_RS_INV:
            done = uielem_rs_inv_step(game, component);
            break;
        default:
            done = true;
            break;
        }

        if( done )
            frame_uitree_advance_after_step(game, cur);
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