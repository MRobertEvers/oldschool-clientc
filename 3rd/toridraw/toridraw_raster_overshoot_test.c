/*
 * How far outside a triangle's vertices does the rasteriser paint?
 *
 *   make -C src test-raster-overshoot
 *
 * WHY THIS EXISTS
 *
 * ToriDraw_CalculateCylinderAabb8point projects eight cylinder corners per
 * model to build a cull box -- 30.3 ns of the 56.1 ns a four-vertex terrain
 * tile spends being projected (test-proj-model-bench). The obvious fix is to
 * drop the corners and take the box from the model's own projected vertices,
 * which are computed anyway: strictly less work, and a far tighter box, since
 * a tile's cylinder has radius sqrt(2)*64 ~ 90 around a 128-unit square.
 *
 * It does not work, and the reason is this file's subject. An EXACT box drops
 * eighteen pixels from the last column of the 3D viewport, in every frame:
 * the raster paints outside the hull its three vertices describe, and the
 * cylinder box was loose enough to hide it. Padding the box by four restores
 * pixel-identity -- but four fitted to one scene is not a bound, and a model
 * at some other angle could overshoot more.
 *
 * So measure the overshoot instead of guessing it. The number this prints is
 * the padding a vertex-derived cull box needs, and the test is what keeps that
 * number true when a kernel changes.
 *
 * HOW
 *
 * Rasterise a triangle into a cleared buffer, find the painted pixels, and
 * compare their extent against the triangle's own vertex box clipped to the
 * screen. Anything painted outside that is overshoot. Reported per side,
 * because they need not be symmetric -- a fill that aligns its destination
 * before it widens (see toridraw_flat_tri_asm_test.c's header) would overshoot
 * to the left and not to the right.
 *
 * The C reference kernels are the subjects rather than the asm ones: the asm
 * is held bit-identical to them by the two _asm_test files next door, so a
 * bound proved here covers both, and this test then needs no assembler.
 *
 * The generators matter as much as the count. Uniform random triangles are
 * almost all fat and interior, and would never reach a sliver one pixel wide
 * or a span that starts just off the left edge -- which is exactly where a
 * walk that rounds or aligns goes outside the hull.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toridraw_types.h"

#include "graphics/shared_tables.h"
#include "graphics/shared_tables.c"
#include "graphics/raster/flat/flat.screen.opaque.branching.s4.c"
#include "graphics/raster/gouraudhsllightness/gouraudhsllightness.screen.opaque.bary.branching.s4.c"

#define W 256
#define H 200
/* Not zero: a kernel legitimately writes black, and a cleared buffer that
 * looks like painted black reads as "nothing was drawn". 0x5A5A5A5A is what
 * the asm tests next door clear with, for the same reason. */
#define CLEAR ((toripixel_t)0x5A5A5A5Au)

static toripixel_t g_fb[W * H];

static uint32_t g_rng = 0x9E3779B9u;

