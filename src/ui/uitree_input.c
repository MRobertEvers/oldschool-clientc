#include "uitree_input.h"

#include "perf/torirs_perf.h"
#include "uitree_inv_view.h"
#include "uitree_layout.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Screen click (px,py) landed on one of an RS_INV grid's slot rects. Inventory
 * components carry cols/rows in their base width/height (4x7 for the backpack),
 * so the node's own layout bounds are only a few pixels — the drawn slots span
 * far past them. The reference hit-tests each 32x32 slot rect directly
 * (addComponentOptions TYPE_INV), so collection must too, or the whole grid is
 * invisible to the right-click menu. scroll_off is folded into the click the
 * same way add_inv_slot_rows does (UITree_AccumScrollOffset). */
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
    layout.offset_x = component->u.rs_inv.inv_slot_offset_x;
    layout.offset_y = component->u.rs_inv.inv_slot_offset_y;

    return UITree_InvViewGridHitTest(
               bx, by, &layout, px + scroll_off_x, py + scroll_off_y) >= 0;
}

bool
UITree_PointInComponent(
    struct UITreeElemPosition const* position,
    int px,
    int py)
{
    if( !position )
        return false;

    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    UITree_LayoutGetBounds(position, &x, &y, &w, &h);
    if( w <= 0 || h <= 0 )
        return false;
    return px >= x && px < x + w && py >= y && py < y + h;
}

static bool
rs_node_is_decorative_passthrough(struct UITreeComponent const* component)
{
    if( component->behavior.button_type != 0 || component->behavior.client_code != 0 )
        return false;
    if( component->behavior.click_mask != 0 )
        return false;
    if( component->menu_options.option[0] != '\0' )
        return false;
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        if( component->menu_options.ops[i][0] != '\0' )
            return false;
    }
    return true;
}

bool
UITree_ComponentIsPassThrough(
    struct UITreeComponent const* component,
    struct UITreeHost const* host)
{
    assert(component);

    /* Runtime click/op/hold/drag hooks make the node a real click target even
     * if it is a layer or decorative graphic (scrollbar arrows are hold-only). */
    struct UITreeRuntimeHooks const* hooks = UITree_Hooks(component);
    if( hooks->on_click.script_id > 0 || hooks->on_op.script_id > 0 ||
        hooks->on_hold.script_id > 0 || hooks->on_drag.script_id > 0 ||
        UITree_ComponentIsDraggable(component) )
        return false;

