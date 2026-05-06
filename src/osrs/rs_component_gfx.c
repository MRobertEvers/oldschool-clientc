#include "rs_component_gfx.h"

#include "bmp.h"
#include "graphics/dash.h"
#include "osrs/clientscript_vm.h"
#include "osrs/dash_utils.h"
#include "osrs/game.h"
#include "osrs/interface_state.h"
#include "osrs/interface.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/scene2.h"
#include "tori_rs_frame_state.h"
#include "tori_rs_render.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint64_t
rs_model_cache_key_u64(
    struct Scene2* scene2,
    struct Scene2Element const* element)
{
    if( !element || !scene2 )
        return 0;
    int visual_id = scene2_element_visual_id(element);
    return ((uint64_t)(uint32_t)visual_id << 24) |
           ((uint64_t)scene2_element_active_anim_id(element) << 8) |
           (uint64_t)scene2_element_active_frame(element);
}

static void
queue_sprite_draw(
    struct ToriRSRenderCommandBuffer* buf,
    int element_id,
    int atlas_index,
    struct DashSprite* sprite,
    int x,
    int y,
    uint8_t sprite_blend_alpha)
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
    c->_sprite_draw.dst_bb_x = x;
    c->_sprite_draw.dst_bb_y = y;
    c->_sprite_draw.src_anchor_x = src_anchor_x;
    c->_sprite_draw.src_anchor_y = src_anchor_y;
    c->_sprite_draw.rotation_r2pi2048 = 0;
    c->_sprite_draw.src_bb_x = src_bb_x;
    c->_sprite_draw.src_bb_y = src_bb_y;
    c->_sprite_draw.src_bb_w = src_bb_w;
    c->_sprite_draw.src_bb_h = src_bb_h;
    c->_sprite_draw.rotated = false;
    if( sprite_blend_alpha != 0 )
    {
        c->_sprite_draw.alpha_mode         = 1;
        c->_sprite_draw.sprite_blend_alpha = sprite_blend_alpha;
    }
}

static int
uiframe_scroll_y_total(struct GGame* game)
{
    struct UITree* ui = game ? game->ui_root_buffer : NULL;
    if( !ui || ui->ui_layer_stack_top < 0 )
        return 0;
    return ui->ui_layer_stack[ui->ui_layer_stack_top].scroll_y_total;
}

static bool
uiframe_cull_box(
    struct GGame* game,
    int x,
    int y,
    int w,
    int h)
{
    if( w <= 0 || h <= 0 )
        return true;
    struct UITree* ui = game ? game->ui_root_buffer : NULL;
    if( !ui || ui->ui_layer_stack_top < 0 )
        return false;
    struct UILayerFrameEntry* e = &ui->ui_layer_stack[ui->ui_layer_stack_top];
    int x1 = x + w;
    int y1 = y + h;
    int ex0 = e->clip_x;
    int ey0 = e->clip_y;
    int ex1 = e->clip_x + e->clip_w;
    int ey1 = e->clip_y + e->clip_h;
    if( x1 <= ex0 || x >= ex1 || y1 <= ey0 || y >= ey1 )
        return true;
    return false;
}

static void
queue_rect_draw(
    struct ToriRSRenderCommandBuffer* buf,
    int x,
    int y,
    int w,
    int h,
    int color_rgb,
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
    c->_rect_draw.color_rgb = color_rgb;
    c->_rect_draw.alpha = alpha;
    c->_rect_draw.fill = fill;
}

