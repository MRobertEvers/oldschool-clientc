/*
 * Microbenchmark: projection_neon_zdiv_tex_tail / projection_neon_zdiv_notex_tail
 * "before" = lane build (vsetq_lane_s32) + vgetq_lane_s32 stores (committed/HEAD)
 * "after"  = stack int32_t[4] + vld1q_s32 / vst1q_s32 + scalar write-back (pending)
 *
 * From this directory (Apple Silicon / Linux aarch64, NEON):
 *   make && ./bench_projection_zdiv_neon_tail
 *
 * Optional: make BENCH_ITERS=5000000 BENCH_WARMUP=10000
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if ( defined(__ARM_NEON) || defined(__ARM_NEON__) ) && !defined(NEON_DISABLED)

#include <arm_neon.h>

#ifndef BENCH_ITERS
#define BENCH_ITERS 2500000
#endif

#ifndef BENCH_WARMUP
#define BENCH_WARMUP 8000
#endif

/* --- "before": HEAD lane-based tails (assert omitted) ------------------- */

static void
bench_tex_tail_lane(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int base,
    int rem,
    int model_mid_z,
    int near_plane_z)
{
    if( rem <= 0 )
        return;

    int z_pad = (near_plane_z < INT_MAX) ? (near_plane_z + 1) : near_plane_z;
    int32x4_t z_i = vdupq_n_s32(z_pad);
    int32x4_t x = vdupq_n_s32(0);
    int32x4_t y = vdupq_n_s32(0);
    if( rem == 1 )
    {
        z_i = vsetq_lane_s32(orthographic_vertices_z[base], z_i, 0);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
    }
    else if( rem == 2 )
    {
        z_i = vsetq_lane_s32(orthographic_vertices_z[base], z_i, 0);
        z_i = vsetq_lane_s32(orthographic_vertices_z[base + 1], z_i, 1);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        x = vsetq_lane_s32(screen_vertices_x[base + 1], x, 1);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
        y = vsetq_lane_s32(screen_vertices_y[base + 1], y, 1);
    }
    else
    {
        z_i = vsetq_lane_s32(orthographic_vertices_z[base], z_i, 0);
        z_i = vsetq_lane_s32(orthographic_vertices_z[base + 1], z_i, 1);
        z_i = vsetq_lane_s32(orthographic_vertices_z[base + 2], z_i, 2);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        x = vsetq_lane_s32(screen_vertices_x[base + 1], x, 1);
        x = vsetq_lane_s32(screen_vertices_x[base + 2], x, 2);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
        y = vsetq_lane_s32(screen_vertices_y[base + 1], y, 1);
        y = vsetq_lane_s32(screen_vertices_y[base + 2], y, 2);
    }

    int32x4_t midz = vdupq_n_s32(model_mid_z);
    int32x4_t outz = vsubq_s32(z_i, midz);

    uint32x4_t clipped_mask = vcltq_s32(z_i, vdupq_n_s32(near_plane_z));

    float32x4_t z_f = vcvtq_f32_s32(z_i);
    float32x4_t recip = vrecpeq_f32(z_f);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);

    float32x4_t scale = vdupq_n_f32((float)(1ll << 30));
    float32x4_t recip_scaled = vmulq_f32(recip, scale);
    int32x4_t recip_q31 = vcvtq_s32_f32(recip_scaled);

    int64x2_t xl0 = vmull_s32(vget_low_s32(x), vget_low_s32(recip_q31));
    int64x2_t xl1 = vmull_s32(vget_high_s32(x), vget_high_s32(recip_q31));
    int64x2_t yl0 = vmull_s32(vget_low_s32(y), vget_low_s32(recip_q31));
    int64x2_t yl1 = vmull_s32(vget_high_s32(y), vget_high_s32(recip_q31));

    int32x4_t x_div = vcombine_s32(vshrn_n_s64(xl0, 30), vshrn_n_s64(xl1, 30));
    int32x4_t y_div = vcombine_s32(vshrn_n_s64(yl0, 30), vshrn_n_s64(yl1, 30));

    int32x4_t neg5000 = vdupq_n_s32(-5000);
    x_div = vbslq_s32(clipped_mask, neg5000, x_div);

    if( rem >= 1 )
    {
        screen_vertices_x[base] = vgetq_lane_s32(x_div, 0);
        screen_vertices_y[base] = vgetq_lane_s32(y_div, 0);
        screen_vertices_z[base] = vgetq_lane_s32(outz, 0);
    }
    if( rem >= 2 )
    {
        screen_vertices_x[base + 1] = vgetq_lane_s32(x_div, 1);
        screen_vertices_y[base + 1] = vgetq_lane_s32(y_div, 1);
        screen_vertices_z[base + 1] = vgetq_lane_s32(outz, 1);
    }
    if( rem >= 3 )
    {
        screen_vertices_x[base + 2] = vgetq_lane_s32(x_div, 2);
        screen_vertices_y[base + 2] = vgetq_lane_s32(y_div, 2);
        screen_vertices_z[base + 2] = vgetq_lane_s32(outz, 2);
    }
}

