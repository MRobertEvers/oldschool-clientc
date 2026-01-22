#ifndef TEXTURE_BLEND_BRANCHING_C
#define TEXTURE_BLEND_BRANCHING_C

#include "clamp.h"
#include "projection.u.c"
#include "texture_simd.u.c"

#include <stdint.h>

static inline void
draw_texture_scanline_opaque_blend_ordered_blerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* texels,
    int texture_width)
{
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

    int screen_x0 = screen_x0_ish16 >> 16;
    int screen_x1 = screen_x1_ish16 >> 16;

    if( screen_x1 >= screen_width )
        screen_x1 = screen_width - 1;

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    step_au_dx <<= 3;
    step_bv_dx <<= 3;
    step_cw_dx <<= 3;

    shade8bit_ish8 += step_shade8bit_dx_ish8 * screen_x0;

    steps = screen_x1 - screen_x0;

    offset += screen_x0;

    // If texture width is 128 or 64.
    // assert(texture_width == 128 || texture_width == 64);
    int texture_shift = (texture_width & 0x80) ? 7 : 6;
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;

    int curr_u;
    int curr_v;
    int next_u;
    int next_v;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;
    int lerp8_shade_step = step_shade8bit_dx_ish8 << 3;
    int shade;
    while( lerp8_steps-- > 0 )
    {
        int w = (cw) >> texture_shift;
        if( w == 0 )
            continue;

        curr_u = (au) / w;
        curr_u = clamp(curr_u, 0, texture_width - 1);
        curr_v = (bv) / w;
        // curr_v = clamp(curr_v, 0, texture_width - 1);

        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;

        w = (cw) >> texture_shift;
        if( w == 0 )
            continue;

        next_u = (au) / w;
        next_u = clamp(next_u, 0x0, texture_width - 1);
        next_v = (bv) / w;

        int step_u = (next_u - curr_u) << (texture_shift - 3);
        int step_v = (next_v - curr_v) << (texture_shift - 3);

        int u_scan = curr_u << texture_shift;
        int v_scan = curr_v << texture_shift;

        shade = shade8bit_ish8 >> 8;

        raster_linear_opaque_blend_lerp8(
            (uint32_t*)pixel_buffer,
            offset,
            (uint32_t*)texels,
            u_scan,
            v_scan,
            step_u,
            step_v,
            texture_shift,
            shade);
        u_scan += step_u;
        v_scan += step_v;
        offset += 8;

        // Checked on 09/15/2025, clang does NOT vectorize this loop.
        // even with -O3
        // On large screens the vectorized version is significantly faster.
        // for( int i = 0; i < 8; i++ )
        // {
        //     int u = u_scan >> texture_shift;
        //     int v = v_scan & 0x3f80;
        //     int texel = texels[u + (v)];
        //     pixel_buffer[offset] = shade_blend(texel, shade);

        //     u_scan += step_u;
        //     v_scan += step_v;

        //     offset += 1;
        // }

        shade8bit_ish8 += lerp8_shade_step;
    }

    if( lerp8_last_steps == 0 )
        return;

    int w = (cw) >> texture_shift;
    if( w == 0 )
        return;

    curr_u = (au) / w;
    curr_u = clamp(curr_u, 0, texture_width - 1);
    curr_v = (bv) / w;

    au += step_au_dx;
    bv += step_bv_dx;
    cw += step_cw_dx;

    w = (cw) >> texture_shift;
    if( w == 0 )
        return;

    next_u = (au) / w;
    next_u = clamp(next_u, 0x0, texture_width - 1);
    next_v = (bv) / w;

    int step_u = (next_u - curr_u) << (texture_shift - 3);
    int step_v = (next_v - curr_v) << (texture_shift - 3);

    int u_scan = curr_u << texture_shift;
    int v_scan = curr_v << texture_shift;

    shade = shade8bit_ish8 >> 8;
    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        int texel = texels[u + v];
        pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}

