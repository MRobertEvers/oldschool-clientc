#include "rs_cs2_dispatch.h"

#include "game/task_cs2_run.h"

#include <assert.h>

void
RS_CS2_SetEventMouse(
    struct RS_CS2Host* host,
    int x,
    int y)
{
    assert(host);
    host->event_mouse_x = x;
    host->event_mouse_y = y;
}

void
RS_CS2_SetEventKey(
    struct RS_CS2Host* host,
    int key_typed,
    int key_pressed)
{
    assert(host);
    host->event_key_typed = key_typed;
    host->event_key_pressed = key_pressed;
}

void
RS_CS2_SetEventOp(
    struct RS_CS2Host* host,
    int op_index,
    int op_subindex)
{
    assert(host);
    host->event_op_index = op_index;
    host->event_op_subindex = op_subindex;
}

void
RS_CS2_SyncKeyState(
    struct RS_CS2Host* host,
    struct LibToriRS_Input const* input)
{
    assert(host);
    assert(input);
    memcpy(host->osrs_key_held, input->osrs_key_held, sizeof(host->osrs_key_held));
    memcpy(host->osrs_key_pressed, input->osrs_key_pressed, sizeof(host->osrs_key_pressed));
}

void
RS_CS2_SetEventDragTarget(
    struct RS_CS2Host* host,
    struct UITree const* tree,
    int target_id)
{
    assert(host);
    assert(tree);
    host->event_drag_target_id = target_id;
    host->event_drag_target_child_index = -1;
    if( target_id >= 0 )
    {
        int32_t tidx = UITree_FindByComponentId(tree, target_id);
        if( tidx >= 0 && tree->components[tidx].dynamic )
            host->event_drag_target_child_index = tree->components[tidx].dynamic_child_index;
    }
}

void
RS_CS2_DispatchHook(
    struct RS_CS2Host* host,
    struct TaskRunner* runner,
    int component_id,
    struct UITreeRuntimeScriptHook const* hook)
{
    struct ToriRS_Task* task;

    assert(host);
    assert(runner);
    if( !hook || hook->script_id <= 0 || component_id < 0 )
        return;

    {
        char const* strp[UITREE_HOOK_STR_ARG_MAX];
        for( int i = 0; i < UITREE_HOOK_STR_ARG_MAX; i++ )
            strp[i] = hook->strv[i];
        task = CreateTask_CS2RunMixed(
            host,
            hook->script_id,
            component_id,
            component_id,
            hook->argc > 0 ? hook->argv : NULL,
            hook->argc,
            hook->str_mask,
            strp,
            hook->str_argc);
    }
    if( !task )
        return;
    /* Enqueue only — the app's per-frame pump drives it. The task queue is a
     * strict serial FIFO, so hook ordering is preserved across IO yields. */
    ToriRS_TaskQueue_Add(runner->queue, task);
}

void
RS_CS2_PumpTransmits(
    struct RS_CS2Host* host,
    struct TaskRunner* runner)
{
    struct ToriRS_Task* task;

    assert(host);
    assert(runner);

    /* Once per logic tick (TS processWidgetTransmits parity): traverse the transmit
     * hooks only when a widget was unhidden this tick. The per-hook last_seen_serial
     * gate inside the dispatch tasks keeps up-to-date hooks from re-firing, so a
     * quiet tick runs zero scripts. */
    if( !host->widgets_loaded_dirty )
        return;
    host->widgets_loaded_dirty = 0;

    task = CreateTask_CS2InvTransmitDispatch(host, -1);
    assert(task);
    ToriRS_TaskQueue_Add(runner->queue, task);

    task = CreateTask_CS2VarTransmitDispatch(host, -1);
    assert(task);
    ToriRS_TaskQueue_Add(runner->queue, task);
}