static void
bench_notex_tail_lane(
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int base,
    int rem,
    int model_mid_z,
    int near_plane_z)
{
    if( rem <= 0 )
        return;

    int z_pad = (near_plane_z < INT_MAX) ? (near_plane_z + 1) : near_plane_z;
    int32x4_t z_i = vdupq_n_s32(z_pad);
    int32x4_t x = vdupq_n_s32(0);
    int32x4_t y = vdupq_n_s32(0);
    if( rem == 1 )
    {
        z_i = vsetq_lane_s32(screen_vertices_z[base], z_i, 0);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
    }
    else if( rem == 2 )
    {
        z_i = vsetq_lane_s32(screen_vertices_z[base], z_i, 0);
        z_i = vsetq_lane_s32(screen_vertices_z[base + 1], z_i, 1);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        x = vsetq_lane_s32(screen_vertices_x[base + 1], x, 1);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
        y = vsetq_lane_s32(screen_vertices_y[base + 1], y, 1);
    }
    else
    {
        z_i = vsetq_lane_s32(screen_vertices_z[base], z_i, 0);
        z_i = vsetq_lane_s32(screen_vertices_z[base + 1], z_i, 1);
        z_i = vsetq_lane_s32(screen_vertices_z[base + 2], z_i, 2);
        x = vsetq_lane_s32(screen_vertices_x[base], x, 0);
        x = vsetq_lane_s32(screen_vertices_x[base + 1], x, 1);
        x = vsetq_lane_s32(screen_vertices_x[base + 2], x, 2);
        y = vsetq_lane_s32(screen_vertices_y[base], y, 0);
        y = vsetq_lane_s32(screen_vertices_y[base + 1], y, 1);
        y = vsetq_lane_s32(screen_vertices_y[base + 2], y, 2);
    }

    int32x4_t midz = vdupq_n_s32(model_mid_z);
    int32x4_t outz = vsubq_s32(z_i, midz);

    uint32x4_t clipped_mask = vcltq_s32(z_i, vdupq_n_s32(near_plane_z));

    float32x4_t z_f = vcvtq_f32_s32(z_i);
    float32x4_t recip = vrecpeq_f32(z_f);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);

    float32x4_t scale = vdupq_n_f32((float)(1ll << 30));
    float32x4_t recip_scaled = vmulq_f32(recip, scale);
    int32x4_t recip_q31 = vcvtq_s32_f32(recip_scaled);

    int64x2_t xl0 = vmull_s32(vget_low_s32(x), vget_low_s32(recip_q31));
    int64x2_t xl1 = vmull_s32(vget_high_s32(x), vget_high_s32(recip_q31));
    int64x2_t yl0 = vmull_s32(vget_low_s32(y), vget_low_s32(recip_q31));
    int64x2_t yl1 = vmull_s32(vget_high_s32(y), vget_high_s32(recip_q31));

    int32x4_t x_div = vcombine_s32(vshrn_n_s64(xl0, 30), vshrn_n_s64(xl1, 30));
    int32x4_t y_div = vcombine_s32(vshrn_n_s64(yl0, 30), vshrn_n_s64(yl1, 30));

    int32x4_t neg5000 = vdupq_n_s32(-5000);
    x_div = vbslq_s32(clipped_mask, neg5000, x_div);

    if( rem >= 1 )
    {
        screen_vertices_x[base] = vgetq_lane_s32(x_div, 0);
        screen_vertices_y[base] = vgetq_lane_s32(y_div, 0);
        screen_vertices_z[base] = vgetq_lane_s32(outz, 0);
    }
    if( rem >= 2 )
    {
        screen_vertices_x[base + 1] = vgetq_lane_s32(x_div, 1);
        screen_vertices_y[base + 1] = vgetq_lane_s32(y_div, 1);
        screen_vertices_z[base + 1] = vgetq_lane_s32(outz, 1);
    }
    if( rem >= 3 )
    {
        screen_vertices_x[base + 2] = vgetq_lane_s32(x_div, 2);
        screen_vertices_y[base + 2] = vgetq_lane_s32(y_div, 2);
        screen_vertices_z[base + 2] = vgetq_lane_s32(outz, 2);
    }
}

