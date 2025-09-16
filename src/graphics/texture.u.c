#ifndef TEXTURE_U_C
#define TEXTURE_U_C

#define SWAP(a, b)                                                                                 \
    {                                                                                              \
        int tmp = a;                                                                               \
        a = b;                                                                                     \
        b = tmp;                                                                                   \
    }

#include "shade.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// clang-format off
#include "projection.u.c"
// clang-format on

#define SCREEN_HEIGHT 600
#define SCREEN_WIDTH 800

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static inline int
clamp(int value, int min, int max)
{
    if( value < min )
        return min;
    if( value > max )
        return max;
    return value;
}

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

static inline void
raster_texture_scanline_transparent_blend_lerp8(
    int* pixel_buffer,
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
    int* texels,
    int texture_width)
{
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    if( screen_x0_ish16 > screen_x1_ish16 )
    {
        SWAP(screen_x0_ish16, screen_x1_ish16);
    }

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

    int screen_x0 = screen_x0_ish16 >> 16;
    int screen_x1 = screen_x1_ish16 >> 16;

    if( screen_x1 >= screen_width )
    {
        screen_x1 = screen_width - 1;
    }

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    step_au_dx <<= 3;
    step_bv_dx <<= 3;
    step_cw_dx <<= 3;

    shade8bit_ish8 += step_shade8bit_dx_ish8 * screen_x0;

    steps = screen_x1 - screen_x0;

    assert(screen_x0 < screen_width);
    assert(screen_x1 < screen_width);

    assert(screen_x0 <= screen_x1);
    assert(screen_x0 >= 0);
    assert(screen_x1 >= 0);

    offset += screen_x0;

    assert(screen_x0 + steps < screen_width);

    // If texture width is 128 or 64.
    assert(texture_width == 128 || texture_width == 64);
    int texture_shift = (texture_width & 0x80) ? 7 : 6;

    int curr_u = 0;
    int curr_v = 0;
    int next_u = 0;
    int next_v = 0;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;
    int lerp8_shade_step = step_shade8bit_dx_ish8 << 3;
    int shade;
    do
    {
        if( lerp8_steps == 0 )
            break;

        int w = (cw) >> texture_shift;
        if( w == 0 )
            continue;

        curr_u = (au) / w;
        curr_u = clamp(curr_u, 0, texture_width - 1);
        curr_v = (bv) / w;
        // curr_v = clamp(curr_v, 0, texture_width - 1);

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
            pixel_buffer, offset, texels, u_scan, v_scan, step_u, step_v, texture_shift, shade);
        u_scan += step_u;
        v_scan += step_v;
        offset += 8;

        // Checked on 09/15/2025, clang does NOT vectorize this loop.
        // even with -O3
        // for( int i = 0; i < 8; i++ )
        // {
        //     int u = u_scan >> texture_shift;
        //     int v = v_scan & 0x3f80;
        //     int texel = texels[u + (v)];
        //     if( texel != 0 )
        //         pixel_buffer[offset] = shade_blend(texel, shade);

        //     u_scan += step_u;
        //     v_scan += step_v;

        //     offset += 1;
        // }

        shade8bit_ish8 += lerp8_shade_step;

    } while( lerp8_steps-- > 0 );

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
        int v = v_scan & 0x3f80;
        int texel = texels[u + v];
        if( texel != 0 )
            pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }

    // int u;
    // int v;
    // int texel;
    // switch( lerp8_last_steps )
    // {
    // case 7:
    //     u = u_scan >> texture_shift;
    //     v = v_scan & 0x3f80;
    //     texel = texels[u + v];
    //     if( texel != 0 )
    //         pixel_buffer[offset] = shade_blend(texel, shade);
    //     u_scan += step_u;
    //     v_scan += step_v;
    //     offset += 1;
    // case 6:
    //     u = u_scan >> texture_shift;
    //     v = v_scan & 0x3f80;
    //     texel = texels[u + v];
    //     if( texel != 0 )
    //         pixel_buffer[offset] = shade_blend(texel, shade);
    //     u_scan += step_u;
    //     v_scan += step_v;
    //     offset += 1;
    // case 5:
    //     u = u_scan >> texture_shift;
    //     v = v_scan & 0x3f80;
    //     texel = texels[u + v];
    //     if( texel != 0 )
    //         pixel_buffer[offset] = shade_blend(texel, shade);
    //     u_scan += step_u;
    //     v_scan += step_v;
    //     offset += 1;
    // case 4:
    //     u = u_scan >> texture_shift;
    //     v = v_scan & 0x3f80;
    //     texel = texels[u + v];
    //     if( texel != 0 )
    //         pixel_buffer[offset] = shade_blend(texel, shade);
    //     u_scan += step_u;
    //     v_scan += step_v;
    //     offset += 1;
    // case 3:
    //     u = u_scan >> texture_shift;
    //     v = v_scan & 0x3f80;
    //     texel = texels[u + v];
    //     if( texel != 0 )
    //         pixel_buffer[offset] = shade_blend(texel, shade);
    //     u_scan += step_u;
    //     v_scan += step_v;
    //     offset += 1;
    // case 2:
    //     u = u_scan >> texture_shift;
    //     v = v_scan & 0x3f80;
    //     texel = texels[u + v];
    //     if( texel != 0 )
    //         pixel_buffer[offset] = shade_blend(texel, shade);
    //     u_scan += step_u;
    //     v_scan += step_v;
    //     offset += 1;
    // case 1:
    //     u = u_scan >> texture_shift;
    //     v = v_scan & 0x3f80;
    //     texel = texels[u + v];
    //     if( texel != 0 )
    //         pixel_buffer[offset] = shade_blend(texel, shade);
    //     u_scan += step_u;
    //     v_scan += step_v;
    //     offset += 1;
    // }
}

