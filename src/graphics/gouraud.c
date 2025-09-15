#include "gouraud.h"

#include "alpha.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

extern int g_hsl16_to_rgb_table[65536];

static int
interpolate_ish16(int x_begin, int x_end, int t_sh16)
{
    int stride = x_end - x_begin;
    return ((x_begin << 16) + stride * t_sh16) >> 16;
}

static int
interpolate_color_rgba_ish16(int color1, int color2, int t_ish16)
{
    int a1 = (color1 >> 24) & 0xFF;
    int r1 = (color1 >> 16) & 0xFF;
    int g1 = (color1 >> 8) & 0xFF;
    int b1 = color1 & 0xFF;

    int a2 = (color2 >> 24) & 0xFF;
    int r2 = (color2 >> 16) & 0xFF;
    int g2 = (color2 >> 8) & 0xFF;
    int b2 = color2 & 0xFF;

    int r = interpolate_ish16(r1, r2, t_ish16);
    int g = interpolate_ish16(g1, g2, t_ish16);
    int b = interpolate_ish16(b1, b2, t_ish16);
    int a = interpolate_ish16(a1, a2, t_ish16);

    return (a << 24) | (r << 16) | (g << 8) | b;
}

/**
 * @brief Requires caller to clamp y value to screen.
 *
 * @param pixel_buffer
 * @param z_buffer
 * @param stride_width
 * @param y
 * @param x_start
 * @param x_end
 * @param depth_start
 * @param depth_end
 * @param color_start
 * @param color_end
 */
void
draw_scanline_gouraud_zbuf(
    int* pixel_buffer,
    int* z_buffer,
    int stride_width,
    int y,
    int x_start,
    int x_end,
    int depth_start_ish16,
    int depth_end_ish16,
    int color_start_hsl16_ish8,
    int color_end_hsl16_ish8)
{
    if( x_start == x_end )
        return;
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
        tmp = color_start_hsl16_ish8;
        color_start_hsl16_ish8 = color_end_hsl16_ish8;
        color_end_hsl16_ish8 = tmp;
        tmp = depth_start_ish16;
        depth_start_ish16 = depth_end_ish16;
        depth_end_ish16 = tmp;
    }

    int dx_stride = x_end - x_start;
    assert(dx_stride > 0);

    int dcolor_hsl16_ish8 = color_end_hsl16_ish8 - color_start_hsl16_ish8;
    int depth_ish16 = depth_start_ish16;

    int step_color_hsl16_ish8 = 0;
    int step_depth_ish16 = 0;
    if( dx_stride > 3 )
    {
        step_color_hsl16_ish8 = dcolor_hsl16_ish8 / dx_stride;
        step_depth_ish16 = (depth_end_ish16 - depth_start_ish16) / dx_stride;
    }

    if( x_end > stride_width )
        x_end = stride_width;

    if( x_start < 0 )
    {
        color_start_hsl16_ish8 -= step_color_hsl16_ish8 * x_start;
        depth_start_ish16 -= step_depth_ish16 * x_start;
        x_start = 0;
    }

    if( x_start > x_end )
        return;

    // Steps by 4.
    int offset = x_start + y * stride_width;
    int steps = (x_end - x_start);

    int color_hsl16_ish8 = color_start_hsl16_ish8;
    while( --steps >= 0 )
    {
        int color_hsl16 = color_hsl16_ish8 >> 8;
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];
        if( depth_ish16 < z_buffer[offset] )
        {
            pixel_buffer[offset] = rgb_color;
            z_buffer[offset] = depth_ish16;
        }

        depth_ish16 += step_depth_ish16;
        color_hsl16_ish8 += step_color_hsl16_ish8;
        offset += 1;
    }
}

