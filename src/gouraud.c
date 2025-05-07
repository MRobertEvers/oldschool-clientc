
static int
interpolate_ish16(int x_begin, int x_end, int t_sh16)
{
    int stride = x_end - x_begin;
    return ((x_begin << 16) + stride * t_sh16) >> 16;
}

/**
 * @brief Requires caller to clamp y value to screen.
 *
 * @param pixel_buffer
 * @param z_buffer
 * @param stride_width
 * @param y
 * @param x_start
 * @param x_end
 * @param depth_start
 * @param depth_end
 * @param color_start
 * @param color_end
 */
void
draw_scanline_gouraud_zbuf(
    int* pixel_buffer,
    int* z_buffer,
    int stride_width,
    int y,
    int x_start,
    int x_end,
    int depth_start,
    int depth_end,
    int color_start,
    int color_end)
{
    if( x_start > x_end )
    {
        int tmp;
        tmp = x_start;
        x_start = x_end;
        x_end = tmp;
        tmp = depth_start;
        depth_start = depth_end;
        depth_end = tmp;
        tmp = color_start;
        color_start = color_end;
        color_end = tmp;
    }

    int dx_stride = x_end - x_start;
    for( int x = x_start; x <= x_end; ++x )
    {
        int t_sh16 = dx_stride == 0 ? 0 : ((x - x_start) << 16) / dx_stride;

        int depth = interpolate_ish16(depth_start, depth_end, t_sh16);
        if( z_buffer[y * stride_width + x] >= depth )
            z_buffer[y * stride_width + x] = depth;
        else
            continue;

        int r = interpolate_ish16((color_start >> 16) & 0xFF, (color_end >> 16) & 0xFF, t_sh16);
        int g = interpolate_ish16((color_start >> 8) & 0xFF, (color_end >> 8) & 0xFF, t_sh16);
        int b = interpolate_ish16(color_start & 0xFF, color_end & 0xFF, t_sh16);
        int a = 0xFF; // Alpha value

        int color = (a << 24) | (r << 16) | (g << 8) | b;

        pixel_buffer[y * stride_width + x] = color;
    }
}
