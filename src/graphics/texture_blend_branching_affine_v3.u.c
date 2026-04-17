#ifndef TEXTURE_BLEND_BRANCHING_AFFINE_V3_C
#define TEXTURE_BLEND_BRANCHING_AFFINE_V3_C

#include "projection.u.c"
#include "raster/texture/span/tex.span.u.c"

#include <stdint.h>

/* draw_texture_scanline_*_blend_affine_branching_lerp8_ish16_ordered are provided by tex.span.* (included above). */

#include "raster/texture/texshadeblend.affine.texopaque.branching.lerp8_v3.u.c"
#include "raster/texture/texshadeblend.affine.textrans.branching.lerp8_v3.u.c"

#endif
