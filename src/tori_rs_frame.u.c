#ifndef TORI_RS_FRAME_U_C
#define TORI_RS_FRAME_U_C

#include "graphics/dash.h"
#include "osrs/game.h"
#include "osrs/interface.h"
#include "osrs/interface_state.h"
#include "osrs/minimap.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/rs_component_gfx.h"
#include "osrs/buildcachedat.h"
#include "osrs/core/clientprot_core.h"
#include "osrs/minimenu.h"
#include "osrs/obj_icon.h"
#include "osrs/rs_component_state.h"
#include "osrs/scene2.h"
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

/** Set per `LibToriRS_FrameNextCommand` call for RS model culling. */
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
        return;
    switch( *fiber->pass )
    {
    case FRAME_PASS_2D:
        emit_marker(fiber->cmds, TORIRS_GFX_STATE_END_2D);
        break;
    default:
        break;
    }
    emit_begin_3d_with_rect(fiber->cmds, dst_x, dst_y, dst_w, dst_h);
    *fiber->pass = FRAME_PASS_3D;
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

static int
uiscene_find_element_id_by_name(struct UIScene* uiscene, char const* name)
{
    if( !uiscene || !name )
        return -1;
    for( int i = 0; i < uiscene->elements_count; i++ )
    {
        if( !uiscene->elements[i].active )
            continue;
        if( strcmp(uiscene->elements[i].name, name) == 0 )
            return i;
    }
    return -1;
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
    if( !game || !game->ui_scene || !fiber->cmds )
        return;
    int lh = layer_h;
    int sh = scroll_height;
    if( sh <= lh || lh < 32 )
        return;
    int track_h = lh - 32;
    if( track_h <= 0 )
        return;
    int grip_size = (track_h * lh) / sh;
    if( grip_size < 8 )
        grip_size = 8;
    if( grip_size > track_h )
        grip_size = track_h;
    int range = sh - lh;
    if( range < 0 )
        range = 0;
    int grip_y = range > 0 ? ((track_h - grip_size) * scroll_pos) / range : 0;
    if( grip_y < 0 )
        grip_y = 0;
    if( grip_y > track_h - grip_size )
        grip_y = track_h - grip_size;

    int sx = layer_x + layer_w;
    int y0 = layer_y;
    int h = layer_h;
    struct DashSprite* sb0 = uiscene_sprite_by_name(game->ui_scene, "scrollbar0", 0);
    struct DashSprite* sb1 = uiscene_sprite_by_name(game->ui_scene, "scrollbar1", 0);
    int e0 = uiscene_find_element_id_by_name(game->ui_scene, "scrollbar0");
    int e1 = uiscene_find_element_id_by_name(game->ui_scene, "scrollbar1");
    frame_emit_pass(fiber, FRAME_PASS_2D);
    if( sb0 && e0 >= 0 )
        emit_ui_sprite_raw(fiber->cmds, e0, 0, sb0, sx, y0);
    if( sb1 && e1 >= 0 )
        emit_ui_sprite_raw(fiber->cmds, e1, 0, sb1, sx, y0 + h - 16);
    /* Client.ts drawScrollbar / SCROLLBAR_* */
    emit_ui_fill_rect(fiber->cmds, sx, y0 + 16, 16, h - 32, 0x23201b, 0, 1u);
    {
        int gt = y0 + 16 + grip_y;
        struct ToriRSRenderCommandBuffer* buf = fiber->cmds;
        emit_ui_fill_rect(buf, sx, gt, 16, grip_size, 0x4d4233, 0, 1u);
        emit_ui_fill_rect(buf, sx, gt, 1, grip_size, 0x766654, 0, 1u);
        emit_ui_fill_rect(buf, sx + 1, gt, 1, grip_size, 0x766654, 0, 1u);
        emit_ui_fill_rect(buf, sx, gt, 16, 1, 0x766654, 0, 1u);
        emit_ui_fill_rect(buf, sx, gt + 1, 16, 1, 0x766654, 0, 1u);
        emit_ui_fill_rect(buf, sx + 15, gt, 1, grip_size, 0x332d25, 0, 1u);
        if( grip_size > 1 )
            emit_ui_fill_rect(buf, sx + 14, gt + 1, 1, grip_size - 1, 0x332d25, 0, 1u);
        emit_ui_fill_rect(buf, sx, gt + grip_size - 1, 16, 1, 0x332d25, 0, 1u);
        if( grip_size > 1 )
            emit_ui_fill_rect(buf, sx + 1, gt + grip_size - 2, 15, 1, 0x332d25, 0, 1u);
    }
}

