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
    /* IF1 scrollbar arrow hold-to-scroll: an arrow press latches this so the bar
     * keeps stepping (and keeps owning the mouse) every frame the button is held,
     * matching TS doScrollbar's per-frame `scrollPos += scrollCycle*4` arrow
     * branch. Owning the mouse through the release also stops the leaked press
     * from becoming a "Walk here" world click. */
    int sb_arrow_held;
    /* The scrollbar which owned this physical press became display-hidden.
     * Swallow the rest of that press through mouse-up; never retarget a held
     * button to whatever replacement/underlay appeared beneath it. */
    int sb_press_cancelled;
    int hover_com_id;
    int prev_hover_com_id;
    /** Exact occupants behind the semantic hover ids. Component ids and array
     * slots are both reusable, so neither alone is an event lifetime fence. */
    int32_t hover_node_index;
    uint32_t hover_node_incarnation;
    int32_t prev_hover_node_index;
    uint32_t prev_hover_node_incarnation;
    /*
     * onMouseRepeat is a CLIENT-CYCLE event, not a frame event: the reference
     * dispatches it from the same 20ms cycle loop that runs processWidgetTimers.
     * That pairing is load-bearing. The cache's mouseover container (script
     * 4725's timer -> 4726) does `cc_deleteall` on interface_161:37 and rebuilds
     * the hover line AND the tooltip from scratch every cycle; the tooltip is
     * only put back by the hovered component's onmouserepeat. Gate the repeat on
     * wall-clock milliseconds instead and it quantizes to the frame period —
     * 3 frames at 120fps is 24.9ms, so it fires ~41 times a second against 50
     * teardowns, and the tooltip is missing from ~1 frame in 5. Native never
     * showed it because the loop is capped at exactly 20ms per frame; the
     * browser (rAF / zero-delay timeout) is not.
     *
     * client_cycle is the app's logic-tick counter, written before each
     * InteractFrame. last_repeat_cycle starts at UINT64_MAX so the first hover
     * repeats immediately.
     */
    uint64_t client_cycle;
    uint64_t last_repeat_cycle;
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

    /**
     * Swipe-to-scroll, the touch UI's way of moving a list: a finger that
     * lands inside a scrollable layer and is HELD (the touch layer sends a
     * press on its own only for a drag; a tap arrives press-and-release
     * together) scrolls that layer by its travel, and nothing under the
     * finger is pressed. Set `touch_scroll` beside App.touch_ui; a mouse
     * never takes this path. `ts_layer` is the layer being scrolled, -1
     * between gestures; the incarnation guards a rebuilt tree.
     */
    int touch_scroll;
    int32_t ts_layer;
    uint32_t ts_incarnation;
    int ts_press_y;
    int ts_start_scroll_y;
    /** A captured swipe target was suppressed/rebuilt mid-press. Keep owning
     * the physical gesture through release so it cannot retarget underneath. */
    int ts_press_cancelled;
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
 * Key targets carry an exact node incarnation rather than hook pointers,
 * because snapshot-by-value is not sufficient here: an earlier onKey script
 * in the same batch can delete and rebuild a collected component. app.c
 * validates each historical occupant immediately before dispatching it.
 */
#define UI_KEY_TARGET_MAX 64

/** Which of the three keyboard hooks a key target carries. One component can
 *  carry any combination (the inventory carries key-down and key-up). */
#define UI_KEY_HOOK_TYPED (1 << 0)
#define UI_KEY_HOOK_DOWN (1 << 1)
#define UI_KEY_HOOK_UP (1 << 2)

/** A component with an onKey handler, plus its screen-space drawn origin
 * captured at collection time (reference caches _absX/_absY before dispatch,
 * and layout is not re-resolved until after the dispatch loop anyway). */
struct UIKeyTarget
{
    int component_id;
    /** Exact node collected for this broadcast. Earlier synchronous key/drag
     * hooks can delete and rebuild the same component id before this target's
     * turn; the replacement must not inherit the old event. */
    int32_t node_index;
    uint32_t node_incarnation;
    int abs_x;
    int abs_y;
    /** UI_KEY_HOOK_* bits present on the component at collection time. The
     *  dispatcher re-reads the hook itself before running it; this only says
     *  which of the three loops need to consider this target at all. */
    int hooks;
};

/** One "run this component script hook" request, with optional event context
 * the dispatcher must apply first. */
