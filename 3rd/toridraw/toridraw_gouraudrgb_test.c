/*
 * Unit test for the `gouraudrgb` raster family.
 *
 *   gouraudrgb.screen.opaque.bary.branching.s4
 *   gouraudrgb.screen.alpha.bary.branching.s4
 *
 * The family interpolates the three RGB channels independently, where its
 * sibling `gouraudhsllightness` interpolates a packed HSL16 word and resolves it
 * through the palette. There is no other rasterizer that produces the same
 * pixels, so nothing here is a parity diff against a reference implementation.
 * Each check instead pins one property that a wrong kernel could not satisfy:
 *
 *   1. coverage      identical to gouraudhsllightness, which the walker is a
 *                    verbatim copy of - a walker bug shows here and nowhere else
 *   2. constant      three equal vertex colours give that exact colour, with no
 *                    palette quantisation anywhere
 *   3. plane (y)     a purely vertical gradient is constant along each row, so
 *                    the row colour is compared to a double-precision plane with
 *                    no quantisation to hide behind
 *   4. plane (x,y)   a general gradient is compared to the same double plane,
 *                    bounded by the s4 stair (the colour is recomputed every
 *                    fourth pixel, so a pixel can lag its own position)
 *   5. clamp         a gradient driven past both ends of the range must clip
 *                    each channel rather than carry into its neighbour
 *   6. alpha         the alpha kernel is the opaque kernel composited with
 *                    alpha_blend(), checked per pixel
 *   7. guards        nothing is written outside the framebuffer, for interior,
 *                    clipped, offscreen and degenerate triangles alike
 *
 * Build and run:
 *   cc -std=c11 -O2 -Wall -Wextra -I3rd/toridraw \
 *      -o /tmp/gouraudrgb_test 3rd/toridraw/toridraw_gouraudrgb_test.c -lm
 *   /tmp/gouraudrgb_test
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toridraw_types.h"

#include "graphics/shared_tables.h"

// clang-format off
#include "graphics/shared_tables.c"

#include "impl/raster/gouraudhsllightness/raster.gouraudhsllightness.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/gouraudrgb/raster.gouraudrgb.opaque.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
#include "impl/raster/gouraudrgb/raster.gouraudrgb.alpha.nofacealpha.nomodulate.painter.branching.s4.scalar.c"
// clang-format on

#define W 200
#define H 150
#define GUARD 64
#define BUF_LEN (GUARD + H * W + GUARD)

#define BG 0x00112233
#define GUARD_FILL 0x5A5A5A5A

struct Tri
{
    const char* name;
    int x[3];
    int y[3];
};

static const struct Tri g_tris[] = {
    { "interior", { 40, 150, 90 }, { 20, 35, 120 } },
    { "interior-flat-top", { 30, 160, 95 }, { 30, 30, 110 } },
    { "interior-flat-bottom", { 30, 160, 95 }, { 110, 110, 30 } },
    { "interior-cw", { 90, 150, 40 }, { 120, 35, 20 } },
    { "sliver-1row", { 20, 180, 100 }, { 60, 60, 61 } },
    { "sliver-1col", { 100, 101, 100 }, { 20, 70, 120 } },
    { "degenerate-zero-area", { 20, 60, 100 }, { 20, 60, 100 } },
    { "degenerate-zero-height", { 20, 60, 100 }, { 40, 40, 40 } },
    { "degenerate-point", { 50, 50, 50 }, { 50, 50, 50 } },
    { "clip-left", { -80, 60, 10 }, { 20, 40, 120 } },
    { "clip-right", { 140, 320, 190 }, { 20, 40, 120 } },
    { "clip-top", { 40, 150, 90 }, { -70, -30, 60 } },
    { "clip-bottom", { 40, 150, 90 }, { 60, 100, 260 } },
    { "clip-corner-tl", { -60, 70, -10 }, { -50, 20, 60 } },
    { "clip-corner-br", { 130, 300, 180 }, { 90, 130, 250 } },
    { "covers-screen", { -400, 600, 100 }, { -300, -300, 500 } },
    { "offscreen-above", { 40, 150, 90 }, { -400, -380, -300 } },
    { "offscreen-below", { 40, 150, 90 }, { 300, 380, 400 } },
    { "offscreen-left", { -400, -300, -350 }, { 20, 40, 120 } },
    { "offscreen-right", { 400, 500, 450 }, { 20, 40, 120 } },
};

#define TRI_COUNT ((int)(sizeof(g_tris) / sizeof(g_tris[0])))

static int g_fail;

#define GEOM(t) W, W, H, (t)->x[0], (t)->x[1], (t)->x[2], (t)->y[0], (t)->y[1], (t)->y[2]

static void
buf_reset(int* buf)
{
    for( int i = 0; i < GUARD; i++ )
        buf[i] = GUARD_FILL;
    for( int i = 0; i < H * W; i++ )
        buf[GUARD + i] = BG;
    for( int i = 0; i < GUARD; i++ )
        buf[GUARD + H * W + i] = GUARD_FILL;
}

static int
guard_intact(const int* buf, const char* what, const char* tri)
{
    for( int i = 0; i < GUARD; i++ )
    {
        if( buf[i] != GUARD_FILL || buf[GUARD + H * W + i] != GUARD_FILL )
        {
            printf("  FAIL %-34s %-22s: wrote outside the framebuffer\n", what, tri);
            g_fail++;
            return 0;
        }
    }
    return 1;
}

static void
pass(const char* what, const char* tri)
{
    printf("  ok   %-34s %s\n", what, tri);
}

/* ------------------------------------------------------------------ plane */

