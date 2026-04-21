#ifndef FLAT_SCREEN_OPAQUE_SORT_S4_U_C
#define FLAT_SCREEN_OPAQUE_SORT_S4_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/flat/flat_screen_edges.h"

#include <assert.h>
#include <stdint.h>

extern int g_hsl16_to_rgb_table[65536];

static inline void
draw_scanline_flat_screen_opaque_sort_s4_clipped(
    int* RESTRICT pixel_buffer,
    int row_off,
    int screen_width,
    int x_start,
    int x_end,
    int rgb_color)
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

    if( x_end >= screen_width )
        x_end = screen_width - 1;

    if( x_start < 0 )
        x_start = 0;

    if( x_start >= x_end )
        return;

    int dx_stride = x_end - x_start;
    assert(dx_stride > 0);

    int offset = row_off + x_start;

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

static inline void
draw_scanline_flat_screen_opaque_sort_s4_noclip(
    int* RESTRICT pixel_buffer,
    int row_off,
    int x_start,
    int x_end,
    int rgb_color)
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

    if( x_start >= x_end )
        return;

    int dx_stride = x_end - x_start;
    assert(dx_stride > 0);

    int offset = row_off + x_start;

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

static inline void
raster_flat_screen_opaque_sort_s4(
    int* RESTRICT pixel_buffer,
    int stride,
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

    int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

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

    int seg1_count = 0;
    if( y0 < screen_height && y1 > y0 )
        seg1_count = (y1 < screen_height) ? (y1 - y0) : (screen_height - y0);

    int noclip_s1 = flat_screen_fixed_edges_no_hclip(
        edge_x_AC_ish16,
        step_edge_x_AC_ish16,
        edge_x_AB_ish16,
        step_edge_x_AB_ish16,
        seg1_count,
        screen_width);

    int seg2_count = 0;
    if( y1 < screen_height && y2 > y1 )
        seg2_count = (y2 < screen_height) ? (y2 - y1) : (screen_height - y1);

    /* BC is not stepped during the first trapezoid; AC is. */
    int ac_at_second_half = edge_x_AC_ish16 + seg1_count * step_edge_x_AC_ish16;
    int noclip_s2 = flat_screen_fixed_edges_no_hclip(
        ac_at_second_half,
        step_edge_x_AC_ish16,
        edge_x_BC_ish16,
        step_edge_x_BC_ish16,
        seg2_count,
        screen_width);

    int row_off = y0 * stride;

    int i = y0;
    for( ; i < y1 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_AB_ish16 >> 16;

        if( noclip_s1 )
        {
            draw_scanline_flat_screen_opaque_sort_s4_noclip(
                pixel_buffer, row_off, x_start_current, x_end_current, rgb_color);
        }
        else
        {
            draw_scanline_flat_screen_opaque_sort_s4_clipped(
                pixel_buffer, row_off, screen_width, x_start_current, x_end_current, rgb_color);
        }
        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;
        row_off += stride;
    }

    if( y1 > y2 )
        return;

    i = y1;
    row_off = y1 * stride;
    for( ; i < y2 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_BC_ish16 >> 16;

        if( noclip_s2 )
        {
            draw_scanline_flat_screen_opaque_sort_s4_noclip(
                pixel_buffer, row_off, x_start_current, x_end_current, rgb_color);
        }
        else
        {
            draw_scanline_flat_screen_opaque_sort_s4_clipped(
                pixel_buffer, row_off, screen_width, x_start_current, x_end_current, rgb_color);
        }

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;
        row_off += stride;
    }
}

#endif
