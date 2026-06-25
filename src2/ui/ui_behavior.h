#ifndef UI_BEHAVIOR_H
#define UI_BEHAVIOR_H

#include "ui_input.h"
#include "vm/csvm.h"

#include <stdbool.h>
#include <stdint.h>

struct VarPVarBitManager;

struct UITreeBehaviorHost
{
    struct CSVM* csvm;
    struct CSVM_State csvm_state;
    struct VarPVarBitManager* varp_varbit;
};

bool
uitree_component_is_clickable(struct StaticUIComponent const* component);

bool
uitree_component_visible(
    struct StaticUIComponent const* component,
    int32_t component_index,
    int32_t hovered_component);

int
uitree_component_rect_color(
    struct StaticUIComponent const* component,
    int32_t component_index,
    int32_t hovered_component,
    struct UITreeBehaviorHost const* host,
    int base_color);

bool
uitree_behavior_is_active(
    struct UITreeBehaviorHost const* host,
    struct StaticUIBehavior const* behavior);

void
uitree_behavior_apply_button_click(
    struct UITreeBehaviorHost* host,
    struct StaticUIBehavior const* behavior);

void
uitree_behavior_handle_input_result(
    struct UITreeBehaviorHost* host,
    struct UITree const* tree,
    struct UIInputResult const* result);

#endif