void
raster_gouraud_zbuf(
    int* pixel_buffer,
    int* z_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int z0,
    int z1,
    int z2,
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16)
{
    // Sort vertices by y
    // where y0 is the bottom vertex and y2 is the top vertex (or bottom of the screen)
    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;

        temp = color0_hsl16;
        color0_hsl16 = color1_hsl16;
        color1_hsl16 = temp;
    }

    if( y1 > y2 )
    {
        int temp = y1;
        y1 = y2;
        y2 = temp;

        temp = x1;
        x1 = x2;
        x2 = temp;

        temp = color1_hsl16;
        color1_hsl16 = color2_hsl16;
        color2_hsl16 = temp;
    }

    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;

        temp = color0_hsl16;
        color0_hsl16 = color1_hsl16;
        color1_hsl16 = temp;
    }

    // skip if the triangle is degenerate
    if( x0 == x1 && x1 == x2 )
        return;

    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;
    int dz_AC = z2 - z0;
    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;
    int dz_AB = z1 - z0;
    int dx_BC = x2 - x1;
    int dy_BC = y2 - y1;
    int dz_BC = z2 - z1;

    // AC is the longest edge
    int step_edge_x_AC_ish16;
    int step_edge_x_AB_ish16;
    int step_edge_x_BC_ish16;

    int step_edge_z_AC_ish16;
    int step_edge_z_AB_ish16;
    int step_edge_z_BC_ish16;

    if( dy_AC > 0 )
    {
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
        step_edge_z_AC_ish16 = (dz_AC << 16) / dy_AC;
    }
    else
    {
        step_edge_x_AC_ish16 = 0;
        step_edge_z_AC_ish16 = 0;
    }

    if( dy_AB > 0 )
    {
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
        step_edge_z_AB_ish16 = (dz_AB << 16) / dy_AB;
    }
    else
    {
        step_edge_x_AB_ish16 = 0;
        step_edge_z_AB_ish16 = 0;
    }

    if( dy_BC > 0 )
    {
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;
        step_edge_z_BC_ish16 = (dz_BC << 16) / dy_BC;
    }
    else
    {
        step_edge_x_BC_ish16 = 0;
        step_edge_z_BC_ish16 = 0;
    }

    /*
     *          /\      y0 (A)
     *         /  \
     *        /    \    y1 (B) (second_half = true above, false below)
     *       /   /
     *      / /  y2 (C) (second_half = false)
     */
    int edge_x_AC_ish16 = x0 << 16;
    int edge_x_AB_ish16 = x0 << 16;
    int edge_x_BC_ish16 = x1 << 16;

    int edge_z_AC_ish16 = z0 << 16;
    int edge_z_AB_ish16 = z0 << 16;
    int edge_z_BC_ish16 = z1 << 16;

    // Interpolate HSL16 colors directly
    int dcolor_AC = color2_hsl16 - color0_hsl16;
    int dcolor_AB = color1_hsl16 - color0_hsl16;
    int dcolor_BC = color2_hsl16 - color1_hsl16;

    int step_edge_color_AC_ish15;
    int step_edge_color_AB_ish15;
    int step_edge_color_BC_ish15;

    if( dy_AC > 0 )
        step_edge_color_AC_ish15 = (dcolor_AC << 15) / dy_AC;
    else
        step_edge_color_AC_ish15 = 0;
    if( dy_AB > 0 )
        step_edge_color_AB_ish15 = (dcolor_AB << 15) / dy_AB;
    else
        step_edge_color_AB_ish15 = 0;
    if( dy_BC > 0 )
        step_edge_color_BC_ish15 = (dcolor_BC << 15) / dy_BC;
    else
        step_edge_color_BC_ish15 = 0;

    int edge_color_AC_ish15 = color0_hsl16 << 15;
    int edge_color_AB_ish15 = color0_hsl16 << 15;
    int edge_color_BC_ish15 = color1_hsl16 << 15;

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;

        edge_z_AC_ish16 -= step_edge_z_AC_ish16 * y0;
        edge_z_AB_ish16 -= step_edge_z_AB_ish16 * y0;

        edge_color_AC_ish15 -= step_edge_color_AC_ish15 * y0;
        edge_color_AB_ish15 -= step_edge_color_AB_ish15 * y0;

        y0 = 0;
    }

    int i = y0;
    for( ; i < y1 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_AB_ish16 >> 16;

        // Get interpolated HSL16 colors
        int color_start_current = edge_color_AC_ish15 >> 7;
        int color_end_current = edge_color_AB_ish15 >> 7;

        draw_scanline_gouraud_zbuf(
            pixel_buffer,
            z_buffer,
            screen_width,
            i,
            x_start_current,
            x_end_current,
            edge_z_AC_ish16,
            edge_z_AB_ish16,
            color_start_current,
            color_end_current);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        edge_z_AC_ish16 += step_edge_z_AC_ish16;
        edge_z_AB_ish16 += step_edge_z_AB_ish16;

        edge_color_AC_ish15 += step_edge_color_AC_ish15;
        edge_color_AB_ish15 += step_edge_color_AB_ish15;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;
        edge_z_BC_ish16 -= step_edge_z_BC_ish16 * y1;
        edge_color_BC_ish15 -= step_edge_color_BC_ish15 * y1;

        y1 = 0;
    }

    i = y1;
    for( ; i < y2 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_BC_ish16 >> 16;

        // Get interpolated HSL16 colors
        int color_start_current = edge_color_AC_ish15 >> 7;
        int color_end_current = edge_color_BC_ish15 >> 7;

        draw_scanline_gouraud_zbuf(
            pixel_buffer,
            z_buffer,
            screen_width,
            i,
            x_start_current,
            x_end_current,
            edge_z_AC_ish16,
            edge_z_BC_ish16,
            color_start_current,
            color_end_current);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        edge_z_AC_ish16 += step_edge_z_AC_ish16;
        edge_z_BC_ish16 += step_edge_z_BC_ish16;

        edge_color_AC_ish15 += step_edge_color_AC_ish15;
        edge_color_BC_ish15 += step_edge_color_BC_ish15;
    }
}

