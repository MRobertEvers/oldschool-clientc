#ifndef SRC_GAME_RS_CS2_DISPATCH_H
#define SRC_GAME_RS_CS2_DISPATCH_H

#include "game/rs_cs2_host.h"
#include "input/torirs_input.h"
#include "task_runner.h"
#include "ui/uitree.h"

/*
 * The game layer's single entry point for executing component script hooks.
 * Owns the "how CS2 hooks run" policy: enqueue the script task and drive it to
 * completion (native drain). UI code returns intents; the application layer
 * applies event context and calls this. Widget-loaded transmit traversal is
 * coalesced into RS_CS2_PumpTransmits, run once per logic tick.
 */

/** Run one hook to completion. No-op if hook is NULL or unset. */
void
RS_CS2_DispatchHook(
    struct RS_CS2Host* host,
    struct TaskRunner* runner,
    int component_id,
    struct UITreeRuntimeScriptHook const* hook);

/** Run a clientscript the server named (RUNCLIENTSCRIPT), with no component
 *  context — nothing in the tree triggered it, so `.cc_*` opcodes inside it
 *  have nothing to resolve against, exactly as in the reference. Argument
 *  encoding matches the hook path: str_mask bit i marks position i as the
 *  string in str_args[i]. */
void
RS_CS2_RunScript(
    struct RS_CS2Host* host,
    struct TaskRunner* runner,
    int script_id,
    int const* int_args,
    int arg_count,
    uint32_t str_mask,
    char const* const* str_args,
    int str_arg_count);

/** Once per logic tick: if any widget was unhidden since the last pump, run the
 *  inv/var transmit traversals (per-hook serial gating makes quiet passes free).
 *  TS parity: processWidgetTransmits gated by widgetsLoadedDirty. */
void
RS_CS2_PumpTransmits(
    struct RS_CS2Host* host,
    struct TaskRunner* runner);

/** Set the script-visible event mouse coordinates. */
void
RS_CS2_SetEventMouse(struct RS_CS2Host* host, int x, int y);

/** Set the script-visible drag target (resolves dynamic child index). */
void
RS_CS2_SetEventDragTarget(
    struct RS_CS2Host* host,
    struct UITree const* tree,
    int target_id);

/** Set the script-visible key event. See struct LibToriRS_KeyEvent for why
 *  key_typed carries the key CODE and key_pressed the CHARACTER. */
void
RS_CS2_SetEventKey(struct RS_CS2Host* host, int key_typed, int key_pressed);

/** Set the script-visible op index (1..10) for the next hook dispatch. Pass
 *  op_index 1 to restore the primary left-click default. */
void
RS_CS2_SetEventOp(struct RS_CS2Host* host, int op_index, int op_subindex);

/** Refresh the KEYHELD / KEYPRESSED snapshot from this frame's input. Call once
 *  per App_RunOnce, before any hook is dispatched. */
void
RS_CS2_SyncKeyState(
    struct RS_CS2Host* host,
    struct LibToriRS_Input const* input);

/** Refresh the MOUSE_GETX / MOUSE_GETY position from this frame's input. Call
 *  once per App_RunOnce, before any hook is dispatched: the hover chrome
 *  (tooltips) places itself at the pointer from inside an onmouserepeat hook. */
void
RS_CS2_SyncMouseState(
    struct RS_CS2Host* host,
    struct LibToriRS_Input const* input);

#endif
