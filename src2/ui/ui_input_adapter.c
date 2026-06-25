#include "ui_input_adapter.h"

#include "../input/libtorirs_input.h"
#include "toriauxlib/vm/toriauxlibvm.h"

#include <string.h>

static int
ui_adapter_get_varp(
    void* ud,
    int id)
{
    return ToriAuxLibVM_GetVarp(ud, id);
}

static int
ui_adapter_get_varbit(
    void* ud,
    int id)
{
    return ToriAuxLibVM_GetVarbit(ud, id);
}

void
ui_input_adapter_init(struct UIInputAdapter* adapter)
{
    if( !adapter )
        return;
    adapter->state.hovered = -1;
    adapter->state.pressed = -1;
    adapter->hovered_node = -1;
}

struct UIInputResult
ui_input_adapter_process(
    struct UIInputAdapter* adapter,
    struct UITree* tree,
    struct LibToriRS_Input* input,
    struct UITreeBehaviorHost* host)
{
    struct UIInputResult last = {
        .hovered = adapter ? adapter->state.hovered : -1,
        .prev_hovered = adapter ? adapter->state.hovered : -1,
        .clicked = -1,
        .hover_changed = false,
    };

    if( !adapter || !tree || !input )
        return last;

    struct UIInputEvent move = {
        .kind = UI_INPUT_MOVE,
        .x = input->curr.mouse_x,
        .y = input->curr.mouse_y,
        .button = 0,
    };
    last = uitree_input_update(&adapter->state, tree, move);
    adapter->hovered_node = adapter->state.hovered;

    if( LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
    {
        struct UIInputEvent down = {
            .kind = UI_INPUT_DOWN,
            .x = input->curr.mouse_x,
            .y = input->curr.mouse_y,
            .button = TORIRSM_LEFT,
        };
        last = uitree_input_update(&adapter->state, tree, down);
        adapter->hovered_node = adapter->state.hovered;
    }

    if( LibToriRS_Input_IsClick(input, TORIRSM_LEFT) )
    {
        struct UIInputEvent up = {
            .kind = UI_INPUT_UP,
            .x = input->last_click_x[TORIRSM_LEFT],
            .y = input->last_click_y[TORIRSM_LEFT],
            .button = TORIRSM_LEFT,
        };
        last = uitree_input_update(&adapter->state, tree, up);
        adapter->hovered_node = adapter->state.hovered;

        if( host )
            uitree_behavior_handle_input_result(host, tree, &last);
    }

    return last;
}

void
ui_input_adapter_init_behavior_host(
    struct UITreeBehaviorHost* host,
    struct ToriAuxLibVM* vm)
{
    if( !host )
        return;

    memset(host, 0, sizeof(*host));
    if( !vm )
        return;

    host->csvm = ToriAuxLibVM_CSVM(vm);
    host->varp_varbit = ToriAuxLibVM_VarPVarBit(vm);
    host->csvm_state.get_varp = ui_adapter_get_varp;
    host->csvm_state.get_varbit = ui_adapter_get_varbit;
    host->csvm_state.ud = vm;
}
