#ifndef TEXTURE_SIMD_U_C
#define TEXTURE_SIMD_U_C

#include <stdint.h>

// clang-format off
#include "shade.h"
// clang-format on

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>

// shade_blend for 4 pixels at a time
static inline uint32x4_t
shade_blend4_neon(uint32x4_t texel, int shade)
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
    uint32x4_t r0 = shade_blend4_neon(t0, shade);
    uint32x4_t r1 = shade_blend4_neon(t1, shade);

    // Store results directly (no transparency masking for opaque rendering)
    vst1q_u32(&pixel_buffer[offset], r0);
    vst1q_u32(&pixel_buffer[offset + 4], r1);
}
#elif defined(__AVX2__) && defined(__AVX22__)
#include <immintrin.h>

// shade_blend for 8 pixels at a time using AVX2
static inline __m256i
shade_blend8(__m256i texel, int shade)
{
    // Convert shade to vector
    __m256i shade_vec = _mm256_set1_epi32(shade);

    // Extract RGB components using masks
    __m256i rb_mask = _mm256_set1_epi32(0x00ff00ff);
    __m256i g_mask = _mm256_set1_epi32(0x0000ff00);

    __m256i rb = _mm256_and_si256(texel, rb_mask);
    __m256i g = _mm256_and_si256(texel, g_mask);

    // Multiply by shade
    rb = _mm256_mullo_epi32(rb, shade_vec);
    g = _mm256_mullo_epi32(g, shade_vec);

    // Apply masks to prevent overflow
    __m256i rb_result_mask = _mm256_set1_epi32(0xFF00FF00);
    __m256i g_result_mask = _mm256_set1_epi32(0x00FF0000);

    rb = _mm256_and_si256(rb, rb_result_mask);
    g = _mm256_and_si256(g, g_result_mask);

    // Combine and shift right by 8
    __m256i result = _mm256_or_si256(rb, g);
    return _mm256_srli_epi32(result, 8);
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
    __m256i r = shade_blend8(t, shade);

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
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & 0x3f80;
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
    __m256i r = shade_blend8(t, shade);

    // Store results directly (no transparency masking for opaque rendering)
    _mm256_storeu_si256((__m256i*)&pixel_buffer[offset], r);
}
#elif defined(__SSE2__)
#include <emmintrin.h>

// shade_blend for 4 pixels at a time using SSE2
static inline __m128i
shade_blend4_sse(__m128i texel, int shade)
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
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & 0x3f80;
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
    for( int i = 0; i < 8; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & 0x3f80;
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