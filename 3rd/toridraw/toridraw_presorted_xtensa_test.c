/*
 * tri.flat.rgb565.xtensa.S on the part it was written for.
 *
 * Two jobs, in this order:
 *
 *   1. CORRECTNESS. The two run doors against raster_flat_screen_*_branching_s4
 *      built for RGB565, framebuffer against framebuffer, every pixel
 *      identical -- the same claim toridraw_presorted_neon_test.c makes for
 *      the AArch64 lane, with its eight generators and its guard bands.
 *
 *   2. THE EDGE-SLOPE ROUTE. The kernel divides exactly with QUOS. The
 *      alternative every other lane takes is a float reciprocal, which is
 *      approximate and therefore changes pixels. This measures both, in
 *      isolation and in the kernel, so the choice is made on numbers.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_cpu.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TORIDRAW_FLAT_TRI_XTENSA_ASM 1

// clang-format off
#include "graphics/shared_tables.h"
#include "graphics/shared_tables.c"
#include "graphics/clamp.h"
#include "impl/raster/flat/raster.flat.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/flat/raster.flat.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "graphics/raster/flat/flat_tri_asm.h"
// clang-format on

#if !defined(TORIDRAW_FLAT_PRESORTED_RUN)
#error "the Xtensa flat arm did not arm -- check the format gate in flat_tri_asm.h"
#endif

/* Not a multiple of 4, 8 or 16: the fill blocks from x_start and aligns
 * itself, and a width that lined up with the block would hide a phase error
 * at the right edge. STRIDE > W for the same reason as the other tests --
 * pitch and clip width are two numbers and a kernel that confused them must
 * not score as correct. */
#define W 137
#define H 91
#define STRIDE 149
#define PIXELS (STRIDE * H)
#define GUARD 512
#define ALLOC (PIXELS + 2 * GUARD)

#define BATCH_MAX 64
#define ROW_INTS TORIDRAW_FLAT_PRESORTED_RUN_ROW_INTS

static toripixel_t g_fb_c[ALLOC];
static toripixel_t g_fb_a[ALLOC];
static _Alignas(16) int g_rows[BATCH_MAX * ROW_INTS];

struct tri
{
    int x[3];
    int y[3];
    int c;
    int alpha;
};

static unsigned g_seed = 20260828u;

static unsigned
next(void)
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}

static int
range(int lo, int hi)
{
    return lo + (int)(next() % (unsigned)(hi - lo + 1));
}

/* The eight generators toridraw_presorted_neon_test.c cycles, verbatim: a
 * uniform random triangle is almost always interior and never reaches the
 * degenerate, flat-topped, sliver, wide, full-width and far-offscreen cases
 * where a hand-written walk actually breaks. */
static void
generate(struct tri* t, int i)
{
    int k = i % 8;
    int j;

    for( j = 0; j < 3; j++ )
    {
        switch( k )
        {
        case 0: /* interior */
            t->x[j] = range(0, W - 1);
            t->y[j] = range(0, H - 1);
            break;
        case 1: /* straddling every edge */
            t->x[j] = range(-W, 2 * W);
            t->y[j] = range(-H, 2 * H);
            break;
        case 2: /* slivers */
            t->x[j] = range(40, 44);
            t->y[j] = range(0, H - 1);
            break;
        case 3: /* flat-topped and flat-bottomed */
            t->x[j] = range(0, W - 1);
            t->y[j] = (j < 2) ? 20 : range(0, H - 1);
            break;
        case 4: /* far off-viewport */
            t->x[j] = range(-4000, 4000);
            t->y[j] = range(-4000, 4000);
            break;
        case 5: /* tiny */
            t->x[j] = range(60, 63);
            t->y[j] = range(40, 43);
            break;
        case 6: /* wide */
            t->x[j] = (j == 0) ? 2 : ((j == 1) ? W - 3 : range(0, W - 1));
            t->y[j] = range(0, H - 1);
            break;
        default: /* full width, every lead-in phase */
            t->x[j] = (j == 0) ? -20 : ((j == 1) ? W + 20 : range(-20, W + 20));
            t->y[j] = range(-2, H + 2);
            break;
        }
    }
    t->c = range(0, 65535);
    /* 0 and 255 are the ends the blend degenerates at; the rest is spread. */
    t->alpha = (i % 17 == 0) ? 0 : ((i % 19 == 0) ? 255 : range(1, 254));
}

