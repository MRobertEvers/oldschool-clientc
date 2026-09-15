#ifndef TRSPK_COLOR_SIMD_SCALAR_H
#define TRSPK_COLOR_SIMD_SCALAR_H

/*
 * Portable lane. This is the definition of the operation -- the SSE2 kernel
 * beside it has to reproduce these four values.
 *
 * Note it multiplies by the reciprocal rather than dividing. That is not a
 * SIMD trick, it is the same arithmetic written so the compiler does not have
 * to emit a divide it cannot fold: `/ 255.0f` is a runtime DIVSS on every
 * target, and there is no target where four of them are the right answer.
 */

#include <stdint.h>

#define TRSPK_COLOR_INV_255 (1.0f / 255.0f)

static inline void
trspk_color_unpack_argb_impl(uint32_t argb, float rgba[4])
{
    rgba[0] = (float)((argb >> 16) & 0xFFu) * TRSPK_COLOR_INV_255;
    rgba[1] = (float)((argb >> 8) & 0xFFu) * TRSPK_COLOR_INV_255;
    rgba[2] = (float)(argb & 0xFFu) * TRSPK_COLOR_INV_255;
    rgba[3] = (float)((argb >> 24) & 0xFFu) * TRSPK_COLOR_INV_255;
}

#endif
