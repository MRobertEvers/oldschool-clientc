#ifndef TEXTURE_AFFINE_U_C
#define TEXTURE_AFFINE_U_C

#include "clamp.h"
#include "projection.u.c"
#include "texture_simd.u.c"

#include <stdint.h>

static inline void
draw_texture_affine_scanline_opaque_blend_ordered_blerp8(
    int* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int u_ish8,
    int v_ish8,
    int step_u_dx_ish8,
    int step_v_dx_ish8,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* texels,
    int texture_width)
{
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    int offset = pixel_offset;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

    int screen_x0 = screen_x0_ish16 >> 16;
    int screen_x1 = screen_x1_ish16 >> 16;

    if( screen_x1 >= screen_width )
        screen_x1 = screen_width - 1;

    if( screen_x0 >= screen_x1 )
        return;

    // Adjust UV coordinates to the starting x position
    u_ish8 += step_u_dx_ish8 * screen_x0;
    v_ish8 += step_v_dx_ish8 * screen_x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * screen_x0;

    int steps = screen_x1 - screen_x0;
    offset += screen_x0;

    // If texture width is 128 or 64.
    // assert(texture_width == 128 || texture_width == 64);
    int texture_shift = 0;

    int lerp8_steps = steps;

    while( lerp8_steps-- > 0 )
    {
        // For affine mapping, we directly use the interpolated UV coordinates
        int curr_u = (u_ish8 >> 8);
        curr_u = clamp(curr_u, 0, texture_width - 1);
        int curr_v = (v_ish8 >> 8);
        curr_v = clamp(curr_v, 0, texture_width - 1);

        int texel = texels[curr_u + (curr_v << 7)];
        pixel_buffer[offset] = shade_blend(texel, shade8bit_ish8 >> 8);

        u_ish8 += step_u_dx_ish8;
        v_ish8 += step_v_dx_ish8;
        shade8bit_ish8 += step_shade8bit_dx_ish8;

        offset += 1;
    }
}

#include "uv_pnm.h"

