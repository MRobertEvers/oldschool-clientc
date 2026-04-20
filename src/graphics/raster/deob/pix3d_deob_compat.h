#ifndef PIX3D_DEOB_COMPAT_H
#define PIX3D_DEOB_COMPAT_H

#include "graphics/dash_restrict.h"
#include "graphics/dash_hsl16.h"

#include <stdint.h>

void pix3d_deob_compat_reset(void);

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

#endif /* PIX3D_DEOB_COMPAT_H */
