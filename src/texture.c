#include "texture.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SCREEN_HEIGHT 600
#define SCREEN_WIDTH 800

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int
interpolate_ish12(int x_begin, int x_end, int t_sh16)
{
    int stride = x_end - x_begin;
    return ((x_begin << 12) + stride * (t_sh16 >> 4)) >> 12;
}

// Helper struct to hold edge stepping state
typedef struct
{
    int x;     // Current x position
    int z;     // Current z position
    int u;     // Current u texture coordinate
    int v;     // Current v texture coordinate
    int dx;    // x step per scanline
    int dz;    // z step per scanline
    int du;    // u step per scanline
    int dv;    // v step per scanline
    int x_rem; // Remainder for x stepping
    int z_rem; // Remainder for z stepping
    int u_rem; // Remainder for u stepping
    int v_rem; // Remainder for v stepping
    int dy;    // Total y distance
} EdgeStepper;

static void
init_edge_stepper(
    EdgeStepper* stepper,
    int x0,
    int y0,
    int z0,
    int u0,
    int v0,
    int x1,
    int y1,
    int z1,
    int u1,
    int v1)
{
    int dy = y1 - y0;
    if( dy == 0 )
    {
        // Handle horizontal edge case
        stepper->dx = 0;
        stepper->dz = 0;
        stepper->du = 0;
        stepper->dv = 0;
        stepper->x_rem = 0;
        stepper->z_rem = 0;
        stepper->u_rem = 0;
        stepper->v_rem = 0;
        stepper->dy = 0;
        return;
    }

    // Calculate steps using fixed-point arithmetic (16.16)
    int dx = x1 - x0;
    int dz = z1 - z0;
    int du = u1 - u0;
    int dv = v1 - v0;

    // Initialize current values
    stepper->x = x0 << 16;
    stepper->z = z0 << 16;
    stepper->u = u0 << 16;
    stepper->v = v0 << 16;
    stepper->dy = dy;

    // Calculate steps per scanline
    stepper->dx = (dx << 16) / dy;
    stepper->dz = (dz << 16) / dy;
    stepper->du = (du << 16) / dy;
    stepper->dv = (dv << 16) / dy;

    // Initialize remainders
    stepper->x_rem = 0;
    stepper->z_rem = 0;
    stepper->u_rem = 0;
    stepper->v_rem = 0;
}

static void
step_edge(EdgeStepper* stepper)
{
    stepper->x += stepper->dx;
    stepper->z += stepper->dz;
    stepper->u += stepper->du;
    stepper->v += stepper->dv;
}

// void
// raster_texture2(
//     int* pixel_buffer,
//     int screen_width,
//     int screen_height,
//     int screen_x0,
//     int screen_x1,
//     int screen_x2,
//     int screen_y0,
//     int screen_y1,
//     int screen_y2,
//     int screen_z0,
//     int screen_z1,
//     int screen_z2,
//     int orthographic_x0,
//     int orthographic_x1,
//     int orthographic_x2,
//     int orthographic_y0,
//     int orthographic_y1,
//     int orthographic_y2,
//     int orthographic_z0,
//     int orthographic_z1,
//     int orthographic_z2,
//     int u0,
//     int u1,
//     int u2,
//     int v0,
//     int v1,
//     int v2,
//     int* texels,
//     int texture_width)
// {
//     // Sort vertices by y-coordinate
//     if( screen_y1 < screen_y0 )
//     {
//         SWAP(screen_y0, screen_y1);
//         SWAP(screen_x0, screen_x1);
//         SWAP(screen_z0, screen_z1);
//         SWAP(orthographic_x0, orthographic_x1);
//         SWAP(orthographic_y0, orthographic_y1);
//         SWAP(orthographic_z0, orthographic_z1);
//         SWAP(u0, u1);
//         SWAP(v0, v1);
//     }
//     if( screen_y2 < screen_y0 )
//     {
//         SWAP(screen_y0, screen_y2);
//         SWAP(screen_x0, screen_x2);
//         SWAP(screen_z0, screen_z2);
//         SWAP(orthographic_x0, orthographic_x2);
//         SWAP(orthographic_y0, orthographic_y2);
//         SWAP(orthographic_z0, orthographic_z2);
//         SWAP(u0, u2);
//         SWAP(v0, v2);
//     }
//     if( screen_y2 < screen_y1 )
//     {
//         SWAP(screen_y1, screen_y2);
//         SWAP(screen_x1, screen_x2);
//         SWAP(screen_z1, screen_z2);
//         SWAP(orthographic_x1, orthographic_x2);
//         SWAP(orthographic_y1, orthographic_y2);
//         SWAP(orthographic_z1, orthographic_z2);
//         SWAP(u1, u2);
//         SWAP(v1, v2);
//     }

