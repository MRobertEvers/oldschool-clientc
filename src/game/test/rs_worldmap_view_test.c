/*
 * Which regions of the world map are on screen, and in what order.
 *
 * The set failing looks like a map that ends in a hard line one region short of
 * its own frame. The order failing looks like a map that fills in from the
 * corner while the place you are looking at stays blank -- and then loads it
 * once you have panned past. Neither is an error anyone can report as one.
 */

#include "game/rs_worldmap_view.h"

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

/** An area big enough that nothing below is clamped by accident. */
static struct RS_WorldMapRegionBounds
open_world(void)
{
    struct RS_WorldMapRegionBounds bounds;
    bounds.low_x = 0;
    bounds.low_y = 0;
    bounds.high_x = 200;
    bounds.high_y = 200;
    return bounds;
}

/**
 * A view of `box` pixels looking at the centre of region (50, 60), drawn at
 * one screen pixel per map tile.
 */
static struct RS_WorldMapViewport
looking_at_region(int region_x, int region_y, int box_w, int box_h)
{
    struct RS_WorldMapViewport viewport;
    viewport.display_tile_x =
        region_x * RS_WORLDMAP_REGION_TILES_X + RS_WORLDMAP_REGION_TILES_X / 2;
    viewport.display_tile_z =
        region_y * RS_WORLDMAP_REGION_TILES_Z + RS_WORLDMAP_REGION_TILES_Z / 2;
    viewport.box_w = box_w;
    viewport.box_h = box_h;
    viewport.region_px = RS_WORLDMAP_REGION_TILES_X;
    return viewport;
}

static bool
visits_contain(struct RS_WorldMapVisits const* visits, int region_x, int region_y)
{
    int i;
    for( i = 0; i < visits->count; i++ )
        if( visits->items[i].region_x == region_x && visits->items[i].region_y == region_y )
            return true;
    return false;
}

static int
index_of(struct RS_WorldMapVisits const* visits, int region_x, int region_y)
{
    int i;
    for( i = 0; i < visits->count; i++ )
        if( visits->items[i].region_x == region_x && visits->items[i].region_y == region_y )
            return i;
    return -1;
}

static void
test_a_viewport_with_no_area_asks_for_nothing(void)
{
    struct RS_WorldMapVisits visits;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    struct RS_WorldMapViewport viewport = looking_at_region(50, 60, 640, 480);

    /* Real states while a layout is still being sized, and every number in the
     * build divides by the zoom. */
    viewport.region_px = 0;
    TEST_ASSERT(RS_WorldMapVisits_Build(&visits, &bounds, &viewport) == 0, "no zoom, no regions");
    TEST_ASSERT(visits.count == 0, "and the list says so");

    viewport = looking_at_region(50, 60, 0, 480);
    TEST_ASSERT(RS_WorldMapVisits_Build(&visits, &bounds, &viewport) == 0, "no width, no regions");
    viewport = looking_at_region(50, 60, 640, 0);
    TEST_ASSERT(RS_WorldMapVisits_Build(&visits, &bounds, &viewport) == 0, "no height, no regions");
}

static void
test_the_region_under_the_view_is_first(void)
{
    struct RS_WorldMapVisits visits;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    struct RS_WorldMapViewport const viewport = looking_at_region(50, 60, 640, 480);

    RS_WorldMapVisits_Build(&visits, &bounds, &viewport);
    TEST_ASSERT(visits.count > 1, "a 640x480 view sees more than one region");
    /* First is the whole point: the frame's bake and load allowance goes to
     * whatever is visited first, and what the player is looking at has to be
     * what gets it. */
    TEST_ASSERT(visits.items[0].region_x == 50, "the region under the view comes first");
    TEST_ASSERT(visits.items[0].region_y == 60, "on both axes");
    TEST_ASSERT(visits.items[0].distance == 0, "at no distance from itself");
}

