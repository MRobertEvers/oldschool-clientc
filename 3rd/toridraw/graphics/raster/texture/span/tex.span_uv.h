#ifndef TEX_SPAN_UV_H
#define TEX_SPAN_UV_H

#include "graphics/alpha.h"
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
 * What a modulated or detail texture needs from the face, computed once per
 * face by the raster and constant across the span.
 *
 * `channel` is the face's chroma at the reference lightness, as three 0..256
 * scales where 256 is the identity. `detail` selects which kernel the span
 * hands its blocks to; it rides here rather than as another parameter because
 * the same value has to reach six functions unchanged.
 */
struct TexSpanTint
{
    int channel[3];
    int detail;
    /* The face's hsl16 with its lightness cleared. The detail kernel needs the
     * chroma itself, not a pre-multiplied colour: it rebuilds the face's colour
     * through the palette at each pixel's own lightness, which is a curve, and
     * scaling one mid-lightness colour by the shade is a straight line. Those
     * disagree most at the ends, which reads as a gradient far steeper than the
     * untextured faces beside it. */
    int chroma;
};

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
 * Per-channel tint, the multiply a modulated texture needs on top of the shade.
 *
 * `tint` is three 0..256 channels, not 0..255: 256 is the identity, matching
 * the convention `shade_blend` already uses for its shade, so a white tint
 * costs nothing in accuracy. Pack it with tex_span_tint_pack.
 *
 * The channels have to be handled separately - the packed red/blue trick the
 * alpha and shade blends use works only when one scalar multiplies both, and
 * here each channel has its own.
 */
static inline int
tex_span_tint_apply(uint32_t lit, const struct TexSpanTint* RESTRICT tint)
{
    uint32_t r = ((lit >> 16) & 0xFFu) * (uint32_t)tint->channel[0] >> 8;
    uint32_t g = ((lit >> 8) & 0xFFu) * (uint32_t)tint->channel[1] >> 8;
    uint32_t b = (lit & 0xFFu) * (uint32_t)tint->channel[2] >> 8;

    if( r > 0xFF ) r = 0xFF;
    if( g > 0xFF ) g = 0xFF;
    if( b > 0xFF ) b = 0xFF;
    return (int)((r << 16) | (g << 8) | b);
}

/** RGB888 -> the three 0..256 channels tex_span_tint_apply wants. */
static inline void
tex_span_tint_pack(int rgb, struct TexSpanTint* RESTRICT out_tint)
{
    for( int i = 0; i < 3; i++ )
    {
        int c = (rgb >> (16 - 8 * i)) & 0xFF;
        out_tint->channel[i] = (c * 256 + 127) / 255;
    }
    out_tint->detail = 0;
    out_tint->chroma = 0;
}

/**
 * The colour an untextured face would have had at this pixel.
 *
 * Exactly what the gouraud path draws: the face's chroma looked up in the
 * palette at the interpolated lightness. `shade` is 7-bit lightness doubled
 * (the span carries shade7bit << 1), so halving it recovers the lightness the
 * lighting pass computed.
 */
static inline uint32_t
tex_span_detail_base(const struct TexSpanTint* RESTRICT tint, int shade)
{
    int lightness = shade >> 1;

    if( lightness < 0 )
        lightness = 0;
    if( lightness > 0x7F )
        lightness = 0x7F;
    return (uint32_t)ToriDraw_Hsl16ToRgb((uint16_t)(tint->chroma | lightness));
}

/**
 * 8-pixel span for a texture that carries per-texel alpha AND is modulated by
 * the face's own colour.
 *
 * An imported RS727 blend layer is a mask, not a diffuse map: its shape is in
 * the alpha and its RGB is a greyscale detail pattern. The surface colour comes
 * from the face. SD's own textures are diffuse maps and must not be modulated,
 * so this is a separate kernel selected by the texture's `modulate` flag rather
 * than a tint threaded through the stock paths - a white tint would still cost
 * three multiplies a pixel on every textured face in the scene.
 *
 * The tint is the face's chroma at a fixed reference lightness, computed once
 * per face by the caller: the per-vertex lightness is already the shade, and
 * applying the authored lightness here as well would count it twice.
 */
