#ifndef GOURAUDHSLLIGHTNESS_SCREEN_OPAQUE_ZBUF_BRANCHING_S4_U_C
#define GOURAUDHSLLIGHTNESS_SCREEN_OPAQUE_ZBUF_BRANCHING_S4_U_C

#include "graphics/tori_compat.h"
#include "graphics/dash_restrict.h"

/**
 * gouraudhsllightness.opaque.zbuf.branching.s4
 *
 * The depth twin of raster.gouraudhsllightness.opaque...painter.branching.s4:
 * the same four-pixel colour blocks, each pixel tested and written back.
 */

#define GZB_FN raster_gouraudhsllightness_screen_opaque_zbuf_branching_s4
#define GZB_ORDERED raster_gouraudhsllightness_screen_opaque_zbuf_branching_s4_ordered
#define GZB_SPAN draw_scanline_gouraudhsllightness_screen_opaque_zbuf_branching_s4_ordered
#define GZB_ALPHA 0
#include "graphics/raster/gouraudhsllightness/gouraudhsllightness.zbuf.branching.s4_tmpl.inc"

#endif
