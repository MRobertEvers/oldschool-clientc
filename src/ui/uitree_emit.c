#include "uitree_emit.h"

#include "uitree_inv_view.h"
#include "uitree_layout.h"
#include "uitree_scroll.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static int
host_scrollbar_scene(struct UITreeHost const* host)
{
    struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SCROLLBAR_SCENE };
    int scene_id = UITree_Host(host, &req);
    return scene_id > 0 ? scene_id : -1;
}

static bool
layer_needs_scroll_offset(struct UITreeComponent const* c)
{
    if( !c || c->type != UIELEM_RS_LAYER )
        return false;
    return UITree_ScrollLayerNeedsVertical(c) || UITree_ScrollLayerNeedsHorizontal(c);
}

static bool
layer_is_if1_scrollbar(struct UITreeComponent const* c)
{
    return layer_needs_scroll_offset(c) && !c->if3;
}

static void
fill_scrollbar_v(
    struct UITreeComponent const* component,
    int32_t node_index,
    int x,
    int y,
    int w,
    int h,
    int scrollbar_scene,
    struct UITreeEmitDesc* out)
{
    memset(out, 0, sizeof(*out));
    out->kind = UITREE_EMIT_SCROLLBAR_V;
    out->node_index = node_index;
    out->component_id = component->component_id;
    out->x = x + w;
    out->y = y;
    out->w = UITREE_SCROLLBAR_THICKNESS;
    out->h = UITree_ScrollLayerNeedsHorizontal(component) ? h - UITREE_SCROLLBAR_THICKNESS : h;
    out->scroll_off_x = component->scroll_x;
    out->scroll_off_y = component->scroll_y;
    out->scroll_content = component->u.rs_layer.scroll_height;
    out->scene_id = scrollbar_scene;
    out->atlas_index = 0;
    out->if3 = 0;
}

static void
fill_scrollbar_h(
    struct UITreeComponent const* component,
    int32_t node_index,
    int x,
    int y,
    int w,
    int h,
    int scrollbar_scene,
    struct UITreeEmitDesc* out)
{
    memset(out, 0, sizeof(*out));
    out->kind = UITREE_EMIT_SCROLLBAR_H;
    out->node_index = node_index;
    out->component_id = component->component_id;
    out->x = x;
    out->y = y + h - UITREE_SCROLLBAR_THICKNESS;
    out->w = UITree_ScrollLayerNeedsVertical(component) ? w - UITREE_SCROLLBAR_THICKNESS : w;
    out->h = UITREE_SCROLLBAR_THICKNESS;
    out->scroll_off_x = component->scroll_x;
    out->scroll_off_y = component->scroll_y;
    out->scroll_content = component->u.rs_layer.scroll_width;
    out->scene_id = scrollbar_scene;
    out->atlas_index = 0;
    out->if3 = 0;
}

bool
UITree_EmitFill(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeComponent const* component,
    int32_t node_index,
    int hovered_component_id,
    struct UITreeEmitDesc* out)
{
    assert(tree);
    assert(component);
    assert(out);

    memset(out, 0, sizeof(*out));

    /* TS Client draw: components swap to their "active" (getIfActive) or "over"
     * (hovered) colour / text / sprite variant. Active is host-evaluated; hover
     * matches this component's own id. */
    bool const hovered =
        hovered_component_id >= 0 && component->component_id == hovered_component_id;
    bool const active = host ? UITree_ComponentIsActiveHost(host, component) : false;

    if( host )
    {
        if( !UITree_ComponentShouldEmit(component, host) )
            return false;
    }

    /* Fully transparent: skip self content; children still walked by emit_walk_node. */
    if( component->trans >= 255 )
        return false;

    int x = 0, y = 0, w = 0, h = 0;
    UITree_LayoutGetBounds(&component->position, &x, &y, &w, &h);

    out->node_index = node_index;
    out->component_id = component->component_id;
    out->x = x;
    out->y = y;
    out->w = w;
    out->h = h;
    out->if3 = component->if3;
    out->trans = component->trans;

