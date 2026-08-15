#ifndef TEXPLANE_PERSP_TEXTRANS_BRANCHING_LERP8_V3_U_C
#define TEXPLANE_PERSP_TEXTRANS_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/span/tex.span.gates.u.c"

/**
 * texplane.persp.textrans.branching.lerp8_v3
 *
 * `texplane` is the projector family: the face names three vertices (p, m, n)
 * whose positions ARE the texture plane, and the kernel walks that plane. It is
 * render type 0.
 *
 * Gate: RGB 0 is a colour key - a hole - left exactly alone.
 *
 * ## Why this exists next to raster_texshadeblend_persp_textrans_branching_lerp8_v3
 *
 * That one is the same gate on the same projector, and it is faster — it reaches
 * the per-ISA SIMD spans, which can be wide because they only ever write the
 * frame buffer. Prefer it.
 *
 * What it cannot do is **addressing**. It takes `texels` and `texture_width`
 * directly and hardcodes repeat through `u_mask` / `v_mask`; there is nowhere to
 * say "clamp this axis". Three of the QBD's fifteen materials need exactly that
 * (two are repeat_s only, one repeats on neither axis), so the matrix would have
 * a hole at its two plainest points without this. It is the sampler-shaped twin,
 * scalar, and it is bit-exact against the SIMD one on RGB whenever the sampler
 * is left at repeat — which the test asserts, because that equality is what says
 * the two paths have not drifted.
 */

#define TSFA_FN raster_texplane_persp_textrans_branching_lerp8_v3
#define TSFA_ORDERED raster_texplane_persp_textrans_branching_lerp8_v3_ordered
#define TSFA_SPAN draw_texture_scanline_textrans_branching_lerp8_v3_ordered
#include "graphics/raster/texture/texplane.persp.branching.lerp8_v3_tmpl.inc"

#endif
