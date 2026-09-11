#ifndef TORIDRAW_BLIT_SIMD_SCALAR_H
#define TORIDRAW_BLIT_SIMD_SCALAR_H

/*
 * Scalar lane: the loop the run walk used inline before the kernel existed.
 * It is the definition of the result the SSE2 lane has to reproduce, and the
 * lane that compiles on a target without SSE2.
 */

#include <assert.h>
#include <stdint.h>

static inline int
toridraw_blit_alpha_run_impl(uint32_t const* row, int count, uint32_t want)
{
    int i = 0;

    assert(row);
    assert(want == 0u || want == 255u);

    while( i < count && (row[i] >> 24) == want )
        i++;
    return i;
}

#endif /* TORIDRAW_BLIT_SIMD_SCALAR_H */
