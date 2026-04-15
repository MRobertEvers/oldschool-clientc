/*
 * Benchmark raster_texture_opaque_blend_blerp8 (texture_blend_branching.u.c) vs
 * raster_texture_opaque_blend_blerp8_v3 (texture_blend_branching_v3.u.c).
 *
 * Same projection + texture_simd are included once via the first header; the v3 header adds
 * the SIMD-path raster without re-including projection/simd (include guards).
 */
#include <stdio.h>
#include <time.h>

int g_sin_table[2048];
int g_cos_table[2048];
int g_tan_table[2048];
int g_reciprocal16_simd[4096];

#define raster_texture_opaque_blend_blerp8 bench_raster_texture_opaque_blend_scalar
#include "../../src/graphics/texture_blend_branching.u.c"
#undef raster_texture_opaque_blend_blerp8

#define raster_texture_opaque_blend_blerp8_v3 bench_raster_texture_opaque_blend_simd
#include "../../src/graphics/texture_blend_branching_v3.u.c"
#undef raster_texture_opaque_blend_blerp8_v3

#ifndef BENCH_ITERS
#define BENCH_ITERS 400
#endif
#ifndef BENCH_WARMUP
#define BENCH_WARMUP 3
#endif

#ifndef MAX_SCREEN_W
#define MAX_SCREEN_W 1920
#endif
#ifndef MAX_SCREEN_H
#define MAX_SCREEN_H 1200
#endif
#ifndef MAX_TEXTURE_DIM
#define MAX_TEXTURE_DIM 128
#endif