extern int g_hsl16_to_rgb_table[65536];

static inline void
draw_scanline_gouraud_s4(
    int* pixel_buffer,
    int offset,
    int stride_width,
    int x_start_ish,
    int x_end_ish,
    int hsl,
    int hsl_step)
{
    if( x_start_ish > x_end_ish )
    {
        int tmp;
        tmp = x_start_ish;
        x_start_ish = x_end_ish;
        x_end_ish = tmp;
    }

    if( x_start_ish < 0 )
        x_start_ish = 0;
    if( x_end_ish >= (stride_width) )
        x_end_ish = (stride_width)-1;
    if( x_start_ish >= x_end_ish )
        return;

    int dx_stride = (x_end_ish - x_start_ish) >> 8;

    // Steps by 4.
    offset += x_start_ish >> 8;

    int steps = dx_stride >> 2;

    hsl += hsl_step * (x_start_ish >> 8);

    hsl_step <<= 2;
    int rgb_color;
    while( steps > 0 )
    {
        rgb_color = g_hsl16_to_rgb_table[hsl >> 8];

        pixel_buffer[offset++] = rgb_color;
        pixel_buffer[offset++] = rgb_color;
        pixel_buffer[offset++] = rgb_color;
        pixel_buffer[offset++] = rgb_color;

        hsl += hsl_step;

        steps -= 1;
    }

    steps = dx_stride & 0x3;
    rgb_color = g_hsl16_to_rgb_table[hsl >> 8];
    while( steps > 0 )
    {
        pixel_buffer[offset++] = rgb_color;

        steps -= 1;
    }
}

