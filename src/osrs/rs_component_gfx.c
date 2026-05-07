#include "rs_component_gfx.h"

#include "bmp.h"
#include "graphics/dash.h"
#include "graphics/dash_math.h"
#include "osrs/clientscript_vm.h"
#include "osrs/dash_utils.h"
#include "osrs/game.h"
#include "osrs/gamecache/gamecache.h"
#include "osrs/interface_state.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/interface.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "tori_rs_frame_state.h"
#include "tori_rs_render.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
    /* Degenerate RS_LAYER clip (width/height 0 in cache) would reject all boxes; BMP path has no
     * per-layer cull — treat as uncullable so RS_GRAPHIC still draws like uitree_loader_test. */
    if( e->clip_w <= 0 || e->clip_h <= 0 )
        return false;
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
    if( component->is_hidden )
        return true;

    /* TYPE_GRAPHIC activeGraphic: script if-active only (not mouse hover; hover is for TEXT/RECT
     * overColour via interface_component_overlay_hover_for_draw). */
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
        game->iface && interface_component_overlay_hover_for_draw(game, component->component_id);
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
        game->iface && interface_component_overlay_hover_for_draw(game, component->component_id);
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
    if( !game || !component || !queued_commands )
        return true;

    int eid = component->u.rs_model.scene_id;
    if( eid < 0 || !game->ui_scene )
        return true;

    struct DashModel* mod = uiscene_element_dash_model(game->ui_scene, eid);
    if( !mod )
        return true;

    (void)project_models;

    struct DashPosition position = { 0 };
    struct DashPosition world_position = { 0 };

    {
        int zm = component->u.rs_model.model_zoom;
        if( zm <= 0 )
            zm = 2000;
        int xa = component->u.rs_model.model_xan & 2047;
        int ya = component->u.rs_model.model_yan & 2047;
        position.pitch = xa;
        position.yaw   = ya;
        position.y += (dash_sin(xa) * zm) >> 16;
        position.z += (dash_cos(xa) * zm) >> 16;
    }

    position.x = position.x - game->camera_world_x;
    position.y = position.y - game->camera_world_y;
    position.z = position.z - game->camera_world_z;

    uint64_t const model_key = uiscene_model_cache_key(eid);
    int const visual_id = torirs_visual_id_from_cache_key(model_key);

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
        cmd->_model_draw.model_key = model_key;
        cmd->_model_draw.visual_id = visual_id;
        cmd->_model_draw.element_id = eid;
        cmd->_model_draw.use_animation = false;
        cmd->_model_draw.animation_index = 0;
        cmd->_model_draw.frame_index = 0;
        memcpy(&cmd->_model_draw.position, &position, sizeof(struct DashPosition));
        memcpy(&cmd->_model_draw.world_position, &world_position, sizeof(struct DashPosition));
        cmd->_model_draw.usage_hint = (uint8_t)TORIRS_USAGE_SCENERY;
        cmd->_model_draw.animation_frame = NULL;
        cmd->_model_draw.animation_framemap = NULL;
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
                                    if( iface && iface->inv_use_mode &&
                                        iface->inv_sel_comp_id == component->component_id &&
                                        iface->inv_sel_slot == i )
                                    {
                                        int rx = slot_x;
                                        int ry = slot_y;
                                        if( iface->inv_drag_area != 0 &&
                                            iface->inv_drag_comp_id == component->component_id &&
                                            iface->inv_drag_slot == i )
                                        {
                                            rx = draw_x;
                                            ry = draw_y;
                                        }
                                        int const o = 0xFFFFFF;
                                        int const a = 255;
                                        queue_rect_draw(
                                            queued_commands, rx, ry, 32, 1, o, a, 1);
                                        queue_rect_draw(
                                            queued_commands, rx, ry + 31, 32, 1, o, a, 1);
                                        queue_rect_draw(
                                            queued_commands, rx, ry, 1, 32, o, a, 1);
                                        queue_rect_draw(
                                            queued_commands, rx + 31, ry, 1, 32, o, a, 1);
                                    }
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

/** Client.ts `niceNumber`: comma groups then optional @cya@ / @gre@ prefixes (10509–10519). */
static void
rs_inv_text_comma_groups(char* dst, int cap, char const* num_digits)
{
    if( !dst || cap < 2 || !num_digits )
    {
        if( dst && cap > 0 )
            dst[0] = '\0';
        return;
    }
    int len = (int)strlen(num_digits);
    if( len <= 0 )
    {
        dst[0] = '\0';
        return;
    }
    int lead = ((len - 1) % 3) + 1;
    int di = 0;
    int pos = 0;
    for( int i = 0; i < lead && pos < len && di + 1 < cap; i++ )
        dst[di++] = num_digits[pos++];
    while( pos < len )
    {
        if( di + 1 >= cap )
            break;
        dst[di++] = ',';
        for( int k = 0; k < 3 && pos < len && di + 1 < cap; k++ )
            dst[di++] = num_digits[pos++];
    }
    dst[di] = '\0';
}

