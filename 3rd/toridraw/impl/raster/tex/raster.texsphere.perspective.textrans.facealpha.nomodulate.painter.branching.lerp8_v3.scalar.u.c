#ifndef TEXSPHERE_PERSP_TEXTRANS_FACEALPHA_BRANCHING_LERP8_V3_U_C
#define TEXSPHERE_PERSP_TEXTRANS_FACEALPHA_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/texmap_common.h"
#include "impl/raster/span/span.tex.gates.u.c"

/**
 * texsphere.persp.textrans.facealpha.branching.lerp8_v3
 *
 * Projection: spherical — longitude and latitude about the mapping centre.
 * Non-linear in the vertex (arctangent and arcsine); the seam fixup folds
 * against a whole turn.
 *
 * Unlike `texplane`, the face carries a *mapping* rather than a projector, so
 * this kernel computes uv per vertex itself — through the arctangent table, not
 * libm — and interpolates it perspective-correctly. See texmap_common.h.
 *
 * Gate: RGB 0 is a colour key - a hole - left exactly alone.
 *
 * facealpha: the face's own alpha weights the result on top of the gate's;
 *   the two compose multiplicatively, in one pass.
 *
 * Addressing (repeat or clamp, per axis) is not a variant axis — it rides the
 * sampler as data.
 */

#define TMAP_FN raster_texsphere_persp_textrans_facealpha_branching_lerp8_v3
#define TMAP_SPAN draw_texture_scanline_textrans_facealpha_branching_lerp8_v3_ordered
#define TMAP_KIND 3
#include "graphics/raster/texture/texmap.persp.branching.lerp8_v3_tmpl.inc"

#endif
