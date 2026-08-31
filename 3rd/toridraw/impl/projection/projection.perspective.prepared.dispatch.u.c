#ifndef TORIDRAW_GRAPHICS_PROJECTION_PREPARED_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_PREPARED_U_C

#include "impl/projection/projection.perspective.prepared.dispatch.h"

/*
 * One lane, selected by the same two macros that decide whether the kernels
 * behind it were built at all: TORIDRAW_APPLE_NEON_PROJECTION_ASM comes from
 * src/makefile, where projection16.aarch64.S is assembled, and
 * TORIDRAW_SSE2_PREPARED_PROJECTION from projection16_simd.u.c, where the SSE2
 * prepared header is included. Nothing else in the tree may test them.
 *
 * `#elif` and not two `#if`s. The two are mutually exclusive by architecture,
 * and the stacked form this replaced -- both blocks inside one function, one
 * after the other -- read as though a build could have both and would then try
 * each in turn.
 */
#if defined(TORIDRAW_APPLE_NEON_PROJECTION_ASM)
#include "impl/projection/projection.perspective.prepared.neon.u.c"
#elif defined(TORIDRAW_SSE2_PREPARED_PROJECTION)
#include "impl/projection/projection.perspective.prepared.sse2.u.c"
#else
#include "impl/projection/projection.perspective.prepared.scalar.u.c"
#endif

#endif /* TORIDRAW_GRAPHICS_PROJECTION_PREPARED_U_C */
