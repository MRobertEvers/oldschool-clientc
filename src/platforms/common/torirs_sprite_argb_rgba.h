#ifndef TORIRS_SPRITE_ARGB_RGBA_H
#define TORIRS_SPRITE_ARGB_RGBA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Expand packed 0xAARRGGBB (DashSprite pixels_argb) into RGBA8 interleaved rows.
 * Matches alpha rules used by the Metal sprite upload path.
 */
void
torirs_copy_sprite_argb_pixels_to_rgba8(
    const uint32_t* src_argb,
    uint8_t* dst_rgba,
    int pixel_count);

#ifdef __cplusplus
}
#endif

#endif
