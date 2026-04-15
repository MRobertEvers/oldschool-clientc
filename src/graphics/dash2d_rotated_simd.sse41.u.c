#include <smmintrin.h>
#include "dash_math.h"

#include <stdint.h>

void
dash2d_blit_rotated_ex_sse41(
    int* src_buffer,
    int src_stride,
    int src_crop_x,
    int src_crop_y,
    int src_width,
    int src_height,
    int src_anchor_x,
    int src_anchor_y,
    int* dst_buffer,
    int dst_stride,
    int dst_x,
    int dst_y,
    int dst_width,
    int dst_height,
    int dst_anchor_x,
    int dst_anchor_y,
    int angle_r2pi2048)
{
    int sin = dash_sin(angle_r2pi2048);
    int cos = dash_cos(angle_r2pi2048);

    int min_x = dst_x;
    int min_y = dst_y;
    int max_x = dst_x + dst_width;
    int max_y = dst_y + dst_height;

    if( min_x < 0 )
        min_x = 0;
    if( max_x > dst_stride )
        max_x = dst_stride;
    if( min_x >= max_x )
        return;

    __m128i j4 = _mm_setr_epi32(0, 1, 2, 3);
    __m128i cos4 = _mm_set1_epi32(cos);
    __m128i sin4 = _mm_set1_epi32(sin);
    __m128i zero = _mm_setzero_si128();
    __m128i w_vec = _mm_set1_epi32(src_width);
    __m128i h_vec = _mm_set1_epi32(src_height);
    __m128i m1 = _mm_set1_epi32(-1);

    const uint32_t* src_u = (const uint32_t*)src_buffer;
    uint32_t* dst_u = (uint32_t*)dst_buffer;

    for( int dst_y_abs = min_y; dst_y_abs < max_y; dst_y_abs++ )
    {
        int rel_y = dst_y_abs - dst_y - dst_anchor_y;
        int rel_x_start = min_x - dst_x - dst_anchor_x;
        int acc_x0 = rel_x_start * cos + rel_y * sin;
        int acc_y0 = -rel_x_start * sin + rel_y * cos;

        int row_w = max_x - min_x;
        int col = 0;

        for( ; col <= row_w - 4; col += 4 )
        {
            int chunk_base_x = acc_x0 + col * cos;
            int chunk_base_y = acc_y0 - col * sin;

            __m128i acc_x_vec = _mm_add_epi32(_mm_set1_epi32(chunk_base_x), _mm_mullo_epi32(j4, cos4));
            __m128i acc_y_vec = _mm_sub_epi32(_mm_set1_epi32(chunk_base_y), _mm_mullo_epi32(j4, sin4));

            __m128i sx = _mm_add_epi32(_mm_srai_epi32(acc_x_vec, 16), _mm_set1_epi32(src_anchor_x));
            __m128i sy = _mm_add_epi32(_mm_srai_epi32(acc_y_vec, 16), _mm_set1_epi32(src_anchor_y));

            __m128i vx_lo = _mm_cmpgt_epi32(sx, m1);
            __m128i vx_hi = _mm_cmpgt_epi32(w_vec, sx);
            __m128i vy_lo = _mm_cmpgt_epi32(sy, m1);
            __m128i vy_hi = _mm_cmpgt_epi32(h_vec, sy);
            __m128i valid = _mm_and_si128(_mm_and_si128(vx_lo, vx_hi), _mm_and_si128(vy_lo, vy_hi));

            uint32_t pix[4];
            for( int k = 0; k < 4; k++ )
            {
                if( _mm_extract_epi32(valid, k) != 0 )
                {
                    int ix = _mm_extract_epi32(sx, k);
                    int iy = _mm_extract_epi32(sy, k);
                    pix[k] = src_u[(size_t)(src_crop_y + iy) * (size_t)src_stride + (size_t)(src_crop_x + ix)];
                }
                else
                {
                    pix[k] = 0;
                }
            }
            __m128i src_px = _mm_loadu_si128((const __m128i*)pix);

            int dst_x_abs = min_x + col;
            __m128i dst_px =
                _mm_loadu_si128((const __m128i*)(dst_u + (size_t)dst_y_abs * (size_t)dst_stride + (size_t)dst_x_abs));

            __m128i is_zero = _mm_cmpeq_epi32(src_px, zero);
            __m128i keep_dst = _mm_or_si128(_mm_andnot_si128(valid, _mm_set1_epi32(-1)), is_zero);
            /* blendv_epi8: MSB of mask -> pick second arg (dst) */
            __m128i result = _mm_blendv_epi8(src_px, dst_px, keep_dst);

            _mm_storeu_si128((__m128i*)(dst_u + (size_t)dst_y_abs * (size_t)dst_stride + (size_t)dst_x_abs), result);
        }

        for( ; col < row_w; col++ )
        {
            int dst_x_abs = min_x + col;
            int rel_x = dst_x_abs - dst_x - dst_anchor_x;

            int src_rel_x = ((rel_x * cos + rel_y * sin) >> 16);
            int src_rel_y = ((-rel_x * sin + rel_y * cos) >> 16);

            int src_x = src_anchor_x + src_rel_x;
            int src_y = src_anchor_y + src_rel_y;

            if( src_x >= 0 && src_x < src_width && src_y >= 0 && src_y < src_height )
            {
                int bx = src_crop_x + src_x;
                int by = src_crop_y + src_y;
                uint32_t src_pixel = src_u[(size_t)by * (size_t)src_stride + (size_t)bx];
                if( src_pixel != 0 )
                    dst_u[(size_t)dst_y_abs * (size_t)dst_stride + (size_t)dst_x_abs] = src_pixel;
            }
        }
    }
}
