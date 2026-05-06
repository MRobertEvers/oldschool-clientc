#include "osrs/ui_scrollbar_emit.h"

#include "osrs/game.h"
#include "tori_rs_render.h"

static void
emit_sb_sprite(
    struct ToriRSRenderCommandBuffer* buf,
    int element_id,
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
    c->_sprite_draw.atlas_index = 0;
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
emit_sb_fill_rect(
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

void
ui_emit_scrollbar_commands(
    struct ToriRSRenderCommandBuffer* buf,
    struct GGame* game,
    int x,
    int y,
    int height,
    int scroll_height,
    int scroll_pos)
{
    if( !buf || !game )
        return;

    int lh = height;
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

    int const e0 = game->ui_root_buffer ? game->ui_root_buffer->ui_scrollbar0_element_id : -1;
    int const e1 = game->ui_root_buffer ? game->ui_root_buffer->ui_scrollbar1_element_id : -1;
    struct UISceneElement* el0 = NULL;
    struct UISceneElement* el1 = NULL;
    if( game->ui_scene && e0 >= 0 && e0 < game->ui_scene->elements_count )
        el0 = &game->ui_scene->elements[e0];
    if( game->ui_scene && e1 >= 0 && e1 < game->ui_scene->elements_count )
        el1 = &game->ui_scene->elements[e1];
    struct DashSprite* sb0 = NULL;
    struct DashSprite* sb1 = NULL;
    if( el0 && el0->active && el0->dash_sprites && el0->dash_sprites_count > 0 )
        sb0 = el0->dash_sprites[0];
    if( el1 && el1->active && el1->dash_sprites && el1->dash_sprites_count > 0 )
        sb1 = el1->dash_sprites[0];

    if( sb0 && e0 >= 0 )
        emit_sb_sprite(buf, e0, sb0, x, y);
    if( sb1 && e1 >= 0 )
        emit_sb_sprite(buf, e1, sb1, x, y + lh - 16);

    /* Client.ts drawScrollbar / SCROLLBAR_* */
    emit_sb_fill_rect(buf, x, y + 16, 16, lh - 32, 0x23201b, 0, 1u);
    {
        int gt = y + 16 + grip_y;
        emit_sb_fill_rect(buf, x, gt, 16, grip_size, 0x4d4233, 0, 1u);
        emit_sb_fill_rect(buf, x, gt, 1, grip_size, 0x766654, 0, 1u);
        emit_sb_fill_rect(buf, x + 1, gt, 1, grip_size, 0x766654, 0, 1u);
        emit_sb_fill_rect(buf, x, gt, 16, 1, 0x766654, 0, 1u);
        emit_sb_fill_rect(buf, x, gt + 1, 16, 1, 0x766654, 0, 1u);
        emit_sb_fill_rect(buf, x + 15, gt, 1, grip_size, 0x332d25, 0, 1u);
        if( grip_size > 1 )
            emit_sb_fill_rect(buf, x + 14, gt + 1, 1, grip_size - 1, 0x332d25, 0, 1u);
        emit_sb_fill_rect(buf, x, gt + grip_size - 1, 16, 1, 0x332d25, 0, 1u);
        if( grip_size > 1 )
            emit_sb_fill_rect(buf, x + 1, gt + grip_size - 2, 15, 1, 0x332d25, 0, 1u);
    }
}
