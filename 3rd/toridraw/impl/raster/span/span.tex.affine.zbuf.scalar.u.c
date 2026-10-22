#ifndef TEX_SPAN_AFFINE_ZBUF_U_C
#define TEX_SPAN_AFFINE_ZBUF_U_C

/*
 * The depth-tested affine texture spans: texopaque and textrans, the two gates
 * the SD affine kernels have. Both from tex.span.affine_zbuf_tmpl.inc, so the
 * pair cannot drift apart; see the template for why they stay scalar and what
 * they copy from the painter's affine span.
 *
 *     draw_texture_scanline_<gate>_affine_zbuf_branching_lerp8_v3_ordered
 */

#include "graphics/clamp.h"
#include "graphics/dash_restrict.h"
#include "graphics/int_wrap.h"
#include "graphics/shade.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/zdepth.h"

#define TAZ_SPAN_FN draw_texture_scanline_texopaque_affine_zbuf_branching_lerp8_v3_ordered
#define TAZ_GATE 0
#include "graphics/raster/texture/span/tex.span.affine_zbuf_tmpl.inc"

#define TAZ_SPAN_FN draw_texture_scanline_textrans_affine_zbuf_branching_lerp8_v3_ordered
#define TAZ_GATE 1
#include "graphics/raster/texture/span/tex.span.affine_zbuf_tmpl.inc"

#endif
