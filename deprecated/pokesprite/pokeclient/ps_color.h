#ifndef PS_COLOR_H
#define PS_COLOR_H

/*
 * The 2D client's colour rules, ported from the generator so a sheet drawn here
 * is the colour its preview was: tools/pokesprite/layers.py (ramp_h, darken,
 * hsl16_rgb) and pokesprite.py (to_hls, from_hls, toward). Keep them in step.
 */

#include <stdint.h>

/** 0xRRGGBB for an OSRS HSL16 colour: the engine's table, brightness 0.8. */
uint32_t
PS_Hsl16ToRgb(int hsl16);

/**
 * A player-colour ramp of `steps` (1..3) colours, darkest first: a small lift
 * weighted to dark colours, shadows toward violet, highlights toward yellow.
 */
void
PS_Ramp(
    uint32_t base_rgb,
    int steps,
    uint32_t out[3]);

/** A dark tint of `rgb`, lightness scaled by `k`, nudged toward violet. */
uint32_t
PS_Darken(
    uint32_t rgb,
    float k);

#endif
