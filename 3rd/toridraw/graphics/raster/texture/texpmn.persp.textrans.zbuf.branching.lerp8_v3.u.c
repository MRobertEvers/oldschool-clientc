#ifndef TEXPMN_PERSP_TEXTRANS_ZBUF_BRANCHING_LERP8_V3_U_C
#define TEXPMN_PERSP_TEXTRANS_ZBUF_BRANCHING_LERP8_V3_U_C

#include "graphics/dash_restrict.h"
#include "graphics/raster/texture/tex_sampler.h"
#include "graphics/raster/texture/texmap_common.h"
#include "graphics/raster/texture/span/tex.span.gates.u.c"
#include "graphics/raster/zbuffer/zbuf_plane.h"

/**
 * texpmn.persp.textrans.zbuf.branching.lerp8_v3
 *
 * Projection: the P/M/N plane frame — render type 0 as the HD reference draws
 * it. Each vertex is projected onto the frame's plane along the plane's normal
 * for a fixed, view-independent uv, then interpolated perspective-correctly.
 * NOT the `texplane` eye-ray walk: that is only right when the face lies in the
 * frame's plane, and HD content routinely puts the frame several edge lengths
 * away. See toridraw_texmap_project_plane in texmap_common.h.
 *
 * Gate: RGB 0 is a hole — the colour key. The pixel under a keyed texel is
 * left exactly as it was, not composited at alpha 0.
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

#define TMAP_FN raster_texpmn_persp_textrans_zbuf_branching_lerp8_v3
#define TMAP_SPAN draw_texture_scanline_textrans_zbuf_branching_lerp8_v3_ordered
#define TMAP_KIND 0
#define TMAP_ZBUF 1
#include "graphics/raster/texture/texmap.persp.branching.lerp8_v3_tmpl.inc"

#endif
