#ifndef TEXPLANE_PERSP_TEXTRANS_FACEALPHA_ZBUF_BRANCHING_LERP8_V3_U_C
#define TEXPLANE_PERSP_TEXTRANS_FACEALPHA_ZBUF_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/span/tex.span.gates.u.c"
#include "graphics/raster/zbuffer/zbuf_plane.h"

/**
 * texplane.persp.textrans.facealpha.zbuf.branching.lerp8_v3
 *
 * `texplane` is the projector family: the face names three vertices (p, m, n)
 * whose positions ARE the texture plane, and the kernel walks that plane. It is
 * render type 0.
 *
 * Gate: RGB 0 is a colour key and is not drawn. Still WRITES depth where it
 *   does draw — a keyed pixel is either fully opaque or absent, never partial.
 *
 * facealpha: the face's own alpha weights the result on top of whatever the
 *   gate produced, and suppresses the depth write for the same reason the
 *   `texalpha` gate does.
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
 */

#define TSFA_FN raster_texplane_persp_textrans_facealpha_zbuf_branching_lerp8_v3
#define TSFA_ORDERED raster_texplane_persp_textrans_facealpha_zbuf_branching_lerp8_v3_ordered
#define TSFA_SPAN draw_texture_scanline_textrans_facealpha_zbuf_branching_lerp8_v3_ordered
#define TSFA_ZBUF 1
#include "graphics/raster/texture/texplane.persp.branching.lerp8_v3_tmpl.inc"

#endif
