#ifndef TORI_RS_FRAME_U_C
#define TORI_RS_FRAME_U_C

#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/chat.h"
#include "osrs/collisionmap_overlay_draw.h"
#include "osrs/entity_hitsplat_emit.h"
#include "osrs/entity_pathing_draw.h"
#include "osrs/game.h"
#include "osrs/gamenet_send.h"
#include "osrs/interface.h"
#include "osrs/interface_state.h"
#include "osrs/minimap.h"
#include "osrs/minimenu.h"
#include "osrs/minimenu_action.h"
#include "osrs/minimenu_game.h"
#include "osrs/obj_icon.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/rs_component_gfx.h"
#include "osrs/rs_component_state.h"
#include "osrs/scene2.h"
#include "osrs/ui_scrollbar_emit.h"
#include "osrs/world.h"
#include "osrs/world_options.h"
#include "osrs/zone_state.h"
#include "tori_rs.h"
#include "tori_rs_frame_state.h"
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

#include "graphics/convex_hull.u.c"

#include "osrs/heightmap.h"
static bool s_frame_project_models;

static enum ToriRS_UsageHint
torirs_usage_hint_for_scene2_category(enum Scene2ElementCategory category)
{
    return (enum ToriRS_UsageHint)(int)category;
}

static void
emit_marker(
    struct ToriRSRenderCommandBuffer* buf,
    uint8_t kind)
{
    if( !buf )
        return;
    struct ToriRSRenderCommand* m = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    m->kind = kind;
}

static void
emit_begin_3d_with_rect(
    struct ToriRSRenderCommandBuffer* buf,
    int x,
    int y,
    int w,
    int h)
{
    if( !buf )
        return;
    struct ToriRSRenderCommand* m = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    m->kind = TORIRS_GFX_STATE_BEGIN_3D;
    m->_begin_3d.x = x;
    m->_begin_3d.y = y;
    m->_begin_3d.w = w;
    m->_begin_3d.h = h;
}

void
frame_emit_pass(
    struct UIFrameState* fiber,
    enum FramePassKind target)
{
    if( !fiber || !fiber->cmds || !fiber->pass || *fiber->pass == target )
        return;
    switch( *fiber->pass )
    {
    case FRAME_PASS_2D:
        emit_marker(fiber->cmds, TORIRS_GFX_STATE_END_2D);
        break;
    case FRAME_PASS_3D:
        emit_marker(fiber->cmds, TORIRS_GFX_STATE_END_3D);
        break;
    default:
        break;
    }
    switch( target )
    {
    case FRAME_PASS_2D:
        emit_marker(fiber->cmds, TORIRS_GFX_STATE_BEGIN_2D);
        break;
    case FRAME_PASS_3D:
    {
        struct GGame* g = fiber->game;
        int bx = 0;
        int by = 0;
        int bw = 0;
        int bh = 0;
        if( g && g->view_port )
        {
            bx = g->viewport_offset_x;
            by = g->viewport_offset_y;
            bw = g->view_port->width;
            bh = g->view_port->height;
        }
        emit_begin_3d_with_rect(fiber->cmds, bx, by, bw, bh);
        break;
    }
    default:
        break;
    }
    *fiber->pass = target;
}

void
frame_emit_pass_3d_with_rect(
    struct UIFrameState* fiber,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h)
{
    if( !fiber || !fiber->cmds || !fiber->pass )
        return;
    if( *fiber->pass == FRAME_PASS_3D )
    {
        /* Already in a 3D pass: close the previous sub-rect so the next BEGIN_3D pushes the right
         * dst_bb (soft3d stack pop/push; consecutive RS_MODEL children each need their own rect). */
        emit_marker(fiber->cmds, TORIRS_GFX_STATE_END_3D);
    }
    else
    {
        switch( *fiber->pass )
        {
        case FRAME_PASS_2D:
            emit_marker(fiber->cmds, TORIRS_GFX_STATE_END_2D);
            break;
        default:
            break;
        }
        *fiber->pass = FRAME_PASS_3D;
    }
    emit_begin_3d_with_rect(fiber->cmds, dst_x, dst_y, dst_w, dst_h);
}

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

/** Level bitmask from first `UIELEM_BUILTIN_WORLD` in the UI tree; default all levels. */
static uint8_t
frame_ui_world_level_mask(struct GGame* game)
{
    if( !game || !game->ui_root_buffer )
        return 0xFu;
    for( uint32_t i = 0; i < game->ui_root_buffer->component_count; i++ )
    {
        if( game->ui_root_buffer->components[i].type == UIELEM_BUILTIN_WORLD )
            return game->ui_root_buffer->components[i].u.world.level_mask;
    }
    return 0xFu;
}

static inline bool
uielem_sprite_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static inline bool
uielem_world_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static inline bool
uielem_minimap_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static inline bool
uielem_compass_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static inline bool
uielem_redstone_tab_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static inline bool
uielem_builtin_sidebar_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static inline bool
uielem_rs_graphic_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static inline bool
uielem_rs_text_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static inline bool
uielem_rs_inv_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static inline bool
uielem_rs_layer_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static inline bool
uielem_rs_model_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static void
emit_ui_sprite_raw(
    struct ToriRSRenderCommandBuffer* buf,
    int element_id,
    int atlas_index,
    struct DashSprite* sprite,
    int dst_x,
    int dst_y)
{
    if( !buf || !sprite )
        return;
    int src_bb_x = 0;
    int src_bb_y = 0;
    int src_bb_w = sprite->width;
    int src_bb_h = sprite->height;
    if( sprite->crop_width > 0 && sprite->crop_height > 0 )
    {
        src_bb_x = sprite->crop_x;
        src_bb_y = sprite->crop_y;
        src_bb_w = sprite->crop_width;
        src_bb_h = sprite->crop_height;
    }
    int src_anchor_x = sprite->crop_x;
    int src_anchor_y = sprite->crop_y;

    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind = TORIRS_GFX_DRAW_SPRITE;
    c->_sprite_draw.element_id = element_id;
    c->_sprite_draw.atlas_index = atlas_index;
    c->_sprite_draw.sprite = sprite;
    c->_sprite_draw.dst_bb_x = dst_x;
    c->_sprite_draw.dst_bb_y = dst_y;
    c->_sprite_draw.dst_bb_w = src_bb_w;
    c->_sprite_draw.dst_bb_h = src_bb_h;
    c->_sprite_draw.src_anchor_x = src_anchor_x;
    c->_sprite_draw.src_anchor_y = src_anchor_y;
    c->_sprite_draw.rotation_r2pi2048 = 0;
    c->_sprite_draw.src_bb_x = src_bb_x;
    c->_sprite_draw.src_bb_y = src_bb_y;
    c->_sprite_draw.src_bb_w = src_bb_w;
    c->_sprite_draw.src_bb_h = src_bb_h;
    c->_sprite_draw.rotated = false;
    c->_sprite_draw.dst_anchor_x = 0;
    c->_sprite_draw.dst_anchor_y = 0;
}

static void
emit_ui_fill_rect(
    struct ToriRSRenderCommandBuffer* buf,
    int x,
    int y,
    int w,
    int h,
    int rgb,
    int alpha,
    uint8_t fill)
{
    if( !buf || w <= 0 || h <= 0 )
        return;
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind = TORIRS_GFX_DRAW_RECT;
    c->_rect_draw.x = x;
    c->_rect_draw.y = y;
    c->_rect_draw.w = w;
    c->_rect_draw.h = h;
    c->_rect_draw.color_rgb = rgb;
    c->_rect_draw.alpha = alpha;
    c->_rect_draw.fill = fill;
}

static int
rs_iface_scroll_clamped_for_layer(
    struct GGame* game,
    int component_id,
    int scroll_height,
    int layer_h)
{
    if( !game->iface || component_id < 0 || component_id >= MAX_IFACE_SCROLL_IDS )
        return 0;
    int sp = game->iface->component_scroll_position[component_id];
    int max_scroll = scroll_height - layer_h;
    if( max_scroll < 0 )
        max_scroll = 0;
    if( sp < 0 )
        sp = 0;
    if( sp > max_scroll )
        sp = max_scroll;
    return sp;
}

static void
rs_ui_layer_emit_scrollbar_at(
    struct UIFrameState* fiber,
    int layer_x,
    int layer_y,
    int layer_w,
    int layer_h,
    int layer_component_id,
    int scroll_height,
    int scroll_pos)
{
    struct GGame* game = fiber->game;
    (void)layer_component_id;
    if( !game || !fiber->cmds )
        return;
    frame_emit_pass(fiber, FRAME_PASS_2D);
    ui_emit_scrollbar_commands(
        fiber->cmds, game, layer_x + layer_w, layer_y, layer_h, scroll_height, scroll_pos);
}

static void
rs_ui_layer_push(
    struct UIFrameState* fiber,
    struct StaticUIComponent* layer)
{
    struct GGame* game = fiber->game;
    struct UITree* ui = game ? game->ui_root_buffer : NULL;
    if( !game || !fiber->cmds || !ui || ui->ui_layer_stack_top + 1 >= UIFRAME_LAYER_STACK_MAX )
        return;
    int lh = layer->position.height;
    int sh = layer->u.rs_layer.scroll_height;
    int sp = rs_iface_scroll_clamped_for_layer(game, layer->component_id, sh, lh);
    int prev =
        ui->ui_layer_stack_top >= 0 ? ui->ui_layer_stack[ui->ui_layer_stack_top].scroll_y_total : 0;
    struct UILayerFrameEntry* e = &ui->ui_layer_stack[++ui->ui_layer_stack_top];
    e->scroll_y_total = prev + sp;
    e->clip_x = layer->position.x;
    e->clip_y = layer->position.y;
    /* Content clip matches packed layer width (same as interface LAYER bounds). If content
     * still paints into the 16px scrollbar column after the bar renders, some packs store width
     * including that gutter — then consider clip_w -= 16 only when scroll_height > height. */
    e->clip_w = layer->position.width;
    e->clip_h = lh;
    if( sh > lh && lh >= 32 )
    {
        e->scrollbar_layer_component_id = layer->component_id;
        e->scrollbar_scroll_height = sh;
    }
    else
    {
        e->scrollbar_layer_component_id = -1;
        e->scrollbar_scroll_height = 0;
    }

    struct ToriRSRenderCommand* pc = LibToriRS_RenderCommandBufferEmplaceCommand(fiber->cmds);
    pc->kind = TORIRS_GFX_STATE_PUSH_CLIP;
    pc->_push_clip.x = e->clip_x;
    pc->_push_clip.y = e->clip_y;
    pc->_push_clip.w = e->clip_w;
    pc->_push_clip.h = e->clip_h;
}

static void
rs_ui_layer_pop(struct GGame* game)
{
    struct UITree* ui = game ? game->ui_root_buffer : NULL;
    if( !ui || ui->ui_layer_stack_top < 0 )
        return;
    struct UILayerFrameEntry* e = &ui->ui_layer_stack[ui->ui_layer_stack_top];
    /* Pop clip before scrollbar: bar is at x+width (outside content clip). Client.ts drawScrollbar.
     */
    if( game->uiscene_queued_commands )
    {
        struct ToriRSRenderCommand* c =
            LibToriRS_RenderCommandBufferEmplaceCommand(game->uiscene_queued_commands);
        c->kind = TORIRS_GFX_STATE_POP_CLIP;
    }
    if( e->scrollbar_layer_component_id >= 0 && e->scrollbar_scroll_height > e->clip_h &&
        game->uiscene_queued_commands )
    {
        struct UIFrameState fiber = {
            .game    = game,
            .cmds    = game->uiscene_queued_commands,
            .mouse_x = game->mouse.cursor_x,
            .mouse_y = game->mouse.cursor_y,
            .pass    = &game->frame_pass,
        };
        int sp = rs_iface_scroll_clamped_for_layer(
            game, e->scrollbar_layer_component_id, e->scrollbar_scroll_height, e->clip_h);
        rs_ui_layer_emit_scrollbar_at(
            &fiber,
            e->clip_x,
            e->clip_y,
            e->clip_w,
            e->clip_h,
            e->scrollbar_layer_component_id,
            e->scrollbar_scroll_height,
            sp);
    }
    ui->ui_layer_stack_top--;
}

static bool
frame_uitree_should_descend(
    struct GGame* game,
    struct StaticUIComponent* c)
{
    if( c->type == UIELEM_RS_LAYER )
    {
        if( c->is_hidden )
            return false;
        if( c->u.rs_layer.hide && !interface_component_is_overlay_hovered(game, c->component_id) )
            return false;
    }
    else if( c->is_hidden )
        return false;
    if( c->first_child < 0 )
        return false;
    if( c->type == UIELEM_BUILTIN_SIDEBAR )
        return frame_sidebar_tab_active(game, c);
    if( c->type == UIELEM_BUILTIN_CHAT_DIALOG )
        return game->iface && game->iface->chat_interface_id >= 0;
    if( c->type == UIELEM_BUILTIN_SIDEBAR_OVERLAY )
        return game->iface && game->iface->sidebar_interface_id >= 0;
    return true;
}

static void
frame_uitree_advance_after_step(
    struct GGame* game,
    int32_t stepped_index)
{
    struct UITree* t = game->ui_root_buffer;
    struct StaticUIComponent* c = &t->components[stepped_index];

    /* Only descend into children when the node was dirty; skip entire subtree otherwise. */
    if( c->is_dirty && frame_uitree_should_descend(game, c) )
    {
        if( game->uitree_stack_top + 1 >= UITREE_TRAVERSAL_STACK_MAX )
        {
            game->uitree_current = -1;
            return;
        }
        game->uitree_stack[++game->uitree_stack_top] = (struct UITraversalFrame){
            .parent_index = stepped_index,
        };
        game->uitree_current = c->first_child;
        return;
    }
    if( c->next_sibling >= 0 )
    {
        if( c->type == UIELEM_RS_LAYER && c->is_dirty )
            rs_ui_layer_pop(game);
        game->uitree_current = c->next_sibling;
        return;
    }
    while( game->uitree_stack_top >= 0 )
    {
        struct UITraversalFrame frame = game->uitree_stack[game->uitree_stack_top--];
        struct StaticUIComponent* p = &t->components[frame.parent_index];
        if( p->type == UIELEM_RS_LAYER && p->is_dirty )
            rs_ui_layer_pop(game);
        if( p->next_sibling >= 0 )
        {
            game->uitree_current = p->next_sibling;
            return;
        }
    }
    if( c->type == UIELEM_RS_LAYER && c->is_dirty )
        rs_ui_layer_pop(game);
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
    int visual_id = scene2_element_visual_id(element);
    return ((uint64_t)(uint32_t)visual_id << 24) |
           ((uint64_t)scene2_element_active_anim_id(element) << 8) |
           (uint64_t)scene2_element_active_frame(element);
}

