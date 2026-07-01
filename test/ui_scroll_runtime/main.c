#include "ui/ui_scroll_runtime.h"

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
test_reset_and_view(void)
{
    struct UIScrollRuntime rt;
    ui_scroll_runtime_reset(&rt);
    rt.scroll_y[10] = 99;

    struct UITreeScrollState view = ui_scroll_runtime_view(&rt);
    TEST_ASSERT(view.scroll_y != NULL && view.scroll_y[10] == 99, "view aliases scroll_y");
    TEST_ASSERT(rt.scrollbar0_scene_id == -1, "scrollbar ids reset");
    return 0;
}

static int
test_drag_lifecycle(void)
{
    struct UIScrollRuntime rt;
    ui_scroll_runtime_reset(&rt);

    ui_scroll_runtime_begin_drag(&rt, 50, UITREE_SCROLLBAR_V_GRIP, 12);
    TEST_ASSERT(ui_scroll_runtime_is_grip_dragging(&rt), "grip dragging");
    TEST_ASSERT(rt.drag_layer_id == 50 && rt.grabbed, "drag state set");

    ui_scroll_runtime_end_drag(&rt);
    TEST_ASSERT(!ui_scroll_runtime_is_grip_dragging(&rt), "drag ended");
    TEST_ASSERT(rt.drag_layer_id == -1, "layer cleared");
    return 0;
}

int
main(void)
{
    int failures = 0;
    failures += test_reset_and_view();
    failures += test_drag_lifecycle();

    if( failures == 0 )
    {
        printf("All ui_scroll_runtime tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
