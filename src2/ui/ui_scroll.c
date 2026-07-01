#include "ui_scroll.h"

#include "uitree_layout.h"

#include <string.h>

bool
uitree_scroll_layer_needs_vertical(struct StaticUIComponent const* layer)
{
    if( !layer || layer->type != UIELEM_RS_LAYER )
        return false;
    int h = 0;
    uitree_layout_get_bounds(&layer->position, NULL, NULL, NULL, &h);
    return layer->u.rs_layer.scroll_height > h;
}

bool
uitree_scroll_layer_needs_horizontal(struct StaticUIComponent const* layer)
{
    if( !layer || layer->type != UIELEM_RS_LAYER )
        return false;
    int w = 0;
    uitree_layout_get_bounds(&layer->position, NULL, NULL, &w, NULL);
    return layer->u.rs_layer.scroll_width > w;
}

int
uitree_scroll_max_x(struct StaticUIComponent const* layer)
{
    int w = 0;
    if( !layer )
        return 0;
    uitree_layout_get_bounds(&layer->position, NULL, NULL, &w, NULL);
    int max = layer->u.rs_layer.scroll_width - w;
    return max > 0 ? max : 0;
}

int
uitree_scroll_max_y(struct StaticUIComponent const* layer)
{
    int h = 0;
    if( !layer )
        return 0;
    uitree_layout_get_bounds(&layer->position, NULL, NULL, NULL, &h);
    int max = layer->u.rs_layer.scroll_height - h;
    return max > 0 ? max : 0;
}

void
uitree_scroll_get_pos(
    struct UITreeScrollState const* scroll,
    int component_id,
    int* sx,
    int* sy)
{
    if( sx )
        *sx = 0;
    if( sy )
        *sy = 0;
    if( !scroll || component_id < 0 || component_id >= UITREE_SCROLL_MAX )
        return;
    if( sx && scroll->scroll_x )
        *sx = scroll->scroll_x[component_id];
    if( sy && scroll->scroll_y )
        *sy = scroll->scroll_y[component_id];
}

void
uitree_scroll_set_pos(
    struct UITreeScrollState const* scroll,
    int component_id,
    int sx,
    int sy)
{
    if( !scroll || component_id < 0 || component_id >= UITREE_SCROLL_MAX )
        return;
    if( scroll->scroll_x )
        scroll->scroll_x[component_id] = sx;
    if( scroll->scroll_y )
        scroll->scroll_y[component_id] = sy;
}

void
uitree_scroll_clamp_pos(
    struct StaticUIComponent const* layer,
    struct UITreeScrollState const* scroll,
    int component_id)
{
    int sx = 0;
    int sy = 0;
    uitree_scroll_get_pos(scroll, component_id, &sx, &sy);
    int max_x = uitree_scroll_max_x(layer);
    int max_y = uitree_scroll_max_y(layer);
    if( sx < 0 )
        sx = 0;
    if( sy < 0 )
        sy = 0;
    if( sx > max_x )
        sx = max_x;
    if( sy > max_y )
        sy = max_y;
    uitree_scroll_set_pos(scroll, component_id, sx, sy);
}