/**
 * The analytic colour plane, in doubles.
 *
 * Barycentric interpolation of the three vertex colours at a pixel centre,
 * independent of every fixed-point decision the kernel makes. Returns 0 when
 * the triangle is degenerate.
 */
static int
plane_eval(
    const struct Tri* t,
    const int rgb[3],
    int shift,
    int px,
    int py,
    double* out)
{
    double x0 = t->x[0], x1 = t->x[1], x2 = t->x[2];
    double y0 = t->y[0], y1 = t->y[1], y2 = t->y[2];

    double area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if( area == 0.0 )
        return 0;

    double c0 = (rgb[0] >> shift) & 0xFF;
    double c1 = (rgb[1] >> shift) & 0xFF;
    double c2 = (rgb[2] >> shift) & 0xFF;

    double w1 = ((px - x0) * (y2 - y0) - (py - y0) * (x2 - x0)) / area;
    double w2 = ((py - y0) * (x1 - x0) - (px - x0) * (y1 - y0)) / area;

    *out = c0 + w1 * (c1 - c0) + w2 * (c2 - c0);
    return 1;
}

/** Largest per-pixel channel step the kernel can take along x, in whole units. */
static double
plane_x_slope(const struct Tri* t, const int rgb[3], int shift)
{
    double a, b;
    if( !plane_eval(t, rgb, shift, 0, 0, &a) )
        return 0.0;
    plane_eval(t, rgb, shift, 1, 0, &b);
    return fabs(b - a);
}

/* ------------------------------------------------------------------ tests */

/*
 * The gouraudrgb walker is a verbatim copy of the gouraudhsllightness one, so
 * the two must light exactly the same pixels. Colour is irrelevant here and is
 * held constant in both; only "did this pixel get written" is compared.
 *
 * This is the check that a walker edit cannot pass by accident: every clip
 * branch, the six-way y permutation, the trapezoid split and the noclip proof
 * all show up as coverage.
 */
static void
test_coverage_matches_hsl(int* a, int* b)
{
    printf("gouraudrgb coverage == gouraudhsllightness coverage\n");

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        buf_reset(a);
        buf_reset(b);

        /* Any two colours that are not the background; only coverage is read. */
        raster_gouraudhsllightness_screen_opaque_bary_branching_s4(
            a + GUARD, GEOM(t), 0x2222, 0x2222, 0x2222);
        raster_gouraudrgb_screen_opaque_bary_branching_s4(
            b + GUARD, GEOM(t), 0x00C0FFEE, 0x00C0FFEE, 0x00C0FFEE);

        if( !guard_intact(a, "gouraudhsllightness coverage", t->name) )
            continue;
        if( !guard_intact(b, "gouraudrgb coverage", t->name) )
            continue;

        int bad = 0;
        int covered = 0;
        for( int p = 0; p < H * W; p++ )
        {
            int hsl_hit = a[GUARD + p] != BG;
            int rgb_hit = b[GUARD + p] != BG;
            covered += rgb_hit;
            if( hsl_hit != rgb_hit )
                bad++;
        }

        if( bad )
        {
            printf(
                "  FAIL %-34s %-22s: %d pixels differ in coverage\n",
                "gouraudrgb coverage",
                t->name,
                bad);
            g_fail++;
        }
        else
        {
            printf("  ok   %-34s %-22s (%d px)\n", "gouraudrgb coverage", t->name, covered);
        }
    }
}