/* The y order the depth sort hands the batch, `<=` tie-breaks included. */
static int
ysort_perm(const int* y)
{
    if( y[0] <= y[1] && y[0] <= y[2] )
        return (y[1] <= y[2]) ? 0 : 1;
    if( y[1] <= y[2] )
        return (y[2] <= y[0]) ? 2 : 3;
    return (y[0] <= y[1]) ? 4 : 5;
}

static const unsigned char g_ysort[6][3] = {
    { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 }, { 1, 0, 2 }, { 2, 0, 1 }, { 2, 1, 0 }
};

static void
stage(int* rows, int i, const struct tri* t)
{
    const unsigned char* pm = g_ysort[ysort_perm(t->y)];
    int* row = rows + i * ROW_INTS;
    int k;

    for( k = 0; k < 3; k++ )
    {
        row[0 + k] = t->x[pm[k]];
        row[4 + k] = t->y[pm[k]];
    }
    row[8] = t->c;
    row[9] = t->alpha;
}

static int
compare(const toripixel_t* a, const toripixel_t* b, const struct tri* t, int idx,
        const char* door)
{
    int i;

    for( i = 0; i < ALLOC; i++ )
    {
        if( a[i] != b[i] )
        {
            int p = i - GUARD;
            if( p < 0 || p >= PIXELS )
                printf("MISMATCH [%s] tri %d OUTSIDE the framebuffer, %d pixels %s the "
                       "picture: c=0x%04X asm=0x%04X\n",
                       door, idx, (p < 0) ? -p : p - PIXELS + 1,
                       (p < 0) ? "before" : "past", a[i], b[i]);
            else
                printf("MISMATCH [%s] tri %d at (%d,%d)%s: c=0x%04X asm=0x%04X\n",
                       door, idx, p % STRIDE, p / STRIDE,
                       (p % STRIDE >= W) ? " -- PAST THE VIEWPORT, in the stride padding"
                                         : "",
                       a[i], b[i]);
            printf("  screen (%d,%d) (%d,%d) (%d,%d)  colour %d  alpha %d\n", t->x[0],
                   t->y[0], t->x[1], t->y[1], t->x[2], t->y[2], t->c, t->alpha);
            return 1;
        }
    }
    return 0;
}

static long
count_drawn(const toripixel_t* fb, toripixel_t hint)
{
    long n = 0;
    int p;
    for( p = 0; p < PIXELS; p++ )
        if( fb[GUARD + p] != hint )
            n++;
    return n;
}

/* ------------------------------------------------------------------ tests */

static int
run_correctness(int alpha_door, int tris, long* out_pixels)
{
    const toripixel_t hint = 0x0841;
    int failures = 0;
    long drawn = 0;
    int i;

    for( i = 0; i < tris; i += BATCH_MAX )
    {
        int n = (tris - i < BATCH_MAX) ? (tris - i) : BATCH_MAX;
        struct tri batch[BATCH_MAX];
        int k;

        for( k = 0; k < ALLOC; k++ )
        {
            g_fb_c[k] = hint;
            g_fb_a[k] = hint;
        }

        for( k = 0; k < n; k++ )
        {
            generate(&batch[k], i + k);
            stage(g_rows, k, &batch[k]);
        }

        /* The C reference draws them one at a time, in the same order, so the
         * comparison scores the draw order across a run as well as the fill. */
        for( k = 0; k < n; k++ )
        {
            const struct tri* t = &batch[k];
            if( alpha_door )
                raster_flat_screen_alpha_branching_s4(g_fb_c + GUARD, STRIDE, W, H, t->x[0],
                                                      t->x[1], t->x[2], t->y[0], t->y[1],
                                                      t->y[2], t->c, t->alpha);
            else
                raster_flat_screen_opaque_branching_s4(g_fb_c + GUARD, STRIDE, W, H,
                                                       t->x[0], t->x[1], t->x[2], t->y[0],
                                                       t->y[1], t->y[2], t->c);
        }

        if( alpha_door )
            TORIDRAW_FLAT_PRESORTED_RUN_ALPHA(g_fb_a + GUARD, STRIDE, W, H, g_rows, n);
        else
            TORIDRAW_FLAT_PRESORTED_RUN_OPAQUE(g_fb_a + GUARD, STRIDE, W, H, g_rows, n);

        drawn += count_drawn(g_fb_c, hint);

        for( k = 0; k < n && failures < 8; k++ )
        {
            if( compare(g_fb_c, g_fb_a, &batch[k], i + k,
                        alpha_door ? "flat alpha" : "flat opaque") )
            {
                failures++;
                break;
            }
        }
        if( failures >= 8 )
            break;
    }

    *out_pixels = drawn;
    return failures;
}

