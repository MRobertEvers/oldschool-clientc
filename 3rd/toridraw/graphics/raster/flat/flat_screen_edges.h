#ifndef FLAT_SCREEN_EDGES_H
#define FLAT_SCREEN_EDGES_H

/*
 * THE A->B PRE-STEP GUARD. Every `branching` triangle walk clips a top that is
 * above the viewport by pre-stepping its edges to row 0:
 *
 *     edge_x_AC_ish16 -= step_edge_x_AC_ish16 * y0;
 *     if( y1 > 0 )
 *         edge_x_AB_ish16 -= step_edge_x_AB_ish16 * y0;
 *
 * The A->B guard is not an optimisation. Rows [y0, y1) use A->B, so when the
 * middle vertex is at or above the top of the viewport the span is empty and
 * the pre-stepped value feeds nothing. A near-horizontal AB has a tiny dy and
 * therefore a slope of hundreds of pixels per row, and that slope times a y0
 * far above the screen overflows the 16.16 product -- for a result that is then
 * discarded. UBSan caught exactly that on the Queen Black Dragon:
 *
 *     zbuf.screen.u.c:474: signed integer overflow: 19595264 * -127
 *
 * A LIVE edge cannot overflow here, which is why this is a guard and not a
 * widening: the edge spans y0..y1 with y1 > 0, so dy >= |y0| and the product is
 * bounded by dx << 16 -- reaching INT_MAX needs a projected dx over 32768px,
 * which is geometry outside the raster's coordinate domain
 * (TORIDRAW_PROJECTED_COORD_LIMIT) rather than an arithmetic problem. The same
 * argument covers A->C (y2 >= 0 is guaranteed by the caller's reject) and B->C
 * (guarded by its own y1 < 0 block).
 */

/**
 * True if for all k in [0, n), affine edges e_a + k*step_a and e_b + k*step_b satisfy
 * horizontal screen interior (no left/right clip): (e_a>>16)>=0 and (e_b>>16)<screen_width.
 * Affine extrema on a segment occur at endpoints k=0 or k=n-1.
 */
static inline int
flat_screen_fixed_edges_no_hclip(
    int e_a0,
    int step_a,
    int e_b0,
    int step_b,
    int n,
    int screen_width)
{
    if( n <= 0 )
        return 1;

    int e_a_first = e_a0;
    int e_a_last = e_a0 + step_a * (n - 1);
    int min_e_a = e_a_first < e_a_last ? e_a_first : e_a_last;

    int e_b_first = e_b0;
    int e_b_last = e_b0 + step_b * (n - 1);
    int max_e_b = e_b_first > e_b_last ? e_b_first : e_b_last;

    return (min_e_a >= 0) && (max_e_b < (screen_width << 16));
}

#endif
