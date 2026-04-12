/* Benchmark harness for painter_bucket vs painter_world3d. */
#include "painter_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BENCH_ITERS
#define BENCH_ITERS 100
#endif

static double
now_seconds(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0.0;
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static const PainterScenerySpec k_heap_default_scenery[] = {
    { 1, 1, 3, 2, 0 },
    { 2, 1, 2, 3, 0 },
    { 7, 3, 4, 4, 0 },
    { 7, 7, 2, 2, 1 },
};

static const PainterScenerySpec k_w3d_default_scenery[] = {
    { 1, 1, 3, 2, 0 },
    { 2, 1, 2, 3, 0 },
    { 6, 4, 4, 4, 0 },
    { 7, 7, 2, 2, 1 },
};

typedef enum
{
    SC_NONE = 0,
    SC_DEFAULT_HEAP,
    SC_DEFAULT_W3D,
    SC_RANDOM20,
} SceneryPreset;

typedef enum
{
    EYE_CENTER = 0,
    EYE_CORNER,
    EYE_EDGE,
} EyePreset;

static void
fill_eye(
    PainterConfig* cfg,
    int gs,
    EyePreset eye)
{
    switch( eye )
    {
    case EYE_CENTER:
        cfg->eye_x = (double)(gs / 2);
        cfg->eye_z = (double)(gs / 2);
        break;
    case EYE_CORNER:
        cfg->eye_x = 0.0;
        cfg->eye_z = 0.0;
        break;
    case EYE_EDGE:
        cfg->eye_x = 0.0;
        cfg->eye_z = (double)(gs / 2);
        break;
    }
}

static uint32_t
lcg_next(uint32_t* s)
{
    *s = *s * 1103515245u + 12345u;
    return *s;
}

static void
gen_random_scenery(
    PainterScenerySpec* out,
    int n,
    int gs,
    int levels,
    uint32_t seed)
{
    for( int i = 0; i < n; i++ )
    {
        int ox = (int)(lcg_next(&seed) % (unsigned)gs);
        int oz = (int)(lcg_next(&seed) % (unsigned)gs);
        int ww = 1 + (int)(lcg_next(&seed) % 4u);
        int hh = 1 + (int)(lcg_next(&seed) % 4u);
        if( ox + ww > gs )
            ww = gs - ox;
        if( oz + hh > gs )
            hh = gs - oz;
        if( ww < 1 )
            ww = 1;
        if( hh < 1 )
            hh = 1;
        int lev = levels > 1 ? (int)(lcg_next(&seed) % (unsigned)levels) : 0;
        out[i].ox = ox;
        out[i].oz = oz;
        out[i].w = ww;
        out[i].h = hh;
        out[i].level = lev;
    }
}

static int
radius_for_grid(int gs)
{
    if( gs <= 15 )
        return gs / 2 > 0 ? gs / 2 : 1;
    return 25 > gs - 1 ? gs - 1 : 25;
}

int
main(void)
{
    const int grids[] = { 51, 104 };
    const double yaws[] = { 0.0, 45.0, 180.0, 360.0 };
    const double fov = 90.0;
    const int levels_opts[] = { 2, 4 };

    printf(
        "%-7s %4s %4s %6s %6s %-8s %-10s %6s %7s %10s %12s %12s\n",
        "algo",
        "grid",
        "lvls",
        "fov",
        "yaw",
        "eye",
        "scenery",
        "sc_ct",
        "radius",
        "iters",
        "time_s",
        "ns/iter");

    for( size_t gi = 0; gi < sizeof(grids) / sizeof(grids[0]); gi++ )
    {
        int gs = grids[gi];
        PainterScenerySpec random_sc[20];

        for( size_t li = 0; li < sizeof(levels_opts) / sizeof(levels_opts[0]); li++ )
        {
            int levels = levels_opts[li];
            gen_random_scenery(random_sc, 20, gs, levels, 0xC0FFEEu ^ (uint32_t)(gs * 31 + levels));

            for( size_t yi = 0; yi < sizeof(yaws) / sizeof(yaws[0]); yi++ )
            {
                double yaw = yaws[yi];

                for( int eye = 0; eye < 3; eye++ )
                {
                    for( int sp = 0; sp < 4; sp++ )
                    {
                        PainterConfig cfg;
                        memset(&cfg, 0, sizeof(cfg));
                        cfg.grid_size = gs;
                        cfg.levels = levels;
                        cfg.yaw_deg = yaw;
                        cfg.fov_deg = fov;
                        cfg.radius = radius_for_grid(gs);
                        fill_eye(&cfg, gs, (EyePreset)eye);

                        const PainterScenerySpec* sc_ptr = NULL;
                        int sc_count = 0;
                        const char* sc_name = "?";

                        switch( sp )
                        {
                        case SC_NONE:
                            sc_ptr = NULL;
                            sc_count = 0;
                            sc_name = "none";
                            break;
                        case SC_DEFAULT_HEAP:
                            sc_ptr = k_heap_default_scenery;
                            sc_count = (int)(sizeof(k_heap_default_scenery) /
                                             sizeof(k_heap_default_scenery[0]));
                            sc_name = "def_heap";
                            /* Skip if grid too small for hardcoded coords */
                            if( gs < 11 )
                                continue;
                            break;
                        case SC_DEFAULT_W3D:
                            sc_ptr = k_w3d_default_scenery;
                            sc_count = (int)(sizeof(k_w3d_default_scenery) /
                                             sizeof(k_w3d_default_scenery[0]));
                            sc_name = "def_w3d";
                            if( gs < 11 )
                                continue;
                            break;
                        case SC_RANDOM20:
                            sc_ptr = random_sc;
                            sc_count = 20;
                            sc_name = "rand20";
                            break;
                        }

                        cfg.scenery = sc_ptr;
                        cfg.scenery_count = sc_count;

                        const char* eye_name =
                            eye == EYE_CENTER ? "center" : (eye == EYE_CORNER ? "corner" : "edge");

                        void* wh = painter_bucket_alloc(&cfg);
                        void* ww = painter_world3d_alloc(&cfg);
                        if( !wh || !ww )
                        {
                            fprintf(stderr, "alloc failed gs=%d levels=%d\n", gs, levels);
                            painter_bucket_free(wh);
                            painter_world3d_free(ww);
                            continue;
                        }

                        /* Warmup */
                        painter_bucket_run(wh, &cfg);
                        painter_world3d_run(ww, &cfg);

                        double t0 = now_seconds();
                        volatile int sink = 0;
                        for( int it = 0; it < BENCH_ITERS; it++ )
                        {
                            painter_bucket_run(wh, &cfg);
                            sink ^= (int)(size_t)wh;
                        }
                        double t1 = now_seconds();
                        double elapsed_h = t1 - t0;
                        double ns_h =
                            elapsed_h > 0.0 ? (elapsed_h * 1e9) / (double)BENCH_ITERS : 0.0;

                        t0 = now_seconds();
                        for( int it = 0; it < BENCH_ITERS; it++ )
                        {
                            painter_world3d_run(ww, &cfg);
                            sink ^= (int)(size_t)ww;
                        }
                        t1 = now_seconds();
                        double elapsed_w = t1 - t0;
                        double ns_w =
                            elapsed_w > 0.0 ? (elapsed_w * 1e9) / (double)BENCH_ITERS : 0.0;

                        (void)sink;

                        const char* name_bucket = (ns_h < ns_w) ? "bucket*" : "bucket";
                        const char* name_w3d = (ns_w < ns_h) ? "w3d*" : "w3d";

                        printf(
                            "%-7s %4d %4d %6.0f %6.0f %-8s %-10s %6d %7d %10d %12.6f %12.1f\n",
                            name_bucket,
                            gs,
                            levels,
                            fov,
                            yaw,
                            eye_name,
                            sc_name,
                            sc_count,
                            cfg.radius,
                            BENCH_ITERS,
                            elapsed_h,
                            ns_h);
                        printf(
                            "%-7s %4d %4d %6.0f %6.0f %-8s %-10s %6d %7d %10d %12.6f %12.1f\n",
                            name_w3d,
                            gs,
                            levels,
                            fov,
                            yaw,
                            eye_name,
                            sc_name,
                            sc_count,
                            cfg.radius,
                            BENCH_ITERS,
                            elapsed_w,
                            ns_w);

                        painter_bucket_free(wh);
                        painter_world3d_free(ww);
                    }
                }
            }
        }
    }

    return 0;
}
