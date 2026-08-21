#include "uitree_emit.h"

#include "perf/torirs_perf.h"
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

/*
 * Height of the obj-icon raster, and the reference an obj drawn as a MODEL is
 * scaled against. The rasteriser's canvas is 36x32 (bridge_rasterize_obj_icon,
 * ItemIconRenderer.OSRS_SPRITE_W/H); the height is the one that matters here
 * because a cell is scaled by its smaller side.
 */
#define UITREE_OBJ_ICON_RASTER_H 32

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
    assert(host);
    req.u.static_sprite.slot = (int)slot;
    scene_id = UITree_Host(host, &req);
    return scene_id > 0 ? scene_id : -1;
}

static bool
layer_needs_scroll_offset(struct UITreeComponent const* c)
{
    assert(c);
    if( c->type != UIELEM_RS_LAYER )
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
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_EMIT_SKIP, 1);
            return false;
        }
    }

    /* Fully transparent: skip self content; children still walked by emit_walk_node. */
    if( component->trans >= 255 )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_EMIT_SKIP, 1);
        return false;
    }

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
             * draw-time centering (widgets-gl type-5 itemId path).
             *
             * Icon flavour selection (not stacked passes):
             * - outline==0 && graphic_shadow==0 (inventory/bank): use the
             *   SHADOW-baked item_scene_id and keep both fields at 0 — the
             *   drop shadow is already in the pixels.
             * - outline==1 && graphic_shadow==0 (collection log / bank cells):
             *   use a pre-baked black-border sprite so Soft3D does not call
             *   SpriteNewGraphicOutline every frame for each unique icon.
             * - graphic_shadow != 0 (skill-guide rows): swap to the plain bake
             *   and forward outline/shadow for Soft3D post-process once.
             *   Stacking on a SHADOW bake doubles the shadow at +2px. */
            if( component->item_id > 0 && component->item_scene_id > 0 )
            {
                int outline = component->u.rs_graphic.outline;
                int graphic_shadow = component->u.rs_graphic.graphic_shadow;
                out->tiled = 0;
                out->flip_h = component->u.rs_graphic.flip_h;
                out->flip_v = component->u.rs_graphic.flip_v;
                if( outline == 0 && graphic_shadow == 0 )
                {
                    out->scene_id = component->item_scene_id;
                    out->atlas_index = component->item_atlas_index;
                    out->outline = 0;
                    out->graphic_shadow = 0;
                }
                else if( outline == 1 && graphic_shadow == 0 )
                {
                    int bordered = -1;
                    if( host )
                    {
                        struct UITreeHostRequest req = {
                            .kind = UITREE_HOST_GET_OBJ_ICON_BORDERED,
                            .u.get_obj_icon_bordered.obj_id = component->item_id,
                            .u.get_obj_icon_bordered.count =
                                component->item_count > 0 ? component->item_count : 1,
                        };
                        bordered = UITree_Host(host, &req);
                    }
                    if( bordered > 0 )
                    {
                        out->scene_id = bordered;
                        out->atlas_index = 0;
                        out->outline = 0;
                        out->graphic_shadow = 0;
                    }
                    else
                    {
                        /* Model still loading — fall back to plain + Soft3D. */
                        int plain = -1;
                        if( host )
                        {
                            struct UITreeHostRequest req = {
                                .kind = UITREE_HOST_GET_OBJ_ICON_PLAIN,
                                .u.get_obj_icon_plain.obj_id = component->item_id,
                                .u.get_obj_icon_plain.count =
                                    component->item_count > 0 ? component->item_count : 1,
                            };
                            plain = UITree_Host(host, &req);
                        }
                        out->scene_id = plain > 0 ? plain : component->item_scene_id;
                        out->atlas_index = plain > 0 ? 0 : component->item_atlas_index;
                        out->outline = outline;
                        out->graphic_shadow = 0;
                    }
                }
                else
                {
                    int plain = -1;
                    if( host )
                    {
                        struct UITreeHostRequest req = {
                            .kind = UITREE_HOST_GET_OBJ_ICON_PLAIN,
                            .u.get_obj_icon_plain.obj_id = component->item_id,
                            .u.get_obj_icon_plain.count =
                                component->item_count > 0 ? component->item_count : 1,
                        };
                        plain = UITree_Host(host, &req);
                    }
                    out->scene_id = plain > 0 ? plain : component->item_scene_id;
                    out->atlas_index = plain > 0 ? 0 : component->item_atlas_index;
                    out->outline = outline;
                    out->graphic_shadow = graphic_shadow;
                }
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
            /* Independent of which graphic variant was chosen above — the
             * angle is a property of the component, not of the sprite. */
            out->sprite_angle_r2pi65536 = component->u.rs_graphic.sprite_angle_r2pi65536;
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
    {
        /* Reference getTempModel(active): the active variant replaces the base
         * model outright, and a widget whose selected variant is "no model"
         * draws nothing (getModel returns null). The 254 special-attack bar is
         * built entirely out of that second half — ten dark cover segments sit
         * over a green bar, each with an if1script comparing varp 300 (spec
         * energy) against its threshold and no active model, so a cover
         * vanishes once the energy passes it. */
        int model_id = active ? component->u.rs_model.active_model_id
                              : component->u.rs_model.gamecache_model_id;

        /* clientCode 328 = local player preview; cache often has modelId=-1. */
        if( model_id < 0 )
        {
            if( !active && component->behavior.client_code == 328 )
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
        out->model_id = model_id;
        out->model_zoom = component->u.rs_model.zoom;
        /*
         * An OBJ bound to a MODEL widget fills the widget; an obj icon fills a
         * 36x32 raster. Both go through the same projection, whose focal
         * length is the fixed WIDGET_MODEL_ZOOM3D of 512 — so the drawn size
         * depends only on the camera distance `zoom`, and a bigger box just
         * adds margin around an icon-sized model rather than showing a bigger
         * one. Scaling the distance by the box restores the proportion the
         * objtype's own `zoom2d` was authored for.
         *
         * `skillmulti` is where this shows: its cells are 65px and up (they
         * grow as the product count falls), so an arrow shaft drawn at icon
         * distance was a thin line in the middle of a large empty button.
         *
         * Gated on `item_id` because only the CS2 CC_SETOBJECT path sets it.
         * The server IF_SETOBJECT path (`App_SetInterfaceObjModel`, the
         * combat tab's wielded weapon) binds the same kind of model but
         * carries its own wire zoom and never sets `item_id`, so it keeps the
         * distance it asked for.
         */
        if( component->item_id > 0 )
        {
            int const box = w < h ? w : h;

            if( box > 0 )
                out->model_zoom = component->u.rs_model.zoom * UITREE_OBJ_ICON_RASTER_H / box;
            out->model_obj_composed = 1;
        }
        out->model_xan = component->u.rs_model.xan;
        out->model_yan = component->u.rs_model.yan;
        out->model_zan = component->u.rs_model.zan;
        out->model_x_offset = component->u.rs_model.x_offset;
        out->model_y_offset = component->u.rs_model.y_offset;
        out->model_orthog = component->u.rs_model.orthog;
        out->model_fixed_zoom = component->u.rs_model.fixed_zoom;
        return true;
    }

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
        out->world_mmb_rotate = component->u.world.mmb_rotate;
        out->world_wheel_zoom = component->u.world.wheel_zoom;
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
        /* That mask placeholder clips the over-filled map to its round window.
         * Which side of the mask is the window is era art, not a widget
         * property — see UITree.mask_keep_opaque. */
        out->mask_scene_id = component->u.minimap.mask_scene_id;
        out->mask_atlas_index = component->u.minimap.mask_atlas_index;
        out->mask_keep_opaque = tree->mask_keep_opaque;
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

    case UIELEM_BUILTIN_WORLDMAP:
    {
        /* The host decides which regions are visible from the widget box plus
         * its own pan/zoom, bakes what it needs, and hands back positioned
         * blits. A zero count still emits: the background fill is the map
         * surface until the regions load. */
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_WORLDMAP_TILES,
            .u.get_worldmap_tiles.out_items = &out->worldmap_tiles,
            .u.get_worldmap_tiles.box_x = out->x,
            .u.get_worldmap_tiles.box_y = out->y,
            .u.get_worldmap_tiles.box_w = out->w,
            .u.get_worldmap_tiles.box_h = out->h,
            .u.get_worldmap_tiles.out_background_rgb = &out->color,
        };
        out->kind = UITREE_EMIT_WORLDMAP;
        out->worldmap_tile_count = UITree_Host(host, &req);
        out->filled = 1;
        return true;
    }

    case UIELEM_BUILTIN_WORLDMAP_OVERVIEW:
    {
        /* Same emit kind as the main map: step 0 is the background fill, then
         * one scaled compositetexture blit. Red viewport rects are CS2 on the
         * sibling overview_overlay layer. */
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_WORLDMAP_OVERVIEW,
            .u.get_worldmap_overview.out_items = &out->worldmap_tiles,
            .u.get_worldmap_overview.box_x = out->x,
            .u.get_worldmap_overview.box_y = out->y,
            .u.get_worldmap_overview.box_w = out->w,
            .u.get_worldmap_overview.box_h = out->h,
            .u.get_worldmap_overview.out_background_rgb = &out->color,
        };
        out->kind = UITREE_EMIT_WORLDMAP;
        out->worldmap_tile_count = UITree_Host(host, &req);
        out->filled = 1;
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
        out->mask_keep_opaque = tree->mask_keep_opaque;
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
        /* RevConfig's sprite= binding owns the revision-specific pack. Keep
         * the static-sprite lookup as compatibility for a configured cross
         * node whose old profile omitted sprite=. */
        out->scene_id = component->u.sprite.scene_id;
        if( out->scene_id <= 0 )
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
    /* Emitted by emit_debug_overlay_pass after every walk pass, so that it
     * lands above the drag ghosts too. Nothing for the tree walk to do. */
    case UIELEM_BUILTIN_DEBUG_OVERLAY:
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

/*
 * Stack-count formatting, colour included.
 *
 * The band arithmetic is the 254 client's `invNumber` (Client.ts:10502) — raw
 * below 100K, "<n/1000>K" below 10M, "<n/1000000>M" above — and it did not
 * change. What this used to miss is that the modern client does not draw the
 * three bands in one colour: `Inv.formatObjAmount` (rt4 `Inv.java:264`) wraps
 * each band in its own tag,
 *
 *     < 100K   <col=ffff00>  yellow
 *     < 10M    <col=ffffff>  white
 *     >= 10M   <col=00ff80>  green
 *
 * and hands the tagged string to `font.renderLeft(s, 0, 9, 16776960, 1)` — the
 * yellow it passes is only the fallback for a string that carries no tag.
 * Emitting the tag rather than resolving the colour here is the same choice
 * REV230_UI_BLANK_PANELS §9 forced on `parawidth`: the renderer owns what a
 * `<col=…>` means, and a second implementation of that drifts. Both back ends
 * tokenise it (`font_draw_string_range`, `ToriDraw_FontVisitGlyphsStyled`) and
 * both draw the shadow pass in black regardless of the tag, matching the
 * reference's black-then-colour pair.
 */
static void
uitree_emit_inv_number(int amount, char* buf, size_t cap)
{
    if( amount < 100000 )
        snprintf(buf, cap, "<col=ffff00>%d</col>", amount);
    else if( amount < 10000000 )
        snprintf(buf, cap, "<col=ffffff>%dK</col>", amount / 1000);
    else
        snprintf(buf, cap, "<col=00ff80>%dM</col>", amount / 1000000);
}

/*
 * Is this node the item cell armed for "Use"?
 *
 * Two spellings, because a rev-230 cell is addressed the way the protocol
 * addresses it: `app_obj_cell_at` resolves a backpack cell to its static
 * PARENT's uid (149:0) plus the child's index, not to the runtime child's own
 * id — the same (container, sub) pair IF_BUTTON carries. So a cell matches
 * either directly (it IS the armed component) or as child `slot` of it. This
 * is the identical test the drag path in emit_walk_node makes, and testing
 * only the first form is why that one changed nothing on its first attempt.
 */
static int
uitree_emit_node_is_armed_cell(
    struct UITree const* tree,
    struct UITreeComponent const* c,
    int armed_component,
    int armed_slot)
{
    if( armed_component < 0 )
        return 0;
    if( c->component_id == armed_component )
        return 1;
    if( !c->dynamic || c->dynamic_child_index != armed_slot )
        return 0;
    if( c->parent < 0 || (uint32_t)c->parent >= tree->component_count )
        return 0;
    return tree->components[c->parent].component_id == armed_component;
}

/*
 * Swap a cell's icon for its white-outlined variant while it is armed for "Use".
 *
 * The rev-230 cell is a CS2 `cc_create`d type-5 graphic carrying the obj, drawn
 * by the generic path — and the generic path never asked. The swap existed only
 * in `emit_rs_inv_slots`, the TYPE_INV grid, which rev 230 does not have: the
 * same shape of bug as the missing stack counts (REV230_UI_BLANK_PANELS §4),
 * one draw feature implemented against the era's dead widget.
 *
 * Reference: the item sprite is baked with an outline *state* —
 * `Inv.renderObjectSprite` draws a value-1 edge at state >= 1 and a white ring
 * on top at state >= 2 (rt4 `Inv.java:231`) — and the IF3 draw asks for
 * `max(2, borderType)` on the selected cell against `borderType` on every other
 * (xrsps `widgets-gl.ts:4180`; the bank's own onload sets borderType 1 with
 * `cc_setoutline(1)`). Here the two states are two baked scene ids, so "raise
 * the state" is "ask the host for the other id"; the host answers > 0 for the
 * one armed (component, slot) and 0 for everything else, so nothing else can
 * light up.
 */
static void
emit_obj_selected_icon(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeComponent const* c,
    struct UITreeEmitDesc* desc)
{
    int armed_component = -1;
    int armed_slot = -1;
    int obj_id = c->item_id;
    int obj_count;
    int outline_scene;

    if( obj_id <= 0 )
        return;
    assert(host);
    assert(desc);
    /* Same two kinds emit_obj_stack_count accepts: a node with a stale item_id
     * under a plain SETGRAPHIC sprite is not an item cell. */
    if( desc->kind != UITREE_EMIT_CC_OBJ &&
        !(desc->kind == UITREE_EMIT_SPRITE && c->item_scene_id > 0) )
        return;

    {
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_INV_SELECTION,
            .u.get_inv_selection.out_component_id = &armed_component,
            .u.get_inv_selection.out_slot = &armed_slot,
        };
        if( !UITree_Host(host, &req) )
            return;
    }
    if( !uitree_emit_node_is_armed_cell(tree, c, armed_component, armed_slot) )
        return;

    obj_count = c->item_count > 0 ? c->item_count : 1;
    {
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_INV_SELECT_ICON,
            .u.get_inv_select_icon.com_id = armed_component,
            .u.get_inv_select_icon.slot = armed_slot,
            .u.get_inv_select_icon.obj_id = obj_id,
            .u.get_inv_select_icon.count = obj_count,
        };
        outline_scene = UITree_Host(host, &req);
    }
    if( outline_scene <= 0 )
        return;
    desc->scene_id = outline_scene;
    desc->atlas_index = 0;
}

