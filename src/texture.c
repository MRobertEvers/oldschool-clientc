#include "texture.h"

#include "projection.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SCREEN_HEIGHT 600
#define SCREEN_WIDTH 800

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void
raster_texture_scanline(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int pixel_offset,
    long long au,
    long long bv,
    long long cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int* texels,
    long long texture_width,
    long long texture_opaque)
{
    if( screen_x0 == screen_x1 )
        return;

    if( screen_x0 > screen_x1 )
    {
        SWAP(screen_x0, screen_x1);
    }

    long long steps, adjust;

    long long offset = pixel_offset;

    if( screen_x0 < 0 )
        screen_x0 = 0;

    if( screen_x1 >= screen_width )
    {
        screen_x1 = screen_width - 1;
    }

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    steps = screen_x1 - screen_x0;

    assert(screen_x0 < screen_width);
    assert(screen_x1 < screen_width);

    assert(screen_x0 <= screen_x1);
    assert(screen_x0 >= 0);
    assert(screen_x1 >= 0);

    offset += screen_x0;

    assert(screen_x0 + steps < screen_width);

    while( steps-- > 0 )
    {
        if( cw == 0 )
            continue;

        int u = (au * (texture_width)) / (-cw);
        int v = (bv * (texture_width)) / (-cw);

        // The osrs rasterizer clamps the u and v coordinates to the texture width.
        int c = -1;
        if( u < 0 )
        {
            u = 0;
            c = 0xFF0000;
        }
        if( v < 0 )
        {
            v = 0;
            c = 0x00FF00;
        }
        if( u >= texture_width )
        {
            c = 0xFF00FF;
            u = texture_width - 1;
        }
        if( v >= texture_width )
        {
            c = 0x00FFFF;
            v = texture_width - 1;
        }

        // Expects the texture width to be a power of two.
        u &= texture_width - 1;
        v &= texture_width - 1;

        assert(u >= 0);
        assert(v >= 0);
        assert(u < texture_width);
        assert(v < texture_width);

        // if( u < 0 )
        //     u = 0;
        // if( v < 0 )
        //     v = 0;
        // if( u >= texture_width )
        //     u = texture_width - 1;
        // if( v >= texture_width )
        //     v = texture_width - 1;

        assert(u >= 0 && u < texture_width);
        assert(v >= 0 && v < texture_width);

        int texel = texels[u + v * texture_width];
        if( texture_opaque || texel != 0 )
        {
            if( c != -1 )
                pixel_buffer[offset] = c;
            else
                pixel_buffer[offset] = texel;
        }

        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;

        offset += 1;
        assert(offset >= 0 && offset < screen_width * screen_height);
    }
}

