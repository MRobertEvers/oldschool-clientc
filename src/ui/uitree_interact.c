#include "uitree_interact.h"

#include "perf/torirs_perf.h"
#include "uitree_hover.h"
#include "uitree_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
torirs_trace_drag(void)
{
    static int cached = -1;
    if( cached < 0 )
    {
        char const* e = getenv("TORIRS_TRACE_DRAG");
        cached = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return cached;
}

void
UIInteraction_Init(struct UIInteraction* interact)
{
    assert(interact);
    memset(interact, 0, sizeof(*interact));
    interact->input_state.hovered = -1;
    interact->input_state.pressed = -1;
    interact->hover_com_id = -1;
    interact->prev_hover_com_id = -1;
    interact->last_repeat_cycle = UINT64_MAX;
    UIMinimenu_Reset(&interact->minimenu);
}

static void
intent_push(
    struct UIInteractOut* out,
    struct UIIntent const* intent)
{
    if( out->intent_count >= UI_INTENT_MAX )
        return;
    out->intents[out->intent_count++] = *intent;
}

struct UITreeRuntimeScriptHook const*
UITree_ResolveClickHook(
    struct UITree* tree,
    int32_t leaf_index,
    int* out_component_id)
{
    int32_t idx;

    assert(tree);
    assert(out_component_id);

    *out_component_id = -1;

    if( leaf_index < 0 || (uint32_t)leaf_index >= tree->component_count )
        return NULL;

    for( idx = leaf_index; idx >= 0; idx = tree->components[idx].parent )
    {
        struct UITreeComponent const* node = &tree->components[idx];
        struct UITreeRuntimeHooks const* hooks = UITree_Hooks(node);
        if( hooks->on_op.script_id > 0 )
        {
            *out_component_id = node->component_id;
            return &hooks->on_op;
        }
        if( hooks->on_click.script_id > 0 )
        {
            *out_component_id = node->component_id;
            return &hooks->on_click;
        }
    }
    return NULL;
}

/* Innermost vertically-scrollable IF1 RS_LAYER whose bounds contain (mx,my), by
 * smallest area (most specific). Wheel scrolls whatever layer is under the
 * cursor even over empty content, unlike the geometric leaf hit-test.
 * IF1-only: reference handleIf1Scrollbars gates native wheel on isIf3===false. */
static int32_t
find_wheel_scroll_layer(
    struct UITree const* tree,
    int mx,
    int my)
{
    int32_t best = -1;
    int best_area = 0;
    int si;

    assert(tree);
    for( si = 0; si < tree->scroll_layers.count; si++ )
    {
        int32_t i = tree->scroll_layers.slots[si];
        struct UITreeComponent const* c;
        int bx = 0, by = 0, bw = 0, bh = 0;
        int offx = 0, offy = 0;
        assert(i >= 0 && (uint32_t)i < tree->component_count);
        c = &tree->components[i];
        if( c->type != UIELEM_RS_LAYER || c->if3 || c->freed || c->component_id < 0 )
            continue;
        if( UITree_ComponentOrAncestorHidden(tree, c->component_id) )
            continue;
        if( !UITree_ScrollLayerNeedsVertical(c) )
            continue;
        UITree_LayoutGetBounds(&c->position, &bx, &by, &bw, &bh);
        if( bw <= 0 || bh <= 0 )
            continue;
        /* Native wheel handling uses the layer's drawn rectangle. This is not
         * always abs_x/y: ordinary descendants inherit ancestor scroll, while
         * an InterfaceParent root deliberately ignores its mount host's local
         * scroll. The shared accumulator encodes exactly that boundary. */
        UITree_AccumScrollOffset(tree, i, &offx, &offy);
        if( !UITree_PointInScrolledBounds(mx, my, bx, by, bw, bh, offx, offy) )
            continue;
        {
            int area = bw * bh;
            if( best < 0 || area < best_area )
            {
                best = i;
                best_area = area;
            }
        }
    }
    return best;
}

/* Innermost component under (mx,my) with a CS2 onScroll handler (reference
 * OsrsClient wheel dispatch: top-most hit widget with an onScroll handler).
 * Screen-space test: ancestor scroll offsets shift drawn positions. */
static int32_t
find_wheel_hook_component(
    struct UITree const* tree,
    int mx,
    int my)
{
    int32_t best = -1;
    int best_area = 0;
    int wi;

    assert(tree);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_WHEEL_SCAN_NODES, (int64_t)tree->wheel_hooks.count);
    for( wi = 0; wi < tree->wheel_hooks.count; wi++ )
    {
        int32_t idx = tree->wheel_hooks.slots[wi];
        struct UITreeComponent const* c;
        int bx = 0, by = 0, bw = 0, bh = 0;
        int offx = 0, offy = 0;
        assert(idx >= 0 && (uint32_t)idx < tree->component_count);
        c = &tree->components[idx];
        if( c->freed || c->component_id < 0 )
            continue;
        if( UITree_Hooks(c)->on_scroll_wheel.script_id <= 0 )
            continue;
        if( UITree_ComponentOrAncestorHidden(tree, c->component_id) )
            continue;
        UITree_LayoutGetBounds(&c->position, &bx, &by, &bw, &bh);
        if( bw <= 0 || bh <= 0 )
            continue;
        UITree_AccumScrollOffset(tree, idx, &offx, &offy);
        if( !UITree_PointInScrolledBounds(mx, my, bx, by, bw, bh, offx, offy) )
            continue;
        {
            int area = bw * bh;
            if( best < 0 || area < best_area )
            {
                best = idx;
                best_area = area;
            }
        }
    }
    return best;
}