/* --- "after": stack-buffer tails (pending tree) ------------------------ */

static void
bench_tex_tail_stack(
    const int* orthographic_vertices_z,
    int* screen_vertices_x,
    int* screen_vertices_y,
    int* screen_vertices_z,
    int base,
    int rem,
    int model_mid_z,
    int near_plane_z)
{
    if( rem <= 0 )
        return;

    int z_pad = (near_plane_z < INT_MAX) ? (near_plane_z + 1) : near_plane_z;

    int32_t tz[4] = { z_pad, z_pad, z_pad, z_pad };
    int32_t tx[4] = { 0 };
    int32_t ty[4] = { 0 };

    for( int i = 0; i < rem; ++i )
    {
        tz[i] = orthographic_vertices_z[base + i];
        tx[i] = screen_vertices_x[base + i];
        ty[i] = screen_vertices_y[base + i];
    }

    int32x4_t z_i = vld1q_s32(tz);
    int32x4_t x = vld1q_s32(tx);
    int32x4_t y = vld1q_s32(ty);

    int32x4_t midz = vdupq_n_s32(model_mid_z);
    int32x4_t outz = vsubq_s32(z_i, midz);

    uint32x4_t clipped_mask = vcltq_s32(z_i, vdupq_n_s32(near_plane_z));

    float32x4_t z_f = vcvtq_f32_s32(z_i);
    float32x4_t recip = vrecpeq_f32(z_f);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);

    float32x4_t scale = vdupq_n_f32((float)(1ll << 30));
    float32x4_t recip_scaled = vmulq_f32(recip, scale);
    int32x4_t recip_q31 = vcvtq_s32_f32(recip_scaled);

    int64x2_t xl0 = vmull_s32(vget_low_s32(x), vget_low_s32(recip_q31));
    int64x2_t xl1 = vmull_s32(vget_high_s32(x), vget_high_s32(recip_q31));
    int64x2_t yl0 = vmull_s32(vget_low_s32(y), vget_low_s32(recip_q31));
    int64x2_t yl1 = vmull_s32(vget_high_s32(y), vget_high_s32(recip_q31));

    int32x4_t x_div = vcombine_s32(vshrn_n_s64(xl0, 30), vshrn_n_s64(xl1, 30));
    int32x4_t y_div = vcombine_s32(vshrn_n_s64(yl0, 30), vshrn_n_s64(yl1, 30));

    int32x4_t neg5000 = vdupq_n_s32(-5000);
    x_div = vbslq_s32(clipped_mask, neg5000, x_div);

    vst1q_s32(tx, x_div);
    vst1q_s32(ty, y_div);
    vst1q_s32(tz, outz);

    for( int i = 0; i < rem; ++i )
    {
        screen_vertices_x[base + i] = tx[i];
        screen_vertices_y[base + i] = ty[i];
        screen_vertices_z[base + i] = tz[i];
    }
}

