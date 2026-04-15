#include "dash.h"

#include <stdint.h>

void
dash2d_blit_sprite_subrect_scalar(
    struct DashGraphics* dash,
    struct DashSprite* sprite,
    struct DashViewPort* view_port,
    int x_offset,
    int y_offset,
    int src_x,
    int src_y,
    int src_w,
    int src_h,
    int* pixel_buffer)
{
    (void)dash;
    if( !sprite || !sprite->pixels_argb || src_w <= 0 || src_h <= 0 )
        return;
    if( src_x < 0 || src_y < 0 || src_x + src_w > sprite->width || src_y + src_h > sprite->height )
        return;

    /* Apply crop offsets */
    x_offset += sprite->crop_x;
    y_offset += sprite->crop_y;

    /* 1. Mathematical Pre-Clipping */
    int cl = view_port->clip_left;
    int ct = view_port->clip_top;
    int cr = view_port->clip_right;
    int cb = view_port->clip_bottom;

    int dst_x1 = x_offset;
    int dst_y1 = y_offset;
    int dst_x2 = x_offset + src_w;
    int dst_y2 = y_offset + src_h;

    /* Clamp the destination rectangle to the viewport bounds */
    int clip_x1 = (dst_x1 > cl) ? dst_x1 : cl;
    int clip_y1 = (dst_y1 > ct) ? dst_y1 : ct;
    int clip_x2 = (dst_x2 < cr) ? dst_x2 : cr;
    int clip_y2 = (dst_y2 < cb) ? dst_y2 : cb;

    int draw_w = clip_x2 - clip_x1;
    int draw_h = clip_y2 - clip_y1;

    /* If the rectangle is entirely clipped, exit early */
    if( draw_w <= 0 || draw_h <= 0 )
        return;

    /* 2. Calculate starting positions based on clipping */
    int src_draw_x = src_x + (clip_x1 - dst_x1);
    int src_draw_y = src_y + (clip_y1 - dst_y1);

    int src_stride = sprite->width;
    int dst_stride = view_port->stride;

    /* 3. Pointer setup */
    const int* src_ptr = sprite->pixels_argb + (src_draw_y * src_stride) + src_draw_x;
    int* dst_ptr = pixel_buffer + (clip_y1 * dst_stride) + clip_x1;

    /* 4. Highly optimized inner loop */
    for( int y = 0; y < draw_h; y++ )
    {
        /* Use local row pointers so the compiler can easily optimize array access */
        const int* src_row = src_ptr;
        int* dst_row = dst_ptr;

        for( int x = 0; x < draw_w; x++ )
        {
            int pixel = src_row[x];
            if( pixel != 0 )
            {
                dst_row[x] = pixel;
            }
        }

        /* Advance pointers to the next row */
        src_ptr += src_stride;
        dst_ptr += dst_stride;
    }
}
