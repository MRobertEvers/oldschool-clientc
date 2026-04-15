#include <smmintrin.h>

static inline __m128i
abs_epi32_sse41(__m128i v)
{
    __m128i zero = _mm_setzero_si128();
    __m128i neg = _mm_sub_epi32(zero, v);
    return _mm_max_epi32(v, neg);
}

static void
bucket_fill_distances(
    struct PainterBucketCtx* w,
    struct Painter* painter,
    int camera_sx,
    int camera_sz,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int max_level)
{
    __m128i v_step = _mm_setr_epi32(0, 1, 2, 3);
    __m128i v_cam_x = _mm_set1_epi32(camera_sx);
    __m128i v_cam_z = _mm_set1_epi32(camera_sz);

    for( int s = 0; s < max_level && s < painter->levels; s++ )
    {
        if( (painter->level_mask & (1u << s)) == 0 )
            continue;

        for( int z = min_draw_z; z < max_draw_z; z++ )
        {
            __m128i v_z = _mm_set1_epi32(z);
            __m128i v_dist_z = abs_epi32_sse41(_mm_sub_epi32(v_z, v_cam_z));

            int x = min_draw_x;
            int ti_base = painter_coord_idx(painter, min_draw_x, z, s);

            for( ; x <= max_draw_x - 4; x += 4, ti_base += 4 )
            {
                __m128i v_x_base = _mm_set1_epi32(x);
                __m128i v_x = _mm_add_epi32(v_x_base, v_step);
                __m128i v_dist_x = abs_epi32_sse41(_mm_sub_epi32(v_x, v_cam_x));
                __m128i v_total_dist = _mm_add_epi32(v_dist_x, v_dist_z);
                _mm_storeu_si128((__m128i*)&w->dist[ti_base], v_total_dist);
            }

            for( ; x < max_draw_x; x++, ti_base++ )
            {
                w->dist[ti_base] = abs(x - camera_sx) + abs(z - camera_sz);
            }
        }
    }
}