    switch( component->type )
    {
    case UIELEM_BUILTIN_SPRITE:
    case UIELEM_RS_GRAPHIC:
        out->kind = UITREE_EMIT_SPRITE;
        if( component->type == UIELEM_BUILTIN_SPRITE )
        {
            out->scene_id = component->u.sprite.scene_id;
            out->atlas_index = component->u.sprite.atlas_index;
        }
        else
        {
            /* Match interfacex: world/minimap/compass placeholders are not blitted
             * as ordinary sprites (minimap mask would draw as opaque black). */
            switch( component->behavior.client_code )
            {
            case 1337: /* CONTENT_WORLD */
            case 1338: /* CONTENT_MINIMAP */
            case 1339: /* CONTENT_COMPASS */
                return false;
            default:
                break;
            }
            /* SETOBJECT on type-5 stores the icon in item_*; SETGRAPHIC chrome
             * stays in rs_graphic.scene_id. Prefer the item overlay when set.
             * Reference draws the 36x32 icon at the widget rect with no
             * draw-time centering (widgets-gl type-5 itemId path). */
            if( component->item_id > 0 && component->item_scene_id > 0 )
            {
                out->scene_id = component->item_scene_id;
                out->atlas_index = component->item_atlas_index;
                out->tiled = 0;
                out->outline = 0;
                out->graphic_shadow = 0;
                out->flip_h = component->u.rs_graphic.flip_h;
                out->flip_v = component->u.rs_graphic.flip_v;
            }
            else if( active && component->u.rs_graphic.scene_id_active > 0 )
            {
                /* getIfActive -> graphic2 (TS Client TYPE_GRAPHIC draw). */
                out->scene_id = component->u.rs_graphic.scene_id_active;
                out->atlas_index = component->u.rs_graphic.atlas_index_active;
                out->tiled = component->u.rs_graphic.tiled;
                out->outline = component->u.rs_graphic.outline;
                out->graphic_shadow = component->u.rs_graphic.graphic_shadow;
                out->flip_h = component->u.rs_graphic.flip_h;
                out->flip_v = component->u.rs_graphic.flip_v;
            }
            else
            {
                out->scene_id = component->u.rs_graphic.scene_id;
                out->atlas_index = component->u.rs_graphic.atlas_index;
                out->tiled = component->u.rs_graphic.tiled;
                out->outline = component->u.rs_graphic.outline;
                out->graphic_shadow = component->u.rs_graphic.graphic_shadow;
                out->flip_h = component->u.rs_graphic.flip_h;
                out->flip_v = component->u.rs_graphic.flip_v;
            }
            if( component->u.rs_graphic.graphic_hitbox_only )
                return false;
        }
        if( out->scene_id <= 0 )
            return false;
        out->rotation = UITree_ComponentSpriteRotation(component, host);
        return true;

    case UIELEM_RS_TEXT:
    {
        char const* text = component->u.rs_text.text;
        int color = component->u.rs_text.color;
        /* TS Client TYPE_TEXT: active -> colour2 (+ text2); else colour. Either way
         * a hover overrides to the matching *Over colour when non-zero. */
        if( active )
        {
            color = component->behavior.active_color;
            if( hovered && component->behavior.active_over_color != 0 )
                color = component->behavior.active_over_color;
            if( component->u.rs_text.text_active && component->u.rs_text.text_active[0] )
                text = component->u.rs_text.text_active;
        }
        else if( hovered && component->behavior.over_color != 0 )
        {
            color = component->behavior.over_color;
        }
        if( !text || text[0] == '\0' )
            return false;
        out->kind = UITREE_EMIT_TEXT;
        out->text = text;
        out->font_id = component->u.rs_text.font_id;
        out->color = color;
        out->text_center = component->u.rs_text.center;
        out->text_y_align = component->u.rs_text.y_align;
        out->text_shadowed = component->u.rs_text.shadowed;
        out->text_line_height = component->u.rs_text.line_height;
        return true;
    }

    case UIELEM_RS_RECT:
    {
        int color = component->u.rs_rect.color;
        if( active )
        {
            color = component->behavior.active_color;
            if( hovered && component->behavior.active_over_color != 0 )
                color = component->behavior.active_over_color;
        }
        else if( hovered && component->behavior.over_color != 0 )
        {
            color = component->behavior.over_color;
        }
        out->kind = UITREE_EMIT_RECT;
        out->color = color;
        out->filled = component->u.rs_rect.filled;
        return true;
    }

    case UIELEM_RS_LINE:
        out->kind = UITREE_EMIT_LINE;
        out->color = component->u.rs_line.color;
        out->line_width = component->u.rs_line.line_width;
        out->line_direction = component->u.rs_line.horizontal ? 1 : 0;
        return true;

    case UIELEM_BUILTIN_PLAYERMODEL:
        /* Builtin player preview placeholder — same stub as clientCode 328. */
        out->kind = UITREE_EMIT_RECT;
        out->color = 0x2a2a2a;
        out->filled = 1;
        return true;

    case UIELEM_RS_MODEL:
        /* clientCode 328 = local player preview; cache often has modelId=-1. */
        if( component->u.rs_model.gamecache_model_id < 0 )
        {
            if( component->behavior.client_code == 328 )
            {
                /* Explicit stub until appearance compositing exists — visible fill
                 * so the preview slot is not silently dropped from the emit list. */
                out->kind = UITREE_EMIT_RECT;
                out->color = 0x2a2a2a;
                out->filled = 1;
                return true;
            }
            return false;
        }
        out->kind = UITREE_EMIT_MODEL;
        out->model_id = component->u.rs_model.gamecache_model_id;
        out->model_zoom = component->u.rs_model.zoom;
        out->model_xan = component->u.rs_model.xan;
        out->model_yan = component->u.rs_model.yan;
        out->model_zan = component->u.rs_model.zan;
        out->model_x_offset = component->u.rs_model.x_offset;
        out->model_y_offset = component->u.rs_model.y_offset;
        out->model_orthog = component->u.rs_model.orthog;
        out->model_fixed_zoom = component->u.rs_model.fixed_zoom;
        return true;

    case UIELEM_CC_OBJ:
        if( component->u.cc_obj.obj_id <= 0 )
            return false;
        out->kind = UITREE_EMIT_CC_OBJ;
        out->obj_id = component->u.cc_obj.obj_id;
        out->obj_count = component->u.cc_obj.obj_count;
        out->scene_id = component->u.cc_obj.scene_id;
        out->atlas_index = component->u.cc_obj.atlas_index;
        return true;

    case UIELEM_BUILTIN_WORLD:
        out->kind = UITREE_EMIT_WORLD;
        return true;

    case UIELEM_BUILTIN_MINIMAP:
        out->kind = UITREE_EMIT_MINIMAP;
        out->scene_id = component->u.minimap.scene_id;
        return true;

    case UIELEM_BUILTIN_COMPASS:
        out->kind = UITREE_EMIT_COMPASS;
        out->scene_id = component->u.sprite.scene_id;
        out->atlas_index = component->u.sprite.atlas_index;
        out->rotation = UITree_ComponentSpriteRotation(component, host);
        return true;

    case UIELEM_RS_LAYER:
    {
        int sb_scene;
        struct UITreeComponent* layer_mut;
        if( component->if3 )
            return false;
        if( !UITree_ScrollLayerNeedsVertical(component) &&
            !UITree_ScrollLayerNeedsHorizontal(component) )
            return false;
        /* Clamp scroll position for thumb math (IF1). */
        layer_mut = (struct UITreeComponent*)component;
        UITree_ScrollClampComponent(layer_mut);
        sb_scene = host_scrollbar_scene(host);
        /* Prefer vertical when both axes need chrome (EmitWalk emits H after children). */
        if( UITree_ScrollLayerNeedsVertical(component) )
        {
            fill_scrollbar_v(component, node_index, x, y, w, h, sb_scene, out);
            return true;
        }
        fill_scrollbar_h(component, node_index, x, y, w, h, sb_scene, out);
        return true;
    }

    case UIELEM_BUILTIN_SIDEBAR:
    case UIELEM_BUILTIN_CHAT:
    case UIELEM_BUILTIN_CHAT_BUTTON:
    case UIELEM_BUILTIN_REDSTONE_TAB:
    case UIELEM_BUILTIN_TAB_ICONS:
    case UIELEM_BUILTIN_CROSS:
    case UIELEM_BUILTIN_MINIMENU:
    case UIELEM_RS_INV:
    case UIELEM_RS_INV_TEXT:
        return false;
    }