bool
rs_gfx_graphic_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* component,
    int cur)
{
    (void)cur;
    struct GGame* game = fiber->game;
    struct ToriRSRenderCommandBuffer* queued_commands = fiber->cmds;
    if( !game || !component || !game->ui_scene || !queued_commands )
        return true;

    /* Active-state branch: use active sprite when VM says component is active. */
    struct ClientScriptVM* vm = game->clientscript_vm;
    bool active = clientscript_vm_active(
        vm,
        component->scripts,
        component->scripts_count,
        component->script_comparator,
        component->script_operand);

    int sid, ai;
    if( active && component->u.rs_graphic.scene_id_active >= 0 )
    {
        sid = component->u.rs_graphic.scene_id_active;
        ai  = component->u.rs_graphic.atlas_index_active;
    }
    else
    {
        sid = component->u.rs_graphic.scene_id;
        ai  = component->u.rs_graphic.atlas_index;
    }

    if( sid < 0 )
        return true;
    struct UISceneElement* el = uiscene_element_at(game->ui_scene, sid);
    if( !el || !el->dash_sprites || ai < 0 || ai >= el->dash_sprites_count )
        return true;
    struct DashSprite* sp = el->dash_sprites[ai];
    if( !sp )
        return true;
    int draw_x = component->position.x;
    int draw_y = component->position.y - uiframe_scroll_y_total(game);
    int cull_w = component->position.width;
    int cull_h = component->position.height;
    if( sp->crop_width > 0 && sp->crop_height > 0 )
    {
        if( cull_w < sp->crop_width )
            cull_w = sp->crop_width;
        if( cull_h < sp->crop_height )
            cull_h = sp->crop_height;
    }
    else
    {
        if( cull_w < sp->width )
            cull_w = sp->width;
        if( cull_h < sp->height )
            cull_h = sp->height;
    }
    if( uiframe_cull_box(game, draw_x, draw_y, cull_w, cull_h) )
        return true;
    frame_emit_pass(fiber, FRAME_PASS_2D);
    queue_sprite_draw(queued_commands, sid, ai, sp, draw_x, draw_y, 0);
    return true;
}

static uint8_t*
rs_pool_dup_zterm(struct GGame* game, char const* buf, int len)
{
    struct UITree* ui = game ? game->ui_root_buffer : NULL;
    if( !ui || len < 0 )
        return NULL;
    char* p = uitree_textpool_push_dup(&ui->text_pool, buf, (size_t)len);
    if( !p )
        return NULL;
    return (uint8_t*)p;
}

/* Font IDs are now resolved at UITree load time via uitree_load.c::ensure_font_id.
 * At render time we just return the pre-resolved id; no buildcache access needed. */
static int
rs_lazy_resolve_font_id(struct GGame* game, struct StaticUIComponent* component)
{
    (void)game;
    return component->u.rs_text.font_id;
}

