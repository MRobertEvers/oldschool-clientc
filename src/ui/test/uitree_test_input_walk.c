/*
 * Differential proof for the input hit walks.
 *
 * The four input entry points (UITree_HitTest, UITree_CollectNodesAt,
 * UITree_HitTestInteractive, UITree_PointBlocksWorld) switch onto an ordered
 * whole-tree collection the moment a single plugin widget anchor exists
 * (UITree_FrameHasDepth). That collection was rewritten to
 *
 *   - take its event buffer from the tree's walk scratch instead of calloc'ing
 *     and freeing `component_count * 2 + 1` events per call (six collections a
 *     frame, 1,037,160 bytes a frame on rev239 root 548),
 *   - evaluate each node's native availability ONCE and carry the
 *     "ancestors are present" fact down the recursion, instead of calling
 *     UITree_NodeNativeInputPresent twice per node and having each of those
 *     re-walk the whole ancestor chain (O(n x depth x 2)),
 *   - answer the world gate's back-to-back PointBlocksWorld/HitTestInteractive
 *     pair from one collection (UITree_PointQuery).
 *
 * None of that may change an answer. The pre-rewrite walk is kept below
 * VERBATIM -- copied out of uitree_input.c / uitree_hover.c as it stood -- and
 * every entry point is compared against it over seeded random scenes: nested
 * layers with clips and scroll offsets, dragged nodes, every hidden flag,
 * sidebar tabs, builtins whose availability comes from the host, no_click_through
 * barriers, mounted sub-interfaces, a world node, inventory grids and widget
 * anchors.
 *
 * The same scenes carry the cost assertions the rewrite exists for: zero
 * scratch growths after warm-up, and a per-walk budget of node visits plus host
 * requests that is linear in the node count -- which the ancestor re-walks
 * could not have met.
 */
#include "test_harness.h"

#include "log/torirs_log.h"
#include "perf/torirs_perf.h"
#include "uitree_frame.h"
#include "uitree_host.h"
#include "uitree_input.h"
#include "uitree_inv_view.h"
#include "uitree_layout.h"
#include "uitree_scroll.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* REFERENCE: the pre-rewrite walk, verbatim. Only the four public entry point
 * names (and the hover region entry point) are prefixed `ref_`, so that both
 * versions can be linked into one binary; nothing inside a body is edited. */
/* ------------------------------------------------------------------------- */

struct FrameInputEvent
{
    int32_t node_plus_one;
    unsigned char menu, interactive, geometric, barrier, world;
};

struct collect_nodes_ctx
{
    int32_t* out;
    struct FrameInputEvent* events;
    int max;
    int count;
    /** Entries below this index were drawn under a blocking panel/mount. */
    int barrier;
};

static int32_t frame_ordered_hit(struct UITree const* tree, struct UITreeHost const* host,
                                 int px, int py, int geometric);

static bool
collect_inv_grid_slot_hit(
    struct UITreeComponent const* component,
    int bx,
    int by,
    int px,
    int py,
    int scroll_off_x,
    int scroll_off_y)
{
    struct UITreeInvGridLayout layout;

    if( component->type != UIELEM_RS_INV )
        return false;

    layout.cols = component->u.rs_inv.cols;
    layout.rows = component->u.rs_inv.rows;
    layout.margin_x = component->u.rs_inv.margin_x;
    layout.margin_y = component->u.rs_inv.margin_y;
    layout.offset_x = UITree_InvSlots(component)->offset_x;
    layout.offset_y = UITree_InvSlots(component)->offset_y;

    return UITree_InvViewGridHitTest(bx, by, &layout, px + scroll_off_x, py + scroll_off_y) >= 0;
}

static int
hit_trace_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_HIT_TRACE") ? 1 : 0;
    return armed;
}