/* Every visible component carrying a CS2 onKey handler, with its screen-space
 * drawn origin. Reference collectWidgetsWithKeyHandlers plus the OsrsClient key
 * block: a BROADCAST, not a hit test. There is no focused-widget concept for
 * onKey, so unlike find_wheel_hook_component above there is deliberately no
 * point-in-bounds test and no zero-size skip -- off-screen and empty handlers
 * still receive keys, and scripts self-gate on varcs.
 *
 * The flat array scan is equivalent to the reference's DFS for membership:
 * UITree_ComponentOrAncestorHidden encodes the same "hidden subtree is pruned"
 * rule, mounted sub-interfaces are reparented into this same array (so they need
 * no separate pass as they do in the reference), and an array scan cannot
 * produce the duplicates the reference has to dedupe. Only enumeration ORDER
 * differs -- build order rather than DFS pre-order, diverging once CC_CREATE
 * appends or reuses slots -- so onKey scripts must not depend on relative
 * ordering. tree->layout_order is the cached pre-order if exact parity is ever
 * needed. Returns the number collected, clamped to max_targets. */
static int
collect_key_targets(
    struct UITree const* tree,
    struct UIKeyTarget* out_targets,
    int max_targets)
{
    int count = 0;
    int i;
    struct UITree* t;

    assert(tree);
    assert(out_targets);

    t = (struct UITree*)tree;

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_KEY_SCAN, 1);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_KEY_SCAN_NODES, (int64_t)t->key_hooks.count);

    for( i = 0; i < t->key_hooks.count && count < max_targets; i++ )
    {
        int32_t idx = t->key_hooks.slots[i];
        struct UITreeComponent const* c;
        int bx = 0, by = 0, bw = 0, bh = 0;
        int offx = 0, offy = 0;
        assert(idx >= 0 && (uint32_t)idx < tree->component_count);
        c = &tree->components[idx];
        if( c->freed || UITree_Hooks(c)->on_key.script_id <= 0 )
            continue;
        if( UITree_ComponentOrAncestorHidden(tree, c->component_id) )
            continue;
        UITree_LayoutGetBounds(&c->position, &bx, &by, &bw, &bh);
        UITree_AccumScrollOffset(tree, idx, &offx, &offy);
        out_targets[count].component_id = c->component_id;
        out_targets[count].abs_x = bx - offx;
        out_targets[count].abs_y = by - offy;
        count++;
    }
    return count;
}

/* Reference event ctx passes mouse coords relative to the component's drawn
 * (screen) position (OsrsClient hover dispatch, coords relative to _absX/Y). */
static void
hover_event_coords(
    struct UITree* tree,
    int component_id,
    int mouse_x,
    int mouse_y,
    struct UIIntent* intent)
{
    int32_t idx;
    int bx = 0, by = 0, bw = 0, bh = 0;
    int offx = 0, offy = 0;

    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return;
    UITree_LayoutGetBounds(&tree->components[idx].position, &bx, &by, &bw, &bh);
    UITree_AccumScrollOffset(tree, idx, &offx, &offy);
    intent->has_event_mouse = 1;
    intent->event_mouse_x = mouse_x - (bx - offx);
    intent->event_mouse_y = mouse_y - (by - offy);
}

static struct UITreeRuntimeScriptHook const*
hook_by_component_id(
    struct UITree* tree,
    int component_id,
    struct UITreeRuntimeScriptHook const* (*pick)(struct UITreeComponent const*))
{
    int32_t idx;
    if( component_id < 0 )
        return NULL;
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return NULL;
    return pick(&tree->components[idx]);
}

static struct UITreeRuntimeScriptHook const*
pick_on_mouse_over(struct UITreeComponent const* node)
{
    return &UITree_Hooks(node)->on_mouse_over;
}

static struct UITreeRuntimeScriptHook const*
pick_on_mouse_leave(struct UITreeComponent const* node)
{
    return &UITree_Hooks(node)->on_mouse_leave;
}

static struct UITreeRuntimeScriptHook const*
pick_on_mouse_repeat(struct UITreeComponent const* node)
{
    return &UITree_Hooks(node)->on_mouse_repeat;
}

static struct UITreeRuntimeScriptHook const*
pick_on_drag_complete(struct UITreeComponent const* node)
{
    return &UITree_Hooks(node)->on_drag_complete;
}

static struct UIInputResult
bridge_input_to_uitree(
    struct UIInputState* ui_state,
    struct UITree* tree,
    struct UITreeHost const* host,
    struct LibToriRS_Input* input)
{
    struct UIInputResult last;
    struct UIInputEvent move = {
        .kind = UI_INPUT_MOVE,
        .x = input->curr.mouse_x,
        .y = input->curr.mouse_y,
        .button = 0,
    };

    last = UITree_InputUpdate(ui_state, tree, host, move);

    if( LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
    {
        struct UIInputEvent down = {
            .kind = UI_INPUT_DOWN,
            .x = input->curr.mouse_x,
            .y = input->curr.mouse_y,
            .button = TORIRSM_LEFT,
        };
        last = UITree_InputUpdate(ui_state, tree, host, down);
    }

    if( LibToriRS_Input_IsClick(input, TORIRSM_LEFT) )
    {
        struct UIInputEvent up = {
            .kind = UI_INPUT_UP,
            .x = input->last_click_x[TORIRSM_LEFT],
            .y = input->last_click_y[TORIRSM_LEFT],
            .button = TORIRSM_LEFT,
        };
        last = UITree_InputUpdate(ui_state, tree, host, up);
    }
    else if( LibToriRS_Input_IsDragEnd(input, TORIRSM_LEFT) )
    {
        /* Fallback for a drag_end that carries no is_click (a release the input
         * layer never saw pressed). The UP must still reach the UI state
         * machine: it fires onDragComplete and clears the source's drag_active —
         * otherwise the widget keeps rendering at its stale drag visual
         * (scrollbar body frozen while the CS2 script moves the caps). */
        struct UIInputEvent up = {
            .kind = UI_INPUT_UP,
            .x = input->curr.mouse_x,
            .y = input->curr.mouse_y,
            .button = TORIRSM_LEFT,
        };
        last = UITree_InputUpdate(ui_state, tree, host, up);
    }

    return last;
}

/* IF1 scrollbars are emit-drawn (not draggable components), so they have no
 * place in the generic input path. Intercept the bar strip before the pointer
 * bridge can hand the press to the object-drag system and drive
 * component->scroll_x/y directly. Returns 1 while the bar owns the mouse. */
static int
interact_scrollbars(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct UITreeHost const* ui_host,
    struct LibToriRS_Input* input,
    struct UIInteractOut* out)
{
    int mx = input->curr.mouse_x;
    int my = input->curr.mouse_y;
    int left_held = LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
    int left_down = LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT);
    int consumed = 0;

