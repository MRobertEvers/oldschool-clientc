#ifndef UI_DEBUG_H
#define UI_DEBUG_H

#include "minimenu_pickset.h"
#include "uitree.h"

#include <stdbool.h>

struct ToriAuxLibCore_Objtype;

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

void
ui_minimenu_debug_log_inv_slot_ops(
    char const* tag,
    struct MinimenuPick const* pick,
    struct StaticUIComponent const* inv_component,
    struct ToriAuxLibCore_Objtype const* obj,
    int menu_rows_added,
    int selected_action,
    int selected_action_index);

#endif
