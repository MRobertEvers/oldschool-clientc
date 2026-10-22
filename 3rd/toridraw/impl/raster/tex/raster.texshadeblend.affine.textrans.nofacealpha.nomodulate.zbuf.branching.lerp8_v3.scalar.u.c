#ifndef TEXSHADEBLEND_AFFINE_TEXTRANS_ZBUF_BRANCHING_LERP8_V3_U_C
#define TEXSHADEBLEND_AFFINE_TEXTRANS_ZBUF_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "impl/raster/span/span.tex.affine.zbuf.scalar.u.c"
#include "graphics/raster/zbuffer/zbuf_plane.h"

/**
 * texshadeblend.affine.textrans.zbuf.branching.lerp8_v3
 *
 * The depth twin of raster.texshadeblend.affine.textrans...painter.branching.
 * lerp8_v3, which is what the SD painter draws an affine-mapped face with.
 *
 * The affine and perspective triangle walks are the same code -- only the span
 * differs -- so this is the texplane template with the affine depth span
 * plugged in, rather than a third copy of the walk. The sampler carries the
 * texels and masks; the span reads nothing else off it.
 */

#define TSFA_FN raster_texshadeblend_affine_textrans_zbuf_branching_lerp8_v3
#define TSFA_ORDERED raster_texshadeblend_affine_textrans_zbuf_branching_lerp8_v3_ordered
#define TSFA_SPAN draw_texture_scanline_textrans_affine_zbuf_branching_lerp8_v3_ordered
#define TSFA_ZBUF 1
#include "graphics/raster/texture/texplane.persp.branching.lerp8_v3_tmpl.inc"

#endif
