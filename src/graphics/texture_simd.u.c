#ifndef TEXTURE_SIMD_U_C
#define TEXTURE_SIMD_U_C

#include <stdint.h>

// clang-format off
#include "shade.h"
// clang-format on

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include <arm_neon.h>

// shade_blend for 4 pixels at a time
static inline uint32x4_t
shade_blend4_neon(
    uint32x4_t texel,
    int shade)
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
    assert(texture_shift == 7 || texture_shift == 6);
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather)
    uint32x4_t t0 = { texels[idx[0]], texels[idx[1]], texels[idx[2]], texels[idx[3]] };
    uint32x4_t t1 = { texels[idx[4]], texels[idx[5]], texels[idx[6]], texels[idx[7]] };

    // Shade blend in SIMD
    uint32x4_t r0 = shade_blend4_neon(t0, shade);
    uint32x4_t r1 = shade_blend4_neon(t1, shade);

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
    assert(texture_shift == 7 || texture_shift == 6);
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather)
    uint32x4_t t0 = { texels[idx[0]], texels[idx[1]], texels[idx[2]], texels[idx[3]] };
    uint32x4_t t1 = { texels[idx[4]], texels[idx[5]], texels[idx[6]], texels[idx[7]] };

    // Shade blend in SIMD
    uint32x4_t r0 = shade_blend4_neon(t0, shade);
    uint32x4_t r1 = shade_blend4_neon(t1, shade);

    // Store results directly (no transparency masking for opaque rendering)
    vst1q_u32(&pixel_buffer[offset], r0);
    vst1q_u32(&pixel_buffer[offset + 4], r1);
}

static inline void
raster_linear_opaque_blend_lerp8_v3(
    uint32_t* __restrict pixel_buffer,
    const uint32_t* __restrict texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int u_mask,
    int v_mask,
    int shade)
{
    uint32x4_t t0, t1;

// We mask U and V at every pixel to prevent texture bleeding/crashes
#define GET_TEXEL_ADDR &texels[((u_scan >> texture_shift) & u_mask) + (v_scan & v_mask)]

    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 0);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 1);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 2);
    u_scan += step_u;
    v_scan += step_v;
    t0 = vld1q_lane_u32(GET_TEXEL_ADDR, t0, 3);
    u_scan += step_u;
    v_scan += step_v;

    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 0);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 1);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 2);
    u_scan += step_u;
    v_scan += step_v;
    t1 = vld1q_lane_u32(GET_TEXEL_ADDR, t1, 3);

    uint32x4_t r0 = shade_blend4_neon(t0, shade);
    uint32x4_t r1 = shade_blend4_neon(t1, shade);

    vst1q_u32(pixel_buffer, r0);
    vst1q_u32(pixel_buffer + 4, r1);
#undef GET_TEXEL_ADDR
}

