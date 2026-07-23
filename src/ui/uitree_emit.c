#include "uitree_emit.h"

#include "uitree_chatview.h"
#include "uitree_hovertext.h"
#include "uitree_inv_view.h"
#include "uitree_layout.h"
#include "uitree_minimenu.h"
#include "uitree_scroll.h"

static int
clip_intersect(
    struct UITreeEmitClip* out,
    struct UITreeEmitClip const* parent,
    int x,
    int y,
    int w,
    int h);

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
host_scrollbar_scene(struct UITreeHost const* host)
{
    struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_SCROLLBAR_SCENE };
    int scene_id = UITree_Host(host, &req);
    return scene_id > 0 ? scene_id : -1;
}

/** Scene id of a client-hardcoded sprite (no owning node), or -1. */
static int
host_static_sprite_scene(
    struct UITreeHost const* host,
    enum UITreeStaticSpriteSlot slot)
{
    struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_STATIC_SPRITE_SCENE };
    int scene_id;
    if( !host )
        return -1;
    req.u.static_sprite.slot = (int)slot;
    scene_id = UITree_Host(host, &req);
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

/**
 * Expand CS1 %1..%5 placeholders into out->text_formatted.
 *
 * The reference client substitutes the value of the component's Nth value
 * script, rendering anything at or above CS1's "infinity" as "*" (the
 * inv-contains sentinel). Values come from the host, which serves them from
 * the last evaluation pass — drawing never runs the VM.
 */
static void
uitree_emit_format_placeholders(
    struct UITreeComponent const* component,
    struct UITreeHost const* host,
    char const* text,
    struct UITreeEmitDesc* out)
{
    assert(component);
    assert(text);
    assert(out);

    out->text_formatted[0] = '\0';
    if( component->behavior.scripts_count <= 0 || !strchr(text, '%') )
        return;

    size_t written = 0;
    for( char const* src = text; *src && written + 1 < sizeof(out->text_formatted); )
    {
        if( src[0] == '%' && src[1] >= '1' && src[1] <= '0' + UITREE_CS1_VALUE_MAX )
        {
            struct UITreeHostRequest req;
            memset(&req, 0, sizeof(req));
            req.kind = UITREE_HOST_EVAL_TEXT_PLACEHOLDER;
            req.u.eval_text_placeholder.component = component;
            req.u.eval_text_placeholder.script_idx = src[1] - '1';

            int value = UITree_Host(host, &req);

            char buf[16];
            int len;
            if( value < UITREE_CS1_VALUE_INFINITY )
                len = snprintf(buf, sizeof(buf), "%d", value);
            else
                len = snprintf(buf, sizeof(buf), "*");

            for( int i = 0; i < len && written + 1 < sizeof(out->text_formatted); i++ )
                out->text_formatted[written++] = buf[i];

            src += 2;
            continue;
        }

        out->text_formatted[written++] = *src++;
    }

    out->text_formatted[written] = '\0';
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
             * as ordinary sprites (minimap mask would draw as opaque black).
             * uitree_build.c retypes these to their builtin, so this only catches
             * content slots that reached the tree some other way (CS2 dynamics). */
            switch( component->behavior.client_code )
            {
            case UITREE_CLIENT_CODE_CONTENT_WORLD:
            case UITREE_CLIENT_CODE_CONTENT_MINIMAP:
            case UITREE_CLIENT_CODE_CONTENT_COMPASS:
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
        uitree_emit_format_placeholders(component, host, text, out);
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
        out->world_level_mask = component->u.world.level_mask;
        return true;

    case UIELEM_BUILTIN_MINIMAP:
    {
        /* The pack graphic is only a mask placeholder; the drawable is the world
         * map the host bakes, which also owns the camera pivot inside it. */
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_MINIMAP_STATE,
            .u.get_minimap_state.out_src_anchor_x = &out->src_anchor_x,
            .u.get_minimap_state.out_src_anchor_y = &out->src_anchor_y,
        };
        out->kind = UITREE_EMIT_MINIMAP;
        out->scene_id = UITree_Host(host, &req);
        if( out->scene_id <= 0 )
            return false;
        /* That mask placeholder clips the over-filled map to its round window
         * (inverted: map shows where the mask is transparent). */
        out->mask_scene_id = component->u.minimap.mask_scene_id;
        out->mask_atlas_index = component->u.minimap.mask_atlas_index;
        out->rotation_r2pi2048 = UITree_ComponentSpriteRotation(component, host);
        /* Entity/flag overlay dots, computed by the host in center-relative
         * pixels (reference minimapDraw). */
        {
            struct UITreeHostRequest dots_req = {
                .kind = UITREE_HOST_GET_MINIMAP_DOTS,
                .u.get_minimap_dots.out_dots = &out->minimap_dots,
            };
            out->minimap_dot_count = UITree_Host(host, &dots_req);
        }
        return true;
    }

