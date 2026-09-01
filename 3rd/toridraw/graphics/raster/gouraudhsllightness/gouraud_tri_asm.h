#ifndef GOURAUD_TRI_ASM_H
#define GOURAUD_TRI_ASM_H

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

/*
 * Routes the opaque gouraud triangle to gouraud_tri_i686.S where the makefile
 * has built it, and to the C everywhere else.
 *
 * The asm is the WHOLE triangle -- vertex sort, prologue, both trapezoid walks
 * and the span fill -- not a leaf the C calls into. That is the point of it:
 * the C's per-row loop spills its accumulators because i686 has four
 * callee-saved registers and the row body needs more than four live values, and
 * no amount of tuning inside a helper fixes an allocation decision made outside
 * it. So the substitution happens at the triangle, or not at all.
 *
 * The two are bit-identical, over 400,000 randomised triangles including
 * degenerate, flat-topped, sliver and fully-offscreen cases
 * (toridraw_gouraud_tri_asm_test.c). That standard is not negotiable here: the
 * 4-pixel colour quantization in toridraw_gouraud_span_fill_short is visible
 * output, pinned separately by toridraw_scanline_parity_test.
 *
 * PIXEL FORMAT. The kernel indexes the palette with a 4-byte scale and stores
 * 4-byte pixels, which is a claim about ONE format -- so it is gated on
 * TORIPIXEL_IS_XRGB8888 and not merely on having been assembled. A build that
 * asks for the asm on some other format falls through to the C twin below.
 * Writing a door for another format means a new file naming it
 * (tri.gouraudhsllightness.rgb565.aarch64.S) and a claim of its own here.
 */

#include "graphics/pixel_format.h"

#if defined(TORIDRAW_GOURAUD_TRI_ASM) && TORIPIXEL_IS_XRGB8888

#include "graphics/shared_tables.h"

void toridraw_gouraud_opaque_s4_sorting_xrgb8888_asm(
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
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16);

#define TORIDRAW_GOURAUD_TRI_OPAQUE_S4 toridraw_gouraud_opaque_s4_sorting_xrgb8888_asm

/*
 * The same kernel entered once for a RUN of triangles instead of once each.
 *
 * `rows` is `count` records of TORIDRAW_GOURAUD_RUN_ROW_INTS ints, 16-byte
 * aligned, laid out x0,x1,x2,pad / y0,y1,y2,pad / c0,c1,c2,pad -- the screen
 * coordinates with offset_x/offset_y already folded in, and the three hsl16
 * vertex colours. The kernel walks them in order, so a painter's draw order
 * survives a batch; see toridraw_raster.u.c for the flushing rule that keeps
 * it that way.
 */
void toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

/*
 * The blending twin, taking the opacity per row -- lane 11, the colour group's
 * spare. A run may mix opacities, and there is no reason for the batcher to
 * split one that does.
 */
void toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

void toridraw_gouraud_alpha_s4_sorting_xrgb8888_asm(
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
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16,
    int alpha);

#define TORIDRAW_GOURAUD_PRESORTED_RUN_OPAQUE toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm
#define TORIDRAW_GOURAUD_PRESORTED_RUN_ALPHA  toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm

/* Sixteen bytes per group, three groups. Must match ROWBYTES in the .S. */
#define TORIDRAW_GOURAUD_RUN_ROW_INTS 12
#define TORIDRAW_GOURAUD_PRESORTED_RUN 1

#elif defined(TORIDRAW_GOURAUD_TRI_NEON_ASM) && TORIPIXEL_IS_XRGB8888

/*
 * The AArch64 / NEON lane: gouraud_tri_aarch64.S. Only the RUN doors exist --
 * the per-face path keeps the C, which is the arm the A/B baseline measures.
 * Same 48-byte row record as the i686 kernel, opacity in lane 11.
 */

#include "graphics/shared_tables.h"

void toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

void toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

#define TORIDRAW_GOURAUD_TRI_OPAQUE_S4 \
    raster_gouraudhsllightness_screen_opaque_bary_branching_s4

#define TORIDRAW_GOURAUD_PRESORTED_RUN_OPAQUE toridraw_gouraud_opaque_s4_presorted_run_xrgb8888_asm
#define TORIDRAW_GOURAUD_PRESORTED_RUN_ALPHA  toridraw_gouraud_alpha_s4_presorted_run_xrgb8888_asm

#define TORIDRAW_GOURAUD_RUN_ROW_INTS 12
#define TORIDRAW_GOURAUD_PRESORTED_RUN 1

#elif defined(TORIDRAW_GOURAUD_TRI_XTENSA_ASM) &&                                                  \
    (TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_RGB565 ||                                                \
     TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_RGB565_BE)

/*
 * The Xtensa LX7 / ESP32-S3 lane: tri.gouraudhsllightness.rgb565.xtensa.S.
 * Only the RUN doors exist, as on AArch64.
 *
 * THIS LANE'S COLOUR STEPS ARE AN EXACT INTEGER DIVIDE, which is a real
 * difference from every other lane and not a detail to discover later. LX7
 * has no double, and the two gradients are the one place this family wants
 * one; see the kernel's header and the IDIV arm of
 * gouraudhsllightness_barycentric_steps.h for why the trade inverts on this
 * core. A build scoring this kernel against the C must build the C with
 * -DTORIDRAW_GOURAUD_STEP_IDIV, or the two disagree by one shade step
 * wherever sarea divides the numerator exactly -- which is the approximation
 * in the reference, not an error in the kernel.
 *
 * Both 16-bit orders are served, by separate symbols; see flat_tri_asm.h.
 */

#include "graphics/shared_tables.h"

#if TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_RGB565_BE
#define TORIDRAW_GOURAUD_PRESORTED_RUN_OPAQUE toridraw_gouraud_opaque_s4_presorted_run_rgb565_be_asm
#define TORIDRAW_GOURAUD_PRESORTED_RUN_ALPHA  toridraw_gouraud_alpha_s4_presorted_run_rgb565_be_asm
#else
#define TORIDRAW_GOURAUD_PRESORTED_RUN_OPAQUE toridraw_gouraud_opaque_s4_presorted_run_rgb565_asm
#define TORIDRAW_GOURAUD_PRESORTED_RUN_ALPHA  toridraw_gouraud_alpha_s4_presorted_run_rgb565_asm
#endif

void TORIDRAW_GOURAUD_PRESORTED_RUN_OPAQUE(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

void TORIDRAW_GOURAUD_PRESORTED_RUN_ALPHA(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

#define TORIDRAW_GOURAUD_TRI_OPAQUE_S4 \
    raster_gouraudhsllightness_screen_opaque_bary_branching_s4

#define TORIDRAW_GOURAUD_RUN_ROW_INTS 12
#define TORIDRAW_GOURAUD_PRESORTED_RUN 1

#else

#define TORIDRAW_GOURAUD_TRI_OPAQUE_S4 \
    raster_gouraudhsllightness_screen_opaque_bary_branching_s4

#endif

#endif
