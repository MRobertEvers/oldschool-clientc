#include "ui_behavior.h"

#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/varp_varbit_manager.h"
#include "toriauxlib/c/toriauxlibcache_clientscript_convert.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "ui_debug.h"
#include "ui_scroll.h"
#include "uitree_host.h"
#include "uitree_layout.h"

#include <stdio.h>

bool
uitree_component_is_clickable(struct StaticUIComponent const* component)
{
    if( !component )
        return false;
    return component->behavior.button_type != 0 || component->behavior.client_code > 0;
}

bool
uitree_component_has_menu_options(struct StaticUIComponent const* component)
{
    if( !component )
        return false;
    if( component->menu_options.option[0] != '\0' )
        return true;
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        if( component->menu_options.ops[i][0] != '\0' )
            return true;
    }
    return false;
}

bool
uitree_component_expects_minimenu_rows(struct StaticUIComponent const* component)
{
    if( !component )
        return false;

    switch( component->type )
    {
    case UIELEM_BUILTIN_WORLD:
    case UIELEM_BUILTIN_SIDEBAR:
    case UIELEM_BUILTIN_CHAT:
    case UIELEM_BUILTIN_CHAT_BUTTON:
    case UIELEM_BUILTIN_CROSS:
    case UIELEM_BUILTIN_MINIMENU:
    case UIELEM_BUILTIN_COMPASS:
    case UIELEM_BUILTIN_MINIMAP:
    case UIELEM_BUILTIN_TAB_ICONS:
    case UIELEM_BUILTIN_REDSTONE_TAB:
    case UIELEM_RS_LAYER:
        return false;
    default:
        break;
    }

    if( uitree_component_has_menu_options(component) )
        return true;

    switch( component->behavior.button_type )
    {
    case COMPONENT_BUTTON_TYPE_CLOSE:
        return true;
    case COMPONENT_BUTTON_TYPE_OK:
        return component->behavior.client_code == 0;
    case COMPONENT_BUTTON_TYPE_TOGGLE:
    case COMPONENT_BUTTON_TYPE_SELECT:
    case COMPONENT_BUTTON_TYPE_CONTINUE:
        return true;
    default:
        return false;
    }
}

bool
uitree_component_visible_by_id(
    struct StaticUIComponent const* component,
    int hovered_component_id)
{
    if( !component )
        return false;
    if( !component->behavior.hide )
        return true;
    if( component->component_id < 0 )
        return true;
    return hovered_component_id == component->component_id;
}

bool
uitree_component_hovered_by_ids(
    int component_id,
    struct UITreeHoverIds const* hover_ids)
{
    if( component_id < 0 || !hover_ids )
        return false;
    return component_id == hover_ids->main_com_id || component_id == hover_ids->side_com_id ||
           component_id == hover_ids->chat_com_id;
}

bool
uitree_component_visible_by_hover_ids(
    struct StaticUIComponent const* component,
    struct UITreeHoverIds const* hover_ids)
{
    if( !component )
        return false;
    if( !component->behavior.hide )
        return true;
    if( component->component_id < 0 )
        return true;
    return uitree_component_hovered_by_ids(component->component_id, hover_ids);
}

bool
uitree_component_visible(
    struct StaticUIComponent const* component,
    int32_t component_index,
    int hovered_component_id)
{
    (void)component_index;
    return uitree_component_visible_by_id(component, hovered_component_id);
}

static bool
uitree_layer_blocks_hover_find(struct StaticUIComponent const* component)
{
    return component &&
           (component->type == UIELEM_RS_LAYER || component->type == UIELEM_BUILTIN_SIDEBAR) &&
           component->behavior.hide;
}

