#ifndef GOURAUD_SIMD_U_C
#define GOURAUD_SIMD_U_C

#include "impl/raster/span/span.gouraudhsllightness.alpha.dispatch.h"

/*
 * ONE LANE, and nothing else in this file. See the header for what a lane owes
 * and why the four bodies are not stacked here any more.
 *
 * The SSE2 arm also answers __SSE4_1__: nothing in it needs anything above the
 * SSE2 floor, and a build that has SSE4.1 but not AVX2 wants this rather than
 * the scalar fallback.
 */
/*
 * A vector lane is selected only where the framebuffer HAS 8-bit lanes to
 * unpack: all three unpack bytes to 16-bit halves and blend four channels
 * uniformly, which is channel-ORDER independent but not channel-WIDTH
 * independent. A 16-bit format takes the scalar lane, which is written in
 * terms of alpha_blend and so is correct on every format.
 */
#if TORIPIXEL_LANES_8BIT && ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "impl/raster/span/span.gouraudhsllightness.alpha.neon32.u.c"
#elif TORIPIXEL_LANES_8BIT && defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "impl/raster/span/span.gouraudhsllightness.alpha.avx.u.c"
#elif TORIPIXEL_LANES_8BIT && (defined(__SSE2__) || defined(__SSE4_1__)) && !defined(SSE2_DISABLED)
#include "impl/raster/span/span.gouraudhsllightness.alpha.sse2.u.c"
#else
#include "impl/raster/span/span.gouraudhsllightness.alpha.scalar.u.c"
#endif

#endif