/*
 * Three equal vertex colours must produce that exact colour. The HSL16 family
 * cannot satisfy this - it round-trips through a 65536-entry palette - and it
 * is the simplest statement of what this family is for.
 */
static void
test_constant_colour(int* buf)
{
    printf("gouraudrgb constant colour is exact (no palette round trip)\n");

    static const int colours[] = { 0x00000000, 0x00FFFFFF, 0x00C0FFEE, 0x00123456, 0x00FF0001 };

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        for( int c = 0; c < (int)(sizeof(colours) / sizeof(colours[0])); c++ )
        {
            int colour = colours[c];

            buf_reset(buf);
            raster_gouraudrgb_screen_opaque_bary_branching_s4(
                buf + GUARD, GEOM(t), colour, colour, colour);

            if( !guard_intact(buf, "gouraudrgb constant", t->name) )
                break;

            int bad = 0;
            for( int p = 0; p < H * W; p++ )
            {
                int got = buf[GUARD + p];
                if( got != BG && got != colour )
                    bad++;
            }

            if( bad )
            {
                printf(
                    "  FAIL %-34s %-22s colour=%06X: %d pixels are not the vertex colour\n",
                    "gouraudrgb constant",
                    t->name,
                    (unsigned)colour,
                    bad);
                g_fail++;
                break;
            }
        }
    }
    printf("  ok   all triangles, all colours\n");
}

/*
 * A gradient with no x component: every pixel of a row carries the same colour,
 * so the s4 stair (which only quantises along x) cannot hide an error and the
 * comparison against the analytic plane is tight.
 *
 * The tolerance is 2/255 per channel: one for the 8.8 truncation in the
 * accumulator, one for the half-step bias the kernel applies at the span start.
 */
static void
test_plane_vertical(int* buf)
{
    printf("gouraudrgb vertical gradient vs analytic plane (tolerance 2)\n");

    /* Flat-top triangle: vertices 0 and 1 share y, so a colour that differs
     * only between {0,1} and {2} varies with y alone. */
    static const struct Tri t = { "vgrad", { 30, 170, 100 }, { 20, 20, 130 } };
    static const int rgb[3] = { 0x00204060, 0x00204060, 0x00E0C0A0 };

    buf_reset(buf);
    raster_gouraudrgb_screen_opaque_bary_branching_s4(
        buf + GUARD, GEOM(&t), rgb[0], rgb[1], rgb[2]);

    if( !guard_intact(buf, "gouraudrgb vgrad", t.name) )
        return;

    int worst = 0;
    int bad = 0;
    long checked = 0;

    for( int py = 0; py < H; py++ )
    {
        for( int px = 0; px < W; px++ )
        {
            int got = buf[GUARD + py * W + px];
            if( got == BG )
                continue;

            for( int ch = 0; ch < 3; ch++ )
            {
                int shift = 16 - ch * 8;
                double want;
                if( !plane_eval(&t, rgb, shift, px, py, &want) )
                    continue;

                int have = (got >> shift) & 0xFF;
                int err = (int)lround(fabs((double)have - want));
                if( err > worst )
                    worst = err;
                if( err > 2 )
                    bad++;
            }
            checked++;
        }
    }

    if( bad )
    {
        printf(
            "  FAIL %-34s: %d channel samples off the plane by more than 2 (worst %d)\n",
            "gouraudrgb vgrad",
            bad,
            worst);
        g_fail++;
    }
    else
    {
        printf("  ok   %ld pixels checked, worst channel error %d\n", checked, worst);
    }
}

