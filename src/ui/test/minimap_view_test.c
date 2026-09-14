/*
 * The minimap's geometry.
 *
 * The failures these check for all leave a map that looks like a map. A dot
 * placed by one convention and a click resolved by another draws correctly and
 * walks you somewhere else; a sign lost in the rotation mirrors the world; a
 * flag left behind by a scene reload marks a tile the player never chose. None
 * of it shows up in a screenshot, which is why it is all arithmetic here.
 */

#include "ui/minimap_view.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/** The identity rotation: the map drawn with north up. */
static struct MinimapRotation
facing_north(void)
{
    struct MinimapRotation r;
    r.sin = 0;
    r.cos = 65536;
    return r;
}

/** A quarter turn. sin/cos tables are 16.16, so a right angle is exact. */
static struct MinimapRotation
turned_quarter(void)
{
    struct MinimapRotation r;
    r.sin = 65536;
    r.cos = 0;
    return r;
}

/** A 152x152 map at (550, 4), the classic fixed-frame placement. */
static struct MinimapView
placed_map(void)
{
    struct MinimapView view;
    MinimapView_Reset(&view);
    MinimapView_Publish(&view, 550, 4, 152, 152, 0);
    return view;
}

static void
test_reset_has_no_map_and_no_flag(void)
{
    struct MinimapView view;

    memset(&view, 0x33, sizeof(view));
    MinimapView_Reset(&view);
    TEST_ASSERT(!view.valid, "a fresh view has not been drawn anywhere");
    TEST_ASSERT(!MinimapView_HasFlag(&view), "a fresh view carries no destination");
}

static void
test_a_click_needs_a_map_on_screen(void)
{
    struct MinimapView view;
    struct MinimapRotation const rotation = facing_north();
    int cx, cy, fx, fz;

    MinimapView_Reset(&view);
    /* Before any frame has drawn the map, the box is all zeroes -- which is a
     * real rectangle at the top-left corner of the screen. Without the valid
     * flag every click in that corner would walk the player somewhere. */
    TEST_ASSERT(
        !MinimapView_ClickToFineOffset(&view, &rotation, 0, 0, &cx, &cy, &fx, &fz),
        "a map that has never been drawn takes no clicks");

    view = placed_map();
    MinimapView_Invalidate(&view);
    TEST_ASSERT(
        !MinimapView_ClickToFineOffset(&view, &rotation, 600, 50, &cx, &cy, &fx, &fz),
        "a map the last frame did not draw takes no clicks");
}

static void
test_invalidating_keeps_the_flag(void)
{
    struct MinimapView view = placed_map();

    /* Switching to a tab that does not show the map must not cancel a walk
     * that is already under way. */
    MinimapView_SetFlag(&view, 21, 34);
    MinimapView_Invalidate(&view);
    TEST_ASSERT(MinimapView_HasFlag(&view), "the destination outlives the frame");
    TEST_ASSERT(view.flag_tile_x == 21 && view.flag_tile_z == 34, "and is where it was");
}

static void
test_only_clicks_inside_the_box_count(void)
{
    struct MinimapView const view = placed_map();
    struct MinimapRotation const rotation = facing_north();
    int cx, cy, fx, fz;

    TEST_ASSERT(
        MinimapView_ClickToFineOffset(&view, &rotation, 550, 4, &cx, &cy, &fx, &fz),
        "the top-left corner is inside the map");
    TEST_ASSERT(
        MinimapView_ClickToFineOffset(&view, &rotation, 701, 155, &cx, &cy, &fx, &fz),
        "the last pixel of the box is inside the map");
    /* Half-open the other way and the map would eat the first column of
     * whatever is drawn beside it. */
    TEST_ASSERT(
        !MinimapView_ClickToFineOffset(&view, &rotation, 702, 80, &cx, &cy, &fx, &fz),
        "one past the right edge is outside");
    TEST_ASSERT(
        !MinimapView_ClickToFineOffset(&view, &rotation, 620, 156, &cx, &cy, &fx, &fz),
        "one past the bottom edge is outside");
    TEST_ASSERT(
        !MinimapView_ClickToFineOffset(&view, &rotation, 549, 80, &cx, &cy, &fx, &fz),
        "one before the left edge is outside");
    TEST_ASSERT(
        !MinimapView_ClickToFineOffset(&view, &rotation, 620, 3, &cx, &cy, &fx, &fz),
        "one above the top edge is outside");
}

