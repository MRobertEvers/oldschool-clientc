#ifndef SHADE_H
#define SHADE_H

#include "dash_restrict.h"
#include "pixel_format.h"

#include <assert.h>
#include <stdint.h>

/**
 * shade_blend(base, shade) -- scale every colour channel of `base` by a 0..255
 * shade, in whatever format this build's framebuffer holds.
 *
 * As with alpha_blend, the unqualified name is the neutral spelling bound by
 * graphics/pixel_format.h; the implementations there are named for the format
 * each one writes (toripixel_xrgb8888_shade_blend,
 * toripixel_rgb565_shade_blend, ...).
 */

#endif