    if( interact->sb_dragging )
    {
        consumed = 1;
        if( left_held )
        {
            if( UITree_ScrollbarHandle(
                    tree, &interact->sb_drag_hit, mx, my,
                    UITREE_SCROLLBAR_ACTION_GRIP_DRAG, 0) )
                out->need_redraw = 1;
        }
        else
            interact->sb_dragging = 0;
    }
    else if( left_held )
    {
        /* Arrows scroll continuously while the button is held (not just on the
         * press edge). Re-hittest the current pointer every frame — as in TS
         * doScrollbar, moving off the arrow stops the step and moving back on
         * resumes it. The grip drag can only be *started* on the press edge. */
        struct UITreeScrollbarHitInfo hit;
        if( UITree_FindScrollbarAt(tree, ui_host, mx, my, &hit) )
        {
            /* Never let this press become an object-drag source. */
            interact->input_state.drag_source_idx = -1;
            interact->input_state.drag_source_id = -1;
            interact->input_state.pressed = -1;
            consumed = 1;
            if( UITree_ScrollbarIsArrowKind(hit.kind) )
            {
                interact->sb_arrow_held = 1;
                if( UITree_ScrollbarHandle(
                        tree, &hit, mx, my, UITREE_SCROLLBAR_ACTION_ARROW_STEP, 0) )
                    out->need_redraw = 1;
            }
            else if( left_down && UITree_ScrollbarIsGripKind(hit.kind) )
            {
                interact->sb_arrow_held = 0;
                interact->sb_drag_hit = hit;
                interact->sb_dragging = 1;
                if( UITree_ScrollbarHandle(
                        tree, &interact->sb_drag_hit, mx, my,
                        UITREE_SCROLLBAR_ACTION_GRIP_DRAG, 0) )
                    out->need_redraw = 1;
            }
        }
        else if( interact->sb_arrow_held )
        {
            /* Held off the arrow after latching: keep owning the mouse (so the
             * eventual release cannot leak into a world walk-click) but stop
             * stepping until the pointer returns to the arrow. */
            consumed = 1;
        }
    }
    else if( interact->sb_arrow_held )
    {
        /* Release frame of an arrow hold: consume it so IsClick does not fall
         * through to left_click_miss (the yellow "Walk here" cross), then retire
         * the latch. */
        consumed = 1;
        interact->sb_arrow_held = 0;
    }

    if( !left_held )
        interact->sb_arrow_held = 0;

    return interact->sb_dragging || interact->sb_arrow_held || consumed;
}

/* Mouse wheel. IF1: natively step scroll_y of the layer under the cursor.
 * IF3: dispatch the innermost CS2 onScroll handler under the cursor with
 * event mouse = (mx relative to the component, +/-1 wheel step) — reference
 * OsrsClient wheel dispatch, scrollCtx mouseY = wheelStep. */
static void
interact_wheel(
    struct UITree* tree,
    struct LibToriRS_Input* input,
    struct UIInteractOut* out)
{
    int const mx = input->curr.mouse_x;
    int const my = input->curr.mouse_y;
    int32_t layer_idx;
    int32_t hook_idx;

    if( input->curr.mouse_wheel_y == 0 )
        return;

    /* IF1 native wheel first (reference order: handleIf1Scrollbars, then the
     * onScroll dispatch over remaining wheel delta). */
    layer_idx = find_wheel_scroll_layer(tree, mx, my);
    if( layer_idx >= 0 )
    {
        struct UITreeComponent* layer = &tree->components[layer_idx];
        /* Wheel up (positive) scrolls content up -> scroll_y down. */
        layer->scroll_y -= input->curr.mouse_wheel_y * UITREE_SCROLLBAR_WHEEL_STEP;
        UITree_ScrollClampComponent(layer);
        out->wheel_consumed = 1;
        out->need_redraw = 1;
        return;
    }

    hook_idx = find_wheel_hook_component(tree, mx, my);
    if( hook_idx < 0 )
        return;

    {
        struct UITreeComponent* c = &tree->components[hook_idx];
        int bx = 0, by = 0, bw = 0, bh = 0;
        int offx = 0, offy = 0;
        struct UIIntent intent = {
            .component_id = c->component_id,
            .hook = &UITree_Hooks(c)->on_scroll_wheel,
        };
        UITree_LayoutGetBounds(&c->position, &bx, &by, &bw, &bh);
        UITree_AccumScrollOffset(tree, hook_idx, &offx, &offy);
        intent.has_event_mouse = 1;
        intent.event_mouse_x = mx - (bx - offx);
        /* Our wheel-up is positive; reference wheelStep is +1 for wheel-down
         * (browser deltaY > 0). */
        intent.event_mouse_y = input->curr.mouse_wheel_y > 0 ? -1 : 1;
        intent_push(out, &intent);
        /* A targeted onScroll handler owns the wheel just like a native IF1
         * scroll layer. Letting the same notch continue to app-level gestures
         * makes an interface over the viewport scroll and zoom the world at
         * once. */
        out->wheel_consumed = 1;
        out->need_redraw = 1;
    }
}

/* Emit one on_drag intent for the current drag source. */
static void
interact_drag_push_ondrag(
    struct UITree* tree,
    struct UIInputState* st,
    struct UIInteractOut* out)
{
    struct UITreeComponent* src;
    int parent_x = 0, parent_y = 0, parent_w = 0, parent_h = 0;
    int32_t parent_idx;
    struct UIIntent intent;

