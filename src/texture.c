#include "texture.h"

#include <assert.h>
#include <stdio.h>
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int
interpolate_ish12(int x_begin, int x_end, int t_sh16)
{
    int stride = x_end - x_begin;
    return ((x_begin << 12) + stride * (t_sh16 >> 4)) >> 12;
}

void
draw_scanline_texture(
    int* pixel_buffer,
    int stride_width,
    int y,
    int screen_x_start,
    int screen_x_end,
    int z_start,
    int z_end,
    int u_start,
    int u_end,
    int v_start,
    int v_end,
    // Max texture is 4096x4096... TODO: Document this.
    int* texels,
    int texture_width)
{
    // screen_x_t = xt / zt
    // xt = screen_x_t * zt
    int dx = screen_x_end - screen_x_start;

    // Note: This does not account for FOV scaling and assumes FOV of 90 degrees
    // Pre-compute u/z and v/z values
    int u_over_z_start = (u_start << 16) / z_start;
    int u_over_z_end = (u_end << 16) / z_end;
    int v_over_z_start = (v_start << 16) / z_start;
    int v_over_z_end = (v_end << 16) / z_end;

    // Pre-compute 1/z values
    int inv_z_start = (1 << 16) / z_start;
    int inv_z_end = (1 << 16) / z_end;

    for( int x = screen_x_start; x < screen_x_end; ++x )
    {
        int t_ish16 = ((x - screen_x_start) << 16) / dx;

        // Interpolate 1/z
        int inv_z = interpolate_ish12(inv_z_start, inv_z_end, t_ish16);

        // Interpolate u/z and v/z
        int u_over_z = interpolate_ish12(u_over_z_start, u_over_z_end, t_ish16);
        int v_over_z = interpolate_ish12(v_over_z_start, v_over_z_end, t_ish16);

        // Divide by 1/z to get final texture coordinates
        int ut = (u_over_z << 12) / inv_z;
        int vt = (v_over_z << 12) / inv_z;

        int texel_index = (vt * texture_width + ut) >> 12;
        if( texel_index < 0 || texel_index >= texture_width * 32 )
        {
            printf("texel_index out of bounds: %d\n", texel_index);
            continue;
        }
        int color = texels[texel_index];
        pixel_buffer[y * stride_width + x] = color;
    }
}

void
raster_texture(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int z0,
    int z1,
    int z2,
    int u0,
    int u1,
    int u2,
    int v0,
    int v1,
    int v2,
    int* texels,
    int texture_width)
{
    // Sort vertices by y-coordinate
    if( y1 < y0 )
    {
        SWAP(y0, y1);
        SWAP(x0, x1);
        SWAP(u0, u1);
        SWAP(v0, v1);
        SWAP(z0, z1);
    }
    if( y2 < y0 )
    {
        SWAP(y0, y2);
        SWAP(x0, x2);
        SWAP(u0, u2);
        SWAP(v0, v2);
        SWAP(z0, z2);
    }
    if( y2 < y1 )
    {
        SWAP(y1, y2);
        SWAP(x1, x2);
        SWAP(u1, u2);
        SWAP(v1, v2);
        SWAP(z1, z2);
    }

    // Calculate deltas for interpolation
    int dy1 = y1 - y0;
    int dy2 = y2 - y0;
    int dy3 = y2 - y1;

    // Compute x coordinates for each scanline
    int dx1 = x1 - x0;
    int dx2 = x2 - x0;
    int dx3 = x2 - x1;

    int dz1 = z1 - z0;
    int dz2 = z2 - z0;
    int dz3 = z2 - z1;

    // Compute texture coordinate deltas
    int du1 = u1 - u0;
    int du2 = u2 - u0;
    int du3 = u2 - u1;

    int dv1 = v1 - v0;
    int dv2 = v2 - v0;
    int dv3 = v2 - v1;

    // Rasterize upper triangle
    if( dy1 > 0 )
    {
        for( int y = y0; y < y1; y++ )
        {
            if( y < 0 || y >= screen_height )
                continue;

            int t1 = ((y - y0) << 16) / dy1;
            int t2 = ((y - y0) << 16) / dy2;

            int start_x = x0 + ((dx1 * t1) >> 16);
            int end_x = x0 + ((dx2 * t2) >> 16);

            int start_u = u0 + ((du1 * t1) >> 16);
            int end_u = u0 + ((du2 * t2) >> 16);

            int start_v = v0 + ((dv1 * t1) >> 16);
            int end_v = v0 + ((dv2 * t2) >> 16);

            int start_z = z0 + ((dz1 * t1) >> 16);
            int end_z = z0 + ((dz2 * t2) >> 16);

            // Ensure left-to-right rasterization
            if( start_x > end_x )
            {
                SWAP(start_x, end_x);
                SWAP(start_u, end_u);
                SWAP(start_v, end_v);
                SWAP(start_z, end_z);
            }

            // Clip x coordinates
            start_x = MAX(0, MIN(start_x, screen_width - 1));
            end_x = MAX(0, MIN(end_x, screen_width - 1));

            draw_scanline_texture(
                pixel_buffer,
                screen_width,
                y,
                start_x,
                end_x,
                start_z,
                end_z,
                start_u,
                end_u,
                start_v,
                end_v,
                texels,
                texture_width);
        }
    }

    // Rasterize lower triangle
    if( dy3 > 0 )
    {
        for( int y = y1; y <= y2; y++ )
        {
            if( y < 0 || y >= screen_height )
                continue;

            int t1 = ((y - y1) << 16) / dy3;
            int t2 = ((y - y0) << 16) / dy2;

            int start_x = x1 + ((dx3 * t1) >> 16);
            int end_x = x0 + ((dx2 * t2) >> 16);

            int start_u = u1 + ((du3 * t1) >> 16);
            int end_u = u0 + ((du2 * t2) >> 16);

            int start_v = v1 + ((dv3 * t1) >> 16);
            int end_v = v0 + ((dv2 * t2) >> 16);

            int start_z = z1 + ((dz3 * t1) >> 16);
            int end_z = z0 + ((dz2 * t2) >> 16);

            // Ensure left-to-right rasterization
            if( start_x > end_x )
            {
                SWAP(start_x, end_x);
                SWAP(start_u, end_u);
                SWAP(start_v, end_v);
                SWAP(start_z, end_z);
            }

            // Clip x coordinates
            start_x = MAX(0, MIN(start_x, screen_width - 1));
            end_x = MAX(0, MIN(end_x, screen_width - 1));

            draw_scanline_texture(
                pixel_buffer,
                screen_width,
                y,
                start_x,
                end_x,
                start_z,
                end_z,
                start_u,
                end_u,
                start_v,
                end_v,
                texels,
                texture_width);
        }
    }
}