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
    int dx = screen_x_end - screen_x_start;
    if( dx <= 0 )
        return;

    // Initialize fixed-point values (16.16 format)
    int u = u_start << 16;
    int v = v_start << 16;
    int z = z_start << 16;

    // Calculate steps per pixel using fixed-point arithmetic
    int du = ((u_end - u_start) << 16) / dx;
    int dv = ((v_end - v_start) << 16) / dx;
    int dz = ((z_end - z_start) << 16) / dx;

    // Pre-calculate texture width shift for faster multiplication
    int texture_width_shift = 0;
    int temp_width = texture_width;
    while( temp_width > 1 )
    {
        texture_width_shift++;
        temp_width >>= 1;
    }

    // Draw the scanline
    screen_x_start = MAX(screen_x_start, 0);
    screen_x_end = MIN(screen_x_end, screen_width - 1);
    for( int x = screen_x_start; x < screen_x_end; x++ )
    {
        int texel_x = (u >> 16);
        int texel_y = (v >> 16);

        // Clamp texture coordinates
        assert(texel_x >= 0 && texel_x < texture_width && texel_y >= 0 && texel_y < texture_width);

        // Calculate texture index using shift instead of multiplication
        int texel_index = (texel_y << texture_width_shift) + texel_x;
        int color = texels[texel_index];

        // Write pixel
        assert(y >= 0 && y < screen_height && x >= 0 && x < screen_width);
        pixel_buffer[y * screen_width + x] = color;

        // Step texture coordinates
        u += du;
        v += dv;
        z += dz;
    }
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

    /**
     * Texture coordinates are mapped from face coordinates.
     * Note: Modal coordinates are the same as screen coordinates prior to projection (aka the
     * orthographic coordinates).
     *
     * We have been given model coordinates, so we need to convert them to face coordinates, which
     * we can then map to texture coordinates.
     *
     * Math: A normal vector defines the coefficients of a plane equation.
     * For example, if the normal vector is (A, B, C), the plane equation is Ax + By + Cz + D = 0.
     *
     * Math: The cross product of two vectors give a vector that is perpendicular to both.
     * With that, if we have two vectors on a plane, we can find the normal vector by taking the
     * cross product.
     */

    /**
     * AB and CA are vectors defining the face plane in model coordinates. AB and CA are the basis
     * of the face coordinates, and we represent the AB and CA vectors in model coordinates because,
     * as we scan across the screen, we are traversing model coordinates.
     * And we know how to map AB => (u, v), and CA => (u, v).
     */
    int ABx = orthographic_x0 - orthographic_x1;
    int ABy = orthographic_y0 - orthographic_y1;
    int ABz = orthographic_z0 - orthographic_z1;

    int CAx = orthographic_x2 - orthographic_x0;
    int CAy = orthographic_y2 - orthographic_y0;
    int CAz = orthographic_z2 - orthographic_z0;

    /**
     * U is the cross product of OA and AB in model coordinates.
     *
     * We want this because as we step across the screen, we are traversing model coordinates, and
     * we want to convert that to the AB, CA basis. This plane will give us the "AB" component of
     * the face coordinates as we scan the screen.
     *
     *
     * The abbreviation "hat" means "basis vector".
     *  "sz" means "screen z basis vector".
     *  "sx" means "screen x basis vector".
     *  "sy" means "screen y basis vector".
     *
     * We use the term "stride" to mean x direction across the screen and "step" to mean y.
     */

    // u
    int determinant_CAxy_originxy = (CAx * orthographic_y0 - CAy * orthographic_x0);

    int u_szhat = determinant_CAxy_originxy << 14;

    // u_stride
    int determinant_CAyz_originyz = (CAy * orthographic_z0 - CAz * orthographic_y0);

    int u_sxhat = determinant_CAyz_originyz << 8;

    // u_step
    int determinant_CAzx_originxz = (CAz * orthographic_x0 - CAx * orthographic_z0);

    int u_syhat = determinant_CAzx_originxz << 5;

    // v
    int determinant_ABxy_originxy = (ABx * orthographic_y0 - ABy * orthographic_x0);
    int v_szhat = determinant_ABxy_originxy << 14;

    // v_stride
    int determinant_AByz_originyz = (ABy * orthographic_z0 - ABz * orthographic_y0);

    int v_sxhat_stride = determinant_AByz_originyz << 8;

    // v_step
    int determinant_ABzx_originzx = (ABz * orthographic_x0 - ABx * orthographic_z0);

    int v_syhat_step = determinant_ABzx_originzx << 5;

    /**
     * For perspective correct projection, we also need to know the "z" coordinate of the point in
     * model coordinates. As we scan across the screen, the normal vector given by AB cross CA gives
     * the plane equation for the face in model coordinates. We then look at the z component of the
     * point on that plane in model coordinates which we use for perspective projection.
     */

    // w
    int determinant_ABxy_CAxy = (ABx * CAy - ABy * CAx);
    int w_szhat = determinant_ABxy_CAxy;

    // w_stride
    int determinant_AByz_CAyz = (ABy * CAz - ABz * CAy);
    int w_sxhat_stride = determinant_AByz_CAyz;

    // w_step
    int determinant_ABzx_CAzx = (ABz * CAx - ABx * CAz);
    int w_syhat_step = determinant_ABzx_CAzx;
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

    // Initialize edge steppers for both parts of the triangle
    EdgeStepper left_edge, right_edge;

    // Top part of triangle (y0 to y1)
    if( screen_y1 > screen_y0 )
    {
        // Determine which edge is left and right
        int x_mid = screen_x0 +
                    ((screen_x1 - screen_x0) * (screen_y1 - screen_y0)) / (screen_y2 - screen_y0);

        if( screen_x1 < x_mid )
        {
            // Left edge: (x0,y0) to (x1,y1)
            init_edge_stepper(
                &left_edge,
                screen_x0,
                screen_y0,
                screen_z0,
                u0,
                v0,
                screen_x1,
                screen_y1,
                screen_z1,
                u1,
                v1);
            // Right edge: (x0,y0) to (x2,y2)
            init_edge_stepper(
                &right_edge,
                screen_x0,
                screen_y0,
                screen_z0,
                u0,
                v0,
                screen_x2,
                screen_y2,
                screen_z2,
                u2,
                v2);
        }
        else
        {
            // Left edge: (x0,y0) to (x2,y2)
            init_edge_stepper(
                &left_edge,
                screen_x0,
                screen_y0,
                screen_z0,
                u0,
                v0,
                screen_x2,
                screen_y2,
                screen_z2,
                u2,
                v2);
            // Right edge: (x0,y0) to (x1,y1)
            init_edge_stepper(
                &right_edge,
                screen_x0,
                screen_y0,
                screen_z0,
                u0,
                v0,
                screen_x1,
                screen_y1,
                screen_z1,
                u1,
                v1);
        }

        // Draw top part of triangle
        for( int y = screen_y0; y < screen_y1; y++ )
        {
            if( y < 0 || y >= screen_height )
                continue;

            int x_start = left_edge.x >> 16;
            int x_end = right_edge.x >> 16;
            int z_start = left_edge.z >> 16;
            int z_end = right_edge.z >> 16;
            int u_start = left_edge.u >> 16;
            int u_end = right_edge.u >> 16;
            int v_start = left_edge.v >> 16;
            int v_end = right_edge.v >> 16;

            draw_scanline_texture(
                pixel_buffer,
                screen_width,
                screen_height,
                y,
                x_start,
                x_end,
                z_start,
                z_end,
                u_start,
                u_end,
                v_start,
                v_end,
                texels,
                texture_width);

            step_edge(&left_edge);
            step_edge(&right_edge);
        }
    }

    // Bottom part of triangle (y1 to y2)
    if( screen_y2 > screen_y1 )
    {
        // Determine which edge is left and right
        if( screen_x1 < screen_x2 )
        {
            // Left edge: (x1,y1) to (x2,y2)
            init_edge_stepper(
                &left_edge,
                screen_x1,
                screen_y1,
                screen_z1,
                u1,
                v1,
                screen_x2,
                screen_y2,
                screen_z2,
                u2,
                v2);
            // Right edge: (x0,y0) to (x2,y2)
            init_edge_stepper(
                &right_edge,
                screen_x0,
                screen_y0,
                screen_z0,
                u0,
                v0,
                screen_x2,
                screen_y2,
                screen_z2,
                u2,
                v2);
        }
        else
        {
            // Left edge: (x0,y0) to (x2,y2)
            init_edge_stepper(
                &left_edge,
                screen_x0,
                screen_y0,
                screen_z0,
                u0,
                v0,
                screen_x2,
                screen_y2,
                screen_z2,
                u2,
                v2);
            // Right edge: (x1,y1) to (x2,y2)
            init_edge_stepper(
                &right_edge,
                screen_x1,
                screen_y1,
                screen_z1,
                u1,
                v1,
                screen_x2,
                screen_y2,
                screen_z2,
                u2,
                v2);
        }

        // Draw bottom part of triangle
        for( int y = screen_y1; y < screen_y2; y++ )
        {
            if( y < 0 || y >= screen_height )
                continue;
            int x_start = left_edge.x >> 16;
            int x_end = right_edge.x >> 16;
            int z_start = left_edge.z >> 16;
            int z_end = right_edge.z >> 16;
            int u_start = left_edge.u >> 16;
            int u_end = right_edge.u >> 16;
            int v_start = left_edge.v >> 16;
            int v_end = right_edge.v >> 16;

            draw_scanline_texture(
                pixel_buffer,
                screen_width,
                screen_height,
                y,
                x_start,
                x_end,
                z_start,
                z_end,
                u_start,
                u_end,
                v_start,
                v_end,
                texels,
                texture_width);

            step_edge(&left_edge);
            step_edge(&right_edge);
        }
    }
}
