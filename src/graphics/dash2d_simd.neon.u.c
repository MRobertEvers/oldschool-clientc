#include <arm_neon.h>
#include <stdint.h>

void
dash2d_blit_sprite_subrect_neon(
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

    /* Client.ts Pix8.draw(x,y): destination is (x + cropX, y + cropY) */
    x_offset += sprite->crop_x;
    y_offset += sprite->crop_y;

    int cl = view_port->clip_left;
    int ct = view_port->clip_top;
    int cr = view_port->clip_right;
    int cb = view_port->clip_bottom;

    /* 1. Pre-calculate clipping mathematically to remove branches from the inner loop */
    int dst_x1 = x_offset;
    int dst_y1 = y_offset;
    int dst_x2 = x_offset + src_w;
    int dst_y2 = y_offset + src_h;

    int clip_x1 = (dst_x1 > cl) ? dst_x1 : cl;
    int clip_y1 = (dst_y1 > ct) ? dst_y1 : ct;
    int clip_x2 = (dst_x2 < cr) ? dst_x2 : cr;
    int clip_y2 = (dst_y2 < cb) ? dst_y2 : cb;

    int draw_w = clip_x2 - clip_x1;
    int draw_h = clip_y2 - clip_y1;

    /* If the rect is entirely outside the viewport, exit early */
    if( draw_w <= 0 || draw_h <= 0 )
        return;

    /* Calculate starting offsets in the source sprite */
    int src_draw_x = src_x + (clip_x1 - dst_x1);
    int src_draw_y = src_y + (clip_y1 - dst_y1);

    int stride = view_port->stride;
    int sw = sprite->width;

    /* Cast to 32-bit unsigned for predictable bitwise SIMD behavior */
    const uint32_t* src_base = (const uint32_t*)sprite->pixels_argb;
    uint32_t* dst_base = (uint32_t*)pixel_buffer;

    uint32x4_t zero_vec = vdupq_n_u32(0);

    for( int y = 0; y < draw_h; y++ )
    {
        /* Calculate row pointers once per scanline */
        const uint32_t* src_row = src_base + ((src_draw_y + y) * sw) + src_draw_x;
        uint32_t* dst_row = dst_base + ((clip_y1 + y) * stride) + clip_x1;

        int x = 0;

        /* 2. Process pixels in chunks of 4 using NEON */
        for( ; x <= draw_w - 4; x += 4 )
        {
            /* Load 4 source pixels */
            uint32x4_t src_pixels = vld1q_u32(src_row + x);

            /* Create a mask: 0xFFFFFFFF where pixel is 0 (transparent), 0x00000000 otherwise */
            uint32x4_t is_zero_mask = vceqq_u32(src_pixels, zero_vec);

            /* Load 4 destination pixels */
            uint32x4_t dst_pixels = vld1q_u32(dst_row + x);

            /* * Bitwise Select: result = (is_zero_mask) ? dst_pixels : src_pixels
             * If the mask is 1 (transparent), keep destination.
             * If the mask is 0 (opaque), overwrite with source.
             */
            uint32x4_t result = vbslq_u32(is_zero_mask, dst_pixels, src_pixels);

            /* Store the 4 blended pixels back to memory */
            vst1q_u32(dst_row + x, result);
        }

        /* 3. Handle the remaining tail pixels (0 to 3 pixels) */
        for( ; x < draw_w; x++ )
        {
            uint32_t pixel = src_row[x];
            if( pixel != 0 )
            {
                dst_row[x] = pixel;
            }
        }
    }
}
