#ifndef TORIDRAW_GRAPHICS_PROJECTION_BOUND_SCALAR_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_BOUND_SCALAR_U_C

#include "projection_bound.h"

static inline void
toridraw_bound_fold_prepared(const int* b, struct ToriDraw_ScreenBound* box)
{
    toridraw_bound_fold_prepared_scalar(b, box);
}

static inline int
toridraw_bound_sweep(
    const int* svx,
    const int* svy,
    int vertex_count,
    struct ToriDraw_ScreenBound* box)
{
    /*
     * Decline, always: with no lane-wise min/max there is nothing a block
     * form would do that the caller's tail loop does not already do, and the
     * four-vertex tile that dominates is four iterations of it.
     *
     * This is the plain-SSE2 lane as well as the no-SIMD one. SSE2 has
     * pminsw/pmaxsw for 16-bit lanes only; projected coordinates are 32-bit,
     * so emulating a 32-bit lane min costs a compare, a blend and two ands --
     * more work than the compare it replaces.
     */
    (void)svx;
    (void)svy;
    (void)vertex_count;
    (void)box;
    return 0;
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_BOUND_SCALAR_U_C */