void
uitree_scroll_intersect_clip(
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
uitree_point_in_clip(int px, int py, struct UITreeScrollClip const* clip)
{
    if( !clip || clip->clip_w <= 0 || clip->clip_h <= 0 )
        return true;
    return px >= clip->clip_x && px < clip->clip_x + clip->clip_w && py >= clip->clip_y &&
           py < clip->clip_y + clip->clip_h;
}

bool
uitree_point_in_scrolled_bounds(
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
uitree_scroll_apply_ancestors(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    int32_t const* ancestors,
    int ancestor_count,
    int* bx,
    int* by,
    struct UITreeScrollClip* clip)
{
    if( !tree || !bx || !by || !clip )
        return;

    for( int i = 0; i < ancestor_count; i++ )
    {
        int32_t idx = ancestors[i];
        if( idx < 0 || (uint32_t)idx >= tree->component_count )
            continue;

        struct StaticUIComponent const* layer = &tree->components[idx];
        if( layer->type != UIELEM_RS_LAYER )
            continue;

        int ax = 0;
        int ay = 0;
        int aw = 0;
        int ah = 0;
        uitree_layout_get_bounds(&layer->position, &ax, &ay, &aw, &ah);
        uitree_scroll_intersect_clip(clip, ax, ay, aw, ah);

        bool vscroll = uitree_scroll_layer_needs_vertical(layer);
        bool hscroll = uitree_scroll_layer_needs_horizontal(layer);
        if( !vscroll && !hscroll )
            continue;

        int sx = 0;
        int sy = 0;
        if( layer->component_id >= 0 )
            uitree_scroll_get_pos(scroll, layer->component_id, &sx, &sy);

        if( hscroll )
            *bx -= sx;
        if( vscroll )
            *by -= sy;
    }
}

static bool
collect_path_recursive(
    struct UITree const* tree,
    int32_t target,
    int32_t current,
    int32_t* path,
    int* path_len,
    int max_len)
{
    if( current == target )
        return true;
    if( *path_len >= max_len )
        return false;

    path[*path_len] = current;
    (*path_len)++;

    struct StaticUIComponent const* node = &tree->components[current];
    for( int32_t child = node->first_child; child >= 0; child = tree->components[child].next_sibling )
    {
        if( collect_path_recursive(tree, target, child, path, path_len, max_len) )
            return true;
    }

    (*path_len)--;
    return false;
}

int
uitree_collect_ancestors(
    struct UITree const* tree,
    int32_t node_index,
    int32_t* ancestors,
    int max_ancestors)
{
    if( !tree || !ancestors || max_ancestors <= 0 || node_index < 0 )
        return 0;

    int32_t path[64];
    int path_len = 0;
    for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        path_len = 0;
        if( collect_path_recursive(tree, node_index, root, path, &path_len, 64) )
            break;
    }

    if( path_len <= 0 )
        return 0;

    int ancestor_count = path_len - 1;
    if( ancestor_count > max_ancestors )
        ancestor_count = max_ancestors;
    memcpy(ancestors, path, (size_t)ancestor_count * sizeof(int32_t));
    return ancestor_count;
}

static enum UITreeScrollbarHitKind
hit_vertical_scrollbar(
    struct StaticUIComponent const* layer,
    int lx,
    int ly,
    int lw,
    int lh,
    struct UITreeScrollState const* scroll,
    int px,
    int py)
{
    if( !uitree_scroll_layer_needs_vertical(layer) )
        return UITREE_SCROLLBAR_NONE;

    int sb_x = lx + lw;
    if( px < sb_x || px >= sb_x + UITREE_SCROLLBAR_THICKNESS || py < ly || py >= ly + lh )
        return UITREE_SCROLLBAR_NONE;

    int hscroll = uitree_scroll_layer_needs_horizontal(layer);
    int vh = hscroll ? lh - UITREE_SCROLLBAR_THICKNESS : lh;
    if( py < ly + UITREE_SCROLLBAR_THICKNESS )
        return UITREE_SCROLLBAR_V_UP;
    if( py >= ly + vh - UITREE_SCROLLBAR_THICKNESS )
        return UITREE_SCROLLBAR_V_DOWN;

    int sy = 0;
    if( layer->component_id >= 0 )
        uitree_scroll_get_pos(scroll, layer->component_id, NULL, &sy);
    int track_h = vh - 32;
    if( track_h <= 0 )
        return UITREE_SCROLLBAR_V_GRIP;
    int grip_size = (track_h * lh) / layer->u.rs_layer.scroll_height;
    if( grip_size < 8 )
        grip_size = 8;
    if( grip_size > track_h )
        grip_size = track_h;
    int range = uitree_scroll_max_y(layer);
    int grip_y = range > 0 ? ((track_h - grip_size) * sy) / range : 0;
    int grip_y0 = ly + UITREE_SCROLLBAR_THICKNESS + grip_y;
    if( py >= grip_y0 && py < grip_y0 + grip_size )
        return UITREE_SCROLLBAR_V_GRIP;
    return UITREE_SCROLLBAR_V_TRACK;
}

static enum UITreeScrollbarHitKind
hit_horizontal_scrollbar(
    struct StaticUIComponent const* layer,
    int lx,
    int ly,
    int lw,
    int lh,
    struct UITreeScrollState const* scroll,
    int px,
    int py)
{
    if( !uitree_scroll_layer_needs_horizontal(layer) )
        return UITREE_SCROLLBAR_NONE;

    int vscroll = uitree_scroll_layer_needs_vertical(layer);
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
        uitree_scroll_get_pos(scroll, layer->component_id, &sx, NULL);
    int track_w = sw - 32;
    if( track_w <= 0 )
        return UITREE_SCROLLBAR_H_GRIP;
    int grip_size = (track_w * lw) / layer->u.rs_layer.scroll_width;
    if( grip_size < 8 )
        grip_size = 8;
    if( grip_size > track_w )
        grip_size = track_w;
    int range = uitree_scroll_max_x(layer);
    int grip_x = range > 0 ? ((track_w - grip_size) * sx) / range : 0;
    int grip_x0 = lx + UITREE_SCROLLBAR_THICKNESS + grip_x;
    if( px >= grip_x0 && px < grip_x0 + grip_size )
        return UITREE_SCROLLBAR_H_GRIP;
    return UITREE_SCROLLBAR_H_TRACK;
}

static bool
find_scrollbar_recursive(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    int32_t node_index,
    int scroll_off_x,
    int scroll_off_y,
    int px,
    int py,
    int padding,
    struct UITreeScrollbarHitInfo* out)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return false;

    struct StaticUIComponent const* component = &tree->components[node_index];
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);

    if( component->type == UIELEM_RS_LAYER )
    {
        int hit_px = px;
        int hit_py = py;
        if( padding > 0 && uitree_scroll_layer_needs_vertical(component) )
        {
            int sb_x = bx + bw;
            if( px >= sb_x - padding && px < sb_x + UITREE_SCROLLBAR_THICKNESS + padding && py >= by &&
                py < by + bh )
                hit_px = sb_x + 1;
        }

        enum UITreeScrollbarHitKind vhit = hit_vertical_scrollbar(
            component, bx, by, bw, bh, scroll, hit_px, hit_py);
        if( vhit != UITREE_SCROLLBAR_NONE )
        {
            out->kind = vhit;
            out->layer_index = node_index;
            out->layer_x = bx;
            out->layer_y = by;
            out->layer_w = bw;
            out->layer_h = bh;
            out->scroll_height = component->u.rs_layer.scroll_height;
            out->scroll_width = component->u.rs_layer.scroll_width;
            return true;
        }

        enum UITreeScrollbarHitKind hhit = hit_horizontal_scrollbar(
            component, bx, by, bw, bh, scroll, hit_px, hit_py);
        if( hhit != UITREE_SCROLLBAR_NONE )
        {
            out->kind = hhit;
            out->layer_index = node_index;
            out->layer_x = bx;
            out->layer_y = by;
            out->layer_w = bw;
            out->layer_h = bh;
            out->scroll_height = component->u.rs_layer.scroll_height;
            out->scroll_width = component->u.rs_layer.scroll_width;
            return true;
        }

        int child_scroll_x = scroll_off_x;
        int child_scroll_y = scroll_off_y;
        if( uitree_scroll_layer_needs_horizontal(component) && component->component_id >= 0 )
        {
            int sx = 0;
            uitree_scroll_get_pos(scroll, component->component_id, &sx, NULL);
            child_scroll_x += sx;
        }
        if( uitree_scroll_layer_needs_vertical(component) && component->component_id >= 0 )
        {
            int sy = 0;
            uitree_scroll_get_pos(scroll, component->component_id, NULL, &sy);
            child_scroll_y += sy;
        }

        for( int32_t child = component->first_child; child >= 0;
             child = tree->components[child].next_sibling )
        {
            if( find_scrollbar_recursive(
                    tree, scroll, child, child_scroll_x, child_scroll_y, px, py, padding, out) )
                return true;
        }
        return false;
    }

    if( !uitree_point_in_scrolled_bounds(px, py, bx, by, bw, bh, scroll_off_x, scroll_off_y) )
        return false;

    for( int32_t child = component->first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        if( find_scrollbar_recursive(
                tree, scroll, child, scroll_off_x, scroll_off_y, px, py, padding, out) )
            return true;
    }
    return false;
}

