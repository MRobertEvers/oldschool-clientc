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
 *   - the pixel limit: ENLARGE_INTERFACE raises the percent until the layout
 *     fits (to a whole multiple in integer mode); KEEP_INTERFACE leaves it.
 *   - the render buffer: a CPU renderer is always the layout; a GPU renderer
 *     is the output, capped by the limit and never below the layout, and on
 *     the layout's grid in integer mode.
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
    int max_pixel_height,
    enum ClientScaleLimitPolicy policy)
{
    struct ClientScaleSettings s;
    s.fit = fit;
    s.max_pixel_height = max_pixel_height;
    s.limit_policy = policy;
    s.output_filter = CLIENT_SCALE_FILTER_NEAREST;
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
    struct ClientScaleSettings const s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 0, 0);
    static int const layouts[][2] = { { 765, 503 }, { 807, 503 }, { 1280, 720 }, { 1024, 768 } };
    int mismatches = 0;

    for( unsigned l = 0; l < sizeof(layouts) / sizeof(layouts[0]); l++ )
        for( int w = 1; w <= 3000; w += 37 )
            for( int h = 1; h <= 2000; h += 29 )
            {
                struct ClientScalePresent p;
                struct ClientScaleRect legacy;
                ClientScale_Present(&s, layouts[l][0], layouts[l][1], w, h, 0, &p);
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
    struct ClientScaleSettings const s = settings(CLIENT_SCALE_FIT_INTEGER, 0, 0);
    struct ClientScalePresent p;
    struct ClientScaleLayout layout;

    CHECK(ClientScale_RoundPercent(&s, 150) == 100, "150%% should round to 100%%");
    CHECK(ClientScale_RoundPercent(&s, 275) == 200, "275%% should round to 200%%");
    CHECK(ClientScale_RoundPercent(&s, 400) == 400, "400%% should stay");

    ClientScale_WindowLayout(&s, 250, 2560, 1440, &layout);
    CHECK(layout.percent == 200 && layout.rounded_by_integer && !layout.raised_by_limit,
        "integer 250%% -> 200%%, got %d", layout.percent);
    CHECK(layout.w == 1280 && layout.h == 720, "layout %dx%d", layout.w, layout.h);

    ClientScale_Present(&s, 765, 503, 1920, 1080, 0, &p);
    CHECK(p.output.w == 1530 && p.output.h == 1006, "2x integer, got %dx%d", p.output.w, p.output.h);
    CHECK(p.output.x == (1920 - 1530) / 2 && p.output.y == (1080 - 1006) / 2, "centred");
    CHECK(!p.integer_fell_back, "a 2x fit should not fall back");

    ClientScale_Present(&s, 765, 503, 700, 500, 0, &p);
    CHECK(p.integer_fell_back, "a window smaller than 1x should fall back");
    CHECK(p.output.w <= 700 && p.output.h <= 500, "fallback fits the area");
}

static void
test_stretch_fills(void)
{
    struct ClientScaleSettings const s = settings(CLIENT_SCALE_FIT_STRETCH, 0, 0);
    struct ClientScalePresent p;

    ClientScale_Present(&s, 765, 503, 1920, 1080, 0, &p);
    CHECK(p.output.x == 0 && p.output.y == 0 && p.output.w == 1920 && p.output.h == 1080,
        "stretch got %d,%d %dx%d", p.output.x, p.output.y, p.output.w, p.output.h);
}

static void
test_limit_policies(void)
{
    struct ClientScaleLayout layout;
    struct ClientScaleSettings s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 1080, CLIENT_SCALE_LIMIT_ENLARGE_INTERFACE);

    ClientScale_WindowLayout(&s, 100, 3840, 2160, &layout);
    CHECK(layout.percent == 200 && layout.raised_by_limit, "4K at 100%% under 1080 -> 200%%, got %d",
        layout.percent);
    CHECK(layout.h <= 1080, "layout %d must fit the limit", layout.h);

    ClientScale_WindowLayout(&s, 100, 2560, 1600, &layout);
    CHECK(layout.percent == 149 && layout.h <= 1080, "1600 rows under 1080 -> 149%%, got %d (%d rows)",
        layout.percent, layout.h);

    ClientScale_WindowLayout(&s, 300, 3840, 2160, &layout);
    CHECK(layout.percent == 300 && !layout.raised_by_limit, "already under the limit: untouched");

    s.fit = CLIENT_SCALE_FIT_INTEGER;
    ClientScale_WindowLayout(&s, 100, 2560, 1600, &layout);
    CHECK(layout.percent == 200 && layout.raised_by_limit, "integer raise lands on 200%%, got %d",
        layout.percent);

    s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 1080, CLIENT_SCALE_LIMIT_KEEP_INTERFACE);
    ClientScale_WindowLayout(&s, 100, 3840, 2160, &layout);
    CHECK(layout.percent == 100 && !layout.raised_by_limit && layout.h == 2160,
        "KEEP_INTERFACE leaves the layout alone");
}

static void
test_render_buffer(void)
{
    struct ClientScalePresent p;
    struct ClientScaleSettings s = settings(CLIENT_SCALE_FIT_KEEP_ASPECT, 0, 0);

    ClientScale_Present(&s, 765, 503, 1920, 1080, 0, &p);
    CHECK(p.render_w == 765 && p.render_h == 503, "a CPU renderer renders the layout");

    ClientScale_Present(&s, 765, 503, 1920, 1080, 1, &p);
    CHECK(p.render_w == p.output.w && p.render_h == p.output.h, "an uncapped GPU renderer renders the output");

    s.max_pixel_height = 720;
    ClientScale_Present(&s, 765, 503, 1920, 1080, 1, &p);
    CHECK(p.render_h == 720 && p.render_w < p.output.w, "capped to 720 rows, got %dx%d", p.render_w,
        p.render_h);

    s.limit_policy = CLIENT_SCALE_LIMIT_KEEP_INTERFACE;
    s.max_pixel_height = 480;
    ClientScale_Present(&s, 1280, 720, 1920, 1080, 1, &p);
    CHECK(p.render_h == 720, "never below the layout, got %d", p.render_h);

    s = settings(CLIENT_SCALE_FIT_INTEGER, 1100, 0);
    ClientScale_Present(&s, 765, 503, 3840, 2160, 1, &p);
    CHECK(p.output.w == 3060 && p.output.h == 2012, "4x integer output");
    CHECK(p.render_w == 1530 && p.render_h == 1006, "capped integer render stays on the grid, got %dx%d",
        p.render_w, p.render_h);
}

static void
test_mapping_round_trip(void)
{
    struct ClientScaleSettings const s = settings(CLIENT_SCALE_FIT_STRETCH, 0, 0);
    struct ClientScalePresent p;
    int x;
    int y;

    ClientScale_Present(&s, 765, 503, 1530, 1006, 0, &p);
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
    test_limit_policies();
    test_render_buffer();
    test_mapping_round_trip();

    if( g_failures )
    {
        printf("client_scale_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("client_scale_test: OK\n");
    return 0;
}