static void
raster_texture_scanline_opaque_blend_lerp8(
    int* pixel_buffer,
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
    int* texels,
    int texture_width)
{
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    if( screen_x0_ish16 > screen_x1_ish16 )
    {
        SWAP(screen_x0_ish16, screen_x1_ish16);
    }

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

    int screen_x0 = screen_x0_ish16 >> 16;
    int screen_x1 = screen_x1_ish16 >> 16;

    if( screen_x1 >= screen_width )
    {
        screen_x1 = screen_width - 1;
    }

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    step_au_dx <<= 3;
    step_bv_dx <<= 3;
    step_cw_dx <<= 3;

    shade8bit_ish8 += step_shade8bit_dx_ish8 * screen_x0;

    steps = screen_x1 - screen_x0;

    assert(screen_x0 < screen_width);
    assert(screen_x1 < screen_width);

    assert(screen_x0 <= screen_x1);
    assert(screen_x0 >= 0);
    assert(screen_x1 >= 0);

    offset += screen_x0;

    assert(screen_x0 + steps < screen_width);

    // If texture width is 128 or 64.
    assert(texture_width == 128 || texture_width == 64);
    int texture_shift = (texture_width & 0x80) ? 7 : 6;

    int curr_u = 0;
    int curr_v = 0;
    int next_u = 0;
    int next_v = 0;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;
    int lerp8_shade_step = step_shade8bit_dx_ish8 << 3;
    int shade;
    do
    {
        if( lerp8_steps == 0 )
            break;

        int w = (cw) >> texture_shift;
        if( w == 0 )
            continue;

        curr_u = (au) / w;
        curr_u = clamp(curr_u, 0, texture_width - 1);
        curr_v = (bv) / w;
        // curr_v = clamp(curr_v, 0, texture_width - 1);

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
            pixel_buffer, offset, texels, u_scan, v_scan, step_u, step_v, texture_shift, shade);
        u_scan += step_u;
        v_scan += step_v;
        offset += 8;

        // Checked on 09/15/2025, clang does NOT vectorize this loop.
        // even with -O3
        // On large screens the vectorized version is significantly faster.
        // for( int i = 0; i < 8; i++ )
        // {
        //     int u = u_scan >> texture_shift;
        //     int v = v_scan & 0x3f80;
        //     int texel = texels[u + (v)];
        //     pixel_buffer[offset] = shade_blend(texel, shade);

        //     u_scan += step_u;
        //     v_scan += step_v;

        //     offset += 1;
        // }

        shade8bit_ish8 += lerp8_shade_step;

    } while( lerp8_steps-- > 0 );

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
        int v = v_scan & 0x3f80;
        int texel = texels[u + v];
        pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}

static void
raster_texture_scanline_transparent_lerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int* texels,
    int texture_width)
{
    if( screen_x0 == screen_x1 )
        return;

    if( screen_x0 > screen_x1 )
    {
        SWAP(screen_x0, screen_x1);
    }

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0 < 0 )
        screen_x0 = 0;

    if( screen_x1 >= screen_width )
    {
        screen_x1 = screen_width - 1;
    }

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    steps = screen_x1 - screen_x0;

    assert(screen_x0 < screen_width);
    assert(screen_x1 < screen_width);

    assert(screen_x0 <= screen_x1);
    assert(screen_x0 >= 0);
    assert(screen_x1 >= 0);

    offset += screen_x0;

    assert(screen_x0 + steps < screen_width);

    // If texture width is 128 or 64.
    assert(texture_width == 128 || texture_width == 64);
    int texture_shift = (texture_width & 0x80) ? 7 : 6;

    int curr_u = 0;
    int curr_v = 0;
    int next_u = 0;
    int next_v = 0;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;

    int shade = shade8bit_ish8 >> 8;

    do
    {
        if( lerp8_steps == 0 )
            break;

        int w = (-cw) >> texture_shift;
        if( w == 0 )
            continue;

        curr_u = (au) / w;
        curr_u = clamp(curr_u, 0, texture_width - 1);
        curr_v = (bv) / w;
        // curr_v = clamp(curr_v, 0, texture_width - 1);

        au += (step_au_dx << 3);
        bv += (step_bv_dx << 3);
        cw += (step_cw_dx << 3);

        w = (-cw) >> texture_shift;
        if( w == 0 )
            continue;

        next_u = (au) / w;
        next_u = clamp(next_u, 0x0, texture_width - 1);
        next_v = (bv) / w;
        // next_v = clamp(next_v, 0x0, texture_width - 1);

        int step_u = (next_u - curr_u);
        int step_v = (next_v - curr_v);

        int u_scan = curr_u << 3;
        int v_scan = curr_v << 3;

        for( int i = 0; i < 8; i++ )
        {
            int u = u_scan >> 3;
            int v = v_scan >> 3;
            u &= texture_width - 1;
            v &= texture_width - 1;
            int texel = texels[u + v * texture_width];
            if( texel != 0 )
                pixel_buffer[offset] = shade_blend(texel, shade);

            u_scan += step_u;
            v_scan += step_v;

            offset += 1;
        }

    } while( lerp8_steps-- > 0 );

    if( lerp8_last_steps == 0 )
        return;

    int w = (-cw) >> texture_shift;
    if( w == 0 )
        return;

    curr_u = (au) / w;
    curr_u = clamp(curr_u, 0, texture_width - 1);
    curr_v = (bv) / w;
    // curr_v = clamp(curr_v, 0, texture_width - 1);

    au += (step_au_dx << 3);
    bv += (step_bv_dx << 3);
    cw += (step_cw_dx << 3);

    w = (-cw) >> texture_shift;
    if( w == 0 )
        return;

    next_u = (au) / w;
    next_u = clamp(next_u, 0x0, texture_width - 1);

    next_v = (bv) / w;
    // next_v = clamp(next_v, 0x0, texture_width - 1);

    int u_scan = curr_u << 3;
    int v_scan = curr_v << 3;

    int step_u = (next_u - curr_u);
    int step_v = (next_v - curr_v);

    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> 3;
        int v = v_scan >> 3;
        u &= texture_width - 1;
        v &= texture_width - 1;
        int texel = texels[u + v * texture_width];
        if( texel != 0 )
            pixel_buffer[offset] = shade_blend(texel, shade);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}