static inline void
raster_texture_affine_opaque_blend_ordered_blerp8(
    int* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_fov,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int uv_pnm_u1,
    int uv_pnm_v1,
    int uv_pnm_u2,
    int uv_pnm_v2,
    int uv_pnm_u3,
    int uv_pnm_v3,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    int* texels,
    int texture_width)
{
    if( y0 >= screen_height )
        return;

    // Calculate the area of the triangle for barycentric interpolation
    int dx_AC = x2 - x0;
    int dy_AC = y2 - y0;
    int dx_AB = x1 - x0;
    int dy_AB = y1 - y0;

    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

    int dy_BC = y2 - y1;
    int dx_BC = x2 - x1;

    // Calculate UV coordinate differences for barycentric interpolation
    int du_ab = (uv_pnm_u2 - uv_pnm_u1);
    int du_ac = (uv_pnm_u3 - uv_pnm_u1);
    int dv_ab = (uv_pnm_v2 - uv_pnm_v1);
    int dv_ac = (uv_pnm_v3 - uv_pnm_v1);

    // Calculate blend color differences
    int dblend7bit_ab = shade7bit_b - shade7bit_a;
    int dblend7bit_ac = shade7bit_c - shade7bit_a;

    int step_edge_x_AC_ish16 = 0;
    int step_edge_x_AB_ish16 = 0;
    int step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    // Calculate UV coordinate gradients using barycentric coordinates (similar to gouraud)
    int step_u_x_ish8 = ((dy_AB * du_ac - dy_AC * du_ab) << 8) / sarea_abc;
    int step_u_y_ish8 = ((dx_AC * du_ab - dx_AB * du_ac) << 8) / sarea_abc;
    int step_v_x_ish8 = ((dy_AB * dv_ac - dy_AC * dv_ab) << 8) / sarea_abc;
    int step_v_y_ish8 = ((dx_AC * dv_ab - dx_AB * dv_ac) << 8) / sarea_abc;

    // Shades are provided 0-127, shift up by 1, then up by 8 to get 0-255.
    int shade8bit_yhat_ish8 = ((dx_AC * dblend7bit_ab - dx_AB * dblend7bit_ac) << 9) / sarea_abc;
    int shade8bit_xhat_ish8 = ((dy_AB * dblend7bit_ac - dy_AC * dblend7bit_ab) << 9) / sarea_abc;

    int u_edge_ish8 = (uv_pnm_u1 << 8) - step_u_x_ish8 * x0 + step_u_x_ish8;
    int v_edge_ish8 = (uv_pnm_v1 << 8) - step_v_x_ish8 * x0 + step_v_x_ish8;
    int shade8bit_edge_ish8 = (shade7bit_a << 9) - shade8bit_xhat_ish8 * x0 + shade8bit_xhat_ish8;

    int edge_x_AC_ish16 = x0 << 16;
    int edge_x_AB_ish16 = x0 << 16;
    int edge_x_BC_ish16 = x1 << 16;

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;
        u_edge_ish8 -= step_u_y_ish8 * y0;
        v_edge_ish8 -= step_v_y_ish8 * y0;
        shade8bit_edge_ish8 -= shade8bit_yhat_ish8 * y0;

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
            draw_texture_affine_scanline_opaque_blend_ordered_blerp8(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                edge_x_AB_ish16,
                edge_x_AC_ish16,
                offset,
                u_edge_ish8,
                v_edge_ish8,
                step_u_x_ish8,
                step_v_x_ish8,
                shade8bit_edge_ish8,
                shade8bit_xhat_ish8,
                texels,
                texture_width);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            u_edge_ish8 += step_u_y_ish8;
            v_edge_ish8 += step_v_y_ish8;

            shade8bit_edge_ish8 += shade8bit_yhat_ish8;

            offset += stride;
        }

        while( y2-- > 0 )
        {
            draw_texture_affine_scanline_opaque_blend_ordered_blerp8(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                edge_x_BC_ish16,
                edge_x_AC_ish16,
                offset,
                u_edge_ish8,
                v_edge_ish8,
                step_u_x_ish8,
                step_v_x_ish8,
                shade8bit_edge_ish8,
                shade8bit_xhat_ish8,
                texels,
                texture_width);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            u_edge_ish8 += step_u_y_ish8;
            v_edge_ish8 += step_v_y_ish8;

            shade8bit_edge_ish8 += shade8bit_yhat_ish8;

            offset += stride;
        }
    }
    else
    {
        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            draw_texture_affine_scanline_opaque_blend_ordered_blerp8(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                edge_x_AC_ish16,
                edge_x_AB_ish16,
                offset,
                u_edge_ish8,
                v_edge_ish8,
                step_u_x_ish8,
                step_v_x_ish8,
                shade8bit_edge_ish8,
                shade8bit_xhat_ish8,
                texels,
                texture_width);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            u_edge_ish8 += step_u_y_ish8;
            v_edge_ish8 += step_v_y_ish8;

            shade8bit_edge_ish8 += shade8bit_yhat_ish8;

            offset += stride;
        }

        while( y2-- > 0 )
        {
            draw_texture_affine_scanline_opaque_blend_ordered_blerp8(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                edge_x_AC_ish16,
                edge_x_BC_ish16,
                offset,
                u_edge_ish8,
                v_edge_ish8,
                step_u_x_ish8,
                step_v_x_ish8,
                shade8bit_edge_ish8,
                shade8bit_xhat_ish8,
                texels,
                texture_width);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            u_edge_ish8 += step_u_y_ish8;
            v_edge_ish8 += step_v_y_ish8;

            shade8bit_edge_ish8 += shade8bit_yhat_ish8;
            offset += stride;
        }
    }
}

