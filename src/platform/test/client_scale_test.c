/*
 * Client scaling: layout, render and output sizes.
 *
 * Every lane's present and pointer mapping reads these answers, so a mistake
 * here is a frame drawn in one place and clicked in another, or a GPU buffer
 * allocated at a size nobody asked for.
 *
 * What is asserted:
 *
 *   - keep-aspect is the letterbox every lane used to carry, exactly, across
 *     a sweep of layouts and windows -- the default must move no pixel.
 *   - integer mode rounds a fractional interface scale down, lands on whole
 *     multiples, centres, and falls back to keep-aspect when the window is
 *     smaller than one multiple.
 *   - stretch fills the area.
 *   - the pixel limit caps the 100% buffer inside both axes of the
 *     resolution, and interface scaling divides that (to a whole multiple in
 *     integer mode), so a CPU renderer never draws more than it and every
 *     scale step still draws a different frame.
 *   - the frame floor lowers the percent, evenly, only when the window cannot
 *     hold the frame at it; a window that can is left at the settings.
 *   - HighDPI: device pixels ignores the density; window points multiplies
 *     the percent by it.
 *   - one pipeline: every renderer draws the buffer (the layout) and the
 *     output rectangle is only where it is stretched to.
 *   - output -> layout mapping inverts the placement and clamps.
 *
 * Build and run:
 *   make -C src test-client-scale
 */

#include "platform/client_scale.h"

#include <stdio.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static struct ClientScaleSettings
settings(
    enum ClientScaleFit fit,
    int max_pixel_height)
{
    struct ClientScaleSettings s;
    s.fit = fit;
    s.max_pixel_width = 0;
    s.max_pixel_height = max_pixel_height;
    s.output_filter = CLIENT_SCALE_FILTER_NEAREST;
    s.high_dpi = CLIENT_SCALE_HIGH_DPI_DEVICE_PIXELS;
    s.density_percent = 100;
    return s;
}

/* The pre-module letterbox (platform_sdl2.c letterbox_dst), kept verbatim as
 * the oracle. */
static void
legacy_letterbox(int logical_w, int logical_h, int window_w, int window_h, struct ClientScaleRect* dst)
{
    dst->x = 0;
    dst->y = 0;
    dst->w = logical_w;
    dst->h = logical_h;
    if( window_w <= 0 || window_h <= 0 )
        return;
    float src_aspect = (float)logical_w / (float)logical_h;
    float window_aspect = (float)window_w / (float)window_h;
    if( src_aspect > window_aspect )
    {
        dst->w = window_w;
        dst->h = (int)(window_w / src_aspect);
        dst->x = 0;
        dst->y = (window_h - dst->h) / 2;
    }
    else
    {
        dst->h = window_h;
        dst->w = (int)(window_h * src_aspect);
        dst->y = 0;
        dst->x = (window_w - dst->w) / 2;
    }
}

static void
test_keep_aspect_is_the_legacy_letterbox(void)
{
    struct ClientScaleSettings const s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 0);
    static int const layouts[][2] = { { 765, 503 }, { 807, 503 }, { 1280, 720 }, { 1024, 768 } };
    int mismatches = 0;

    for( unsigned l = 0; l < sizeof(layouts) / sizeof(layouts[0]); l++ )
        for( int w = 1; w <= 3000; w += 37 )
            for( int h = 1; h <= 2000; h += 29 )
            {
                struct ClientScalePresent p;
                struct ClientScaleRect legacy;
                ClientScale_Present(&s, layouts[l][0], layouts[l][1], w, h, &p);
                legacy_letterbox(layouts[l][0], layouts[l][1], w, h, &legacy);
                if( legacy.w < 1 )
                    legacy.w = 1;
                if( legacy.h < 1 )
                    legacy.h = 1;
                if( p.output.x != legacy.x || p.output.y != legacy.y || p.output.w != legacy.w ||
                    p.output.h != legacy.h )
                    mismatches++;
            }
    CHECK(mismatches == 0, "keep-aspect differs from the legacy letterbox %d times", mismatches);
}

