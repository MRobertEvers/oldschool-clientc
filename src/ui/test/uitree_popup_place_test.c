/*
 * Where a settings popup goes when it opens beside the row that opened it.
 *
 * Placement is the kind of thing that is obviously right until a row is near
 * an edge, and then the popup is half off screen or sitting on top of the
 * swatch the player is watching. Both the All Settings colour picker and its
 * number-entry twin used to do this arithmetic inline, identically, with two
 * different numbers -- so neither could be checked and a fix to one would not
 * reach the other.
 *
 * What is asserted:
 *
 *   - the popup goes to the LEFT of its anchor, which is the rule that stops
 *     it covering the swatch or field being edited. Those are docked on the
 *     settings panel's right edge, so left is the only side with room.
 *   - with no anchor it is centred horizontally and a quarter down, which is
 *     where a dialogue with nothing to point at belongs.
 *   - every edge clamps: off the left, off the top, off the right.
 *   - the BOTTOM clamp is tighter than the canvas, and is a parameter rather
 *     than a constant. The colour picker's axis popup drops below the panel,
 *     so it needs room under itself; the number entry does not, and may sit
 *     lower. Both fractions are tested, because a shared constant here would
 *     silently move one of the two.
 *   - the clamps compose: an anchor at the far corner is pulled back in on
 *     both axes at once.
 *
 * Build and run:
 *   make -C src test-uitree-popup-place
 */

#include "ui/uitree_popup_place.h"

#include <stdio.h>

/* The fixed 765x503 frame these panels are laid out over. */
#define CANVAS_W 765
#define CANVAS_H 503
#define GAP 8

/* The two callers, by their real numbers. */
#define COLOUR_W 230
#define COLOUR_LIMIT_NUM 2
#define COLOUR_LIMIT_DEN 3
#define NUMBER_W 200
#define NUMBER_LIMIT_NUM 3
#define NUMBER_LIMIT_DEN 4

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

static struct UIPopupPlacement
colour_at(
    int have_anchor,
    int anchor_x,
    int anchor_y)
{
    return UITree_PlacePopupBesideAnchor(
        CANVAS_W,
        CANVAS_H,
        COLOUR_W,
        have_anchor,
        anchor_x,
        anchor_y,
        GAP,
        COLOUR_LIMIT_NUM,
        COLOUR_LIMIT_DEN);
}

static void
test_it_sits_left_of_its_anchor(void)
{
    /* A row comfortably inside the canvas: left of the swatch, a gap above. */
    struct UIPopupPlacement placement = colour_at(1, 600, 200);

    CHECK(
        placement.x == 600 - COLOUR_W - GAP,
        "x is %d, want the anchor minus the width and the gap (%d)",
        placement.x,
        600 - COLOUR_W - GAP);
    CHECK(placement.y == 200 - GAP, "y is %d, want %d", placement.y, 200 - GAP);
    CHECK(
        placement.x + COLOUR_W <= 600,
        "the popup overlaps its own anchor; the swatch being edited is hidden");
}

static void
test_with_no_anchor_it_is_centred(void)
{
    struct UIPopupPlacement placement = colour_at(0, 0, 0);

    CHECK(
        placement.x == (CANVAS_W - COLOUR_W) / 2,
        "x is %d, want centred (%d)",
        placement.x,
        (CANVAS_W - COLOUR_W) / 2);
    CHECK(placement.y == CANVAS_H / 4, "y is %d, want a quarter down", placement.y);

    /* The anchor coordinates are ignored, not used, when there is no anchor --
     * a caller passing stale values from a destroyed row must not move it. */
    {
        struct UIPopupPlacement stale = colour_at(0, 9999, 9999);

        CHECK(
            stale.x == placement.x && stale.y == placement.y,
            "coordinates were used despite have_anchor being false");
    }
}

