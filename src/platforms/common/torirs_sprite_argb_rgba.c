#include "platforms/common/torirs_sprite_argb_rgba.h"

#include <stddef.h>

void
torirs_copy_sprite_argb_pixels_to_rgba8(
    const uint32_t* src_argb,
    uint8_t* dst_rgba,
    int pixel_count)
{
    if( !src_argb || !dst_rgba || pixel_count <= 0 )
        return;
    for( int p = 0; p < pixel_count; ++p )
    {
        uint32_t pix = (uint32_t)src_argb[p];
        uint8_t a_hi = (uint8_t)((pix >> 24) & 0xFFu);
        uint32_t rgb = pix & 0x00FFFFFFu;
        uint8_t a = 0;
        if( a_hi != 0 )
            a = a_hi;
        else if( rgb != 0u )
            a = 0xFFu;
        dst_rgba[(size_t)p * 4u + 0u] = (uint8_t)((pix >> 16) & 0xFFu);
        dst_rgba[(size_t)p * 4u + 1u] = (uint8_t)((pix >> 8) & 0xFFu);
        dst_rgba[(size_t)p * 4u + 2u] = (uint8_t)(pix & 0xFFu);
        dst_rgba[(size_t)p * 4u + 3u] = a;
    }
}