static void
test_the_order_is_nearest_first(void)
{
    struct RS_WorldMapVisits visits;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    struct RS_WorldMapViewport const viewport = looking_at_region(50, 60, 640, 480);
    int i;

    RS_WorldMapVisits_Build(&visits, &bounds, &viewport);
    for( i = 1; i < visits.count; i++ )
        TEST_ASSERT(
            visits.items[i - 1].distance <= visits.items[i].distance,
            "every region is at least as near as the one after it");

    /* Its immediate neighbours outrank a region three away, whichever corner
     * that one happens to be scanned from. */
    TEST_ASSERT(index_of(&visits, 51, 60) < index_of(&visits, 53, 60), "one east beats three east");
    TEST_ASSERT(index_of(&visits, 50, 61) < index_of(&visits, 47, 60), "one north beats three west");
}

static void
test_a_scan_order_would_not_do(void)
{
    struct RS_WorldMapVisits visits;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    struct RS_WorldMapViewport const viewport = looking_at_region(50, 60, 640, 480);
    int first_index;

    RS_WorldMapVisits_Build(&visits, &bounds, &viewport);
    /* Scanning top-left onwards puts the far corner of the view first, and the
     * allowance goes there: a map that fills in from the corner while the
     * centre stays blank. Naming the corner explicitly is what separates
     * "sorted somehow" from "sorted by distance". */
    first_index = index_of(&visits, visits.items[visits.count - 1].region_x, visits.items[visits.count - 1].region_y);
    TEST_ASSERT(first_index == visits.count - 1, "the furthest region is last, not first");
    TEST_ASSERT(
        visits.items[visits.count - 1].distance > visits.items[0].distance,
        "and it really is further away");
}

static void
test_regions_the_same_distance_away_have_a_stated_order(void)
{
    struct RS_WorldMapVisits visits;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    struct RS_WorldMapViewport const viewport = looking_at_region(50, 60, 640, 480);

    RS_WorldMapVisits_Build(&visits, &bounds, &viewport);
    /* Four regions surround the centre at exactly the same distance. */
    TEST_ASSERT(
        index_of(&visits, 50, 59) < index_of(&visits, 50, 61),
        "an equal-distance tie is broken by region_y, lower first");
    TEST_ASSERT(
        index_of(&visits, 49, 60) < index_of(&visits, 51, 60),
        "and then by region_x, lower first");
    /* Both pairs really are ties, or the assertions above are about distance. */
    TEST_ASSERT(
        visits.items[index_of(&visits, 50, 59)].distance ==
            visits.items[index_of(&visits, 50, 61)].distance,
        "the first pair is equidistant");
    TEST_ASSERT(
        visits.items[index_of(&visits, 49, 60)].distance ==
            visits.items[index_of(&visits, 51, 60)].distance,
        "and so is the second");
}

static void
test_the_order_is_total(void)
{
    struct RS_WorldMapVisit near_centre = { 50, 60, 100 };
    struct RS_WorldMapVisit far_out = { 50, 60, 400 };
    struct RS_WorldMapVisit same_y_low_x = { 49, 60, 100 };
    struct RS_WorldMapVisit same_y_high_x = { 51, 60, 100 };
    struct RS_WorldMapVisit low_y = { 51, 59, 100 };
    struct RS_WorldMapVisit high_y = { 49, 61, 100 };

    /* Asserted on the rule itself rather than through a sorted list, because
     * no list can reach it: regions are generated in scan order, which already
     * agrees with the tie-break, so a sort that ignored the tie-break entirely
     * would still come out right AS LONG AS it happened to be stable -- and
     * nothing requires it to be. The tie-break is what makes the answer the
     * same either way. */
    TEST_ASSERT(RS_WorldMapVisit_Compare(&near_centre, &far_out) < 0, "nearer comes first");
    TEST_ASSERT(RS_WorldMapVisit_Compare(&far_out, &near_centre) > 0, "and further comes after");
    TEST_ASSERT(
        RS_WorldMapVisit_Compare(&same_y_low_x, &same_y_high_x) < 0,
        "an equal-distance tie is decided, not left open");
    TEST_ASSERT(
        RS_WorldMapVisit_Compare(&same_y_high_x, &same_y_low_x) > 0, "and decided consistently");
    /* region_y decides before region_x does, which is what makes this one
     * order rather than two half-orders that can contradict each other. */
    TEST_ASSERT(
        RS_WorldMapVisit_Compare(&low_y, &high_y) < 0, "region_y outranks region_x");
    TEST_ASSERT(RS_WorldMapVisit_Compare(&high_y, &low_y) > 0, "both ways round");
    /* Only a region compared with itself has no order. */
    TEST_ASSERT(
        RS_WorldMapVisit_Compare(&near_centre, &near_centre) == 0, "a region ties only itself");
}

