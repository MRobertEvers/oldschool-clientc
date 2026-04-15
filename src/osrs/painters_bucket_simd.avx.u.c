/* AVX2 intrinsics (requires __AVX2__); filename kept as painters_bucket_simd.avx for consistency
 * with project naming (see texture_simd.avx.u.c). */
#include <immintrin.h>

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
    int min_level,
    int max_level)
{
    __m256i v_step8 = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    __m256i v_cam_x = _mm256_set1_epi32(camera_sx);
    __m256i v_cam_z = _mm256_set1_epi32(camera_sz);

    for( int s = min_level; s < max_level && s < painter->levels; s++ )
    {
        for( int z = min_draw_z; z < max_draw_z; z++ )
        {
            __m256i v_z = _mm256_set1_epi32(z);
            __m256i v_dist_z = _mm256_abs_epi32(_mm256_sub_epi32(v_z, v_cam_z));

            int x = min_draw_x;
            int ti_base = painter_coord_idx(painter, min_draw_x, z, s);

            for( ; x <= max_draw_x - 8; x += 8, ti_base += 8 )
            {
                __m256i v_x_base = _mm256_set1_epi32(x);
                __m256i v_x = _mm256_add_epi32(v_x_base, v_step8);
                __m256i v_dist_x = _mm256_abs_epi32(_mm256_sub_epi32(v_x, v_cam_x));
                __m256i v_total_dist = _mm256_add_epi32(v_dist_x, v_dist_z);
                _mm256_storeu_si256((__m256i*)&w->dist[ti_base], v_total_dist);
            }

            for( ; x < max_draw_x; x++, ti_base++ )
            {
                w->dist[ti_base] = abs(x - camera_sx) + abs(z - camera_sz);
            }
        }
    }
}
