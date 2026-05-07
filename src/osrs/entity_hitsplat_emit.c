#include "entity_hitsplat_emit.h"

#include "graphics/dash.h"
#include "osrs/game.h"
#include "osrs/game_entity.h"
#include "osrs/heightmap.h"
#include "osrs/painters.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/revconfig/uitree_textpool.h"
#include "osrs/scene2.h"
#include "osrs/world.h"
#include "tori_rs_render.h"

#include <stdio.h>
#include <string.h>

#define HITSPLAT_DEFAULT_HALF_H 90

static int
hitsplat_entity_cache_level(
    struct World* world,
    int draw_x,
    int draw_z,
    int base_level)
{
    int sx = draw_x / 128;
    int sz = draw_z / 128;
    if( !world->painter || sx < 0 || sz < 0 || sx >= world->_scene_size ||
        sz >= world->_scene_size )
        return base_level;
    return entity_cache_level_for_column(
        base_level, painter_column_has_link_below(world->painter, sx, sz));
}

static int
hitsplat_model_half_height(struct Scene2Element* se)
{
    if( !se )
        return HITSPLAT_DEFAULT_HALF_H;
    struct DashModel* m = scene2_element_dash_model(se);
    if( !m )
        return HITSPLAT_DEFAULT_HALF_H;
    const struct DashBoundsCylinder* bc = dashmodel_bounds_cylinder_const(m);
    if( !bc )
        return HITSPLAT_DEFAULT_HALF_H;
    return (bc->center_to_top_edge + bc->center_to_bottom_edge) >> 1;
}

/** Client.ts entity.height: full vertical extent from bounds cylinder (getOverlayPosEntity height). */
static int
hitsplat_model_vertical_span(struct Scene2Element* se)
{
    if( !se )
        return 2 * HITSPLAT_DEFAULT_HALF_H;
    struct DashModel* m = scene2_element_dash_model(se);
    if( !m )
        return 2 * HITSPLAT_DEFAULT_HALF_H;
    const struct DashBoundsCylinder* bc = dashmodel_bounds_cylinder_const(m);
    if( !bc )
        return 2 * HITSPLAT_DEFAULT_HALF_H;
    return bc->center_to_top_edge + bc->center_to_bottom_edge;
}

static bool
hitsplat_project(
    struct GGame* game,
    int world_x,
    int world_y,
    int world_z,
    int* out_px,
    int* out_py)
{
    if( !game->sys_dash || !game->view_port || !game->camera )
        return false;
    int sx = world_x - game->camera_world_x;
    int sy = world_y - game->camera_world_y;
    int sz = world_z - game->camera_world_z;
    int px;
    int py;
    if( !dash3d_project_point(game->sys_dash, sx, sy, sz, game->view_port, game->camera, &px, &py) )
        return false;
    *out_px = px + game->viewport_offset_x;
    *out_py = py + game->viewport_offset_y;
    return true;
}

static void
emit_overlay_fill_rect(
    struct ToriRSRenderCommandBuffer* cmds,
    int x,
    int y,
    int w,
    int h,
    int color_rgb)
{
    if( !cmds || w <= 0 || h <= 0 )
        return;
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(cmds);
    c->kind = TORIRS_GFX_DRAW_RECT;
    c->_rect_draw.x = x;
    c->_rect_draw.y = y;
    c->_rect_draw.w = w;
    c->_rect_draw.h = h;
    c->_rect_draw.color_rgb = color_rgb;
    c->_rect_draw.alpha = 255;
    c->_rect_draw.fill = 1;
}

/** Client.ts entityOverlays: combatCycle > loopCycle+100, Pix2D.fillRect green/red 30x5. */
static void
emit_combat_health_bar(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmds,
    struct Scene2Element* se,
    int wx,
    int wz,
    int combat_cycle,
    int health,
    int total_health)
{
    struct World* w = game->world;
    if( !w || !w->heightmap )
        return;
    if( combat_cycle <= game->cycle + 100 )
        return;
    if( total_health <= 0 )
        return;

    int wgreen = (health * 30) / total_health;
    if( wgreen > 30 )
        wgreen = 30;
    if( wgreen < 0 )
        wgreen = 0;
    int wred = 30 - wgreen;

    int span = hitsplat_model_vertical_span(se);
    int slevel = hitsplat_entity_cache_level(w, wx, wz, 0);
    int ground = heightmap_get_interpolated(w->heightmap, wx, wz, slevel);
    int anchor_y = ground - (span + 15);

    int px;
    int py;
    if( !hitsplat_project(game, wx, anchor_y, wz, &px, &py) )
        return;

    int bar_y = py - 3;
    int bar_x0 = px - 15;
    if( wgreen > 0 )
        emit_overlay_fill_rect(cmds, bar_x0, bar_y, wgreen, 5, 0x00ff00);
    if( wred > 0 )
        emit_overlay_fill_rect(cmds, bar_x0 + wgreen, bar_y, wred, 5, 0xff0000);
}