    return false;
}

void
UITree_EmitBufferInit(struct UITreeEmitBuffer* buf)
{
    assert(buf);
    memset(buf, 0, sizeof(*buf));
}

void
UITree_EmitBufferFree(struct UITreeEmitBuffer* buf)
{
    if( !buf )
        return;
    free(buf->cmds);
    memset(buf, 0, sizeof(*buf));
}

static void
emit_buffer_append(
    struct UITreeEmitBuffer* buf,
    struct UITreeEmitDesc const* desc);

/** Expand TYPE_INV grid into per-slot sprites via host GET_INV_SOURCE_SLOT. */
static void
emit_rs_inv_slots(
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    struct UITreeComponent const* c,
    int32_t idx,
    int x,
    int y,
    int scroll_off_x,
    int scroll_off_y,
    int in_drag,
    int drag_dx,
    int drag_dy,
    int in_deferred,
    struct UITreeEmitClip const* parent_clip)
{
    struct UITreeInvGridLayout layout;
    int slot_limit;
    int slot;

    assert(c && c->type == UIELEM_RS_INV);
    assert(out && parent_clip);

    layout.cols = c->u.rs_inv.cols;
    layout.rows = c->u.rs_inv.rows;
    layout.margin_x = c->u.rs_inv.margin_x;
    layout.margin_y = c->u.rs_inv.margin_y;
    layout.offset_x = c->u.rs_inv.inv_slot_offset_x;
    layout.offset_y = c->u.rs_inv.inv_slot_offset_y;
    slot_limit = UITree_InvViewGridSlotLimit(&layout);