static void
test_integer_mode(void)
{
    struct ClientScaleSettings const s = settings(CLIENT_SCALE_FIT_INTEGER, 0);
    struct ClientScalePresent p;
    struct ClientScaleLayout layout;

    CHECK(ClientScale_RoundPercent(&s, 150) == 100, "150%% should round to 100%%");
    CHECK(ClientScale_RoundPercent(&s, 275) == 200, "275%% should round to 200%%");
    CHECK(ClientScale_RoundPercent(&s, 400) == 400, "400%% should stay");

    /* The fixed window's size is rounded; a resizable layout never is. Every
     * step but 100% and 200% used to be refused on a Retina laptop. */
    ClientScale_WindowLayout(&s, 250, 2560, 1440, &layout);
    CHECK(layout.percent == 250 && !layout.rounded_by_integer && !layout.raised_by_limit,
        "integer 250%% stays 250%%, got %d", layout.percent);
    CHECK(layout.w == 1024 && layout.h == 576, "layout %dx%d", layout.w, layout.h);
    ClientScale_Present(&s, layout.w, layout.h, 2560, 1440, &p);
    CHECK(p.output.w == 2048 && p.output.h == 1152 && !p.integer_fell_back,
        "250%% shows at the largest whole multiple, 2x, got %dx%d", p.output.w, p.output.h);

    ClientScale_Present(&s, 765, 503, 1920, 1080, &p);
    CHECK(p.output.w == 1530 && p.output.h == 1006, "2x integer, got %dx%d", p.output.w, p.output.h);
    CHECK(p.output.x == (1920 - 1530) / 2 && p.output.y == (1080 - 1006) / 2, "centred");
    CHECK(!p.integer_fell_back, "a 2x fit should not fall back");

    ClientScale_Present(&s, 765, 503, 700, 500, &p);
    CHECK(p.integer_fell_back, "a window smaller than 1x should fall back");
    CHECK(p.output.w <= 700 && p.output.h <= 500, "fallback fits the area");
}

static void
test_stretch_fills(void)
{
    struct ClientScaleSettings const s = settings(CLIENT_SCALE_FIT_STRETCH, 0);
    struct ClientScalePresent p;

    ClientScale_Present(&s, 765, 503, 1920, 1080, &p);
    CHECK(p.output.x == 0 && p.output.y == 0 && p.output.w == 1920 && p.output.h == 1080,
        "stretch got %d,%d %dx%d", p.output.x, p.output.y, p.output.w, p.output.h);
}

static void
test_limit_raises_the_scale(void)
{
    struct ClientScaleLayout layout;
    struct ClientScaleSettings s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 1080);

    ClientScale_WindowLayout(&s, 100, 3840, 2160, &layout);
    CHECK(layout.percent == 200 && layout.raised_by_limit, "4K at 100%% under 1080 -> 200%%, got %d",
        layout.percent);
    CHECK(layout.h <= 1080, "layout %d must fit the limit", layout.h);

    ClientScale_WindowLayout(&s, 100, 2560, 1600, &layout);
    CHECK(layout.percent == 149 && layout.h <= 1080, "1600 rows under 1080 -> 149%%, got %d (%d rows)",
        layout.percent, layout.h);

    /* The scale divides the LIMITED buffer: 300% of 4K under 1080 rows is a
     * third of 1920x1080, not the same 1280x720 that 300% of 4K alone is. */
    ClientScale_WindowLayout(&s, 300, 3840, 2160, &layout);
    CHECK(layout.percent == 600 && layout.raised_by_limit && layout.w == 640 && layout.h == 360,
        "300%% under a 1080 limit is 640x360, got %dx%d at %d%%", layout.w, layout.h, layout.percent);

    /* Integer mode raises exactly as keep aspect does: whole multiples are
     * the present's, not the percent's. */
    s.fit = CLIENT_SCALE_FIT_INTEGER;
    ClientScale_WindowLayout(&s, 100, 2560, 1600, &layout);
    CHECK(layout.percent == 149 && layout.raised_by_limit, "integer raise is 149%% too, got %d",
        layout.percent);
    ClientScale_WindowLayout(&s, 200, 2560, 1600, &layout);
    CHECK(layout.percent == 298 && layout.h <= 540, "integer 200%% of a 149%% limit is 298%%, got %d",
        layout.percent);
}