static inline void
draw_texture_scanline_opaque_blend_ordered_blerp8_v3(
    int* pixel_buffer,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* texels,
    int texture_width)
{
    int x0 = (screen_x0_ish16 - 1) >> 16;
    if( x0 < 0 )
        x0 = 0;
    int x1 = screen_x1_ish16 >> 16;
    if( x1 >= screen_width )
        x1 = screen_width - 1;
    if( x0 >= x1 )
        return;

    int adjust = x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    int texture_shift = (texture_width == 128) ? 7 : 6;
    int v_mask = (texture_width == 128) ? 0x3F80 : 0x0FC0;
    int u_mask = texture_width - 1;

    int steps = x1 - x0;
    int offset = pixel_offset + x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * x0;

    int blocks = steps >> 3;
    int remaining = steps & 7;

// Re-useable UV calculation logic
#define CALC_BLOCK_PARAMS(W_VAL, AU_VAL, BV_VAL)                                                   \
    float inv_w = 1.0f / (float)((W_VAL) >> texture_shift);                                        \
    int cur_u = (int)((AU_VAL) * inv_w);                                                           \
    int cur_v = (int)((BV_VAL) * inv_w);                                                           \
    float inv_w_n = 1.0f / (float)(((W_VAL) + (step_cw_dx << 3)) >> texture_shift);                \
    int nxt_u = (int)(((AU_VAL) + (step_au_dx << 3)) * inv_w_n);                                   \
    int nxt_v = (int)(((BV_VAL) + (step_bv_dx << 3)) * inv_w_n);                                   \
    int s_u = (nxt_u - cur_u) << (texture_shift - 3);                                              \
    int s_v = (nxt_v - cur_v) << (texture_shift - 3);

    while( blocks-- )
    {
        if( cw > 0 )
        {
            CALC_BLOCK_PARAMS(cw, au, bv);
            raster_linear_opaque_blend_lerp8_v3(
                (uint32_t*)&pixel_buffer[offset],
                (uint32_t*)texels,
                cur_u << texture_shift,
                cur_v << texture_shift,
                s_u,
                s_v,
                texture_shift,
                u_mask,
                v_mask,
                shade8bit_ish8 >> 8);
        }
        au += (step_au_dx << 3);
        bv += (step_bv_dx << 3);
        cw += (step_cw_dx << 3);
        offset += 8;
        shade8bit_ish8 += (step_shade8bit_dx_ish8 << 3);
    }

    // --- FIX: Proper Tail Cleanup ---
    if( remaining > 0 && cw > 0 )
    {
        CALC_BLOCK_PARAMS(cw, au, bv);
        int u_scan = cur_u << texture_shift;
        int v_scan = cur_v << texture_shift;
        int shade = shade8bit_ish8 >> 8;

        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            pixel_buffer[offset++] = shade_blend(texels[u + v], shade);
            u_scan += s_u;
            v_scan += s_v;
        }
    }
#undef CALC_BLOCK_PARAMS
}

#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include <immintrin.h>

// shade_blend for 8 pixels at a time using AVX2
static inline __m256i
shade_blend8_avx2(
    __m256i texel,
    int shade)
{
    // Expand 8-bit channels to 16-bit (similar to NEON vmovl_u8)
    __m256i texel_lo = _mm256_unpacklo_epi8(texel, _mm256_setzero_si256());
    __m256i texel_hi = _mm256_unpackhi_epi8(texel, _mm256_setzero_si256());

    // Multiply by shade (similar to NEON vmulq_n_u16)
    __m256i shade_16 = _mm256_set1_epi16(shade);
    texel_lo = _mm256_mullo_epi16(texel_lo, shade_16);
    texel_hi = _mm256_mullo_epi16(texel_hi, shade_16);

    // >> 8 (same as scalar shade_blend and NEON vshrq_n_u16)
    texel_lo = _mm256_srli_epi16(texel_lo, 8);
    texel_hi = _mm256_srli_epi16(texel_hi, 8);

    // Pack back to 8-bit (similar to NEON vqmovn_u16)
    __m256i shaded = _mm256_packus_epi16(texel_lo, texel_hi);

    return shaded;
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
    assert(texture_shift == 7 || texture_shift == 6);
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather)
    __m256i t = _mm256_set_epi32(
        texels[idx[7]],
        texels[idx[6]],
        texels[idx[5]],
        texels[idx[4]],
        texels[idx[3]],
        texels[idx[2]],
        texels[idx[1]],
        texels[idx[0]]);

    // Shade blend in SIMD
    __m256i r = shade_blend8_avx2(t, shade);

    // Handle transparency: preserve existing pixel buffer where texel is 0
    __m256i zero = _mm256_setzero_si256();
    __m256i existing = _mm256_loadu_si256((__m256i*)&pixel_buffer[offset]);

    // Create mask for non-zero texels (true where texel is 0)
    __m256i mask = _mm256_cmpeq_epi32(t, zero);

    // Select existing pixels where texel is 0, shaded result where texel is not 0
    r = _mm256_blendv_epi8(r, existing, mask);

    // Store results
    _mm256_storeu_si256((__m256i*)&pixel_buffer[offset], r);
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
    assert(texture_shift == 7 || texture_shift == 6);
    int vm = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & vm;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather)
    __m256i t = _mm256_set_epi32(
        texels[idx[7]],
        texels[idx[6]],
        texels[idx[5]],
        texels[idx[4]],
        texels[idx[3]],
        texels[idx[2]],
        texels[idx[1]],
        texels[idx[0]]);

    // Shade blend in SIMD
    __m256i r = shade_blend8_avx2(t, shade);

    // Store results directly (no transparency masking for opaque rendering)
    _mm256_storeu_si256((__m256i*)&pixel_buffer[offset], r);
}
#elif (defined(__SSE2__) || defined(__SSE4_1__)) && !defined(SSE2_DISABLED)
#include <emmintrin.h>

