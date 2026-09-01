#ifndef TORIDRAW_GRAPHICS_PROJECTION_BOUND_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_BOUND_U_C

#include "impl/projection/projection.bound.dispatch.h"

/*
 * Same gates the projection ladder in projection16_simd.u.c selects its
 * kernels by, so the sweep vectorises exactly where they do -- with one
 * addition: __aarch64__ is required for the NEON lane, because the horizontal
 * reduce it ends on (vminvq_s32) does not exist on A32.
 */
#if defined(__aarch64__) && ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && \
    !defined(NEON_DISABLED)
#include "impl/projection/projection.bound.neon64.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED) && !defined(SSE41_DISABLED)
#include "impl/projection/projection.bound.sse41.u.c"
#else
#include "impl/projection/projection.bound.scalar.u.c"
#endif

#endif /* TORIDRAW_GRAPHICS_PROJECTION_BOUND_U_C */
