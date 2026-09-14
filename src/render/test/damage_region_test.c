/*
 * The damage region a retained frame presents.
 *
 * Getting this wrong does not crash. It leaves stale pixels on screen that
 * never repair, or it silently presents the whole canvas and the saving the
 * region exists for quietly disappears -- and both look like a working client
 * from the outside, which is why the arithmetic is worth pinning here rather
 * than watching for.
 *
 * What is asserted:
 *
 *   - the box is the union of everything added, and clamps onto the canvas
 *     from either edge.
 *   - the two ways a region says "present everything": an empty box after
 *     clamping, and a box that covers the canvas anyway. Both clear `valid`,
 *     because a box that is always the screen bounds nothing and still costs
 *     a clip test in every draw.
 *   - rects fold when they touch and stay separate when they do not. This is
 *     the reason the list exists: the world viewport and the minimap sit at
 *     opposite ends of the same rows, so their union swallows the sidebar
 *     between them. The test states that as the inequality it is.
 *   - entity overlays carry exactly the viewport's box, so adding the same
 *     area repeatedly must not produce a list of duplicates.
 *   - more than four disjoint areas poisons the list rather than truncating
 *     it. A truncated list is the dangerous outcome: it presents some of the
 *     damage and looks correct until the part it dropped changes.
 *   - a rect that clamps away drops the whole list and leaves the box, which
 *     is slower and always right.
 *
 * Build and run:
 *   make -C src test-damage-region
 */

#include "render/torirs_damage_region.h"

#include <stdio.h>

#define CANVAS_W 765
#define CANVAS_H 503

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
check_box(
    struct ToriRS_DamageRegion const* region,
    int want_x,
    int want_y,
    int want_w,
    int want_h,
    char const* what)
{
    int x = -1;
    int y = -1;
    int w = -1;
    int h = -1;

    CHECK(ToriRS_DamageRegionBox(region, &x, &y, &w, &h) == 1, "%s: box not valid", what);
    CHECK(
        x == want_x && y == want_y && w == want_w && h == want_h,
        "%s: box %d,%d %dx%d, want %d,%d %dx%d",
        what,
        x,
        y,
        w,
        h,
        want_x,
        want_y,
        want_w,
        want_h);
}

static void
test_box_is_the_union(void)
{
    struct ToriRS_DamageRegion region;
    int x;
    int y;
    int w;
    int h;

    ToriRS_DamageRegionReset(&region);
    CHECK(
        ToriRS_DamageRegionBox(&region, &x, &y, &w, &h) == 0,
        "a reset region claimed a valid box");

    ToriRS_DamageRegionAdd(&region, 10, 20, 30, 40);
    check_box(&region, 10, 20, 30, 40, "one area");

    /* Grows in every direction, not just forward. */
    ToriRS_DamageRegionAdd(&region, 5, 100, 10, 10);
    check_box(&region, 5, 20, 35, 90, "two areas");

    /* An empty area is not a point at the origin; it is nothing. */
    ToriRS_DamageRegionAdd(&region, 0, 0, 0, 0);
    ToriRS_DamageRegionAdd(&region, 0, 0, -5, 10);
    check_box(&region, 5, 20, 35, 90, "after empty adds");
}

static void
test_clamp_onto_the_canvas(void)
{
    struct ToriRS_DamageRegion region;
    int x;
    int y;
    int w;
    int h;

    /* Off the top-left. */
    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, -30, -20, 100, 100);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);
    check_box(&region, 0, 0, 70, 80, "clamped at the top-left");

    /* Off the bottom-right. */
    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, CANVAS_W - 10, CANVAS_H - 10, 100, 100);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);
    check_box(&region, CANVAS_W - 10, CANVAS_H - 10, 10, 10, "clamped at the bottom-right");

    /* Entirely off the canvas: nothing to present, so nothing is claimed. */
    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, -500, -500, 100, 100);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);
    CHECK(
        ToriRS_DamageRegionBox(&region, &x, &y, &w, &h) == 0,
        "an off-canvas box stayed valid");

    /* Exactly abutting the left edge from outside: the clamp leaves width 0,
     * not a negative width. A zero-width box is still nothing to present, and
     * testing only the negative case leaves this boundary free to be wrong. */
    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, -100, 10, 100, 50);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);
    CHECK(
        ToriRS_DamageRegionBox(&region, &x, &y, &w, &h) == 0,
        "a box that clamped to zero width stayed valid (w %d)",
        w);

    /* And the same at the bottom edge, in height. */
    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, 10, CANVAS_H, 50, 100);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);
    CHECK(
        ToriRS_DamageRegionBox(&region, &x, &y, &w, &h) == 0,
        "a box that clamped to zero height stayed valid (h %d)",
        h);

    /* The whole canvas is not worth bounding. */
    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, 0, 0, CANVAS_W, CANVAS_H);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);
    CHECK(
        ToriRS_DamageRegionBox(&region, &x, &y, &w, &h) == 0,
        "a full-canvas box stayed valid, so every draw pays a clip test for nothing");

    /* One pixel short of the canvas still is. */
    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, 0, 0, CANVAS_W, CANVAS_H - 1);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);
    check_box(&region, 0, 0, CANVAS_W, CANVAS_H - 1, "one row short of the canvas");
}

