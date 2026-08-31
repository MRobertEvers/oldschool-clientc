#ifndef TORIDRAW_BLIT_SIMD_SSE2_H
#define TORIDRAW_BLIT_SIMD_SSE2_H

/*
 * SSE2 lane: four pixels of alpha tested per iteration.
 *
 *   movdqu    four source words
 *   pand      keep the alpha byte, drop the colour
 *   pcmpeqd   all four against the wanted alpha at once
 *   movmskps  the four results as four bits
 *
 * The loop branches once per four pixels instead of once per pixel, and the
 * branch it does take is the one that ends the run -- the mispredict a sprite
 * edge causes is paid once per run rather than once per edge pixel.
 *
 * The load is unaligned because neither end is under our control: the source
 * is a sprite row plus a clip offset, and both can start at any pixel.
 *
 * FIRST_DIFF turns the four-bit mask into the index of the first lane that did
 * NOT match, without a branch per lane. Indexed by the four inverted bits, so
 * entry 0 (every lane matched) is 4 and never reached -- the mask is only
 * consulted when at least one lane differed.
 */

#include <assert.h>
#include <emmintrin.h>
#include <stdint.h>

static const unsigned char toridraw_blit_first_diff[16] = { 4, 0, 1, 0, 2, 0, 1, 0,
                                                            3, 0, 1, 0, 2, 0, 1, 0 };

static inline int
toridraw_blit_alpha_run_impl(
    uint32_t const* row,
    int count,
    uint32_t want)
{
    __m128i const amask = _mm_set1_epi32((int)0xFF000000u);
    __m128i const wantv = _mm_set1_epi32((int)(want << 24));
    int i = 0;

    assert(row);
    assert(want == 0u || want == 255u);

    for( ; i + 4 <= count; i += 4 )
    {
        __m128i v = _mm_loadu_si128((__m128i const*)(row + i));
        int m = _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(v, amask), wantv)));

        if( m != 0xF )
            return i + toridraw_blit_first_diff[(~m) & 0xF];
    }
    while( i < count && (row[i] >> 24) == want )
        i++;
    return i;
}

#endif /* TORIDRAW_BLIT_SIMD_SSE2_H */