static inline void
raster_texture_opaque_blend_ordered_blerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
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
    if( y0 >= screen_height )
        return;

    // These two vectors now point in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.

    int dy_AC = y2 - y0;
    int dy_AB = y1 - y0;

    int dx_AC = x2 - x0;
    int dx_AB = x1 - x0;

    // Do the same computation for the blend color.
    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

    int dy_BC = y2 - y1;
    int dx_BC = x2 - x1;

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

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    int vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    int vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    int vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    int vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    int vUVPlane_normal_xhat = vU_z * vV_y - vU_y * vV_z;
    int vUVPlane_normal_yhat = vU_x * vV_z - vU_z * vV_x;
    int vUVPlane_normal_zhat = vU_y * vV_x - vU_x * vV_y;

    int vOVPlane_normal_xhat = orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    int vOVPlane_normal_yhat = orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    int vOVPlane_normal_zhat = orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    int vUOPlane_normal_xhat = vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    int vUOPlane_normal_yhat = vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    int vUOPlane_normal_zhat = vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // Same idea here for color. Solve the system of equations.
    // Barycentric coordinates.

    // Shades are provided 0-127, shift up by 1, then up by 8 to get 0-255.
    // Again, kramer's rule.
    int shade8bit_yhat_ish8 = ((dx_AC * dblend7bit_ab - dx_AB * dblend7bit_ac) << 9) / sarea_abc;
    int shade8bit_xhat_ish8 = ((dy_AB * dblend7bit_ac - dy_AC * dblend7bit_ab) << 9) / sarea_abc;

    int shade8bit_edge_ish8 = (shade7bit_a << 9) - shade8bit_xhat_ish8 * x0 + shade8bit_xhat_ish8;

    int au = 0;
    int bv = 0;
    int cw = 0;

    int edge_x_AC_ish16 = x0 << 16;
    int edge_x_AB_ish16 = x0 << 16;
    int edge_x_BC_ish16 = x1 << 16;

    if( y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;
        shade8bit_edge_ish8 -= shade8bit_yhat_ish8 * y0;

        y0 = 0;
    }

    if( y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * y1;

        y1 = 0;
    }

    au = project_scale_unit(vOVPlane_normal_zhat, camera_fov);
    bv = project_scale_unit(vUOPlane_normal_zhat, camera_fov);
    cw = project_scale_unit(vUVPlane_normal_zhat, camera_fov);

    int dy = y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    int offset = y0 * screen_width;

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
            draw_texture_scanline_opaque_blend_ordered_blerp8(
                pixel_buffer,
                screen_width,
                screen_height,
                edge_x_AB_ish16,
                edge_x_AC_ish16,
                offset,
                au,
                bv,
                cw,
                vOVPlane_normal_xhat,
                vUOPlane_normal_xhat,
                vUVPlane_normal_xhat,
                shade8bit_edge_ish8,
                shade8bit_xhat_ish8,
                texels,
                texture_width);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            au += vOVPlane_normal_yhat;
            bv += vUOPlane_normal_yhat;
            cw += vUVPlane_normal_yhat;

            shade8bit_edge_ish8 += shade8bit_yhat_ish8;

            offset += screen_width;
        }

        while( y2-- > 0 )
        {
            draw_texture_scanline_opaque_blend_ordered_blerp8(
                pixel_buffer,
                screen_width,
                screen_height,
                edge_x_BC_ish16,
                edge_x_AC_ish16,
                offset,
                au,
                bv,
                cw,
                vOVPlane_normal_xhat,
                vUOPlane_normal_xhat,
                vUVPlane_normal_xhat,
                shade8bit_edge_ish8,
                shade8bit_xhat_ish8,
                texels,
                texture_width);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            au += vOVPlane_normal_yhat;
            bv += vUOPlane_normal_yhat;
            cw += vUVPlane_normal_yhat;

            shade8bit_edge_ish8 += shade8bit_yhat_ish8;

            offset += screen_width;
        }
    }
    else
    {
        y2 -= y1;
        y1 -= y0;

        while( y1-- > 0 )
        {
            draw_texture_scanline_opaque_blend_ordered_blerp8(
                pixel_buffer,
                screen_width,
                screen_height,
                edge_x_AC_ish16,
                edge_x_AB_ish16,
                offset,
                au,
                bv,
                cw,
                vOVPlane_normal_xhat,
                vUOPlane_normal_xhat,
                vUVPlane_normal_xhat,
                shade8bit_edge_ish8,
                shade8bit_xhat_ish8,
                texels,
                texture_width);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_AB_ish16 += step_edge_x_AB_ish16;

            au += vOVPlane_normal_yhat;
            bv += vUOPlane_normal_yhat;
            cw += vUVPlane_normal_yhat;

            shade8bit_edge_ish8 += shade8bit_yhat_ish8;

            offset += screen_width;
        }

        while( y2-- > 0 )
        {
            draw_texture_scanline_opaque_blend_ordered_blerp8(
                pixel_buffer,
                screen_width,
                screen_height,
                edge_x_AC_ish16,
                edge_x_BC_ish16,
                offset,
                au,
                bv,
                cw,
                vOVPlane_normal_xhat,
                vUOPlane_normal_xhat,
                vUVPlane_normal_xhat,
                shade8bit_edge_ish8,
                shade8bit_xhat_ish8,
                texels,
                texture_width);

            edge_x_AC_ish16 += step_edge_x_AC_ish16;
            edge_x_BC_ish16 += step_edge_x_BC_ish16;

            au += vOVPlane_normal_yhat;
            bv += vUOPlane_normal_yhat;
            cw += vUVPlane_normal_yhat;

            shade8bit_edge_ish8 += shade8bit_yhat_ish8;

            offset += screen_width;
        }
    }
}

