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

/**
 * The six pieces a SPRITE-drawn vertical scrollbar is made of, in the order
 * the reference's own repainter hands them over.
 *
 * OldSchool draws a scrollbar out of art; the 2004 client draws one out of
 * filled rectangles in three hardcoded colours, which is what this tree has
 * always done. Both are kept, because both are right on the frame they belong
 * to -- and which one is drawn is decided by whether a skin was declared, not
 * by a mode flag somebody has to remember to set.
 *
 * The order is `~scrollbar_vertical_repaint`'s (clientscript 838), read off
 * the chatbox's own call in `toplevel_chatbox_background`:
 *
 *     ~scrollbar_vertical_repaint(interface_162:559, 792, 789, 790, 791, 773, 788, ...)
 *
 * whose six graphics land on that component's subs 0,2,1,3,4,5 -- trough,
 * dragger top, dragger middle, dragger bottom, up arrow, down arrow.
 */
enum UITreeScrollbarSkinPiece
{
    /** Tiled down the track: the groove the dragger runs in. */
    UITREE_SCROLLBAR_SKIN_TROUGH = 0,
    UITREE_SCROLLBAR_SKIN_DRAGGER_TOP,
    /** Tiled: the dragger is three pieces so it can be any length. */
    UITREE_SCROLLBAR_SKIN_DRAGGER_MID,
    UITREE_SCROLLBAR_SKIN_DRAGGER_BOTTOM,
    UITREE_SCROLLBAR_SKIN_ARROW_UP,
    UITREE_SCROLLBAR_SKIN_ARROW_DOWN,

    UITREE_SCROLLBAR_SKIN_COUNT
};

/** The dragger's end caps, at the height OldSchool cut them: 16x5 each. */
#define UITREE_SCROLLBAR_SKIN_CAP_H 5
/** Shortest dragger the three-piece build can make: two caps and a row of
 *  middle. Below it the caps would overlap and read as one smear. */
#define UITREE_SCROLLBAR_SKIN_DRAGGER_MIN (2 * UITREE_SCROLLBAR_SKIN_CAP_H + 1)

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
    /** A captured scrollbar must never transfer to a node later recycled into
     * the same component-array slot. */
    uint32_t layer_incarnation;
    int layer_x;
    int layer_y;
    int layer_w;
    int layer_h;
    int scroll_height;
    int scroll_width;
};

/**
 * OSRS clips every positive-size layer/container to its own bounds before
 * drawing children — not only scrollable layers (readme "interfacex — layer
 * clipping"). True for the container types whose children are clipped; the
 * positive-size check stays at the call site. Shared by the emit walk and the
 * hit/hover/drop walks so drawn pixels and hitboxes agree.
 */
bool
UITree_ComponentClipsChildren(struct UITreeComponent const* component);

/**
 * True for containers that establish a new draw SURFACE (reference Pix2D PixMap:
 * the chatback and sidebar are separate draw targets). A layer's clip is clamped
 * to its enclosing surface, never to intermediate ancestor layers.
 */
bool
UITree_ComponentEstablishesSurface(struct UITreeComponent const* component);

/**
 * The interface layer child-clip RULE, shared by the emit walk and the
 * hit/hover/drop walks so drawn pixels and hitboxes always agree (one place, no
 * drift). A component that clips its children restricts them to its own screen
 * box ∩ the enclosing `surface` — NEVER compounded with intermediate ancestor
 * layers (reference drawInterface + Pix2D.setClipping, which overwrites the clip
 * and clamps only to the surface PixMap). Surface containers (chat/sidebar)
 * additionally become the surface for their descendants.
 *
 * `surface` is the enclosing surface clip; an empty rect (clip_w/clip_h <= 0)
 * means "unbounded / whole screen". Box coords are screen-space. On a clipping
 * component writes *out_child (the child clip) and *out_surface (the child
 * surface) and returns true; otherwise returns false and the caller keeps its
 * inherited clip and surface. `surface` may be NULL (treated as unbounded).
 */
/**
 * True when a clipping component has collapsed to nothing (zero or negative
 * width/height) and its children must therefore be pruned, not drawn.
 *
 * This is the case UITree_LayerChildClip cannot express: in that API an empty
 * rect means "unbounded", so it has to refuse a degenerate box, and a caller
 * that only asks it ends up drawing the subtree at full size against whatever
 * clip it inherited. That is exactly what a rev-230 orb is — the "empty" half
 * of the fill is a 26x0 layer over a 26x26 sprite, and the CS2 that owns the
 * fill level expresses "full" by setting that layer's height to 0.
 *
 * Every walker (emit, hit-test, hover, drop) must check this before descending,
 * or hitboxes and pixels disagree.
 */
bool
UITree_LayerCullsChildren(
    struct UITreeComponent const* component,
    int box_w,
    int box_h);

bool
UITree_LayerChildClip(
    struct UITreeComponent const* component,
    struct UITreeScrollClip const* surface,
    int box_x,
    int box_y,
    int box_w,
    int box_h,
    struct UITreeScrollClip* out_child,
    struct UITreeScrollClip* out_surface);

bool
UITree_ScrollLayerNeedsVertical(struct UITreeComponent const* layer);

bool
UITree_ScrollLayerNeedsHorizontal(struct UITreeComponent const* layer);

int
UITree_ScrollMaxX(struct UITreeComponent const* layer);

int
UITree_ScrollMaxY(struct UITreeComponent const* layer);

/** Compute the canonical in-range scroll position without mutating the layer.
 * Runtime owners publish the result through `UITree_SetScrollPosAt`; emit and
 * hit testing may use the local result directly. */
void
UITree_ScrollGetClamped(
    struct UITreeComponent const* layer,
    int* out_x,
    int* out_y);

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

/**
 * Resolve a node's actual drawn box in canvas coordinates. This is its laid-out
 * box after the same ancestor-scroll and active-drag translation used by emit.
 * Screen-anchored world/minimap/compass nodes deliberately keep their resolved
 * box, matching the emit path which does not translate those surfaces.
 */
int
UITree_NodeDrawnBounds(
    struct UITree const* tree,
    int32_t node_index,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

bool
UITree_FindScrollbarAt(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py,
    struct UITreeScrollbarHitInfo* out);

bool
UITree_ScrollbarHandle(
    struct UITree* tree,
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