static inline void
raster_linear_alpha_modulate_lerp8_v3(
    uint32_t* RESTRICT pixel_buffer,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int u_mask,
    int v_mask,
    int shade,
    const struct TexSpanTint* RESTRICT tint)
{
    for( int i = 0; i < 8; i++ )
    {
        int u = (u_scan >> texture_shift) & u_mask;
        int v = v_scan & v_mask;
        uint32_t texel = texels[u + v];
        int alpha = (int)(texel >> 24);

        if( alpha != 0 )
        {
            int lit = tex_span_tint_apply(
                (uint32_t)shade_blend(texel & 0x00FFFFFFu, shade), tint);
            pixel_buffer[i] = (uint32_t)(alpha == 0xFF
                                             ? lit
                                             : alpha_blend(alpha, (int)pixel_buffer[i], lit));
        }

        u_scan += step_u;
        v_scan += step_v;
    }
}

/**
 * A detail map only ever DARKENS: its brightest texel is the identity and
 * everything below it shades the surface down.
 *
 * The obvious alternative - a neutral midpoint with gain either side, so a
 * bright texel brightens - was tried and is wrong twice over. The base is the
 * palette's own lightness ramp, which already washes toward white at the top,
 * so any gain above 1 clamps whole regions to flat white; and the ramp is a
 * curve, so multiplying it by a second curve steepens the gradient against the
 * untextured faces beside it. Bounded at 1 the kernel can only ever remove
 * light, which is what a detail map is for and what keeps the shading of a
 * textured face agreeing with its untextured neighbours.
 *
 * The bake normalises each material to its own peak, so "brightest texel" means
 * the material's own maximum rather than whatever level it happened to bake at.
 */
#define TEX_SPAN_DETAIL_IDENTITY 255

/**
 * 8-pixel span for an HD program used as a DETAIL MAP over the face's own
 * colour.
 *
 * The other three kernels all treat the texture as the surface: they write some
 * function of the texel and the shade, and where the texel is absent they write
 * nothing. That is right for a diffuse map and for a mask, and wrong for the
 * bulk of an imported RS727 lane, which is neither - those are programs that
 * modulated something else in the source renderer. Rendered as a surface they
 * are blown-out white; skipped, the model loses all its detail; blended against
 * the framebuffer they show whatever the painter happened to draw first, which
 * on this lane is the striping.
 *
 * So this one reconstructs what the face would have been WITHOUT the texture -
 * its own chroma at the per-pixel lightness the raster is already carrying,
 * which is exactly the flat/gouraud fallback colour - and lets the texel scale
 * it about a neutral midpoint. Consequences worth being explicit about:
 *
 *   - every pixel is written, so the face is opaque. No holes, and no
 *     dependence on what was drawn underneath: the same face renders the same
 *     whatever the sort does with it.
 *   - a neutral texel is the identity. A material that bakes to nothing useful
 *     degrades to the flat colour the lane already falls back to, rather than
 *     to white.
 *   - it cannot brighten past 2x, which is the clamp below.
 */
static inline void
raster_linear_detail_lerp8_v3(
    uint32_t* RESTRICT pixel_buffer,
    const uint32_t* RESTRICT texels,
    int u_scan,
    int v_scan,
    int step_u,
    int step_v,
    int texture_shift,
    int u_mask,
    int v_mask,
    int shade,
    const struct TexSpanTint* RESTRICT tint)
{
    /* The untextured face colour at this shade, once per block: the chroma is
     * constant across the face and the shade across these 8 pixels. */
    uint32_t const base = tex_span_detail_base(tint, shade);

    for( int i = 0; i < 8; i++ )
    {
        int u = (u_scan >> texture_shift) & u_mask;
        int v = v_scan & v_mask;
        uint32_t texel = texels[u + v];

        /* Detail is a scalar: these programs carry no colour of their own, and
         * green is the cheapest stand-in for luma that is already isolated.
         * 255 maps to 256 so the brightest texel is the exact identity. */
        uint32_t level = (texel >> 8) & 0xFFu;
        uint32_t scale = (level * 256u + 127u) / TEX_SPAN_DETAIL_IDENTITY;
        uint32_t r = (((base >> 16) & 0xFFu) * scale) >> 8;
        uint32_t g = (((base >> 8) & 0xFFu) * scale) >> 8;
        uint32_t b = ((base & 0xFFu) * scale) >> 8;

        pixel_buffer[i] = (r << 16) | (g << 8) | b;

        u_scan += step_u;
        v_scan += step_v;
    }
}