static inline void
raster_texture_opaque_blend_blerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
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

            raster_texture_opaque_blend_ordered_blerp8(
                pixel_buffer,
                screen_width,
                screen_height,
                camera_fov,
                x0,
                x1,
                x2,
                y0,
                y1,
                y2,
                orthographic_uvorigin_x0,
                orthographic_uend_x1,
                orthographic_vend_x2,
                orthographic_uvorigin_y0,
                orthographic_uend_y1,
                orthographic_vend_y2,
                orthographic_uvorigin_z0,
                orthographic_uend_z1,
                orthographic_vend_z2,
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

            raster_texture_opaque_blend_ordered_blerp8(
                pixel_buffer,
                screen_width,
                screen_height,
                camera_fov,
                x0,
                x2,
                x1,
                y0,
                y2,
                y1,
                orthographic_uvorigin_x0,
                orthographic_uend_x1,
                orthographic_vend_x2,
                orthographic_uvorigin_y0,
                orthographic_uend_y1,
                orthographic_vend_y2,
                orthographic_uvorigin_z0,
                orthographic_uend_z1,
                orthographic_vend_z2,
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

            raster_texture_opaque_blend_ordered_blerp8(
                pixel_buffer,
                screen_width,
                screen_height,
                camera_fov,
                x1,
                x2,
                x0,
                y1,
                y2,
                y0,
                orthographic_uvorigin_x0,
                orthographic_uend_x1,
                orthographic_vend_x2,
                orthographic_uvorigin_y0,
                orthographic_uend_y1,
                orthographic_vend_y2,
                orthographic_uvorigin_z0,
                orthographic_uend_z1,
                orthographic_vend_z2,
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

            raster_texture_opaque_blend_ordered_blerp8(
                pixel_buffer,
                screen_width,
                screen_height,
                camera_fov,
                x1,
                x0,
                x2,
                y1,
                y0,
                y2,
                orthographic_uvorigin_x0,
                orthographic_uend_x1,
                orthographic_vend_x2,
                orthographic_uvorigin_y0,
                orthographic_uend_y1,
                orthographic_vend_y2,
                orthographic_uvorigin_z0,
                orthographic_uend_z1,
                orthographic_vend_z2,
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

            raster_texture_opaque_blend_ordered_blerp8(
                pixel_buffer,
                screen_width,
                screen_height,
                camera_fov,
                x2,
                x0,
                x1,
                y2,
                y0,
                y1,
                orthographic_uvorigin_x0,
                orthographic_uend_x1,
                orthographic_vend_x2,
                orthographic_uvorigin_y0,
                orthographic_uend_y1,
                orthographic_vend_y2,
                orthographic_uvorigin_z0,
                orthographic_uend_z1,
                orthographic_vend_z2,
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

            raster_texture_opaque_blend_ordered_blerp8(
                pixel_buffer,
                screen_width,
                screen_height,
                camera_fov,
                x2,
                x1,
                x0,
                y2,
                y1,
                y0,
                orthographic_uvorigin_x0,
                orthographic_uend_x1,
                orthographic_vend_x2,
                orthographic_uvorigin_y0,
                orthographic_uend_y1,
                orthographic_vend_y2,
                orthographic_uvorigin_z0,
                orthographic_uend_z1,
                orthographic_vend_z2,
                shade7bit_c,
                shade7bit_b,
                shade7bit_a,
                texels,
                texture_width);
        }
    }
}

#endif