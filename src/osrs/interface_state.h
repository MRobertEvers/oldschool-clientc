#ifndef OSRS_INTERFACE_STATE_H
#define OSRS_INTERFACE_STATE_H

#include <stddef.h>

#define MAX_IFACE_SCROLL_IDS 8192

struct InterfaceState
{
    int tab_interface_id[14];
    int selected_tab;

    int sidebar_interface_id;
    int viewport_interface_id;
    int chat_interface_id;

    int component_scroll_position[MAX_IFACE_SCROLL_IDS];

    int current_hovered_interface_id;

    /** Client.ts overMainComId / overSideComId / overChatComId (addComponentOptions regions). */
    int over_main_com_id;
    int over_side_com_id;
    int over_chat_com_id;

    int selected_area;
    int selected_item;
    int selected_interface;
    int selected_cycle;

    /** Client.ts useMode / targetMode + spell target (inventory right-click). */
    int inv_use_mode;
    int inv_target_mode;
    int inv_sel_obj_id;
    int inv_sel_slot;
    int inv_sel_comp_id;
    char inv_sel_obj_name[64];
    int inv_target_src_comp_id;
    int inv_target_mask;
    char inv_target_op[128];

    /** Client.ts objDragArea / objDragCycles / objGrabThreshold — inventory drag-drop. */
    int inv_drag_area;
    int inv_drag_comp_id;
    int inv_drag_slot;
    int inv_drag_cycles;
    int inv_drag_grab_threshold;
    int inv_drag_grab_x;
    int inv_drag_grab_y;
    /** Last buffer coords while cursor was inside the game rect; used when cursor maps to -1. */
    int inv_drag_last_good_x;
    int inv_drag_last_good_y;
    /** Suppress duplicate primary from TORIRSEV2_CLICK after drag mouse-up handled it. */
    int inv_suppress_next_left_click;
};

struct InterfaceState*
interface_state_new(void);

void
interface_state_free(struct InterfaceState* iface);

#endif