void
raster_texture_step(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int screen_z0,
    int screen_z1,
    int screen_z2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int* texels,
    int texture_width,
    int texture_opaque)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
        SWAP(screen_z0, screen_z2);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
        SWAP(screen_z0, screen_z1);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
        SWAP(screen_z1, screen_z2);
    }

    int total_height = screen_y2 - screen_y0;
    if( total_height == 0 )
        return;

    if( screen_y0 >= screen_height )
        return;

    // TODO: Remove this check for callers that cull correctly.
    if( total_height >= screen_height )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // TODO: Remove this check for callers that cull correctly.
    if( (screen_x0 < 0 || screen_x1 < 0 || screen_x2 < 0) &&
        (screen_x0 > screen_width || screen_x1 > screen_width || screen_x2 > screen_width) )
    {
        // This can happen if vertices extremely close to the camera plane, but outside the FOV
        // are projected. Those vertices need to be culled.
        return;
    }

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    long long vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    long long vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    long long vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    long long vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    long long vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    long long vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    // TODO: The derivation leaves this as the VU plane,
    // so this needs to be flipped.
    long long vUVPlane_normal_xhat = vU_y * vV_z - vU_z * vV_y;
    long long vUVPlane_normal_yhat = vU_z * vV_x - vU_x * vV_z;
    long long vUVPlane_normal_zhat = vU_x * vV_y - vU_y * vV_x;

    long long vOVPlane_normal_xhat =
        orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    long long vOVPlane_normal_yhat =
        orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    long long vOVPlane_normal_zhat =
        orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    long long vUOPlane_normal_xhat =
        vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    long long vUOPlane_normal_yhat =
        vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    long long vUOPlane_normal_zhat =
        vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // These two vectors now polong long in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.

    long long dy_AC = screen_y2 - screen_y0;
    long long dy_AB = screen_y1 - screen_y0;
    long long dy_BC = screen_y2 - screen_y1;

    long long dx_AC = screen_x2 - screen_x0;
    long long dx_AB = screen_x1 - screen_x0;
    long long dx_BC = screen_x2 - screen_x1;

    long long step_edge_x_AC_ish16 = 0;
    long long step_edge_x_AB_ish16 = 0;
    long long step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    long long au = 0;
    long long bv = 0;
    long long cw = 0;

    long long edge_x_AC_ish16 = screen_x0 << 16;
    long long edge_x_AB_ish16 = screen_x0 << 16;
    long long edge_x_BC_ish16 = screen_x1 << 16;

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

    au = vOVPlane_normal_zhat * UNIT_SCALE;
    bv = vUOPlane_normal_zhat * UNIT_SCALE;
    cw = vUVPlane_normal_zhat * UNIT_SCALE;

    long long dy = screen_y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    long long steps = screen_y1 - screen_y0;
    long long offset = screen_y0 * screen_width;

    assert(screen_y0 < screen_height);

    while( steps-- > 0 )
    {
        long long x_start = edge_x_AC_ish16 >> 16;
        long long x_end = edge_x_AB_ish16 >> 16;

        raster_texture_scanline(
            pixel_buffer,
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
            texels,
            texture_width,
            texture_opaque);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += screen_width;
    }

    if( screen_y2 >= screen_height )
        screen_y2 = screen_height - 1;

    if( screen_y1 > screen_y2 )
        return;

    dy = screen_y1 - (screen_height >> 1);
    au = vOVPlane_normal_zhat * UNIT_SCALE;
    bv = vUOPlane_normal_zhat * UNIT_SCALE;
    cw = vUVPlane_normal_zhat * UNIT_SCALE;
    au += vOVPlane_normal_yhat * dy;
    bv += vUOPlane_normal_yhat * dy;
    cw += vUVPlane_normal_yhat * dy;

    assert(screen_y1 >= 0);
    assert(screen_y1 <= screen_height);
    assert(screen_y2 >= 0);
    assert(screen_y2 <= screen_height);

    offset = screen_y1 * screen_width;
    steps = screen_y2 - screen_y1;
    while( steps-- > 0 )
    {
        // break;
        raster_texture_scanline(
            pixel_buffer,
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
            texels,
            texture_width,
            texture_opaque);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += screen_width;
    }
}

