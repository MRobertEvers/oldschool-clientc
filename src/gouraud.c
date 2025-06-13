#include "gouraud.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

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
    int depth_start,
    int depth_end,
    int color_start,
    int color_end)
{
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
        tmp = depth_start;
        depth_start = depth_end;
        depth_end = tmp;
        tmp = color_start;
        color_start = color_end;
        color_end = tmp;
    }

    int dx_stride = x_end - x_start;
    for( int x = x_start; x <= x_end; ++x )
    {
        int t_sh16 = dx_stride == 0 ? 0 : ((x - x_start) << 16) / dx_stride;

        int depth = interpolate_ish16(depth_start, depth_end, t_sh16);
        if( z_buffer[y * stride_width + x] <= depth )
            z_buffer[y * stride_width + x] = depth;
        else
            continue;

        int r = interpolate_ish16((color_start >> 16) & 0xFF, (color_end >> 16) & 0xFF, t_sh16);
        int g = interpolate_ish16((color_start >> 8) & 0xFF, (color_end >> 8) & 0xFF, t_sh16);
        int b = interpolate_ish16(color_start & 0xFF, color_end & 0xFF, t_sh16);
        int a = 0xFF; // Alpha value

        // blend alpha
        // int alpha = (color >> 24) & 0xFF;
        int alpha = a;
        int inv_alpha = 0xFF - alpha;

        int r_blend = (r * alpha + inv_alpha * r) >> 8;
        int g_blend = (g * alpha + inv_alpha * g) >> 8;
        int b_blend = (b * alpha + inv_alpha * b) >> 8;
        int a_blend = (a * alpha + inv_alpha * a) >> 8;

        int color = (a_blend << 24) | (r_blend << 16) | (g_blend << 8) | b_blend;

        pixel_buffer[y * stride_width + x] = color;
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
    int color0,
    int color1,
    int color2)
{
    // Sort vertices by y
    // where y0 is the bottom vertex and y2 is the top vertex
    if( y0 > y1 )
    {
        int t;
        t = y0;
        y0 = y1;
        y1 = t;
        t = x0;
        x0 = x1;
        x1 = t;
        t = z0;
        z0 = z1;
        z1 = t;
        t = color0;
        color0 = color1;
        color1 = t;
    }
    if( y0 > y2 )
    {
        int t;
        t = y0;
        y0 = y2;
        y2 = t;
        t = x0;
        x0 = x2;
        x2 = t;
        t = z0;
        z0 = z2;
        z2 = t;
        t = color0;
        color0 = color2;
        color2 = t;
    }
    if( y1 > y2 )
    {
        int t;
        t = y1;
        y1 = y2;
        y2 = t;
        t = x1;
        x1 = x2;
        x2 = t;
        t = z1;
        z1 = z2;
        z2 = t;
        t = color1;
        color1 = color2;
        color2 = t;
    }

    int total_height = y2 - y0;
    if( total_height == 0 )
        return;

    // skip if the triangle is degenerate
    if( x0 == x1 && x1 == x2 )
        return;

    for( int i = 0; i < total_height; ++i )
    {
        /*
         *          /\      y2
         *         /  \
         *        /    \    y1 (second_half = true above)
         *       /   /
         *      / /  y0 (second_half = false)
         */
        bool second_half = i > (y1 - y0) || y1 == y0;
        int segment_height = second_half ? y2 - y1 : y1 - y0;
        if( segment_height == 0 )
            continue;

        int alpha_ish16 = (i << 16) / total_height;
        int beta_ish16 = ((i - (second_half ? y1 - y0 : 0)) << 16) / segment_height;

        int ax = interpolate_ish16(x0, x2, alpha_ish16);
        int bx = second_half ? interpolate_ish16(x1, x2, beta_ish16)
                             : interpolate_ish16(x0, x1, beta_ish16);

        int acolor = interpolate_color_rgba_ish16(color0, color2, alpha_ish16);
        int bcolor = second_half ? interpolate_color_rgba_ish16(color1, color2, beta_ish16)
                                 : interpolate_color_rgba_ish16(color0, color1, beta_ish16);

        int adepth = interpolate_ish16(z0, z2, alpha_ish16);
        int bdepth = second_half ? interpolate_ish16(z1, z2, beta_ish16)
                                 : interpolate_ish16(z0, z1, beta_ish16);

        int y = y0 + i;
        if( y >= 0 && y < screen_height )
        {
            if( ax < 0 )
                ax = 0;
            if( ax >= screen_width )
                ax = screen_width - 1;

            if( bx < 0 )
                bx = 0;
            if( bx >= screen_width )
                bx = screen_width - 1;

            draw_scanline_gouraud_zbuf(
                pixel_buffer, z_buffer, screen_width, y, ax, bx, adepth, bdepth, acolor, bcolor);
        }
    }
}