static void
test_scale_steps_under_a_limit(void)
{
    struct ClientScaleLayout layout;
    struct ClientScaleSettings s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 768);
    int const scales[] = { 100, 125, 150, 175, 200 };
    int previous_w = 1 << 30;

    /* The reported case: a Retina laptop in window points under 1366x768.
     * Every step of the scale has to draw a different frame. */
    s.max_pixel_width = 1366;
    s.high_dpi = CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS;
    s.density_percent = 200;
    for( int i = 0; i < (int)(sizeof(scales) / sizeof(scales[0])); i++ )
    {
        ClientScale_WindowLayout(&s, scales[i], 3024, 1844, &layout);
        CHECK(layout.w < previous_w && layout.w <= 1366 && layout.h <= 768,
            "%d%% under 1366x768 must shrink the buffer and stay inside it, got %dx%d", scales[i],
            layout.w, layout.h);
        previous_w = layout.w;
    }

    /* The window floor stops where the limit cannot hold the frame: 768 rows
     * show 503 at 152%, so 200% asks for no more window than that. */
    CHECK(ClientScale_WindowFloorPercent(&s, 200, 807, 503) == 304,
        "a 200%% floor under 768 rows is 152%% of points, got %d",
        ClientScale_WindowFloorPercent(&s, 200, 807, 503));
    CHECK(ClientScale_WindowFloorPercent(&s, 125, 807, 503) == 250, "125%% fits: kept");
    s.max_pixel_width = 854;
    s.max_pixel_height = 480;
    CHECK(ClientScale_WindowFloorPercent(&s, 150, 807, 503) == 200,
        "a limit below the frame still owes the frame at 100%%");


}
static void
test_render_buffer(void)
{
    struct ClientScalePresent p;
    struct ClientScaleSettings s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 0);

    /* The buffer is the output: the world is drawn at the window's pixels and
     * the interface is scaled into it, whatever size the layout is. */
    ClientScale_Present(&s, 765, 503, 1920, 1080, &p);
    CHECK(p.output.h == 1080, "the output is the stretch, got %d", p.output.h);
    CHECK(p.render_w == p.output.w && p.render_h == p.output.h,
        "with no limit the buffer is the output, got %dx%d for %dx%d", p.render_w, p.render_h,
        p.output.w, p.output.h);

    /* Window points: the buffer is the output in points. */
    s.high_dpi = CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS;
    s.density_percent = 200;
    ClientScale_Present(&s, 1512, 922, 3024, 1844, &p);
    CHECK(p.render_w == 1512 && p.render_h == 922, "window points halves a 2x output, got %dx%d",
        p.render_w, p.render_h);

    /* The limit shrinks the buffer evenly, never the layout. */
    s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 768);
    s.max_pixel_width = 1366;
    ClientScale_Present(&s, 1008, 614, 3024, 1844, &p);
    CHECK(p.render_w <= 1366 && p.render_h <= 768 && p.render_w >= 1250,
        "a 1366x768 limit caps the buffer, got %dx%d", p.render_w, p.render_h);

    /* Integer: whole multiples all the way down. 4x output under a 720-row
     * limit cannot hold 2x of 503 rows, so the buffer is the layout. */
    s = settings(CLIENT_SCALE_FIT_INTEGER, 720);
    ClientScale_Present(&s, 765, 503, 3840, 2160, &p);
    CHECK(p.output.w == 3060 && p.output.h == 2012, "4x integer output");
    CHECK(p.render_w == 765 && p.render_h == 503, "a limit below 2x keeps the layout's size");
    s = settings(CLIENT_SCALE_FIT_INTEGER, 1080);
    ClientScale_Present(&s, 765, 503, 3840, 2160, &p);
    CHECK(p.render_w == 1530 && p.render_h == 1006, "a 1080 limit holds 2x, which divides 4x, got %dx%d",
        p.render_w, p.render_h);
    s = settings(CLIENT_SCALE_FIT_INTEGER, 0);
    ClientScale_Present(&s, 765, 503, 3840, 2160, &p);
    CHECK(p.render_w == 3060, "no limit: the buffer is the 4x output, got %d", p.render_w);
}

