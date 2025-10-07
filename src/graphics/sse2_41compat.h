#ifndef SSE2_41COMPAT_H
#define SSE2_41COMPAT_H

#if defined(__SSE4_1__)
#include <smmintrin.h>

static inline __m128i
mullo_epi32_sse4_1(__m128i a, __m128i b)
{
    return _mm_mullo_epi32(a, b);
}

#else // __SSE2__
#include <emmintrin.h>

static inline __m128i
mullo_epi32_sse2(__m128i a, __m128i b)
{
    __m128i tmp1 = _mm_mul_epu32(a, b);                                       // a0*b0, a2*b2
    __m128i tmp2 = _mm_mul_epu32(_mm_srli_si128(a, 4), _mm_srli_si128(b, 4)); // a1*b1, a3*b3

    return _mm_unpacklo_epi32(
        _mm_shuffle_epi32(tmp1, _MM_SHUFFLE(0, 0, 2, 0)),
        _mm_shuffle_epi32(tmp2, _MM_SHUFFLE(0, 0, 2, 0)));
}

#endif

static inline __m128i
mullo_epi32_sse(__m128i a, __m128i b)
{
#if defined(__SSE4_1__)
    return mullo_epi32_sse4_1(a, b);
#else
    return mullo_epi32_sse2(a, b);
#endif
}

#endif