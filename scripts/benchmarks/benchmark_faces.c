#include <arm_neon.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_VERTICES 100000
#define NUM_FACES 200000
#define MODEL_MIN_DEPTH 100

// ------------------- Scalar version -------------------
static inline int
process_face_scalar(
    int xa, int ya, int za, int xb, int yb, int zb, int xc, int yc, int zc, int model_min_depth)
{
    int dot = (xa - xb) * (yc - yb) - (ya - yb) * (xc - xb);
    if( dot <= 0 )
        return -1;

    int zsum = za + zb + zc;
    int depth = zsum / 3 + model_min_depth;
    return depth;
}

// ------------------- NEON version -------------------
static inline int
process_face_neon(
    int xa, int ya, int za, int xb, int yb, int zb, int xc, int yc, int zc, int model_min_depth)
{
    int32x4_t vxa = { xa, ya, xc, 0 };
    int32x4_t vxb = { xb, yb, xb, 0 };
    int32x4_t vyc = { yc, yb, xb, 0 };

    int32x4_t diff1 = vsubq_s32(vxa, vxb);
    int32x4_t diff2 = vsubq_s32(vyc, vxb);
    int32x4_t mul = vmulq_s32(diff1, diff2);

    int dot = vgetq_lane_s32(mul, 0) - vgetq_lane_s32(mul, 1);
    if( dot <= 0 )
        return -1;

    int32x4_t vz = { za, zb, zc, 0 };
    int32x2_t sum_low = vadd_s32(vget_low_s32(vz), vget_high_s32(vz));
    int zsum = vget_lane_s32(sum_low, 0) + vget_lane_s32(sum_low, 1);

    int depth = (zsum * 21845) >> 16; // divide by 3
    depth += model_min_depth;
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

    // Benchmark scalar
    double t0 = now_ms();
    for( int f = 0; f < NUM_FACES; f++ )
    {
        int a = fa[f], b = fb[f], c = fc[f];
        checksum += process_face_scalar(
            vx[a], vy[a], vz[a], vx[b], vy[b], vz[b], vx[c], vy[c], vz[c], MODEL_MIN_DEPTH);
    }
    double t1 = now_ms();

    // Benchmark NEON
    double t2 = now_ms();
    for( int f = 0; f < NUM_FACES; f++ )
    {
        int a = fa[f], b = fb[f], c = fc[f];
        checksum += process_face_neon(
            vx[a], vy[a], vz[a], vx[b], vy[b], vz[b], vx[c], vy[c], vz[c], MODEL_MIN_DEPTH);
    }
    double t3 = now_ms();

    printf("Scalar: %.2f ms\n", t1 - t0);
    printf("NEON:   %.2f ms\n", t3 - t2);
    printf("Checksum: %d\n", checksum);

    free(vx);
    free(vy);
    free(vz);
    free(fa);
    free(fb);
    free(fc);
    return 0;
}