static void
raster_texture_scanline_opaque_lerp8(
    int* pixel_buffer,
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
    int shade8bit,
    int* texels,
    int texture_width)
{
    if( screen_x0_ish16 == screen_x1_ish16 )
        return;

    if( screen_x0_ish16 > screen_x1_ish16 )
    {
        SWAP(screen_x0_ish16, screen_x1_ish16);
    }

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0_ish16 < 0 )
        screen_x0_ish16 = 0;

    int screen_x0 = screen_x0_ish16 >> 16;
    int screen_x1 = screen_x1_ish16 >> 16;

    if( screen_x1 >= screen_width )
    {
        screen_x1 = screen_width - 1;
    }

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    step_au_dx <<= 3;
    step_bv_dx <<= 3;
    step_cw_dx <<= 3;

    steps = screen_x1 - screen_x0;

    assert(screen_x0 < screen_width);
    assert(screen_x1 < screen_width);

    assert(screen_x0 <= screen_x1);
    assert(screen_x0 >= 0);
    assert(screen_x1 >= 0);

    offset += screen_x0;

    assert(screen_x0 + steps < screen_width);

    // If texture width is 128 or 64.
    assert(texture_width == 128 || texture_width == 64);
    int texture_shift = (texture_width & 0x80) ? 7 : 6;

    int curr_u = 0;
    int curr_v = 0;
    int next_u = 0;
    int next_v = 0;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;
    do
    {
        if( lerp8_steps == 0 )
            break;

        int w = (cw) >> texture_shift;
        if( w == 0 )
            continue;

        curr_u = (au) / w;
        curr_u = clamp(curr_u, 0, texture_width - 1);
        curr_v = (bv) / w;
        // curr_v = clamp(curr_v, 0, texture_width - 1);

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

        raster_linear_opaque_blend_lerp8(
            pixel_buffer, offset, texels, u_scan, v_scan, step_u, step_v, texture_shift, shade8bit);
        u_scan += step_u;
        v_scan += step_v;
        offset += 8;

        // Checked on 09/15/2025, clang does NOT vectorize this loop.
        // even with -O3
        // On large screens the vectorized version is significantly faster.
        // for( int i = 0; i < 8; i++ )
        // {
        //     int u = u_scan >> texture_shift;
        //     int v = v_scan & 0x3f80;
        //     int texel = texels[u + (v)];
        //     pixel_buffer[offset] = shade_blend(texel, shade);

        //     u_scan += step_u;
        //     v_scan += step_v;

        //     offset += 1;
        // }

    } while( lerp8_steps-- > 0 );

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

    for( int i = 0; i < lerp8_last_steps; i++ )
    {
        int u = u_scan >> texture_shift;
        int v = v_scan & 0x3f80;
        int texel = texels[u + v];
        pixel_buffer[offset] = shade_blend(texel, shade8bit);

        u_scan += step_u;
        v_scan += step_v;

        offset += 1;
    }
}

