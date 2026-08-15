#ifndef TEXSHADEBLEND_PERSP_TEXOPAQUE_FACEALPHA_BRANCHING_LERP8_V3_U_C
#define TEXSHADEBLEND_PERSP_TEXOPAQUE_FACEALPHA_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/span/tex.span.facealpha.u.c"

/**
 * texshadeblend.persp.texopaque.facealpha.branching.lerp8_v3
 *
 * A textured face whose texture has no transparent texels, composited into the
 * frame with a per-face alpha. Every texel is drawn, so coverage is the whole
 * triangle and the alpha is the only thing deciding how much of the destination
 * survives.
 *
 * The `branching` family previously had no path for alpha on a textured face at
 * all: the model raster passes textured faces straight to the plain kernels,
 * which overwrite, so an alpha-blended textured face drew fully opaque. This is
 * the missing gate. (The `scanline` family already had its own facealpha
 * variants - see scanline.texture.u.c - but that family is off by default.)
 */

#define TSFA_FN raster_texshadeblend_persp_texopaque_facealpha_branching_lerp8_v3
#define TSFA_ORDERED raster_texshadeblend_persp_texopaque_facealpha_branching_lerp8_v3_ordered
#define TSFA_SPAN draw_texture_scanline_opaque_blend_facealpha_branching_lerp8_v3_ordered
#include "graphics/raster/texture/texshadeblend.persp.facealpha.branching.lerp8_v3.inc"

#endif