void
draw_scanline_gouraud_blend_s4(
    int* pixel_buffer,
    int stride_width,
    int y,
    int x_start,
    int x_end,
    int color_start_hsl16_ish8,
    int color_end_hsl16_ish8,
    int alpha)
{
    if( x_start == x_end )
        return;
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
        tmp = color_start_hsl16_ish8;
        color_start_hsl16_ish8 = color_end_hsl16_ish8;
        color_end_hsl16_ish8 = tmp;
    }

    int dx_stride = x_end - x_start;
    assert(dx_stride > 0);

    int dcolor_hsl16_ish8 = color_end_hsl16_ish8 - color_start_hsl16_ish8;

    int step_color_hsl16_ish8 = 0;
    if( dx_stride > 3 )
    {
        step_color_hsl16_ish8 = dcolor_hsl16_ish8 / dx_stride;
    }

    if( x_end >= stride_width )
    {
        x_end = stride_width - 1;
    }
    if( x_start < 0 )
    {
        color_start_hsl16_ish8 -= step_color_hsl16_ish8 * x_start;
        x_start = 0;
    }

    if( x_start >= x_end )
        return;

    // Steps by 4.
    int offset = x_start + y * stride_width;
    int steps = (x_end - x_start) >> 2;
    step_color_hsl16_ish8 <<= 2;

    int color_hsl16_ish8 = color_start_hsl16_ish8;
    while( --steps >= 0 )
    {
        int color_hsl16 = color_hsl16_ish8 >> 8;
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

        int rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset++] = rgb_blend;

        rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset++] = rgb_blend;

        rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset++] = rgb_blend;

        rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset++] = rgb_blend;

        // pixel_buffer[offset++] = rgb_color;
        // pixel_buffer[offset++] = rgb_color;
        // pixel_buffer[offset++] = rgb_color;
        // pixel_buffer[offset++] = rgb_color;

        color_hsl16_ish8 += step_color_hsl16_ish8;
    }

    steps = (x_end - x_start) & 0x3;
    while( --steps >= 0 )
    {
        int color_hsl16 = color_hsl16_ish8 >> 8;
        assert(color_hsl16 >= 0 && color_hsl16 < 65536);
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

        int rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset++] = rgb_blend;
    }
}

// #include "gouraud_deob.h"

