/**
 * Convex polygon span decomposition — the geometry every renderer shares.
 *
 * Worth testing here rather than per backend precisely because it IS shared:
 * a bug in it is a bug in all four at once, and it is the half that is easy to
 * get subtly wrong (double-counted vertices, off-by-one on the right edge,
 * clipping applied after the span is emitted).
 */

#include "render/torirs_polygon.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    char const* what)
{
    g_checks++;
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

#define GRID 64
static unsigned char g_grid[GRID][GRID];
static int g_span_count;

static void
grid_span(
    void* user_data,
    int x,
    int y,
    int count)
{
    (void)user_data;
    g_span_count++;
    for( int i = 0; i < count; i++ )
        if( x + i >= 0 && x + i < GRID && y >= 0 && y < GRID )
            g_grid[y][x + i]++;
}

static void
reset(void)
{
    memset(g_grid, 0, sizeof(g_grid));
    g_span_count = 0;
}

static int
grid_total(void)
{
    int n = 0;
    for( int y = 0; y < GRID; y++ )
        for( int x = 0; x < GRID; x++ )
            n += g_grid[y][x];
    return n;
}

static int
grid_max(void)
{
    int m = 0;
    for( int y = 0; y < GRID; y++ )
        for( int x = 0; x < GRID; x++ )
            if( g_grid[y][x] > m )
                m = g_grid[y][x];
    return m;
}

static void
test_rectangle_area(void)
{
    /* A 10x8 axis-aligned box. Half-open in y: rows 4..11 inclusive of the top
     * edge and exclusive of the bottom, so 8 rows of 11 pixels. */
    int const xs[] = { 5, 15, 15, 5 };
    int const ys[] = { 4, 4, 12, 12 };

    reset();
    ToriRS_PolygonFillConvex(xs, ys, 4, 0, 0, GRID, GRID, grid_span, NULL);

    check(g_span_count == 8, "a box fills one span per row");
    check(grid_total() == 8 * 11, "box area is rows x width");
    check(grid_max() == 1, "no pixel is filled twice");
    check(g_grid[4][5] == 1 && g_grid[11][15] == 1, "corners are covered");
    check(g_grid[12][5] == 0, "the bottom row is exclusive");
}

static void
test_triangle_is_not_double_counted(void)
{
    /* Vertices are where a naive fill counts a crossing twice and widens the
     * span, or counts zero and drops the row. */
    int const xs[] = { 10, 30, 20 };
    int const ys[] = { 5, 5, 25 };

    reset();
    ToriRS_PolygonFillConvex(xs, ys, 3, 0, 0, GRID, GRID, grid_span, NULL);

    check(grid_total() > 0, "triangle fills something");
    check(grid_max() == 1, "no pixel filled twice at a vertex row");
    /* Apex row is one pixel wide-ish; base row is the widest. */
    {
        int base = 0;
        int apex = 0;
        for( int x = 0; x < GRID; x++ )
        {
            base += g_grid[5][x] ? 1 : 0;
            apex += g_grid[24][x] ? 1 : 0;
        }
        check(base > apex, "the base row is wider than the apex row");
    }
}

static void
test_clipping(void)
{
    int const xs[] = { -20, 40, 40, -20 };
    int const ys[] = { -20, -20, 40, 40 };

    reset();
    ToriRS_PolygonFillConvex(xs, ys, 4, 10, 10, 8, 6, grid_span, NULL);

    check(grid_total() == 8 * 6, "a polygon larger than the clip fills exactly the clip");
    for( int y = 0; y < GRID; y++ )
        for( int x = 0; x < GRID; x++ )
            if( g_grid[y][x] && (x < 10 || x >= 18 || y < 10 || y >= 16) )
            {
                check(0, "nothing is filled outside the scissor box");
                return;
            }
    check(1, "nothing is filled outside the scissor box");
}

static void
test_degenerate(void)
{
    int const xs[] = { 5, 20, 35 };
    int const ys[] = { 5, 5, 5 };

    reset();
    /* Fewer than three points is not a polygon. */
    ToriRS_PolygonFillConvex(xs, ys, 2, 0, 0, GRID, GRID, grid_span, NULL);
    check(grid_total() == 0, "two points fill nothing");

    /* Three collinear points enclose no area. */
    reset();
    ToriRS_PolygonFillConvex(xs, ys, 3, 0, 0, GRID, GRID, grid_span, NULL);
    check(grid_total() == 0, "a zero-area polygon fills nothing");

    /* An empty scissor box. */
    reset();
    {
        int const bx[] = { 0, 10, 10, 0 };
        int const by[] = { 0, 0, 10, 10 };
        ToriRS_PolygonFillConvex(bx, by, 4, 0, 0, 0, 0, grid_span, NULL);
    }
    check(grid_total() == 0, "an empty clip fills nothing");
}

static void
test_winding_independence(void)
{
    int const cw_x[] = { 5, 5, 15, 15 };
    int const cw_y[] = { 4, 12, 12, 4 };
    int ccw_total;
    int cw_total;

    reset();
    {
        int const xs[] = { 5, 15, 15, 5 };
        int const ys[] = { 4, 4, 12, 12 };
        ToriRS_PolygonFillConvex(xs, ys, 4, 0, 0, GRID, GRID, grid_span, NULL);
    }
    ccw_total = grid_total();

    reset();
    ToriRS_PolygonFillConvex(cw_x, cw_y, 4, 0, 0, GRID, GRID, grid_span, NULL);
    cw_total = grid_total();

    check(ccw_total == cw_total && ccw_total > 0, "fill does not depend on winding order");
}

int
main(void)
{
    test_rectangle_area();
    test_triangle_is_not_double_counted();
    test_clipping();
    test_degenerate();
    test_winding_independence();

    printf("torirs_polygon_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