static int32_t
hit_test_interactive_recursive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int32_t node_index,
    int px,
    int py,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip,
    struct UITreeScrollClip const* surface,
    int* out_blocks,
    int* out_blocks_world)
{
    assert(tree);
    if( out_blocks )
        *out_blocks = 0;
    if( out_blocks_world )
        *out_blocks_world = 0;
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return -1;

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_WALK_HIT, 1);

    if( clip && clip->clip_w > 0 && clip->clip_h > 0 && !UITree_PointInClip(px, py, clip) )
        return -1;

    struct UITreeComponent const* component = &tree->components[node_index];

    /* Match emit: hidden subtrees are not interactive.
     *
     * screen_hidden included, and it is the one that was missing: the title
     * screen's four groups (menu / form / info / progress) are fully
     * overlapping siblings that app_title_sync_groups switches between with
     * exactly this flag, so while it went untested every one of them took
     * clicks at once and the LAST declared won. On the login form that is the
     * info screen's "Try again" plate, which covers the right half of Login
     * and the left half of Cancel -- both buttons visibly lit and dead over
     * the overlap. */
    if( component->behavior.hide || component->mount_hidden || component->frame_hidden || component->screen_hidden ||
        (component->projection_hidden || component->widget_hidden) )
        return -1;

    /* Inactive sidebar tabs contribute nothing — gate FIRST, exactly like the
     * emit walk's early return (uitree_emit.c). Sidebar tabs are fully
     * overlapping siblings; if this ran only on recursion (below) an inactive
     * tab carrying no_click_through would still set *out_blocks and discard the
     * active tab's already-found hit. Return -1 with blocks left 0. */
    if( component->type == UIELEM_BUILTIN_SIDEBAR && host )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        if( UITree_Host(host, &req) != component->u.sidebar.tabno )
            return -1;
    }

    if( !UITree_NodeNativeInputPresent(tree, host, node_index) ) return -1;
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    UITree_LayoutGetBounds(&component->position, &bx, &by, &bw, &bh);

    /* A drag in progress moves the widget (and its whole subtree) on screen but
     * leaves position.abs_* untouched (emit applies the same translation). Fold
     * the drag delta into the scroll offset — PointInScrolledBounds tests against
     * bx - scroll_off, so subtracting the delta shifts the hitbox to match what
     * is drawn. This keeps a dragged item's hitbox under the cursor. */
    if( component->drag_active )
    {
        scroll_off_x -= component->drag_visual_x - (bx - scroll_off_x);
        scroll_off_y -= component->drag_visual_y - (by - scroll_off_y);
    }

    bool const point_in_self =
        UITree_PointInScrolledBounds(px, py, bx, by, bw, bh, scroll_off_x, scroll_off_y);

    /*
     * Every node, and through TORIRS_REPORT.
     *
     * It used to print only `component_id > 0` and through TORIRS_LOG, which
     * left it blind in both directions at once: a builtin (a login field, a
     * login button, the minimap) carries no component id, and TORIRS_LOG is
     * compiled out of the optimized build people actually run. So the trace
     * that exists to answer "why did my click do nothing" could not see the
     * nodes whose clicks are hardest to reason about. It is gated by its own
     * environment variable, which is what TORIRS_REPORT is for.
     */
    if( hit_trace_armed() )
        TORIRS_REPORT("hit: idx=%d com=%d type=%d box=%d,%d %dx%d in_self=%d passthru=%d vis=%d\n",
            node_index,
            component->component_id,
            (int)component->type,
            bx, by, bw, bh,
            (int)point_in_self,
            (int)UITree_ComponentIsPassThrough(component, host),
            (int)UITree_ComponentHitTestVisibleHost(component, -1, host));

    int32_t hit = -1;
    if( point_in_self &&
        !UITree_ComponentIsPassThrough(component, host) &&
        UITree_ComponentHitTestVisibleHost(component, -1, host) )
        hit = node_index;

    /* A no_click_through node covering the point blocks click-through to nodes
     * rendered underneath it (even if the node itself is a passthrough container). */
    int blocks = (point_in_self && component->no_click_through) ? 1 : 0;
    int blocks_world = blocks;

    int child_scroll_x = scroll_off_x;
    int child_scroll_y = scroll_off_y;
    struct UITreeScrollClip child_clip = clip ? *clip : (struct UITreeScrollClip){ 0 };
    struct UITreeScrollClip child_surface = surface ? *surface : (struct UITreeScrollClip){ 0 };

    /* Same shared clip rule as the emit walk (UITree_LayerChildClip), so hitboxes
     * match drawn pixels: clip to own bounds ∩ the enclosing surface, never
     * compounded with ancestor layers. Screen coords — scroll_off_x/y already
     * folds in any drag delta, matching emit's drag-shifted clip. */
    {
        struct UITreeScrollClip cc, cs;
        /* Collapsed clipping layer: nothing under it is drawn, so nothing under
         * it can be hit either (same rule as emit_walk_node). Sentinel is -1,
         * matching every other "no hit here" exit in this function — this one
         * used to return 0, a valid node index, which a parent's `child_hit >=
         * 0` check reads as a real hit on node 0 and lets it clobber an
         * already-found sibling's hit (uitree_input.c below, `child_hit >= 0`).
         * A collapsed sibling next to a real interactive target (e.g. the
         * stat-orbs panel next to the minimap in the rev-230 gameframe) made
         * every click on the target resolve to node 0 instead. */
        if( UITree_LayerCullsChildren(component, bw, bh) )
            return -1;
        if( UITree_LayerChildClip(
                component, surface, bx - scroll_off_x, by - scroll_off_y, bw, bh, &cc, &cs) )
        {
            child_clip = cc;
            child_surface = cs;
        }
    }
    if( component->type == UIELEM_RS_LAYER )
    {
        int effective_scroll_x;
        int effective_scroll_y;
        UITree_ScrollGetClamped(component, &effective_scroll_x, &effective_scroll_y);
        if( UITree_ScrollLayerNeedsHorizontal(component) )
            child_scroll_x += effective_scroll_x;
        if( UITree_ScrollLayerNeedsVertical(component) )
            child_scroll_y += effective_scroll_y;
    }

    /* The physical tree reparents mounted interface roots under their host, but
     * the reference still traverses them as a separate, final pass. Ordinary
     * children inherit the host's local scroll; mounted roots deliberately do
     * not (ancestor scroll is already present in scroll_off_x/y).
     *
     * A type-0 InterfaceParent raises its barrier at this boundary, against the
     * host's clipped rectangle. That discards the host/ordinary-child hit just
     * as class415 clears earlier mouse/menu work, while allowing the mounted
     * subtree walked next to become the target. */
    int const mount_rec = UITree_InterfaceParentFind(tree, component->component_id);
    int const has_mounts = mount_rec >= 0;
    int const mount_type = has_mounts ? tree->interface_parents[mount_rec].type : -1;
    for( int mount_sweep = 0; mount_sweep <= has_mounts; mount_sweep++ )
    {
        if( mount_sweep == 1 && mount_type == 0 && point_in_self )
        {
            hit = -1;
            blocks = 1;
            blocks_world = 1;
        }

        for( int32_t child = component->first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            int const is_mount =
                has_mounts &&
                UITree_ChildMountType(
                    tree, component->component_id, &tree->components[child]) >= 0;
            int child_blocks = 0;
            int child_blocks_world = 0;
            int32_t child_hit;

            if( is_mount != mount_sweep )
                continue;
            child_hit = hit_test_interactive_recursive(
                tree,
                host,
                child,
                px,
                py,
                is_mount ? scroll_off_x : child_scroll_x,
                is_mount ? scroll_off_y : child_scroll_y,
                &child_clip,
                &child_surface,
                &child_blocks,
                &child_blocks_world);
            /* Later siblings render on top. A blocking child also discards this
             * node's own hit and earlier siblings. */
            if( child_blocks )
            {
                hit = child_hit;
                blocks = 1;
            }
            else if( child_hit >= 0 )
                hit = child_hit;
            /* World blocking only accumulates: a sibling drawn later cannot
             * un-block what an earlier one covered, because both remain drawn. */
            if( child_blocks_world )
                blocks_world = 1;
        }
    }

    if( out_blocks )
        *out_blocks = blocks;
    if( out_blocks_world )
        *out_blocks_world = blocks_world;
    return hit;
}