static void
queue_texture_load_from_event(
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    int texture_id,
    struct DashTexture* texture_nullable)
{
    struct ToriRSRenderCommand* c =
        LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
    c->kind = TORIRS_GFX_RES_TEX_LOAD;
    c->_texture_load.texture_id = texture_id;
    c->_texture_load.texture_nullable = texture_nullable;
}

static void
queue_sprite_load_from_event(
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    int element_id,
    int atlas_index,
    struct DashSprite* sprite)
{
    struct ToriRSRenderCommand* c =
        LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
    c->kind = TORIRS_GFX_RES_SPRITE_LOAD;
    c->_sprite_load.element_id = element_id;
    c->_sprite_load.atlas_index = atlas_index;
    c->_sprite_load.sprite = sprite;
}

static void
queue_sprite_unload_from_event(
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    int element_id,
    int atlas_index,
    struct DashSprite* sprite)
{
    struct ToriRSRenderCommand* c =
        LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
    c->kind = TORIRS_GFX_RES_SPRITE_UNLOAD;
    c->_sprite_load.element_id = element_id;
    c->_sprite_load.atlas_index = atlas_index;
    c->_sprite_load.sprite = sprite;
}

static void
queue_font_load_from_event(
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    int font_id,
    struct DashPixFont* font)
{
    struct ToriRSRenderCommand* c =
        LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
    c->kind = TORIRS_GFX_RES_FONT_LOAD;
    c->_font_load.font_id = font_id;
    c->_font_load.font = font;
}

static void
queue_sprite_draw_from_event(
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    int element_id,
    int atlas_index,
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

    struct ToriRSRenderCommand* command =
        LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
    command->kind = TORIRS_GFX_DRAW_SPRITE;
    command->_sprite_draw.element_id = element_id;
    command->_sprite_draw.atlas_index = atlas_index;
    command->_sprite_draw.sprite = sprite;
    command->_sprite_draw.dst_bb_x = x;
    command->_sprite_draw.dst_bb_y = y;
    command->_sprite_draw.src_anchor_x = src_anchor_x;
    command->_sprite_draw.src_anchor_y = src_anchor_y;
    command->_sprite_draw.rotation_r2pi2048 = rotation_r2pi2048;
    command->_sprite_draw.src_bb_x = src_bb_x;
    command->_sprite_draw.src_bb_y = src_bb_y;
    command->_sprite_draw.src_bb_w = src_bb_w;
    command->_sprite_draw.src_bb_h = src_bb_h;
}

static struct DashSprite*
game_uiscene_sprite_atlas0(
    struct GGame* game,
    int element_id)
{
    if( !game || !game->ui_scene || element_id < 0 || element_id >= game->ui_scene->elements_count )
        return NULL;
    struct UISceneElement* el = &game->ui_scene->elements[element_id];
    if( !el->active || !el->dash_sprites || el->dash_sprites_count <= 0 )
        return NULL;
    return el->dash_sprites[0];
}

/** Drawable pixel size for UI sprites (Pix32 cache uses crop vs full buffer). */
static void
dashsprite_draw_dims(
    struct DashSprite const* sp,
    int* out_w,
    int* out_h)
{
    if( sp->crop_width > 0 && sp->crop_height > 0 )
    {
        *out_w = sp->crop_width;
        *out_h = sp->crop_height;
    }
    else
    {
        *out_w = sp->width;
        *out_h = sp->height;
    }
}

/** Dot placement for minimap TORIRS_GFX_DRAW_SPRITE dots.
 * `pivot_x`, `pivot_y`: screen pixel of minimap rotation center (= component top-left + UITree
 * anchor); must match TORIRS_GFX_DRAW_SPRITE rotated minimap dst_anchor.
 * World deltas use the same fine units as `camera_world_x` / loc `world_x` (128 per tile).
 * Projection matches Client.ts minimapDrawDot after scaling: TS uses (world/32) deltas and >>16;
 * here (world*65536)>>21 == ((world/32)*65536)>>16. */
struct MinimapDotLayout
{
    int ok;
    int dst_x;
    int dst_y;
    int sw;
    int sh;
    int src_ix;
    int src_iy;
    int src_iw;
    int src_ih;
};

static struct MinimapDotLayout
minimap_compute_dot_layout(
    struct DashSprite const* sprite,
    int comp_x,
    int comp_y,
    int comp_w,
    int comp_h,
    int pivot_x,
    int pivot_y,
    int camera_yaw,
    int ref_wx,
    int ref_wz,
    int entity_wx,
    int entity_wz)
{
    struct MinimapDotLayout L = { 0 };
    if( !sprite )
        return L;

    int dx = entity_wx - ref_wx;
    int dz = entity_wz - ref_wz;

    /* Client.ts distance check: dx_ts^2+dz_ts^2 > 6400 with ts = floor(world/32). */
    int64_t const max_d2 = (int64_t)6400 * 32 * 32;
    if( (int64_t)dx * dx + (int64_t)dz * dz > max_d2 )
        return L;

    int yaw = camera_yaw & 0x7ff;
    int sin_y = dash_sin(yaw);
    int cos_y = dash_cos(yaw);

    int64_t const rot_x = ((int64_t)dx * (int64_t)cos_y + (int64_t)dz * (int64_t)sin_y) >> 21;
    int64_t const rot_y = ((int64_t)dz * (int64_t)cos_y - (int64_t)dx * (int64_t)sin_y) >> 21;

    /* TS: plotSprite(..., 83 - rot_y - ...): screen Y decreases as rot_y grows. */
    int dot_x = pivot_x + (int)rot_x;
    int dot_y = pivot_y - (int)rot_y;

    int sw = 0;
    int sh = 0;
    dashsprite_draw_dims(sprite, &sw, &sh);
    if( sw <= 0 || sh <= 0 )
        return L;
    int half_w = sw / 2;
    int half_h = sh / 2;
    int min_cx = comp_x + half_w;
    int max_cx = comp_x + comp_w - sw + half_w;
    int min_cy = comp_y + half_h;
    int max_cy = comp_y + comp_h - sh + half_h;
    if( min_cx > max_cx || min_cy > max_cy )
        return L;
    if( dot_x < min_cx )
        dot_x = min_cx;
    if( dot_x > max_cx )
        dot_x = max_cx;
    if( dot_y < min_cy )
        dot_y = min_cy;
    if( dot_y > max_cy )
        dot_y = max_cy;

    L.sw = sw;
    L.sh = sh;
    L.dst_x = dot_x - half_w;
    L.dst_y = dot_y - half_h;
    if( sprite->crop_width > 0 && sprite->crop_height > 0 )
    {
        L.src_ix = sprite->crop_x;
        L.src_iy = sprite->crop_y;
        L.src_iw = sprite->crop_width;
        L.src_ih = sprite->crop_height;
    }
    else
    {
        L.src_ix = 0;
        L.src_iy = 0;
        L.src_iw = sprite->width;
        L.src_ih = sprite->height;
    }
    L.ok = 1;
    return L;
}

/* Emit a single 2-pixel-wide dot on the minimap for the entity at world position
 * (entity_world_x, entity_world_z).  Mirrors Client.ts minimapDrawDot 11637-11663.
 *
 * sprite_element_id / sprite_atlas_index: from `GGame` fields filled at UI load; if element id is
 *   -1, draw uses legacy atlas slot 200 (no UIScene binding).
 * comp_x, comp_y: top-left corner of the minimap element in UI viewport space.
 * camera_yaw:  current camera yaw (0–2047 units, full circle).
 * ref_wx, ref_wz: world position (sub-tile units) that the minimap centers on — must match
 *   static minimap texture anchor (here: camera), not necessarily the local player.
 * pivot_x, pivot_y: rotation center on screen (component position + UITree anchor). */
static void
minimap_emit_dot(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* buf,
    int sprite_element_id,
    int sprite_atlas_index,
    struct DashSprite* sprite,
    int comp_x,
    int comp_y,
    int comp_w,
    int comp_h,
    int pivot_x,
    int pivot_y,
    int camera_yaw,
    int ref_wx,
    int ref_wz,
    int entity_wx,
    int entity_wz)
{
    (void)game;
    if( !sprite || !buf )
        return;

    struct MinimapDotLayout L = minimap_compute_dot_layout(
        sprite,
        comp_x,
        comp_y,
        comp_w,
        comp_h,
        pivot_x,
        pivot_y,
        camera_yaw,
        ref_wx,
        ref_wz,
        entity_wx,
        entity_wz);
    if( !L.ok )
        return;

    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind = TORIRS_GFX_DRAW_SPRITE;
    if( sprite_element_id >= 0 )
    {
        c->_sprite_draw.element_id = sprite_element_id;
        c->_sprite_draw.atlas_index = sprite_atlas_index;
    }
    else
    {
        c->_sprite_draw.element_id = -1;
        c->_sprite_draw.atlas_index = 200; /* legacy path when sprite not in UIScene */
    }
    c->_sprite_draw.sprite = sprite;
    c->_sprite_draw.dst_bb_x = L.dst_x;
    c->_sprite_draw.dst_bb_y = L.dst_y;
    c->_sprite_draw.dst_bb_w = L.sw;
    c->_sprite_draw.dst_bb_h = L.sh;
    c->_sprite_draw.rotated = false;
    c->_sprite_draw.rotation_r2pi2048 = 0;
    if( sprite->crop_width > 0 && sprite->crop_height > 0 )
    {
        c->_sprite_draw.src_bb_x = L.src_ix;
        c->_sprite_draw.src_bb_y = L.src_iy;
        c->_sprite_draw.src_bb_w = L.src_iw;
        c->_sprite_draw.src_bb_h = L.src_ih;
    }
    else
    {
        c->_sprite_draw.src_bb_x = 0;
        c->_sprite_draw.src_bb_y = 0;
        c->_sprite_draw.src_bb_w = 0;
        c->_sprite_draw.src_bb_h = 0;
    }
    c->_sprite_draw.src_anchor_x = 0;
    c->_sprite_draw.src_anchor_y = 0;
    c->_sprite_draw.dst_anchor_x = 0;
    c->_sprite_draw.dst_anchor_y = 0;
    c->_sprite_draw.mask_element_id = -1;
    c->_sprite_draw.clip_w = 0;
    c->_sprite_draw.alpha_mode = 1;
}

/** Emit TORIRS_GFX_DRAW_SPRITE for each MINIMAP_RENDER_COMMAND_LOC in `dyn`. */
static void
minimap_emit_dynamic_loc_gfx(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* buf,
    struct Minimap* mm,
    struct MinimapRenderCommandBuffer* dyn,
    int mm_comp_x,
    int mm_comp_y,
    int mm_comp_w,
    int mm_comp_h,
    int mm_pivot_x,
    int mm_pivot_y,
    int mm_cam_yaw,
    int mm_ref_wx,
    int mm_ref_wz)
{
    struct UITree* uit = game->ui_root_buffer;
    if( !buf || !mm || !dyn )
        return;

    for( int i = 0; i < dyn->count; i++ )
    {
        struct MinimapRenderCommand* cmd = &dyn->commands[i];
        if( cmd->kind != MINIMAP_RENDER_COMMAND_LOC )
            continue;
        int idx = cmd->u.loc.loc_idx;
        if( idx < 0 || idx >= mm->locs_count )
            continue;
        struct MinimapLoc* loc = &mm->locs[idx];
        int dot_eid = -1;
        switch( loc->type )
        {
        case MINIMAP_LOC_TYPE_OBJECT:
            dot_eid = uit ? uit->ui_minimap_mapdots0_element_id : -1;
            break;
        case MINIMAP_LOC_TYPE_NPC:
            dot_eid = uit ? uit->ui_minimap_mapdots1_element_id : -1;
            break;
        case MINIMAP_LOC_TYPE_PLAYER:
            dot_eid = uit ? uit->ui_minimap_mapdots3_element_id : -1;
            break;
        default:
            break;
        }
        struct DashSprite* spr = game_uiscene_sprite_atlas0(game, dot_eid);
        if( !spr )
            continue;
        minimap_emit_dot(
            game,
            buf,
            dot_eid,
            0,
            spr,
            mm_comp_x,
            mm_comp_y,
            mm_comp_w,
            mm_comp_h,
            mm_pivot_x,
            mm_pivot_y,
            mm_cam_yaw,
            mm_ref_wx,
            mm_ref_wz,
            loc->world_x,
            loc->world_z);
    }
}

static void
queue_static_ui_minimap_draws(
    struct UIFrameState* fiber,
    struct StaticUIComponent* component)
{
    struct GGame* game = fiber->game;
    struct Minimap* mm = NULL;
    if( game->world && game->world->minimap )
        mm = game->world->minimap;
    if( !mm )
    {
        fprintf(stderr, "[minimap] draw skipped: no world minimap\n");
        return;
    }

    struct UISceneElement* element =
        uiscene_element_at(game->ui_scene, component->u.minimap.scene_id);
    if( !element || !element->dash_sprites || !element->dash_sprites[0] )
    {
        fprintf(
            stderr,
            "[minimap] draw skipped: scene_id=%d element=%p dash_sprites[0]=%p\n",
            component->u.minimap.scene_id,
            (void*)element,
            (void*)(element && element->dash_sprites ? element->dash_sprites[0] : NULL));
        return;
    }

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

    int anchor_x = 0;
    int anchor_y = 0;

    camera_tile_x = game->camera_world_x / 128;
    camera_tile_z = game->camera_world_z / 128;

    anchor_x = camera_tile_x * (static_sprite->width / 104);
    anchor_y = static_sprite->height - camera_tile_z * (static_sprite->height / 104);

    if( game->uiscene_queued_commands && component->position.width > 0 &&
        component->position.height > 0 )
    {
        frame_emit_pass(fiber, FRAME_PASS_2D);
        struct ToriRSRenderCommand* clr =
            LibToriRS_RenderCommandBufferEmplaceCommand(game->uiscene_queued_commands);
        clr->kind = TORIRS_GFX_STATE_CLEAR_RECT;
        clr->_clear_rect.x = component->position.x;
        clr->_clear_rect.y = component->position.y;
        clr->_clear_rect.w = component->position.width;
        clr->_clear_rect.h = component->position.height;
    }

