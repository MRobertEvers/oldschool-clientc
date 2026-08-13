#ifndef SRC_RENDER_TRSPK_SPRITE_H
#define SRC_RENDER_TRSPK_SPRITE_H

#include "render/torirs_render.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Staging for rotated-masked sprite bakes (the minimap and the compass).
 *
 * Soft3D rotates straight into the framebuffer and needs none of this. The GPU
 * backends cannot: they have to hand GL a finished RGBA rectangle, so the blit
 * needs somewhere to land first. Both of them used to calloc/free that
 * rectangle inside the draw, which put a malloc and a free of the whole
 * minimap pixmap in the frame path -- twice per frame, once per backend copy
 * of the same forty lines.
 *
 * One of these per renderer is enough. A bake is consumed by the texture
 * upload before the next one starts (GL copies client memory at call time), so
 * the minimap and the compass take turns with the same buffer rather than
 * holding one each.
 */
struct TRSPK_RotmaskBake
{
    uint32_t* pixels;
    /** Allocated pixels, not bytes. */
    size_t capacity;
};

/**
 * ToriDraw ARGB -> the RGBA byte order GL wants, in place or between buffers.
 *
 * Restores an opaque alpha for pixels that carry colour but no alpha, which is
 * how ToriDraw spells "opaque" in sprites that were never given an alpha
 * channel. Skipping it leaves the minimap's ground transparent and its
 * channels swapped.
 */
void
trspk_sprite_argb_to_rgba(
    uint32_t const* src,
    uint32_t* dst,
    size_t count);

/**
 * Rotate `sp` through `mask_sp` into a dst_w x dst_h RGBA rectangle.
 *
 * Returns the baked pixels, owned by `bake` and valid until the next bake on
 * the same buffer, or NULL if it could not be sized. Grows `bake` on demand
 * and never shrinks it, so a steady-state minimap allocates nothing.
 */
uint32_t const*
trspk_sprite_rotmask_bake(
    struct TRSPK_RotmaskBake* bake,
    struct ToriRS_RenderCommand_Sprite const* cmd,
    struct ToriDraw_Sprite* sp,
    struct ToriDraw_Sprite* mask_sp,
    int dst_w,
    int dst_h);

/** Release the staging buffer. Safe on a zeroed bake, and re-usable after. */
void
trspk_sprite_rotmask_bake_release(struct TRSPK_RotmaskBake* bake);

void
trspk_sprite_local_to_uv(
    int local_x,
    int local_y,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y,
    int crop_w,
    int crop_h,
    int rotation_r2pi2048,
    float u0,
    float v0,
    float u1,
    float v1,
    float* out_u,
    float* out_v);

void
trspk_sprite_rotated_corners(
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    int dst_anchor_x,
    int dst_anchor_y,
    int src_anchor_x,
    int src_anchor_y,
    int crop_w,
    int crop_h,
    int rotation_r2pi2048,
    float u0,
    float v0,
    float u1,
    float v1,
    float pos[4][2],
    float uv[4][2]);

void
trspk_sprite_tile_phase_origin(
    int dest_x,
    int dest_y,
    int origin_x,
    int origin_y,
    int tile_w,
    int tile_h,
    int* out_start_x,
    int* out_start_y);

#ifdef __cplusplus
}
#endif

#endif