static void
test_every_edge_clamps(void)
{
    /* A row near the left edge would put the popup off screen. */
    {
        struct UIPopupPlacement placement = colour_at(1, 10, 200);

        CHECK(placement.x == 0, "x is %d off the left edge, want 0", placement.x);
    }

    /* A row at the top. */
    {
        struct UIPopupPlacement placement = colour_at(1, 600, 4);

        CHECK(placement.y == 0, "y is %d above the top edge, want 0", placement.y);
    }

    /*
     * An anchor PAST the right edge. Reachable -- a component's absolute box
     * can sit outside the canvas while its panel is being laid out -- and it
     * is the only way the right clamp fires at all, since the popup is
     * otherwise always placed to the left of something that is on screen.
     * Without this case the right clamp is dead code that still compiles.
     */
    {
        struct UIPopupPlacement placement = colour_at(1, CANVAS_W + 35, 200);

        CHECK(
            placement.x == CANVAS_W - COLOUR_W,
            "an off-canvas anchor gave x %d, want the right edge %d",
            placement.x,
            CANVAS_W - COLOUR_W);
        CHECK(
            placement.x + COLOUR_W <= CANVAS_W,
            "the popup hangs off the right edge (x %d)",
            placement.x);
    }

    /*
     * A popup WIDER than its canvas ends up at a negative x, because the left
     * clamp runs before the right one and the right one then pushes it back
     * out. Pinned rather than fixed: it cannot happen at either real size
     * (230 and 200 against 765), and the clamp order is what keeps the
     * ordinary off-the-left case landing at 0 rather than somewhere arbitrary.
     * Reordering to "fix" this would move the common case.
     */
    {
        struct UIPopupPlacement placement = UITree_PlacePopupBesideAnchor(
            200, CANVAS_H, 280, 0, 0, 0, GAP, COLOUR_LIMIT_NUM, COLOUR_LIMIT_DEN);

        CHECK(
            placement.x == 200 - 280,
            "a popup wider than the canvas gave x %d, want %d",
            placement.x,
            200 - 280);
    }

    /* Both at once: an anchor in the far corner. */
    {
        struct UIPopupPlacement placement = colour_at(1, 4, CANVAS_H - 4);

        CHECK(placement.x == 0, "corner anchor x is %d, want 0", placement.x);
        CHECK(
            placement.y == CANVAS_H * COLOUR_LIMIT_NUM / COLOUR_LIMIT_DEN,
            "corner anchor y is %d, want the bottom limit %d",
            placement.y,
            CANVAS_H * COLOUR_LIMIT_NUM / COLOUR_LIMIT_DEN);
    }
}

static void
test_the_bottom_limit_is_per_caller(void)
{
    int const low_anchor_y = CANVAS_H - 20;
    struct UIPopupPlacement colour = colour_at(1, 600, low_anchor_y);
    struct UIPopupPlacement number = UITree_PlacePopupBesideAnchor(
        CANVAS_W,
        CANVAS_H,
        NUMBER_W,
        1,
        600,
        low_anchor_y,
        GAP,
        NUMBER_LIMIT_NUM,
        NUMBER_LIMIT_DEN);

    CHECK(
        colour.y == CANVAS_H * COLOUR_LIMIT_NUM / COLOUR_LIMIT_DEN,
        "the colour picker's bottom limit is %d, want %d",
        colour.y,
        CANVAS_H * COLOUR_LIMIT_NUM / COLOUR_LIMIT_DEN);
    CHECK(
        number.y == CANVAS_H * NUMBER_LIMIT_NUM / NUMBER_LIMIT_DEN,
        "the number entry's bottom limit is %d, want %d",
        number.y,
        CANVAS_H * NUMBER_LIMIT_NUM / NUMBER_LIMIT_DEN);

    /*
     * And they DIFFER. The colour picker's axis popup drops below the panel,
     * so it stops higher; the number entry has nothing under it. A shared
     * constant here would silently move one of the two, which is exactly what
     * putting this arithmetic in one place invites.
     */
    CHECK(
        colour.y < number.y,
        "the two bottom limits are the same (%d); the colour picker must stop "
        "higher to leave room for its axis popup",
        colour.y);

    /* Under the limit, the anchor is followed rather than clamped. */
    {
        struct UIPopupPlacement high = colour_at(1, 600, 100);

        CHECK(high.y == 100 - GAP, "a high anchor was clamped to %d", high.y);
    }
}

int
main(void)
{
    test_it_sits_left_of_its_anchor();
    test_with_no_anchor_it_is_centred();
    test_every_edge_clamps();
    test_the_bottom_limit_is_per_caller();

    if( g_failures )
    {
        printf("uitree_popup_place_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("uitree_popup_place_test: OK\n");
    return 0;
}
