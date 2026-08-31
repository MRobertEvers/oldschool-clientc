#ifndef TORIDRAW_GRAPHICS_PROJECTION_BOUND_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_BOUND_U_C

#include "impl/projection/projection.bound.dispatch.h"

/*
 * Same gates the projection ladder in projection16_simd.u.c selects its
 * kernels by, so the sweep vectorises exactly where they do.
 *
 * The ARM lane is split by encoding width rather than gated on __aarch64__.
 * The sweep body -- vld1q_s32, vminq_s32, vmaxq_s32 -- is common to A32 and
 * A64; only the horizontal reduce at the end differs, and A32 has one, it is
 * just not a single instruction: a pairwise VPMIN.S32/VPMAX.S32 fold, 4 lanes
 * to 2 to 1, run once per model. So armv7 takes the vector sweep too, and the
 * scalar file is left to the lanes with no 32-bit lane-wise min/max at all.
 * aarch64 keeps neon64 for the one-instruction vminvq_s32/vmaxvq_s32 reduce.
 */
#if defined(__aarch64__) && ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && \
    !defined(NEON_DISABLED)
#include "impl/projection/projection.bound.neon64.u.c"
#elif ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(__aarch64__) && \
    !defined(NEON_DISABLED)
#include "impl/projection/projection.bound.neon32.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED) && !defined(SSE41_DISABLED)
#include "impl/projection/projection.bound.sse41.u.c"
#else
#include "impl/projection/projection.bound.scalar.u.c"
#endif

#endif /* TORIDRAW_GRAPHICS_PROJECTION_BOUND_U_C */
