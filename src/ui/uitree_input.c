#include <stdio.h>
#include <stdlib.h>
#include "uitree_input.h"

#include "perf/torirs_perf.h"
#include "uitree_inv_view.h"
#include "uitree_layout.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "log/torirs_log.h"

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
    layout.offset_x = UITree_InvSlots(component)->offset_x;
    layout.offset_y = UITree_InvSlots(component)->offset_y;

    return UITree_InvViewGridHitTest(bx, by, &layout, px + scroll_off_x, py + scroll_off_y) >= 0;
}

bool
UITree_PointInComponent(
    struct UITreeElemPosition const* position,
    int px,
    int py)
{
    assert(position);

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
    if( UITree_MenuOptions(component)->option[0] != '\0' )
        return false;
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        if( UITree_MenuOptions(component)->ops[i][0] != '\0' )
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
        hooks->on_hold.script_id > 0 || hooks->on_click_repeat.script_id > 0 ||
        hooks->on_release.script_id > 0 || hooks->on_drag.script_id > 0 ||
        UITree_ComponentIsDraggable(component) )
        return false;

    /* An IF3 text-entry field is a click target on the strength of BEING one.
     * It carries no op, no click mask and no hook -- `~torirs_cc_search_box`
     * creates the type-12 child and sets nothing but its font, its colour and
     * its input limits -- so every other test here calls it decoration and the
     * click falls through to the panel behind it. That is exactly what "the
     * text box does nothing" was: no menu row, no packet, no focus, nothing to
     * see. @see `rs_text.input` in uitree.h. */
    if( UITree_IsInputNode(component) )
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
        if( UITree_MenuOptions(component)->option[0] != '\0' )
            return false;
        for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
        {
            if( UITree_MenuOptions(component)->ops[i][0] != '\0' )
                return false;
        }
        return true;
    case UIELEM_BUILTIN_CROSS:
        /* The click marker is decoration, same as the two arms below — it is
         * never a target. Gating this on GET_CROSS_ACTIVE (which is what
         * ComponentVisibleHost/ComponentShouldEmit legitimately ask, and which
         * still drives whether it draws) made the node interactive for the 400ms
         * the cross animates: its layout box is 16x16 at the canvas origin, so
         * the top-left corner of the viewport went dead for the rest of every
         * click's marker. */
        return true;
    case UIELEM_BUILTIN_INKWELL:
    case UIELEM_BUILTIN_MULTIWAY:
    case UIELEM_BUILTIN_REBOOT_TIMER:
        /*
         * The rest of the decoration this switch's own rule ("every decorative
         * overlay type needs an entry") had not caught yet.
         *
         * The touch marker is the cross's twin and cost the same way: 64x64,
         * parked at the canvas ORIGIN when idle and following the finger when
         * not, a late root sibling -- so with no entry here it fell to
         * `default: return false` and won every hit test it covered. On the
         * mobile gameframe the top-left corner is where the logout, chat and
         * keyboard stones live: a TAP on them resolved to the marker and ran
         * nothing, while a long press still worked, because the minimenu is
         * built from components that carry ops and never sees this node.
         *
         * The multiway icon and the reboot countdown are the same kind of
         * thing drawn over the viewport: never targets, and a node that eats a
         * world click there is a dead patch of ground. A hook attached to any
         * of the three still makes it interactive -- that test is above.
         */
        return true;
    case UIELEM_BUILTIN_MINIMENU:
    {
        assert(host);
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_MINIMENU_VISIBLE };
        return UITree_Host(host, &req) == 0;
    }
    case UIELEM_BUILTIN_DEBUG_OVERLAY:
        /* Pass-through here even though the overlay *is* clickable: its node is
         * an unsized late root sibling, so making it interactive would shadow
         * the whole interface (see the note below). The overlay owns its own
         * hit test instead — the app offers the event to ToriRSChrome_Mouse* first
         * and only falls through to the tree when that returns 0. That also
         * keeps the module free of any dependency on ui/ input. */
        return true;
    case UIELEM_BUILTIN_HOVERTEXT:
    case UIELEM_BUILTIN_ENTITY_OVERLAY:
        /* Purely decorative overlays (the "Walk here /..." line; health bars
         * and hitsplats); they must never eat world clicks.
         *
         * RevConfig normally places these as late siblings, and
         * UITree_HitTestInteractive lets a later root's hit win over earlier
         * ones — so a non-passthrough unsized overlay node here does not just
         * shadow the world, it shadows the entire interface. Every decorative
         * overlay type needs an entry in this switch. */
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
 *   - the clipped host rectangle of a modal sub-interface (IF_OPENSUB type 0).
 *     The reference applies this barrier at the host before walking the mounted
 *     group, so blank space outside a smaller/offset mounted root is modal too.
 *     Overlay/tab mounts remain transparent unless one of their own layers
 *     raises noClickThrough.
 *
 * It deliberately does NOT include "some widget here is interactive" — the
 * caller already tests that separately, and folding the two would make a
 * hovered chat line block the wheel.
 */