static void
test_rects_beat_their_own_bounding_box(void)
{
    /* The shape that motivates the list: the world viewport on the left and
     * the minimap at the top right, with the sidebar strip between them
     * untouched. Their union is most of the screen; the two rects are not. */
    struct ToriRS_DamageRegion region;
    struct ToriRS_DamageRect const* rects = NULL;
    int count;
    long box_area;
    long rect_area;

    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, 4, 4, 512, 334);   /* world viewport */
    ToriRS_DamageRegionAdd(&region, 570, 9, 146, 151); /* minimap */
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);

    count = ToriRS_DamageRegionRects(&region, &rects);
    CHECK(count == 2, "rect count %d, want 2 (the two do not touch)", count);
    if( count == 2 )
    {
        rect_area = (long)rects[0].w * rects[0].h + (long)rects[1].w * rects[1].h;
        box_area = (long)region.w * region.h;
        CHECK(
            rect_area < box_area,
            "rects cover %ld px and their box %ld px -- the list should be the tighter answer",
            rect_area,
            box_area);
    }

    /* Entity overlays carry exactly the viewport's box. Adding it again must
     * fold, or the list is duplicates before it reaches the pair that count. */
    ToriRS_DamageRegionAdd(&region, 4, 4, 512, 334);
    ToriRS_DamageRegionAdd(&region, 4, 4, 512, 334);
    ToriRS_DamageRegionAdd(&region, 100, 100, 20, 20); /* inside the viewport */
    count = ToriRS_DamageRegionRects(&region, &rects);
    CHECK(count == 2, "rect count %d after re-adding the same area, want 2", count);

    /* The fold is strict overlap, so two areas that merely share an edge stay
     * two rects. Pinned because it is the boundary of the predicate and both
     * answers present the same pixels -- which is exactly the kind of
     * difference a later tidy-up flips without noticing. */
    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, 0, 0, 100, 50);
    ToriRS_DamageRegionAdd(&region, 100, 0, 50, 50);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);
    count = ToriRS_DamageRegionRects(&region, &rects);
    CHECK(count == 2, "abutting rect count %d, want 2 (the fold is strict overlap)", count);

    /* One pixel of real overlap does fold them. */
    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, 0, 0, 100, 50);
    ToriRS_DamageRegionAdd(&region, 99, 0, 50, 50);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);
    count = ToriRS_DamageRegionRects(&region, &rects);
    CHECK(count == 1, "overlapping rect count %d, want 1", count);
    if( count == 1 )
        CHECK(
            rects[0].x == 0 && rects[0].w == 149,
            "folded rect x %d w %d, want 0 and 149",
            rects[0].x,
            rects[0].w);
}

static void
test_overflow_poisons_rather_than_truncates(void)
{
    struct ToriRS_DamageRegion region;
    struct ToriRS_DamageRect const* rects = NULL;

    ToriRS_DamageRegionReset(&region);
    for( int i = 0; i < 6; i++ )
        ToriRS_DamageRegionAdd(&region, i * 50, i * 50, 20, 20);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);

    CHECK(
        ToriRS_DamageRegionRects(&region, &rects) == 0,
        "six disjoint areas produced a rect list; a truncated list presents "
        "some of the damage and looks correct until the dropped part changes");
    check_box(&region, 0, 0, 270, 270, "box after rect overflow");
}

static void
test_the_list_is_optional_but_the_box_is_not(void)
{
    struct ToriRS_DamageRegion region;
    struct ToriRS_DamageRect const* rects = NULL;

    /* keep_rects false: the caller's rect arm is switched off. */
    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, 4, 4, 100, 100);
    ToriRS_DamageRegionAdd(&region, 400, 4, 100, 100);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, false);
    CHECK(ToriRS_DamageRegionRects(&region, &rects) == 0, "rects survived keep_rects = false");
    check_box(&region, 4, 4, 496, 100, "box survives keep_rects = false");

    /* A rect that clamps away takes the list with it, not just itself. */
    ToriRS_DamageRegionReset(&region);
    ToriRS_DamageRegionAdd(&region, 4, 4, 100, 100);
    ToriRS_DamageRegionAdd(&region, CANVAS_W + 50, 4, 20, 20);
    ToriRS_DamageRegionClamp(&region, CANVAS_W, CANVAS_H, true);
    CHECK(
        ToriRS_DamageRegionRects(&region, &rects) == 0,
        "a rect clamped to nothing left a partial list standing");
    CHECK(region.valid, "the box should survive a dropped rect list");
}

int
main(void)
{
    test_box_is_the_union();
    test_clamp_onto_the_canvas();
    test_rects_beat_their_own_bounding_box();
    test_overflow_poisons_rather_than_truncates();
    test_the_list_is_optional_but_the_box_is_not();

    if( g_failures )
    {
        printf("damage_region_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("damage_region_test: OK\n");
    return 0;
}