static uint32_t
rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static int
rnd_range(int lo, int hi)
{
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

struct Overshoot
{
    int left;
    int right;
    int top;
    int bottom;
    long painted_tris;
};

static void
note_worst(struct Overshoot* w, const struct Overshoot* o)
{
    if( o->left > w->left )
        w->left = o->left;
    if( o->right > w->right )
        w->right = o->right;
    if( o->top > w->top )
        w->top = o->top;
    if( o->bottom > w->bottom )
        w->bottom = o->bottom;
}

/*
 * One triangle's overshoot, or nothing painted. The hull is clipped to the
 * screen first: a vertex at x = -40 does not entitle the kernel to paint at
 * -40, and the question here is only whether it paints outside what the
 * vertices claim, within the part of the screen it could reach at all.
 */
static int
measure(const int* x, const int* y, struct Overshoot* out)
{
    int hull_min_x = x[0] < x[1] ? ( x[0] < x[2] ? x[0] : x[2] ) : ( x[1] < x[2] ? x[1] : x[2] );
    int hull_max_x = x[0] > x[1] ? ( x[0] > x[2] ? x[0] : x[2] ) : ( x[1] > x[2] ? x[1] : x[2] );
    int hull_min_y = y[0] < y[1] ? ( y[0] < y[2] ? y[0] : y[2] ) : ( y[1] < y[2] ? y[1] : y[2] );
    int hull_max_y = y[0] > y[1] ? ( y[0] > y[2] ? y[0] : y[2] ) : ( y[1] > y[2] ? y[1] : y[2] );
    int min_px = W;
    int max_px = -1;
    int min_py = H;
    int max_py = -1;
    int px;
    int py;

    if( hull_min_x < 0 )
        hull_min_x = 0;
    if( hull_min_y < 0 )
        hull_min_y = 0;
    if( hull_max_x > W - 1 )
        hull_max_x = W - 1;
    if( hull_max_y > H - 1 )
        hull_max_y = H - 1;

    for( py = 0; py < H; py++ )
    {
        for( px = 0; px < W; px++ )
        {
            if( g_fb[py * W + px] == CLEAR )
                continue;
            if( px < min_px )
                min_px = px;
            if( px > max_px )
                max_px = px;
            if( py < min_py )
                min_py = py;
            if( py > max_py )
                max_py = py;
        }
    }

    if( max_px < 0 )
        return 0; /* nothing painted -- degenerate or fully clipped */

    out->left = hull_min_x - min_px;
    out->right = max_px - hull_max_x;
    out->top = hull_min_y - min_py;
    out->bottom = max_py - hull_max_y;
    if( out->left < 0 )
        out->left = 0;
    if( out->right < 0 )
        out->right = 0;
    if( out->top < 0 )
        out->top = 0;
    if( out->bottom < 0 )
        out->bottom = 0;
    out->painted_tris = 1;
    return 1;
}

/*
 * The shapes a walk actually breaks on. `kind` selects a family rather than
 * scaling one, because the interesting cases are qualitatively different:
 * a sliver's span is a single pixel that rounding can move, and a triangle
 * straddling an edge puts the span's lead-in at every alignment phase.
 */
static void
generate(int kind, int* x, int* y)
{
    int i;
    switch( kind )
    {
    case 0: /* interior, fat */
        for( i = 0; i < 3; i++ )
        {
            x[i] = rnd_range(20, W - 20);
            y[i] = rnd_range(20, H - 20);
        }
        break;
    case 1: /* straddling every edge */
        for( i = 0; i < 3; i++ )
        {
            x[i] = rnd_range(-40, W + 40);
            y[i] = rnd_range(-40, H + 40);
        }
        break;
    case 2: /* vertical sliver: one pixel wide, tall */
        x[0] = rnd_range(0, W - 1);
        x[1] = x[0] + rnd_range(0, 1);
        x[2] = x[0] + rnd_range(0, 1);
        y[0] = rnd_range(0, H - 1);
        y[1] = rnd_range(0, H - 1);
        y[2] = rnd_range(0, H - 1);
        break;
    case 3: /* horizontal sliver: one pixel tall, wide */
        y[0] = rnd_range(0, H - 1);
        y[1] = y[0] + rnd_range(0, 1);
        y[2] = y[0] + rnd_range(0, 1);
        x[0] = rnd_range(0, W - 1);
        x[1] = rnd_range(0, W - 1);
        x[2] = rnd_range(0, W - 1);
        break;
    case 4: /* hard against the right edge, which is where the frame lost its
             * pixels -- every phase of the span's end modulo four */
        for( i = 0; i < 3; i++ )
        {
            x[i] = rnd_range(W - 12, W + 8);
            y[i] = rnd_range(0, H - 1);
        }
        break;
    default: /* hard against the left edge */
        for( i = 0; i < 3; i++ )
        {
            x[i] = rnd_range(-8, 12);
            y[i] = rnd_range(0, H - 1);
        }
        break;
    }
}

#define KINDS 6
#define PER_KIND 4000

int
main(void)
{
    static const char* kind_name[KINDS] = { "interior",     "straddling", "v-sliver",
                                            "h-sliver",     "right edge", "left edge" };
    struct Overshoot worst_all = { 0, 0, 0, 0, 0 };
    int failures = 0;

    /* The kernels index this to turn an hsl16 into a pixel; without it every
     * colour comes out zero and nothing looks painted. */
    init_hsl16_to_rgb_table();

    printf("raster overshoot beyond the vertex hull, %d triangles per shape\n",
           PER_KIND * 2);
    printf("%-12s %-10s %6s %6s %6s %6s   %s\n", "kernel", "shape", "left", "right", "top",
           "bottom", "painted");
    printf("--------------------------------------------------------------------------\n");

    for( int kernel = 0; kernel < 2; kernel++ )
    {
        for( int kind = 0; kind < KINDS; kind++ )
        {
            struct Overshoot worst = { 0, 0, 0, 0, 0 };
            for( int i = 0; i < PER_KIND; i++ )
            {
                int x[3];
                int y[3];
                struct Overshoot o = { 0, 0, 0, 0, 0 };

                generate(kind, x, y);
                memset(g_fb, 0x5A, sizeof(g_fb));

                if( kernel == 0 )
                    raster_flat_screen_opaque_branching_s4(
                        g_fb, W, W, H, x[0], x[1], x[2], y[0], y[1], y[2],
                        (int)(rnd() & 0x7FFF) | 1);
                else
                    raster_gouraudhsllightness_screen_opaque_bary_branching_s4(
                        g_fb, W, W, H, x[0], x[1], x[2], y[0], y[1], y[2],
                        (int)(rnd() % 127) + 1, (int)(rnd() % 127) + 1,
                        (int)(rnd() % 127) + 1);

                if( measure(x, y, &o) )
                {
                    worst.painted_tris++;
                    note_worst(&worst, &o);
                }
            }
            printf("%-12s %-10s %6d %6d %6d %6d   %ld\n", kernel == 0 ? "flat" : "gouraud",
                   kind_name[kind], worst.left, worst.right, worst.top, worst.bottom,
                   worst.painted_tris);
            note_worst(&worst_all, &worst);
        }
    }

    printf("\n  worst over everything: left %d, right %d, top %d, bottom %d\n",
           worst_all.left, worst_all.right, worst_all.top, worst_all.bottom);

    {
        int pad = worst_all.left;
        if( worst_all.right > pad )
            pad = worst_all.right;
        if( worst_all.top > pad )
            pad = worst_all.top;
        if( worst_all.bottom > pad )
            pad = worst_all.bottom;
        printf("\n  A cull box derived from a model's projected vertices must be\n");
        printf("  padded by %d pixel(s) to be conservative.\n", pad);
        if( pad == 0 )
            printf("\n  ZERO means the raster stays inside the hull, and the missing\n"
                   "  pixels at the viewport edge have some other cause -- do not\n"
                   "  conclude a pad is safe from this run alone.\n");
    }

    return failures ? 1 : 0;
}
