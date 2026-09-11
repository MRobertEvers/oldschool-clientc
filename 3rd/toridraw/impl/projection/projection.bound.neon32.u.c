#ifndef TORIDRAW_GRAPHICS_PROJECTION_BOUND_NEON32_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_BOUND_NEON32_U_C

#include "impl/projection/projection.bound.dispatch.h"

#include <arm_neon.h>

/* The A32 NEON baseline, so armv7 gets the same sweep aarch64 does.
 *
 * The body is identical to the neon64 file -- vld1q_s32, vminq_s32 and
 * vmaxq_s32 are the same instructions in both encodings. The one A64-only
 * intrinsic there is the horizontal reduce (vminvq_s32 / vmaxvq_s32), and A32
 * replaces it with the pairwise fold below: VPMIN.S32 takes a 4-lane vector to
 * 2 lanes, then 2 to 1. Two instructions instead of one, run once per model,
 * not once per vertex.
 *
 * aarch64 keeps the neon64 file for the single-instruction reduce; this one
 * would compile and run correctly there too. */

static inline int32_t
toridraw_bound_horizontal_min_s32(int32x4_t v)
{
    int32x2_t pair = vpmin_s32(vget_low_s32(v), vget_high_s32(v));
    pair = vpmin_s32(pair, pair);
    return vget_lane_s32(pair, 0);
}

static inline int32_t
toridraw_bound_horizontal_max_s32(int32x4_t v)
{
    int32x2_t pair = vpmax_s32(vget_low_s32(v), vget_high_s32(v));
    pair = vpmax_s32(pair, pair);
    return vget_lane_s32(pair, 0);
}

/*
 * The fold, closed with ONE store rather than four lane extracts.
 *
 * The first form of this reduced each of the four vectors to a lane and
 * moved each lane to a GPR (vmov r, d[0]) to store it -- four NEON-to-ARM
 * transfers per model, which serialise the two pipelines. The pairwise ops
 * reduce two vectors at once if they are fed as a pair: after the first
 * vpmin/vpmax each 64-bit half holds {x0, x1}; pairing the x half with the y
 * half gives {min_x, min_y} and {max_x, max_y} in one more op each, and a
 * vzip interleaves them into {min_x, max_x, min_y, max_y} -- which is the
 * struct's own layout, so it is stored whole. Nothing crosses to the ARM
 * side; the caller reads the fields back from L1.
 */
_Static_assert(
    sizeof(struct ToriDraw_ScreenBound) == 4 * sizeof(int),
    "ToriDraw_ScreenBound is stored as one int32x4");

static inline void
toridraw_bound_fold_prepared(const int* b, struct ToriDraw_ScreenBound* box)
{
    int32x4_t const bmin_x = vld1q_s32(b + 0);
    int32x4_t const bmax_x = vld1q_s32(b + 4);
    int32x4_t const bmin_y = vld1q_s32(b + 8);
    int32x4_t const bmax_y = vld1q_s32(b + 12);
    int32x2_t const min_x2 = vpmin_s32(vget_low_s32(bmin_x), vget_high_s32(bmin_x));
    int32x2_t const max_x2 = vpmax_s32(vget_low_s32(bmax_x), vget_high_s32(bmax_x));
    int32x2_t const min_y2 = vpmin_s32(vget_low_s32(bmin_y), vget_high_s32(bmin_y));
    int32x2_t const max_y2 = vpmax_s32(vget_low_s32(bmax_y), vget_high_s32(bmax_y));
    int32x2_t const mins = vpmin_s32(min_x2, min_y2); /* {min_x, min_y} */
    int32x2_t const maxs = vpmax_s32(max_x2, max_y2); /* {max_x, max_y} */
    int32x2x2_t const zipped = vzip_s32(mins, maxs);  /* {min_x,max_x} {min_y,max_y} */
    vst1q_s32(&box->min_x, vcombine_s32(zipped.val[0], zipped.val[1]));
}

static inline int
toridraw_bound_sweep(
    const int* svx,
    const int* svy,
    int vertex_count,
    struct ToriDraw_ScreenBound* box)
{
    /* Two accumulator sets: a single min/max chain is bound by the two-cycle
     * latency of each step, and the loop is otherwise a pair of loads --
     * eight vertices a trip halves the chain per vertex. */
    int32x4_t vmin_x;
    int32x4_t vmax_x;
    int32x4_t vmin_y;
    int32x4_t vmax_y;
    int32x4_t vmin_x2;
    int32x4_t vmax_x2;
    int32x4_t vmin_y2;
    int32x4_t vmax_y2;
    int i;

    if( vertex_count < 4 )
        return 0;

    vmin_x = vld1q_s32(svx);
    vmax_x = vmin_x;
    vmin_y = vld1q_s32(svy);
    vmax_y = vmin_y;
    vmin_x2 = vmin_x;
    vmax_x2 = vmax_x;
    vmin_y2 = vmin_y;
    vmax_y2 = vmax_y;

    for( i = 4; i + 8 <= vertex_count; i += 8 )
    {
        int32x4_t const x = vld1q_s32(svx + i);
        int32x4_t const y = vld1q_s32(svy + i);
        int32x4_t const x2 = vld1q_s32(svx + i + 4);
        int32x4_t const y2 = vld1q_s32(svy + i + 4);
        vmin_x = vminq_s32(vmin_x, x);
        vmax_x = vmaxq_s32(vmax_x, x);
        vmin_y = vminq_s32(vmin_y, y);
        vmax_y = vmaxq_s32(vmax_y, y);
        vmin_x2 = vminq_s32(vmin_x2, x2);
        vmax_x2 = vmaxq_s32(vmax_x2, x2);
        vmin_y2 = vminq_s32(vmin_y2, y2);
        vmax_y2 = vmaxq_s32(vmax_y2, y2);
    }
    if( i + 4 <= vertex_count )
    {
        int32x4_t const x = vld1q_s32(svx + i);
        int32x4_t const y = vld1q_s32(svy + i);
        vmin_x = vminq_s32(vmin_x, x);
        vmax_x = vmaxq_s32(vmax_x, x);
        vmin_y = vminq_s32(vmin_y, y);
        vmax_y = vmaxq_s32(vmax_y, y);
        i += 4;
    }

    box->min_x = toridraw_bound_horizontal_min_s32(vminq_s32(vmin_x, vmin_x2));
    box->max_x = toridraw_bound_horizontal_max_s32(vmaxq_s32(vmax_x, vmax_x2));
    box->min_y = toridraw_bound_horizontal_min_s32(vminq_s32(vmin_y, vmin_y2));
    box->max_y = toridraw_bound_horizontal_max_s32(vmaxq_s32(vmax_y, vmax_y2));
    return i;
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_BOUND_NEON32_U_C */
