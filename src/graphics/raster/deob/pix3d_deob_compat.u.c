#ifndef PIX3D_DEOB_COMPAT_U_C
#define PIX3D_DEOB_COMPAT_U_C

#include "graphics/raster/deob/pix3d_deob_compat.h"
#include "graphics/raster/deob/pix3d_deob_state.h"
#include "graphics/shared_tables.h"

#include <stddef.h>

#include "graphics/raster/deob/pix3d_deob_state.c"
#include "graphics/raster/flat/flat.deob.u.c"
#include "graphics/raster/gouraud/gouraud.deob.u.c"
#include "graphics/raster/texture/texture.deob.u.c"

static int* s_deob_compat_pixels;
static int s_deob_compat_w;
static int s_deob_compat_h;
static int s_deob_compat_stride;
static int s_deob_compat_clip_max_y;

void pix3d_deob_compat_reset(void)
{
    pix3d_deob_free();
    s_deob_compat_pixels = NULL;
    s_deob_compat_w = 0;
    s_deob_compat_h = 0;
    s_deob_compat_stride = 0;
    s_deob_compat_clip_max_y = 0;
}

static void
pix3d_deob_compat_ensure(
    int* pixels,
    int screen_width,
    int screen_height,
    int stride,
    int clip_max_y)
{
    if( pixels == s_deob_compat_pixels && screen_width == s_deob_compat_w && screen_height == s_deob_compat_h
        && stride == s_deob_compat_stride && clip_max_y == s_deob_compat_clip_max_y )
        return;

    pix3d_deob_set_clipping(pixels, screen_width, screen_height, stride, clip_max_y);
    pix3d_deob_set_alpha(0);
    pix3d_deob_set_hclip(1);
    pix3d_deob_set_low_detail(1);
    pix3d_deob_set_low_mem(0);

    s_deob_compat_pixels = pixels;
    s_deob_compat_w = screen_width;
    s_deob_compat_h = screen_height;
    s_deob_compat_stride = stride;
    s_deob_compat_clip_max_y = clip_max_y;
}

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
    int alpha7)
{
    pix3d_deob_compat_ensure(pixel_buffer, screen_width, screen_height, stride, screen_height);
    if( alpha7 >= 0xFF )
        pix3d_deob_set_alpha(0);
    else
        pix3d_deob_set_alpha(256 - alpha7);

    int rgb = g_hsl16_to_rgb_table[hsl16_color & 0xFFFF];
    pix3d_deob_flat_triangle(x1, x2, x3, y1, y2, y3, rgb);
}

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
    int alpha7)
{
    pix3d_deob_compat_ensure(pixel_buffer, screen_width, screen_height, stride, screen_height);
    if( alpha7 >= 0xFF )
        pix3d_deob_set_alpha(0);
    else
        pix3d_deob_set_alpha(256 - alpha7);

    pix3d_deob_gouraud_triangle(x1, x2, x3, y1, y2, y3, color_a, color_b, color_c);
}

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
    int offset_y)
{
    (void)camera_fov;
    (void)near_plane_z;
    (void)offset_x;
    (void)offset_y;
    (void)texture_size;

    pix3d_deob_compat_ensure(pixel_buffer, screen_width, screen_height, stride, screen_height);
    pix3d_deob_set_alpha(0);
    g_pix3d_deob_opaque = texture_opaque ? 1 : 0;

    pix3d_deob_texture_triangle(
        x1,
        x2,
        x3,
        y1,
        y2,
        y3,
        shade_a << 8,
        shade_b << 8,
        shade_c << 8,
        orthographic_uvorigin_x0,
        orthographic_uend_x1,
        orthographic_vend_x2,
        orthographic_uvorigin_y0,
        orthographic_uend_y1,
        orthographic_vend_y2,
        orthographic_uvorigin_z0,
        orthographic_uend_z1,
        orthographic_vend_z2,
        texels,
        texture_opaque ? 1 : 0);
}

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
    int offset_y)
{
    raster_texture_screen_deob_compat(
        pixel_buffer,
        stride,
        screen_width,
        screen_height,
        camera_fov,
        x1,
        x2,
        x3,
        y1,
        y2,
        y3,
        orthographic_uvorigin_x0,
        orthographic_uend_x1,
        orthographic_vend_x2,
        orthographic_uvorigin_y0,
        orthographic_uend_y1,
        orthographic_vend_y2,
        orthographic_uvorigin_z0,
        orthographic_uend_z1,
        orthographic_vend_z2,
        shade,
        shade,
        shade,
        texels,
        texture_size,
        texture_opaque,
        near_plane_z,
        offset_x,
        offset_y);
}

#endif /* PIX3D_DEOB_COMPAT_U_C */
