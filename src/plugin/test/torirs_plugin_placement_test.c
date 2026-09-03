/*
 * Exact placement geometry, without a client, renderer, or plugin host.
 *
 * The pixel-grid checks are deliberately an independent oracle: region
 * subtraction and intersection are compared point by point with the boolean
 * set operation they promise. The named cases then pin fragment ordering,
 * all nine anchors, aliasing, and the bounded failure contract.
 */

#include "plugin/torirs_plugin_placement.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond, what)                                                                          \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failures++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                       \
        }                                                                                          \
    } while( 0 )

static struct ToriRS_PlacementRect
rect(
    int x,
    int y,
    int w,
    int h)
{
    struct ToriRS_PlacementRect answer = { .x = x, .y = y, .w = w, .h = h };
    return answer;
}

static bool
rect_equal(
    struct ToriRS_PlacementRect const* a,
    struct ToriRS_PlacementRect const* b)
{
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

static uint64_t
region_area(struct ToriRS_PlacementRegion const* region)
{
    uint64_t area = 0;
    int const count = ToriRS_PlacementRegion_RectCount(region);

    for( int i = 0; i < count; i++ )
    {
        struct ToriRS_PlacementRect piece;
        ToriRS_PlacementRegion_RectAt(region, i, &piece);
        area += (uint64_t)piece.w * (uint64_t)piece.h;
    }
    return area;
}

static bool
region_has_pixel(
    struct ToriRS_PlacementRegion const* region,
    int x,
    int y)
{
    struct ToriRS_PlacementRect pixel = rect(x, y, 1, 1);
    return ToriRS_PlacementRegion_ContainsRect(region, &pixel);
}

static void
check_boolean_grid(
    struct ToriRS_PlacementRegion const* answer,
    struct ToriRS_PlacementRegion const* a,
    struct ToriRS_PlacementRegion const* b,
    bool subtract,
    char const* what)
{
    bool matches = true;

    for( int y = -2; y < 14; y++ )
        for( int x = -2; x < 16; x++ )
        {
            bool const in_a = region_has_pixel(a, x, y);
            bool const in_b = region_has_pixel(b, x, y);
            bool const expected = subtract ? in_a && !in_b : in_a && in_b;
            if( region_has_pixel(answer, x, y) != expected )
                matches = false;
        }
    CHECK(matches, what);
}

static void
test_rect_validation(void)
{
    struct ToriRS_PlacementRect good = rect(-20, -10, 40, 30);
    struct ToriRS_PlacementRect empty = rect(INT_MAX, INT_MAX, 0, 0);
    struct ToriRS_PlacementRect negative_w = rect(0, 0, -1, 1);
    struct ToriRS_PlacementRect negative_h = rect(0, 0, 1, -1);
    struct ToriRS_PlacementRect bad_right = rect(INT_MAX, 0, 1, 1);
    struct ToriRS_PlacementRect bad_bottom = rect(0, INT_MAX, 1, 1);

    CHECK(ToriRS_PlacementRect_IsValid(&good), "negative origins are valid");
    CHECK(ToriRS_PlacementRect_IsValid(&empty), "representable empty rectangle is valid");
    CHECK(!ToriRS_PlacementRect_IsValid(&negative_w), "negative width is invalid");
    CHECK(!ToriRS_PlacementRect_IsValid(&negative_h), "negative height is invalid");
    CHECK(!ToriRS_PlacementRect_IsValid(&bad_right), "overflowing right edge is invalid");
    CHECK(!ToriRS_PlacementRect_IsValid(&bad_bottom), "overflowing bottom edge is invalid");
}

static void
test_exact_center_subtraction(void)
{
    struct ToriRS_PlacementRegion area;
    struct ToriRS_PlacementRect canvas = rect(0, 0, 100, 100);
    struct ToriRS_PlacementRect cut = rect(40, 30, 20, 40);
    struct ToriRS_PlacementRect expected[] = {
        { 0,  0,  100, 30 },
        { 0,  30, 40,  40 },
        { 60, 30, 40,  40 },
        { 0,  70, 100, 30 },
    };

    ToriRS_PlacementRegion_SetRect(&area, &canvas);
    CHECK(
        ToriRS_PlacementRegion_SubtractRect(&area, &cut, &area) == TORIRS_PLACEMENT_OK,
        "center subtraction succeeds in place");
    CHECK(ToriRS_PlacementRegion_RectCount(&area) == 4, "center cut keeps four fragments");
    for( int i = 0; i < 4; i++ )
    {
        struct ToriRS_PlacementRect actual;
        ToriRS_PlacementRegion_RectAt(&area, i, &actual);
        CHECK(rect_equal(&actual, &expected[i]), "center-cut fragment and order are exact");
    }
    CHECK(region_area(&area) == 9200, "center cut loses only the cut's area");

    struct ToriRS_PlacementRect safe = rect(10, 35, 20, 20);
    struct ToriRS_PlacementRect covered = rect(45, 40, 1, 1);
    struct ToriRS_PlacementRect straddles = rect(30, 40, 40, 10);
    struct ToriRS_PlacementRect empty = rect(999, 999, 0, 4);
    CHECK(ToriRS_PlacementRegion_ContainsRect(&area, &safe), "fragment contains safe box");
    CHECK(!ToriRS_PlacementRegion_ContainsRect(&area, &covered), "cut point is not contained");
    CHECK(!ToriRS_PlacementRegion_ContainsRect(&area, &straddles), "box may not straddle a hole");
    CHECK(ToriRS_PlacementRegion_ContainsRect(&area, &empty), "empty geometry is contained");
}

static void
test_clipped_and_covering_cuts(void)
{
    struct ToriRS_PlacementRegion original;
    struct ToriRS_PlacementRegion answer;
    struct ToriRS_PlacementRect canvas = rect(0, 0, 100, 100);
    struct ToriRS_PlacementRect outside = rect(150, 10, 20, 20);
    struct ToriRS_PlacementRect clipped = rect(-10, 20, 30, 20);
    struct ToriRS_PlacementRect covers = rect(-10, -10, 120, 120);

    ToriRS_PlacementRegion_SetRect(&original, &canvas);
    CHECK(
        ToriRS_PlacementRegion_SubtractRect(&original, &outside, &answer) == TORIRS_PLACEMENT_OK &&
            ToriRS_PlacementRegion_Equals(&answer, &original),
        "non-overlapping cut changes nothing");
    CHECK(
        ToriRS_PlacementRegion_SubtractRect(&original, &clipped, &answer) == TORIRS_PLACEMENT_OK,
        "partly outside cut is clipped");
    CHECK(region_area(&answer) == 9600, "only the in-bounds part is removed");
    CHECK(
        ToriRS_PlacementRegion_SubtractRect(&original, &covers, &answer) == TORIRS_PLACEMENT_OK,
        "covering cut succeeds");
    CHECK(ToriRS_PlacementRegion_RectCount(&answer) == 0, "covering cut leaves empty set");
}

static void
test_union_subtract_and_intersection(void)
{
    struct ToriRS_PlacementRegion a;
    struct ToriRS_PlacementRegion b;
    struct ToriRS_PlacementRegion answer;
    struct ToriRS_PlacementRegion alias;
    struct ToriRS_PlacementRect box = rect(0, 0, 12, 10);
    struct ToriRS_PlacementRect cut_a = rect(2, 2, 3, 4);
    struct ToriRS_PlacementRect cut_b = rect(7, 0, 2, 5);
    struct ToriRS_PlacementRect add_a = rect(-1, 1, 8, 5);
    struct ToriRS_PlacementRect add_b = rect(4, 4, 9, 7);

    ToriRS_PlacementRegion_SetRect(&a, &box);
    ToriRS_PlacementRegion_Clear(&b);
    CHECK(
        ToriRS_PlacementRegion_AddRect(&b, &cut_b) == TORIRS_PLACEMENT_OK,
        "first cut joins region");
    CHECK(
        ToriRS_PlacementRegion_AddRect(&b, &cut_a) == TORIRS_PLACEMENT_OK,
        "second cut joins region out of geometric order");
    CHECK(
        ToriRS_PlacementRegion_Subtract(&a, &b, &answer) == TORIRS_PLACEMENT_OK,
        "fragmented region subtraction succeeds");
    check_boolean_grid(&answer, &a, &b, true, "fragmented subtraction matches set difference");
    CHECK(region_area(&answer) == 120 - 12 - 10, "disjoint cuts remove their exact area");

    ToriRS_PlacementRegion_Clear(&a);
    ToriRS_PlacementRegion_Clear(&b);
    CHECK(
        ToriRS_PlacementRegion_AddRect(&a, &add_a) == TORIRS_PLACEMENT_OK &&
            ToriRS_PlacementRegion_AddRect(&a, &add_b) == TORIRS_PLACEMENT_OK,
        "overlapping rectangles form one exact union");
    box = rect(1, -1, 9, 12);
    ToriRS_PlacementRegion_SetRect(&b, &box);
    CHECK(
        ToriRS_PlacementRegion_Intersect(&a, &b, &answer) == TORIRS_PLACEMENT_OK,
        "fragmented intersection succeeds");
    check_boolean_grid(&answer, &a, &b, false, "intersection matches boolean set operation");

    alias = a;
    CHECK(
        ToriRS_PlacementRegion_Intersect(&alias, &b, &alias) == TORIRS_PLACEMENT_OK &&
            ToriRS_PlacementRegion_Equals(&alias, &answer),
        "intersection permits output to alias input");
}

static void
test_containment_and_geometric_equality(void)
{
    struct ToriRS_PlacementRegion two_piece;
    struct ToriRS_PlacementRegion three_piece;
    struct ToriRS_PlacementRect left = rect(0, 0, 40, 100);
    struct ToriRS_PlacementRect middle_right = rect(40, 30, 60, 40);
    struct ToriRS_PlacementRect top_left = rect(0, 0, 40, 30);
    struct ToriRS_PlacementRect middle = rect(0, 30, 100, 40);
    struct ToriRS_PlacementRect bottom_left = rect(0, 70, 40, 30);
    struct ToriRS_PlacementRect spans_seam = rect(10, 35, 80, 20);
    struct ToriRS_PlacementRect spans_hole = rect(10, 10, 80, 20);

    ToriRS_PlacementRegion_Clear(&two_piece);
    CHECK(
        ToriRS_PlacementRegion_AddRect(&two_piece, &left) == TORIRS_PLACEMENT_OK,
        "two-piece region starts");
    CHECK(
        ToriRS_PlacementRegion_AddRect(&two_piece, &middle_right) == TORIRS_PLACEMENT_OK,
        "two-piece region completes");

    ToriRS_PlacementRegion_Clear(&three_piece);
    CHECK(
        ToriRS_PlacementRegion_AddRect(&three_piece, &bottom_left) == TORIRS_PLACEMENT_OK &&
            ToriRS_PlacementRegion_AddRect(&three_piece, &middle) == TORIRS_PLACEMENT_OK &&
            ToriRS_PlacementRegion_AddRect(&three_piece, &top_left) == TORIRS_PLACEMENT_OK,
        "alternate decomposition builds in reverse order");

    CHECK(
        ToriRS_PlacementRegion_ContainsRect(&two_piece, &spans_seam),
        "containment covers a box tiled by multiple rectangles");
    CHECK(
        !ToriRS_PlacementRegion_ContainsRect(&two_piece, &spans_hole),
        "containment rejects uncovered area");
    CHECK(
        ToriRS_PlacementRegion_Equals(&two_piece, &three_piece),
        "equality compares covered geometry, not decomposition");
}

static void
test_anchored_placement(void)
{
    struct ToriRS_PlacementRegion area;
    struct ToriRS_PlacementRect canvas = rect(0, 0, 100, 100);
    struct ToriRS_PlacementRect cut = rect(40, 30, 20, 40);
    struct ToriRS_PlacementRect expected[TORIRS_PLACEMENT_ANCHOR_COUNT] = {
        { 5,  5,  10, 10 },
        { 45, 5,  10, 10 },
        { 85, 5,  10, 10 },
        { 5,  45, 10, 10 },
        { 15, 45, 10, 10 },
        { 85, 45, 10, 10 },
        { 5,  85, 10, 10 },
        { 45, 85, 10, 10 },
        { 85, 85, 10, 10 },
    };

    ToriRS_PlacementRegion_SetRect(&area, &canvas);
    CHECK(
        ToriRS_PlacementRegion_SubtractRect(&area, &cut, &area) == TORIRS_PLACEMENT_OK,
        "placement fixture fragments");

    for( int anchor = 0; anchor < TORIRS_PLACEMENT_ANCHOR_COUNT; anchor++ )
    {
        struct ToriRS_PlacementRect actual;
        struct ToriRS_PlacementRect with_margin;
        CHECK(
            ToriRS_PlacementRegion_Place(&area, anchor, 10, 10, 5, &actual),
            "each anchor finds a fragment");
        CHECK(rect_equal(&actual, &expected[anchor]), "anchor chooses deterministic box");
        with_margin = rect(actual.x - 5, actual.y - 5, actual.w + 10, actual.h + 10);
        CHECK(
            ToriRS_PlacementRegion_ContainsRect(&area, &with_margin),
            "placed box keeps its margin inside safe area");
    }

    struct ToriRS_PlacementRect untouched = rect(1, 2, 3, 4);
    struct ToriRS_PlacementRect before = untouched;
    CHECK(
        !ToriRS_PlacementRegion_Place(
            &area, TORIRS_PLACEMENT_ANCHOR_TOP_RIGHT, 90, 90, 6, &untouched),
        "oversize placement is absent");
    CHECK(rect_equal(&untouched, &before), "failed placement leaves output untouched");
}

static void
test_bounded_transaction(void)
{
    struct ToriRS_PlacementRegion area;
    struct ToriRS_PlacementRegion before;
    struct ToriRS_PlacementRect canvas = rect(0, 0, 129, 101);

    ToriRS_PlacementRegion_SetRect(&area, &canvas);
    for( int i = 0; i < 32; i++ )
    {
        struct ToriRS_PlacementRect slit = rect(1 + 2 * i, 0, 1, 101);
        CHECK(
            ToriRS_PlacementRegion_SubtractRect(&area, &slit, &area) == TORIRS_PLACEMENT_OK,
            "vertical slit remains within fragment bound");
    }
    CHECK(ToriRS_PlacementRegion_RectCount(&area) == 33, "slits produce 33 exact strips");

    before = area;
    struct ToriRS_PlacementRect crosscut = rect(0, 50, 129, 1);
    CHECK(
        ToriRS_PlacementRegion_SubtractRect(&area, &crosscut, &area) == TORIRS_PLACEMENT_LIMIT,
        "66-fragment answer reports the fixed ceiling");
    CHECK(memcmp(&area, &before, sizeof(area)) == 0, "limit leaves aliased output untouched");
    CHECK(region_area(&area) == region_area(&before), "limit never publishes a lossy fallback");
}

#define PROPERTY_RECTS 6
#define PROPERTY_TRIALS 96

static uint32_t g_random = 0x4f1bbcdcU;

static uint32_t
property_random(void)
{
    g_random = g_random * 1664525U + 1013904223U;
    return g_random;
}

static bool
raw_rect_has_pixel(
    struct ToriRS_PlacementRect const* item,
    int x,
    int y)
{
    return item->w > 0 && item->h > 0 && x >= item->x && x < item->x + item->w && y >= item->y &&
           y < item->y + item->h;
}

static bool
raw_region_has_pixel(
    struct ToriRS_PlacementRect const* items,
    int item_count,
    int x,
    int y)
{
    for( int i = 0; i < item_count; i++ )
        if( raw_rect_has_pixel(&items[i], x, y) )
            return true;
    return false;
}

/**
 * Many small overlapping inputs catch edge-order and band-coalescing cases a
 * hand-picked L shape does not. The expected answer is just boolean pixel
 * membership over a bounded integer grid, independent of the implementation.
 */
static void
test_small_grid_properties(void)
{
    for( int trial = 0; trial < PROPERTY_TRIALS; trial++ )
    {
        struct ToriRS_PlacementRect a_items[PROPERTY_RECTS];
        struct ToriRS_PlacementRect b_items[PROPERTY_RECTS];
        struct ToriRS_PlacementRegion a;
        struct ToriRS_PlacementRegion a_reverse;
        struct ToriRS_PlacementRegion b;
        struct ToriRS_PlacementRegion intersection;
        struct ToriRS_PlacementRegion subtraction;
        bool union_matches = true;
        bool intersection_matches = true;
        bool subtraction_matches = true;

        ToriRS_PlacementRegion_Clear(&a);
        ToriRS_PlacementRegion_Clear(&b);
        for( int i = 0; i < PROPERTY_RECTS; i++ )
        {
            a_items[i] = rect(
                (int)(property_random() % 15) - 3,
                (int)(property_random() % 13) - 3,
                (int)(property_random() % 7),
                (int)(property_random() % 7));
            b_items[i] = rect(
                (int)(property_random() % 15) - 3,
                (int)(property_random() % 13) - 3,
                (int)(property_random() % 7),
                (int)(property_random() % 7));
            CHECK(
                ToriRS_PlacementRegion_AddRect(&a, &a_items[i]) == TORIRS_PLACEMENT_OK,
                "small-grid union A stays within bound");
            CHECK(
                ToriRS_PlacementRegion_AddRect(&b, &b_items[i]) == TORIRS_PLACEMENT_OK,
                "small-grid union B stays within bound");
        }

        ToriRS_PlacementRegion_Clear(&a_reverse);
        for( int i = PROPERTY_RECTS - 1; i >= 0; i-- )
            CHECK(
                ToriRS_PlacementRegion_AddRect(&a_reverse, &a_items[i]) == TORIRS_PLACEMENT_OK,
                "reverse small-grid union stays within bound");
        CHECK(
            memcmp(&a, &a_reverse, sizeof(a)) == 0,
            "canonical union is independent of declaration order");

        CHECK(
            ToriRS_PlacementRegion_Intersect(&a, &b, &intersection) == TORIRS_PLACEMENT_OK,
            "small-grid intersection stays within bound");
        CHECK(
            ToriRS_PlacementRegion_Subtract(&a, &b, &subtraction) == TORIRS_PLACEMENT_OK,
            "small-grid subtraction stays within bound");

        for( int y = -4; y < 18; y++ )
            for( int x = -4; x < 20; x++ )
            {
                bool const in_a = raw_region_has_pixel(a_items, PROPERTY_RECTS, x, y);
                bool const in_b = raw_region_has_pixel(b_items, PROPERTY_RECTS, x, y);
                if( region_has_pixel(&a, x, y) != in_a || region_has_pixel(&b, x, y) != in_b )
                    union_matches = false;
                if( region_has_pixel(&intersection, x, y) != (in_a && in_b) )
                    intersection_matches = false;
                if( region_has_pixel(&subtraction, x, y) != (in_a && !in_b) )
                    subtraction_matches = false;
            }
        CHECK(union_matches, "small-grid unions match raw rectangles");
        CHECK(intersection_matches, "small-grid intersections match boolean oracle");
        CHECK(subtraction_matches, "small-grid subtractions match boolean oracle");
    }
}

int
main(void)
{
    test_rect_validation();
    test_exact_center_subtraction();
    test_clipped_and_covering_cuts();
    test_union_subtract_and_intersection();
    test_containment_and_geometric_equality();
    test_anchored_placement();
    test_bounded_transaction();
    test_small_grid_properties();

    if( g_failures )
    {
        fprintf(stderr, "plugin placement: %d/%d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("plugin placement: %d checks, all passed\n", g_checks);
    return 0;
}
