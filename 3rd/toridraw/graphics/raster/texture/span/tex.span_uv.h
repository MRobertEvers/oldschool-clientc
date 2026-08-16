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
 * A wrapping axis is unbounded, so the caller needs the true quotient's low
 * bits. The float reciprocal stands only where it still carries them.
 *
 * v always wraps. u wraps only where the sampler says the material repeats -
 * see tex_span_u_quotient.
 */
static inline int
tex_span_wrapped_quotient(int n, int w, float inv_w)
{
    float quotient = (float)n * inv_w;
    if( quotient > -TEX_SPAN_RECIPROCAL_EXACT_LIMIT &&
        quotient < TEX_SPAN_RECIPROCAL_EXACT_LIMIT )
        return (int)quotient;
    return n / w;
}

/**
 * u under CLAMP addressing, which is what the reference does and what every
 * span without a sampler does.
 *
 * The value is bounded into the texture, so the reciprocal only has to place it
 * on the correct side of each bound, which relative error cannot change. The
 * clamp is applied in float so an out-of-range quotient is never converted -
 * that conversion is undefined, and on x86 it yields INT_MIN.
 *
 * A repeat-addressed material wants tex_span_wrapped_quotient instead. Only the
 * sampler kernels can tell the difference, because only they carry the address
 * mode; the plain spans have no sampler and clamp unconditionally.
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
 * True when an 8-pixel linear fit between a block's two endpoints is worth
 * drawing. Because a passing fit is bounded by one tile, it also guarantees the
 * step and scan arithmetic that draws it stays inside int.
 *
 * Applied to every axis that can leave the texture. That is v always, and u
 * whenever the material repeats: a clamped u is bounded by construction and
 * passes trivially, a wrapped one is as unbounded as v and breaks the fit the
 * same way.
 */
static inline int
tex_span_lerp8_fits(int cur, int nxt, int texture_width)
{
    if( cur < -TEX_SPAN_V_LERP_MAGNITUDE_LIMIT || cur > TEX_SPAN_V_LERP_MAGNITUDE_LIMIT ||
        nxt < -TEX_SPAN_V_LERP_MAGNITUDE_LIMIT || nxt > TEX_SPAN_V_LERP_MAGNITUDE_LIMIT )
        return 0;

    int delta = nxt - cur;
    return delta >= -texture_width && delta <= texture_width;
}

/**
 * Only the low log2(texture_width) bits of an index survive the repeat mask, so
 * the scan can start from the folded value: `(cur << shift) & mask` equals
 * `(cur & (texture_width - 1)) << shift` without the overflowing shift.
 */
static inline int
tex_span_wrapped_scan_start(int cur, int texture_width, int texture_shift)
{
    return (cur & (texture_width - 1)) << texture_shift;
}

/**
 * The per-pixel path: an exact divide per pixel, the same mapping the reference
 * rasterizer uses for the whole span. Only blocks that fail
 * tex_span_lerp8_fits come here, so this is cold.
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
