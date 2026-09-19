/*
 * ToriDraw2D_BlitArgbAlpha's alpha==255 run walk, against the per-pixel ladder
 * it replaces.
 *
 * The fast path claims the output is byte-identical, not merely close. It rests
 * on one arithmetic fact: a source pixel with a==255 already has 0xFF in its
 * top byte, so the `argb | 0xFF000000` the per-pixel path applies is a no-op on
 * exactly the pixels the run copies -- which is what lets a memcpy stand in for
 * the blend. Every interface sprite in the client goes through this path, so a
 * one-pixel disagreement would be a subtle, everywhere-at-once rendering bug.
 *
 * `opaque_reference` below is the per-pixel loop copied verbatim out of
 * toridraw_2d.c as it stood before the run walk. It has to be a copy: the
 * function under test now returns before reaching that loop in exactly the case
 * being tested, so comparing it against itself would compare the fast path with
 * the fast path.
 *
 * The generated sprites deliberately favour the shapes the run walk splits on
 * -- long opaque interiors, long transparent surrounds, isolated partial
 * pixels, and alternating single pixels that defeat run detection entirely --
 * plus fully-opaque and fully-transparent rows, and clipping on every edge.
 *
 * Standalone TU, no cache or disk. Includes the .c so the file-static blend
 * helper both sides need is in scope.
 */
#include "toridraw_2d.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
opaque_reference(
    struct ToriDraw_ViewPort* view_port,
    int dst_x,
    int dst_y,
    uint32_t const* src,
    int src_w,
    int src_h,
    int alpha,
    int* pixel_buffer)
{
    if( src_w <= 0 || src_h <= 0 || alpha <= 0 )
        return;
    if( alpha > 255 )
        alpha = 255;

    int64_t x0 = dst_x;
    int64_t y0 = dst_y;
    int64_t x1 = (int64_t)dst_x + src_w;
    int64_t y1 = (int64_t)dst_y + src_h;
    if( x0 < view_port->clip_left )
        x0 = view_port->clip_left;
    if( y0 < view_port->clip_top )
        y0 = view_port->clip_top;
    if( x1 > view_port->clip_right )
        x1 = view_port->clip_right;
    if( y1 > view_port->clip_bottom )
        y1 = view_port->clip_bottom;
    if( x0 >= x1 || y0 >= y1 )
        return;

    int const src_x0 = (int)(x0 - dst_x);
    int const src_y0 = (int)(y0 - dst_y);
    int const draw_w = (int)(x1 - x0);
    int const draw_h = (int)(y1 - y0);
    int const stride = view_port->stride;

    for( int y = 0; y < draw_h; y++ )
    {
        uint32_t const* srow = src + (size_t)(src_y0 + y) * src_w + src_x0;
        int* drow = pixel_buffer + (size_t)((int)y0 + y) * stride + (int)x0;
        for( int x = 0; x < draw_w; x++ )
            toridraw2d_blend_argb_unclipped(&drow[x], srow[x], alpha);
    }
}

/* xorshift32, so the corpus is identical on every platform and every run. */
static uint32_t g_rng = 0x2463534Du;

static uint32_t
rng_next(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* `shape` selects which alpha distribution the sprite gets, so the corpus
 * covers the run walk's branches rather than only its average case. */
static void
fill_sprite(uint32_t* px, int w, int h, int shape)
{
    int const n = w * h;
    for( int i = 0; i < n; i++ )
    {
        uint32_t const rgb = rng_next() & 0x00FFFFFFu;
        uint32_t a;
        switch( shape )
        {
        case 0: /* every pixel opaque -- one run per row */
            a = 255;
            break;
        case 1: /* every pixel clear -- the whole blit touches nothing */
            a = 0;
            break;
        case 2: /* alternating -- defeats run detection entirely */
            a = (i & 1) ? 255 : 0;
            break;
        case 3: /* opaque interior, clear surround -- the common sprite */
            a = ((i % w) < 2 || (i % w) > w - 3) ? 0 : 255;
            break;
        case 4: /* partials only -- never takes a run at all */
            a = 1 + (rng_next() % 254);
            break;
        default: /* everything mixed, biased toward the two extremes */
        {
            uint32_t const r = rng_next() % 10;
            a = (r < 4) ? 255 : (r < 8) ? 0 : (1 + (rng_next() % 254));
            break;
        }
        }
        px[i] = (a << 24) | rgb;
    }
}

int
main(void)
{
    enum
    {
        CANVAS_W = 61,
        CANVAS_H = 43,
        CANVAS_N = CANVAS_W * CANVAS_H,
        MAX_SPRITE = 24 * 24
    };

    int got[CANVAS_N];
    int want[CANVAS_N];
    uint32_t sprite[MAX_SPRITE];

    long cases = 0;
    long mismatches = 0;
    long pixels_differing = 0;

    for( int shape = 0; shape <= 5; shape++ )
    {
        for( int trial = 0; trial < 700; trial++ )
        {
            int const sw = 1 + (int)(rng_next() % 24);
            int const sh = 1 + (int)(rng_next() % 24);
            fill_sprite(sprite, sw, sh, shape);

            /* Span well past every edge so each trial can land fully inside,
             * straddle one edge, straddle a corner, or miss the canvas. */
            int const dx = (int)(rng_next() % (CANVAS_W + 32)) - 16;
            int const dy = (int)(rng_next() % (CANVAS_H + 32)) - 16;

            struct ToriDraw_ViewPort vp;
            memset(&vp, 0, sizeof(vp));
            vp.clip_left = (int)(rng_next() % 5);
            vp.clip_top = (int)(rng_next() % 5);
            vp.clip_right = CANVAS_W - (int)(rng_next() % 5);
            vp.clip_bottom = CANVAS_H - (int)(rng_next() % 5);
            vp.stride = CANVAS_W;

            /* Both sides start from the same non-uniform destination, so a
             * blend that wrongly skips a pixel shows up instead of matching a
             * cleared buffer by luck. */
            for( int i = 0; i < CANVAS_N; i++ )
            {
                int const seed = (int)(rng_next() & 0x00FFFFFF);
                got[i] = (int)0xFF000000 | seed;
                want[i] = got[i];
            }

            ToriDraw2D_BlitArgbAlpha(&vp, dx, dy, sprite, sw, sh, 255, got);
            opaque_reference(&vp, dx, dy, sprite, sw, sh, 255, want);

            cases++;
            int differing = 0;
            for( int i = 0; i < CANVAS_N; i++ )
                if( got[i] != want[i] )
                    differing++;

            if( differing )
            {
                mismatches++;
                pixels_differing += differing;
                if( mismatches <= 3 )
                    fprintf(
                        stderr,
                        "mismatch: shape=%d sprite=%dx%d at (%d,%d) "
                        "clip=[%d,%d,%d,%d] %d pixels differ\n",
                        shape, sw, sh, dx, dy, vp.clip_left, vp.clip_top,
                        vp.clip_right, vp.clip_bottom, differing);
            }
        }
    }

    printf(
        "blit opaque run walk: %ld cases, %ld mismatches (%ld pixels)\n",
        cases, mismatches, pixels_differing);

    if( mismatches )
    {
        printf("ToriDraw2D opaque-blit test FAILED.\n");
        return 1;
    }
    printf("All ToriDraw2D opaque-blit tests passed.\n");
    return 0;
}
