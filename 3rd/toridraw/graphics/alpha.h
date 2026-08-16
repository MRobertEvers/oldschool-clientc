#ifndef ALPHA_H
#define ALPHA_H

#include "dash_restrict.h"

/**
 * This is a cute way to alpha blend two rgb colors.
 *
 * Red and blue are carried in one multiply: masking with 0xFF00FF leaves them
 * far enough apart that scaling by alpha cannot make one carry into the other,
 * so a single shift and mask recovers both. Green goes separately.
 *
 * The arithmetic must be UNSIGNED. The widest term is 0xFF00FF * 0xFF, which
 * is 4,261,543,425: that fits a uint32 with room to spare but overflows a
 * signed int, and the wrapped product shifts negative and yields a garbage
 * colour. It needs a high red channel and a high alpha to happen at all, which
 * is why it survived so long — UBSan caught it as `15466510 * 175` on the
 * dark-red QBD. No widening to 64 bits is required, only the right signedness.
 *
 * @param alpha
 * @param base
 * @param other
 * @return int
 */
static inline int
alpha_blend(int alpha, int base, int other)
{
    unsigned int const a = (unsigned int)alpha;
    unsigned int const a_inv = 0xFFu - a;
    unsigned int const b = (unsigned int)base;
    unsigned int const o = (unsigned int)other;

    return (int)(((((b & 0xFF00FFu) * a_inv) >> 8) & 0xFF00FFu) +
                 ((((o & 0xFF00FFu) * a) >> 8) & 0xFF00FFu) +
                 ((((o & 0xFF00u) * a) >> 8) & 0xFF00u) +
                 ((((b & 0xFF00u) * a_inv) >> 8) & 0xFF00u));
}

#endif