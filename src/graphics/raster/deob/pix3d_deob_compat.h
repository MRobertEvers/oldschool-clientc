#ifndef PIX3D_DEOB_COMPAT_H
#define PIX3D_DEOB_COMPAT_H

#include "graphics/dash_restrict.h"
#include "graphics/dash_hsl16.h"

#include <stdint.h>

void pix3d_deob_compat_reset(void);

/* Once per rendered frame (Soft3D): dump prior totals, zero counters, rearm line budget. */
void pix3d_deob_compat_dbg_tick(void);

void raster_flat_screen_deob_compat(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x1,
    int x2,
    int x3,
    int y1,
    int y2,
    int y3,
    int hsl16_color,
    int alpha7);

void raster_gouraud_screen_deob_compat(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x1,
    int x2,
    int x3,
    int y1,
    int y2,
    int y3,
    int color_a,
    int color_b,
    int color_c,
    int alpha7);

/* pix3d_deob_texture_triangle: primary entry is here; benchmarks/benchmark_project/benchmark_main.c
 * also calls it directly for BENCH_TEX_*_DEOB. Pass 7-bit shades (compat clamps via deob_shade_7bit).
 *
 * Pix3D.textureTriangle contract (see Client-TS/src/dash3d/Pix3D.ts): (x1,y1,shade_a)…(x3,y3,shade_c)
 * are one triangle in screen space; orthographic_uvorigin is texture corner O, orthographic_uend is
 * corner B (txB in TS), orthographic_vend is corner C (txC) — the same three mesh positions as the
 * face’s UV mapping (e.g. tp_vertex / tm_vertex / tn_vertex in render_texture), not an arbitrary
 * permutation vs (x1,x2,x3). */
void raster_texture_screen_deob_compat(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_fov,
    int x1,
    int x2,
    int x3,
    int y1,
    int y2,
    int y3,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade_a,
    int shade_b,
    int shade_c,
    int* RESTRICT texels,
    int texture_size,
    int texture_opaque,
    int near_plane_z,
    int offset_x,
    int offset_y);

void raster_texture_flat_screen_deob_compat(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_fov,
    int x1,
    int x2,
    int x3,
    int y1,
    int y2,
    int y3,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade,
    int* RESTRICT texels,
    int texture_size,
    int texture_opaque,
    int near_plane_z,
    int offset_x,
    int offset_y);

void raster_texture_screen_deob2_compat(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_fov,
    int x1,
    int x2,
    int x3,
    int y1,
    int y2,
    int y3,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade_a,
    int shade_b,
    int shade_c,
    int* RESTRICT texels,
    int texture_size,
    int texture_opaque,
    int near_plane_z,
    int offset_x,
    int offset_y);

void raster_texture_flat_screen_deob2_compat(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_fov,
    int x1,
    int x2,
    int x3,
    int y1,
    int y2,
    int y3,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade,
    int* RESTRICT texels,
    int texture_size,
    int texture_opaque,
    int near_plane_z,
    int offset_x,
    int offset_y);

#endif /* PIX3D_DEOB_COMPAT_H */
