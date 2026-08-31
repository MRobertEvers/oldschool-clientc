#ifndef TORIDRAW_RASTER_SCANLINE_U_C
#define TORIDRAW_RASTER_SCANLINE_U_C

/**
 * The `scanline` raster family.
 *
 * Include this before the triangles/ dispatchers; it brings in every variant:
 *
 *   flat.screen.{opaque,alpha}.scanline.s8
 *   gouraudhsllightness.screen.{opaque,alpha}.bary.scanline.s4
 *   texshade{flat,blend}.{persp,affine}.{texopaque,textrans}[.facealpha].scanline.lerp8
 *
 * See scanline_common.h for what the family does differently from `branching`
 * and `sort`.
 */

#include "graphics/raster/scanline/scanline_common.h"
#include "graphics/raster/scanline/scanline_select.h"

#include "impl/raster/span/span.solid.scanline.scalar.u.c"

#include "impl/raster/scanline/scanline.flat.u.c"
#include "impl/raster/scanline/scanline.gouraudhsllightness.u.c"

#include "impl/raster/scanline/scanline.texture.u.c"

#endif