static void
collect_nodes_recursive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int32_t node_index,
    int px,
    int py,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip,
    struct UITreeScrollClip const* surface,
    struct collect_nodes_ctx* ctx)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return;

    bool const clipped = clip && clip->clip_w > 0 && clip->clip_h > 0 && !UITree_PointInClip(px, py, clip);
    if( clipped && !ctx->events ) return;

    struct UITreeComponent const* component = &tree->components[node_index];

    if( component->behavior.hide || component->mount_hidden || component->screen_hidden || (component->projection_hidden || component->widget_hidden) ||
        component->frame_hidden ) return;
    if( !UITree_NodeNativeInputPresent(tree, host, node_index) ) return;

    /* Inactive sidebar tabs contribute nothing — gate FIRST (like the emit
     * walk), before the no_click_through barrier below. Otherwise an inactive
     * but overlapping tab carrying no_click_through raises ctx->barrier and
     * discards the active tab's already-collected inventory/menu entries. */
    if( component->type == UIELEM_BUILTIN_SIDEBAR && host )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        if( UITree_Host(host, &req) != component->u.sidebar.tabno )
            return;
    }

    if( !UITree_NodeNativeInputPresent(tree, host, node_index) ) return;
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    UITree_LayoutGetBounds(&component->position, &bx, &by, &bw, &bh);

    if( component->drag_active )
    {
        scroll_off_x -= component->drag_visual_x - (bx - scroll_off_x);
        scroll_off_y -= component->drag_visual_y - (by - scroll_off_y);
    }

    bool const point_in_self =
        !clipped && UITree_PointInScrolledBounds(px, py, bx, by, bw, bh, scroll_off_x, scroll_off_y);

    /* A blocking panel discards everything rendered under it — including
     * entries already collected — but keeps itself and its subtree. */
    if( !ctx->events && point_in_self &&
        component->no_click_through && ctx->count > ctx->barrier )
        ctx->barrier = ctx->count;

    /* An RS_INV grid's clickable area is the union of its slot rects, not its
     * (cols x rows)-pixel layout bounds — collect it when a slot is hit even if
     * the click misses the tiny node box. */
    bool const inv_slot_hit =
        !clipped && collect_inv_grid_slot_hit(component, bx, by, px, py, scroll_off_x, scroll_off_y);

    struct FrameInputEvent event = { .node_plus_one = node_index + 1 };
    event.barrier = point_in_self && component->no_click_through;
    event.world = node_index == tree->world_index && point_in_self;
    event.geometric = point_in_self && component->type != UIELEM_RS_LAYER;
    event.interactive = point_in_self &&
                        !UITree_ComponentIsPassThrough(component, host) &&
                        UITree_ComponentHitTestVisibleHost(component, -1, host);
    if( (point_in_self || inv_slot_hit) &&
        UITree_ComponentHitTestVisibleHost(component, -1, host) )
    {
        bool const inv_grid =
            component->type == UIELEM_RS_INV || component->type == UIELEM_RS_INV_TEXT;
        /* Op-bearing containers (e.g. an IF3 layer with cache ops but no CS2
         * hook yet) are menu targets even though the click path treats them as
         * pass-through (reference collects any widget with option strings).
         * Chat panels carry social-op templates in their chat config. */
        bool const has_ops = UITree_ComponentHasMenuOptions(component) ||
                             UITree_MenuOptions(component)->option[0] != '\0' ||
                             component->type == UIELEM_BUILTIN_CHAT;
        /* A script-created cell holding an obj is a menu target on the strength
         * of the obj alone. It carries no ops and no hook of its own — the
         * rev-230 worn tab puts "Remove" on the slot LAYER, not on the item
         * child — so every other test here calls it pass-through chrome and
         * drops it, and the equipment slots become unclickable. */
        bool const has_obj = component->item_id > 0;
        event.menu = inv_grid || has_ops || has_obj || !UITree_ComponentIsPassThrough(component, host);
        if( event.menu && !ctx->events && ctx->count < ctx->max )
            ctx->out[ctx->count++] = node_index;
    }

    if( ctx->events && ctx->count < ctx->max ) ctx->events[ctx->count++] = event;

    int child_scroll_x = scroll_off_x;
    int child_scroll_y = scroll_off_y;
    struct UITreeScrollClip child_clip = clip ? *clip : (struct UITreeScrollClip){ 0 };
    struct UITreeScrollClip child_surface = surface ? *surface : (struct UITreeScrollClip){ 0 };

    {
        struct UITreeScrollClip cc, cs;
        /* Collapsed clipping layer: nothing under it is drawn, so nothing under
         * it can be hit either (same rule as emit_walk_node). */
        if( UITree_LayerCullsChildren(component, bw, bh) )
            return;
        if( UITree_LayerChildClip(
                component, surface, bx - scroll_off_x, by - scroll_off_y, bw, bh, &cc, &cs) )
        {
            child_clip = cc;
            child_surface = cs;
        }
    }
    if( component->type == UIELEM_RS_LAYER )
    {
        int effective_scroll_x;
        int effective_scroll_y;
        UITree_ScrollGetClamped(component, &effective_scroll_x, &effective_scroll_y);
        if( UITree_ScrollLayerNeedsHorizontal(component) )
            child_scroll_x += effective_scroll_x;
        if( UITree_ScrollLayerNeedsVertical(component) )
            child_scroll_y += effective_scroll_y;
    }

    /* Keep menu traversal in the same mount-last order and coordinate space as
     * rendering. At the type-0 boundary, slice away the host and everything
     * below/inside its ordinary subtree; mounted entries appended afterwards
     * remain eligible. */
    int const mount_rec = UITree_InterfaceParentFind(tree, component->component_id);
    int const has_mounts = mount_rec >= 0;
    int const mount_type = has_mounts ? tree->interface_parents[mount_rec].type : -1;
    for( int mount_sweep = 0; mount_sweep <= has_mounts; mount_sweep++ )
    {
        if( mount_sweep == 1 && mount_type == 0 && point_in_self )
        {
            if( ctx->events && ctx->count < ctx->max )
                ctx->events[ctx->count++] = (struct FrameInputEvent){ .node_plus_one = node_index + 1, .barrier = 1 };
            else ctx->barrier = ctx->count;
        }

        for( int32_t child = component->first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            int const is_mount =
                has_mounts &&
                UITree_ChildMountType(
                    tree, component->component_id, &tree->components[child]) >= 0;
            if( is_mount != mount_sweep )
                continue;
            collect_nodes_recursive(
                tree,
                host,
                child,
                px,
                py,
                is_mount ? scroll_off_x : child_scroll_x,
                is_mount ? scroll_off_y : child_scroll_y,
                &child_clip,
                &child_surface,
                ctx);
        }
    }
}

static struct FrameInputEvent*
frame_input_events(struct UITree const* tree, struct UITreeHost const* host,
                   int px, int py, int* count)
{
    struct collect_nodes_ctx ctx = { .max = (int)tree->component_count * 2 + 1 };
    ctx.events = calloc((size_t)ctx.max, sizeof(*ctx.events));
    assert(ctx.events);
    for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
        if( UITree_RootIsDisplayable(tree, root) )
            collect_nodes_recursive(tree, host, root, px, py, 0, 0, NULL, NULL, &ctx);
    *count = UITree_FrameReorder(tree, host, ctx.events, ctx.count, sizeof(*ctx.events),
                                offsetof(struct FrameInputEvent, node_plus_one));
    return ctx.events;
}

static int32_t
frame_ordered_hit(struct UITree const* tree, struct UITreeHost const* host,
                  int px, int py, int geometric)
{
    int count;
    int32_t hit = -1;
    struct FrameInputEvent* events = frame_input_events(tree, host, px, py, &count);
    for( int i = 0; i < count; i++ )
    {
        if( events[i].barrier || events[i].world ) hit = -1;
        if( geometric ? events[i].geometric : events[i].interactive ) hit = events[i].node_plus_one - 1;
    }
    free(events);
    return hit;
}

static int32_t
ref_hit_test(
    struct UITree const* tree,
    int px,
    int py)
{
    assert(tree);
    if( UITree_FrameHasDepth(tree) ) return frame_ordered_hit(tree, NULL, px, py, 1);
    if( tree->root_index < 0 )
        return -1;

    int32_t hit = -1;
    for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        int32_t root_hit;
        if( !UITree_RootIsDisplayable(tree, root) )
            continue;
        root_hit = UITree_HitTestRecursive(tree, root, px, py);
        if( root_hit >= 0 )
            hit = root_hit;
    }

    return hit;
}

static int
ref_collect_nodes_at(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py,
    int32_t* out_nodes,
    int max_nodes)
{
    struct collect_nodes_ctx ctx = { .out = out_nodes, .max = max_nodes };

    assert(tree);
    assert(out_nodes);

    if( UITree_FrameHasDepth(tree) )
    {
        int count, barrier = 0, kept = 0;
        struct FrameInputEvent* events = frame_input_events(tree, host, px, py, &count);
        for( int i = 0; i < count; i++ )
            if( events[i].barrier || events[i].world ) barrier = i;
        for( int i = count - 1; i >= barrier && kept < max_nodes; i-- )
            if( events[i].menu ) out_nodes[kept++] = events[i].node_plus_one - 1;
        free(events);
        return kept;
    }

    for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        if( !UITree_RootIsDisplayable(tree, root) )
            continue;
        collect_nodes_recursive(tree, host, root, px, py, 0, 0, NULL, NULL, &ctx);
    }

    /* Slice below the top-most blocking panel, then reverse to top-most-first. */
    {
        int kept = ctx.count - ctx.barrier;
        for( int i = 0; i < kept / 2; i++ )
        {
            int32_t tmp = out_nodes[ctx.barrier + i];
            out_nodes[ctx.barrier + i] = out_nodes[ctx.count - 1 - i];
            out_nodes[ctx.count - 1 - i] = tmp;
        }
        if( ctx.barrier > 0 && kept > 0 )
            memmove(out_nodes, out_nodes + ctx.barrier, (size_t)kept * sizeof(out_nodes[0]));
        return kept;
    }
}

static int32_t
ref_hit_test_interactive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py)
{
    assert(tree);
    if( UITree_FrameHasDepth(tree) ) return frame_ordered_hit(tree, host, px, py, 0);
    if( tree->root_index < 0 )
        return -1;

    int32_t hit = -1;
    for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        int root_blocks = 0;
        int32_t root_hit;
        if( !UITree_RootIsDisplayable(tree, root) )
            continue;
        root_hit = hit_test_interactive_recursive(
            tree, host, root, px, py, 0, 0, NULL, NULL, &root_blocks, NULL);
        /* Later roots render on top. A no_click_through root captures the point
         * and discards hits from roots underneath (even if it has no hit itself). */
        if( root_blocks )
            hit = root_hit;
        else if( root_hit >= 0 )
            hit = root_hit;
    }

    return hit;
}

