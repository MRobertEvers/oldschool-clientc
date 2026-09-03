#include "plugin/torirs_plugin_placement.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

/** Two edges per axis, from each rectangle in two input regions. */
#define PLACEMENT_COMBINE_EDGES_MAX (TORIRS_PLACEMENT_REGION_RECTS_MAX * 4)

static int64_t
placement_rect_right(struct ToriRS_PlacementRect const* rect)
{
    assert(rect);
    return (int64_t)rect->x + rect->w;
}

static int64_t
placement_rect_bottom(struct ToriRS_PlacementRect const* rect)
{
    assert(rect);
    return (int64_t)rect->y + rect->h;
}

bool
ToriRS_PlacementRect_IsValid(struct ToriRS_PlacementRect const* rect)
{
    assert(rect);

    if( rect->w < 0 )
        return false;
    if( rect->h < 0 )
        return false;
    if( placement_rect_right(rect) > INT_MAX )
        return false;
    if( placement_rect_bottom(rect) > INT_MAX )
        return false;
    return true;
}

static void
placement_rect_assert_valid(struct ToriRS_PlacementRect const* rect)
{
#ifdef NDEBUG
    (void)rect;
#else
    assert(rect);
    assert(rect->w >= 0);
    assert(rect->h >= 0);
    assert(placement_rect_right(rect) <= INT_MAX);
    assert(placement_rect_bottom(rect) <= INT_MAX);
#endif
}

#ifndef NDEBUG
static bool
placement_rects_overlap(
    struct ToriRS_PlacementRect const* a,
    struct ToriRS_PlacementRect const* b)
{
    assert(a);
    assert(b);

    return a->x < placement_rect_right(b) && b->x < placement_rect_right(a) &&
           a->y < placement_rect_bottom(b) && b->y < placement_rect_bottom(a);
}
#endif

static void
placement_region_assert_valid(struct ToriRS_PlacementRegion const* region)
{
    assert(region);
    assert(region->_rect_count >= 0);
    assert(region->_rect_count <= TORIRS_PLACEMENT_REGION_RECTS_MAX);

    for( int i = 0; i < region->_rect_count; i++ )
    {
        struct ToriRS_PlacementRect const* rect = &region->_rects[i];
        placement_rect_assert_valid(rect);
        assert(rect->w > 0);
        assert(rect->h > 0);

#ifndef NDEBUG
        for( int j = i + 1; j < region->_rect_count; j++ )
            assert(!placement_rects_overlap(rect, &region->_rects[j]));
#endif
    }
}

static bool
placement_rect_intersection(
    struct ToriRS_PlacementRect const* a,
    struct ToriRS_PlacementRect const* b,
    struct ToriRS_PlacementRect* out)
{
    int64_t right;
    int64_t bottom;
    int left;
    int top;

    assert(a);
    assert(b);
    assert(out);

    right = placement_rect_right(a) < placement_rect_right(b) ? placement_rect_right(a)
                                                              : placement_rect_right(b);
    bottom = placement_rect_bottom(a) < placement_rect_bottom(b) ? placement_rect_bottom(a)
                                                                 : placement_rect_bottom(b);
    left = a->x > b->x ? a->x : b->x;
    top = a->y > b->y ? a->y : b->y;

    if( right <= left || bottom <= top )
        return false;

    out->x = left;
    out->y = top;
    out->w = (int)(right - left);
    out->h = (int)(bottom - top);
    return true;
}

static uint64_t
placement_rect_area(struct ToriRS_PlacementRect const* rect)
{
    assert(rect);
    return (uint64_t)rect->w * (uint64_t)rect->h;
}

void
ToriRS_PlacementRegion_Clear(struct ToriRS_PlacementRegion* region)
{
    assert(region);
    memset(region, 0, sizeof(*region));
}

void
ToriRS_PlacementRegion_SetRect(
    struct ToriRS_PlacementRegion* region,
    struct ToriRS_PlacementRect const* rect)
{
    assert(region);
    placement_rect_assert_valid(rect);

