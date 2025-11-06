#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>

// Graham scan sorting helpers
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

    if( store_idx > left )
        sort_by_polar_angle(indices, left, store_idx - 1, x_coords, y_coords, pivot_x, pivot_y);
    if( store_idx < right )
        sort_by_polar_angle(indices, store_idx + 1, right, x_coords, y_coords, pivot_x, pivot_y);
}

// Graham scan algorithm - O(n log n)
static size_t
compute_convex_hull_graham(float* x_coords, float* y_coords, size_t num_points, float* out_x, float* out_y)
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

    size_t* indices = (size_t*)malloc(num_points * sizeof(size_t));
    for( size_t i = 0; i < num_points; i++ )
    {
        indices[i] = i;
    }

    size_t min_idx = 0;
    for( size_t i = 1; i < num_points; i++ )
    {
        if( y_coords[i] < y_coords[min_idx] ||
            (y_coords[i] == y_coords[min_idx] && x_coords[i] < x_coords[min_idx]) )
        {
            min_idx = i;
        }
    }

    size_t temp_idx = indices[0];
    indices[0] = indices[min_idx];
    indices[min_idx] = temp_idx;

    float pivot_x = x_coords[indices[0]];
    float pivot_y = y_coords[indices[0]];

    sort_by_polar_angle(indices, 1, num_points - 1, x_coords, y_coords, pivot_x, pivot_y);

    size_t hull_size = 0;
    out_x[hull_size] = x_coords[indices[0]];
    out_y[hull_size] = y_coords[indices[0]];
    hull_size++;

    out_x[hull_size] = x_coords[indices[1]];
    out_y[hull_size] = y_coords[indices[1]];
    hull_size++;

    for( size_t i = 2; i < num_points; i++ )
    {
        while( hull_size > 1 )
        {
            float p1_x = out_x[hull_size - 2];
            float p1_y = out_y[hull_size - 2];
            float p2_x = out_x[hull_size - 1];
            float p2_y = out_y[hull_size - 1];
            float p3_x = x_coords[indices[i]];
            float p3_y = y_coords[indices[i]];

            float cross = (p2_x - p1_x) * (p3_y - p1_y) - (p2_y - p1_y) * (p3_x - p1_x);
            if( cross > 0 )
            {
                break;
            }
            hull_size--;
        }
        out_x[hull_size] = x_coords[indices[i]];
        out_y[hull_size] = y_coords[indices[i]];
        hull_size++;
    }

    free(indices);
    return hull_size;
}

// Jarvis march algorithm - O(nh)
static size_t
compute_convex_hull_jarvis(float* x_coords, float* y_coords, size_t num_points, float* out_x, float* out_y)
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

    size_t left = 0;
    for( size_t i = 1; i < num_points; i++ )
    {
        if( x_coords[i] < x_coords[left] ||
            (x_coords[i] == x_coords[left] && y_coords[i] < y_coords[left]) )
        {
            left = i;
        }
    }

    size_t current = left;
    size_t hull_size = 0;

    do
    {
        float cx = x_coords[current];
        float cy = y_coords[current];
        
        out_x[hull_size] = cx;
        out_y[hull_size] = cy;
        hull_size++;

        if( hull_size > num_points )
        {
            return 0;
        }

        size_t next = 0;
        float nx = x_coords[next];
        float ny = y_coords[next];

        for( size_t i = 1; i < num_points; i++ )
        {
            float ix = x_coords[i];
            float iy = y_coords[i];

            long long cross = (long long)(iy - cy) * (nx - ix) - (long long)(ix - cx) * (ny - iy);

            if( cross > 0 )
            {
                next = i;
                nx = ix;
                ny = iy;
            }
            else if( cross == 0 )
            {
                long long dist_i = (long long)(cx - ix) * (cx - ix) + (long long)(cy - iy) * (cy - iy);
                long long dist_next = (long long)(cx - nx) * (cx - nx) + (long long)(cy - ny) * (cy - ny);
                if( dist_i > dist_next )
                {
                    next = i;
                    nx = ix;
                    ny = iy;
                }
            }
        }

        current = next;
    }
    while( current != left );

    return hull_size;
}

// Generate random points in a range
static void
generate_random_points(float* x_coords, float* y_coords, size_t num_points, float min_val, float max_val)
{
    float range = max_val - min_val;
    for( size_t i = 0; i < num_points; i++ )
    {
        x_coords[i] = min_val + ((float)rand() / RAND_MAX) * range;
        y_coords[i] = min_val + ((float)rand() / RAND_MAX) * range;
    }
}

