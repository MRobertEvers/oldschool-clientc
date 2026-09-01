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
 * PIXEL FORMAT. The i686 and AArch64 kernels index the palette with a 4-byte
 * scale and store 4-byte pixels, which is a claim about ONE format -- so they
 * are gated on TORIPIXEL_IS_XRGB8888 and not merely on having been assembled.
 * A build that asks for the asm on some other format falls through to the C
 * twin below, the same way a lane with no kernel for a family declines and
 * the caller carries on. Writing a door for another format means a new file
 * naming it and a claim of its own here, which is what the Xtensa arm is:
 * tri.flat.rgb565.xtensa.S indexes a 2-byte palette, stores 2-byte pixels and
 * blends with the RGB565 spread arithmetic, so it claims RGB565 and declines
 * everywhere else.
 *
 * WHAT THE WALK NAMES. Every arm binds the two run doors to
 * TORIDRAW_FLAT_PRESORTED_RUN_OPAQUE and ..._ALPHA -- the neutral spelling,
 * carrying no format -- and impl/raster/walk/walk.batched.u.c writes those.
 * A format token belongs in the name of an IMPLEMENTATION and nowhere else;
 * a walk that spells one is a walk that has to be edited to reach a lane it
 * has no opinion about. This is the same rule graphics/alpha.h states for
 * alpha_blend, applied to a door instead of a blend.
 *
 * The gouraud and texture families have no such spelling and should not grow
 * one yet: every arm either of them has is XRGB8888, so a neutral name there
 * would select between one thing. It appears when a family acquires a second
 * format, which is what has just happened to this one.
 */

#include "graphics/pixel_format.h"

#if defined(TORIDRAW_FLAT_TRI_ASM) && TORIPIXEL_IS_XRGB8888

#include "graphics/shared_tables.h"

void toridraw_flat_opaque_s4_sorting_xrgb8888_asm(
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

void toridraw_flat_alpha_s4_sorting_xrgb8888_asm(
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
void toridraw_flat_opaque_s4_presorted_run_xrgb8888_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

void toridraw_flat_alpha_s4_presorted_run_xrgb8888_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

#define TORIDRAW_FLAT_TRI_OPAQUE_S4 toridraw_flat_opaque_s4_sorting_xrgb8888_asm
#define TORIDRAW_FLAT_TRI_ALPHA_S4  toridraw_flat_alpha_s4_sorting_xrgb8888_asm

#define TORIDRAW_FLAT_PRESORTED_RUN_OPAQUE toridraw_flat_opaque_s4_presorted_run_xrgb8888_asm
#define TORIDRAW_FLAT_PRESORTED_RUN_ALPHA  toridraw_flat_alpha_s4_presorted_run_xrgb8888_asm

/* Sixteen bytes per group, three groups. Must match ROWBYTES in the .S. */
#define TORIDRAW_FLAT_PRESORTED_RUN_ROW_INTS 12
#define TORIDRAW_FLAT_PRESORTED_RUN 1

#elif defined(TORIDRAW_FLAT_TRI_NEON_ASM) && TORIPIXEL_IS_XRGB8888

/*
 * The AArch64 / NEON lane: flat_tri_aarch64.S. Only the RUN doors exist --
 * the per-face path keeps the C, which is the arm the A/B baseline measures,
 * and a run is the only thing the depth sort ever hands the asm. Same row
 * record as the i686 kernel.
 */

#include "graphics/shared_tables.h"

void toridraw_flat_opaque_s4_presorted_run_xrgb8888_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

void toridraw_flat_alpha_s4_presorted_run_xrgb8888_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

#define TORIDRAW_FLAT_TRI_OPAQUE_S4 raster_flat_screen_opaque_branching_s4
#define TORIDRAW_FLAT_TRI_ALPHA_S4  raster_flat_screen_alpha_branching_s4

#define TORIDRAW_FLAT_PRESORTED_RUN_OPAQUE toridraw_flat_opaque_s4_presorted_run_xrgb8888_asm
#define TORIDRAW_FLAT_PRESORTED_RUN_ALPHA  toridraw_flat_alpha_s4_presorted_run_xrgb8888_asm

#define TORIDRAW_FLAT_PRESORTED_RUN_ROW_INTS 12
#define TORIDRAW_FLAT_PRESORTED_RUN 1

#elif defined(TORIDRAW_FLAT_TRI_XTENSA_ASM) &&                                                     \
    (TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_RGB565 ||                                                \
     TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_RGB565_BE)

/*
 * The Xtensa LX7 / ESP32-S3 lane: tri.flat.rgb565.xtensa.S. As on AArch64
 * only the RUN doors exist, and the per-face path keeps the C.
 *
 * WHY THIS IS THE ONLY PLACE A FORMAT IS NAMED TWICE. Every C kernel in the
 * library is format-blind -- it stores what the palette gave it and calls
 * alpha_blend, and graphics/pixel_format.h binds those to one format for the
 * build. That is why a new format needs no new kernels. Assembly cannot call
 * an inline C function, so a hand-written door has to CONTAIN the arithmetic
 * and therefore has to claim which format it wrote. Hence the gate, hence
 * TORIPIXEL_IS_XRGB8888 on the two lanes above.
 *
 * A build that assembled the .S but selected a 32-bit framebuffer arrives here
 * and DECLINES -- the symbols are linked and unreachable -- because a kernel
 * that stores halfwords into a 32-bit buffer is not a slower answer, it is a
 * wrong one.
 *
 * BOTH 16-BIT ORDERS ARE SERVED, by different symbols, so the routing and the
 * kernel cannot disagree about which one a build got. Most of the kernel did
 * not care: the opaque door looks a word up in the palette and stores it, and
 * the palette was generated in the build's own order, so it assembles
 * identically for both. Only the BLEND touches channel fields, and that is
 * where the .S spends its one #if. Assemble it with
 * -DTORIDRAW_XTENSA_RGB565_BE=1 for the swapped order; it picks its entry
 * point names off the same macro, so a mismatch between this header and that
 * build flag is an undefined symbol at link rather than swapped reds on a
 * panel.
 *
 * Same 48-byte row record as the other two lanes. The record is ints and does
 * not know the pixel format, which is why one batched walk feeds all three.
 */

#include "graphics/shared_tables.h"

#if TORIDRAW_PIXEL_FORMAT == TORIDRAW_PF_RGB565_BE
#define TORIDRAW_FLAT_PRESORTED_RUN_OPAQUE toridraw_flat_opaque_s4_presorted_run_rgb565_be_asm
#define TORIDRAW_FLAT_PRESORTED_RUN_ALPHA  toridraw_flat_alpha_s4_presorted_run_rgb565_be_asm
#else
#define TORIDRAW_FLAT_PRESORTED_RUN_OPAQUE toridraw_flat_opaque_s4_presorted_run_rgb565_asm
#define TORIDRAW_FLAT_PRESORTED_RUN_ALPHA  toridraw_flat_alpha_s4_presorted_run_rgb565_asm
#endif

void TORIDRAW_FLAT_PRESORTED_RUN_OPAQUE(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    const int* rows,
    int count);

void TORIDRAW_FLAT_PRESORTED_RUN_ALPHA(
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
