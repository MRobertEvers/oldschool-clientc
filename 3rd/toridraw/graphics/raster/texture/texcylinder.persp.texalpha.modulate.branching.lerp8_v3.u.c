#ifndef TEXCYLINDER_PERSP_TEXALPHA_MODULATE_BRANCHING_LERP8_V3_U_C
#define TEXCYLINDER_PERSP_TEXALPHA_MODULATE_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/texmap_common.h"
#include "graphics/raster/texture/span/tex.span.gates.u.c"

/**
 * texcylinder.persp.texalpha.modulate.branching.lerp8_v3
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
 * Gate: the texel's own alpha byte is its coverage, 0-255. The gate a
 *   procedural RS727 material needs: nine of the QBD's fifteen have no
 *   fully-opaque texel anywhere, and a one-bit key cannot express a ramp.
 *
 * modulate: the face's own colour tints the shaded texel, for the greyscale
 *   detail maps whose RGB is not the surface colour.
 *
 * Addressing (repeat or clamp, per axis) is not a variant axis — it rides the
 * sampler as data.
 */

#define TMAP_FN raster_texcylinder_persp_texalpha_modulate_branching_lerp8_v3
#define TMAP_SPAN draw_texture_scanline_texalpha_modulate_branching_lerp8_v3_ordered
#define TMAP_KIND 1
#include "graphics/raster/texture/texmap.persp.branching.lerp8_v3_tmpl.inc"

#endif