/* -------------------------------------------------------------- benchmarks */

/*
 * A batch of triangles that look like a model's faces rather than like the
 * correctness generators: on screen, a few hundred pixels each. A benchmark
 * built from the adversarial set would spend its time in the cull.
 */
static void
build_bench_batch(int n)
{
    int i;
    g_seed = 99001u;
    for( i = 0; i < n; i++ )
    {
        struct tri t;
        int cx = range(10, W - 30);
        int cy = range(6, H - 24);
        int j;
        for( j = 0; j < 3; j++ )
        {
            t.x[j] = cx + range(0, 22);
            t.y[j] = cy + range(0, 18);
        }
        t.c = range(0, 65535);
        t.alpha = 128;
        stage(g_rows, i, &t);
    }
}

/*
 * The other end of the span-width distribution: a face that covers most of
 * the panel. A model's faces are small and the vector fill never sees them;
 * a floor quad, a background plate or a scaled-up icon is where a wide store
 * has anything to do.
 */
static void
build_bench_batch_wide(int n)
{
    int i;
    g_seed = 4242u;
    for( i = 0; i < n; i++ )
    {
        struct tri t;
        t.x[0] = range(0, 6);
        t.x[1] = range(W - 8, W - 1);
        t.x[2] = range(0, W - 1);
        t.y[0] = range(0, 4);
        t.y[1] = range(H / 2 - 4, H / 2 + 4);
        t.y[2] = range(H - 6, H - 1);
        t.c = range(0, 65535);
        t.alpha = 128;
        stage(g_rows, i, &t);
    }
}

static uint32_t
bench_asm(int alpha_door, int n, int reps)
{
    uint32_t t0, t1;
    int r;

    /* warm the caches: the first pass pulls the kernel out of flash. */
    if( alpha_door )
        TORIDRAW_FLAT_PRESORTED_RUN_ALPHA(g_fb_a + GUARD, STRIDE, W, H, g_rows, n);
    else
        TORIDRAW_FLAT_PRESORTED_RUN_OPAQUE(g_fb_a + GUARD, STRIDE, W, H, g_rows, n);

    t0 = esp_cpu_get_cycle_count();
    for( r = 0; r < reps; r++ )
    {
        if( alpha_door )
            TORIDRAW_FLAT_PRESORTED_RUN_ALPHA(g_fb_a + GUARD, STRIDE, W, H, g_rows, n);
        else
            TORIDRAW_FLAT_PRESORTED_RUN_OPAQUE(g_fb_a + GUARD, STRIDE, W, H, g_rows, n);
    }
    t1 = esp_cpu_get_cycle_count();
    return t1 - t0;
}

