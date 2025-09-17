#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N 277 // number of triangles to test
#define ITERATIONS 1000000

// More precise timing using nanoseconds directly
static inline long long
now_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// Version 1: oriented area (condensed form)
static inline int
dot_v1(int xa, int ya, int xb, int yb, int xc, int yc)
{
    return xa * (yb - yc) + xb * (yc - ya) + xc * (ya - yb);
}

// Version 2: expanded cross product
static inline int
dot_v2(int xa, int ya, int xb, int yb, int xc, int yc)
{
    return (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb);
}

// Removed old timing helper as we now use now_ns()

int
main(void)
{
    // Allocate random triangles
    int* xa = malloc(N * sizeof(int));
    int* ya = malloc(N * sizeof(int));
    int* xb = malloc(N * sizeof(int));
    int* yb = malloc(N * sizeof(int));
    int* xc = malloc(N * sizeof(int));
    int* yc = malloc(N * sizeof(int));

    if( !xa || !ya || !xb || !yb || !xc || !yc )
    {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    srand(12345);
    for( size_t i = 0; i < N; i++ )
    {
        xa[i] = rand() & 0xFFFF;
        ya[i] = rand() & 0xFFFF;
        xb[i] = rand() & 0xFFFF;
        yb[i] = rand() & 0xFFFF;
        xc[i] = rand() & 0xFFFF;
        yc[i] = rand() & 0xFFFF;
    }

    volatile long long checksum = 0; // prevents compiler from optimizing away
    long long v1_total = 0;
    long long v2_total = 0;

    for( int iter = 0; iter < ITERATIONS; iter++ )
    {
        // Benchmark version 1
        long long t1 = now_ns();
        for( size_t i = 0; i < N; i++ )
        {
            checksum += dot_v1(xa[i], ya[i], xb[i], yb[i], xc[i], yc[i]);
        }
        long long t2 = now_ns();
        v1_total += (t2 - t1);

        // Benchmark version 2
        long long t3 = now_ns();
        for( size_t i = 0; i < N; i++ )
        {
            checksum += dot_v2(xa[i], ya[i], xb[i], yb[i], xc[i], yc[i]);
        }
        long long t4 = now_ns();
        v2_total += (t4 - t3);
    }

    printf("v1 avg time: %.3f ns (checksum=%lld)\n", (double)v1_total / ITERATIONS, checksum);
    printf("v2 avg time: %.3f ns (checksum=%lld)\n", (double)v2_total / ITERATIONS, checksum);

    // // Benchmark version 1
    // double t1 = now_sec();
    // for( size_t i = 0; i < N; i++ )
    // {
    //     checksum += dot_v1(xa[i], ya[i], xb[i], yb[i], xc[i], yc[i]);
    // }
    // double t2 = now_sec();
    // printf("v1 time: %.3f sec (checksum=%lld)\n", t2 - t1, checksum);

    // checksum = 0;

    // // Benchmark version 2
    // double t3 = now_sec();
    // for( size_t i = 0; i < N; i++ )
    // {
    //     checksum += dot_v2(xa[i], ya[i], xb[i], yb[i], xc[i], yc[i]);
    // }
    // double t4 = now_sec();
    // printf("v2 time: %.3f sec (checksum=%lld)\n", t4 - t3, checksum);

    free(xa);
    free(ya);
    free(xb);
    free(yb);
    free(xc);
    free(yc);

    return 0;
}
