#ifndef TORIDRAW_RASTER_SCANLINE_COMMON_H
#define TORIDRAW_RASTER_SCANLINE_COMMON_H

/**
 * Shared front-end for the `scanline` raster family.
 *
 * The existing families (`branching`, `sort`) each re-derive the whole
 * trapezoid walk per variant: `branching` duplicates the ordered walker behind
 * a six-way y-permutation dispatch, `sort` swaps every per-vertex attribute
 * into place. Both then test the horizontal clip once per pixel-run and the
 * vertical clip once per row.
 *
 * The `scanline` family splits that into three phases that each happen exactly
 * once per triangle:
 *
 *   1. `scanline_tri_build`  - y-sort the three screen vertices, recording the
 *      permutation and its parity. Attribute gradients (barycentric colour,
 *      perspective uv, per-vertex shade) are plane equations and therefore
 *      invariant under vertex permutation, so callers compute them from the
 *      *unsorted* vertices and never permute an attribute. Parity lets the
 *      sorted signed area be recovered from the unsorted one for free.
 *
 *   2. `scanline_edges_build` - resolve both trapezoid segments up front:
 *      which edge is left, both edges' 16.16 start/step, the first on-screen
 *      row, and the row count. Vertical clipping is folded into the edge
 *      accumulators arithmetically, so no walker ever tests y inside its loop.
 *
 *   3. `scanline_segment_no_hclip` - classify each segment once. When every row
 *      of a segment lies inside [0, screen_width) the walker runs a span
 *      kernel with no bounds arithmetic at all. Only `flat` had this before;
 *      here every family gets it.
 *
 * Fill rule matches the rest of the codebase: a row covers [x_left >> 16,
 * x_right >> 16), left inclusive, right exclusive, sampled at integer y.
 */

#include "graphics/dash_restrict.h"
#include "graphics/shared_tables.h"

/** Screen triangle sorted top-to-bottom (ya <= yb <= yc). */
struct ScanlineTri
{
    int xa;
    int ya;
    int xb;
    int yb;
    int xc;
    int yc;

    /** Original vertex index that ended up at a/b/c. */
    int perm[3];

    /**
     * Twice the signed area of the *sorted* triangle:
     *   (xb-xa)*(yc-ya) - (xc-xa)*(yb-ya)
     * Sign selects which edge is on the left; zero means degenerate.
     */
    int sarea;

    /** Non-zero when perm is an odd permutation of (0,1,2). */
    int parity;
};

/**
 * Sort three screen vertices by y, tracking the permutation.
 *
 * Returns 0 when the triangle cannot produce pixels (zero height or zero
 * area), in which case *out is not meaningfully populated.
 */
static inline int
scanline_tri_build(
    struct ScanlineTri* RESTRICT out,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2)
{
    int xs[3];
    int ys[3];
    int i0 = 0;
    int i1 = 1;
    int i2 = 2;
    int parity = 0;
    int tmp;

    xs[0] = x0;
    xs[1] = x1;
    xs[2] = x2;
    ys[0] = y0;
    ys[1] = y1;
    ys[2] = y2;

    if( ys[i0] > ys[i1] )
    {
        tmp = i0;
        i0 = i1;
        i1 = tmp;
        parity ^= 1;
    }
    if( ys[i1] > ys[i2] )
    {
        tmp = i1;
        i1 = i2;
        i2 = tmp;
        parity ^= 1;
    }
    if( ys[i0] > ys[i1] )
    {
        tmp = i0;
        i0 = i1;
        i1 = tmp;
        parity ^= 1;
    }

    out->xa = xs[i0];
    out->ya = ys[i0];
    out->xb = xs[i1];
    out->yb = ys[i1];
    out->xc = xs[i2];
    out->yc = ys[i2];
    out->perm[0] = i0;
    out->perm[1] = i1;
    out->perm[2] = i2;
    out->parity = parity;

    if( out->yc == out->ya )
        return 0;

    out->sarea = (out->xb - out->xa) * (out->yc - out->ya) -
                 (out->xc - out->xa) * (out->yb - out->ya);

    return out->sarea != 0;
}

/**
 * Both trapezoid segments, fully clipped in y and resolved left/right.
 *
 * Index 0 is the segment between the top and middle vertices, index 1 the one
 * between the middle and bottom vertices. `rows[i] == 0` means that segment
 * contributes nothing.
 */
struct ScanlineEdges
{
    int rows[2];
    int y0[2];
    int l_ish16[2];
    int l_step[2];
    int r_ish16[2];
    int r_step[2];
};