static void
test_the_centre_is_the_centre_of_a_box_that_is_not_square(void)
{
    struct MinimapView view;
    struct MinimapRotation const north = facing_north();
    int cx, cy, fx, fz;

    /* A square map cannot tell its own width from its height, so the classic
     * 152x152 frame hides an axis swap completely. A resized or mobile frame
     * is not square, and there the swap puts the map's centre off the map. */
    MinimapView_Reset(&view);
    MinimapView_Publish(&view, 100, 100, 200, 100, 0);
    TEST_ASSERT(
        MinimapView_ClickToFineOffset(&view, &north, 200, 150, &cx, &cy, &fx, &fz),
        "the middle of a wide map is a click");
    TEST_ASSERT(cx == 0, "half the WIDTH along is the horizontal centre");
    TEST_ASSERT(cy == 0, "half the HEIGHT down is the vertical centre");
}

static void
test_a_click_at_the_centre_is_the_players_own_tile(void)
{
    struct MinimapView const view = placed_map();
    struct MinimapRotation const rotation = facing_north();
    int cx, cy, fx, fz;

    TEST_ASSERT(
        MinimapView_ClickToFineOffset(&view, &rotation, 550 + 76, 4 + 76, &cx, &cy, &fx, &fz),
        "the centre is a click");
    TEST_ASSERT(cx == 0 && cy == 0, "the centre is the origin the click is measured from");
    TEST_ASSERT(fx == 0 && fz == 0, "and it asks for no movement at all");
}

static void
test_a_click_resolves_to_the_direction_it_points(void)
{
    struct MinimapView const view = placed_map();
    struct MinimapRotation const north = facing_north();
    int cx, cy, fx, fz;

    /* 32 pixels right of centre, north up. One tile is 4 pixels, so that is 8
     * tiles east: 8 * 128 = 1024 fine units, with no north-south component. */
    MinimapView_ClickToFineOffset(&view, &north, 550 + 76 + 32, 4 + 76, &cx, &cy, &fx, &fz);
    TEST_ASSERT(cx == 32 && cy == 0, "the click is 32 pixels right of centre");
    TEST_ASSERT(fx == 1024, "32 map pixels east is eight tiles east");
    TEST_ASSERT(fz == 0, "and not a step north or south");

    /* Straight UP the screen. The screen's y grows downward and the world's z
     * grows north, so the offset comes back NEGATIVE and the caller subtracts
     * it, walking north. Lose that sign and clicking north walks south, which
     * is a map that is correct and a client that is unusable. */
    MinimapView_ClickToFineOffset(&view, &north, 550 + 76, 4 + 76 - 32, &cx, &cy, &fx, &fz);
    TEST_ASSERT(cy == -32, "the click is 32 pixels above centre");
    TEST_ASSERT(fx == 0, "and not a step east or west");
    TEST_ASSERT(fz == -1024, "up the screen is the far side of the player");
}

static void
test_a_turned_map_turns_the_click_with_it(void)
{
    struct MinimapView const view = placed_map();
    struct MinimapRotation const quarter = turned_quarter();
    int cx, cy, fx, fz;
    int north_fx, north_fz;
    struct MinimapRotation const north = facing_north();

    MinimapView_ClickToFineOffset(
        &view, &north, 550 + 76 + 32, 4 + 76, &cx, &cy, &north_fx, &north_fz);
    MinimapView_ClickToFineOffset(&view, &quarter, 550 + 76 + 32, 4 + 76, &cx, &cy, &fx, &fz);

    /* The same pixel, the map turned a quarter: the ground under it is a
     * quarter turn away too. A rotation applied in the wrong sense still moves
     * the player, just consistently to the wrong side of themselves. */
    TEST_ASSERT(north_fx == 1024 && north_fz == 0, "north-up: due east");
    TEST_ASSERT(fx == 0, "turned: no longer east at all");
    TEST_ASSERT(fz == -1024, "turned: the same distance, a quarter turn round");
}

static void
test_the_yaw_is_wrapped(void)
{
    struct MinimapView view = placed_map();

    /* The camera's angle accumulates without bound, and the tables are indexed
     * by it. An unwrapped angle reads off the end of a 2048-entry table. */
    MinimapView_Publish(&view, 550, 4, 152, 152, 2048 + 300);
    TEST_ASSERT(MinimapView_Yaw(&view) == 300, "a full turn past is the same angle");
    MinimapView_Publish(&view, 550, 4, 152, 152, 2047);
    TEST_ASSERT(MinimapView_Yaw(&view) == 2047, "the last angle of a turn is its own");
}