static void
emit_hitsplat_for_slots(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmds,
    int hit_eid,
    struct DashSprite** hit_sprites,
    int hit_sprites_count,
    uint8_t* damage_values,
    uint8_t* damage_types,
    int* damage_cycles,
    int wx,
    int wz,
    int half_h,
    int font_id,
    struct DashPixFont* font,
    struct UITree* uit)
{
    struct World* w = game->world;
    if( !w || !w->heightmap )
        return;

    int slevel = hitsplat_entity_cache_level(w, wx, wz, 0);
    int ground = heightmap_get_interpolated(w->heightmap, wx, wz, slevel);
    int anchor_y = ground - half_h;

    for( int i = 0; i < ENTITY_DAMAGE_SLOTS; i++ )
    {
        if( damage_cycles[i] <= game->cycle )
            continue;

        int px;
        int py;
        if( !hitsplat_project(game, wx, anchor_y, wz, &px, &py) )
            continue;

        if( i == 1 )
            py -= 20;
        else if( i == 2 )
        {
            px -= 15;
            py -= 10;
        }
        else if( i == 3 )
        {
            px += 15;
            py -= 10;
        }

        int t = (int)damage_types[i];
        if( t < 0 || !hit_sprites || t >= hit_sprites_count || !hit_sprites[t] )
            continue;

        struct DashSprite* splat = hit_sprites[t];
        struct ToriRSRenderCommand* sp = LibToriRS_RenderCommandBufferEmplaceCommand(cmds);
        sp->kind = TORIRS_GFX_DRAW_SPRITE;
        sp->_sprite_draw.element_id = hit_eid;
        sp->_sprite_draw.atlas_index = t;
        sp->_sprite_draw.sprite = splat;
        sp->_sprite_draw.dst_bb_x = px - 12;
        sp->_sprite_draw.dst_bb_y = py - 12;
        sp->_sprite_draw.dst_bb_w = 0;
        sp->_sprite_draw.dst_bb_h = 0;
        sp->_sprite_draw.rotated = false;
        sp->_sprite_draw.rotation_r2pi2048 = 0;
        sp->_sprite_draw.src_bb_x = splat->crop_x;
        sp->_sprite_draw.src_bb_y = splat->crop_y;
        sp->_sprite_draw.src_bb_w = splat->crop_width;
        sp->_sprite_draw.src_bb_h = splat->crop_height;
        sp->_sprite_draw.src_anchor_x = 0;
        sp->_sprite_draw.src_anchor_y = 0;
        sp->_sprite_draw.dst_anchor_x = 0;
        sp->_sprite_draw.dst_anchor_y = 0;
        sp->_sprite_draw.mask_element_id = -1;
        sp->_sprite_draw.clip_w = 0;
        sp->_sprite_draw.alpha_mode = 1;

        char buf[16];
        snprintf(buf, sizeof(buf), "%u", (unsigned)damage_values[i]);
        size_t len = strlen(buf);
        char* pooled = NULL;
        if( uit )
            pooled = uitree_textpool_push_dup(&uit->text_pool, buf, len);
        if( !pooled )
            continue;

        /* Match Client.ts PixFont.centreString on (projectX, projectY+4) black then
         * (projectX-1, projectY+3) white; TORIRS pen_y matches tooltip/chat (y - height2d). */
        int h2 = font && font->height2d > 0 ? font->height2d : 12;

        if( font )
        {
            uint8_t* utext = (uint8_t*)(void*)pooled;
            int tw = dashfont_text_width(font, utext);

            int draw_y_black = py + 4 - h2;
            int left_black = px - tw / 2;

            int draw_y_white = py + 3 - h2;
            int left_white = (px - 1) - tw / 2;

            struct ToriRSRenderCommand* fd = LibToriRS_RenderCommandBufferEmplaceCommand(cmds);
            fd->kind = TORIRS_GFX_DRAW_FONT;
            fd->_font_draw.font_id = font_id;
            fd->_font_draw.font = font;
            fd->_font_draw.text = utext;
            fd->_font_draw.x = left_black;
            fd->_font_draw.y = draw_y_black;
            fd->_font_draw.color_rgb = 0x000000;
            fd->_font_draw.shadowed = 0;

            fd = LibToriRS_RenderCommandBufferEmplaceCommand(cmds);
            fd->kind = TORIRS_GFX_DRAW_FONT;
            fd->_font_draw.font_id = font_id;
            fd->_font_draw.font = font;
            fd->_font_draw.text = utext;
            fd->_font_draw.x = left_white;
            fd->_font_draw.y = draw_y_white;
            fd->_font_draw.color_rgb = 0xFFFFFF;
            fd->_font_draw.shadowed = 0;
        }
    }
}