/*
 * The stack count over a single item widget.
 *
 * The TYPE_INV grid draws its own counts per slot (below), but rev 230 has no
 * TYPE_INV inventory: the gameframe's CS2 `cc_create`s one widget per slot and
 * hangs the obj on it with SETOBJECT, so the count has to be drawn here or
 * nowhere. It was nowhere — every stack in the backpack, the bank and the shop
 * drew its icon with no number on it.
 *
 * `icon` is the desc just appended for the item, so the number inherits every
 * offset the icon took (scroll, drag, ghosting) by construction. Same
 * conventions as the grid path: p11 with a drop shadow, colour from the tag
 * `uitree_emit_inv_number` writes, and the baseline at `iconY + 9`.
 *
 * That 9 is not a nudge, and the number has to be *inside* the icon's box for
 * the same reason the reference never has to think about it: over there the
 * count is rasterised INTO the 36x32 item sprite (`Inv.renderObjectSprite` ->
 * `font.renderLeft(text, 0, 9, …)`), so it is clipped by exactly whatever
 * clips the icon and by nothing else. Here it is a sibling draw sharing the
 * icon's clip rect, which is equivalent only while it stays inside the icon's
 * own rows. It did not: the y was written as `iconY + 9 - 12`, "one line above
 * the baseline", against a renderer whose box path puts the *glyph top* at the
 * box y — so the digits drew three rows above the icon, and every stack in the
 * bank's top row lost its top third to the scroll viewport's clip. Asking for
 * baseline semantics (`text_baseline`) states the reference's number directly
 * and takes the ascent guess out of it.
 */