static void
uitree_find_hovered_component_id_recursive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int32_t node_index,
    int mouse_x,
    int mouse_y,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip,
    int* out_hovered_component_id)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return;

    if( clip && clip->clip_w > 0 && clip->clip_h > 0 && !uitree_point_in_clip(mouse_x, mouse_y, clip) )
        return;

    struct StaticUIComponent const* component = &tree->components[node_index];

    if( uitree_layer_blocks_hover_find(component) )
        return;

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);

    bool const mouse_in_bounds = uitree_point_in_scrolled_bounds(
        mouse_x, mouse_y, bx, by, bw, bh, scroll_off_x, scroll_off_y);

    if( mouse_in_bounds )
    {
        if( component->component_id >= 0 &&
            (component->behavior.over_layer_id >= 0 || component->behavior.over_color != 0) )
        {
            int const hover_id = component->behavior.over_layer_id >= 0
                                     ? component->behavior.over_layer_id
                                     : component->component_id;
            *out_hovered_component_id = hover_id;
            if( component->behavior.over_layer_id >= 0 )
            {
                ui_hover_debug_log(
                    "trigger rs_id=%d -> over_layer_id=%d",
                    component->component_id,
                    component->behavior.over_layer_id);
            }
        }
    }

    bool recurse_children = mouse_in_bounds;
    if( component->type == UIELEM_BUILTIN_SIDEBAR && host && host->get_selected_tab )
    {
        if( host->get_selected_tab(host->user) != component->u.sidebar.tabno )
            recurse_children = false;
    }

    if( !recurse_children )
        return;

    int child_scroll_x = scroll_off_x;
    int child_scroll_y = scroll_off_y;
    struct UITreeScrollClip child_clip = clip ? *clip : (struct UITreeScrollClip){ 0 };

    if( component->type == UIELEM_RS_LAYER )
    {
        uitree_scroll_intersect_clip(&child_clip, bx, by, bw, bh);
        if( scroll && uitree_scroll_layer_needs_horizontal(component) && component->component_id >= 0 )
        {
            int sx = 0;
            uitree_scroll_get_pos(scroll, component->component_id, &sx, NULL);
            child_scroll_x += sx;
        }
        if( scroll && uitree_scroll_layer_needs_vertical(component) && component->component_id >= 0 )
        {
            int sy = 0;
            uitree_scroll_get_pos(scroll, component->component_id, NULL, &sy);
            child_scroll_y += sy;
        }
    }

    for( int32_t child = component->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        uitree_find_hovered_component_id_recursive(
            tree,
            host,
            scroll,
            child,
            mouse_x,
            mouse_y,
            child_scroll_x,
            child_scroll_y,
            &child_clip,
            out_hovered_component_id);
    }
}

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
    int* out_hovered_component_id)
{
    if( !tree || !out_hovered_component_id )
        return;

    *out_hovered_component_id = -1;

    if( region_w <= 0 || region_h <= 0 )
        return;

    if( mouse_x < region_x || mouse_y < region_y || mouse_x >= region_x + region_w ||
        mouse_y >= region_y + region_h )
        return;

    if( start_index >= 0 )
    {
        if( (uint32_t)start_index >= tree->component_count )
            return;
        uitree_find_hovered_component_id_recursive(
            tree, host, scroll, start_index, mouse_x, mouse_y, 0, 0, NULL, out_hovered_component_id);
        return;
    }

    if( tree->root_index < 0 )
        return;

    for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        uitree_find_hovered_component_id_recursive(
            tree, host, scroll, root, mouse_x, mouse_y, 0, 0, NULL, out_hovered_component_id);
    }
}

void
uitree_find_hovered_component_id(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int mouse_x,
    int mouse_y,
    int* out_hovered_component_id)
{
    uitree_find_hovered_component_id_for_region(
        tree,
        host,
        scroll,
        mouse_x,
        mouse_y,
        0,
        0,
        UITREE_LAYOUT_ROOT_W,
        UITREE_LAYOUT_ROOT_H,
        -1,
        out_hovered_component_id);
}