static void
raster_texture_scanline_lerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int screen_x0,
    int screen_x1,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int* texels,
    int texture_width,
    int texture_opaque)
{
    if( screen_x0 == screen_x1 )
        return;

    if( screen_x0 > screen_x1 )
    {
        SWAP(screen_x0, screen_x1);
    }

    int steps, adjust;

    int offset = pixel_offset;

    if( screen_x0 < 0 )
        screen_x0 = 0;

    if( screen_x1 >= screen_width )
    {
        screen_x1 = screen_width - 1;
    }

    if( screen_x0 >= screen_x1 )
        return;

    adjust = screen_x0 - (screen_width >> 1);
    au += step_au_dx * adjust;
    bv += step_bv_dx * adjust;
    cw += step_cw_dx * adjust;

    steps = screen_x1 - screen_x0;

    assert(screen_x0 < screen_width);
    assert(screen_x1 < screen_width);

    assert(screen_x0 <= screen_x1);
    assert(screen_x0 >= 0);
    assert(screen_x1 >= 0);

    offset += screen_x0;

    assert(screen_x0 + steps < screen_width);

    // If texture width is 128 or 64.
    assert(texture_width == 128 || texture_width == 64);
    int texture_shift = (texture_width & 0x80) ? 7 : 6;

    int lerp8_steps = steps >> 3;
    int lerp8_last_steps = steps & 0x7;
    int curr_u = 0;
    int curr_v = 0;

    int u = au;
    int v = bv;
    int w = cw;

    int curr_w_dsh7 = w >> 7;
    if( curr_w_dsh7 != 0 )
    {
        curr_u = u / (-curr_w_dsh7);
        curr_v = v / (-curr_w_dsh7);

        if( curr_u < 0 )
            curr_u = 0;
        else if( curr_u >= texture_width )
            curr_u = texture_width - 1;

        if( curr_v < 0 )
            curr_v = 0;
        else if( curr_v >= texture_width )
            curr_v = texture_width - 1;
    }

    int next_u = 0;
    int next_v = 0;

    u = u + (step_au_dx << 3);
    v = v + (step_bv_dx << 3);
    w = w + (step_cw_dx << 3);

    curr_w_dsh7 = w >> 7;
    if( curr_w_dsh7 != 0 )
    {
        next_u = u / (-curr_w_dsh7);
        next_v = v / (-curr_w_dsh7);

        if( next_u < 0x7 )
            next_u = 0x7;
        else if( next_u >= texture_width )
            next_u = texture_width - 1;

        if( next_v < 0x7 )
            next_v = 0x7;
        else if( next_v >= texture_width )
            next_v = texture_width - 1;
    }

    int step_u = (next_u - curr_u) >> 3;
    int step_v = (next_v - curr_v) >> 3;

    do
    {
        if( lerp8_steps == 0 )
            break;

        for( int i = 0; i < 8; i++ )
        {
            int texel = texels[curr_u + curr_v * texture_width];
            if( texture_opaque || texel != 0 )
                pixel_buffer[offset++] = texel;

            curr_u += step_u;
            curr_v += step_v;

            assert(curr_u >= 0 && curr_u < texture_width);
            assert(curr_v >= 0 && curr_v < texture_width);
        }

        curr_u = next_u;
        curr_v = next_v;

        u = u + (step_au_dx << 3);
        v = v + (step_bv_dx << 3);
        w = w + (step_cw_dx << 3);

        curr_w_dsh7 = w >> 7;
        if( curr_w_dsh7 != 0 )
        {
            next_u = u / (-curr_w_dsh7);
            next_v = v / (-curr_w_dsh7);

            if( next_u < 0x7 )
                next_u = 0x7;
            else if( next_u >= texture_width )
                next_u = texture_width - 1;

            if( next_v < 0x7 )
                next_v = 0x7;
            else if( next_v >= texture_width )
                next_v = texture_width - 1;
        }

        step_u = (next_u - curr_u) >> 3;
        step_v = (next_v - curr_v) >> 3;

        // if( (-cw) >> texture_shift == 0 )
        //     continue;

        // // Instead of multiplying au,bv by texture width, shift the divisor down.
        // int u = (au) / ((-cw) >> texture_shift);
        // int v = (bv) / ((-cw) >> texture_shift);

        // // This will wrap at the texture width.
        // // The osrs rasterizer tiles textures implicitly by ignoring overflow when
        // // stepping.
        // u &= texture_width - 1;
        // v &= texture_width - 1;

        // // The osrs rasterizer clamps the u and v coordinates to the texture width.
        // int c = -1;

        // assert(u >= 0);
        // assert(v >= 0);
        // assert(u < texture_width);
        // assert(v < texture_width);

        // // if( u < 0 )
        // //     u = 0;
        // // if( v < 0 )
        // //     v = 0;
        // // if( u >= texture_width )
        // //     u = texture_width - 1;
        // // if( v >= texture_width )
        // //     v = texture_width - 1;

        // assert(u >= 0 && u < texture_width);
        // assert(v >= 0 && v < texture_width);

        // int texel = texels[u + v * texture_width];
        // if( texture_opaque || texel != 0 )
        // {
        //     if( c != -1 )
        //         pixel_buffer[offset] = c;
        //     else
        //         pixel_buffer[offset] = texel;
        // }

        // au += step_au_dx;
        // bv += step_bv_dx;
        // cw += step_cw_dx;

        // offset += 1;
        // assert(offset >= 0 && offset < screen_width * screen_height);
    } while( lerp8_steps-- > 0 );

    while( lerp8_last_steps-- > 0 )
    {
        int texel = texels[curr_u + curr_v * texture_width];
        if( texture_opaque || texel != 0 )
            pixel_buffer[offset++] = texel;

        curr_u += step_u;
        curr_v += step_v;
    }
}

