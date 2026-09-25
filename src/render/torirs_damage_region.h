#ifndef SRC_RENDER_TORIRS_DAMAGE_REGION_H
#define SRC_RENDER_TORIRS_DAMAGE_REGION_H

/**
 * What a retained frame still has to present.
 *
 * When the UI tree draws nothing new, the frame before it is still on screen
 * and most of it is still correct. The parts that are not -- the world
 * viewport, the minimap, the overlays inside them -- are the damage, and
 * presenting only those is the whole saving.
 *
 * The region carries the damage twice over, because the two answers cost
 * differently:
 *
 *   - one BOX, the union of everything live. Always correct, always the
 *     answer if anything goes wrong, and one BitBlt.
 *   - up to four RECTS, the live areas folded together where they touch.
 *     Tighter in pixels and more calls, which is why the box is the default
 *     -- see the caller's TORIRS_DAMAGE_RECTS arm. Overflowing four is not
 *     an error: the list poisons itself and the box stands.
 *
 * Geometry only. Nothing here reads a frame, a clock or the environment;
 * deciding WHICH descriptors are live, and whether the rect arm is switched
 * on, belongs to the caller that can see them.
 *
 * The usual order is Reset, then Add per live area, then Clamp once against
 * the canvas -- Clamp is what decides the region is not worth having (empty,
 * or the whole canvas anyway) and says so by clearing `valid`.
 */

#include <stdbool.h>

/** Four is above the two an in-world frame actually produces (world,
 *  minimap); past that the box is used instead, which is always correct and
 *  only ever does more work. */
#define TORIRS_DAMAGE_RECT_MAX 4

struct ToriRS_DamageRect
{
    int x;
    int y;
    int w;
    int h;
};

struct ToriRS_DamageRegion
{
    /** 0 when the whole canvas must be treated as damaged. Every other field
     *  is meaningless while this is 0. */
    int valid;
    int x;
    int y;
    int w;
    int h;
    struct ToriRS_DamageRect rects[TORIRS_DAMAGE_RECT_MAX];
    /** -1 once more than TORIRS_DAMAGE_RECT_MAX disjoint areas were added:
     *  the list is then abandoned in favour of the box. */
    int rect_count;
};

/** Forget the previous frame's damage. Both halves. */
void
ToriRS_DamageRegionReset(struct ToriRS_DamageRegion* region);

/**
 * Add one live area, in canvas pixels.
 *
 * Grows the box, and folds the area into a rect it already touches rather
 * than appending a second copy -- entity overlays carry exactly the world
 * viewport's box, so without the fold the list is full of duplicates before
 * it reaches the two rects that matter. An empty area is ignored.
 */
void
ToriRS_DamageRegionAdd(
    struct ToriRS_DamageRegion* region,
    int x,
    int y,
    int w,
    int h);

/**
 * Clamp onto a canvas of `width` x `height`, and decide whether the region is
 * worth having at all.
 *
 * Clears `valid` when the box is empty after clamping, or when it covers the
 * canvas anyway -- a box that is the whole screen is not worth the extra clip
 * test in every draw, nor the second present path.
 *
 * `keep_rects` false drops the rect list and leaves the box. So does a rect
 * that clamps to nothing: the box still covers those pixels, so the frame
 * stays correct and merely does the larger amount of work.
 */
void
ToriRS_DamageRegionClamp(
    struct ToriRS_DamageRegion* region,
    int width,
    int height,
    bool keep_rects);

/** The box, or 0 when the whole canvas must be presented. */
int
ToriRS_DamageRegionBox(
    struct ToriRS_DamageRegion const* region,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

/**
 * The rect list, or 0 when there is none and the box should be used instead.
 * `out_rects` is only written when the return value is positive.
 */
int
ToriRS_DamageRegionRects(
    struct ToriRS_DamageRegion const* region,
    struct ToriRS_DamageRect const** out_rects);

#endif /* SRC_RENDER_TORIRS_DAMAGE_REGION_H */
