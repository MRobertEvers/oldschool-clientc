#include "platforms/common/torirs_sprite_draw_cpu.h"

extern "C" {
#include "graphics/dash_math.h"
}

bool
torirs_sprite_inverse_rot_params_from_cmd(
    const struct ToriRSRenderCommand* cmd,
    int ix,
    int iy,
    int iw,
    int ih,
    float tex_w,
    float tex_h,
    float atlas_u0_norm,
    float atlas_v0_norm,
    struct SpriteInverseRotParams* out_params)
{
    if( !cmd || !out_params || tex_w <= 0.0f || tex_h <= 0.0f || iw <= 0 || ih <= 0 )
        return false;

    const int dst_bb_x = cmd->_sprite_draw.dst_bb_x;
    const int dst_bb_y = cmd->_sprite_draw.dst_bb_y;
    SpriteInverseRotParams p{};
    p.dst_origin_x = (float)dst_bb_x;
    p.dst_origin_y = (float)dst_bb_y;
    p.dst_anchor_x = (float)cmd->_sprite_draw.dst_anchor_x;
    p.dst_anchor_y = (float)cmd->_sprite_draw.dst_anchor_y;
    p.src_anchor_x = (float)cmd->_sprite_draw.src_anchor_x;
    p.src_anchor_y = (float)cmd->_sprite_draw.src_anchor_y;
    p.src_size_x = (float)iw;
    p.src_size_y = (float)ih;

    const float atlas_x = atlas_u0_norm * tex_w;
    const float atlas_y = atlas_v0_norm * tex_h;
    p.src_crop_x = (float)ix + atlas_x;
    p.src_crop_y = (float)iy + atlas_y;
    p.tex_size_x = tex_w;
    p.tex_size_y = tex_h;

    const int ang = cmd->_sprite_draw.rotation_r2pi2048 & 2047;
    p.cos_val = (float)dash_cos(ang) / 65536.0f;
    p.sin_val = (float)dash_sin(ang) / 65536.0f;
    p._pad0 = 0.0f;
    p._pad1 = 0.0f;
    *out_params = p;
    return true;
}

void
torirs_sprite_rotated_dst_corners_logical_px(
    int dst_bb_x,
    int dst_bb_y,
    int dst_bb_w,
    int dst_bb_h,
    int dst_anchor_x,
    int dst_anchor_y,
    int rotation_r2pi2048,
    float out_px[4],
    float out_py[4])
{
    if( !out_px || !out_py || dst_bb_w <= 0 || dst_bb_h <= 0 )
        return;

    const float pivot_x = (float)dst_bb_x + (float)dst_anchor_x;
    const float pivot_y = (float)dst_bb_y + (float)dst_anchor_y;
    const int ang = rotation_r2pi2048 & 2047;
    const float ca = (float)dash_cos(ang) / 65536.0f;
    const float sa = (float)dash_sin(ang) / 65536.0f;

    struct
    {
        int lx, ly;
    } corners[4] = {
        { 0, 0 },
        { dst_bb_w, 0 },
        { dst_bb_w, dst_bb_h },
        { 0, dst_bb_h },
    };
    for( int k = 0; k < 4; ++k )
    {
        float Lx = (float)(corners[k].lx - dst_anchor_x);
        float Ly = (float)(corners[k].ly - dst_anchor_y);
        out_px[k] = pivot_x + ca * Lx - sa * Ly;
        out_py[k] = pivot_y + sa * Lx + ca * Ly;
    }
}
