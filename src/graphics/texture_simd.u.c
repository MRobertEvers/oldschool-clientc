#ifndef TEXTURE_SIMD_U_C
#define TEXTURE_SIMD_U_C

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>

// shade_blend for 4 pixels at a time
static inline uint32x4_t
shade_blend4(uint32x4_t texel, int shade)
{
    // Expand 8-bit channels to 16-bit
    uint8x16_t texel_u8 = vreinterpretq_u8_u32(texel);
    uint16x8_t lo = vmovl_u8(vget_low_u8(texel_u8));
    uint16x8_t hi = vmovl_u8(vget_high_u8(texel_u8));

    // Multiply by shade
    lo = vmulq_n_u16(lo, shade);
    hi = vmulq_n_u16(hi, shade);

    // >> 8 (same as scalar shade_blend)
    lo = vshrq_n_u16(lo, 8);
    hi = vshrq_n_u16(hi, 8);

    // Narrow back to u8
    uint8x16_t shaded_u8 = vcombine_u8(vqmovn_u16(lo), vqmovn_u16(hi));

    return vreinterpretq_u32_u8(shaded_u8);
}

static inline void
raster_linear_transparent_blend_lerp8(
    uint32_t* pixel_buffer,
    int offset,
    const uint32_t* texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade)
{
    int idx[8];
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & 0x3f80;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather)
    uint32x4_t t0 = { texels[idx[0]], texels[idx[1]], texels[idx[2]], texels[idx[3]] };
    uint32x4_t t1 = { texels[idx[4]], texels[idx[5]], texels[idx[6]], texels[idx[7]] };

    // Shade blend in SIMD
    uint32x4_t r0 = shade_blend4(t0, shade);
    uint32x4_t r1 = shade_blend4(t1, shade);

    // Handle transparency: preserve existing pixel buffer where texel is 0
    uint32x4_t zero = vdupq_n_u32(0);
    uint32x4_t existing0 = vld1q_u32(&pixel_buffer[offset]);
    uint32x4_t existing1 = vld1q_u32(&pixel_buffer[offset + 4]);

    // Create masks for non-zero texels
    uint32x4_t mask0 = vceqq_u32(t0, zero); // true where texel is 0
    uint32x4_t mask1 = vceqq_u32(t1, zero); // true where texel is 0

    // Select existing pixels where texel is 0, shaded result where texel is not 0
    r0 = vbslq_u32(mask0, existing0, r0);
    r1 = vbslq_u32(mask1, existing1, r1);

    // Store results
    vst1q_u32(&pixel_buffer[offset], r0);
    vst1q_u32(&pixel_buffer[offset + 4], r1);
}

static inline void
raster_linear_opaque_blend_lerp8(
    uint32_t* pixel_buffer,
    int offset,
    const uint32_t* texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade)
{
    int idx[8];
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & 0x3f80;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather)
    uint32x4_t t0 = { texels[idx[0]], texels[idx[1]], texels[idx[2]], texels[idx[3]] };
    uint32x4_t t1 = { texels[idx[4]], texels[idx[5]], texels[idx[6]], texels[idx[7]] };

    // Shade blend in SIMD
    uint32x4_t r0 = shade_blend4(t0, shade);
    uint32x4_t r1 = shade_blend4(t1, shade);

    // Store results directly (no transparency masking for opaque rendering)
    vst1q_u32(&pixel_buffer[offset], r0);
    vst1q_u32(&pixel_buffer[offset + 4], r1);
}
#else

static inline void
raster_linear_transparent_blend_lerp8(
    uint32_t* pixel_buffer,
    int offset,
    const uint32_t* texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade)
{
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & 0x3f80;
        int texel = texels[u + (v)];
        if( texel != 0 )
            pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}

static inline void
raster_linear_opaque_blend_lerp8(
    uint32_t* pixel_buffer,
    int offset,
    const uint32_t* texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade)
{
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & 0x3f80;
        int texel = texels[u + (v)];
        pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}
#endif

#endif