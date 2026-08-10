#ifndef TORIDRAW_RASTER_SCANLINE_TEXTURE_U_C
#define TORIDRAW_RASTER_SCANLINE_TEXTURE_U_C

/**
 * Textured triangles, `scanline` walk.
 *
 * Sixteen variants generated from scanline.texture_tmpl.inc:
 *
 *   texshade{flat,blend} . {persp,affine} . {texopaque,textrans} [. facealpha]
 *                        . scanline . lerp8
 *
 * `texshadeflat` takes one face shade, `texshadeblend` interpolates a shade
 * plane across the triangle. `texopaque` writes every texel, `textrans` skips
 * texel 0. `facealpha` additionally blends the shaded texel into the
 * destination with a per-face alpha - the combination the `branching` family
 * has no path for, so alpha-blended textured geometry previously had to be
 * drawn opaque.
 *
 * The 8-pixel inner kernels are the existing per-ISA SIMD spans from
 * raster/texture/span/tex.span.u.c, except in the facealpha variants, which
 * need a read-modify-write.
 */

#include "graphics/alpha.h"
#include "graphics/clamp.h"
#include "graphics/dash_restrict.h"
#include "graphics/projection.u.c"
#include "graphics/raster/scanline/scanline_common.h"
#include "graphics/raster/texture/span/tex.span.u.c"
#include "graphics/shade.h"

#include <stdint.h>

/* ------------------------------------- perspective / flat shade / opaque */

#define SLT_FN raster_texshadeflat_persp_texopaque_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeflat_persp_texopaque
#define SLT_PERSP 1
#define SLT_TRANS 0
#define SLT_SHADE_INTERP 0
#define SLT_FACE_ALPHA 0
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeflat_persp_textrans_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeflat_persp_textrans
#define SLT_PERSP 1
#define SLT_TRANS 1
#define SLT_SHADE_INTERP 0
#define SLT_FACE_ALPHA 0
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeflat_persp_texopaque_facealpha_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeflat_persp_texopaque_facealpha
#define SLT_PERSP 1
#define SLT_TRANS 0
#define SLT_SHADE_INTERP 0
#define SLT_FACE_ALPHA 1
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeflat_persp_textrans_facealpha_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeflat_persp_textrans_facealpha
#define SLT_PERSP 1
#define SLT_TRANS 1
#define SLT_SHADE_INTERP 0
#define SLT_FACE_ALPHA 1
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

/* ------------------------------------ perspective / blend shade / opaque */

#define SLT_FN raster_texshadeblend_persp_texopaque_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeblend_persp_texopaque
#define SLT_PERSP 1
#define SLT_TRANS 0
#define SLT_SHADE_INTERP 1
#define SLT_FACE_ALPHA 0
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeblend_persp_textrans_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeblend_persp_textrans
#define SLT_PERSP 1
#define SLT_TRANS 1
#define SLT_SHADE_INTERP 1
#define SLT_FACE_ALPHA 0
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeblend_persp_texopaque_facealpha_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeblend_persp_texopaque_facealpha
#define SLT_PERSP 1
#define SLT_TRANS 0
#define SLT_SHADE_INTERP 1
#define SLT_FACE_ALPHA 1
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeblend_persp_textrans_facealpha_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeblend_persp_textrans_facealpha
#define SLT_PERSP 1
#define SLT_TRANS 1
#define SLT_SHADE_INTERP 1
#define SLT_FACE_ALPHA 1
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

/* ----------------------------------------- affine / flat shade / opaque */

#define SLT_FN raster_texshadeflat_affine_texopaque_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeflat_affine_texopaque
#define SLT_PERSP 0
#define SLT_TRANS 0
#define SLT_SHADE_INTERP 0
#define SLT_FACE_ALPHA 0
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeflat_affine_textrans_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeflat_affine_textrans
#define SLT_PERSP 0
#define SLT_TRANS 1
#define SLT_SHADE_INTERP 0
#define SLT_FACE_ALPHA 0
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeflat_affine_texopaque_facealpha_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeflat_affine_texopaque_facealpha
#define SLT_PERSP 0
#define SLT_TRANS 0
#define SLT_SHADE_INTERP 0
#define SLT_FACE_ALPHA 1
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeflat_affine_textrans_facealpha_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeflat_affine_textrans_facealpha
#define SLT_PERSP 0
#define SLT_TRANS 1
#define SLT_SHADE_INTERP 0
#define SLT_FACE_ALPHA 1
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

/* ---------------------------------------- affine / blend shade / opaque */

#define SLT_FN raster_texshadeblend_affine_texopaque_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeblend_affine_texopaque
#define SLT_PERSP 0
#define SLT_TRANS 0
#define SLT_SHADE_INTERP 1
#define SLT_FACE_ALPHA 0
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeblend_affine_textrans_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeblend_affine_textrans
#define SLT_PERSP 0
#define SLT_TRANS 1
#define SLT_SHADE_INTERP 1
#define SLT_FACE_ALPHA 0
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeblend_affine_texopaque_facealpha_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeblend_affine_texopaque_facealpha
#define SLT_PERSP 0
#define SLT_TRANS 0
#define SLT_SHADE_INTERP 1
#define SLT_FACE_ALPHA 1
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#define SLT_FN raster_texshadeblend_affine_textrans_facealpha_scanline_lerp8
#define SLT_SPAN_FN scanline_span_texshadeblend_affine_textrans_facealpha
#define SLT_PERSP 0
#define SLT_TRANS 1
#define SLT_SHADE_INTERP 1
#define SLT_FACE_ALPHA 1
#include "graphics/raster/scanline/scanline.texture_tmpl.inc"

#endif