static int
ref_point_blocks_world(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py)
{
    assert(tree);
    if( tree->root_index < 0 )
        return 0;

    if( UITree_FrameHasDepth(tree) )
    {
        int count, blocked = 0;
        struct FrameInputEvent* events = frame_input_events(tree, host, px, py, &count);
        for( int i = 0; i < count; i++ )
        {
            if( events[i].world ) blocked = 0;
            else if( events[i].barrier ) blocked = 1;
        }
        free(events);
        return blocked;
    }

    for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        int root_blocks = 0;
        int root_blocks_world = 0;
        if( !UITree_RootIsDisplayable(tree, root) )
            continue;
        (void)hit_test_interactive_recursive(
            tree, host, root, px, py, 0, 0, NULL, NULL, &root_blocks, &root_blocks_world);
        if( root_blocks_world )
            return 1;
    }

    return 0;
}

/* ---- and the hover walk, likewise verbatim ---- */

struct FrameHoverEvent
{
    int32_t node_plus_one;
    int hovered;
    int reset;
};
struct FrameHoverEvents
{
    struct FrameHoverEvent* items;
    int count, capacity;
};

static void
find_hovered_recursive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int32_t node_index,
    int mouse_x,
    int mouse_y,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip,
    struct UITreeScrollClip const* surface,
    int* out_hovered_component_id,
    struct FrameHoverEvents* ordered)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return;

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_WALK_HOVER, 1);

    bool const clipped = clip && clip->clip_w > 0 && clip->clip_h > 0 &&
                         !UITree_PointInClip(mouse_x, mouse_y, clip);
    if( clipped && !ordered->items ) return;

    struct UITreeComponent const* component = &tree->components[node_index];
    if( !UITree_NodeNativeVisible(tree, host, node_index, -1) ) return;

    /* Match hit-test / emit: any hidden node is pruned (no self-report, no
     * children). IF_SETHIDE on type=5 spell icons must stop on_mouse_repeat
     * from firing — otherwise a later hidden Lumbridge icon overwrites a
     * visible jewellery-enchant sibling (last-match-wins). IF1 overlayer
     * tooltips stay correct: the visible cell redirects via over_layer_id;
     * the hidden tooltip layer never needs to self-report. */
    if( component->behavior.hide || component->mount_hidden || component->screen_hidden || (component->projection_hidden || component->widget_hidden) ||
        component->frame_hidden ) return;

    /* Inactive sidebar tabs contribute nothing — gate FIRST (like the emit
     * walk), before this node can self-report as hovered via over_layer_id /
     * over_color / hover hooks below. */
    if( component->type == UIELEM_BUILTIN_SIDEBAR && host )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        if( UITree_Host(host, &req) != component->u.sidebar.tabno )
            return;
    }

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    UITree_LayoutGetBounds(&component->position, &bx, &by, &bw, &bh);

    bool const mouse_in_bounds = !clipped && UITree_PointInScrolledBounds(
        mouse_x, mouse_y, bx, by, bw, bh, scroll_off_x, scroll_off_y);

    /* field4189/noClickThrough clears mouse events queued by widgets rendered
     * underneath before this widget contributes its own events. Preserve that
     * ordering here: discard the previous hover, then let this node/children
     * become hovered below. */
    int candidate = -1;
    int const reset = mouse_in_bounds &&
                      (component->no_click_through || (ordered->items && node_index == tree->world_index));

    if( mouse_in_bounds && component->component_id >= 0 )
    {
        /* IF1 over-layer / colourOver redirect (TS addComponentOptions). */
        if( component->behavior.over_layer_id >= 0 )
            candidate = component->behavior.over_layer_id;
        else if( component->behavior.over_color != 0 )
            candidate = component->component_id;
        /* CS2/IF3 addition: components with hover scripts must also report as
         * hovered so on_mouse_over / on_mouse_leave / on_mouse_repeat dispatch
         * (main loop). */
        else if( UITree_Hooks(component)->on_mouse_over.script_id > 0 ||
                 UITree_Hooks(component)->on_mouse_leave.script_id > 0 ||
                 UITree_Hooks(component)->on_mouse_repeat.script_id > 0 )
            candidate = component->component_id;
    }

    if( ordered->items )
    {
        assert(ordered->count < ordered->capacity);
        ordered->items[ordered->count++] = (struct FrameHoverEvent){ node_index + 1, candidate, reset };
    }
    else
    {
        if( reset ) *out_hovered_component_id = -1;
        if( candidate >= 0 ) *out_hovered_component_id = candidate;
    }

    /* Inactive sidebar tabs already returned above; recursion only depends on
     * the mouse being inside this node's bounds. */
    if( !mouse_in_bounds && !ordered->items ) return;

    int child_scroll_x = scroll_off_x;
    int child_scroll_y = scroll_off_y;
    struct UITreeScrollClip child_clip = clip ? *clip : (struct UITreeScrollClip){ 0 };
    struct UITreeScrollClip child_surface = surface ? *surface : (struct UITreeScrollClip){ 0 };

    /* Same shared clip rule as the emit walk (UITree_LayerChildClip), so hover
     * matches drawn pixels: own bounds ∩ enclosing surface, never compounded
     * with ancestor layers. Screen coords (emit clips at x - scroll_off). */
    {
        struct UITreeScrollClip cc, cs;
        /* Collapsed clipping layer: nothing under it is drawn, so nothing under
         * it can be hit either (same rule as emit_walk_node). */
        if( UITree_LayerCullsChildren(component, bw, bh) )
            return;
        if( UITree_LayerChildClip(
                component, surface, bx - scroll_off_x, by - scroll_off_y, bw, bh, &cc, &cs) )
        {
            child_clip = cc;
            child_surface = cs;
        }
    }
    if( component->type == UIELEM_RS_LAYER )
    {
        int effective_scroll_x;
        int effective_scroll_y;
        UITree_ScrollGetClamped(component, &effective_scroll_x, &effective_scroll_y);
        if( UITree_ScrollLayerNeedsHorizontal(component) )
            child_scroll_x += effective_scroll_x;
        if( UITree_ScrollLayerNeedsVertical(component) )
            child_scroll_y += effective_scroll_y;
    }

    /* Rendering visits InterfaceParent roots after ordinary children and does
     * not apply the host's own scroll to them. Hover must use that same order
     * and origin or a mounted control can draw in one place and fire hover
     * hooks in another. Type-0 capture also clears hover selected underneath
     * the host before the mounted subtree gets its turn. */
    int const mount_rec = UITree_InterfaceParentFind(tree, component->component_id);
    int const has_mounts = mount_rec >= 0;
    int const mount_type = has_mounts ? tree->interface_parents[mount_rec].type : -1;
    for( int mount_sweep = 0; mount_sweep <= has_mounts; mount_sweep++ )
    {
        if( mount_sweep == 1 && mount_type == 0 )
            *out_hovered_component_id = -1;

        for( int32_t child = component->first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            int const is_mount =
                has_mounts &&
                UITree_ChildMountType(
                    tree, component->component_id, &tree->components[child]) >= 0;
            if( is_mount != mount_sweep )
                continue;
            find_hovered_recursive(
                tree,
                host,
                child,
                mouse_x,
                mouse_y,
                is_mount ? scroll_off_x : child_scroll_x,
                is_mount ? scroll_off_y : child_scroll_y,
                &child_clip,
                &child_surface,
                out_hovered_component_id, ordered);
        }
    }
}

