#ifndef GOURAUD_U_C
#define GOURAUD_U_C

#include "alpha.h"

#include <assert.h>
extern int g_reciprocal16[2048];
#include <stdbool.h>

// clang-format off
#include "gouraud_simd_alpha.u.c"
#include "gouraud_barycentric.u.c"
// clang-format on

extern int g_hsl16_to_rgb_table[65536];

static inline void
draw_scanline_gouraud_s4(
    int* pixel_buffer,
    int screen_width,
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

    int dcolor_hsl16_ish8 = color_end_hsl16_ish8 - color_start_hsl16_ish8;

    int step_color_hsl16_ish8 = 0;
    if( dx_stride > 3 )
    {
        step_color_hsl16_ish8 = dcolor_hsl16_ish8 / dx_stride;
    }

    if( x_end >= screen_width )
    {
        x_end = screen_width - 1;
    }
    if( x_start < 0 )
    {
        color_start_hsl16_ish8 -= step_color_hsl16_ish8 * x_start;
        x_start = 0;
    }

    if( x_start >= x_end )
        return;

    dx_stride = x_end - x_start;

    // Steps by 4.
    int offset = x_start + y * screen_width;
    int steps = (dx_stride) >> 2;
    step_color_hsl16_ish8 <<= 2;

    int color_hsl16_ish8 = color_start_hsl16_ish8;
    while( steps > 0 )
    {
        int color_hsl16 = color_hsl16_ish8 >> 8;
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

        for( int i = 0; i < 4; i++ )
        {
            pixel_buffer[offset] = rgb_color;
            offset += 1;
        }

        color_hsl16_ish8 += step_color_hsl16_ish8;

        steps -= 1;
    }

    int rgb_color = g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8];
    steps = (dx_stride) & 0x3;
    switch( steps )
    {
    case 3:
        pixel_buffer[offset] = rgb_color;
        offset += 1;
    case 2:
        pixel_buffer[offset] = rgb_color;
        offset += 1;
    case 1:
        pixel_buffer[offset] = rgb_color;
    }
}

static inline void
draw_scanline_gouraud_alpha_s4(
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

    dx_stride = x_end - x_start;

    // Steps by 4.
    int offset = x_start + y * stride_width;
    int steps = (dx_stride) >> 2;
    step_color_hsl16_ish8 <<= 2;

    int color_hsl16_ish8 = color_start_hsl16_ish8;
    while( --steps >= 0 )
    {
        int color_hsl16 = color_hsl16_ish8 >> 8;
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

        // Tested in lumbridge church window, this is faster than the scalar version.
        raster_linear_alpha_s4((uint32_t*)pixel_buffer, offset, rgb_color, alpha);
        offset += 4;

        // Checked on 09/16/2025, clang does NOT vectorize this loop.
        // even with -O3
        // for( int i = 0; i < 4; i++ )
        // {
        //     int rgb_blend = pixel_buffer[offset];
        //     rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        //     pixel_buffer[offset] = rgb_blend;
        //     offset += 1;
        // }

        color_hsl16_ish8 += step_color_hsl16_ish8;
    }

    int rgb_color = g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8];
    int rgb_blend;
    steps = (dx_stride) & 0x3;
    switch( steps )
    {
    case 3:
        rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
        offset += 1;
    case 2:
        rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
        offset += 1;
    case 1:
        rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
    }
}

// #include "gouraud_deob.h"

static inline void
raster_gouraud_alpha_s4(
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

        draw_scanline_gouraud_alpha_s4(
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

        draw_scanline_gouraud_alpha_s4(
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

static inline void
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

        draw_scanline_gouraud_s4(
            pixel_buffer,
            screen_width,
            i,
            x_start_current,
            x_end_current,
            color_start_current,
            color_end_current);

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

        draw_scanline_gouraud_s4(
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

#endif