    for( slot = 0; slot < slot_limit; slot++ )
    {
        struct UIInvSlotData slot_data;
        struct UITreeEmitDesc desc;
        int slot_x = 0;
        int slot_y = 0;
        int slot_w = 0;
        int slot_h = 0;
        int scene_id = -1;
        int atlas_index = 0;
        int obj_id = 0;
        int obj_count = 0;

        UITree_InvViewGridRect(x, y, &layout, slot, &slot_x, &slot_y, &slot_w, &slot_h);

        memset(&slot_data, 0, sizeof(slot_data));
        {
            struct UITreeHostRequest req = {
                .kind = UITREE_HOST_GET_INV_SOURCE_SLOT,
                .u.get_inv_source_slot.source_id = c->u.rs_inv.inv_source_id,
                .u.get_inv_source_slot.slot = slot,
                .u.get_inv_source_slot.out = &slot_data,
            };
            if( UITree_Host(host, &req) )
            {
                obj_id = slot_data.obj_id;
                obj_count = slot_data.obj_count;
                scene_id = slot_data.scene_id;
                atlas_index = slot_data.atlas_index;
            }
        }

        if( obj_id > 0 && scene_id >= 0 )
        {
            /* keep item icon */
        }
        else if( slot < UI_INV_SLOT_OFFSET_MAX )
        {
            int bg_scene = c->u.rs_inv.inv_slot_bg_scene_id[slot];
            int bg_atlas = c->u.rs_inv.inv_slot_bg_atlas_index[slot];
            if( bg_scene < 0 )
                continue;
            scene_id = bg_scene;
            atlas_index = bg_atlas;
            obj_id = 0;
            obj_count = 0;
        }
        else
        {
            continue;
        }

        memset(&desc, 0, sizeof(desc));
        desc.kind = UITREE_EMIT_SPRITE;
        desc.node_index = idx;
        desc.component_id = c->component_id;
        desc.x = slot_x;
        desc.y = slot_y;
        desc.w = slot_w;
        desc.h = slot_h;
        desc.scene_id = scene_id;
        desc.atlas_index = atlas_index;
        desc.obj_id = obj_id;
        desc.obj_count = obj_count;
        desc.inv_source_id = c->u.rs_inv.inv_source_id;
        desc.inv_slot = slot;
        desc.if3 = 0;

        desc.x -= scroll_off_x;
        desc.y -= scroll_off_y;
        if( in_drag )
        {
            desc.x += drag_dx;
            desc.y += drag_dy;
        }
        if( in_deferred )
        {
            if( c->drag_visual_trans >= 0 )
                desc.trans = c->drag_visual_trans;
            else if( desc.trans < 128 )
                desc.trans = 128;
        }
        desc.scroll_off_x = scroll_off_x;
        desc.scroll_off_y = scroll_off_y;
        desc.clip = *parent_clip;
        emit_buffer_append(out, &desc);
    }
}

