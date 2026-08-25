/*
 * gouraud_tri_i686.S against its C reference, timed.
 *
 * Correctness is toridraw_gouraud_tri_asm_test.c's job; this one only answers
 * "is it faster", and only for the win32/pentium4 lane, which is the lane the
 * XP box runs. It is a micro-benchmark and it is NOT the acceptance instrument:
 * the ladder on the P4 is, because local and XP rankings have repeatedly failed
 * to transfer (the gouraud fill is 4.7% of frame here and does not resolve at
 * all there). What this is for is catching a kernel that is slower before
 * spending twenty minutes of XP wall clock on it.
 *
 * The corpus is built ONCE and reused, so both arms see identical work and an
 * identical cache state. The order is a palindrome -- C, asm, asm, C -- because
 * a fixed order plus any monotone drift in the machine manufactures a
 * difference with the right sign and no meaning; forward-then-backward puts
 * each arm's two slots equidistant from the rep midpoint so linear drift
 * cancels.
 *
 * The triangle mix is taken from the shipping distribution, not invented:
 * mostly small interior triangles, a minority partly offscreen, and spans that
 * are usually short (37.3% of real spans are one pixel). A corpus of big fat
 * interior triangles would measure the fill loop, which the P4 ladder says is
 * not where the time is.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "toridraw_types.h"

#include "graphics/shared_tables.h"
#include "graphics/shared_tables.c"
#include "graphics/raster/gouraudhsllightness/gouraudhsllightness.screen.opaque.bary.branching.s4.c"

void toridraw_gouraud_tri_opaque_s4_asm(
    toripixel_t* pixel_buffer,
    int stride,
    int screen_width,
    int screen_height,
    int x0,
    int x1,
    int x2,
    int y0,
    int y1,
    int y2,
    int color0_hsl16,
    int color1_hsl16,
    int color2_hsl16);

#define W 765 /* the client's own viewport, so the spans are the client's */
#define H 503

struct tri
{
    int x[3];
    int y[3];
    int c[3];
};

static unsigned g_seed = 987654321u;

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

static void
build_corpus(struct tri* c, int n)
{
    int i;

    for( i = 0; i < n; i++ )
    {
        int cx;
        int cy;
        int r;
        int k;

        /* Size distribution: small dominates. The steady state draws 11,570
         * gouraud triangles into a 765x503 viewport at 2.95 spans each, which
         * is a screen full of small triangles, not a handful of large ones. */
        switch( next() % 8 )
        {
        case 0:
            r = range(20, 90);
            break;
        case 1:
        case 2:
            r = range(8, 24);
            break;
        default:
            r = range(1, 9);
            break;
        }

        /* One in eight straddles an edge, so the clamped loop and the y
         * pre-steps get their share. */
        if( (next() % 8) == 0 )
        {
            cx = range(-60, W + 60);
            cy = range(-60, H + 60);
        }
        else
        {
            cx = range(0, W - 1);
            cy = range(0, H - 1);
        }

        for( k = 0; k < 3; k++ )
        {
            c[i].x[k] = cx + range(-r, r);
            c[i].y[k] = cy + range(-r, r);
            c[i].c[k] = range(0, 0xFFFF);
        }
    }
}

static double
seconds(LARGE_INTEGER a, LARGE_INTEGER b, LARGE_INTEGER f)
{
    return (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart;
}

static void
run_c(toripixel_t* fb, const struct tri* c, int n)
{
    int i;

    for( i = 0; i < n; i++ )
        raster_gouraudhsllightness_screen_opaque_bary_branching_s4(
            fb, W, W, H,
            c[i].x[0], c[i].x[1], c[i].x[2],
            c[i].y[0], c[i].y[1], c[i].y[2],
            c[i].c[0], c[i].c[1], c[i].c[2]);
}

static void
run_asm(toripixel_t* fb, const struct tri* c, int n)
{
    int i;

    for( i = 0; i < n; i++ )
        toridraw_gouraud_tri_opaque_s4_asm(
            fb, W, W, H,
            c[i].x[0], c[i].x[1], c[i].x[2],
            c[i].y[0], c[i].y[1], c[i].y[2],
            c[i].c[0], c[i].c[1], c[i].c[2]);
}

int
main(int argc, char** argv)
{
    int tris = (argc > 1) ? atoi(argv[1]) : 12000;
    int frames = (argc > 2) ? atoi(argv[2]) : 300;
    int reps = (argc > 3) ? atoi(argv[3]) : 5;
    int rep;
    int f;
    double tc = 0.0;
    double ta = 0.0;
    LARGE_INTEGER freq;
    struct tri* corpus = malloc(sizeof(*corpus) * tris);
    toripixel_t* fb = malloc(sizeof(*fb) * W * H);

    assert(corpus);
    assert(fb);

    init_hsl16_to_rgb_table();
    build_corpus(corpus, tris);
    memset(fb, 0, sizeof(*fb) * W * H);
    QueryPerformanceFrequency(&freq);

    /* Warm both arms and the framebuffer before any clock is read. The first
     * pass over a 1.5 MB framebuffer is a cold-cache measurement of the
     * allocator, not of either kernel. */
    run_c(fb, corpus, tris);
    run_asm(fb, corpus, tris);

    printf("%d triangles x %d frames x %d reps  (order C,asm,asm,C per rep)\n",
           tris, frames, reps);

    for( rep = 0; rep < reps; rep++ )
    {
        LARGE_INTEGER t0;
        LARGE_INTEGER t1;
        LARGE_INTEGER t2;
        LARGE_INTEGER t3;
        LARGE_INTEGER t4;
        double a;
        double b;

        QueryPerformanceCounter(&t0);
        for( f = 0; f < frames; f++ )
            run_c(fb, corpus, tris);
        QueryPerformanceCounter(&t1);
        for( f = 0; f < frames; f++ )
            run_asm(fb, corpus, tris);
        QueryPerformanceCounter(&t2);
        for( f = 0; f < frames; f++ )
            run_asm(fb, corpus, tris);
        QueryPerformanceCounter(&t3);
        for( f = 0; f < frames; f++ )
            run_c(fb, corpus, tris);
        QueryPerformanceCounter(&t4);

        a = seconds(t0, t1, freq) + seconds(t3, t4, freq);
        b = seconds(t1, t2, freq) + seconds(t2, t3, freq);
        tc += a;
        ta += b;
        printf("  rep%d  C %8.4f s   asm %8.4f s   asm/C %6.3f\n",
               rep, a, b, (a > 0.0) ? b / a : 0.0);
    }

    printf("\nC   total %8.4f s -> %7.3f us per frame of %d triangles\n",
           tc, tc / (double)frames / (double)reps / 2.0 * 1e6, tris);
    printf("asm total %8.4f s -> %7.3f us per frame of %d triangles\n",
           ta, ta / (double)frames / (double)reps / 2.0 * 1e6, tris);
    printf("asm is %6.3fx the cost of C  (%+.1f%%)\n",
           (tc > 0.0) ? ta / tc : 0.0,
           (tc > 0.0) ? (ta / tc - 1.0) * 100.0 : 0.0);

    free(corpus);
    free(fb);
    return 0;
}