    ToriRS_PlacementRegion_Clear(region);
    if( rect->w == 0 || rect->h == 0 )
        return;
    region->_rects[0] = *rect;
    region->_rect_count = 1;
}

int
ToriRS_PlacementRegion_RectCount(struct ToriRS_PlacementRegion const* region)
{
    assert(region);
    placement_region_assert_valid(region);
    return region->_rect_count;
}

void
ToriRS_PlacementRegion_RectAt(
    struct ToriRS_PlacementRegion const* region,
    int index,
    struct ToriRS_PlacementRect* out_rect)
{
    assert(region);
    assert(index >= 0);
    assert(index < region->_rect_count);
    assert(out_rect);
    placement_region_assert_valid(region);
    *out_rect = region->_rects[index];
}

enum PlacementCombine
{
    PLACEMENT_COMBINE_UNION = 0,
    PLACEMENT_COMBINE_INTERSECT,
    PLACEMENT_COMBINE_SUBTRACT,
    PLACEMENT_COMBINE_COUNT
};

static int
placement_edges_add(
    int* edges,
    int edge_count,
    struct ToriRS_PlacementRegion const* region,
    bool horizontal)
{
    assert(edges);
    assert(edge_count >= 0);
    assert(edge_count <= PLACEMENT_COMBINE_EDGES_MAX);
    assert(region);

    for( int i = 0; i < region->_rect_count; i++ )
    {
        struct ToriRS_PlacementRect const* rect = &region->_rects[i];
        assert(edge_count + 2 <= PLACEMENT_COMBINE_EDGES_MAX);
        if( horizontal )
        {
            edges[edge_count++] = rect->x;
            edges[edge_count++] = (int)placement_rect_right(rect);
        }
        else
        {
            edges[edge_count++] = rect->y;
            edges[edge_count++] = (int)placement_rect_bottom(rect);
        }
    }
    return edge_count;
}

static int
placement_edges_sort_unique(
    int* edges,
    int edge_count)
{
    assert(edge_count >= 0);
    if( edge_count == 0 )
        return 0;
    assert(edges);

    for( int i = 1; i < edge_count; i++ )
    {
        int const edge = edges[i];
        int at = i;
        while( at > 0 && edge < edges[at - 1] )
        {
            edges[at] = edges[at - 1];
            at--;
        }
        edges[at] = edge;
    }

    int unique_count = 1;
    for( int i = 1; i < edge_count; i++ )
        if( edges[i] != edges[unique_count - 1] )
            edges[unique_count++] = edges[i];
    return unique_count;
}

/** All cell boundaries are input edges, so membership is constant in a cell. */
static bool
placement_region_covers_cell(
    struct ToriRS_PlacementRegion const* region,
    int left,
    int top,
    int right,
    int bottom)
{
    assert(region);
    assert(left < right);
    assert(top < bottom);

    for( int i = 0; i < region->_rect_count; i++ )
    {
        struct ToriRS_PlacementRect const* rect = &region->_rects[i];
        if( rect->x <= left && placement_rect_right(rect) >= right && rect->y <= top &&
            placement_rect_bottom(rect) >= bottom )
            return true;
    }
    return false;
}

/**
 * Add one horizontal band to a canonical answer. An identical interval in
 * the preceding band extends vertically; otherwise this is a new fragment.
 */
static bool
placement_region_emit_band(
    struct ToriRS_PlacementRegion* region,
    int left,
    int top,
    int right,
    int bottom)
{
    int64_t width_span;
    int64_t height_span;

    assert(region);
    assert(left < right);
    assert(top < bottom);
    width_span = (int64_t)right - left;
    height_span = (int64_t)bottom - top;
    assert(width_span <= INT_MAX);
    assert(height_span <= INT_MAX);

    int const width = (int)width_span;
    int const height = (int)height_span;

    for( int i = region->_rect_count - 1; i >= 0; i-- )
    {
        struct ToriRS_PlacementRect* prior = &region->_rects[i];
        int64_t const extended_height = (int64_t)prior->h + height;
        if( prior->x == left && prior->w == width && placement_rect_bottom(prior) == top &&
            extended_height <= INT_MAX )
        {
            prior->h = (int)extended_height;
            return true;
        }
    }

    if( region->_rect_count >= TORIRS_PLACEMENT_REGION_RECTS_MAX )
        return false;
    region->_rects[region->_rect_count++] = (struct ToriRS_PlacementRect){
        .x = left,
        .y = top,
        .w = width,
        .h = height,
    };
    return true;
}