/*
 * TORIRS_HIT_TRACE, read once.
 *
 * It used to be a getenv() per node, and the hit walk runs over the whole tree
 * twice a frame (app_world_mouse_gate asks PointBlocksWorld and then
 * HitTestInteractive at the same point). getenv on Windows is a linear scan of
 * the environment block, so an off-by-default trace was costing a scan per
 * component per walk per frame -- 14.3% of non-raster work on an in-world
 * frame, all of it spent deciding not to print.
 */
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
    if( component->behavior.hide || component->frame_hidden || component->screen_hidden ||
        component->replacement_hidden || component->projection_hidden )
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
    if( point_in_self && !UITree_ComponentIsPassThrough(component, host) &&
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
    if( component->behavior.hide || component->frame_hidden || component->screen_hidden ||
        component->replacement_hidden || component->projection_hidden )
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

struct collect_nodes_ctx
{
    int32_t* out;
    int max;
    int count;
    /** Entries below this index were drawn under a blocking panel/mount. */
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

    if( component->behavior.hide || component->frame_hidden || component->screen_hidden ||
        component->replacement_hidden || component->projection_hidden )
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

    if( (point_in_self || inv_slot_hit) && UITree_ComponentHitTestVisibleHost(component, -1, host) )
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
        if( (inv_grid || has_ops || has_obj || !UITree_ComponentIsPassThrough(component, host)) &&
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
            ctx->barrier = ctx->count;

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

/* A structural paint-order key for a native node and a semantic overlay
 * boundary. This deliberately mirrors emit_walk_node instead of relying on
 * array/component ids: dynamic nodes reuse both, mounted roots have their own
 * final sweep, and a picked-up drag subtree is emitted in a second global
 * pass. */
struct role_boundary_order
{
    struct UITree const* tree;
    struct UITreeHost const* host;
    int32_t candidate;
    int32_t anchor;
    uint32_t anchor_incarnation;
    bool replace;
    uint64_t sequence;
    uint64_t candidate_sequence;
    uint64_t boundary_sequence;
    /** Optional point query, sharing this exact structural sequence. A cover is
     * either an interactive native node or an input-blocking native boundary
     * (`noClickThrough` / a type-0 InterfaceParent) at the queried pixel. */
    int point_query;
    int point_x;
    int point_y;
    uint64_t cover_sequence;
    bool candidate_seen;
    bool boundary_seen;
    bool cover_seen;
};

static void
role_boundary_mark_node(
    struct role_boundary_order* order,
    int32_t node,
    struct UITreeComponent const* component,
    bool point_in_self,
    bool inv_slot_hit)
{
    order->sequence++;
    if( node == order->candidate )
    {
        order->candidate_sequence = order->sequence;
        order->candidate_seen = true;
    }
    /*
     * An inventory slot is cover, and it is the one target that reaches neither
     * half of the test beside it.
     *
     * A TYPE_INV node carries cols/rows in its layout width/height -- 4x7 for
     * the backpack -- so its own bounds are a few pixels and `point_in_self` is
     * false over nearly every slot the player can see; and `IsPassThrough` says
     * true for the type besides, because the click path resolves a slot rather
     * than the container. Collection has always compensated with its own grid
     * test (@see collect_inv_grid_slot_hit); this predicate did not, so it
     * answered "nothing native here" over an open inventory.
     *
     * What that broke is the FRAME-surface plugin region, which is skipped
     * exactly when a native widget covers the point. A frame plugin that
     * claimed its own panel rectangle -- mobile-gameframe's drawer does, to
     * stop a tap falling through to the world behind a floating panel -- then
     * won the point over every item in it, and app_plugin_region_at's caller
     * drops the game's rows wholesale, so the inventory answered with a
     * Cancel-only menu and no left click at all.
     */
    if( order->point_query &&
        ((point_in_self &&
          (component->no_click_through ||
           (!UITree_ComponentIsPassThrough(component, order->host) &&
            UITree_ComponentHitTestVisibleHost(component, -1, order->host)))) ||
         (inv_slot_hit && UITree_ComponentHitTestVisibleHost(component, -1, order->host))) )
    {
        order->cover_sequence = order->sequence;
        order->cover_seen = true;
    }
}

static void
role_boundary_mark_anchor(struct role_boundary_order* order)
{
    order->sequence++;
    order->boundary_sequence = order->sequence;
    order->boundary_seen = true;
}

/** A blocking boundary which has no native paint node of its own. Type-0
 * InterfaceParents raise this between the host's ordinary children and the
 * mounted subtree, so it needs a sequence position of its own. */
static void
role_boundary_mark_cover(struct role_boundary_order* order)
{
    order->sequence++;
    order->cover_sequence = order->sequence;
    order->cover_seen = true;
}

static void
role_boundary_walk_node(
    struct role_boundary_order* order,
    int32_t node,
    bool drag_pass,
    bool in_deferred,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeScrollClip const* clip,
    struct UITreeScrollClip const* surface)
{
    struct UITree const* tree;
    struct UITreeComponent const* component;
    struct UITreeScrollClip child_clip;
    struct UITreeScrollClip child_surface;
    int x, y, w, h;
    int child_scroll_x;
    int child_scroll_y;
    bool point_in_self = false;
    bool inv_slot_hit = false;

    assert(order);
    tree = order->tree;
    if( node < 0 || (uint32_t)node >= tree->component_count )
        return;
    component = &tree->components[node];
    if( component->freed || component->frame_hidden || component->screen_hidden ||
        component->projection_hidden || component->behavior.hide )
        return;

    /* Same selected-tab pruning as emit and interactive hit testing. */
    if( component->type == UIELEM_BUILTIN_SIDEBAR && order->host )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        if( UITree_Host(order->host, &req) != component->u.sidebar.tabno )
            return;
    }

    UITree_LayoutGetBounds(&component->position, &x, &y, &w, &h);
    if( UITree_LayerCullsChildren(component, w, h) )
        return;

    /* Replacement art is inserted before drag classification, exactly where
     * the native subtree becomes a tombstone. It appears only in the ordinary
     * pass; replacement-hidden descendants and unrelated replacements prune. */
    if( component->replacement_hidden )
    {
        if( !drag_pass && order->replace && node == order->anchor &&
            component->incarnation == order->anchor_incarnation )
            role_boundary_mark_anchor(order);
        return;
    }

    /* A picked-up component carries its whole subtree in screen space. Folding
     * that delta into the inherited scroll offset is the same transform the
     * interactive hit walk uses, while the structural half below still decides
     * whether this node belongs to the ordinary or deferred paint pass. */
    if( component->drag_active )
    {
        scroll_off_x -= component->drag_visual_x - (x - scroll_off_x);
        scroll_off_y -= component->drag_visual_y - (y - scroll_off_y);
    }
    if( order->point_query &&
        (!clip || clip->clip_w <= 0 || clip->clip_h <= 0 ||
         UITree_PointInClip(order->point_x, order->point_y, clip)) )
    {
        point_in_self = UITree_PointInScrolledBounds(
            order->point_x,
            order->point_y,
            x,
            y,
            w,
            h,
            scroll_off_x,
            scroll_off_y);
        /* Under the same clip as the bounds test above, because a slot scrolled
         * out of its viewport is not on screen to cover anything. */
        inv_slot_hit = collect_inv_grid_slot_hit(
            component, x, y, order->point_x, order->point_y, scroll_off_x, scroll_off_y);
    }

    if( component->drag_active && component->drag_behavior != 1 )
        in_deferred = true;

    if( in_deferred )
    {
        if( !drag_pass )
            return;
    }
    else if( drag_pass )
    {
        /* The drag pass descends through ordinary nodes without painting them
         * until it reaches a deferred drag source. */
        int const has_mounts = UITree_ContainerHasMounts(tree, component->component_id);
        for( int mount_sweep = 0; mount_sweep <= has_mounts; mount_sweep++ )
        {
            for( int32_t child = component->first_child; child >= 0;
                 child = tree->components[child].next_sibling )
            {
                int const is_mount =
                    has_mounts &&
                    UITree_ChildMountType(tree, component->component_id,
                                          &tree->components[child]) >= 0;
                if( is_mount != mount_sweep )
                    continue;
                role_boundary_walk_node(
                    order,
                    child,
                    drag_pass,
                    false,
                    scroll_off_x,
                    scroll_off_y,
                    clip,
                    surface);
            }
        }
        return;
    }

    /* A node's own native descriptors precede its children. Treat even a
     * visually empty interactive container as occupying that structural paint
     * position; that is the conservative input answer when it covers a plugin
     * region. */
    role_boundary_mark_node(order, node, component, point_in_self, inv_slot_hit);

    child_scroll_x = scroll_off_x;
    child_scroll_y = scroll_off_y;
    child_clip = clip ? *clip : (struct UITreeScrollClip){ 0 };
    child_surface = surface ? *surface : (struct UITreeScrollClip){ 0 };
    {
        struct UITreeScrollClip cc, cs;
        if( UITree_LayerChildClip(
                component,
                surface,
                x - scroll_off_x,
                y - scroll_off_y,
                w,
                h,
                &cc,
                &cs) )
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

    {
        int const mount_rec = UITree_InterfaceParentFind(tree, component->component_id);
        int const has_mounts = UITree_ContainerHasMounts(tree, component->component_id);
        int const mount_type = mount_rec >= 0 ? tree->interface_parents[mount_rec].type : -1;
        for( int mount_sweep = 0; mount_sweep <= has_mounts; mount_sweep++ )
        {
            /* Reference input installs a modal barrier at the host boundary,
             * after ordinary children and before its mounted interface. It can
             * cover an earlier role overlay even when the mounted root has no
             * interactive child at this pixel. */
            if( order->point_query && mount_sweep == 1 && mount_type == 0 &&
                point_in_self )
                role_boundary_mark_cover(order);
            for( int32_t child = component->first_child; child >= 0;
                 child = tree->components[child].next_sibling )
            {
                int const is_mount =
                    has_mounts &&
                    UITree_ChildMountType(tree, component->component_id,
                                          &tree->components[child]) >= 0;
                if( is_mount != mount_sweep )
                    continue;
                role_boundary_walk_node(
                    order,
                    child,
                    drag_pass,
                    in_deferred,
                    is_mount ? scroll_off_x : child_scroll_x,
                    is_mount ? scroll_off_y : child_scroll_y,
                    &child_clip,
                    &child_surface);
            }
        }
    }

    if( !order->replace && node == order->anchor &&
        component->incarnation == order->anchor_incarnation )
        role_boundary_mark_anchor(order);
}

static void
role_boundary_walk_tree(struct role_boundary_order* order)
{
    struct UITree const* tree;

    assert(order);
    tree = order->tree;
    /* One monotonically increasing sequence spans both passes, so every
     * deferred drag node naturally sorts above every ordinary descriptor. */
    for( int drag_pass = 0; drag_pass <= (UITree_HasActiveDrag(tree) ? 1 : 0);
         drag_pass++ )
    {
        for( int32_t root = tree->root_index; root >= 0;
             root = tree->components[root].next_sibling )
        {
            if( !UITree_RootIsDisplayable(tree, root) )
                continue;
            role_boundary_walk_node(
                order,
                root,
                drag_pass != 0,
                false,
                0,
                0,
                NULL,
                NULL);
        }
    }
}

bool
UITree_NodePaintsAfterRoleBoundary(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int32_t candidate_node,
    int32_t anchor_node,
    uint32_t anchor_incarnation,
    bool replace)
{
    struct role_boundary_order order;

    assert(tree);
    if( candidate_node < 0 || anchor_node < 0 ||
        (uint32_t)candidate_node >= tree->component_count ||
        (uint32_t)anchor_node >= tree->component_count )
        return false;
    if( tree->components[candidate_node].freed || tree->components[anchor_node].freed ||
        tree->components[anchor_node].incarnation != anchor_incarnation )
        return false;

    memset(&order, 0, sizeof(order));
    order.tree = tree;
    order.host = host;
    order.candidate = candidate_node;
    order.anchor = anchor_node;
    order.anchor_incarnation = anchor_incarnation;
    order.replace = replace;

    role_boundary_walk_tree(&order);

    return order.candidate_seen && order.boundary_seen &&
           order.candidate_sequence > order.boundary_sequence;
}

bool
UITree_PointInputCoverPaintsAfterRoleBoundary(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py,
    int32_t anchor_node,
    uint32_t anchor_incarnation,
    bool replace)
{
    struct role_boundary_order order;

    assert(tree);
    if( anchor_node < 0 || (uint32_t)anchor_node >= tree->component_count ||
        tree->components[anchor_node].freed || anchor_incarnation == 0 ||
        tree->components[anchor_node].incarnation != anchor_incarnation )
        return false;

    memset(&order, 0, sizeof(order));
    order.tree = tree;
    order.host = host;
    order.candidate = -1;
    order.anchor = anchor_node;
    order.anchor_incarnation = anchor_incarnation;
    order.replace = replace;
    order.point_query = 1;
    order.point_x = px;
    order.point_y = py;
    role_boundary_walk_tree(&order);
    return order.cover_seen && order.boundary_seen &&
           order.cover_sequence > order.boundary_sequence;
}

bool
UITree_PointHasNativeInputCover(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py)
{
    struct role_boundary_order order;

    assert(tree);
    memset(&order, 0, sizeof(order));
    order.tree = tree;
    order.host = host;
    order.candidate = -1;
    order.anchor = -1;
    order.point_query = 1;
    order.point_x = px;
    order.point_y = py;
    role_boundary_walk_tree(&order);
    return order.cover_seen;
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

static int
input_gesture_target_display_hidden(
    struct UIInputState const* state,
    struct UITree const* tree)
{
    assert(state);
    assert(tree);

    if( state->pressed >= 0 )
    {
        if( (uint32_t)state->pressed >= tree->component_count ||
            tree->components[state->pressed].freed || state->pressed_incarnation == 0 ||
            tree->components[state->pressed].incarnation != state->pressed_incarnation )
            return 1;
        if( UITree_NodeOrAncestorDisplayHidden(tree, state->pressed) )
            return 1;
    }
    /* UIInputState predates an explicit initializer, so callers which only
     * initialize hovered/pressed leave this scalar at C's zero default. A
     * source index has ownership only while a deferred or active drag says it
     * does; index 0 by itself is not a latent gesture. */
    if( state->drag_source_idx < 0 ||
        (!state->deferred_click && !state->drag_active) )
        return 0;
    if( (uint32_t)state->drag_source_idx >= tree->component_count )
        return 1;
    if( tree->components[state->drag_source_idx].freed ||
        state->drag_source_incarnation == 0 ||
        tree->components[state->drag_source_idx].incarnation !=
            state->drag_source_incarnation ||
        tree->components[state->drag_source_idx].component_id != state->drag_source_id )
        return 1;
    return UITree_NodeOrAncestorDisplayHidden(tree, state->drag_source_idx);
}

static void
input_cancel_gesture(
    struct UIInputState* state,
    struct UITree* tree)
{
    assert(state);
    assert(tree);

    /* Only mutate render state when the saved incarnation still names this
     * component; a reclaimed index belongs to its new node. */
    if( state->drag_source_idx >= 0 &&
        (uint32_t)state->drag_source_idx < tree->component_count &&
        !tree->components[state->drag_source_idx].freed &&
        state->drag_source_incarnation != 0 &&
        tree->components[state->drag_source_idx].incarnation ==
            state->drag_source_incarnation &&
        tree->components[state->drag_source_idx].component_id == state->drag_source_id )
    {
        UITree_SetComponentDragActive(tree, state->drag_source_idx, 0);
        tree->components[state->drag_source_idx].drag_visual_trans = -1;
    }
    state->pressed = -1;
    state->pressed_incarnation = 0;
    state->drag_active = 0;
    state->drag_source_idx = -1;
    state->drag_source_incarnation = 0;
    state->drag_source_id = -1;
    state->drag_target_id = -1;
    state->drag_target_idx = -1;
    state->drag_target_incarnation = 0;
    state->deferred_click = 0;
    state->release_click_suppressed = 0;
    state->drag_duration = 0;
    state->thresholds_set = 0;
    state->cancelled_press = 1;
}

int
UITree_InputCancelDisplayHidden(
    struct UIInputState* state,
    struct UITree* tree)
{
    assert(state);
    assert(tree);
    if( !input_gesture_target_display_hidden(state, tree) )
        return 0;
    input_cancel_gesture(state, tree);
    return 1;
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
    result.drag_source_incarnation = 0;
    result.drag_source_id = -1;
    result.drag_target_id = -1;
    result.drag_target_idx = -1;
    result.drag_target_incarnation = 0;
    result.released_source_idx = -1;
    result.released_source_incarnation = 0;
    result.released_source_id = -1;

    assert(state);
    assert(tree);

    /* A frame replacement can become active between input frames. Treat its
     * native subtree exactly like CSS display:none: retire any pointer
     * ownership before hold/repeat/drag/release code gets another chance to
     * dispatch into it. Keep a small latch until mouse-up so the same physical
     * press cannot be retargeted to the plugin or world underneath. */
    (void)UITree_InputCancelDisplayHidden(state, tree);

    switch( event.kind )
    {
    case UI_INPUT_MOVE:
        state->hovered = UITree_HitTestInteractive(tree, host, event.x, event.y);
        break;

    case UI_INPUT_DOWN:
        /* A new physical press cannot belong to an older cancelled gesture
         * whose release was lost to focus loss. */
        state->cancelled_press = 0;
        state->hovered = UITree_HitTestInteractive(tree, host, event.x, event.y);
        state->pressed = state->hovered;
        state->pressed_incarnation =
            state->pressed >= 0 && (uint32_t)state->pressed < tree->component_count
                ? tree->components[state->pressed].incarnation
                : 0;
        state->drag_active = 0;
        state->drag_source_idx = -1;
        state->drag_source_incarnation = 0;
        state->drag_source_id = -1;
        state->drag_target_id = -1;
        state->drag_target_idx = -1;
        state->drag_target_incarnation = 0;
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
                state->drag_source_incarnation = c->incarnation;
                state->drag_source_id = c->component_id;
            }
            else
            {
                /* Non-draggable with on_click/on_op: fire on press (Jagex
                 * Client loopLayer field1871 / xrsps widgetClickInput).
                 * Scrollbar tracks call cc_dragpickup from onclick — that must
                 * run while the button is still held. Hold-only chrome (arrow
                 * on_hold) must not get a synthetic click here. */
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
        int const cancelled_press = state->cancelled_press;
        int32_t const up_hit = UITree_HitTestInteractive(tree, host, event.x, event.y);
        state->hovered = up_hit;
        result.released_source_idx = state->pressed;
        result.released_source_incarnation = state->pressed_incarnation;
        if( state->pressed >= 0 && (uint32_t)state->pressed < tree->component_count )
            result.released_source_id = tree->components[state->pressed].component_id;
        if( state->drag_active )
        {
            result.drag_ended = 1;
            result.drag_source_idx = state->drag_source_idx;
            result.drag_source_incarnation = state->drag_source_incarnation;
            result.drag_source_id = state->drag_source_id;
            result.drag_target_id = state->drag_target_id;
            result.drag_target_idx = state->drag_target_idx;
            result.drag_target_incarnation = state->drag_target_incarnation;
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
        state->pressed_incarnation = 0;
        state->deferred_click = 0;
        state->release_click_suppressed = 0;
        state->drag_active = 0;
        state->drag_duration = 0;
        /* End of gesture: drop the source so a stale idx cannot resume drag. */
        state->drag_source_idx = -1;
        state->drag_source_incarnation = 0;
        state->drag_source_id = -1;
        state->drag_target_id = -1;
        state->drag_target_idx = -1;
        state->drag_target_incarnation = 0;
        state->cancelled_press = 0;
        result.cancelled_press = cancelled_press;
        break;
    }
    }

    if( event.kind != UI_INPUT_UP )
        result.cancelled_press = state->cancelled_press;

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

    /* Direct callers may tick a drag without first delivering a MOVE event.
     * Apply the same display:none cancellation invariant here as InputUpdate. */
    if( UITree_InputCancelDisplayHidden(state, tree) )
        return 1;

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

    state->drag_target_idx = UITree_FindDropTargetNode(
        tree, mouse_x, mouse_y, state->drag_source_id, &state->drag_target_id);
    state->drag_target_incarnation =
        state->drag_target_idx >= 0 &&
                (uint32_t)state->drag_target_idx < tree->component_count
            ? tree->components[state->drag_target_idx].incarnation
            : 0;

    return changed || state->drag_active;
}
