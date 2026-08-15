#ifndef GOURAUDHSLLIGHTNESS_BARYCENTRIC_STEPS_H
#define GOURAUDHSLLIGHTNESS_BARYCENTRIC_STEPS_H

/**
 * Note: both gouraud colour gradients divide by this same `sarea`, so sharing
 * one reciprocal looks like an easy win. It is not. The result indexes the
 * HSL16 table, so it has to match the division exactly, and an exact shared
 * reciprocal needs a 64-bit divide plus a multiply-back correction per
 * gradient - more work than the two 32-bit divides it replaces. A float
 * reciprocal is cheap but `numerator << 8` overruns a 24-bit mantissa. Left
 * as-is deliberately.
 */
static inline int
gouraudhsllightness_barycentric_hsl_step_ish8(
    int numerator,
    int sarea)
{
    return (numerator << 8) / sarea;
}

#endif