void
draw_scanline_gouraud2(
    int* pixel_buffer,
    int stride_width,
    int y,
    int x_start,
    int x_end,
    int color_start,
    int color_end)
{
    if( x_start == x_end )
        return;
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
        tmp = color_start;
        color_start = color_end;
        color_end = tmp;
    }

    int dx_stride = x_end - x_start;
    for( int x = x_start; x <= x_end; ++x )
    {
        int t_sh16 = dx_stride == 0 ? 0 : ((x - x_start) << 16) / dx_stride;

        int r = interpolate_ish16((color_start >> 16) & 0xFF, (color_end >> 16) & 0xFF, t_sh16);
        int g = interpolate_ish16((color_start >> 8) & 0xFF, (color_end >> 8) & 0xFF, t_sh16);
        int b = interpolate_ish16(color_start & 0xFF, color_end & 0xFF, t_sh16);
        int a = 0xFF; // Alpha value

        int alpha = a;
        int inv_alpha = 0xFF - alpha;

        int r_blend = (r * alpha + inv_alpha * r) >> 8;
        int g_blend = (g * alpha + inv_alpha * g) >> 8;
        int b_blend = (b * alpha + inv_alpha * b) >> 8;

        int color = (a << 24) | (r_blend << 16) | (g_blend << 8) | b_blend;

        pixel_buffer[y * stride_width + x] = color;
    }
}

void
draw_scanline_gouraud3(
    int* pixel_buffer,
    int stride_width,
    int y,
    int x_start,
    int x_end,
    int color_start,
    int color_end)
{
    if( x_start == x_end )
        return;
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
        tmp = color_start;
        color_start = color_end;
        color_end = tmp;
    }

    int dx_stride = x_end - x_start;
    for( int x = x_start; x <= x_end; ++x )
    {
        if( x < 0 )
            continue;
        if( x >= stride_width )
            continue;

        int t_sh16 = dx_stride == 0 ? 0 : ((x - x_start) << 16) / dx_stride;

        int r = interpolate_ish16((color_start >> 16) & 0xFF, (color_end >> 16) & 0xFF, t_sh16);
        int g = interpolate_ish16((color_start >> 8) & 0xFF, (color_end >> 8) & 0xFF, t_sh16);
        int b = interpolate_ish16(color_start & 0xFF, color_end & 0xFF, t_sh16);
        int a = 0xFF; // Alpha value

        int alpha = a;
        int inv_alpha = 0xFF - alpha;

        int r_blend = (r * alpha + inv_alpha * r) >> 8;
        int g_blend = (g * alpha + inv_alpha * g) >> 8;
        int b_blend = (b * alpha + inv_alpha * b) >> 8;

        int color = (a << 24) | (r_blend << 16) | (g_blend << 8) | b_blend;

        pixel_buffer[y * stride_width + x] = color;
    }
}

struct ScanLine
{
    int y;
    int x_start;
    int x_end;
    int color_start;
    int color_end;
};

static struct ScanLine
raster_gouraud_2(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int index,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0,
    int color1,
    int color2)
{
    int i = index;
    struct ScanLine scanline = { 0 };

    /*
     *          /\      y2
     *         /  \
     *        /    \    y1 (second_half = true above)
     *       /   /
     *      / /  y0 (second_half = false)
     */
    int total_height = y2 - y0;
    bool second_half = i > (y1 - y0) || y1 == y0;
    int segment_height = second_half ? y2 - y1 : y1 - y0;
    if( segment_height == 0 )
        return scanline;

    int alpha_ish16 = (i << 16) / total_height;
    int beta_ish16 = ((i - (second_half ? y1 - y0 : 0)) << 16) / segment_height;

    int ax = interpolate_ish16(x0, x2, alpha_ish16);
    int bx =
        second_half ? interpolate_ish16(x1, x2, beta_ish16) : interpolate_ish16(x0, x1, beta_ish16);

    int acolor = interpolate_color_rgba_ish16(color0, color2, alpha_ish16);
    int bcolor = second_half ? interpolate_color_rgba_ish16(color1, color2, beta_ish16)
                             : interpolate_color_rgba_ish16(color0, color1, beta_ish16);

    int y = y0 + i;
    if( y >= 0 && y < screen_height )
    {
        if( ax < 0 )
            ax = 0;
        if( ax >= screen_width )
            ax = screen_width - 1;

        if( bx < 0 )
            bx = 0;
        if( bx >= screen_width )
            bx = screen_width - 1;

        scanline.y = y;
        scanline.x_start = ax;
        scanline.x_end = bx;
        scanline.color_start = acolor;
        scanline.color_end = bcolor;

        return scanline;
    }
}

extern int g_hsl16_to_rgb_table[65536];