    frame_emit_pass(fiber, FRAME_PASS_2D);

    struct ToriRSRenderCommand* static_mm_draw =
        LibToriRS_RenderCommandBufferEmplaceCommand(game->uiscene_queued_commands);
    static_mm_draw->kind = TORIRS_GFX_DRAW_SPRITE;
    static_mm_draw->_sprite_draw.element_id = component->u.minimap.scene_id;
    static_mm_draw->_sprite_draw.atlas_index = 0;
    static_mm_draw->_sprite_draw.sprite = static_sprite;
    static_mm_draw->_sprite_draw.dst_anchor_x = component->position.anchor_x;
    static_mm_draw->_sprite_draw.dst_anchor_y = component->position.anchor_y;
    static_mm_draw->_sprite_draw.dst_bb_x = component->position.x;
    static_mm_draw->_sprite_draw.dst_bb_y = component->position.y;
    static_mm_draw->_sprite_draw.dst_bb_w = component->position.width;
    static_mm_draw->_sprite_draw.dst_bb_h = component->position.height;
    static_mm_draw->_sprite_draw.rotated = true;
    static_mm_draw->_sprite_draw.rotation_r2pi2048 = ((game->camera_yaw) & 0x7ff);
    static_mm_draw->_sprite_draw.src_bb_x = 0;
    static_mm_draw->_sprite_draw.src_bb_y = 0;
    static_mm_draw->_sprite_draw.src_bb_w = static_sprite->crop_width;
    static_mm_draw->_sprite_draw.src_bb_h = static_sprite->crop_height;
    static_mm_draw->_sprite_draw.src_anchor_x = anchor_x;
    static_mm_draw->_sprite_draw.src_anchor_y = anchor_y;

    int mm_comp_x = component->position.x;
    int mm_comp_y = component->position.y;
    int mm_comp_w = component->position.width;
    int mm_comp_h = component->position.height;
    int const mm_pivot_x = mm_comp_x + component->position.anchor_x;
    int const mm_pivot_y = mm_comp_y + component->position.anchor_y;
    int mm_cam_yaw = game->camera_yaw;

    int const mm_ref_wx = game->camera_world_x;
    int const mm_ref_wz = game->camera_world_z;

    int pl_wx = mm_ref_wx;
    int pl_wz = mm_ref_wz;
    if( game->world )
    {
        struct PlayerEntity* lp = world_player(game->world, ACTIVE_PLAYER_SLOT);
        if( lp && lp->alive )
        {
            pl_wx = lp->draw_position.x;
            pl_wz = lp->draw_position.z;
        }
    }

    /* Ground items: minimap dots from world_cycle (world->obj_stacks / ObjStackEntity). */

    struct MinimapRenderCommandBuffer* dyn = game->minimap_dynamic_commands;
    if( dyn && game->uiscene_queued_commands )
    {
        minimap_commands_reset(dyn);
        minimap_render_dynamic(mm, sw_x, sw_z, ne_x, ne_z, dyn);
        minimap_emit_dynamic_loc_gfx(
            game,
            game->uiscene_queued_commands,
            mm,
            dyn,
            mm_comp_x,
            mm_comp_y,
            mm_comp_w,
            mm_comp_h,
            mm_pivot_x,
            mm_pivot_y,
            mm_cam_yaw,
            mm_ref_wx,
            mm_ref_wz);
    }

    struct UITree* uit = game->ui_root_buffer;

    /* ── Hint arrow, destination flag, local player (not in dynamic loc list). ─ */
    if( !game->world )
        goto mm_done;

    /* Hint arrow — flashes if hint_arrow_type is set and loopCycle % 20 < 10. */
    if( game->hint_arrow_type != 0 && (game->cycle % 20) < 10 )
    {
        int const arrow_eid = uit ? uit->ui_minimap_mapmarker2_element_id : -1;
        struct DashSprite* arrow = game_uiscene_sprite_atlas0(game, arrow_eid);
        if( arrow && game->hint_arrow_tile_x >= 0 && game->hint_arrow_tile_z >= 0 )
        {
            minimap_emit_dot(
                game,
                game->uiscene_queued_commands,
                arrow_eid,
                0,
                arrow,
                mm_comp_x,
                mm_comp_y,
                mm_comp_w,
                mm_comp_h,
                mm_pivot_x,
                mm_pivot_y,
                mm_cam_yaw,
                mm_ref_wx,
                mm_ref_wz,
                game->hint_arrow_tile_x * 128 + 64,
                game->hint_arrow_tile_z * 128 + 64);
        }
    }

    /* Destination flag (set by minimap click or world click). */
    if( game->minimap_flag_has )
    {
        int const flag_eid = uit ? uit->ui_minimap_mapmarker_element_id : -1;
        struct DashSprite* flag = game_uiscene_sprite_atlas0(game, flag_eid);
        if( flag )
        {
            minimap_emit_dot(
                game,
                game->uiscene_queued_commands,
                flag_eid,
                0,
                flag,
                mm_comp_x,
                mm_comp_y,
                mm_comp_w,
                mm_comp_h,
                mm_pivot_x,
                mm_pivot_y,
                mm_cam_yaw,
                mm_ref_wx,
                mm_ref_wz,
                game->minimap_flag_x * 128 + 64,
                game->minimap_flag_z * 128 + 64);
        }
    }

    /* Local player: white 3x3 rect at active player (same projection as minimap_emit_dot). */
    {
        int dx = pl_wx - mm_ref_wx;
        int dz = pl_wz - mm_ref_wz;
        int yaw = mm_cam_yaw & 0x7ff;
        int sin_y = dash_sin(yaw);
        int cos_y = dash_cos(yaw);
        int64_t const rot_x = ((int64_t)dx * (int64_t)cos_y + (int64_t)dz * (int64_t)sin_y) >> 21;
        int64_t const rot_y = ((int64_t)dz * (int64_t)cos_y - (int64_t)dx * (int64_t)sin_y) >> 21;
        int dot_x = mm_pivot_x + (int)rot_x;
        int dot_y = mm_pivot_y - (int)rot_y;
        if( dot_x < mm_comp_x + 1 )
            dot_x = mm_comp_x + 1;
        if( dot_y < mm_comp_y + 1 )
            dot_y = mm_comp_y + 1;
        if( dot_x > mm_comp_x + mm_comp_w - 2 )
            dot_x = mm_comp_x + mm_comp_w - 2;
        if( dot_y > mm_comp_y + mm_comp_h - 2 )
            dot_y = mm_comp_y + mm_comp_h - 2;
        struct ToriRSRenderCommand* lp_dot =
            LibToriRS_RenderCommandBufferEmplaceCommand(game->uiscene_queued_commands);
        lp_dot->kind = TORIRS_GFX_DRAW_RECT;
        lp_dot->_rect_draw.x = dot_x - 1;
        lp_dot->_rect_draw.y = dot_y - 1;
        lp_dot->_rect_draw.w = 3;
        lp_dot->_rect_draw.h = 3;
        lp_dot->_rect_draw.color_rgb = 0xFFFFFF;
        lp_dot->_rect_draw.alpha = 0;
        lp_dot->_rect_draw.fill = 1;
    }

mm_done:;
}

static void
queue_static_load_scene2_events(
    struct Scene2* scene2,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !scene2 )
        return;

    struct Scene2Event scene_event = { 0 };
    while( scene2_eventbuffer_pop(scene2, &scene_event) )
    {
        switch( scene_event.type )
        {
        case SCENE2_EVENT_TEXTURE_BATCH_BEGIN:
        {
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind = TORIRS_GFX_BATCH2D_TEX_BEGIN;
            ev->_batch.batch_id = scene_event.u.batch.batch_id;
            continue;
        }
        case SCENE2_EVENT_TEXTURE_BATCH_END:
        {
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind = TORIRS_GFX_BATCH2D_TEX_END;
            ev->_batch.batch_id = scene_event.u.batch.batch_id;
            continue;
        }
        case SCENE2_EVENT_TEXTURE_LOADED:
        {
            int tex_id = scene_event.u.texture.texture_id;
            struct DashTexture* texture = scene2_texture_get(scene2, tex_id);
            queue_texture_load_from_event(render_command_buffer, tex_id, texture);
            continue;
        }
        case SCENE2_EVENT_ANIMATION_LOADED:
        {
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind = scene_event.batched ? TORIRS_GFX_BATCH3D_ANIM_ADD : TORIRS_GFX_RES_ANIM_LOAD;
            ev->_animation_load.model = scene_event.u.animation.model;
            ev->_animation_load.frame = scene_event.u.animation.frame;
            ev->_animation_load.framemap = scene_event.u.animation.framemap;
            ev->_animation_load.visual_id = scene_event.u.animation.visual_id;
            ev->_animation_load.anim_id = scene_event.u.animation.anim_id;
            ev->_animation_load.animation_index = scene_event.u.animation.animation_index;
            ev->_animation_load.frame_index = scene_event.u.animation.frame_index;
            ev->_animation_load.usage_hint = (uint8_t)torirs_usage_hint_for_scene2_category(
                (enum Scene2ElementCategory)scene_event.u.animation.element_category);
            continue;
        }
        case SCENE2_EVENT_BATCH_BEGIN:
        {
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind = TORIRS_GFX_BATCH3D_BEGIN;
            ev->_batch.batch_id = scene_event.u.batch.batch_id;
            ev->_batch.usage_hint = (uint8_t)TORIRS_USAGE_SCENERY;
            continue;
        }
        case SCENE2_EVENT_BATCH_END:
        {
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind = TORIRS_GFX_BATCH3D_END;
            ev->_batch.batch_id = scene_event.u.batch.batch_id;
            ev->_batch.usage_hint = (uint8_t)TORIRS_USAGE_SCENERY;
            continue;
        }
        case SCENE2_EVENT_BATCH_CLEAR:
        {
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind = TORIRS_GFX_BATCH3D_CLEAR;
            ev->_batch.batch_id = scene_event.u.batch.batch_id;
            ev->_batch.usage_hint = (uint8_t)TORIRS_USAGE_SCENERY;
            continue;
        }
        case SCENE2_EVENT_MODEL_LOADED:
        {
            int eid = scene_event.u.model.element_id;
            if( eid < 0 || eid >= scene2_elements_total(scene2) )
                continue;
            struct Scene2Element* el = scene2_element_at(scene2, eid);
            if( !el || !scene2_element_is_active(el) || !scene2_element_dash_model(el) )
                continue;
            if( scene2_element_parent_entity_id(el) != scene_event.u.model.parent_entity_id )
                continue;
            struct DashModel* model = scene_event.u.model.model;
            uint64_t model_key = model_cache_key_u64(scene2, el);
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind =
                scene_event.batched ? TORIRS_GFX_BATCH3D_MODEL_ADD : TORIRS_GFX_RES_MODEL_LOAD;
            ev->_model_load.model = model;
            ev->_model_load.model_key = model_key;
            ev->_model_load.visual_id = scene_event.u.model.visual_id;
            ev->_model_load.usage_hint = (uint8_t)torirs_usage_hint_for_scene2_category(
                (enum Scene2ElementCategory)scene_event.u.model.element_category);
            ev->_model_load.world_x = scene_event.u.model.world_x;
            ev->_model_load.world_y = scene_event.u.model.world_y;
            ev->_model_load.world_z = scene_event.u.model.world_z;
            ev->_model_load.world_yaw_r2pi2048 = scene_event.u.model.world_yaw_r2pi2048;
            continue;
        }
        case SCENE2_EVENT_MODEL_UNLOADED:
        {
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind = TORIRS_GFX_RES_MODEL_UNLOAD;
            ev->_model_load.model = scene_event.u.model.model;
            ev->_model_load.model_key = 0;
            ev->_model_load.visual_id = scene_event.u.model.visual_id;
            ev->_model_load.usage_hint = (uint8_t)torirs_usage_hint_for_scene2_category(
                (enum Scene2ElementCategory)scene_event.u.model.element_category);
            ev->_model_load.world_x = 0;
            ev->_model_load.world_y = 0;
            ev->_model_load.world_z = 0;
            ev->_model_load.world_yaw_r2pi2048 = 0;
            continue;
        }
        default:
            break;
        }
    }
    scene2_flush_deferred_array_frees(scene2);
}

static void
queue_static_load_uiscene_events(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !game->ui_scene )
        return;

    struct UISceneEvent ui_event = { 0 };
    while( uiscene_eventbuffer_pop(game->ui_scene, &ui_event) )
    {
        switch( ui_event.type )
        {
        case UISCENE_EVENT_ELEMENT_ACQUIRED:
        {
            struct UISceneElement* element =
                uiscene_element_at(game->ui_scene, ui_event.element_id);
            if( !element || !element->dash_sprites )
                continue;
            for( int ai = 0; ai < element->dash_sprites_count; ++ai )
            {
                struct DashSprite* sp = element->dash_sprites[ai];
                if( !sp )
                    continue;
                queue_sprite_load_from_event(render_command_buffer, ui_event.element_id, ai, sp);
            }
            break;
        }
        case UISCENE_EVENT_ELEMENT_RELEASED:
        {
            for( int ri = 0; ri < ui_event.released_sprites_count; ri++ )
            {
                struct DashSprite* sp = ui_event.released_sprites[ri];
                if( !sp )
                    continue;
                queue_sprite_unload_from_event(render_command_buffer, ui_event.element_id, ri, sp);
            }
            if( ui_event.released_sprites )
            {
                if( !ui_event.released_sprites_borrowed )
                {
                    for( int ri = 0; ri < ui_event.released_sprites_count; ri++ )
                    {
                        if( ui_event.released_sprites[ri] )
                            dashsprite_free(ui_event.released_sprites[ri]);
                    }
                }
                free(ui_event.released_sprites);
            }
            break;
        }
        case UISCENE_EVENT_FONT_ADDED:
        {
            struct DashPixFont* font = uiscene_font_get(game->ui_scene, ui_event.font_id);
            if( font )
                queue_font_load_from_event(render_command_buffer, ui_event.font_id, font);
            break;
        }
        case UISCENE_EVENT_BATCH_SPRITE_BEGIN:
        {
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind = TORIRS_GFX_BATCH2D_SPRITE_BEGIN;
            ev->_batch.batch_id = ui_event.batch_id;
            break;
        }
        case UISCENE_EVENT_BATCH_SPRITE_END:
        {
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind = TORIRS_GFX_BATCH2D_SPRITE_END;
            ev->_batch.batch_id = ui_event.batch_id;
            break;
        }
        case UISCENE_EVENT_BATCH_FONT_BEGIN:
        {
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind = TORIRS_GFX_BATCH2D_FONT_BEGIN;
            ev->_batch.batch_id = ui_event.batch_id;
            break;
        }
        case UISCENE_EVENT_BATCH_FONT_END:
        {
            struct ToriRSRenderCommand* ev =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            ev->kind = TORIRS_GFX_BATCH2D_FONT_END;
            ev->_batch.batch_id = ui_event.batch_id;
            break;
        }
        default:
            break;
        }
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

    queue_static_load_scene2_events(scene2, render_command_buffer);

    queue_static_load_uiscene_events(game, render_command_buffer);
}

