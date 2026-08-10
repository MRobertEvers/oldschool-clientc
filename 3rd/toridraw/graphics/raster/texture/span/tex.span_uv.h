#ifndef TEX_SPAN_UV_H
#define TEX_SPAN_UV_H

#include "graphics/alpha.h"
#include "graphics/clamp.h"
#include "graphics/dash_restrict.h"
#include "graphics/shade.h"

#include <stdint.h>

/*
 * Shared uv rules for the perspective texture spans.
 *
 * Every perspective span kernel walks 8 pixels at a time and fits a straight
 * line between the exact uv at the block's two endpoints. Two things break
 * that fit when a face's texture plane passes close to the eye - which is what
 * the camera sitting inside a large model's bounding sphere does. See
 * docs/qbd_toridraw_streaks_debug.md.
 *
 *   1. u/w and v/w grow without bound as w approaches 0. u is clamped into the
 *      texture so it survives, but v wraps, so its low bits - exactly the bits
 *      v_mask keeps - are the answer. They are also the first thing a float
 *      reciprocal loses, and `cur_v << texture_shift` throws the rest away by
 *      overflowing.
 *
 *   2. Once a block spans more than about one texture tile, the straight line
 *      is not an approximation of the hyperbola at all. It sweeps the texture
 *      smoothly where the true mapping jumps - a streak.
 *
 * The helpers below keep the endpoint quotients exact and say when the linear
 * fit is still meaningful. Blocks that fail are drawn a pixel at a time, which
 * is how the reference rasterizer draws every pixel
 * (docs/raster_scanlines_thedaneeffect.txt).
 */

/* Past this the float product's ulp exceeds one texel row, so the low bits that
 * select the row are already gone. (float)n * (1.0f / (float)w) carries about
 * 1.8e-7 of relative error, so 2^21 leaves a couple of bits of margin. */
#define TEX_SPAN_RECIPROCAL_EXACT_LIMIT 2097152.0f

/* Keeps `nxt_v - cur_v` inside int for the fit test below. A quotient this
 * large is pathological anyway and takes the per-pixel path. */
#define TEX_SPAN_V_LERP_MAGNITUDE_LIMIT (1 << 29)

/**
 * v is unbounded and wraps, so the caller needs the true quotient's low bits.
 * The float reciprocal stands only where it still carries them.
 */
static inline int
tex_span_v_quotient(int bv, int w, float inv_w)
{
    float quotient = (float)bv * inv_w;
    if( quotient > -TEX_SPAN_RECIPROCAL_EXACT_LIMIT &&
        quotient < TEX_SPAN_RECIPROCAL_EXACT_LIMIT )
        return (int)quotient;
    return bv / w;
}

/**
 * u is clamped into the texture, so the reciprocal only has to place the value
 * on the correct side of each bound, which relative error cannot change. The
 * clamp is applied in float so an out-of-range quotient is never converted -
 * that conversion is undefined, and on x86 it yields INT_MIN.
 */
static inline int
tex_span_u_quotient(int au, float inv_w, int texture_width)
{
    float quotient = (float)au * inv_w;
    if( quotient <= 0.0f )
        return 0;
    if( quotient >= (float)(texture_width - 1) )
        return texture_width - 1;
    return (int)quotient;
}

/**
 * True when an 8-pixel linear fit between cur_v and nxt_v is worth drawing.
 * Because a passing fit is bounded by one tile, it also guarantees the step and
 * scan arithmetic that draws it stays inside int.
 */
static inline int
tex_span_lerp8_v_fits(int cur_v, int nxt_v, int texture_width)
{
    if( cur_v < -TEX_SPAN_V_LERP_MAGNITUDE_LIMIT ||
        cur_v > TEX_SPAN_V_LERP_MAGNITUDE_LIMIT ||
        nxt_v < -TEX_SPAN_V_LERP_MAGNITUDE_LIMIT ||
        nxt_v > TEX_SPAN_V_LERP_MAGNITUDE_LIMIT )
        return 0;

    int delta = nxt_v - cur_v;
    return delta >= -texture_width && delta <= texture_width;
}

/**
 * Only the low log2(texture_width) bits of a row index survive v_mask, so the
 * scan can start from the folded row: `(cur_v << shift) & v_mask` equals
 * `(cur_v & (texture_width - 1)) << shift` without the overflowing shift.
 */
static inline int
tex_span_v_scan_start(int cur_v, int texture_width, int texture_shift)
{
    return (cur_v & (texture_width - 1)) << texture_shift;
}

/**
 * 8-pixel span for a texture that carries per-texel alpha.
 *
 * The stock opaque and transparent kernels treat a texel as all-or-nothing:
 * opaque writes every texel, transparent writes those that are non-zero. This
 * one blends each texel over the framebuffer by its own coverage, which is what
 * lets an imported material with a continuous alpha ramp draw as authored
 * rather than being thresholded into holes.
 *
 * Alpha 0 is skipped rather than blended - it is a no-op either way, and
 * skipping keeps the common empty region as cheap as the transparent kernel.
 * The blend itself is `alpha_blend`, so it shares the unsigned arithmetic that
 * path already relies on.
 */