static void
bench_notex_tail_stack(
    int* sx,
    int* sy,
    int* sz,
    int base,
    int rem,
    int mid_z,
    int near_z)
{
    if( rem <= 0 )
        return;

    int32_t tx[4] = { 0 }, ty[4] = { 0 }, tz[4] = { 0 };

    for( int i = 0; i < rem; ++i )
    {
        tx[i] = sx[base + i];
        ty[i] = sy[base + i];
        tz[i] = sz[base + i];
    }
    for( int i = rem; i < 4; ++i )
    {
        tz[i] = near_z + 1;
    }

    int32x4_t x = vld1q_s32(tx);
    int32x4_t y = vld1q_s32(ty);
    int32x4_t z_i = vld1q_s32(tz);

    int32x4_t outz = vsubq_s32(z_i, vdupq_n_s32(mid_z));
    uint32x4_t clipped_mask = vcltq_s32(z_i, vdupq_n_s32(near_z));

    float32x4_t z_f = vcvtq_f32_s32(z_i);
    float32x4_t recip = vrecpeq_f32(z_f);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);
    recip = vmulq_f32(vrecpsq_f32(z_f, recip), recip);

    float32x4_t recip_scaled = vmulq_f32(recip, vdupq_n_f32((float)(1ll << 30)));
    int32x4_t recip_q31 = vcvtq_s32_f32(recip_scaled);

    int64x2_t xl0 = vmull_s32(vget_low_s32(x), vget_low_s32(recip_q31));
    int64x2_t xl1 = vmull_s32(vget_high_s32(x), vget_high_s32(recip_q31));
    int64x2_t yl0 = vmull_s32(vget_low_s32(y), vget_low_s32(recip_q31));
    int64x2_t yl1 = vmull_s32(vget_high_s32(y), vget_high_s32(recip_q31));

    int32x4_t x_div = vcombine_s32(vshrn_n_s64(xl0, 30), vshrn_n_s64(xl1, 30));
    int32x4_t y_div = vcombine_s32(vshrn_n_s64(yl0, 30), vshrn_n_s64(yl1, 30));

    x_div = vbslq_s32(clipped_mask, vdupq_n_s32(-5000), x_div);

    vst1q_s32(tx, x_div);
    vst1q_s32(ty, y_div);
    vst1q_s32(tz, outz);

    for( int i = 0; i < rem; ++i )
    {
        sx[base + i] = tx[i];
        sy[base + i] = ty[i];
        sz[base + i] = tz[i];
    }
}

/* --- harness ------------------------------------------------------------ */

