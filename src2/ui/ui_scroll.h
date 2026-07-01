#ifndef UI_SCROLL_H
#define UI_SCROLL_H

#include "uitree.h"

#include <stdbool.h>
#include <stdint.h>

#define UITREE_SCROLL_MAX 8192
#define UITREE_SCROLLBAR_THICKNESS 16
#define UITREE_SCROLLBAR_ARROW_DELTA 4

/* Client.ts SCROLLBAR_* colors (ARGB). */
#define UITREE_SCROLLBAR_TRACK_ARGB 0xFF23201B
#define UITREE_SCROLLBAR_GRIP_ARGB 0xFF4D4233
#define UITREE_SCROLLBAR_GRIP_HI_ARGB 0xFF766654
#define UITREE_SCROLLBAR_GRIP_LO_ARGB 0xFF332D25

/* Phased draw steps per scrollbar axis (see Client.ts drawScrollbar). */
#define UITREE_SCROLLBAR_V_DRAW_STEPS 9
#define UITREE_SCROLLBAR_H_DRAW_STEPS 9

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
uitree_scroll_layer_needs_vertical(struct StaticUIComponent const* layer);

bool
uitree_scroll_layer_needs_horizontal(struct StaticUIComponent const* layer);

int
uitree_scroll_max_x(struct StaticUIComponent const* layer);

int
uitree_scroll_max_y(struct StaticUIComponent const* layer);

void
uitree_scroll_get_pos(
    struct UITreeScrollState const* scroll,
    int component_id,
    int* sx,
    int* sy);

void
uitree_scroll_set_pos(
    struct UITreeScrollState const* scroll,
    int component_id,
    int sx,
    int sy);

void
uitree_scroll_clamp_pos(
    struct StaticUIComponent const* layer,
    struct UITreeScrollState const* scroll,
    int component_id);

void
uitree_scroll_intersect_clip(
    struct UITreeScrollClip* clip,
    int x,
    int y,
    int w,
    int h);

bool
uitree_point_in_clip(int px, int py, struct UITreeScrollClip const* clip);

bool
uitree_point_in_scrolled_bounds(
    int px,
    int py,
    int bx,
    int by,
    int bw,
    int bh,
    int scroll_off_x,
    int scroll_off_y);

void
uitree_scroll_apply_ancestors(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    int32_t const* ancestors,
    int ancestor_count,
    int* bx,
    int* by,
    struct UITreeScrollClip* clip);

int
uitree_collect_ancestors(
    struct UITree const* tree,
    int32_t node_index,
    int32_t* ancestors,
    int max_ancestors);

bool
uitree_find_scrollbar_at(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    int px,
    int py,
    struct UITreeScrollbarHitInfo* out);

bool
uitree_find_scrollbar_at_padded(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    int px,
    int py,
    int padding,
    struct UITreeScrollbarHitInfo* out);

bool
uitree_scrollbar_hit_for_layer(
    struct UITree const* tree,
    int32_t layer_index,
    struct UITreeScrollbarHitInfo* out);

bool
uitree_scrollbar_is_grip_kind(enum UITreeScrollbarHitKind kind);

bool
uitree_scrollbar_is_arrow_kind(enum UITreeScrollbarHitKind kind);

bool
uitree_scrollbar_handle(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    struct UITreeScrollbarHitInfo const* hit,
    int px,
    int py,
    enum UITreeScrollbarAction action,
    int step);

#endif
