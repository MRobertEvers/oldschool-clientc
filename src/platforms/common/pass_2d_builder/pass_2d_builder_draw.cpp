#include "platforms/common/pass_2d_builder/pass_2d_builder.h"

#include "graphics/dash.h"
#include "platforms/common/torirs_font_glyph_quad.h"
#include "platforms/common/torirs_gpu_clipspace.h"
#include "platforms/common/torirs_sprite_draw_cpu.h"
#include "tori_rs_render.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Pass2DBuilder::enqueue_sprite_from_draw
//
// Mirrors metal_sprite_draw_impl() without any Metal pipeline-state checks.
// The caller is responsible for looking up atlas_tex, tex_w/h, and atlas_u0/v0
// from their renderer's sprite cache before calling this method.
// ---------------------------------------------------------------------------
void
Pass2DBuilder::enqueue_sprite_from_draw(
    const struct ToriRSRenderCommand* cmd,
    void* atlas_tex,
    float tex_w,
    float tex_h,
    float atlas_u0,
    float atlas_v0,
    float fbw,
    float fbh)
{
    const struct DashSprite* sp = cmd->_sprite_draw.sprite;
    if( !sp || sp->width <= 0 || sp->height <= 0 || !atlas_tex )
        return;

    const int iw = cmd->_sprite_draw.src_bb_w > 0 ? cmd->_sprite_draw.src_bb_w : sp->width;
    const int ih = cmd->_sprite_draw.src_bb_h > 0 ? cmd->_sprite_draw.src_bb_h : sp->height;
    const int ix = cmd->_sprite_draw.src_bb_x;
    const int iy = cmd->_sprite_draw.src_bb_y;
    if( ix < 0 || iy < 0 || ix + iw > sp->width || iy + ih > sp->height )
        return;

    const float tw = (float)sp->width;
    const float th = (float)sp->height;
    const bool rotated = cmd->_sprite_draw.rotated;

    float px[4], py[4], log_u[4], log_v[4];
    SpriteInverseRotParams inv_params{};

    if( rotated )
    {
        const int dw = cmd->_sprite_draw.dst_bb_w;
        const int dh = cmd->_sprite_draw.dst_bb_h;
        if( dw <= 0 || dh <= 0 || iw <= 0 || ih <= 0 )
            return;
        const int dst_bb_x = cmd->_sprite_draw.dst_bb_x;
        const int dst_bb_y = cmd->_sprite_draw.dst_bb_y;
        px[0] = px[3] = (float)dst_bb_x;
        px[1] = px[2] = (float)(dst_bb_x + dw);
        py[0] = py[1] = (float)dst_bb_y;
        py[2] = py[3] = (float)(dst_bb_y + dh);
        log_u[0] = log_u[3] = (float)dst_bb_x;
        log_u[1] = log_u[2] = (float)(dst_bb_x + dw);
        log_v[0] = log_v[1] = (float)dst_bb_y;
        log_v[2] = log_v[3] = (float)(dst_bb_y + dh);
        if( !torirs_sprite_inverse_rot_params_from_cmd(
                cmd, ix, iy, iw, ih, tex_w, tex_h, atlas_u0, atlas_v0, &inv_params) )
            return;
    }
    else
    {
        const int dst_x = cmd->_sprite_draw.dst_bb_x + sp->crop_x;
        const int dst_y = cmd->_sprite_draw.dst_bb_y + sp->crop_y;
        px[0] = px[3] = (float)dst_x;
        px[1] = px[2] = (float)(dst_x + iw);
        py[0] = py[1] = (float)dst_y;
        py[2] = py[3] = (float)(dst_y + ih);
    }

    float c0x, c0y, c1x, c1y, c2x, c2y, c3x, c3y;
    torirs_logical_pixel_to_ndc(px[0], py[0], fbw, fbh, &c0x, &c0y);
    torirs_logical_pixel_to_ndc(px[1], py[1], fbw, fbh, &c1x, &c1y);
    torirs_logical_pixel_to_ndc(px[2], py[2], fbw, fbh, &c2x, &c2y);
    torirs_logical_pixel_to_ndc(px[3], py[3], fbw, fbh, &c3x, &c3y);

    SpriteQuadVertex six[6];
    if( rotated )
    {
        six[0] = { c0x, c0y, log_u[0], log_v[0] };
        six[1] = { c1x, c1y, log_u[1], log_v[1] };
        six[2] = { c2x, c2y, log_u[2], log_v[2] };
        six[3] = { c0x, c0y, log_u[0], log_v[0] };
        six[4] = { c2x, c2y, log_u[2], log_v[2] };
        six[5] = { c3x, c3y, log_u[3], log_v[3] };
        enqueue_sprite(atlas_tex, six, nullptr, &inv_params);
    }
    else
    {
        const float u0 = (float)ix / tw;
        const float v0 = (float)iy / th;
        const float u1 = (float)(ix + iw) / tw;
        const float v1 = (float)(iy + ih) / th;
        six[0] = { c0x, c0y, u0, v0 };
        six[1] = { c1x, c1y, u1, v0 };
        six[2] = { c2x, c2y, u1, v1 };
        six[3] = { c0x, c0y, u0, v0 };
        six[4] = { c2x, c2y, u1, v1 };
        six[5] = { c3x, c3y, u0, v1 };
        enqueue_sprite(atlas_tex, six, nullptr);
    }
}

