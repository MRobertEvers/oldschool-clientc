#ifndef TORIDRAW_GRAPHICS_PROJECTION_BOUND_H
#define TORIDRAW_GRAPHICS_PROJECTION_BOUND_H

/*
 * The screen-space min/max sweep over a model's projected vertices, split out
 * per ISA.
 *
 * Two operations, both pure over plain int arrays -- no scene, no model, no
 * viewport. The caller (toridraw_projected_bound in toridraw_render.u.c) keeps
 * the sentinel test, the pick dilation and the viewport offset, so an ISA lane
 * only ever has to answer "what are the four extremes".
 *
 *   projection_bound.u.c          selects one lane
 *   projection_bound.neon.u.c     AArch64: lane-wise min/max + vminvq reduce
 *   projection_bound.sse41.u.c    SSE4.1: pmin/pmaxsd + shuffle reduce
 *   projection_bound.scalar.u.c   everywhere else, including plain SSE2 --
 *                                 it has neither a 32-bit lane min/max nor a
 *                                 horizontal one, so the tail loop IS the
 *                                 kernel there.
 */

/** The four extremes, before the viewport offset and the pick slop. */
struct ToriDraw_ScreenBound
{
    int min_x;
    int max_x;
    int min_y;
    int max_y;
};

/*
 * Fold the four-lane block a prepared projection kernel already reduced into.
 *
 * The block is the transposed accumulator the AArch64 assembly and the SSE2
 * fused-yaw kernels leave behind: lanes 0..3 are min x, 4..7 max x, 8..11
 * min y, 12..15 max y. Sixteen scalar compares, once per model.
 */
static inline void
toridraw_bound_fold_prepared_scalar(const int* b, struct ToriDraw_ScreenBound* box)
{
    box->min_x = b[0];
    box->max_x = b[4];
    box->min_y = b[8];
    box->max_y = b[12];
    for( int lane = 1; lane < 4; lane++ )
    {
        if( b[lane] < box->min_x )
            box->min_x = b[lane];
        if( b[4 + lane] > box->max_x )
            box->max_x = b[4 + lane];
        if( b[8 + lane] < box->min_y )
            box->min_y = b[8 + lane];
        if( b[12 + lane] > box->max_y )
            box->max_y = b[12 + lane];
    }
}

/*
 * Each lane below defines these two:
 *
 *   void toridraw_bound_fold_prepared(const int* b, struct ToriDraw_ScreenBound* box)
 *       Writes every field of `box` from the 16-int prepared block.
 *
 *   int toridraw_bound_sweep(const int* svx, const int* svy, int vertex_count,
 *                            struct ToriDraw_ScreenBound* box)
 *       Consumes a whole number of leading vertices and returns how many.
 *       A non-zero return means it covered vertex 0 and has written EVERY
 *       field of `box` from exactly the vertices it consumed, so the caller's
 *       own vertex-0 seed is superseded rather than merged with. Zero means it
 *       declined (too few vertices, or no lane path here) and the seed stands.
 *       The caller finishes vertices [return, vertex_count) scalar either way.
 */

#endif /* TORIDRAW_GRAPHICS_PROJECTION_BOUND_H */
