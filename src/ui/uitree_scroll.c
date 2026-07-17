#include "uitree_scroll.h"

#include "uitree_layout.h"

#include <assert.h>
#include <string.h>

bool
UITree_ScrollLayerNeedsVertical(struct UITreeComponent const* layer)
{
    if( !layer || layer->type != UIELEM_RS_LAYER )
        return false;
    int h = 0;
    UITree_LayoutGetBounds(&layer->position, NULL, NULL, NULL, &h);
    return layer->u.rs_layer.scroll_height > h;
}

bool
UITree_ScrollLayerNeedsHorizontal(struct UITreeComponent const* layer)
{
    if( !layer || layer->type != UIELEM_RS_LAYER )
        return false;
    int w = 0;
    UITree_LayoutGetBounds(&layer->position, NULL, NULL, &w, NULL);
    return layer->u.rs_layer.scroll_width > w;
}

int
UITree_ScrollMaxX(struct UITreeComponent const* layer)
{
    if( !layer )
        return 0;
    int w = 0;
    UITree_LayoutGetBounds(&layer->position, NULL, NULL, &w, NULL);
    int max = layer->u.rs_layer.scroll_width - w;
    return max > 0 ? max : 0;
}

int
UITree_ScrollMaxY(struct UITreeComponent const* layer)
{
    if( !layer )
        return 0;
    int h = 0;
    UITree_LayoutGetBounds(&layer->position, NULL, NULL, NULL, &h);
    int max = layer->u.rs_layer.scroll_height - h;
    return max > 0 ? max : 0;
}

void
UITree_ScrollGetPos(
    struct UITreeScrollState const* scroll,
    int component_id,
    int* sx,
    int* sy)
{
    if( sx )
        *sx = 0;
    if( sy )
        *sy = 0;
    assert(scroll);
    if( component_id < 0 || component_id >= UITREE_SCROLL_MAX )
        return;
    if( sx && scroll->scroll_x )
        *sx = scroll->scroll_x[component_id];
    if( sy && scroll->scroll_y )
        *sy = scroll->scroll_y[component_id];
}

void
UITree_ScrollSetPos(
    struct UITreeScrollState const* scroll,
    int component_id,
    int sx,
    int sy)
{
    assert(scroll);
    if( component_id < 0 || component_id >= UITREE_SCROLL_MAX )
        return;
    if( scroll->scroll_x )
        scroll->scroll_x[component_id] = sx;
    if( scroll->scroll_y )
        scroll->scroll_y[component_id] = sy;
}

void
UITree_ScrollClampPos(
    struct UITreeComponent const* layer,
    struct UITreeScrollState const* scroll,
    int component_id)
{
    int sx = 0;
    int sy = 0;
    UITree_ScrollGetPos(scroll, component_id, &sx, &sy);
    int max_x = UITree_ScrollMaxX(layer);
    int max_y = UITree_ScrollMaxY(layer);
    if( sx < 0 )
        sx = 0;
    if( sy < 0 )
        sy = 0;
    if( sx > max_x )
        sx = max_x;
    if( sy > max_y )
        sy = max_y;
    UITree_ScrollSetPos(scroll, component_id, sx, sy);
}

void
UITree_ScrollIntersectClip(
    struct UITreeScrollClip* clip,
    int x,
    int y,
    int w,
    int h)
{
    if( !clip || w <= 0 || h <= 0 )
        return;
    if( clip->clip_w <= 0 || clip->clip_h <= 0 )
    {
        clip->clip_x = x;
        clip->clip_y = y;
        clip->clip_w = w;
        clip->clip_h = h;
        return;
    }

    int x0 = clip->clip_x > x ? clip->clip_x : x;
    int y0 = clip->clip_y > y ? clip->clip_y : y;
    int x1 = clip->clip_x + clip->clip_w;
    int x1n = x + w;
    if( x1 > x1n )
        x1 = x1n;
    int y1 = clip->clip_y + clip->clip_h;
    int y1n = y + h;
    if( y1 > y1n )
        y1 = y1n;
    clip->clip_x = x0;
    clip->clip_y = y0;
    clip->clip_w = x1 > x0 ? x1 - x0 : 0;
    clip->clip_h = y1 > y0 ? y1 - y0 : 0;
}