/*
 * A general gradient, where the colour also varies along x.
 *
 * The kernel recomputes the packed colour once every four pixels, so a drawn
 * pixel legitimately carries the plane's value from up to three pixels to its
 * left. The bound is therefore 3 * |d(channel)/dx| plus the same 2 of
 * fixed-point slack as above - and it is a real bound, not a fudge: a kernel
 * with a wrong plane produces an error that grows with distance from the
 * vertices and blows straight through it.
 */
static void
test_plane_general(int* buf)
{
    printf("gouraudrgb general gradient vs analytic plane (s4 stair bound)\n");

    static const struct Tri tris[] = {
        { "grad-interior", { 30, 175, 95 }, { 15, 40, 135 } },
        { "grad-cw", { 95, 175, 30 }, { 135, 40, 15 } },
        { "grad-wide", { 5, 195, 100 }, { 10, 25, 140 } },
    };
    static const int rgb[3] = { 0x00FF0000, 0x0000FF00, 0x000000FF };

    for( int i = 0; i < (int)(sizeof(tris) / sizeof(tris[0])); i++ )
    {
        const struct Tri* t = &tris[i];

        buf_reset(buf);
        raster_gouraudrgb_screen_opaque_bary_branching_s4(
            buf + GUARD, GEOM(t), rgb[0], rgb[1], rgb[2]);

        if( !guard_intact(buf, "gouraudrgb grad", t->name) )
            continue;

        double tol[3];
        for( int ch = 0; ch < 3; ch++ )
            tol[ch] = 3.0 * plane_x_slope(t, rgb, 16 - ch * 8) + 2.0;

        double worst = 0.0;
        int bad = 0;
        long checked = 0;

        for( int py = 0; py < H; py++ )
        {
            for( int px = 0; px < W; px++ )
            {
                int got = buf[GUARD + py * W + px];
                if( got == BG )
                    continue;

                for( int ch = 0; ch < 3; ch++ )
                {
                    int shift = 16 - ch * 8;
                    double want;
                    if( !plane_eval(t, rgb, shift, px, py, &want) )
                        continue;

                    double err = fabs((double)((got >> shift) & 0xFF) - want);
                    if( err > worst )
                        worst = err;
                    if( err > tol[ch] )
                        bad++;
                }
                checked++;
            }
        }

        if( bad )
        {
            printf(
                "  FAIL %-34s %-22s: %d channel samples outside the stair bound (worst %.2f)\n",
                "gouraudrgb grad",
                t->name,
                bad,
                worst);
            g_fail++;
        }
        else
        {
            printf(
                "  ok   %-34s %-22s %ld px, worst %.2f, bound %.2f/%.2f/%.2f\n",
                "gouraudrgb grad",
                t->name,
                checked,
                worst,
                tol[0],
                tol[1],
                tol[2]);
        }
    }
}

/*
 * The clamp, tested where it lives.
 *
 * The HSL16 family gets its bounds free by masking into the palette table; this
 * one has to clip each channel explicitly. Without that clip a channel that
 * steps below 0 or above 255 carries into its neighbour and the pixel changes
 * colour completely rather than saturating - and the high byte, which nothing
 * else ever writes, picks up bits too.
 *
 * This is checked against gouraudrgb_pack_ish8 directly rather than by scanning
 * rendered triangles. Overshoot at a *drawn* pixel is real but rare - it needs
 * a span endpoint to land just past a vertex, which happened on a single pixel
 * of the whole triangle set when this was measured - so a scan is a flaky
 * detector that passes for the wrong reason on most geometry. The accumulators
 * that reach the helper routinely sit a few counts outside the range, so the
 * helper's contract is the thing worth pinning, and it is pinned exactly.
 */
