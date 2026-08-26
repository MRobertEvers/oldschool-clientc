#ifndef TRSPK_SWIZZLE_SIMD_SCALAR_H
#define TRSPK_SWIZZLE_SIMD_SCALAR_H

/* Portable lane: the per-pixel rule, once per pixel. Included only from
 * trspk_swizzle_simd.h, which defines trspk_swizzle_pixel above it. */

#include <stddef.h>
#include <stdint.h>

static inline void
trspk_swizzle_argb_to_abgr_impl(uint32_t const* src, uint32_t* dst, size_t count)
{
    size_t i;
    for( i = 0u; i < count; i++ )
        dst[i] = trspk_swizzle_pixel(src[i]);
}

#endif