/** True if `(px,py)` lies inside the `[component:world]` layout rect (first match in tree). */
static bool
frame_point_in_world_builtin_xy(
    struct GGame* game,
    int px,
    int py)
{
    if( !game || !game->ui_root_buffer )
        return false;
    for( uint32_t i = 0; i < game->ui_root_buffer->component_count; i++ )
    {
        struct StaticUIComponent* c = &game->ui_root_buffer->components[i];
        if( c->type != UIELEM_BUILTIN_WORLD )
            continue;
        if( c->position.kind != UIPOS_XY )
            continue;
        int x = c->position.x;
        int y = c->position.y;
        int w = c->position.width;
        int h = c->position.height;
        if( w <= 0 || h <= 0 )
            continue;
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    return false;
}

/**
 * Screen coords for world 3D pick: use the click press position when a click fired this frame
 * (ensures the pickset matches the crosshair anchor), otherwise the live cursor.
 */
static void
frame_world_pick_pointer_xy(
    struct GGame* game,
    int* out_px,
    int* out_py)
{
    if( game->mouse.buttons[TORIRSM_LEFT].click_this_frame )
    {
        *out_px = game->mouse.buttons[TORIRSM_LEFT].click_press_x;
        *out_py = game->mouse.buttons[TORIRSM_LEFT].click_press_y;
    }
    else
    {
        *out_px = game->mouse.cursor_x;
        *out_py = game->mouse.cursor_y;
    }
}

/** Live cursor for terrain tile hover (`mouse_tile_*` and hull overlay). */
static void
frame_world_hover_pointer_xy(
    struct GGame* game,
    int* out_px,
    int* out_py)
{
    *out_px = game->mouse.cursor_x;
    *out_py = game->mouse.cursor_y;
}

/** 3D tile/entity pick only when UITree hover is the world panel (or fallback rect match). */
static bool
frame_world_pick_allowed(struct GGame* game)
{
    if( !game || !game->ui_root_buffer )
        return false;
    if( game->minimenu.visible )
        return false;

    struct UITree* tree = game->ui_root_buffer;
    int h = tree->hover_node_index;
    if( h >= 0 && (uint32_t)h < tree->component_count )
        return tree->components[h].type == UIELEM_BUILTIN_WORLD;

    /* Fallback: check whether the live cursor is inside the world panel rect. */
    return frame_point_in_world_builtin_xy(game, game->mouse.cursor_x, game->mouse.cursor_y);
}

void
LibToriRS_FrameBegin(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    game->at_painters_command_index = 0;
    game->at_render_command_index = 0;
    game->at_ui_render_command_index = 0;

    if( game->cross_mode != 0 )
    {
        game->cross_cycle += 20;
        if( game->cross_cycle >= 400 )
            game->cross_mode = 0;
    }

    game->tile_clicked_x = -1;
    game->tile_clicked_z = -1;
    game->tile_clicked_level = -1;
    game->mouse_tile_x = -1;
    game->mouse_tile_z = -1;
    game->mouse_tile_level = -1;
    game->tile_hover_overlay_emitted = false;
    game->tile_hover_hull_buffered = false;

    game->camera->pitch = game->camera_pitch;
    game->camera->yaw = game->camera_yaw;
    game->camera->roll = game->camera_roll;

    game->uitree_stack_top = -1;
    game->uitree_current = (game->ui_root_buffer && game->ui_root_buffer->root_index >= 0)
                               ? game->ui_root_buffer->root_index
                               : -1;
    if( game->ui_root_buffer )
    {
        game->ui_root_buffer->ui_layer_stack_top = -1;
        uitree_textpool_reset(&game->ui_root_buffer->text_pool);
    }
    game->uiscene_command_idx = 0;
    if( game->minimenu_commands )
        minimenu_commands_reset(game->minimenu_commands);
    if( game->uiscene_queued_commands )
        LibToriRS_RenderCommandBufferReset(game->uiscene_queued_commands);

    interface_update_region_hover_ids(game);

    if( game->ui_root_buffer )
        uitree_sync_hover_option_set(game, game->mouse.cursor_x, game->mouse.cursor_y);

    if( game->ui_root_buffer )
        uitree_mark_all_dirty(game->ui_root_buffer);

    /* 3D clear / projection / cull use `view_port` + offsets; keep them in sync with the
       `[component:world]` layout rect from static UI (see plan: world INI is source of truth). */
    if( game->ui_root_buffer && game->view_port )
    {
        struct UITree* tree = game->ui_root_buffer;
        for( uint32_t i = 0; i < tree->component_count; i++ )
        {
            struct StaticUIComponent* c = &tree->components[i];
            if( c->type != UIELEM_BUILTIN_WORLD )
                continue;
            if( c->position.kind != UIPOS_XY )
                continue;
            if( c->position.width <= 0 || c->position.height <= 0 )
                break;
            if( game->viewport_offset_x == c->position.x &&
                game->viewport_offset_y == c->position.y &&
                game->view_port->width == c->position.width &&
                game->view_port->height == c->position.height )
            {
                break;
            }
            LibToriRS_GameSetWorldViewportRect(
                game, c->position.x, c->position.y, c->position.width, c->position.height);
            break;
        }
    }

    game->frame_pass = FRAME_PASS_NONE;

    world_pickset_reset(&game->pickset);

    LibToriRS_RenderCommandBufferReset(render_command_buffer);
    queue_static_load_commands(game, render_command_buffer);

    if( game->world && game->world->load_complete && game->world->painter &&
        game->sys_painter_buffer && game->world->cullmap )
    {
        struct PaintersBuffer* buffer = game->sys_painter_buffer;

        struct Painter* painter = game->world->painter;
        int camera_sx = (game->camera_world_x) / 128;
        int camera_sz = (game->camera_world_z) / 128;
        int camera_slevel = game->camera_world_y / 240;

        painter_set_camera_angles(painter, game->camera_pitch, game->camera_yaw);
        painter_set_level_mask(painter, frame_ui_world_level_mask(game));

        static int painter_bench_frames;
        static uint64_t painter_bench_sum_paint_ns;
        static uint64_t painter_bench_sum_paint3_ns;
        static uint64_t painter_bench_sum_paint4_ns;
        struct timespec t0, t1;
        uint64_t dt_paint_ns;
        uint64_t dt_paint3_ns;
        uint64_t dt_paint4_ns;

        // painter_paint_world3d(painter, buffer, camera_sx, camera_sz, camera_slevel);
        painter_paint_bucket(painter, buffer, camera_sx, camera_sz, camera_slevel);
        // if( (rand() & 1) == 0 )
        // {
        //     clock_gettime(CLOCK_MONOTONIC, &t0);
        //     painter_paint_world3d(painter, buffer, camera_sx, camera_sz, camera_slevel);
        //     clock_gettime(CLOCK_MONOTONIC, &t1);
        //     dt_paint_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull +
        //                   (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        //     clock_gettime(CLOCK_MONOTONIC, &t0);
        //     painter_paint_bucket(painter, buffer, camera_sx, camera_sz, camera_slevel);
        //     clock_gettime(CLOCK_MONOTONIC, &t1);
        //     dt_paint3_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull +
        //                    (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        // }
        // else
        // {
        //     clock_gettime(CLOCK_MONOTONIC, &t0);
        //     painter_paint_bucket(painter, buffer, camera_sx, camera_sz, camera_slevel);
        //     clock_gettime(CLOCK_MONOTONIC, &t1);
        //     dt_paint3_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull +
        //                    (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        //     clock_gettime(CLOCK_MONOTONIC, &t0);
        //     painter_paint_world3d(painter, buffer, camera_sx, camera_sz, camera_slevel);
        //     clock_gettime(CLOCK_MONOTONIC, &t1);
        //     dt_paint_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull +
        //                   (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        // }

        // painter_bench_sum_paint_ns += dt_paint_ns;
        // painter_bench_sum_paint3_ns += dt_paint3_ns;
        // painter_bench_sum_paint4_ns += dt_paint4_ns;
        // painter_bench_frames++;
        // if( painter_bench_frames >= 30 )
        // {
        //     fprintf(
        //         stderr,
        //         "painter bench (avg over %d frames): paint_w3d=%.3f ms paint_bucket=%.3f ms \n",
        //         painter_bench_frames,
        //         (double)painter_bench_sum_paint_ns / (double)painter_bench_frames / 1e6,
        //         (double)painter_bench_sum_paint3_ns / (double)painter_bench_frames / 1e6);
        //     painter_bench_frames = 0;
        //     painter_bench_sum_paint_ns = 0;
        //     painter_bench_sum_paint3_ns = 0;
        //     painter_bench_sum_paint4_ns = 0;
        // }
    }

    /* Minimenu, crosshair, and hover tooltip are emitted via UITree (see rev_245_2_ui.ini). */
}

static void
element_step_animation(
    struct Scene2Element* scene_element,
    struct EntityAnimation* animation)
{
    struct Scene2Frames* primary = scene2_element_primary_frames(scene_element);
    struct Scene2Frames* secondary = scene2_element_secondary_frames(scene_element);

    uint16_t primary_id = animation->primary_anim.anim_id;
    uint16_t secondary_id = animation->secondary_anim.anim_id;
    bool primary_active = primary_id != 0 && primary_id != (uint16_t)-1;
    bool secondary_active = secondary_id != 0 && secondary_id != (uint16_t)-1;

    if( primary_active && primary && primary->count > 0 )
    {
        int frame = animation->primary_anim.frame;
        scene2_element_set_active_anim_id(scene_element, primary_id);
        scene2_element_set_active_animation_index(scene_element, 0);
        scene2_element_set_active_frame(scene_element, (uint8_t)frame);
    }
    else if( secondary_active && secondary && secondary->count > 0 )
    {
        int frame = animation->secondary_anim.frame;
        scene2_element_set_active_anim_id(scene_element, secondary_id);
        scene2_element_set_active_animation_index(scene_element, 1);
        scene2_element_set_active_frame(scene_element, (uint8_t)frame);
    }
    else
    {
        scene2_element_set_active_anim_id(scene_element, 0);
        scene2_element_set_active_animation_index(scene_element, 0);
        scene2_element_set_active_frame(scene_element, 0);
    }
}

static void
entity_player_animate(
    struct World* world,
    int player_entity_id)
{
    if( player_entity_id < 0 || player_entity_id >= entity_vec_count(&world->players) )
        return;
    struct PlayerEntity* player = world_player(world, player_entity_id);
    struct EntityAnimation* animation = &player->animation;
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, player->scene_element2.element_id);
    if( !scene_element )
        return;

    element_step_animation(scene_element, animation);
}

static void
entity_npc_animate(
    struct World* world,
    int npc_entity_id)
{
    if( npc_entity_id < 0 || npc_entity_id >= entity_vec_count(&world->npcs) )
        return;
    struct NPCEntity* npc = world_npc(world, npc_entity_id);
    struct EntityAnimation* animation = &npc->animation;
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, npc->scene_element2.element_id);
    if( !scene_element )
        return;

    element_step_animation(scene_element, animation);
}

static void
entity_projectile_animate(
    struct World* world,
    int projectile_entity_id)
{
    if( projectile_entity_id < 0 || projectile_entity_id >= entity_vec_count(&world->projectiles) )
        return;
    struct ProjectileEntity* p = world_projectile(world, projectile_entity_id);
    struct EntityAnimation* animation = &p->animation;
    struct Scene2Element* scene_element =
        scene2_element_at(world->scene2, p->scene_element2.element_id);
    if( !scene_element )
        return;

    element_step_animation(scene_element, animation);
}

static void
entity_map_build_loc_entity_animate(
    struct World* world,
    int map_build_loc_entity_id)
{
    if( map_build_loc_entity_id < 0 ||
        map_build_loc_entity_id >= entity_vec_count(&world->map_build_loc_entities) )
        return;
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

        element_step_animation(scene_element, animation);
    }

    if( map_build_loc_entity->scene_element_two.element_id != -1 )
    {
        animation = &map_build_loc_entity->animation_two;
        scene_element =
            scene2_element_at(world->scene2, map_build_loc_entity->scene_element_two.element_id);
        scene2_element_expect(scene_element, "entity_map_build_loc_entity_animate secondary");

        element_step_animation(scene_element, animation);
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
    case ENTITY_KIND_PROJECTILE:
        entity_projectile_animate(world, entity_id_from_uid(entity_uid));
        break;
    case ENTITY_KIND_MAP_BUILD_TILE:
    case ENTITY_KIND_OBJ_STACK:
        return;
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
        int lid = entity_id_from_uid(entity_id);
        if( lid < 0 || lid >= entity_vec_count(&world->map_build_loc_entities) )
            return false;
        struct MapBuildLocEntity* map_build_loc_entity = world_loc_entity(world, lid);
        return map_build_loc_entity->interactable;
    }
    case ENTITY_KIND_OBJ_STACK:
    {
        int payload = entity_id_from_uid((uint32_t)entity_id);
        int eid     = entity_obj_stack_eid_from_uid_payload(payload);
        if( eid < 0 || eid >= entity_vec_count(&world->obj_stack_entities) )
            return false;
        struct ObjStackEntity* ost = world_obj_stack_entity(world, eid);
        return ost && ost->alive;
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
        int pid = entity_id_from_uid(entity_uid);
        if( pid < 0 || pid >= entity_vec_count(&world->players) )
            return;
        struct PlayerEntity* player = world_player(world, pid);
        coords->x = player->pathing.route_x[0];
        coords->z = player->pathing.route_z[0];
    }
    break;
    case ENTITY_KIND_NPC:
    {
        int nid = entity_id_from_uid(entity_uid);
        if( nid < 0 || nid >= entity_vec_count(&world->npcs) )
            return;
        struct NPCEntity* npc = world_npc(world, nid);
        coords->x = npc->pathing.route_x[0];
        coords->z = npc->pathing.route_z[0];
    }
    break;
    case ENTITY_KIND_MAP_BUILD_LOC:
    {
        int lid = entity_id_from_uid(entity_uid);
        if( lid < 0 || lid >= entity_vec_count(&world->map_build_loc_entities) )
            return;
        struct MapBuildLocEntity* map_build_loc_entity = world_loc_entity(world, lid);
        coords->x = map_build_loc_entity->scene_coord.sx;
        coords->z = map_build_loc_entity->scene_coord.sz;
    }
    break;
    case ENTITY_KIND_OBJ_STACK:
    {
        int oid =
            entity_obj_stack_eid_from_uid_payload(entity_id_from_uid((uint32_t)entity_uid));
        if( oid < 0 || oid >= entity_vec_count(&world->obj_stack_entities) )
            return;
        struct ObjStackEntity* ost = world_obj_stack_entity(world, oid);
        coords->x = ost->world_tile_x;
        coords->z = ost->world_tile_z;
    }
    break;
    }
}