bool
uitree_behavior_is_active(
    struct UITreeBehaviorHost const* host,
    struct StaticUIBehavior const* behavior)
{
    if( !host || !behavior || !behavior->script_comparator || !behavior->script_operand )
        return false;

    if( behavior->script_kind == CS2VM_SCRIPT_KIND_CS2 )
    {
        if( !host->cs2vm )
            return false;
    }
    else if( !host->csvm )
    {
        return false;
    }

    int count = behavior->scripts_count;
    if( count <= 0 )
        return false;

    for( int i = 0; i < count; i++ )
    {
        if( !behavior->scripts || !behavior->scripts[i] )
            return false;

        int script_len =
            behavior->scripts_lengths ? behavior->scripts_lengths[i] : 0;
        int value = 0;
        if( behavior->script_kind == CS2VM_SCRIPT_KIND_CS2 && host->cs2vm )
            value = cs2vm_eval(host->cs2vm, behavior->scripts[i], &host->cs2vm_state);
        else if( host->csvm )
            value = csvm_eval_len(host->csvm, behavior->scripts[i], &host->csvm_state, script_len);
        else
            return false;

        int operand = behavior->script_operand[i];
        int comp = behavior->script_comparator[i];
        if( behavior->script_kind == CS2VM_SCRIPT_KIND_CS2 )
        {
            if( !cs2vm_compare(comp, value, operand) )
                return false;
        }
        else if( !csvm_compare(comp, value, operand) )
            return false;
    }
    return true;
}

static struct ToriAuxLibCore_ScriptHook const*
uitree_behavior_hook_slot(
    struct ToriAuxLibCore_Component const* component,
    enum UITreeBehaviorHookKind hook_kind)
{
    if( !component )
        return NULL;

    switch( hook_kind )
    {
    case UITREE_BEHAVIOR_HOOK_ON_LOAD:
        return &component->on_load;
    case UITREE_BEHAVIOR_HOOK_ON_CLICK:
        return &component->on_click;
    case UITREE_BEHAVIOR_HOOK_ON_VARP_TRANSMIT:
        return &component->on_varp_transmit;
    default:
        return NULL;
    }
}

void
uitree_behavior_run_hook(
    struct UITreeBehaviorHost* host,
    struct ToriAuxLibCore* core,
    struct ToriAuxLibCache* cache,
    struct ToriAuxLibCore_Component const* component,
    enum UITreeBehaviorHookKind hook_kind)
{
    if( !host || !host->cs2vm || !component )
        return;

    struct ToriAuxLibCore_ScriptHook const* hook = uitree_behavior_hook_slot(component, hook_kind);
    if( !hook || hook->argc <= 0 )
        return;

    int script_id = hook->argv[0];
    if( script_id < 0 )
        return;

    struct ToriAuxLibCore_ClientScript* script = NULL;
    if( core )
        script = ToriAuxLibCore_ClientScriptGet(core, script_id);
    if( !script && cache )
        script = ToriAuxLibCache_ClientScriptResolve(cache, script_id);
    if( !script )
        return;

    if( !script->cs2vm_ops )
        ToriAuxLibCore_ClientScriptBuildCs2vmOps(script);
    if( !script->cs2vm_ops || script->cs2vm_op_count <= 0 )
        return;

    (void)cs2vm_eval(host->cs2vm, script->cs2vm_ops, &host->cs2vm_state);
}

int
uitree_component_rect_color(
    struct StaticUIComponent const* component,
    struct UITreeHoverIds const* hover_ids,
    struct UITreeBehaviorHost const* host,
    int base_color)
{
    if( !component )
        return base_color;

    int color = base_color;
    bool hovered = uitree_component_hovered_by_ids(component->component_id, hover_ids);
    bool active = host && component->behavior.script_comparator &&
                  uitree_behavior_is_active(host, &component->behavior);

    if( active )
        color = component->behavior.active_color ? component->behavior.active_color : color;
    if( hovered )
    {
        if( active && component->behavior.active_over_color != 0 )
            color = component->behavior.active_over_color;
        else if( !active && component->behavior.over_color != 0 )
            color = component->behavior.over_color;
    }
    return color;
}

