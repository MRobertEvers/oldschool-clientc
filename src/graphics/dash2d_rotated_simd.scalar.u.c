#include "dash_math.h"

#include <stdint.h>

void
dash2d_blit_rotated_ex_scalar(
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

    for( int dst_y_abs = min_y; dst_y_abs < max_y; dst_y_abs++ )
    {
        for( int dst_x_abs = min_x; dst_x_abs < max_x; dst_x_abs++ )
        {
            int rel_x = dst_x_abs - dst_x - dst_anchor_x;
            int rel_y = dst_y_abs - dst_y - dst_anchor_y;

            int src_rel_x = ((rel_x * cos + rel_y * sin) >> 16);
            int src_rel_y = ((-rel_x * sin + rel_y * cos) >> 16);

            int src_x = src_anchor_x + src_rel_x;
            int src_y = src_anchor_y + src_rel_y;

            if( src_x >= 0 && src_x < src_width && src_y >= 0 && src_y < src_height )
            {
                int bx = src_crop_x + src_x;
                int by = src_crop_y + src_y;
                int src_pixel = src_buffer[by * src_stride + bx];
                if( src_pixel != 0 )
                    dst_buffer[dst_y_abs * dst_stride + dst_x_abs] = src_pixel;
            }
        }
    }
}
