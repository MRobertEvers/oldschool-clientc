#ifndef TORIDRAW_FB_CLEAR_H
#define TORIDRAW_FB_CLEAR_H

/*
 * Clearing a 32-bit framebuffer to a constant.
 *
 * The C function below is the reference and is always compiled. When
 * TORIDRAW_FB_CLEAR_ASM is defined, TORIDRAW_FB_CLEAR32 resolves to the
 * hand-written i686 kernel in fb_clear_i686.S instead, which uses non-temporal
 * stores; see that file for why the shape of this buffer -- long, contiguous,
 * aligned, write-only -- is the one case in the renderer where they pay, and
 * why the rasterizer's 7-pixel spans are the case where they cost nine times.
 *
 * Both write the same bytes. toridraw_fb_clear_test compares them.
 */

#include "pixel_format.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Fill @p count_px pixels at @p dst with @p value.
 *
 * @param dst       32-bit pixel buffer, any alignment.
 * @param count_px  pixel count, not a byte count. Zero is a legitimate
 *                  no-op: a zero-area surface is a real runtime state, not a
 *                  caller bug.
 * @param value     the pixel written to every slot.
 */
static inline void
ToriDraw_FbClear32(uint32_t* dst, size_t count_px, uint32_t value)
{
    size_t i = 0;

    if( count_px == 0 )
        return;
    assert(dst);

    /* Four at a time so the compiler has an obvious unroll to widen; this is
     * the shape the scalar loop in the soft3d renderer already had. */
    for( ; i + 4 <= count_px; i += 4 )
    {
        dst[i] = value;
        dst[i + 1] = value;
        dst[i + 2] = value;
        dst[i + 3] = value;
    }
    for( ; i < count_px; i++ )
        dst[i] = value;
}

/*
 * PIXEL FORMAT. fb_clear_i686.S writes 4-byte pixels, so it claims
 * TORIPIXEL_IS_XRGB8888 rather than merely having been assembled; any other
 * format takes the C twin above.
 */
#if defined(TORIDRAW_FB_CLEAR_ASM) && TORIDRAW_FB_CLEAR_ASM && TORIPIXEL_IS_XRGB8888

/**
 * @brief Hand-written i686 SSE2 twin of ToriDraw_FbClear32.
 *
 * Ends in an sfence, so the buffer is safe to read -- by a blit, by the next
 * pass -- the instant this returns.
 */
void toridraw_fb_clear32_nt_xrgb8888_asm(uint32_t* dst, size_t count_px, uint32_t value);

#define TORIDRAW_FB_CLEAR32 toridraw_fb_clear32_nt_xrgb8888_asm

#else

#define TORIDRAW_FB_CLEAR32 ToriDraw_FbClear32

#endif

#endif /* TORIDRAW_FB_CLEAR_H */
