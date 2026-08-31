#ifndef ALPHA_H
#define ALPHA_H

#include "dash_restrict.h"
#include "pixel_format.h"

/**
 * alpha_blend(alpha, base, other) -- composite `other` over `base` at a SOURCE
 * weight of 0..255, in whatever format this build's framebuffer holds.
 *
 * The name carries no format because it is the neutral spelling: it is bound
 * by graphics/pixel_format.h to exactly one implementation, and every one of
 * those does carry its format (toripixel_xrgb8888_alpha_blend,
 * toripixel_rgb565_alpha_blend, ...). Include this header to blend; read that
 * one to find out what the arithmetic is on a given target.
 */

#endif
