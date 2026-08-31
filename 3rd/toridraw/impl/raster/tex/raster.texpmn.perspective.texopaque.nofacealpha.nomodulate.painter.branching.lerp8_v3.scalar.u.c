#ifndef TEXPMN_PERSP_TEXOPAQUE_BRANCHING_LERP8_V3_U_C
#define TEXPMN_PERSP_TEXOPAQUE_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/texmap_common.h"
#include "impl/raster/span/span.tex.gates.u.c"

/**
 * texpmn.persp.texopaque.branching.lerp8_v3
 *
 * Projection: the P/M/N plane frame — render type 0 as the HD reference draws
 * it. Each vertex is projected onto the frame's plane along the plane's normal
 * for a fixed, view-independent uv, then interpolated perspective-correctly.
 * NOT the `texplane` eye-ray walk: that is only right when the face lies in the
 * frame's plane, and HD content routinely puts the frame several edge lengths
 * away. See toridraw_texmap_project_plane in texmap_common.h.
 *
 * Gate: every texel covers the pixel; the texture has no holes.
 *
 * Addressing (repeat or clamp, per axis) is not a variant axis — it rides the
 * sampler as data.
 */

#define TMAP_FN raster_texpmn_persp_texopaque_branching_lerp8_v3
#define TMAP_SPAN draw_texture_scanline_texopaque_branching_lerp8_v3_ordered
#define TMAP_KIND 0
#include "graphics/raster/texture/texmap.persp.branching.lerp8_v3_tmpl.inc"

#endif
