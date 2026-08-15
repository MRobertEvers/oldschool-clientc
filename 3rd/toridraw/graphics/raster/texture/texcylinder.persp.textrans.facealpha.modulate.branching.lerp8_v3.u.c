#ifndef TEXCYLINDER_PERSP_TEXTRANS_FACEALPHA_MODULATE_BRANCHING_LERP8_V3_U_C
#define TEXCYLINDER_PERSP_TEXTRANS_FACEALPHA_MODULATE_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/texmap_common.h"
#include "graphics/raster/texture/span/tex.span.gates.u.c"

/**
 * texcylinder.persp.textrans.facealpha.modulate.branching.lerp8_v3
 *
 * Projection: cylindrical — angle about the mapping axis, height along it.
 * Non-linear in the vertex (arctangent), so no plane can express it; the
 * seam fixup folds a vertex that wrapped the long way round back onto the
 * short arc.
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
 * modulate: the face's own colour tints the shaded texel, for the greyscale
 *   detail maps whose RGB is not the surface colour.
 *
 * Addressing (repeat or clamp, per axis) is not a variant axis — it rides the
 * sampler as data.
 */

#define TMAP_FN raster_texcylinder_persp_textrans_facealpha_modulate_branching_lerp8_v3
#define TMAP_SPAN draw_texture_scanline_textrans_facealpha_modulate_branching_lerp8_v3_ordered
#define TMAP_KIND 1
#include "graphics/raster/texture/texmap.persp.branching.lerp8_v3_tmpl.inc"

#endif
