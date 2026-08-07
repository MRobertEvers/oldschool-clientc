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
    test_drag_scrollbar_ondrag_held();
    test_drag_scrollbar_inplace_emit();
    test_drag_scrollbar_137_geometry();
    test_drag_cc_dragpickup_seeds();
    test_scroll_hit();
    test_drag_scrolled();
    test_emit_icons();
    test_emit_golden();
    test_key_dispatch();
    test_minimenu();
    test_id_index();
    test_child_subid();
    test_menu_submenus();
    test_component_params();
    test_open_close_steady();
    test_clear_hooks_preserves_sibling_on_op();
    test_chatmodal_reclaim_no_shadow_text();
    test_live_node_sets();
    test_debug_overlay();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    printf("All UITree tests passed.\n");
    return 0;
}
