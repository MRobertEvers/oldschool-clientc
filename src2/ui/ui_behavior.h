#ifndef UI_BEHAVIOR_H
#define UI_BEHAVIOR_H

#include "ui_input.h"
#include "vm/cs2vm.h"
#include "vm/csvm.h"

#include <stdbool.h>
#include <stdint.h>

struct UITree;
struct UITreeHost;

struct VarPVarBitManager;
struct ToriAuxLibCore;
struct ToriAuxLibCache;
struct ToriAuxLibCore_Component;

struct UITreeBehaviorHost
{
    struct CSVM* csvm;
    struct CSVM_State csvm_state;
    struct CS2VM* cs2vm;
    struct CS2VM_State cs2vm_state;
    struct VarPVarBitManager* varp_varbit;
};

bool
uitree_component_is_clickable(struct StaticUIComponent const* component);

bool
uitree_component_has_menu_options(struct StaticUIComponent const* component);

bool
uitree_component_expects_minimenu_rows(struct StaticUIComponent const* component);

bool
uitree_component_visible_by_id(
    struct StaticUIComponent const* component,
    int hovered_component_id);

bool
uitree_component_visible_by_hover_ids(
    struct StaticUIComponent const* component,
    struct UITreeHoverIds const* hover_ids);

bool
uitree_component_hovered_by_ids(
    int component_id,
    struct UITreeHoverIds const* hover_ids);

bool
uitree_component_visible(
    struct StaticUIComponent const* component,
    int32_t component_index,
    int hovered_component_id);

void
uitree_find_hovered_component_id(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int mouse_x,
    int mouse_y,
    int* out_hovered_component_id);

void
uitree_find_hovered_component_id_for_region(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int mouse_x,
    int mouse_y,
    int region_x,
    int region_y,
    int region_w,
    int region_h,
    int32_t start_index,
    int* out_hovered_component_id);

int
uitree_component_rect_color(
    struct StaticUIComponent const* component,
    struct UITreeHoverIds const* hover_ids,
    struct UITreeBehaviorHost const* host,
    int base_color);

bool
uitree_behavior_is_active(
    struct UITreeBehaviorHost const* host,
    struct StaticUIBehavior const* behavior);

enum UITreeBehaviorHookKind
{
    UITREE_BEHAVIOR_HOOK_ON_LOAD,
    UITREE_BEHAVIOR_HOOK_ON_CLICK,
    UITREE_BEHAVIOR_HOOK_ON_VARP_TRANSMIT,
};

void
uitree_behavior_run_hook(
    struct UITreeBehaviorHost* host,
    struct ToriAuxLibCore* core,
    struct ToriAuxLibCache* cache,
    struct ToriAuxLibCore_Component const* component,
    enum UITreeBehaviorHookKind hook_kind);

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
