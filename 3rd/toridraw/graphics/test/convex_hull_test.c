/**
 * Convex hull behaviour, including the degenerate shapes a loc outline actually
 * produces: a footprint seen edge-on, a one-tile loc, a slope that puts several
 * corners on one screen line.
 */

#include "graphics/convex_hull.h"

#include <stdio.h>

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

/** Every input point must lie inside or on the returned hull. */
static int
hull_contains_all(
    const int* x,
    const int* y,
    int count,
    const int* hx,
    const int* hy,
    int hull_size)
{
    if( hull_size < 3 )
        return 1; /* Degenerate hulls are checked directly by their cases. */

    for( int i = 0; i < count; i++ )
    {
        for( int h = 0; h < hull_size; h++ )
        {
            int const n = (h + 1) % hull_size;
            long long const cross = (long long)(hx[n] - hx[h]) * (y[i] - hy[h]) -
                                    (long long)(hy[n] - hy[h]) * (x[i] - hx[h]);
            /* Counter-clockwise hull: every point is left of or on each edge. */
            if( cross < 0 )
                return 0;
        }
    }
    return 1;
}

static void
test_square(void)
{
    /* A unit square with a point in the middle: the middle one is interior and
     * must not survive. */
    int const x[] = { 0, 10, 10, 0, 5 };
    int const y[] = { 0, 0, 10, 10, 5 };
    int hx[8];
    int hy[8];
    int const n = ToriDraw_ConvexHull(x, y, 5, hx, hy);

    check(n == 4, "square with an interior point hulls to 4 corners");
    check(hull_contains_all(x, y, 5, hx, hy, n), "every input point is inside the hull");
}

static void
test_collinear_edge_points_dropped(void)
{
    /* Points along the bottom edge must not each become a vertex — otherwise a
     * footprint's outline gains and loses vertices as the camera turns. */
    int const x[] = { 0, 5, 10, 10, 0 };
    int const y[] = { 0, 0, 0, 10, 10 };
    int hx[8];
    int hy[8];
    int const n = ToriDraw_ConvexHull(x, y, 5, hx, hy);

    check(n == 4, "a point mid-edge is not a hull vertex");
}

static void
test_all_collinear(void)
{
    /* A footprint seen edge-on. The hull is the segment, not a point. */
    int const x[] = { 0, 3, 9, 6 };
    int const y[] = { 0, 3, 9, 6 };
    int hx[8];
    int hy[8];
    int const n = ToriDraw_ConvexHull(x, y, 4, hx, hy);

    check(n == 2, "collinear input hulls to its two extremes");
    check(
        (hx[0] == 0 && hx[1] == 9) || (hx[0] == 9 && hx[1] == 0),
        "the two extremes are the endpoints");
}

static void
test_small_inputs(void)
{
    int const x[] = { 4, 7 };
    int const y[] = { 5, 9 };
    int hx[4];
    int hy[4];

    check(ToriDraw_ConvexHull(x, y, 0, hx, hy) == 0, "empty input gives an empty hull");
    check(ToriDraw_ConvexHull(x, y, 1, hx, hy) == 1, "one point is its own hull");
    check(ToriDraw_ConvexHull(x, y, 2, hx, hy) == 2, "two points are their own hull");
}

static void
test_duplicate_points(void)
{
    /* Adjacent tiles share corners, so a footprint's corner list always has
     * duplicates — they must not create zero-length hull edges. */
    int const x[] = { 0, 0, 10, 10, 10, 0, 0 };
    int const y[] = { 0, 0, 0, 0, 10, 10, 10 };
    int hx[8];
    int hy[8];
    int const n = ToriDraw_ConvexHull(x, y, 7, hx, hy);

    check(n == 4, "duplicated corners still hull to 4 vertices");
    for( int i = 0; i < n; i++ )
    {
        int const j = (i + 1) % n;
        check(!(hx[i] == hx[j] && hy[i] == hy[j]), "no zero-length hull edge");
    }
}

static void
test_winding_is_counter_clockwise(void)
{
    int const x[] = { 0, 10, 10, 0 };
    int const y[] = { 0, 0, 10, 10 };
    int hx[8];
    int hy[8];
    int const n = ToriDraw_ConvexHull(x, y, 4, hx, hy);
    long long area2 = 0;

    for( int i = 0; i < n; i++ )
    {
        int const j = (i + 1) % n;
        area2 += (long long)hx[i] * hy[j] - (long long)hx[j] * hy[i];
    }
    check(area2 > 0, "hull winds counter-clockwise");
}

/** Large coordinates must not overflow the turn test. */
static void
test_large_coordinates(void)
{
    int const x[] = { -100000, 100000, 100000, -100000, 0 };
    int const y[] = { -100000, -100000, 100000, 100000, 0 };
    int hx[8];
    int hy[8];
    int const n = ToriDraw_ConvexHull(x, y, 5, hx, hy);

    check(n == 4, "far off-screen points still hull correctly");
    check(hull_contains_all(x, y, 5, hx, hy, n), "large-coordinate hull contains its input");
}

/** A footprint's tile corners, the real input shape: already in scan order. */
static void
test_footprint_grid(void)
{
    int x[64];
    int y[64];
    int hx[64];
    int hy[64];
    int count = 0;

    for( int tz = 0; tz < 3; tz++ )
    {
        for( int tx = 0; tx < 3; tx++ )
        {
            static int const corner[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
            for( int c = 0; c < 4; c++ )
            {
                x[count] = (tx + corner[c][0]) * 20;
                y[count] = (tz + corner[c][1]) * 20;
                count++;
            }
        }
    }

    {
        int const n = ToriDraw_ConvexHull(x, y, count, hx, hy);
        check(count == 36, "3x3 footprint contributes 36 corners");
        check(n == 4, "and hulls to the outer 4 -- one outline, not nine boxes");
        check(hull_contains_all(x, y, count, hx, hy, n), "footprint hull contains every corner");
    }
}

int
main(void)
{
    test_square();
    test_collinear_edge_points_dropped();
    test_all_collinear();
    test_small_inputs();
    test_duplicate_points();
    test_winding_is_counter_clockwise();
    test_large_coordinates();
    test_footprint_grid();

    printf("convex_hull_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
