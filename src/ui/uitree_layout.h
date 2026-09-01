#ifndef SRC_UITREE_LAYOUT_H
#define SRC_UITREE_LAYOUT_H

#include "uitree.h"

#include <stdint.h>

/** Host size the root interface is laid out against — the client canvas in the
 *  standalone viewer, the gameframe slot in the real client. Interfaces read it
 *  back through if_getwidth/if_getheight on their own root (which is
 *  widthMode/heightMode 1, i.e. "fill my parent") and size themselves from it,
 *  so a viewer that always hands over the full canvas makes any interface whose
 *  page mixes centred (yMode 1) and absolute (yMode 0) children lay out wrong —
 *  e.g. bank 12's settings page in cache.jan2026, which is authored for the
 *  fixed-mode modal slot. Runtime-settable so the viewer can host an interface
 *  at the size the client would. */
extern int UITree_LayoutRootWidth;
extern int UITree_LayoutRootHeight;

void
UITree_LayoutSetRootSize(int width, int height);

/** Canvas rows the OPERATING SYSTEM is covering at the bottom of the window --
 *  a soft keyboard, and nothing else so far. 0 on every platform that never
 *  raises one, which is what makes this a no-op everywhere but a phone.
 *
 *  As reported, not as the canvas can honour it: read it through
 *  UITree_LayoutSafeBottomEdge, which is what both consumers actually want. */
extern int UITree_LayoutSafeBottomInset;

/** Set the bottom inset. Does NOT invalidate any tree -- the caller owns the
 *  tree and knows whether one exists, exactly as with the root size. */
void
UITree_LayoutSetSafeBottomInset(int inset);

#define UITREE_LAYOUT_ROOT_W (UITree_LayoutRootWidth)
#define UITREE_LAYOUT_ROOT_H (UITree_LayoutRootHeight)

/**
 * The canvas row where the OS safe area ends -- the canvas height, less
 * whatever the platform is covering at the bottom.
 *
 * At least 1. A band can never cover the WHOLE canvas, and answering 0 would
 * turn every consumer's arithmetic negative for a state only a bogus platform
 * report can produce. Clamped on the way out rather than on the way in
 * because the canvas is free to shrink after the band was reported.
 *
 * Both consumers of the band read it here: the layout, for rows whose profile
 * declared `safe_area=os:bottom`, and `api->safe_os`, for plugins placing their
 * own chrome. They cannot disagree about where the keyboard starts.
 */
static inline int
UITree_LayoutSafeBottomEdge(void)
{
    int const edge = UITree_LayoutRootHeight - UITree_LayoutSafeBottomInset;
    return edge < 1 ? 1 : edge;
}
#define UITREE_SIDEBAR_PANEL_W 190
#define UITREE_SIDEBAR_PANEL_H 261

static inline int
UITree_MulShift14(int a, int b)
{
    return (int)(((int64_t)a * (int64_t)b) >> 14);
}

void
UITree_LayoutInvalidate(struct UITree* tree);

/** Same, but naming the node whose own layout inputs changed.
 *
 *  Prefer this wherever the caller knows the node: it lets the next resolve
 *  walk that node and its descendants instead of every component in the
 *  tree. UITree_LayoutInvalidateBoxes is the same thing without a node, and
 *  costs a full sweep. */
void
UITree_LayoutInvalidateNode(struct UITree* tree, int32_t idx);

/** Mark every node's resolved box as possibly out of date, in O(1). Every write
 *  to a layout input must call this (or UITree_LayoutInvalidate, which also
 *  clears the per-node resolved flags), because UITree_LayoutResolve skips the
 *  walk entirely when nothing has invalidated since the last one. */
void
UITree_LayoutInvalidateBoxes(struct UITree* tree);

/** Re-resolve at root dims if any layout was invalidated since the last
 *  resolve (reference WidgetManager.ensureLayout — CS2 getters must not read
 *  stale geometry mid-script). No-op when layout is current. */
void
UITree_EnsureLayout(struct UITree const* tree);

/** Same guarantee as UITree_EnsureLayout, but only for `idx` and its ancestors.
 *  A node's box depends solely on its ancestors, so a single CS2 getter needs
 *  the root->node chain (O(depth)) rather than a full O(components) resolve.
 *  Leaves layout_stale set — the rest of the tree is still unresolved. */
void
UITree_EnsureLayoutFor(
    struct UITree const* tree,
    int32_t idx);

void
UITree_LayoutResolve(
    struct UITree* tree,
    int root_x,
    int root_y,
    int root_w,
    int root_h);

/** Resolved size of one component that had an onResize listener before a
 *  relayout. `node_index` plus `component_id` guards against a reclaimed slot
 *  being mistaken for the component that was snapshotted. */
struct UITreeResizeHookSnapshot
{
    int32_t node_index;
    int component_id;
    int width;
    int height;
};

/** Snapshot the currently eligible onResize listeners and their cached
 *  computed dimensions before changing the root box. This deliberately does
 *  not resolve pending invalidation: the reference compares the old computed
 *  fields with the ensuing layout pass. Returns the number written, clamped to
 *  `max_entries`. */
int
UITree_SnapshotResizeHooks(
    struct UITree const* tree,
    struct UITreeResizeHookSnapshot* out_entries,
    int max_entries);

/** After relayout, collect only snapshotted listeners whose resolved width or
 *  height changed when `trigger_resize` is nonzero. A false trigger produces
 *  no events even if dimensions changed; position-only movement is always
 *  ignored. Since selection walks the snapshot rather than the live hook set,
 *  a listener registered later cannot join the same resize dispatch. Returns
 *  the number of component ids written, clamped to `max_ids`. */
int
UITree_CollectResizedHookIds(
    struct UITree const* tree,
    struct UITreeResizeHookSnapshot const* entries,
    int entry_count,
    int trigger_resize,
    int* out_component_ids,
    int max_ids);

void
UITree_LayoutGetBounds(
    struct UITreeElemPosition const* position,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

#endif
