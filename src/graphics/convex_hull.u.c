#ifndef CONVEX_HULL_U_C
#define CONVEX_HULL_U_C

#include <stdlib.h>
#include <string.h>

// Manual quicksort implementation to avoid globals and structs
static void
swap_indices(size_t* arr, size_t i, size_t j)
{
    size_t temp = arr[i];
    arr[i] = arr[j];
    arr[j] = temp;
}

static int
compare_by_angle(
    float* x_coords, float* y_coords, size_t idx_a, size_t idx_b, float pivot_x, float pivot_y)
{
    float ax = x_coords[idx_a];
    float ay = y_coords[idx_a];
    float bx = x_coords[idx_b];
    float by = y_coords[idx_b];

    float cross = (ax - pivot_x) * (by - pivot_y) - (ay - pivot_y) * (bx - pivot_x);
    if( cross == 0 )
    {
        float dist_a = (ax - pivot_x) * (ax - pivot_x) + (ay - pivot_y) * (ay - pivot_y);
        float dist_b = (bx - pivot_x) * (bx - pivot_x) + (by - pivot_y) * (by - pivot_y);
        return (dist_a < dist_b) ? -1 : (dist_a > dist_b) ? 1 : 0;
    }
    return (cross > 0) ? -1 : 1;
}

static void
sort_by_polar_angle(
    size_t* indices,
    size_t left,
    size_t right,
    float* x_coords,
    float* y_coords,
    float pivot_x,
    float pivot_y)
{
    if( left >= right )
        return;

    // Quicksort partition
    size_t pivot_idx = left + (right - left) / 2;
    swap_indices(indices, pivot_idx, right);

    size_t store_idx = left;
    for( size_t i = left; i < right; i++ )
    {
        if( compare_by_angle(x_coords, y_coords, indices[i], indices[right], pivot_x, pivot_y) < 0 )
        {
            swap_indices(indices, i, store_idx);
            store_idx++;
        }
    }
    swap_indices(indices, store_idx, right);

    // Recursively sort left and right partitions
    if( store_idx > left )
        sort_by_polar_angle(indices, left, store_idx - 1, x_coords, y_coords, pivot_x, pivot_y);
    if( store_idx < right )
        sort_by_polar_angle(indices, store_idx + 1, right, x_coords, y_coords, pivot_x, pivot_y);
}

// Compute convex hull using Graham scan algorithm
// Returns points in counter-clockwise order
// out_x and out_y must be pre-allocated with at least num_points capacity
// Returns the number of points in the convex hull
static inline size_t
convex_hull_graham_scan(
    float* x_coords, float* y_coords, size_t num_points, float* out_x, float* out_y)
{
    if( num_points < 3 )
    {
        for( size_t i = 0; i < num_points; i++ )
        {
            out_x[i] = x_coords[i];
            out_y[i] = y_coords[i];
        }
        return num_points;
    }

    // Create index array for sorting
    size_t* indices = (size_t*)malloc(num_points * sizeof(size_t));
    for( size_t i = 0; i < num_points; i++ )
    {
        indices[i] = i;
    }

    // Find the point with lowest y-coordinate (and leftmost if tie)
    size_t min_idx = 0;
    for( size_t i = 1; i < num_points; i++ )
    {
        if( y_coords[i] < y_coords[min_idx] ||
            (y_coords[i] == y_coords[min_idx] && x_coords[i] < x_coords[min_idx]) )
        {
            min_idx = i;
        }
    }

    // Swap minimum point to front
    size_t temp_idx = indices[0];
    indices[0] = indices[min_idx];
    indices[min_idx] = temp_idx;

    float pivot_x = x_coords[indices[0]];
    float pivot_y = y_coords[indices[0]];

    // Sort points by polar angle with respect to pivot
    sort_by_polar_angle(indices, 1, num_points - 1, x_coords, y_coords, pivot_x, pivot_y);

    // Build convex hull using output arrays
    size_t hull_size = 0;
    out_x[hull_size] = x_coords[indices[0]];
    out_y[hull_size] = y_coords[indices[0]];
    hull_size++;

    out_x[hull_size] = x_coords[indices[1]];
    out_y[hull_size] = y_coords[indices[1]];
    hull_size++;

    for( size_t i = 2; i < num_points; i++ )
    {
        // Remove points that make clockwise turn
        while( hull_size > 1 )
        {
            float p1_x = out_x[hull_size - 2];
            float p1_y = out_y[hull_size - 2];
            float p2_x = out_x[hull_size - 1];
            float p2_y = out_y[hull_size - 1];
            float p3_x = x_coords[indices[i]];
            float p3_y = y_coords[indices[i]];

            // Cross product to check turn direction
            float cross = (p2_x - p1_x) * (p3_y - p1_y) - (p2_y - p1_y) * (p3_x - p1_x);
            if( cross > 0 )
            {
                // Counter-clockwise turn - good
                break;
            }
            // Clockwise or collinear - remove p2
            hull_size--;
        }
        out_x[hull_size] = x_coords[indices[i]];
        out_y[hull_size] = y_coords[indices[i]];
        hull_size++;
    }

    free(indices);
    return hull_size;
}

