#ifndef TORIDRAW_GRAPHICS_DIV3_H
#define TORIDRAW_GRAPHICS_DIV3_H

/**
 * The face-depth average's divide by three, in one place.
 *
 * Every sort that orders faces back-to-front averages the three projected
 * vertex depths, and they must all average them the SAME way -- the
 * bitonic+radix sort is held to the bucket sort order for order, and a
 * rounding difference between the two swaps coplanar faces and moves pixels.
 * It lived in toridraw_render.u.c, where the two lanes of the bitonic+radix
 * sort and the debug dumper reached it only because the unity build had
 * already pasted that file in above them; here each of them can state the
 * dependency and parse alone.
 */

/* z_sum / 3, via the 16.16 reciprocal (21845 == 65536/3). Overflows at
 * z_sum > 98,304 (~32,768 average projected depth per vertex); a wrapped z_sum
 * goes negative and buckets outside the depth table, so the model loses faces
 * from some camera angles and not others. Only reachable with geometry that is
 * already wrong -- guard by range once per model, not by widening here. */
static inline int
div3_fast_fixedpoint(int z_sum)
{
    return (z_sum * 21845) >> 16;
}

#endif
