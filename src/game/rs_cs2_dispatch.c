#include "rs_cs2_dispatch.h"

#include "game/task_cs2_run.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <string.h>

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
RS_CS2_SyncMouseState(
    struct RS_CS2Host* host,
    struct LibToriRS_Input const* input)
{
    int mx;
    int my;

    assert(host);
    assert(input);
    mx = input->curr.mouse_x;
    my = input->curr.mouse_y;
    /* Off-canvas reads as (-1,-1): scripts test mouse_getx() = -1 to decide the
     * pointer left the client and tear their hover chrome down. */
    if( mx < 0 || my < 0 || mx >= UITREE_LAYOUT_ROOT_W || my >= UITREE_LAYOUT_ROOT_H )
    {
        mx = -1;
        my = -1;
    }
    host->mouse_x = mx;
    host->mouse_y = my;
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
RS_CS2_RunScript(
    struct RS_CS2Host* host,
    struct TaskRunner* runner,
    int script_id,
    int const* int_args,
    int arg_count,
    uint64_t str_mask,
    char const* const* str_args,
    int str_arg_count)
{
    struct ToriRS_Task* task;

    assert(host);
    assert(runner);
    if( script_id <= 0 )
        return;

    task = CreateTask_CS2RunMixed(
        host,
        script_id,
        -1,
        -1,
        arg_count > 0 ? int_args : NULL,
        arg_count,
        str_mask,
        str_args,
        str_arg_count);
    if( !task )
        return;
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

    /* Once per logic tick (TS processWidgetTransmits parity): re-dispatch the
     * transmit hooks when a widget was unhidden (widgets_loaded_dirty), a
     * varp/varc value changed (var_transmit_dirty), or a container changed
     * (inv_transmit_dirty). The per-hook last_seen_serial gate inside the
     * dispatch tasks keeps up-to-date hooks from re-firing, so a quiet tick
     * runs zero scripts. */
    if( !host->widgets_loaded_dirty && !host->var_transmit_dirty &&
        !host->inv_transmit_dirty && !host->misc_transmit_dirty &&
        !host->friend_transmit_dirty )
        return;

    /* Inv hooks re-run on unhide OR a container change. The container filter
     * mirrors the var one: a plain UPDATE_INV only re-runs the hooks that list
     * that container as a trigger, while an unhide (which says nothing about
     * what changed) re-checks everything. Dispatching only on unhide was the
     * bug that left a server-driven inventory permanently blank — the paint
     * script ran once at build time against an empty container and nothing
     * ever asked it to run again. */
    if( host->widgets_loaded_dirty || host->inv_transmit_dirty )
    {
        int container = -1;
        if( !host->widgets_loaded_dirty && !host->inv_changed_all &&
            host->inv_changed_count == 1 )
            container = host->inv_changed_ids[0];
        task = CreateTask_CS2InvTransmitDispatch(host, container);
        assert(task);
        ToriRS_TaskQueue_Add(runner->queue, task);
    }

    /* An unhide has to re-check every hook (a widget that was hidden through any
     * number of value changes must repaint). A plain value change only re-runs
     * the hooks that listed one of the changed vars as a trigger — otherwise
     * rev230's per-tick clock varc drags every hook's script along with it. */
    {
        int const* ids = host->var_changed_ids;
        int count = host->var_changed_count;
        if( host->widgets_loaded_dirty || host->var_changed_all )
        {
            ids = NULL;
            count = 0;
        }
        task = CreateTask_CS2VarTransmitDispatchSet(host, ids, count);
        assert(task);
        ToriRS_TaskQueue_Add(runner->queue, task);
    }

    /*
     * Stat transmits. Filtered by the changed skills the same way the var
     * dispatch filters on changed varps — the XP-drop hook lists all
     * twenty-four skills as triggers and diffs the values it is handed, so a
     * hook fired for a skill that did not change produces a spurious drop
     * rather than a wasted call.
     *
     * `TORIRS_STAT_DEBUG=1` prints each hook this fires, which is how the
     * XP-drop panel's listener was confirmed to be reached at all.
     */
    if( host->stat_transmit_dirty )
    {
        int const* ids = host->stat_changed_ids;
        int count = host->stat_changed_count;

        if( host->stat_changed_all )
        {
            ids = NULL;
            count = 0;
        }
        task = CreateTask_CS2StatTransmitDispatchSet(host, ids, count);
        assert(task);
        ToriRS_TaskQueue_Add(runner->queue, task);
    }

    /* Misc transmits (run energy, run weight). No trigger set to filter on —
     * the hooks carry none — so this re-runs every registered misc hook, which
     * is why it is gated on a real value change rather than on the unhide flag
     * as well. There are only a couple of these hooks in the gameframe. */
    if( host->misc_transmit_dirty )
    {
        task = CreateTask_CS2MiscTransmitDispatch(host);
        assert(task);
        ToriRS_TaskQueue_Add(runner->queue, task);
    }

    /* Friend transmits (the friends and ignore side panels). Like misc there is
     * no trigger set — IF_SETONFRIENDTRANSMIT registers none — so every hook
     * re-runs, and like misc it is gated on a real change rather than on the
     * unhide flag: the hooks it fires cc_deleteall and rebuild a whole list.
     * Without this branch the panels paint once at mount, against whatever the
     * store held at that instant, and never again. */
    if( host->friend_transmit_dirty )
    {
        task = CreateTask_CS2FriendTransmitDispatch(host);
        assert(task);
        ToriRS_TaskQueue_Add(runner->queue, task);
    }

    host->widgets_loaded_dirty = 0;
    host->var_transmit_dirty = 0;
    host->var_changed_count = 0;
    host->var_changed_all = 0;
    host->inv_transmit_dirty = 0;
    host->inv_changed_count = 0;
    host->inv_changed_all = 0;
    host->stat_transmit_dirty = 0;
    host->stat_changed_count = 0;
    host->stat_changed_all = 0;
    host->misc_transmit_dirty = 0;
    host->friend_transmit_dirty = 0;
}
