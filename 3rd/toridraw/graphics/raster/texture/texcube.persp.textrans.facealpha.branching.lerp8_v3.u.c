#ifndef TEXCUBE_PERSP_TEXTRANS_FACEALPHA_BRANCHING_LERP8_V3_U_C
#define TEXCUBE_PERSP_TEXTRANS_FACEALPHA_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/texmap_common.h"
#include "graphics/raster/texture/span/tex.span.gates.u.c"

/**
 * texcube.persp.textrans.facealpha.branching.lerp8_v3
 *
 * Projection: cube — the triangle's own normal picks one of six faces, then the
 * projection is flat. Affine over a face, and the only one of the three
 * that needs no seam fixup, because a triangle lands wholly on one face.
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

#define TMAP_FN raster_texcube_persp_textrans_facealpha_branching_lerp8_v3
#define TMAP_SPAN draw_texture_scanline_textrans_facealpha_branching_lerp8_v3_ordered
#define TMAP_KIND 2
#include "graphics/raster/texture/texmap.persp.branching.lerp8_v3_tmpl.inc"

#endif