    case UIELEM_BUILTIN_ENTITY_OVERLAY:
    {
        /* Reference drawEntities runs inside the scene pass, so the overlay is
         * clipped to the world box the app reports; positions are absolute
         * screen pixels the host already projected. */
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_ENTITY_OVERLAYS,
            .u.get_entity_overlays.out_items = &out->entity_overlays,
            .u.get_entity_overlays.out_clip_x = &out->clip.x,
            .u.get_entity_overlays.out_clip_y = &out->clip.y,
            .u.get_entity_overlays.out_clip_w = &out->clip.w,
            .u.get_entity_overlays.out_clip_h = &out->clip.h,
        };
        out->kind = UITREE_EMIT_ENTITY_OVERLAY;
        out->entity_overlay_count = UITree_Host(host, &req);
        return out->entity_overlay_count > 0;
    }

    case UIELEM_BUILTIN_COMPASS:
        out->kind = UITREE_EMIT_COMPASS;
        out->scene_id = component->u.sprite.scene_id;
        out->atlas_index = component->u.sprite.atlas_index;
        /* No RevConfig sprite= binding (interface-open path): fall back to the
         * client-hardcoded compass the host loaded. */
        if( out->scene_id <= 0 )
            out->scene_id = host_static_sprite_scene(host, UITREE_STATIC_SPRITE_COMPASS);
        if( out->scene_id <= 0 )
            return false;
        /* The pack's placeholder graphic doubles as the circular clip. */
        out->mask_scene_id = component->u.sprite.mask_scene_id;
        out->mask_atlas_index = component->u.sprite.mask_atlas_index;
        out->rotation_r2pi2048 = UITree_ComponentSpriteRotation(component, host);
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

    case UIELEM_BUILTIN_CROSS:
    {
        /* Reference drawMinimenu-adjacent cross draw: 8-frame pack centered on
         * the click point (Client.ts plots cross[cycle/100] at crossX-8). */
        int cx = 0;
        int cy = 0;
        struct UITreeHostRequest pos_req = {
            .kind = UITREE_HOST_GET_CROSS_POSITION,
            .u.get_cross_position.out_x = &cx,
            .u.get_cross_position.out_y = &cy,
        };
        if( !UITree_Host(host, &pos_req) )
            return false;
        out->kind = UITREE_EMIT_SPRITE;
        out->scene_id = host_static_sprite_scene(host, UITREE_STATIC_SPRITE_CROSS);
        if( out->scene_id <= 0 )
            return false;
        {
            struct UITreeHostRequest frame_req = { .kind =
                                                       UITREE_HOST_GET_CROSS_ATLAS_FRAME };
            out->atlas_index = UITree_Host(host, &frame_req);
        }
        out->x = cx - 8;
        out->y = cy - 8;
        out->w = 16;
        out->h = 16;
        return true;
    }

    case UIELEM_BUILTIN_TAB_ICONS:
    {
        /* Icons draw only for tabs with an interface assigned (reference
         * drawSidebarIcons gates on sideOverlayId[n] != -1). */
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_TAB_ENABLED,
            .u.tab_enabled.tabno = component->u.tab_icon.tabno,
        };
        if( host && !UITree_Host(host, &req) )
            return false;
        out->kind = UITREE_EMIT_SPRITE;
        out->scene_id = component->u.tab_icon.scene_id;
        out->atlas_index = component->u.tab_icon.atlas_index;
        if( out->scene_id <= 0 )
            return false;
        return true;
    }

    case UIELEM_BUILTIN_REDSTONE_TAB:
        /* ShouldEmit already gated on this being the selected tab; the node
         * carries both variants and the highlight is the active one. */
        out->kind = UITREE_EMIT_SPRITE;
        out->scene_id = component->u.redstone_tab.scene_id_active > 0
                            ? component->u.redstone_tab.scene_id_active
                            : component->u.redstone_tab.scene_id;
        out->atlas_index = component->u.redstone_tab.scene_id_active > 0
                               ? component->u.redstone_tab.atlas_index_active
                               : component->u.redstone_tab.atlas_index;
        if( out->scene_id <= 0 )
            return false;
        return true;

    case UIELEM_BUILTIN_SIDEBAR:
    case UIELEM_BUILTIN_CHAT:
    case UIELEM_BUILTIN_CHAT_BUTTON:
    case UIELEM_BUILTIN_MINIMENU:
    case UIELEM_BUILTIN_HOVERTEXT:
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

