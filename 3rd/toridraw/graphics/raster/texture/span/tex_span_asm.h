#ifndef TEX_SPAN_ASM_H
#define TEX_SPAN_ASM_H

/*
 * Hand-written perspective texture span kernels.
 *
 * The C twin in tex.span.sse2.u.c stays the reference: it defines the pixels,
 * and toridraw_texspan_asm_test.c fails if these do not reproduce it byte for
 * byte on every span it replays. Nothing here is allowed to be "close enough" --
 * a texture span that differs by one texel row is the streaking class of bug
 * documented in docs/qbd_toridraw_streaks_debug.md, and it does not show up as
 * a wrong pixel so much as a wrong-looking rock.
 *
 * WHY BY HAND
 *
 * Measured on the osrs239 lumbridge-ground steady state (win64 OPT=1), the
 * perspective span's per-block setup -- one float reciprocal plus the two
 * quotient helpers, once per 8 pixels -- costs 0.353 ms of a 4.004 ms render,
 * MORE than the 0.266 ms the SSE2 gather it feeds costs. That setup is a serial
 * chain: cw >> shift, cvtsi2ss, divss, two mulss, cvttss2si, about 30 cycles
 * deep, and the compiler schedules it in the same iteration as the fill that
 * consumes it.
 *
 * It does not have to be. Block k's far endpoint IS block k+1's near endpoint
 * (the C carries it in cur_u/cur_v/have_cur), so the reciprocal for k+1 is
 * already computed during k. Issuing it BEFORE k's eight pixels are stored
 * hides the divide behind the stores. That is software pipelining across a loop
 * with four data-dependent branches in it, which GCC will not do, and it is the
 * reason this file exists -- not register allocation, which the compiled build
 * already gets right (2.3% spill traffic in the hot SSE cluster).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Bit-exact hand-written twin of
 *  draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered. */
void toridraw_texspan_opaque_lerp8_v3_asm(
    int* pixel_buffer,
    int screen_width,
    int screen_x0_ish16,
    int screen_x1_ish16,
    int pixel_offset,
    int au,
    int bv,
    int cw,
    int step_au_dx,
    int step_bv_dx,
    int step_cw_dx,
    int shade8bit_ish8,
    int step_shade8bit_dx_ish8,
    int* texels,
    int texture_width);

#ifdef __cplusplus
}
#endif

#endif