void
raster_texture(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int screen_z0,
    int screen_z1,
    int screen_z2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int* texels,
    int texture_width,
    int texture_opaque)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
        SWAP(screen_z0, screen_z2);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
        SWAP(screen_z0, screen_z1);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
        SWAP(screen_z1, screen_z2);
    }

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
    int total_height = screen_y2 - screen_y0;
    if( total_height == 0 )
        return;

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

    if( screen_y0 > screen_y1 )
        return;

    int x_start, x_end, x, u, v;

    int y = screen_y0;
    for( ; y < screen_y1 && y < screen_height; y++ )
    {
        if( y < 0 || y >= screen_height )
            continue;

        x_start = edge_x_AC_ish16 >> 16;
        x_end = edge_x_AB_ish16 >> 16;

        if( x_start > x_end )
        {
            SWAP(x_start, x_end);
        }

        for( x = x_start; x < x_end; x++ )
        {
            if( x < 0 || x >= screen_width )
                continue;

            au = vOVPlane_normal_zhat * UNIT_SCALE + vOVPlane_normal_xhat * (x - 400) +
                 vOVPlane_normal_yhat * (y - 300);
            bv = vUOPlane_normal_zhat * UNIT_SCALE + vUOPlane_normal_xhat * (x - 400) +
                 vUOPlane_normal_yhat * (y - 300);
            cw = vUVPlane_normal_zhat * UNIT_SCALE + vUVPlane_normal_xhat * (x - 400) +
                 vUVPlane_normal_yhat * (y - 300);

            assert(cw != 0);

            u = (au * texture_width) / (-cw);
            v = (bv * texture_width) / (-cw);

            if( u < 0 )
                u = 0;
            if( u >= texture_width )
                u = texture_width - 1;
            if( v < 0 )
                v = 0;
            if( v >= texture_width )
                v = texture_width - 1;

            assert(u >= 0);
            assert(u < texture_width);
            assert(v >= 0);
            assert(v < texture_width);
            int texel = texels[u + v * texture_width];

            int offset = y * screen_width + x;
            assert(offset >= 0 && offset < screen_width * screen_height);
            pixel_buffer[offset] = texel;
        }

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;
    }

    if( screen_y1 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y1;
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;
        screen_y1 = 0;
    }

    y = screen_y1;

    for( ; y < screen_y2 && y < screen_height; y++ )
    {
        if( y < 0 || y >= screen_height )
            continue;

        x_start = edge_x_AC_ish16 >> 16;
        x_end = edge_x_BC_ish16 >> 16;

        if( x_start > x_end )
        {
            SWAP(x_start, x_end);
        }

        for( x = x_start; x < x_end; x++ )
        {
            if( x < 0 || x >= screen_width )
                continue;

            au = vOVPlane_normal_zhat * UNIT_SCALE + vOVPlane_normal_xhat * (x - 400) +
                 vOVPlane_normal_yhat * (y - 300);
            bv = vUOPlane_normal_zhat * UNIT_SCALE + vUOPlane_normal_xhat * (x - 400) +
                 vUOPlane_normal_yhat * (y - 300);
            cw = vUVPlane_normal_zhat * UNIT_SCALE + vUVPlane_normal_xhat * (x - 400) +
                 vUVPlane_normal_yhat * (y - 300);

            assert(cw != 0);

            u = (au * texture_width) / (-cw);
            v = (bv * texture_width) / (-cw);

            if( u < 0 )
                u = 0;
            if( u >= texture_width )
                u = texture_width - 1;
            if( v < 0 )
                v = 0;
            if( v >= texture_width )
                v = texture_width - 1;

            assert(u >= 0 && u < texture_width);
            assert(v >= 0 && v < texture_width);
            int texel = texels[u + v * texture_width];

            int offset = y * screen_width + x;
            assert(offset >= 0 && offset < screen_width * screen_height);
            pixel_buffer[offset] = texel;
        }

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;
    }
}

