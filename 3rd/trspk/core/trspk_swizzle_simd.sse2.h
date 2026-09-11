#ifndef TRSPK_SWIZZLE_SIMD_SSE2_H
#define TRSPK_SWIZZLE_SIMD_SSE2_H

/*
 * SSE2 lane: four pixels per pass. Included only from trspk_swizzle_simd.h,
 * which defines trspk_swizzle_pixel above it -- used here for the tail.
 *
 * The R/B exchange is one mask and two shifts against the whole register.
 * Masking with 0x00FF00FF isolates R (bits 16-23) and B (bits 0-7) together;
 * shifting that right by 16 drops R into B's place, shifting it left by 16
 * drops B into R's place and pushes R out of the lane entirely, so the two
 * halves simply OR back together. Green and alpha never move. PSHUFB would do
 * the whole shuffle in one instruction but is SSSE3, and the target is a
 * Pentium 4.
 *
 * The alpha rule is branchless: both questions -- "is the alpha byte zero" and
 * "does the pixel have any colour" -- become compare masks, and the choice
 * between them is an ANDNOT/AND pair.
 *
 * Loads and stores are unaligned on purpose: sprite buffers arrive from the
 * cache loader at whatever alignment the archive gave them, and src == dst is
 * the normal case, since the GL lanes swizzle in place.
 */

#include <emmintrin.h>
#include <stddef.h>
#include <stdint.h>

static inline void
trspk_swizzle_argb_to_abgr_impl(uint32_t const* src, uint32_t* dst, size_t count)
{
    const __m128i mask_rb = _mm_set1_epi32(0x00FF00FF);
    const __m128i mask_g = _mm_set1_epi32(0x0000FF00);
    const __m128i mask_rgb = _mm_set1_epi32(0x00FFFFFF);
    const __m128i mask_a = _mm_set1_epi32((int)0xFF000000);
    const __m128i zero = _mm_setzero_si128();
    size_t i = 0u;

    for( ; i + 4u <= count; i += 4u )
    {
        __m128i pix = _mm_loadu_si128((const __m128i*)(const void*)(src + i));
        __m128i rb = _mm_and_si128(pix, mask_rb);
        __m128i colour = _mm_or_si128(
            _mm_or_si128(_mm_srli_epi32(rb, 16), _mm_slli_epi32(rb, 16)),
            _mm_and_si128(pix, mask_g));

        __m128i a_hi = _mm_and_si128(pix, mask_a);
        /* cmpeq against zero twice: the first says "is zero", the second
         * inverts it, giving "has any colour". */
        __m128i has_rgb = _mm_cmpeq_epi32(
            _mm_cmpeq_epi32(_mm_and_si128(pix, mask_rgb), zero), zero);
        __m128i a_zero = _mm_cmpeq_epi32(a_hi, zero);
        __m128i alpha = _mm_or_si128(
            _mm_andnot_si128(a_zero, a_hi),
            _mm_and_si128(a_zero, _mm_and_si128(has_rgb, mask_a)));

        _mm_storeu_si128((__m128i*)(void*)(dst + i), _mm_or_si128(colour, alpha));
    }

    for( ; i < count; i++ )
        dst[i] = trspk_swizzle_pixel(src[i]);
}

#endif