static void
rs_ui_layer_push(
    struct UIFrameState* fiber,
    struct StaticUIComponent* layer)
{
    struct GGame* game = fiber->game;
    if( !game || !fiber->cmds || game->ui_layer_stack_top + 1 >= UIFRAME_LAYER_STACK_MAX )
        return;
    int lh = layer->position.height;
    int sh = layer->u.rs_layer.scroll_height;
    int sp = rs_iface_scroll_clamped_for_layer(game, layer->component_id, sh, lh);
    int prev =
        game->ui_layer_stack_top >= 0 ? game->ui_layer_stack[game->ui_layer_stack_top].scroll_y_total
                                      : 0;
    struct UILayerFrameEntry* e = &game->ui_layer_stack[++game->ui_layer_stack_top];
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
    if( !game || game->ui_layer_stack_top < 0 )
        return;
    struct UILayerFrameEntry* e = &game->ui_layer_stack[game->ui_layer_stack_top];
    /* Pop clip before scrollbar: bar is at x+width (outside content clip). Client.ts drawScrollbar. */
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
            .game = game,
            .cmds = game->uiscene_queued_commands,
            .mouse_x = game->mouse_x,
            .mouse_y = game->mouse_y,
            .pass = &game->frame_pass,
        };
        int sp = rs_iface_scroll_clamped_for_layer(
            game,
            e->scrollbar_layer_component_id,
            e->scrollbar_scroll_height,
            e->clip_h);
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
    game->ui_layer_stack_top--;
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
        if( c->u.rs_layer.hide &&
            !interface_component_is_overlay_hovered(game, c->component_id) )
            return false;
    }
    else if( c->is_hidden )
        return false;
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

/* Emit a single 2-pixel-wide dot on the minimap for the entity at world position
 * (entity_world_x, entity_world_z).  Mirrors Client.ts minimapDrawDot 11637-11663.
 *
 * comp_x, comp_y: top-left corner of the minimap element in UI viewport space.
 * camera_yaw:  current camera yaw (0–2047 units, full circle).
 * pl_wx, pl_wz: local player world position in sub-tile units (128 per tile). */
