#include "test_harness.h"
#include <stdlib.h>

void test_plugin_contract_native(void);
void test_native_geometry_audit(void);
void test_native_object_swap_state(void);
void test_native_hook_slot_ownership(void);
void test_retained_operation_state(void);
void test_live_widget_geometry(void);
void test_plugin_contract_copy(void);

int g_failures;

int
main(void)
{
    if( getenv("TORIRS_TEST_CONTRACT_V3") )
    {
        test_plugin_contract_native();
    test_native_geometry_audit();
    test_native_object_swap_state();
    test_native_hook_slot_ownership();
    test_retained_operation_state();
    test_live_widget_geometry();
        test_plugin_contract_copy();
        return g_failures ? 1 : 0;
    }
    test_plugin_contract_native();
    test_native_geometry_audit();
    test_native_object_swap_state();
    test_native_hook_slot_ownership();
    test_retained_operation_state();
    test_live_widget_geometry();
    test_plugin_contract_copy();
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
    test_input_field();
    test_same_frame_press_release_clicks();
    test_touch_swipe_scrolls_layer();
    test_feedback_overlay_never_takes_a_click();
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
    test_chrome_panel_draw();
    test_chrome_shell();
    test_entity_overlay_draw_order();
    test_scripted_entity_overlay();
    test_scripted_entity_overlay_clipped();
    test_scripted_overlay_arc();
    test_server_driven_viewport_widgets();
    test_frame_replacement();
    test_frame_authored_metadata();
    test_frame_declared_depth();
    test_roles();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }

    printf("All UITree tests passed.\n");
    return 0;
}
