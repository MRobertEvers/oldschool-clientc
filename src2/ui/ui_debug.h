#ifndef UI_DEBUG_H
#define UI_DEBUG_H

#include "uitree.h"

#include <stdbool.h>

bool
ui_minimenu_debug_enabled(void);

void
ui_minimenu_debug_log(char const* fmt, ...);

char const*
ui_minimenu_debug_uielem_name(int type);

void
ui_minimenu_debug_log_menu_options(
    char const* tag,
    int component_id,
    int node_idx,
    int type,
    struct StaticUIMenuOptions const* opts,
    int button_type,
    int client_code);

#endif
