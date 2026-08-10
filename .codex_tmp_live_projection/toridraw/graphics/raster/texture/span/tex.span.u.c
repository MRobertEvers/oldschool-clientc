#ifndef TEX_SPAN_U_C
#define TEX_SPAN_U_C

#include "graphics/dash_restrict.h"

#include <stdint.h>

#include "graphics/clamp.h"

// clang-format off
#include "graphics/shade.h"
// clang-format on

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "tex.span.neon.u.c"
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "tex.span.avx.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "tex.span.sse41.u.c"
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "tex.span.sse2.u.c"
#else
#include "tex.span.scalar.u.c"
#endif

#endif