bool
UITree_PointInClip(int px, int py, struct UITreeScrollClip const* clip)
{
    if( !clip || clip->clip_w <= 0 || clip->clip_h <= 0 )
        return true;
    return px >= clip->clip_x && px < clip->clip_x + clip->clip_w &&
           py >= clip->clip_y && py < clip->clip_y + clip->clip_h;
}

bool
UITree_PointInScrolledBounds(
    int px,
    int py,
    int bx,
    int by,
    int bw,
    int bh,
    int scroll_off_x,
    int scroll_off_y)
{
    if( bw <= 0 || bh <= 0 )
        return false;
    return px >= bx - scroll_off_x && px < bx - scroll_off_x + bw &&
           py >= by - scroll_off_y && py < by - scroll_off_y + bh;
}

void
UITree_ScrollApplyAncestors(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    int32_t const* ancestors,
    int ancestor_count,
    int* bx,
    int* by,
    struct UITreeScrollClip* clip)
{
    assert(tree);
    if( !bx || !by || !clip )
        return;

    for( int i = 0; i < ancestor_count; i++ )
    {
        int32_t idx = ancestors[i];
        if( idx < 0 || (uint32_t)idx >= tree->component_count )
            continue;

        struct UITreeComponent const* layer = &tree->components[idx];
        if( layer->type != UIELEM_RS_LAYER && layer->type != UIELEM_BUILTIN_SIDEBAR )
            continue;

        int ax = 0;
        int ay = 0;
        int aw = 0;
        int ah = 0;
        UITree_LayoutGetBounds(&layer->position, &ax, &ay, &aw, &ah);
        UITree_ScrollIntersectClip(clip, ax, ay, aw, ah);

        if( layer->type != UIELEM_RS_LAYER )
            continue;

        bool vscroll = UITree_ScrollLayerNeedsVertical(layer);
        bool hscroll = UITree_ScrollLayerNeedsHorizontal(layer);
        if( !vscroll && !hscroll )
            continue;

        int sx = 0;
        int sy = 0;
        if( layer->component_id >= 0 )
            UITree_ScrollGetPos(scroll, layer->component_id, &sx, &sy);

        if( hscroll )
            *bx -= sx;
        if( vscroll )
            *by -= sy;
    }
}

int
UITree_CollectAncestors(
    struct UITree const* tree,
    int32_t node_index,
    int32_t* ancestors,
    int max_ancestors)
{
    assert(tree);
    if( !ancestors || max_ancestors <= 0 || node_index < 0 )
        return 0;
    if( (uint32_t)node_index >= tree->component_count )
        return 0;

    int count = 0;
    int32_t cur = tree->components[node_index].parent;
    while( cur >= 0 && (uint32_t)cur < tree->component_count && count < max_ancestors )
    {
        ancestors[count++] = cur;
        cur = tree->components[cur].parent;
    }

    for( int i = 0, j = count - 1; i < j; i++, j-- )
    {
        int32_t tmp = ancestors[i];
        ancestors[i] = ancestors[j];
        ancestors[j] = tmp;
    }
    return count;
}

