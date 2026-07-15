#ifndef UI_SCROLL_RUNTIME_H
#define UI_SCROLL_RUNTIME_H

#include "ui_scroll.h"

#include <stdbool.h>

/** Per-component scroll offsets and active scrollbar drag state. */
struct UIScrollRuntime
{
    int scroll_x[UITREE_SCROLL_MAX];
    int scroll_y[UITREE_SCROLL_MAX];
    /** RS component id of the layer being scrollbar-dragged (-1 = none). */
    int drag_layer_id;
    enum UITreeScrollbarHitKind drag_kind;
    int drag_anchor;
    /** Client.ts scrollGrabbed: widens grip drag hit zone while dragging. */
    bool grabbed;
    /** ToriDraw scene sprite ids for vertical/horizontal scrollbar arrow atlases. */
    int scrollbar0_scene_id;
    int scrollbar1_scene_id;
};

void
ui_scroll_runtime_reset(struct UIScrollRuntime* rt);

struct UITreeScrollState
ui_scroll_runtime_view(struct UIScrollRuntime const* rt);

void
ui_scroll_runtime_begin_drag(
    struct UIScrollRuntime* rt,
    int layer_id,
    enum UITreeScrollbarHitKind kind,
    int anchor);

void
ui_scroll_runtime_end_drag(struct UIScrollRuntime* rt);

bool
ui_scroll_runtime_is_grip_dragging(struct UIScrollRuntime const* rt);

#endif
