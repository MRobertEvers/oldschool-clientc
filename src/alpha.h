#ifndef ALPHA_H
#define ALPHA_H

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
alpha_blend(int alpha, int base, int other)
{
    int alpha_inv = 0xFF - alpha;
    return ((((base & 0xFF00FF) * alpha_inv) >> 8) & 0xFF00FF) +
           ((((other & 0xFF00FF) * alpha) >> 8) & 0xFF00FF) +
           ((((other & 0xFF00) * alpha) >> 8) & 0xFF00) +
           ((((base & 0xFF00) * alpha_inv) >> 8) & 0xFF00);
}

#endif