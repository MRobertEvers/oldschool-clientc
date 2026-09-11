#ifndef TEXPLANE_PERSP_TEXOPAQUE_FACEALPHA_MODULATE_BRANCHING_LERP8_V3_U_C
#define TEXPLANE_PERSP_TEXOPAQUE_FACEALPHA_MODULATE_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "impl/raster/span/span.tex.gates.u.c"

/**
 * texplane.persp.texopaque.facealpha.modulate.branching.lerp8_v3
 *
 * `texplane` is the projector family: the face names three vertices (p, m, n)
 * whose positions ARE the texture plane, and the kernel walks that plane. It is
 * render type 0, and it is what every pre-existing textured kernel here does —
 * the `texcylinder`, `texcube` and `texsphere` families are the ones that carry
 * a mapping instead of a projector.
 *
 * Gate: every texel covers the pixel; the texture has no holes.
 *   A diffuse map with no transparency.
 *
 * facealpha: the face's own alpha weights the result on top of whatever the
 *   gate produced. The two compose multiplicatively, in one pass.
 *
 * modulate: the face's own colour tints the shaded texel. Twelve of the QBD's
 *   fifteen materials are greyscale detail maps whose RGB is not the surface
 *   colour, and SD's `texel x lightness` contract has no colour term, so drawn
 *   literally they render grey.
 *
 * Addressing (repeat or clamp, per axis) is not a variant axis — it rides the
 * sampler as data, because it changes an index computation rather than the
 * shape of the composite.
 */

#define TSFA_FN raster_texplane_persp_texopaque_facealpha_modulate_branching_lerp8_v3
#define TSFA_ORDERED raster_texplane_persp_texopaque_facealpha_modulate_branching_lerp8_v3_ordered
#define TSFA_SPAN draw_texture_scanline_texopaque_facealpha_modulate_branching_lerp8_v3_ordered
#include "graphics/raster/texture/texplane.persp.branching.lerp8_v3_tmpl.inc"

#endif