struct UIIntent
{
    int component_id;
    /** Exact source identity at collection time. An earlier intent may run
     * CC_DELETEALL and rebuild the same component id into the same slot. */
    int has_node_identity;
    int32_t node_index;
    uint32_t node_incarnation;
    struct UITreeRuntimeScriptHook const* hook;
    /** This intent is the component's primary pointer click. Kept separate
     *  from event_mouse because clicks and hover/drag hooks can both carry
     *  component-relative coordinates. */
    int is_click;
    int has_event_mouse;
    int event_mouse_x;
    int event_mouse_y;
    int has_drag_target;
    int drag_target_id;
    int has_drag_target_identity;
    int32_t drag_target_node_index;
    uint32_t drag_target_node_incarnation;
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
    /** The physical left click belongs to a native gesture whose exact owner
     * became display-hidden or was recycled before release. App-level plugin
     * regions sit outside the UITree input bridge, so they must observe this
     * fence too: the release is swallowed rather than retargeted to newly
     * exposed replacement art. */
    int cancelled_pointer_click;
    /** Menu option index selected this frame (mousedown), or -1. */
    int minimenu_select;
    /** Menu was closed by clicking away this frame. */
    int minimenu_closed;
    /** The minimenu owned pointer input for this frame. This remains set when
     * selecting an option hides the menu before app-level pointer handlers
     * run, preventing that same press from reaching content underneath. */
    int minimenu_consumed_pointer;
    /** Left click landed on the minimap widget (chrome gesture like the tab
     * icons — the node has no component id): app maps it to a walk. */
    int minimap_click;
    int minimap_click_x;
    int minimap_click_y;
    /**
     * Left click landed on a chat filter button, and which filter it was.
     *
     * Reported rather than acted on, for a reason the minimap's flag above
     * does not have: a plugin layout may have claimed that rectangle, and the
     * app cannot un-cycle a filter that this walk already cycled. Everything a
     * plugin region can intercept has to leave here as a REQUEST -- the
     * interception clears these the same way it clears `clicked_com_id` -- or
     * the click is spent before anyone can take it.
     *
     * -1 in `chat_button_filter` means no button was clicked.
     */
    int chat_button_filter;
    /** An interface under the cursor handled this frame's wheel, either by
     * natively stepping an IF1 scroll layer or dispatching a CS2 onScroll hook.
     * App-level wheel gestures (notably world camera zoom) check this so the
     * same notch cannot propagate through the interface to the world. */
    int wheel_consumed;

    /* Keyboard broadcast: dispatch is the cross product of key_events and
     * key_targets. See UI_KEY_TARGET_MAX for why this is a separate list. */
    struct UIKeyTarget key_targets[UI_KEY_TARGET_MAX];
    int key_target_count;
    struct LibToriRS_KeyEvent key_events[LIBTORIRS_KEY_EVENT_MAX];
    int key_event_count;
    int key_mouse_x;
    int key_mouse_y;
    /* OSRS key codes that went down / came up this frame, for the on_key_down
     * and on_key_up hooks. Separate from key_events because those are typed
     * events (a printable key arrives as a character, with no code) and
     * because a release produces no typed event at all. */
    int key_down_codes[LIBTORIRS_KEY_EVENT_MAX];
    int key_down_count;
    int key_up_codes[LIBTORIRS_KEY_EVENT_MAX];
    int key_up_count;
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

/** Run a frame while an external surface owns the physical left press.
 * Hover, wheel, keyboard, and right-click minimenu behavior remain live, but
 * native left press/hold/repeat/release/drag behavior is suppressed. */
void
UITree_InteractFrameWithPointerCapture(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct UITreeHost const* ui_host,
    struct LibToriRS_Input* input,
    uint64_t now_ms,
    int left_pointer_captured,
    struct UIInteractOut* out);

/**
 * Run a frame with both pointer claims stated.
 *
 * `left_pointer_captured` is the capture above: an external surface owns the
 * physical press while the tree keeps hovering and scrolling underneath it.
 *
 * `pointer_owned` is the stronger claim, for an opaque overlay DRAWN over the
 * tree in the same canvas -- a chrome window rasterised into the frame. Then
 * no hover, no wheel, no left gesture and no right-press menu request reaches
 * the tree, and the component that was hovered is told it was left. Keys stay
 * live: they are not aimed with the mouse.
 */
void
UITree_InteractFrameWithPointerOwner(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct UITreeHost const* ui_host,
    struct LibToriRS_Input* input,
    uint64_t now_ms,
    int left_pointer_captured,
    int pointer_owned,
    struct UIInteractOut* out);

/** Prefer on_op, else on_click; walk parents from leaf until a hooked node is
 * found. Exposed for tests. */
struct UITreeRuntimeScriptHook const*
UITree_ResolveClickHook(
    struct UITree* tree,
    int32_t leaf_index,
    int* out_component_id);

/**
 * Apply a host-staged cc/if_dragpickup after CS2 has run in the same frame
 * (press-time track onclick → pending → consume while still held). Returns the
 * number of intents written to out.
 */
int
UITree_InteractConsumePendingDragPickup(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct UITreeHost const* ui_host,
    struct LibToriRS_Input* input,
    struct UIInteractOut* out);

#endif