    assert(tree);
    assert(st);
    assert(out);
    assert(st->drag_source_idx >= 0);
    assert((uint32_t)st->drag_source_idx < tree->component_count);

    src = &tree->components[st->drag_source_idx];
    parent_idx = src->parent;
    intent = (struct UIIntent){
        .component_id = st->drag_source_id,
        .hook = &UITree_Hooks(src)->on_drag,
        .has_drag_target = 1,
        .drag_target_id = st->drag_target_id,
    };
    if( src->drag_render_area_uid >= 0 )
    {
        int32_t area = UITree_ResolveDragRenderArea(tree, src);
        /* Miss must not wipe the widget parent: absolute screen coords
         * (parent_y=0) make event_mousey huge and pin scroll to max. */
        if( area >= 0 )
            parent_idx = area;
    }
    if( parent_idx >= 0 )
    {
        int poffx = 0, poffy = 0;
        UITree_LayoutGetBounds(
            &tree->components[parent_idx].position,
            &parent_x,
            &parent_y,
            &parent_w,
            &parent_h);
        /* Screen-space origin of the coordinate space: the area may
         * itself sit inside a scrolled layer (reference uses the
         * render area's _absX/_absY). */
        UITree_AccumScrollOffset(tree, parent_idx, &poffx, &poffy);
        parent_x -= poffx;
        parent_y -= poffy;
    }
    /* Script-space mouse: drag visual relative to the drag render
     * area (the track for a scrollbar dragger), folded back into
     * content space via the area's scroll. */
    intent.has_event_mouse = 1;
    intent.event_mouse_x = src->drag_visual_x - parent_x +
                           (parent_idx >= 0 ? tree->components[parent_idx].scroll_x : 0);
    intent.event_mouse_y = src->drag_visual_y - parent_y +
                           (parent_idx >= 0 ? tree->components[parent_idx].scroll_y : 0);
    if( torirs_trace_drag() )
    {
        fprintf(
            stderr,
            "TORIRS_TRACE_DRAG on_drag src=%d area=%d area_uid=%d area_xywh=%d,%d,%d,%d "
            "visual_y=%d parent_y=%d event_y=%d hook=%d\n",
            st->drag_source_id,
            parent_idx >= 0 ? tree->components[parent_idx].component_id : -1,
            src->drag_render_area_uid,
            parent_x,
            parent_y,
            parent_w,
            parent_h,
            src->drag_visual_y,
            parent_y,
            intent.event_mouse_y,
            intent.hook ? intent.hook->script_id : -1);
    }
    intent_push(out, &intent);
    out->need_redraw = 1;
}

/* Apply a CS2 cc/if_dragpickup that the host staged on the tree. Returns 1 if a
 * pickup was consumed (an on_drag was pushed for it this frame). */
static int
interact_drag_consume_pending(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct UITreeHost const* ui_host,
    struct LibToriRS_Input* input,
    int left_held,
    struct UIInteractOut* out)
{
    struct UIInputState* st;
    int32_t idx;
    struct UITreeComponent* src;
    int mx;
    int my;
    int pending_id;
    int pending_x;
    int pending_y;

    assert(interact);
    assert(tree);
    assert(input);
    assert(out);

    if( !tree->pending_drag_pickup )
        return 0;

    /* Snapshot then clear only after accept — a refuse must not drop the
     * request when a live drag blocks it (next frame may be free). */
    pending_id = tree->pending_drag_pickup_id;
    pending_x = tree->pending_drag_pickup_x;
    pending_y = tree->pending_drag_pickup_y;

    /* Reference dragTryPickup refuses when a drag is already live or anti_drag
     * is set. An already-active source from a real press wins. Drop the pending
     * — retrying mid-drag would stomp the live gesture. */
    st = &interact->input_state;
    if( tree->anti_drag || st->drag_active )
    {
        tree->pending_drag_pickup = 0;
        return 0;
    }

    idx = UITree_FindByComponentId(tree, pending_id);
    if( idx < 0 )
    {
        tree->pending_drag_pickup = 0;
        return 0;
    }

    src = &tree->components[idx];
    /* getDragLayer null → no-op (Client.dragTryPickup). Without a render area
     * the track/list itself must not become a drag source. */
    if( src->drag_render_area_uid < 0 &&
        UITree_ClickMaskDragDepth(src->behavior.click_mask) == 0 )
    {
        tree->pending_drag_pickup = 0;
        return 0;
    }

    tree->pending_drag_pickup = 0;
    mx = input->curr.mouse_x;
    my = input->curr.mouse_y;

    st->drag_source_idx = idx;
    st->drag_source_id = src->component_id;
    st->drag_pickup_x = pending_x;
    st->drag_pickup_y = pending_y;
    st->drag_click_x = mx;
    st->drag_click_y = my;
    st->drag_duration = 0;
    st->drag_target_id = -1;
    st->deferred_click = 0;
    /* Force active immediately: scripted pickup (scrollbar track jump) has
     * already chosen the grab offset; there is no deadzone to wait out. */
    st->drag_active = 1;
    UITree_SetComponentDragActive(tree, idx, 1);
    src->drag_visual_trans = (src->drag_behavior == 1) ? -1 : 128;

    /* Seed visual + clamp via the normal tick path. */
    (void)UITree_InputDragTick(st, tree, ui_host, mx, my, 1);
    interact_drag_push_ondrag(tree, st, out);

    if( torirs_trace_drag() )
    {
        fprintf(
            stderr,
            "TORIRS_TRACE_DRAG pickup id=%d pickup_xy=%d,%d held=%d visual_y=%d\n",
            src->component_id,
            pending_x,
            pending_y,
            left_held,
            src->drag_visual_y);
    }