static inline void
raster_linear_alpha_blend_lerp8_v3(
    uint32_t* RESTRICT pixel_buffer,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int u_mask,
    int v_mask,
    int shade)
{
    for( int i = 0; i < 8; i++ )
    {
        int u = (u_scan >> texture_shift) & u_mask;
        int v = v_scan & v_mask;
        uint32_t texel = texels[u + v];
        int alpha = (int)(texel >> 24);

        if( alpha != 0 )
        {
            int lit = shade_blend(texel & 0x00FFFFFFu, shade);
            pixel_buffer[i] = (uint32_t)(alpha == 0xFF
                                             ? lit
                                             : alpha_blend(alpha, (int)pixel_buffer[i], lit));
        }

        u_scan += step_u;
        v_scan += step_v;
    }
}

/**
 * Per-pixel tail/fallback twin of the above, for a partial block or one whose
 * uv gradient the linear fit cannot represent.
 */
static inline void
tex_span_alpha_exact_block(
    int* RESTRICT pixel_buffer,
    int offset,
    const int* RESTRICT texels,
    int count,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade,
    int texture_width,
    int texture_shift)
{
    for( int i = 0; i < count; i++ )
    {
        int w = cw >> texture_shift;
        if( w != 0 )
        {
            int u = clamp(au / w, 0, texture_width - 1);
            int v = (bv / w) & (texture_width - 1);
            uint32_t texel = (uint32_t)texels[u + (v << texture_shift)];
            int alpha = (int)(texel >> 24);
            if( alpha != 0 )
            {
                int lit = shade_blend(texel & 0x00FFFFFFu, shade);
                pixel_buffer[offset + i] =
                    alpha == 0xFF ? lit
                                  : alpha_blend(alpha, pixel_buffer[offset + i], lit);
            }
        }
        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;
    }
}

/**
 * Perspective scanline for an alpha-blended texture.
 *
 * Structurally identical to its opaque and transparent twins in the ISA span
 * files - the same block walk, the same uv rules, the same per-pixel fallback -
 * differing only in the kernel it hands each block to. It lives here rather
 * than being copied five times because that walk contains no intrinsics; only
 * the 8-pixel kernel is a candidate for vectorising, and this one is scalar for
 * now.
 */
static inline void
draw_texture_scanline_alpha_blend_branching_lerp8_v3_ordered(
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

    int step_au8 = step_au_dx << 3;
    int step_bv8 = step_bv_dx << 3;
    int step_cw8 = step_cw_dx << 3;

    int cur_u = 0;
    int cur_v = 0;
    int have_cur = 0;

    while( blocks-- )
    {
        int w = cw >> texture_shift;
        if( w != 0 )
        {
            if( !have_cur )
            {
                float inv_w = 1.0f / (float)w;
                cur_u = tex_span_u_quotient(au, inv_w, texture_width);
                cur_v = tex_span_v_quotient(bv, w, inv_w);
            }

            int w_n = (cw + step_cw8) >> texture_shift;
            int nxt_u = cur_u;
            int nxt_v = cur_v;
            if( w_n != 0 )
            {
                float inv_w_n = 1.0f / (float)w_n;
                nxt_u = tex_span_u_quotient(au + step_au8, inv_w_n, texture_width);
                nxt_v = tex_span_v_quotient(bv + step_bv8, w_n, inv_w_n);
            }

            if( w_n != 0 && tex_span_lerp8_v_fits(cur_v, nxt_v, texture_width) )
            {
                raster_linear_alpha_blend_lerp8_v3(
                    (uint32_t*)&pixel_buffer[offset],
                    (const uint32_t*)texels,
                    cur_u << texture_shift,
                    tex_span_v_scan_start(cur_v, texture_width, texture_shift),
                    (nxt_u - cur_u) << (texture_shift - 3),
                    (nxt_v - cur_v) << (texture_shift - 3),
                    texture_shift,
                    u_mask,
                    v_mask,
                    shade8bit_ish8 >> 8);
            }
            else
            {
                tex_span_alpha_exact_block(
                    pixel_buffer, offset, texels, 8, au, bv, cw,
                    step_au_dx, step_bv_dx, step_cw_dx,
                    shade8bit_ish8 >> 8, texture_width, texture_shift);
            }

            cur_u = nxt_u;
            cur_v = nxt_v;
            have_cur = (w_n != 0);
        }
        else
        {
            have_cur = 0;
        }
        au += step_au8;
        bv += step_bv8;
        cw += step_cw8;
        offset += 8;
        shade8bit_ish8 += (step_shade8bit_dx_ish8 << 3);
    }

    if( remaining > 0 && (cw >> texture_shift) != 0 )
        tex_span_alpha_exact_block(
            pixel_buffer, offset, texels, remaining, au, bv, cw,
            step_au_dx, step_bv_dx, step_cw_dx,
            shade8bit_ish8 >> 8, texture_width, texture_shift);
}

/**
 * The per-pixel path: an exact divide per pixel, the same mapping the reference
 * rasterizer uses for the whole span. Only blocks that fail
 * tex_span_lerp8_v_fits come here, so this is cold.
 */
static inline void
tex_span_exact_block(
    int* RESTRICT pixel_buffer,
    int offset,
    const int* RESTRICT texels,
    int count,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade,
    int texture_width,
    int texture_shift,
    int transparent)
{
    for( int i = 0; i < count; i++ )
    {
        int w = cw >> texture_shift;
        if( w != 0 )
        {
            int u = clamp(au / w, 0, texture_width - 1);
            int v = (bv / w) & (texture_width - 1);
            int texel = texels[u + (v << texture_shift)];
            if( !transparent || texel != 0 )
                pixel_buffer[offset + i] = shade_blend(texel, shade);
        }
        au += step_au_dx;
        bv += step_bv_dx;
        cw += step_cw_dx;
    }
}

#endif
