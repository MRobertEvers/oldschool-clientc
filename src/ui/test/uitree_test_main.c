#include "test_harness.h"

int g_failures;

int
main(void)
{
    test_dirty_marking();
    test_walk_topology();
    test_mounted_world_resize();
    test_hover_input();
    test_click_event_coords();
    test_pointer_owner_blocks_tree();
    test_layout_build();
    test_mutate_emit();
    test_apply_object_silhouette();
    test_drag_composite();
    test_drag_scrollbar_ondrag_held();
    test_drag_scrollbar_inplace_emit();
    test_drag_scrollbar_137_geometry();
    test_drag_cc_dragpickup_seeds();
    test_press_repeat_and_release();
    test_frame_hidden_cancels_active_input();
    test_scroll_hit();
    test_wheel_stops_at_interface();
    test_drag_scrolled();
    test_emit_icons();
    test_emit_stack_count_zero();
    test_emit_stack_count_placeholder();
    test_emit_golden();
    test_key_dispatch();
    test_same_frame_press_release_clicks();
    test_minimenu();
    test_id_index();
    test_child_subid();
    test_menu_submenus();
    test_component_params();
    test_inkwell_spec_copy();
    test_open_close_steady();
    test_mounted_component_inherits_container_hidden();
    test_clear_hooks_preserves_sibling_on_op();
    test_mount_slot_reclaim_no_shadow_text();
    test_live_node_sets();
    test_debug_overlay();
    test_chrome_exec();
    test_entity_overlay_draw_order();
    test_scripted_entity_overlay();
    test_scripted_entity_overlay_clipped();
    test_scripted_overlay_arc();
    test_server_driven_viewport_widgets();
    test_frame_replacement();
    test_roles();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    printf("All UITree tests passed.\n");
    return 0;
}