// Generate points in a circle pattern (produces smaller hulls)
static void
generate_circle_points(float* x_coords, float* y_coords, size_t num_points, float radius)
{
    for( size_t i = 0; i < num_points; i++ )
    {
        // Random angle
        float angle = ((float)rand() / RAND_MAX) * 2.0f * 3.14159265359f;
        // Random radius (sqrt for uniform distribution)
        float r = radius * sqrtf((float)rand() / RAND_MAX);
        x_coords[i] = r * cosf(angle);
        y_coords[i] = r * sinf(angle);
    }
}

// Get time in microseconds
static double
get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000.0 + ts.tv_nsec / 1000.0;
}

// Run benchmark
static void
benchmark(size_t num_points, const char* distribution)
{
    printf("\n=== Benchmark: %zu points (%s distribution) ===\n", num_points, distribution);

    float* x_coords = (float*)malloc(num_points * sizeof(float));
    float* y_coords = (float*)malloc(num_points * sizeof(float));
    float* out_x_graham = (float*)malloc(num_points * sizeof(float));
    float* out_y_graham = (float*)malloc(num_points * sizeof(float));
    float* out_x_jarvis = (float*)malloc(num_points * sizeof(float));
    float* out_y_jarvis = (float*)malloc(num_points * sizeof(float));

    if( strcmp(distribution, "random") == 0 )
    {
        generate_random_points(x_coords, y_coords, num_points, 0.0f, 1000.0f);
    }
    else
    {
        generate_circle_points(x_coords, y_coords, num_points, 500.0f);
    }

    // Warmup
    compute_convex_hull_graham(x_coords, y_coords, num_points, out_x_graham, out_y_graham);
    compute_convex_hull_jarvis(x_coords, y_coords, num_points, out_x_jarvis, out_y_jarvis);

    // Benchmark Graham scan
    const int iterations = 100;
    double graham_start = get_time_us();
    size_t graham_hull_size = 0;
    for( int i = 0; i < iterations; i++ )
    {
        graham_hull_size = compute_convex_hull_graham(x_coords, y_coords, num_points, out_x_graham, out_y_graham);
    }
    double graham_end = get_time_us();
    double graham_avg = (graham_end - graham_start) / iterations;

    // Benchmark Jarvis march
    double jarvis_start = get_time_us();
    size_t jarvis_hull_size = 0;
    for( int i = 0; i < iterations; i++ )
    {
        jarvis_hull_size = compute_convex_hull_jarvis(x_coords, y_coords, num_points, out_x_jarvis, out_y_jarvis);
    }
    double jarvis_end = get_time_us();
    double jarvis_avg = (jarvis_end - jarvis_start) / iterations;

    // Verify both produce same hull size
    printf("Graham scan:  %.2f μs (hull size: %zu)\n", graham_avg, graham_hull_size);
    printf("Jarvis march: %.2f μs (hull size: %zu)\n", jarvis_avg, jarvis_hull_size);
    
    if( graham_hull_size == jarvis_hull_size )
    {
        printf("✓ Hull sizes match\n");
    }
    else
    {
        printf("✗ ERROR: Hull sizes differ!\n");
    }

    double speedup = jarvis_avg / graham_avg;
    if( speedup > 1.0 )
    {
        printf("Graham scan is %.2fx faster\n", speedup);
    }
    else
    {
        printf("Jarvis march is %.2fx faster\n", 1.0 / speedup);
    }

    free(x_coords);
    free(y_coords);
    free(out_x_graham);
    free(out_y_graham);
    free(out_x_jarvis);
    free(out_y_jarvis);
}

int
main(void)
{
    srand(42); // Fixed seed for reproducibility

    printf("Convex Hull Algorithm Benchmark\n");
    printf("================================\n");

    // Test with different point counts
    benchmark(120, "random");
    benchmark(120, "circle");
    
    benchmark(200, "random");
    benchmark(200, "circle");
    
    benchmark(300, "random");
    benchmark(300, "circle");
    
    benchmark(500, "random");
    benchmark(500, "circle");

    printf("\n=== Summary ===\n");
    printf("Graham scan: O(n log n) - better for large hulls\n");
    printf("Jarvis march: O(nh) - better for small hulls (h << n)\n");
    printf("\nRandom distribution typically produces larger hulls.\n");
    printf("Circle distribution produces smaller hulls (~sqrt(n) points on hull).\n");

    return 0;
}