/** Writes niceNumber into `out` (includes leading space per Client.ts). Returns written length. */
static int
rs_inv_text_format_nice(char* out, int cap, int amount)
{
    char num[24];
    int nd = snprintf(num, sizeof(num), "%d", amount);
    if( nd <= 0 || nd >= (int)sizeof(num) || cap < 4 )
        return 0;
    char comma[40];
    rs_inv_text_comma_groups(comma, (int)sizeof(comma), num);
    int cl = (int)strlen(comma);
    if( cl > 8 )
    {
        return snprintf(
            out,
            (size_t)cap,
            " @gre@%.*s million @whi@(%s)",
            cl - 8,
            comma,
            comma);
    }
    if( cl > 4 )
    {
        return snprintf(
            out, (size_t)cap, " @cya@%.*sK @whi@(%s)", cl - 4, comma, comma);
    }
    return snprintf(out, (size_t)cap, " %s", comma);
}

/** When INV_TEXT has no load-time peer, find a RS_INV in the UITree sharing the same inv_pool
 * index and slot grid so UPDATE_INV_* gamecache rows can still be read. */
static int
inv_text_pick_fallback_inv_cache_cid(
    struct GGame* game,
    struct StaticUIComponent const* inv_text,
    int want_slots)
{
    struct UITree* t = game ? game->ui_root_buffer : NULL;
    if( !t || !inv_text || inv_text->type != UIELEM_RS_INV_TEXT )
        return -1;
    int inv_i = inv_text->u.rs_inv.inv_index;
    int self_id = inv_text->component_id;
    for( uint32_t i = 0; i < t->component_count; i++ )
    {
        struct StaticUIComponent const* c = &t->components[i];
        if( c->type != UIELEM_RS_INV || c->is_hidden )
            continue;
        if( c->component_id == self_id )
            continue;
        if( c->u.rs_inv.inv_index != inv_i )
            continue;
        if( want_slots > 0 && c->u.rs_inv.cols * c->u.rs_inv.rows != want_slots )
            continue;
        return c->component_id;
    }
    return -1;
}

static void
inv_text_wire_obj_count(
    struct GGame* game,
    struct StaticUIComponent* component,
    int slot,
    int* out_wire,
    int* out_count)
{
    *out_wire = 0;
    *out_count = 1;
    int inv_i = component->u.rs_inv.inv_index;
    if( game && game->inv_pool && inv_i >= 0 && inv_i < game->inv_pool->count && slot >= 0 &&
        slot < UI_INVENTORY_MAX_ITEMS )
    {
        struct UIInventoryItem* it = &game->inv_pool->inventories[inv_i].items[slot];
        if( it->obj_id > 0 )
        {
            *out_wire = it->obj_id;
            *out_count = it->obj_count > 0 ? it->obj_count : 1;
            return;
        }
    }
    int cid = component->component_id;
    if( component->inv_text_peer_inv_component_id >= 0 )
        cid = component->inv_text_peer_inv_component_id;
    if( game && game->gamecache && cid >= 0 )
    {
        struct GameCacheComponent* cc = gamecache_get_component(game->gamecache, cid);
        if( cc && cc->invSlotObjId &&
            (cc->type == COMPONENT_TYPE_INV || cc->type == COMPONENT_TYPE_INV_TEXT) &&
            slot >= 0 && slot < cc->width * cc->height )
        {
            *out_wire = cc->invSlotObjId[slot];
            if( cc->invSlotObjCount )
                *out_count = cc->invSlotObjCount[slot] > 0 ? cc->invSlotObjCount[slot] : 1;
        }
    }
    if( *out_wire <= 0 && component->inv_text_peer_inv_component_id < 0 && game &&
        game->gamecache && game->ui_root_buffer )
    {
        int want_slots = component->u.rs_inv.cols * component->u.rs_inv.rows;
        int alt = inv_text_pick_fallback_inv_cache_cid(game, component, want_slots);
        if( alt >= 0 && alt != cid )
        {
            struct GameCacheComponent* cc = gamecache_get_component(game->gamecache, alt);
            if( cc && cc->invSlotObjId &&
                (cc->type == COMPONENT_TYPE_INV || cc->type == COMPONENT_TYPE_INV_TEXT) &&
                slot >= 0 && slot < cc->width * cc->height )
            {
                *out_wire = cc->invSlotObjId[slot];
                if( cc->invSlotObjCount )
                    *out_count = cc->invSlotObjCount[slot] > 0 ? cc->invSlotObjCount[slot] : 1;
            }
        }
    }
}