bool
uitree_find_scrollbar_at(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    int px,
    int py,
    struct UITreeScrollbarHitInfo* out)
{
    return uitree_find_scrollbar_at_padded(tree, scroll, px, py, 0, out);
}

bool
uitree_find_scrollbar_at_padded(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    int px,
    int py,
    int padding,
    struct UITreeScrollbarHitInfo* out)
{
    if( !tree || !out || tree->root_index < 0 )
        return false;

    memset(out, 0, sizeof(*out));
    for( int32_t root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        if( find_scrollbar_recursive(tree, scroll, root, 0, 0, px, py, padding, out) )
            return true;
    }
    return false;
}

bool
uitree_scrollbar_hit_for_layer(
    struct UITree const* tree,
    int32_t layer_index,
    struct UITreeScrollbarHitInfo* out)
{
    if( !tree || !out || layer_index < 0 || (uint32_t)layer_index >= tree->component_count )
        return false;

    struct StaticUIComponent const* layer = &tree->components[layer_index];
    if( layer->type != UIELEM_RS_LAYER )
        return false;

    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    uitree_layout_get_bounds(&layer->position, &bx, &by, &bw, &bh);
    out->kind = uitree_scroll_layer_needs_vertical(layer) ? UITREE_SCROLLBAR_V_GRIP
                                                          : UITREE_SCROLLBAR_H_GRIP;
    out->layer_index = layer_index;
    out->layer_x = bx;
    out->layer_y = by;
    out->layer_w = bw;
    out->layer_h = bh;
    out->scroll_height = layer->u.rs_layer.scroll_height;
    out->scroll_width = layer->u.rs_layer.scroll_width;
    return true;
}

