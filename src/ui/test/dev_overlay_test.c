/*
 * The developer overlay's record (ui/dev_overlay.h).
 *
 * A created overlay is zeroed: no panel, hidden, an empty frame-time ring.
 * The frame-time row reads milliseconds to two decimals, truncated, and "--"
 * for a mean of 0; a short buffer is still terminated.
 *
 * Run: make -C src test-dev-overlay
 */

#include "ui/dev_overlay.h"

#include "mem/torirs_mem.h"

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
test_create_is_zeroed(void)
{
    struct DevOverlay* overlay = DevOverlay_Create();

    TEST_ASSERT(overlay != NULL, "create hands back a record");
    TEST_ASSERT(overlay->panel == 0, "no panel built");
    TEST_ASSERT(overlay->frame_row == 0, "no row built");
    TEST_ASSERT(overlay->visible == 0, "hidden");
    TEST_ASSERT(overlay->frame_times.count == 0, "the ring is empty");
    TEST_ASSERT(FrameTimeRing_MeanUs(&overlay->frame_times) == 0, "an empty ring has no mean");
    TEST_ASSERT(overlay->ui.panel_count == 0, "the chrome is not initialised here");
    DevOverlay_Free(overlay);
    DevOverlay_Free(NULL);
}

static void
test_frame_time_text(void)
{
    char text[32];
    char small[4];

    DevOverlay_FrameTimeText(0, text, sizeof(text));
    TEST_ASSERT(strcmp(text, "--") == 0, "a mean of 0 reads --");
    DevOverlay_FrameTimeText(3456, text, sizeof(text));
    TEST_ASSERT(strcmp(text, "3.45 ms") == 0, "two decimals, truncated");
    DevOverlay_FrameTimeText(16000, text, sizeof(text));
    TEST_ASSERT(strcmp(text, "16.00 ms") == 0, "whole milliseconds keep both decimals");
    DevOverlay_FrameTimeText(9, text, sizeof(text));
    TEST_ASSERT(strcmp(text, "0.00 ms") == 0, "a sub-10us mean that is not 0 still prints");
    DevOverlay_FrameTimeText(123456, small, sizeof(small));
    TEST_ASSERT(strcmp(small, "123") == 0, "a short buffer is cut and terminated");
}

int
main(void)
{
    ToriMem_Init();
    test_create_is_zeroed();
    test_frame_time_text();
    ToriMem_Shutdown();
    if( g_failures )
    {
        printf("dev_overlay_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("dev_overlay_test: ok\n");
    return 0;
}
