#ifndef TEX_SPAN_U_C
#define TEX_SPAN_U_C

#include "graphics/dash_restrict.h"
#include "graphics/pixel_format.h"

#include <stdint.h>

#include "graphics/clamp.h"

// clang-format off
#include "graphics/shade.h"
#include "graphics/raster/texture/span/tex.span_uv.h"
// clang-format on

/*
 * A vector lane composes in 8-bit texel lanes and stores whole native words,
 * so it is selected only where those are the same thing --
 * TORIPIXEL_TEXEL_SPACE_IS_NATIVE. Every other format takes the scalar span,
 * which converts per pixel and is correct on all of them.
 */
#if TORIPIXEL_TEXEL_SPACE_IS_NATIVE && \
    ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)
#include "impl/raster/span/span.tex.neon.u.c"
#elif TORIPIXEL_TEXEL_SPACE_IS_NATIVE && defined(__AVX2__) && !defined(AVX2_DISABLED)
#include "impl/raster/span/span.tex.avx.u.c"
#elif TORIPIXEL_TEXEL_SPACE_IS_NATIVE && defined(__SSE4_1__) && !defined(SSE2_DISABLED)
#include "impl/raster/span/span.tex.sse41.u.c"
#elif TORIPIXEL_TEXEL_SPACE_IS_NATIVE && defined(__SSE2__) && !defined(SSE2_DISABLED)
#include "impl/raster/span/span.tex.sse2.u.c"
#else
#include "impl/raster/span/span.tex.scalar.u.c"
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
#include "graphics/pixel_format.h"

/* PIXEL FORMAT. The span asm stores 4-byte pixels and samples 4-byte
 * texels; it claims that format rather than merely having been
 * assembled, and any other format takes the C reference below. */
#if defined(TORIDRAW_TEXSPAN_ASM) && TORIPIXEL_IS_XRGB8888
#include "graphics/raster/texture/span/tex_span_asm.h"
#endif

static inline void
tex_span_opaque_lerp8_v3_ordered(
    toripixel_t* RESTRICT pixel_buffer,
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
#if defined(TORIDRAW_TEXSPAN_ASM) && TORIPIXEL_IS_XRGB8888
    toridraw_texspan_opaque_lerp8_v3_xrgb8888_asm(
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