static void
test_pack_clamps(void)
{
    printf("gouraudrgb_pack_ish8 clamps each channel independently\n");

    /* 8.8 inputs spanning well past both ends of the range. */
    static const int ish8[] = {
        -0x8000, -0x0400, -0x0101, -0x0100, -0x0001, 0x0000, 0x0080,
        0x7F00,  0xFF00,  0xFFFF,  0x10000, 0x10100, 0x40000,
    };
    const int n = (int)(sizeof(ish8) / sizeof(ish8[0]));

    int bad = 0;
    long checked = 0;

    for( int i = 0; i < n; i++ )
    {
        for( int j = 0; j < n; j++ )
        {
            for( int k = 0; k < n; k++ )
            {
                int got = gouraudrgb_pack_ish8(ish8[i], ish8[j], ish8[k]);

                int want_r = ish8[i] >> 8;
                int want_g = ish8[j] >> 8;
                int want_b = ish8[k] >> 8;
                want_r = want_r < 0 ? 0 : (want_r > 0xFF ? 0xFF : want_r);
                want_g = want_g < 0 ? 0 : (want_g > 0xFF ? 0xFF : want_g);
                want_b = want_b < 0 ? 0 : (want_b > 0xFF ? 0xFF : want_b);

                int want = (want_r << 16) | (want_g << 8) | want_b;

                if( got != want )
                    bad++;
                checked++;
            }
        }
    }

    if( bad )
    {
        printf(
            "  FAIL gouraudrgb_pack_ish8: %d of %ld channel triples are not the "
            "saturating pack\n",
            bad,
            checked);
        g_fail++;
    }
    else
    {
        printf("  ok   %ld channel triples, all saturating\n", checked);
    }
}

/*
 * The same property observed end to end: no rendered pixel may set a bit above
 * red, and a channel with no gradient of its own may not move. Weaker than the
 * check above (see its note on how rarely a drawn pixel overshoots), but it is
 * what catches a clamp that is correct in the helper and bypassed in the span.
 */
static void
test_channel_clamp(int* buf)
{
    printf("gouraudrgb rendered pixels never carry between channels\n");

    /*
     * Each case holds ONE channel constant across all three vertices while the
     * other two swing the full range. The constant channel is the detector: it
     * has no gradient of its own, so the only way it can move is if a
     * neighbouring channel overshot its byte and carried into it. `witness` is
     * the channel that must not move, and `value` what it must stay at.
     */
    static const struct
    {
        int rgb[3];
        int shift;
        int value;
    } cases[] = {
        /* green pinned at 0x80, red and blue swinging */
        { { 0x00FF8000, 0x000080FF, 0x00FF80FF }, 8, 0x80 },
        /* red pinned at 0x40, green and blue swinging */
        { { 0x0040FF00, 0x004000FF, 0x0040FFFF }, 16, 0x40 },
        /* blue pinned at 0xC0, red and green swinging */
        { { 0x00FF00C0, 0x0000FFC0, 0x00FFFFC0 }, 0, 0xC0 },
        /* Every channel hard against both ends of the range at once. No
         * witness channel here (shift 24 reads the byte that must stay zero),
         * but this is the set that actually produced an out-of-range drawn
         * pixel when the clamp was removed. */
        { { 0x00000000, 0x00FFFFFF, 0x00000000 }, 24, 0x00 },
        { { 0x00FFFFFF, 0x00000000, 0x00FFFFFF }, 24, 0x00 },
        { { 0x000000FF, 0x00FF0000, 0x0000FF00 }, 24, 0x00 },
        { { 0x00FF00FF, 0x0000FF00, 0x00FF00FF }, 24, 0x00 },
    };

    int bad_high = 0;
    int bad_witness = 0;
    long checked = 0;

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        for( int e = 0; e < (int)(sizeof(cases) / sizeof(cases[0])); e++ )
        {
            buf_reset(buf);
            raster_gouraudrgb_screen_opaque_bary_branching_s4(
                buf + GUARD, GEOM(t), cases[e].rgb[0], cases[e].rgb[1], cases[e].rgb[2]);

            if( !guard_intact(buf, "gouraudrgb clamp", t->name) )
                break;

            for( int p = 0; p < H * W; p++ )
            {
                int got = buf[GUARD + p];
                if( got == BG )
                    continue;
                checked++;

                /* Nothing above bit 23 may ever be set: a negative channel
                 * shifted into place is exactly how that happens. */
                if( ((unsigned)got & 0xFF000000u) != 0 )
                    bad_high++;

                if( ((got >> cases[e].shift) & 0xFF) != cases[e].value )
                    bad_witness++;
            }
        }
    }

    if( bad_high || bad_witness )
    {
        printf(
            "  FAIL gouraudrgb clamp: %d pixels set bits above red, "
            "%d pixels moved a channel that has no gradient\n",
            bad_high,
            bad_witness);
        g_fail++;
    }
    else
    {
        printf("  ok   %ld pixels, 3 witness channels, all triangles\n", checked);
    }
}

