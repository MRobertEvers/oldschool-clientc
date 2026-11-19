/*
 * What the client keeps about its canvas scaling (ui/canvas_scale.h).
 *
 * The window that must be reported before a setting can be relaid out, the
 * density default, the changes that report themselves, the fixed window's
 * stated layout and the readout taken from it, and the one-time restatement
 * of the lane's window mode.
 *
 * Run: make -C src test-canvas-scale
 */

#include "ui/canvas_scale.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_a_window_must_be_reported_first(void)
{
    struct UICanvasScale scale;

    memset(&scale, 0, sizeof(scale));
    TEST_ASSERT(!UICanvasScale_HasWindow(&scale), "before the first resize there is nothing to divide");
    UICanvasScale_SetWindow(&scale, 1600, 900);
    TEST_ASSERT(UICanvasScale_HasWindow(&scale), "a reported window can be divided");
    TEST_ASSERT(scale.window_w == 1600 && scale.window_h == 900, "and is kept unscaled");
}

static void
test_the_density_defaults_to_one(void)
{
    struct UICanvasScale scale;

    memset(&scale, 0, sizeof(scale));
    TEST_ASSERT(UICanvasScale_DensityPercent(&scale) == 100, "no report reads as 1x");
    TEST_ASSERT(UICanvasScale_SetDensity(&scale, 200), "a new density is a change");
    TEST_ASSERT(UICanvasScale_DensityPercent(&scale) == 200, "and is what is read back");
    TEST_ASSERT(!UICanvasScale_SetDensity(&scale, 200), "the same density twice changes once");
}

static void
test_high_dpi_auto_reports_a_change_only(void)
{
    struct UICanvasScale scale;

    memset(&scale, 0, sizeof(scale));
    TEST_ASSERT(UICanvasScale_HighDpiAuto(&scale) == CLIENT_SCALE_HIGH_DPI_DEVICE_PIXELS,
        "automatic starts as device pixels");
    TEST_ASSERT(!UICanvasScale_SetHighDpiAuto(&scale, CLIENT_SCALE_HIGH_DPI_DEVICE_PIXELS),
        "restating it is no change");
    TEST_ASSERT(UICanvasScale_SetHighDpiAuto(&scale, CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS),
        "a different meaning is");
    TEST_ASSERT(UICanvasScale_HighDpiAuto(&scale) == CLIENT_SCALE_HIGH_DPI_WINDOW_POINTS, "and is kept");
}

static void
test_a_fixed_window_states_its_layout(void)
{
    struct UICanvasScale scale;
    struct ClientScalePresent present;

    memset(&scale, 0, sizeof(scale));
    memset(&present, 0, sizeof(present));
    present.render_w = 1530;
    present.render_h = 1006;
    TEST_ASSERT(!scale.present_known, "nothing has been presented yet");
    UICanvasScale_SetPresent(&scale, &present);
    TEST_ASSERT(scale.present_known && scale.present.render_w == 1530, "the present is recorded");

    scale.layout.raised_by_limit = 1;
    scale.layout.lowered_to_fit = 1;
    UICanvasScale_SetFixedLayout(&scale, 765, 503, 200, 250);
    TEST_ASSERT(scale.layout.w == 765 && scale.layout.h == 503, "the pinned frame");
    TEST_ASSERT(scale.layout.percent == 200 && scale.layout.shown_percent == 200, "at the rounded percent");
    TEST_ASSERT(scale.layout.rounded_by_integer, "which says it was rounded");
    TEST_ASSERT(!scale.layout.raised_by_limit && !scale.layout.lowered_to_fit,
        "neither the limit nor the floor touches a fixed window");
    TEST_ASSERT(scale.shown_percent == 0, "the readout waits to be taken");
    UICanvasScale_TakeShownPercent(&scale);
    TEST_ASSERT(scale.shown_percent == 200, "and reads the layout's percent");

    UICanvasScale_SetFixedLayout(&scale, 765, 503, 150, 150);
    TEST_ASSERT(!scale.layout.rounded_by_integer, "a percent that needed no rounding says so");
}

static void
test_the_window_mode_is_restated_once(void)
{
    struct UICanvasScale scale;

    memset(&scale, 0, sizeof(scale));
    TEST_ASSERT(!UICanvasScale_NativeWindowModeSynced(&scale), "not restated at boot");
    UICanvasScale_MarkNativeWindowModeSynced(&scale);
    TEST_ASSERT(UICanvasScale_NativeWindowModeSynced(&scale), "and never again once it has been");
}

int
main(void)
{
    test_a_window_must_be_reported_first();
    test_the_density_defaults_to_one();
    test_high_dpi_auto_reports_a_change_only();
    test_a_fixed_window_states_its_layout();
    test_the_window_mode_is_restated_once();
    if( g_failures )
    {
        printf("canvas_scale: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("canvas_scale: all checks passed\n");
    return 0;
}
