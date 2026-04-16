#ifndef TEXTURE_BLEND_BRANCHING_V3_C
#define TEXTURE_BLEND_BRANCHING_V3_C

#include "clamp.h"
#include "projection.u.c"
#include "raster/texture/span/tex.span.u.c"

#include <stdint.h>

/* draw_texture_scanline_*_blend_ordered_branching_lerp8_v3 are provided by tex.span.* (included above). */

#include "raster/texture/texshadeblend.persp.texopaque.branching.lerp8_v3.u.c"
#include "raster/texture/texshadeblend.persp.textrans.branching.lerp8_v3.u.c"

#endif