static void
test_a_dot_at_the_player_sits_under_the_player(void)
{
    struct MinimapRotation const north = facing_north();
    int dx, dy;

    TEST_ASSERT(MinimapView_PlaceDot(&north, 0, 0, 4, 4, &dx, &dy), "zero distance is on the map");
    /* Half the sprite back on each axis. Without it every icon hangs down and
     * right of the thing it marks, by half its own size. */
    TEST_ASSERT(dx == -2 && dy == -2, "a 4x4 icon is centred by backing off two");

    TEST_ASSERT(MinimapView_PlaceDot(&north, 0, 0, 3, 3, &dx, &dy), "an odd icon too");
    TEST_ASSERT(dx == -1 && dy == -1, "an odd icon backs off the truncated half");

    /* Not every map icon is square -- a hull's is long -- and a square one
     * cannot tell which axis its own size was applied to. */
    TEST_ASSERT(MinimapView_PlaceDot(&north, 0, 0, 20, 6, &dx, &dy), "a long icon too");
    TEST_ASSERT(dx == -10, "the horizontal offset backs off half the WIDTH");
    TEST_ASSERT(dy == -3, "the vertical offset backs off half the HEIGHT");
}

static void
test_a_dot_is_placed_at_the_maps_scale(void)
{
    struct MinimapRotation const north = facing_north();
    int dx, dy;

    /* Eight tiles east: 1024 fine units, 32 map pixels. */
    MinimapView_PlaceDot(&north, 1024, 0, 4, 4, &dx, &dy);
    TEST_ASSERT(dx == 32 - 2, "eight tiles east is 32 map pixels east");
    TEST_ASSERT(dy == -2, "and no pixels up or down");

    /* Eight tiles north. The world's z grows north and the screen's y grows
     * down, so this has to come back NEGATIVE. Lose that and every player
     * behind you is drawn in front of you, which is a map that looks fine and
     * is upside down. */
    MinimapView_PlaceDot(&north, 0, 1024, 4, 4, &dx, &dy);
    TEST_ASSERT(dx == -2, "eight tiles north is no pixels east");
    TEST_ASSERT(dy == -32 - 2, "and 32 map pixels UP the screen");
}

static void
test_a_turned_map_turns_its_dots_with_it(void)
{
    struct MinimapRotation const quarter = turned_quarter();
    int dx, dy;

    /* The same thing, the map turned a quarter: it has to land a quarter turn
     * round the centre. A dot rotated in the opposite sense to the click makes
     * the map disagree with itself -- you walk to what you saw, and arrive
     * somewhere else. */
    MinimapView_PlaceDot(&quarter, 1024, 0, 4, 4, &dx, &dy);
    TEST_ASSERT(dx == -2, "turned: no longer east on the map");
    TEST_ASSERT(dy == 32 - 2, "turned: the same distance, a quarter turn round");
}

static void
test_a_dot_beyond_the_radius_is_refused(void)
{
    struct MinimapRotation const north = facing_north();
    int const radius_fine = MINIMAP_DOT_RADIUS_PIXELS * MINIMAP_FINE_PER_PIXEL;
    int dx, dy;

    TEST_ASSERT(
        MinimapView_PlaceDot(&north, radius_fine, 0, 4, 4, &dx, &dy),
        "exactly at the radius is still on the map");
    TEST_ASSERT(
        !MinimapView_PlaceDot(&north, radius_fine + MINIMAP_FINE_PER_PIXEL, 0, 4, 4, &dx, &dy),
        "one pixel past the radius is off it");
    TEST_ASSERT(
        !MinimapView_PlaceDot(&north, 0, -radius_fine - MINIMAP_FINE_PER_PIXEL, 4, 4, &dx, &dy),
        "and in the other direction too");

    /* The radius is a circle, not a box. Along the diagonal the box reaches
     * about 1.41 times further, and a dot out there sits on the frame around
     * the map rather than on the map. */
    {
        int const diagonal = (radius_fine * 3) / 4;
        TEST_ASSERT(
            !MinimapView_PlaceDot(&north, diagonal, diagonal, 4, 4, &dx, &dy),
            "the corner of the box is outside the circle");
    }
}

static void
test_the_flag_moves_with_a_reloaded_scene(void)
{
    struct MinimapView view = placed_map();

    MinimapView_SetFlag(&view, 40, 50);
    /* The scene's origin moved eight tiles east; the flag marks the same
     * ground, so its scene coordinate has to move the other way. */
    TEST_ASSERT(MinimapView_RebaseFlag(&view, 8, 0, 104), "the flag survived the reload");
    TEST_ASSERT(view.flag_tile_x == 32, "and slid with the scene");
    TEST_ASSERT(view.flag_tile_z == 50, "on the axis that moved only");
}

