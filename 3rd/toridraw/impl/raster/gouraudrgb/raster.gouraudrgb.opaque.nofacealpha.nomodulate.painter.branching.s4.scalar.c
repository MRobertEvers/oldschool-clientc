#ifndef GOURAUDRGB_SCREEN_OPAQUE_BARY_BRANCHING_S4_C
#define GOURAUDRGB_SCREEN_OPAQUE_BARY_BRANCHING_S4_C

#include "graphics/tori_compat.h"
#include "graphics/dash_restrict.h"
#include "graphics/raster/flat/flat_screen_edges.h"
#include "graphics/raster/gouraudrgb/gouraudrgb_barycentric_steps.h"

#include "graphics/shared_tables.h"

/**
 * gouraudrgb.screen.opaque.bary.branching.s4
 *
 * Vertex colours arrive as packed 0x00RRGGBB and are interpolated per channel,
 * so the drawn gradient is the straight line between the two colours. The
 * gouraudhsllightness twin instead interpolates a packed HSL16 word through the
 * palette, which is only a colour gradient when the two endpoints share a hue.
 */

#define GRGB_FN raster_gouraudrgb_screen_opaque_bary_branching_s4
#define GRGB_ORDERED raster_gouraudrgb_screen_opaque_bary_branching_s4_ordered
#define GRGB_SPAN draw_scanline_gouraudrgb_screen_opaque_bary_branching_s4_ordered
#define GRGB_SPAN_NC draw_scanline_gouraudrgb_screen_opaque_bary_branching_s4_ordered_noclip
#define GRGB_ALPHA 0
#include "impl/raster/gouraudrgb/raster.gouraudrgb.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.inc"

#endif