static bool
uielem_redstone_tab_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_redstone_tab_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    struct StaticUIComponent* component = node;
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

    bool is_active = (game->iface->selected_tab == tabno);
    int sid =
        is_active ? component->u.redstone_tab.scene_id_active : component->u.redstone_tab.scene_id;
    int ai = is_active ? component->u.redstone_tab.atlas_index_active
                       : component->u.redstone_tab.atlas_index;
    if( sid < 0 )
    {
        return true;
    }

    struct UISceneElement* elem = uiscene_element_at(game->ui_scene, sid);
    if( !elem || !elem->dash_sprites )
        return true;
    struct DashSprite* sp = elem->dash_sprites[ai];
    if( !sp )
        return true;

    int draw_x = x;
    int draw_y = y;
    frame_emit_pass(fiber, FRAME_PASS_2D);
    queue_sprite_draw_from_event(game->uiscene_queued_commands, sid, ai, sp, draw_x, draw_y, 0);

    return true;
}

static bool
uielem_builtin_sidebar_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_builtin_sidebar_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    struct StaticUIComponent* component = node;
    assert(component->type == UIELEM_BUILTIN_SIDEBAR);
    if( !game || !game->iface )
        return true;

    /* Wrong tab or modal sidebar: emit nothing (traversal also skips first_child subtree). */
    if( !frame_sidebar_tab_active(game, component) )
        return true;

    /* Active tab: panel is drawn by RS child nodes under this builtin. */
    return true;
}

static inline bool
uielem_builtin_chat_dialog_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static bool
uielem_builtin_chat_dialog_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_builtin_chat_dialog_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    assert(node->type == UIELEM_BUILTIN_CHAT_DIALOG);
    (void)game;
    /* IF_OPENCHAT subtree is drawn by RS children under this builtin (expand_chat_dialog_rs_tree). */
    return true;
}

static inline bool
uielem_builtin_sidebar_overlay_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static bool
uielem_builtin_sidebar_overlay_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_builtin_sidebar_overlay_is_dirty(fiber, node) )
        return true;
    assert(node->type == UIELEM_BUILTIN_SIDEBAR_OVERLAY);
    /* IF_OPENSIDE subtree: RS children under this builtin (expand_sidebar_overlay_rs_tree). */
    return true;
}

static bool
uielem_sprite_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_sprite_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    struct StaticUIComponent* component = node;
    assert(component->type == UIELEM_BUILTIN_SPRITE);

    struct UISceneElement* element =
        uiscene_element_at(game->ui_scene, component->u.sprite.scene_id);
    if( !element )
        return true;

    struct DashSprite* sprite = element->dash_sprites[component->u.sprite.atlas_index];
    if( !sprite )
        return true;

    frame_emit_pass(fiber, FRAME_PASS_2D);
    queue_sprite_draw_from_event(
        game->uiscene_queued_commands,
        component->u.sprite.scene_id,
        component->u.sprite.atlas_index,
        sprite,
        component->position.x,
        component->position.y,
        0);

    return true;
}

/** Visual id reserved for tile hover overlay (not from Scene2). */
enum
{
    TILE_HOVER_OVERLAY_VISUAL_ID = 0x7e500001,
    TILE_HOVER_HULL_MAX_VERTS = 4,
    TILE_HOVER_MAX_FACES = TILE_HOVER_HULL_MAX_VERTS - 2
};

/**
 * Called from PNTR_CMD_TERRAIN when the hovered tile is identified.
 *
 * Uses dash3d_project_point (read-only; never touches dash->screen_vertices_*) to project the 4
 * tile corners, computes the convex hull, and populates game->tile_hover_overlay_model with
 * model-LOCAL vertices (relative to the tile's SW corner).  The SW-corner camera-relative
 * position is saved to game->tile_hover_overlay_pos so frame_tile_hover_overlay_emit can call
 * dash3d_project_model with the right offset — matching the same convention used by terrain tiles.
 *
 * Using model-local vertices keeps model_min_depth small (~tile half-diagonal, ≈90) so that
 * screen_vertices_z values (also small, relative to the model center) yield depth_avg < 1500
 * in the bucket sort.  Face winding is reversed vs. the convex-hull CCW order (math Y-up) so
 * the triangles are CCW in screen space (Y-down), passing the bucket-sort's back-face cull.
 */
static void
frame_tile_hover_overlay_buffer(struct GGame* game, int sx, int sz, int lvl)
{
    if( !game->tile_hover_overlay_model || !game->world || !game->world->heightmap ||
        !game->sys_dash || !game->view_port || !game->camera )
        return;

    struct HeightmapHeights heights = { 0 };
    heightmap_get_heights(game->world->heightmap, sx, sz, lvl, &heights);

    /* Camera-relative world coords of each corner (SW=0, SE=1, NE=2, NW=3). */
    int32_t rel_x[4] = {
        sx * 128 - game->camera_world_x,
        (sx + 1) * 128 - game->camera_world_x,
        (sx + 1) * 128 - game->camera_world_x,
        sx * 128 - game->camera_world_x,
    };
    int32_t rel_y[4] = {
        (int32_t)heights.sw_height - game->camera_world_y,
        (int32_t)heights.se_height - game->camera_world_y,
        (int32_t)heights.ne_height - game->camera_world_y,
        (int32_t)heights.nw_height - game->camera_world_y,
    };
    int32_t rel_z[4] = {
        sz * 128 - game->camera_world_z,
        sz * 128 - game->camera_world_z,
        (sz + 1) * 128 - game->camera_world_z,
        (sz + 1) * 128 - game->camera_world_z,
    };

    /* Model position = camera-relative SW corner.  Vertices stored relative to this origin. */
    struct DashPosition pos = { 0 };
    pos.x = rel_x[0];
    pos.y = rel_y[0];
    pos.z = rel_z[0];

    float* in_px = game->tile_hover_scratch;
    float* in_py = game->tile_hover_scratch + 8;
    float* out_hx = game->tile_hover_scratch + 16;
    float* out_hy = game->tile_hover_scratch + 24;

    int orig_idx[4];
    int n_ok = 0;
    for( int i = 0; i < 4; i++ )
    {
        if( rel_x[i] < -32768 || rel_x[i] > 32767 || rel_y[i] < -32768 || rel_y[i] > 32767 ||
            rel_z[i] < -32768 || rel_z[i] > 32767 )
            return;

        int scr_x = 0;
        int scr_y = 0;
        if( !dash3d_project_point(
                game->sys_dash, rel_x[i], rel_y[i], rel_z[i], game->view_port, game->camera,
                &scr_x, &scr_y) )
            continue;
        orig_idx[n_ok] = i;
        in_px[n_ok] = (float)scr_x;
        in_py[n_ok] = (float)scr_y;
        n_ok++;
    }

    if( n_ok < 3 )
        return;

    size_t hull_n = compute_convex_hull(in_px, in_py, (size_t)n_ok, out_hx, out_hy);
    if( hull_n < 3 )
        return;

    int32_t hull_vx[TILE_HOVER_HULL_MAX_VERTS];
    int32_t hull_vy[TILE_HOVER_HULL_MAX_VERTS];
    int32_t hull_vz[TILE_HOVER_HULL_MAX_VERTS];
    int hv = 0;
    for( size_t hi = 0; hi < hull_n; hi++ )
    {
        int best_j = -1;
        float best_d = 1e30f;
        for( int j = 0; j < n_ok; j++ )
        {
            float dx = out_hx[hi] - in_px[j];
            float dy = out_hy[hi] - in_py[j];
            float d = dx * dx + dy * dy;
            if( d < best_d )
            {
                best_d = d;
                best_j = j;
            }
        }
        if( best_j < 0 || best_d > 25.0f )
            return;
        int ci = orig_idx[best_j];
        /* Model-local coords: offset from SW corner so vertices are small (0..128 in X/Z). */
        hull_vx[hv] = rel_x[ci] - pos.x;
        hull_vy[hv] = rel_y[ci] - pos.y;
        hull_vz[hv] = rel_z[ci] - pos.z;
        hv++;
    }

    int face_count = hv - 2;
    if( face_count < 1 || face_count > TILE_HOVER_MAX_FACES )
        return;

    int16_t face_ia[TILE_HOVER_MAX_FACES];
    int16_t face_ib[TILE_HOVER_MAX_FACES];
    int16_t face_ic[TILE_HOVER_MAX_FACES];
    for( int f = 0; f < face_count; f++ )
    {
        face_ia[f] = 0;
        /* Swap B and C to get CCW winding in screen-space (Y-down), which is the opposite of
         * the CCW-in-math-coords (Y-up) order returned by the convex hull algorithm. */
        face_ib[f] = (int16_t)(f + 2);
        face_ic[f] = (int16_t)(f + 1);
    }

    hsl16_t hull_hsl = (hsl16_t)(uint16_t)0xAF26u;
    uint16_t hsl_a[TILE_HOVER_MAX_FACES];
    uint16_t hsl_b[TILE_HOVER_MAX_FACES];
    uint16_t hsl_c[TILE_HOVER_MAX_FACES];
    alphaint_t face_alpha[TILE_HOVER_MAX_FACES];
    int32_t face_tex[TILE_HOVER_MAX_FACES];
    for( int f = 0; f < face_count; f++ )
    {
        hsl_a[f] = (uint16_t)hull_hsl;
        hsl_b[f] = (uint16_t)hull_hsl;
        hsl_c[f] = (uint16_t)hull_hsl;
        face_alpha[f] = (alphaint_t)115;
        face_tex[f] = -1;
    }

    struct DashModel* m = game->tile_hover_overlay_model;
    dashmodel_set_vertices_i32(m, hv, hull_vx, hull_vy, hull_vz);
    dashmodel_set_face_indices_i16(m, face_count, face_ia, face_ib, face_ic);
    dashmodel_set_face_colors_i16(m, hsl_a, hsl_b, hsl_c);
    dashmodel_set_face_alphas(m, face_alpha, face_count);
    dashmodel_set_face_textures_i32(m, face_tex, face_count);
    dashmodel_set_bounds_cylinder(m);
    dashmodel_set_loaded(m, true);

    game->tile_hover_overlay_pos = pos;
    game->tile_hover_hull_buffered = true;
}

/**
 * Called from world_done (in its own uielem_world_step iteration, after every terrain DRAW_MODEL
 * has been drained by the platform).  Projects the pre-built overlay model using the stored
 * tile position and queues one TORIRS_GFX_DRAW_MODEL command.  Returns true iff queued.
 */
static bool
frame_tile_hover_overlay_emit(struct UIFrameState* fiber, struct GGame* game)
{
    if( !fiber || !game->uiscene_queued_commands || !game->tile_hover_overlay_model ||
        !game->tile_hover_hull_buffered || !game->sys_dash ||
        !game->view_port || !game->camera )
        return false;

    struct DashModel* m = game->tile_hover_overlay_model;
    struct DashPosition pos = game->tile_hover_overlay_pos;
    struct DashPosition world_pos = { 0 };

    int cull = dash3d_project_model(game->sys_dash, m, &pos, game->view_port, game->camera);
    if( cull != DASHCULL_VISIBLE )
        return false;

    frame_emit_pass(fiber, FRAME_PASS_3D);
    {
        struct ToriRSRenderCommand* rc =
            LibToriRS_RenderCommandBufferEmplaceCommand(game->uiscene_queued_commands);
        memset(rc, 0, sizeof(*rc));
        rc->kind = TORIRS_GFX_DRAW_MODEL;
        rc->_model_draw.model = m;
        rc->_model_draw.model_key =
            ((uint64_t)(uint32_t)game->cycle << 32) ^ (uint64_t)(uintptr_t)m ^ 0x54494c48u;
        rc->_model_draw.visual_id = TILE_HOVER_OVERLAY_VISUAL_ID;
        rc->_model_draw.element_id = -1;
        rc->_model_draw.use_animation = false;
        rc->_model_draw.animation_index = 0;
        rc->_model_draw.frame_index = 0;
        memcpy(&rc->_model_draw.position, &pos, sizeof(pos));
        memcpy(&rc->_model_draw.world_position, &world_pos, sizeof(world_pos));
        rc->_model_draw.usage_hint = (uint8_t)TORIRS_USAGE_SCENERY;
        rc->_model_draw.animation_frame = NULL;
        rc->_model_draw.animation_framemap = NULL;
        rc->_model_draw.uv_offset_u = 0.f;
        rc->_model_draw.uv_offset_v = 0.f;
        rc->_model_draw.alpha_mode = 1;
    }
    return true;
}

