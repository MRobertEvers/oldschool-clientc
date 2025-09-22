#ifndef GOURAUD_BRANCHING_BARYCENTRIC_C
#define GOURAUD_BRANCHING_BARYCENTRIC_C

#include <assert.h>
#include <stdbool.h>
extern int g_hsl16_to_rgb_table[65536];
extern int g_reciprocal16[4096];

/**
 * Tested on Mac M4.
 *
 * This is slower than the "sorting" version on P-Cores.
 * For E-Cores, this is faster. E.g. Running in lower power mode.
 */
static inline void
draw_scanline_gouraud_ordered_bary_bs4(
    int* pixel_buffer,
    int offset,
    int screen_width,
    int y,
    int x_start_ish16,
    int x_end_ish16,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8)
{
    if( x_start_ish16 == x_end_ish16 )
        return;

    int x_end = x_end_ish16 >> 16;

    if( x_end >= screen_width )
        x_end = screen_width - 1;

    int x_start = x_start_ish16 >> 16;
    if( x_start < 0 )
        x_start = 0;

    if( x_start >= x_end )
        return;

    offset += x_start;
    color_hsl16_ish8 += x_start * color_step_hsl16_ish8;

    int stride = (x_end - x_start);

    int steps = (stride) >> 2;
    color_step_hsl16_ish8 <<= 2;

    while( steps > 0 )
    {
        int color_hsl16 = color_hsl16_ish8 >> 8;
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

        for( int i = 0; i < 4; i++ )
        {
            pixel_buffer[offset] = rgb_color;
            offset += 1;
        }

        color_hsl16_ish8 += color_step_hsl16_ish8;

        steps -= 1;
    }

    int rgb_color = g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8];
    steps = (stride) & 0x3;
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
raster_gouraud_ordered_bary_bs4(
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
    if( y2 - y0 == 0 )
        return;

    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;
    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;

    int sarea = dx_AB * dy_AC - dx_AC * dy_AB;
    if( sarea == 0 )
        return;

    int d_hsl_AB = color1_hsl16 - color0_hsl16;
    int d_hsl_AC = color2_hsl16 - color0_hsl16;

    /**
     * This is derived from a barycentric coordinate.
     */
    int step_x_hsl_ish8 = ((d_hsl_AB * dy_AC - d_hsl_AC * dy_AB) << 8) / sarea;
    int step_y_hsl_ish8 = ((d_hsl_AC * dx_AB - d_hsl_AB * dx_AC) << 8) / sarea;

    int step_edge_x_AC_ish16;
    int step_edge_x_AB_ish16;
    int step_edge_x_BC_ish16;

    /**
     * Attention! This relies on the reciprocol table, and that triangles that
     * are too big are already clipped away.
     */
    if( dy_AC > 0 )
    {
        // step_edge_x_AC_ish16 = (dx_AC)*g_reciprocal16[dy_AC];

        assert(dy_AC < 4096);
        // step_edge_x_AC_ish16 = (dx_AC)*g_reciprocal16[dy_AC];
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    }
    else
        step_edge_x_AC_ish16 = 0;

    if( dy_AB > 0 )
    {
        assert(dy_AB < 4096);
        // step_edge_x_AB_ish16 = (dx_AB)*g_reciprocal16[dy_AB];
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    }
    else
        step_edge_x_AB_ish16 = 0;

    if( y2 != y1 )
    {
        // assert(y2 - y1 < 4096);
        // step_edge_x_BC_ish16 = ((x2 - x1)) * g_reciprocal16[y2 - y1];
        step_edge_x_BC_ish16 = ((x2 - x1) << 16) / (y2 - y1);
    }
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

    int hsl_ish8 = step_x_hsl_ish8 + (color0_hsl16 << 8) - (x0 * step_x_hsl_ish8);

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;

        hsl_ish8 -= step_y_hsl_ish8 * y0;

        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;

        y1 = 0;
    }

    int offset = y0 * screen_width;

    if( y1 >= screen_height )
    {
        y1 = screen_height - 1;
        y2 = screen_height - 1;
    }

    if( (y0 == y1 && step_edge_x_AC_ish16 <= step_edge_x_BC_ish16) ||
        (y0 != y1 && step_edge_x_AC_ish16 >= step_edge_x_AB_ish16) )
    {
        y2 -= y1;
        y1 -= y0;

        while( --y1 > 0 )
        {
            draw_scanline_gouraud_ordered_bary_bs4(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_AB_ish16,
                edge_x_AC_ish16,
                hsl_ish8,
                step_x_hsl_ish8);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            hsl_ish8 += step_y_hsl_ish8;

            offset += screen_width;
        }

        while( --y2 > 0 )
        {
            draw_scanline_gouraud_ordered_bary_bs4(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_BC_ish16,
                edge_x_AC_ish16,
                hsl_ish8,
                step_x_hsl_ish8);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            hsl_ish8 += step_y_hsl_ish8;
            offset += screen_width;
        }
    }
    else
    {
        y2 -= y1;
        y1 -= y0;

        while( --y1 > 0 )
        {
            // if( i >= screen_height )
            //     break;

            draw_scanline_gouraud_ordered_bary_bs4(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_AC_ish16,
                edge_x_AB_ish16,
                hsl_ish8,
                step_x_hsl_ish8);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            hsl_ish8 += step_y_hsl_ish8;
            offset += screen_width;
        }

        while( --y2 > 0 )
        {
            draw_scanline_gouraud_ordered_bary_bs4(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_AC_ish16,
                edge_x_BC_ish16,
                hsl_ish8,
                step_x_hsl_ish8);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            hsl_ish8 += step_y_hsl_ish8;
            offset += screen_width;
        }
    }
}

static inline void
raster_gouraud_bary_bs4(
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
            if( y2 < 0 || y0 >= screen_height )
                return;

            raster_gouraud_ordered_bary_bs4(
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
            if( y1 < 0 || y0 >= screen_height )
                return;

            raster_gouraud_ordered_bary_bs4(
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
            if( y0 < 0 || y1 >= screen_height )
                return;

            raster_gouraud_ordered_bary_bs4(
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
            if( y2 < 0 || y1 >= screen_height )
                return;

            raster_gouraud_ordered_bary_bs4(
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
            if( y1 < 0 || y2 >= screen_height )
                return;

            raster_gouraud_ordered_bary_bs4(
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
            if( y0 < 0 || y2 >= screen_height )
                return;

            raster_gouraud_ordered_bary_bs4(
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