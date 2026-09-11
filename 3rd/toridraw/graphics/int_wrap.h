#ifndef TORIDRAW_INT_WRAP_H
#define TORIDRAW_INT_WRAP_H

/*
 * Modular int arithmetic for the shade and colour gradient chains.
 *
 * These kernels are a 1:1 port of the reference Java client, and the Java
 * client relies on int wraparound here. Java defines signed overflow to wrap;
 * C leaves it undefined. The difference is not academic -- the overflow is
 * reachable from ordinary play.
 *
 * A gradient like shade8bit_xhat_ish8 is ((dshade << 9) / sarea), which works
 * out to roughly 512 * 127 / (the triangle's x extent). That depends only on
 * how wide the triangle projects, not on how big it is, so a face that lands
 * near edge-on -- as terrain and walls do wholesale at a flat camera pitch --
 * yields a gradient in the millions while sarea is still a perfectly legal
 * nonzero 1 or 2. The base term multiplies that gradient by a screen x, and
 * anything past |x| ~ 800 exceeds INT_MAX:
 *
 *   texshadeblend.persp.textrans.branching.lerp8_v3.u.c:119:
 *     runtime error: signed integer overflow: -2661376 * 1253
 *
 * -2661376 is the gradient, in 8.8: -10396 shade units per pixel on a 0..255
 * range, i.e. the full sweep in a fortieth of a pixel. 1253 is an unremarkable
 * screen x. Only the exactly-zero area is rejected by the walkers, and a
 * sliver of area 1 is what explodes.
 *
 * Clamping the gradient, or culling on a minimum |sarea|, would both change
 * which pixels are drawn on every sliver in the scene. Truncating an unsigned
 * product back to int instead reproduces the reference's value bit for bit,
 * which is what these do. The int <- unsigned conversion is implementation
 * defined rather than undefined, and every compiler this tree is built with
 * defines it as the two's complement wrap Java specifies.
 *
 * Apply them to the whole chain, not just to the multiply that reports first.
 * Once the base has wrapped, the per-row and per-block accumulation of the
 * gradient overflows in its own right.
 */

static inline int
toridraw_wrap_mul(int a, int b)
{
    return (int)((unsigned)a * (unsigned)b);
}

static inline int
toridraw_wrap_add(int a, int b)
{
    return (int)((unsigned)a + (unsigned)b);
}

static inline int
toridraw_wrap_sub(int a, int b)
{
    return (int)((unsigned)a - (unsigned)b);
}

static inline int
toridraw_wrap_shl(int value, int bits)
{
    return (int)((unsigned)value << bits);
}

#endif
