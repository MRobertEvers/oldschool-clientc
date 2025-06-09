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
draw_scanline_gouraud(
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

            draw_scanline_gouraud(pixel_buffer, screen_width, y, ax, bx, acolor, bcolor);
        }
    }
}