static bool
uielem_world_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_world_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    struct StaticUIComponent* component = node;
    assert(component->type == UIELEM_BUILTIN_WORLD);

    struct PaintersElementCommand* cmd = NULL;
    struct DashPosition position = { 0 };
    struct Scene2Element* scene_element = NULL;

    if( !game->sys_painter_buffer )
    {
        // if( game->uiscene_queued_commands )
        //     frame_emit_pass(fiber, FRAME_PASS_NONE);
        return true;
    }

    int cap = game->sys_painter_buffer->command_count;
    if( game->cc < cap )
        cap = game->cc;

    if( game->at_painters_command_index == 0 && game->view_port && game->uiscene_queued_commands &&
        game->view_port->width > 0 && game->view_port->height > 0 )
    {
        frame_emit_pass(fiber, FRAME_PASS_2D);
        struct ToriRSRenderCommand* clr =
            LibToriRS_RenderCommandBufferEmplaceCommand(game->uiscene_queued_commands);
        clr->kind = TORIRS_GFX_STATE_CLEAR_RECT;
        clr->_clear_rect.x = game->viewport_offset_x;
        clr->_clear_rect.y = game->viewport_offset_y;
        clr->_clear_rect.w = game->view_port->width;
        clr->_clear_rect.h = game->view_port->height;
    }

next:
    if( game->at_painters_command_index >= cap )
        goto world_done;

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

        struct DashPosition world_position = { 0 };
        memcpy(&world_position, ent_pos, sizeof(struct DashPosition));

        position.x = position.x - game->camera_world_x;
        position.y = position.y - game->camera_world_y;
        position.z = position.z - game->camera_world_z;

        int cull = DASHCULL_VISIBLE;

        cull = dash3d_project_model(
            game->sys_dash, ent_model, &position, game->view_port, game->camera);

        if( cull != DASHCULL_VISIBLE )
            break;

        uint32_t ent_uid = (uint32_t)scene2_element_parent_entity_id(scene_element);
        entity_animate(game->world, (int)ent_uid);

        /* Mouse-pick: if cursor is over this entity's projected bounds, add it to pickset. */
        if( frame_world_pick_allowed(game) )
        {
            int ppx = 0;
            int ppy = 0;
            frame_world_pick_pointer_xy(game, &ppx, &ppy);
            int mvx = ppx - game->viewport_offset_x;
            int mvy = ppy - game->viewport_offset_y;
            if( mvx >= 0 && mvy >= 0 && entity_interactable(game->world, (int)ent_uid) &&
                dash3d_projected_model_contains(
                    game->sys_dash, ent_model, game->view_port, mvx, mvy) )
            {
                struct EntityCoords coords = { 0, 0 };
                entity_coords_from_element(game->world, (int)ent_uid, &coords);
                world_pickset_add(
                    &game->pickset,
                    coords.x,
                    coords.z,
                    (int)entity_kind_from_uid(ent_uid),
                    entity_id_from_uid(ent_uid));
            }
        }

        /* Terrain-only mouse_tile_* misses thin ground items; refresh from live cursor + entity. */
        if( frame_world_pick_allowed(game) )
        {
            int hpx = 0;
            int hpy = 0;
            frame_world_hover_pointer_xy(game, &hpx, &hpy);
            int hmvx = hpx - game->viewport_offset_x;
            int hmvy = hpy - game->viewport_offset_y;
            if( hmvx >= 0 && hmvy >= 0 && entity_interactable(game->world, (int)ent_uid) &&
                dash3d_projected_model_contains(
                    game->sys_dash, ent_model, game->view_port, hmvx, hmvy) )
            {
                enum EntityKind ek = entity_kind_from_uid(ent_uid);
                if( ek == ENTITY_KIND_PLAYER || ek == ENTITY_KIND_NPC || ek == ENTITY_KIND_MAP_BUILD_LOC ||
                    ek == ENTITY_KIND_OBJ_STACK )
                {
                    struct EntityCoords coords = { -9999, -9999 };
                    entity_coords_from_element(game->world, (int)ent_uid, &coords);
                    if( !(coords.x == -9999 && coords.z == -9999) )
                    {
                        game->mouse_tile_x = coords.x;
                        game->mouse_tile_z = coords.z;
                        if( ek == ENTITY_KIND_OBJ_STACK )
                        {
                            int oid = entity_obj_stack_eid_from_uid_payload(
                                entity_id_from_uid(ent_uid));
                            if( oid >= 0 && oid < entity_vec_count(&game->world->obj_stack_entities) )
                            {
                                struct ObjStackEntity* ost = world_obj_stack_entity(game->world, oid);
                                if( ost && ost->alive && ost->level >= 0 &&
                                    ost->level < MAP_TERRAIN_LEVELS )
                                    game->mouse_tile_level = ost->level;
                            }
                        }
                        else if( ek == ENTITY_KIND_MAP_BUILD_LOC )
                        {
                            int lid = entity_id_from_uid(ent_uid);
                            if( lid >= 0 && lid < entity_vec_count(&game->world->map_build_loc_entities) )
                            {
                                struct MapBuildLocEntity* loc = world_loc_entity(game->world, lid);
                                if( loc )
                                    game->mouse_tile_level = (int)loc->scene_coord.slevel;
                            }
                        }
                        else if( game->mouse_tile_level < 0 || game->mouse_tile_level >= MAP_TERRAIN_LEVELS )
                            game->mouse_tile_level = (game->camera_world_y / 240);
                        if( game->mouse_tile_level < 0 )
                            game->mouse_tile_level = 0;
                        if( game->mouse_tile_level >= MAP_TERRAIN_LEVELS )
                            game->mouse_tile_level = MAP_TERRAIN_LEVELS - 1;
                    }
                }
            }
        }

        if( game->uiscene_queued_commands )
            frame_emit_pass(fiber, FRAME_PASS_3D);

        {
            struct ToriRSRenderCommand* rc =
                LibToriRS_RenderCommandBufferEmplaceCommand(game->uiscene_queued_commands);
            rc->kind = TORIRS_GFX_DRAW_MODEL;
            rc->_model_draw.model = ent_model;
            rc->_model_draw.model_key = model_cache_key_u64(game->world->scene2, scene_element);
            rc->_model_draw.visual_id = scene2_element_visual_id(scene_element);
            rc->_model_draw.element_id = scene2_element_id(game->world->scene2, scene_element);
            rc->_model_draw.use_animation = scene2_element_active_anim_id(scene_element) != 0;
            rc->_model_draw.animation_index = scene2_element_active_animation_index(scene_element);
            rc->_model_draw.frame_index = scene2_element_active_frame(scene_element);
            memcpy(&rc->_model_draw.position, &position, sizeof(struct DashPosition));
            memcpy(&rc->_model_draw.world_position, &world_position, sizeof(struct DashPosition));
            rc->_model_draw.usage_hint = (uint8_t)torirs_usage_hint_for_scene2_category(
                scene2_element_category(scene_element));
            rc->_model_draw.animation_frame = rc->_model_draw.use_animation
                                                  ? scene2_element_dash_animation_frame(
                                                        scene_element,
                                                        rc->_model_draw.animation_index,
                                                        rc->_model_draw.frame_index)
                                                  : NULL;
            rc->_model_draw.animation_framemap =
                rc->_model_draw.use_animation ? scene2_element_dash_framemap(scene_element) : NULL;
        }
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

        struct DashPosition world_position = { 0 };
        memcpy(&world_position, tile_pos, sizeof(struct DashPosition));

        position.x = position.x - game->camera_world_x;
        position.y = position.y - game->camera_world_y;
        position.z = position.z - game->camera_world_z;

        int cull = dash3d_project_model(
            game->sys_dash, tile_model, &position, game->view_port, game->camera);
        if( cull != DASHCULL_VISIBLE )
            break;

        int ppx = 0;
        int ppy = 0;
        frame_world_hover_pointer_xy(game, &ppx, &ppy);
        int mvx = ppx - game->viewport_offset_x;
        int mvy = ppy - game->viewport_offset_y;
        if( frame_world_pick_allowed(game) && mvx >= 0 && mvy >= 0 &&
            dash3d_projected_model_contains(game->sys_dash, tile_model, game->view_port, mvx, mvy) )
        {
            game->mouse_tile_x = sx;
            game->mouse_tile_z = sz;
            game->mouse_tile_level = slevel;
            frame_tile_hover_overlay_buffer(game, sx, sz, slevel);
        }

        if( game->uiscene_queued_commands )
            frame_emit_pass(fiber, FRAME_PASS_3D);

        {
            struct ToriRSRenderCommand* rc =
                LibToriRS_RenderCommandBufferEmplaceCommand(game->uiscene_queued_commands);
            rc->kind = TORIRS_GFX_DRAW_MODEL;
            rc->_model_draw.model = tile_model;
            rc->_model_draw.model_key = model_cache_key_u64(game->world->scene2, scene_element);
            rc->_model_draw.visual_id = scene2_element_visual_id(scene_element);
            rc->_model_draw.element_id = scene2_element_id(game->world->scene2, scene_element);
            rc->_model_draw.use_animation = scene2_element_active_anim_id(scene_element) != 0;
            rc->_model_draw.animation_index = scene2_element_active_animation_index(scene_element);
            rc->_model_draw.frame_index = scene2_element_active_frame(scene_element);
            memcpy(&rc->_model_draw.position, &position, sizeof(struct DashPosition));
            memcpy(&rc->_model_draw.world_position, &world_position, sizeof(struct DashPosition));
            rc->_model_draw.usage_hint = (uint8_t)torirs_usage_hint_for_scene2_category(
                scene2_element_category(scene_element));
            rc->_model_draw.animation_frame = rc->_model_draw.use_animation
                                                  ? scene2_element_dash_animation_frame(
                                                        scene_element,
                                                        rc->_model_draw.animation_index,
                                                        rc->_model_draw.frame_index)
                                                  : NULL;
            rc->_model_draw.animation_framemap =
                rc->_model_draw.use_animation ? scene2_element_dash_framemap(scene_element) : NULL;
        }
    }
    break;
    default:
        break;
    }

    return false;

world_done:
    if( game->uiscene_queued_commands )
    {
        if( !game->tile_hover_overlay_emitted )
        {
            game->tile_hover_overlay_emitted = true;
            if( frame_tile_hover_overlay_emit(fiber, game) )
                return false;
        }
        if( game->debug_entity_pathing )
        {
            frame_emit_pass(fiber, FRAME_PASS_2D);
            entity_pathing_draw_emit(game, game->uiscene_queued_commands);
        }
        frame_emit_pass(fiber, FRAME_PASS_2D);
        entity_hitsplat_emit(game, game->uiscene_queued_commands);
        if( game->debug_collisionmap_overlay )
        {
            frame_emit_pass(fiber, FRAME_PASS_2D);
            collisionmap_overlay_draw_emit(game, game->uiscene_queued_commands);
        }
        frame_emit_pass(fiber, FRAME_PASS_NONE);
    }
    return true;
}

static bool
uielem_minimap_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_minimap_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    struct StaticUIComponent* component = node;
    assert(component->type == UIELEM_BUILTIN_MINIMAP);

    struct UISceneElement* element =
        uiscene_element_at(game->ui_scene, component->u.minimap.scene_id);
    if( !element )
        return true;

    queue_static_ui_minimap_draws(fiber, component);
    return true;
}

static bool
uielem_compass_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_compass_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    struct StaticUIComponent* component = node;
    assert(component->type == UIELEM_BUILTIN_COMPASS);

    struct UISceneElement* element =
        uiscene_element_at(game->ui_scene, component->u.sprite.scene_id);
    if( !element )
        return true;

    struct DashSprite* sprite = element->dash_sprites[component->u.sprite.atlas_index];
    if( !sprite )
        return true;

    frame_emit_pass(fiber, FRAME_PASS_2D);
    {
        struct ToriRSRenderCommand* command =
            LibToriRS_RenderCommandBufferEmplaceCommand(game->uiscene_queued_commands);
        command->kind = TORIRS_GFX_DRAW_SPRITE;
        command->_sprite_draw.element_id = component->u.sprite.scene_id;
        command->_sprite_draw.atlas_index = component->u.sprite.atlas_index;
        command->_sprite_draw.rotated = true;
        command->_sprite_draw.sprite = sprite;
        command->_sprite_draw.dst_bb_x = component->position.x;
        command->_sprite_draw.dst_bb_y = component->position.y;
        command->_sprite_draw.dst_bb_w = component->position.width;
        command->_sprite_draw.dst_bb_h = component->position.height;
        command->_sprite_draw.dst_anchor_x = component->position.anchor_x;
        command->_sprite_draw.dst_anchor_y = component->position.anchor_y;
        command->_sprite_draw.src_bb_x = sprite->crop_x;
        command->_sprite_draw.src_bb_y = sprite->crop_y;
        command->_sprite_draw.src_bb_w = sprite->crop_width;
        command->_sprite_draw.src_bb_h = sprite->crop_height;
        command->_sprite_draw.src_anchor_x = sprite->crop_width >> 1;
        command->_sprite_draw.src_anchor_y = sprite->crop_height >> 1;
        command->_sprite_draw.rotation_r2pi2048 = ((game->camera_yaw) & 0x7ff);
    }

    return true;
}

static bool
uielem_rs_graphic_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node,
    int cur)
{
    if( !uielem_rs_graphic_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    struct StaticUIComponent* component = node;
    assert(component->type == UIELEM_RS_GRAPHIC);
    return rs_gfx_graphic_step(fiber, component, cur);
}

static bool
uielem_rs_text_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_rs_text_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    struct StaticUIComponent* component = node;
    assert(component->type == UIELEM_RS_TEXT);
    return rs_gfx_text_step(fiber, component);
}

static bool
uielem_rs_inv_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_rs_inv_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    struct StaticUIComponent* component = node;
    assert(component->type == UIELEM_RS_INV);
    return rs_gfx_inv_step(fiber, component);
}

static bool
uielem_rs_layer_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_rs_layer_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    if( node->is_hidden )
        return true;
    if( node->u.rs_layer.hide && !interface_component_is_overlay_hovered(game, node->component_id) )
        return true;
    struct StaticUIComponent* component = node;
    assert(component->type == UIELEM_RS_LAYER);
    rs_ui_layer_push(fiber, component);
    return true;
}

static inline bool
uielem_rs_rect_is_dirty(
    struct UIFrameState const* fiber,
    struct StaticUIComponent const* node)
{
    (void)fiber;
    return node->is_dirty;
}

static bool
uielem_rs_rect_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_rs_rect_is_dirty(fiber, node) )
        return true;
    if( node->is_hidden )
        return true;
    assert(node->type == UIELEM_RS_RECT);
    return rs_gfx_rect_step(fiber, node);
}

static bool
uielem_rs_model_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    if( !uielem_rs_model_is_dirty(fiber, node) )
        return true;
    struct GGame* game = fiber->game;
    struct StaticUIComponent* component = node;
    assert(component->type == UIELEM_RS_MODEL);
    return rs_gfx_model_step(fiber, component, s_frame_project_models);
}

