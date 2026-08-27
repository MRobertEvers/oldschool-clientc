#ifndef TORIDRAW_BLIT_SIMD_H
#define TORIDRAW_BLIT_SIMD_H

/*
 * Alpha-run scanning for the sprite blit, as a kernel family.
 *
 * ToriDraw2D_BlitArgbAlpha's opaque path walks a source row in runs: a run of
 * a==255 is handed to memcpy, a run of a==0 is skipped, and anything else is
 * blended one pixel at a time. Walking runs instead of pixels is what removed
 * the per-pixel branch ladder -- but FINDING the run still cost a load, a
 * shift, a compare and a branch for every pixel it covered, and on the XP
 * profile that scan was 7.2% of the frame, second only to the raster kernels.
 *
 * The test is the same for all four pixels of an SSE2 word: mask off the top
 * byte, compare against the wanted alpha, and read the four results out as a
 * movemask. Four pixels per iteration, and no data-dependent branch until the
 * run actually ends.
 *
 * Arranged the way the raster, projection and trspk colour kernels are: one
 * entry point, an ISA-specific implementation selected here, and a scalar
 * version that is the definition of what the others must reproduce.
 * TORIDRAW_BLIT_SSE2_DISABLED compiles the SSE2 lane back out, which is how
 * the two are A/B'd against each other.
 *
 * EXACTNESS. This is a search, not arithmetic: both lanes return the same
 * index for the same row or one of them is wrong. There is no accuracy
 * argument to make and none is claimed. toridraw_blit_opaque_test.c compares
 * the whole blit against the per-pixel ladder it replaces, so a lane that
 * disagreed by one pixel fails there.
 */

#include <stdint.h>

#if defined(__SSE2__) && !defined(TORIDRAW_BLIT_SSE2_DISABLED)
#include "toridraw_blit_simd.sse2.h"
#else
#include "toridraw_blit_simd.scalar.h"
#endif

/**
 * @brief Length of the leading run in `row[0..count)` whose alpha is `want`.
 *
 * `want` is 0 or 255 -- the two alphas the opaque walk splits on. Returns 0
 * when the first pixel does not match, so a caller that has already tested
 * row[0] can rely on a result of at least 1 and cannot fail to advance.
 */
static inline int
toridraw_blit_alpha_run(uint32_t const* row, int count, uint32_t want)
{
    return toridraw_blit_alpha_run_impl(row, count, want);
}

#endif /* TORIDRAW_BLIT_SIMD_H */
