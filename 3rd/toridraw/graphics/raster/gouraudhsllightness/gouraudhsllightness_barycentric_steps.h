#ifndef GOURAUDHSLLIGHTNESS_BARYCENTRIC_STEPS_H
#define GOURAUDHSLLIGHTNESS_BARYCENTRIC_STEPS_H

#include "graphics/int_wrap.h"

/**
 * The two gouraud colour gradients: dHSL/dx and dHSL/dy, both in 1/256ths, both
 * dividing by the same triangle area. One reciprocal, two multiplies.
 *
 * WHY THIS IS ALLOWED TO BE APPROXIMATE, AND THE EDGE SLOPES ARE NOT.
 *
 * These two quotients are a *shade* step. They accumulate into an index into
 * g_hsl16_to_rgb_table, shifted down by 8 on the way, so being one 1/256th out
 * moves a pixel's colour by at most one palette entry after 256 pixels of span.
 * The three edge slopes in the same prologue are a *screen x* in 1/65536ths;
 * being one out there is eventually a wrong pixel column, and a triangle whose
 * right edge lands one column off does not tile with its neighbour. Those still
 * divide, and are meant to. Do not extend this to them.
 *
 * WHY DOUBLE AND NOT FLOAT.
 *
 * The old note here said a float reciprocal was cheap but that `numerator << 8`
 * overruns a 24-bit mantissa. That is true and it is why float is not used: the
 * d_hsl differences reach +-65535 and the dy they multiply reaches a few
 * hundred, so the shifted product does not fit an int32 and is *meant* to wrap
 * (hence toridraw_wrap_shl -- the plain signed shift it replaces was undefined
 * on exactly the inputs it sees most). A double holds all 32 of those bits
 * exactly, so the only error left is the rounding of the reciprocal and of the
 * product: two roundings, relative error under 2^-52, and |quotient| < 2^31, so
 * the computed value sits within 2^-21 of the true quotient. It can therefore
 * only truncate to a different integer when the true quotient is within 2^-21
 * of one -- which in practice means when sarea divides the numerator exactly,
 * and then the answer is out by exactly 1.
 *
 * Measured against the integer divide over 600,000 randomised gradients with
 * full-range wrapping numerators and |sarea| drawn from the shape actually seen
 * in a scene (3,020,338 triangles over 300 lumbridge frames): worst |error| 1,
 * mean |error| 0.00003 -- about 18 differences in 600,000.
 *
 * WHY NOT A RECIPROCAL TABLE.
 *
 * That was the first thing tried, because 73% of triangles have |sarea| < 16
 * and would hit the first cache line of one. Priced on the Pentium 4 that is
 * the acceptance target, over the whole five-divide prologue:
 *
 *   five idivl (what shipped)               75.65 ns
 *   this, 3 idivl + divsd + 2 mulsd         65.33 ns   1.158x
 *   8 KB table, 2^30 fixed point            64.00 ns   1.182x
 *   8 KB table, 2^32 fixed point            64.39 ns   1.175x
 *
 * The table's 1.3 ns edge is inside the noise, and the probe flatters it: its
 * divisors come from a 1 KB ring, so the table is always hot, which it would
 * not be against a real framebuffer and texture working set. It is also less
 * accurate (worst |error| 2) and needs a range check and a fallback for
 * |sarea| >= 2048. Two roundings and no table wins on every axis but one, and
 * that one is not resolvable.
 */
static inline double
gouraudhsllightness_barycentric_recip(int sarea)
{
    /* sarea == 0 is a degenerate triangle and every caller returns before
     * reaching here; this would be an infinity, not a fault. */
    return 1.0 / (double)sarea;
}

static inline int
gouraudhsllightness_barycentric_hsl_step_ish8(
    int numerator,
    double recip_sarea)
{
    return (int)((double)toridraw_wrap_shl(numerator, 8) * recip_sarea);
}

#endif
