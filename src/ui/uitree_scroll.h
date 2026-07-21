#ifndef SRC_UITREE_SCROLL_H
#define SRC_UITREE_SCROLL_H

#include "uitree.h"
#include "uitree_host.h"

#include <stdbool.h>
#include <stdint.h>

#define UITREE_SCROLLBAR_THICKNESS 16
#define UITREE_SCROLLBAR_ARROW_DELTA 4
/** Pixels of vertical scroll per mouse-wheel notch (reference:
 * OsrsClient.handleIf1Scrollbars scrollY += wheelDeltaY * 45). */
#define UITREE_SCROLLBAR_WHEEL_STEP 45

/* Client.ts / widgets-gl.ts SCROLLBAR_* colors (ARGB). */
#define UITREE_SCROLLBAR_TRACK_ARGB 0xFF23201B
#define UITREE_SCROLLBAR_GRIP_ARGB 0xFF4D4233
#define UITREE_SCROLLBAR_GRIP_HI_ARGB 0xFF766654
#define UITREE_SCROLLBAR_GRIP_LO_ARGB 0xFF332D25

/* Phased draw steps per scrollbar axis (see Client.ts drawScrollbar / v1). */
#define UITREE_SCROLLBAR_V_DRAW_STEPS 9
#define UITREE_SCROLLBAR_H_DRAW_STEPS 9

struct UITreeScrollClip
{
    int clip_x;
    int clip_y;
    int clip_w;
    int clip_h;
};

enum UITreeScrollbarHitKind
{
    UITREE_SCROLLBAR_NONE = 0,
    UITREE_SCROLLBAR_V_UP,
    UITREE_SCROLLBAR_V_DOWN,
    UITREE_SCROLLBAR_V_GRIP,
    UITREE_SCROLLBAR_V_TRACK,
    UITREE_SCROLLBAR_H_LEFT,
    UITREE_SCROLLBAR_H_RIGHT,
    UITREE_SCROLLBAR_H_GRIP,
    UITREE_SCROLLBAR_H_TRACK,
};

enum UITreeScrollbarAction
{
    UITREE_SCROLLBAR_ACTION_NONE = 0,
    UITREE_SCROLLBAR_ACTION_ARROW_STEP,
    UITREE_SCROLLBAR_ACTION_GRIP_DRAG,
};

struct UITreeScrollbarHitInfo
{
    enum UITreeScrollbarHitKind kind;
    int32_t layer_index;
    int layer_x;
    int layer_y;
    int layer_w;
    int layer_h;
    int scroll_height;
    int scroll_width;
};

bool
UITree_ScrollLayerNeedsVertical(struct UITreeComponent const* layer);

bool
UITree_ScrollLayerNeedsHorizontal(struct UITreeComponent const* layer);

int
UITree_ScrollMaxX(struct UITreeComponent const* layer);

int
UITree_ScrollMaxY(struct UITreeComponent const* layer);

/** Clamp UITreeComponent.scroll_x/y into valid range for an IF1 scroll layer. */
void
UITree_ScrollClampComponent(struct UITreeComponent* layer);

void
UITree_ScrollIntersectClip(
    struct UITreeScrollClip* clip,
    int x,
    int y,
    int w,
    int h);

bool
UITree_PointInClip(int px, int py, struct UITreeScrollClip const* clip);

bool
UITree_PointInScrolledBounds(
    int px,
    int py,
    int bx,
    int by,
    int bw,
    int bh,
    int scroll_off_x,
    int scroll_off_y);

/**
 * Sum the canonical scroll offsets of all scrollable RS_LAYER ancestors of
 * node_index (excluding the node itself). Converts a resolved content-space
 * position to its drawn/screen position: drawn = abs - off. Accumulation stops
 * at InterfaceParent mount boundaries, matching emit_walk_node.
 */
void
UITree_AccumScrollOffset(
    struct UITree const* tree,
    int32_t node_index,
    int* off_x,
    int* off_y);

bool
UITree_FindScrollbarAt(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py,
    struct UITreeScrollbarHitInfo* out);

bool
UITree_ScrollbarHandle(
    struct UITree const* tree,
    struct UITreeScrollbarHitInfo const* hit,
    int px,
    int py,
    enum UITreeScrollbarAction action,
    int step);

bool
UITree_ScrollbarIsGripKind(enum UITreeScrollbarHitKind kind);

bool
UITree_ScrollbarIsArrowKind(enum UITreeScrollbarHitKind kind);

#endif
