#ifndef TORIDRAW_GOURAUD_ALPHA_SPAN_SCALAR_U_C
#define TORIDRAW_GOURAUD_ALPHA_SPAN_SCALAR_U_C

#include "gouraudhsllightness.screen.alpha.span.h"

/*
 * No vector blend in this build: four calls to alpha_blend, which is also the
 * definition the three vector lanes reproduce. wasm, and any build with its
 * ISA gate disabled.
 */

static inline void
raster_linear_alpha_s4(
    uint32_t* RESTRICT pixel_buffer,
    int offset,
    int rgb_color,
    int alpha)
{
    for( int i = 0; i < 4; i++ )
    {
        int rgb_blend = pixel_buffer[offset];
        rgb_blend = alpha_blend(alpha, rgb_blend, rgb_color);
        pixel_buffer[offset] = rgb_blend;
        offset += 1;
    }
}

#endif /* TORIDRAW_GOURAUD_ALPHA_SPAN_SCALAR_U_C */
