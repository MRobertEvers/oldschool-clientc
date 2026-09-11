/*
 * The startup progress bar, against the references' own measurements.
 *
 * Asserted rather than eyeballed, and that is not a preference. The bar is on
 * screen for a fraction of a second during boot, and this client's exit-frame
 * capture RE-RENDERS after boot has finished -- so a screenshot of it is not
 * merely awkward to catch, it cannot contain the bar at all. Numbers are the
 * only way to know this is the reference's picture.
 *
 * The numbers come from Client-TS GameShell.messageBox and the deob's
 * class510.method11179, which agree:
 *
 *   strokeRect(w/2 - 152, h/2 - 18, 304, 34)      the red track
 *   fillRect  (+2, +2, progress * 3, 30)          the red fill
 *   drawRect  (+1, +1, 302, 32)  [deob only]      the black inset rule
 *   fillRect  (+2 + p*3, +2, 300 - p*3, 30)       the black cover
 */

#include "engine/boot_bar.h"

#include <stdio.h>
#include <stdlib.h>

static int g_failures;
static int g_checks;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define CANVAS_W 765
#define CANVAS_H 503

static uint32_t* g_px;

static uint32_t
at(int x, int y)
{
    if( x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H )
        return 0xDEADBEEFu;
    return g_px[(size_t)y * CANVAS_W + x];
}

static void
draw(int percent)
{
    BootBar_Draw(g_px, CANVAS_W, CANVAS_H, percent);
}

/* Width of the red run along the fill's own middle row. */
static int
fill_run(void)
{
    int bar_x = BootBar_OriginX(CANVAS_W);
    int bar_y = BootBar_OriginY(CANVAS_H);
    int row = bar_y + BOOT_BAR_INSET + BOOT_BAR_FILL_H / 2;
    int run = 0;
    for( int x = bar_x + BOOT_BAR_INSET; x < bar_x + BOOT_BAR_INSET + BOOT_BAR_FILL_W; x++ )
        if( at(x, row) == BOOT_BAR_COLOR )
            run++;
    return run;
}

/* The reference centres the 304x34 track on the canvas with its top 18px
 * above the middle. Both files compute exactly this. */
static void
test_origin(void)
{
    TEST_ASSERT(BootBar_OriginX(CANVAS_W) == CANVAS_W / 2 - 152, "track x is w/2 - 152");
    TEST_ASSERT(BootBar_OriginY(CANVAS_H) == CANVAS_H / 2 - 18, "track y is h/2 - 18");
}

static void
test_track_border(void)
{
    int bx = BootBar_OriginX(CANVAS_W);
    int by = BootBar_OriginY(CANVAS_H);

    draw(0);
    TEST_ASSERT(at(bx, by) == BOOT_BAR_COLOR, "track top-left is red");
    TEST_ASSERT(at(bx + BOOT_BAR_W - 1, by) == BOOT_BAR_COLOR, "track top-right is red");
    TEST_ASSERT(at(bx, by + BOOT_BAR_H - 1) == BOOT_BAR_COLOR, "track bottom-left is red");
    TEST_ASSERT(
        at(bx + BOOT_BAR_W - 1, by + BOOT_BAR_H - 1) == BOOT_BAR_COLOR,
        "track bottom-right is red");

    /* One pixel further out is canvas, so the track really is 304x34 and not
     * a pixel wider. */
    TEST_ASSERT(at(bx - 1, by) == 0x000000u, "nothing left of the track");
    TEST_ASSERT(at(bx + BOOT_BAR_W, by) == 0x000000u, "nothing right of the track");
    TEST_ASSERT(at(bx, by - 1) == 0x000000u, "nothing above the track");
    TEST_ASSERT(at(bx, by + BOOT_BAR_H) == 0x000000u, "nothing below the track");
}

/* The deob rules a black rectangle between the red border and the fill after
 * drawing the fill; that inset is what makes the bar look recessed. */
static void
test_black_inset_rule(void)
{
    int bx = BootBar_OriginX(CANVAS_W);
    int by = BootBar_OriginY(CANVAS_H);

    draw(100);
    TEST_ASSERT(at(bx, by + 10) == BOOT_BAR_COLOR, "outer edge stays red at full");
    TEST_ASSERT(at(bx + 1, by + 10) == 0x000000u, "the rule sits inside the red border");
    TEST_ASSERT(at(bx + 2, by + 10) == BOOT_BAR_COLOR, "the fill starts inside the rule");
    TEST_ASSERT(
        at(bx + BOOT_BAR_W - 2, by + 10) == 0x000000u, "the rule closes on the right too");
}

/* Three pixels per percent over a 300-wide interior: 0 draws none, 100 fills
 * it exactly, and the halves land where arithmetic says. */
static void
test_fill_scales_three_pixels_per_percent(void)
{
    draw(0);
    TEST_ASSERT(fill_run() == 0, "0% fills nothing");

    draw(1);
    TEST_ASSERT(fill_run() == 3, "1% is three pixels");

    draw(50);
    TEST_ASSERT(fill_run() == 150, "50% is half the interior");

    draw(100);
    TEST_ASSERT(fill_run() == BOOT_BAR_FILL_W, "100% fills the interior exactly");
}

/* A percent outside 0..100 must not run the fill past the track and across
 * the canvas -- the boot steps are client-supplied and a future one could. */
static void
test_percent_is_clamped(void)
{
    int bx = BootBar_OriginX(CANVAS_W);
    int by = BootBar_OriginY(CANVAS_H);

    draw(1000);
    TEST_ASSERT(fill_run() == BOOT_BAR_FILL_W, "an over-large percent stops at full");
    TEST_ASSERT(
        at(bx + BOOT_BAR_W, by + 10) == 0x000000u, "an over-large percent stays in the track");

    draw(-5);
    TEST_ASSERT(fill_run() == 0, "a negative percent draws no fill");
}

/* Everything that is not the bar is black: the references clear the canvas on
 * the frame that first shows it. */
static void
test_canvas_is_cleared(void)
{
    int bx = BootBar_OriginX(CANVAS_W);
    int by = BootBar_OriginY(CANVAS_H);
    int stray = 0;

    draw(60);
    for( int y = 0; y < CANVAS_H; y++ )
    {
        for( int x = 0; x < CANVAS_W; x++ )
        {
            int inside = x >= bx && x < bx + BOOT_BAR_W && y >= by && y < by + BOOT_BAR_H;
            if( !inside && at(x, y) != 0x000000u )
                stray++;
        }
    }
    TEST_ASSERT(stray == 0, "nothing is drawn outside the track");
}

/* The caption's baseline is 22 below the track's top, centred on it -- the
 * only two numbers App_Render needs from here to place the text. */
static void
test_caption_anchor(void)
{
    TEST_ASSERT(BOOT_BAR_TEXT_BASELINE == 22, "caption baseline is 22 from the track top");
    TEST_ASSERT(BOOT_BAR_W / 2 == 152, "caption centres on the track's middle");
}

int
main(void)
{
    g_failures = 0;
    g_checks = 0;

    g_px = malloc((size_t)CANVAS_W * CANVAS_H * sizeof(*g_px));
    if( !g_px )
    {
        fprintf(stderr, "FAIL: canvas alloc\n");
        return 1;
    }

    test_origin();
    test_track_border();
    test_black_inset_rule();
    test_fill_scales_three_pixels_per_percent();
    test_percent_is_clamped();
    test_canvas_is_cleared();
    test_caption_anchor();

    free(g_px);

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s) of %d check(s)\n", g_failures, g_checks);
        return 1;
    }
    printf("boot_bar_test: ok (%d checks)\n", g_checks);
    return 0;
}