/*
 * The alpha kernel must be the opaque kernel composited with alpha_blend().
 *
 * Coverage is taken from the opaque pass rather than guessed from the
 * background: an alpha-blended pixel can legitimately land back on the
 * background colour, so "changed" is not the same as "covered".
 */
static void
test_alpha_algebra(int* opaque_buf, int* got)
{
    printf("gouraudrgb.alpha == alpha_blend(alpha, dst, gouraudrgb.opaque)\n");

    static const int alphas[] = { 0x00, 0x01, 0x40, 0x80, 0xC0, 0xFF };
    static const int rgb[3] = { 0x00FF4020, 0x002040FF, 0x0020FF40 };

    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        buf_reset(opaque_buf);
        raster_gouraudrgb_screen_opaque_bary_branching_s4(
            opaque_buf + GUARD, GEOM(t), rgb[0], rgb[1], rgb[2]);
        if( !guard_intact(opaque_buf, "gouraudrgb alpha (opaque pass)", t->name) )
            continue;

        for( int ai = 0; ai < (int)(sizeof(alphas) / sizeof(alphas[0])); ai++ )
        {
            int alpha = alphas[ai];

            buf_reset(got);
            raster_gouraudrgb_screen_alpha_bary_branching_s4(
                got + GUARD, GEOM(t), rgb[0], rgb[1], rgb[2], alpha);

            if( !guard_intact(got, "gouraudrgb alpha", t->name) )
                break;

            int bad = 0;
            for( int p = 0; p < H * W; p++ )
            {
                int opaque = opaque_buf[GUARD + p];
                int expect = (opaque == BG) ? BG : alpha_blend(alpha, BG, opaque);
                if( got[GUARD + p] != expect )
                    bad++;
            }

            if( bad )
            {
                printf(
                    "  FAIL %-34s %-22s alpha=%3d: %d pixels are not the blend\n",
                    "gouraudrgb alpha",
                    t->name,
                    alpha,
                    bad);
                g_fail++;
                break;
            }
        }
    }
    printf("  ok   all triangles, all alphas\n");
}

/*
 * Guard-band sweep over every triangle for both gates, including the ones the
 * other tests skip once they have failed. Cheap, and it is the check that says
 * a clipped or degenerate triangle cannot corrupt memory.
 */
static void
test_guards(int* buf)
{
    printf("gouraudrgb writes nothing outside the framebuffer\n");

    int ok = 1;
    for( int i = 0; i < TRI_COUNT; i++ )
    {
        const struct Tri* t = &g_tris[i];

        buf_reset(buf);
        raster_gouraudrgb_screen_opaque_bary_branching_s4(
            buf + GUARD, GEOM(t), 0x00FF0000, 0x0000FF00, 0x000000FF);
        ok &= guard_intact(buf, "gouraudrgb.opaque guards", t->name);

        buf_reset(buf);
        raster_gouraudrgb_screen_alpha_bary_branching_s4(
            buf + GUARD, GEOM(t), 0x00FF0000, 0x0000FF00, 0x000000FF, 0x80);
        ok &= guard_intact(buf, "gouraudrgb.alpha guards", t->name);
    }
    if( ok )
        pass("gouraudrgb guards", "all triangles, both gates");
}

int
main(void)
{
    static int a[BUF_LEN];
    static int b[BUF_LEN];

    /* Only gouraudhsllightness needs the palette, and only for the coverage
     * comparison; gouraudrgb never touches it. */
    init_hsl16_to_pixel_table();

    test_coverage_matches_hsl(a, b);
    test_constant_colour(a);
    test_plane_vertical(a);
    test_plane_general(a);
    test_pack_clamps();
    test_channel_clamp(a);
    test_alpha_algebra(a, b);
    test_guards(a);

    if( g_fail )
    {
        printf("\n%d gouraudrgb check(s) FAILED\n", g_fail);
        return 1;
    }

    printf("\nall gouraudrgb checks passed\n");
    return 0;
}
