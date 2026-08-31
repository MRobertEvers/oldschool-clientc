#ifndef TEXCYLINDER_PERSP_TEXALPHA_FACEALPHA_MODULATE_ZBUF_BRANCHING_LERP8_V3_U_C
#define TEXCYLINDER_PERSP_TEXALPHA_FACEALPHA_MODULATE_ZBUF_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/texmap_common.h"
#include "impl/raster/span/span.tex.gates.u.c"
#include "graphics/raster/zbuffer/zbuf_plane.h"

/**
 * texcylinder.persp.texalpha.facealpha.modulate.zbuf.branching.lerp8_v3
 *
 * Projection: cylindrical — angle about the mapping axis, height along it.
 * Non-linear in the vertex (arctangent), so no plane can express it; the seam
 * fixup folds a vertex that wrapped the long way round back onto the short arc.
 *
 * Gate: the texel's own alpha byte is its coverage, 0-255. Tests depth and
 *   never writes it: a surface the destination shows through must not occlude
 *   what is drawn after it.
 *
 * facealpha: the face's own alpha weights the result on top of whatever the
 *   gate produced, and suppresses the depth write for the same reason the
 *   `texalpha` gate does.
 *
 * modulate: the face's own colour tints the shaded texel, for the greyscale
 *   detail maps whose RGB is not the surface colour.
 *
 * zbuf: every pixel is tested against the scene z-buffer before it is composed,
 *   and an opaque one is written back. The caller resets the buffer per model,
 *   so the test resolves a model against ITSELF — which is the point: this is
 *   the family ToriDraw_RenderHDZBuffered draws through, and that entry point
 *   does not sort faces or honour priorities at all. Depth decides, per pixel.
 *
 *   The uv walk is its plain twin's, block fit included. A depth-tested face
 *   therefore samples exactly the texels its plain twin sampled and differs only
 *   in which of them survive.
 *
 * Addressing (repeat or clamp, per axis) is not a variant axis — it rides the
 * sampler as data.
 *
 * Unlike `texplane`, the face carries a *mapping* rather than a projector, so
 * this kernel computes uv per vertex itself — through the arctangent table, not
 * libm — and interpolates it perspective-correctly. See texmap_common.h.
 */

#define TMAP_FN raster_texcylinder_persp_texalpha_facealpha_modulate_zbuf_branching_lerp8_v3
#define TMAP_SPAN draw_texture_scanline_texalpha_facealpha_modulate_zbuf_branching_lerp8_v3_ordered
#define TMAP_KIND 1
#define TMAP_ZBUF 1
#include "graphics/raster/texture/texmap.persp.branching.lerp8_v3_tmpl.inc"

#endif