static void
emit_buffer_append(
    struct UITreeEmitBuffer* buf,
    struct UITreeEmitDesc const* desc)
{
    assert(buf && desc);
    if( buf->count >= buf->cap )
    {
        int n = buf->cap == 0 ? 64 : buf->cap * 2;
        struct UITreeEmitDesc* grown =
            realloc(buf->cmds, (size_t)n * sizeof(struct UITreeEmitDesc));
        assert(grown);
        buf->cmds = grown;
        buf->cap = n;
    }
    buf->cmds[buf->count++] = *desc;
}

static int
clip_intersect(
    struct UITreeEmitClip* dst,
    struct UITreeEmitClip const* a,
    int x,
    int y,
    int w,
    int h)
{
    int x0 = a->x;
    int y0 = a->y;
    int x1 = a->x + a->w;
    int y1 = a->y + a->h;
    int bx0 = x;
    int by0 = y;
    int bx1 = x + w;
    int by1 = y + h;
    if( bx0 > x0 )
        x0 = bx0;
    if( by0 > y0 )
        y0 = by0;
    if( bx1 < x1 )
        x1 = bx1;
    if( by1 < y1 )
        y1 = by1;
    if( x0 >= x1 || y0 >= y1 )
    {
        dst->x = dst->y = dst->w = dst->h = 0;
        return 0;
    }
    dst->x = x0;
    dst->y = y0;
    dst->w = x1 - x0;
    dst->h = y1 - y0;
    return 1;
}

static void
emit_append_layer_scrollbars(
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    struct UITreeComponent* layer,
    int32_t idx,
    struct UITreeEmitClip const* parent_clip,
    int x,
    int y,
    int w,
    int h)
{
    int sb_scene;
    int vscroll;
    int hscroll;
    struct UITreeEmitDesc desc;
    struct UITreeEmitClip bar_clip;

    assert(layer && out && parent_clip);
    assert(layer->type == UIELEM_RS_LAYER && !layer->if3);

    UITree_ScrollClampComponent(layer);
    sb_scene = host_scrollbar_scene(host);
    vscroll = UITree_ScrollLayerNeedsVertical(layer);
    hscroll = UITree_ScrollLayerNeedsHorizontal(layer);

    if( vscroll )
    {
        int bar_h = hscroll ? h - UITREE_SCROLLBAR_THICKNESS : h;
        fill_scrollbar_v(layer, idx, x, y, w, h, sb_scene, &desc);
        if( clip_intersect(
                &bar_clip, parent_clip, x + w, y, UITREE_SCROLLBAR_THICKNESS, bar_h) )
        {
            desc.clip = bar_clip;
            emit_buffer_append(out, &desc);
        }
    }
    if( hscroll )
    {
        int bar_w = vscroll ? w - UITREE_SCROLLBAR_THICKNESS : w;
        fill_scrollbar_h(layer, idx, x, y, w, h, sb_scene, &desc);
        if( clip_intersect(
                &bar_clip,
                parent_clip,
                x,
                y + h - UITREE_SCROLLBAR_THICKNESS,
                bar_w,
                UITREE_SCROLLBAR_THICKNESS) )
        {
            desc.clip = bar_clip;
            emit_buffer_append(out, &desc);
        }
    }
}

static int
child_is_interface_parent_mount(
    struct UITree const* tree,
    int container_uid,
    struct UITreeComponent const* child)
{
    int mi;
    int group;
    if( !child || container_uid < 0 )
        return 0;
    group = (child->component_id >> 16) & 0xffff;
    for( mi = 0; mi < tree->interface_parent_count; mi++ )
    {
        if( tree->interface_parents[mi].container_uid == container_uid &&
            tree->interface_parents[mi].group_id == group )
            return 1;
    }
    return 0;
}

