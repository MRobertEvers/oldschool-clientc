#ifndef TORIDRAW_GOURAUD_ALPHA_SPAN_H
#define TORIDRAW_GOURAUD_ALPHA_SPAN_H

/*
 * The four-pixel alpha span blend, split per ISA.
 *
 * One operation, written four times: blend four consecutive framebuffer pixels
 * toward a single colour at a constant alpha. It is the innermost thing the
 * translucent gouraud spans do, so each instruction set gets its own copy --
 * but WHICH copy a build compiles is a property of the machine and not of the
 * span, and the span never asks. All four copies used to sit inside one
 * `#if`/`#elif` chain in this family's .u.c, redefining the same function name
 * under each arm.
 *
 *   ...span.u.c          selects one lane
 *   ...span.neon.u.c     AArch64
 *   ...span.avx.u.c      AVX2
 *   ...span.sse2.u.c     SSE2 -- carries the measurement note explaining why
 *                        it is NOT the arm an x86 build should want on an
 *                        efficiency core
 *   ...span.scalar.u.c   four calls to alpha_blend, which is the definition
 *                        the other three reproduce
 *
 * THE HOOK CONTRACT. Each lane defines exactly this, under this name:
 *
 *   void raster_linear_alpha_s4(toripixel_t* RESTRICT pixel_buffer,
 *                               int offset, int rgb_color, int alpha)
 *       Blends pixel_buffer[offset .. offset + 3] toward rgb_color at `alpha`
 *       (0 = keep the buffer, 0xFF = take the colour), in place. All four
 *       pixels are read and written unconditionally, so the caller owns the
 *       span-end clamp; there is no partial form and no lane declines.
 */

#include "graphics/dash_restrict.h"

#include <stdint.h>

// clang-format off
#include "graphics/alpha.h"
// clang-format on

#endif /* TORIDRAW_GOURAUD_ALPHA_SPAN_H */