    /* Held is the normal case: `scrollbar_vertical_jump` picks the dragger up
     * with the cursor at its centre and the gesture continues from there
     * (reference setDragSource makes the picked-up widget the live drag
     * source). Only a press already released this frame completes at once. */
    if( !left_held )
    {
        struct UIIntent complete = {
            .component_id = st->drag_source_id,
            .hook = &UITree_Hooks(src)->on_drag_complete,
            .has_drag_target = 1,
            .drag_target_id = st->drag_target_id,
            .has_event_mouse = 1,
            .event_mouse_x = 0,
            .event_mouse_y = 0,
        };
        if( out->intent_count > 0 )
        {
            complete.event_mouse_x = out->intents[out->intent_count - 1].event_mouse_x;
            complete.event_mouse_y = out->intents[out->intent_count - 1].event_mouse_y;
        }
        intent_push(out, &complete);
        UITree_SetComponentDragActive(tree, idx, 0);
        st->drag_active = 0;
        st->drag_source_idx = -1;
        st->drag_source_id = -1;
        st->drag_target_id = -1;
    }
    return 1;
}

int
UITree_InteractConsumePendingDragPickup(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct UITreeHost const* ui_host,
    struct LibToriRS_Input* input,
    struct UIInteractOut* out)
{
    int left_held;

    assert(interact);
    assert(tree);
    assert(input);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->hover_com_id = -1;
    out->clicked_com_id = -1;
    out->minimenu_select = -1;

    left_held = LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
    (void)interact_drag_consume_pending(interact, tree, ui_host, input, left_held, out);
    return out->intent_count;
}

/* Drag tick while held (deadzone+deadtime); emits onDrag / onDragComplete
 * intents with drag-target and script-space mouse context. */
static void
interact_drag(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct UITreeHost const* ui_host,
    struct LibToriRS_Input* input,
    int sb_owns_mouse,
    struct UIInputResult const* ui_result,
    struct UIInteractOut* out)
{
    struct UIInputState* st = &interact->input_state;
    /* Hold, not press-edge or input-level IsDragging: the input layer's 5px
     * deadzone must not gate UITree drag ticks. Scrollbar thumbs use
     * drag_behavior==1 with their own (usually 0) deadzone/deadtime; waiting
     * on IsDragging left on_drag silent while drag_visual still moved once
     * the input threshold finally tripped. Same gate as interact_hold. */
    int left_held = LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);
    int picked_up = 0;

    if( !sb_owns_mouse )
        picked_up = interact_drag_consume_pending(
            interact, tree, ui_host, input, left_held, out);

    if( !sb_owns_mouse && !picked_up && st->drag_source_idx >= 0 && left_held )
    {
        int drag_ch = UITree_InputDragTick(
            st, tree, ui_host, input->curr.mouse_x, input->curr.mouse_y, 1);
        if( st->drag_active )
            interact_drag_push_ondrag(tree, st, out);
        else if( drag_ch )
            out->need_redraw = 1;
    }

    if( ui_result->drag_ended )
    {
        if( ui_result->drag_source_id >= 0 )
        {
            struct UIIntent intent = {
                .component_id = ui_result->drag_source_id,
                .hook = hook_by_component_id(
                    tree, ui_result->drag_source_id, pick_on_drag_complete),
                .has_drag_target = 1,
                .drag_target_id = ui_result->drag_target_id,
            };
            intent_push(out, &intent);
        }
        out->need_redraw = 1;
    }
}

/* onHold/onClickRepeat fire every tick while a widget stays pressed and no drag is active
 * (reference OsrsClient: clickedWidget && isHolding && !isDraggingWidget;
 * holdCtx mouse is relative to the widget's drawn position). Scrollbar arrows
 * hold-scroll through this. */
static void
interact_hold(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct LibToriRS_Input* input,
    int sb_owns_mouse,
    struct UIInteractOut* out)
{
    struct UIInputState* st = &interact->input_state;
    int left_held = LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT);

    if( sb_owns_mouse || !left_held || st->drag_active )
        return;
    if( st->pressed < 0 || (uint32_t)st->pressed >= tree->component_count )
        return;

    {
        struct UITreeComponent* c = &tree->components[st->pressed];
        int bx = 0, by = 0, bw = 0, bh = 0;
        int offx = 0, offy = 0;
        struct UITreeRuntimeHooks const* hooks = UITree_Hooks(c);
        UITree_LayoutGetBounds(&c->position, &bx, &by, &bw, &bh);
        UITree_AccumScrollOffset(tree, st->pressed, &offx, &offy);
        if( hooks->on_hold.script_id > 0 )
        {
            struct UIIntent intent = {
                .component_id = c->component_id,
                .hook = &hooks->on_hold,
                .has_event_mouse = 1,
                .event_mouse_x = input->curr.mouse_x - (bx - offx),
                .event_mouse_y = input->curr.mouse_y - (by - offy),
            };
            intent_push(out, &intent);
            out->need_redraw = 1;
        }
        if( hooks->on_click_repeat.script_id > 0 )
        {
            struct UIIntent intent = {
                .component_id = c->component_id,
                .hook = &hooks->on_click_repeat,
                .has_event_mouse = 1,
                .event_mouse_x = input->curr.mouse_x - (bx - offx),
                .event_mouse_y = input->curr.mouse_y - (by - offy),
            };
            intent_push(out, &intent);
            out->need_redraw = 1;
        }
    }
}

/* onRelease belongs to the widget that received mouse-down, not whatever is
 * under the pointer on mouse-up. It fires after drag completion, too. */
static void
interact_release(
    struct UITree* tree,
    struct LibToriRS_Input* input,
    struct UIInputResult const* ui_result,
    struct UIInteractOut* out)
{
    int32_t idx = ui_result->released_source_idx;
    struct UITreeComponent const* c;
    struct UITreeRuntimeScriptHook const* hook;
    struct UIIntent intent;

    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;
    c = &tree->components[idx];
    if( c->freed || c->component_id != ui_result->released_source_id )
        return;
    hook = &UITree_Hooks(c)->on_release;
    if( hook->script_id <= 0 )
        return;
    memset(&intent, 0, sizeof(intent));
    intent.component_id = c->component_id;
    intent.hook = hook;
    hover_event_coords(
        tree, c->component_id, input->curr.mouse_x, input->curr.mouse_y, &intent);
    intent_push(out, &intent);
    out->need_redraw = 1;
}