static void
minimap_emit_dot(
    struct GGame*                    game,
    struct ToriRSRenderCommandBuffer* buf,
    struct ToriRSRenderCommand*       base_cmd, /* copy element_id from this */
    struct DashSprite*                sprite,
    int comp_x,
    int comp_y,
    int comp_w,
    int comp_h,
    int camera_yaw,
    int pl_wx,
    int pl_wz,
    int entity_wx,
    int entity_wz)
{
    if( !sprite || !buf )
        return;

    int dx = entity_wx - pl_wx;
    int dz = entity_wz - pl_wz;

    /* Distance cull: > 6400 world-units (~50 tiles) away. */
    if( (int64_t)dx * dx + (int64_t)dz * dz >= (int64_t)6400 * 6400 )
        return;

    int yaw     = camera_yaw & 0x7ff;
    /* dash_sin/cos return 65536-scale fixed-point values (like TS Client.SINE * 65536). */
    int sin_y   = dash_sin(yaw);
    int cos_y   = dash_cos(yaw);

    /* TS: dotx = 97 + ((dx * cos + dz * sin) >> 16)  with cos scaled by 65536*(256/(zoom+256)).
     * With default zoom=0 (C constant 256→256/(0+256)=1.0) we get:
     *   dot_x = center_x + ((dx * cos_y + dz * sin_y) >> 11)
     * where >>11 matches the click-handler's inverse formula. */
    int dot_x = 97 + comp_x + ((dx * cos_y + dz * sin_y) >> 11);
    int dot_y = 78 + comp_y + ((dz * cos_y - dx * sin_y) >> 11);

    /* Clamp to minimap bounds. */
    int half_w = sprite->width  / 2;
    int half_h = sprite->height / 2;
    if( dot_x < comp_x + half_w || dot_x >= comp_x + comp_w - half_w )
        return;
    if( dot_y < comp_y + half_h || dot_y >= comp_y + comp_h - half_h )
        return;

    struct ToriRSRenderCommand* c = LibToriRS_RenderCommandBufferEmplaceCommand(buf);
    c->kind                          = TORIRS_GFX_DRAW_SPRITE;
    c->_sprite_draw.element_id       = base_cmd ? base_cmd->_sprite_draw.element_id : -1;
    c->_sprite_draw.atlas_index      = 200; /* unique atlas slot for dots */
    c->_sprite_draw.sprite           = sprite;
    c->_sprite_draw.dst_bb_x         = dot_x - half_w;
    c->_sprite_draw.dst_bb_y         = dot_y - half_h;
    c->_sprite_draw.dst_bb_w         = sprite->width;
    c->_sprite_draw.dst_bb_h         = sprite->height;
    c->_sprite_draw.rotated          = false;
    c->_sprite_draw.rotation_r2pi2048 = 0;
    c->_sprite_draw.src_bb_x         = 0;
    c->_sprite_draw.src_bb_y         = 0;
    c->_sprite_draw.src_bb_w         = 0;
    c->_sprite_draw.src_bb_h         = 0;
    c->_sprite_draw.src_anchor_x     = 0;
    c->_sprite_draw.src_anchor_y     = 0;
    c->_sprite_draw.dst_anchor_x     = 0;
    c->_sprite_draw.dst_anchor_y     = 0;
    c->_sprite_draw.mask_element_id  = -1;
    c->_sprite_draw.clip_w           = 0;
    c->_sprite_draw.alpha_mode       = 1;
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

    struct MinimapRenderCommandBuffer* dyn = game->minimap_dynamic_commands;
    if( !dyn )
    {
        return;
    }
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
        struct ToriRSRenderCommand* dotc =
            LibToriRS_RenderCommandBufferEmplaceCommand(game->uiscene_queued_commands);
        dotc->kind = TORIRS_GFX_DRAW_SPRITE;
        dotc->_sprite_draw.element_id = component->u.minimap.scene_id;
        dotc->_sprite_draw.atlas_index = 100 + j;
        dotc->_sprite_draw.sprite = dot;
        dotc->_sprite_draw.dst_bb_x = dot_x;
        dotc->_sprite_draw.dst_bb_y = dot_y;
        dotc->_sprite_draw.dst_bb_w = dot->width;
        dotc->_sprite_draw.dst_bb_h = dot->height;
        dotc->_sprite_draw.rotation_r2pi2048 = 0;
        dotc->_sprite_draw.src_bb_x = 0;
        dotc->_sprite_draw.src_bb_y = 0;
        dotc->_sprite_draw.src_bb_w = 0;
        dotc->_sprite_draw.src_bb_h = 0;
    }

    /* ── Entity minimap dots (NPCs, players, ground items). ─────────────────
     * Mirrors Client.ts minimapDraw 11529-11603 via the minimapDrawDot helper. */
    if( !game->world )
        goto mm_done;

    int mm_comp_x  = component->position.x;
    int mm_comp_y  = component->position.y;
    int mm_comp_w  = component->position.width;
    int mm_comp_h  = component->position.height;
    int mm_cam_yaw = game->camera_yaw;

    /* Local player world position. */
    int pl_wx = game->camera_world_x;
    int pl_wz = game->camera_world_z;
    {
        struct PlayerEntity* lp = world_player(game->world, ACTIVE_PLAYER_SLOT);
        if( lp && lp->alive )
        {
            pl_wx = lp->draw_position.x;
            pl_wz = lp->draw_position.z;
        }
    }

    /* Ground items (mapdots0 = yellow dot, index 0 in TS). */
    {
        struct DashSprite* dot_obj =
            game->ui_scene ? uiscene_sprite_by_name(game->ui_scene, "mapdots0", 0) : NULL;
        if( dot_obj && game->obj_stacks )
        {
            int level = (game->camera_world_y < 0) ? 0 : 0; /* always level 0 for now */
            if( game->obj_stacks[level] )
            {
                for( int sx = 0; sx < ZONE_SCENE_SIZE; sx++ )
                {
                    for( int sz = 0; sz < ZONE_SCENE_SIZE; sz++ )
                    {
                        struct ObjStackEntry* entry =
                            game->obj_stacks[level][sx * ZONE_SCENE_SIZE + sz];
                        if( !entry )
                            continue;
                        int wx = sx * 128 + 64;
                        int wz = sz * 128 + 64;
                        minimap_emit_dot(
                            game,
                            game->uiscene_queued_commands,
                            NULL,
                            dot_obj,
                            mm_comp_x, mm_comp_y, mm_comp_w, mm_comp_h,
                            mm_cam_yaw, pl_wx, pl_wz, wx, wz);
                    }
                }
            }
        }
    }

    /* NPCs (mapdots1). */
    {
        struct DashSprite* dot_npc =
            game->ui_scene ? uiscene_sprite_by_name(game->ui_scene, "mapdots1", 0) : NULL;
        if( dot_npc )
        {
            for( int i = 0; i < game->world->active_npc_count; i++ )
            {
                int npc_id = game->world->active_npcs[i];
                struct NPCEntity* npc = world_npc(game->world, npc_id);
                if( !npc || !npc->alive )
                    continue;
                minimap_emit_dot(
                    game,
                    game->uiscene_queued_commands,
                    NULL,
                    dot_npc,
                    mm_comp_x, mm_comp_y, mm_comp_w, mm_comp_h,
                    mm_cam_yaw, pl_wx, pl_wz,
                    npc->draw_position.x, npc->draw_position.z);
            }
        }
    }

    /* Other players (mapdots2). */
    {
        struct DashSprite* dot_player =
            game->ui_scene ? uiscene_sprite_by_name(game->ui_scene, "mapdots2", 0) : NULL;
        if( dot_player )
        {
            for( int i = 0; i < game->world->active_player_count; i++ )
            {
                int pid = game->world->active_players[i];
                if( pid == ACTIVE_PLAYER_SLOT )
                    continue;
                struct PlayerEntity* p = world_player(game->world, pid);
                if( !p || !p->alive )
                    continue;
                minimap_emit_dot(
                    game,
                    game->uiscene_queued_commands,
                    NULL,
                    dot_player,
                    mm_comp_x, mm_comp_y, mm_comp_w, mm_comp_h,
                    mm_cam_yaw, pl_wx, pl_wz,
                    p->draw_position.x, p->draw_position.z);
            }
        }
    }

    /* Local player: white 3x3 rect at minimap center (97,78). */
    {
        struct ToriRSRenderCommand* lp_dot =
            LibToriRS_RenderCommandBufferEmplaceCommand(game->uiscene_queued_commands);
        lp_dot->kind          = TORIRS_GFX_DRAW_RECT;
        lp_dot->_rect_draw.x  = mm_comp_x + 97 - 1;
        lp_dot->_rect_draw.y  = mm_comp_y + 78 - 1;
        lp_dot->_rect_draw.w  = 3;
        lp_dot->_rect_draw.h  = 3;
        lp_dot->_rect_draw.color_rgb = 0xFFFFFF;
        lp_dot->_rect_draw.alpha     = 0;
        lp_dot->_rect_draw.fill      = 1;
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

void
LibToriRS_FrameBegin(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    game->at_painters_command_index = 0;
    game->at_render_command_index = 0;
    game->at_ui_render_command_index = 0;

    game->interface_consumed_click = 0;

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

    game->camera->pitch = game->camera_pitch;
    game->camera->yaw = game->camera_yaw;
    game->camera->roll = game->camera_roll;

    game->uitree_stack_top = -1;
    game->uitree_current = (game->ui_root_buffer && game->ui_root_buffer->root_index >= 0)
                               ? game->ui_root_buffer->root_index
                               : -1;
    game->ui_layer_stack_top = -1;
    game->ui_frame_text_pool_used = 0;
    game->uiscene_command_idx = 0;
    if( game->uiscene_queued_commands )
        LibToriRS_RenderCommandBufferReset(game->uiscene_queued_commands);

    interface_update_region_hover_ids(game);

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

    /* Overlay: click-cross sprite (yellow = walk, red = interact).
     * Emitted as a TORIRS_GFX_DRAW_SPRITE command so all renderers handle it uniformly.
     * Advance cross_cycle here (it was updated at the top of FrameBegin only for mode != 0). */
    if( game->cross_mode != 0 && game->ui_scene )
    {
        int frame_idx = game->cross_cycle / 100;
        if( game->cross_mode == 2 )
            frame_idx += 4;
        if( frame_idx > 7 )
            frame_idx = 7;
        struct DashSprite* sp = uiscene_sprite_by_name(game->ui_scene, "cross", frame_idx);
        if( sp )
        {
            struct ToriRSRenderCommand* c =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            c->kind                       = TORIRS_GFX_DRAW_SPRITE;
            c->_sprite_draw.element_id    = -1;
            c->_sprite_draw.atlas_index   = 0;
            c->_sprite_draw.sprite        = sp;
            c->_sprite_draw.dst_bb_x      = game->cross_x - 8 - game->viewport_offset_x;
            c->_sprite_draw.dst_bb_y      = game->cross_y - 8 - game->viewport_offset_y;
            c->_sprite_draw.dst_bb_w      = 0;
            c->_sprite_draw.dst_bb_h      = 0;
            c->_sprite_draw.rotated       = false;
            c->_sprite_draw.rotation_r2pi2048 = 0;
            c->_sprite_draw.src_bb_x      = 0;
            c->_sprite_draw.src_bb_y      = 0;
            c->_sprite_draw.src_bb_w      = 0;
            c->_sprite_draw.src_bb_h      = 0;
            c->_sprite_draw.src_anchor_x  = 0;
            c->_sprite_draw.src_anchor_y  = 0;
            c->_sprite_draw.dst_anchor_x  = 0;
            c->_sprite_draw.dst_anchor_y  = 0;
            c->_sprite_draw.mask_element_id = -1;
            c->_sprite_draw.clip_w        = 0;
            c->_sprite_draw.alpha_mode    = 1; /* alpha-blend so transparent pixels are correct */
        }
    }

    /* ── Resolve overlay font (b12; fallback p11). ───────────────────────── */
    struct DashPixFont* ov_font   = NULL;
    int                 ov_font_id = -1;
    if( game->ui_scene && game->buildcachedat )
    {
        ov_font_id = buildcachedat_get_font_ref_id(game->buildcachedat, "b12");
        if( ov_font_id < 0 )
            ov_font_id = buildcachedat_get_font_ref_id(game->buildcachedat, "p11");
        if( ov_font_id >= 0 )
            ov_font = uiscene_font_get(game->ui_scene, ov_font_id);
    }

    /* ── Overlay: hover feedback line (Client.ts drawFeedback). ─────────────
     * Emitted when the minimenu is NOT open and option_set has at least one option.
     * Shows the last (lowest-priority) option text, e.g. "Walk here" or
     * "Examine Oak tree".  Suffix " / N more options" when N > 1 extra.     */
    if( !game->minimenu_visible && game->option_set.option_count > 0 && ov_font )
    {
        /* Use a stack buffer so we can append the suffix without heap alloc. */
        static char fb_text[128];
        int n = game->option_set.option_count;
        /* Sorted last entry = lowest-priority (Walk here or last examine). */
        struct WorldOption* last_opt = world_option_set_get_option(&game->option_set, n - 1);
        if( n > 2 )
            snprintf(fb_text, sizeof(fb_text), "%s @whi@/ %d more options", last_opt->text, n - 1);
        else
            snprintf(fb_text, sizeof(fb_text), "%s", last_opt->text);

        /* Pool the text so the pointer stays valid for the whole frame. */
        size_t fb_need = strlen(fb_text) + 1;
        const uint8_t* fb_ptr = NULL;
        if( game->ui_frame_text_pool_used + fb_need <= game->ui_frame_text_pool_cap )
        {
            char* dst = game->ui_frame_text_pool + game->ui_frame_text_pool_used;
            memcpy(dst, fb_text, fb_need);
            game->ui_frame_text_pool_used += fb_need;
            fb_ptr = (const uint8_t*)dst;
        }
        else
        {
            /* Grow the pool. */
            size_t nc = game->ui_frame_text_pool_cap ? game->ui_frame_text_pool_cap * 2 : 4096u;
            while( game->ui_frame_text_pool_used + fb_need > nc )
                nc *= 2;
            char* nb = (char*)realloc(game->ui_frame_text_pool, nc);
            if( nb )
            {
                game->ui_frame_text_pool     = nb;
                game->ui_frame_text_pool_cap = nc;
                char* dst = nb + game->ui_frame_text_pool_used;
                memcpy(dst, fb_text, fb_need);
                game->ui_frame_text_pool_used += fb_need;
                fb_ptr = (const uint8_t*)dst;
            }
        }

        if( fb_ptr )
        {
            struct ToriRSRenderCommand* fc =
                LibToriRS_RenderCommandBufferEmplaceCommand(render_command_buffer);
            fc->kind              = TORIRS_GFX_DRAW_FONT;
            fc->_font_draw.font_id  = ov_font_id;
            fc->_font_draw.font     = ov_font;
            fc->_font_draw.text     = fb_ptr;
            fc->_font_draw.x        = game->viewport_offset_x + 4;
            fc->_font_draw.y        = game->viewport_offset_y + 15;
            fc->_font_draw.color_rgb = 0xFFFFFF; /* white */
        }
    }

    /* ── Overlay: context menu (right-click minimenu). ───────────────────────
     * Emitted last so it draws on top of everything else.
     * Text lives in ui_frame_text_pool which is valid for the whole frame.  */
    if( game->minimenu_visible && game->minimenu_option_count > 0 )
        minimenu_draw(game, render_command_buffer, ov_font_id, ov_font);
}

static void
element_step_animation(
    struct Scene2Element* scene_element,
    struct EntityAnimation* animation)
{
    struct Scene2Frames* primary = scene2_element_primary_frames(scene_element);
    struct Scene2Frames* secondary = scene2_element_secondary_frames(scene_element);

    if( animation->primary_anim.anim_id != -1 && primary && primary->count > 0 )
    {
        int frame = animation->primary_anim.frame;
        scene2_element_set_active_anim_id(scene_element, animation->primary_anim.anim_id);
        scene2_element_set_active_animation_index(scene_element, 0);
        scene2_element_set_active_frame(scene_element, (uint8_t)frame);
    }
    else if( animation->secondary_anim.anim_id != -1 && secondary && secondary->count > 0 )
    {
        int frame = animation->secondary_anim.frame;
        scene2_element_set_active_anim_id(scene_element, animation->secondary_anim.anim_id);
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
    {
        if( game->uiscene_queued_commands )
            frame_emit_pass(fiber, FRAME_PASS_NONE);
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

        entity_animate(game->world, scene2_element_parent_entity_id(scene_element));

        /* Mouse-pick: if cursor is over this entity's projected bounds, add it to pickset. */
        {
            int mvx = game->mouse_x - game->viewport_offset_x;
            int mvy = game->mouse_y - game->viewport_offset_y;
            uint32_t ent_uid = (uint32_t)scene2_element_parent_entity_id(scene_element);
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

        int mvx = game->mouse_x - game->viewport_offset_x;
        int mvy = game->mouse_y - game->viewport_offset_y;
        if( mvx >= 0 && mvy >= 0 &&
            dash3d_projected_model_contains(game->sys_dash, tile_model, game->view_port, mvx, mvy) )
        {
            game->mouse_tile_x = sx;
            game->mouse_tile_z = sz;
            game->mouse_tile_level = slevel;
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

    {
        bool done = game->at_painters_command_index >= cap;
        if( done && game->uiscene_queued_commands )
            frame_emit_pass(fiber, FRAME_PASS_NONE);
        return done;
    }
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
    if( node->u.rs_layer.hide &&
        !interface_component_is_overlay_hovered(game, node->component_id) )
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
                    .game = game,
                    .cmds = game->uiscene_queued_commands,
                    .mouse_x = game->mouse_x,
                    .mouse_y = game->mouse_y,
                    .pass = &game->frame_pass,
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
            .game = game,
            .cmds = game->uiscene_queued_commands,
            .mouse_x = game->mouse_x,
            .mouse_y = game->mouse_y,
            .pass = &game->frame_pass,
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
        case UIELEM_BUILTIN_COMPASS:
            done = uielem_compass_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_REDSTONE_TAB:
            done = uielem_redstone_tab_step(&fiber, component);
            break;
        case UIELEM_BUILTIN_SIDEBAR:
            done = uielem_builtin_sidebar_step(&fiber, component);
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
frame_find_builtin(struct GGame* game, enum StaticUIComponentType ty)
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
frame_point_in_component_xy(struct StaticUIComponent* c, int px, int py)
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

static void
frame_handle_interface_and_world_clicks(struct GGame* game)
{
    if( !game->mouse_clicked || !game->iface )
        return;

    if( game->ui_root_buffer && game->iface->sidebar_interface_id == -1 )
    {
        for( uint32_t i = 0; i < game->ui_root_buffer->component_count; i++ )
        {
            struct StaticUIComponent* c = &game->ui_root_buffer->components[i];
            if( c->type != UIELEM_BUILTIN_REDSTONE_TAB )
                continue;
            if( !frame_point_in_component_xy(c, game->mouse_clicked_x, game->mouse_clicked_y) )
                continue;
            game->iface->selected_tab = c->u.redstone_tab.tabno;
            game->interface_consumed_click = 1;
            return;
        }
    }

    if( game->interface_consumed_click )
        return;

    /* UI button click dispatch (sidebar / viewport / chat).
     * Mirror Client.ts mouseLoop → doAction → IF_BUTTON path.
     * For TOGGLE/SELECT buttons, apply varp/varbit optimistic update so the UI
     * reflects the change immediately even though we don't send a real packet. */
    if( game->buildcachedat && game->iface )
    {
        int cx = game->mouse_clicked_x;
        int cy = game->mouse_clicked_y;

        /* Sidebar region */
        if( cx > 553 && cy > 205 && cx < 743 && cy < 466 )
        {
            int root_comp_id = -1;
            int root_x = 553, root_y = 205;

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
                    struct CacheDatConfigComponent* sroot =
                        buildcachedat_get_component(game->buildcachedat, compno);
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
                struct CacheDatConfigComponent* root =
                    buildcachedat_get_component(game->buildcachedat, root_comp_id);
                int comp_id = -1, client_code = 0, btn_action = 0;
                int pa = 0, pb = 0, pc = 0;
                if( root && interface_find_button_click_at(
                        game, root, root_x, root_y, cx, cy,
                        &comp_id, &client_code, &btn_action, &pa, &pb, &pc) )
                {
                    interface_apply_button_click_varp_optimistic(game, comp_id);
                    clientprot_if_button(game, comp_id);
                    game->interface_consumed_click = 1;
                }
            }
        }

        /* Viewport region */
        if( !game->interface_consumed_click &&
            cx > 4 && cy > 4 && cx < 516 && cy < 338 &&
            game->iface->viewport_interface_id >= 0 )
        {
            struct CacheDatConfigComponent* root =
                buildcachedat_get_component(game->buildcachedat,
                                            game->iface->viewport_interface_id);
            int comp_id = -1, client_code = 0, btn_action = 0;
            int pa = 0, pb = 0, pc = 0;
            if( root && interface_find_button_click_at(
                    game, root, 4, 4, cx, cy,
                    &comp_id, &client_code, &btn_action, &pa, &pb, &pc) )
            {
                interface_apply_button_click_varp_optimistic(game, comp_id);
                clientprot_if_button(game, comp_id);
                game->interface_consumed_click = 1;
            }
        }
    }

    if( game->interface_consumed_click )
        return;

    {
        struct StaticUIComponent* mm = frame_find_builtin(game, UIELEM_BUILTIN_MINIMAP);
        if( mm && frame_point_in_component_xy(mm, game->mouse_clicked_x, game->mouse_clicked_y) )
        {
            int bx = mm->position.x;
            int by = mm->position.y;
            int mx = game->mouse_clicked_x;
            int my = game->mouse_clicked_y;
            int x = mx - 25 - bx;
            int y = my - 4 - by;
            if( x >= 0 && y >= 0 && x < 146 && y < 151 )
            {
                x -= 73;
                y -= 75;
                int yaw = game->camera_yaw & 0x7ff;
                int sin_yaw = dash_sin(yaw);
                int cos_yaw = dash_cos(yaw);
                int zoomf = 256;
                sin_yaw = (sin_yaw * zoomf) >> 8;
                cos_yaw = (cos_yaw * zoomf) >> 8;
                int rel_x = (y * sin_yaw + x * cos_yaw) >> 11;
                int rel_y = (y * cos_yaw - x * sin_yaw) >> 11;
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
                int tile_x = (pw + rel_x) >> 7;
                int tile_z = (pz - rel_y) >> 7;
                int lvl = (game->mouse_tile_level >= 0) ? game->mouse_tile_level : 0;
                game->tile_clicked_x     = tile_x;
                game->tile_clicked_z     = tile_z;
                game->tile_clicked_level = lvl;
                /* Set minimap flag for the destination marker (task 9 / TS 11596-11599). */
                game->minimap_flag_x   = tile_x;
                game->minimap_flag_z   = tile_z;
                game->minimap_flag_has = 1;
                game->cross_x          = mx;
                game->cross_y          = my;
                game->cross_mode       = 1;
                game->cross_cycle      = 0;
                game->interface_consumed_click = 1;
            }
            return;
        }
    }

    {
        struct StaticUIComponent* wv = frame_find_builtin(game, UIELEM_BUILTIN_WORLD);
        if( !wv || !frame_point_in_component_xy(wv, game->mouse_clicked_x, game->mouse_clicked_y) )
            return;
        if( game->mouse_tile_x < 0 )
            return;
        game->tile_clicked_x     = game->mouse_tile_x;
        game->tile_clicked_z     = game->mouse_tile_z;
        game->tile_clicked_level = game->mouse_tile_level;
        game->minimap_flag_has   = 0; /* clear destination flag on direct world click */
        game->cross_x            = game->mouse_clicked_x;
        game->cross_y            = game->mouse_clicked_y;
        game->cross_mode         = 1;
        game->cross_cycle        = 0;
    }
}

void
LibToriRS_FrameEnd(struct GGame* game)
{
    game->frame_pass = FRAME_PASS_NONE;
    /* Build optionset from pickset for tooltip and context menu (Client.ts menuOption /
     * drawTooltip). */
    if( game->world )
    {
        game->option_set.option_count = 0;
        world_options_add_pickset_options(game->world, &game->pickset, &game->option_set);
    }

    frame_handle_interface_and_world_clicks(game);

    /* Advance the LRU frame counter for obj icon sprites (mirrors ObjType.spriteCache).
     * Called once at the end of every frame so cache_lookup can stamp last_used correctly. */
    obj_icon_cache_tick();
}

#endif