    switch( component->type )
    {
    case UIELEM_BUILTIN_WORLD:
    case UIELEM_BUILTIN_SIDEBAR:
    case UIELEM_BUILTIN_CHAT:
        return true;
    case UIELEM_RS_LAYER:
        /*
         * A layer is *usually* structure and must not eat clicks — but "usually"
         * is not "always", and the cache says which. `stats:attack` is a
         * `type=0` layer with `clickmask=6` and `op1=*`/`op2=*`: the whole cell
         * is the button, and script 393 fills its op text in
         * (`if_setop(2, "View <skill> guide", $component0)`) while the graphics
         * and numbers stacked on top of it are ops-less children.
         *
         * Returning `true` unconditionally here made every such cell
         * unclickable: the children are decorative and correctly refuse the
         * hit, and the parent that owns the ops refused it too, so the click
         * resolved to no component at all and no menu was ever built.
         *
         * Deliberately NARROWER than `rs_node_is_decorative_passthrough`, which
         * the graphic/text/rect arms use: that predicate also makes a node
         * interactive for a non-zero `button_type` or `client_code`, and on a
         * *layer* those two catch structural slots that were never meant to be
         * click targets. Measured: reusing it verbatim made `toplevel:sidemodal`
         * (161:74, `clientcode=1354`, 190x261 over the whole sidebar) start
         * picking, so empty sidebar space answered with a Cancel-only menu
         * where it used to fall through. Real components mounted inside it
         * still won — the hit test takes the topmost — so nothing broke, but it
         * is a behaviour change with no reason behind it.
         *
         * A click mask or an actual op is the cache saying "this is a button".
         * That is the whole of what this arm needs, and it leaves every other
         * layer exactly as pass-through as it was.
         */
        if( component->behavior.click_mask != 0 )
            return false;
        if( component->menu_options.option[0] != '\0' )
            return false;
        for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
        {
            if( component->menu_options.ops[i][0] != '\0' )
                return false;
        }
        return true;
    case UIELEM_BUILTIN_CROSS:
    {
        assert(host);
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_CROSS_ACTIVE };
        return UITree_Host(host, &req) == 0;
    }
    case UIELEM_BUILTIN_MINIMENU:
    {
        assert(host);
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_MINIMENU_VISIBLE };
        return UITree_Host(host, &req) == 0;
    }
    case UIELEM_BUILTIN_HOVERTEXT:
    case UIELEM_BUILTIN_ENTITY_OVERLAY:
        /* Purely decorative overlays (the "Walk here /..." line; health bars
         * and hitsplats); they must never eat world clicks.
         *
         * These are pushed as *late* root siblings, and UITree_HitTestInteractive
         * lets a later root's hit win over earlier ones — so a non-passthrough
         * unsized overlay node here does not just shadow the world, it shadows
         * the entire interface. Anything added to app_push_builtin_overlay_nodes
         * needs an entry in this switch. */
        return true;
    case UIELEM_RS_INV:
    case UIELEM_RS_INV_TEXT:
        return true;
    case UIELEM_RS_GRAPHIC:
    case UIELEM_RS_TEXT:
    case UIELEM_RS_RECT:
    case UIELEM_RS_MODEL:
    case UIELEM_RS_LINE:
        if( !rs_node_is_decorative_passthrough(component) )
            return false;
        /* Cache clickmask/ops make continue prompts live; choice rows are
         * cc_created TEXT with neither. At rev 230 the server arms them via
         * IF_SETEVENTS on the parent container (sub range) — without consulting
         * that mask they stay decorative and every mouse click falls through. */
        if( host )
        {
            struct UITreeHostRequest req = {
                .kind = UITREE_HOST_GET_IF_EVENTS,
                .u.get_if_events.com_id = component->component_id,
            };
            if( UITree_Host(host, &req) != 0 )
                return false;
        }
        return true;
    default:
        return false;
    }
}

