#ifndef TRSPK_COLOR_SIMD_H
#define TRSPK_COLOR_SIMD_H

/*
 * Packed colour -> normalised float, as a kernel family.
 *
 * Three places in trspk unpacked a packed colour into four floats, and all
 * three wrote it as `(float)byte / 255.0f`. That compiles to DIVSS -- four of
 * them per colour -- and on the Pentium 4 target DIVSS is ~30 cycles and does
 * not pipeline. In the vertex bake, which unpacked three colours per face,
 * that was twelve non-pipelined divisions per face and the single most
 * expensive thing on the path.
 *
 * A division by a constant is a multiplication by its reciprocal, and four
 * lanes of it are one SSE2 instruction. So this is arranged the way the raster
 * and projection kernels are: one entry point, an ISA-specific implementation
 * chosen here, and a scalar version that is the definition of what the others
 * must reproduce. TRSPK_SSE2_DISABLED compiles the SSE2 lane back out, which
 * is how the two are A/B'd against each other.
 *
 * ACCURACY. Multiplying by the rounded reciprocal is not always the same bit
 * pattern as dividing, but it is the same colour: the error is at most one ulp
 * of a value in [0,1], and every consumer either hands the float to a GPU as a
 * normalised colour or takes it back through (x * 255 + 0.5) truncation, which
 * one ulp cannot move off the byte it came from. The bake bench checks this
 * directly -- packed and float forms produce identical vertex colours.
 */

#include <stdint.h>

#if defined(__SSE2__) && !defined(TRSPK_SSE2_DISABLED)
#include "trspk_color_simd.sse2.h"
#else
#include "trspk_color_simd.scalar.h"
#endif

/**
 * Unpack 0xAARRGGBB into { r, g, b, a }, each normalised to [0,1].
 */
static inline void
trspk_color_unpack_argb(uint32_t argb, float rgba[4])
{
    trspk_color_unpack_argb_impl(argb, rgba);
}

/**
 * Unpack 0x00RRGGBB with an alpha supplied separately -- the shape the hsl16
 * palette hands back, where the colour carries no alpha of its own.
 */
static inline void
trspk_color_unpack_rgb_alpha(uint32_t rgb, uint8_t alpha, float rgba[4])
{
    trspk_color_unpack_argb_impl(((uint32_t)alpha << 24) | (rgb & 0x00FFFFFFu), rgba);
}

#endif