static double
now_seconds(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0.0;
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

enum
{
    kBuf = 32,
    kBase = 8,
    kNCases = 8
};

typedef struct
{
    int mid_z;
    int near_z;
    int ortho[3];
    int ix[3];
    int iy[3];
    int iz[3];
    int rem;
} Case3;

static const Case3 g_cases[kNCases] = {
    { 100, 50, { 800, 0, 0 }, { 1200, 0, 0 }, { -3400, 0, 0 }, { 900, 0, 0 }, 1 },
    { 100, 50, { 800, 801, 0 }, { 1200, -1300, 0 }, { -3400, 2100, 0 }, { 900, 901, 0 }, 2 },
    { 100, 50, { 800, 801, 802 }, { 1200, -1300, 1 << 20 }, { -3400, 2100, -(1 << 19) }, { 900, 901, 902 }, 3 },
    { 0, 2000, { 1500, 0, 0 }, { 500, 0, 0 }, { 500, 0, 0 }, { 1500, 0, 0 }, 1 },
    { 0, 2000, { 1500, 1600, 0 }, { 500, 600, 0 }, { 500, 600, 0 }, { 1500, 1600, 0 }, 2 },
    { 0, 2000, { 1500, 1600, 1700 }, { 500, 600, 700 }, { 500, 600, 700 }, { 1500, 1600, 1700 }, 3 },
    { 50, 4000, { 100, 200, 300 }, { 1 << 18, 2 << 18, 3 << 18 }, { -(1 << 17), 0, 1 << 19 }, { 100, 200, 300 }, 3 },
    { -20, 10, { 5, 6, 7 }, { 100, 101, 102 }, { 200, 201, 202 }, { 5, 6, 7 }, 3 },
};

static int
cmp_triplet(int rem, const int* a, const int* b)
{
    return memcmp(a, b, (size_t)rem * sizeof(int)) == 0;
}

static int
run_correctness(void)
{
    int ortho[kBuf], sx[kBuf], sy[kBuf], sz[kBuf];
    int ox[kBuf], oy[kBuf], oz[kBuf];

    for( int ci = 0; ci < kNCases; ++ci )
    {
        const Case3* c = &g_cases[ci];
        int rem = c->rem;
        int b = kBase;

        memset(ortho, 0x2a, sizeof(ortho));
        memset(sx, 0x2a, sizeof(sx));
        memset(sy, 0x2a, sizeof(sy));
        memset(sz, 0x2a, sizeof(sz));

        for( int j = 0; j < rem; ++j )
        {
            ortho[b + j] = c->ortho[j];
            sx[b + j] = c->ix[j];
            sy[b + j] = c->iy[j];
            sz[b + j] = c->iz[j];
        }

        memcpy(ox, sx, sizeof(sx));
        memcpy(oy, sy, sizeof(sy));
        memcpy(oz, sz, sizeof(sz));

        bench_tex_tail_lane(ortho, sx, sy, sz, b, rem, c->mid_z, c->near_z);
        int lx[3], ly[3], lz[3];
        for( int j = 0; j < rem; ++j )
        {
            lx[j] = sx[b + j];
            ly[j] = sy[b + j];
            lz[j] = sz[b + j];
        }

        for( int j = 0; j < rem; ++j )
        {
            ortho[b + j] = c->ortho[j];
            sx[b + j] = c->ix[j];
            sy[b + j] = c->iy[j];
            sz[b + j] = c->iz[j];
        }
        bench_tex_tail_stack(ortho, sx, sy, sz, b, rem, c->mid_z, c->near_z);
        if( !cmp_triplet(rem, lx, sx + b) || !cmp_triplet(rem, ly, sy + b) || !cmp_triplet(rem, lz, sz + b) )
        {
            fprintf(stderr, "tex_tail mismatch case %d rem %d\n", ci, rem);
            return 0;
        }

        /* notex: restore screen-space inputs */
        for( int j = 0; j < rem; ++j )
        {
            sx[b + j] = ox[b + j];
            sy[b + j] = oy[b + j];
            sz[b + j] = oz[b + j];
        }
        bench_notex_tail_lane(sx, sy, sz, b, rem, c->mid_z, c->near_z);
        for( int j = 0; j < rem; ++j )
        {
            lx[j] = sx[b + j];
            ly[j] = sy[b + j];
            lz[j] = sz[b + j];
        }

        for( int j = 0; j < rem; ++j )
        {
            sx[b + j] = ox[b + j];
            sy[b + j] = oy[b + j];
            sz[b + j] = oz[b + j];
        }
        bench_notex_tail_stack(sx, sy, sz, b, rem, c->mid_z, c->near_z);
        if( !cmp_triplet(rem, lx, sx + b) || !cmp_triplet(rem, ly, sy + b) || !cmp_triplet(rem, lz, sz + b) )
        {
            fprintf(stderr, "notex_tail mismatch case %d rem %d\n", ci, rem);
            return 0;
        }
    }

    return 1;
}

typedef void (*bench_tex_fn)(
    const int*, int*, int*, int*, int, int, int, int);
typedef void (*bench_notex_fn)(int*, int*, int*, int, int, int, int);

static const Case3*
pick_case_with_rem(int rem)
{
    for( int i = 0; i < kNCases; ++i )
    {
        if( g_cases[i].rem == rem )
            return &g_cases[i];
    }
    return &g_cases[0];
}

static void
time_tex(const char* label, bench_tex_fn fn, int rem, volatile unsigned* sink_out)
{
    const Case3* c = pick_case_with_rem(rem);
    int ortho[kBuf], sx[kBuf], sy[kBuf], sz[kBuf];
    volatile unsigned sink = 0;
    int b = kBase;
    int r = c->rem;

    for( int w = 0; w < BENCH_WARMUP; ++w )
    {
        for( int j = 0; j < r; ++j )
        {
            ortho[b + j] = c->ortho[j];
            sx[b + j] = c->ix[j];
            sy[b + j] = c->iy[j];
            sz[b + j] = 0;
        }
        fn(ortho, sx, sy, sz, b, r, c->mid_z, c->near_z);
        sink ^= (unsigned)sx[b];
    }

    double t0 = now_seconds();
    for( int i = 0; i < BENCH_ITERS; ++i )
    {
        for( int j = 0; j < r; ++j )
        {
            ortho[b + j] = c->ortho[j];
            sx[b + j] = c->ix[j];
            sy[b + j] = c->iy[j];
            sz[b + j] = 0;
        }
        fn(ortho, sx, sy, sz, b, r, c->mid_z, c->near_z);
        sink ^= (unsigned)sx[b] ^ (unsigned)sy[b] ^ (unsigned)sz[b];
    }
    double t1 = now_seconds();

    *sink_out ^= sink;

    double elapsed = t1 - t0;
    long long calls = (long long)BENCH_ITERS;
    double ns = (elapsed > 0.0) ? (elapsed * 1e9) / (double)calls : 0.0;
    printf("  %-22s rem=%d  calls=%lld  %.2f ns/call\n", label, rem, calls, ns);
}

static void
time_notex(const char* label, bench_notex_fn fn, int rem, volatile unsigned* sink_out)
{
    const Case3* c = pick_case_with_rem(rem);
    int sx[kBuf], sy[kBuf], sz[kBuf];
    volatile unsigned sink = 0;
    int b = kBase;
    int r = c->rem;

    for( int w = 0; w < BENCH_WARMUP; ++w )
    {
        for( int j = 0; j < r; ++j )
        {
            sx[b + j] = c->ix[j];
            sy[b + j] = c->iy[j];
            sz[b + j] = c->ortho[j];
        }
        fn(sx, sy, sz, b, r, c->mid_z, c->near_z);
        sink ^= (unsigned)sx[b];
    }

    double t0 = now_seconds();
    for( int i = 0; i < BENCH_ITERS; ++i )
    {
        for( int j = 0; j < r; ++j )
        {
            sx[b + j] = c->ix[j];
            sy[b + j] = c->iy[j];
            sz[b + j] = c->ortho[j];
        }
        fn(sx, sy, sz, b, r, c->mid_z, c->near_z);
        sink ^= (unsigned)sx[b] ^ (unsigned)sy[b] ^ (unsigned)sz[b];
    }
    double t1 = now_seconds();

    *sink_out ^= sink;

    double elapsed = t1 - t0;
    long long calls = (long long)BENCH_ITERS;
    double ns = (elapsed > 0.0) ? (elapsed * 1e9) / (double)calls : 0.0;
    printf("  %-22s rem=%d  calls=%lld  %.2f ns/call\n", label, rem, calls, ns);
}

int main(void)
{
    if( !run_correctness() )
        return 1;

    printf("correctness: all %d cases (tex + notex) lane vs stack match\n", kNCases);
    printf(
        "timing: BENCH_ITERS=%d BENCH_WARMUP=%d (fixed case per rem; reload inputs each call)\n\n",
        BENCH_ITERS,
        BENCH_WARMUP);

    volatile unsigned g_sink = 0;

    printf("tex_tail:\n");
    for( int rem = 1; rem <= 3; ++rem )
    {
        time_tex("lane (before)", bench_tex_tail_lane, rem, &g_sink);
        time_tex("stack (after)", bench_tex_tail_stack, rem, &g_sink);
        printf("\n");
    }

    printf("notex_tail:\n");
    for( int rem = 1; rem <= 3; ++rem )
    {
        time_notex("lane (before)", bench_notex_tail_lane, rem, &g_sink);
        time_notex("stack (after)", bench_notex_tail_stack, rem, &g_sink);
        printf("\n");
    }

    (void)g_sink;
    return 0;
}

#else

int main(void)
{
    printf("bench_projection_zdiv_neon_tail: skipped (no ARM NEON in this build)\n");
    return 0;
}

#endif
