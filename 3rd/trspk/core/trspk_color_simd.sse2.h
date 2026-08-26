#ifndef TRSPK_COLOR_SIMD_SSE2_H
#define TRSPK_COLOR_SIMD_SSE2_H

/*
 * SSE2 lane: four bytes -> four normalised floats, no division.
 *
 *   movd      the packed colour into a register
 *   punpck*   spread the four bytes into four 32-bit lanes, zero-extended
 *   cvtdq2ps  four ints -> four floats in one instruction
 *   mulps     one reciprocal multiply for all four
 *
 * The unpack is done with two PUNPCK steps against a zero register rather than
 * PSHUFB, because PSHUFB is SSSE3 and the target here is a Pentium 4. The byte
 * order out of the interleave is B, G, R, A -- the little-endian order of an
 * 0xAARRGGBB word -- so a single SHUFPS puts it back as R, G, B, A. That is
 * one shuffle, against four shifts and four masks done scalar.
 *
 * The store is unaligned on purpose: these land in struct members (a bake
 * face's color_a[4]) that carry only 4-byte alignment.
 */

#include <emmintrin.h>
#include <stdint.h>

static inline void
trspk_color_unpack_argb_impl(uint32_t argb, float rgba[4])
{
    const __m128i zero = _mm_setzero_si128();
    __m128i packed = _mm_cvtsi32_si128((int)argb);
    __m128 scaled;

    /* 0xAARRGGBB -> bytes B,G,R,A -> words -> dwords, zero-extended. */
    packed = _mm_unpacklo_epi8(packed, zero);
    packed = _mm_unpacklo_epi16(packed, zero);

    scaled = _mm_mul_ps(_mm_cvtepi32_ps(packed), _mm_set1_ps(1.0f / 255.0f));
    /* B,G,R,A -> R,G,B,A */
    scaled = _mm_shuffle_ps(scaled, scaled, _MM_SHUFFLE(3, 0, 1, 2));

    _mm_storeu_ps(rgba, scaled);
}

#endif
