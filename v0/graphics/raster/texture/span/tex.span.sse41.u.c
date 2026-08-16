#include "graphics/dash_restrict.h"
#include "graphics/shade.h"

#include <assert.h>
#include <smmintrin.h>
#include <stdint.h>

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
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
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
    r0 = _mm_blendv_epi8(r0, existing0, mask0);
    r1 = _mm_blendv_epi8(r1, existing1, mask1);

    // Store results
    _mm_storeu_si128((__m128i*)&pixel_buffer[offset], r0);
    _mm_storeu_si128((__m128i*)&pixel_buffer[offset + 4], r1);
}

static inline void
raster_linear_transparent_texshadeflat_lerp8(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade)
{
    raster_linear_transparent_blend_lerp8(
        pixel_buffer,
        offset,
        texels,
        u_scan,
        v_scan,
        step_u,
        step_v,
        texture_shift,
        shade);
}

static inline void
raster_linear_opaque_blend_lerp8(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
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

static inline void
raster_linear_opaque_texshadeflat_lerp8(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int shade)
{
    raster_linear_opaque_blend_lerp8(
        pixel_buffer,
        offset,
        texels,
        u_scan,
        v_scan,
        step_u,
        step_v,
        texture_shift,
        shade);
}

static inline void
draw_texture_scanline_opaque_blend_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
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
    int* RESTRICT texels,
    int texture_width)
{
    (void)stride;
    (void)screen_height;
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

    int screen_x0 = (screen_x0_ish16 - 1) >> 16;
    int screen_x1 = screen_x1_ish16 >> 16;

    if( screen_x0 < 0 )
        screen_x0 = 0;
    if( screen_x1 >= screen_width )
        screen_x1 = screen_width - 1;

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += (step_au_dx)*adjust;
    bv += (step_bv_dx)*adjust;
    cw += (step_cw_dx)*adjust;

    step_au_dx <<= 3;
    step_bv_dx <<= 3;
    step_cw_dx <<= 3;

    shade8bit_ish8 += step_shade8bit_dx_ish8 * screen_x0;

    steps = screen_x1 - screen_x0;

    offset += screen_x0;

    int texture_shift = (texture_width & 0x80) ? 7 : 6;
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;

    int curr_u;
    int curr_v;
    int next_u;
    int next_v;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;
    int lerp8_shade_step = step_shade8bit_dx_ish8 << 3;
    int shade;
    while( lerp8_steps-- > 0 )
    {
        int w = (cw) >> texture_shift;
        if( w == 0 )
            continue;

        curr_u = (au) / w;
        curr_u = clamp(curr_u, 0, texture_width - 1);
        curr_v = (bv) / w;

        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;

        w = (cw) >> texture_shift;
        if( w == 0 )
            continue;

        next_u = (au) / w;
        next_u = clamp(next_u, 0x0, texture_width - 1);
        next_v = (bv) / w;

        int step_u = (next_u - curr_u) << (texture_shift - 3);
        int step_v = (next_v - curr_v) << (texture_shift - 3);

        int u_scan = curr_u << texture_shift;
        int v_scan = curr_v << texture_shift;

        shade = shade8bit_ish8 >> 8;

        raster_linear_opaque_blend_lerp8(
            (uint32_t*)pixel_buffer,
            offset,
            (uint32_t*)texels,
            u_scan,
            v_scan,
            step_u,
            step_v,
            texture_shift,
            shade);
        u_scan += step_u;
        v_scan += step_v;
        offset += 8;

        shade8bit_ish8 += lerp8_shade_step;
    }

    if( lerp8_last_steps == 0 )
        return;

    int w = (cw) >> texture_shift;
    if( w == 0 )
        return;

    curr_u = (au) / w;
    curr_u = clamp(curr_u, 0, texture_width - 1);
    curr_v = (bv) / w;

    au += step_au_dx;
    bv += step_bv_dx;
    cw += step_cw_dx;

    w = (cw) >> texture_shift;
    if( w == 0 )
        return;

    next_u = (au) / w;
    next_u = clamp(next_u, 0x0, texture_width - 1);
    next_v = (bv) / w;

    int step_u = (next_u - curr_u) << (texture_shift - 3);
    int step_v = (next_v - curr_v) << (texture_shift - 3);

    int u_scan = curr_u << texture_shift;
    int v_scan = curr_v << texture_shift;

    shade = shade8bit_ish8 >> 8;
    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        int texel = texels[u + v];
        pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}