static double
now_seconds(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0.0;
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static void
fill_lookup_stubs(void)
{
    for( int i = 0; i < 2048; i++ )
    {
        g_sin_table[i] = 0;
        g_cos_table[i] = 65536;
        g_tan_table[i] = 65536;
    }
    for( int i = 0; i < 4096; i++ )
        g_reciprocal16_simd[i] = (int)(65536.0 * 4096.0 / (double)(i + 1));
}

typedef void (*blerp8_fn)(
    int* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int camera_fov,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int orthographic_uvorigin_x0,
    int orthographic_uend_x1,
    int orthographic_vend_x2,
    int orthographic_uvorigin_y0,
    int orthographic_uend_y1,
    int orthographic_vend_y2,
    int orthographic_uvorigin_z0,
    int orthographic_uend_z1,
    int orthographic_vend_z2,
    int shade7bit_a,
    int shade7bit_b,
    int shade7bit_c,
    int* texels,
    int texture_width);

typedef struct
{
    const char* name;
    int screen_w;
    int screen_h;
    int texture_dim;
    int camera_fov;
    int x0, y0, x1, y1, x2, y2;
    int shade_a, shade_b, shade_c;
    /* Default orthographic UV cube corners; some tests override via notes only — kept
     * identical to typical 128 atlas mapping unless we add fields later. */
} bench_case_t;

static double
time_raster(
    blerp8_fn fn,
    int* pixels,
    int screen_w,
    int screen_h,
    int texture_dim,
    int camera_fov,
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    int shade_a,
    int shade_b,
    int shade_c,
    int* texels)
{
    int stride = screen_w;
    for( int w = 0; w < BENCH_WARMUP; w++ )
        fn(pixels,
           stride,
           screen_w,
           screen_h,
           camera_fov,
           x0,
           x1,
           x2,
           y0,
           y1,
           y2,
           0,
           texture_dim,
           0,
           0,
           0,
           texture_dim,
           0,
           0,
           texture_dim,
           shade_a,
           shade_b,
           shade_c,
           texels,
           texture_dim);

    double t0 = now_seconds();
    for( int n = 0; n < BENCH_ITERS; n++ )
        fn(pixels,
           stride,
           screen_w,
           screen_h,
           camera_fov,
           x0,
           x1,
           x2,
           y0,
           y1,
           y2,
           0,
           texture_dim,
           0,
           0,
           0,
           texture_dim,
           0,
           0,
           texture_dim,
           shade_a,
           shade_b,
           shade_c,
           texels,
           texture_dim);
    double t1 = now_seconds();

    volatile int sink = pixels[(screen_h / 2) * stride + (screen_w / 2)];
    (void)sink;
    return t1 - t0;
}

int
main(void)
{
    static int pixels[MAX_SCREEN_W * MAX_SCREEN_H];
    static int texels[MAX_TEXTURE_DIM * MAX_TEXTURE_DIM];

    fill_lookup_stubs();

    const bench_case_t cases[] = {
        /* Large triangles, common resolution */
        { "large_flat_base_1024x768",
         1024,                                768,
         128,                                            256,
         100,                                                      120,
         900,                                                                120,
         512,                                                                          620,
         64,                                                                                      64,
         64                                                                                                },
        { "large_steep_right_1024x768",
         1024,                                768,
         128,                                            256,
         50,                                                       50,
         980,                                                                50,
         200,                                                                          720,
         80,                                                                                      72,
         96                                                                                                },
        { "near_fullscreen_1920x1080",
         1920,                                1080,
         128,                                            384,
         80,                                                       80,
         1840,                                                               80,
         960,                                                                          1000,
         64,                                                                                      64,
         64                                                                                                },
        /* Small / medium raster cost */
        { "tiny_bbox_320x240",          320,  240,  128, 256, 140, 100, 180, 100, 160, 130,  64,  64,  64  },
        { "shallow_wide_band_1280x720",
         1280,                                720,
         128,                                            256,
         40,                                                       300,
         1240,                                                               300,
         640,                                                                          380,
         96,                                                                                      96,
         96                                                                                                },
        { "tall_narrow_800x1200",       800,  1200, 128, 256, 380, 40,  420, 40,  400, 1180, 48,  48,  48  },
        /* Texture 64 (different shift/mask in SIMD) */
        { "tex64_large_1024x768",       1024, 768,  64,  256, 100, 100, 924, 100, 512, 700,  64,  64,  64  },
        /* Camera FOV */
        { "fov_narrow_128",             1024, 768,  128, 128, 200, 150, 824, 150, 512, 650,  64,  64,  64  },
        { "fov_wide_640",               1024, 768,  128, 640, 120, 140, 904, 140, 512, 600,  64,  64,  64  },
        /* Shade extremes */
        { "shade_dark_edges",           1024, 768,  128, 256, 50,  50,  974, 50,  512, 718,  8,   120, 8   },
        { "shade_bright",               1024, 768,  128, 256, 100, 100, 924, 100, 512, 680,  120, 120, 120 },
        /* Vertex order / winding: same geometry, different (xi,yi) assignments
         * (y0,y1,y2 ordering) to exercise all six branches inside blerp8. */
        { "order_y_asc_top_bl_br",      1024, 768,  128, 256, 512, 80,  120, 660, 904, 660,  64,  64,  64  },
        { "order_bl_br_top",            1024, 768,  128, 256, 120, 660, 904, 660, 512, 80,   64,  64,  64  },
        { "order_br_bl_top",            1024, 768,  128, 256, 904, 660, 120, 660, 512, 80,   64,  64,  64  },
        { "order_top_br_bl",            1024, 768,  128, 256, 512, 80,  904, 660, 120, 660,  64,  64,  64  },
        { "order_bl_top_br",            1024, 768,  128, 256, 120, 660, 512, 80,  904, 660,  64,  64,  64  },
        { "order_br_top_bl",            1024, 768,  128, 256, 904, 660, 512, 80,  120, 660,  64,  64,  64  },
    };

    const int n_cases = (int)(sizeof(cases) / sizeof(cases[0]));

    printf(
        "raster_texture_opaque_blend_blerp8 vs raster_texture_opaque_blend_blerp8_v3\n"
        "iters=%d  warmup=%d\n\n",
        BENCH_ITERS,
        BENCH_WARMUP);

    for( int c = 0; c < n_cases; c++ )
    {
        const bench_case_t* bc = &cases[c];
        if( bc->screen_w > MAX_SCREEN_W || bc->screen_h > MAX_SCREEN_H ||
            bc->texture_dim > MAX_TEXTURE_DIM )
        {
            fprintf(
                stderr,
                "skip %s: screen %dx%d or tex %d exceeds compile-time max\n",
                bc->name,
                bc->screen_w,
                bc->screen_h,
                bc->texture_dim);
            continue;
        }

        for( int i = 0; i < bc->texture_dim * bc->texture_dim; i++ )
            texels[i] = 0x00ab00ff | ((i & 0xff) << 16);

        double ts = time_raster(
            bench_raster_texture_opaque_blend_scalar,
            pixels,
            bc->screen_w,
            bc->screen_h,
            bc->texture_dim,
            bc->camera_fov,
            bc->x0,
            bc->y0,
            bc->x1,
            bc->y1,
            bc->x2,
            bc->y2,
            bc->shade_a,
            bc->shade_b,
            bc->shade_c,
            texels);

        double tv = time_raster(
            bench_raster_texture_opaque_blend_simd,
            pixels,
            bc->screen_w,
            bc->screen_h,
            bc->texture_dim,
            bc->camera_fov,
            bc->x0,
            bc->y0,
            bc->x1,
            bc->y1,
            bc->x2,
            bc->y2,
            bc->shade_a,
            bc->shade_b,
            bc->shade_c,
            texels);

        double ms_s = (ts * 1e3) / (double)BENCH_ITERS;
        double ms_v = (tv * 1e3) / (double)BENCH_ITERS;
        double speedup = tv > 0.0 ? ts / tv : 0.0;

        printf(
            "%s  (%dx%d tex=%d fov=%d)\n"
            "  raster_texture_opaque_blend_blerp8:    %6.4f s total  %6.3f ms/call\n"
            "  raster_texture_opaque_blend_blerp8_v3: %6.4f s total  %6.3f ms/call  (SIMD ~%.2f× "
            "faster)\n\n",
            bc->name,
            bc->screen_w,
            bc->screen_h,
            bc->texture_dim,
            bc->camera_fov,
            ts,
            ms_s,
            tv,
            ms_v,
            speedup);
    }

    return 0;
}