void
draw_scanline_gouraud(
    int* pixel_buffer,
    int stride_width,
    int y,
    int x_start,
    int x_end,
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
    }

    int dx_stride = x_end - x_start;
    assert(dx_stride > 0);

    int dcolor_hsl16_ish8 = color_end_hsl16_ish8 - color_start_hsl16_ish8;

    int step_color_hsl16_ish8 = 0;
    if( dx_stride > 3 )
        step_color_hsl16_ish8 = dcolor_hsl16_ish8 / dx_stride;

    if( x_end > stride_width )
        x_end = stride_width;

    if( x_start < 0 )
    {
        color_start_hsl16_ish8 -= step_color_hsl16_ish8 * x_start;
        x_start = 0;
    }

    if( x_start > x_end )
        return;

    int offset = x_start + y * stride_width;
    int steps = (x_end - x_start) >> 2;
    step_color_hsl16_ish8 <<= 2;

    int color_hsl16_ish8 = color_start_hsl16_ish8;
    while( --steps >= 0 )
    {
        int color_hsl16 = color_hsl16_ish8 >> 8;
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

        pixel_buffer[offset++] = rgb_color;
        pixel_buffer[offset++] = rgb_color;
        pixel_buffer[offset++] = rgb_color;
        pixel_buffer[offset++] = rgb_color;

        color_hsl16_ish8 += step_color_hsl16_ish8;
    }

    steps = (x_end - x_start) & 0x3;
    while( --steps >= 0 )
    {
        int color_hsl16 = color_hsl16_ish8 >> 8;
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];
        pixel_buffer[offset++] = rgb_color;
    }
}

void
raster_gouraud(
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

    // TODO: Remove this check for callers that cull correctly.
    if( total_height >= screen_height )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // TODO: Remove this check for callers that cull correctly.
    if( (x0 < 0 || x1 < 0 || x2 < 0) &&
        (x0 > screen_width || x1 > screen_width || x2 > screen_width) )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

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

    int i = y0;
    // if( i < 0 )
    // {
    //     edge_x_AC_ish16 -= step_edge_x_AC_ish16 * i;
    //     edge_x_AB_ish16 -= step_edge_x_AB_ish16 * i;

    //     edge_color_AC_ish15 -= step_edge_color_AC_ish15 * i;
    //     edge_color_AB_ish15 -= step_edge_color_AB_ish15 * i;

    //     i = 0;
    // }

    for( ; i < y1 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_AB_ish16 >> 16;

        // Get interpolated HSL16 colors
        int color_start_current = edge_color_AC_ish15 >> 7;
        int color_end_current = edge_color_AB_ish15 >> 7;

        //         17137664
        // colorC
        // 17432576

        // color_start_current = 311001088 >> 7;
        // color_end_current = 311336960 >> 7;

        if( i >= 0 )
        {
            draw_scanline_gouraud(
                pixel_buffer,
                screen_width,
                i,
                x_start_current,
                x_end_current,
                color_start_current,
                color_end_current);
        }
        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        edge_color_AC_ish15 += step_edge_color_AC_ish15;
        edge_color_AB_ish15 += step_edge_color_AB_ish15;
    }
    // if( i < 0 )
    // {
    //     edge_x_AC_ish16 -= step_edge_x_AC_ish16 * i;
    //     edge_x_BC_ish16 -= step_edge_x_BC_ish16 * i;

    //     edge_color_AC_ish15 -= step_edge_color_AC_ish15 * i;
    //     edge_color_BC_ish15 -= step_edge_color_BC_ish15 * i;

    //     i = 0;
    // }
    for( ; i < y2 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_BC_ish16 >> 16;

        // Get interpolated HSL16 colors
        int color_start_current = edge_color_AC_ish15 >> 7;
        int color_end_current = edge_color_BC_ish15 >> 7;

        if( i >= 0 )
            draw_scanline_gouraud(
                pixel_buffer,
                screen_width,
                i,
                x_start_current,
                x_end_current,
                color_start_current,
                color_end_current);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        edge_color_AC_ish15 += step_edge_color_AC_ish15;
        edge_color_BC_ish15 += step_edge_color_BC_ish15;
    }
}