void
raster_texture_transparent_blend_lerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    int* texels,
    int texture_width)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
        SWAP(shade7bit_a, shade7bit_c);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
        SWAP(shade7bit_a, shade7bit_b);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
        SWAP(shade7bit_b, shade7bit_c);
    }

    if( screen_y0 >= screen_height )
        return;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    int vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    int vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    int vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    int vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    int vUVPlane_normal_xhat = vU_z * vV_y - vU_y * vV_z;
    int vUVPlane_normal_yhat = vU_x * vV_z - vU_z * vV_x;
    int vUVPlane_normal_zhat = vU_y * vV_x - vU_x * vV_y;

    int vOVPlane_normal_xhat = orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    int vOVPlane_normal_yhat = orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    int vOVPlane_normal_zhat = orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    int vUOPlane_normal_xhat = vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    int vUOPlane_normal_yhat = vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    int vUOPlane_normal_zhat = vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // These two vectors now point in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.

    int dy_AC = screen_y2 - screen_y0;
    int dy_AB = screen_y1 - screen_y0;
    int dy_BC = screen_y2 - screen_y1;

    int dx_AC = screen_x2 - screen_x0;
    int dx_AB = screen_x1 - screen_x0;
    int dx_BC = screen_x2 - screen_x1;

    int dblend7bit_ab = shade7bit_b - shade7bit_a;
    int dblend7bit_ac = shade7bit_c - shade7bit_a;

    int step_edge_x_AC_ish16 = 0;
    int step_edge_x_AB_ish16 = 0;
    int step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    // Do the same computation for the blend color.
    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

    // Same idea here for color. Solve the system of equations.
    // Barycentric coordinates.

    // Shades are provided 0-127, shift up by 1, then up by 8 to get 0-255.
    // Again, kramer's rule.
    int shade8bit_yhat_ish8 = ((dx_AC * dblend7bit_ab - dx_AB * dblend7bit_ac) << 9) / sarea_abc;
    int shade8bit_xhat_ish8 = ((dy_AB * dblend7bit_ac - dy_AC * dblend7bit_ab) << 9) / sarea_abc;

    int shade8bit_edge_ish8 =
        (shade7bit_a << 9) - shade8bit_xhat_ish8 * screen_x0 + shade8bit_xhat_ish8;

    int au = 0;
    int bv = 0;
    int cw = 0;

    int edge_x_AC_ish16 = screen_x0 << 16;
    int edge_x_AB_ish16 = screen_x0 << 16;
    int edge_x_BC_ish16 = screen_x1 << 16;

    if( screen_y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * screen_y0;
        shade8bit_edge_ish8 -= shade8bit_yhat_ish8 * screen_y0;
        screen_y0 = 0;
    }

    if( screen_y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;

        screen_y1 = 0;
    }

    if( screen_y0 > screen_y1 )
        return;

    if( screen_y1 >= screen_height )
        screen_y1 = screen_height - 1;

    au = project_scale_unit(vOVPlane_normal_zhat, camera_fov);
    bv = project_scale_unit(vUOPlane_normal_zhat, camera_fov);
    cw = project_scale_unit(vUVPlane_normal_zhat, camera_fov);

    int dy = screen_y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    int steps = screen_y1 - screen_y0;
    int offset = screen_y0 * screen_width;

    assert(screen_y0 < screen_height);

    while( steps-- > 0 )
    {
        // int x_start = edge_x_AC_ish16 >> 16;
        // int x_end = edge_x_AB_ish16 >> 16;

        raster_texture_scanline_transparent_blend_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            edge_x_AC_ish16,
            edge_x_AB_ish16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade8bit_edge_ish8,
            shade8bit_xhat_ish8,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        shade8bit_edge_ish8 += shade8bit_yhat_ish8;

        offset += screen_width;
    }

    if( screen_y2 >= screen_height )
        screen_y2 = screen_height - 1;

    if( screen_y1 > screen_y2 )
        return;

    dy = screen_y1 - (screen_height >> 1);
    au = project_scale_unit(vOVPlane_normal_zhat, camera_fov);
    bv = project_scale_unit(vUOPlane_normal_zhat, camera_fov);
    cw = project_scale_unit(vUVPlane_normal_zhat, camera_fov);
    au += vOVPlane_normal_yhat * dy;
    bv += vUOPlane_normal_yhat * dy;
    cw += vUVPlane_normal_yhat * dy;

    assert(screen_y1 >= 0);
    assert(screen_y1 <= screen_height);
    assert(screen_y2 >= 0);
    assert(screen_y2 <= screen_height);

    offset = screen_y1 * screen_width;
    steps = screen_y2 - screen_y1;
    while( steps-- > 0 )
    {
        raster_texture_scanline_transparent_blend_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            edge_x_AC_ish16,
            edge_x_BC_ish16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade8bit_edge_ish8,
            shade8bit_xhat_ish8,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        shade8bit_edge_ish8 += shade8bit_yhat_ish8;

        offset += screen_width;
    }
}