static void
test_the_order_is_the_same_every_frame(void)
{
    struct RS_WorldMapVisits a;
    struct RS_WorldMapVisits b;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    struct RS_WorldMapViewport const viewport = looking_at_region(50, 60, 640, 480);
    int i;

    RS_WorldMapVisits_Build(&a, &bounds, &viewport);
    RS_WorldMapVisits_Build(&b, &bounds, &viewport);
    TEST_ASSERT(a.count == b.count, "the same view sees the same regions");
    /* Ties broken by region rather than left to the sort. Otherwise the same
     * two regions at the same distance swap places every frame, and at a
     * steady zoom each keeps losing the allowance to the other -- a pair of
     * tiles that never finish loading while nothing is moving. */
    for( i = 0; i < a.count && i < b.count; i++ )
        TEST_ASSERT(
            a.items[i].region_x == b.items[i].region_x &&
                a.items[i].region_y == b.items[i].region_y,
            "and in the same order");
}

/**
 * Assert the visit set is exactly the rectangle of regions named, and nothing
 * else.
 *
 * Asking only whether a few regions are PRESENT lets several wrong extents
 * through: one that reaches too far on an axis, one that used the other axis's
 * box dimension, one that forgot to divide by the zoom. All of them still
 * contain the handful of regions anyone would think to name.
 */
static void
expect_exact_range(
    struct RS_WorldMapVisits const* visits,
    int low_x,
    int high_x,
    int low_y,
    int high_y,
    char const* what)
{
    int region_x;
    int region_y;
    int expected = (high_x - low_x + 1) * (high_y - low_y + 1);

    if( visits->count != expected )
    {
        printf(
            "FAIL %s: expected %d regions, got %d\n", what, expected, visits->count);
        g_failures++;
        return;
    }
    for( region_y = low_y; region_y <= high_y; region_y++ )
        for( region_x = low_x; region_x <= high_x; region_x++ )
            if( !visits_contain(visits, region_x, region_y) )
            {
                printf("FAIL %s: region %d,%d missing\n", what, region_x, region_y);
                g_failures++;
                return;
            }
}

static void
test_the_edge_region_is_drawn(void)
{
    struct RS_WorldMapVisits visits;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    /* Exactly three regions wide and three tall, centred on one, at one screen
     * pixel per map tile. The box itself covers regions 49..51 and 59..61; the
     * slack adds one more each way. */
    struct RS_WorldMapViewport const viewport =
        looking_at_region(50, 60, RS_WORLDMAP_REGION_TILES_X * 3, RS_WORLDMAP_REGION_TILES_Z * 3);

    RS_WorldMapVisits_Build(&visits, &bounds, &viewport);
    /* One region of slack each way, on all four sides. Without it the map ends
     * in a hard line one region short of the frame, with the background
     * showing through the strip between. */
    expect_exact_range(&visits, 48, 53, 58, 63, "a three-region box plus its slack");
}

static void
test_a_box_that_is_not_square_reads_each_axis_from_its_own_side(void)
{
    struct RS_WorldMapVisits visits;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    /* Six regions wide, two tall. A square box cannot tell which dimension
     * drives which axis, and a tall thin map surface with them crossed loads a
     * band of regions beside it and none of the ones above. */
    struct RS_WorldMapViewport const viewport =
        looking_at_region(50, 60, RS_WORLDMAP_REGION_TILES_X * 6, RS_WORLDMAP_REGION_TILES_Z * 2);

    RS_WorldMapVisits_Build(&visits, &bounds, &viewport);
    expect_exact_range(&visits, 46, 54, 58, 62, "a wide, short box");
}