void
raster_gouraud2(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0,
    int color1,
    int color2)
{
    // Sort vertices by y
    // where y0 is the bottom vertex and y2 is the top vertex
    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;

        temp = color0;
        color0 = color1;
        color1 = temp;
    }

    if( y1 > y2 )
    {
        int temp = y1;
        y1 = y2;
        y2 = temp;

        temp = x1;
        x1 = x2;
        x2 = temp;

        temp = color1;
        color1 = color2;
        color2 = temp;
    }

    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;

        temp = color0;
        color0 = color1;
        color1 = temp;
    }

    int total_height = y2 - y0;
    if( total_height == 0 )
        return;

    // TODO: Remove this check for callers that cull correctly.
    if( total_height >= screen_height )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // TODO: Remove this check for callers that cull correctly.
    if( (x0 < 0 || x1 < 0 || x2 < 0) &&
        (x0 > screen_width || x1 > screen_width || x2 > screen_width) )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // skip if the triangle is degenerate
    if( x0 == x1 && x1 == x2 )
        return;

    for( int i = 0; i < total_height; ++i )
    {
        /*
         *          /\      y2
         *         /  \
         *        /    \    y1 (second_half = true above)
         *       /   /
         *      / /  y0 (second_half = false)
         */
        bool second_half = i > (y1 - y0) || y1 == y0;
        int segment_height = second_half ? y2 - y1 : y1 - y0;
        if( segment_height == 0 )
            continue;

        int alpha_ish16 = (i << 16) / total_height;
        int beta_ish16 = ((i - (second_half ? y1 - y0 : 0)) << 16) / segment_height;

        int ax = interpolate_ish16(x0, x2, alpha_ish16);
        int bx = second_half ? interpolate_ish16(x1, x2, beta_ish16)
                             : interpolate_ish16(x0, x1, beta_ish16);

        int acolor = interpolate_color_rgba_ish16(color0, color2, alpha_ish16);
        int bcolor = second_half ? interpolate_color_rgba_ish16(color1, color2, beta_ish16)
                                 : interpolate_color_rgba_ish16(color0, color1, beta_ish16);

        int y = y0 + i;
        if( y >= 0 && y < screen_height )
        {
            if( ax < 0 )
                ax = 0;
            if( ax >= screen_width )
                ax = screen_width - 1;

            if( bx < 0 )
                bx = 0;
            if( bx >= screen_width )
                bx = screen_width - 1;

            draw_scanline_gouraud2(pixel_buffer, screen_width, y, ax, bx, acolor, bcolor);
        }
    }
}