/**
 * Per-pixel tail/fallback twin of the above, for a partial block or one whose
 * uv gradient the linear fit cannot represent.
 *
 * `tint` is NULL for a plain alpha texture and the packed per-face tint for a
 * modulated one. This is the cold path - the branch costs nothing measurable
 * here, which is why it is one function rather than the two the 8-pixel kernel
 * is split into.
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
    int texture_shift,
    const struct TexSpanTint* RESTRICT tint)
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
            if( tint && tint->detail )
            {
                /* Opaque by construction - see raster_linear_detail_lerp8_v3. */
                uint32_t base = tex_span_detail_base(tint, shade);
                uint32_t scale =
                    ((((texel >> 8) & 0xFFu) * 256u) + 127u) / TEX_SPAN_DETAIL_IDENTITY;
                uint32_t r = (((base >> 16) & 0xFFu) * scale) >> 8;
                uint32_t g = (((base >> 8) & 0xFFu) * scale) >> 8;
                uint32_t b = ((base & 0xFFu) * scale) >> 8;
                pixel_buffer[offset + i] = (int)((r << 16) | (g << 8) | b);
            }
            else if( alpha != 0 )
            {
                int lit = shade_blend(texel & 0x00FFFFFFu, shade);
                if( tint )
                    lit = tex_span_tint_apply((uint32_t)lit, tint);
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
 * Perspective scanline for an alpha-blended texture, modulated by the face's
 * colour when `tint` is non-NULL.
 *
 * Structurally identical to its opaque and transparent twins in the ISA span
 * files - the same block walk, the same uv rules, the same per-pixel fallback -
 * differing only in the kernel it hands each block to. It lives here rather
 * than being copied five times because that walk contains no intrinsics; only
 * the 8-pixel kernel is a candidate for vectorising, and this one is scalar for
 * now.
 *
 * The tint selects between two 8-pixel kernels rather than being passed into
 * one: the modulate is three multiplies a pixel, and a plain alpha texture
 * should not pay them to multiply by white.
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
    int texture_width,
    const struct TexSpanTint* RESTRICT tint)
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
                if( tint && tint->detail )
                    raster_linear_detail_lerp8_v3(
                        (uint32_t*)&pixel_buffer[offset],
                        (const uint32_t*)texels,
                        cur_u << texture_shift,
                        tex_span_v_scan_start(cur_v, texture_width, texture_shift),
                        (nxt_u - cur_u) << (texture_shift - 3),
                        (nxt_v - cur_v) << (texture_shift - 3),
                        texture_shift,
                        u_mask,
                        v_mask,
                        shade8bit_ish8 >> 8,
                        tint);
                else if( tint )
                    raster_linear_alpha_modulate_lerp8_v3(
                        (uint32_t*)&pixel_buffer[offset],
                        (const uint32_t*)texels,
                        cur_u << texture_shift,
                        tex_span_v_scan_start(cur_v, texture_width, texture_shift),
                        (nxt_u - cur_u) << (texture_shift - 3),
                        (nxt_v - cur_v) << (texture_shift - 3),
                        texture_shift,
                        u_mask,
                        v_mask,
                        shade8bit_ish8 >> 8,
                        tint);
                else
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
                    shade8bit_ish8 >> 8, texture_width, texture_shift, tint);
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
            shade8bit_ish8 >> 8, texture_width, texture_shift, tint);
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
