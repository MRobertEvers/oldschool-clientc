#ifndef FLAT_U_C
#define FLAT_U_C

#include "alpha.h"

#include <assert.h>

extern int g_hsl16_to_rgb_table[65536];

static inline void
draw_scanline_flat_s4(
    int* pixel_buffer, int stride_width, int y, int x_start, int x_end, int color_hsl16)
{
    if( x_start == x_end )
        return;
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
    }

    int dx_stride = x_end - x_start;
    assert(dx_stride > 0);

    if( x_end > stride_width )
        x_end = stride_width;

    if( x_start < 0 )
        x_start = 0;

    if( x_start > x_end )
        return;

    // Steps by 4.
    int offset = x_start + y * stride_width;
    int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

    int steps = dx_stride >> 2;
    while( --steps >= 0 )
    {
        pixel_buffer[offset++] = rgb_color;
        pixel_buffer[offset++] = rgb_color;
        pixel_buffer[offset++] = rgb_color;
        pixel_buffer[offset++] = rgb_color;
    }

    steps = dx_stride & 0x3;
    switch( steps )
    {
    case 3:
        pixel_buffer[offset++] = rgb_color;
    case 2:
        pixel_buffer[offset++] = rgb_color;
    case 1:
        pixel_buffer[offset] = rgb_color;
    }
}

void
raster_flat_s4(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color_hsl16)
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
    }

    if( y1 > y2 )
    {
        int temp = y1;
        y1 = y2;
        y2 = temp;

        temp = x1;
        x1 = x2;
        x2 = temp;
    }

    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;
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

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;

        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;

        y1 = 0;
    }

    int i = y0;
    for( ; i < y1 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_AB_ish16 >> 16;

        draw_scanline_flat_s4(
            pixel_buffer, screen_width, i, x_start_current, x_end_current, color_hsl16);
        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;
    }

    if( y1 > y2 )
        return;

    i = y1;
    for( ; i < y2 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_BC_ish16 >> 16;

        draw_scanline_flat_s4(
            pixel_buffer, screen_width, i, x_start_current, x_end_current, color_hsl16);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;
    }
}

static inline void
draw_scanline_flat_alpha_s4(
    int* pixel_buffer, int stride_width, int y, int x_start, int x_end, int color_hsl16, int alpha)
{
    if( x_start == x_end )
        return;
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
    }

    int dx_stride = x_end - x_start;
    assert(dx_stride > 0);

    if( x_end > stride_width )
        x_end = stride_width;

    if( x_start < 0 )
        x_start = 0;

    if( x_start > x_end )
        return;

    // Steps by 4.
    int offset = x_start + y * stride_width;
    int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

    int steps = (x_end - x_start) >> 2;
    int rgb_blend;
    while( --steps >= 0 )
    {
        rgb_blend = pixel_buffer[offset];
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
    }

    steps = (x_end - x_start) & 0x3;
    while( --steps >= 0 )
    {
        rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset++] = rgb_blend;
    }
}

void
raster_flat_alpha_s4(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color_hsl16,
    int alpha)
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
    }

    if( y1 > y2 )
    {
        int temp = y1;
        y1 = y2;
        y2 = temp;

        temp = x1;
        x1 = x2;
        x2 = temp;
    }

    if( y0 > y1 )
    {
        int temp = y0;
        y0 = y1;
        y1 = temp;

        temp = x0;
        x0 = x1;
        x1 = temp;
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

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;

        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;

        y1 = 0;
    }

    int i = y0;
    for( ; i < y1 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_AB_ish16 >> 16;

        draw_scanline_flat_alpha_s4(
            pixel_buffer, screen_width, i, x_start_current, x_end_current, color_hsl16, alpha);
        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;
    }

    if( y1 > y2 )
        return;

    i = y1;
    for( ; i < y2 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_BC_ish16 >> 16;

        draw_scanline_flat_alpha_s4(
            pixel_buffer, screen_width, i, x_start_current, x_end_current, color_hsl16, alpha);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;
    }
}

#endif