static bool
placement_combine_cell(
    bool in_a,
    bool in_b,
    int operation)
{
    assert(operation >= 0);
    assert(operation < PLACEMENT_COMBINE_COUNT);

    if( operation == PLACEMENT_COMBINE_UNION )
        return in_a || in_b;
    if( operation == PLACEMENT_COMBINE_INTERSECT )
        return in_a && in_b;
    return in_a && !in_b;
}

/**
 * Construct the boolean set operation directly from its edge grid.
 *
 * Doing this in one pass matters for the bound: subtracting several cuts one
 * at a time can temporarily create more fragments than the final answer has.
 * Horizontal bands are canonical, so input order and input decomposition do
 * not affect either iteration order or the capacity verdict.
 */
static enum ToriRS_PlacementStatus
placement_region_combine(
    struct ToriRS_PlacementRegion const* a,
    struct ToriRS_PlacementRegion const* b,
    int operation,
    struct ToriRS_PlacementRegion* out)
{
    int x_edges[PLACEMENT_COMBINE_EDGES_MAX];
    int y_edges[PLACEMENT_COMBINE_EDGES_MAX];
    int x_count = 0;
    int y_count = 0;
    struct ToriRS_PlacementRegion answer;

    assert(a);
    assert(b);
    assert(operation >= 0);
    assert(operation < PLACEMENT_COMBINE_COUNT);
    assert(out);
    placement_region_assert_valid(a);
    placement_region_assert_valid(b);

    x_count = placement_edges_add(x_edges, x_count, a, true);
    x_count = placement_edges_add(x_edges, x_count, b, true);
    y_count = placement_edges_add(y_edges, y_count, a, false);
    y_count = placement_edges_add(y_edges, y_count, b, false);
    x_count = placement_edges_sort_unique(x_edges, x_count);
    y_count = placement_edges_sort_unique(y_edges, y_count);
    ToriRS_PlacementRegion_Clear(&answer);

    for( int yi = 0; yi + 1 < y_count; yi++ )
    {
        int run_left = 0;
        int run_right = 0;
        bool in_run = false;

        for( int xi = 0; xi + 1 < x_count; xi++ )
        {
            int const left = x_edges[xi];
            int const right = x_edges[xi + 1];
            int const top = y_edges[yi];
            int const bottom = y_edges[yi + 1];
            bool const in_a = placement_region_covers_cell(a, left, top, right, bottom);
            bool const in_b = placement_region_covers_cell(b, left, top, right, bottom);
            bool const included = placement_combine_cell(in_a, in_b, operation);

            if( included && !in_run )
            {
                run_left = left;
                run_right = right;
                in_run = true;
            }
            else if( included && (int64_t)right - run_left <= INT_MAX )
                run_right = right;
            else
            {
                if( in_run &&
                    !placement_region_emit_band(&answer, run_left, top, run_right, bottom) )
                    return TORIRS_PLACEMENT_LIMIT;
                in_run = included;
                if( included )
                {
                    run_left = left;
                    run_right = right;
                }
            }
        }

        if( in_run && !placement_region_emit_band(
                          &answer, run_left, y_edges[yi], run_right, y_edges[yi + 1]) )
            return TORIRS_PLACEMENT_LIMIT;
    }

    placement_region_assert_valid(&answer);
    *out = answer;
    return TORIRS_PLACEMENT_OK;
}