// shade_blend for 4 pixels at a time using SSE2
static inline __m128i
shade_blend4_sse(
    __m128i texel,
    int shade)
{
    // Expand 8-bit channels to 16-bit (similar to NEON vmovl_u8)
    __m128i texel_lo = _mm_unpacklo_epi8(texel, _mm_setzero_si128());
    __m128i texel_hi = _mm_unpackhi_epi8(texel, _mm_setzero_si128());

    // Multiply by shade (similar to NEON vmulq_n_u16)
    __m128i shade_16 = _mm_set1_epi16(shade);
    texel_lo = _mm_mullo_epi16(texel_lo, shade_16);
    texel_hi = _mm_mullo_epi16(texel_hi, shade_16);

    // >> 8 (same as scalar shade_blend and NEON vshrq_n_u16)
    texel_lo = _mm_srli_epi16(texel_lo, 8);
    texel_hi = _mm_srli_epi16(texel_hi, 8);

    // Pack back to 8-bit (similar to NEON vqmovn_u16)
    __m128i shaded = _mm_packus_epi16(texel_lo, texel_hi);

    return shaded;
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
    assert(texture_shift == 7 || texture_shift == 6);
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather) - process in two groups of 4
    __m128i t0 = _mm_set_epi32(texels[idx[3]], texels[idx[2]], texels[idx[1]], texels[idx[0]]);
    __m128i t1 = _mm_set_epi32(texels[idx[7]], texels[idx[6]], texels[idx[5]], texels[idx[4]]);

    // Shade blend in SIMD
    __m128i r0 = shade_blend4_sse(t0, shade);
    __m128i r1 = shade_blend4_sse(t1, shade);

    // Handle transparency: preserve existing pixel buffer where texel is 0
    __m128i zero = _mm_setzero_si128();
    __m128i existing0 = _mm_loadu_si128((__m128i*)&pixel_buffer[offset]);
    __m128i existing1 = _mm_loadu_si128((__m128i*)&pixel_buffer[offset + 4]);

    // Create masks for non-zero texels (true where texel is 0)
    __m128i mask0 = _mm_cmpeq_epi32(t0, zero);
    __m128i mask1 = _mm_cmpeq_epi32(t1, zero);

    // Select existing pixels where texel is 0, shaded result where texel is not 0
    // Use SSE2-compatible conditional selection
    r0 = _mm_or_si128(_mm_and_si128(mask0, existing0), _mm_andnot_si128(mask0, r0));
    r1 = _mm_or_si128(_mm_and_si128(mask1, existing1), _mm_andnot_si128(mask1, r1));

    // Store results
    _mm_storeu_si128((__m128i*)&pixel_buffer[offset], r0);
    _mm_storeu_si128((__m128i*)&pixel_buffer[offset + 4], r1);
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
    assert(texture_shift == 7 || texture_shift == 6);
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    // Load 8 texels (scalar gather) - process in two groups of 4
    __m128i t0 = _mm_set_epi32(texels[idx[3]], texels[idx[2]], texels[idx[1]], texels[idx[0]]);
    __m128i t1 = _mm_set_epi32(texels[idx[7]], texels[idx[6]], texels[idx[5]], texels[idx[4]]);

    // Shade blend in SIMD
    __m128i r0 = shade_blend4_sse(t0, shade);
    __m128i r1 = shade_blend4_sse(t1, shade);

    // Store results directly (no transparency masking for opaque rendering)
    _mm_storeu_si128((__m128i*)&pixel_buffer[offset], r0);
    _mm_storeu_si128((__m128i*)&pixel_buffer[offset + 4], r1);
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
    assert(texture_shift == 7 || texture_shift == 6);
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
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
    assert(texture_shift == 7 || texture_shift == 6);
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        int texel = texels[u + (v)];
        pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}
#endif

#endif