void
raster_gouraud3(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0,
    int color1,
    int color2)
{
    // Sort vertices by y
    // where y0 is the bottom vertex and y2 is the top vertex
    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;

        temp = color0;
        color0 = color1;
        color1 = temp;
    }

    if( y1 > y2 )
    {
        int temp = y1;
        y1 = y2;
        y2 = temp;

        temp = x1;
        x1 = x2;
        x2 = temp;

        temp = color1;
        color1 = color2;
        color2 = temp;
    }

    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;

        temp = color0;
        color0 = color1;
        color1 = temp;
    }

    int total_height = y2 - y0;
    if( total_height == 0 )
        return;

    // TODO: Remove this check for callers that cull correctly.
    if( total_height >= screen_height )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // TODO: Remove this check for callers that cull correctly.
    if( (x0 < 0 || x1 < 0 || x2 < 0) &&
        (x0 > screen_width || x1 > screen_width || x2 > screen_width) )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // skip if the triangle is degenerate
    if( x0 == x1 && x1 == x2 )
        return;

    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;

    int dx_BC = x2 - x1;
    int dy_BC = y2 - y1;

    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;

    int step_edge_x_AB_ish16 = 0;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;

    int step_edge_x_BC_ish16 = 0;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    int step_edge_x_AC_ish16 = 0;
    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;

    int edge_x_AC_ish16 = x0 << 16;
    int edge_x_AB_ish16 = x0 << 16;
    int edge_x_BC_ish16 = x1 << 16;

    int edge_color_r_AC_ish15 = (color0 >> 16) << 15;
    int edge_color_g_AC_ish15 = ((color0 >> 8) & 0xFF) << 15;
    int edge_color_b_AC_ish15 = (color0 & 0xFF) << 15;

    int edge_color_r_AB_ish15 = (color0 >> 16) << 15;
    int edge_color_g_AB_ish15 = ((color0 >> 8) & 0xFF) << 15;
    int edge_color_b_AB_ish15 = (color0 & 0xFF) << 15;

    int edge_color_r_BC_ish15 = (color1 >> 16) << 15;
    int edge_color_g_BC_ish15 = ((color1 >> 8) & 0xFF) << 15;
    int edge_color_b_BC_ish15 = (color1 & 0xFF) << 15;

    int dcolor_AC_r = ((color2 >> 16) - (color0 >> 16));
    int dcolor_AC_g = ((color2 >> 8) & 0xFF) - ((color0 >> 8) & 0xFF);
    int dcolor_AC_b = (color2 & 0xFF) - (color0 & 0xFF);

    int dcolor_AB_r = ((color1 >> 16) - (color0 >> 16));
    int dcolor_AB_g = ((color1 >> 8) & 0xFF) - ((color0 >> 8) & 0xFF);
    int dcolor_AB_b = (color1 & 0xFF) - (color0 & 0xFF);

    int dcolor_BC_r = ((color2 >> 16) - (color1 >> 16));
    int dcolor_BC_g = ((color2 >> 8) & 0xFF) - ((color1 >> 8) & 0xFF);
    int dcolor_BC_b = (color2 & 0xFF) - (color1 & 0xFF);

    int step_edge_color_r_AC_ish15 = 0;
    if( dy_AC > 0 )
        step_edge_color_r_AC_ish15 = (dcolor_AC_r << 15) / dy_AC;

    int step_edge_color_g_AC_ish15 = 0;
    if( dy_AC > 0 )
        step_edge_color_g_AC_ish15 = (dcolor_AC_g << 15) / dy_AC;

    int step_edge_color_b_AC_ish15 = 0;
    if( dy_AC > 0 )
        step_edge_color_b_AC_ish15 = (dcolor_AC_b << 15) / dy_AC;

    int step_edge_color_r_AB_ish15 = 0;
    if( dy_AB > 0 )
        step_edge_color_r_AB_ish15 = (dcolor_AB_r << 15) / dy_AB;

    int step_edge_color_g_AB_ish15 = 0;
    if( dy_AB > 0 )
        step_edge_color_g_AB_ish15 = (dcolor_AB_g << 15) / dy_AB;

    int step_edge_color_b_AB_ish15 = 0;
    if( dy_AB > 0 )
        step_edge_color_b_AB_ish15 = (dcolor_AB_b << 15) / dy_AB;

    int step_edge_color_r_BC_ish15 = 0;
    if( dy_BC > 0 )
        step_edge_color_r_BC_ish15 = (dcolor_BC_r << 15) / dy_BC;

    int step_edge_color_g_BC_ish15 = 0;
    if( dy_BC > 0 )
        step_edge_color_g_BC_ish15 = (dcolor_BC_g << 15) / dy_BC;

    int step_edge_color_b_BC_ish15 = 0;
    if( dy_BC > 0 )
        step_edge_color_b_BC_ish15 = (dcolor_BC_b << 15) / dy_BC;

    int i = y0;

    for( ; i < y1 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_AB_ish16 >> 16;

        int color_start_r_current = edge_color_r_AC_ish15 >> 15;
        int color_start_g_current = edge_color_g_AC_ish15 >> 15;
        int color_start_b_current = edge_color_b_AC_ish15 >> 15;

        int color_end_r_current = edge_color_r_AB_ish15 >> 15;
        int color_end_g_current = edge_color_g_AB_ish15 >> 15;
        int color_end_b_current = edge_color_b_AB_ish15 >> 15;

        int color_start_current = (0xFF << 24) | (color_start_r_current << 16) |
                                  (color_start_g_current << 8) | color_start_b_current;
        int color_end_current = (0xFF << 24) | (color_end_r_current << 16) |
                                (color_end_g_current << 8) | color_end_b_current;

        if( i >= 0 )
            draw_scanline_gouraud3(
                pixel_buffer,
                screen_width,
                i,
                x_start_current,
                x_end_current,
                color_start_current,
                color_end_current);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        edge_color_r_AC_ish15 += step_edge_color_r_AC_ish15;
        edge_color_g_AC_ish15 += step_edge_color_g_AC_ish15;
        edge_color_b_AC_ish15 += step_edge_color_b_AC_ish15;

        edge_color_r_AB_ish15 += step_edge_color_r_AB_ish15;
        edge_color_g_AB_ish15 += step_edge_color_g_AB_ish15;
        edge_color_b_AB_ish15 += step_edge_color_b_AB_ish15;
    }

    for( ; i < y2 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_BC_ish16 >> 16;
        int x_end_current = edge_x_AC_ish16 >> 16;

        int color_start_r_current = edge_color_r_BC_ish15 >> 15;
        int color_start_g_current = edge_color_g_BC_ish15 >> 15;
        int color_start_b_current = edge_color_b_BC_ish15 >> 15;

        int color_end_r_current = edge_color_r_AC_ish15 >> 15;
        int color_end_g_current = edge_color_g_AC_ish15 >> 15;
        int color_end_b_current = edge_color_b_AC_ish15 >> 15;

        int color_start_current = (0xFF << 24) | (color_start_r_current << 16) |
                                  (color_start_g_current << 8) | color_start_b_current;
        int color_end_current = (0xFF << 24) | (color_end_r_current << 16) |
                                (color_end_g_current << 8) | color_end_b_current;

        if( i >= 0 )
            draw_scanline_gouraud3(
                pixel_buffer,
                screen_width,
                i,
                x_start_current,
                x_end_current,
                color_start_current,
                color_end_current);

        edge_x_BC_ish16 += step_edge_x_BC_ish16;
        edge_x_AC_ish16 += step_edge_x_AC_ish16;

        edge_color_r_BC_ish15 += step_edge_color_r_BC_ish15;
        edge_color_g_BC_ish15 += step_edge_color_g_BC_ish15;
        edge_color_b_BC_ish15 += step_edge_color_b_BC_ish15;

        edge_color_r_AC_ish15 += step_edge_color_r_AC_ish15;
        edge_color_g_AC_ish15 += step_edge_color_g_AC_ish15;
        edge_color_b_AC_ish15 += step_edge_color_b_AC_ish15;
    }
}

void gouraudRaster(int x0, int x1, int color0, int color1, int* dst, int offset, int length);

