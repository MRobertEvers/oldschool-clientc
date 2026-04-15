#ifndef TEXTURE_SIMD_U_C
#define TEXTURE_SIMD_U_C

#include <stdint.h>

#include "clamp.h"

// clang-format off
#include "shade.h"
// clang-format on

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "texture_simd.neon.u.c"
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "texture_simd.avx.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "texture_simd.sse41.u.c"
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "texture_simd.sse2.u.c"
#else
#include "texture_simd.scalar.u.c"
#endif

#endif