static bool
uielem_hover_tooltip_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    struct GGame* game = fiber->game;
    if( !game || !game->iface || !game->ui_scene || !fiber->cmds )
        return true;
    if( game->minimenu.visible )
        return true;

    /* Tooltip source: UITree hover menu when an interface wins over the world layer; otherwise
     * world pick (in viewport) or legacy CacheDat hover line. */
    struct UITree* uit = game->ui_root_buffer;
    int ui_from_uitree = uit && uit->uitree_optionset.option_count > 0 &&
                         !(uit->hover_node_index >= 0 &&
                           uit->components[uit->hover_node_index].type == UIELEM_BUILTIN_WORLD);

    struct WorldOption temp_option = { 0 };
    struct WorldOption* first_opt = NULL;

    static char tooltip_buf[256];

    if( ui_from_uitree )
    {
        struct UITreeOptionSet* uos = &uit->uitree_optionset;
        struct UITreeOption* def = uitree_option_set_get_option(uos, uos->option_count - 1);
        if( uos->option_count > 2 )
            snprintf(
                tooltip_buf,
                sizeof(tooltip_buf),
                "%s @whi@/ %d more options",
                def->text,
                uos->option_count - 2);
        else
            snprintf(tooltip_buf, sizeof(tooltip_buf), "%s", def->text);
    }
    else
    {
        int in_world = 0;
        if( game->ui_root_buffer && game->iface )
        {
            for( uint32_t i = 0; i < game->ui_root_buffer->component_count; i++ )
            {
                struct StaticUIComponent* c = &game->ui_root_buffer->components[i];
                if( c->type != UIELEM_BUILTIN_WORLD )
                    continue;
                int wx = game->viewport_offset_x;
                int wy = game->viewport_offset_y;
                int ww = c->position.width > 0 ? c->position.width : 512;
                int wh = c->position.height > 0 ? c->position.height : 334;

                if( game->mouse.cursor_x >= wx && game->mouse.cursor_x < wx + ww &&
                    game->mouse.cursor_y >= wy && game->mouse.cursor_y < wy + wh )
                {
                    in_world = 1;
                    break;
                }
            }
        }

        if( in_world )
        {
            if( game->option_set.option_count <= 0 )
                return true;
            minimenu_game_world_ts_default_row(
                &game->option_set, game->mouse_tile_x, game->mouse_tile_z, &temp_option);
            first_opt = &temp_option;
        }
        else
        {
            if( !interface_hover_tooltip_line(
                    game, temp_option.text, (int)sizeof(temp_option.text)) )
                return true;
            first_opt = &temp_option;
        }

        if( !first_opt || !first_opt->text[0] )
            return true;

        if( in_world && game->option_set.option_count > 2 )
            snprintf(
                tooltip_buf,
                sizeof(tooltip_buf),
                "%s @whi@/ %d more options",
                first_opt->text,
                game->option_set.option_count - 2);
        else
            snprintf(tooltip_buf, sizeof(tooltip_buf), "%s", first_opt->text);
    }

    if( !tooltip_buf[0] )
        return true;

    /* Text pen: `node->position` from [layout] c=hover_tooltip x/y (Client.ts drawFeedback). */
    int font_id = node->u.hover_tooltip.font_id >= 0
                      ? node->u.hover_tooltip.font_id
                      : gamecache_get_font_ref_id(game->gamecache, "b12");
    if( font_id < 0 )
        font_id = gamecache_get_font_ref_id(game->gamecache, "p11");
    if( font_id < 0 )
        return true;
    struct DashPixFont* font = uiscene_font_get(game->ui_scene, font_id);
    if( !font )
        return true;

    /* Pool the text. */
    struct UITree* uit_tt = game->ui_root_buffer;
    if( !uit_tt )
        return true;
    size_t text_len = strlen(tooltip_buf);
    char* dst = uitree_textpool_push_dup(&uit_tt->text_pool, tooltip_buf, text_len);
    if( !dst )
        return true;

    /* Pen x/y from [layout]: pre-summed client-canvas coords (see rev UI INI comment next to
     * hover_tooltip). PixFont-style pen_y minus height2d for glyph base_y (TORIRS_GFX_DRAW_FONT).
     */
    int pen_x = node->position.x;
    int draw_y = node->position.y;
    if( font->height2d > 0 )
        draw_y -= font->height2d;

    frame_emit_pass(fiber, FRAME_PASS_2D);
    /* Client.ts drawStringAntiMacro(..., shadowed: true): black (+1,+1) mask per glyph, then
     * foreground with color tags. One TORIRS_GFX_DRAW_FONT + shadowed; do not use a second
     * ex_clipped pass here — tags would recolor the "shadow" (e.g. @whi@) and look like double
     * text. */
    struct ToriRSRenderCommand* cmd = LibToriRS_RenderCommandBufferEmplaceCommand(fiber->cmds);
    cmd->kind = TORIRS_GFX_DRAW_FONT;
    cmd->_font_draw.font_id = font_id;
    cmd->_font_draw.font = font;
    cmd->_font_draw.text = (const uint8_t*)dst;
    cmd->_font_draw.x = pen_x;
    cmd->_font_draw.y = draw_y;
    cmd->_font_draw.color_rgb = 0xFFFFFF;
    cmd->_font_draw.shadowed = 1;

    return true;
}

bool
uielem_minimenu_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    struct GGame* game = fiber->game;
    if( !game || !game->ui_scene || !fiber->cmds )
        return true;
    if( !game->minimenu.visible || game->minimenu.option_count <= 0 )
        return true;

    int ov_font_id = node->u.minimenu.font_id;
    struct DashPixFont* ov_font = NULL;
    if( ov_font_id >= 0 )
        ov_font = uiscene_font_get(game->ui_scene, ov_font_id);

    minimenu_game_enqueue(game, ov_font_id);

    frame_emit_pass(fiber, FRAME_PASS_2D);
    minimenu_game_translate_commands(game, fiber->cmds, ov_font_id, ov_font);
    return true;
}

bool
uielem_crosshair_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    struct GGame* game = fiber->game;
    if( !game || !game->ui_scene || !fiber->cmds )
        return true;
    if( game->cross_mode == 0 )
        return true;

    int frame_idx = game->cross_cycle / 100;
    if( game->cross_mode == 2 )
        frame_idx += 4;
    if( frame_idx > 7 )
        frame_idx = 7;

    int const cross_id = node->u.crosshair.scene_id;
    struct UISceneElement* cross_el = NULL;
    if( cross_id >= 0 && cross_id < game->ui_scene->elements_count )
        cross_el = &game->ui_scene->elements[cross_id];
    if( !cross_el || !cross_el->active || !cross_el->dash_sprites )
        return true;
    if( frame_idx < 0 || frame_idx >= cross_el->dash_sprites_count )
        return true;
    struct DashSprite* sp = cross_el->dash_sprites[frame_idx];
    if( !sp )
        return true;

    frame_emit_pass(fiber, FRAME_PASS_2D);
    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(fiber->cmds);
    c->kind = TORIRS_GFX_DRAW_SPRITE;
    c->_sprite_draw.element_id = cross_id;
    c->_sprite_draw.atlas_index = frame_idx;
    c->_sprite_draw.sprite = sp;
    c->_sprite_draw.dst_bb_x = game->cross_x - node->u.crosshair.hotspot_offset;
    c->_sprite_draw.dst_bb_y = game->cross_y - node->u.crosshair.hotspot_offset;
    c->_sprite_draw.dst_bb_w = 0;
    c->_sprite_draw.dst_bb_h = 0;
    c->_sprite_draw.rotated = false;
    c->_sprite_draw.rotation_r2pi2048 = 0;
    c->_sprite_draw.src_bb_x = 0;
    c->_sprite_draw.src_bb_y = 0;
    c->_sprite_draw.src_bb_w = 0;
    c->_sprite_draw.src_bb_h = 0;
    c->_sprite_draw.src_anchor_x = 0;
    c->_sprite_draw.src_anchor_y = 0;
    c->_sprite_draw.dst_anchor_x = 0;
    c->_sprite_draw.dst_anchor_y = 0;
    c->_sprite_draw.mask_element_id = -1;
    c->_sprite_draw.clip_w = 0;
    c->_sprite_draw.alpha_mode = 1;

    return true;
}

bool
uielem_chat_messages_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    struct GGame* game = fiber->game;
    if( !game || !game->chat || !fiber->cmds )
        return true;
    if( game->iface && game->iface->chat_interface_id >= 0 )
        return true;

    frame_emit_pass(fiber, FRAME_PASS_2D);
    chat_draw_messages(
        game->chat, game, fiber->cmds, node->u.chat_messages.font_id, -1, -1, -1, -1);
    return true;
}

bool
uielem_chat_input_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    struct GGame* game = fiber->game;
    if( !game || !game->chat || !fiber->cmds )
        return true;
    if( game->iface && game->iface->chat_interface_id >= 0 )
        return true;

    frame_emit_pass(fiber, FRAME_PASS_2D);
    chat_draw_input(game->chat, game, fiber->cmds, node->u.chat_input.font_id, -1, -1);
    return true;
}