enum ToriRS_PlacementStatus
ToriRS_PlacementRegion_AddRect(
    struct ToriRS_PlacementRegion* region,
    struct ToriRS_PlacementRect const* rect)
{
    struct ToriRS_PlacementRegion addition;
    struct ToriRS_PlacementRegion answer;
    enum ToriRS_PlacementStatus status;

    assert(region);
    assert(rect);
    placement_region_assert_valid(region);
    placement_rect_assert_valid(rect);

    ToriRS_PlacementRegion_SetRect(&addition, rect);
    status = placement_region_combine(region, &addition, PLACEMENT_COMBINE_UNION, &answer);
    if( status != TORIRS_PLACEMENT_OK )
        return status;
    *region = answer;
    return TORIRS_PLACEMENT_OK;
}

enum ToriRS_PlacementStatus
ToriRS_PlacementRegion_Intersect(
    struct ToriRS_PlacementRegion const* a,
    struct ToriRS_PlacementRegion const* b,
    struct ToriRS_PlacementRegion* out)
{
    return placement_region_combine(a, b, PLACEMENT_COMBINE_INTERSECT, out);
}

enum ToriRS_PlacementStatus
ToriRS_PlacementRegion_SubtractRect(
    struct ToriRS_PlacementRegion const* source,
    struct ToriRS_PlacementRect const* cut,
    struct ToriRS_PlacementRegion* out)
{
    struct ToriRS_PlacementRegion cuts;

    assert(source);
    assert(cut);
    assert(out);
    placement_region_assert_valid(source);
    placement_rect_assert_valid(cut);

    ToriRS_PlacementRegion_SetRect(&cuts, cut);
    return placement_region_combine(source, &cuts, PLACEMENT_COMBINE_SUBTRACT, out);
}

enum ToriRS_PlacementStatus
ToriRS_PlacementRegion_Subtract(
    struct ToriRS_PlacementRegion const* source,
    struct ToriRS_PlacementRegion const* cuts,
    struct ToriRS_PlacementRegion* out)
{
    return placement_region_combine(source, cuts, PLACEMENT_COMBINE_SUBTRACT, out);
}

bool
ToriRS_PlacementRegion_ContainsRect(
    struct ToriRS_PlacementRegion const* region,
    struct ToriRS_PlacementRect const* rect)
{
    uint64_t covered = 0;

    assert(region);
    assert(rect);
    placement_region_assert_valid(region);
    placement_rect_assert_valid(rect);

    if( rect->w == 0 || rect->h == 0 )
        return true;

    for( int i = 0; i < region->_rect_count; i++ )
    {
        struct ToriRS_PlacementRect overlap;
        if( placement_rect_intersection(&region->_rects[i], rect, &overlap) )
            covered += placement_rect_area(&overlap);
    }
    return covered == placement_rect_area(rect);
}

bool
ToriRS_PlacementRegion_Equals(
    struct ToriRS_PlacementRegion const* a,
    struct ToriRS_PlacementRegion const* b)
{
    assert(a);
    assert(b);
    placement_region_assert_valid(a);
    placement_region_assert_valid(b);

    for( int i = 0; i < a->_rect_count; i++ )
        if( !ToriRS_PlacementRegion_ContainsRect(b, &a->_rects[i]) )
            return false;
    for( int i = 0; i < b->_rect_count; i++ )
        if( !ToriRS_PlacementRegion_ContainsRect(a, &b->_rects[i]) )
            return false;
    return true;
}

static int64_t
placement_abs_i64(int64_t value)
{
    return value < 0 ? -value : value;
}

static int64_t
placement_anchor_axis(
    int64_t low,
    int64_t high,
    int side,
    int margin)
{
    assert(side >= 0);
    assert(side <= 2);

    if( side == 0 )
        return 2 * (low + margin);
    if( side == 1 )
        return low + high;
    return 2 * (high - margin);
}

static int64_t
placement_rect_anchor_axis(
    int low,
    int size,
    int side)
{
    assert(side >= 0);
    assert(side <= 2);

    if( side == 0 )
        return 2 * (int64_t)low;
    if( side == 1 )
        return 2 * (int64_t)low + size;
    return 2 * ((int64_t)low + size);
}

