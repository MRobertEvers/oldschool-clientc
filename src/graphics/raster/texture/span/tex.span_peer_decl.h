#ifndef TEX_SPAN_PEER_DECL_H
#define TEX_SPAN_PEER_DECL_H

#include "graphics/dash_restrict.h"
#include <stdint.h>

static inline void
raster_linear_transparent_blend_lerp8(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade);

static inline void
raster_linear_transparent_texshadeflat_lerp8(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade);

static inline void
raster_linear_opaque_blend_lerp8(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade);

static inline void
raster_linear_opaque_texshadeflat_lerp8(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade);

static inline void
draw_texture_scanline_opaque_blend_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* RESTRICT texels,
    int texture_width);

static inline void
draw_texture_scanline_transparent_blend_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* RESTRICT texels,
    int texture_width);

static inline void
raster_linear_transparent_blend_lerp8_v3(
    uint32_t* __restrict pixel_buffer,
    const uint32_t* __restrict texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int u_mask,
    int v_mask,
    int shade);

static inline void
raster_linear_opaque_blend_lerp8_v3(
    uint32_t* __restrict pixel_buffer,
    const uint32_t* __restrict texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int u_mask,
    int v_mask,
    int shade);

static inline void
draw_texture_scanline_transparent_blend_branching_lerp8_v3_ordered(
    int* RESTRICT pixel_buffer,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* RESTRICT texels,
    int texture_width);

static inline void
draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered(
    int* RESTRICT pixel_buffer,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* RESTRICT texels,
    int texture_width);

static inline void
draw_texture_scanline_opaque_blend_affine_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x_start,
    int x_end,
    int pixel_offset,
    int shade_ish8,
    int shade_step_ish8,
    int u,
    int v,
    int w,
    int step_u_dx,
    int step_v_dx,
    int step_w_dx,
    int* RESTRICT texels,
    int origin_x);

static inline void
draw_texture_scanline_transparent_blend_affine_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x_start,
    int x_end,
    int pixel_offset,
    int shade_ish8,
    int shade_step_ish8,
    int u,
    int v,
    int w,
    int step_u_dx,
    int step_v_dx,
    int step_w_dx,
    int* RESTRICT texels,
    int origin_x);

static inline void
draw_texture_scanline_opaque_blend_affine_branching_lerp8_ish16_ordered(
    int* RESTRICT pixel_buffer,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* RESTRICT texels,
    int texture_width);

static inline void
draw_texture_scanline_transparent_blend_affine_branching_lerp8_ish16_ordered(
    int* RESTRICT pixel_buffer,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* RESTRICT texels,
    int texture_width);

#endif
