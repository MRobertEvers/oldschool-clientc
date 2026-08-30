#ifndef TORIDRAW_GRAPHICS_WINDING_H
#define TORIDRAW_GRAPHICS_WINDING_H

/**
 * The screen-space winding test, in one place.
 *
 * Six call sites computed this cross product from six coordinates, and every
 * one of them spelled it out again: the bucket sort's two copies, the raster
 * face test, the HD renderer, the post-clip test, and the z-buffer triangle.
 * Six copies of one expression is six chances for them to disagree, and they
 * must not -- a clipped face culled by a different rule from its unclipped
 * neighbours tears the silhouette.
 *
 * Mind the types, because they are the whole point and they are easy to
 * misread. The deltas are `int`; only the PRODUCT is widened. On i686 that is
 * the 32x32->64 idiom, which is a single one-operand `imull` with the result
 * in EDX:EAX -- not 64-bit arithmetic. Declaring the deltas `int64_t` instead,
 * as this code used to, asks for a real 64x64->64 multiply that i686 has no
 * instruction for, so gcc expands each product into `mull` plus two `imull`
 * plus the sign-extension and borrow chains: measured at 80 instructions and a
 * 28-byte stack frame, against 38 and 12 bytes for the form below.
 *
 * The widened product is exact for every int32 input -- verified against the
 * int64-delta form over 12M random triangles from +/-600 to +/-1048576 with
 * zero disagreements. A truncating 32-bit product is NOT: it starts diverging
 * past deltas of about +/-32k, and the clip path computes winding on pre-clip
 * projected coordinates that the viewport does not bound. That form was
 * measured, found to save 11 instructions, and rejected -- the whole winding
 * population is below the frame's noise floor, so there is nothing to buy with
 * an unprovable range assumption.
 */

#include <stdbool.h>
#include <stdlib.h>

/** TORIDRAW_FLIP_WINDING=1: cull the opposite screen-space winding.
 *  A bisect knob for the rs2012 QBD import - if a ported model renders
 *  inside-out (interior faces surviving, exterior culled), its faces are wound
 *  against the engine convention and the repair belongs in the importer, not
 *  here. See docs/qbd_toridraw_streaks_debug.md. */
static int
toridraw_flip_winding(void)
{
    static int on = -1;
    if( on < 0 )
    {
        const char* v = getenv("TORIDRAW_FLIP_WINDING");
        on = (v && v[0] && v[0] != '0') ? 1 : 0;
    }
    return on;
}

/**
 * Twice the signed area of triangle (a, b, c) in screen space, taking b as the
 * origin. Positive is one winding, negative the other; zero is degenerate.
 */
static inline long long
toridraw_winding_2d(int ax, int ay, int bx, int by, int cx, int cy)
{
    const int dx1 = ax - bx;
    const int dy1 = ay - by;
    const int dx2 = cx - bx;
    const int dy2 = cy - by;

    return (long long)dx1 * dy2 - (long long)dy1 * dx2;
}

/** True when this screen-space winding should be drawn. */
static inline bool
toridraw_winding_front_facing(long long winding)
{
    return toridraw_flip_winding() ? (winding < 0) : (winding > 0);
}

/** The common case: compute the winding of (a, b, c) and test it. */
static inline bool
toridraw_winding_2d_front_facing(
    int ax, int ay, int bx, int by, int cx, int cy)
{
    return toridraw_winding_front_facing(
        toridraw_winding_2d(ax, ay, bx, by, cx, cy));
}

/*
 * The same two, with the flip already in hand.
 *
 * TORIDRAW_FLIP_WINDING is a process constant read once into a static, but the
 * read is a function call with a branch on that static, and the plain forms
 * above make it PER FACE -- inside every cull loop in the depth sort, and again
 * in the raster walk. The flat sort already hoists it out of its own loop; these
 * let every other per-face caller do the same, and say in the signature that
 * the value is loop-invariant rather than leaving it to the optimiser to prove
 * across an opaque getenv-caching call.
 */
static inline bool
toridraw_winding_front_facing_flip(long long winding, int flip)
{
    return flip ? (winding < 0) : (winding > 0);
}

static inline bool
toridraw_winding_2d_front_facing_flip(
    int ax, int ay, int bx, int by, int cx, int cy, int flip)
{
    return toridraw_winding_front_facing_flip(
        toridraw_winding_2d(ax, ay, bx, by, cx, cy), flip);
}

#endif
