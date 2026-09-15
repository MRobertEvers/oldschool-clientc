#ifndef TORIDRAW_GRAPHICS_PROJECTION_PREPARED_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_PREPARED_U_C

#include "impl/projection/projection.perspective.prepared.dispatch.h"

/*
 * One lane, selected by the macros that decide whether the kernels behind it
 * were built at all: TORIDRAW_APPLE_NEON_PROJECTION_ASM comes from
 * src/makefile, where projection16.aarch64.S is assembled, and
 * TORIDRAW_SSE2_PREPARED_PROJECTION from projection16_simd.u.c, where the SSE2
 * prepared header is included. Nothing else in the tree may test them.
 *
 * The neon32 arm tests the compiler's own __ARM_NEON instead, in the same
 * spelling projection.perspective.plain.dispatch.u.c uses to pick the portable
 * NEON ladder -- the kernels it selects are intrinsics in a header this file
 * includes, so there is no separately-built artifact for a makefile to
 * announce and no third macro to keep in sync with one.
 *
 * ORDER IS THE POINT. aarch64 with the assembly assembled takes the assembly;
 * every other NEON build -- armv7, and the aarch64 builds that do not assemble
 * it -- takes neon32, which is the only lane besides SSE2 that serves BOTH
 * near-clip families. PREPARED_PROJECTION_DISABLED compiles the intrinsics
 * lane back out to `scalar`, the same A/B switch the SSE2 gate offers.
 *
 * `#elif` and not four `#if`s. The lanes are mutually exclusive by
 * architecture, and the stacked form this replaced -- both blocks inside one
 * function, one after the other -- read as though a build could have two and
 * would then try each in turn.
 */
#if defined(TORIDRAW_APPLE_NEON_PROJECTION_ASM)
#include "impl/projection/projection.perspective.prepared.neon64.u.c"
#elif ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED) && \
    !defined(PREPARED_PROJECTION_DISABLED)
#include "impl/projection/projection.perspective.prepared.neon32.u.c"
#elif defined(TORIDRAW_SSE2_PREPARED_PROJECTION)
#include "impl/projection/projection.perspective.prepared.sse2.u.c"
#else
#include "impl/projection/projection.perspective.prepared.scalar.u.c"
#endif

#endif /* TORIDRAW_GRAPHICS_PROJECTION_PREPARED_U_C */