static void
emit_minimenu_rect(
    struct UITreeEmitBuffer* out,
    struct UITreeComponent const* c,
    int32_t idx,
    struct UITreeEmitClip const* clip,
    int x,
    int y,
    int w,
    int h,
    int color)
{
    struct UITreeEmitDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_RECT;
    desc.node_index = idx;
    desc.component_id = c->component_id;
    desc.x = x;
    desc.y = y;
    desc.w = w;
    desc.h = h;
    desc.color = color;
    desc.filled = 1;
    desc.clip = *clip;
    emit_buffer_append(out, &desc);
}

static void
emit_minimenu_text(
    struct UITreeEmitBuffer* out,
    struct UITreeComponent const* c,
    int32_t idx,
    struct UITreeEmitClip const* clip,
    int x,
    int y,
    int w,
    int h,
    int font_id,
    int color,
    int shadowed,
    char const* text)
{
    struct UITreeEmitDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_TEXT;
    desc.node_index = idx;
    desc.component_id = c->component_id;
    desc.x = x;
    desc.y = y;
    desc.w = w;
    desc.h = h;
    desc.font_id = font_id;
    desc.color = color;
    desc.text_shadowed = shadowed;
    desc.text = text;
    desc.clip = *clip;
    emit_buffer_append(out, &desc);
}

/*
 * Expand the minimenu node into the reference "Choose Option" popup: body
 * fill, black title bar + border strips, title, then one row per option
 * (hover yellow / white, shadowed) drawn bottom-to-top. Model comes from the
 * host so the ui layer stays leaf (reference Client.drawMinimenu; geometry
 * mirrors v1 runescape.c minimenu steps).
 */
static void
emit_minimenu(
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    struct UITreeComponent const* c,
    int32_t idx,
    struct UITreeEmitClip const* parent_clip)
{
    struct UIMinimenu const* menu = NULL;

    assert(c && c->type == UIELEM_BUILTIN_MINIMENU);
    assert(out && parent_clip);

    if( !host )
        return;
    /* Same host gate EmitFill applies to every other node type. */
    if( !UITree_ComponentShouldEmit(c, host) )
        return;
    {
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_MINIMENU_STATE,
            .u.get_minimenu_state.out = &menu,
        };
        if( !UITree_Host(host, &req) || !menu )
            return;
    }
    if( !menu->visible || menu->option_count <= 0 )
        return;

    int const mx = menu->x;
    int const my = menu->y;
    int const mw = menu->width;
    int const mh = menu->height;
    struct UIMinimenuLayout const* layout = &menu->layout;
    int font_id = c->u.minimenu.font_id > 0 ? c->u.minimenu.font_id : menu->font_id;

    /* Body + title bar + separator / bottom / left / right border strips. */
    emit_minimenu_rect(out, c, idx, parent_clip, mx, my, mw, mh, UITREE_MINIMENU_COLOR_BODY);
    emit_minimenu_rect(
        out, c, idx, parent_clip, mx + 1, my + 1, mw - 2, layout->header_bar_h, 0x000000);
    emit_minimenu_rect(
        out, c, idx, parent_clip, mx + 1, my + layout->separator_y, mw - 2, 1, 0x000000);
    emit_minimenu_rect(out, c, idx, parent_clip, mx + 1, my + mh - 2, mw - 2, 1, 0x000000);
    emit_minimenu_rect(
        out,
        c,
        idx,
        parent_clip,
        mx + 1,
        my + layout->separator_y,
        1,
        mh - layout->border_inset,
        0x000000);
    emit_minimenu_rect(
        out,
        c,
        idx,
        parent_clip,
        mx + mw - 2,
        my + layout->separator_y,
        1,
        mh - layout->border_inset,
        0x000000);

    /* Title baseline sits at y+14 in the reference (drawString x+3,y+14);
     * DrawStringBox places the baseline at box_y + ascent, so the box starts
     * just under the border. */
    emit_minimenu_text(
        out,
        c,
        idx,
        parent_clip,
        mx + 3,
        my + 2,
        mw - 6,
        layout->header_bar_h,
        font_id,
        UITREE_MINIMENU_COLOR_BODY,
        0,
        "Choose Option");

    for( int i = 0; i < menu->option_count; i++ )
    {
        int const row_baseline = UIMinimenu_OptionY(menu, i);
        int const hovered = menu->hovered_option == i;
        emit_minimenu_text(
            out,
            c,
            idx,
            parent_clip,
            mx + 3,
            row_baseline - layout->line_height + 2,
            mw - 6,
            layout->row_stride + 2,
            font_id,
            hovered ? 0xFFFF00 : 0xFFFFFF,
            1,
            menu->options[i].text);
    }
}