//     /**
//      * Texture coordinates are mapped from face coordinates.
//      * Note: Modal coordinates are the same as screen coordinates prior to projection (aka the
//      * orthographic coordinates).
//      *
//      * We have been given model coordinates, so we need to convert them to face coordinates,
//      which
//      * we can then map to texture coordinates.
//      *
//      * Math: A normal vector defines the coefficients of a plane equation.
//      * For example, if the normal vector is (A, B, C), the plane equation is Ax + By + Cz + D =
//      0.
//      *
//      * Math: The cross product of two vectors give a vector that is perpendicular to both.
//      * With that, if we have two vectors on a plane, we can find the normal vector by taking the
//      * cross product.
//      */

//     /**
//      * AB and CA are vectors defining the face plane in model coordinates. AB and CA are the
//      basis
//      * of the face coordinates, and we represent the AB and CA vectors in model coordinates
//      because,
//      * as we scan across the screen, we are traversing model coordinates.
//      * And we know how to map AB => (u, v), and CA => (u, v).
//      */
//     int ABx = orthographic_x0 - orthographic_x1;
//     int ABy = orthographic_y0 - orthographic_y1;
//     int ABz = orthographic_z0 - orthographic_z1;

//     int CAx = orthographic_x2 - orthographic_x0;
//     int CAy = orthographic_y2 - orthographic_y0;
//     int CAz = orthographic_z2 - orthographic_z0;

//     /**
//      * U is the cross product of OA and AB in model coordinates.
//      *
//      * We want this because as we step across the screen, we are traversing model coordinates,
//      and
//      * we want to convert that to the AB, CA basis. This plane will give us the "AB" component of
//      * the face coordinates as we scan the screen.
//      *
//      *
//      * The abbreviation "hat" means "basis vector".
//      *  "sz" means "screen z basis vector".
//      *  "sx" means "screen x basis vector".
//      *  "sy" means "screen y basis vector".
//      *
//      * We use the term "stride" to mean x direction across the screen and "step" to mean y.
//      */

//     // u
//     int determinant_CAxy_originxy = (CAx * orthographic_y0 - CAy * orthographic_x0);

//     int u_szhat = determinant_CAxy_originxy << 14;

//     // u_stride
//     int determinant_CAyz_originyz = (CAy * orthographic_z0 - CAz * orthographic_y0);

//     int u_sxhat = determinant_CAyz_originyz << 8;

//     // u_step
//     int determinant_CAzx_originxz = (CAz * orthographic_x0 - CAx * orthographic_z0);

//     int u_syhat = determinant_CAzx_originxz << 5;

//     // v
//     int determinant_ABxy_originxy = (ABx * orthographic_y0 - ABy * orthographic_x0);
//     int v_szhat = determinant_ABxy_originxy << 14;

//     // v_stride
//     int determinant_AByz_originyz = (ABy * orthographic_z0 - ABz * orthographic_y0);

//     int v_sxhat_stride = determinant_AByz_originyz << 8;

