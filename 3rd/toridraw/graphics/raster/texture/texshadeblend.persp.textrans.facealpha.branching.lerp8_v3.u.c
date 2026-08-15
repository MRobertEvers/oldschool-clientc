#ifndef TEXSHADEBLEND_PERSP_TEXTRANS_FACEALPHA_BRANCHING_LERP8_V3_U_C
#define TEXSHADEBLEND_PERSP_TEXTRANS_FACEALPHA_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/span/tex.span.facealpha.u.c"

/**
 * texshadeblend.persp.textrans.facealpha.branching.lerp8_v3
 *
 * A textured face whose texture is colour-keyed - texel 0 is a hole - composited
 * with a per-face alpha. The two gates compose rather than overlap: the key
 * decides *whether* a pixel is touched, the alpha decides *how much* of the
 * destination survives where it is. A texel-0 pixel is skipped outright and
 * keeps the destination exactly, which is not the same as blending it at alpha
 * 0 - that would still read and rewrite the pixel, and any later gate that
 * cares about untouched pixels would see it as covered.
 */

#define TSFA_FN raster_texshadeblend_persp_textrans_facealpha_branching_lerp8_v3
#define TSFA_ORDERED raster_texshadeblend_persp_textrans_facealpha_branching_lerp8_v3_ordered
#define TSFA_SPAN draw_texture_scanline_transparent_blend_facealpha_branching_lerp8_v3_ordered
#include "graphics/raster/texture/texshadeblend.persp.facealpha.branching.lerp8_v3.inc"

#endif
