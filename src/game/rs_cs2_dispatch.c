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

    task = CreateTask_CS2Run(
        host,
        hook->script_id,
        component_id,
        component_id,
        hook->argc > 0 ? hook->argv : NULL,
        hook->argc);
    if( !task )
        return;
    ToriRS_TaskQueue_Add(runner->queue, task);
    TaskRunner_Drain(runner);

    if( host->pending_inv_transmit_redispatch )
    {
        host->pending_inv_transmit_redispatch = 0;
        task = CreateTask_CS2InvTransmitDispatch(host, -1);
        assert(task);
        ToriRS_TaskQueue_Add(runner->queue, task);
        TaskRunner_Drain(runner);
    }
    if( host->pending_var_transmit_redispatch )
    {
        host->pending_var_transmit_redispatch = 0;
        task = CreateTask_CS2VarTransmitDispatch(host, -1);
        assert(task);
        ToriRS_TaskQueue_Add(runner->queue, task);
        TaskRunner_Drain(runner);
    }
}
