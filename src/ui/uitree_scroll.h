#ifndef SRC_UITREE_SCROLL_H
#define SRC_UITREE_SCROLL_H

#include "uitree.h"
#include "uitree_host.h"

#include <stdbool.h>
#include <stdint.h>

#define UITREE_SCROLL_MAX 8192
#define UITREE_SCROLLBAR_THICKNESS 16
#define UITREE_SCROLLBAR_ARROW_DELTA 4

struct UITreeScrollState
{
    int* scroll_x;
    int* scroll_y;
};

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

void
UITree_ScrollGetPos(
    struct UITreeScrollState const* scroll,
    int component_id,
    int* sx,
    int* sy);

void
UITree_ScrollSetPos(
    struct UITreeScrollState const* scroll,
    int component_id,
    int sx,
    int sy);

void
UITree_ScrollClampPos(
    struct UITreeComponent const* layer,
    struct UITreeScrollState const* scroll,
    int component_id);

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

void
UITree_ScrollApplyAncestors(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    int32_t const* ancestors,
    int ancestor_count,
    int* bx,
    int* by,
    struct UITreeScrollClip* clip);

int
UITree_CollectAncestors(
    struct UITree const* tree,
    int32_t node_index,
    int32_t* ancestors,
    int max_ancestors);

bool
UITree_FindScrollbarAt(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int px,
    int py,
    struct UITreeScrollbarHitInfo* out);

bool
UITree_ScrollbarHandle(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
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
