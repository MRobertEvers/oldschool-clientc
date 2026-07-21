#ifndef SRC_UITREE_INTERACT_H
#define SRC_UITREE_INTERACT_H

#include "input/torirs_input.h"
#include "uitree.h"
#include "uitree_host.h"
#include "uitree_input.h"
#include "uitree_minimenu.h"
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
    /* Right-click popup: while visible it owns the mouse (all other
     * interaction is suppressed; select on mousedown; click-away closes). */
    struct UIMinimenu minimenu;
    /* A left press the popup consumed also owns its release. The reference's
     * mouseClickButton is a single-shot event raised and consumed inside one
     * mouseLoop, but here select fires on the mousedown edge and the popup is
     * hidden immediately, so without this latch the mouseup edge escapes into
     * the normal click path (IsClick -> left_click_miss) and runs a second
     * action at the menu-row position. */
    int swallow_left_click;
};

#define UI_INTENT_MAX 16

/*
 * Keys deliberately do NOT go through the intent list.
 *
 * A key dispatch is a broadcast -- every event this frame times every visible
 * onKey handler -- which routinely exceeds UI_INTENT_MAX. Raising that cap is
 * not an option: app.c snapshots a ~464-byte UITreeRuntimeScriptHook per intent,
 * so 16 is already a 7.4 KB stack array and a realistic key burst would push it
 * past 60 KB. A separate list also stops a key burst from starving
 * click/hover/drag intents, which must not be dropped.
 *
 * Key targets carry component IDS rather than hook pointers, because
 * snapshot-by-value is not sufficient here: an earlier onKey script in the same
 * batch can delete a component collected during the scan, so app.c re-resolves
 * every id immediately before dispatching it.
 */
#define UI_KEY_TARGET_MAX 64

/** A component with an onKey handler, plus its screen-space drawn origin
 * captured at collection time (reference caches _absX/_absY before dispatch,
 * and layout is not re-resolved until after the dispatch loop anyway). */
struct UIKeyTarget
{
    int component_id;
    int abs_x;
    int abs_y;
};

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
    /** Op index (1..10) reported to the script. 0 means "unset", which the
     *  dispatcher turns into 1, the primary left-click op. Only op-key matches
     *  currently set anything else. */
    int op_index;
};

struct UIInteractOut
{
    struct UIIntent intents[UI_INTENT_MAX];
    int intent_count;
    int need_redraw;
    int hover_com_id;
    int clicked_com_id;
    /** Screen position of the left click that produced clicked_com_id. */
    int clicked_x;
    int clicked_y;
    /** Right click landed while no menu was open: app builds + shows one. */
    int right_click;
    int right_click_x;
    int right_click_y;
    /** Left click hit no UI component this frame (the world element is
     * pass-through): the app's world hittest owns it. */
    int left_click_miss;
    int left_click_miss_x;
    int left_click_miss_y;
    /** Menu option index selected this frame (mousedown), or -1. */
    int minimenu_select;
    /** Menu was closed by clicking away this frame. */
    int minimenu_closed;

    /* Keyboard broadcast: dispatch is the cross product of key_events and
     * key_targets. See UI_KEY_TARGET_MAX for why this is a separate list. */
    struct UIKeyTarget key_targets[UI_KEY_TARGET_MAX];
    int key_target_count;
    struct LibToriRS_KeyEvent key_events[LIBTORIRS_KEY_EVENT_MAX];
    int key_event_count;
    int key_mouse_x;
    int key_mouse_y;
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