static enum UITreeScrollbarHitKind
hit_vertical_scrollbar(
    struct UITreeComponent const* layer,
    int lx,
    int ly,
    int lw,
    int lh,
    struct UITreeScrollState const* scroll,
    int px,
    int py)
{
    if( !UITree_ScrollLayerNeedsVertical(layer) )
        return UITREE_SCROLLBAR_NONE;

    int sb_x = lx + lw;
    if( px < sb_x || px >= sb_x + UITREE_SCROLLBAR_THICKNESS || py < ly || py >= ly + lh )
        return UITREE_SCROLLBAR_NONE;

    int hscroll = UITree_ScrollLayerNeedsHorizontal(layer);
    int vh = hscroll ? lh - UITREE_SCROLLBAR_THICKNESS : lh;
    if( py < ly + UITREE_SCROLLBAR_THICKNESS )
        return UITREE_SCROLLBAR_V_UP;
    if( py >= ly + vh - UITREE_SCROLLBAR_THICKNESS )
        return UITREE_SCROLLBAR_V_DOWN;

    int sy = 0;
    if( layer->component_id >= 0 )
        UITree_ScrollGetPos(scroll, layer->component_id, NULL, &sy);
    int track_h = vh - 32;
    if( track_h <= 0 )
        return UITREE_SCROLLBAR_V_GRIP;
    int grip_size = (track_h * lh) / layer->u.rs_layer.scroll_height;
    if( grip_size < 8 )
        grip_size = 8;
    if( grip_size > track_h )
        grip_size = track_h;
    int range = UITree_ScrollMaxY(layer);
    int grip_y = range > 0 ? ((track_h - grip_size) * sy) / range : 0;
    int grip_y0 = ly + UITREE_SCROLLBAR_THICKNESS + grip_y;
    if( py >= grip_y0 && py < grip_y0 + grip_size )
        return UITREE_SCROLLBAR_V_GRIP;
    return UITREE_SCROLLBAR_V_TRACK;
}

static enum UITreeScrollbarHitKind
hit_horizontal_scrollbar(
    struct UITreeComponent const* layer,
    int lx,
    int ly,
    int lw,
    int lh,
    struct UITreeScrollState const* scroll,
    int px,
    int py)
{
    if( !UITree_ScrollLayerNeedsHorizontal(layer) )
        return UITREE_SCROLLBAR_NONE;

    int vscroll = UITree_ScrollLayerNeedsVertical(layer);
    int sb_y = ly + lh - UITREE_SCROLLBAR_THICKNESS;
    int sw = vscroll ? lw - UITREE_SCROLLBAR_THICKNESS : lw;
    if( py < sb_y || py >= sb_y + UITREE_SCROLLBAR_THICKNESS || px < lx || px >= lx + sw )
        return UITREE_SCROLLBAR_NONE;

    if( px < lx + UITREE_SCROLLBAR_THICKNESS )
        return UITREE_SCROLLBAR_H_LEFT;
    if( px >= lx + sw - UITREE_SCROLLBAR_THICKNESS )
        return UITREE_SCROLLBAR_H_RIGHT;

    int sx = 0;
    if( layer->component_id >= 0 )
        UITree_ScrollGetPos(scroll, layer->component_id, &sx, NULL);
    int track_w = sw - 32;
    if( track_w <= 0 )
        return UITREE_SCROLLBAR_H_GRIP;
    int grip_size = (track_w * lw) / layer->u.rs_layer.scroll_width;
    if( grip_size < 8 )
        grip_size = 8;
    if( grip_size > track_w )
        grip_size = track_w;
    int range = UITree_ScrollMaxX(layer);
    int grip_x = range > 0 ? ((track_w - grip_size) * sx) / range : 0;
    int grip_x0 = lx + UITREE_SCROLLBAR_THICKNESS + grip_x;
    if( px >= grip_x0 && px < grip_x0 + grip_size )
        return UITREE_SCROLLBAR_H_GRIP;
    return UITREE_SCROLLBAR_H_TRACK;
}

