#ifndef GOURAUD_SCREEN_ALPHA_BARY_BRANCHING_S4_C
#define GOURAUD_SCREEN_ALPHA_BARY_BRANCHING_S4_C

#include "graphics/alpha.h"
#include "graphics/dash_restrict.h"
#include "graphics/raster/gouraud/gouraud_barycentric_steps.h"

extern int g_hsl16_to_rgb_table[65536];

static inline void
draw_scanline_gouraud_screen_alpha_bary_branching_s4_ordered(
    int* RESTRICT pixel_buffer,
    int offset,
    int screen_width,
    int y,
    int x_start_ish16,
    int x_end_ish16,
    int color_hsl16_ish8,
    int color_step_hsl16_ish8,
    int alpha)
{
    (void)y;

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

    while( steps-- > 0 )
    {
        int color_hsl16 = color_hsl16_ish8 >> 8;
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];

        for( int i = 0; i < 4; i++ )
        {
            int rgb_blend = pixel_buffer[offset];
            rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
            pixel_buffer[offset] = rgb_blend;
            offset += 1;
        }

        color_hsl16_ish8 += color_step_hsl16_ish8;
    }

    int rgb_color = g_hsl16_to_rgb_table[color_hsl16_ish8 >> 8];
    switch( (stride) & 0x3 )
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
raster_gouraud_screen_alpha_bary_branching_s4_ordered(
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
    int color2_hsl16,
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

    int d_hsl_AB = color1_hsl16 - color0_hsl16;
    int d_hsl_AC = color2_hsl16 - color0_hsl16;

    int step_x_hsl_ish8 =
        gouraud_barycentric_hsl_step_ish8(d_hsl_AB * dy_AC - d_hsl_AC * dy_AB, sarea);
    int step_y_hsl_ish8 =
        gouraud_barycentric_hsl_step_ish8(d_hsl_AC * dx_AB - d_hsl_AB * dx_AC, sarea);

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

    if( y2 != y1 )
        step_edge_x_BC_ish16 = ((x2 - x1) << 16) / (y2 - y1);
    else
        step_edge_x_BC_ish16 = 0;

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
            draw_scanline_gouraud_screen_alpha_bary_branching_s4_ordered(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_AB_ish16,
                edge_x_AC_ish16,
                hsl_ish8,
                step_x_hsl_ish8,
                alpha);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            hsl_ish8 += step_y_hsl_ish8;

            offset += stride;
        }

        while( y2-- > 0 )
        {
            draw_scanline_gouraud_screen_alpha_bary_branching_s4_ordered(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_BC_ish16,
                edge_x_AC_ish16,
                hsl_ish8,
                step_x_hsl_ish8,
                alpha);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            hsl_ish8 += step_y_hsl_ish8;
            offset += stride;
        }
    }
    else
    {
        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            draw_scanline_gouraud_screen_alpha_bary_branching_s4_ordered(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_AC_ish16,
                edge_x_AB_ish16,
                hsl_ish8,
                step_x_hsl_ish8,
                alpha);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            hsl_ish8 += step_y_hsl_ish8;
            offset += stride;
        }

        while( y2-- > 0 )
        {
            draw_scanline_gouraud_screen_alpha_bary_branching_s4_ordered(
                pixel_buffer,
                offset,
                screen_width,
                0,
                edge_x_AC_ish16,
                edge_x_BC_ish16,
                hsl_ish8,
                step_x_hsl_ish8,
                alpha);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            hsl_ish8 += step_y_hsl_ish8;
            offset += stride;
        }
    }
}
static inline void
raster_gouraud_screen_alpha_bary_branching_s4(
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
    int color2_hsl16,
    int alpha)
{
    if( y0 <= y1 && y0 <= y2 )
    {
        if( y0 > screen_height )
            return;

        if( y1 <= y2 )
        {
            if( y2 < 0 || y0 > screen_height )
                return;

            raster_gouraud_screen_alpha_bary_branching_s4_ordered(
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
                color2_hsl16,
                alpha);
        }
        else
        {
            if( y1 < 0 || y0 > screen_height )
                return;

            raster_gouraud_screen_alpha_bary_branching_s4_ordered(
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
                color1_hsl16,
                alpha);
        }
    }
    else if( y1 <= y2 )
    {
        if( y1 > screen_height )
            return;

        if( y2 <= y0 )
        {
            if( y0 < 0 || y1 > screen_height )
                return;

            raster_gouraud_screen_alpha_bary_branching_s4_ordered(
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
                color0_hsl16,
                alpha);
        }
        else
        {
            if( y2 < 0 || y1 > screen_height )
                return;

            raster_gouraud_screen_alpha_bary_branching_s4_ordered(
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
                color2_hsl16,
                alpha);
        }
    }
    else
    {
        if( y2 > screen_height )
            return;

        if( y0 <= y1 )
        {
            if( y1 < 0 || y2 > screen_height )
                return;

            raster_gouraud_screen_alpha_bary_branching_s4_ordered(
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
                color1_hsl16,
                alpha);
        }
        else
        {
            if( y0 < 0 || y2 > screen_height )
                return;

            raster_gouraud_screen_alpha_bary_branching_s4_ordered(
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
                color0_hsl16,
                alpha);
        }
    }
}

#endif
