#include "interface_state.h"

#include <stdlib.h>
#include <string.h>

struct InterfaceState*
interface_state_new(void)
{
    struct InterfaceState* iface = calloc(1, sizeof(struct InterfaceState));
    if( !iface )
        return NULL;

    for( int i = 0; i < 14; i++ )
        iface->tab_interface_id[i] = -1;
    iface->selected_tab = 3;
    iface->sidebar_interface_id = -1;
    iface->viewport_interface_id = -1;
    iface->chat_interface_id = -1;
    iface->current_hovered_interface_id = -1;
    iface->over_main_com_id = -1;
    iface->over_side_com_id = -1;
    iface->over_chat_com_id = -1;
    iface->selected_area = 0;
    iface->selected_item = 0;
    iface->selected_interface = -1;
    iface->selected_cycle = 0;
    iface->inv_use_mode          = 0;
    iface->inv_target_mode       = 0;
    iface->inv_sel_obj_id        = 0;
    iface->inv_sel_slot          = 0;
    iface->inv_sel_comp_id       = -1;
    iface->inv_sel_obj_name[0]   = '\0';
    iface->inv_target_src_comp_id = -1;
    iface->inv_target_mask      = 0;
    iface->inv_target_op[0]     = '\0';
    iface->inv_drag_area           = 0;
    iface->inv_drag_comp_id        = -1;
    iface->inv_drag_slot           = -1;
    iface->inv_drag_cycles         = 0;
    iface->inv_drag_grab_threshold = 0;
    iface->inv_drag_grab_x         = 0;
    iface->inv_drag_grab_y         = 0;
    iface->inv_drag_last_good_x    = -1;
    iface->inv_drag_last_good_y    = -1;
    iface->inv_suppress_next_left_click = 0;
    return iface;
}

void
interface_state_free(struct InterfaceState* iface)
{
    free(iface);
}