void
raster_texture_opaque_blend_lerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    int* texels,
    int texture_width)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
        SWAP(shade7bit_a, shade7bit_c);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
        SWAP(shade7bit_a, shade7bit_b);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
        SWAP(shade7bit_b, shade7bit_c);
    }

    if( screen_y0 >= screen_height )
        return;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    int vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    int vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    int vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    int vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    int vUVPlane_normal_xhat = vU_z * vV_y - vU_y * vV_z;
    int vUVPlane_normal_yhat = vU_x * vV_z - vU_z * vV_x;
    int vUVPlane_normal_zhat = vU_y * vV_x - vU_x * vV_y;

    int vOVPlane_normal_xhat = orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    int vOVPlane_normal_yhat = orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    int vOVPlane_normal_zhat = orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    int vUOPlane_normal_xhat = vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    int vUOPlane_normal_yhat = vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    int vUOPlane_normal_zhat = vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // These two vectors now point in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.

    int dy_AC = screen_y2 - screen_y0;
    int dy_AB = screen_y1 - screen_y0;
    int dy_BC = screen_y2 - screen_y1;

    int dx_AC = screen_x2 - screen_x0;
    int dx_AB = screen_x1 - screen_x0;
    int dx_BC = screen_x2 - screen_x1;

    int dblend7bit_ab = shade7bit_b - shade7bit_a;
    int dblend7bit_ac = shade7bit_c - shade7bit_a;

    int step_edge_x_AC_ish16 = 0;
    int step_edge_x_AB_ish16 = 0;
    int step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    // Do the same computation for the blend color.
    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

    // Same idea here for color. Solve the system of equations.
    // Barycentric coordinates.

    // Shades are provided 0-127, shift up by 1, then up by 8 to get 0-255.
    // Again, kramer's rule.
    int shade8bit_yhat_ish8 = ((dx_AC * dblend7bit_ab - dx_AB * dblend7bit_ac) << 9) / sarea_abc;
    int shade8bit_xhat_ish8 = ((dy_AB * dblend7bit_ac - dy_AC * dblend7bit_ab) << 9) / sarea_abc;

    int shade8bit_edge_ish8 =
        (shade7bit_a << 9) - shade8bit_xhat_ish8 * screen_x0 + shade8bit_xhat_ish8;

    int au = 0;
    int bv = 0;
    int cw = 0;

    int edge_x_AC_ish16 = screen_x0 << 16;
    int edge_x_AB_ish16 = screen_x0 << 16;
    int edge_x_BC_ish16 = screen_x1 << 16;

    if( screen_y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * screen_y0;
        shade8bit_edge_ish8 -= shade8bit_yhat_ish8 * screen_y0;
        screen_y0 = 0;
    }

    if( screen_y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;

        screen_y1 = 0;
    }

    if( screen_y0 > screen_y1 )
        return;

    if( screen_y1 >= screen_height )
        screen_y1 = screen_height - 1;

    au = project_scale_unit(vOVPlane_normal_zhat, camera_fov);
    bv = project_scale_unit(vUOPlane_normal_zhat, camera_fov);
    cw = project_scale_unit(vUVPlane_normal_zhat, camera_fov);

    int dy = screen_y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    int steps = screen_y1 - screen_y0;
    int offset = screen_y0 * screen_width;

    assert(screen_y0 < screen_height);

    while( steps-- > 0 )
    {
        // int x_start = edge_x_AC_ish16 >> 16;
        // int x_end = edge_x_AB_ish16 >> 16;

        raster_texture_scanline_opaque_blend_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            edge_x_AC_ish16,
            edge_x_AB_ish16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade8bit_edge_ish8,
            shade8bit_xhat_ish8,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        shade8bit_edge_ish8 += shade8bit_yhat_ish8;

        offset += screen_width;
    }

    if( screen_y2 >= screen_height )
        screen_y2 = screen_height - 1;

    if( screen_y1 > screen_y2 )
        return;

    dy = screen_y1 - (screen_height >> 1);
    au = project_scale_unit(vOVPlane_normal_zhat, camera_fov);
    bv = project_scale_unit(vUOPlane_normal_zhat, camera_fov);
    cw = project_scale_unit(vUVPlane_normal_zhat, camera_fov);
    au += vOVPlane_normal_yhat * dy;
    bv += vUOPlane_normal_yhat * dy;
    cw += vUVPlane_normal_yhat * dy;

    assert(screen_y1 >= 0);
    assert(screen_y1 <= screen_height);
    assert(screen_y2 >= 0);
    assert(screen_y2 <= screen_height);

    offset = screen_y1 * screen_width;
    steps = screen_y2 - screen_y1;
    while( steps-- > 0 )
    {
        raster_texture_scanline_opaque_blend_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            edge_x_AC_ish16,
            edge_x_BC_ish16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade8bit_edge_ish8,
            shade8bit_xhat_ish8,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        shade8bit_edge_ish8 += shade8bit_yhat_ish8;

        offset += screen_width;
    }
}