static void
test_a_flag_left_behind_is_dropped(void)
{
    struct MinimapView view = placed_map();

    MinimapView_SetFlag(&view, 3, 50);
    /* Clamped instead of dropped, this would mark the very edge of the new
     * scene -- a destination nobody chose, which the player then walks to. */
    TEST_ASSERT(!MinimapView_RebaseFlag(&view, 8, 0, 104), "the flag did not survive");
    TEST_ASSERT(!MinimapView_HasFlag(&view), "and is gone rather than clamped");
    /* Both coordinates, not just the one the sentinel is read from. Every
     * reader of the flag copies the PAIR out and each gates on x alone by
     * convention; a z left behind is one forgotten guard away from being the
     * coordinate a walk is sent to. */
    TEST_ASSERT(view.flag_tile_z < 0, "a cleared flag keeps no coordinate at all");

    MinimapView_SetFlag(&view, 40, 100);
    TEST_ASSERT(!MinimapView_RebaseFlag(&view, 0, -8, 104), "off the far edge too");
    TEST_ASSERT(!MinimapView_HasFlag(&view), "and gone");

    /* Nothing to move is not a failed move, but it must not invent one. A
     * scene that slid WEST moves every coordinate up, and the sentinel is a
     * coordinate: without a test for "is there a flag at all", the absence of
     * one turns into a flag a couple of tiles from the corner, and the player
     * walks to it. */
    TEST_ASSERT(!MinimapView_RebaseFlag(&view, 8, 8, 104), "no flag, nothing to rebase");
    TEST_ASSERT(!MinimapView_HasFlag(&view), "and still no flag");
    TEST_ASSERT(!MinimapView_RebaseFlag(&view, -4, -4, 104), "a westward reload invents none");
    TEST_ASSERT(!MinimapView_HasFlag(&view), "and still no flag");
}

static void
test_the_dot_buffer_hands_out_blank_slots(void)
{
    struct MinimapDots dots;
    struct UITreeMinimapDot* dot;
    int i;

    MinimapDots_Reset(&dots);
    dot = MinimapDots_Push(&dots);
    TEST_ASSERT(dot != NULL, "an empty frame has room");
    TEST_ASSERT(dots.count == 1, "and the push counts");
    dot->rotate = 300;
    dot->color = 0xFF00FF00u;

    /* The array outlives the frame. A slot handed back still carrying the
     * rotation a ship's icon left in it spins whatever lands there next, which
     * reads as an animation nobody wrote. */
    MinimapDots_Reset(&dots);
    TEST_ASSERT(dots.count == 0, "a reset frame is empty");
    dot = MinimapDots_Push(&dots);
    TEST_ASSERT(dot->rotate == 0, "the slot comes back without its last rotation");
    TEST_ASSERT(dot->color == 0, "or its last colour");

    MinimapDots_Reset(&dots);
    for( i = 0; i < MINIMAP_DOTS_MAX; i++ )
        TEST_ASSERT(MinimapDots_Push(&dots) != NULL, "a frame holds its full complement");
    /* Refused, not wrapped: a wrapped push would overwrite the dots already
     * placed this frame, so a crowded map would lose the icons it drew first. */
    TEST_ASSERT(MinimapDots_Push(&dots) == NULL, "one past full is refused");
    TEST_ASSERT(dots.count == MINIMAP_DOTS_MAX, "and does not grow the frame");
}

int
main(void)
{
    test_reset_has_no_map_and_no_flag();
    test_a_click_needs_a_map_on_screen();
    test_invalidating_keeps_the_flag();
    test_only_clicks_inside_the_box_count();
    test_the_centre_is_the_centre_of_a_box_that_is_not_square();
    test_a_click_at_the_centre_is_the_players_own_tile();
    test_a_click_resolves_to_the_direction_it_points();
    test_a_turned_map_turns_the_click_with_it();
    test_the_yaw_is_wrapped();
    test_a_dot_at_the_player_sits_under_the_player();
    test_a_dot_is_placed_at_the_maps_scale();
    test_a_turned_map_turns_its_dots_with_it();
    test_a_dot_beyond_the_radius_is_refused();
    test_the_flag_moves_with_a_reloaded_scene();
    test_a_flag_left_behind_is_dropped();
    test_the_dot_buffer_hands_out_blank_slots();

    if( g_failures )
    {
        printf("minimap_view: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("minimap_view: all checks passed\n");
    return 0;
}