//     // v_step
//     int determinant_ABzx_originzx = (ABz * orthographic_x0 - ABx * orthographic_z0);

//     int v_syhat_step = determinant_ABzx_originzx << 5;

//     /**
//      * For perspective correct projection, we also need to know the "z" coordinate of the point
//      in
//      * model coordinates. As we scan across the screen, the normal vector given by AB cross CA
//      gives
//      * the plane equation for the face in model coordinates. We then look at the z component of
//      the
//      * point on that plane in model coordinates which we use for perspective projection.
//      */

//     // w
//     int determinant_ABxy_CAxy = (ABx * CAy - ABy * CAx);
//     int w_szhat = determinant_ABxy_CAxy;

//     // w_stride
//     int determinant_AByz_CAyz = (ABy * CAz - ABz * CAy);
//     int w_sxhat_stride = determinant_AByz_CAyz;

//     // w_step
//     int determinant_ABzx_CAzx = (ABz * CAx - ABx * CAz);
//     int w_syhat_step = determinant_ABzx_CAzx;
// }

void
draw_scanline_texture(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
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
    if( screen_x_start == screen_x_end )
        return;
    if( screen_x_start > screen_x_end )
    {
        int tmp;
        tmp = screen_x_start;
        screen_x_start = screen_x_end;
        screen_x_end = tmp;
        tmp = u_start;
        u_start = u_end;
        u_end = tmp;
        tmp = v_start;
        v_start = v_end;
        v_end = tmp;
        tmp = z_start;
        z_start = z_end;
        z_end = tmp;
    }

    int dx_stride = screen_x_end - screen_x_start;
    assert(dx_stride > 0);

    int du = u_end - u_start;
    int dv = v_end - v_start;
    int dz = z_end - z_start;

    int step_u_ish20 = 0;
    int step_v_ish20 = 0;
    int step_z_ish16 = 0;
    if( dx_stride > 0 )
    {
        step_u_ish20 = (du << 20) / dx_stride;
        step_v_ish20 = (dv << 20) / dx_stride;
        step_z_ish16 = (dz << 16) / dx_stride;
    }

    if( screen_x_end > screen_width )
        screen_x_end = screen_width;

    int u_ish20 = u_start << 20;
    int v_ish20 = v_start << 20;
    int z_ish16 = z_start << 16;

    if( screen_x_start < 0 )
    {
        u_ish20 -= step_u_ish20 * screen_x_start;
        v_ish20 -= step_v_ish20 * screen_x_start;
        z_ish16 -= step_z_ish16 * screen_x_start;
        screen_x_start = 0;
    }

    if( screen_x_start > screen_x_end )
        return;

    // Steps by 4.
    int offset = screen_x_start + y * screen_width;
    int steps = dx_stride;

    while( --steps >= 0 )
    {
        int z = z_ish16 >> 16;
        int utexture;
        int vtexture;
        if( z == 0 )
        {
            utexture = 0;
            vtexture = 0;
        }
        else
        {
            utexture = (u_ish20 / z) >> 20;
            vtexture = (v_ish20 / z) >> 20;
        }

        assert(utexture >= 0 && utexture < texture_width);
        assert(vtexture >= 0 && vtexture < texture_width);

        int texel_offset = utexture + vtexture * texture_width;
        int texel = texels[texel_offset];

        pixel_buffer[offset] = texel;

        u_ish20 += step_u_ish20;
        v_ish20 += step_v_ish20;
        z_ish16 += step_z_ish16;
        offset += 1;
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
    int u0,
    int u1,
    int u2,
    int v0,
    int v1,
    int v2,
    int* texels,
    int texture_width)
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

    const int UNIT_SCALE = 512;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    int vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    int vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    int vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    int vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

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

    return;

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

            cw = 1;
            assert(cw != 0);

            u = -(au * texture_width) / cw;
            v = -(bv * texture_width) / cw;

            u = 0;
            v = 0;

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
raster_texture2(
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
    int orthographic_x0,
    int orthographic_x1,
    int orthographic_x2,
    int orthographic_y0,
    int orthographic_y1,
    int orthographic_y2,
    int orthographic_z0,
    int orthographic_z1,
    int orthographic_z2,
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
    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
        SWAP(screen_z0, screen_z1);
        SWAP(orthographic_x0, orthographic_x1);
        SWAP(orthographic_y0, orthographic_y1);
        SWAP(orthographic_z0, orthographic_z1);
        SWAP(u0, u1);
        SWAP(v0, v1);
    }
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
        SWAP(screen_z0, screen_z2);
        SWAP(orthographic_x0, orthographic_x2);
        SWAP(orthographic_y0, orthographic_y2);
        SWAP(orthographic_z0, orthographic_z2);
        SWAP(u0, u2);
        SWAP(v0, v2);
    }
    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
        SWAP(screen_z1, screen_z2);
        SWAP(orthographic_x1, orthographic_x2);
        SWAP(orthographic_y1, orthographic_y2);
        SWAP(orthographic_z1, orthographic_z2);
        SWAP(u1, u2);
        SWAP(v1, v2);
    }

    int total_height = screen_y2 - screen_y0;
    if( total_height == 0 )
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

    // skip if the triangle is degenerate
    if( screen_x0 == screen_x1 && screen_x1 == screen_x2 )
        return;

    int dx_AC = screen_x2 - screen_x0;
    int dy_AC = screen_y2 - screen_y0;
    int dz_AC = screen_z2 - screen_z0;
    int dx_AB = screen_x1 - screen_x0;
    int dy_AB = screen_y1 - screen_y0;
    int dz_AB = screen_z1 - screen_z0;
    int dx_BC = screen_x2 - screen_x1;
    int dy_BC = screen_y2 - screen_y1;
    int dz_BC = screen_z2 - screen_z1;

    int du_AC = u2 - u0;
    int dv_AC = v2 - v0;
    int dw_AC = orthographic_z2 - orthographic_z0;
    int du_AB = u1 - u0;
    int dv_AB = v1 - v0;
    int dw_AB = orthographic_z1 - orthographic_z0;
    int du_BC = u2 - u1;
    int dv_BC = v2 - v1;
    int dw_BC = orthographic_z2 - orthographic_z1;

    // AC is the longest edge
    int step_edge_x_AC_ish16;
    int step_edge_x_AB_ish16;
    int step_edge_x_BC_ish16;

    int step_edge_orthographic_x_AC_ish16;
    int step_edge_orthographic_x_AB_ish16;
    int step_edge_orthographic_x_BC_ish16;

    int step_edge_z_AC_ish16;
    int step_edge_z_AB_ish16;
    int step_edge_z_BC_ish16;

    int step_edge_u_AC_ish16;
    int step_edge_u_AB_ish16;
    int step_edge_u_BC_ish16;

    int step_edge_v_AC_ish16;
    int step_edge_v_AB_ish16;
    int step_edge_v_BC_ish16;

    int step_edge_w_AC_ish16;
    int step_edge_w_AB_ish16;
    int step_edge_w_BC_ish16;

    if( dy_AC > 0 )
    {
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
        step_edge_z_AC_ish16 = (dz_AC << 16) / dy_AC;
        step_edge_u_AC_ish16 = (du_AC << 16) / dy_AC;
        step_edge_v_AC_ish16 = (dv_AC << 16) / dy_AC;
        step_edge_w_AC_ish16 = (dw_AC << 16) / dy_AC;
        step_edge_orthographic_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    }
    else
    {
        step_edge_x_AC_ish16 = 0;
        step_edge_z_AC_ish16 = 0;
        step_edge_u_AC_ish16 = 0;
        step_edge_v_AC_ish16 = 0;
        step_edge_w_AC_ish16 = 0;
        step_edge_orthographic_x_AC_ish16 = 0;
    }

    if( dy_AB > 0 )
    {
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
        step_edge_z_AB_ish16 = (dz_AB << 16) / dy_AB;
        step_edge_u_AB_ish16 = (du_AB << 16) / dy_AB;
        step_edge_v_AB_ish16 = (dv_AB << 16) / dy_AB;
        step_edge_w_AB_ish16 = (dw_AB << 16) / dy_AB;
        step_edge_orthographic_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    }
    else
    {
        step_edge_x_AB_ish16 = 0;
        step_edge_z_AB_ish16 = 0;
        step_edge_u_AB_ish16 = 0;
        step_edge_v_AB_ish16 = 0;
        step_edge_w_AB_ish16 = 0;
        step_edge_orthographic_x_AB_ish16 = 0;
    }

    if( dy_BC > 0 )
    {
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;
        step_edge_z_BC_ish16 = (dz_BC << 16) / dy_BC;
        step_edge_u_BC_ish16 = (du_BC << 16) / dy_BC;
        step_edge_v_BC_ish16 = (dv_BC << 16) / dy_BC;
        step_edge_w_BC_ish16 = (dw_BC << 16) / dy_BC;
        step_edge_orthographic_x_BC_ish16 = (dx_BC << 16) / dy_BC;
    }
    else
    {
        step_edge_x_BC_ish16 = 0;
        step_edge_z_BC_ish16 = 0;
        step_edge_u_BC_ish16 = 0;
        step_edge_v_BC_ish16 = 0;
        step_edge_w_BC_ish16 = 0;
        step_edge_orthographic_x_BC_ish16 = 0;
    }

    /*
     *          /\      y0 (A)
     *         /  \
     *        /    \    y1 (B) (second_half = true above, false below)
     *       /   /
     *      / /  y2 (C) (second_half = false)
     */
    int edge_x_AC_ish16 = screen_x0 << 16;
    int edge_x_AB_ish16 = screen_x0 << 16;
    int edge_x_BC_ish16 = screen_x1 << 16;

    int edge_z_AC_ish16 = screen_z0 << 16;
    int edge_z_AB_ish16 = screen_z0 << 16;
    int edge_z_BC_ish16 = screen_z1 << 16;

    int edge_orthographic_x_AC_ish16 = orthographic_x0 << 16;
    int edge_orthographic_x_AB_ish16 = orthographic_x0 << 16;
    int edge_orthographic_x_BC_ish16 = orthographic_x1 << 16;

    int edge_u_AC_ish16 = u0 << 16;
    int edge_u_AB_ish16 = u0 << 16;
    int edge_u_BC_ish16 = u1 << 16;

    int edge_v_AC_ish16 = v0 << 16;
    int edge_v_AB_ish16 = v0 << 16;
    int edge_v_BC_ish16 = v1 << 16;

    int edge_w_AC_ish16 = orthographic_z0 << 16;
    int edge_w_AB_ish16 = orthographic_z0 << 16;
    int edge_w_BC_ish16 = orthographic_z1 << 16;

    if( screen_y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * screen_y0;

        edge_orthographic_x_AC_ish16 -= step_edge_orthographic_x_AC_ish16 * screen_y0;
        edge_orthographic_x_AB_ish16 -= step_edge_orthographic_x_AB_ish16 * screen_y0;

        edge_z_AC_ish16 -= step_edge_z_AC_ish16 * screen_y0;
        edge_z_AB_ish16 -= step_edge_z_AB_ish16 * screen_y0;

        edge_u_AC_ish16 -= step_edge_u_AC_ish16 * screen_y0;
        edge_u_AB_ish16 -= step_edge_u_AB_ish16 * screen_y0;

        edge_v_AC_ish16 -= step_edge_v_AC_ish16 * screen_y0;
        edge_v_AB_ish16 -= step_edge_v_AB_ish16 * screen_y0;

        edge_w_AC_ish16 -= step_edge_w_AC_ish16 * screen_y0;
        edge_w_AB_ish16 -= step_edge_w_AB_ish16 * screen_y0;

        screen_y0 = 0;
    }

    if( screen_y0 > screen_y1 )
        return;

    if( screen_y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;
        edge_orthographic_x_BC_ish16 -= step_edge_orthographic_x_BC_ish16 * screen_y1;
        edge_z_BC_ish16 -= step_edge_z_BC_ish16 * screen_y1;
        edge_u_BC_ish16 -= step_edge_u_BC_ish16 * screen_y1;
        edge_v_BC_ish16 -= step_edge_v_BC_ish16 * screen_y1;
        edge_w_BC_ish16 -= step_edge_w_BC_ish16 * screen_y1;

        screen_y1 = 0;
    }

    int i = screen_y0;
    for( ; i < screen_y1 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_AC_ish16 >> 16;
        int x_end_current = edge_x_AB_ish16 >> 16;

        int orthographic_x_start_current = edge_orthographic_x_AC_ish16 >> 16;
        int orthographic_x_end_current = edge_orthographic_x_AB_ish16 >> 16;

        int w_start_current = edge_w_AC_ish16 >> 16;
        int w_end_current = edge_w_AB_ish16 >> 16;

        assert(w_start_current > 0);
        assert(w_end_current > 0);

        int u_start_current = edge_u_AC_ish16 >> 16;
        int u_end_current = edge_u_AB_ish16 >> 16;

        int v_start_current = edge_v_AC_ish16 >> 16;
        int v_end_current = edge_v_AB_ish16 >> 16;

        draw_scanline_texture(
            pixel_buffer,
            screen_width,
            screen_height,
            i,
            x_start_current,
            x_end_current,
            w_start_current,
            w_end_current,
            u_start_current,
            u_end_current,
            v_start_current,
            v_end_current,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        edge_u_AC_ish16 += step_edge_u_AC_ish16;
        edge_u_AB_ish16 += step_edge_u_AB_ish16;

        edge_v_AC_ish16 += step_edge_v_AC_ish16;
        edge_v_AB_ish16 += step_edge_v_AB_ish16;

        edge_w_AC_ish16 += step_edge_w_AC_ish16;
        edge_w_AB_ish16 += step_edge_w_AB_ish16;
    }

    for( ; i < screen_y2 && i < screen_height; ++i )
    {
        int x_start_current = edge_x_BC_ish16 >> 16;
        int x_end_current = edge_x_AC_ish16 >> 16;

        int orthographic_x_start_current = edge_orthographic_x_BC_ish16 >> 16;
        int orthographic_x_end_current = edge_orthographic_x_AC_ish16 >> 16;

        int w_start_current = edge_w_BC_ish16 >> 16;
        int w_end_current = edge_w_AC_ish16 >> 16;

        assert(w_start_current > 0);
        assert(w_end_current > 0);

        int u_start_current = edge_u_BC_ish16 >> 16;
        int u_end_current = edge_u_AC_ish16 >> 16;

        int v_start_current = edge_v_BC_ish16 >> 16;
        int v_end_current = edge_v_AC_ish16 >> 16;

        draw_scanline_texture(
            pixel_buffer,
            screen_width,
            screen_height,
            i,
            x_start_current,
            x_end_current,
            w_start_current,
            w_end_current,
            u_start_current,
            u_end_current,
            v_start_current,
            v_end_current,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        edge_u_AC_ish16 += step_edge_u_AC_ish16;
        edge_u_BC_ish16 += step_edge_u_BC_ish16;

        edge_v_AC_ish16 += step_edge_v_AC_ish16;
        edge_v_BC_ish16 += step_edge_v_BC_ish16;

        edge_w_AC_ish16 += step_edge_w_AC_ish16;
        edge_w_BC_ish16 += step_edge_w_BC_ish16;
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
