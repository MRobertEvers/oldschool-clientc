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
    iface->selected_area = 0;
    iface->selected_item = 0;
    iface->selected_interface = -1;
    iface->selected_cycle = 0;
    return iface;
}

void
interface_state_free(struct InterfaceState* iface)
{
    free(iface);
}