static uint32_t
bench_c(int alpha_door, int n, int reps)
{
    uint32_t t0, t1;
    int r, k;

    for( k = 0; k < n; k++ )
    {
        const int* row = g_rows + k * ROW_INTS;
        if( alpha_door )
            raster_flat_screen_alpha_branching_s4(g_fb_c + GUARD, STRIDE, W, H, row[0],
                                                  row[1], row[2], row[4], row[5], row[6],
                                                  row[8], row[9]);
        else
            raster_flat_screen_opaque_branching_s4(g_fb_c + GUARD, STRIDE, W, H, row[0],
                                                   row[1], row[2], row[4], row[5], row[6],
                                                   row[8]);
    }

    t0 = esp_cpu_get_cycle_count();
    for( r = 0; r < reps; r++ )
    {
        for( k = 0; k < n; k++ )
        {
            const int* row = g_rows + k * ROW_INTS;
            if( alpha_door )
                raster_flat_screen_alpha_branching_s4(g_fb_c + GUARD, STRIDE, W, H, row[0],
                                                      row[1], row[2], row[4], row[5],
                                                      row[6], row[8], row[9]);
            else
                raster_flat_screen_opaque_branching_s4(g_fb_c + GUARD, STRIDE, W, H,
                                                       row[0], row[1], row[2], row[4],
                                                       row[5], row[6], row[8]);
        }
    }
    t1 = esp_cpu_get_cycle_count();
    return t1 - t0;
}

/* ---------------------------------------------- the edge-slope route, alone */

/*
 * Three slopes per triangle, the two ways.
 *
 * `sink` is what keeps the compiler from deleting the work; the inputs come
 * out of the staged batch so both routes see the same divisors, including the
 * small ones a real model produces.
 */
static volatile int g_sink;

static uint32_t
bench_slopes_quos(const int* dx, const int* dy, int n, int reps)
{
    uint32_t t0, t1;
    int r, i, acc = 0;

    t0 = esp_cpu_get_cycle_count();
    for( r = 0; r < reps; r++ )
        for( i = 0; i < n; i++ )
            acc += (dx[i] << 16) / dy[i];
    t1 = esp_cpu_get_cycle_count();
    g_sink = acc;
    return t1 - t0;
}

static uint32_t
bench_slopes_float(const int* dx, const int* dy, int n, int reps)
{
    uint32_t t0, t1;
    int r, i, acc = 0;

    t0 = esp_cpu_get_cycle_count();
    for( r = 0; r < reps; r++ )
        for( i = 0; i < n; i++ )
            acc += (int)((float)(dx[i] << 16) * (1.0f / (float)dy[i]));
    t1 = esp_cpu_get_cycle_count();
    g_sink = acc;
    return t1 - t0;
}

/*
 * How often the float route would even give the same answer. A differing
 * slope is a differing edge, which is differing pixels -- so this is the
 * price of the route, not a curiosity.
 */
static void
slope_exactness(const int* dx, const int* dy, int n)
{
    int differ = 0;
    int worst = 0;
    int i;

    for( i = 0; i < n; i++ )
    {
        int exact = (dx[i] << 16) / dy[i];
        int approx = (int)((float)(dx[i] << 16) * (1.0f / (float)dy[i]));
        int d = exact - approx;
        if( d < 0 )
            d = -d;
        if( d )
            differ++;
        if( d > worst )
            worst = d;
    }
    printf("  float vs exact: %d of %d slopes differ, worst |delta| = %d (16.16 units)\n",
           differ, n, worst);
}

/* --------------------------------------------------------------------- app */

#define SLOPE_N 512

