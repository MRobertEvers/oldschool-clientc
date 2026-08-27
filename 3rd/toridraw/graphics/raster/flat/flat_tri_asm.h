#ifndef FLAT_TRI_ASM_H
#define FLAT_TRI_ASM_H

/*
 * Routes the flat triangle to flat_tri_i686.S where the makefile has built it,
 * and to the C everywhere else. Mirrors gouraud_tri_asm.h, which carries the
 * longer argument for why a whole-triangle substitution is the only one worth
 * making on this target.
 *
 * Both opacities are covered here, unlike gouraud, because both are cheap once
 * the colour is constant: the opaque span becomes a store loop that can align
 * itself and widen, and the alpha span's source term folds into a per-triangle
 * constant so the per-pixel work is one multiply and one add per channel, four
 * pixels at a time.
 *
 * The two are bit-identical to their C twins over 200,000 randomised triangles
 * each, single and batched, including degenerate, flat-topped, sliver and
 * fully-offscreen cases (toridraw_flat_tri_asm_test.c).
 *
 * PIXEL FORMAT. The kernels index g_hsl16_to_rgb_table with a 4-byte scale and
 * store 4-byte pixels. Under TORIDRAW_PIXEL16 both are 2 bytes, so the asm
 * would be silently wrong rather than merely absent -- hence the hard error.
 */

#ifdef TORIDRAW_FLAT_TRI_ASM

#ifdef TORIDRAW_PIXEL16
#error "flat_tri_i686.S assumes 32-bit pixels and a 32-bit palette"
#endif

#include "graphics/shared_tables.h"

void toridraw_flat_tri_opaque_s4_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color_hsl16);

void toridraw_flat_tri_alpha_s4_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color_hsl16,
    int alpha);

/*
 * The same kernels entered once for a RUN of triangles instead of once each.
 *
 * `rows` is `count` records of TORIDRAW_FLAT_BATCH_ROW_INTS ints, 16-byte
 * aligned, laid out x0,x1,x2,pad / y0,y1,y2,pad / color,alpha,pad,pad -- the
 * screen coordinates with offset_x/offset_y already folded in. The kernels walk
 * them in order, so a painter's draw order survives a batch; see
 * toridraw_raster.u.c for the flushing rule that keeps it that way.
 */
void toridraw_flat_batch_opaque_s4_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

void toridraw_flat_batch_alpha_s4_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

#define TORIDRAW_FLAT_TRI_OPAQUE_S4 toridraw_flat_tri_opaque_s4_asm
#define TORIDRAW_FLAT_TRI_ALPHA_S4  toridraw_flat_tri_alpha_s4_asm

/* Sixteen bytes per group, three groups. Must match ROWBYTES in the .S. */
#define TORIDRAW_FLAT_BATCH_ROW_INTS 12
#define TORIDRAW_FLAT_BATCH 1

#else

#define TORIDRAW_FLAT_TRI_OPAQUE_S4 raster_flat_screen_opaque_branching_s4
#define TORIDRAW_FLAT_TRI_ALPHA_S4  raster_flat_screen_alpha_branching_s4

#endif

#endif
