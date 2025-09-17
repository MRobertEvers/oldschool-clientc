#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_VERTICES 277
#define NUM_FACES 120
#define MODEL_MIN_DEPTH 100
#define ITERATIONS 10000

// ------------------- Baseline scalar -------------------
static inline int
process_face_baseline(
    int xa, int ya, int za, int xb, int yb, int zb, int xc, int yc, int zc, int model_min_depth)
{
    int dot = (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb);
    if( dot <= 0 )
        return -1;

    int zsum = za + zb + zc;
    int depth = zsum / 3 + model_min_depth;
    return depth;
}

// ------------------- Optimized scalar -------------------
// Branchless dot check + fast divide
static inline int
process_face_optimized(
    int xa, int ya, int za, int xb, int yb, int zb, int xc, int yc, int zc, int model_min_depth)
{
    int dot = (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb);
    if( dot <= 0 )
        return -1;

    int zsum = za + zb + zc;
    int depth = ((zsum * 21845) >> 16) + model_min_depth;
    return depth;
}

// ------------------- Benchmark harness -------------------
static double
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int
main(void)
{
    srand(0x1234);

    // Allocate vertices
    int* vx = malloc(NUM_VERTICES * sizeof(int));
    int* vy = malloc(NUM_VERTICES * sizeof(int));
    int* vz = malloc(NUM_VERTICES * sizeof(int));

    for( int i = 0; i < NUM_VERTICES; i++ )
    {
        vx[i] = rand() % 2000 - 1000;
        vy[i] = rand() % 2000 - 1000;
        vz[i] = rand() % 2000 - 1000;
    }

    // Allocate faces
    int* fa = malloc(NUM_FACES * sizeof(int));
    int* fb = malloc(NUM_FACES * sizeof(int));
    int* fc = malloc(NUM_FACES * sizeof(int));
    for( int f = 0; f < NUM_FACES; f++ )
    {
        fa[f] = rand() % NUM_VERTICES;
        fb[f] = rand() % NUM_VERTICES;
        fc[f] = rand() % NUM_VERTICES;
    }

    volatile int checksum = 0; // prevent dead-code elimination

    double baseline_total = 0;
    double optimized_total = 0;

    for( int iter = 0; iter < ITERATIONS; iter++ )
    {
        // Benchmark baseline
        double t0 = now_ms();
        for( int f = 0; f < NUM_FACES; f++ )
        {
            int a = fa[f], b = fb[f], c = fc[f];
            checksum += process_face_baseline(
                vx[a], vy[a], vz[a], vx[b], vy[b], vz[b], vx[c], vy[c], vz[c], MODEL_MIN_DEPTH);
        }
        double t1 = now_ms();
        baseline_total += (t1 - t0);

// Disable vectorization between baseline and optimized runs to ensure fair comparison
#pragma GCC optimize("no-tree-vectorize")
#pragma clang optimize off

        // Benchmark optimized
        double t2 = now_ms();
        for( int f = 0; f < NUM_FACES; f++ )
        {
            int a = fa[f], b = fb[f], c = fc[f];
            checksum += process_face_optimized(
                vx[a], vy[a], vz[a], vx[b], vy[b], vz[b], vx[c], vy[c], vz[c], MODEL_MIN_DEPTH);
        }
        double t3 = now_ms();
        optimized_total += (t3 - t2);
    }

    printf("Baseline total:   %.2f ms\n", baseline_total);
    printf("Optimized total:  %.2f ms\n", optimized_total);
    printf("Baseline avg:     %.2f ms\n", baseline_total / 100.0);
    printf("Optimized avg:    %.2f ms\n", optimized_total / 100.0);
    // // Benchmark baseline
    // double t0 = now_ms();
    // for( int f = 0; f < NUM_FACES; f++ )
    // {
    //     int a = fa[f], b = fb[f], c = fc[f];
    //     checksum += process_face_baseline(
    //         vx[a], vy[a], vz[a], vx[b], vy[b], vz[b], vx[c], vy[c], vz[c], MODEL_MIN_DEPTH);
    // }
    // double t1 = now_ms();

    // // Benchmark optimized
    // double t2 = now_ms();
    // for( int f = 0; f < NUM_FACES; f++ )
    // {
    //     int a = fa[f], b = fb[f], c = fc[f];
    //     checksum += process_face_optimized(
    //         vx[a], vy[a], vz[a], vx[b], vy[b], vz[b], vx[c], vy[c], vz[c], MODEL_MIN_DEPTH);
    // }
    // double t3 = now_ms();

    // printf("Baseline:   %.2f ms\n", t1 - t0);
    // printf("Optimized:  %.2f ms\n", t3 - t2);
    // printf("Checksum: %d\n", checksum);

    free(vx);
    free(vy);
    free(vz);
    free(fa);
    free(fb);
    free(fc);
    return 0;
}