void
raster_gouraud_blend_s4(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16,
    int alpha)
{
    // gouraud_deob_draw_triangle(
    //     pixel_buffer, y0, y1, y2, x0, x1, x2, color0_hsl16, color1_hsl16, color2_hsl16);
    // return;
    assert(alpha >= 0 && alpha <= 0xFF);

    // Sort vertices by y
    // where y0 is the bottom vertex and y2 is the top vertex (or bottom of the screen)
    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;

        temp = color0_hsl16;
        color0_hsl16 = color1_hsl16;
        color1_hsl16 = temp;
    }

    if( y1 > y2 )
    {
        int temp = y1;
        y1 = y2;
        y2 = temp;

        temp = x1;
        x1 = x2;
        x2 = temp;

        temp = color1_hsl16;
        color1_hsl16 = color2_hsl16;
        color2_hsl16 = temp;
    }

    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;

        temp = color0_hsl16;
        color0_hsl16 = color1_hsl16;
        color1_hsl16 = temp;
    }

    int total_height = y2 - y0;
    if( total_height == 0 )
        return;

    // skip if the triangle is degenerate
    if( x0 == x1 && x1 == x2 )
        return;

    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;
    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;
    int dx_BC = x2 - x1;
    int dy_BC = y2 - y1;

    // AC is the longest edge
    int step_edge_x_AC_ish16;
    int step_edge_x_AB_ish16;
    int step_edge_x_BC_ish16;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    else
        step_edge_x_AC_ish16 = 0;

    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    else
        step_edge_x_AB_ish16 = 0;

    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;
    else
        step_edge_x_BC_ish16 = 0;

    /*
     *          /\      y0 (A)
     *         /  \
     *        /    \    y1 (B) (second_half = true above, false below)
     *       /   /
     *      / /  y2 (C) (second_half = false)
     */
    int edge_x_AC_ish16 = x0 << 16;
    int edge_x_AB_ish16 = x0 << 16;
    int edge_x_BC_ish16 = x1 << 16;

    // Interpolate HSL16 colors directly
    int dcolor_AC = color2_hsl16 - color0_hsl16;
    int dcolor_AB = color1_hsl16 - color0_hsl16;
    int dcolor_BC = color2_hsl16 - color1_hsl16;

    int step_edge_color_AC_ish15;
    int step_edge_color_AB_ish15;
    int step_edge_color_BC_ish15;

    if( dy_AC > 0 )
        step_edge_color_AC_ish15 = (dcolor_AC << 15) / dy_AC;
    else
        step_edge_color_AC_ish15 = 0;
    if( dy_AB > 0 )
        step_edge_color_AB_ish15 = (dcolor_AB << 15) / dy_AB;
    else
        step_edge_color_AB_ish15 = 0;
    if( dy_BC > 0 )
        step_edge_color_BC_ish15 = (dcolor_BC << 15) / dy_BC;
    else
        step_edge_color_BC_ish15 = 0;

    int edge_color_AC_ish15 = color0_hsl16 << 15;
    int edge_color_AB_ish15 = color0_hsl16 << 15;
    int edge_color_BC_ish15 = color1_hsl16 << 15;

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;

        edge_color_AC_ish15 -= step_edge_color_AC_ish15 * y0;
        edge_color_AB_ish15 -= step_edge_color_AB_ish15 * y0;

        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;
        edge_color_BC_ish15 -= step_edge_color_BC_ish15 * y1;

        y1 = 0;
    }

    int i = y0;
    for( ; i < y1 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_AB_ish16 >> 16;

        // Get interpolated HSL16 colors
        int color_start_current = edge_color_AC_ish15 >> 7;
        int color_end_current = edge_color_AB_ish15 >> 7;

        draw_scanline_gouraud_blend_s4(
            pixel_buffer,
            screen_width,
            i,
            x_start_current,
            x_end_current,
            color_start_current,
            color_end_current,
            alpha);
        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        edge_color_AC_ish15 += step_edge_color_AC_ish15;
        edge_color_AB_ish15 += step_edge_color_AB_ish15;
    }

    if( y1 > y2 )
        return;

    i = y1;
    for( ; i < y2 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_BC_ish16 >> 16;

        // Get interpolated HSL16 colors
        int color_start_current = edge_color_AC_ish15 >> 7;
        int color_end_current = edge_color_BC_ish15 >> 7;

        draw_scanline_gouraud_blend_s4(
            pixel_buffer,
            screen_width,
            i,
            x_start_current,
            x_end_current,
            color_start_current,
            color_end_current,
            alpha);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        edge_color_AC_ish15 += step_edge_color_AC_ish15;
        edge_color_BC_ish15 += step_edge_color_BC_ish15;
    }
}

