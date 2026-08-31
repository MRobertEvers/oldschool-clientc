#ifndef TORIDRAW_GOURAUD_ALPHA_SPAN_AVX_U_C
#define TORIDRAW_GOURAUD_ALPHA_SPAN_AVX_U_C

#include "impl/raster/span/span.gouraudhsllightness.alpha.dispatch.h"

#include <immintrin.h>

/*
 * The AVX2 lane. Four pixels in one 128-bit vector, the same widen/multiply/
 * narrow as the others -- what AVX2 buys here is not width but that it does
 * not carry the efficiency-core penalty the plain SSE2 lane was measured to
 * pay; see that lane's note.
 */

// alpha_blend for 4 pixels at a time using AVX2
static inline __m128i
alpha_blend4_avx2(
    __m128i pixels,
    __m128i colors,
    int alpha)
{
    // Calculate inverse alpha
    int alpha_inv = 0xFF - alpha;

    // Expand pixels to 16-bit (unpack low and high parts)
    __m128i pixels_lo = _mm_unpacklo_epi8(pixels, _mm_setzero_si128());
    __m128i pixels_hi = _mm_unpackhi_epi8(pixels, _mm_setzero_si128());

    // Expand colors to 16-bit (unpack low and high parts)
    __m128i colors_lo = _mm_unpacklo_epi8(colors, _mm_setzero_si128());
    __m128i colors_hi = _mm_unpackhi_epi8(colors, _mm_setzero_si128());

    // Apply alpha blending: (pixels * alpha_inv + colors * alpha) >> 8
    __m128i result_lo = _mm_mullo_epi16(pixels_lo, _mm_set1_epi16(alpha_inv));
    result_lo = _mm_add_epi16(result_lo, _mm_mullo_epi16(colors_lo, _mm_set1_epi16(alpha)));
    result_lo = _mm_srli_epi16(result_lo, 8);

    __m128i result_hi = _mm_mullo_epi16(pixels_hi, _mm_set1_epi16(alpha_inv));
    result_hi = _mm_add_epi16(result_hi, _mm_mullo_epi16(colors_hi, _mm_set1_epi16(alpha)));
    result_hi = _mm_srli_epi16(result_hi, 8);

    // Narrow back to 8-bit
    __m128i result = _mm_packus_epi16(result_lo, result_hi);

    return result;
}

static inline void
raster_linear_alpha_s4(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    int rgb_color,
    int alpha)
{
    // Load 4 existing pixels from buffer
    __m128i pixels = _mm_loadu_si128((__m128i*)&pixel_buffer[offset]);

    // Create vector with 4 copies of rgb_color
    __m128i colors = _mm_set1_epi32(rgb_color);

    // Apply alpha blending using vectorized function
    __m128i result = alpha_blend4_avx2(pixels, colors, alpha);

    // Store result back to buffer
    _mm_storeu_si128((__m128i*)&pixel_buffer[offset], result);
}

#endif /* TORIDRAW_GOURAUD_ALPHA_SPAN_AVX_U_C */
