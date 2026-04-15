#include <arm_neon.h>
#include "dash_math.h"

#include <stdint.h>

void
dash2d_blit_rotated_ex_neon(
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

    static const int32_t j_lut[4] = {0, 1, 2, 3};
    int32x4_t j4 = vld1q_s32(j_lut);
    uint32x4_t zero_u = vdupq_n_u32(0);
    int32x4_t zero_s = vdupq_n_s32(0);
    int32x4_t w_vec = vdupq_n_s32(src_width);
    int32x4_t h_vec = vdupq_n_s32(src_height);

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

            int32x4_t acc_x_vec = vaddq_s32(vdupq_n_s32(chunk_base_x), vmulq_n_s32(j4, cos));
            int32x4_t acc_y_vec = vsubq_s32(vdupq_n_s32(chunk_base_y), vmulq_n_s32(j4, sin));

            int32x4_t sx = vaddq_s32(vshrq_n_s32(acc_x_vec, 16), vdupq_n_s32(src_anchor_x));
            int32x4_t sy = vaddq_s32(vshrq_n_s32(acc_y_vec, 16), vdupq_n_s32(src_anchor_y));

            uint32x4_t vx_lo = vcgeq_s32(sx, zero_s);
            uint32x4_t vx_hi = vcltq_s32(sx, w_vec);
            uint32x4_t vy_lo = vcgeq_s32(sy, zero_s);
            uint32x4_t vy_hi = vcltq_s32(sy, h_vec);
            uint32x4_t valid = vandq_u32(vandq_u32(vx_lo, vx_hi), vandq_u32(vy_lo, vy_hi));

            int32_t sx_arr[4];
            int32_t sy_arr[4];
            uint32_t valid_arr[4];
            vst1q_s32(sx_arr, sx);
            vst1q_s32(sy_arr, sy);
            vst1q_u32(valid_arr, valid);

            uint32_t pix[4];
            for( int k = 0; k < 4; k++ )
            {
                if( valid_arr[k] != 0u )
                {
                    int ix = sx_arr[k];
                    int iy = sy_arr[k];
                    pix[k] = src_u[(size_t)(src_crop_y + iy) * (size_t)src_stride + (size_t)(src_crop_x + ix)];
                }
                else
                {
                    pix[k] = 0;
                }
            }
            uint32x4_t src_px = vld1q_u32(pix);

            int dst_x_abs = min_x + col;
            uint32x4_t dst_px = vld1q_u32(dst_u + (size_t)dst_y_abs * (size_t)dst_stride + (size_t)dst_x_abs);

            uint32x4_t is_zero = vceqq_u32(src_px, zero_u);
            uint32x4_t keep_dst = vorrq_u32(vmvnq_u32(valid), is_zero);
            uint32x4_t result = vbslq_u32(keep_dst, dst_px, src_px);

            vst1q_u32(dst_u + (size_t)dst_y_abs * (size_t)dst_stride + (size_t)dst_x_abs, result);
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