static void
interact_hover(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct UITreeHost const* ui_host,
    struct LibToriRS_Input* input,
    uint64_t now_ms,
    struct UIInteractOut* out)
{
    int mx = input->curr.mouse_x;
    int my = input->curr.mouse_y;

    /* The host is load-bearing, not optional: without it the hover walk cannot
     * ask which sidebar tab is selected, so it descends into EVERY tab's
     * subtree. Because the walk is last-match-wins (reference
     * addComponentOptions), components from tabs that are not even on screen
     * then override the visible ones — which showed up as stat cells having a
     * hover box a few pixels tall. */
    interact->hover_com_id = UITree_FindHoveredComponentIdForRegion(
        tree, ui_host, -1, mx, my, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    if( interact->hover_com_id != interact->prev_hover_com_id )
    {
        if( interact->prev_hover_com_id >= 0 )
        {
            struct UIIntent intent = {
                .component_id = interact->prev_hover_com_id,
                .hook = hook_by_component_id(
                    tree, interact->prev_hover_com_id, pick_on_mouse_leave),
            };
            hover_event_coords(tree, interact->prev_hover_com_id, mx, my, &intent);
            intent_push(out, &intent);
        }
        if( interact->hover_com_id >= 0 )
        {
            struct UIIntent intent = {
                .component_id = interact->hover_com_id,
                .hook = hook_by_component_id(
                    tree, interact->hover_com_id, pick_on_mouse_over),
            };
            hover_event_coords(tree, interact->hover_com_id, mx, my, &intent);
            intent_push(out, &intent);
        }
        interact->prev_hover_com_id = interact->hover_com_id;
        out->need_redraw = 1;
    }

    /* onMouseRepeat fires for the still-hovered component once per client
     * cycle (reference gates on cycleCntr; 20ms game tick). Keyed on the cycle
     * the app is in, NOT on elapsed milliseconds — see client_cycle in the
     * header for why the difference is visible. */
    if( interact->hover_com_id >= 0 && interact->client_cycle != interact->last_repeat_cycle )
    {
        struct UITreeRuntimeScriptHook const* repeat_hook = hook_by_component_id(
            tree, interact->hover_com_id, pick_on_mouse_repeat);
        interact->last_repeat_cycle = interact->client_cycle;
        if( repeat_hook && repeat_hook->script_id > 0 )
        {
            struct UIIntent intent = {
                .component_id = interact->hover_com_id,
                .hook = repeat_hook,
            };
            hover_event_coords(tree, interact->hover_com_id, mx, my, &intent);
            intent_push(out, &intent);
        }
    }

    out->hover_com_id = interact->hover_com_id;
}

/*
 * Right-click minimenu gesture (reference choose-option): while the popup is
 * visible it owns the mouse completely — rows select on MOUSEDOWN (either
 * button), pressing outside closes (and a right press reopens at the new
 * point), and no other interaction runs. When no menu is open, a right press
 * asks the app to build + show one. Returns 1 while the menu owns the mouse.
 */
static int
interact_minimenu(
    struct UIInteraction* interact,
    struct LibToriRS_Input* input,
    struct UIInteractOut* out)
{
    struct UIMinimenu* menu = &interact->minimenu;

    if( !menu->visible )
    {
        /* A left press the popup never saw starts a fresh gesture: drop any
         * stale latch so a swallowed press whose release went missing (focus
         * loss, drag) cannot eat this click. */
        if( LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
            interact->swallow_left_click = 0;
        if( LibToriRS_Input_IsMouseDown(input, TORIRSM_RIGHT) )
        {
            out->right_click = 1;
            out->right_click_x = input->curr.mouse_x;
            out->right_click_y = input->curr.mouse_y;
        }
        return 0;
    }

    /* Keep ownership observable after a selected option synchronously hides
     * the menu in the app. App-level gestures also poll the raw input state,
     * so visibility alone cannot stop this press propagating to them. */
    out->minimenu_consumed_pointer = 1;

    if( UIMinimenu_UpdateHover(menu, input->curr.mouse_x, input->curr.mouse_y) )
        out->need_redraw = 1;

    {
        int const left_down = LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT);
        int const right_down = LibToriRS_Input_IsMouseDown(input, TORIRSM_RIGHT);
        if( left_down || right_down )
        {
            int const mx = input->curr.mouse_x;
            int const my = input->curr.mouse_y;
            int const hit = UIMinimenu_HitOption(menu, mx, my);
            /* Whatever this press does — select, close, or bounce off chrome —
             * the popup consumed it, so its release belongs to the popup too. */
            if( left_down )
                interact->swallow_left_click = 1;
            if( hit >= 0 )
            {
                /* App dispatches, then hides. */
                out->minimenu_select = hit;
            }
            else if( hit == -2 )
            {
                UIMinimenu_Hide(menu);
                out->minimenu_closed = 1;
                out->need_redraw = 1;
                if( right_down )
                {
                    out->right_click = 1;
                    out->right_click_x = mx;
                    out->right_click_y = my;
                }
            }
            /* hit == -1: chrome / close margin — swallow the press. */
        }
        else if(
            UIMinimenu_HitOption(menu, input->curr.mouse_x, input->curr.mouse_y) == -2 )
        {
            /* Reference close-on-leave (Client.ts:8559): the menu rect plus a
             * 10px margin is a hover deadzone — moving the mouse beyond it
             * dismisses the popup without a click. Only on press-less frames;
             * a press that lands outside goes through the branch above so its
             * swallow/reopen semantics stay intact. */
            UIMinimenu_Hide(menu);
            out->minimenu_closed = 1;
            out->need_redraw = 1;
        }
    }
    return 1;
}

static void
interact_click(
    struct UITree* tree,
    struct UITreeHost const* ui_host,
    struct LibToriRS_Input* input,
    struct UIInputResult const* ui_result,
    struct UIInteractOut* out)
{
    struct UITreeRuntimeScriptHook const* click_hook = NULL;
    int hook_com_id = -1;
    int32_t ihit;
    int click_x;
    int click_y;

