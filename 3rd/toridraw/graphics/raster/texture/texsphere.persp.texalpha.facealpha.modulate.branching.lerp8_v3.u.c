#ifndef TEXSPHERE_PERSP_TEXALPHA_FACEALPHA_MODULATE_BRANCHING_LERP8_V3_U_C
#define TEXSPHERE_PERSP_TEXALPHA_FACEALPHA_MODULATE_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/texmap_common.h"
#include "graphics/raster/texture/span/tex.span.gates.u.c"

/**
 * texsphere.persp.texalpha.facealpha.modulate.branching.lerp8_v3
 *
 * Projection: spherical — longitude and latitude about the mapping centre.
 * Non-linear in the vertex (arctangent and arcsine); the seam fixup folds
 * against a whole turn.
 *
 * Unlike `texplane`, the face carries a *mapping* rather than a projector, so
 * this kernel computes uv per vertex itself — through the arctangent table, not
 * libm — and interpolates it perspective-correctly. See texmap_common.h.
 *
 * Gate: the texel's own alpha byte is its coverage, 0-255. The gate a
 *   procedural RS727 material needs: nine of the QBD's fifteen have no
 *   fully-opaque texel anywhere, and a one-bit key cannot express a ramp.
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

#define TMAP_FN raster_texsphere_persp_texalpha_facealpha_modulate_branching_lerp8_v3
#define TMAP_SPAN draw_texture_scanline_texalpha_facealpha_modulate_branching_lerp8_v3_ordered
#define TMAP_KIND 3
#include "graphics/raster/texture/texmap.persp.branching.lerp8_v3_tmpl.inc"

#endif
