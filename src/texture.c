#include "texture.h"

#include <assert.h>
#include <stdint.h>
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
    int* texels,
    int texture_width)
{
    int dx = screen_x_end - screen_x_start;
    if( dx <= 0 )
        return;

    for( int x = screen_x_start; x < screen_x_end; ++x )
    {
        int t_ish16 = ((x - screen_x_start) << 16) / dx;

        // Linear interpolation of texture coordinates
        uint32_t ut = u_start + ((u_end - u_start) * t_ish16 >> 16);
        uint32_t vt = v_start + ((v_end - v_start) * t_ish16 >> 16);

        // Convert to texture space (assuming texture coordinates are in 16.16 fixed point)
        int texel_x = (ut) >> 20;
        int texel_y = (vt) >> 20;

        // Clamp texture coordinates
        texel_x = MAX(0, MIN(texel_x, texture_width - 1));
        texel_y = MAX(0, MIN(texel_y, texture_width - 1));

        int texel_index = texel_y * texture_width + texel_x;
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

    // Convert texture coordinates to 16.16 fixed point
    u0 <<= 20;
    u1 <<= 20;
    u2 <<= 20;
    v0 <<= 20;
    v1 <<= 20;
    v2 <<= 20;

    // Pre-compute perspective-correct values
    int u_over_z0 = (u0) / z0;
    int u_over_z1 = (u1) / z1;
    int u_over_z2 = (u2) / z2;
    int v_over_z0 = (v0) / z0;
    int v_over_z1 = (v1) / z1;
    int v_over_z2 = (v2) / z2;

    int inv_z0 = (1 << 16) / (z0);
    int inv_z1 = (1 << 16) / (z1);
    int inv_z2 = (1 << 16) / (z2);

    // Calculate deltas for interpolation
    int dy1 = y1 - y0;
    int dy2 = y2 - y0;
    int dy3 = y2 - y1;

    // Compute x coordinates for each scanline
    int dx1 = x1 - x0;
    int dx2 = x2 - x0;
    int dx3 = x2 - x1;

    // Compute perspective-correct deltas
    int du_over_z1 = u_over_z1 - u_over_z0;
    int du_over_z2 = u_over_z2 - u_over_z0;
    int du_over_z3 = u_over_z2 - u_over_z1;

    int dv_over_z1 = v_over_z1 - v_over_z0;
    int dv_over_z2 = v_over_z2 - v_over_z0;
    int dv_over_z3 = v_over_z2 - v_over_z1;

    int dinv_z1 = inv_z1 - inv_z0;
    int dinv_z2 = inv_z2 - inv_z0;
    int dinv_z3 = inv_z2 - inv_z1;

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

            // Interpolate perspective-correct values
            int start_u_over_z = u_over_z0 + ((du_over_z1 * t1) >> 20);
            int end_u_over_z = u_over_z0 + ((du_over_z2 * t2) >> 20);

            int start_v_over_z = v_over_z0 + ((dv_over_z1 * t1) >> 20);
            int end_v_over_z = v_over_z0 + ((dv_over_z2 * t2) >> 20);

            int start_inv_z = inv_z0 + ((dinv_z1 * t1) >> 16);
            int end_inv_z = inv_z0 + ((dinv_z2 * t2) >> 16);

            // Ensure left-to-right rasterization
            if( start_x > end_x )
            {
                SWAP(start_x, end_x);
                SWAP(start_u_over_z, end_u_over_z);
                SWAP(start_v_over_z, end_v_over_z);
                SWAP(start_inv_z, end_inv_z);
            }

            // Convert perspective-correct values to screen space
            int start_z = (1 << 16) / start_inv_z;
            int end_z = (1 << 16) / end_inv_z;

            // Convert to final texture coordinates (in 16.16 fixed point)
            int start_u = (start_u_over_z * start_z);
            int end_u = (end_u_over_z * end_z);
            int start_v = (start_v_over_z * start_z);
            int end_v = (end_v_over_z * end_z);

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

            // Interpolate perspective-correct values
            int start_u_over_z = u_over_z1 + ((du_over_z3 * t1) >> 20);
            int end_u_over_z = u_over_z0 + ((du_over_z2 * t2) >> 20);

            int start_v_over_z = v_over_z1 + ((dv_over_z3 * t1) >> 20);
            int end_v_over_z = v_over_z0 + ((dv_over_z2 * t2) >> 20);

            int start_inv_z = inv_z1 + ((dinv_z3 * t1) >> 16);
            int end_inv_z = inv_z0 + ((dinv_z2 * t2) >> 16);

            // Ensure left-to-right rasterization
            if( start_x > end_x )
            {
                SWAP(start_x, end_x);
                SWAP(start_u_over_z, end_u_over_z);
                SWAP(start_v_over_z, end_v_over_z);
                SWAP(start_inv_z, end_inv_z);
            }

            // Convert perspective-correct values to screen space
            int start_z = (1 << 16) / start_inv_z;
            int end_z = (1 << 16) / end_inv_z;

            // Convert to final texture coordinates (in 16.16 fixed point)
            int start_u = (start_u_over_z * start_z);
            int end_u = (end_u_over_z * end_z);
            int start_v = (start_v_over_z * start_z);
            int end_v = (end_v_over_z * end_z);

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