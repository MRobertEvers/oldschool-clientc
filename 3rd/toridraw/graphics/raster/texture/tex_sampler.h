#ifndef TEX_SAMPLER_H
#define TEX_SAMPLER_H

#include "graphics/alpha.h"
#include "graphics/clamp.h"
#include "graphics/dash_restrict.h"
#include "graphics/shade.h"

#include <stdint.h>

/*
 * Everything a textured span needs that is constant across the triangle.
 *
 * ## Why a struct
 *
 * The `branching` texture kernels grew one argument per capability, and the
 * capabilities multiply: three texel gates, per-face alpha, a face-colour tint,
 * and per-axis addressing. Threaded positionally that is a 30-argument call and
 * a new one every time an axis is added. One pointer instead keeps the call
 * stable, and — more usefully — keeps the *derived* values (shift, masks) in one
 * place so a span cannot recompute them differently from its triangle.
 *
 * ## The alpha convention, once
 *
 * Every alpha here is a SOURCE weight: 0xFF draws the source outright, 0 leaves
 * the destination. That is alpha_blend()'s convention. It is the inverse of a
 * model's raw `face_alphas` byte, which the caller flips (`0xFF - a`) first.
 *
 * ## Texel alpha vs the colour key
 *
 * These are different mechanisms and they compose rather than overlap:
 *
 *   - the KEY (`textrans`) is one bit: RGB == 0 means "not drawn at all". The
 *     destination is left exactly, which is not the same as compositing at
 *     alpha 0 — alpha_blend(0, dst, src) still rounds dst down.
 *   - TEXEL ALPHA (`texalpha`) is the frame's own 0-255 alpha byte, which a
 *     procedural (RS727) texture actually carries. A colour key cannot express
 *     it; thresholding a ramp through one invents hard holes the source never
 *     had, which is the documented QBD "neck stripe" failure.
 */
struct ToriDraw_TexSampler
{
    /** ARGB8888. Alpha in the high byte, read only by the `texalpha` gate. */
    const int* RESTRICT texels;
    /** 64 or 128. */
    int width;
    /** log2(width) — 6 or 7. */
    int shift;
    /** Repeat masks: u is masked post-shift, v is masked pre-shift (row-major). */
    int u_mask;
    int v_mask;
    /** Non-zero clamps that axis to [0, width-1] instead of wrapping it.
     *  Three of the QBD's fifteen materials need it: two are repeat_s only,
     *  one repeats on neither axis.
     *
     *  Both are honoured on both span paths. That is worth stating because it
     *  was once true of `t` only: `s` was clamped whatever this said, which made
     *  a repeating material sample one column for every coordinate past the
     *  edge — see tex_span_u_quotient_mode. */
    int clamp_s;
    int clamp_t;
    /** Source weight for the whole face, 0-255. Read only by `facealpha`. */
    int face_alpha;
    /** Per-channel tint, 0-256, where 256 is the exact identity. Read only by
     *  `modulate`. 256 rather than 255 is deliberate: `(c * 256) >> 8 == c`, so
     *  a white tint changes nothing at all, which is what makes it safe to route
     *  a stock face through a modulate kernel in a test. */
    int tint_r;
    int tint_g;
    int tint_b;
};

/**
 * Fill the derived fields. `width` must be 64 or 128.
 *
 * The addressing defaults are the SD spans' own — u clamped, v wrapped — not
 * "repeat on both". A caller with a material overwrites them; a caller without
 * one wants whatever the plain kernels do, because a sampler kernel and its
 * plain twin drawing the same triangle differently is the failure the matrix
 * test exists to catch, and the two disagree only on u.
 */
static inline void
ToriDraw_TexSamplerInit(
    struct ToriDraw_TexSampler* s,
    const int* texels,
    int width)
{
    s->texels = texels;
    s->width = width;
    s->shift = (width == 128) ? 7 : 6;
    s->u_mask = width - 1;
    s->v_mask = (width == 128) ? 0x3F80 : 0x0FC0;
    s->clamp_s = 1;
    s->clamp_t = 0;
    s->face_alpha = 0xFF;
    s->tint_r = 256;
    s->tint_g = 256;
    s->tint_b = 256;
}

/**
 * Texel index from an 8.8-ish u scan and a pre-shifted v scan.
 *
 * The repeat path is the existing spans' arithmetic unchanged, so a face that
 * does not clamp samples exactly what it did before. The clamp path has to undo
 * the pre-shift on v to bound the row, which is why it cannot just swap masks.
 */
static inline int
tex_sampler_index(
    const struct ToriDraw_TexSampler* s,
    int u_scan,
    int v_scan)
{
    int u;
    int v;

    if( s->clamp_s )
        u = clamp(u_scan >> s->shift, 0, s->width - 1);
    else
        u = (u_scan >> s->shift) & s->u_mask;

    if( s->clamp_t )
        v = clamp(v_scan >> s->shift, 0, s->width - 1) << s->shift;
    else
        v = v_scan & s->v_mask;

    return u + v;
}

/**
 * Exact `a * b / 255` for two 0-255 weights, without a divide.
 *
 * Plain `(a * b) >> 8` is off by one at the top — 255*255>>8 is 254 — which
 * turns "fully opaque texel on a fully opaque face" into a blend, and a blend is
 * not the identity. This form rounds correctly at both ends.
 */
static inline int
tex_sampler_mul255(int a, int b)
{
    int t = a * b + 128;
    return (t + (t >> 8)) >> 8;
}

/** Per-channel tint. 256 is the exact identity, see the field comment. */
/**
 * Shade and tint in ONE multiply, per channel.
 *
 * The obvious composition — shade the texel, then tint the result — rounds
 * twice, and the second step multiplies the first step's rounding error along
 * with the colour. A tint above unity (which a detail map normalised on its own
 * mean routinely is) therefore amplifies the quantisation of an already
 * 8-bit-truncated value, and a smooth gradient comes apart into steps. That is
 * visible as streaking, and only on modulated faces, because only they amplify.
 *
 * Folding the two factors into a single shift keeps the full precision of the
 * product and rounds once. `shade` is 0..256 and the tint is 0..~1024, so the
 * widest product is 255 * 256 * 1024, well inside int32.
 */
static inline int
tex_sampler_shade_tint(const struct ToriDraw_TexSampler* s, int rgb, int shade)
{
    int r = (((rgb >> 16) & 0xFF) * shade * s->tint_r) >> 16;
    int g = (((rgb >> 8) & 0xFF) * shade * s->tint_g) >> 16;
    int b = ((rgb & 0xFF) * shade * s->tint_b) >> 16;

    if( r > 0xFF )
        r = 0xFF;
    if( g > 0xFF )
        g = 0xFF;
    if( b > 0xFF )
        b = 0xFF;

    return (r << 16) | (g << 8) | b;
}

static inline int
tex_sampler_tint(const struct ToriDraw_TexSampler* s, int rgb)
{
    int r = (((rgb >> 16) & 0xFF) * s->tint_r) >> 8;
    int g = (((rgb >> 8) & 0xFF) * s->tint_g) >> 8;
    int b = ((rgb & 0xFF) * s->tint_b) >> 8;

    if( r > 0xFF )
        r = 0xFF;
    if( g > 0xFF )
        g = 0xFF;
    if( b > 0xFF )
        b = 0xFF;

    return (r << 16) | (g << 8) | b;
}

#endif
