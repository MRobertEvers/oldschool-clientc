#ifndef TEXCUBE_PERSP_TEXALPHA_FACEALPHA_MODULATE_BRANCHING_LERP8_V3_U_C
#define TEXCUBE_PERSP_TEXALPHA_FACEALPHA_MODULATE_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/texmap_common.h"
#include "impl/raster/span/span.tex.gates.u.c"

/**
 * texcube.persp.texalpha.facealpha.modulate.branching.lerp8_v3
 *
 * Projection: cube — the triangle's own normal picks one of six faces, then the
 * projection is flat. Affine over a face, and the only one of the three
 * that needs no seam fixup, because a triangle lands wholly on one face.
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

#define TMAP_FN raster_texcube_persp_texalpha_facealpha_modulate_branching_lerp8_v3
#define TMAP_SPAN draw_texture_scanline_texalpha_facealpha_modulate_branching_lerp8_v3_ordered
#define TMAP_KIND 2
#include "graphics/raster/texture/texmap.persp.branching.lerp8_v3_tmpl.inc"

#endif
