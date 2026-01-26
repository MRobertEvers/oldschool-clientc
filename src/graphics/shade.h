#ifndef SHADE_H
#define SHADE_H

#include <assert.h>

/**
 * This is a cute way to alpha blend two rgb colors.
 *
 * Since alpha is 0-0xFF, then (alpha * 0xFF) < 0x10000 i.e. (0 to 0xFFFF, 0xFF^2),
 * so we can multiply the r,b components (0xFF00FF) and the result of the multiplcation
 * for r,b will be 0xRRRRBBBB, so we can shift and mask.
 *
 * @param alpha
 * @param base
 * @param other
 * @return int
 */
static inline int
shade_blend(
    int base,
    int shade)
{
    int rb = base & 0x00ff00ff;
    int g = base & 0x0000ff00;

    rb *= shade;
    g *= shade;

    rb &= 0xFF00FF00;
    g &= 0x00FF0000;

    return (rb | g) >> 8;
}

#endif