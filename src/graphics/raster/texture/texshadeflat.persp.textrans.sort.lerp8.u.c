#ifndef TEXSHADEFLAT_PERSP_TEXTRANS_SORT_LERP8_U_C
#define TEXSHADEFLAT_PERSP_TEXTRANS_SORT_LERP8_U_C

#include "graphics/dash_restrict.h"
#include "tex_shard_swap.h"

#include <assert.h>

static inline void
raster_texshadeflat_persp_textrans_sort_lerp8(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_fov,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit,
    int* RESTRICT texels,
    int texture_width)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
    }

    if( screen_y0 >= screen_height )
        return;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    int vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    int vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    int vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    int vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    // TODO: The derivation leaves this as the VU plane,
    // so this needs to be flipped.
    int vUVPlane_normal_xhat = vU_y * vV_z - vU_z * vV_y;
    int vUVPlane_normal_yhat = vU_z * vV_x - vU_x * vV_z;
    int vUVPlane_normal_zhat = vU_x * vV_y - vU_y * vV_x;

    int vOVPlane_normal_xhat = orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    int vOVPlane_normal_yhat = orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    int vOVPlane_normal_zhat = orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    int vUOPlane_normal_xhat = vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    int vUOPlane_normal_yhat = vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    int vUOPlane_normal_zhat = vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // These two vectors now point in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.

    int dy_AC = screen_y2 - screen_y0;
    int dy_AB = screen_y1 - screen_y0;
    int dy_BC = screen_y2 - screen_y1;

    int dx_AC = screen_x2 - screen_x0;
    int dx_AB = screen_x1 - screen_x0;
    int dx_BC = screen_x2 - screen_x1;

    int step_edge_x_AC_ish16 = 0;
    int step_edge_x_AB_ish16 = 0;
    int step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    // Do the same computation for the blend color.
    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

    int au = 0;
    int bv = 0;
    int cw = 0;

    int edge_x_AC_ish16 = screen_x0 << 16;
    int edge_x_AB_ish16 = screen_x0 << 16;
    int edge_x_BC_ish16 = screen_x1 << 16;

    if( screen_y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * screen_y0;
        screen_y0 = 0;
    }

    if( screen_y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;

        screen_y1 = 0;
    }

    if( screen_y0 > screen_y1 )
        return;

    if( screen_y1 >= screen_height )
        screen_y1 = screen_height - 1;

    au = project_scale_unit(vOVPlane_normal_zhat, camera_fov);
    bv = project_scale_unit(vUOPlane_normal_zhat, camera_fov);
    cw = project_scale_unit(vUVPlane_normal_zhat, camera_fov);

    int dy = screen_y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    int steps = screen_y1 - screen_y0;
    int offset = screen_y0 * stride;

    int shade = shade7bit << 9;

    assert(screen_y0 < screen_height);

    while( steps-- > 0 )
    {
        int x_start = edge_x_AC_ish16 >> 16;
        int x_end = edge_x_AB_ish16 >> 16;

        raster_texshadeflat_persp_textrans_sort_lerp8_scanline(
            pixel_buffer,
            stride,
            screen_width,
            screen_height,
            x_start,
            x_end,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += stride;
    }

    if( screen_y2 >= screen_height )
        screen_y2 = screen_height - 1;

    if( screen_y1 > screen_y2 )
        return;

    dy = screen_y1 - (screen_height >> 1);
    au = project_scale_unit(vOVPlane_normal_zhat, camera_fov);
    bv = project_scale_unit(vUOPlane_normal_zhat, camera_fov);
    cw = project_scale_unit(vUVPlane_normal_zhat, camera_fov);
    au += vOVPlane_normal_yhat * dy;
    bv += vUOPlane_normal_yhat * dy;
    cw += vUVPlane_normal_yhat * dy;

    assert(screen_y1 >= 0);
    assert(screen_y1 <= screen_height);
    assert(screen_y2 >= 0);
    assert(screen_y2 <= screen_height);

    offset = screen_y1 * stride;
    steps = screen_y2 - screen_y1;
    while( steps-- > 0 )
    {
        raster_texshadeflat_persp_textrans_sort_lerp8_scanline(
            pixel_buffer,
            stride,
            screen_width,
            screen_height,
            edge_x_AC_ish16 >> 16,
            edge_x_BC_ish16 >> 16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += stride;
    }
}

#endif
