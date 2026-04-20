/* Times raster_texshadeflat_persp_textrans_sort_lerp8_scanline (scalar; includes texture.u.c). */
#include <time.h>

int g_sin_table[2048];
int g_cos_table[2048];
int g_tan_table[2048];
int g_reciprocal16_simd[4096];

#include "../../src/graphics/old/texture.u.c"

#ifndef BENCH_ITERS
#define BENCH_ITERS 5000
#endif

static double
now_seconds(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0.0;
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

int main(void)
{
    enum
    {
        screen_width = 8192,
        texture_width = 128,
    };
    static int pixel_row[screen_width];
    static int texels[texture_width * texture_width];

    for( int i = 0; i < texture_width * texture_width; i++ )
        texels[i] = 0x00ab00ff;

    int au = 32768;
    int bv = 32768;
    int cw = -65536;
    int step_au_dx = 0;
    int step_bv_dx = 0;
    int step_cw_dx = 0;
    int shade8bit_ish8 = 128 << 8;

    int screen_x0 = 0;
    int screen_x1 = 8191;
    int pixel_offset = 0;

    double t0 = now_seconds();
    for( int n = 0; n < BENCH_ITERS; n++ )
    {
        raster_texshadeflat_persp_textrans_sort_lerp8_scanline(
            pixel_row,
            screen_width,
            screen_width,
            1,
            screen_x0,
            screen_x1,
            pixel_offset,
            au,
            bv,
            cw,
            step_au_dx,
            step_bv_dx,
            step_cw_dx,
            shade8bit_ish8,
            texels,
            texture_width);
    }
    double t1 = now_seconds();

    double elapsed = t1 - t0;
    double ns_per = elapsed > 0.0 ? (elapsed * 1e9) / (double)BENCH_ITERS : 0.0;

    volatile int sink = pixel_row[screen_width / 2];
    (void)sink;

    printf(
        "texture_scanline transparent_sort_lerp8: iters=%d  time=%.6f s  %.1f ns/iter\n",
        BENCH_ITERS,
        elapsed,
        ns_per);
    return 0;
}
