#ifndef GOURAUD_BARYCENTRIC_U_C
#define GOURAUD_BARYCENTRIC_U_C

#include <limits.h>

extern int g_hsl16_to_rgb_table[65536];

static inline void
draw_scanline_gouraud_s4_bary(
    int* pixel_buffer,
    int offset,
    int stride_width,
    int x_start_ish,
    int x_end_ish,
    int hsl,
    int hsl_step)
{
    if( x_start_ish > x_end_ish )
    {
        int tmp;
        tmp = x_start_ish;
        x_start_ish = x_end_ish;
        x_end_ish = tmp;
    }

    int x_start = x_start_ish >> 8;
    int x_end = x_end_ish >> 8;

    if( x_start < 0 )
        x_start = 0;
    if( x_end >= (stride_width) )
        x_end = stride_width - 1;
    if( x_start >= x_end )
        return;

    int dx_stride = (x_end - x_start);

    // Steps by 4.
    offset += x_start;

    int steps = dx_stride >> 2;

    hsl += hsl_step * (x_start);

    hsl_step <<= 2;
    int rgb_color;
    while( steps > 0 )
    {
        int hsl_idx = (hsl >> 8) & 0xFFFF; // Ensure index is within bounds
        rgb_color = g_hsl16_to_rgb_table[hsl_idx];

        // This loop is vectorized by clang.
        // Even on msvc and gcc, this is faster than the manually vectorized version.
        for( int i = 0; i < 4; i++ )
        {
            pixel_buffer[offset] = rgb_color;
            offset += 1;
        }

        hsl += hsl_step;

        steps -= 1;
    }

    int hsl_idx = (hsl >> 8) & 0xFFFF; // Ensure index is within bounds
    rgb_color = g_hsl16_to_rgb_table[hsl_idx];
    switch( dx_stride & 0x3 )
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
raster_gouraud_s4_bary(
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
    /**
     * Barycentric coordinates rely on ordering.
     */
    int dx_ordered_AC = x2 - x0;
    int dy_ordered_AC = y2 - y0;
    int dx_ordered_AB = x1 - x0;
    int dy_ordered_AB = y1 - y0;

    int area = dx_ordered_AB * dy_ordered_AC - dx_ordered_AC * dy_ordered_AB;
    if( area == 0 )
        return;

    // Barycentric coordinates for color.
    int dhsl_AB = color1_hsl16 - color0_hsl16;
    int dhsl_AC = color2_hsl16 - color0_hsl16;

    // Sort vertices by y where y0 is the bottom vertex and y2 is the top vertex(
    // or bottom of the screen)
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
    if( y0 >= screen_height )
        return;
    if( y2 < 0 )
        return;

    int hsl = color0_hsl16;

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

    static const int coord_shift = 8;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << coord_shift) / dy_AC;
    else
        step_edge_x_AC_ish16 = 0;

    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << coord_shift) / dy_AB;
    else
        step_edge_x_AB_ish16 = 0;

    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << coord_shift) / dy_BC;
    else
        step_edge_x_BC_ish16 = 0;

    /*
     *          /\      y0 (A)
     *         /  \
     *        /    \    y1 (B) (second_half = true above, false below)
     *       /   /
     *      / /  y2 (C) (second_half = false)
     */
    int edge_x_AC_ish16 = x0 << coord_shift;
    int edge_x_AB_ish16 = x0 << coord_shift;
    int edge_x_BC_ish16 = x1 << coord_shift;

    // Compute color steps with overflow protection
    int step_x_temp = ((int)dhsl_AB * dy_ordered_AC - (int)dhsl_AC * dy_ordered_AB);
    int step_y_temp = ((int)dhsl_AC * dx_ordered_AB - (int)dhsl_AB * dx_ordered_AC);

    // Scale and divide by area with bounds checking
    step_x_temp = (step_x_temp << 8) / area;
    step_y_temp = (step_y_temp << 8) / area;

    // Convert back to int with bounds checking
    int color_step_x =
        (step_x_temp > INT_MAX) ? INT_MAX : ((step_x_temp < INT_MIN) ? INT_MIN : (int)step_x_temp);
    int color_step_y =
        (step_y_temp > INT_MAX) ? INT_MAX : ((step_y_temp < INT_MIN) ? INT_MIN : (int)step_y_temp);

    // Calculate initial HSL value with overflow protection
    int hsl_temp = (int)color_step_x + ((int)hsl << 8) - (int)x0 * color_step_x;
    hsl = (hsl_temp > INT_MAX) ? INT_MAX : ((hsl_temp < INT_MIN) ? INT_MIN : (int)hsl_temp);

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;

        hsl -= color_step_y * y0;

        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;

        y1 = 0;
    }
    if( y1 >= screen_height )
        y1 = screen_height - 1;

    int offset = y0 * screen_width;

    int stride_width = screen_width;
    int i = y0;
    for( ; i < y1; ++i )
    {
        draw_scanline_gouraud_s4_bary(
            pixel_buffer,
            offset,
            stride_width,
            edge_x_AC_ish16,
            edge_x_AB_ish16,
            hsl,
            color_step_x);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        hsl += color_step_y;
        offset += screen_width;
    }

    if( y2 >= screen_height )
        y2 = screen_height - 1;

    if( y1 >= y2 )
        return;

    i = y1;
    for( ; i < y2; ++i )
    {
        draw_scanline_gouraud_s4_bary(
            pixel_buffer,
            offset,
            stride_width,
            edge_x_AC_ish16,
            edge_x_BC_ish16,
            hsl,
            color_step_x);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        hsl += color_step_y;
        offset += screen_width;
    }
}

#endif