static int
ref_find_hovered_for_region(
    struct UITree const* tree, struct UITreeHost const* host, int32_t root_index,
    int mouse_x, int mouse_y, int region_x, int region_y, int region_w, int region_h)
{
    int hovered = -1;
    struct FrameHoverEvents ordered = { 0 };
    assert(tree);
    if( region_w <= 0 || region_h <= 0 || mouse_x < region_x || mouse_y < region_y ||
        mouse_x >= region_x + region_w || mouse_y >= region_y + region_h ||
        (root_index >= 0 && (uint32_t)root_index >= tree->component_count) ) return -1;
    if( UITree_FrameHasDepth(tree) )
    {
        ordered.capacity = (int)tree->component_count + 1;
        ordered.items = calloc((size_t)ordered.capacity, sizeof(*ordered.items));
        assert(ordered.items);
    }
    if( root_index >= 0 )
        find_hovered_recursive(tree, host, root_index, mouse_x, mouse_y, 0, 0,
                               NULL, NULL, &hovered, &ordered);
    else
        for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
            if( UITree_RootIsDisplayable(tree, root) )
                find_hovered_recursive(tree, host, root, mouse_x, mouse_y, 0, 0,
                                       NULL, NULL, &hovered, &ordered);
    if( ordered.items )
    {
        ordered.count = UITree_FrameReorder(tree, host, ordered.items, ordered.count,
                          sizeof(*ordered.items), offsetof(struct FrameHoverEvent, node_plus_one));
        for( int i = 0; i < ordered.count; i++ )
        {
            if( ordered.items[i].reset ) hovered = -1;
            if( ordered.items[i].hovered >= 0 ) hovered = ordered.items[i].hovered;
        }
        free(ordered.items);
    }
    return hovered;
}

/* ------------------------------------------------------------------------- */
/* SCENES                                                                     */
/* ------------------------------------------------------------------------- */

#define WALK_CANVAS_W UITREE_LAYOUT_ROOT_W
#define WALK_CANVAS_H UITREE_LAYOUT_ROOT_H
#define WALK_MAX_HITS 256
#define WALK_GROUP_BASE 700
#define WALK_MOUNT_GROUP 900

