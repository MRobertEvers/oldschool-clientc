#ifndef TORIDRAW_ZBUF_PLANE_H
#define TORIDRAW_ZBUF_PLANE_H

/*
 * The depth plane a depth-tested triangle interpolates.
 *
 * Every zbuffered kernel needs the same three numbers and derives them the same
 * way, so they live here rather than once per family. The kernels differ in what
 * they do with a pixel; they must not differ in where they think it is.
 *
 * The interpolant is the DEPTH KEY (graphics/zdepth.h), not z. That is what
 * makes a plane the right model at all: under perspective the key is
 * TORIDRAW_ZDEPTH_PERSP_SCALE / z, which is exactly linear in screen space, so
 * three corners determine it everywhere and a span advances it with one add per
 * pixel. Interpolating z instead bows away from the surface between vertices —
 * the error is largest in the middle of a long triangle, which is precisely
 * where two interpenetrating parts have to be resolved.
 */

#include "graphics/zdepth.h"

#include <stdbool.h>

/**
 * key(x, y) == base + step_dx * x + step_dy * y, in absolute screen pixels.
 *
 * Based at the origin rather than at a corner so a caller that clips its first
 * row to 0 does not have to unwind the vertex it was based on: the row key is
 * `base + step_dy * y` for whichever y it actually starts at.
 */
struct ToriDraw_ZbufPlane
{
    float base;
    float step_dx;
    float step_dy;
};

/**
 * Solve the plane through three screen corners and their keys.
 *
 * Solved in double, and this is not caution about the keys — they are small.
 * The corner coordinates are ints that a near-clip rebuild can place far outside
 * the viewport, and their cross products are the one quantity here that would
 * overflow 32 bits.
 *
 * The corners may be handed over in any order: a plane is invariant under
 * permutation, and the sign the permutation flips divides out. Returns false for
 * a degenerate triangle, which is the same triangle every walker rejects on its
 * own signed-area test — so a false here is a triangle nothing would have drawn.
 */
static inline bool
toridraw_zbuf_plane_solve(
    struct ToriDraw_ZbufPlane* out,
    int x0,
    int y0,
    float key0,
    int x1,
    int y1,
    float key1,
    int x2,
    int y2,
    float key2)
{
    double const dx_ab = (double)x1 - (double)x0;
    double const dy_ab = (double)y1 - (double)y0;
    double const dx_ac = (double)x2 - (double)x0;
    double const dy_ac = (double)y2 - (double)y0;

    double const area = dx_ab * dy_ac - dx_ac * dy_ab;
    if( area == 0.0 )
        return false;

    double const dk_ab = (double)key1 - (double)key0;
    double const dk_ac = (double)key2 - (double)key0;

    double const step_dx = (dk_ab * dy_ac - dk_ac * dy_ab) / area;
    double const step_dy = (dk_ac * dx_ab - dk_ab * dx_ac) / area;

    out->step_dx = (float)step_dx;
    out->step_dy = (float)step_dy;
    out->base = (float)((double)key0 - step_dx * (double)x0 - step_dy * (double)y0);
    return true;
}

/** The key at the left edge (x == 0) of row `y`. */
static inline float
toridraw_zbuf_plane_row(
    const struct ToriDraw_ZbufPlane* plane,
    int y)
{
    return plane->base + plane->step_dy * (float)y;
}

#endif