/*
 * Expand the mouseover-text node into the single top-left line the reference
 * builds from CS2 (script 4726 -> proc 4727: one cc_create'd TEXT child,
 * font 496, shadow on, colour 0xD8D8D8). Model comes from the host, and the
 * app owns placement, so this is one TEXT desc and nothing else.
 */
static void
emit_hovertext(
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    struct UITreeComponent const* c,
    int32_t idx,
    struct UITreeEmitClip const* parent_clip)
{
    struct UIHoverText const* hover = NULL;

    assert(c && c->type == UIELEM_BUILTIN_HOVERTEXT);
    assert(out && parent_clip);

    if( !host )
        return;
    if( !UITree_ComponentShouldEmit(c, host) )
        return;
    {
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_HOVERTEXT_STATE,
            .u.get_hovertext_state.out = &hover,
        };
        if( !UITree_Host(host, &req) || !hover )
            return;
    }
    if( !hover->visible || hover->text[0] == '\0' )
        return;

    {
        int const font_id = c->u.hovertext.font_id > 0 ? c->u.hovertext.font_id : hover->font_id;
        struct UITreeEmitDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.kind = UITREE_EMIT_TEXT;
        desc.node_index = idx;
        desc.component_id = c->component_id;
        desc.x = hover->x;
        desc.y = hover->y;
        /* Left-aligned single line; the app sizes the box from the viewport. */
        desc.w = hover->w;
        desc.h = hover->h > 0 ? hover->h : UITREE_HOVERTEXT_BOX_H;
        desc.font_id = font_id;
        desc.color = UITREE_HOVERTEXT_COLOR;
        desc.text_shadowed = 1;
        desc.text = hover->text;
        desc.clip = *parent_clip;
        emit_buffer_append(out, &desc);
    }
}

static void
emit_chat_span(
    struct UITreeEmitBuffer* out,
    struct UITreeComponent const* c,
    int32_t idx,
    struct UITreeEmitClip const* clip,
    int x,
    int baseline_y,
    int w,
    int font_id,
    int color,
    int centered,
    char const* text)
{
    struct UITreeEmitDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_TEXT;
    desc.node_index = idx;
    desc.component_id = c->component_id;
    desc.x = x;
    /* Baseline semantics: box top = baseline - line height (p12 ascent 12). */
    desc.y = baseline_y - 12;
    desc.w = w;
    desc.h = 14;
    desc.font_id = font_id;
    desc.color = color;
    desc.text_center = centered;
    desc.text = text;
    desc.clip = *clip;
    emit_buffer_append(out, &desc);
}

/*
 * Expand the chat panel into TEXT descs from the host's flattened draw model
 * (reference drawChat; layout math lives game-side in rs_chat.c). Clips the
 * message area to the region so scrolled lines never bleed into the input
 * line or outside the chatback.
 */
static void
emit_chat(
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    struct UITreeComponent const* c,
    int32_t idx,
    struct UITreeEmitClip const* parent_clip)
{
    struct UIChatView const* view = NULL;
    int x = 0, y = 0, w = 0, h = 0;

    assert(c && c->type == UIELEM_BUILTIN_CHAT);
    assert(out && parent_clip);