// ---------------------------------------------------------------------------
// Pass2DBuilder::append_font_draw
//
// Mirrors metal_frame_event_font_draw() without Metal-specific state checks.
// The caller is responsible for looking up atlas_tex from their font cache.
// ---------------------------------------------------------------------------
void
Pass2DBuilder::append_font_draw(
    const struct ToriRSRenderCommand* cmd,
    void* atlas_tex,
    float fbw,
    float fbh)
{
    const struct DashPixFont* f = cmd->_font_draw.font;
    if( !f || !f->atlas || !cmd->_font_draw.text || f->height2d <= 0 || !atlas_tex )
        return;

    set_font(cmd->_font_draw.font_id, atlas_tex);

    const struct DashFontAtlas* atlas = f->atlas;
    const float inv_aw = 1.0f / (float)atlas->atlas_width;
    const float inv_ah = 1.0f / (float)atlas->atlas_height;

    const uint8_t* text = cmd->_font_draw.text;
    const int length = (int)strlen((const char*)text);
    int color_rgb = cmd->_font_draw.color_rgb;
    float cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
    float cg = (float)((color_rgb >> 8) & 0xFF) / 255.0f;
    float cb = (float)(color_rgb & 0xFF) / 255.0f;
    const float ca = 1.0f;
    int pen_x = cmd->_font_draw.x;
    const int base_y = cmd->_font_draw.y;

    for( int i = 0; i < length; i++ )
    {
        if( text[i] == '@' && i + 5 <= length && text[i + 4] == '@' )
        {
            const int new_color = dashfont_evaluate_color_tag((const char*)&text[i + 1]);
            if( new_color >= 0 )
            {
                color_rgb = new_color;
                cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
                cg = (float)((color_rgb >> 8) & 0xFF) / 255.0f;
                cb = (float)(color_rgb & 0xFF) / 255.0f;
            }
            if( i + 6 <= length && text[i + 5] == ' ' )
                i += 5;
            else
                i += 4;
            continue;
        }

        const int c = dashfont_charcode_to_glyph(text[i]);
        if( c < DASH_FONT_CHAR_COUNT )
        {
            const int gw = atlas->glyph_w[c];
            const int gh = atlas->glyph_h[c];
            if( gw > 0 && gh > 0 )
            {
                const float sx0 = (float)(pen_x + f->char_offset_x[c]);
                const float sy0 = (float)(base_y + f->char_offset_y[c]);
                const float sx1 = sx0 + (float)gw;
                const float sy1 = sy0 + (float)gh;
                const float u0 = (float)atlas->glyph_x[c] * inv_aw;
                const float v0 = (float)atlas->glyph_y[c] * inv_ah;
                const float u1 = (float)(atlas->glyph_x[c] + gw) * inv_aw;
                const float v1 = (float)(atlas->glyph_y[c] + gh) * inv_ah;
                float vq[48];
                torirs_font_glyph_quad_clipspace(
                    sx0, sy0, sx1, sy1, u0, v0, u1, v1, cr, cg, cb, ca, fbw, fbh, vq);
                append_glyph_quad(vq);
            }
        }
        int adv = f->char_advance[c];
        if( adv <= 0 )
            adv = 4;
        pen_x += adv;
    }

    close_font_segment();
}