void
entity_hitsplat_emit(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmds)
{
    if( !game || !cmds || !game->world )
        return;
    if( !game->sys_dash || !game->view_port || !game->camera || !game->world->heightmap )
        return;

    struct World* w = game->world;

    struct PlayerEntity* local = world_player(w, ACTIVE_PLAYER_SLOT);
    if( local->alive && local->scene_element2.element_id >= 0 )
    {
        struct Scene2Element* se =
            scene2_element_at(w->scene2, local->scene_element2.element_id);
        emit_combat_health_bar(
            game,
            cmds,
            se,
            local->draw_position.x,
            local->draw_position.z,
            local->combat_cycle,
            local->health,
            local->total_health);
    }

    for( int i = 0; i < w->active_player_count; i++ )
    {
        int pid = w->active_players[i];
        if( pid < 0 || pid == ACTIVE_PLAYER_SLOT )
            continue;
        struct PlayerEntity* pl = world_player(w, pid);
        if( !pl->alive || pl->scene_element2.element_id < 0 )
            continue;
        struct Scene2Element* se =
            scene2_element_at(w->scene2, pl->scene_element2.element_id);
        emit_combat_health_bar(
            game,
            cmds,
            se,
            pl->draw_position.x,
            pl->draw_position.z,
            pl->combat_cycle,
            pl->health,
            pl->total_health);
    }

    for( int i = 0; i < w->active_npc_count; i++ )
    {
        int nid = w->active_npcs[i];
        if( nid < 0 )
            continue;
        struct NPCEntity* npc = world_npc(w, nid);
        if( !npc->alive || npc->scene_element2.element_id < 0 )
            continue;
        struct Scene2Element* se =
            scene2_element_at(w->scene2, npc->scene_element2.element_id);
        emit_combat_health_bar(
            game,
            cmds,
            se,
            npc->draw_position.x,
            npc->draw_position.z,
            npc->combat_cycle,
            npc->health,
            npc->total_health);
    }

    if( !game->ui_scene || game->hitmarks_uiscene_element_id < 0 ||
        game->hitsplat_damage_font_id < 0 )
        return;

    int hit_eid = game->hitmarks_uiscene_element_id;
    struct UISceneElement* hit_el = uiscene_element_at(game->ui_scene, hit_eid);
    if( !hit_el || !hit_el->dash_sprites || hit_el->dash_sprites_count <= 0 )
        return;

    int font_id = game->hitsplat_damage_font_id;
    struct DashPixFont* font = uiscene_font_get(game->ui_scene, font_id);
    if( !font )
        return;

    struct UITree* uit = game->ui_root_buffer;

    if( local->alive && local->scene_element2.element_id >= 0 )
    {
        struct Scene2Element* se =
            scene2_element_at(w->scene2, local->scene_element2.element_id);
        int hh = hitsplat_model_half_height(se);
        emit_hitsplat_for_slots(
            game,
            cmds,
            hit_eid,
            hit_el->dash_sprites,
            hit_el->dash_sprites_count,
            local->damage_values,
            local->damage_types,
            local->damage_cycles,
            local->draw_position.x,
            local->draw_position.z,
            hh,
            font_id,
            font,
            uit);
    }

    for( int i = 0; i < w->active_player_count; i++ )
    {
        int pid = w->active_players[i];
        if( pid < 0 || pid == ACTIVE_PLAYER_SLOT )
            continue;
        struct PlayerEntity* pl = world_player(w, pid);
        if( !pl->alive || pl->scene_element2.element_id < 0 )
            continue;
        struct Scene2Element* se =
            scene2_element_at(w->scene2, pl->scene_element2.element_id);
        int hh = hitsplat_model_half_height(se);
        emit_hitsplat_for_slots(
            game,
            cmds,
            hit_eid,
            hit_el->dash_sprites,
            hit_el->dash_sprites_count,
            pl->damage_values,
            pl->damage_types,
            pl->damage_cycles,
            pl->draw_position.x,
            pl->draw_position.z,
            hh,
            font_id,
            font,
            uit);
    }

    for( int i = 0; i < w->active_npc_count; i++ )
    {
        int nid = w->active_npcs[i];
        if( nid < 0 )
            continue;
        struct NPCEntity* npc = world_npc(w, nid);
        if( !npc->alive || npc->scene_element2.element_id < 0 )
            continue;
        struct Scene2Element* se =
            scene2_element_at(w->scene2, npc->scene_element2.element_id);
        int hh = hitsplat_model_half_height(se);
        emit_hitsplat_for_slots(
            game,
            cmds,
            hit_eid,
            hit_el->dash_sprites,
            hit_el->dash_sprites_count,
            npc->damage_values,
            npc->damage_types,
            npc->damage_cycles,
            npc->draw_position.x,
            npc->draw_position.z,
            hh,
            font_id,
            font,
            uit);
    }
}