static void
test_the_extent_follows_the_zoom(void)
{
    struct RS_WorldMapVisits visits;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    struct RS_WorldMapViewport viewport =
        looking_at_region(50, 60, RS_WORLDMAP_REGION_TILES_X * 3, RS_WORLDMAP_REGION_TILES_Z * 3);

    /* Half the pixels per region is twice as much map in the same box: the
     * same three-region box now covers six. An extent that did not divide by
     * the zoom would show the same regions at every zoom level, which is a map
     * that appears to stop at the same place however far you pull back. */
    viewport.region_px = RS_WORLDMAP_REGION_TILES_X / 2;
    RS_WorldMapVisits_Build(&visits, &bounds, &viewport);
    expect_exact_range(&visits, 46, 54, 56, 64, "the same box at half the zoom");
}

static void
test_nothing_outside_the_area_is_asked_for(void)
{
    struct RS_WorldMapVisits visits;
    struct RS_WorldMapRegionBounds bounds;
    struct RS_WorldMapViewport const viewport = looking_at_region(5, 5, 640, 480);
    int i;

    /* A small area with the view parked in its corner. A region outside the
     * bounds has no terrain to bake, so asking for one spends the frame's
     * allowance on a load that can never arrive. */
    bounds.low_x = 4;
    bounds.low_y = 4;
    bounds.high_x = 7;
    bounds.high_y = 6;
    RS_WorldMapVisits_Build(&visits, &bounds, &viewport);
    TEST_ASSERT(visits.count > 0, "the area's own regions are still drawn");
    for( i = 0; i < visits.count; i++ )
    {
        TEST_ASSERT(visits.items[i].region_x >= 4, "no region west of the area");
        TEST_ASSERT(visits.items[i].region_x <= 7, "none east of it");
        TEST_ASSERT(visits.items[i].region_y >= 4, "none south of it");
        TEST_ASSERT(visits.items[i].region_y <= 6, "none north of it");
    }
    TEST_ASSERT(visits.count == 4 * 3, "and every one inside it is");
}

static void
test_a_view_off_the_area_asks_for_nothing(void)
{
    struct RS_WorldMapVisits visits;
    struct RS_WorldMapRegionBounds bounds;
    /* Looking a long way past the area's eastern edge. The clamps cross, and
     * a loop that ran anyway would walk backwards -- or, with the comparison
     * the other way round, over every region in between. */
    struct RS_WorldMapViewport const viewport = looking_at_region(500, 60, 640, 480);

    bounds.low_x = 0;
    bounds.low_y = 0;
    bounds.high_x = 10;
    bounds.high_y = 200;
    TEST_ASSERT(
        RS_WorldMapVisits_Build(&visits, &bounds, &viewport) == 0,
        "a view off the area draws none of it");
}

static void
test_a_wide_box_sees_more_than_a_narrow_one(void)
{
    struct RS_WorldMapVisits narrow;
    struct RS_WorldMapVisits wide;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    struct RS_WorldMapViewport const narrow_view = looking_at_region(50, 60, 200, 480);
    struct RS_WorldMapViewport const wide_view = looking_at_region(50, 60, 1600, 480);

    RS_WorldMapVisits_Build(&narrow, &bounds, &narrow_view);
    RS_WorldMapVisits_Build(&wide, &bounds, &wide_view);
    TEST_ASSERT(wide.count > narrow.count, "a wider box covers more regions");
    /* Widening must not reach further up and down: a box's two dimensions have
     * to drive their own axes, or a tall thin map surface loads a band of
     * regions beside it and none of the ones above. */
    TEST_ASSERT(
        visits_contain(&wide, 50, 62) == visits_contain(&narrow, 50, 62),
        "and no further north for it");
}

