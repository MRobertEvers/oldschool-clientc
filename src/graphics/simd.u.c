#ifndef SIMD_U_C
#define SIMD_U_C

// fill_pixels.c
#include <stddef.h>
#include <stdint.h>

#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

static inline void
simd_write4(uint32_t* buffer, uint32_t value)
{
#if defined(__AVX2__)
    // AVX2: 8 pixels at a time
    __m256i vcolor = _mm256_set1_epi32(value);
    for( ; i + 8 <= count; i += 8 )
    {
        _mm256_storeu_si256((__m256i*)(buffer + i), vcolor);
    }
#elif defined(__SSE2__)
    // SSE2: 4 pixels at a time
    __m128i vcolor = _mm_set1_epi32(value);
    for( ; i + 4 <= count; i += 4 )
    {
        _mm_storeu_si128((__m128i*)(buffer + i), vcolor);
    }
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    // NEON: 4 pixels at a time
    uint32x4_t vcolor = vdupq_n_u32(value);
    vst1q_u32(buffer, vcolor);
#else
    // Scalar fallback for remaining pixels
    for( ; i < 4; i++ )
    {
        buffer[i] = value;
    }
#endif
}

#endif