#ifndef PAINTERS_BUCKET_SIMD_U_C
#define PAINTERS_BUCKET_SIMD_U_C

// clang-format off
#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "painters_bucket_simd.neon.u.c"
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "painters_bucket_simd.avx.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "painters_bucket_simd.sse41.u.c"
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "painters_bucket_simd.sse2.u.c"
#else
#include "painters_bucket_simd.scaler.u.c"
#endif
// clang-format on

#endif /* PAINTERS_BUCKET_SIMD_U_C */
