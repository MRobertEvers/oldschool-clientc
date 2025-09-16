
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>

// alpha_blend for 4 pixels at a time
static inline uint32x4_t
alpha_blend4(uint32x4_t pixels, uint32x4_t colors, int alpha)
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
raster_linear_alpha_s4(uint32_t* pixel_buffer, int offset, int rgb_color, int alpha)
{
    // Load 4 existing pixels from buffer
    uint32x4_t pixels = vld1q_u32(&pixel_buffer[offset]);

    // Create vector with 4 copies of rgb_color
    uint32x4_t colors = vdupq_n_u32(rgb_color);

    // Apply alpha blending using vectorized function
    uint32x4_t result = alpha_blend4(pixels, colors, alpha);

    // Store result back to buffer
    vst1q_u32(&pixel_buffer[offset], result);
}

#else

static inline void
raster_linear_alpha_s4(uint32_t* pixel_buffer, int offset, int rgb_color, int alpha)
{
    for( int i = 0; i < 4; i++ )
    {
        int rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
        offset += 1;
    }
}

#endif