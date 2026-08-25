#ifndef GOURAUD_TRI_ASM_H
#define GOURAUD_TRI_ASM_H

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
 * PIXEL FORMAT. The kernel indexes g_hsl16_to_rgb_table with a 4-byte scale and
 * stores 4-byte pixels. Under TORIDRAW_PIXEL16 both are 2 bytes, so the asm
 * would be silently wrong rather than merely absent -- hence the hard error.
 * The makefile does not build the kernel into a 16-bit-pixel configuration; if
 * that ever changes, this is where it stops.
 */

#ifdef TORIDRAW_GOURAUD_TRI_ASM

#ifdef TORIDRAW_PIXEL16
#error "gouraud_tri_i686.S assumes 32-bit pixels and a 32-bit palette"
#endif

#include "graphics/shared_tables.h"

void toridraw_gouraud_tri_opaque_s4_asm(
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

#define TORIDRAW_GOURAUD_TRI_OPAQUE_S4 toridraw_gouraud_tri_opaque_s4_asm

#else

#define TORIDRAW_GOURAUD_TRI_OPAQUE_S4 \
    raster_gouraudhsllightness_screen_opaque_bary_branching_s4

#endif

#endif