bool
rs_gfx_text_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* component)
{
    struct GGame* game = fiber->game;
    struct ToriRSRenderCommandBuffer* queued_commands = fiber->cmds;
    if( !game || !component || !game->ui_scene || !queued_commands )
        return true;
    if( component->is_hidden )
        return true;

    int fid = rs_lazy_resolve_font_id(game, component);
    if( fid < 0 )
        return true;
    struct DashPixFont* font = uiscene_font_get(game->ui_scene, fid);
    if( !font )
        return true;

    struct ClientScriptVM* vm = game->clientscript_vm;
    int colour = component->u.rs_text.color;
    char const* text_src = component->u.rs_text.text;
    bool active = clientscript_vm_active(
        vm,
        component->scripts,
        component->scripts_count,
        component->script_comparator,
        component->script_operand);
    if( active )
    {
        colour = component->u.rs_text.active_color;
        if( component->u.rs_text.active_text && component->u.rs_text.active_text[0] != '\0' )
            text_src = component->u.rs_text.active_text;
    }
    bool hovered =
        game->iface && interface_component_is_overlay_hovered(game, component->component_id);
    if( hovered )
    {
        if( active && component->u.rs_text.active_over_color != 0 )
            colour = component->u.rs_text.active_over_color;
        else if( !active && component->u.rs_text.over_color != 0 )
            colour = component->u.rs_text.over_color;
    }

    if( !text_src || text_src[0] == '\0' )
        return true;

    int base_x = component->position.x;
    int base_y =
        component->position.y + font->height2d - uiframe_scroll_y_total(game);
    int pw = component->position.width;

    if( uiframe_cull_box(
            game, base_x, base_y - font->height2d, pw, component->position.height ) )
        return true;

    char line_buf[512];
    char expanded_buf[512];
    char const* rest = text_src;

    while( rest[0] != '\0' )
    {
        char const* line_end = rest;
        while( line_end[0] != '\0' && !(line_end[0] == '\\' && line_end[1] == 'n') )
            line_end++;
        int line_len = (int)(line_end - rest);
        if( line_len >= (int)sizeof(line_buf) )
            line_len = (int)sizeof(line_buf) - 1;
        if( line_len > 0 )
        {
            memcpy(line_buf, rest, (size_t)line_len);
            line_buf[line_len] = '\0';

            int exp_len = 0;
            for( int i = 0; i < line_len && exp_len < 511; i++ )
            {
                if( line_buf[i] == '%' && i + 1 < line_len )
                {
                    int script_idx = line_buf[i + 1] - '1';
                    if( script_idx >= 0 && script_idx <= 4 )
                    {
                        /* Always evaluate; return -2 when script is missing (mirrors
                         * Client.ts getIfVar which returns -2 for out-of-range scripts,
                         * displayed via inf() as "-2", never as the literal "%1"). */
                        int val = -2;
                        if( component->scripts && script_idx < component->scripts_count )
                            val = clientscript_vm_eval(vm, &component->scripts[script_idx]);
                        char val_buf[16];
                        int n;
                        if( val >= 999999999 )
                        {
                            val_buf[0] = '*';
                            val_buf[1] = '\0';
                            n = 1;
                        }
                        else
                        {
                            n = snprintf(val_buf, sizeof(val_buf), "%d", val);
                        }
                        for( int j = 0; j < n && val_buf[j] && exp_len < 511; j++ )
                            expanded_buf[exp_len++] = val_buf[j];
                        i++;
                        continue;
                    }
                }
                expanded_buf[exp_len++] = line_buf[i];
            }
            expanded_buf[exp_len] = '\0';

            uint8_t* pooled = rs_pool_dup_zterm(game, expanded_buf, exp_len);
            if( !pooled )
                return true;

            int draw_x = base_x;
            if( component->u.rs_text.center )
            {
                int text_w = dashfont_text_width_taggable(font, pooled);
                draw_x = base_x + (pw / 2) - (text_w / 2);
            }

            int draw_y = base_y - font->height2d;

            frame_emit_pass(fiber, FRAME_PASS_2D);
            if( component->u.rs_text.shadowed )
            {
                struct ToriRSRenderCommand* sh =
                    LibToriRS_RenderCommandBufferEmplaceCommand(queued_commands);
                sh->kind = TORIRS_GFX_DRAW_FONT;
                sh->_font_draw.font_id = fid;
                sh->_font_draw.font = font;
                sh->_font_draw.text = pooled;
                sh->_font_draw.x = draw_x + 1;
                sh->_font_draw.y = draw_y + 1;
                sh->_font_draw.color_rgb = 0;
            }
            {
                struct ToriRSRenderCommand* c =
                    LibToriRS_RenderCommandBufferEmplaceCommand(queued_commands);
                c->kind = TORIRS_GFX_DRAW_FONT;
                c->_font_draw.font_id = fid;
                c->_font_draw.font = font;
                c->_font_draw.text = pooled;
                c->_font_draw.x = draw_x;
                c->_font_draw.y = draw_y;
                c->_font_draw.color_rgb = colour;
            }
        }
        base_y += font->height2d;
        rest = (line_end[0] == '\\' && line_end[1] == 'n') ? line_end + 2 : line_end;
        if( rest[0] == '\0' )
            break;
    }

    return true;
}