static void
test_resolution_limit(void)
{
    struct ClientScaleLayout layout;
    struct ClientScaleSettings s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 1080);

    /* An ultrawide under 1920x1080: the WIDTH is what is over. */
    s.max_pixel_width = 1920;
    ClientScale_WindowLayout(&s, 100, 3440, 1440, &layout);
    CHECK(layout.raised_by_limit && layout.w <= 1920 && layout.h <= 1080,
        "3440x1440 under 1920x1080 must fit both axes, got %dx%d at %d%%", layout.w, layout.h,
        layout.percent);

    /* The limit is the buffer, whatever the interface scale asked for. */
    s.max_pixel_width = 854;
    s.max_pixel_height = 480;
    ClientScale_WindowLayout(&s, 100, 2384, 1832, &layout);
    CHECK(layout.w <= 854 && layout.h <= 480 && layout.w >= 620,
        "the buffer is inside 854x480 keeping the window's shape, got %dx%d", layout.w, layout.h);
}

static void
test_settings_decide_the_buffer_without_a_floor(void)
{
    struct ClientScaleLayout layout;
    struct ClientScaleSettings s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 0);

    /* 200% in a 1192x916 game area is a 596x458 buffer: WindowLayout applies
     * no floor. @see test_frame_floor for what the client layers on top. */
    ClientScale_WindowLayout(&s, 200, 1192, 916, &layout);
    CHECK(layout.percent == 200 && layout.w == 596 && layout.h == 458,
        "200%% of 1192x916 is 596x458, got %dx%d at %d%%", layout.w, layout.h, layout.percent);

    s.fit = CLIENT_SCALE_FIT_INTEGER;
    ClientScale_WindowLayout(&s, 250, 2384, 1832, &layout);
    CHECK(layout.percent == 250 && layout.w == 953, "integer 250%% stays 250%%, got %d", layout.percent);
}

