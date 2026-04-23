#pragma once

#include "platforms/common/buffered_sprite_2d.h"
#include "tori_rs_render.h"

/**
 * Fill inverse-rotation fragment constants for TORIRS_GFX_SPRITE_DRAW (rotated path).
 * @param atlas_u0_norm atlas u0 in [0,1], atlas_v0_norm v0 in [0,1] for the sprite subtexture
 * @param tex_w,tex_h backing texture size in pixels
 */
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
    SpriteInverseRotParams* out_params);

/**
 * Rotated sprite: four destination corners in logical pixels (axis-aligned dst rect corners
 * rotated about dst_anchor). Uses Dash g_cos/g_sin via dash_cos/dash_sin / 65536 — matches
 * Metal inverse-rot trig and replaces cosf(angle) on CPU-rotation backends for consistency.
 */
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
    float out_py[4]);
