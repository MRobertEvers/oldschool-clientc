#ifndef TORIDRAW_GRAPHICS_PROJECTION_BOUND_SSE41_U_C
#define TORIDRAW_GRAPHICS_PROJECTION_BOUND_SSE41_U_C

#include "impl/projection/projection.bound.dispatch.h"

#include <smmintrin.h>

/* _mm_min_epi32/_mm_max_epi32 are SSE4.1; plain SSE2 has no 32-bit lane
 * min/max at all, which is why that lane takes the scalar file instead. */

static inline void
toridraw_bound_fold_prepared(const int* b, struct ToriDraw_ScreenBound* box)
{
    /* Sixteen scalar compares: there is no horizontal min here either, and
     * this runs once per model, not once per vertex. */
    toridraw_bound_fold_prepared_scalar(b, box);
}

static inline int
toridraw_bound_sweep(
    const int* svx,
    const int* svy,
    int vertex_count,
    struct ToriDraw_ScreenBound* box)
{
    __m128i vmin_x;
    __m128i vmax_x;
    __m128i vmin_y;
    __m128i vmax_y;
    int i;

    if( vertex_count < 4 )
        return 0;

    vmin_x = _mm_loadu_si128((const __m128i*)svx);
    vmax_x = vmin_x;
    vmin_y = _mm_loadu_si128((const __m128i*)svy);
    vmax_y = vmin_y;

    for( i = 4; i + 4 <= vertex_count; i += 4 )
    {
        __m128i const x = _mm_loadu_si128((const __m128i*)(svx + i));
        __m128i const y = _mm_loadu_si128((const __m128i*)(svy + i));
        vmin_x = _mm_min_epi32(vmin_x, x);
        vmax_x = _mm_max_epi32(vmax_x, x);
        vmin_y = _mm_min_epi32(vmin_y, y);
        vmax_y = _mm_max_epi32(vmax_y, y);
    }

    /* 4 -> 2 -> 1 with the lane shuffles SSE2 already had. */
    vmin_x = _mm_min_epi32(vmin_x, _mm_shuffle_epi32(vmin_x, 0x4E));
    vmin_x = _mm_min_epi32(vmin_x, _mm_shuffle_epi32(vmin_x, 0xB1));
    vmax_x = _mm_max_epi32(vmax_x, _mm_shuffle_epi32(vmax_x, 0x4E));
    vmax_x = _mm_max_epi32(vmax_x, _mm_shuffle_epi32(vmax_x, 0xB1));
    vmin_y = _mm_min_epi32(vmin_y, _mm_shuffle_epi32(vmin_y, 0x4E));
    vmin_y = _mm_min_epi32(vmin_y, _mm_shuffle_epi32(vmin_y, 0xB1));
    vmax_y = _mm_max_epi32(vmax_y, _mm_shuffle_epi32(vmax_y, 0x4E));
    vmax_y = _mm_max_epi32(vmax_y, _mm_shuffle_epi32(vmax_y, 0xB1));

    box->min_x = _mm_cvtsi128_si32(vmin_x);
    box->max_x = _mm_cvtsi128_si32(vmax_x);
    box->min_y = _mm_cvtsi128_si32(vmin_y);
    box->max_y = _mm_cvtsi128_si32(vmax_y);
    return i;
}

#endif /* TORIDRAW_GRAPHICS_PROJECTION_BOUND_SSE41_U_C */
