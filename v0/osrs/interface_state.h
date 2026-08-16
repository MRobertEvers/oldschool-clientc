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

    int selected_area;
    int selected_item;
    int selected_interface;
    int selected_cycle;
};

struct InterfaceState*
interface_state_new(void);

void
interface_state_free(struct InterfaceState* iface);

#endif
