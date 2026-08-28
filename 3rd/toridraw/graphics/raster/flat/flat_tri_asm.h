/*
 * DOOR NAMING -- the same scheme in flat/, gouraudhsllightness/ and texture/.
 *
 * Two independent axes: WHO PUT THE VERTICES IN Y ORDER, and HOW MANY
 * TRIANGLES ARRIVE PER CALL. Three of the four cells exist. There is no
 * self-sorting run door and there should not be: a run only ever comes from
 * the depth sort, which ordered it on the way past.
 *
 *   suffix              vertices arrive   per call   runs the y-sort ladder
 *   ------------------  ----------------  ---------  ----------------------
 *   _sorting_asm        any order         one        YES
 *   _presorted_asm      already ordered   one        no
 *   _presorted_run_asm  already ordered   a run      no
 *
 * The old names hid the axis that matters. `_tri_` and a bare `_asm` said
 * "one triangle" and left out that this is the only door that sorts;
 * `_batch_` said "several triangles" and left out that a run is ALWAYS
 * presorted -- so nothing in the name told you that the run door and the
 * presorted door share a body, or that "batched" and "presorted" were never
 * alternatives to choose between.
 *
 * All the doors of a family converge on one body, so they cannot draw
 * different triangles. What differs is only what each pays to reach it:
 * sorting pays the six-way compare ladder, which is up to six unpredictable
 * branches on a part that costs twenty pipeline stages for a mispredict; a
 * single-triangle door pays the cdecl marshal, the call, and the callee-saved
 * registers once per triangle where a run pays them once per run.
 */

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

void toridraw_flat_opaque_s4_sorting_asm(
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

void toridraw_flat_alpha_s4_sorting_asm(
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
 * `rows` is `count` records of TORIDRAW_FLAT_PRESORTED_RUN_ROW_INTS ints, 16-byte
 * aligned, laid out x0,x1,x2,pad / y0,y1,y2,pad / color,alpha,pad,pad -- the
 * screen coordinates with offset_x/offset_y already folded in. The kernels walk
 * them in order, so a painter's draw order survives a batch; see
 * toridraw_raster.u.c for the flushing rule that keeps it that way.
 */
void toridraw_flat_opaque_s4_presorted_run_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

void toridraw_flat_alpha_s4_presorted_run_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

#define TORIDRAW_FLAT_TRI_OPAQUE_S4 toridraw_flat_opaque_s4_sorting_asm
#define TORIDRAW_FLAT_TRI_ALPHA_S4  toridraw_flat_alpha_s4_sorting_asm

/* Sixteen bytes per group, three groups. Must match ROWBYTES in the .S. */
#define TORIDRAW_FLAT_PRESORTED_RUN_ROW_INTS 12
#define TORIDRAW_FLAT_PRESORTED_RUN 1

#elif defined(TORIDRAW_FLAT_TRI_NEON_ASM)

/*
 * The AArch64 / NEON lane: flat_tri_aarch64.S. Only the RUN doors exist --
 * the per-face path keeps the C, which is the arm the A/B baseline measures,
 * and a run is the only thing the depth sort ever hands the asm. Same row
 * record as the i686 kernel.
 */

#ifdef TORIDRAW_PIXEL16
#error "flat_tri_aarch64.S assumes 32-bit pixels and a 32-bit palette"
#endif

#include "graphics/shared_tables.h"

void toridraw_flat_opaque_s4_presorted_run_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

void toridraw_flat_alpha_s4_presorted_run_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

#define TORIDRAW_FLAT_TRI_OPAQUE_S4 raster_flat_screen_opaque_branching_s4
#define TORIDRAW_FLAT_TRI_ALPHA_S4  raster_flat_screen_alpha_branching_s4

#define TORIDRAW_FLAT_PRESORTED_RUN_ROW_INTS 12
#define TORIDRAW_FLAT_PRESORTED_RUN 1

#else

#define TORIDRAW_FLAT_TRI_OPAQUE_S4 raster_flat_screen_opaque_branching_s4
#define TORIDRAW_FLAT_TRI_ALPHA_S4  raster_flat_screen_alpha_branching_s4

#endif

#endif
