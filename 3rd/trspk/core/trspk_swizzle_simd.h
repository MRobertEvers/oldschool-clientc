#ifndef TRSPK_SWIZZLE_SIMD_H
#define TRSPK_SWIZZLE_SIMD_H

/*
 * ARGB -> ABGR over a whole sprite, as a kernel family.
 *
 * The GL lanes upload sprite pixels as GL_RGBA and the client's sprites are
 * 0xAARRGGBB, so every pixel of every sprite has its red and blue exchanged on
 * the way in. That is a byte shuffle -- no arithmetic at all -- and it was
 * done one pixel at a time with four shifts, three masks and three ors. Four
 * pixels of it fit in one SSE2 register, so the whole loop becomes a load,
 * a handful of logical ops and a store per four pixels.
 *
 * It also fixes up alpha on the way past: the RS sprite convention is that
 * colour 0 means transparent rather than alpha 0, so a pixel arriving with a
 * zero alpha byte is opaque if it has any colour and transparent if it does
 * not. Scalar code asks that as two branches per pixel, over data with long
 * transparent runs; in the vector lane it is a compare and a select, and
 * nothing can mispredict.
 *
 * Arranged like the raster and projection kernels: one entry point, an ISA
 * lane chosen here, and a scalar lane that defines what the others reproduce.
 * TRSPK_SSE2_DISABLED compiles the SSE2 lane back out for an A/B.
 */

#include <stddef.h>
#include <stdint.h>

/**
 * The rule, for one pixel. Both lanes are defined in terms of this: the scalar
 * one calls it, and the vector one has to agree with it exactly (it uses this
 * for its own tail).
 */
static inline uint32_t
trspk_swizzle_pixel(uint32_t pix)
{
    uint32_t const rgb = pix & 0x00FFFFFFu;
    uint32_t const a_hi = (pix >> 24) & 0xFFu;
    uint32_t const a = (a_hi != 0u) ? a_hi : (rgb != 0u ? 0xFFu : 0u);

    return ((pix >> 16) & 0xFFu) | (((pix >> 8) & 0xFFu) << 8) |
        ((pix & 0xFFu) << 16) | (a << 24);
}

#if defined(__SSE2__) && !defined(TRSPK_SSE2_DISABLED)
#include "trspk_swizzle_simd.sse2.h"
#else
#include "trspk_swizzle_simd.scalar.h"
#endif

/**
 * Exchange R and B across `count` pixels, applying the colour-0-is-transparent
 * rule to the alpha byte. `src` and `dst` may be the same buffer; they must
 * not otherwise overlap.
 */
static inline void
trspk_swizzle_argb_to_abgr(uint32_t const* src, uint32_t* dst, size_t count)
{
    trspk_swizzle_argb_to_abgr_impl(src, dst, count);
}

#endif
