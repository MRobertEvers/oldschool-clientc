/*
 * ToriDraw2D_BlitArgbScaledAlpha's 1:1 shortcut, against the loop it skips.
 *
 * The scaled blit takes the unscaled path when the destination box is the size
 * of the source. That is a claim about arithmetic, not an approximation: with
 * dst == src the stepper's sx_step is 1 and its remainder step is 0, so `sx`
 * walks first_x + x and the rows walk first_y + y — the same addresses
 * BlitArgbAlpha computes directly. This pins the claim, because the shortcut is
 * on a path every interface sprite in the client goes through, and a one-pixel
 * disagreement there would be a subtle, everywhere-at-once rendering bug.
 *
 * `scaled_reference` below is the stepping loop copied verbatim out of
 * toridraw_2d.c. It has to be a copy: the function under test now returns
 * before reaching that code in exactly the case being tested, so comparing it
 * against itself would compare the shortcut with the shortcut.
 *
 * Standalone TU, no cache or disk. Includes the .c so the file-static blend
 * helper both sides need is in scope.
 */
#include "toridraw_2d.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
scaled_reference(
    struct ToriDraw_ViewPort* vp,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    uint32_t const* src,
    int src_w,
    int src_h,
    int alpha,
    int* pixel_buffer)
{
    if( src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 || alpha <= 0 )
        return;
    if( alpha > 255 )
        alpha = 255;

    int64_t x0 = dst_x;
    int64_t y0 = dst_y;
    int64_t x1 = (int64_t)dst_x + dst_w;
    int64_t y1 = (int64_t)dst_y + dst_h;
    if( x0 < vp->clip_left )
        x0 = vp->clip_left;
    if( y0 < vp->clip_top )
        y0 = vp->clip_top;
    if( x1 > vp->clip_right )
        x1 = vp->clip_right;
    if( y1 > vp->clip_bottom )
        y1 = vp->clip_bottom;
    if( x0 >= x1 || y0 >= y1 )
        return;

    int const first_x = (int)(x0 - dst_x);
    int const first_y = (int)(y0 - dst_y);
    int const draw_w = (int)(x1 - x0);
    int const draw_h = (int)(y1 - y0);
    int const stride = vp->stride;

    int64_t const x_num = (int64_t)first_x * src_w;
    int sx0 = (int)(x_num / dst_w);
    int const x_rem0 = (int)(x_num % dst_w);
    int const sx_step = src_w / dst_w;
    int const x_rem_step = src_w % dst_w;

    int64_t const y_num = (int64_t)first_y * src_h;
    int sy = (int)(y_num / dst_h);
    int y_rem = (int)(y_num % dst_h);
    int const sy_step = src_h / dst_h;
    int const y_rem_step = src_h % dst_h;

    for( int y = 0; y < draw_h; y++ )
    {
        uint32_t const* srow = src + (size_t)sy * src_w;
        int* drow = pixel_buffer + (size_t)((int)y0 + y) * stride + (int)x0;

        int sx = sx0;
        int x_rem = x_rem0;
        for( int x = 0; x < draw_w; x++ )
        {
            toridraw2d_blend_argb_unclipped(&drow[x], srow[sx], alpha);
            sx += sx_step;
            x_rem += x_rem_step;
            if( x_rem >= dst_w )
            {
                x_rem -= dst_w;
                sx++;
            }
        }

        sy += sy_step;
        y_rem += y_rem_step;
        if( y_rem >= dst_h )
        {
            y_rem -= dst_h;
            sy++;
        }
    }
}

#define CANVAS_W 200
#define CANVAS_H 160

static unsigned g_rng = 12345u;

static unsigned
next_rand(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng;
}

int
main(void)
{
    int fails = 0;
    int cases = 0;

    /* Sizes, placements and clip rects are randomised rather than enumerated
     * because the interesting cases are the CLIPPED ones — a sprite hanging off
     * an edge is where first_x/first_y stop being 0 and the two paths' source
     * addressing could diverge. Negative and past-the-edge origins are in the
     * range on purpose, and alpha runs past 255 to cover the clamp. */
    for( int iter = 0; iter < 4000; iter++ )
    {
        int const sw = 1 + (int)(next_rand() % 40);
        int const sh = 1 + (int)(next_rand() % 40);
        int const dx = (int)(next_rand() % 260) - 40;
        int const dy = (int)(next_rand() % 220) - 40;
        int const alpha = (int)(next_rand() % 300);

        uint32_t* src = malloc((size_t)sw * (size_t)sh * sizeof(*src));
        int* got = malloc(sizeof(int) * CANVAS_W * CANVAS_H);
        int* want = malloc(sizeof(int) * CANVAS_W * CANVAS_H);
        struct ToriDraw_ViewPort vp;

        assert(src);
        assert(got);
        assert(want);

        for( int i = 0; i < sw * sh; i++ )
            src[i] = next_rand();
        /* Non-zero destination: the blend reads it, so a path that wrote the
         * wrong source pixel over an already-correct one would still show. */
        for( int i = 0; i < CANVAS_W * CANVAS_H; i++ )
        {
            int const v = (int)next_rand();
            got[i] = v;
            want[i] = v;
        }

        memset(&vp, 0, sizeof(vp));
        vp.stride = CANVAS_W;
        vp.clip_left = (int)(next_rand() % 30);
        vp.clip_top = (int)(next_rand() % 30);
        vp.clip_right = CANVAS_W - (int)(next_rand() % 30);
        vp.clip_bottom = CANVAS_H - (int)(next_rand() % 30);

        ToriDraw2D_BlitArgbScaledAlpha(&vp, dx, dy, sw, sh, src, sw, sh, alpha, got);
        scaled_reference(&vp, dx, dy, sw, sh, src, sw, sh, alpha, want);
        cases++;

        if( memcmp(got, want, sizeof(int) * CANVAS_W * CANVAS_H) != 0 )
        {
            fails++;
            if( fails <= 3 )
                printf(
                    "MISMATCH iter=%d sw=%d sh=%d dst=(%d,%d) alpha=%d "
                    "clip=(%d,%d,%d,%d)\n",
                    iter, sw, sh, dx, dy, alpha,
                    vp.clip_left, vp.clip_top, vp.clip_right, vp.clip_bottom);
        }

        free(src);
        free(got);
        free(want);
    }

    printf("blit 1:1 shortcut: %d cases, %d mismatches\n", cases, fails);
    if( fails )
        return 1;
    printf("All ToriDraw2D scaled-blit tests passed.\n");
    return 0;
}
