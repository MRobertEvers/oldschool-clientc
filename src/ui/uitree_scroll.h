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
    /** A captured scrollbar must never transfer to a node later recycled into
     * the same component-array slot. */
    uint64_t layer_incarnation;
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
 * the chatback and sidebar are separate draw targets). On a tree that does not
 * compound layer clips, a layer's clip is clamped to its enclosing surface, never
 * to intermediate ancestor layers (UITree_LayerChildClip).
 */
bool
UITree_ComponentEstablishesSurface(struct UITreeComponent const* component);

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

/** What UITree_LayerChildClip decided for a component's children. */
enum UITreeChildClip
{
    /** The component clips nothing: its children keep the inherited clip and
     *  surface. */
    UITREE_CHILD_CLIP_INHERIT = 0,
    /** *out_child and *out_surface hold the children's clip and surface. */
    UITREE_CHILD_CLIP_SET,
    /** The children's clip is empty -- the box lies wholly outside the clip
     *  the component was drawn under. Prune the subtree exactly as for
     *  UITree_LayerCullsChildren. Only a compounding tree answers this. */
    UITREE_CHILD_CLIP_EMPTY,
};

/**
 * The interface layer child-clip RULE, shared by the emit walk and the
 * hit/hover/drop walks so drawn pixels and hitboxes always agree (one place, no
 * drift). Which rule is the era's (UITree.layer_clip_compounds):
 *
 *  - OldSchool compounds. A clipping component restricts its children to its
 *    own screen box ∩ `enclosing`, the clip it was itself drawn under, so every
 *    ancestor layer bounds every descendant (rev 239 class163.method5467 takes
 *    max(x, clipLeft) .. min(x + w, clipRight) against the clip it was handed).
 *    Interface 660's notification box grows from 1x2 by resizing its
 *    container; the title strip 8 px down inside it must stay invisible until
 *    the container is tall enough to show it.
 *  - The 2004 client overwrites. Children are clipped to the own box ∩ the
 *    enclosing `surface`, never to intermediate ancestor layers (Pix2D.setBounds
 *    clamps only to the screen). Surface containers (chat/sidebar, scroll
 *    viewports) additionally become the surface for their descendants.
 *
 * `enclosing` and `surface` may be NULL; NULL or an empty rect (clip_w/clip_h
 * <= 0) means "unbounded / whole screen", which is why a compounded clip that
 * comes out empty is answered UITREE_CHILD_CLIP_EMPTY and never as a rect. Box
 * coords are screen-space.
 */
enum UITreeChildClip
UITree_LayerChildClip(
    struct UITree const* tree,
    struct UITreeComponent const* component,
    struct UITreeScrollClip const* enclosing,
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