// Jarvis march (gift wrapping) algorithm - O(nh) where h is hull size
// Simpler than Graham scan, no sorting required
// Returns points in counter-clockwise order
// out_x and out_y must be pre-allocated with at least num_points capacity
// Returns the number of points in the convex hull
static inline size_t
convex_hull_jarvis(float* x_coords, float* y_coords, size_t num_points, float* out_x, float* out_y)
{
    if( num_points < 3 )
    {
        for( size_t i = 0; i < num_points; i++ )
        {
            out_x[i] = x_coords[i];
            out_y[i] = y_coords[i];
        }
        return num_points;
    }

    // Find the leftmost point (lowest x, then lowest y for tiebreak)
    size_t left = 0;
    for( size_t i = 1; i < num_points; i++ )
    {
        if( x_coords[i] < x_coords[left] ||
            (x_coords[i] == x_coords[left] && y_coords[i] < y_coords[left]) )
        {
            left = i;
        }
    }

    // Current point we are on
    size_t current = left;
    size_t hull_size = 0;

    do
    {
        float cx = x_coords[current];
        float cy = y_coords[current];

        out_x[hull_size] = cx;
        out_y[hull_size] = cy;
        hull_size++;

        // Safety check to prevent infinite loops
        if( hull_size > num_points )
        {
            return 0; // Invalid hull
        }

        // Find the next point - all points are to the right of the
        // line between current and next
        size_t next = 0;
        float nx = x_coords[next];
        float ny = y_coords[next];

        for( size_t i = 1; i < num_points; i++ )
        {
            float ix = x_coords[i];
            float iy = y_coords[i];

            // Cross product: (q - p) × (r - q)
            // Positive = counter-clockwise turn (i is to the left of current->next)
            // Negative = clockwise turn (i is to the right of current->next)
            long long cross = (long long)(iy - cy) * (nx - ix) - (long long)(ix - cx) * (ny - iy);

            // If positive cross product, or collinear but farther away, update next
            if( cross > 0 )
            {
                next = i;
                nx = ix;
                ny = iy;
            }
            else if( cross == 0 )
            {
                // Collinear - pick the farther point
                long long dist_i =
                    (long long)(cx - ix) * (cx - ix) + (long long)(cy - iy) * (cy - iy);
                long long dist_next =
                    (long long)(cx - nx) * (cx - nx) + (long long)(cy - ny) * (cy - ny);
                if( dist_i > dist_next )
                {
                    next = i;
                    nx = ix;
                    ny = iy;
                }
            }
        }

        current = next;
    } while( current != left );

    return hull_size;
}

static inline size_t
compute_convex_hull(float* x_coords, float* y_coords, size_t num_points, float* out_x, float* out_y)
{
    return convex_hull_graham_scan(x_coords, y_coords, num_points, out_x, out_y);
}

#endif