static inline void
raster_texture_affine_opaque_blend_blerp8(
    int* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_fov,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int orthographic_x0,
    int orthographic_x1,
    int orthographic_x2,
    int orthographic_y0,
    int orthographic_y1,
    int orthographic_y2,
    int orthographic_z0,
    int orthographic_z1,
    int orthographic_z2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    int* texels,
    int texture_width)
{
    struct UVFaceCoords uv_pnm;
    uv_pnm_compute(
        &uv_pnm,
        orthographic_uvorigin_x0,
        orthographic_uvorigin_y0,
        orthographic_uvorigin_z0,
        orthographic_uend_x1,
        orthographic_uend_y1,
        orthographic_uend_z1,
        orthographic_vend_x2,
        orthographic_vend_y2,
        orthographic_vend_z2,
        orthographic_x0,
        orthographic_y0,
        orthographic_z0,
        orthographic_x1,
        orthographic_y1,
        orthographic_z1,
        orthographic_x2,
        orthographic_y2,
        orthographic_z2);

    int u0 = (int)(uv_pnm.u1 * 128);
    int v0 = (int)(uv_pnm.v1 * 128);
    int u1 = (int)(uv_pnm.u2 * 128);
    int v1 = (int)(uv_pnm.v2 * 128);
    int u2 = (int)(uv_pnm.u3 * 128);
    int v2 = (int)(uv_pnm.v3 * 128);

    // either.
    // y0, y1, y2,
    // y0, y2, y1,
    // y1, y0, y2,
    // y1, y2, y0,
    // y2, y0, y1,
    // y2, y1, y0,
    if( y0 <= y1 && y0 <= y2 )
    {
        // y0, y1, y2,
        if( y1 <= y2 )
        {
            if( y2 < 0 || y0 >= screen_height )
                return;

            raster_texture_affine_opaque_blend_ordered_blerp8(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                camera_fov,
                x0,
                x1,
                x2,
                y0,
                y1,
                y2,
                u0,
                v0,
                u1,
                v1,
                u2,
                v2,
                shade7bit_a,
                shade7bit_b,
                shade7bit_c,
                texels,
                texture_width);
        }
        // y0, y2, y1,
        else
        {
            if( y1 < 0 || y0 >= screen_height )
                return;

            raster_texture_affine_opaque_blend_ordered_blerp8(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                camera_fov,
                x0,
                x2,
                x1,
                y0,
                y2,
                y1,
                u0,
                v0,
                u2,
                v2,
                u1,
                v1,
                shade7bit_a,
                shade7bit_c,
                shade7bit_b,
                texels,
                texture_width);
        }
    }
    else if( y1 <= y2 )
    {
        // y1, y2, y0
        if( y2 <= y0 )
        {
            if( y0 < 0 || y1 >= screen_height )
                return;

            raster_texture_affine_opaque_blend_ordered_blerp8(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                camera_fov,
                x1,
                x2,
                x0,
                y1,
                y2,
                y0,
                u1,
                v1,
                u2,
                v2,
                u0,
                v0,
                shade7bit_b,
                shade7bit_c,
                shade7bit_a,
                texels,
                texture_width);
        }
        // y1, y0, y2,
        else
        {
            if( y2 < 0 || y1 >= screen_height )
                return;

            raster_texture_affine_opaque_blend_ordered_blerp8(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                camera_fov,
                x1,
                x0,
                x2,
                y1,
                y0,
                y2,
                u1,
                v1,
                u0,
                v0,
                u2,
                v2,
                shade7bit_b,
                shade7bit_a,
                shade7bit_c,
                texels,
                texture_width);
        }
    }
    else
    {
        // y2, y0, y1,
        if( y0 <= y1 )
        {
            if( y1 < 0 || y2 >= screen_height )
                return;

            raster_texture_affine_opaque_blend_ordered_blerp8(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                camera_fov,
                x2,
                x0,
                x1,
                y2,
                y0,
                y1,
                u2,
                v2,
                u0,
                v0,
                u1,
                v1,
                shade7bit_c,
                shade7bit_a,
                shade7bit_b,
                texels,
                texture_width);
        }
        // y2, y1, y0,
        else
        {
            if( y0 < 0 || y2 >= screen_height )
                return;

            raster_texture_affine_opaque_blend_ordered_blerp8(
                pixel_buffer,
                stride,
                screen_width,
                screen_height,
                camera_fov,
                x2,
                x1,
                x0,
                y2,
                y1,
                y0,
                u2,
                v2,
                u1,
                v1,
                u0,
                v0,
                shade7bit_c,
                shade7bit_b,
                shade7bit_a,
                texels,
                texture_width);
        }
    }
}

#endif