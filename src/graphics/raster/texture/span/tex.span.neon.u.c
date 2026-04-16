#include "graphics/dash_restrict.h"
#include "graphics/shade.h"

#include <arm_neon.h>
#include <assert.h>
#include <stdint.h>

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
draw_texture_scanline_opaque_blend_ordered_branching_lerp8(
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
draw_texture_scanline_transparent_blend_ordered_branching_lerp8(
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
    uint32x4_t t0, t1;

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

    uint32x4_t zero = vdupq_n_u32(0);
    uint32x4_t existing0 = vld1q_u32(pixel_buffer);
    uint32x4_t existing1 = vld1q_u32(pixel_buffer + 4);
    uint32x4_t m0 = vceqq_u32(t0, zero);
    uint32x4_t m1 = vceqq_u32(t1, zero);
    r0 = vbslq_u32(m0, existing0, r0);
    r1 = vbslq_u32(m1, existing1, r1);

    vst1q_u32(pixel_buffer, r0);
    vst1q_u32(pixel_buffer + 4, r1);
#undef GET_TEXEL_ADDR
}

static inline void
draw_texture_scanline_opaque_blend_ordered_branching_lerp8_v3(
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

    // --- FIX: Proper Tail Cleanup ---
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
draw_texture_scanline_transparent_blend_ordered_branching_lerp8_v3(
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
draw_texture_scanline_opaque_blend_affine_branching_lerp8_ordered_ish16(
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
draw_texture_scanline_transparent_blend_affine_branching_lerp8_ordered_ish16(
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