static void
emit_walk_node(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    int32_t idx,
    struct UITreeEmitClip const* parent_clip,
    int scroll_off_x,
    int scroll_off_y,
    int hovered_component_id,
    int drag_pass,
    int in_drag,
    int drag_dx,
    int drag_dy,
    int in_deferred)
{
    struct UITreeComponent* c;
    struct UITreeEmitDesc desc;
    struct UITreeEmitClip layer_clip;
    struct UITreeEmitClip const* child_clip;
    int x = 0, y = 0, w = 0, h = 0;
    int32_t child;
    int if1_bar;
    int scroll_layer;
    int child_scroll_x;
    int child_scroll_y;

    assert(tree && out && parent_clip);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;

    c = &tree->components[idx];
    /* Hide-gated layers stay invisible unless their component_id is hovered. */
    if( c->behavior.hide && !UITree_ComponentVisibleById(c, hovered_component_id) )
        return;

    /* Inactive sidebar tabs prune their whole mounted subtree (same gate as
     * UITree_ComponentVisibleHost; ShouldEmit only skips the container's own
     * draw, not its children). */
    if( c->type == UIELEM_BUILTIN_SIDEBAR && host )
    {
        struct UITreeHostRequest tab_req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        if( UITree_Host(host, &tab_req) != c->u.sidebar.tabno )
            return;
    }

    UITree_LayoutGetBounds(&c->position, &x, &y, &w, &h);

    /* A drag source begins a screen-space translation that carries to its whole
     * subtree, so a composite widget (e.g. a scrollbar thumb built from cap +
     * middle sprites) moves as one unit rather than only its own drawn content.
     * drag_visual_x/y are screen coords (mouse - pickup), so the delta is taken
     * against this node's pre-drag screen position (abs - scroll offset). */
    if( c->drag_active )
    {
        drag_dx = c->drag_visual_x - (x - scroll_off_x);
        drag_dy = c->drag_visual_y - (y - scroll_off_y);
        in_drag = 1;
        /* Picked-up drags (behavior != 1) defer the whole subtree to the top
         * drag pass; scrollbar-style drags (behavior 1) stay in place. */
        in_deferred = (c->drag_behavior != 1);
    }

    if( in_deferred )
    {
        /* A deferred drag subtree draws only on the drag pass. */
        if( !drag_pass )
            return;
    }
    else if( drag_pass )
    {
        /* On the drag pass, non-deferred nodes only descend to reach any
         * deferred drag source deeper in the tree; they do not draw here.
         * Same mount-last sweep as the draw path so both agree on order. */
        for( int mount_sweep = 0; mount_sweep < 2; mount_sweep++ )
        {
            for( child = c->first_child; child >= 0;
                 child = tree->components[child].next_sibling )
            {
                if( child_is_interface_parent_mount(
                        tree, c->component_id, &tree->components[child]) != mount_sweep )
                    continue;
                emit_walk_node(
                    tree,
                    host,
                    out,
                    child,
                    parent_clip,
                    scroll_off_x,
                    scroll_off_y,
                    hovered_component_id,
                    drag_pass,
                    in_drag,
                    drag_dx,
                    drag_dy,
                    in_deferred);
            }
        }
        return;
    }

    scroll_layer = layer_needs_scroll_offset(c);
    if1_bar = layer_is_if1_scrollbar(c);
    if( scroll_layer )
        UITree_ScrollClampComponent(c);

    child_scroll_x = scroll_off_x;
    child_scroll_y = scroll_off_y;
    if( scroll_layer )
    {
        if( UITree_ScrollLayerNeedsHorizontal(c) )
            child_scroll_x += c->scroll_x;
        if( UITree_ScrollLayerNeedsVertical(c) )
            child_scroll_y += c->scroll_y;
    }

    child_clip = parent_clip;
    if( UITree_ComponentClipsChildren(c) && w > 0 && h > 0 )
    {
        int clip_x = x - scroll_off_x + (in_drag ? drag_dx : 0);
        int clip_y = y - scroll_off_y + (in_drag ? drag_dy : 0);
        if( clip_intersect(&layer_clip, parent_clip, clip_x, clip_y, w, h) )
            child_clip = &layer_clip;
    }

    if( !if1_bar && c->type == UIELEM_RS_INV )
    {
        emit_rs_inv_slots(
            host,
            out,
            c,
            idx,
            x,
            y,
            scroll_off_x,
            scroll_off_y,
            in_drag,
            drag_dx,
            drag_dy,
            in_deferred,
            parent_clip);
    }
    else if( !if1_bar && UITree_EmitFill(tree, host, c, idx, hovered_component_id, &desc) )
    {
        if( desc.kind != UITREE_EMIT_WORLD && desc.kind != UITREE_EMIT_MINIMAP &&
            desc.kind != UITREE_EMIT_COMPASS )
        {
            desc.x -= scroll_off_x;
            desc.y -= scroll_off_y;
            if( in_drag )
            {
                /* Shift the whole picked-up subtree by the drag delta. */
                desc.x += drag_dx;
                desc.y += drag_dy;
            }
            if( in_deferred )
            {
                /* Ghost the picked-up widget (source uses its own trans;
                 * children fall back to a translucent default). */
                if( c->drag_visual_trans >= 0 )
                    desc.trans = c->drag_visual_trans;
                else if( desc.trans < 128 )
                    desc.trans = 128;
            }
            desc.scroll_off_x = scroll_off_x;
            desc.scroll_off_y = scroll_off_y;
            desc.clip = *parent_clip;
            emit_buffer_append(out, &desc);
        }
    }

    /* Sweep 0 draws the container's own children, sweep 1 the InterfaceParent
     * mounts — reference widgets-gl renders mounted interface roots LAST, on top
     * of the container's own children. Mounts are ordinary children here
     * (task_cs2_run reparents the pack root under the container) and
     * link_under_parent appends, so without this they only stay on top until the
     * container gains another child. */
    for( int mount_sweep = 0; mount_sweep < 2; mount_sweep++ )
    {
        for( child = c->first_child; child >= 0; child = tree->components[child].next_sibling )
        {
            int sx = child_scroll_x;
            int sy = child_scroll_y;
            int const is_mount =
                child_is_interface_parent_mount(tree, c->component_id, &tree->components[child]);
            if( is_mount != mount_sweep )
                continue;
            /* InterfaceParent mounts: no scroll offset (TS widgets-gl). */
            if( is_mount )
            {
                sx = scroll_off_x;
                sy = scroll_off_y;
            }
            emit_walk_node(
                tree,
                host,
                out,
                child,
                child_clip,
                sx,
                sy,
                hovered_component_id,
                drag_pass,
                in_drag,
                drag_dx,
                drag_dy,
                in_deferred);
        }
    }

    if( if1_bar && !drag_pass )
    {
        emit_append_layer_scrollbars(
            host, out, c, idx, parent_clip, x - scroll_off_x, y - scroll_off_y, w, h);
    }
}

static void
emit_walk_pass(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    int canvas_w,
    int canvas_h,
    int hovered_component_id,
    int drag_pass)
{
    struct UITreeEmitClip root_clip;
    int32_t root;

    assert(tree && out);
    root_clip.x = 0;
    root_clip.y = 0;
    root_clip.w = canvas_w;
    root_clip.h = canvas_h;

    for( root = tree->root_index; root >= 0; root = tree->components[root].next_sibling )
    {
        emit_walk_node(
            tree,
            host,
            out,
            root,
            &root_clip,
            0,
            0,
            hovered_component_id,
            drag_pass,
            0,
            0,
            0,
            0);
    }
}

void
UITree_EmitWalk(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    int hovered_component_id)
{
    assert(tree);
    assert(out);
    /* Single interleaved pass in tree order (reference widgets-gl drawNode emits a
     * widget's own fill/sprite/text inline, then descends into children), then
     * deferred drag sources on top. Splitting text into its own pass put every
     * text in the tree above every non-text, so a widget group that should cover
     * an earlier one — an open dropdown over a label — drew under its text. */
    emit_walk_pass(
        tree, host, out, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, hovered_component_id, 0);
    emit_walk_pass(
        tree, host, out, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, hovered_component_id, 1);
}
