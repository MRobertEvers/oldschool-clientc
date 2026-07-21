#ifndef SRC_UITREE_INTERACT_H
#define SRC_UITREE_INTERACT_H

#include "input/torirs_input.h"
#include "uitree.h"
#include "uitree_host.h"
#include "uitree_input.h"
#include "uitree_scroll.h"

#include <stdint.h>

/*
 * Per-frame UI interaction: scrollbar capture, pointer input, wheel, drag,
 * hover, and click-hook resolution. This layer owns interaction POLICY (which
 * node's hook wins, when hover fires) but never executes CS2 — it returns a
 * list of intents for the application layer to dispatch. It must not include
 * game/ or asyncio headers.
 */

struct UIInteraction
{
    struct UIInputState input_state;
    /* IF1 scrollbar grip-drag capture: once a grip/track is pressed, the mouse
     * owns that bar until release (mirrors TS scrollGrabbed). */
    struct UITreeScrollbarHitInfo sb_drag_hit;
    int sb_dragging;
    int hover_com_id;
    int prev_hover_com_id;
    uint64_t last_repeat_ms;
};

#define UI_INTENT_MAX 16

/** One "run this component script hook" request, with optional event context
 * the dispatcher must apply first. */
struct UIIntent
{
    int component_id;
    struct UITreeRuntimeScriptHook const* hook;
    int has_event_mouse;
    int event_mouse_x;
    int event_mouse_y;
    int has_drag_target;
    int drag_target_id;
};

struct UIInteractOut
{
    struct UIIntent intents[UI_INTENT_MAX];
    int intent_count;
    int need_redraw;
    int hover_com_id;
    int clicked_com_id;
};

void
UIInteraction_Init(struct UIInteraction* interact);

/** Run one frame of interaction against the tree. now_ms gates the once-per-
 * client-cycle hooks (onMouseRepeat). */
void
UITree_InteractFrame(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct UITreeHost const* ui_host,
    struct LibToriRS_Input* input,
    uint64_t now_ms,
    struct UIInteractOut* out);

/** Prefer on_op, else on_click; walk parents from leaf until a hooked node is
 * found. Exposed for tests. */
struct UITreeRuntimeScriptHook const*
UITree_ResolveClickHook(
    struct UITree* tree,
    int32_t leaf_index,
    int* out_component_id);

#endif