/*
 * Returns the topmost interactive hit in the subtree, or -1. When a node with
 * no_click_through geometrically contains the point, *out_blocks is set so the
 * caller discards anything rendered *under* this subtree (mirrors the reference's
 * collectWidgetsAtPoint noClickThrough slice — a modal panel eats click-through).
 *
 * `*out_blocks_world` answers a second, wider question the world gate asks:
 * does the interface stop input reaching the SCENE here, whether or not any
 * widget wanted the click itself. Two things set it, and they are the two the
 * reference has (xrsps `findBlockingWidgetInHits`, which is `isPointOverWidget`
 * — "should the UI consume this world click"):
 *
 *   - a `noClickThrough` layer covering the point. In the reference this is
 *     the cache field on type-0 records, decoded beside scrollWidth/Height, and
 *     `Cs1ScriptRunner:542` resets the whole minimenu to Cancel when one is
 *     under the pointer — i.e. it discards the world rows the scene pass added.
 *   - the root of a **modal** sub-interface mount (IF_OPENSUB type 0). This is
 *     what makes an open bank opaque: `bankmain` declares `noclickthrough` only
 *     on small inner layers (capacity, tag popups, dropdown), never on its own
 *     root, so the flag alone does not cover the panel. The mount type does.
 *
 * It deliberately does NOT include "some widget here is interactive" — the
 * caller already tests that separately, and folding the two would make a
 * hovered chat line block the wheel.
 */
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

    /* Match emit: hidden subtrees are not interactive. */
    if( component->behavior.hide )
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

    int32_t hit = -1;
    if( point_in_self && !UITree_ComponentIsPassThrough(component, host) &&
        UITree_ComponentHitTestVisibleHost(component, -1, host) )
        hit = node_index;

    /* A no_click_through node covering the point blocks click-through to nodes
     * rendered underneath it (even if the node itself is a passthrough container). */
    int blocks = (point_in_self && component->no_click_through) ? 1 : 0;
    int blocks_world = blocks;

    if( point_in_self && !blocks_world && component->parent >= 0 &&
        (uint32_t)component->parent < tree->component_count )
        blocks_world = UITree_ChildMountType(
                           tree,
                           tree->components[component->parent].component_id,
                           component) == 0;

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
        /* Canonical scroll offset lives on the component (emit + CS2 opcodes
         * and the scrollbar hit path all read it). */
        if( UITree_ScrollLayerNeedsHorizontal(component) )
            child_scroll_x += component->scroll_x;
        if( UITree_ScrollLayerNeedsVertical(component) )
            child_scroll_y += component->scroll_y;
    }

    /* Inactive sidebar tabs already returned above, so all sidebars reaching
     * here are active — recurse unconditionally. */
    for( int32_t child = component->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        int child_blocks = 0;
        int child_blocks_world = 0;
        int32_t child_hit = hit_test_interactive_recursive(
            tree, host, child, px, py,
            child_scroll_x, child_scroll_y, &child_clip, &child_surface, &child_blocks,
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
        /* World blocking only accumulates: a sibling drawn later cannot un-block
         * what an earlier one covered, because both are still on screen. */
        if( child_blocks_world )
            blocks_world = 1;
    }

    if( out_blocks )
        *out_blocks = blocks;
    if( out_blocks_world )
        *out_blocks_world = blocks_world;
    return hit;
}

int32_t
UITree_HitTestRecursive(
    struct UITree const* tree,
    int32_t node_index,
    int px,
    int py)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return -1;

    struct UITreeComponent const* component = &tree->components[node_index];

    /* Match emit: hidden subtrees are not interactive. */
    if( component->behavior.hide )
        return -1;

    int32_t hit = -1;
    /* Layers are containers only — do not claim the hit themselves. Empty overlay
     * layers otherwise steal clicks from hooked widgets underneath. */
    if( component->type != UIELEM_RS_LAYER &&
        UITree_PointInComponent(&component->position, px, py) )
        hit = node_index;

    for( int32_t child = component->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        int32_t child_hit = UITree_HitTestRecursive(tree, child, px, py);
        if( child_hit >= 0 )
            hit = child_hit;
    }

    return hit;
}

int32_t
UITree_HitTest(
    struct UITree const* tree,
    int px,
    int py)
{
    assert(tree);
    if( tree->root_index < 0 )
        return -1;

    int32_t hit = -1;
    for( int32_t root = tree->root_index; root >= 0;
         root = tree->components[root].next_sibling )
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

struct collect_nodes_ctx
{
    int32_t* out;
    int max;
    int count;
    /** Entries below this index were drawn under a no_click_through panel. */
    int barrier;
};

/* Same traversal rules as hit_test_interactive_recursive, but appends every
 * menu-relevant containing node in RENDER order (under -> top); the caller
 * slices at the barrier and reverses for top-most-first. */
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

    if( clip && clip->clip_w > 0 && clip->clip_h > 0 && !UITree_PointInClip(px, py, clip) )
        return;

    struct UITreeComponent const* component = &tree->components[node_index];

    if( component->behavior.hide )
        return;

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
        UITree_PointInScrolledBounds(px, py, bx, by, bw, bh, scroll_off_x, scroll_off_y);

    /* A blocking panel discards everything rendered under it — including
     * entries already collected — but keeps itself and its subtree. */
    if( point_in_self && component->no_click_through && ctx->count > ctx->barrier )
        ctx->barrier = ctx->count;

    /* An RS_INV grid's clickable area is the union of its slot rects, not its
     * (cols x rows)-pixel layout bounds — collect it when a slot is hit even if
     * the click misses the tiny node box. */
    bool const inv_slot_hit =
        collect_inv_grid_slot_hit(component, bx, by, px, py, scroll_off_x, scroll_off_y);

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
                             component->menu_options.option[0] != '\0' ||
                             component->type == UIELEM_BUILTIN_CHAT;
        /* A script-created cell holding an obj is a menu target on the strength
         * of the obj alone. It carries no ops and no hook of its own — the
         * rev-230 worn tab puts "Remove" on the slot LAYER, not on the item
         * child — so every other test here calls it pass-through chrome and
         * drops it, and the equipment slots become unclickable. */
        bool const has_obj = component->item_id > 0;
        if( (inv_grid || has_ops || has_obj ||
             !UITree_ComponentIsPassThrough(component, host)) &&
            ctx->count < ctx->max )
            ctx->out[ctx->count++] = node_index;
    }

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
        if( UITree_ScrollLayerNeedsHorizontal(component) )
            child_scroll_x += component->scroll_x;
        if( UITree_ScrollLayerNeedsVertical(component) )
            child_scroll_y += component->scroll_y;
    }

    /* Inactive sidebar tabs already returned above (before the barrier); all
     * sidebars reaching here are active, so recurse unconditionally. */
    for( int32_t child = component->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        collect_nodes_recursive(
            tree, host, child, px, py, child_scroll_x, child_scroll_y, &child_clip, &child_surface,
            ctx);
    }
}