    if( ui_result->clicked < 0 || (uint32_t)ui_result->clicked >= tree->component_count )
        return;

    /* Press-edge clicks happen before last_click_* is written (that is set on
     * release). Use the live pointer so hit re-tests and chrome gestures land
     * on the press position. */
    if( ui_result->press_click )
    {
        click_x = input->curr.mouse_x;
        click_y = input->curr.mouse_y;
    }
    else
    {
        click_x = input->last_click_x[TORIRSM_LEFT];
        click_y = input->last_click_y[TORIRSM_LEFT];
    }
    ihit = UITree_HitTestInteractive(tree, ui_host, click_x, click_y);

    /* Gameframe chrome gestures resolve here, before hooks: a click on a tab
     * icon (or its redstone) switches the sidebar tab (reference tab hit-boxes
     * in gameLoop; ours come from the INI layout), and a privacy-bar button
     * cycles its chat filter mode. Both consume the click. */
    if( ihit >= 0 && (uint32_t)ihit < tree->component_count && ui_host )
    {
        struct UITreeComponent const* hit_c = &tree->components[ihit];
        int tabno = -1;
        if( hit_c->type == UIELEM_BUILTIN_TAB_ICONS )
            tabno = hit_c->u.tab_icon.tabno;
        else if( hit_c->type == UIELEM_BUILTIN_REDSTONE_TAB )
            tabno = hit_c->u.redstone_tab.tabno;
        if( tabno >= 0 )
        {
            struct UITreeHostRequest enabled_req = {
                .kind = UITREE_HOST_GET_TAB_ENABLED,
                .u.tab_enabled.tabno = tabno,
            };
            if( UITree_Host(ui_host, &enabled_req) )
            {
                struct UITreeHostRequest set_req = {
                    .kind = UITREE_HOST_SET_SELECTED_TAB,
                    .u.set_selected_tab.tabno = tabno,
                };
                UITree_Host(ui_host, &set_req);
                out->need_redraw = 1;
            }
            return;
        }
        if( hit_c->type == UIELEM_BUILTIN_CHAT_BUTTON )
        {
            struct UITreeHostRequest cycle_req = {
                .kind = UITREE_HOST_CYCLE_CHAT_FILTER_MODE,
                .u.chat_filter.filter = (int)hit_c->u.chat_button.filter,
            };
            UITree_Host(ui_host, &cycle_req);
            out->need_redraw = 1;
            return;
        }
        if( hit_c->type == UIELEM_BUILTIN_MINIMAP )
        {
            /* Minimap click-to-walk: the widget has no component id, so it
             * cannot travel through clicked_com_id; report it as a chrome
             * gesture and let the app do the tile math. */
            out->minimap_click = 1;
            out->minimap_click_x = click_x;
            out->minimap_click_y = click_y;
            out->need_redraw = 1;
            return;
        }
    }

    /* Prefer interactive hit so clickMask targets beat decorative overlays. */
    if( ihit >= 0 && (uint32_t)ihit < tree->component_count )
        click_hook = UITree_ResolveClickHook(tree, ihit, &hook_com_id);
    if( !click_hook )
        click_hook = UITree_ResolveClickHook(tree, ui_result->clicked, &hook_com_id);

    out->clicked_com_id =
        hook_com_id >= 0 ? hook_com_id
        : (ihit >= 0 && (uint32_t)ihit < tree->component_count
               ? tree->components[ihit].component_id
               : tree->components[ui_result->clicked].component_id);
    out->clicked_x = click_x;
    out->clicked_y = click_y;

    {
        struct UIIntent intent = {
            .component_id = hook_com_id,
            .hook = click_hook,
            .is_click = 1,
        };
        /* onClick event_mouse is relative to the component whose hook is
         * dispatched, just like hover/repeat. Slider tracks consume that value
         * directly to turn the click position into a percentage; leaving it
         * unset reused the host's previous event coordinates and made the
         * thumb jump somewhere unrelated to the pointer. */
        if( hook_com_id >= 0 )
            hover_event_coords(tree, hook_com_id, click_x, click_y, &intent);
        intent_push(out, &intent);
    }
    out->need_redraw = 1;
}

/* Does this key event trigger the binding in `slot`?
 *
 * A binding stores up to five (keychar, keycode) alternatives, and an event
 * carries exactly one of the two forms, so compare against whichever the event
 * actually has. */
static int
opkey_slot_matches(
    struct UITreeOpKeyBinding const* slot,
    struct LibToriRS_KeyEvent const* event)
{
    if( !slot->bound )
        return 0;
    /* SETOPKEYIGNOREHELD: fire once per physical press, ignoring OS repeat. */
    if( slot->ignore_held && event->is_repeat )
        return 0;
    for( int i = 0; i < slot->pair_count; i++ )
    {
        if( event->key_typed >= 0 )
        {
            if( slot->key_codes[i] == event->key_typed )
                return 1;
        }
        else if( event->key_pressed > 0 && slot->key_chars[i] == event->key_pressed )
            return 1;
    }
    return 0;
}

/* Op-key bindings: fire a component's on_op as if its op had been picked from
 * the menu. This goes BEYOND the reference, which stores opKeys and never reads
 * them back (hasKeyBindings is written and never tested), so F-key tab switching
 * and Escape-to-close do not work there. Without a real op index threaded
 * through, on_op would always report op 1. */
static void
interact_op_keys(
    struct UITree* tree,
    struct LibToriRS_Input* input,
    struct UIInteractOut* out)
{
    int oi;

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_OPKEY_SCAN_NODES, (int64_t)tree->opkeys.count);
    for( oi = 0; oi < tree->opkeys.count; oi++ )
    {
        int32_t idx = tree->opkeys.slots[oi];
        struct UITreeComponent const* node;
        assert(idx >= 0 && (uint32_t)idx < tree->component_count);
        node = &tree->components[idx];
        if( node->freed || node->component_id < 0 || !UITree_HasOpKeys(node) )
            continue;
        if( UITree_Hooks(node)->on_op.script_id <= 0 )
            continue;
        if( UITree_ComponentOrAncestorHidden(tree, node->component_id) )
            continue;

        for( int ev = 0; ev < input->key_event_count; ev++ )
        {
            for( int slot = 0; slot < UITREE_OPKEY_SLOTS; slot++ )
            {
                if( !opkey_slot_matches(&UITree_OpKeys(node)->slots[slot], &input->key_events[ev]) )
                    continue;
                {
                    struct UIIntent intent = {
                        .component_id = node->component_id,
                        .hook = &UITree_Hooks(node)->on_op,
                        .op_index = slot + 1,
                    };
                    intent_push(out, &intent);
                    out->need_redraw = 1;
                }
                break; /* one op per component per event */
            }
        }
    }
}