bool
rs_gfx_rect_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* component)
{
    struct GGame* game = fiber->game;
    struct ToriRSRenderCommandBuffer* queued_commands = fiber->cmds;
    if( !game || !component || !queued_commands )
        return true;
    if( component->is_hidden )
        return true;

    struct ClientScriptVM* vm = game->clientscript_vm;
    int colour = component->u.rs_rect.color;
    bool active = clientscript_vm_active(
        vm,
        component->scripts,
        component->scripts_count,
        component->script_comparator,
        component->script_operand);
    if( active )
        colour = component->u.rs_rect.active_color;
    bool hovered =
        game->iface && interface_component_is_overlay_hovered(game, component->component_id);
    if( hovered )
    {
        if( active && component->u.rs_rect.active_over_color != 0 )
            colour = component->u.rs_rect.active_over_color;
        else if( !active && component->u.rs_rect.over_color != 0 )
            colour = component->u.rs_rect.over_color;
    }

    int x = component->position.x;
    int y = component->position.y - uiframe_scroll_y_total(game);
    int w = component->position.width;
    int h = component->position.height;
    if( uiframe_cull_box(game, x, y, w, h) )
        return true;

    int alpha = component->u.rs_rect.alpha;
    uint8_t fill = component->u.rs_rect.fill;

    frame_emit_pass(fiber, FRAME_PASS_2D);
    queue_rect_draw(
        queued_commands,
        x,
        y,
        w,
        h,
        colour,
        alpha,
        fill);
    return true;
}

bool
rs_gfx_model_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* component,
    bool project_models)
{
    struct GGame* game = fiber->game;
    struct ToriRSRenderCommandBuffer* queued_commands = fiber->cmds;
    if( !game || !component || !game->world || !game->world->scene2 || !queued_commands )
        return true;
    int eid = component->u.rs_model.scene2_element_id;
    if( eid < 0 )
        return true;
    struct Scene2Element* se = scene2_element_at(game->world->scene2, eid);
    if( !se )
        return true;

    /* Active/inactive switch: mirrors TS drawInterface TYPE_MODEL path (10424-10458).
     * When active, use active_anim_id (modelAnim2); when inactive, use anim_id (modelAnim). */
    {
        struct ClientScriptVM* vm = game->clientscript_vm;
        bool active = clientscript_vm_active(
            vm,
            component->scripts,
            component->scripts_count,
            component->script_comparator,
            component->script_operand);
        int desired_anim = active ? component->active_anim_id : component->anim_id;
        if( desired_anim >= 0 )
            scene2_element_set_active_anim_id(se, (uint16_t)desired_anim);
    }

    struct DashModel* mod = scene2_element_dash_model(se);
    struct DashPosition* sepos = scene2_element_dash_position(se);
    if( !mod || !sepos )
        return true;

    struct DashPosition position = { 0 };
    memcpy(&position, sepos, sizeof(struct DashPosition));
    struct DashPosition world_position = { 0 };
    memcpy(&world_position, sepos, sizeof(struct DashPosition));
    position.x = position.x - game->camera_world_x;
    position.y = position.y - game->camera_world_y;
    position.z = position.z - game->camera_world_z;

    if( project_models )
    {
        int cull =
            dash3d_project_model(game->sys_dash, mod, &position, game->view_port, game->camera);
        if( cull != DASHCULL_VISIBLE )
            return true;
    }

    frame_emit_pass_3d_with_rect(
        fiber,
        component->position.x,
        component->position.y,
        component->position.width,
        component->position.height);
    {
        struct ToriRSRenderCommand* cmd =
            LibToriRS_RenderCommandBufferEmplaceCommand(queued_commands);
        cmd->kind = TORIRS_GFX_DRAW_MODEL;
        cmd->_model_draw.model = mod;
        cmd->_model_draw.model_key = rs_model_cache_key_u64(game->world->scene2, se);
        cmd->_model_draw.visual_id = scene2_element_visual_id(se);
        cmd->_model_draw.element_id = eid;
        cmd->_model_draw.use_animation = scene2_element_active_anim_id(se) != 0;
        cmd->_model_draw.animation_index = scene2_element_active_animation_index(se);
        cmd->_model_draw.frame_index = scene2_element_active_frame(se);
        memcpy(&cmd->_model_draw.position, &position, sizeof(struct DashPosition));
        memcpy(&cmd->_model_draw.world_position, &world_position, sizeof(struct DashPosition));
        cmd->_model_draw.usage_hint = (uint8_t)scene2_element_category(se);
        cmd->_model_draw.animation_frame =
            cmd->_model_draw.use_animation
                ? scene2_element_dash_animation_frame(
                      se, cmd->_model_draw.animation_index, cmd->_model_draw.frame_index)
                : NULL;
        cmd->_model_draw.animation_framemap =
            cmd->_model_draw.use_animation ? scene2_element_dash_framemap(se) : NULL;
    }
    return true;
}