void
gouraudTriangle(
    int* pixels, int xA, int xB, int xC, int yA, int yB, int yC, int colorA, int colorB, int colorC)
{
    int sxa = xA;
    int sxb = xB;
    int sxc = xC;
    int dxAB = xB - xA;
    int dyAB = yB - yA;
    int dxAC = xC - xA;
    int dyAC = yC - yA;

    int xStepAB = 0;
    int colorStepAB = 0;
    if( yB != yA )
    {
        xStepAB = (dxAB << 16) / dyAB;
        colorStepAB = ((colorB - colorA) << 15) / dyAB;
    }

    int xStepBC = 0;
    int colorStepBC = 0;
    if( yC != yB )
    {
        xStepBC = ((xC - xB) << 16) / (yC - yB);
        colorStepBC = ((colorC - colorB) << 15) / (yC - yB);
    }

    int xStepAC = 0;
    int colorStepAC = 0;
    if( yC != yA )
    {
        xStepAC = ((xA - xC) << 16) / (yA - yC);
        colorStepAC = ((colorA - colorC) << 15) / (yA - yC);
    }

    // this won't change any rendering, saves not wasting time "drawing" an invalid triangle
    int triangleArea = (dxAB * dyAC) - (dyAB * dxAC);
    if( triangleArea == 0 )
    {
        return;
    }

    if( yA <= yB && yA <= yC )
    {
        if( yA < 800 )
        {
            if( yB > 800 )
            {
                yB = 800;
            }

            if( yC > 800 )
            {
                yC = 800;
            }

            if( yB < yC )
            {
                xC = xA <<= 16;
                colorC = colorA <<= 15;
                if( yA < 0 )
                {
                    xC -= xStepAC * yA;
                    xA -= xStepAB * yA;
                    colorC -= colorStepAC * yA;
                    colorA -= colorStepAB * yA;
                    yA = 0;
                }

                xB <<= 16;
                colorB <<= 15;
                if( yB < 0 )
                {
                    xB -= xStepBC * yB;
                    colorB -= colorStepBC * yB;
                    yB = 0;
                }

                if( (yA != yB && xStepAC < xStepAB) || (yA == yB && xStepAC > xStepBC) )
                {
                    yC -= yB;
                    yB -= yA;
                    yA = 800 * yA;

                    while( --yB >= 0 )
                    {
                        gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7, pixels, yA, 0);
                        xC += xStepAC;
                        xA += xStepAB;
                        colorC += colorStepAC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                    while( --yC >= 0 )
                    {
                        gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7, pixels, yA, 0);
                        xC += xStepAC;
                        xB += xStepBC;
                        colorC += colorStepAC;
                        colorB += colorStepBC;
                        yA += 800;
                    }
                }
                else
                {
                    yC -= yB;
                    yB -= yA;
                    yA = 800 * yA;

                    while( --yB >= 0 )
                    {
                        gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7, pixels, yA, 0);
                        xC += xStepAC;
                        xA += xStepAB;
                        colorC += colorStepAC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                    while( --yC >= 0 )
                    {
                        gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7, pixels, yA, 0);
                        xC += xStepAC;
                        xB += xStepBC;
                        colorC += colorStepAC;
                        colorB += colorStepBC;
                        yA += 800;
                    }
                }
            }
            else
            {
                xB = xA <<= 16;
                colorB = colorA <<= 15;
                if( yA < 0 )
                {
                    xB -= xStepAC * yA;
                    xA -= xStepAB * yA;
                    colorB -= colorStepAC * yA;
                    colorA -= colorStepAB * yA;
                    yA = 0;
                }

                xC <<= 16;
                colorC <<= 15;
                if( yC < 0 )
                {
                    xC -= xStepBC * yC;
                    colorC -= colorStepBC * yC;
                    yC = 0;
                }

                if( (yA != yC && xStepAC < xStepAB) || (yA == yC && xStepBC > xStepAB) )
                {
                    yB -= yC;
                    yC -= yA;
                    yA = 800 * yA;

                    while( --yC >= 0 )
                    {
                        gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7, pixels, yA, 0);
                        xB += xStepAC;
                        xA += xStepAB;
                        colorB += colorStepAC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                    while( --yB >= 0 )
                    {
                        gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7, pixels, yA, 0);
                        xC += xStepBC;
                        xA += xStepAB;
                        colorC += colorStepBC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                }
                else
                {
                    yB -= yC;
                    yC -= yA;
                    yA = 800 * yA;

                    while( --yC >= 0 )
                    {
                        gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7, pixels, yA, 0);
                        xB += xStepAC;
                        xA += xStepAB;
                        colorB += colorStepAC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                    while( --yB >= 0 )
                    {
                        gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7, pixels, yA, 0);
                        xC += xStepBC;
                        xA += xStepAB;
                        colorC += colorStepBC;
                        colorA += colorStepAB;
                        yA += 800;
                    }
                }
            }
        }
    }
    else if( yB <= yC )
    {
        if( yB < 800 )
        {
            if( yC > 800 )
            {
                yC = 800;
            }

            if( yA > 800 )
            {
                yA = 800;
            }

            if( yC < yA )
            {
                xA = xB <<= 16;
                colorA = colorB <<= 15;
                if( yB < 0 )
                {
                    xA -= xStepAB * yB;
                    xB -= xStepBC * yB;
                    colorA -= colorStepAB * yB;
                    colorB -= colorStepBC * yB;
                    yB = 0;
                }

                xC <<= 16;
                colorC <<= 15;
                if( yC < 0 )
                {
                    xC -= xStepAC * yC;
                    colorC -= colorStepAC * yC;
                    yC = 0;
                }

                if( (yB != yC && xStepAB < xStepBC) || (yB == yC && xStepAB > xStepAC) )
                {
                    yA -= yC;
                    yC -= yB;
                    yB = 800 * yB;

                    while( --yC >= 0 )
                    {
                        gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7, pixels, yB, 0);
                        xA += xStepAB;
                        xB += xStepBC;
                        colorA += colorStepAB;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                    while( --yA >= 0 )
                    {
                        gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7, pixels, yB, 0);
                        xA += xStepAB;
                        xC += xStepAC;
                        colorA += colorStepAB;
                        colorC += colorStepAC;
                        yB += 800;
                    }
                }
                else
                {
                    yA -= yC;
                    yC -= yB;
                    yB = 800 * yB;

                    while( --yC >= 0 )
                    {
                        gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7, pixels, yB, 0);
                        xA += xStepAB;
                        xB += xStepBC;
                        colorA += colorStepAB;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                    while( --yA >= 0 )
                    {
                        gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7, pixels, yB, 0);
                        xA += xStepAB;
                        xC += xStepAC;
                        colorA += colorStepAB;
                        colorC += colorStepAC;
                        yB += 800;
                    }
                }
            }
            else
            {
                xC = xB <<= 16;
                colorC = colorB <<= 15;
                if( yB < 0 )
                {
                    xC -= xStepAB * yB;
                    xB -= xStepBC * yB;
                    colorC -= colorStepAB * yB;
                    colorB -= colorStepBC * yB;
                    yB = 0;
                }

                xA <<= 16;
                colorA <<= 15;
                if( yA < 0 )
                {
                    xA -= xStepAC * yA;
                    colorA -= colorStepAC * yA;
                    yA = 0;
                }

                if( xStepAB < xStepBC )
                {
                    yC -= yA;
                    yA -= yB;
                    yB = 800 * yB;

                    while( --yA >= 0 )
                    {
                        gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7, pixels, yB, 0);
                        xC += xStepAB;
                        xB += xStepBC;
                        colorC += colorStepAB;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                    while( --yC >= 0 )
                    {
                        gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7, pixels, yB, 0);
                        xA += xStepAC;
                        xB += xStepBC;
                        colorA += colorStepAC;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                }
                else
                {
                    yC -= yA;
                    yA -= yB;
                    yB = 800 * yB;

                    while( --yA >= 0 )
                    {
                        gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7, pixels, yB, 0);
                        xC += xStepAB;
                        xB += xStepBC;
                        colorC += colorStepAB;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                    while( --yC >= 0 )
                    {
                        gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7, pixels, yB, 0);
                        xA += xStepAC;
                        xB += xStepBC;
                        colorA += colorStepAC;
                        colorB += colorStepBC;
                        yB += 800;
                    }
                }
            }
        }
    }
    else if( yC < 800 )
    {
        if( yA > 800 )
        {
            yA = 800;
        }

        if( yB > 800 )
        {
            yB = 800;
        }

        if( yA < yB )
        {
            xB = xC <<= 16;
            colorB = colorC <<= 15;
            if( yC < 0 )
            {
                xB -= xStepBC * yC;
                xC -= xStepAC * yC;
                colorB -= colorStepBC * yC;
                colorC -= colorStepAC * yC;
                yC = 0;
            }

            xA <<= 16;
            colorA <<= 15;
            if( yA < 0 )
            {
                xA -= xStepAB * yA;
                colorA -= colorStepAB * yA;
                yA = 0;
            }

            if( xStepBC < xStepAC )
            {
                yB -= yA;
                yA -= yC;
                yC = 800 * yC;

                while( --yA >= 0 )
                {
                    gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7, pixels, yC, 0);
                    xB += xStepBC;
                    xC += xStepAC;
                    colorB += colorStepBC;
                    colorC += colorStepAC;
                    yC += 800;
                }
                while( --yB >= 0 )
                {
                    gouraudRaster(xB >> 16, xA >> 16, colorB >> 7, colorA >> 7, pixels, yC, 0);
                    xB += xStepBC;
                    xA += xStepAB;
                    colorB += colorStepBC;
                    colorA += colorStepAB;
                    yC += 800;
                }
            }
            else
            {
                yB -= yA;
                yA -= yC;
                yC = 800 * yC;

                while( --yA >= 0 )
                {
                    gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7, pixels, yC, 0);
                    xB += xStepBC;
                    xC += xStepAC;
                    colorB += colorStepBC;
                    colorC += colorStepAC;
                    yC += 800;
                }
                while( --yB >= 0 )
                {
                    gouraudRaster(xA >> 16, xB >> 16, colorA >> 7, colorB >> 7, pixels, yC, 0);
                    xB += xStepBC;
                    xA += xStepAB;
                    colorB += colorStepBC;
                    colorA += colorStepAB;
                    yC += 800;
                }
            }
        }
        else
        {
            xA = xC <<= 16;
            colorA = colorC <<= 15;
            if( yC < 0 )
            {
                xA -= xStepBC * yC;
                xC -= xStepAC * yC;
                colorA -= colorStepBC * yC;
                colorC -= colorStepAC * yC;
                yC = 0;
            }

            xB <<= 16;
            colorB <<= 15;
            if( yB < 0 )
            {
                xB -= xStepAB * yB;
                colorB -= colorStepAB * yB;
                yB = 0;
            }

            if( xStepBC < xStepAC )
            {
                yA -= yB;
                yB -= yC;
                yC = 800 * yC;

                while( --yB >= 0 )
                {
                    gouraudRaster(xA >> 16, xC >> 16, colorA >> 7, colorC >> 7, pixels, yC, 0);
                    xA += xStepBC;
                    xC += xStepAC;
                    colorA += colorStepBC;
                    colorC += colorStepAC;
                    yC += 800;
                }
                while( --yA >= 0 )
                {
                    gouraudRaster(xB >> 16, xC >> 16, colorB >> 7, colorC >> 7, pixels, yC, 0);
                    xB += xStepAB;
                    xC += xStepAC;
                    colorB += colorStepAB;
                    colorC += colorStepAC;
                    yC += 800;
                }
            }
            else
            {
                yA -= yB;
                yB -= yC;
                yC = 800 * yC;

                while( --yB >= 0 )
                {
                    gouraudRaster(xC >> 16, xA >> 16, colorC >> 7, colorA >> 7, pixels, yC, 0);
                    xA += xStepBC;
                    xC += xStepAC;
                    colorA += colorStepBC;
                    colorC += colorStepAC;
                    yC += 800;
                }
                while( --yA >= 0 )
                {
                    gouraudRaster(xC >> 16, xB >> 16, colorC >> 7, colorB >> 7, pixels, yC, 0);
                    xB += xStepAB;
                    xC += xStepAC;
                    colorB += colorStepAB;
                    colorC += colorStepAC;
                    yC += 800;
                }
            }
        }
    }
}

