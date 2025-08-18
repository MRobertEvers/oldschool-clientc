#ifndef SHADE_H
#define SHADE_H

#include <assert.h>

/**
 * This is a cute way to alpha blend two rgb colors.
 *
 * Since alpha is 0-0xFF, then alpha * 0xFF < 0x10000,
 * so we can multiply the r,b components (0xFF00FF) and the result of the multiplcation
 * for r,b will be 0xRRRRBBBB, so we can shift and mask.
 *
 * @param alpha
 * @param base
 * @param other
 * @return int
 */
static inline int
shade_blend(int base, int shade)
{
    assert(shade >= 0 && shade <= 0xFF);

    int r = (base & 0xFF0000) >> 16;
    int g = (base & 0x00FF00) >> 8;
    int b = (base & 0x0000FF);

    r *= shade;
    g *= shade;
    b *= shade;

    r >>= 8;
    g >>= 8;
    b >>= 8;

    r &= 0xFF;
    g &= 0xFF;
    b &= 0xFF;

    return (r << 16) | (g << 8) | b;
}

#endif