    if( !host )
        return;
    {
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_CHAT_STATE,
            .u.get_chat_state.out = &view,
        };
        if( !UITree_Host(host, &req) || !view )
            return;
    }
    if( !view->visible )
        return;

    UITree_LayoutGetBounds(&c->position, &x, &y, &w, &h);

    if( view->centered )
    {
        for( int i = 0; i < view->center_count; i++ )
        {
            struct UIChatViewLine const* line = &view->center_lines[i];
            for( int s = 0; s < line->span_count; s++ )
                emit_chat_span(
                    out,
                    c,
                    idx,
                    parent_clip,
                    x,
                    y + line->baseline_y,
                    w,
                    view->font_id,
                    line->spans[s].color,
                    1,
                    line->spans[s].text);
        }
        return;
    }

    {
        /* Message window clip (reference setClipping(0,0,463,77)). */
        struct UITreeEmitClip msg_clip;
        if( !clip_intersect(&msg_clip, parent_clip, x, y, w < 463 ? w : 463, 77) )
            msg_clip = *parent_clip;
        for( int i = 0; i < view->line_count; i++ )
        {
            struct UIChatViewLine const* line = &view->lines[i];
            for( int s = 0; s < line->span_count; s++ )
                emit_chat_span(
                    out,
                    c,
                    idx,
                    &msg_clip,
                    x + line->spans[s].x,
                    y + line->baseline_y,
                    w - line->spans[s].x,
                    view->font_id,
                    line->spans[s].color,
                    0,
                    line->spans[s].text);
        }
    }

    /* Chat scrollbar, drawn unconditionally like the reference
     * (drawScrollbar(463, 0, chatScrollHeight-chatScrollPos-77, chatScrollHeight,
     * 77), Client.ts:11485). Local x=463 puts it just right of the 463-wide
     * message column; height is the message window (77), not the full chat node.
     * The desc-driven scrollbar_v render (torirs_frame.c) reads scroll_content /
     * scroll_off_y straight from here, so no component backing is needed. */
    {
        struct UITreeEmitDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.kind = UITREE_EMIT_SCROLLBAR_V;
        desc.node_index = idx;
        desc.component_id = c->component_id;
        desc.x = x + 463;
        desc.y = y;
        desc.w = UITREE_SCROLLBAR_THICKNESS;
        desc.h = 77;
        desc.scroll_content = view->scroll_height;
        desc.scroll_off_y = view->scroll_pos;
        desc.scene_id = host_scrollbar_scene(host);
        desc.atlas_index = 0;
        desc.if3 = 0;
        desc.clip = *parent_clip;
        emit_buffer_append(out, &desc);
    }

    if( view->has_input_line )
    {
        struct UIChatViewLine const* line = &view->input_line;
        for( int s = 0; s < line->span_count; s++ )
            emit_chat_span(
                out,
                c,
                idx,
                parent_clip,
                x + line->spans[s].x,
                y + line->baseline_y,
                w - line->spans[s].x,
                view->font_id,
                line->spans[s].color,
                0,
                line->spans[s].text);
        /* Separator above the input line (reference Pix2D.hline(0, 77, 479):
         * spans the 463-wide message column plus the scrollbar, so it runs
         * from the left edge all the way to the scrollbar's right side). */
        {
            struct UITreeEmitDesc desc;
            memset(&desc, 0, sizeof(desc));
            desc.kind = UITREE_EMIT_RECT;
            desc.node_index = idx;
            desc.component_id = c->component_id;
            desc.x = x;
            desc.y = y + 77;
            desc.w = 463 + UITREE_SCROLLBAR_THICKNESS;
            desc.h = 1;
            desc.color = 0x000000;
            desc.filled = 1;
            desc.clip = *parent_clip;
            emit_buffer_append(out, &desc);
        }
    }
}

/*
 * Expand a privacy-bar button into its two lines (reference
 * redrawPrivacySettings: label centered in WHITE, current mode centered in the
 * mode's colour, both shadowed). All geometry, labels, and colours come from
 * the node's INI-decoded config; the live mode comes from the host.
 */
static void
emit_chat_button(
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    struct UITreeComponent const* c,
    int32_t idx,
    struct UITreeEmitClip const* parent_clip)
{
    struct UITreeChatButtonConfig const* cfg;
    int x = 0, y = 0, w = 0, h = 0;
    int mode = 0;

    assert(c && c->type == UIELEM_BUILTIN_CHAT_BUTTON);
    assert(out && parent_clip);