static void
emit_obj_stack_count(
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    struct UITreeComponent const* c,
    int32_t idx,
    struct UITreeEmitDesc const* icon,
    struct UITreeEmitClip const* parent_clip)
{
    int obj_id = c->item_id;
    int obj_count;
    int font_id;
    int stackable = 0;
    char namebuf[4] = { 0 };
    struct UITreeEmitDesc count_desc;

    if( !host || obj_id <= 0 )
        return;
    /* Only the two kinds that actually carry an obj icon; a plain SETGRAPHIC
     * sprite that happens to sit on a node with a stale item_id must not
     * sprout a number. */
    if( icon->kind != UITREE_EMIT_CC_OBJ &&
        !(icon->kind == UITREE_EMIT_SPRITE && c->item_scene_id > 0) )
        return;
    /* Two "never a number" signals: the _NONUM opcode variant (mode 2), and a
     * negative count on any variant — cc_setobject($obj, -1) is how the spell
     * tooltip asks for a bare rune icon (the script draws its own have/need
     * text beside it). Reference: the item-sprite cache key forces the no-num
     * mode whenever qty == -1. Zero is NOT one of these signals — an
     * out-of-stock shop slot legitimately carries item_id >= 0 with count 0
     * and must print "0", so it falls through to the normal draw below rather
     * than being floored to 1 (that floor used to run unconditionally here
     * and made every 0-stock shop line read "1"). */
    if( c->item_num_mode == 2 || c->item_count < 0 )
        return;
    obj_count = c->item_count;

    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_INV_COUNT_FONT };
        font_id = UITree_Host(host, &req);
    }
    if( font_id < 0 )
        return;
    {
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_OBJ_NAME,
            .u.get_obj_name.obj_id = obj_id,
            .u.get_obj_name.out = namebuf,
            .u.get_obj_name.cap = (int)sizeof(namebuf),
            .u.get_obj_name.out_stackable = &stackable,
        };
        UITree_Host(host, &req);
    }
    /* _ALWAYS_NUM (mode 1) numbers even a lone unstackable; the plain opcode
     * numbers stacks only. */
    if( c->item_num_mode != 1 && !stackable && obj_count == 1 )
        return;

    memset(&count_desc, 0, sizeof(count_desc));
    count_desc.kind = UITREE_EMIT_TEXT;
    count_desc.node_index = idx;
    count_desc.component_id = c->component_id;
    count_desc.x = icon->x;
    count_desc.y = icon->y + 9;
    count_desc.text_baseline = 1;
    count_desc.w = icon->w > 0 ? icon->w : 36;
    count_desc.h = 14;
    count_desc.font_id = font_id;
    /* Fallback only: every string uitree_emit_inv_number writes carries its own
     * <col=…>. Same value the reference passes (16776960). */
    count_desc.color = 0xFFFF00;
    count_desc.text_shadowed = 1;
    count_desc.trans = icon->trans;
    uitree_emit_inv_number(obj_count, count_desc.text_formatted, sizeof(count_desc.text_formatted));
    count_desc.text = "";
    count_desc.scroll_off_x = icon->scroll_off_x;
    count_desc.scroll_off_y = icon->scroll_off_y;
    count_desc.clip = *parent_clip;
    emit_buffer_append(out, &count_desc);
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
            /* Selected for "Use" (reference outline = 0xFFFFFF): swap the plain
             * icon for the white-outlined variant. The host answers >0 only for
             * the one armed (component, slot); every other slot keeps its icon. */
            if( host )
            {
                struct UITreeHostRequest sel_req = {
                    .kind = UITREE_HOST_GET_INV_SELECT_ICON,
                    .u.get_inv_select_icon.com_id = c->component_id,
                    .u.get_inv_select_icon.slot = slot,
                    .u.get_inv_select_icon.obj_id = obj_id,
                    .u.get_inv_select_icon.count = obj_count,
                };
                int outline_scene = UITree_Host(host, &sel_req);
                if( outline_scene > 0 )
                {
                    scene_id = outline_scene;
                    atlas_index = 0;
                }
            }
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

        /* Stack count (reference: p11 with a black drop shadow, baseline at
         * (slotX+dx, slotY+9), drawn when the icon is stackable (owi==33) or
         * the count isn't 1). desc.x/y already carry every offset the icon
         * got — scroll, whole-node drag, armed-slot drag — so the number
         * rides along. Shadow is the font's +1/+1 pass (identical to the
         * reference's black-then-colour pair). Baseline semantics and the
         * per-band colour tag are shared with the CS2 item path above; see
         * emit_obj_stack_count for why neither is a box measurement. */
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
                count_desc.y = desc.y + 9;
                count_desc.text_baseline = 1;
                count_desc.w = slot_w;
                count_desc.h = 14;
                count_desc.font_id = count_font_id;
                count_desc.color = 0xFFFF00; /* fallback; the string is tagged */
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
    return UITree_ChildMountType(tree, container_uid, child) >= 0;
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

    if( drag_pass )
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_WALK_EMIT_DRAG, 1);
    else
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_WALK_EMIT, 1);

    c = &tree->components[idx];
    /* Hide-gated layers stay invisible unless their component_id is hovered. */
    if( c->behavior.hide && !UITree_ComponentVisibleById(c, hovered_component_id) )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_EMIT_SKIP, 1);
        return;
    }

    /* Inactive sidebar tabs prune their whole mounted subtree (same gate as
     * UITree_ComponentVisibleHost; ShouldEmit only skips the container's own
     * draw, not its children). */
    if( c->type == UIELEM_BUILTIN_SIDEBAR && host )
    {
        struct UITreeHostRequest tab_req = { .kind = UITREE_HOST_GET_SELECTED_TAB };
        if( UITree_Host(host, &tab_req) != c->u.sidebar.tabno )
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_EMIT_SKIP, 1);
            return;
        }
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
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_EMIT_SKIP, 1);
            return;
        }
    }
    else if( drag_pass )
    {
        /* On the drag pass, non-deferred nodes only descend to reach any
         * deferred drag source deeper in the tree; they do not draw here.
         * Same mount-last sweep as the draw path so both agree on order. */
        int const has_mounts = UITree_ContainerHasMounts(tree, c->component_id);
        for( int mount_sweep = 0; mount_sweep <= has_mounts; mount_sweep++ )
        {
            for( child = c->first_child; child >= 0;
                 child = tree->components[child].next_sibling )
            {
                if( has_mounts &&
                    child_is_interface_parent_mount(
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
        /* Collapsed clipping layer: its children are clipped away entirely.
         * Returning here also skips this node's own draw, which costs nothing —
         * the types that clip (RS_LAYER, sidebar, chat, inv grid) paint no
         * content of their own. */
        if( UITree_LayerCullsChildren(c, w, h) )
        {
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_EMIT_SKIP, 1);
            return;
        }
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
            /*
             * A dragged CS2 item cell follows the mouse at trans 128, the same
             * way an armed TYPE_INV grid slot does.
             *
             * The grid path (emit_rs_inv_slots) has done this for a long time
             * and this one had not, so at rev 230 — where the backpack is
             * `cc_create`d cells and there is no grid node at all — a drag
             * showed nothing at all until the release, and then the item
             * appeared in its new slot. That is the whole of "drag and drop
             * just snaps to the destination and doesn't render the dimmed
             * object": the *machine* was working (the swap is correct, the
             * deadzone and dead-time are applied, the packet goes out), only
             * its feedback was missing.
             *
             * Matched on component id ALONE — not on the draw kind. A rev-230
             * item cell emits as an ordinary SPRITE (the obj icon is baked into
             * a scene atlas), not as UITREE_EMIT_CC_OBJ, so gating on the kind
             * looks right and matches nothing. What makes a node the dragged
             * one is that the drag machine armed it, which is a question about
             * identity and not about how it draws.
             */
            if( host && c->component_id >= 0 )
            {
                int armed_component = -1;
                int armed_slot = -1;
                int armed_dx = 0;
                int armed_dy = 0;
                struct UITreeHostRequest drag_req = {
                    .kind = UITREE_HOST_GET_INV_DRAG,
                    .u.get_inv_drag.out_slot = &armed_slot,
                    .u.get_inv_drag.out_dx = &armed_dx,
                    .u.get_inv_drag.out_dy = &armed_dy,
                    .u.get_inv_drag.out_component_id = &armed_component,
                };

                if( UITree_Host(host, &drag_req) && armed_component >= 0 )
                {
                    /*
                     * The armed cell is named the way every dynamic child is
                     * named: (container, index within it). `app_obj_cell_at`
                     * resolves a rev-230 backpack cell to `149:0` plus a slot,
                     * not to the cell's own runtime component id — the same
                     * (container, sub) addressing IF_BUTTON uses.
                     *
                     * Both forms are accepted because both exist: a cell that
                     * IS its own component matches directly, and one that is a
                     * child of the armed container matches on its index. Testing
                     * only the first is why the first version of this changed
                     * nothing at all.
                     */
                    int is_armed = armed_component == c->component_id;

                    if( !is_armed && c->dynamic && c->dynamic_child_index == armed_slot &&
                        c->parent >= 0 && (uint32_t)c->parent < tree->component_count )
                        is_armed = tree->components[c->parent].component_id == armed_component;

                    if( is_armed )
                    {
                        desc.x += armed_dx;
                        desc.y += armed_dy;
                        desc.trans = 128;
                    }
                }
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
        if( desc.kind == UITREE_EMIT_ENTITY_OVERLAY )
        {
            /* EmitFill already put the world viewport box (reported by the
             * host) in desc.clip. Intersect it with the enclosing clip instead
             * of clobbering it, so overhead chat, prayer/skull headicons,
             * health bars and hitsplats stay inside the scene viewport and
             * never paint over the sidebar or chatbox — the overlay node is a
             * late root sibling, so *parent_clip is the whole canvas (reference
             * drawEntities runs inside the scene pass, clipped to the game
             * viewport). */
            struct UITreeEmitClip world_box = desc.clip;
            clip_intersect(
                &desc.clip, parent_clip, world_box.x, world_box.y, world_box.w, world_box.h);
        }
        else
        {
            desc.clip = *parent_clip;
        }
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
        emit_obj_selected_icon(tree, host, c, &desc);
        emit_buffer_append(out, &desc);
        emit_obj_stack_count(host, out, c, idx, &desc, parent_clip);
    }

    /* Sweep 0 draws the container's own children, sweep 1 the InterfaceParent
     * mounts — reference widgets-gl renders mounted interface roots LAST, on top
     * of the container's own children. Mounts are ordinary children here
     * (task_cs2_run reparents the pack root under the container) and
     * link_under_parent appends, so without this they only stay on top until the
     * container gains another child. */
    int const has_mounts = UITree_ContainerHasMounts(tree, c->component_id);
    for( int mount_sweep = 0; mount_sweep <= has_mounts; mount_sweep++ )
    {
        for( child = c->first_child; child >= 0; child = tree->components[child].next_sibling )
        {
            int sx = child_scroll_x;
            int sy = child_scroll_y;
            int const is_mount =
                has_mounts
                    ? child_is_interface_parent_mount(
                          tree, c->component_id, &tree->components[child])
                    : 0;
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
        /* Don't render unplaced orphan interface groups (CS2 auto-mounts them for
         * property access, not display) — they would cover the gameframe. */
        if( !UITree_RootIsDisplayable(tree, root) )
            continue;
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

/*
 * The debug overlay's own pass. It is not a layout walk: the overlay's content
 * is a display list the host already built in screen space, so the pass only
 * has to find the nodes and make one host call each. Running it after the walk
 * passes — rather than emitting the node in tree order — is what makes the
 * overlay unconditionally topmost, drag ghosts included, which is the whole
 * point of a debug overlay.
 *
 * The nodes come from `tree->debug_overlays`, not from a descent. The boot
 * manifest's RevConfig can park the overlay under any container it likes
 * (`p=<some_panel>` in the layout record), so finding it used to mean walking
 * the whole tree — and every lane's manifest declares an overlay that is
 * switched off until the P key, so the walk ran on every frame of every
 * session to arrive at a node that draws nothing. Measured in the browser at
 * 512.4 ms across a 23.5 s trace, 5.5% of all non-idle main-thread time: not
 * the visiting, but ~3600 cache misses a frame reading four fields out of a
 * 1.7 KB component each. The live set answers the same question in O(overlays).
 *
 * One desc for the entire display list. The render layer walks the prims
 * itself (torirs_frame's sb_steps expansion), so nothing here copies per-prim
 * state into the emit buffer.
 */
static void
emit_debug_overlay_node(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    int32_t idx)
{
    struct UITreeComponent const* c = &tree->components[idx];
    struct UITreeEmitDesc desc;
    struct UITreeHostRequest req;

    assert(!c->freed);
    assert(c->type == UIELEM_BUILTIN_DEBUG_OVERLAY);

    memset(&desc, 0, sizeof(desc));
    req.kind = UITREE_HOST_GET_DEBUG_OVERLAY;
    req.u.get_debug_overlay.out_prims = &desc.debug_prims;
    desc.debug_prim_count = UITree_Host(host, &req);
    if( desc.debug_prim_count <= 0 || !desc.debug_prims )
        return;

    desc.kind = UITREE_EMIT_DEBUG_OVERLAY;
    desc.node_index = idx;
    desc.component_id = c->component_id;
    desc.debug_font_id[TORIDBG_FONT_SMALL] = c->u.debug_overlay.font_id_small;
    desc.debug_font_id[TORIDBG_FONT_MENU] = c->u.debug_overlay.font_id_menu;
    desc.debug_font_id[TORIDBG_FONT_BODY] = c->u.debug_overlay.font_id_body;
    desc.debug_skin_scene_id = c->u.debug_overlay.skin_scene_id;
    for( int i = 0; i < TORIDBG_SKIN_SLOT_COUNT; i++ )
        desc.debug_skin_atlas[i] = c->u.debug_overlay.skin_atlas[i];
    /* Screen-space, not laid out: every prim already carries absolute
     * pixels and its own scissor box. The desc clip is the canvas. */
    desc.clip.x = 0;
    desc.clip.y = 0;
    desc.clip.w = UITREE_LAYOUT_ROOT_W;
    desc.clip.h = UITREE_LAYOUT_ROOT_H;
    emit_buffer_append(out, &desc);
}

static void
emit_debug_overlay_pass(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out)
{
    struct UITreeNodeSet const* set = &tree->debug_overlays;

    for( int32_t s = 0; s < set->count; s++ )
        emit_debug_overlay_node(tree, host, out, set->slots[s]);
}

/*
 * Entity overlays belong to the SCENE pass, not to their place in the tree.
 *
 * The reference draws health bars and hitsplats inside drawEntities, which is
 * part of the 3D pass -- so every interface painted afterwards covers them.
 * Ours is a root-level builtin listed after the gameframe, which is the right
 * place to READ it from (it is projected against the world rect the
 * gameframe's viewport reports) and the wrong place to DRAW it: at a
 * resizable layout the viewport is the whole canvas and the chatbox floats
 * over it, so a bar or a hitsplat above an entity standing behind the chat
 * drew on top of the chat text.
 *
 * Clipping cannot express that -- the chat is INSIDE the world rect, so the
 * scene clip the overlay already carries is no help. Z-order is the fix: the
 * descs move to directly after the world they were projected against, which
 * leaves the interfaces, drag ghosts and screen chrome (cross, hovertext,
 * minimenu) above them, exactly as the reference orders them.
 */
static void
emit_hoist_entity_overlays(struct UITreeEmitBuffer* out)
{
    int world = -1;
    int write;

    assert(out);
    /* The last one: a tree can only draw one world, and the app latches the
     * last WORLD desc as the viewport for the same reason. */
    for( int i = 0; i < out->count; i++ )
        if( out->cmds[i].kind == UITREE_EMIT_WORLD )
            world = i;
    if( world < 0 )
        return;

    write = world + 1;
    for( int i = write; i < out->count; i++ )
    {
        struct UITreeEmitDesc moved;
        if( out->cmds[i].kind != UITREE_EMIT_ENTITY_OVERLAY )
            continue;
        if( i != write )
        {
            /* Stable rotate: the overlay lands at `write` and everything it
             * jumped over keeps its own relative order. */
            moved = out->cmds[i];
            memmove(
                &out->cmds[write + 1],
                &out->cmds[write],
                (size_t)(i - write) * sizeof(*out->cmds));
            out->cmds[write] = moved;
        }
        write++;
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
    {
        int free_len = 0;
        for( int32_t i = tree->free_head; i >= 0; i = tree->components[i].free_next )
            free_len++;
        TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_UITREE_COMPONENTS, (int64_t)tree->component_count);
        TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_UITREE_CAPACITY, (int64_t)tree->component_capacity);
        TORIRS_PERF_COUNT_SET(TORIRS_PERF_CTR_UITREE_FREE_LIST, free_len);
        TORIRS_PERF_COUNT_SET(
            TORIRS_PERF_CTR_UITREE_NODE_BYTES,
            (int64_t)sizeof(struct UITreeComponent) * tree->component_capacity);
    }
    /* Single interleaved pass in tree order (reference widgets-gl drawNode emits a
     * widget's own fill/sprite/text inline, then descends into children), then
     * deferred drag sources on top. Splitting text into its own pass put every
     * text in the tree above every non-text, so a widget group that should cover
     * an earlier one — an open dropdown over a label — drew under its text. */
    emit_walk_pass(
        tree, host, out, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, hovered_component_id, 0);
    /* The second pass has nothing to draw unless a drag is running: every node it
     * reaches takes the descend-only branch. It was the single largest traversal
     * in the client (more visits than the draw pass, since descend-only bypasses
     * the collapsed-layer prune) and on an ordinary frame all of it was waste. */
    if( UITree_HasActiveDrag(tree) )
    {
        emit_walk_pass(
            tree, host, out, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, hovered_component_id, 1);
    }
    emit_hoist_entity_overlays(out);
    /* Last, so developer chrome is over everything including drag ghosts. */
    emit_debug_overlay_pass(tree, host, out);
}