static void
test_frame_floor(void)
{
    struct ClientScaleLayout layout;
    struct ClientScaleSettings s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 0);

    /* The reported case: 200% on a Retina display (400% of drawable pixels)
     * in a 1532x1000 game area was a 383x250 buffer, and the 765x503 login
     * screen showed a quarter of itself. */
    s.high_dpi = CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS;
    s.density_percent = 200;
    ClientScale_WindowLayout(&s, 200, 1532, 1000, &layout);
    ClientScale_LowerToFloor(&s, 765, 503, 1532, 1000, &layout);
    CHECK(layout.lowered_to_fit && layout.w >= 765 && layout.h >= 503,
        "a window too small for 200%% lowers the scale to hold the frame, got %dx%d at %d%%",
        layout.w, layout.h, layout.percent);
    CHECK(layout.percent == 198 && layout.shown_percent == 99,
        "lowered evenly to the largest that fits, got %d%% (shown %d)", layout.percent,
        layout.shown_percent);

    /* A window grown to hold it keeps the chosen scale. */
    ClientScale_WindowLayout(&s, 200, 3228, 2012, &layout);
    ClientScale_LowerToFloor(&s, 807, 503, 3228, 2012, &layout);
    CHECK(!layout.lowered_to_fit && layout.percent == 400 && layout.w == 807,
        "a window holding the frame keeps 200%%, got %dx%d at %d%%", layout.w, layout.h,
        layout.percent);

    /* Integer mode lowers exactly as far as the frame needs, as the official
     * client does; it used to drop to a whole 100 on top. */
    s = settings(CLIENT_SCALE_FIT_INTEGER, 0);
    ClientScale_WindowLayout(&s, 300, 2000, 1400, &layout);
    ClientScale_LowerToFloor(&s, 807, 503, 2000, 1400, &layout);
    CHECK(layout.lowered_to_fit && layout.percent == 247 && layout.w >= 807,
        "integer 300%% in a 2000x1400 window lowers to 247%%, got %d", layout.percent);

    /* The screenshot: 375% of window points, integer, on a 1192x917-point
     * Retina window. Was lowered to 100%; the frame fits at 147%. */
    s = settings(CLIENT_SCALE_FIT_INTEGER, 0);
    s.high_dpi = CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS;
    s.density_percent = 200;
    ClientScale_WindowLayout(&s, 375, 2384, 1834, &layout);
    ClientScale_LowerToFloor(&s, 807, 503, 2384, 1834, &layout);
    CHECK(layout.shown_percent == 147 && layout.w >= 807 && layout.h >= 503,
        "375%% on a 1192x917 window lowers only to 147%%, got %d%% (%dx%d)", layout.shown_percent,
        layout.w, layout.h);
    ClientScale_WindowLayout(&s, 125, 2384, 1834, &layout);
    ClientScale_LowerToFloor(&s, 807, 503, 2384, 1834, &layout);
    CHECK(!layout.lowered_to_fit && layout.shown_percent == 125, "125%% fits and is kept, got %d%%",
        layout.shown_percent);

    /* Smaller than the frame at 100%: the buffer is the frame, shrunk to fit. */
    s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 0);
    ClientScale_WindowLayout(&s, 100, 600, 400, &layout);
    ClientScale_LowerToFloor(&s, 765, 503, 600, 400, &layout);
    CHECK(layout.w >= 765 && layout.h >= 503, "never below the frame, got %dx%d", layout.w,
        layout.h);
}

static void
test_high_dpi(void)
{
    struct ClientScaleLayout layout;
    struct ClientScaleSettings s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 0);

    s.density_percent = 200;
    ClientScale_WindowLayout(&s, 100, 2400, 1600, &layout);
    CHECK(layout.percent == 100 && layout.w == 2400, "device pixels ignores the density");

    s.high_dpi = CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS;
    ClientScale_WindowLayout(&s, 150, 2400, 1600, &layout);
    CHECK(layout.percent == 300 && layout.shown_percent == 150 && layout.w == 800,
        "window points: 150%% on 2x is 300%% of drawable pixels, got %d (shown %d)",
        layout.percent, layout.shown_percent);
    ClientScale_WindowLayout(&s, 100, 2400, 1600, &layout);
    CHECK(layout.w == 1200 && layout.h == 800, "window points lays out at points");

}

static void
test_mapping_round_trip(void)
{
    struct ClientScaleSettings const s = settings(CLIENT_SCALE_FIT_STRETCH, 0);
    struct ClientScalePresent p;
    int x;
    int y;

    ClientScale_Present(&s, 765, 503, 1530, 1006, &p);
    ClientScale_OutputToLayout(&p.output, 765, 503, 20, 40, &x, &y);
    CHECK(x == 10 && y == 20, "2x maps to half, got %d,%d", x, y);
    ClientScale_OutputToLayout(&p.output, 765, 503, -5, 99999, &x, &y);
    CHECK(x == 0 && y == 502, "clamped, got %d,%d", x, y);
}

int
main(void)
{
    test_keep_aspect_is_the_legacy_letterbox();
    test_integer_mode();
    test_stretch_fills();
    test_limit_raises_the_scale();
    test_scale_steps_under_a_limit();
    test_render_buffer();
    test_resolution_limit();
    test_settings_decide_the_buffer_without_a_floor();
    test_frame_floor();
    test_high_dpi();
    test_mapping_round_trip();

    if( g_failures )
    {
        printf("client_scale_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("client_scale_test: OK\n");
    return 0;
}