/* Keyboard. The reference dispatches keys LAST, at the tail of handleUiInput
 * after all mouse handling, broadcasting every event to every visible onKey
 * handler. Emits component ids plus snapshotted screen origins rather than
 * UIIntents -- see UI_KEY_TARGET_MAX in uitree_interact.h for why. */
static void
interact_keys(
    struct UITree* tree,
    struct LibToriRS_Input* input,
    struct UIInteractOut* out)
{
    int target_count;
    int event_count;

    assert(tree);
    assert(input);
    assert(out);

    if( input->key_event_count <= 0 )
        return;

    /* Op-key bindings resolve to on_op intents, so they run through the normal
     * intent list rather than the key broadcast below. */
    interact_op_keys(tree, input, out);

    target_count = collect_key_targets(tree, out->key_targets, UI_KEY_TARGET_MAX);
    if( target_count <= 0 )
        return;

    event_count = input->key_event_count;
    if( event_count > LIBTORIRS_KEY_EVENT_MAX )
        event_count = LIBTORIRS_KEY_EVENT_MAX;

    out->key_target_count = target_count;
    out->key_event_count = event_count;
    memcpy(out->key_events, input->key_events, (size_t)event_count * sizeof(out->key_events[0]));
    out->key_mouse_x = input->curr.mouse_x;
    out->key_mouse_y = input->curr.mouse_y;
    out->need_redraw = 1;
}

void
UITree_InteractFrame(
    struct UIInteraction* interact,
    struct UITree* tree,
    struct UITreeHost const* ui_host,
    struct LibToriRS_Input* input,
    uint64_t now_ms,
    struct UIInteractOut* out)
{
    struct UIInputResult ui_result;
    int sb_owns_mouse;
    int swallow_click;

    assert(interact);
    assert(tree);
    assert(input);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->hover_com_id = -1;
    out->clicked_com_id = -1;
    out->minimenu_select = -1;

    /* An open minimenu owns the whole pointer: no scrollbars, drags, hover, or
     * clicks reach the tree until it closes (reference choose-option). */
    if( interact_minimenu(interact, input, out) )
        return;

    /* The popup closed on the press edge of a click it consumed; this is the
     * matching release. Retire the latch and let nothing downstream treat it
     * as a click — the action already ran on the press. */
    swallow_click =
        interact->swallow_left_click && LibToriRS_Input_IsClick(input, TORIRSM_LEFT);
    if( swallow_click )
        interact->swallow_left_click = 0;

    sb_owns_mouse = interact_scrollbars(interact, tree, ui_host, input, out);

    /* While a scrollbar owns the mouse, keep the generic hover/click/drag path
     * from seeing this press at all. */
    if( sb_owns_mouse )
    {
        memset(&ui_result, 0, sizeof(ui_result));
        ui_result.hovered = interact->input_state.hovered;
        ui_result.prev_hovered = interact->input_state.hovered;
        ui_result.clicked = -1;
        ui_result.drag_source_idx = -1;
        ui_result.drag_source_id = -1;
        ui_result.drag_target_id = -1;
        ui_result.released_source_idx = -1;
        ui_result.released_source_id = -1;
    }
    else
        ui_result = bridge_input_to_uitree(&interact->input_state, tree, ui_host, input);

    /* A component action may synchronously remount or close the interface on
     * its press edge.  Its matching release must remain owned by that UI
     * gesture even when the component no longer exists by then; otherwise the
     * release falls through as a fresh world click. */
    if( ui_result.press_click )
        interact->swallow_left_click = 1;

    interact_wheel(tree, input, out);
    interact_drag(interact, tree, ui_host, input, sb_owns_mouse, &ui_result, out);
    interact_release(tree, input, &ui_result, out);
    interact_hold(interact, tree, input, sb_owns_mouse, out);
    interact_hover(interact, tree, ui_host, input, now_ms, out);
    if( !swallow_click )
        interact_click(tree, ui_host, input, &ui_result, out);
    interact_keys(tree, input, out);

    /* Left click that hit no component at all: pass-through elements (the
     * world viewport) sit under it, so the app runs its world hittest. The
     * minimenu early-return above already swallows clicks while a menu is
     * open, and scrollbar ownership zeroes ui_result.
     *
     * drag_ended is the *UITree* drag machine, not the input layer's 5px
     * pointer deadzone: a widget that was actually picked up and dropped owns
     * its release, so letting go of a dragged scrollbar/item over the viewport
     * must not also walk there. The input deadzone deliberately no longer
     * speaks to this (see LibToriRS_Input_PushMouseUp) — a click made while the
     * hand is still moving is a click. */
    if( !sb_owns_mouse && !swallow_click && !ui_result.drag_ended && ui_result.clicked < 0 &&
        LibToriRS_Input_IsClick(input, TORIRSM_LEFT) )
    {
        out->left_click_miss = 1;
        out->left_click_miss_x = input->last_click_x[TORIRSM_LEFT];
        out->left_click_miss_y = input->last_click_y[TORIRSM_LEFT];
    }
}