bool
ToriRS_PlacementRegion_Place(
    struct ToriRS_PlacementRegion const* region,
    int anchor,
    int width,
    int height,
    int margin,
    struct ToriRS_PlacementRect* out_rect)
{
    struct ToriRS_PlacementRect best = { 0 };
    int best_fragment_x = 0;
    int best_fragment_y = 0;
    uint64_t best_distance = 0;
    int64_t min_x;
    int64_t min_y;
    int64_t max_right;
    int64_t max_bottom;
    int64_t target_x;
    int64_t target_y;
    int horizontal;
    int vertical;
    bool found = false;

    assert(region);
    assert(anchor >= 0);
    assert(anchor < TORIRS_PLACEMENT_ANCHOR_COUNT);
    assert(width > 0);
    assert(height > 0);
    assert(margin >= 0);
    assert(out_rect);
    placement_region_assert_valid(region);

    if( region->_rect_count == 0 )
        return false;

    horizontal = anchor % 3;
    vertical = anchor / 3;
    min_x = region->_rects[0].x;
    min_y = region->_rects[0].y;
    max_right = placement_rect_right(&region->_rects[0]);
    max_bottom = placement_rect_bottom(&region->_rects[0]);
    for( int i = 1; i < region->_rect_count; i++ )
    {
        struct ToriRS_PlacementRect const* rect = &region->_rects[i];
        if( rect->x < min_x )
            min_x = rect->x;
        if( rect->y < min_y )
            min_y = rect->y;
        if( placement_rect_right(rect) > max_right )
            max_right = placement_rect_right(rect);
        if( placement_rect_bottom(rect) > max_bottom )
            max_bottom = placement_rect_bottom(rect);
    }

    target_x = placement_anchor_axis(min_x, max_right, horizontal, margin);
    target_y = placement_anchor_axis(min_y, max_bottom, vertical, margin);

    for( int i = 0; i < region->_rect_count; i++ )
    {
        struct ToriRS_PlacementRect const* fragment = &region->_rects[i];
        struct ToriRS_PlacementRect candidate;
        int64_t const inner_w = (int64_t)fragment->w - 2 * (int64_t)margin;
        int64_t const inner_h = (int64_t)fragment->h - 2 * (int64_t)margin;
        int64_t candidate_x;
        int64_t candidate_y;
        uint64_t distance;

        if( inner_w < width || inner_h < height )
            continue;

        if( horizontal == 0 )
            candidate_x = (int64_t)fragment->x + margin;
        else if( horizontal == 1 )
            candidate_x = (int64_t)fragment->x + (fragment->w - width) / 2;
        else
            candidate_x = placement_rect_right(fragment) - margin - width;

        if( vertical == 0 )
            candidate_y = (int64_t)fragment->y + margin;
        else if( vertical == 1 )
            candidate_y = (int64_t)fragment->y + (fragment->h - height) / 2;
        else
            candidate_y = placement_rect_bottom(fragment) - margin - height;

        candidate.x = (int)candidate_x;
        candidate.y = (int)candidate_y;
        candidate.w = width;
        candidate.h = height;
        distance =
            (uint64_t)placement_abs_i64(
                placement_rect_anchor_axis(candidate.x, candidate.w, horizontal) - target_x) +
            (uint64_t)placement_abs_i64(
                placement_rect_anchor_axis(candidate.y, candidate.h, vertical) - target_y);

        if( !found || distance < best_distance ||
            (distance == best_distance && fragment->y < best_fragment_y) ||
            (distance == best_distance && fragment->y == best_fragment_y &&
             fragment->x < best_fragment_x) )
        {
            best = candidate;
            best_fragment_x = fragment->x;
            best_fragment_y = fragment->y;
            best_distance = distance;
            found = true;
        }
    }

    if( !found )
        return false;
    *out_rect = best;
    return true;
}
