#include "ui_behavior.h"

#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/varp_varbit_manager.h"

bool
uitree_component_is_clickable(struct StaticUIComponent const* component)
{
    if( !component )
        return false;
    return component->behavior.button_type != 0 || component->behavior.client_code > 0;
}

bool
uitree_component_visible(
    struct StaticUIComponent const* component,
    int32_t component_index,
    int32_t hovered_component)
{
    if( !component )
        return false;
    if( !component->behavior.hide )
        return true;
    return hovered_component == component_index;
}

bool
uitree_behavior_is_active(
    struct UITreeBehaviorHost const* host,
    struct StaticUIBehavior const* behavior)
{
    if( !host || !host->csvm || !behavior || !behavior->script_comparator || !behavior->script_operand )
        return false;

    int count = behavior->scripts_count;
    if( count <= 0 )
        return false;

    for( int i = 0; i < count; i++ )
    {
        if( !behavior->scripts || !behavior->scripts[i] )
            return false;

        int value = 0;
        if( behavior->script_kind == CS2VM_SCRIPT_KIND_CS2 && host->cs2vm )
            value = cs2vm_eval(host->cs2vm, behavior->scripts[i], &host->cs2vm_state);
        else if( host->csvm )
            value = csvm_eval(host->csvm, behavior->scripts[i], &host->csvm_state);
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

int
uitree_component_rect_color(
    struct StaticUIComponent const* component,
    int32_t component_index,
    int32_t hovered_component,
    struct UITreeBehaviorHost const* host,
    int base_color)
{
    if( !component )
        return base_color;

    int color = base_color;
    bool hovered = hovered_component == component_index;
    bool active = host && uitree_behavior_is_active(host, &component->behavior);

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
