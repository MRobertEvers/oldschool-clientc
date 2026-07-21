#include "test_harness.h"

int g_failures;

int
main(void)
{
    test_dirty_marking();
    test_walk_topology();
    test_hover_input();
    test_layout_build();
    test_mutate_emit();
    test_apply_object_silhouette();
    test_drag_composite();
    test_scroll_hit();
    test_drag_scrolled();
    test_emit_icons();
    test_emit_golden();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    printf("All UITree tests passed.\n");
    return 0;
}