void
gouraudRaster(int x0, int x1, int color0, int color1, int* dst, int offset, int length)
{
    int rgb;

    if( true )
    {
        int colorStep;

        if( x0 < 0 || x1 < 0 || x0 >= 800 || x1 >= 800 )
        {
            if( x1 - x0 > 3 )
            {
                colorStep = (color1 - color0) / (x1 - x0);
            }
            else
            {
                colorStep = 0;
            }

            if( x1 > 800 )
            {
                x1 = 800;
            }

            if( x0 < 0 )
            {
                color0 -= x0 * colorStep;
                x0 = 0;
            }

            if( x0 >= x1 )
            {
                return;
            }

            offset += x0;
            length = (x1 - x0) >> 2;
            colorStep <<= 2;
        }
        else if( x0 < x1 )
        {
            offset += x0;
            length = (x1 - x0) >> 2;

            if( length > 0 )
            {
                colorStep = (color1 - color0) / length;
            }
            else
            {
                colorStep = 0;
            }
        }
        else
        {
            return;
        }

        if( true )
        {
            while( --length >= 0 )
            {
                rgb = g_hsl16_to_rgb_table[color0 >> 8];
                color0 += colorStep;

                dst[offset++] = rgb;
                dst[offset++] = rgb;
                dst[offset++] = rgb;
                dst[offset++] = rgb;
            }

            length = (x1 - x0) & 0x3;
            if( length > 0 )
            {
                rgb = g_hsl16_to_rgb_table[color0 >> 8];

                while( --length >= 0 )
                {
                    dst[offset++] = rgb;
                }
            }
        }
    }
    else if( x0 < x1 )
    {
        int colorStep = (color1 - color0) / (x1 - x0);

        if( true )
        {
            if( x1 > 800 )
            {
                x1 = 800;
            }

            if( x0 < 0 )
            {
                color0 -= x0 * colorStep;
                x0 = 0;
            }

            if( x0 >= x1 )
            {
                return;
            }
        }

        offset += x0;
        length = x1 - x0;

        if( true )
        {
            while( --length >= 0 )
            {
                dst[offset++] = g_hsl16_to_rgb_table[color0 >> 8];
                color0 += colorStep;
            }
        }
    }
}