void
textureTriangle(
    int xA,
    int xB,
    int xC,
    int yA,
    int yB,
    int yC,
    int shadeA,
    int shadeB,
    int shadeC,
    int originX,
    int originY,
    int originZ,
    int txB,
    int txC,
    int tyB,
    int tyC,
    int tzB,
    int tzC,
    int* pixel_buffer,
    int* texels,
    int texture_width)
{
    // _Pix3D.opaque = !_Pix3D.textureHasTransparency[texture];

    int verticalX = originX - txB;
    int verticalY = originY - tyB;
    int verticalZ = originZ - tzB;

    int horizontalX = txC - originX;
    int horizontalY = tyC - originY;
    int horizontalZ = tzC - originZ;

    int u = ((horizontalX * originY) - (horizontalY * originX)) << 14;
    int uStride = ((horizontalY * originZ) - (horizontalZ * originY)) << 8;
    int uStepVertical = ((horizontalZ * originX) - (horizontalX * originZ)) << 5;

    int v = ((verticalX * originY) - (verticalY * originX)) << 14;
    int vStride = ((verticalY * originZ) - (verticalZ * originY)) << 8;
    int vStepVertical = ((verticalZ * originX) - (verticalX * originZ)) << 5;

    int w = ((verticalY * horizontalX) - (verticalX * horizontalY)) << 14;
    int wStride = ((verticalZ * horizontalY) - (verticalY * horizontalZ)) << 8;
    int wStepVertical = ((verticalX * horizontalZ) - (verticalZ * horizontalX)) << 5;

    int dxAB = xB - xA;
    int dyAB = yB - yA;
    int dxAC = xC - xA;
    int dyAC = yC - yA;

    int xStepAB = 0;
    int shadeStepAB = 0;
    if( yB != yA )
    {
        xStepAB = (dxAB << 16) / dyAB;
        shadeStepAB = ((shadeB - shadeA) << 16) / dyAB;
    }

    int xStepBC = 0;
    int shadeStepBC = 0;
    if( yC != yB )
    {
        xStepBC = ((xC - xB) << 16) / (yC - yB);
        shadeStepBC = ((shadeC - shadeB) << 16) / (yC - yB);
    }

    int xStepAC = 0;
    int shadeStepAC = 0;
    if( yC != yA )
    {
        xStepAC = ((xA - xC) << 16) / (yA - yC);
        shadeStepAC = ((shadeA - shadeC) << 16) / (yA - yC);
    }

    // this won't change any rendering, saves not wasting time "drawing" an invalid triangle
    int triangleArea = (dxAB * dyAC) - (dyAB * dxAC);
    if( triangleArea == 0 )
    {
        return;
    }

    if( yA <= yB && yA <= yC )
    {
        if( yA < SCREEN_HEIGHT )
        {
            if( yB > SCREEN_HEIGHT )
            {
                yB = SCREEN_HEIGHT;
            }

            if( yC > SCREEN_HEIGHT )
            {
                yC = SCREEN_HEIGHT;
            }

            if( yB < yC )
            {
                xC = xA <<= 16;
                shadeC = shadeA <<= 16;
                if( yA < 0 )
                {
                    xC -= xStepAC * yA;
                    xA -= xStepAB * yA;
                    shadeC -= shadeStepAC * yA;
                    shadeA -= shadeStepAB * yA;
                    yA = 0;
                }

                xB <<= 16;
                shadeB <<= 16;
                if( yB < 0 )
                {
                    xB -= xStepBC * yB;
                    shadeB -= shadeStepBC * yB;
                    yB = 0;
                }

                int dy = yA - (SCREEN_HEIGHT / 2);
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;

                if( (yA != yB && xStepAC < xStepAB) || (yA == yB && xStepAC > xStepBC) )
                {
                    yC -= yB;
                    yB -= yA;
                    yA = yA * SCREEN_WIDTH;

                    while( --yB >= 0 )
                    {
                        textureRaster(
                            xC >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeC >> 8,
                            shadeA >> 8);
                        xC += xStepAC;
                        xA += xStepAB;
                        shadeC += shadeStepAC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xC >> 16,
                            xB >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeC >> 8,
                            shadeB >> 8);
                        xC += xStepAC;
                        xB += xStepBC;
                        shadeC += shadeStepAC;
                        shadeB += shadeStepBC;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
                else
                {
                    yC -= yB;
                    yB -= yA;
                    yA = yA * SCREEN_WIDTH;

                    while( --yB >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xC >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeC >> 8);
                        xC += xStepAC;
                        xA += xStepAB;
                        shadeC += shadeStepAC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xB >> 16,
                            xC >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeB >> 8,
                            shadeC >> 8);
                        xC += xStepAC;
                        xB += xStepBC;
                        shadeC += shadeStepAC;
                        shadeB += shadeStepBC;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
            }
            else
            {
                xB = xA <<= 16;
                shadeB = shadeA <<= 16;
                if( yA < 0 )
                {
                    xB -= xStepAC * yA;
                    xA -= xStepAB * yA;
                    shadeB -= shadeStepAC * yA;
                    shadeA -= shadeStepAB * yA;
                    yA = 0;
                }

                xC <<= 16;
                shadeC <<= 16;
                if( yC < 0 )
                {
                    xC -= xStepBC * yC;
                    shadeC -= shadeStepBC * yC;
                    yC = 0;
                }

                int dy = yA - (SCREEN_HEIGHT / 2);
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;

                if( (yA == yC || xStepAC >= xStepAB) && (yA != yC || xStepBC <= xStepAB) )
                {
                    yB -= yC;
                    yC -= yA;
                    yA = yA * SCREEN_WIDTH;

                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xB >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeB >> 8);
                        xB += xStepAC;
                        xA += xStepAB;
                        shadeB += shadeStepAC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yB >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xC >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeC >> 8);
                        xC += xStepBC;
                        xA += xStepAB;
                        shadeC += shadeStepBC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
                else
                {
                    yB -= yC;
                    yC -= yA;
                    yA = yA * SCREEN_WIDTH;

                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xB >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeB >> 8,
                            shadeA >> 8);
                        xB += xStepAC;
                        xA += xStepAB;
                        shadeB += shadeStepAC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yB >= 0 )
                    {
                        textureRaster(
                            xC >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yA,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeC >> 8,
                            shadeA >> 8);
                        xC += xStepBC;
                        xA += xStepAB;
                        shadeC += shadeStepBC;
                        shadeA += shadeStepAB;
                        yA += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
            }
        }
    }
    else if( yB <= yC )
    {
        if( yB < SCREEN_HEIGHT )
        {
            if( yC > SCREEN_HEIGHT )
            {
                yC = SCREEN_HEIGHT;
            }

            if( yA > SCREEN_HEIGHT )
            {
                yA = SCREEN_HEIGHT;
            }

            if( yC < yA )
            {
                xA = xB <<= 16;
                shadeA = shadeB <<= 16;
                if( yB < 0 )
                {
                    xA -= xStepAB * yB;
                    xB -= xStepBC * yB;
                    shadeA -= shadeStepAB * yB;
                    shadeB -= shadeStepBC * yB;
                    yB = 0;
                }

                xC <<= 16;
                shadeC <<= 16;
                if( yC < 0 )
                {
                    xC -= xStepAC * yC;
                    shadeC -= shadeStepAC * yC;
                    yC = 0;
                }

                int dy = yB - (SCREEN_HEIGHT / 2);
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;

                if( (yB != yC && xStepAB < xStepBC) || (yB == yC && xStepAB > xStepAC) )
                {
                    yA -= yC;
                    yC -= yB;
                    yB = yB * SCREEN_WIDTH;

                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xB >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeB >> 8);
                        xA += xStepAB;
                        xB += xStepBC;
                        shadeA += shadeStepAB;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yA >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xC >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeC >> 8);
                        xA += xStepAB;
                        xC += xStepAC;
                        shadeA += shadeStepAB;
                        shadeC += shadeStepAC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
                else
                {
                    yA -= yC;
                    yC -= yB;
                    yB = yB * SCREEN_WIDTH;

                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xB >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeB >> 8,
                            shadeA >> 8);
                        xA += xStepAB;
                        xB += xStepBC;
                        shadeA += shadeStepAB;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yA >= 0 )
                    {
                        textureRaster(
                            xC >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeC >> 8,
                            shadeA >> 8);
                        xA += xStepAB;
                        xC += xStepAC;
                        shadeA += shadeStepAB;
                        shadeC += shadeStepAC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
            }
            else
            {
                xC = xB <<= 16;
                shadeC = shadeB <<= 16;
                if( yB < 0 )
                {
                    xC -= xStepAB * yB;
                    xB -= xStepBC * yB;
                    shadeC -= shadeStepAB * yB;
                    shadeB -= shadeStepBC * yB;
                    yB = 0;
                }

                xA <<= 16;
                shadeA <<= 16;
                if( yA < 0 )
                {
                    xA -= xStepAC * yA;
                    shadeA -= shadeStepAC * yA;
                    yA = 0;
                }

                int dy = yB - (SCREEN_HEIGHT / 2);
                u += uStepVertical * dy;
                v += vStepVertical * dy;
                w += wStepVertical * dy;

                if( xStepAB < xStepBC )
                {
                    yC -= yA;
                    yA -= yB;
                    yB = yB * SCREEN_WIDTH;

                    while( --yA >= 0 )
                    {
                        textureRaster(
                            xC >> 16,
                            xB >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeC >> 8,
                            shadeB >> 8);
                        xC += xStepAB;
                        xB += xStepBC;
                        shadeC += shadeStepAB;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xA >> 16,
                            xB >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeA >> 8,
                            shadeB >> 8);
                        xA += xStepAC;
                        xB += xStepBC;
                        shadeA += shadeStepAC;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
                else
                {
                    yC -= yA;
                    yA -= yB;
                    yB = yB * SCREEN_WIDTH;

                    while( --yA >= 0 )
                    {
                        textureRaster(
                            xB >> 16,
                            xC >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeB >> 8,
                            shadeC >> 8);
                        xC += xStepAB;
                        xB += xStepBC;
                        shadeC += shadeStepAB;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                    while( --yC >= 0 )
                    {
                        textureRaster(
                            xB >> 16,
                            xA >> 16,
                            pixel_buffer,
                            yB,
                            texels,
                            0,
                            0,
                            u,
                            v,
                            w,
                            uStride,
                            vStride,
                            wStride,
                            shadeB >> 8,
                            shadeA >> 8);
                        xA += xStepAC;
                        xB += xStepBC;
                        shadeA += shadeStepAC;
                        shadeB += shadeStepBC;
                        yB += SCREEN_WIDTH;
                        u += uStepVertical;
                        v += vStepVertical;
                        w += wStepVertical;
                    }
                }
            }
        }
    }
    else if( yC < SCREEN_HEIGHT )
    {
        if( yA > SCREEN_HEIGHT )
        {
            yA = SCREEN_HEIGHT;
        }

        if( yB > SCREEN_HEIGHT )
        {
            yB = SCREEN_HEIGHT;
        }

        if( yA < yB )
        {
            xB = xC <<= 16;
            shadeB = shadeC <<= 16;
            if( yC < 0 )
            {
                xB -= xStepBC * yC;
                xC -= xStepAC * yC;
                shadeB -= shadeStepBC * yC;
                shadeC -= shadeStepAC * yC;
                yC = 0;
            }

            xA <<= 16;
            shadeA <<= 16;
            if( yA < 0 )
            {
                xA -= xStepAB * yA;
                shadeA -= shadeStepAB * yA;
                yA = 0;
            }

            int dy = yC - (SCREEN_HEIGHT / 2);
            u += uStepVertical * dy;
            v += vStepVertical * dy;
            w += wStepVertical * dy;

            if( xStepBC < xStepAC )
            {
                yB -= yA;
                yA -= yC;
                yC = yC * SCREEN_WIDTH;

                while( --yA >= 0 )
                {
                    textureRaster(
                        xB >> 16,
                        xC >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeB >> 8,
                        shadeC >> 8);
                    xB += xStepBC;
                    xC += xStepAC;
                    shadeB += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
                while( --yB >= 0 )
                {
                    textureRaster(
                        xB >> 16,
                        xA >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeB >> 8,
                        shadeA >> 8);
                    xB += xStepBC;
                    xA += xStepAB;
                    shadeB += shadeStepBC;
                    shadeA += shadeStepAB;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
            }
            else
            {
                yB -= yA;
                yA -= yC;
                yC = yC * SCREEN_WIDTH;

                while( --yA >= 0 )
                {
                    textureRaster(
                        xC >> 16,
                        xB >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeC >> 8,
                        shadeB >> 8);
                    xB += xStepBC;
                    xC += xStepAC;
                    shadeB += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
                while( --yB >= 0 )
                {
                    textureRaster(
                        xA >> 16,
                        xB >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeA >> 8,
                        shadeB >> 8);
                    xB += xStepBC;
                    xA += xStepAB;
                    shadeB += shadeStepBC;
                    shadeA += shadeStepAB;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
            }
        }
        else
        {
            xA = xC <<= 16;
            shadeA = shadeC <<= 16;
            if( yC < 0 )
            {
                xA -= xStepBC * yC;
                xC -= xStepAC * yC;
                shadeA -= shadeStepBC * yC;
                shadeC -= shadeStepAC * yC;
                yC = 0;
            }

            xB <<= 16;
            shadeB <<= 16;
            if( yB < 0 )
            {
                xB -= xStepAB * yB;
                shadeB -= shadeStepAB * yB;
                yB = 0;
            }

            int dy = yC - (SCREEN_HEIGHT / 2);
            u += uStepVertical * dy;
            v += vStepVertical * dy;
            w += wStepVertical * dy;

            if( xStepBC < xStepAC )
            {
                yA -= yB;
                yB -= yC;
                yC = yC * SCREEN_WIDTH;

                while( --yB >= 0 )
                {
                    textureRaster(
                        xA >> 16,
                        xC >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeA >> 8,
                        shadeC >> 8);
                    xA += xStepBC;
                    xC += xStepAC;
                    shadeA += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
                while( --yA >= 0 )
                {
                    textureRaster(
                        xB >> 16,
                        xC >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeB >> 8,
                        shadeC >> 8);
                    xB += xStepAB;
                    xC += xStepAC;
                    shadeB += shadeStepAB;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
            }
            else
            {
                yA -= yB;
                yB -= yC;
                yC = yC * SCREEN_WIDTH;

                while( --yB >= 0 )
                {
                    textureRaster(
                        xC >> 16,
                        xA >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeC >> 8,
                        shadeA >> 8);
                    xA += xStepBC;
                    xC += xStepAC;
                    shadeA += shadeStepBC;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
                while( --yA >= 0 )
                {
                    textureRaster(
                        xC >> 16,
                        xB >> 16,
                        pixel_buffer,
                        yC,
                        texels,
                        0,
                        0,
                        u,
                        v,
                        w,
                        uStride,
                        vStride,
                        wStride,
                        shadeC >> 8,
                        shadeB >> 8);
                    xB += xStepAB;
                    xC += xStepAC;
                    shadeB += shadeStepAB;
                    shadeC += shadeStepAC;
                    yC += SCREEN_WIDTH;
                    u += uStepVertical;
                    v += vStepVertical;
                    w += wStepVertical;
                }
            }
        }
    }
}

void
textureRaster(
    int xA,
    int xB,
    int* dst,
    int offset,
    int* texels,
    int curU,
    int curV,
    int u,
    int v,
    int w,
    int uStride,
    int vStride,
    int wStride,
    int shadeA,
    int shadeB)
{
    if( xA >= xB )
    {
        return;
    }

    int shadeStrides;
    int strides;
    if( true )
    {
        shadeStrides = (shadeB - shadeA) / (xB - xA);

        if( xB > SCREEN_WIDTH )
        {
            xB = SCREEN_WIDTH;
        }

        if( xA < 0 )
        {
            shadeA -= xA * shadeStrides;
            xA = 0;
        }

        if( xA >= xB )
        {
            return;
        }

        strides = (xB - xA) >> 3;
        shadeStrides <<= 12;
        shadeA <<= 9;
    }
    else
    {
        if( xB - xA > 7 )
        {
            strides = (xB - xA) >> 3;
            // shadeStrides = (shadeB - shadeA) * _Pix3D.reciprical15[strides] >> 6;
        }
        else
        {
            strides = 0;
            shadeStrides = 0;
        }

        shadeA <<= 9;
    }

    offset += xA;

    if( false )
    {
        int nextU = 0;
        int nextV = 0;
        int dx = xA - (SCREEN_WIDTH / 2);

        u = u + (uStride >> 3) * dx;
        v = v + (vStride >> 3) * dx;
        w = w + (wStride >> 3) * dx;

        int curW = w >> 12;
        if( curW != 0 )
        {
            curU = u / curW;
            curV = v / curW;
            if( curU < 0 )
            {
                curU = 0;
            }
            else if( curU > 0xfc0 )
            {
                curU = 0xfc0;
            }
        }

        u = u + uStride;
        v = v + vStride;
        w = w + wStride;

        curW = w >> 12;
        if( curW != 0 )
        {
            nextU = u / curW;
            nextV = v / curW;
            if( nextU < 0x7 )
            {
                nextU = 0x7;
            }
            else if( nextU > 0xfc0 )
            {
                nextU = 0xfc0;
            }
        }

        int stepU = (nextU - curU) >> 3;
        int stepV = (nextV - curV) >> 3;
        curU += (shadeA >> 3) & 0xc0000;
        int shadeShift = shadeA >> 23;

        if( true )
        {
            while( strides-- > 0 )
            {
                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU = nextU;
                curV = nextV;

                u += uStride;
                v += vStride;
                w += wStride;

                curW = w >> 12;
                if( curW != 0 )
                {
                    nextU = u / curW;
                    nextV = v / curW;
                    if( nextU < 0x7 )
                    {
                        nextU = 0x7;
                    }
                    else if( nextU > 0xfc0 )
                    {
                        nextU = 0xfc0;
                    }
                }

                stepU = (nextU - curU) >> 3;
                stepV = (nextV - curV) >> 3;
                shadeA += shadeStrides;
                curU += (shadeA >> 3) & 0xc0000;
                shadeShift = shadeA >> 23;
            }

            strides = xB - xA & 0x7;
            while( strides-- > 0 )
            {
                dst[offset++] = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift;
                curU += stepU;
                curV += stepV;
            }
        }
        else
        {
            while( strides-- > 0 )
            {
                int rgb;
                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU = nextU;
                curV = nextV;

                u += uStride;
                v += vStride;
                w += wStride;

                curW = w >> 12;
                if( curW != 0 )
                {
                    nextU = u / curW;
                    nextV = v / curW;
                    if( nextU < 7 )
                    {
                        nextU = 7;
                    }
                    else if( nextU > 0xfc0 )
                    {
                        nextU = 0xfc0;
                    }
                }

                stepU = (nextU - curU) >> 3;
                stepV = (nextV - curV) >> 3;
                shadeA += shadeStrides;
                curU += (shadeA >> 3) & 0xc0000;
                shadeShift = shadeA >> 23;
            }

            strides = (xB - xA) & 0x7;
            while( strides-- > 0 )
            {
                int rgb;
                if( (rgb = (uint32_t)texels[(curV & 0xfc0) + (curU >> 6)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }

                offset++;
                curU += stepU;
                curV += stepV;
            }
        }
    }
    else
    {
        int nextU = 0;
        int nextV = 0;
        int dx = xA - (SCREEN_WIDTH / 2);

        u = u + (uStride >> 3) * dx;
        v = v + (vStride >> 3) * dx;
        w = w + (wStride >> 3) * dx;

        int curW = w >> 14;
        if( curW != 0 )
        {
            curU = u / curW;
            curV = v / curW;
            if( curU < 0 )
            {
                curU = 0;
            }
            else if( curU > 0x3f80 )
            {
                curU = 0x3f80;
            }
        }

        u = u + uStride;
        v = v + vStride;
        w = w + wStride;

        curW = w >> 14;
        if( curW != 0 )
        {
            nextU = u / curW;
            nextV = v / curW;
            if( nextU < 0x7 )
            {
                nextU = 0x7;
            }
            else if( nextU > 0x3f80 )
            {
                nextU = 0x3f80;
            }
        }

        int stepU = (nextU - curU) >> 3;
        int stepV = (nextV - curV) >> 3;
        curU += shadeA & 0x600000;
        int shadeShift = shadeA >> 23;

        if( true )
        {
            while( strides-- > 0 )
            {
                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;

                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU = nextU;
                curV = nextV;

                u += uStride;
                v += vStride;
                w += wStride;

                curW = w >> 14;
                if( curW != 0 )
                {
                    nextU = u / curW;
                    nextV = v / curW;
                    if( nextU < 0x7 )
                    {
                        nextU = 0x7;
                    }
                    else if( nextU > 0x3f80 )
                    {
                        nextU = 0x3f80;
                    }
                }

                stepU = (nextU - curU) >> 3;
                stepV = (nextV - curV) >> 3;
                shadeA += shadeStrides;
                curU += shadeA & 0x600000;
                shadeShift = shadeA >> 23;
            }

            strides = xB - xA & 0x7;
            while( strides-- > 0 )
            {
                dst[offset++] = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift;
                curU += stepU;
                curV += stepV;
            }
        }
        else
        {
            while( strides-- > 0 )
            {
                int rgb;
                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU += stepU;
                curV += stepV;

                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }
                offset++;
                curU = nextU;
                curV = nextV;

                u += uStride;
                v += vStride;
                w += wStride;

                curW = w >> 14;
                if( curW != 0 )
                {
                    nextU = u / curW;
                    nextV = v / curW;
                    if( nextU < 0x7 )
                    {
                        nextU = 0x7;
                    }
                    else if( nextU > 0x3f80 )
                    {
                        nextU = 0x3f80;
                    }
                }

                stepU = (nextU - curU) >> 3;
                stepV = (nextV - curV) >> 3;
                shadeA += shadeStrides;
                curU += shadeA & 0x600000;
                shadeShift = shadeA >> 23;
            }

            strides = xB - xA & 0x7;
            while( strides-- > 0 )
            {
                int rgb;
                if( (rgb = (uint32_t)texels[(curV & 0x3f80) + (curU >> 7)] >> shadeShift) != 0 )
                {
                    dst[offset] = rgb;
                }

                offset++;
                curU += stepU;
                curV += stepV;
            }
        }
    }
}
