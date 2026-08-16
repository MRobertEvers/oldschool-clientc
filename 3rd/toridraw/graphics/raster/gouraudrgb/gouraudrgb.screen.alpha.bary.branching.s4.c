#ifndef GOURAUDRGB_SCREEN_ALPHA_BARY_BRANCHING_S4_C
#define GOURAUDRGB_SCREEN_ALPHA_BARY_BRANCHING_S4_C

#include "graphics/tori_compat.h"
#include "graphics/alpha.h"
#include "graphics/dash_restrict.h"
#include "graphics/raster/flat/flat_screen_edges.h"
#include "graphics/raster/gouraudrgb/gouraudrgb_barycentric_steps.h"

#include "graphics/shared_tables.h"

/**
 * gouraudrgb.screen.alpha.bary.branching.s4
 *
 * The opaque kernel's walk with a per-face alpha blend into the destination.
 * `alpha` is the source weight, 0xFF being fully opaque - the same convention
 * alpha_blend() and the gouraudhsllightness alpha twin use, and the inverse of
 * the raw model face_alphas byte, which the caller is expected to have already
 * flipped.
 */

#define GRGB_FN raster_gouraudrgb_screen_alpha_bary_branching_s4
#define GRGB_ORDERED raster_gouraudrgb_screen_alpha_bary_branching_s4_ordered
#define GRGB_SPAN draw_scanline_gouraudrgb_screen_alpha_bary_branching_s4_ordered
#define GRGB_SPAN_NC draw_scanline_gouraudrgb_screen_alpha_bary_branching_s4_ordered_noclip
#define GRGB_ALPHA 1
#include "graphics/raster/gouraudrgb/gouraudrgb.screen.bary.branching.s4.inc"

#endif
