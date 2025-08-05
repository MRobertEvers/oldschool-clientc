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

    return ((((base & 0xFF00FF) * shade) >> 8) & 0xFF00FF) +
           ((((base & 0xFF00) * shade) >> 8) & 0xFF00);
}

#endif