#ifndef TORIDRAW_GOURAUD_ALPHA_SPAN_NEON_U_C
#define TORIDRAW_GOURAUD_ALPHA_SPAN_NEON_U_C

#include "impl/raster/span/span.gouraudhsllightness.alpha.dispatch.h"
#include <arm_neon.h>

/*
 * The AArch64 lane: four pixels blended per vector, the 8-bit channels widened
 * to 16 bits in two halves so the multiply cannot overflow, and narrowed back
 * with a saturating move.
 */

// alpha_blend for 4 pixels at a time
static inline uint32x4_t
alpha_blend4_neon(
    uint32x4_t pixels,
    uint32x4_t colors,
    int alpha)
{
    // Calculate inverse alpha
    int alpha_inv = 0xFF - alpha;

    // Expand pixels to 16-bit
    uint8x16_t pixels_u8 = vreinterpretq_u8_u32(pixels);
    uint16x8_t pixels_lo = vmovl_u8(vget_low_u8(pixels_u8));
    uint16x8_t pixels_hi = vmovl_u8(vget_high_u8(pixels_u8));

    // Expand texels to 16-bit
    uint8x16_t texels_u8 = vreinterpretq_u8_u32(colors);
    uint16x8_t texels_lo = vmovl_u8(vget_low_u8(texels_u8));
    uint16x8_t texels_hi = vmovl_u8(vget_high_u8(texels_u8));

    // Apply alpha blending: (pixels * alpha_inv + texels * alpha) >> 8
    uint16x8_t result_lo = vmulq_n_u16(pixels_lo, alpha_inv);
    result_lo = vmlaq_n_u16(result_lo, texels_lo, alpha);
    result_lo = vshrq_n_u16(result_lo, 8);

    uint16x8_t result_hi = vmulq_n_u16(pixels_hi, alpha_inv);
    result_hi = vmlaq_n_u16(result_hi, texels_hi, alpha);
    result_hi = vshrq_n_u16(result_hi, 8);

    // Narrow back to 8-bit
    uint8x16_t result_u8 = vcombine_u8(vqmovn_u16(result_lo), vqmovn_u16(result_hi));

    return vreinterpretq_u32_u8(result_u8);
}

static inline void
raster_linear_alpha_s4(
    toripixel_t* RESTRICT pixel_buffer,
    int offset,
    int rgb_color,
    int alpha)
{
    // Load 4 existing pixels from buffer
    uint32x4_t pixels = vld1q_u32(&pixel_buffer[offset]);

    // Create vector with 4 copies of rgb_color
    uint32x4_t colors = vdupq_n_u32(rgb_color);

    // Apply alpha blending using vectorized function
    uint32x4_t result = alpha_blend4_neon(pixels, colors, alpha);

    // Store result back to buffer
    vst1q_u32(&pixel_buffer[offset], result);
}

#endif /* TORIDRAW_GOURAUD_ALPHA_SPAN_NEON_U_C */
