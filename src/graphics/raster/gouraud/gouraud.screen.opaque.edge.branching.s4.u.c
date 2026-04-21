#ifndef GOURAUD_SCREEN_OPAQUE_EDGE_BRANCHING_S4_U_C
#define GOURAUD_SCREEN_OPAQUE_EDGE_BRANCHING_S4_U_C

#include "graphics/dash_restrict.h"

#include <stdint.h>

extern int g_hsl16_to_rgb_table[65536];
extern int g_reciprocal15[4096];

/**
 * Tested on Mac M4.
 *
 * Branching (no per-triangle vertex sort) is slower than the sorting edge-walk
 * on P-cores; on E-cores it can be faster (e.g. low power mode).
 */
static inline void
draw_scanline_gouraud_screen_opaque_edge_branching_s4_ordered(
    int* RESTRICT pixel_buffer,
    int offset,
    int screen_width,
    int y,
    int x_start_ish16,
    int x_end_ish16,
    int color_start_hsl16_ish8,
    int color_end_hsl16_ish8)
{
    (void)y;

    if( x_start_ish16 == x_end_ish16 )
        return;

    int x_start = x_start_ish16 >> 16;
    int x_end = x_end_ish16 >> 16;

    if( x_start >= x_end )
        return;

    int dx_stride = x_end - x_start;

    int dcolor_hsl16_ish8 = color_end_hsl16_ish8 - color_start_hsl16_ish8;

    int step_color_hsl16_ish8 = 0;
    if( dx_stride > 3 )
    {
        step_color_hsl16_ish8 = ((dcolor_hsl16_ish8)*g_reciprocal15[dx_stride]) >> 15;
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

    offset += x_start;
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
raster_gouraud_screen_opaque_edge_branching_s4_ordered(
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
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16)
{
    if( y2 - y0 == 0 )
        return;

    if( x0 == x1 && x1 == x2 )
        return;

    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;
    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;
    int dx_BC = x2 - x1;
    int dy_BC = y2 - y1;

    int step_edge_x_AC_ish16;
    int step_edge_x_AB_ish16;
    int step_edge_x_BC_ish16;

    if( dy_AC > 0 )
    {
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    }
    else
        step_edge_x_AC_ish16 = 0;

    if( dy_AB > 0 )
    {
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    }
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

    int dcolor_AC = color2_hsl16 - color0_hsl16;
    int dcolor_AB = color1_hsl16 - color0_hsl16;
    int dcolor_BC = color2_hsl16 - color1_hsl16;

    int step_edge_color_AC_ish15;
    int step_edge_color_AB_ish15;
    int step_edge_color_BC_ish15;

    if( dy_AC > 0 )
    {
        step_edge_color_AC_ish15 = (dcolor_AC << 15) / dy_AC;
    }
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

    int offset = y0 * stride;

    if( y1 > screen_height )
    {
        y1 = screen_height;
        y2 = screen_height;
    }
    else if( y2 > screen_height )
    {
        y2 = screen_height;
    }

    if( (y0 == y1 && step_edge_x_AC_ish16 <= step_edge_x_BC_ish16) ||
        (y0 != y1 && step_edge_x_AC_ish16 >= step_edge_x_AB_ish16) )
    {
        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            draw_scanline_gouraud_screen_opaque_edge_branching_s4_ordered(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_AB_ish16,
                edge_x_AC_ish16,
                edge_color_AB_ish15 >> 7,
                edge_color_AC_ish15 >> 7);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            edge_color_AC_ish15 += step_edge_color_AC_ish15;
            edge_color_AB_ish15 += step_edge_color_AB_ish15;

            offset += stride;
        }

        while( y2-- > 0 )
        {
            draw_scanline_gouraud_screen_opaque_edge_branching_s4_ordered(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_BC_ish16,
                edge_x_AC_ish16,
                edge_color_BC_ish15 >> 7,
                edge_color_AC_ish15 >> 7);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            edge_color_AC_ish15 += step_edge_color_AC_ish15;
            edge_color_BC_ish15 += step_edge_color_BC_ish15;

            offset += stride;
        }
    }
    else
    {
        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            draw_scanline_gouraud_screen_opaque_edge_branching_s4_ordered(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_AC_ish16,
                edge_x_AB_ish16,
                edge_color_AC_ish15 >> 7,
                edge_color_AB_ish15 >> 7);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            edge_color_AC_ish15 += step_edge_color_AC_ish15;
            edge_color_AB_ish15 += step_edge_color_AB_ish15;

            offset += stride;
        }

        while( y2-- > 0 )
        {
            draw_scanline_gouraud_screen_opaque_edge_branching_s4_ordered(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_AC_ish16,
                edge_x_BC_ish16,
                edge_color_AC_ish15 >> 7,
                edge_color_BC_ish15 >> 7);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            edge_color_AC_ish15 += step_edge_color_AC_ish15;
            edge_color_BC_ish15 += step_edge_color_BC_ish15;

            offset += stride;
        }
    }
}

static inline void
raster_gouraud_screen_opaque_edge_branching_s4(
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
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16)
{
    if( y0 <= y1 && y0 <= y2 )
    {
        if( y0 >= screen_height )
            return;

        if( y1 <= y2 )
        {
            if( y2 < 0 || y0 >= screen_height )
                return;

            raster_gouraud_screen_opaque_edge_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x0,
                x1,
                x2,
                y0,
                y1,
                y2,
                color0_hsl16,
                color1_hsl16,
                color2_hsl16);
        }
        else
        {
            if( y1 < 0 || y0 >= screen_height )
                return;

            raster_gouraud_screen_opaque_edge_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x0,
                x2,
                x1,
                y0,
                y2,
                y1,
                color0_hsl16,
                color2_hsl16,
                color1_hsl16);
        }
    }
    else if( y1 <= y2 )
    {
        if( y1 >= screen_height )
            return;

        if( y2 <= y0 )
        {
            if( y0 < 0 || y1 >= screen_height )
                return;

            raster_gouraud_screen_opaque_edge_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x1,
                x2,
                x0,
                y1,
                y2,
                y0,
                color1_hsl16,
                color2_hsl16,
                color0_hsl16);
        }
        else
        {
            if( y2 < 0 || y1 >= screen_height )
                return;

            raster_gouraud_screen_opaque_edge_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x1,
                x0,
                x2,
                y1,
                y0,
                y2,
                color1_hsl16,
                color0_hsl16,
                color2_hsl16);
        }
    }
    else
    {
        if( y2 >= screen_height )
            return;

        if( y0 <= y1 )
        {
            if( y1 < 0 || y2 >= screen_height )
                return;

            raster_gouraud_screen_opaque_edge_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x2,
                x0,
                x1,
                y2,
                y0,
                y1,
                color2_hsl16,
                color0_hsl16,
                color1_hsl16);
        }
        else
        {
            if( y0 < 0 || y2 >= screen_height )
                return;

            raster_gouraud_screen_opaque_edge_branching_s4_ordered(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                x2,
                x1,
                x0,
                y2,
                y1,
                y0,
                color2_hsl16,
                color1_hsl16,
                color0_hsl16);
        }
    }
}

#endif