    if( !host )
        return;
    cfg = &c->u.chat_button;
    UITree_LayoutGetBounds(&c->position, &x, &y, &w, &h);

    {
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_CHAT_FILTER_MODE,
            .u.chat_filter.filter = (int)cfg->filter,
        };
        mode = UITree_Host(host, &req);
        if( mode < 0 || mode >= 4 )
            mode = 0;
    }

    if( cfg->label[0] )
    {
        struct UITreeEmitDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.kind = UITREE_EMIT_TEXT;
        desc.node_index = idx;
        desc.component_id = c->component_id;
        desc.x = x;
        desc.y = y + cfg->label_y;
        desc.w = w;
        desc.h = h;
        desc.font_id = cfg->font_id;
        desc.color = 0xFFFFFF;
        desc.text_center = cfg->center;
        desc.text_shadowed = cfg->shadowed;
        desc.text = cfg->label;
        desc.clip = *parent_clip;
        emit_buffer_append(out, &desc);
    }

    if( cfg->mode_label[mode][0] )
    {
        struct UITreeEmitDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.kind = UITREE_EMIT_TEXT;
        desc.node_index = idx;
        desc.component_id = c->component_id;
        desc.x = x;
        desc.y = y + cfg->mode_y;
        desc.w = w;
        desc.h = h;
        desc.font_id = cfg->font_id;
        desc.color = cfg->mode_color[mode];
        desc.text_center = cfg->center;
        desc.text_shadowed = cfg->shadowed;
        desc.text = cfg->mode_label[mode];
        desc.clip = *parent_clip;
        emit_buffer_append(out, &desc);
    }
}

/*
 * Expand TYPE_INV_TEXT into per-slot obj-name lines (reference drawInterface
 * TYPE_INV_TEXT: "name" or "name xN" for stacks, grid stride marginX+115 /
 * marginY+12, y is the text baseline).
 */
static void
emit_rs_inv_text_slots(
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    struct UITreeComponent const* c,
    int32_t idx,
    int x,
    int y,
    int scroll_off_x,
    int scroll_off_y,
    struct UITreeEmitClip const* clip)
{
    int slot = 0;

    assert(c && c->type == UIELEM_RS_INV_TEXT);
    if( !host || c->u.rs_inv_text.inv_source_id < 0 )
        return;

    for( int row = 0; row < c->u.rs_inv_text.rows; row++ )
    {
        for( int col = 0; col < c->u.rs_inv_text.cols; col++, slot++ )
        {
            struct UIInvSlotData data = { 0 };
            char name[64] = { 0 };
            int stackable = 0;
            struct UITreeEmitDesc desc;

            {
                struct UITreeHostRequest req = {
                    .kind = UITREE_HOST_GET_INV_SOURCE_SLOT,
                    .u.get_inv_source_slot.source_id = c->u.rs_inv_text.inv_source_id,
                    .u.get_inv_source_slot.slot = slot,
                    .u.get_inv_source_slot.out = &data,
                };
                if( !UITree_Host(host, &req) || data.obj_id <= 0 )
                    continue;
            }
            {
                struct UITreeHostRequest req = {
                    .kind = UITREE_HOST_GET_OBJ_NAME,
                    .u.get_obj_name.obj_id = data.obj_id,
                    .u.get_obj_name.out = name,
                    .u.get_obj_name.cap = (int)sizeof(name),
                    .u.get_obj_name.out_stackable = &stackable,
                };
                if( !UITree_Host(host, &req) || !name[0] )
                    continue;
            }

            memset(&desc, 0, sizeof(desc));
            desc.kind = UITREE_EMIT_TEXT;
            desc.node_index = idx;
            desc.component_id = c->component_id;
            desc.x = x + col * (c->u.rs_inv_text.margin_x + 115) - scroll_off_x;
            /* Reference y is the baseline; our text box top sits one line up. */
            desc.y = y + row * (c->u.rs_inv_text.margin_y + 12) - 12 - scroll_off_y;
            desc.w = 115;
            desc.h = 14;
            desc.font_id = c->u.rs_inv_text.font_id;
            desc.color = c->u.rs_inv_text.color;
            desc.text_center = c->u.rs_inv_text.center;
            desc.text_shadowed = c->u.rs_inv_text.shadowed;
            if( stackable || data.obj_count != 1 )
                snprintf(
                    desc.text_formatted,
                    sizeof(desc.text_formatted),
                    "%s x%d",
                    name,
                    data.obj_count);
            else
                snprintf(desc.text_formatted, sizeof(desc.text_formatted), "%s", name);
            desc.text = ""; /* renderer uses text_formatted when set */
            desc.clip = *clip;
            emit_buffer_append(out, &desc);
        }
    }
}

