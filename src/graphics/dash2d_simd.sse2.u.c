#include <emmintrin.h>
#include <stdint.h>

void
dash2d_blit_sprite_subrect_sse2(
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

    x_offset += sprite->crop_x;
    y_offset += sprite->crop_y;

    int cl = view_port->clip_left;
    int ct = view_port->clip_top;
    int cr = view_port->clip_right;
    int cb = view_port->clip_bottom;

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

    if( draw_w <= 0 || draw_h <= 0 )
        return;

    int src_draw_x = src_x + (clip_x1 - dst_x1);
    int src_draw_y = src_y + (clip_y1 - dst_y1);

    int stride = view_port->stride;
    int sw = sprite->width;

    const uint32_t* src_base = (const uint32_t*)sprite->pixels_argb;
    uint32_t* dst_base = (uint32_t*)pixel_buffer;

    __m128i zero_vec = _mm_setzero_si128();

    for( int y = 0; y < draw_h; y++ )
    {
        const uint32_t* src_row = src_base + ((src_draw_y + y) * sw) + src_draw_x;
        uint32_t* dst_row = dst_base + ((clip_y1 + y) * stride) + clip_x1;

        int x = 0;

        for( ; x <= draw_w - 4; x += 4 )
        {
            __m128i src_pixels = _mm_loadu_si128((const __m128i*)(src_row + x));
            __m128i is_zero_mask = _mm_cmpeq_epi32(src_pixels, zero_vec);
            __m128i dst_pixels = _mm_loadu_si128((const __m128i*)(dst_row + x));
            /* result = (is_zero_mask & dst_pixels) | (~is_zero_mask & src_pixels) */
            __m128i result = _mm_or_si128(
                _mm_and_si128(is_zero_mask, dst_pixels),
                _mm_andnot_si128(is_zero_mask, src_pixels));
            _mm_storeu_si128((__m128i*)(dst_row + x), result);
        }

        for( ; x < draw_w; x++ )
        {
            uint32_t pixel = src_row[x];
            if( pixel != 0 )
                dst_row[x] = pixel;
        }
    }
}
