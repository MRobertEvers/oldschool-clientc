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
    struct UITreeScrollState const* scroll,
    int px,
    int py);

struct UIInputResult
UITree_InputUpdate(
    struct UIInputState* state,
    struct UITree* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
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
