#ifndef TEX_SPAN_UV_H
#define TEX_SPAN_UV_H

#include "graphics/clamp.h"
#include "graphics/dash_restrict.h"
#include "graphics/shade.h"
#include "graphics/shared_tables.h"

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