void
raster_gouraud_s4(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16)
{
    /**
     * Barycentric coordinates rely on ordering.
     */
    int dx_ordered_AC = x2 - x0;
    int dy_ordered_AC = y2 - y0;
    int dx_ordered_AB = x1 - x0;
    int dy_ordered_AB = y1 - y0;

    int area = dx_ordered_AB * dy_ordered_AC - dx_ordered_AC * dy_ordered_AB;
    if( area == 0 )
        return;

    // Barycentric coordinates for color.
    int dhsl_AB = color1_hsl16 - color0_hsl16;
    int dhsl_AC = color2_hsl16 - color0_hsl16;

    // Sort vertices by y
    // where y0 is the bottom vertex and y2 is the top vertex (or bottom of the screen)
    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;

        temp = color0_hsl16;
        color0_hsl16 = color1_hsl16;
        color1_hsl16 = temp;
    }

    if( y1 > y2 )
    {
        int temp = y1;
        y1 = y2;
        y2 = temp;

        temp = x1;
        x1 = x2;
        x2 = temp;

        temp = color1_hsl16;
        color1_hsl16 = color2_hsl16;
        color2_hsl16 = temp;
    }

    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;

        temp = color0_hsl16;
        color0_hsl16 = color1_hsl16;
        color1_hsl16 = temp;
    }

    /**
     * If tiles that are culled far outside the FOV, then this is not needed.
     */
    // if( (y2 - y0) >= screen_height )
    // {
    //     // This can happen if vertices extremely close to the camera plane, but outside the FOV
    //     // are projected. Those vertices need to be culled.
    //     return;
    // }

    // // TODO: Remove this check for callers that cull correctly.
    // if( (x0 < 0 || x1 < 0 || x2 < 0) &&
    //     (x0 > screen_width || x1 > screen_width || x2 > screen_width) )
    // {
    //     // This can happen if vertices extremely close to the camera plane, but outside the FOV
    //     // are projected. Those vertices need to be culled.
    //     return;
    // }

    // // skip if the triangle is degenerate
    // if( x0 == x1 && x1 == x2 )
    //     return;

    // Use hsl from color0 because this is where the vertex attribute scan is starting
    // as we scan across the screen.
    int hsl = color0_hsl16;

    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;
    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;
    int dx_BC = x2 - x1;
    int dy_BC = y2 - y1;

    // AC is the longest edge
    int step_edge_x_AC_ish16;
    int step_edge_x_AB_ish16;
    int step_edge_x_BC_ish16;

    static const int coord_shift = 8;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << coord_shift) / dy_AC;
    else
        step_edge_x_AC_ish16 = 0;

    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << coord_shift) / dy_AB;
    else
        step_edge_x_AB_ish16 = 0;

    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << coord_shift) / dy_BC;
    else
        step_edge_x_BC_ish16 = 0;

    /*
     *          /\      y0 (A)
     *         /  \
     *        /    \    y1 (B) (second_half = true above, false below)
     *       /   /
     *      / /  y2 (C) (second_half = false)
     */
    int edge_x_AC_ish16 = x0 << coord_shift;
    int edge_x_AB_ish16 = x0 << coord_shift;
    int edge_x_BC_ish16 = x1 << coord_shift;

    int color_step_x = ((dhsl_AB * dy_ordered_AC - dhsl_AC * dy_ordered_AB) << 8) / area;
    int color_step_y = ((dhsl_AC * dx_ordered_AB - dhsl_AB * dx_ordered_AC) << 8) / area;

    hsl = color_step_x + (hsl << 8) - x0 * color_step_x;

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;

        hsl -= color_step_y * y0;

        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;

        y1 = 0;
    }
    if( y1 >= screen_height )
        y1 = screen_height - 1;

    int offset = y0 * screen_width;

    int stride_width = screen_width << 8;
    int i = y0;
    for( ; i < y1; ++i )
    {
        // int x_start_current = edge_x_AC_ish16 >> 0;
        // int x_end_current = edge_x_AB_ish16 >> 0;

        draw_scanline_gouraud_s4(
            pixel_buffer,
            offset,
            stride_width,
            edge_x_AC_ish16,
            edge_x_AB_ish16,
            hsl,
            color_step_x);
        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        hsl += color_step_y;
        offset += screen_width;
    }
    if( y1 >= y2 )
        return;

    if( y2 >= screen_height )
        y2 = screen_height - 1;

    i = y1;
    for( ; i < y2; ++i )
    {
        // int x_start_current = edge_x_AC_ish16 >> 0;
        // int x_end_current = edge_x_BC_ish16 >> 0;

        draw_scanline_gouraud_s4(
            pixel_buffer,
            offset,
            stride_width,
            edge_x_AC_ish16,
            edge_x_BC_ish16,
            hsl,
            color_step_x);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        hsl += color_step_y;
        offset += screen_width;
    }
}