static uint32_t
walk_rand(uint32_t* state)
{
    /* xorshift32: the scenes have to be reproducible from a seed alone, so the
     * platform's rand() is not usable here. */
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int
walk_range(uint32_t* state, int lo, int hi)
{
    assert(hi >= lo);
    return lo + (int)(walk_rand(state) % (uint32_t)(hi - lo + 1));
}

/** True `percent` of the time. */
static int
walk_chance(uint32_t* state, int percent)
{
    return (int)(walk_rand(state) % 100u) < percent;
}

struct WalkScene
{
    struct UITree* tree;
    struct UITreeHost host;
    struct TestHostState state;
    int node_count;
    int32_t nodes[3200];
    /* Coverage tallies, asserted non-zero over the whole run. */
    int has_anchor;
    int has_hidden_builtin;
    int has_barrier;
    int has_mount;
    int has_minimenu;
    int has_sidebar;
    int has_world;
    int has_inv;
    int has_drag;
    int has_scroll;
    int has_hidden_flag;
};

struct WalkCoverage
{
    int scenes;
    int scenes_with_anchor;
    int scenes_with_hidden_builtin;
    int scenes_with_barrier;
    int scenes_with_mount;
    int scenes_with_sidebar;
    int scenes_with_world;
    int scenes_with_inv;
    int scenes_with_drag;
    int scenes_with_scroll;
    int scenes_with_hidden_flag;
    int scenes_with_minimenu;
    int points;
    int hits;
    int barrier_answers;
    int menu_rows;
    int hover_answers;
    int scenes_that_grew_scratch;
    int warm_walks_measured;
    int worst_budget_percent;
    long long comparisons;
    int max_nodes;
    int max_depth;
};

/* A parent with input but no paint (a hidden MINIMAP, an unlit REDSTONE_TAB) is
 * the case the rewrite has to get right, so builtins are seeded densely enough
 * that scenes contain them. */
static enum UITreeComponentType const walk_builtin_types[] = {
    UIELEM_BUILTIN_MINIMAP, UIELEM_BUILTIN_COMPASS,      UIELEM_BUILTIN_CROSS,
    UIELEM_BUILTIN_REDSTONE_TAB, UIELEM_BUILTIN_SIDEBAR,
};

static enum UITreeComponentType const walk_plain_types[] = {
    UIELEM_RS_LAYER, UIELEM_RS_RECT, UIELEM_RS_GRAPHIC, UIELEM_RS_TEXT, UIELEM_RS_MODEL,
};

static int
walk_node_depth(struct UITree const* tree, int32_t node)
{
    int depth = 0;
    while( node >= 0 )
    {
        node = tree->components[node].parent;
        depth++;
    }
    return depth;
}

static void
scene_free(struct WalkScene* scene)
{
    UITree_Free(scene->tree);
    scene->tree = NULL;
}

/*
 * One random scene.
 *
 * `allow_minimenu` exists because UITree_HitTest walks with host == NULL and
 * UITree_ComponentIsPassThrough asserts a host for UIELEM_BUILTIN_MINIMENU. A
 * host-less geometric walk over a minimenu node under the point therefore
 * aborts by contract -- in the reference exactly as in the rewrite -- so the
 * scenes that carry one are compared on the three host-bearing entry points
 * and UITree_PointQuery, and the geometric entry point gets every other scene.
 */
static void
scene_build(struct WalkScene* scene, uint32_t seed, int allow_minimenu, struct WalkCoverage* cov)
{
    uint32_t rng = seed ? seed : 1u;
    int const target = walk_chance(&rng, 15) ? walk_range(&rng, 1200, 3000)
                                             : walk_range(&rng, 50, 420);
    int const root_count = walk_range(&rng, 1, 3);
    int world_placed = 0;

    memset(scene, 0, sizeof(*scene));
    scene->tree = UITree_New(64);
    assert(scene->tree);
    UITree_TestHostInit(&scene->host, &scene->state);
    scene->state.selected_tab = walk_range(&rng, 0, 3);
    scene->state.cross_active = walk_chance(&rng, 50);
    scene->state.minimenu_visible = walk_chance(&rng, 50);
    scene->state.minimap_hidden = walk_chance(&rng, 40);
    scene->state.compass_hidden = walk_chance(&rng, 40);
    scene->state.if_events_every = walk_chance(&rng, 50) ? walk_range(&rng, 2, 5) : 0;

    for( int i = 0; i < root_count; i++ )
    {
        struct UITreeNodeSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_RS_LAYER;
        spec.component_id = (WALK_GROUP_BASE + i) << 16;
        spec.width = WALK_CANVAS_W;
        spec.height = WALK_CANVAS_H;
        scene->nodes[scene->node_count++] = UITree_Push(scene->tree, -1, &spec);
    }

    while( scene->node_count < target )
    {
        struct UITreeNodeSpec spec;
        int32_t node;
        /* Bias towards the most recent nodes so the trees get genuinely deep --
         * depth is what the old ancestor re-walks charged for. */
        int const pool = scene->node_count;
        int parent_slot = walk_chance(&rng, 70) ? walk_range(&rng, pool > 12 ? pool - 12 : 0, pool - 1)
                                                : walk_range(&rng, 0, pool - 1);
        int32_t parent = scene->nodes[parent_slot];
        int const group = WALK_GROUP_BASE + (parent_slot % root_count);
        enum UITreeComponentType type;

        memset(&spec, 0, sizeof(spec));
        if( allow_minimenu && walk_chance(&rng, 2) )
            type = UIELEM_BUILTIN_MINIMENU;
        else if( walk_chance(&rng, 14) )
            type = walk_builtin_types[walk_rand(&rng) % (sizeof(walk_builtin_types) /
                                                         sizeof(walk_builtin_types[0]))];
        else if( !world_placed && walk_chance(&rng, 4) )
            type = UIELEM_BUILTIN_WORLD;
        else if( walk_chance(&rng, 6) )
            type = UIELEM_RS_INV;
        else
            type = walk_plain_types[walk_rand(&rng) %
                                    (sizeof(walk_plain_types) / sizeof(walk_plain_types[0]))];

        spec.type = type;
        spec.component_id = (group << 16) | (scene->node_count + 1);
        /* Boxes stay near the parent so a deep subtree does not simply walk off
         * the canvas and stop being hit by anything. A few are collapsed on
         * purpose (UITree_LayerCullsChildren). */
        spec.x = walk_range(&rng, -30, 70);
        spec.y = walk_range(&rng, -30, 60);
        spec.width = walk_chance(&rng, 8) ? 0 : walk_range(&rng, 8, 220);
        spec.height = walk_chance(&rng, 8) ? 0 : walk_range(&rng, 8, 180);

        switch( type )
        {
        case UIELEM_RS_RECT:
            spec.u.rs_rect.color = 0x304050;
            spec.u.rs_rect.filled = 1;
            break;
        case UIELEM_RS_INV:
            spec.u.rs_inv.cols = walk_range(&rng, 1, 4);
            spec.u.rs_inv.rows = walk_range(&rng, 1, 7);
            spec.u.rs_inv.margin_x = walk_range(&rng, 0, 8);
            spec.u.rs_inv.margin_y = walk_range(&rng, 0, 8);
            scene->has_inv = 1;
            break;
        case UIELEM_BUILTIN_SIDEBAR:
            spec.u.sidebar.tabno = walk_range(&rng, 0, 4);
            scene->has_sidebar = 1;
            break;
        case UIELEM_BUILTIN_REDSTONE_TAB:
            spec.u.redstone_tab.tabno = walk_range(&rng, 0, 4);
            break;
        case UIELEM_BUILTIN_MINIMENU:
            spec.u.minimenu.font_id = 0;
            scene->has_minimenu = 1;
            break;
        case UIELEM_BUILTIN_WORLD:
            world_placed = 1;
            scene->has_world = 1;
            break;
        default:
            break;
        }

        /* Cache ops / a click mask are what stop a node being decoration. */
        if( walk_chance(&rng, 25) )
            snprintf(spec.menu_options.ops[walk_range(&rng, 0, 1)],
                     UITREE_MENU_OPTION_LEN, "Use");
        node = UITree_Push(scene->tree, parent, &spec);
        assert(node >= 0);
        scene->nodes[scene->node_count++] = node;

        {
            struct UITreeComponent* component = &scene->tree->components[node];
            if( walk_chance(&rng, 15) )
                component->behavior.click_mask = 6;
            if( walk_chance(&rng, 12) )
            {
                UITree_HooksMut(component)->on_click.script_id = 42;
                UITree_HooksMut(component)->on_mouse_over.script_id = 43;
            }
            /* The three things that make a node self-report as hovered. */
            if( walk_chance(&rng, 10) )
                UITree_HooksMut(component)->on_mouse_repeat.script_id = 44;
            if( walk_chance(&rng, 10) )
                component->behavior.over_color = 0x00ff00;
            if( walk_chance(&rng, 6) )
                component->behavior.over_layer_id = spec.component_id;
            if( walk_chance(&rng, 8) )
                component->item_id = walk_range(&rng, 1, 9999);
            if( walk_chance(&rng, 8) )
            {
                component->no_click_through = 1;
                scene->has_barrier = 1;
            }
            /* Low per-node odds on purpose: any hidden flag prunes a whole
             * subtree, and at 5% a tree 40 deep is dead before the point ever
             * reaches a leaf. */
            if( walk_chance(&rng, 2) ) { component->behavior.hide = 1; scene->has_hidden_flag = 1; }
            if( walk_chance(&rng, 2) ) { component->mount_hidden = 1; scene->has_hidden_flag = 1; }
            if( walk_chance(&rng, 2) ) { component->screen_hidden = 1; scene->has_hidden_flag = 1; }
            if( walk_chance(&rng, 2) ) { component->projection_hidden = 1; scene->has_hidden_flag = 1; }
            if( walk_chance(&rng, 2) ) { component->widget_hidden = 1; scene->has_hidden_flag = 1; }
            if( walk_chance(&rng, 2) ) { component->frame_hidden = 1; scene->has_hidden_flag = 1; }
            if( walk_chance(&rng, 5) )
            {
                UITree_SetComponentDragActive(scene->tree, node, 1);
                component->drag_visual_x = walk_range(&rng, -40, 240);
                component->drag_visual_y = walk_range(&rng, -40, 200);
                scene->has_drag = 1;
            }
            if( type == UIELEM_RS_LAYER && walk_chance(&rng, 18) )
            {
                UITree_SetScrollSizeAt(scene->tree, node, walk_range(&rng, 0, 400),
                                       walk_range(&rng, 0, 400));
                UITree_SetScrollPosAt(scene->tree, node, walk_range(&rng, 0, 120),
                                      walk_range(&rng, 0, 120));
                scene->has_scroll = 1;
            }
        }
    }

    /* Mounted sub-interfaces: a child whose id group differs from its host's is
     * a mount when the host carries an InterfaceParent record for that group.
     * Type 0 is the modal barrier the walks treat specially. */
    for( int i = 0; i < 4; i++ )
    {
        int const host_slot = walk_range(&rng, 0, scene->node_count - 1);
        int32_t const host_node = scene->nodes[host_slot];
        int32_t const child = scene->tree->components[host_node].first_child;
        if( child < 0 )
            continue;
        scene->tree->components[child].component_id =
            (WALK_MOUNT_GROUP << 16) | (int)(walk_rand(&rng) % 4000u);
        UITree_InterfaceParentSet(scene->tree, scene->tree->components[host_node].component_id,
                                  WALK_MOUNT_GROUP, walk_range(&rng, 0, 1) ? 0 : 1);
        scene->has_mount = 1;
    }

    for( int i = 0; i < scene->node_count; i++ )
    {
        enum UITreeComponentType const type = scene->tree->components[scene->nodes[i]].type;
        if( type != UIELEM_BUILTIN_MINIMAP && type != UIELEM_BUILTIN_REDSTONE_TAB &&
            type != UIELEM_BUILTIN_COMPASS && type != UIELEM_BUILTIN_CROSS )
            continue;
        scene->has_hidden_builtin = 1;
        break;
    }

    /* Anchors last: they are what puts the tree on the depth path at all. */
    if( walk_chance(&rng, 70) )
    {
        int const edges = walk_range(&rng, 1, 4);
        for( int i = 0; i < edges; i++ )
        {
            int32_t const widget = scene->nodes[walk_range(&rng, 0, scene->node_count - 1)];
            int32_t const anchor_target = scene->nodes[walk_range(&rng, 0, scene->node_count - 1)];
            enum UITreeWidgetRelation const relation =
                walk_chance(&rng, 45) ? UITREE_WIDGET_RELATION_OVER
                                      : (walk_chance(&rng, 60) ? UITREE_WIDGET_RELATION_BEHIND
                                                               : UITREE_WIDGET_RELATION_REPLACE);
            if( UITree_WidgetSetAnchor(scene->tree, UITree_RefAt(scene->tree, widget), 7,
                                       UITree_RefAt(scene->tree, anchor_target),
                                       relation) == UITREE_WIDGET_ANCHOR_OK )
                scene->has_anchor = 1;
        }
    }

    UITree_TestResolve(scene->tree);

    cov->scenes++;
    cov->scenes_with_anchor += scene->has_anchor;
    cov->scenes_with_hidden_builtin += scene->has_hidden_builtin;
    cov->scenes_with_barrier += scene->has_barrier;
    cov->scenes_with_mount += scene->has_mount;
    cov->scenes_with_sidebar += scene->has_sidebar;
    cov->scenes_with_world += scene->has_world;
    cov->scenes_with_inv += scene->has_inv;
    cov->scenes_with_drag += scene->has_drag;
    cov->scenes_with_scroll += scene->has_scroll;
    cov->scenes_with_hidden_flag += scene->has_hidden_flag;
    cov->scenes_with_minimenu += scene->has_minimenu;
    if( scene->node_count > cov->max_nodes )
        cov->max_nodes = scene->node_count;
    for( int i = 0; i < scene->node_count; i++ )
    {
        int const depth = walk_node_depth(scene->tree, scene->nodes[i]);
        if( depth > cov->max_depth )
            cov->max_depth = depth;
    }
}

static int
host_request_total(struct TestHostState const* state)
{
    int total = 0;
    for( int i = 0; i < UITREE_HOST_REQUEST_COUNT; i++ )
        total += state->request_count[i];
    return total;
}

/* ------------------------------------------------------------------------- */
/* The differential run                                                       */
/* ------------------------------------------------------------------------- */

static void
scene_compare_point(struct WalkScene* scene, int px, int py, int warm, struct WalkCoverage* cov)
{
    struct UITree* tree = scene->tree;
    struct UITreeHost* host = &scene->host;
    int32_t ref_nodes[WALK_MAX_HITS];
    int32_t new_nodes[WALK_MAX_HITS];
    int ref_count, new_count;
    int32_t ref_interactive, new_interactive;
    int ref_blocks, new_blocks;
    int query_blocks = 0;
    int32_t query_hit = -1;
    int ref_hovered, new_hovered;
    uint32_t allocs_before, visits_before;
    uint32_t const allocs_at_entry = UITree_WalkScratchAllocs(tree);
    int requests_before;
    char message[160];

    cov->points++;

    /* --- geometric hit (host-less; see scene_build) --- */
    if( !scene->has_minimenu )
    {
        int32_t const ref_geo = ref_hit_test(tree, px, py);
        int32_t const new_geo = UITree_HitTest(tree, px, py);
        snprintf(message, sizeof(message), "HitTest at %d,%d: ref %d vs %d", px, py,
                 (int)ref_geo, (int)new_geo);
        TEST_ASSERT(ref_geo == new_geo, message);
        cov->comparisons++;
    }

    /* --- interactive hit --- */
    ref_interactive = ref_hit_test_interactive(tree, host, px, py);
    allocs_before = UITree_WalkScratchAllocs(tree);
    visits_before = UITree_WalkNodeVisits(tree);
    requests_before = host_request_total(&scene->state);
    new_interactive = UITree_HitTestInteractive(tree, host, px, py);
    snprintf(message, sizeof(message), "HitTestInteractive at %d,%d: ref %d vs %d", px, py,
             (int)ref_interactive, (int)new_interactive);
    TEST_ASSERT(ref_interactive == new_interactive, message);
    cov->comparisons++;
    if( new_interactive >= 0 )
        cov->hits++;

    if( warm )
    {
        uint32_t const visits = UITree_WalkNodeVisits(tree) - visits_before;
        int const requests = host_request_total(&scene->state) - requests_before;
        int const budget = (int)visits + requests;
        snprintf(message, sizeof(message), "one walk allocated (%u growths)",
                 UITree_WalkScratchAllocs(tree) - allocs_before);
        TEST_ASSERT(UITree_WalkScratchAllocs(tree) == allocs_before, message);
        cov->comparisons++;
        /* The ancestor re-walks made this O(n x depth); a linear budget is a
         * bound they could not meet on a tree this deep. */
        snprintf(message, sizeof(message), "walk budget: %u visits + %d requests over %d nodes",
                 visits, requests, scene->node_count);
        TEST_ASSERT(budget <= 2 * scene->node_count, message);
        cov->comparisons++;
        cov->warm_walks_measured++;
        if( budget * 100 / (2 * scene->node_count) > cov->worst_budget_percent )
            cov->worst_budget_percent = budget * 100 / (2 * scene->node_count);
    }

    /* --- world blocking --- */
    ref_blocks = ref_point_blocks_world(tree, host, px, py);
    new_blocks = UITree_PointBlocksWorld(tree, host, px, py);
    snprintf(message, sizeof(message), "PointBlocksWorld at %d,%d: ref %d vs %d", px, py,
             ref_blocks, new_blocks);
    TEST_ASSERT(ref_blocks == new_blocks, message);
    cov->comparisons++;
    if( new_blocks )
        cov->barrier_answers++;

    /* --- both at once --- */
    UITree_PointQuery(tree, host, px, py, &query_blocks, &query_hit);
    snprintf(message, sizeof(message), "PointQuery blocks at %d,%d: %d vs %d", px, py,
             query_blocks, ref_blocks);
    TEST_ASSERT(query_blocks == ref_blocks, message);
    snprintf(message, sizeof(message), "PointQuery hit at %d,%d: %d vs %d", px, py, (int)query_hit,
             (int)ref_interactive);
    TEST_ASSERT(query_hit == ref_interactive, message);
    cov->comparisons += 2;

    /* --- menu stack --- */
    memset(ref_nodes, 0xff, sizeof(ref_nodes));
    memset(new_nodes, 0xff, sizeof(new_nodes));
    ref_count = ref_collect_nodes_at(tree, host, px, py, ref_nodes, WALK_MAX_HITS);
    visits_before = UITree_WalkNodeVisits(tree);
    requests_before = host_request_total(&scene->state);
    new_count = UITree_CollectNodesAt(tree, host, px, py, new_nodes, WALK_MAX_HITS);
    if( warm )
    {
        uint32_t const visits = UITree_WalkNodeVisits(tree) - visits_before;
        int const requests = host_request_total(&scene->state) - requests_before;
        snprintf(message, sizeof(message),
                 "CollectNodesAt budget: %u visits + %d requests over %d nodes", visits, requests,
                 scene->node_count);
        TEST_ASSERT((int)visits + requests <= 2 * scene->node_count, message);
        cov->comparisons++;
    }
    snprintf(message, sizeof(message), "CollectNodesAt count at %d,%d: ref %d vs %d", px, py,
             ref_count, new_count);
    TEST_ASSERT(ref_count == new_count, message);
    cov->comparisons++;
    cov->menu_rows += new_count;
    if( ref_count == new_count )
    {
        for( int i = 0; i < ref_count; i++ )
        {
            snprintf(message, sizeof(message), "CollectNodesAt[%d] at %d,%d: ref %d vs %d", i, px,
                     py, (int)ref_nodes[i], (int)new_nodes[i]);
            TEST_ASSERT(ref_nodes[i] == new_nodes[i], message);
            cov->comparisons++;
        }
    }

    /* --- hover, whole canvas and one sub-region --- */
    ref_hovered = ref_find_hovered_for_region(tree, host, -1, px, py, 0, 0, WALK_CANVAS_W,
                                              WALK_CANVAS_H);
    new_hovered = UITree_FindHoveredComponentIdForRegion(tree, host, -1, px, py, 0, 0,
                                                         WALK_CANVAS_W, WALK_CANVAS_H);
    snprintf(message, sizeof(message), "hover at %d,%d: ref %d vs %d", px, py, ref_hovered,
             new_hovered);
    TEST_ASSERT(ref_hovered == new_hovered, message);
    cov->comparisons++;
    if( new_hovered >= 0 )
        cov->hover_answers++;

    {
        int32_t const region_root = scene->nodes[(px * 7 + py * 13 + scene->node_count) %
                                                 scene->node_count];
        ref_hovered = ref_find_hovered_for_region(tree, host, region_root, px, py, 0, 0,
                                                  WALK_CANVAS_W, WALK_CANVAS_H);
        new_hovered = UITree_FindHoveredComponentIdForRegion(tree, host, region_root, px, py, 0, 0,
                                                             WALK_CANVAS_W, WALK_CANVAS_H);
        snprintf(message, sizeof(message), "hover region %d at %d,%d: ref %d vs %d",
                 (int)region_root, px, py, ref_hovered, new_hovered);
        TEST_ASSERT(ref_hovered == new_hovered, message);
        cov->comparisons++;
    }

    /* The whole point of the scratch: it grows on the first walk of a scene and
     * never again, however many walks the scene then runs. */
    if( warm )
    {
        snprintf(message, sizeof(message), "warm point at %d,%d grew the scratch %u times", px, py,
                 UITree_WalkScratchAllocs(tree) - allocs_at_entry);
        TEST_ASSERT(UITree_WalkScratchAllocs(tree) == allocs_at_entry, message);
        cov->comparisons++;
    }
}

/*
 * Touch every walk once, at a point that is inside the hover region, so that
 * each scratch slot has been grown before the measured points start. Without
 * it the first measured point can be a node centre that lies off the canvas,
 * which makes UITree_FindHoveredComponentIdForRegion return before it borrows
 * the hover slot -- and the slot then grows on some later point, for a reason
 * that has nothing to do with the walks reallocating.
 */
static int
scene_warm(struct WalkScene* scene)
{
    struct UITree* tree = scene->tree;
    int const px = WALK_CANVAS_W / 2;
    int const py = WALK_CANVAS_H / 2;
    uint32_t const before = UITree_WalkScratchAllocs(tree);
    int32_t nodes[WALK_MAX_HITS];
    int blocks = 0;
    int32_t hit = -1;

    if( !scene->has_minimenu )
        (void)UITree_HitTest(tree, px, py);
    (void)UITree_HitTestInteractive(tree, &scene->host, px, py);
    (void)UITree_PointBlocksWorld(tree, &scene->host, px, py);
    UITree_PointQuery(tree, &scene->host, px, py, &blocks, &hit);
    (void)UITree_CollectNodesAt(tree, &scene->host, px, py, nodes, WALK_MAX_HITS);
    (void)UITree_FindHoveredComponentIdForRegion(tree, &scene->host, -1, px, py, 0, 0,
                                                 WALK_CANVAS_W, WALK_CANVAS_H);
    (void)UITree_FindHoveredComponentIdForRegion(tree, &scene->host, scene->nodes[0], px, py, 0, 0,
                                                 WALK_CANVAS_W, WALK_CANVAS_H);
    return UITree_WalkScratchAllocs(tree) > before;
}

void
test_input_walk_equivalence(void)
{
    struct WalkCoverage cov;
    int const scene_count = 48;

    printf("TEST: input hit walks match the pre-optimisation reference\n");
    memset(&cov, 0, sizeof(cov));

    for( int s = 0; s < scene_count; s++ )
    {
        struct WalkScene scene;
        uint32_t rng;
        int const points = 26;

        scene_build(&scene, 0x9e3779b9u + (uint32_t)s * 2654435761u, (s % 6) == 5, &cov);
        cov.scenes_that_grew_scratch += scene_warm(&scene);
        rng = 0x1234567u + (uint32_t)s;

        for( int p = 0; p < points; p++ )
        {
            int px, py;
            if( p % 2 == 0 )
            {
                /* Aim at a node so the walks actually resolve hits. */
                int32_t const node = scene.nodes[walk_range(&rng, 0, scene.node_count - 1)];
                int bx = 0, by = 0, bw = 0, bh = 0;
                UITree_LayoutGetBounds(&scene.tree->components[node].position, &bx, &by, &bw, &bh);
                px = bx + bw / 2 + walk_range(&rng, -2, 2);
                py = by + bh / 2 + walk_range(&rng, -2, 2);
            }
            else
            {
                px = walk_range(&rng, -12, WALK_CANVAS_W + 12);
                py = walk_range(&rng, -12, WALK_CANVAS_H + 12);
            }
            /* Every point is measured: scene_warm has already grown the slots. */
            scene_compare_point(&scene, px, py, 1, &cov);
        }

        scene_free(&scene);
    }

    printf("      %d scenes (max %d nodes, max depth %d), %d points, %lld comparisons\n",
           cov.scenes, cov.max_nodes, cov.max_depth, cov.points, cov.comparisons);
    printf("      with anchors %d | hidden-capable builtins %d | barriers %d | mounts %d\n",
           cov.scenes_with_anchor, cov.scenes_with_hidden_builtin, cov.scenes_with_barrier,
           cov.scenes_with_mount);
    printf("      sidebars %d | world %d | inv grids %d | drags %d | scroll %d | hidden flags %d | "
           "minimenu %d\n",
           cov.scenes_with_sidebar, cov.scenes_with_world, cov.scenes_with_inv,
           cov.scenes_with_drag, cov.scenes_with_scroll, cov.scenes_with_hidden_flag,
           cov.scenes_with_minimenu);
    printf("      interactive hits %d | world-blocked answers %d | menu rows %d | hovered %d\n",
           cov.hits, cov.barrier_answers, cov.menu_rows, cov.hover_answers);
    printf("      scratch grew in %d scenes, never again in %d warm walks; worst walk used %d%% of "
           "the 2n budget\n",
           cov.scenes_that_grew_scratch, cov.warm_walks_measured, cov.worst_budget_percent);

    TEST_ASSERT(cov.scenes_with_anchor > 0, "some scenes put the tree on the anchor depth path");
    TEST_ASSERT(cov.scenes_with_anchor < cov.scenes, "some scenes stay on the native path");
    TEST_ASSERT(cov.scenes_with_hidden_builtin > 0, "some scenes carry host-gated builtins");
    TEST_ASSERT(cov.scenes_with_barrier > 0, "some scenes carry no_click_through barriers");
    TEST_ASSERT(cov.scenes_with_mount > 0, "some scenes carry mounted sub-interfaces");
    TEST_ASSERT(cov.scenes_with_sidebar > 0, "some scenes carry sidebar tabs");
    TEST_ASSERT(cov.scenes_with_world > 0, "some scenes carry a world node");
    TEST_ASSERT(cov.scenes_with_inv > 0, "some scenes carry inventory grids");
    TEST_ASSERT(cov.scenes_with_drag > 0, "some scenes carry a dragged node");
    TEST_ASSERT(cov.scenes_with_scroll > 0, "some scenes carry scrolled layers");
    TEST_ASSERT(cov.scenes_with_hidden_flag > 0, "some scenes carry hidden nodes");
    TEST_ASSERT(cov.scenes_with_minimenu > 0, "some scenes carry a minimenu builtin");
    TEST_ASSERT(cov.max_nodes >= 1200, "the run includes trees of thousands of nodes");
    TEST_ASSERT(cov.max_depth >= 8, "the run includes deep trees");
    TEST_ASSERT(cov.hits > 0, "the points actually resolve to interactive nodes");
    TEST_ASSERT(cov.barrier_answers > 0, "the points actually resolve to blocked world clicks");
    TEST_ASSERT(cov.menu_rows > 0, "the points actually collect menu rows");
    TEST_ASSERT(cov.hover_answers > 0, "the points actually resolve hovered components");
    TEST_ASSERT(cov.scenes_that_grew_scratch == cov.scenes_with_anchor,
                "every anchored scene grows the scratch exactly once, on its first walk");
    TEST_ASSERT(cov.warm_walks_measured > 0, "the zero-allocation claim was actually measured");
}