static inline void
draw_texture_scanline_transparent_blend_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
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
    int* RESTRICT texels,
    int texture_width)
{
    (void)stride;
    (void)screen_height;
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

    int screen_x0 = (screen_x0_ish16 - 1) >> 16;
    int screen_x1 = screen_x1_ish16 >> 16;

    if( screen_x0 < 0 )
        screen_x0 = 0;
    if( screen_x1 >= screen_width )
        screen_x1 = screen_width - 1;

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += (step_au_dx)*adjust;
    bv += (step_bv_dx)*adjust;
    cw += (step_cw_dx)*adjust;

    step_au_dx <<= 3;
    step_bv_dx <<= 3;
    step_cw_dx <<= 3;

    shade8bit_ish8 += step_shade8bit_dx_ish8 * screen_x0;

    steps = screen_x1 - screen_x0;

    offset += screen_x0;

    int texture_shift = (texture_width & 0x80) ? 7 : 6;
    int mask = texture_shift == 7 ? 0x3f80 : 0x0fc0;

    int curr_u;
    int curr_v;
    int next_u;
    int next_v;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;
    int lerp8_shade_step = step_shade8bit_dx_ish8 << 3;
    int shade;
    while( lerp8_steps-- > 0 )
    {
        int w = (cw) >> texture_shift;
        if( w == 0 )
            continue;

        curr_u = (au) / w;
        curr_u = clamp(curr_u, 0, texture_width - 1);
        curr_v = (bv) / w;

        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;

        w = (cw) >> texture_shift;
        if( w == 0 )
            continue;

        next_u = (au) / w;
        next_u = clamp(next_u, 0x0, texture_width - 1);
        next_v = (bv) / w;

        int step_u = (next_u - curr_u) << (texture_shift - 3);
        int step_v = (next_v - curr_v) << (texture_shift - 3);

        int u_scan = curr_u << texture_shift;
        int v_scan = curr_v << texture_shift;

        shade = shade8bit_ish8 >> 8;

        raster_linear_transparent_blend_lerp8(
            (uint32_t*)pixel_buffer,
            offset,
            (uint32_t*)texels,
            u_scan,
            v_scan,
            step_u,
            step_v,
            texture_shift,
            shade);
        u_scan += step_u;
        v_scan += step_v;
        offset += 8;

        shade8bit_ish8 += lerp8_shade_step;
    }

    if( lerp8_last_steps == 0 )
        return;

    int w = (cw) >> texture_shift;
    if( w == 0 )
        return;

    curr_u = (au) / w;
    curr_u = clamp(curr_u, 0, texture_width - 1);
    curr_v = (bv) / w;

    au += step_au_dx;
    bv += step_bv_dx;
    cw += step_cw_dx;

    w = (cw) >> texture_shift;
    if( w == 0 )
        return;

    next_u = (au) / w;
    next_u = clamp(next_u, 0x0, texture_width - 1);
    next_v = (bv) / w;

    int step_u = (next_u - curr_u) << (texture_shift - 3);
    int step_v = (next_v - curr_v) << (texture_shift - 3);

    int u_scan = curr_u << texture_shift;
    int v_scan = curr_v << texture_shift;

    shade = shade8bit_ish8 >> 8;
    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & mask;
        int texel = texels[u + v];
        if( texel != 0 )
            pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
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
    int idx[8];
    assert(texture_shift == 7 || texture_shift == 6);
    for( int i = 0; i < 8; i++ )
    {
        int u = (u_scan >> texture_shift) & u_mask;
        int v = v_scan & v_mask;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    __m128i t0 = _mm_set_epi32(texels[idx[3]], texels[idx[2]], texels[idx[1]], texels[idx[0]]);
    __m128i t1 = _mm_set_epi32(texels[idx[7]], texels[idx[6]], texels[idx[5]], texels[idx[4]]);
    __m128i r0 = shade_blend4_sse(t0, shade);
    __m128i r1 = shade_blend4_sse(t1, shade);

    _mm_storeu_si128((__m128i*)pixel_buffer, r0);
    _mm_storeu_si128((__m128i*)(pixel_buffer + 4), r1);
}

static inline void
raster_linear_transparent_blend_lerp8_v3(
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
    int idx[8];
    assert(texture_shift == 7 || texture_shift == 6);
    for( int i = 0; i < 8; i++ )
    {
        int u = (u_scan >> texture_shift) & u_mask;
        int v = v_scan & v_mask;
        idx[i] = u + v;
        u_scan += step_u;
        v_scan += step_v;
    }

    __m128i t0 = _mm_set_epi32(texels[idx[3]], texels[idx[2]], texels[idx[1]], texels[idx[0]]);
    __m128i t1 = _mm_set_epi32(texels[idx[7]], texels[idx[6]], texels[idx[5]], texels[idx[4]]);
    __m128i r0 = shade_blend4_sse(t0, shade);
    __m128i r1 = shade_blend4_sse(t1, shade);

    __m128i zero = _mm_setzero_si128();
    __m128i ex0 = _mm_loadu_si128((__m128i*)pixel_buffer);
    __m128i ex1 = _mm_loadu_si128((__m128i*)(pixel_buffer + 4));
    __m128i bm0 = _mm_cmpeq_epi32(t0, zero);
    __m128i bm1 = _mm_cmpeq_epi32(t1, zero);
    r0 = _mm_blendv_epi8(r0, ex0, bm0);
    r1 = _mm_blendv_epi8(r1, ex1, bm1);

    _mm_storeu_si128((__m128i*)pixel_buffer, r0);
    _mm_storeu_si128((__m128i*)(pixel_buffer + 4), r1);
}

static inline void
draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered(
    int* RESTRICT pixel_buffer,
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
    int* RESTRICT texels,
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
        if( (cw >> texture_shift) != 0 )
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

    if( remaining > 0 && (cw >> texture_shift) != 0 )
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

static inline void
draw_texture_scanline_transparent_blend_branching_lerp8_v3_ordered(
    int* RESTRICT pixel_buffer,
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
    int* RESTRICT texels,
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
        if( (cw >> texture_shift) != 0 )
        {
            CALC_BLOCK_PARAMS(cw, au, bv);
            cur_u = clamp(cur_u, 0, texture_width - 1);
            nxt_u = clamp(nxt_u, 0, texture_width - 1);
            s_u = (nxt_u - cur_u) << (texture_shift - 3);
            raster_linear_transparent_blend_lerp8_v3(
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

    if( remaining > 0 && (cw >> texture_shift) != 0 )
    {
        CALC_BLOCK_PARAMS(cw, au, bv);
        cur_u = clamp(cur_u, 0, texture_width - 1);
        nxt_u = clamp(nxt_u, 0, texture_width - 1);
        s_u = (nxt_u - cur_u) << (texture_shift - 3);
        int u_scan = cur_u << texture_shift;
        int v_scan = cur_v << texture_shift;
        int shade = shade8bit_ish8 >> 8;

        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            int t = texels[u + v];
            if( t != 0 )
                pixel_buffer[offset] = shade_blend(t, shade);
            offset++;
            u_scan += s_u;
            v_scan += s_v;
        }
    }
#undef CALC_BLOCK_PARAMS
}

static inline void
draw_texture_scanline_opaque_blend_affine_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x_start,
    int x_end,
    int pixel_offset,
    int shade_ish8,
    int shade_step_ish8,
    int u,
    int v,
    int w,
    int step_u_dx,
    int step_v_dx,
    int step_w_dx,
    int* RESTRICT texels,
    int origin_x)
{
    if( x_end > screen_width )
    {
        x_end = screen_width;
    }
    if( x_start < 0 )
    {
        x_start = 0;
    }

    if( x_start >= x_end )
    {
        return;
    }

    int offset = pixel_offset + x_start;
    int shade_accum = x_start * shade_step_ish8 + shade_ish8;
    int width = x_end - x_start;
    int dx = x_start - origin_x;
    int u_start = step_u_dx * dx + u;
    int v_start = step_v_dx * dx + v;
    int w_start = step_w_dx * dx + w;

    int w_div = w_start >> 14;
    int u_coord = 0;
    int v_coord = 0;
    if( w_div == 0 )
    {
        u_coord = 0;
        v_coord = 0;
    }
    else
    {
        u_coord = u_start / w_div;
        v_coord = v_start / w_div;
    }

    int u_end = step_u_dx * width + u_start;
    int v_end = step_v_dx * width + v_start;
    int w_end = step_w_dx * width + w_start;

    int w_div_end = w_end >> 14;
    int u_coord_end = 0;
    int v_coord_end = 0;
    if( w_div_end == 0 )
    {
        u_coord_end = 0;
        v_coord_end = 0;
    }
    else
    {
        u_coord_end = u_end / w_div_end;
        v_coord_end = v_end / w_div_end;
    }

    int uv_packed = (u_coord << 18) + v_coord;
    int uv_step = ((u_coord_end - u_coord) / width << 18) + (v_coord_end - v_coord) / width;

    int steps_8 = width >> 3;
    int shade_step_8 = shade_step_ish8 << 3;
    int shade = shade_accum >> 8;

    if( steps_8 > 0 )
    {
        do
        {
            int texel = texels[((uint32_t)uv_packed >> 25) + (uv_packed & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            int uv_next = uv_packed + uv_step;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_packed = uv_step + uv_next;
            shade_accum += shade_step_8;
            shade = shade_accum >> 8;
            steps_8--;
        } while( steps_8 > 0 );
    }

    int remaining = (x_end - x_start) & 0x7;
    if( remaining > 0 )
    {
        do
        {
            int texel = texels[((uint32_t)uv_packed >> 25) + (uv_packed & 0x3F80)];
            pixel_buffer[offset++] = shade_blend(texel, shade);
            uv_packed += uv_step;
            remaining--;
        } while( remaining > 0 );
    }
}

static inline void
draw_texture_scanline_transparent_blend_affine_branching_lerp8_ordered(
    int* RESTRICT pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x_start,
    int x_end,
    int pixel_offset,
    int shade_ish8,
    int shade_step_ish8,
    int u,
    int v,
    int w,
    int step_u_dx,
    int step_v_dx,
    int step_w_dx,
    int* RESTRICT texels,
    int origin_x)
{
    if( x_end > screen_width )
    {
        x_end = screen_width;
    }
    if( x_start < 0 )
    {
        x_start = 0;
    }

    if( x_start >= x_end )
    {
        return;
    }

    int offset = pixel_offset + x_start;
    int shade_accum = x_start * shade_step_ish8 + shade_ish8;
    int width = x_end - x_start;
    int dx = x_start - origin_x;
    int u_start = step_u_dx * dx + u;
    int v_start = step_v_dx * dx + v;
    int w_start = step_w_dx * dx + w;

    int w_div = w_start >> 14;
    int u_coord = 0;
    int v_coord = 0;
    if( w_div == 0 )
    {
        u_coord = 0;
        v_coord = 0;
    }
    else
    {
        u_coord = u_start / w_div;
        v_coord = v_start / w_div;
    }

    int u_end = step_u_dx * width + u_start;
    int v_end = step_v_dx * width + v_start;
    int w_end = step_w_dx * width + w_start;

    int w_div_end = w_end >> 14;
    int u_coord_end = 0;
    int v_coord_end = 0;
    if( w_div_end == 0 )
    {
        u_coord_end = 0;
        v_coord_end = 0;
    }
    else
    {
        u_coord_end = u_end / w_div_end;
        v_coord_end = v_end / w_div_end;
    }

    int uv_packed = (u_coord << 18) + v_coord;
    int uv_step = ((u_coord_end - u_coord) / width << 18) + (v_coord_end - v_coord) / width;

    int steps_8 = width >> 3;
    int shade_step_8 = shade_step_ish8 << 3;
    int shade = shade_accum >> 8;

    if( steps_8 > 0 )
    {
        do
        {
            int texel = texels[((uint32_t)uv_packed >> 25) + (uv_packed & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;
            int uv_next = uv_packed + uv_step;

            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_next = uv_step + uv_next;
            texel = texels[((uint32_t)uv_next >> 25) + (uv_next & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;

            uv_packed = uv_step + uv_next;
            shade_accum += shade_step_8;
            shade = shade_accum >> 8;
            steps_8--;
        } while( steps_8 > 0 );
    }

    int remaining = (x_end - x_start) & 0x7;
    if( remaining > 0 )
    {
        do
        {
            int texel = texels[((uint32_t)uv_packed >> 25) + (uv_packed & 0x3F80)];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);
            offset += 1;
            uv_packed += uv_step;
            remaining--;
        } while( remaining > 0 );
    }
}


static inline void
draw_texture_scanline_opaque_blend_affine_branching_lerp8_ish16_ordered(
    int* RESTRICT pixel_buffer,
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
    int* RESTRICT texels,
    int texture_width)
{
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

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

    int offset = pixel_offset + x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * x0;

    int width = x1 - x0;

    int texture_shift = (texture_width == 128) ? 7 : 6;
    int v_mask = (texture_width == 128) ? 0x3F80 : 0x0FC0;
    int u_mask = texture_width - 1;

    int w_div = cw >> texture_shift;
    int u_coord = 0;
    int v_coord = 0;
    if( w_div != 0 )
    {
        u_coord = au / w_div;
        v_coord = bv / w_div;
    }
    u_coord = clamp(u_coord, 0, texture_width - 1);

    int au_end = au + step_au_dx * width;
    int bv_end = bv + step_bv_dx * width;
    int cw_end = cw + step_cw_dx * width;
    int w_div_end = cw_end >> texture_shift;
    int u_coord_end = 0;
    int v_coord_end = 0;
    if( w_div_end != 0 )
    {
        u_coord_end = au_end / w_div_end;
        v_coord_end = bv_end / w_div_end;
    }
    u_coord_end = clamp(u_coord_end, 0, texture_width - 1);

    int u_scan = u_coord << texture_shift;
    int v_scan = v_coord << texture_shift;
    int step_u = ((u_coord_end - u_coord) << texture_shift) / width;
    int step_v = ((v_coord_end - v_coord) << texture_shift) / width;

    int blocks = width >> 3;
    int remaining = width & 7;

    while( blocks-- )
    {
        raster_linear_opaque_blend_lerp8_v3(
            (uint32_t*)&pixel_buffer[offset],
            (uint32_t*)texels,
            u_scan,
            v_scan,
            step_u,
            step_v,
            texture_shift,
            u_mask,
            v_mask,
            shade8bit_ish8 >> 8);
        u_scan += step_u * 8;
        v_scan += step_v * 8;
        offset += 8;
        shade8bit_ish8 += (step_shade8bit_dx_ish8 << 3);
    }

    if( remaining > 0 )
    {
        int shade = shade8bit_ish8 >> 8;
        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            pixel_buffer[offset++] = shade_blend(texels[u + v], shade);
            u_scan += step_u;
            v_scan += step_v;
        }
    }
}

static inline void
draw_texture_scanline_transparent_blend_affine_branching_lerp8_ish16_ordered(
    int* RESTRICT pixel_buffer,
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
    int* RESTRICT texels,
    int texture_width)
{
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

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

    int offset = pixel_offset + x0;
    shade8bit_ish8 += step_shade8bit_dx_ish8 * x0;

    int width = x1 - x0;

    int texture_shift = (texture_width == 128) ? 7 : 6;
    int v_mask = (texture_width == 128) ? 0x3F80 : 0x0FC0;
    int u_mask = texture_width - 1;

    int w_div = cw >> texture_shift;
    int u_coord = 0;
    int v_coord = 0;
    if( w_div != 0 )
    {
        u_coord = au / w_div;
        v_coord = bv / w_div;
    }
    u_coord = clamp(u_coord, 0, texture_width - 1);

    int au_end = au + step_au_dx * width;
    int bv_end = bv + step_bv_dx * width;
    int cw_end = cw + step_cw_dx * width;
    int w_div_end = cw_end >> texture_shift;
    int u_coord_end = 0;
    int v_coord_end = 0;
    if( w_div_end != 0 )
    {
        u_coord_end = au_end / w_div_end;
        v_coord_end = bv_end / w_div_end;
    }
    u_coord_end = clamp(u_coord_end, 0, texture_width - 1);

    int u_scan = u_coord << texture_shift;
    int v_scan = v_coord << texture_shift;
    int step_u = ((u_coord_end - u_coord) << texture_shift) / width;
    int step_v = ((v_coord_end - v_coord) << texture_shift) / width;

    int blocks = width >> 3;
    int remaining = width & 7;

    while( blocks-- )
    {
        raster_linear_transparent_blend_lerp8_v3(
            (uint32_t*)&pixel_buffer[offset],
            (uint32_t*)texels,
            u_scan,
            v_scan,
            step_u,
            step_v,
            texture_shift,
            u_mask,
            v_mask,
            shade8bit_ish8 >> 8);
        u_scan += step_u * 8;
        v_scan += step_v * 8;
        offset += 8;
        shade8bit_ish8 += (step_shade8bit_dx_ish8 << 3);
    }

    if( remaining > 0 )
    {
        int shade = shade8bit_ish8 >> 8;
        for( int i = 0; i < remaining; i++ )
        {
            int u = (u_scan >> texture_shift) & u_mask;
            int v = v_scan & v_mask;
            int t = texels[u + v];
            if( t != 0 )
                pixel_buffer[offset] = shade_blend(t, shade);
            offset++;
            u_scan += step_u;
            v_scan += step_v;
        }
    }
}
