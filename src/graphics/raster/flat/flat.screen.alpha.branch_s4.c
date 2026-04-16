#ifndef FLAT_SCREEN_ALPHA_BRANCH_S4_C
#define FLAT_SCREEN_ALPHA_BRANCH_S4_C

#include "graphics/dash_restrict.h"
#include "graphics/alpha.h"

#include <assert.h>
#include <stdint.h>

extern int g_hsl16_to_rgb_table[65536];

static inline void
draw_scanline_flat_alpha_ordered_bs4(
    int* RESTRICT pixel_buffer,
    int offset,
    int screen_width,
    int x_start_ish16,
    int x_end_ish16,
    int color_hsl16,
    int alpha)
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

    int span = (x_end - x_start);
    assert(span > 0);

    int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

    int steps = (span) >> 2;
    while( steps-- > 0 )
    {
        for( int i = 0; i < 4; i++ )
        {
            int rgb_blend = pixel_buffer[offset];
            rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
            pixel_buffer[offset] = rgb_blend;
            offset += 1;
        }
    }

    switch( (span) & 0x3 )
    {
    case 3:
    {
        int rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
        offset += 1;
    }
    case 2:
    {
        int rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
        offset += 1;
    }
    case 1:
    {
        int rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
    }
    }
}

static inline void
raster_flat_alpha_ordered_bs4(
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
    int color_hsl16,
    int alpha)
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

    int step_edge_x_AC_ish16;
    int step_edge_x_AB_ish16;
    int step_edge_x_BC_ish16;

    if( dy_AC > 0 && dy_AC < 4096 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    else
        step_edge_x_AC_ish16 = 0;

    if( dy_AB > 0 && dy_AB < 4096 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    else
        step_edge_x_AB_ish16 = 0;

    if( y2 != y1 && y2 - y1 < 4096 )
        step_edge_x_BC_ish16 = ((x2 - x1) << 16) / (y2 - y1);
    else
        step_edge_x_BC_ish16 = 0;

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

    int offset = y0 * stride;

    if( y1 >= screen_height )
    {
        y1 = screen_height - 1;
        y2 = screen_height - 1;
    }
    else if( y2 >= screen_height )
    {
        y2 = screen_height - 1;
    }

    if( (y0 == y1 && step_edge_x_AC_ish16 <= step_edge_x_BC_ish16) ||
        (y0 != y1 && step_edge_x_AC_ish16 >= step_edge_x_AB_ish16) )
    {
        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            draw_scanline_flat_alpha_ordered_bs4(
                pixel_buffer,
                offset,
                screen_width,
                edge_x_AB_ish16,
                edge_x_AC_ish16,
                color_hsl16,
                alpha);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            offset += stride;
        }

        while( y2-- > 0 )
        {
            draw_scanline_flat_alpha_ordered_bs4(
                pixel_buffer,
                offset,
                screen_width,
                edge_x_BC_ish16,
                edge_x_AC_ish16,
                color_hsl16,
                alpha);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            offset += stride;
        }
    }
    else
    {
        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            draw_scanline_flat_alpha_ordered_bs4(
                pixel_buffer,
                offset,
                screen_width,
                edge_x_AC_ish16,
                edge_x_AB_ish16,
                color_hsl16,
                alpha);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            offset += stride;
        }

        while( y2-- > 0 )
        {
            draw_scanline_flat_alpha_ordered_bs4(
                pixel_buffer,
                offset,
                screen_width,
                edge_x_AC_ish16,
                edge_x_BC_ish16,
                color_hsl16,
                alpha);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            offset += stride;
        }
    }
}

static inline void
raster_flat_alpha_bs4(
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
    int color_hsl16,
    int alpha)
{
    if( y0 <= y1 && y0 <= y2 )
    {
        if( y0 >= screen_height )
            return;

        if( y1 <= y2 )
        {
            if( y2 < 0 || y0 >= screen_height )
                return;

            raster_flat_alpha_ordered_bs4(
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
                color_hsl16,
                alpha);
        }
        else
        {
            if( y1 < 0 || y0 >= screen_height )
                return;

            raster_flat_alpha_ordered_bs4(
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
                color_hsl16,
                alpha);
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

            raster_flat_alpha_ordered_bs4(
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
                color_hsl16,
                alpha);
        }
        else
        {
            if( y2 < 0 || y1 >= screen_height )
                return;

            raster_flat_alpha_ordered_bs4(
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
                color_hsl16,
                alpha);
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

            raster_flat_alpha_ordered_bs4(
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
                color_hsl16,
                alpha);
        }
        else
        {
            if( y0 < 0 || y2 >= screen_height )
                return;

            raster_flat_alpha_ordered_bs4(
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
                color_hsl16,
                alpha);
        }
    }
}

#endif