/**
 * Resolve the two segments of `t` against a screen of `screen_height` rows.
 *
 * Vertical clipping is applied by advancing the edge accumulators, never by
 * skipping rows in a loop. Returns 0 when nothing is on screen.
 */
static inline int
scanline_edges_build(
    struct ScanlineEdges* RESTRICT e,
    const struct ScanlineTri* RESTRICT t,
    int screen_height)
{
    int ya = t->ya;
    int yb = t->yb;
    int yc = t->yc;

    if( yc < 0 || ya > screen_height )
        return 0;

    int dy_ac = yc - ya;
    int step_ac = ((t->xc - t->xa) << 16) / dy_ac;
    int step_ab = (yb > ya) ? (((t->xb - t->xa) << 16) / (yb - ya)) : 0;
    int step_bc = (yc > yb) ? (((t->xc - t->xb) << 16) / (yc - yb)) : 0;

    int x_ac = t->xa << 16;
    int x_ab = t->xa << 16;
    int x_bc = t->xb << 16;

    int y_seg1 = ya;
    int y_seg2 = yb;

    if( ya < 0 )
    {
        x_ac -= step_ac * ya;
        x_ab -= step_ab * ya;
        y_seg1 = 0;
    }

    if( yb < 0 )
    {
        x_bc -= step_bc * yb;
        y_seg2 = 0;
    }

    /* AC is continuous across both segments; carry it to the second start. */
    int x_ac_seg2 = x_ac + step_ac * (y_seg2 - y_seg1);

    int y_end1 = yb < screen_height ? yb : screen_height;
    int y_end2 = yc < screen_height ? yc : screen_height;

    int rows1 = y_end1 - y_seg1;
    int rows2 = y_end2 - y_seg2;
    if( rows1 < 0 )
        rows1 = 0;
    if( rows2 < 0 )
        rows2 = 0;

    if( rows1 == 0 && rows2 == 0 )
        return 0;

    e->rows[0] = rows1;
    e->rows[1] = rows2;
    e->y0[0] = y_seg1;
    e->y0[1] = y_seg2;

    /* x_ac(yb) - xb = -sarea / (yc - ya), and yc - ya > 0, so AC is the left
     * edge exactly when sarea is positive. */
    if( t->sarea > 0 )
    {
        e->l_ish16[0] = x_ac;
        e->l_step[0] = step_ac;
        e->r_ish16[0] = x_ab;
        e->r_step[0] = step_ab;

        e->l_ish16[1] = x_ac_seg2;
        e->l_step[1] = step_ac;
        e->r_ish16[1] = x_bc;
        e->r_step[1] = step_bc;
    }
    else
    {
        e->l_ish16[0] = x_ab;
        e->l_step[0] = step_ab;
        e->r_ish16[0] = x_ac;
        e->r_step[0] = step_ac;

        e->l_ish16[1] = x_bc;
        e->l_step[1] = step_bc;
        e->r_ish16[1] = x_ac_seg2;
        e->r_step[1] = step_ac;
    }

    return 1;
}

/**
 * True when every row of a segment is fully inside [0, screen_width).
 *
 * Both edges are affine in the row index, so their extrema over the segment
 * occur at the first or last row - two multiplies decide it for the whole run.
 */
static inline int
scanline_segment_no_hclip(
    int l_ish16,
    int l_step,
    int r_ish16,
    int r_step,
    int rows,
    int screen_width)
{
    if( rows <= 0 )
        return 1;

    int l_last = l_ish16 + l_step * (rows - 1);
    int r_last = r_ish16 + r_step * (rows - 1);

    int l_min = l_ish16 < l_last ? l_ish16 : l_last;
    int r_max = r_ish16 > r_last ? r_ish16 : r_last;

    return (l_min >= 0) && (r_max < (screen_width << 16));
}

/**
 * Clamp one row's 16.16 edge pair to the screen and emit integer bounds.
 * Returns the pixel count, or 0 when the row is empty.
 */
static inline int
scanline_row_clip(
    int l_ish16,
    int r_ish16,
    int screen_width,
    int* RESTRICT out_x_start)
{
    int x_start = l_ish16 >> 16;
    int x_end = r_ish16 >> 16;

    if( x_start < 0 )
        x_start = 0;
    if( x_end > screen_width )
        x_end = screen_width;

    *out_x_start = x_start;
    return x_end - x_start;
}

/** Barycentric attribute gradient, matching gouraudhsllightness_barycentric_hsl_step_ish8. */
static inline int
scanline_bary_step_ish8(
    int numerator,
    int sarea)
{
    return (numerator << 8) / sarea;
}

#endif
