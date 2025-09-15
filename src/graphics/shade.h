#ifndef SHADE_H
#define SHADE_H

#include <arm_neon.h>

#include <assert.h>

/**
 * NEON vectorized version that shades 4 colors at once.
 * Takes arrays of 4 base colors and 4 shade values.
 * Returns the shaded colors in the output array.
 *
 * @param base_colors Array of 4 RGB colors to shade
 * @param shades Array of 4 shade intensities (0-255)
 * @param output Array to store 4 resulting shaded colors
 */
static inline void
shade_blend_neon(const int32_t* base_colors, const int32_t* shades, int32_t* output)
{
    // Load 4 colors and 4 shades into NEON registers
    int32x4_t colors = vld1q_s32(base_colors);
    int32x4_t shade_values = vld1q_s32(shades);

    // Extract R and B components (0x00FF00FF mask)
    int32x4_t rb_mask = vdupq_n_s32(0x00FF00FF);
    int32x4_t rb = vandq_s32(colors, rb_mask);

    // Extract G component (0x0000FF00 mask)
    int32x4_t g_mask = vdupq_n_s32(0x0000FF00);
    int32x4_t g = vandq_s32(colors, g_mask);

    // Multiply components by shade values
    rb = vmulq_s32(rb, shade_values);
    g = vmulq_s32(g, shade_values);

    // Mask results
    int32x4_t rb_result_mask = vdupq_n_s32(0xFF00FF00);
    int32x4_t g_result_mask = vdupq_n_s32(0x00FF0000);
    rb = vandq_s32(rb, rb_result_mask);
    g = vandq_s32(g, g_result_mask);

    // Combine results and shift right by 8
    int32x4_t combined = vorrq_s32(rb, g);
    int32x4_t result = vshrq_n_s32(combined, 8);

    // Store results back to memory
    vst1q_s32(output, result);
}

/**
 * This is a cute way to alpha blend two rgb colors.
 *
 * Since alpha is 0-0xFF, then (alpha * 0xFF) < 0x10000 i.e. (0 to 0xFFFF, 0xFF^2),
 * so we can multiply the r,b components (0xFF00FF) and the result of the multiplcation
 * for r,b will be 0xRRRRBBBB, so we can shift and mask.
 *
 * @param alpha
 * @param base
 * @param other
 * @return int
 */
static inline int
shade_blend(int base, int shade)
{
    int rb = base & 0x00ff00ff;
    int g = base & 0x0000ff00;

    rb *= shade;
    g *= shade;

    rb &= 0xFF00FF00;
    g &= 0x00FF0000;

    return (rb | g) >> 8;
}

#endif