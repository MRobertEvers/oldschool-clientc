#ifndef SRC_UITREE_INPUT_H
#define SRC_UITREE_INPUT_H

#include "uitree.h"
#include "uitree_host.h"
#include "uitree_scroll.h"

#include <stdbool.h>
#include <stdint.h>

struct UIInputState
{
    int32_t hovered;
    int32_t pressed;
    /** Drag gesture (TS OsrsClient widget drag). */
    int drag_active;
    int32_t drag_source_idx;
    int drag_source_id;
    int drag_target_id;
    int drag_pickup_x;
    int drag_pickup_y;
    int drag_click_x;
    int drag_click_y;
    int drag_duration;
    int deferred_click; /* 1 = fire click on mouseup if drag never started */
    /* Non-draggable press already fired result.clicked (Jagex/xrsps onclick on
     * mousedown). Suppress the matching release so on_click runs once. */
    int release_click_suppressed;
    int thresholds_set;
};

enum UIInputEventKind
{
    UI_INPUT_MOVE = 0,
    UI_INPUT_DOWN,
    UI_INPUT_UP,
};

struct UIInputEvent
{
    enum UIInputEventKind kind;
    int x;
    int y;
    int button;
};

struct UIInputResult
{
    int32_t hovered;
    int32_t prev_hovered;
    int32_t clicked;
    bool hover_changed;
    int drag_started;
    int drag_moved;
    int drag_ended;
    int32_t drag_source_idx;
    int drag_source_id;
    int drag_target_id;
    int deferred_click_fired;
    /** Widget that owned this frame's mouse-up, even when the release was not
     * also a click (pointer moved away or a drag completed). */
    int32_t released_source_idx;
    int released_source_id;
    /* 1 = clicked was armed on the press edge (non-draggable). interact_click
     * must use the current pointer, not last_click_* (set only on release). */
    int press_click;
};

bool
UITree_PointInComponent(
    struct UITreeElemPosition const* position,
    int px,
    int py);

int32_t
UITree_HitTestRecursive(
    struct UITree const* tree,
    int32_t node_index,
    int px,
    int py);

int32_t
UITree_HitTest(
    struct UITree const* tree,
    int px,
    int py);

bool
UITree_ComponentIsPassThrough(
    struct UITreeComponent const* component,
    struct UITreeHost const* host);

int32_t
UITree_HitTestInteractive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py);

/**
 * Does the interface stop input reaching the world at this point?
 *
 * This is the reference's `isPointOverWidget` — "should the UI consume a world
 * click here" — and it is a *different* question from
 * `UITree_HitTestInteractive`, which asks "is there a widget that wants the
 * click". A bank's dark background wants nothing and must still swallow the
 * click, the hover text and the wheel; a hovered chat line wants the click and
 * must not swallow the wheel. Gating the world on the interactive hit alone is
 * why "Walk here" appeared over an open bank and why scrolling the bank's item
 * pane also zoomed the camera behind it.
 *
 * Two things block: a `noClickThrough` layer covering the point, and the root
 * of any mounted sub-interface. The latter is deliberately independent of the
 * mount type and of click hooks/menu ops: visible, actionless panel space still
 * belongs to the interface and must not leak a click to the world underneath.
 */
int
UITree_PointBlocksWorld(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py);

/**
 * Collect every menu-relevant node under (px,py), TOP-MOST FIRST: interactive
 * nodes plus RS_INV/RS_INV_TEXT grids (whose rows come from inventory slots).
 * Applies the same visibility / clip / scroll / no_click_through rules as
 * UITree_HitTestInteractive (a blocking panel discards nodes drawn under it —
 * reference collectWidgetsAtPoint noClickThrough slice). Returns the count
 * written to out_nodes (node indices), clamped to max_nodes.
 */
int
UITree_CollectNodesAt(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py,
    int32_t* out_nodes,
    int max_nodes);

struct UIInputResult
UITree_InputUpdate(
    struct UIInputState* state,
    struct UITree* tree,
    struct UITreeHost const* host,
    struct UIInputEvent event);

/**
 * Advance drag while left button held. Call each frame after InputUpdate.
 * Uses per-component deadzone/deadtime; sets visual overrides on source.
 * Returns non-zero if drag state changed (started/moved/ended visuals).
 */
int
UITree_InputDragTick(
    struct UIInputState* state,
    struct UITree* tree,
    struct UITreeHost const* host,
    int mouse_x,
    int mouse_y,
    int left_held);

#endif