void
raster_texture_transparent_lerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit,
    int* texels,
    int texture_width)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
    }

    if( screen_y0 >= screen_height )
        return;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    int vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    int vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    int vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    int vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    // TODO: The derivation leaves this as the VU plane,
    // so this needs to be flipped.
    int vUVPlane_normal_xhat = vU_y * vV_z - vU_z * vV_y;
    int vUVPlane_normal_yhat = vU_z * vV_x - vU_x * vV_z;
    int vUVPlane_normal_zhat = vU_x * vV_y - vU_y * vV_x;

    int vOVPlane_normal_xhat = orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    int vOVPlane_normal_yhat = orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    int vOVPlane_normal_zhat = orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    int vUOPlane_normal_xhat = vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    int vUOPlane_normal_yhat = vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    int vUOPlane_normal_zhat = vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // These two vectors now point in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.

    int dy_AC = screen_y2 - screen_y0;
    int dy_AB = screen_y1 - screen_y0;
    int dy_BC = screen_y2 - screen_y1;

    int dx_AC = screen_x2 - screen_x0;
    int dx_AB = screen_x1 - screen_x0;
    int dx_BC = screen_x2 - screen_x1;

    int step_edge_x_AC_ish16 = 0;
    int step_edge_x_AB_ish16 = 0;
    int step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    // Do the same computation for the blend color.
    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

    int au = 0;
    int bv = 0;
    int cw = 0;

    int edge_x_AC_ish16 = screen_x0 << 16;
    int edge_x_AB_ish16 = screen_x0 << 16;
    int edge_x_BC_ish16 = screen_x1 << 16;

    if( screen_y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * screen_y0;
        screen_y0 = 0;
    }

    if( screen_y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;

        screen_y1 = 0;
    }

    if( screen_y0 > screen_y1 )
        return;

    if( screen_y1 >= screen_height )
        screen_y1 = screen_height - 1;

    au = project_scale_unit(vOVPlane_normal_zhat, camera_fov);
    bv = project_scale_unit(vUOPlane_normal_zhat, camera_fov);
    cw = project_scale_unit(vUVPlane_normal_zhat, camera_fov);

    int dy = screen_y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    int steps = screen_y1 - screen_y0;
    int offset = screen_y0 * screen_width;

    int shade = shade7bit << 9;

    assert(screen_y0 < screen_height);

    while( steps-- > 0 )
    {
        int x_start = edge_x_AC_ish16 >> 16;
        int x_end = edge_x_AB_ish16 >> 16;

        raster_texture_scanline_transparent_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            x_start,
            x_end,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += screen_width;
    }

    if( screen_y2 >= screen_height )
        screen_y2 = screen_height - 1;

    if( screen_y1 > screen_y2 )
        return;

    dy = screen_y1 - (screen_height >> 1);
    au = project_scale_unit(vOVPlane_normal_zhat, camera_fov);
    bv = project_scale_unit(vUOPlane_normal_zhat, camera_fov);
    cw = project_scale_unit(vUVPlane_normal_zhat, camera_fov);
    au += vOVPlane_normal_yhat * dy;
    bv += vUOPlane_normal_yhat * dy;
    cw += vUVPlane_normal_yhat * dy;

    assert(screen_y1 >= 0);
    assert(screen_y1 <= screen_height);
    assert(screen_y2 >= 0);
    assert(screen_y2 <= screen_height);

    offset = screen_y1 * screen_width;
    steps = screen_y2 - screen_y1;
    while( steps-- > 0 )
    {
        raster_texture_scanline_transparent_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            edge_x_AC_ish16 >> 16,
            edge_x_BC_ish16 >> 16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += screen_width;
    }
}