bool
uielem_chat_privacy_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* node)
{
    struct GGame* game = fiber->game;
    if( !game || !game->chat || !fiber->cmds )
        return true;
    if( game->iface && game->iface->chat_interface_id >= 0 )
        return true;

    frame_emit_pass(fiber, FRAME_PASS_2D);
    chat_draw_privacy(game->chat, game, fiber->cmds, node->u.chat_privacy.font_id);
    return true;
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
    s_frame_project_models = project_models;

    while( true )
    {
        if( render_command_buffer && game->at_render_command_index <
                                         LibToriRS_RenderCommandBufferCount(render_command_buffer) )
        {
            cmd = LibToriRS_RenderCommandBufferAt(
                render_command_buffer, game->at_render_command_index);
            memcpy(command, cmd, sizeof(struct ToriRSRenderCommand));
            game->at_render_command_index++;
            return true;
        }

        if( game->uiscene_queued_commands &&
            game->uiscene_command_idx < game->uiscene_queued_commands->command_count )
        {
            cmd = LibToriRS_RenderCommandBufferAt(
                game->uiscene_queued_commands, game->uiscene_command_idx);
            memcpy(command, cmd, sizeof(struct ToriRSRenderCommand));
            game->uiscene_command_idx++;

            return true;
        }

        if( !game->ui_root_buffer || game->uitree_current < 0 )
        {
            if( game->uiscene_queued_commands && game->frame_pass != FRAME_PASS_NONE )
            {
                struct UIFrameState fiber = {
                    .game    = game,
                    .cmds    = game->uiscene_queued_commands,
                    .mouse_x = game->mouse.cursor_x,
                    .mouse_y = game->mouse.cursor_y,
                    .pass    = &game->frame_pass,
                };
                frame_emit_pass(&fiber, FRAME_PASS_NONE);
                continue;
            }
            return false;
        }

        if( !game->uiscene_queued_commands )
            return false;

        LibToriRS_RenderCommandBufferReset(game->uiscene_queued_commands);
        game->uiscene_command_idx = 0;

        int32_t cur = game->uitree_current;
        component = &game->ui_root_buffer->components[cur];

        struct UIFrameState fiber = {
            .game    = game,
            .cmds    = game->uiscene_queued_commands,
            .mouse_x = game->mouse.cursor_x,
            .mouse_y = game->mouse.cursor_y,
            .pass    = &game->frame_pass,
        };
        switch( component->type )
        {
        case UIELEM_BUILTIN_SPRITE:
            done = uielem_sprite_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_WORLD:
            done = uielem_world_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_MINIMAP:
            done = uielem_minimap_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_HOVER_TOOLTIP:
            done = uielem_hover_tooltip_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_CROSSHAIR:
            done = uielem_crosshair_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_MINIMENU:
            done = uielem_minimenu_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_CHAT_MESSAGES:
            done = uielem_chat_messages_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_CHAT_INPUT:
            done = uielem_chat_input_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_CHAT_PRIVACY:
            done = uielem_chat_privacy_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_COMPASS:
            done = uielem_compass_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_REDSTONE_TAB:
            done = uielem_redstone_tab_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_SIDEBAR:
            done = uielem_builtin_sidebar_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_CHAT_DIALOG:
            done = uielem_builtin_chat_dialog_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_SIDEBAR_OVERLAY:
            done = uielem_builtin_sidebar_overlay_step(&fiber, component);
            break;
        case UIELEM_RS_GRAPHIC:
            done = uielem_rs_graphic_step(&fiber, component, cur);
            break;
        case UIELEM_RS_TEXT:
            done = uielem_rs_text_step(&fiber, component);
            break;
        case UIELEM_RS_LAYER:
            done = uielem_rs_layer_step(&fiber, component);
            break;
        case UIELEM_RS_MODEL:
            done = uielem_rs_model_step(&fiber, component);
            break;
        case UIELEM_RS_INV:
            done = uielem_rs_inv_step(&fiber, component);
            break;
        case UIELEM_RS_RECT:
            done = uielem_rs_rect_step(&fiber, component);
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

static struct StaticUIComponent*
frame_find_builtin(
    struct GGame* game,
    enum StaticUIComponentType ty)
{
    if( !game->ui_root_buffer )
        return NULL;
    for( uint32_t i = 0; i < game->ui_root_buffer->component_count; i++ )
    {
        if( game->ui_root_buffer->components[i].type == ty )
            return &game->ui_root_buffer->components[i];
    }
    return NULL;
}

static bool
frame_point_in_component_xy(
    struct StaticUIComponent* c,
    int px,
    int py)
{
    if( !c || c->position.kind != UIPOS_XY )
        return false;
    int x = c->position.x;
    int y = c->position.y;
    int w = c->position.width;
    int h = c->position.height;
    if( w <= 0 || h <= 0 )
        return false;
    return px >= x && px < x + w && py >= y && py < y + h;
}

/** Client.ts tabLoop(): bottom sidebar icon row (sideOverlayId[7..13]) before chatModeLoop. */
static bool
frame_try_ts_bottom_sidebar_tabs(
    struct GGame* game,
    int mx,
    int my)
{
    struct InterfaceState* iface = game->iface;
    if( !iface )
        return false;

#define TAB_BOTTOM_HIT(x0, x1, y0, y1, tab_idx)                                                    \
    if( (mx) >= (x0) && (mx) <= (x1) && (my) >= (y0) && (my) < (y1) &&                             \
        iface->tab_interface_id[(tab_idx)] >= 0 )                                                  \
    {                                                                                              \
        iface->selected_tab = (tab_idx);                                                           \
        return true;                                                                               \
    }

    TAB_BOTTOM_HIT(540, 574, 466, 502, 7);
    TAB_BOTTOM_HIT(572, 602, 466, 503, 8);
    TAB_BOTTOM_HIT(599, 629, 466, 503, 9);
    TAB_BOTTOM_HIT(627, 671, 467, 502, 10);
    TAB_BOTTOM_HIT(669, 699, 466, 503, 11);
    TAB_BOTTOM_HIT(696, 726, 466, 503, 12);
    TAB_BOTTOM_HIT(724, 758, 466, 502, 13);
#undef TAB_BOTTOM_HIT

    return false;
}

/**
 * Game-coord anchor for the walk/interact crosshair: same rule as world 3D pick
 * (see frame_world_pick_pointer_xy).
 */
static void
frame_cross_xy_from_cursor(
    struct GGame* game,
    int* out_x,
    int* out_y)
{
    frame_world_pick_pointer_xy(game, out_x, out_y);
}

/** Set the click crosshair and start the animation (coords match world tile pick). */
static void
game_cross_set(struct GGame* game, int mode)
{
    frame_cross_xy_from_cursor(game, &game->cross_x, &game->cross_y);
    game->cross_mode  = mode;
    game->cross_cycle = 0;
}

static void
frame_handle_interface_and_world_clicks(struct GGame* game)
{
    if( !game->mouse.buttons[TORIRSM_LEFT].click_this_frame || !game->iface )
        return;

    int const cx = game->mouse.buttons[TORIRSM_LEFT].click_press_x;
    int const cy = game->mouse.buttons[TORIRSM_LEFT].click_press_y;

    if( game->ui_root_buffer && game->iface->sidebar_interface_id == -1 )
    {
        for( uint32_t i = 0; i < game->ui_root_buffer->component_count; i++ )
        {
            struct StaticUIComponent* c = &game->ui_root_buffer->components[i];
            if( c->type != UIELEM_BUILTIN_REDSTONE_TAB )
                continue;
            if( !frame_point_in_component_xy(c, cx, cy) )
                continue;
            game->iface->selected_tab = c->u.redstone_tab.tabno;
            if( c->u.redstone_tab.tabno == game->magic_tab_spellbook_sidebar_tabno )
            {
                interface_magic_tab_request_inv_transmit_if_configured(game);
            }
            game->interface_consumed_click = 1;
            return;
        }
    }

    /* Snapshot from LibToriRS_GameProcessInput (minimenu/scroll/etc.); clear the live field so
     * FrameEnd dispatch below only sees flags set by this function, not stale input state. */
    int const consumed_from_input = game->interface_consumed_click;
    game->interface_consumed_click = 0;

    if( !consumed_from_input &&
        frame_try_ts_bottom_sidebar_tabs(game, cx, cy) )
    {
        game->interface_consumed_click = 1;
        return;
    }

    if( !consumed_from_input && game->chat &&
        chat_handle_privacy_strip_click(game->chat, game, cx, cy, 1) )
    {
        game->interface_consumed_click = 1;
        return;
    }

    if( !consumed_from_input && game->chat &&
        chat_try_begin_typing_click(game->chat, game, cx, cy) )
    {
        game->interface_consumed_click = 1;
        return;
    }

    if( consumed_from_input )
    {
        /* Suppress duplicate tile_click / MOVE_GAMECLICK, but still move the walk cross to this
         * left press when it lands in world or minimap aim — otherwise the sprite stays at the
         * last minimenu anchor (often right-click). Skip when a menu row actually ran use_option
         * (that path sets cross_x/y intentionally, e.g. right-press anchor). */
        if( !game->minimenu_used_option_this_left_click )
        {
            struct StaticUIComponent* mm = frame_find_builtin(game, UIELEM_BUILTIN_MINIMAP);
            if( mm && frame_point_in_component_xy(mm, cx, cy) )
            {
                int bx = mm->position.x;
                int by = mm->position.y;
                int const pivot_x = bx + mm->position.anchor_x;
                int const pivot_y = by + mm->position.anchor_y;
                int const dpx     = cx - pivot_x;
                int const dpy     = cy - pivot_y;
                int const max_off_x = mm->position.width / 2 + 32;
                int const max_off_y = mm->position.height / 2 + 32;
                if( dpx >= -max_off_x && dpx <= max_off_x && dpy >= -max_off_y && dpy <= max_off_y )
                    game_cross_set(game, 1);
            }
            struct StaticUIComponent* wv = frame_find_builtin(game, UIELEM_BUILTIN_WORLD);
            if( wv && frame_point_in_component_xy(wv, cx, cy) )
                game_cross_set(game, 1);
        }
        return;
    }

    /* UI button click dispatch (sidebar / viewport / chat).
     * Mirror Client.ts mouseLoop → doAction → IF_BUTTON path.
     * For TOGGLE/SELECT buttons, apply varp/varbit optimistic update so the UI
     * reflects the change immediately even though we don't send a real packet. */
    if( game->buildcachedat && game->iface )
    {
        /* Sidebar region */
        if( iface_point_in_sidebar_overlay(cx, cy) )
        {
            int root_comp_id = -1;
            int root_x = IFACE_SIDEBAR_X0, root_y = IFACE_SIDEBAR_Y0;

            if( game->iface->sidebar_interface_id >= 0 )
            {
                root_comp_id = game->iface->sidebar_interface_id;
            }
            else if( game->ui_root_buffer )
            {
                for( uint32_t i = 0; i < game->ui_root_buffer->component_count; i++ )
                {
                    struct StaticUIComponent* sb = &game->ui_root_buffer->components[i];
                    if( sb->type != UIELEM_BUILTIN_SIDEBAR ||
                        sb->u.sidebar.tabno != game->iface->selected_tab )
                        continue;
                    int compno = sb->u.sidebar.componentno;
                    if( compno < 0 )
                        continue;
                    struct GameCacheComponent* sroot =
                        gamecache_get_component(game->gamecache, compno);
                    if( !sroot )
                        continue;
                    root_comp_id = compno;
                    root_x = sb->position.x + sroot->x;
                    root_y = sb->position.y + sroot->y;
                    break;
                }
            }

            if( root_comp_id >= 0 )
            {
                struct GameCacheComponent* root =
                    gamecache_get_component(game->gamecache, root_comp_id);
                int comp_id = -1, client_code = 0, btn_action = 0;
                int pa = 0, pb = 0, pc = 0;
                if( root && interface_find_button_click_at(
                                game, root, root_x, root_y, cx, cy,
                                &comp_id, &client_code, &btn_action, &pa, &pb, &pc) )
                {
                    interface_apply_button_click_varp_optimistic(game, comp_id);
                    interface_dispatch_button_action(game, comp_id, btn_action);
                    game->interface_consumed_click = 1;
                }
            }
        }

        /* Viewport region */
        if( !game->interface_consumed_click && cx > 4 && cy > 4 && cx < 516 && cy < 338 &&
            game->iface->viewport_interface_id >= 0 )
        {
            struct GameCacheComponent* root =
                gamecache_get_component(game->gamecache, game->iface->viewport_interface_id);
            int comp_id = -1, client_code = 0, btn_action = 0;
            int pa = 0, pb = 0, pc = 0;
            if( root &&
                interface_find_button_click_at(
                    game, root, 4, 4, cx, cy, &comp_id, &client_code, &btn_action, &pa, &pb, &pc) )
            {
                interface_apply_button_click_varp_optimistic(game, comp_id);
                interface_dispatch_button_action(game, comp_id, btn_action);
                game->interface_consumed_click = 1;
            }
        }

        /* Chat region (IF_OPENCHAT): Client.ts drawInterface @ (0,0) in chat pixmap -> screen (17,357). */
        if( !game->interface_consumed_click && cx > 17 && cy > 357 && cx < 426 && cy < 453 &&
            game->iface->chat_interface_id >= 0 )
        {
            struct GameCacheComponent* root =
                gamecache_get_component(game->gamecache, game->iface->chat_interface_id);
            int comp_id = -1, client_code = 0, btn_action = 0;
            int pa = 0, pb = 0, pc = 0;
            if( root &&
                interface_find_button_click_at(
                    game, root, 17, 357, cx, cy, &comp_id, &client_code, &btn_action, &pa, &pb, &pc) )
            {
                interface_apply_button_click_varp_optimistic(game, comp_id);
                interface_dispatch_button_action(game, comp_id, btn_action);
                game->interface_consumed_click = 1;
            }
        }
    }

    if( game->interface_consumed_click )
        return;

    {
        struct StaticUIComponent* mm = frame_find_builtin(game, UIELEM_BUILTIN_MINIMAP);
        if( mm && frame_point_in_component_xy(mm, cx, cy) )
        {
            int bx = mm->position.x;
            int by = mm->position.y;
            int const pivot_x = bx + mm->position.anchor_x;
            int const pivot_y = by + mm->position.anchor_y;
            int const dpx     = cx - pivot_x;
            int const dpy     = cy - pivot_y;
            int const max_off_x = mm->position.width / 2 + 32;
            int const max_off_y = mm->position.height / 2 + 32;
            if( dpx >= -max_off_x && dpx <= max_off_x && dpy >= -max_off_y && dpy <= max_off_y )
            {
                int yaw = game->camera_yaw & 0x7ff;
                int64_t const cos_yaw = dash_cos(yaw);
                int64_t const sin_yaw = dash_sin(yaw);
                int64_t const S = 65536;
                int64_t const num_wx =
                    ((int64_t)dpx << 21) * cos_yaw + ((int64_t)dpy << 21) * sin_yaw;
                int64_t const num_wz =
                    ((int64_t)dpx << 21) * sin_yaw - ((int64_t)dpy << 21) * cos_yaw;
                int64_t const dwx = num_wx / (S * S);
                int64_t const dwz = num_wz / (S * S);
                int pw = game->camera_world_x;
                int pz = game->camera_world_z;
                if( game->world )
                {
                    struct PlayerEntity* pl = world_player(game->world, ACTIVE_PLAYER_SLOT);
                    if( pl && pl->alive )
                    {
                        pw = pl->draw_position.x;
                        pz = pl->draw_position.z;
                    }
                }
                int tile_x = (int)((int64_t)pw + dwx) >> 7;
                int tile_z = (int)((int64_t)pz - dwz) >> 7;
                int lvl = (game->mouse_tile_level >= 0) ? game->mouse_tile_level : 0;
                game->tile_clicked_x     = tile_x;
                game->tile_clicked_z     = tile_z;
                game->tile_clicked_level = lvl;
                /* Set minimap flag for the destination marker. */
                game->minimap_flag_x   = tile_x;
                game->minimap_flag_z   = tile_z;
                game->minimap_flag_has = 1;
                game_cross_set(game, 1);
                game->interface_consumed_click = 1;
            }
            return;
        }
    }

    {
        struct StaticUIComponent* wv = frame_find_builtin(game, UIELEM_BUILTIN_WORLD);
        if( !wv || !frame_point_in_component_xy(wv, cx, cy) )
            return;

        /* Client.ts mouseLoop (~8607): menu closed + menuNumEntries>0 → doAction(menuNumEntries-1).
         * Default row = menuOption[last] after buildMinimenu bubble-sort (same as
         * minimenu_game_world_ts_default_row → tmp[last] after minimenu_sort_ts_action_order).
         * doAction strips priority (+2000) once before comparing opcodes (8796–8798); use
         * minimenu_action_norm_dispatch for WALK vs interact — priority NPC rows must dispatch. */
        if( game->option_set.option_count > 0 && game->torirs_step_input )
        {
            struct WorldOption def;
            minimenu_game_world_ts_default_row(
                &game->option_set, game->mouse_tile_x, game->mouse_tile_z, &def);
            int const dispatch_norm = minimenu_action_norm_dispatch((int)def.action);
            if( dispatch_norm != (int)MINIMENU_ACTION_WALK )
            {
                game->minimenu.option_action[0]  = (int)def.action;
                game->minimenu.option_param_a[0] = def.param_a;
                game->minimenu.option_param_b[0] = def.param_b;
                game->minimenu.option_param_c[0] = def.param_c;
                game->minimenu.option_count      = 1;
                minimenu_game_use_option(game, game->torirs_step_input, 0);
                game->minimenu_used_option_this_left_click = true;
                /* Examine (OP_NPC6 / OP_OBJ6): doAction does not set crossMode 2; Client keeps yellow
                 * from World.groundX tryMove — yellow cross here. */
                if( dispatch_norm == (int)MINIMENU_ACTION_OPNPC6 ||
                    dispatch_norm == (int)MINIMENU_ACTION_OPOBJ6 )
                    game_cross_set(game, 1);
                return;
            }
        }

        if( game->mouse_tile_x < 0 )
            return;

        game_cross_set(game, 1);

        game->tile_clicked_x     = game->mouse_tile_x;
        game->tile_clicked_z     = game->mouse_tile_z;
        game->tile_clicked_level = game->mouse_tile_level;
        game->minimap_flag_has   = 0; /* clear destination flag on direct world click */
    }
}

void
LibToriRS_FrameEnd(struct GGame* game)
{
    game->frame_pass = FRAME_PASS_NONE;
    /* World spatial pick → WorldOptionSet only (Client.ts addWorldOptions). Right-click inventory
     * lines live in UITree (uitree_sync_hover_option_set); minimenu_game_show merges UITree vs this
     * set — see minimenu_game_show. */
    if( game->world )
    {
        game->option_set.option_count = 0;
        world_options_add_pickset_options(game, game->world, &game->pickset, &game->option_set);
    }

    frame_handle_interface_and_world_clicks(game);

    /* Click consumption is only meaningful between GameProcessInput and this FrameEnd pass.
     * Clear after dispatch so the flag never leaks into later work in the same frame or any
     * code path that might render without a fresh LibToriRS_GameProcessInput. */
    game->interface_consumed_click = 0;

    /* Advance the LRU frame counter for obj icon sprites (mirrors ObjType.spriteCache).
     * Called once at the end of every frame so cache_lookup can stamp last_used correctly. */
    obj_icon_cache_tick();

    /* Reset scroll_cycle at end of frame to mirror Client.ts maindraw (10524). */
    game->scroll_cycle = 0;
}

#endif