static bool
find_scrollbar_recursive(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int32_t node_index,
    int scroll_off_x,
    int scroll_off_y,
    int px,
    int py,
    struct UITreeScrollbarHitInfo* out)
{
    assert(tree);
    if( node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return false;

    struct UITreeComponent const* component = &tree->components[node_index];
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    UITree_LayoutGetBounds(&component->position, &bx, &by, &bw, &bh);

    if( component->type == UIELEM_RS_LAYER )
    {
        int const hit_bx = bx - scroll_off_x;
        int const hit_by = by - scroll_off_y;

        enum UITreeScrollbarHitKind vhit = hit_vertical_scrollbar(
            component, hit_bx, hit_by, bw, bh, scroll, px, py);
        if( vhit != UITREE_SCROLLBAR_NONE )
        {
            out->kind = vhit;
            out->layer_index = node_index;
            out->layer_x = hit_bx;
            out->layer_y = hit_by;
            out->layer_w = bw;
            out->layer_h = bh;
            out->scroll_height = component->u.rs_layer.scroll_height;
            out->scroll_width = component->u.rs_layer.scroll_width;
            return true;
        }

        enum UITreeScrollbarHitKind hhit = hit_horizontal_scrollbar(
            component, hit_bx, hit_by, bw, bh, scroll, px, py);
        if( hhit != UITREE_SCROLLBAR_NONE )
        {
            out->kind = hhit;
            out->layer_index = node_index;
            out->layer_x = hit_bx;
            out->layer_y = hit_by;
            out->layer_w = bw;
            out->layer_h = bh;
            out->scroll_height = component->u.rs_layer.scroll_height;
            out->scroll_width = component->u.rs_layer.scroll_width;
            return true;
        }

        int child_scroll_x = scroll_off_x;
        int child_scroll_y = scroll_off_y;
        if( UITree_ScrollLayerNeedsHorizontal(component) && component->component_id >= 0 )
        {
            int sx = 0;
            UITree_ScrollGetPos(scroll, component->component_id, &sx, NULL);
            child_scroll_x += sx;
        }
        if( UITree_ScrollLayerNeedsVertical(component) && component->component_id >= 0 )
        {
            int sy = 0;
            UITree_ScrollGetPos(scroll, component->component_id, NULL, &sy);
            child_scroll_y += sy;
        }

        for( int32_t child = component->first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            if( find_scrollbar_recursive(
                    tree, host, scroll, child, child_scroll_x, child_scroll_y, px, py, out) )
                return true;
        }
        return false;
    }

    if( !UITree_PointInScrolledBounds(px, py, bx, by, bw, bh, scroll_off_x, scroll_off_y) )
        return false;

    bool recurse_children = true;
    if( component->type == UIELEM_BUILTIN_SIDEBAR && host )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        if( UITree_Host(host, &req) != component->u.sidebar.tabno )
            recurse_children = false;
    }

    if( !recurse_children )
        return false;

    for( int32_t child = component->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        if( find_scrollbar_recursive(
                tree, host, scroll, child, scroll_off_x, scroll_off_y, px, py, out) )
            return true;
    }
    return false;
}

bool
UITree_FindScrollbarAt(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeScrollState const* scroll,
    int px,
    int py,
    struct UITreeScrollbarHitInfo* out)
{
    assert(tree);
    assert(out);
    if( tree->root_index < 0 )
        return false;

    memset(out, 0, sizeof(*out));
    for( int32_t root = tree->root_index; root >= 0;
         root = tree->components[root].next_sibling )
    {
        if( find_scrollbar_recursive(tree, host, scroll, root, 0, 0, px, py, out) )
            return true;
    }
    return false;
}

static bool
scrollbar_apply_vertical_grip(
    struct UITreeComponent const* layer,
    struct UITreeScrollbarHitInfo const* hit,
    int py,
    int max_y,
    int* sy)
{
    int vh = hit->layer_h;
    if( UITree_ScrollLayerNeedsHorizontal(layer) )
        vh -= UITREE_SCROLLBAR_THICKNESS;
    int track_h = vh - 32;
    if( track_h <= 0 )
        return false;
    int grip_size = (track_h * hit->layer_h) / hit->scroll_height;
    if( grip_size < 8 )
        grip_size = 8;
    if( grip_size > track_h )
        grip_size = track_h;
    int rel_y = py - hit->layer_y - UITREE_SCROLLBAR_THICKNESS - grip_size / 2;
    int max_rel = track_h - grip_size;
    if( max_rel < 0 )
        max_rel = 0;
    if( rel_y < 0 )
        rel_y = 0;
    if( rel_y > max_rel )
        rel_y = max_rel;
    *sy = max_rel > 0 ? (rel_y * max_y) / max_rel : 0;
    return true;
}

