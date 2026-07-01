#include "ui/ui_cross_cursor.h"

#include <stdio.h>

#define TEST_ASSERT(cond, msg) \
    do \
    { \
        if( !(cond) ) \
        { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
    } while( 0 )

static int
test_show_and_tick(void)
{
    struct UICrossCursorState state;
    ui_cross_cursor_reset(&state);
    TEST_ASSERT(!ui_cross_cursor_is_active(&state), "inactive after reset");

    ui_cross_cursor_show(&state, UI_CROSS_MODE_WALK, 100, 200);
    TEST_ASSERT(ui_cross_cursor_is_active(&state), "active after show");
    TEST_ASSERT(state.x == 100 && state.y == 200, "position set");

    ui_cross_cursor_tick(&state, 20);
    TEST_ASSERT(state.cycle == 20, "cycle advanced");
    TEST_ASSERT(ui_cross_cursor_atlas_frame(&state) == 0, "walk frame phase 0");

    ui_cross_cursor_tick(&state, 380);
    TEST_ASSERT(!ui_cross_cursor_is_active(&state), "decays to off at 400");
    return 0;
}

static int
test_interact_frames(void)
{
    struct UICrossCursorState state;
    ui_cross_cursor_show(&state, UI_CROSS_MODE_INTERACT, 0, 0);
    state.cycle = 200;
    TEST_ASSERT(ui_cross_cursor_atlas_frame(&state) == 6, "interact frame offset +4");
    return 0;
}

int
main(void)
{
    int failures = 0;
    failures += test_show_and_tick();
    failures += test_interact_frames();

    if( failures == 0 )
    {
        printf("All ui_cross_cursor tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