static void
test_a_zoomed_out_view_sees_more(void)
{
    struct RS_WorldMapVisits close;
    struct RS_WorldMapVisits far;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    struct RS_WorldMapViewport close_view = looking_at_region(50, 60, 640, 480);
    struct RS_WorldMapViewport far_view = looking_at_region(50, 60, 640, 480);

    /* Half the pixels per region is twice as much map in the same box. */
    far_view.region_px = close_view.region_px / 2;
    RS_WorldMapVisits_Build(&close, &bounds, &close_view);
    RS_WorldMapVisits_Build(&far, &bounds, &far_view);
    TEST_ASSERT(far.count > close.count, "zooming out brings more regions into view");
}

static void
test_the_frame_holds_a_bounded_number_of_regions(void)
{
    struct RS_WorldMapVisits visits;
    struct RS_WorldMapRegionBounds const bounds = open_world();
    struct RS_WorldMapViewport viewport = looking_at_region(100, 100, 4000, 4000);

    /* A zoom far past anything the client offers, over an area far bigger than
     * the box. The cap is a refusal to list more, not a wrap. */
    viewport.region_px = 1;
    RS_WorldMapVisits_Build(&visits, &bounds, &viewport);
    /* Exactly full, not "no more than full". The slot one past the end of the
     * array is the count itself, so an off-by-one here does not produce a
     * count one too high -- it produces whatever the overrun happened to write
     * there, which can be any small number at all. */
    TEST_ASSERT(visits.count == RS_WORLDMAP_VISITS_MAX, "the frame's list fills and stops");
}

static void
test_the_blit_buffer_hands_out_blank_slots(void)
{
    struct RS_WorldMapTiles tiles;
    struct UITreeWorldMapTile* tile;
    int i;

    RS_WorldMapTiles_Reset(&tiles);
    tile = RS_WorldMapTiles_Push(&tiles);
    TEST_ASSERT(tile != NULL, "an empty frame has room");
    TEST_ASSERT(tiles.count == 1, "and the push counts");
    tile->scaled = 1;
    tile->scene_id = 77;

    /* The array outlives the frame. A slot handed back still carrying the
     * `scaled` flag a stretched region left in it stretches whatever icon
     * lands there next, to a size nobody chose. */
    RS_WorldMapTiles_Reset(&tiles);
    TEST_ASSERT(tiles.count == 0, "a reset frame is empty");
    tile = RS_WorldMapTiles_Push(&tiles);
    TEST_ASSERT(tile->scaled == 0, "the slot comes back unstretched");
    TEST_ASSERT(tile->scene_id == 0, "and without its last sprite");

    RS_WorldMapTiles_Reset(&tiles);
    for( i = 0; i < RS_WORLDMAP_TILES_MAX; i++ )
        TEST_ASSERT(RS_WorldMapTiles_Push(&tiles) != NULL, "a frame holds its full complement");
    TEST_ASSERT(RS_WorldMapTiles_Push(&tiles) == NULL, "one past full is refused");
    TEST_ASSERT(tiles.count == RS_WORLDMAP_TILES_MAX, "and does not grow the frame");
}

int
main(void)
{
    test_a_viewport_with_no_area_asks_for_nothing();
    test_the_region_under_the_view_is_first();
    test_the_order_is_nearest_first();
    test_a_scan_order_would_not_do();
    test_regions_the_same_distance_away_have_a_stated_order();
    test_the_order_is_total();
    test_the_order_is_the_same_every_frame();
    test_the_edge_region_is_drawn();
    test_a_box_that_is_not_square_reads_each_axis_from_its_own_side();
    test_the_extent_follows_the_zoom();
    test_nothing_outside_the_area_is_asked_for();
    test_a_view_off_the_area_asks_for_nothing();
    test_a_wide_box_sees_more_than_a_narrow_one();
    test_a_zoomed_out_view_sees_more();
    test_the_frame_holds_a_bounded_number_of_regions();
    test_the_blit_buffer_hands_out_blank_slots();

    if( g_failures )
    {
        printf("rs_worldmap_view: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_worldmap_view: all checks passed\n");
    return 0;
}
