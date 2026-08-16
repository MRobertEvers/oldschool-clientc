#include "ui/ui_hover_routing.h"

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
test_begin_commit_dirty(void)
{
    struct UIHoverRouting routing;
    ui_hover_routing_reset(&routing);

    ui_hover_routing_begin_frame(&routing);
    routing.over_main_com_id = 10;
    TEST_ASSERT(ui_hover_routing_commit_frame(&routing), "first commit dirty");
    TEST_ASSERT(!ui_hover_routing_commit_frame(&routing), "unchanged not dirty");

    routing.over_side_com_id = 20;
    TEST_ASSERT(ui_hover_routing_commit_frame(&routing), "side change dirty");
    TEST_ASSERT(routing.over_side_com_id_prev == 20, "prev updated");
    return 0;
}

static int
test_to_ids(void)
{
    struct UIHoverRouting routing;
    ui_hover_routing_reset(&routing);
    routing.over_main_com_id = 1;
    routing.over_chat_com_id = 3;

    struct UITreeHoverIds ids = ui_hover_routing_to_ids(&routing);
    TEST_ASSERT(ids.main_com_id == 1 && ids.side_com_id == -1 && ids.chat_com_id == 3, "ids map");
    return 0;
}

int
main(void)
{
    int failures = 0;
    failures += test_begin_commit_dirty();
    failures += test_to_ids();

    if( failures == 0 )
    {
        printf("All ui_hover_routing tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
