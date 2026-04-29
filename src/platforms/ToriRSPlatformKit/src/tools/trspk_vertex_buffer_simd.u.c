#ifndef TRSPK_VERTEX_BUFFER_SIMD_U_C
#define TRSPK_VERTEX_BUFFER_SIMD_U_C

// clang-format off
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "trspk_vertex_buffer_simd.neon.u.c"
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "trspk_vertex_buffer_simd.avx.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "trspk_vertex_buffer_simd.sse41.u.c"
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "trspk_vertex_buffer_simd.sse2.u.c"
#else
#include "trspk_vertex_buffer_simd.scalar.u.c"
#endif
// clang-format on

#endif /* TRSPK_VERTEX_BUFFER_SIMD_U_C */
