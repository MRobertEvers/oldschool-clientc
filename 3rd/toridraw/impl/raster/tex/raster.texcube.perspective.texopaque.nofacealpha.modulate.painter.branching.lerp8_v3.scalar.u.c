#ifndef TEXCUBE_PERSP_TEXOPAQUE_MODULATE_BRANCHING_LERP8_V3_U_C
#define TEXCUBE_PERSP_TEXOPAQUE_MODULATE_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/texmap_common.h"
#include "impl/raster/span/span.tex.gates.u.c"

/**
 * texcube.persp.texopaque.modulate.branching.lerp8_v3
 *
 * Projection: cube — the triangle's own normal picks one of six faces, then the
 * projection is flat. Affine over a face, and the only one of the three
 * that needs no seam fixup, because a triangle lands wholly on one face.
 *
 * Unlike `texplane`, the face carries a *mapping* rather than a projector, so
 * this kernel computes uv per vertex itself — through the arctangent table, not
 * libm — and interpolates it perspective-correctly. See texmap_common.h.
 *
 * Gate: every texel covers the pixel; the texture has no holes.
 *
 * modulate: the face's own colour tints the shaded texel, for the greyscale
 *   detail maps whose RGB is not the surface colour.
 *
 * Addressing (repeat or clamp, per axis) is not a variant axis — it rides the
 * sampler as data.
 */

#define TMAP_FN raster_texcube_persp_texopaque_modulate_branching_lerp8_v3
#define TMAP_SPAN draw_texture_scanline_texopaque_modulate_branching_lerp8_v3_ordered
#define TMAP_KIND 2
#include "graphics/raster/texture/texmap.persp.branching.lerp8_v3_tmpl.inc"

#endif
