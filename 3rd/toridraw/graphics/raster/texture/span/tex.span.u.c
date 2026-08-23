#ifndef TEX_SPAN_U_C
#define TEX_SPAN_U_C

#include "graphics/dash_restrict.h"

#include <stdint.h>

#include "graphics/clamp.h"

// clang-format off
#include "graphics/shade.h"
#include "tex.span_uv.h"
// clang-format on

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "tex.span.neon.u.c"
#elif defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "tex.span.avx.u.c"
#elif defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "tex.span.sse41.u.c"
#elif defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "tex.span.sse2.u.c"
#else
#include "tex.span.scalar.u.c"
#endif

/*
 * The hand-written twin, on the lanes that have one.
 *
 * The indirection is a named function and not a rename of the C, because the C
 * has to survive: it stays compiled, it stays the reference, and
 * toridraw_texspan_asm_test.c replays spans through both and fails on the first
 * differing pixel. Nothing here quietly becomes the definition of what a
 * texture span is -- the asm has to keep agreeing with something still in the
 * build.
 *
 * The makefile decides which lanes qualify, and withdraws the define from any
 * probe build: the census and ablation counters live in the C, so a measurement
 * build that silently ran the asm would report a texture span that draws no
 * spans at all.
 */
#ifdef TORIDRAW_TEXSPAN_ASM
#include "tex_span_asm.h"
#endif

static inline void
tex_span_opaque_lerp8_v3_ordered(
    int* RESTRICT pixel_buffer,
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
    int* RESTRICT texels,
    int texture_width)
{
#ifdef TORIDRAW_TEXSPAN_ASM
    toridraw_texspan_opaque_lerp8_v3_asm(
#else
    draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered(
#endif
        pixel_buffer,
        screen_width,
        screen_x0_ish16,
        screen_x1_ish16,
        pixel_offset,
        au,
        bv,
        cw,
        step_au_dx,
        step_bv_dx,
        step_cw_dx,
        shade8bit_ish8,
        step_shade8bit_dx_ish8,
        texels,
        texture_width);
}

#endif