int
UITree_CollectNodesAt(
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

    for( int32_t root = tree->root_index; root >= 0;
         root = tree->components[root].next_sibling )
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

int32_t
UITree_HitTestInteractive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py)
{
    assert(tree);
    if( tree->root_index < 0 )
        return -1;

    int32_t hit = -1;
    for( int32_t root = tree->root_index; root >= 0;
         root = tree->components[root].next_sibling )
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

int
UITree_PointBlocksWorld(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py)
{
    assert(tree);
    if( tree->root_index < 0 )
        return 0;

    for( int32_t root = tree->root_index; root >= 0;
         root = tree->components[root].next_sibling )
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

struct UIInputResult
UITree_InputUpdate(
    struct UIInputState* state,
    struct UITree* tree,
    struct UITreeHost const* host,
    struct UIInputEvent event)
{
    struct UIInputResult result;
    int32_t const prev_hovered = state ? state->hovered : -1;

    memset(&result, 0, sizeof(result));
    result.hovered = state ? state->hovered : -1;
    result.prev_hovered = prev_hovered;
    result.clicked = -1;
    result.drag_source_idx = -1;
    result.drag_source_id = -1;
    result.drag_target_id = -1;

    assert(state);
    assert(tree);

    switch( event.kind )
    {
    case UI_INPUT_MOVE:
        state->hovered = UITree_HitTestInteractive(tree, host, event.x, event.y);
        break;

    case UI_INPUT_DOWN:
        state->hovered = UITree_HitTestInteractive(tree, host, event.x, event.y);
        state->pressed = state->hovered;
        state->drag_active = 0;
        state->drag_source_idx = -1;
        state->drag_source_id = -1;
        state->drag_target_id = -1;
        state->deferred_click = 0;
        state->release_click_suppressed = 0;
        state->drag_duration = 0;
        state->drag_click_x = event.x;
        state->drag_click_y = event.y;
        state->thresholds_set = 0;
        if( state->pressed >= 0 && (uint32_t)state->pressed < tree->component_count )
        {
            struct UITreeComponent const* c = &tree->components[state->pressed];
            int bx = 0, by = 0, bw = 0, bh = 0;
            int offx = 0, offy = 0;
            /* Reference dragTryPickup walks every widget under the cursor and
             * starts a drag before onclick. Our hit test is topmost-only, so a
             * full-size noclickthrough track (or decorative cap) sitting on the
             * thumb steals the press and never arms the dragger. Prefer a
             * draggable sibling that also contains the point (last = topmost). */
            if( !tree->anti_drag && !UITree_ComponentIsDraggable(c) && c->parent >= 0 &&
                (uint32_t)c->parent < tree->component_count )
            {
                int32_t prefer = -1;
                int32_t sibling = tree->components[c->parent].first_child;
                for( ; sibling >= 0; sibling = tree->components[sibling].next_sibling )
                {
                    struct UITreeComponent const* s = &tree->components[sibling];
                    int sbx = 0, sby = 0, sbw = 0, sbh = 0;
                    int soffx = 0, soffy = 0;
                    if( !UITree_ComponentIsDraggable(s) || s->behavior.hide )
                        continue;
                    UITree_LayoutGetBounds(&s->position, &sbx, &sby, &sbw, &sbh);
                    UITree_AccumScrollOffset(tree, sibling, &soffx, &soffy);
                    if( UITree_PointInScrolledBounds(
                            event.x, event.y, sbx, sby, sbw, sbh, soffx, soffy) )
                        prefer = sibling;
                }
                if( prefer >= 0 )
                {
                    state->pressed = prefer;
                    c = &tree->components[prefer];
                }
            }
            UITree_LayoutGetBounds(&c->position, &bx, &by, &bw, &bh);
            /* Pickup offset is against the DRAWN position: ancestor scroll
             * offsets shift the widget on screen while abs_* stays in content
             * space. Without this, grabbing a widget inside a scrolled layer
             * makes it jump by the scroll amount (reference records
             * _dragPickupOffset against screen coords, OsrsClient ~7407). */
            UITree_AccumScrollOffset(tree, state->pressed, &offx, &offy);
            state->drag_pickup_x = event.x - (bx - offx);
            state->drag_pickup_y = event.y - (by - offy);
            if( !tree->anti_drag && UITree_ComponentIsDraggable(c) )
            {
                /* Defer click until mouseup if drag never starts. */
                state->deferred_click = 1;
                state->drag_source_idx = state->pressed;
                state->drag_source_id = c->component_id;
            }
            else
            {
                /* Non-draggable with on_click/on_op: fire on press (Jagex
                 * Client loopLayer field1871 / xrsps widgetClickInput).
                 * Scrollbar tracks call cc_dragpickup from onclick on the
                 * press edge so the jump can run this frame; continuous drag
                 * from the track is not wanted — consume_pending one-shots.
                 * Hold-only chrome (arrow on_hold) must not get a synthetic
                 * click here. */
                struct UITreeRuntimeHooks const* hooks = UITree_Hooks(c);
                if( hooks->on_click.script_id > 0 || hooks->on_op.script_id > 0 )
                {
                    result.clicked = state->pressed;
                    result.press_click = 1;
                    state->release_click_suppressed = 1;
                }
            }
        }
        break;

    case UI_INPUT_UP:
    {
        int32_t const up_hit = UITree_HitTestInteractive(tree, host, event.x, event.y);
        state->hovered = up_hit;
        if( state->drag_active )
        {
            result.drag_ended = 1;
            result.drag_source_idx = state->drag_source_idx;
            result.drag_source_id = state->drag_source_id;
            result.drag_target_id = state->drag_target_id;
            if( state->drag_source_idx >= 0 &&
                (uint32_t)state->drag_source_idx < tree->component_count )
            {
                struct UITreeComponent* src = &tree->components[state->drag_source_idx];
                UITree_SetComponentDragActive(tree, state->drag_source_idx, 0);
                src->drag_visual_trans = -1;
            }
            /* Cancel deferred click. */
            state->deferred_click = 0;
        }
        else if( state->deferred_click && state->pressed >= 0 && state->pressed == up_hit )
        {
            result.clicked = up_hit;
            result.deferred_click_fired = 1;
        }
        else if(
            !state->deferred_click && !state->release_click_suppressed && state->pressed >= 0 &&
            state->pressed == up_hit )
        {
            result.clicked = up_hit;
        }
        state->pressed = -1;
        state->deferred_click = 0;
        state->release_click_suppressed = 0;
        state->drag_active = 0;
        state->drag_duration = 0;
        /* End of gesture: drop the source so a stale idx cannot resume drag. */
        state->drag_source_idx = -1;
        state->drag_source_id = -1;
        state->drag_target_id = -1;
        break;
    }
    }

    result.hovered = state->hovered;
    result.prev_hovered = prev_hovered;
    result.hover_changed = state->hovered != prev_hovered;
    return result;
}

int
UITree_InputDragTick(
    struct UIInputState* state,
    struct UITree* tree,
    struct UITreeHost const* host,
    int mouse_x,
    int mouse_y,
    int left_held)
{
    struct UITreeComponent* src;
    int changed = 0;
    int dist;
    int zone;
    int threshold;
    int target_x;
    int target_y;

    assert(state);
    assert(tree);
    (void)host;

    if( !left_held || state->drag_source_idx < 0 ||
        (uint32_t)state->drag_source_idx >= tree->component_count )
    {
        return 0;
    }

    src = &tree->components[state->drag_source_idx];
    if( tree->anti_drag && !state->drag_active )
        return 0;

    zone = src->drag_dead_zone;
    threshold = src->drag_dead_time;
    state->drag_duration++;

    {
        int dx = mouse_x - state->drag_click_x;
        int dy = mouse_y - state->drag_click_y;
        if( dx < 0 )
            dx = -dx;
        if( dy < 0 )
            dy = -dy;
        dist = dx > dy ? dx : dy;
    }

    if( !state->drag_active )
    {
        if( state->drag_duration > threshold && dist > zone )
        {
            state->drag_active = 1;
            UITree_SetComponentDragActive(tree, state->drag_source_idx, 1);
            src->drag_visual_trans = (src->drag_behavior == 1) ? -1 : 128;
            changed = 1;
        }
        else
            return 0;
    }

    target_x = mouse_x - state->drag_pickup_x;
    target_y = mouse_y - state->drag_pickup_y;

    /* Clamp only with explicit drag parent (flag depth or drag_render_area). */
    {
        int depth = UITree_ClickMaskDragDepth(src->behavior.click_mask);
        int32_t clamp_idx = -1;
        if( src->drag_render_area_uid >= 0 )
        {
            int32_t area = UITree_ResolveDragRenderArea(tree, src);
            if( area >= 0 )
                clamp_idx = area;
        }
        else if( depth > 0 )
        {
            int d;
            clamp_idx = src->parent;
            for( d = 1; d < depth && clamp_idx >= 0; d++ )
                clamp_idx = tree->components[clamp_idx].parent;
        }
        if( clamp_idx >= 0 && (uint32_t)clamp_idx < tree->component_count )
        {
            int cx, cy, cw, ch;
            int sw = 0, sh = 0;
            int coffx = 0, coffy = 0;
            UITree_LayoutGetBounds(&tree->components[clamp_idx].position, &cx, &cy, &cw, &ch);
            UITree_LayoutGetBounds(&src->position, NULL, NULL, &sw, &sh);
            /* Clamp rect in screen space: the render area itself may sit
             * inside a scrolled layer. */
            UITree_AccumScrollOffset(tree, clamp_idx, &coffx, &coffy);
            cx -= coffx;
            cy -= coffy;
            /* Keep the whole widget inside the area (reference: targetAbs +
             * widget size <= parentAbs + parent size) — a full-width dragger
             * then cannot wiggle horizontally at all. */
            if( cw > 0 && target_x + sw > cx + cw )
                target_x = cx + cw - sw;
            if( ch > 0 && target_y + sh > cy + ch )
                target_y = cy + ch - sh;
            if( target_x < cx )
                target_x = cx;
            if( target_y < cy )
                target_y = cy;
        }
    }

    if( src->drag_visual_x != target_x || src->drag_visual_y != target_y )
        changed = 1;
    src->drag_visual_x = target_x;
    src->drag_visual_y = target_y;

    state->drag_target_id =
        UITree_FindDropTarget(tree, mouse_x, mouse_y, state->drag_source_id);

    return changed || state->drag_active;
}
