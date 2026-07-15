#ifndef TEX_VPENTIUM4_SPAN_U_C
#define TEX_VPENTIUM4_SPAN_U_C

#include "graphics/clamp.h"
#include "graphics/dash_restrict.h"

#include <stdint.h>

// clang-format off
#include "graphics/shade.h"
// clang-format on
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "tex_vpentium4.span.neon.u.c"
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "tex_vpentium4.span.avx.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "tex_vpentium4.span.sse41.u.c"
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "tex_vpentium4.span.sse2.u.c"
#else
#include "tex_vpentium4.span.scalar.u.c"
#endif

#endif /* TEX_VPENTIUM4_SPAN_U_C */
