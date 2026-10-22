#ifndef FLAT_SCREEN_ALPHA_ZBUF_BRANCHING_S4_U_C
#define FLAT_SCREEN_ALPHA_ZBUF_BRANCHING_S4_U_C

#include "graphics/tori_compat.h"
#include "graphics/dash_restrict.h"

/**
 * flat.alpha.zbuf.branching.s4
 *
 * The depth twin of raster.flat.alpha...painter.branching.s4. A translucent
 * face tests depth and never writes it, so what is drawn after it still shows.
 */

#define FZB_FN raster_flat_screen_alpha_zbuf_branching_s4
#define FZB_ORDERED raster_flat_screen_alpha_zbuf_branching_s4_ordered
#define FZB_SPAN draw_scanline_flat_screen_alpha_zbuf_branching_s4_ordered
#define FZB_ALPHA 1
#include "graphics/raster/flat/flat.zbuf.branching.s4_tmpl.inc"

#endif
