#ifndef GOURAUD_SCREEN_OPAQUE_BARY_SORT_S1_U_C
#define GOURAUD_SCREEN_OPAQUE_BARY_SORT_S1_U_C

#include "graphics/dash_restrict.h"

#include <stdint.h>

extern int g_hsl16_to_rgb_table[65536];

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

static inline void
draw_scanline_gouraud_screen_opaque_bary_sort_s1(
    int* RESTRICT pixel_buffer,
    int offset,
    int screen_width,
    int y,
    int x_lo_ish16,
    int x_hi_ish16,
    int hsl_at_row_x0_ish8,
    int step_x_hsl_ish8)
{
    (void)y;

    if( x_lo_ish16 == x_hi_ish16 )
        return;

    int x_end = x_hi_ish16 >> 16;

    if( x_end >= screen_width )
        x_end = screen_width - 1;

    int x_start = x_lo_ish16 >> 16;
    if( x_start < 0 )
        x_start = 0;

    if( x_start >= x_end )
        return;

    offset += x_start;
    int color_hsl16_ish8 = hsl_at_row_x0_ish8 + x_start * step_x_hsl_ish8;

    int stride = (x_end - x_start);

    for( int j = 0; j < stride; j++ )
    {
        int color_hsl16 = color_hsl16_ish8 >> 8;
        int rgb_color = g_hsl16_to_rgb_table[color_hsl16];
        pixel_buffer[offset] = rgb_color;
        offset += 1;
        color_hsl16_ish8 += step_x_hsl_ish8;
    }
}

static inline void
raster_gouraud_screen_opaque_bary_sort_s1(
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

    if( x0 == x1 && x1 == x2 )
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
        (int)(((int64_t)(d_hsl_AB * dy_AC - d_hsl_AC * dy_AB) << 8) / sarea);
    int step_y_hsl_ish8 =
        (int)(((int64_t)(d_hsl_AC * dx_AB - d_hsl_AB * dx_AC) << 8) / sarea);

    int dx_BC = x2 - x1;
    int dy_BC = y2 - y1;

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

    if( y1 >= screen_height )
        y1 = screen_height - 1;
    if( y2 >= screen_height )
        y2 = screen_height - 1;

    int offset = y0 * stride;

    int i = y0;
    for( ; i < y1; ++i )
    {
        int x_lo = MIN(edge_x_AC_ish16, edge_x_AB_ish16);
        int x_hi = MAX(edge_x_AC_ish16, edge_x_AB_ish16);

        draw_scanline_gouraud_screen_opaque_bary_sort_s1(
            pixel_buffer,
            offset,
            screen_width,
            i,
            x_lo,
            x_hi,
            hsl_ish8,
            step_x_hsl_ish8);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        hsl_ish8 += step_y_hsl_ish8;

        offset += stride;
    }

    if( y1 > y2 )
        return;

    i = y1;
    for( ; i < y2; ++i )
    {
        int x_lo = MIN(edge_x_AC_ish16, edge_x_BC_ish16);
        int x_hi = MAX(edge_x_AC_ish16, edge_x_BC_ish16);

        draw_scanline_gouraud_screen_opaque_bary_sort_s1(
            pixel_buffer,
            offset,
            screen_width,
            i,
            x_lo,
            x_hi,
            hsl_ish8,
            step_x_hsl_ish8);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        hsl_ish8 += step_y_hsl_ish8;

        offset += stride;
    }
}

#endif