void
app_main(void)
{
    static int dx[SLOPE_N];
    static int dy[SLOPE_N];
    long pixels_o = 0, pixels_a = 0;
    int fail_o, fail_a;
    uint32_t cyc;
    const int TRIS = 20000;
    const int BN = 64;
    const int REPS = 200;
    int i;

    printf("\n=== tri.flat.rgb565.xtensa.S on ESP32-S3 @ %d MHz ===\n",
           CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    printf("format: %s\n", TORIPIXEL_FORMAT_NAME);
#if defined(TORIDRAW_XTENSA_NO_PIE)
    printf("fill: scalar 32-bit stores (TORIDRAW_XTENSA_NO_PIE)\n");
#else
    printf("fill: PIE 128-bit vector stores\n");
#endif

    init_hsl16_to_pixel_table();

    printf("\n-- correctness, %d triangles per door --\n", TRIS);
    fail_o = run_correctness(0, TRIS, &pixels_o);
    printf("flat opaque: %s  (%ld pixels drawn)\n", fail_o ? "FAIL" : "bit-identical",
           pixels_o);
    fail_a = run_correctness(1, TRIS, &pixels_a);
    printf("flat alpha : %s  (%ld pixels drawn)\n", fail_a ? "FAIL" : "bit-identical",
           pixels_a);

    if( fail_o || fail_a )
    {
        printf("\nRESULT: FAIL\n");
        return;
    }

    printf("\n-- whole kernel, %d triangles x %d reps --\n", BN, REPS);
    build_bench_batch(BN);

    cyc = bench_c(0, BN, REPS);
    printf("opaque  C reference : %10u cycles  (%6.1f per triangle)\n", (unsigned)cyc,
           (double)cyc / (BN * REPS));
    cyc = bench_asm(0, BN, REPS);
    printf("opaque  xtensa asm  : %10u cycles  (%6.1f per triangle)\n", (unsigned)cyc,
           (double)cyc / (BN * REPS));

    cyc = bench_c(1, BN, REPS);
    printf("alpha   C reference : %10u cycles  (%6.1f per triangle)\n", (unsigned)cyc,
           (double)cyc / (BN * REPS));
    cyc = bench_asm(1, BN, REPS);
    printf("alpha   xtensa asm  : %10u cycles  (%6.1f per triangle)\n", (unsigned)cyc,
           (double)cyc / (BN * REPS));

    printf("\n-- wide faces (spans across the panel), %d triangles x %d reps --\n", BN,
           REPS);
    build_bench_batch_wide(BN);
    cyc = bench_c(0, BN, REPS);
    printf("opaque  C reference : %10u cycles  (%6.1f per triangle)\n", (unsigned)cyc,
           (double)cyc / (BN * REPS));
    cyc = bench_asm(0, BN, REPS);
    printf("opaque  xtensa asm  : %10u cycles  (%6.1f per triangle)\n", (unsigned)cyc,
           (double)cyc / (BN * REPS));
    cyc = bench_c(1, BN, REPS);
    printf("alpha   C reference : %10u cycles  (%6.1f per triangle)\n", (unsigned)cyc,
           (double)cyc / (BN * REPS));
    cyc = bench_asm(1, BN, REPS);
    printf("alpha   xtensa asm  : %10u cycles  (%6.1f per triangle)\n", (unsigned)cyc,
           (double)cyc / (BN * REPS));

    /*
     * Two bands, because they answer different halves of the question. The
     * on-screen band is what a model's faces actually produce; the wide band
     * is what a triangle straddling the viewport produces, and it is where a
     * 24-bit mantissa stops holding a 16.16 quotient.
     */
    for( int band = 0; band < 2; band++ )
    {
        printf("\n-- the edge-slope route, %s, %d divides x %d reps --\n",
               band ? "clipped/far (dy 1..4000)" : "on-screen (dy 1..90)", SLOPE_N, REPS);
        g_seed = 7331u;
        for( i = 0; i < SLOPE_N; i++ )
        {
            dx[i] = band ? range(-8000, 8000) : range(-200, 200);
            dy[i] = band ? range(1, 4000) : range(1, 90);
        }
        cyc = bench_slopes_quos(dx, dy, SLOPE_N, REPS);
        printf("quos (exact)        : %10u cycles  (%5.2f per divide)\n", (unsigned)cyc,
               (double)cyc / ((double)SLOPE_N * REPS));
        cyc = bench_slopes_float(dx, dy, SLOPE_N, REPS);
        printf("float reciprocal    : %10u cycles  (%5.2f per divide)\n", (unsigned)cyc,
               (double)cyc / ((double)SLOPE_N * REPS));
        slope_exactness(dx, dy, SLOPE_N);
    }

    printf("\nRESULT: PASS\n");
}
