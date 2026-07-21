#ifndef SRC_GAME_RS_CS2_DISPATCH_H
#define SRC_GAME_RS_CS2_DISPATCH_H

#include "game/rs_cs2_host.h"
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

#endif