void
uitree_behavior_apply_button_click(
    struct UITreeBehaviorHost* host,
    struct StaticUIBehavior const* behavior)
{
    if( !host || !host->varp_varbit || !behavior )
        return;

    if( !behavior->scripts || behavior->scripts_count < 1 || !behavior->scripts[0] )
        return;

    int* script = behavior->scripts[0];
    int opcode = script[0];
    struct VarPVarBitManager* mgr = host->varp_varbit;

    if( opcode == 5 )
    {
        int varp = script[1];
        int current = varp_varbit_get_varp(mgr, varp);

        if( behavior->button_type == COMPONENT_BUTTON_TYPE_TOGGLE )
            varp_varbit_set_varp_optimistic(mgr, varp, 1 - current);
        else if( behavior->button_type == COMPONENT_BUTTON_TYPE_SELECT && behavior->script_operand )
        {
            int operand = behavior->script_operand[0];
            if( current != operand )
                varp_varbit_set_varp_optimistic(mgr, varp, operand);
        }
    }
    else if( opcode == 14 && behavior->button_type == COMPONENT_BUTTON_TYPE_TOGGLE )
    {
        int varbit_id = script[1];
        int current = varp_varbit_get_varbit(mgr, varbit_id);
        int new_val = 1 - current;

        if( varbit_id < 0 || varbit_id >= mgr->varbit_count )
            return;
        struct VarBitType const* vb = &mgr->varbit_types[varbit_id];
        if( vb->basevar < 0 || vb->basevar >= mgr->varp_count )
            return;

        int bit_count = vb->endbit - vb->startbit;
        if( bit_count <= 0 || bit_count >= VARP_VARBIT_READBIT_MAX )
            return;

        int mask = mgr->readbit[bit_count];
        int base_val = varp_varbit_get_varp(mgr, vb->basevar);
        int cleared = base_val & ~(mask << vb->startbit);
        int updated = cleared | ((new_val & mask) << vb->startbit);
        varp_varbit_set_varp_optimistic(mgr, vb->basevar, updated);
    }
    else if( opcode == 14 && behavior->button_type == COMPONENT_BUTTON_TYPE_SELECT && behavior->script_operand )
    {
        int varbit_id = script[1];
        int operand = behavior->script_operand[0];
        int current = varp_varbit_get_varbit(mgr, varbit_id);
        if( current == operand )
            return;

        if( varbit_id < 0 || varbit_id >= mgr->varbit_count )
            return;
        struct VarBitType const* vb = &mgr->varbit_types[varbit_id];
        if( vb->basevar < 0 || vb->basevar >= mgr->varp_count )
            return;

        int bit_count = vb->endbit - vb->startbit;
        if( bit_count <= 0 || bit_count >= VARP_VARBIT_READBIT_MAX )
            return;

        int mask = mgr->readbit[bit_count];
        int base_val = varp_varbit_get_varp(mgr, vb->basevar);
        int cleared = base_val & ~(mask << vb->startbit);
        int updated = cleared | ((operand & mask) << vb->startbit);
        varp_varbit_set_varp_optimistic(mgr, vb->basevar, updated);
    }
}

void
uitree_behavior_handle_input_result(
    struct UITreeBehaviorHost* host,
    struct UITree const* tree,
    struct UIInputResult const* result)
{
    if( !host || !tree || !result || result->clicked < 0 ||
        (uint32_t)result->clicked >= tree->component_count )
        return;

    struct StaticUIComponent* component = &tree->components[result->clicked];
    if( !uitree_component_is_clickable(component) )
        return;

    uitree_behavior_apply_button_click(host, &component->behavior);
}
