#ifndef GOURAUD_BRANCHING_C
#define GOURAUD_BRANCHING_C

#include <assert.h>
#include <stdbool.h>
extern int g_hsl16_to_rgb_table[65536];

static inline void
draw_scanline_gouraud_ordered_bs4(
    int* pixel_buffer,
    int offset,
    int screen_width,
    int y,
    int x_start,
    int x_end,
    int color_start_hsl16_ish15,
    int color_end_hsl16_ish15)
{
    if( x_start == x_end )
        return;
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
        tmp = color_start_hsl16_ish15;
        color_start_hsl16_ish15 = color_end_hsl16_ish15;
        color_end_hsl16_ish15 = tmp;
    }

    int dx_stride = x_end - x_start;
    assert(dx_stride > 0);

    int dcolor_hsl16_ish15 = color_end_hsl16_ish15 - color_start_hsl16_ish15;

    int step_color_hsl16_ish15 = 0;
    if( dx_stride > 3 )
    {
        step_color_hsl16_ish15 = dcolor_hsl16_ish15 / dx_stride;
    }

    if( x_end >= screen_width )
    {
        x_end = screen_width - 1;
    }
    if( x_start < 0 )
    {
        color_start_hsl16_ish15 -= step_color_hsl16_ish15 * x_start;
        x_start = 0;
    }

    if( x_start >= x_end )
        return;

    dx_stride = x_end - x_start;
    offset += x_start;

    int steps = (dx_stride) >> 2;
    step_color_hsl16_ish15 <<= 2;

    int color_hsl16_ish15 = color_start_hsl16_ish15;
    while( --steps >= 0 )
    {
        int color_hsl16 = color_hsl16_ish15 >> 15;
        assert(color_hsl16 >= 0 && color_hsl16 < 65536);
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

        for( int i = 0; i < 4; i++ )
        {
            pixel_buffer[offset] = rgb_color;
            offset += 1;
        }

        color_hsl16_ish15 += step_color_hsl16_ish15;
    }

    int rgb_color = g_hsl16_to_rgb_table[color_hsl16_ish15 >> 15];
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
raster_gouraud_ordered_bs4(
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
    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;
    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;

    int sarea = dx_AB * dy_AC - dx_AC * dy_AB;
    if( sarea == 0 )
        return;

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

    // // Interpolate HSL16 colors directly
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

    int offset = y0 * screen_width;

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

    if( (y0 == y1 && step_edge_x_AC_ish16 <= step_edge_x_BC_ish16) ||
        (y0 != y1 && step_edge_x_AC_ish16 >= step_edge_x_AB_ish16) )
    {
        int i = y0;

        for( ; i < y1; ++i )
        {
            draw_scanline_gouraud_ordered_bs4(
                pixel_buffer,
                offset,
                screen_width,
                i,
                edge_x_AB_ish16 >> 16,
                edge_x_AC_ish16 >> 16,
                edge_color_AC_ish15,
                edge_color_AB_ish15);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            edge_color_AC_ish15 += step_edge_color_AC_ish15;
            edge_color_AB_ish15 += step_edge_color_AB_ish15;
            offset += screen_width;
        }

        for( ; i < y2; ++i )
        {
            draw_scanline_gouraud_ordered_bs4(
                pixel_buffer,
                offset,
                screen_width,
                i,
                edge_x_BC_ish16 >> 16,
                edge_x_AC_ish16 >> 16,
                edge_color_AC_ish15,
                edge_color_BC_ish15);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            edge_color_AC_ish15 += step_edge_color_AC_ish15;
            edge_color_BC_ish15 += step_edge_color_BC_ish15;
            offset += screen_width;
        }
    }
    else
    // step_edge_color_AC_ish15 < step_edge_color_AB_ish15
    {
        int i = y0;
        for( ; i < y1; ++i )
        {
            assert(edge_x_AC_ish16 >> 16 <= edge_x_AB_ish16 >> 16);
            draw_scanline_gouraud_ordered_bs4(
                pixel_buffer,
                offset,
                screen_width,
                i,
                edge_x_AC_ish16 >> 16,
                edge_x_AB_ish16 >> 16,
                edge_color_AC_ish15,
                edge_color_AB_ish15);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            edge_color_AC_ish15 += step_edge_color_AC_ish15;
            edge_color_AB_ish15 += step_edge_color_AB_ish15;
            offset += screen_width;
        }

        for( ; i < y2; ++i )
        {
            assert(edge_x_AC_ish16 >> 16 <= edge_x_BC_ish16 >> 16);
            draw_scanline_gouraud_ordered_bs4(
                pixel_buffer,
                offset,
                screen_width,
                i,
                edge_x_AC_ish16 >> 16,
                edge_x_BC_ish16 >> 16,
                edge_color_AC_ish15,
                edge_color_BC_ish15);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            edge_color_AC_ish15 += step_edge_color_AC_ish15;
            edge_color_BC_ish15 += step_edge_color_BC_ish15;
            offset += screen_width;
        }
    }
}

static inline void
raster_gouraud_bs4(
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
    // either.
    // y0, y1, y2,
    // y0, y2, y1,
    // y1, y0, y2,
    // y1, y2, y0,
    // y2, y0, y1,
    // y2, y1, y0,
    if( y0 <= y1 && y0 <= y2 )
    {
        if( y0 >= screen_height )
            return;

        // y0, y1, y2,
        if( y1 <= y2 )
        {
            if( y2 < 0 )
                return;

            if( y1 >= screen_height )
                y1 = screen_height - 1;

            if( y2 >= screen_height )
                y2 = screen_height - 1;

            raster_gouraud_ordered_bs4(
                pixel_buffer,
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
        // y0, y2, y1,
        else
        {
            if( y1 < 0 )
                return;

            if( y1 >= screen_height )
                y1 = screen_height - 1;

            if( y2 >= screen_height )
                y2 = screen_height - 1;

            raster_gouraud_ordered_bs4(
                pixel_buffer,
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

        // y1, y2, y0
        if( y2 <= y0 )
        {
            if( y0 < 0 )
                return;

            if( y0 >= screen_height )
                y0 = screen_height - 1;

            if( y2 >= screen_height )
                y2 = screen_height - 1;

            raster_gouraud_ordered_bs4(
                pixel_buffer,
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
        // y1, y0, y2,
        else
        {
            if( y2 < 0 )
                return;

            if( y0 >= screen_height )
                y0 = screen_height - 1;

            if( y2 >= screen_height )
                y2 = screen_height - 1;

            raster_gouraud_ordered_bs4(
                pixel_buffer,
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

        // y2, y0, y1,
        if( y0 <= y1 )
        {
            if( y1 < 0 )
                return;

            if( y1 >= screen_height )
                y1 = screen_height - 1;

            if( y0 >= screen_height )
                y0 = screen_height - 1;

            raster_gouraud_ordered_bs4(
                pixel_buffer,
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
        // y2, y1, y0,
        else
        {
            if( y0 < 0 )
                return;

            if( y1 >= screen_height )
                y1 = screen_height - 1;

            if( y0 >= screen_height )
                y0 = screen_height - 1;

            raster_gouraud_ordered_bs4(
                pixel_buffer,
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