bool
rs_gfx_inv_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* component)
{
    struct GGame* game = fiber->game;
    struct ToriRSRenderCommandBuffer* queued_commands = fiber->cmds;
    if( !game || !component || !game->ui_scene || !queued_commands || !game->inv_pool )
        return true;
    int inv_i = component->u.rs_inv.inv_index;
    /* inv_i may be -1 when no live inventory is assigned to this tab (e.g. equipment tab before
     * the server sends worn items). In that case skip item drawing but still draw background
     * slot sprites (the equipment silhouette images). Client.ts always draws invBackground. */
    struct UIInventory* inv = (inv_i >= 0 && inv_i < game->inv_pool->count)
                                  ? &game->inv_pool->inventories[inv_i]
                                  : NULL;
    int cols = component->u.rs_inv.cols;
    int rows = component->u.rs_inv.rows;
    int margin_x = component->u.rs_inv.margin_x;
    int margin_y = component->u.rs_inv.margin_y;
    if( cols <= 0 )
        cols = 4;
    int scroll_off = uiframe_scroll_y_total(game);
    int base_x = component->position.x;
    int base_y = component->position.y - scroll_off;

    int i = 0;
    for( int row = 0; row < rows; row++ )
    {
        for( int col = 0; col < cols; col++, i++ )
        {
            int slot_x = base_x + col * (margin_x + 32);
            int slot_y = base_y + row * (margin_y + 32);
            if( i < UI_INV_SLOT_OFFSET_MAX )
            {
                slot_x += component->u.rs_inv.inv_slot_offset_x[i];
                slot_y += component->u.rs_inv.inv_slot_offset_y[i];
            }

            bool has_item = false;
            if( inv )
            {
                struct UIInventoryItem* it = &inv->items[i];
                if( it->obj_id > 0 )
                {
                    has_item = true;
                    if( it->scene_id >= 0 )
                    {
                        struct UISceneElement* el = uiscene_element_at(game->ui_scene, it->scene_id);
                        if( el && el->dash_sprites )
                        {
                            int ai = it->atlas_index;
                            if( ai >= 0 && ai < el->dash_sprites_count )
                            {
                                struct DashSprite* sp = el->dash_sprites[ai];
                                if( sp )
                                {
                                    uint8_t blend = 0;
                                    int draw_x    = slot_x;
                                    int draw_y    = slot_y;
                                    struct InterfaceState* iface = game->iface;
                                    if( iface && iface->inv_drag_area != 0 &&
                                        iface->inv_drag_comp_id == component->component_id &&
                                        iface->inv_drag_slot == i )
                                    {
                                        blend = 128;
                                        int dx, dy;
                                        interface_inv_drag_delta(game, &dx, &dy);
                                        draw_x += dx;
                                        draw_y += dy;
                                    }
                                    frame_emit_pass(fiber, FRAME_PASS_2D);
                                    queue_sprite_draw(
                                        queued_commands, it->scene_id, ai, sp, draw_x, draw_y, blend);
                                }
                            }
                        }
                    }
                }
            }

            if( !has_item && i < UI_INV_SLOT_OFFSET_MAX &&
                component->u.rs_inv.inv_slot_bg_scene_id[i] >= 0 )
            {
                int bg_sid = component->u.rs_inv.inv_slot_bg_scene_id[i];
                int bg_ai = component->u.rs_inv.inv_slot_bg_atlas_index[i];
                struct UISceneElement* bg_el = uiscene_element_at(game->ui_scene, bg_sid);
                if( bg_el && bg_el->dash_sprites && bg_ai >= 0 &&
                    bg_ai < bg_el->dash_sprites_count )
                {
                    struct DashSprite* bg_sp = bg_el->dash_sprites[bg_ai];
                    if( bg_sp )
                    {
                        frame_emit_pass(fiber, FRAME_PASS_2D);
                        queue_sprite_draw(queued_commands, bg_sid, bg_ai, bg_sp, slot_x, slot_y, 0);
                    }
                }
            }
        }
    }

    return true;
}