static bool
scrollbar_apply_horizontal_grip(
    struct UITreeComponent const* layer,
    struct UITreeScrollbarHitInfo const* hit,
    int px,
    int max_x,
    int* sx)
{
    int sw = hit->layer_w;
    (void)layer;
    int track_w = sw - 32;
    if( track_w <= 0 )
        return false;
    int grip_size = (track_w * hit->layer_w) / hit->scroll_width;
    if( grip_size < 8 )
        grip_size = 8;
    if( grip_size > track_w )
        grip_size = track_w;
    int rel_x = px - hit->layer_x - UITREE_SCROLLBAR_THICKNESS - grip_size / 2;
    int max_rel = track_w - grip_size;
    if( max_rel < 0 )
        max_rel = 0;
    if( rel_x < 0 )
        rel_x = 0;
    if( rel_x > max_rel )
        rel_x = max_rel;
    *sx = max_rel > 0 ? (rel_x * max_x) / max_rel : 0;
    return true;
}

bool
UITree_ScrollbarHandle(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    struct UITreeScrollbarHitInfo const* hit,
    int px,
    int py,
    enum UITreeScrollbarAction action,
    int step)
{
    assert(tree);
    assert(scroll);
    if( !hit || hit->kind == UITREE_SCROLLBAR_NONE || hit->layer_index < 0 )
        return false;

    struct UITreeComponent const* layer = &tree->components[hit->layer_index];
    if( layer->component_id < 0 )
        return false;

    int sx = 0;
    int sy = 0;
    UITree_ScrollGetPos(scroll, layer->component_id, &sx, &sy);
    int max_x = UITree_ScrollMaxX(layer);
    int max_y = UITree_ScrollMaxY(layer);
    int const delta = step > 0 ? step : UITREE_SCROLLBAR_ARROW_DELTA;

    switch( hit->kind )
    {
    case UITREE_SCROLLBAR_V_UP:
        if( action != UITREE_SCROLLBAR_ACTION_ARROW_STEP )
            return false;
        sy -= delta;
        break;
    case UITREE_SCROLLBAR_V_DOWN:
        if( action != UITREE_SCROLLBAR_ACTION_ARROW_STEP )
            return false;
        sy += delta;
        break;
    case UITREE_SCROLLBAR_H_LEFT:
        if( action != UITREE_SCROLLBAR_ACTION_ARROW_STEP )
            return false;
        sx -= delta;
        break;
    case UITREE_SCROLLBAR_H_RIGHT:
        if( action != UITREE_SCROLLBAR_ACTION_ARROW_STEP )
            return false;
        sx += delta;
        break;
    case UITREE_SCROLLBAR_V_GRIP:
    case UITREE_SCROLLBAR_V_TRACK:
        if( action != UITREE_SCROLLBAR_ACTION_GRIP_DRAG )
            return false;
        if( !scrollbar_apply_vertical_grip(layer, hit, py, max_y, &sy) )
            return false;
        break;
    case UITREE_SCROLLBAR_H_GRIP:
    case UITREE_SCROLLBAR_H_TRACK:
        if( action != UITREE_SCROLLBAR_ACTION_GRIP_DRAG )
            return false;
        if( !scrollbar_apply_horizontal_grip(layer, hit, px, max_x, &sx) )
            return false;
        break;
    default:
        return false;
    }

    if( sx < 0 )
        sx = 0;
    if( sy < 0 )
        sy = 0;
    if( sx > max_x )
        sx = max_x;
    if( sy > max_y )
        sy = max_y;
    UITree_ScrollSetPos(scroll, layer->component_id, sx, sy);
    return true;
}

bool
UITree_ScrollbarIsGripKind(enum UITreeScrollbarHitKind kind)
{
    switch( kind )
    {
    case UITREE_SCROLLBAR_V_GRIP:
    case UITREE_SCROLLBAR_V_TRACK:
    case UITREE_SCROLLBAR_H_GRIP:
    case UITREE_SCROLLBAR_H_TRACK:
        return true;
    default:
        return false;
    }
}

bool
UITree_ScrollbarIsArrowKind(enum UITreeScrollbarHitKind kind)
{
    switch( kind )
    {
    case UITREE_SCROLLBAR_V_UP:
    case UITREE_SCROLLBAR_V_DOWN:
    case UITREE_SCROLLBAR_H_LEFT:
    case UITREE_SCROLLBAR_H_RIGHT:
        return true;
    default:
        return false;
    }
}