void
raster_texture_opaque_lerp8(
    int* pixel_buffer,
    int screen_width,
    int screen_height,
    int camera_fov,
    int screen_x0,
    int screen_x1,
    int screen_x2,
    int screen_y0,
    int screen_y1,
    int screen_y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit,
    int* texels,
    int texture_width)
{
    if( screen_y2 < screen_y0 )
    {
        SWAP(screen_y0, screen_y2);
        SWAP(screen_x0, screen_x2);
    }

    if( screen_y1 < screen_y0 )
    {
        SWAP(screen_y0, screen_y1);
        SWAP(screen_x0, screen_x1);
    }

    if( screen_y2 < screen_y1 )
    {
        SWAP(screen_y1, screen_y2);
        SWAP(screen_x1, screen_x2);
    }

    if( screen_y0 >= screen_height )
        return;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vU_x = orthographic_uend_x1 - orthographic_uvorigin_x0;
    int vU_y = orthographic_uend_y1 - orthographic_uvorigin_y0;
    int vU_z = orthographic_uend_z1 - orthographic_uvorigin_z0;

    // Assumes that the world coordinates differ from uv coordinates only by a scaling factor
    int vV_x = orthographic_vend_x2 - orthographic_uvorigin_x0;
    int vV_y = orthographic_vend_y2 - orthographic_uvorigin_y0;
    int vV_z = orthographic_vend_z2 - orthographic_uvorigin_z0;

    int vUVPlane_normal_xhat = vU_z * vV_y - vU_y * vV_z;
    int vUVPlane_normal_yhat = vU_x * vV_z - vU_z * vV_x;
    int vUVPlane_normal_zhat = vU_y * vV_x - vU_x * vV_y;

    int vOVPlane_normal_xhat = orthographic_uvorigin_y0 * vV_z - orthographic_uvorigin_z0 * vV_y;
    int vOVPlane_normal_yhat = orthographic_uvorigin_z0 * vV_x - orthographic_uvorigin_x0 * vV_z;
    int vOVPlane_normal_zhat = orthographic_uvorigin_x0 * vV_y - orthographic_uvorigin_y0 * vV_x;

    int vUOPlane_normal_xhat = vU_y * orthographic_uvorigin_z0 - vU_z * orthographic_uvorigin_y0;
    int vUOPlane_normal_yhat = vU_z * orthographic_uvorigin_x0 - vU_x * orthographic_uvorigin_z0;

    int vUOPlane_normal_zhat = vU_x * orthographic_uvorigin_y0 - vU_y * orthographic_uvorigin_x0;

    // These two vectors now point in the direction or U or V.
    // TODO: Need to make sure this is the right order.
    // Compute the partial derivatives of the uv coordinates with respect to the x and y coordinates
    // of the screen.

    int dy_AC = screen_y2 - screen_y0;
    int dy_AB = screen_y1 - screen_y0;
    int dy_BC = screen_y2 - screen_y1;

    int dx_AC = screen_x2 - screen_x0;
    int dx_AB = screen_x1 - screen_x0;
    int dx_BC = screen_x2 - screen_x1;

    int step_edge_x_AC_ish16 = 0;
    int step_edge_x_AB_ish16 = 0;
    int step_edge_x_BC_ish16 = 0;

    if( dy_AC > 0 )
        step_edge_x_AC_ish16 = (dx_AC << 16) / dy_AC;
    if( dy_AB > 0 )
        step_edge_x_AB_ish16 = (dx_AB << 16) / dy_AB;
    if( dy_BC > 0 )
        step_edge_x_BC_ish16 = (dx_BC << 16) / dy_BC;

    // Do the same computation for the blend color.
    int sarea_abc = dx_AC * dy_AB - dx_AB * dy_AC;
    if( sarea_abc == 0 )
        return;

    // Same idea here for color. Solve the system of equations.
    // Barycentric coordinates.

    int au = 0;
    int bv = 0;
    int cw = 0;

    int edge_x_AC_ish16 = screen_x0 << 16;
    int edge_x_AB_ish16 = screen_x0 << 16;
    int edge_x_BC_ish16 = screen_x1 << 16;

    if( screen_y0 < 0 )
    {
        edge_x_AC_ish16 -= step_edge_x_AC_ish16 * screen_y0;
        edge_x_AB_ish16 -= step_edge_x_AB_ish16 * screen_y0;
        screen_y0 = 0;
    }

    if( screen_y1 < 0 )
    {
        edge_x_BC_ish16 -= step_edge_x_BC_ish16 * screen_y1;

        screen_y1 = 0;
    }

    if( screen_y0 > screen_y1 )
        return;

    if( screen_y1 >= screen_height )
        screen_y1 = screen_height - 1;

    au = project_scale_unit(vOVPlane_normal_zhat, camera_fov);
    bv = project_scale_unit(vUOPlane_normal_zhat, camera_fov);
    cw = project_scale_unit(vUVPlane_normal_zhat, camera_fov);

    int dy = screen_y0 - (screen_height >> 1);
    au += vOVPlane_normal_yhat * (dy);
    bv += vUOPlane_normal_yhat * (dy);
    cw += vUVPlane_normal_yhat * (dy);

    int steps = screen_y1 - screen_y0;
    int offset = screen_y0 * screen_width;

    int shade8bit = shade7bit << 1;

    assert(screen_y0 < screen_height);

    while( steps-- > 0 )
    {
        raster_texture_scanline_opaque_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            edge_x_AC_ish16,
            edge_x_AB_ish16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade8bit,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_AB_ish16 += step_edge_x_AB_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += screen_width;
    }

    if( screen_y2 >= screen_height )
        screen_y2 = screen_height - 1;

    if( screen_y1 > screen_y2 )
        return;

    dy = screen_y1 - (screen_height >> 1);
    au = project_scale_unit(vOVPlane_normal_zhat, camera_fov);
    bv = project_scale_unit(vUOPlane_normal_zhat, camera_fov);
    cw = project_scale_unit(vUVPlane_normal_zhat, camera_fov);
    au += vOVPlane_normal_yhat * dy;
    bv += vUOPlane_normal_yhat * dy;
    cw += vUVPlane_normal_yhat * dy;

    assert(screen_y1 >= 0);
    assert(screen_y1 <= screen_height);
    assert(screen_y2 >= 0);
    assert(screen_y2 <= screen_height);

    offset = screen_y1 * screen_width;
    steps = screen_y2 - screen_y1;
    while( steps-- > 0 )
    {
        raster_texture_scanline_opaque_lerp8(
            pixel_buffer,
            screen_width,
            screen_height,
            edge_x_AC_ish16,
            edge_x_BC_ish16,
            offset,
            au,
            bv,
            cw,
            vOVPlane_normal_xhat,
            vUOPlane_normal_xhat,
            vUVPlane_normal_xhat,
            shade8bit,
            texels,
            texture_width);

        edge_x_AC_ish16 += step_edge_x_AC_ish16;
        edge_x_BC_ish16 += step_edge_x_BC_ish16;

        au += vOVPlane_normal_yhat;
        bv += vUOPlane_normal_yhat;
        cw += vUVPlane_normal_yhat;

        offset += screen_width;
    }
}

#endif