/** Stack-count formatting (reference invNumber, Client.ts:10502): raw below
 * 100K, then "<n/1000>K" below 10M, then "<n/1000000>M". */
static void
uitree_emit_inv_number(int amount, char* buf, size_t cap)
{
    if( amount < 100000 )
        snprintf(buf, cap, "%d", amount);
    else if( amount < 10000000 )
        snprintf(buf, cap, "%dK", amount / 1000);
    else
        snprintf(buf, cap, "%dM", amount / 1000000);
}

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
    int slot_drag_source = -1;
    int slot_drag_slot = -1;
    int slot_drag_dx = 0;
    int slot_drag_dy = 0;
    int count_font_id = -1;

    assert(c && c->type == UIELEM_RS_INV);
    assert(out && parent_clip);

    if( host )
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_INV_COUNT_FONT };
        count_font_id = UITree_Host(host, &req);
    }

    layout.cols = c->u.rs_inv.cols;
    layout.rows = c->u.rs_inv.rows;
    layout.margin_x = c->u.rs_inv.margin_x;
    layout.margin_y = c->u.rs_inv.margin_y;
    layout.offset_x = c->u.rs_inv.inv_slot_offset_x;
    layout.offset_y = c->u.rs_inv.inv_slot_offset_y;
    slot_limit = UITree_InvViewGridSlotLimit(&layout);

    /* Armed slot press/drag (reference TYPE_INV draw: only the objDragSlot
     * icon renders at the mouse delta with trans 128; the host has already
     * applied the +-5px deadzone and the 5-cycle gate to dx/dy). */
    if( host )
    {
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_INV_DRAG,
            .u.get_inv_drag.out_source_id = &slot_drag_source,
            .u.get_inv_drag.out_slot = &slot_drag_slot,
            .u.get_inv_drag.out_dx = &slot_drag_dx,
            .u.get_inv_drag.out_dy = &slot_drag_dy,
        };
        if( !UITree_Host(host, &req) )
            slot_drag_slot = -1;
    }

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
        /* Armed slot: this one icon follows the mouse semi-transparently
         * (reference transPlotSprite(slotX+dx, slotY+dy, 128)); every other
         * slot stays put. Background fallbacks (obj_id 0) are never armed. */
        if( obj_id > 0 && slot == slot_drag_slot &&
            slot_drag_source == c->u.rs_inv.inv_source_id )
        {
            desc.x += slot_drag_dx;
            desc.y += slot_drag_dy;
            desc.trans = 128;
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

        /* Stack count (reference: p11 yellow with a black drop shadow at
         * (slotX+dx, slotY+9), drawn when the icon is stackable (owi==33) or
         * the count isn't 1). desc.x/y already carry every offset the icon
         * got — scroll, whole-node drag, armed-slot drag — so the number
         * rides along. Shadow is the font's +1/+1 pass (identical to the
         * reference's black-then-yellow pair). */
        if( obj_id > 0 && count_font_id >= 0 )
        {
            int stackable = 0;
            char namebuf[4] = { 0 };
            struct UITreeHostRequest req = {
                .kind = UITREE_HOST_GET_OBJ_NAME,
                .u.get_obj_name.obj_id = obj_id,
                .u.get_obj_name.out = namebuf,
                .u.get_obj_name.cap = (int)sizeof(namebuf),
                .u.get_obj_name.out_stackable = &stackable,
            };
            UITree_Host(host, &req);
            if( stackable || obj_count != 1 )
            {
                struct UITreeEmitDesc count_desc;
                memset(&count_desc, 0, sizeof(count_desc));
                count_desc.kind = UITREE_EMIT_TEXT;
                count_desc.node_index = idx;
                count_desc.component_id = c->component_id;
                count_desc.x = desc.x;
                /* Reference y is the p11 baseline at slotY+9; our text box
                 * top sits one line up (same convention as TYPE_INV_TEXT). */
                count_desc.y = desc.y + 9 - 12;
                count_desc.w = slot_w;
                count_desc.h = 14;
                count_desc.font_id = count_font_id;
                count_desc.color = 0xFFFF00;
                count_desc.text_shadowed = 1;
                uitree_emit_inv_number(
                    obj_count, count_desc.text_formatted,
                    sizeof(count_desc.text_formatted));
                count_desc.text = "";
                count_desc.scroll_off_x = scroll_off_x;
                count_desc.scroll_off_y = scroll_off_y;
                count_desc.clip = *parent_clip;
                emit_buffer_append(out, &count_desc);
            }
        }
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
    struct UITreeEmitClip const* surface_clip,
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
    struct UITreeEmitClip layer_surface;
    struct UITreeEmitClip const* child_clip;
    struct UITreeEmitClip const* child_surface;
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
                    surface_clip,
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
    child_surface = surface_clip;
    {
        /* Shared interface layer child-clip rule (UITree_LayerChildClip): clip to
         * own bounds ∩ the enclosing surface, never compounded with ancestor
         * layers — same helper the hit/hover/drop walks use, so drawn pixels and
         * hitboxes agree. UITreeEmitClip and UITreeScrollClip are the same rect;
         * convert at this one seam. */
        int clip_x = x - scroll_off_x + (in_drag ? drag_dx : 0);
        int clip_y = y - scroll_off_y + (in_drag ? drag_dy : 0);
        struct UITreeScrollClip surf = { surface_clip->x, surface_clip->y, surface_clip->w,
                                         surface_clip->h };
        struct UITreeScrollClip cc, cs;
        if( UITree_LayerChildClip(c, &surf, clip_x, clip_y, w, h, &cc, &cs) )
        {
            layer_clip = (struct UITreeEmitClip){ cc.clip_x, cc.clip_y, cc.clip_w, cc.clip_h };
            layer_surface = (struct UITreeEmitClip){ cs.clip_x, cs.clip_y, cs.clip_w, cs.clip_h };
            child_clip = &layer_clip;
            child_surface = &layer_surface;
        }
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
    else if( !if1_bar && c->type == UIELEM_BUILTIN_MINIMENU )
    {
        /* Screen-anchored popup chrome: multi-desc expansion, never scrolled
         * or dragged (same shape as the RS_INV slot expansion above). */
        emit_minimenu(host, out, c, idx, parent_clip);
    }
    else if( !if1_bar && c->type == UIELEM_BUILTIN_HOVERTEXT )
    {
        emit_hovertext(host, out, c, idx, parent_clip);
    }
    else if( !if1_bar && c->type == UIELEM_BUILTIN_CHAT_BUTTON )
    {
        /* Fixed chrome: multi-desc expansion, never scrolled or dragged. */
        emit_chat_button(host, out, c, idx, parent_clip);
    }
    else if( !if1_bar && c->type == UIELEM_BUILTIN_CHAT )
    {
        emit_chat(host, out, c, idx, parent_clip);
    }
    else if( !if1_bar && c->type == UIELEM_RS_INV_TEXT )
    {
        emit_rs_inv_text_slots(
            host, out, c, idx, x, y, scroll_off_x, scroll_off_y, parent_clip);
    }
    else if( !if1_bar && UITree_EmitFill(tree, host, c, idx, hovered_component_id, &desc) )
    {
        /* World/minimap/compass are screen-anchored chrome: they still emit,
         * but never take the scroll/drag translation. */
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
        }
        desc.scroll_off_x = scroll_off_x;
        desc.scroll_off_y = scroll_off_y;
        desc.clip = *parent_clip;
        /* TORIRS_MODEL_CLIP_DEBUG: model widget box vs. the clip it will be
         * scissored to — the model overflows its box and is only bounded by
         * this clip (the enclosing interface layer ∩ surface). A clip narrower
         * than the widget's own right edge is what crops a chathead. */
        if( desc.kind == UITREE_EMIT_MODEL && getenv("TORIRS_MODEL_CLIP_DEBUG") )
            fprintf(
                stderr,
                "model com=0x%08x box=%d,%d %dx%d (right=%d) clip=%d,%d %dx%d (right=%d)\n",
                desc.component_id, desc.x, desc.y, desc.w, desc.h, desc.x + desc.w,
                desc.clip.x, desc.clip.y, desc.clip.w, desc.clip.h,
                desc.clip.x + desc.clip.w);
        emit_buffer_append(out, &desc);
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
                child_surface,
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