bool
rs_gfx_inv_text_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* component)
{
    struct GGame* game = fiber->game;
    struct ToriRSRenderCommandBuffer* queued_commands = fiber->cmds;
    if( !game || !component || !game->ui_scene || !queued_commands || !game->ui_root_buffer )
        return true;
    if( component->is_hidden )
        return true;
    if( component->type != UIELEM_RS_INV_TEXT )
        return true;

    const char* inv_dbg = getenv( "TORI_INV_TEXT_DEBUG" );
    if( inv_dbg && inv_dbg[0] != '\0' && strcmp( inv_dbg, "0" ) != 0 )
    {
        int w0 = 0;
        int c0 = 1;
        inv_text_wire_obj_count(game, component, 0, &w0, &c0);
        fprintf(
            stderr,
            "[TORI_INV_TEXT_DEBUG] comp_id=%d inv_index=%d peer_inv_id=%d font_id=%d rows=%d "
            "cols=%d slot0_wire=%d slot0_count=%d\n",
            component->component_id,
            component->u.rs_inv.inv_index,
            component->inv_text_peer_inv_component_id,
            component->inv_text_font_id,
            component->u.rs_inv.rows,
            component->u.rs_inv.cols,
            w0,
            c0);
    }

    int fid = component->inv_text_font_id;
    if( fid < 0 )
        return true;
    struct DashPixFont* font = uiscene_font_get(game->ui_scene, fid);
    if( !font )
        return true;

    int cols = component->u.rs_inv.cols;
    int rows = component->u.rs_inv.rows;
    int margin_x = component->u.rs_inv.margin_x;
    int margin_y = component->u.rs_inv.margin_y;
    if( cols <= 0 )
        cols = 4;
    int const cell_w = 115;
    int const cell_h = 12;
    int scroll_off = uiframe_scroll_y_total(game);
    int base_x = component->position.x;
    int base_y = component->position.y - scroll_off;
    int pw = component->position.width;
    int colour = component->inv_text_color;

    int slot = 0;
    for( int row = 0; row < rows; row++ )
    {
        for( int col = 0; col < cols; col++, slot++ )
        {
            int text_x = base_x + col * (margin_x + cell_w);
            int text_y = base_y + row * (margin_y + cell_h);
            if( slot < UI_INV_SLOT_OFFSET_MAX )
            {
                text_x += component->u.rs_inv.inv_slot_offset_x[slot];
                text_y += component->u.rs_inv.inv_slot_offset_y[slot];
            }

            if( uiframe_cull_box(game, text_x, text_y, cell_w, cell_h) )
                continue;

            int wire = 0;
            int count = 1;
            inv_text_wire_obj_count(game, component, slot, &wire, &count);
            if( wire <= 0 )
                continue;

            int obj_def = wire - 1;
            struct GameCacheObj* obj =
                game->gamecache ? gamecache_get_obj(game->gamecache, obj_def) : NULL;
            char const* name = (obj && obj->name && obj->name[0]) ? obj->name : "";
            char line_buf[512];
            int ln = snprintf(line_buf, sizeof(line_buf), "%s", name);
            if( ln < 0 || ln >= (int)sizeof(line_buf) )
                continue;

            if( obj && (obj->stackable || count != 1) )
            {
                char nice[128];
                int nn = rs_inv_text_format_nice(nice, (int)sizeof(nice), count);
                if( nn > 0 && ln + 2 + nn < (int)sizeof(line_buf) )
                {
                    memcpy(line_buf + ln, " x", 2);
                    ln += 2;
                    memcpy(line_buf + ln, nice, (size_t)nn);
                    ln += nn;
                    line_buf[ln] = '\0';
                }
            }

            if( ln <= 0 )
                continue;

            uint8_t* pooled = rs_pool_dup_zterm(game, line_buf, ln);
            if( !pooled )
                continue;

            int draw_x = text_x;
            if( component->inv_text_center )
            {
                int text_w = dashfont_text_width_taggable(font, pooled);
                draw_x = text_x + (pw / 2) - (text_w / 2);
            }

            int draw_y = text_y;

            frame_emit_pass(fiber, FRAME_PASS_2D);
            if( component->inv_text_shadowed )
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
    }

    return true;
}