bool
uitree_scrollbar_is_grip_kind(enum UITreeScrollbarHitKind kind)
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
uitree_scrollbar_is_arrow_kind(enum UITreeScrollbarHitKind kind)
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

static bool
uitree_scrollbar_apply_vertical_grip(
    struct StaticUIComponent const* layer,
    struct UITreeScrollbarHitInfo const* hit,
    int py,
    int max_y,
    int* sy)
{
    int vh = hit->layer_h;
    if( uitree_scroll_layer_needs_horizontal(layer) )
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
    *sy = max_y > 0 ? (rel_y * max_y) / max_rel : 0;
    return true;
}

static bool
uitree_scrollbar_apply_horizontal_grip(
    struct StaticUIComponent const* layer,
    struct UITreeScrollbarHitInfo const* hit,
    int px,
    int max_x,
    int* sx)
{
    int sw = hit->layer_w;
    if( uitree_scroll_layer_needs_vertical(layer) )
        sw = hit->layer_w;
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
    *sx = max_x > 0 ? (rel_x * max_x) / max_rel : 0;
    return true;
}

bool
uitree_scrollbar_handle(
    struct UITree const* tree,
    struct UITreeScrollState const* scroll,
    struct UITreeScrollbarHitInfo const* hit,
    int px,
    int py,
    enum UITreeScrollbarAction action,
    int step)
{
    if( !tree || !scroll || !hit || hit->kind == UITREE_SCROLLBAR_NONE || hit->layer_index < 0 )
        return false;

    struct StaticUIComponent const* layer = &tree->components[hit->layer_index];
    if( layer->component_id < 0 )
        return false;

    int sx = 0;
    int sy = 0;
    uitree_scroll_get_pos(scroll, layer->component_id, &sx, &sy);
    int max_x = uitree_scroll_max_x(layer);
    int max_y = uitree_scroll_max_y(layer);
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
        if( !uitree_scrollbar_apply_vertical_grip(layer, hit, py, max_y, &sy) )
            return false;
        break;
    case UITREE_SCROLLBAR_H_GRIP:
    case UITREE_SCROLLBAR_H_TRACK:
        if( action != UITREE_SCROLLBAR_ACTION_GRIP_DRAG )
            return false;
        if( !uitree_scrollbar_apply_horizontal_grip(layer, hit, px, max_x, &sx) )
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
    uitree_scroll_set_pos(scroll, layer->component_id, sx, sy);
    return true;
}
