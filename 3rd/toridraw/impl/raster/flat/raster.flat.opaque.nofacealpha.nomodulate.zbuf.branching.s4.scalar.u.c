#ifndef FLAT_SCREEN_OPAQUE_ZBUF_BRANCHING_S4_U_C
#define FLAT_SCREEN_OPAQUE_ZBUF_BRANCHING_S4_U_C

#include "graphics/tori_compat.h"
#include "graphics/dash_restrict.h"

/**
 * flat.opaque.zbuf.branching.s4
 *
 * The depth twin of raster.flat.opaque...painter.branching.s4: same pixels,
 * each tested against the z-buffer and written back.
 */

#define FZB_FN raster_flat_screen_opaque_zbuf_branching_s4
#define FZB_ORDERED raster_flat_screen_opaque_zbuf_branching_s4_ordered
#define FZB_SPAN draw_scanline_flat_screen_opaque_zbuf_branching_s4_ordered
#define FZB_ALPHA 0
#include "graphics/raster/flat/flat.zbuf.branching.s4_tmpl.inc"

#endif
