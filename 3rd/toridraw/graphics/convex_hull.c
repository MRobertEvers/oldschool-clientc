#include "convex_hull.h"

#include <assert.h>

/**
 * Cross product of (b-a) x (c-a).
 *
 * `long long` because the operands are screen pixels: a 4096-wide viewport with
 * off-screen projected points gives differences past 2^16, and their product
 * past 2^32. In 32-bit arithmetic that wraps, and a wrapped sign is a turn read
 * backwards — the hull then folds through itself instead of being convex.
 *
 * > 0 counter-clockwise, < 0 clockwise, 0 collinear.
 */
static long long
toridraw_hull_cross(
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy)
{
    return (long long)(bx - ax) * (long long)(cy - ay) -
           (long long)(by - ay) * (long long)(cx - ax);
}

static long long
toridraw_hull_dist2(
    int ax,
    int ay,
    int bx,
    int by)
{
    long long dx = (long long)(bx - ax);
    long long dy = (long long)(by - ay);
    return dx * dx + dy * dy;
}

static void
toridraw_hull_swap(
    int* indices,
    int i,
    int j)
{
    int const tmp = indices[i];
    indices[i] = indices[j];
    indices[j] = tmp;
}

/**
 * Order the points after the pivot by the angle they make with it, nearest
 * first among collinear ones.
 *
 * Insertion sort, deliberately. The v0 version used a recursing quicksort whose
 * partition degrades to one frame per point on sorted input — and the input
 * here is a footprint's tile corners, which arrive in scan order and are as
 * close to sorted as a real input gets. At this size (tens of points) an
 * insertion sort is also simply faster, and it cannot overflow a stack.
 */
static void
toridraw_hull_sort_by_angle(
    const int* x,
    const int* y,
    int* indices,
    int count,
    int pivot_x,
    int pivot_y)
{
    for( int i = 2; i < count; i++ )
    {
        int const key = indices[i];
        int j = i - 1;

        while( j >= 1 )
        {
            long long const cross =
                toridraw_hull_cross(pivot_x, pivot_y, x[indices[j]], y[indices[j]], x[key], y[key]);
            int after;

            if( cross != 0 )
                after = cross < 0; /* key is clockwise of indices[j] -> sorts later */
            else
                after = toridraw_hull_dist2(pivot_x, pivot_y, x[indices[j]], y[indices[j]]) >
                        toridraw_hull_dist2(pivot_x, pivot_y, x[key], y[key]);

            if( !after )
                break;
            indices[j + 1] = indices[j];
            j--;
        }
        indices[j + 1] = key;
    }
}

int
ToriDraw_ConvexHull(
    const int* x,
    const int* y,
    int count,
    int* out_x,
    int* out_y)
{
    int indices[TORIDRAW_CONVEX_HULL_MAX_POINTS];
    int lowest;
    int hull_size;

    assert(x);
    assert(y);
    assert(out_x);
    assert(out_y);
    assert(count >= 0);
    assert(count <= TORIDRAW_CONVEX_HULL_MAX_POINTS);

    /* Under three points the input IS the hull. Not a failure: a footprint seen
     * edge-on projects to a line, and a caller draws that as a line. */
    if( count < 3 )
    {
        for( int i = 0; i < count; i++ )
        {
            out_x[i] = x[i];
            out_y[i] = y[i];
        }
        return count;
    }

    for( int i = 0; i < count; i++ )
        indices[i] = i;

    /* Lowest y, leftmost on a tie. This point is on the hull by construction,
     * and every other point lies in the half-plane above it, so sorting by
     * angle around it spans less than 180 degrees and cannot wrap. */
    lowest = 0;
    for( int i = 1; i < count; i++ )
    {
        if( y[i] < y[lowest] || (y[i] == y[lowest] && x[i] < x[lowest]) )
            lowest = i;
    }
    toridraw_hull_swap(indices, 0, lowest);

    toridraw_hull_sort_by_angle(x, y, indices, count, x[indices[0]], y[indices[0]]);

    out_x[0] = x[indices[0]];
    out_y[0] = y[indices[0]];
    hull_size = 1;

    for( int i = 1; i < count; i++ )
    {
        int const px = x[indices[i]];
        int const py = y[indices[i]];

        /* Drop the previous vertex while it is not a left turn. `<= 0` rather
         * than `< 0` also drops collinear points, so a straight edge comes back
         * as its two endpoints instead of every sample along it — which is what
         * keeps the emitted polygon's vertex count stable as the camera turns. */
        while( hull_size >= 2 &&
               toridraw_hull_cross(
                   out_x[hull_size - 2],
                   out_y[hull_size - 2],
                   out_x[hull_size - 1],
                   out_y[hull_size - 1],
                   px,
                   py) <= 0 )
            hull_size--;

        out_x[hull_size] = px;
        out_y[hull_size] = py;
        hull_size++;
    }

    /*
     * All-collinear input collapses to the two extremes.
     *
     * The scan above leaves such a set as a single point (every turn is
     * degenerate, so every vertex is popped), which would draw nothing where
     * the caller can plainly see a line. The sort put the farthest point last,
     * so the pivot and that point are the segment.
     */
    if( hull_size < 2 )
    {
        out_x[0] = x[indices[0]];
        out_y[0] = y[indices[0]];
        out_x[1] = x[indices[count - 1]];
        out_y[1] = y[indices[count - 1]];
        hull_size = 2;
    }

    return hull_size;
}
