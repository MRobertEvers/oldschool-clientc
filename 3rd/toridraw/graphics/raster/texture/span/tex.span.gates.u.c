#ifndef TEX_SPAN_TEX2_U_C
#define TEX_SPAN_TEX2_U_C

/*
 * The extended perspective texture spans: ten points of the capability matrix
 *
 *     gate {texopaque, textrans, texalpha} x facealpha {0,1} x modulate {0,1}
 *
 * minus the two the per-ISA SIMD spans already cover (texopaque and textrans
 * with neither facealpha nor modulate), which are untouched and keep their
 * vector implementations.
 *
 * All ten are generated from one template so the uv walk cannot drift between
 * them; see tex.span.gates_tmpl.inc for what is shared and what is not. Naming
 * mirrors the triangle variants that call them, in file-name order:
 *
 *     draw_texture_scanline_<gate>[_facealpha][_modulate]_branching_lerp8_v3_ordered
 */

#include "graphics/alpha.h"
#include "graphics/clamp.h"
#include "graphics/dash_restrict.h"
#include "graphics/shade.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/span/tex.span_uv.h"

#include <stdint.h>

/* Token paste for the per-variant block8/exact helpers. Two levels, so the
 * argument is expanded before it is pasted. */
#define TS2_CAT_(a, b) a##b
#define TS2_CAT(a, b) TS2_CAT_(a, b)

/* --------------------------------------------------- texopaque (gate 0) */

#define TS2_SPAN_FN draw_texture_scanline_texopaque_facealpha_branching_lerp8_v3_ordered
#define TS2_GATE 0
#define TS2_FACEALPHA 1
#define TS2_MODULATE 0
#include "graphics/raster/texture/span/tex.span.gates_tmpl.inc"

#define TS2_SPAN_FN draw_texture_scanline_texopaque_modulate_branching_lerp8_v3_ordered
#define TS2_GATE 0
#define TS2_FACEALPHA 0
#define TS2_MODULATE 1
#include "graphics/raster/texture/span/tex.span.gates_tmpl.inc"

#define TS2_SPAN_FN draw_texture_scanline_texopaque_facealpha_modulate_branching_lerp8_v3_ordered
#define TS2_GATE 0
#define TS2_FACEALPHA 1
#define TS2_MODULATE 1
#include "graphics/raster/texture/span/tex.span.gates_tmpl.inc"

/* ---------------------------------------------------- textrans (gate 1) */

#define TS2_SPAN_FN draw_texture_scanline_textrans_facealpha_branching_lerp8_v3_ordered
#define TS2_GATE 1
#define TS2_FACEALPHA 1
#define TS2_MODULATE 0
#include "graphics/raster/texture/span/tex.span.gates_tmpl.inc"

#define TS2_SPAN_FN draw_texture_scanline_textrans_modulate_branching_lerp8_v3_ordered
#define TS2_GATE 1
#define TS2_FACEALPHA 0
#define TS2_MODULATE 1
#include "graphics/raster/texture/span/tex.span.gates_tmpl.inc"

#define TS2_SPAN_FN draw_texture_scanline_textrans_facealpha_modulate_branching_lerp8_v3_ordered
#define TS2_GATE 1
#define TS2_FACEALPHA 1
#define TS2_MODULATE 1
#include "graphics/raster/texture/span/tex.span.gates_tmpl.inc"

/* ---------------------------------------------------- texalpha (gate 2) */

#define TS2_SPAN_FN draw_texture_scanline_texalpha_branching_lerp8_v3_ordered
#define TS2_GATE 2
#define TS2_FACEALPHA 0
#define TS2_MODULATE 0
#include "graphics/raster/texture/span/tex.span.gates_tmpl.inc"

#define TS2_SPAN_FN draw_texture_scanline_texalpha_facealpha_branching_lerp8_v3_ordered
#define TS2_GATE 2
#define TS2_FACEALPHA 1
#define TS2_MODULATE 0
#include "graphics/raster/texture/span/tex.span.gates_tmpl.inc"

#define TS2_SPAN_FN draw_texture_scanline_texalpha_modulate_branching_lerp8_v3_ordered
#define TS2_GATE 2
#define TS2_FACEALPHA 0
#define TS2_MODULATE 1
#include "graphics/raster/texture/span/tex.span.gates_tmpl.inc"

#define TS2_SPAN_FN draw_texture_scanline_texalpha_facealpha_modulate_branching_lerp8_v3_ordered
#define TS2_GATE 2
#define TS2_FACEALPHA 1
#define TS2_MODULATE 1
#include "graphics/raster/texture/span/tex.span.gates_tmpl.inc"

#endif
