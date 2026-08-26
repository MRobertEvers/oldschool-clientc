#include "uitree_emit.h"

#include "uitree_frame.h"

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
#include "log/torirs_log.h"

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
    int scroll_x;
    int scroll_y;

    UITree_ScrollGetClamped(component, &scroll_x, &scroll_y);
    memset(out, 0, sizeof(*out));
    out->kind = UITREE_EMIT_SCROLLBAR_V;
    out->node_index = node_index;
    out->component_id = component->component_id;
    out->x = x + w;
    out->y = y;
    out->w = UITREE_SCROLLBAR_THICKNESS;
    out->h = UITree_ScrollLayerNeedsHorizontal(component) ? h - UITREE_SCROLLBAR_THICKNESS : h;
    out->scroll_off_x = scroll_x;
    out->scroll_off_y = scroll_y;
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
    int scroll_x;
    int scroll_y;

    UITree_ScrollGetClamped(component, &scroll_x, &scroll_y);
    memset(out, 0, sizeof(*out));
    out->kind = UITREE_EMIT_SCROLLBAR_H;
    out->node_index = node_index;
    out->component_id = component->component_id;
    out->x = x;
    out->y = y + h - UITREE_SCROLLBAR_THICKNESS;
    out->w = UITree_ScrollLayerNeedsVertical(component) ? w - UITREE_SCROLLBAR_THICKNESS : w;
    out->h = UITREE_SCROLLBAR_THICKNESS;
    out->scroll_off_x = scroll_x;
    out->scroll_off_y = scroll_y;
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
 * inv-contains sentinel). Task_CS1Eval publishes the values on the component
 * before the frame fence, so drawing neither runs the VM nor round-trips the
 * already tree-owned result through the host.
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
            int const value = host ? component->cs1_values[src[1] - '1'] : 0;

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

    /* The two guards below reject ~118 of the visits that reach this function in
     * a quiet frame — measured as the drop in host round trips, not off
     * `uitree_emit_skip`, which seven different sites share — and neither of
     * them reads `out`. Clearing a 520-byte descriptor above them therefore
     * zeroed ~61 KB per frame that was discarded on the next line; together with
     * the host call this is worth -1.4% of the emit stage. The clear sits after
     * them instead, and
     * `active` (a host round trip) is computed only once a node is going to
     * draw. Everything from here to the switch writes `out`, so the clear still
     * precedes the first field written and the untouched tail is still zero —
     * which the frame-to-frame byte compare in app.c depends on. */
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

    memset(out, 0, sizeof(*out));

    /* TS Client draw: components swap to their "active" (getIfActive) or "over"
     * (hovered) colour / text / sprite variant. Active is host-evaluated; hover
     * matches this component's own id. */
    bool const hovered =
        hovered_component_id >= 0 && component->component_id == hovered_component_id;
    /* Task_CS1Eval publishes this through UITree_SetCS1ActiveAt before the
     * settled frame fence. Reading the tree-owned cache directly avoids one
     * host callback per drawable node and makes that typed publication the
     * single mutation seam that retention observes. */
    bool const active = host && component->cs1_active != 0;

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

    case UIELEM_RS_ARC:
        out->kind = UITREE_EMIT_ARC;
        out->color = component->u.rs_arc.color;
        out->filled = component->u.rs_arc.filled;
        out->line_width =
            component->u.rs_arc.line_width > 0 ? component->u.rs_arc.line_width : 1;
        out->arc_start = component->u.rs_arc.arc_start;
        out->arc_end = component->u.rs_arc.arc_end;
        return true;

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
        return true;

    case UIELEM_BUILTIN_MINIMAP:
    {
        int frame_mask_overridden = 0;
        /* The pack graphic is only a mask placeholder; the drawable is the world
         * map the host bakes, which also owns the camera pivot inside it. */
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_MINIMAP_STATE,
            .u.get_minimap_state.out_src_anchor_x = &out->src_anchor_x,
            .u.get_minimap_state.out_src_anchor_y = &out->src_anchor_y,
        };
        /* MINIMAP_TOGGLE: the server can take the map away. Nothing is
         * drawn in its place -- the hole in the mapback frame art is what
         * shows through, which is the reference's "hidden" too. */
        {
            struct UITreeHostRequest hidden_req = { .kind = UITREE_HOST_GET_MINIMAP_HIDDEN };
            if( UITree_Host(host, &hidden_req) )
                return false;
        }
        out->kind = UITREE_EMIT_MINIMAP;
        out->scene_id = UITree_Host(host, &req);
        if( out->scene_id <= 0 )
            return false;
        /* That mask placeholder clips the over-filled map to its round window.
         * Which side of the mask is the window is era art, not a widget
         * property — see UITree.mask_keep_opaque. */
        out->mask_scene_id = component->u.minimap.mask_scene_id;
        out->mask_atlas_index = component->u.minimap.mask_atlas_index;
        {
            int mask_override = 0;
            if( UITree_FrameSkinOverride(tree, node_index, NULL, &mask_override) )
            {
                frame_mask_overridden = 1;
                out->mask_scene_id = mask_override;
                out->mask_atlas_index = 0;
            }
        }
        /* Plugin masks have one stable API convention: transparent pixels are
         * the window. Native cache masks remain era-dependent. */
        out->mask_keep_opaque = frame_mask_overridden ? 0 : tree->mask_keep_opaque;
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
        out->entity_overlay_source = UITREE_EMIT_OVERLAY_ENTITY;
        /* The walk temporarily carries a zero-count descriptor through common
         * clipping so it can retain that exact shape out of band, then removes
         * it before publishing the renderer command list. */
        return true;
    }

    case UIELEM_BUILTIN_COMPASS:
    {
        int frame_mask_overridden = 0;
        out->kind = UITREE_EMIT_COMPASS;
        out->scene_id = component->u.sprite.scene_id;
        out->atlas_index = component->u.sprite.atlas_index;
        {
            int art_override = 0;
            int mask_override = 0;
            if( UITree_FrameSkinOverride(
                    tree, node_index, &art_override, &mask_override) )
            {
                frame_mask_overridden = 1;
                if( art_override > 0 )
                {
                    out->scene_id = art_override;
                    out->atlas_index = 0;
                }
                out->mask_scene_id = mask_override;
                out->mask_atlas_index = 0;
            }
        }
        /* No RevConfig sprite= binding (interface-open path): fall back to the
         * client-hardcoded compass the host loaded. */
        if( out->scene_id <= 0 )
            out->scene_id = host_static_sprite_scene(host, UITREE_STATIC_SPRITE_COMPASS);
        if( out->scene_id <= 0 )
            return false;
        /* The pack's placeholder graphic doubles as the circular clip. */
        if( !frame_mask_overridden )
        {
            out->mask_scene_id = component->u.sprite.mask_scene_id;
            out->mask_atlas_index = component->u.sprite.mask_atlas_index;
        }
        out->mask_keep_opaque = frame_mask_overridden ? 0 : tree->mask_keep_opaque;
        out->rotation_r2pi2048 = UITree_ComponentSpriteRotation(component, host);
        return true;
    }

    case UIELEM_RS_LAYER:
    {
        int sb_scene;
        if( component->if3 )
            return false;
        if( !UITree_ScrollLayerNeedsVertical(component) &&
            !UITree_ScrollLayerNeedsHorizontal(component) )
            return false;
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

    case UIELEM_BUILTIN_MULTIWAY:
    {
        /* Reference drawScene tail: `if (inMultizone === 1) headicons[1]
         * .plotSprite(472, 296)`. Both of those numbers are revconfig's here
         * -- the frame through `sprite=headicons[1]`, the place through the
         * layout row -- so this is only the gate and the blit. */
        struct UITreeHostRequest req = { .kind = UITREE_HOST_GET_MULTIWAY };
        if( !UITree_Host(host, &req) )
            return false;
        if( component->u.sprite.scene_id <= 0 )
            return false;
        out->kind = UITREE_EMIT_SPRITE;
        out->scene_id = component->u.sprite.scene_id;
        out->atlas_index = component->u.sprite.atlas_index;
        return true;
    }

    case UIELEM_BUILTIN_REBOOT_TIMER:
    {
        /* Reference drawScene tail: 'System update in: M:SS' at (4, 329) in
         * yellow. The host owns the string because it owns the clock; the
         * font, the colour and the place are the widget's. */
        char const* text = NULL;
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_REBOOT_TIMER,
            .u.get_reboot_timer.out_text = &text,
        };
        if( !UITree_Host(host, &req) || !text || !text[0] )
            return false;
        if( component->u.reboot_timer.font_id <= 0 )
            return false;
        out->kind = UITREE_EMIT_TEXT;
        out->font_id = component->u.reboot_timer.font_id;
        out->color = component->u.reboot_timer.color;
        out->text = text;
        /* The reference expresses this as font.drawString(s, x, y), so the
         * layout's y is the baseline and the box does not align it. That is
         * what lets the revconfig row carry the reference's own coordinates
         * rather than a guess at where the top of the line would be. */
        out->text_baseline = 1;
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
        /* TUT_FLASH: the tutorial points at a tab by blinking its icon, which
         * is drawn as NOT DRAWING it for half of each cycle -- there is no
         * highlight sprite, the gap is the signal (reference drawSidebarIcons).
         * It belongs here rather than in whatever handled the packet: the icon
         * is this component, and a blink is a property of drawing it. */
        req.kind = UITREE_HOST_GET_TAB_FLASH_HIDDEN;
        if( host && UITree_Host(host, &req) )
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
emit_buffer_advance_publication(struct UITreeEmitBuffer* buf)
{
    buf->publication_seq++;
    if( buf->publication_seq == 0 )
        buf->publication_seq++;
}

bool
UITree_EmitBufferHostInputsCurrent(
    struct UITreeEmitBuffer const* buf,
    struct UITreeHost const* host)
{
    assert(buf);
    return UITree_HostInputStampIsCurrent(&buf->host_input_stamp, host);
}

static bool
emit_retain_gate_sources_quiet(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer const* buf,
    int hovered_component_id,
    struct UITreeEmitRetainGate const* gate)
{
    /* A pending invalidation precedes the completed resolver-sequence bump.
     * Both pending flags therefore belong to the identity; otherwise the
     * frame between invalidation and resolution could retain stale boxes. */
    return gate->primed && gate->source_tree == tree && !UITree_HasActiveDrag(tree) &&
           !tree->layout_stale &&
           !tree->layout_force_full &&
           tree->dirty_gen == gate->dirty_gen &&
           tree->layout_resolve_seq == gate->layout_resolve_seq &&
           tree->generation == gate->tree_generation &&
           hovered_component_id == gate->hovered_component_id &&
           UITree_EmitBufferHostInputsCurrent(buf, host);
}

bool
UITree_EmitRetainGateQuiet(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer const* buf,
    int hovered_component_id,
    struct UITreeEmitRetainGate const* gate)
{
    assert(tree);
    assert(buf);
    assert(gate);

    return gate->source_buffer == buf &&
           gate->buffer_publication_seq == buf->publication_seq &&
           emit_retain_gate_sources_quiet(
               tree, host, buf, hovered_component_id, gate);
}

void
UITree_EmitRetainGateCapture(
    struct UITree const* tree,
    struct UITreeEmitBuffer const* buf,
    int hovered_component_id,
    struct UITreeEmitRetainGate* gate)
{
    assert(tree);
    assert(buf);
    assert(gate);

    gate->source_tree = tree;
    gate->source_buffer = buf;
    gate->buffer_publication_seq = buf->publication_seq;
    gate->dirty_gen = tree->dirty_gen;
    gate->layout_resolve_seq = tree->layout_resolve_seq;
    gate->tree_generation = tree->generation;
    gate->hovered_component_id = hovered_component_id;
    gate->primed = 1;
}

bool
UITree_EmitRetainGateRefreshVolatile(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* buf,
    int const* hovered_component_id,
    struct UITreeEmitRetainGate const* gate)
{
    assert(tree);
    assert(buf);
    assert(hovered_component_id);
    assert(gate);

    if( !UITree_EmitRetainGateQuiet(
            tree, host, buf, *hovered_component_id, gate) ||
        buf->volatile_unrefreshable )
        return false;
    if( !buf->volatile_refs )
        return true;
    if( !UITree_EmitRefreshVolatile(tree, host, buf) )
        return false;

    /* Host callbacks are arbitrary App/plugin code. They can advance an input
     * epoch, change topology, invalidate layout, or move hover while refreshing
     * a same-frame pointer. Recheck the complete semantic identity captured by
     * the original full walk before publishing any partially refreshed list.
     * Ignore only publication_seq: this refresh itself deliberately advanced it. */
    return emit_retain_gate_sources_quiet(
        tree, host, buf, *hovered_component_id, gate);
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
    cfg = UITree_ChatButton(c);
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
    int is_placeholder = 0;
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
            .u.get_obj_name.out_placeholder = &is_placeholder,
        };
        UITree_Host(host, &req);
    }
    /*
     * A bank placeholder never carries a number, and the obj record is the only
     * thing that can say so. `bankmain_drawitem` (clientscript 278) draws one
     * with the *plain* opcode at a count of zero — `cc_setobject($obj, 0)` —
     * which is byte for byte what a shop's out-of-stock line does
     * (clientscript 1076, `cc_setobject($obj, inv_getnum(...))`). Same opcode,
     * same count, and the shop must print its "0"; so the difference cannot
     * come from the call, only from the obj, and a placeholder is the record
     * that carries a template. Without this every placeholder in the bank wore
     * a yellow "0".
     */
    if( is_placeholder )
        return;
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
    layout.offset_x = UITree_InvSlots(c)->offset_x;
    layout.offset_y = UITree_InvSlots(c)->offset_y;
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

        /* > 0, not >= 0: the bridge allocates icon scene ids from 1 up and
         * answers -1 when it cannot build one, so 0 is not an icon — it is a
         * slot whose icon reference was never set. Drawing it emitted an empty
         * sprite and then the stack count on top, which reads as a floating
         * number with no item under it. */
        if( obj_id > 0 && scene_id > 0 )
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
            int bg_scene = UITree_InvSlots(c)->bg_scene_id[slot];
            int bg_atlas = UITree_InvSlots(c)->bg_atlas_index[slot];
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

/*
 * Paint declared against a semantic frame slot belongs at one exact boundary:
 * after the slot node's whole subtree, before the next sibling.
 *
 * It cannot use the FRAME or CANVAS plugin passes. Both are global z-order
 * buckets and turn a local relationship (the map housing is directly over the
 * minimap) into a relationship with every interface on the screen. Emitting
 * here preserves ordinary descriptor/item order, including a target expanded
 * into several descriptors and descendants several levels deep.
 *
 * `subtree_emit_start` is also the visibility fence. A host-hidden minimap,
 * collapsed container, inactive tab, or otherwise empty target emitted no
 * part of its subtree, so attaching its housing would leave chrome for a
 * surface that is not there. Early structural visibility rejects return before
 * this helper; the count catches draw-time rejects such as MINIMAP_TOGGLE.
 */
static void
emit_frame_slot_overlay(
    struct UITree const* tree,
    struct UITreeEmitBuffer* out,
    int32_t idx,
    struct UITreeEmitClip const* parent_clip,
    int subtree_emit_start)
{
    struct UITreeFrameOverlay overlay;
    struct UITreeEmitDesc desc;

    assert(tree);
    assert(out);
    assert(parent_clip);
    if( out->count <= subtree_emit_start ||
        !UITree_FrameOverlayOverride(tree, idx, &overlay) || overlay.scene_id <= 0 )
        return;

    memset(&desc, 0, sizeof(desc));
    desc.kind = UITREE_EMIT_SPRITE;
    /* Paint-only: it borrows the semantic node's placement and clip but is not
     * another interactive component, so it must not acquire its id. */
    desc.node_index = -1;
    desc.component_id = -1;
    desc.scene_id = overlay.scene_id;
    desc.atlas_index = 0;
    desc.x = overlay.x;
    desc.y = overlay.y;
    desc.clip = *parent_clip;
    desc.if3 = 0;
    desc.trans = overlay.trans;
    emit_buffer_append(out, &desc);
}

static void
emit_role_overlay_groups(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    int32_t idx,
    struct UITreeEmitClip const* parent_clip,
    struct UITreeRoleOverlayGroup const* groups,
    int group_count,
    int replace)
{
    struct UITreeComponent const* component;

    assert(tree);
    assert(out);
    assert(parent_clip);
    if( !groups || group_count <= 0 || idx < 0 ||
        (uint32_t)idx >= tree->component_count )
        return;
    component = &tree->components[idx];
    for( int i = 0; i < group_count; i++ )
    {
        struct UITreeRoleOverlayGroup const* group = &groups[i];
        struct UITreeEmitDesc desc;
        struct UITreeHostRequest clip_req;

        if( group->node_index != idx ||
            group->node_incarnation != component->incarnation ||
            !!group->replace != !!replace )
            continue;

        /* Hit regions are consumed on the following interaction frame. Stamp
         * the exact same parent clip as paint now; a missing/hidden target is
         * never reached and therefore leaves its regions inactive. */
        memset(&clip_req, 0, sizeof(clip_req));
        clip_req.kind = UITREE_HOST_SET_ROLE_OVERLAY_CLIP;
        clip_req.u.set_role_overlay_clip.node_index = idx;
        clip_req.u.set_role_overlay_clip.node_incarnation = component->incarnation;
        clip_req.u.set_role_overlay_clip.replace = replace;
        clip_req.u.set_role_overlay_clip.clip_x = parent_clip->x;
        clip_req.u.set_role_overlay_clip.clip_y = parent_clip->y;
        clip_req.u.set_role_overlay_clip.clip_w = parent_clip->w;
        clip_req.u.set_role_overlay_clip.clip_h = parent_clip->h;
        (void)UITree_Host(host, &clip_req);

        if( group->item_count <= 0 || !group->items )
            continue;
        memset(&desc, 0, sizeof(desc));
        desc.kind = UITREE_EMIT_ENTITY_OVERLAY;
        /* Paint-only: the replacement's hit surface is the plugin region, not
         * a second copy of the native semantic component. */
        desc.node_index = -1;
        desc.component_id = -1;
        desc.clip = *parent_clip;
        desc.entity_overlays = group->items;
        desc.entity_overlay_count = group->item_count;
        desc.entity_overlay_source = UITREE_EMIT_OVERLAY_NONE;
        emit_buffer_append(out, &desc);
    }
}

/* TORIRS_MODEL_CLIP_DEBUG, read once -- it sits on a per-model-desc path in
 * the emit walk, where a getenv() per frame per model is a linear scan of the
 * environment block to decide not to print. */
static int
model_clip_debug_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_MODEL_CLIP_DEBUG") ? 1 : 0;
    return armed;
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
    int in_deferred,
    struct UITreeRoleOverlayGroup const* role_groups,
    int role_group_count)
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
    int subtree_emit_start;

    assert(tree && out && parent_clip);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;

    if( drag_pass )
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_WALK_EMIT_DRAG, 1);
    else
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_WALK_EMIT, 1);

    c = &tree->components[idx];
    /* Native/script hiding outranks replacement art too: an anchor is local to
     * a target that is actually present in this frame, not a way to resurrect
     * a collapsed tab or a gameframe lane that suppressed the whole surface. */
    if( c->frame_hidden || c->projection_hidden )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_EMIT_SKIP, 1);
        return;
    }
    /* Hide-gated layers stay invisible unless their component_id is hovered. */
    if( c->behavior.hide && !UITree_ComponentVisibleById(c, hovered_component_id) )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_EMIT_SKIP, 1);
        return;
    }

    /* After the own-hide reject, not before: a self-hidden node contributes
     * nothing to the list, so marks on it are worth filtering, and that is
     * where two thirds of this client's per-frame UI damage lands (closed
     * interfaces still ticking their 3D models). Safe only because every write
     * to a `hide` bit bumps dirty_gen unconditionally rather than through the
     * filtered MarkNodeDirty path — see UITree_SetHide. */
    if( (uint32_t)idx < tree->emit_visited_cap )
        tree->emit_visited[idx] = 1;

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

    /* Structural collapse is decided before replacement and before drag. A
     * replacement cannot resurrect a zero-sized clipping layer; conversely a
     * target that became replacement-hidden during the canvas prepass must
     * never enter the deferred-drag machinery and leak its native ghost on the
     * second pass. */
    if( UITree_LayerCullsChildren(c, w, h) )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_EMIT_SKIP, 1);
        return;
    }
    if( c->replacement_hidden )
    {
        if( !drag_pass )
            emit_role_overlay_groups(
                tree,
                host,
                out,
                idx,
                parent_clip,
                role_groups,
                role_group_count,
                /*replace=*/1);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_UITREE_EMIT_SKIP, 1);
        return;
    }

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
                    in_deferred,
                    role_groups,
                    role_group_count);
            }
        }
        return;
    }

    scroll_layer = layer_needs_scroll_offset(c);
    if1_bar = layer_is_if1_scrollbar(c);

    child_scroll_x = scroll_off_x;
    child_scroll_y = scroll_off_y;
    if( scroll_layer )
    {
        int clamped_x;
        int clamped_y;
        UITree_ScrollGetClamped(c, &clamped_x, &clamped_y);
        if( UITree_ScrollLayerNeedsHorizontal(c) )
            child_scroll_x += clamped_x;
        if( UITree_ScrollLayerNeedsVertical(c) )
            child_scroll_y += clamped_y;
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
        if( UITree_LayerChildClip(c, &surf, clip_x, clip_y, w, h, &cc, &cs) )
        {
            layer_clip = (struct UITreeEmitClip){ cc.clip_x, cc.clip_y, cc.clip_w, cc.clip_h };
            layer_surface = (struct UITreeEmitClip){ cs.clip_x, cs.clip_y, cs.clip_w, cs.clip_h };
            child_clip = &layer_clip;
            child_surface = &layer_surface;
        }
        /*
         * The entity overlay clips its CHILDREN to the world rect, and only
         * them.
         *
         * Its children are the scripted entity overlays
         * (game/rs_entity_overlay.h) -- world content, which the reference
         * draws inside the scene pass under the same clip the health bars get.
         * A 60x60 marker on a loc at the edge of the viewport paints over the
         * inventory without this.
         *
         * It is NOT in UITree_ComponentClipsChildren, because that predicate
         * carries a second meaning this node cannot accept: a clipping layer
         * with a degenerate box prunes its whole subtree AND skips its own
         * draw (UITree_LayerCullsChildren). This node's own content is the
         * host's health bars and hitsplats, which carry their own clip and
         * must still draw on a tree whose App never wrote a world rect here.
         */
        if( c->type == UIELEM_BUILTIN_ENTITY_OVERLAY && w > 0 && h > 0 )
        {
            struct UITreeScrollClip cc2 = surf;
            UITree_ScrollIntersectClip(&cc2, clip_x, clip_y, w, h);
            layer_clip = (struct UITreeEmitClip){ cc2.clip_x, cc2.clip_y, cc2.clip_w, cc2.clip_h };
            child_clip = &layer_clip;
            child_surface = &layer_clip;
        }
    }

    subtree_emit_start = out->count;

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
            if( desc.entity_overlay_source != UITREE_EMIT_OVERLAY_NONE )
                out->volatile_overlay_enclosing_clip[desc.entity_overlay_source] = *parent_clip;
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
        if( desc.kind == UITREE_EMIT_MODEL && model_clip_debug_armed() )
            TORIRS_LOG("model com=0x%08x box=%d,%d %dx%d (right=%d) clip=%d,%d %dx%d (right=%d)\n",
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
                in_deferred,
                role_groups,
                role_group_count);
        }
    }

    if( if1_bar && !drag_pass )
    {
        emit_append_layer_scrollbars(
            host, out, c, idx, parent_clip, x - scroll_off_x, y - scroll_off_y, w, h);
    }

    emit_frame_slot_overlay(tree, out, idx, parent_clip, subtree_emit_start);
    emit_role_overlay_groups(
        tree,
        host,
        out,
        idx,
        parent_clip,
        role_groups,
        role_group_count,
        /*replace=*/0);
}

static void
emit_walk_pass(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    int canvas_w,
    int canvas_h,
    int hovered_component_id,
    int drag_pass,
    struct UITreeRoleOverlayGroup const* role_groups,
    int role_group_count)
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
            0,
            role_groups,
            role_group_count);
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
    struct UITreeDebugOverlayConfig const* overlay = UITree_DebugOverlay(c);
    desc.debug_font_id[TORIRS_CHROME_FONT_SMALL] = overlay->font_id_small;
    desc.debug_font_id[TORIRS_CHROME_FONT_MENU] = overlay->font_id_menu;
    desc.debug_font_id[TORIRS_CHROME_FONT_BODY] = overlay->font_id_body;
    desc.debug_skin_scene_id = overlay->skin_scene_id;
    for( int i = 0; i < TORIRS_CHROME_SKIN_SLOT_COUNT; i++ )
        desc.debug_skin_atlas[i] = overlay->skin_atlas[i];
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
 * The plugin FRAME overlay: one desc, in canvas space, hoisted to sit directly
 * over the 3D scene -- under every interface, and over the entity overlays.
 *
 * This is where the reference's own frame art is drawn, and a gameframe cannot
 * be drawn anywhere else. The canvas pass below paints over the interfaces,
 * which is right for a readout and wrong for chrome: a sidebar panel emitted
 * there covers the inventory it is meant to sit behind, and a chatbox backing
 * covers the chat text.
 *
 * OVER the entity overlays and not under them, for the reason the overlays
 * were hoisted in the first place: a health bar above an entity standing
 * behind the chatbox must not draw on the chatbox, and under a plugin layout
 * the chatbox backing is one of these blits.
 *
 * No world in the tree means no frame either. A layout that could not find the
 * scene has nothing to be a frame AROUND, and appending the chrome anyway
 * would put it over the interfaces -- the one place it must never be.
 */
static void
emit_plugin_frame_pass(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out)
{
    struct UITreeEmitDesc desc;
    struct UITreeHostRequest req;
    int world = -1;
    int at;

    assert(tree);
    assert(out);

    for( int i = 0; i < out->count; i++ )
        if( out->cmds[i].kind == UITREE_EMIT_WORLD )
            world = i;
    if( world < 0 )
        return;

    memset(&desc, 0, sizeof(desc));
    memset(&req, 0, sizeof(req));
    req.kind = UITREE_HOST_GET_FRAME_OVERLAYS;
    req.u.get_entity_overlays.out_items = &desc.entity_overlays;
    req.u.get_entity_overlays.out_clip_x = &desc.clip.x;
    req.u.get_entity_overlays.out_clip_y = &desc.clip.y;
    req.u.get_entity_overlays.out_clip_w = &desc.clip.w;
    req.u.get_entity_overlays.out_clip_h = &desc.clip.h;
    out->volatile_overlay_seen |= (uint8_t)(1u << UITREE_EMIT_OVERLAY_FRAME);
    desc.entity_overlay_count = UITree_Host(host, &req);
    desc.kind = UITREE_EMIT_ENTITY_OVERLAY;
    desc.entity_overlay_source = UITREE_EMIT_OVERLAY_FRAME;
    desc.node_index = -1;
    desc.component_id = -1;
    out->volatile_overlay_template[UITREE_EMIT_OVERLAY_FRAME] = desc;
    if( desc.entity_overlay_count <= 0 || !desc.entity_overlays )
        return;
    out->volatile_overlay_nonempty |= (uint8_t)(1u << UITREE_EMIT_OVERLAY_FRAME);
    emit_buffer_append(out, &desc);

    /* Stable rotate to just after the world, exactly as the entity-overlay
     * hoist does it -- and BEFORE that hoist runs, which is what orders the
     * two: the hoist then inserts the bars at the same index and pushes this
     * desc one further along, so the chrome paints over them. */
    at = out->count - 1;
    if( at > world + 1 )
    {
        struct UITreeEmitDesc moved = out->cmds[at];
        memmove(
            &out->cmds[world + 2],
            &out->cmds[world + 1],
            (size_t)(at - world - 1) * sizeof(*out->cmds));
        out->cmds[world + 1] = moved;
    }
}

/*
 * The plugin CANVAS overlay: one desc, in canvas space, above everything the
 * tree drew.
 *
 * No node behind it, unlike the entity overlay and the debug overlay, and that
 * is deliberate. Both of those are components a profile has to author, and a
 * profile that forgot one is a lane where the feature silently does not exist
 * -- which is exactly what a plugin must not depend on. A plugin runs on every
 * lane this client boots, including the ones whose gameframe comes out of a
 * 2004 cache and knows nothing about any of this, so its surface is the
 * canvas itself and the pass that emits it is unconditional.
 *
 * It costs one host call per frame when no plugin drew, and that call answers
 * zero -- the same shape as the entity overlay's, which also asks every frame.
 *
 * Placed BEFORE the debug overlay pass: developer chrome stays on top of
 * everything, plugin chrome sits over the game.
 */
static void
emit_plugin_canvas_pass(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out)
{
    assert(tree);

    struct UITreeEmitDesc desc;
    struct UITreeHostRequest req;

    memset(&desc, 0, sizeof(desc));
    memset(&req, 0, sizeof(req));
    req.kind = UITREE_HOST_GET_CANVAS_OVERLAYS;
    req.u.get_entity_overlays.out_items = &desc.entity_overlays;
    req.u.get_entity_overlays.out_clip_x = &desc.clip.x;
    req.u.get_entity_overlays.out_clip_y = &desc.clip.y;
    req.u.get_entity_overlays.out_clip_w = &desc.clip.w;
    req.u.get_entity_overlays.out_clip_h = &desc.clip.h;
    out->volatile_overlay_seen |= (uint8_t)(1u << UITREE_EMIT_OVERLAY_CANVAS);
    desc.entity_overlay_count = UITree_Host(host, &req);
    desc.kind = UITREE_EMIT_ENTITY_OVERLAY;
    desc.entity_overlay_source = UITREE_EMIT_OVERLAY_CANVAS;
    desc.node_index = -1;
    desc.component_id = -1;
    out->volatile_overlay_template[UITREE_EMIT_OVERLAY_CANVAS] = desc;
    if( desc.entity_overlay_count <= 0 || !desc.entity_overlays )
        return;
    /* The same emit kind, because the item vocabulary and the renderer's
     * expansion of it are the same; only the clip the host reported differs. */
    out->volatile_overlay_nonempty |= (uint8_t)(1u << UITREE_EMIT_OVERLAY_CANVAS);
    emit_buffer_append(out, &desc);

    /*
     * ...and then slide it back under the POINTER FEEDBACK.
     *
     * Plugin chrome belongs over the game and under the things that answer the
     * pointer: the right-click menu, the mouseover line and the click cross.
     * Appending puts it over all three, and an orb drawn across an open
     * minimenu is not a layering nicety -- the menu is what the player is
     * reading, and half of it is behind an orb.
     *
     * Found by NODE TYPE rather than by emit kind, because those three carry
     * no kind of their own: the minimenu is a run of RECT and TEXT descs, the
     * hover line is TEXT, the cross is a SPRITE. What they have in common is
     * the builtin they were emitted from, which every desc names.
     */
    {
        int insert_at = -1;

        for( int i = 0; i < out->count - 1; i++ )
        {
            int32_t const node = out->cmds[i].node_index;
            enum UITreeComponentType type;

            if( node < 0 || (uint32_t)node >= tree->component_count )
                continue;
            type = tree->components[node].type;
            if( type != UIELEM_BUILTIN_MINIMENU && type != UIELEM_BUILTIN_HOVERTEXT &&
                type != UIELEM_BUILTIN_CROSS )
                continue;
            insert_at = i;
            break;
        }

        if( insert_at >= 0 )
        {
            struct UITreeEmitDesc const moved = out->cmds[out->count - 1];
            memmove(
                &out->cmds[insert_at + 1],
                &out->cmds[insert_at],
                (size_t)(out->count - 1 - insert_at) * sizeof(*out->cmds));
            out->cmds[insert_at] = moved;
        }
    }
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
static int
emit_is_in_entity_overlay(struct UITree const* tree, int32_t idx)
{
    int32_t const owner = tree->entity_overlay_index;

    if( owner < 0 || idx < 0 )
        return 0;
    for( int guard = 0; idx >= 0 && guard < 64; guard++ )
    {
        if( idx == owner )
            return 1;
        if( (uint32_t)idx >= tree->component_count )
            return 0;
        idx = tree->components[idx].parent;
    }
    return 0;
}

static void
emit_hoist_entity_overlays(struct UITree const* tree, struct UITreeEmitBuffer* out)
{
    int world = -1;
    int write;

    assert(tree);
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
        /*
         * The host-drawn items are one desc; the SCRIPTED overlays
         * (game/rs_entity_overlay.h) are ordinary components under the same
         * builtin, emitting ordinary sprites and text. Both have to move, or a
         * fishing-spot marker draws over the inventory instead of in the world.
         */
        if( !(out->cmds[i].kind == UITREE_EMIT_ENTITY_OVERLAY &&
              out->cmds[i].entity_overlay_source == UITREE_EMIT_OVERLAY_ENTITY) &&
            !emit_is_in_entity_overlay(tree, out->cmds[i].node_index) )
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
    struct UITreeRoleOverlayGroup const* role_groups = NULL;
    struct UITreeHost const* stamp_host = host;
    struct UITreeHost observed_host;
    int role_group_count = 0;
    int role_anchor_seen = 0;

    assert(tree);
    assert(out);

    /* Observe host reads through a shallow copy: the application's host stays
     * immutable, while every UITree_Host call made by this walk contributes
     * its classified input domains to the buffer. This records requests which
     * return "nothing" too — important because a later zero-to-nonzero answer
     * can add a descriptor that did not exist to be refreshed in place. */
    out->host_input_dependencies = 0;
    if( host )
    {
        observed_host = *host;
        observed_host.observed_input_mask = &out->host_input_dependencies;
        host = &observed_host;
    }

    out->volatile_overlay_seen = 0;
    out->volatile_overlay_nonempty = 0;
    memset(out->volatile_overlay_template, 0, sizeof(out->volatile_overlay_template));
    memset(
        out->volatile_overlay_enclosing_clip,
        0,
        sizeof(out->volatile_overlay_enclosing_clip));
    for( int source = UITREE_EMIT_OVERLAY_NONE;
         source <= UITREE_EMIT_OVERLAY_FRAME;
         source++ )
        out->volatile_overlay_insert_at[source] = -1;
    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_BEGIN_OVERLAYS };
        (void)UITree_Host(host, &req);
    }
    /*
     * Draw never reads a stale box (reference ensureLayout, run before the
     * widget draw for exactly this reason). A layout input can be written from
     * anywhere between two frames -- the CS1 clientCode tick resizes the
     * friends/ignore list scroll extents, which invalidates every resolved box
     * -- and UITree_LayoutGetBounds answers an unresolved node with its
     * AUTHORED x/y/w/h. For a mounted IF1 subtree those are relative to the
     * interface, so the sidebar's whole inventory drew at 16,8 under a clip of
     * zero width: no item icons at all. No-op when nothing invalidated.
     */
    /* Publication fence for a plugin gameframe. Geometry and art are effective
     * layers now, so this does not race CS1/CS2 by rewriting their native
     * fields. It only re-resolves semantic membership when topology changed
     * since the declaration, before EnsureLayout consumes those bindings. */
    UITree_FrameReassert((struct UITree*)tree);
    UITree_EnsureLayout(tree);
    /* Canvas subscribers run once, after layout is resolved and before the DFS,
     * so explicit role anchors are known at the exact subtree boundary where
     * they belong. Ordinary canvas items remain in their global pass below. */
    {
        struct UITreeHostRequest req = {
            .kind = UITREE_HOST_GET_ROLE_OVERLAY_GROUPS,
            .u.get_role_overlay_groups = {
                .out_groups = &role_groups,
                .out_anchor_seen = &role_anchor_seen,
            },
        };
        role_group_count = UITree_Host(host, &req);
        if( role_group_count < 0 )
            role_group_count = 0;
    }
    /* Reachability scratch for the retention signal — see UITree::emit_visited.
     * Grown to the current node count and cleared here so that what it holds
     * during the frame after this walk is exactly "entered by this walk". */
    {
        struct UITree* mut = (struct UITree*)tree;
        if( mut->emit_visited_cap < tree->component_count )
        {
            uint32_t cap = tree->component_capacity > tree->component_count
                               ? tree->component_capacity
                               : tree->component_count;
            uint8_t* grown = realloc(mut->emit_visited, cap);
            assert(grown);
            mut->emit_visited = grown;
            mut->emit_visited_cap = cap;
        }
        if( mut->emit_visited_cap > 0 )
            memset(mut->emit_visited, 0, mut->emit_visited_cap);
    }
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
        tree,
        host,
        out,
        UITREE_LAYOUT_ROOT_W,
        UITREE_LAYOUT_ROOT_H,
        hovered_component_id,
        0,
        role_groups,
        role_group_count);
    /* The second pass has nothing to draw unless a drag is running: every node it
     * reaches takes the descend-only branch. It was the single largest traversal
     * in the client (more visits than the draw pass, since descend-only bypasses
     * the collapsed-layer prune) and on an ordinary frame all of it was waste. */
    if( UITree_HasActiveDrag(tree) )
    {
        emit_walk_pass(
            tree,
            host,
            out,
            UITREE_LAYOUT_ROOT_W,
            UITREE_LAYOUT_ROOT_H,
            hovered_component_id,
            1,
            role_groups,
            role_group_count);
    }
    /* Keep an empty builtin overlay in the private working list through every
     * z-order rotation. The final scan removes it before publication, after it
     * has captured the exact slot where a later retained refresh must put it
     * back. Recording its earlier tree-order slot is insufficient on a lane
     * without a world, where the canvas overlay can still rotate ahead of it. */
    for( int i = 0; i < out->count; i++ )
    {
        struct UITreeEmitDesc const* d = &out->cmds[i];
        if( d->entity_overlay_source != UITREE_EMIT_OVERLAY_ENTITY )
            continue;
        out->volatile_overlay_seen |= (uint8_t)(1u << UITREE_EMIT_OVERLAY_ENTITY);
        out->volatile_overlay_template[UITREE_EMIT_OVERLAY_ENTITY] = *d;
    }
    /* A layout plugin's gameframe: over the scene, under the interfaces.
     * Before the hoist, which is what puts the bars and hitsplats it moves
     * BEHIND the chrome rather than over it. */
    emit_plugin_frame_pass(tree, host, out);
    emit_hoist_entity_overlays(tree, out);
    /* Plugin chrome: over the interfaces, under the pointer feedback and the
     * developer overlay. */
    emit_plugin_canvas_pass(tree, host, out);
    /* Last, so developer chrome is over everything including drag ghosts. */
    emit_debug_overlay_pass(tree, host, out);

    /* One pass over the finished list to answer "may this list be retained?".
     * See UITreeEmitBuffer.volatile_refs — the test is on the pointers, so it
     * stays correct as kinds are added. A few hundred predictable loads against
     * a walk that visits millions of nodes; it does not register in the stage. */
    out->volatile_refs = 0;
    out->volatile_desc_refs = 0;
    out->volatile_unrefreshable = 0;
    for( int i = 0; i < out->count; i++ )
    {
        struct UITreeEmitDesc const* d = &out->cmds[i];
        if( d->entity_overlay_source != UITREE_EMIT_OVERLAY_NONE )
        {
            uint8_t const bit = (uint8_t)(1u << d->entity_overlay_source);
            out->volatile_overlay_seen |= bit;
            out->volatile_overlay_template[d->entity_overlay_source] = *d;
            out->volatile_overlay_insert_at[d->entity_overlay_source] = i;
            if( d->entity_overlay_count <= 0 || !d->entity_overlays )
            {
                /* Empty descriptors are useful only as placement metadata;
                 * never expose them to renderer or golden-list consumers. */
                if( i + 1 < out->count )
                    memmove(
                        &out->cmds[i],
                        &out->cmds[i + 1],
                        (size_t)(out->count - i - 1) * sizeof(*out->cmds));
                out->count--;
                i--;
                continue;
            }
            out->volatile_overlay_nonempty |= bit;
            continue;
        }
        if( !d->minimap_dots && !d->entity_overlays && !d->worldmap_tiles && !d->debug_prims )
            continue;
        out->volatile_refs++;
        out->volatile_desc_refs++;
        /* A WORLDMAP desc does not record which of the two host requests filled
         * it (tiles vs overview), so it cannot be re-issued from the desc alone.
         * Likewise an overlay without provenance is unsafe to guess. Refusing
         * to refresh falls back to the full walk, which was always correct. */
        if( d->worldmap_tiles ||
            (d->entity_overlays &&
             d->entity_overlay_source == UITREE_EMIT_OVERLAY_NONE) )
            out->volatile_unrefreshable = 1;
    }
    for( int source = UITREE_EMIT_OVERLAY_ENTITY;
         source <= UITREE_EMIT_OVERLAY_FRAME;
         source++ )
        if( out->volatile_overlay_seen & (uint8_t)(1u << source) )
            out->volatile_refs++;
    if( role_anchor_seen )
    {
        /* Local anchors are interleaved at arbitrary subtree boundaries and a
         * retained refresh has no stable insertion table for them. A full walk
         * is the bounded, correct refresh while any subscriber asks for one. */
        out->volatile_refs++;
        out->volatile_unrefreshable = 1;
    }

    UITree_HostInputStampCapture(
        stamp_host, out->host_input_dependencies, &out->host_input_stamp);
    emit_buffer_advance_publication(out);
}

static void
emit_buffer_insert_at(
    struct UITreeEmitBuffer* out,
    int at,
    struct UITreeEmitDesc const* desc)
{
    int const old_count = out->count;
    int const source = desc->entity_overlay_source;

    assert(out);
    assert(desc);
    assert(at >= 0 && at <= old_count);
    assert(source >= UITREE_EMIT_OVERLAY_ENTITY && source <= UITREE_EMIT_OVERLAY_FRAME);
    emit_buffer_append(out, desc);
    if( at < old_count )
    {
        memmove(
            &out->cmds[at + 1],
            &out->cmds[at],
            (size_t)(old_count - at) * sizeof(*out->cmds));
        out->cmds[at] = *desc;
    }
    /* Preserve conceptual insertion slots for sources that are currently
     * empty. A strictly later slot moves with this insertion; an equal slot
     * remains before it, which preserves the order captured by the full walk
     * when two absent overlays share the same boundary. */
    for( int other = UITREE_EMIT_OVERLAY_ENTITY;
         other <= UITREE_EMIT_OVERLAY_FRAME;
         other++ )
        if( other != source && out->volatile_overlay_insert_at[other] > at )
            out->volatile_overlay_insert_at[other]++;
    out->volatile_overlay_insert_at[source] = at;
}

static void
emit_buffer_remove_at(struct UITreeEmitBuffer* out, int at)
{
    int source;

    assert(out);
    assert(at >= 0 && at < out->count);
    source = out->cmds[at].entity_overlay_source;
    assert(source >= UITREE_EMIT_OVERLAY_ENTITY && source <= UITREE_EMIT_OVERLAY_FRAME);
    if( at + 1 < out->count )
        memmove(
            &out->cmds[at],
            &out->cmds[at + 1],
            (size_t)(out->count - at - 1) * sizeof(*out->cmds));
    out->count--;
    for( int other = UITREE_EMIT_OVERLAY_ENTITY;
         other <= UITREE_EMIT_OVERLAY_FRAME;
         other++ )
        if( other != source && out->volatile_overlay_insert_at[other] > at )
            out->volatile_overlay_insert_at[other]--;
    /* The removed source still belongs immediately before the command that
     * slid into its old slot (or at end if it was last). */
    out->volatile_overlay_insert_at[source] = at;
}

static int
emit_overlay_insert_index(
    struct UITree const* tree,
    struct UITreeEmitBuffer const* out,
    enum UITreeEmitOverlaySource source)
{
    int world = -1;

    assert(tree);
    assert(out);
    if( source == UITREE_EMIT_OVERLAY_CANVAS )
    {
        int debug = -1;
        for( int i = 0; i < out->count; i++ )
        {
            int32_t const node = out->cmds[i].node_index;
            if( out->cmds[i].kind == UITREE_EMIT_DEBUG_OVERLAY && debug < 0 )
                debug = i;
            if( node >= 0 && (uint32_t)node < tree->component_count )
            {
                enum UITreeComponentType const type = tree->components[node].type;
                if( type == UIELEM_BUILTIN_MINIMENU ||
                    type == UIELEM_BUILTIN_HOVERTEXT ||
                    type == UIELEM_BUILTIN_CROSS )
                    return i;
            }
        }
        return debug >= 0 ? debug : out->count;
    }

    for( int i = 0; i < out->count; i++ )
        if( out->cmds[i].kind == UITREE_EMIT_WORLD )
            world = i;
    if( world < 0 )
        return -1;
    if( source == UITREE_EMIT_OVERLAY_ENTITY )
        return world + 1;

    /* Frame chrome belongs after host + scripted entity overlays and before
     * every interface. The full walk's hoist establishes the same block. */
    for( int at = world + 1; at < out->count; at++ )
    {
        struct UITreeEmitDesc const* d = &out->cmds[at];
        if( (d->kind == UITREE_EMIT_ENTITY_OVERLAY &&
             d->entity_overlay_source == UITREE_EMIT_OVERLAY_ENTITY) ||
            emit_is_in_entity_overlay(tree, d->node_index) )
            continue;
        return at;
    }
    return out->count;
}

int
UITree_EmitRefreshVolatile(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out)
{
    static enum UITreeEmitOverlaySource const overlay_order[] = {
        UITREE_EMIT_OVERLAY_ENTITY,
        UITREE_EMIT_OVERLAY_FRAME,
        UITREE_EMIT_OVERLAY_CANVAS,
    };
    uint32_t dirty_before;

    assert(tree);
    assert(out);
    assert(!out->volatile_unrefreshable);
    /* Invalidate any gate bound to the pre-refresh buffer even if a callback
     * aborts after changing only part of the volatile descriptor set. */
    emit_buffer_advance_publication(out);
    dirty_before = tree->dirty_gen;

    {
        struct UITreeHostRequest req = { .kind = UITREE_HOST_BEGIN_OVERLAYS };
        (void)UITree_Host(host, &req);
    }
    if( tree->dirty_gen != dirty_before )
        return 0;

    /* Discover Canvas role anchors before refreshing any disposable source.
     * A role anchor has no global descriptor to patch in place: its exact
     * target boundary is part of the DFS, so the retained frame must fall
     * back to a full walk. Asking only after FRAME/CANVAS refresh used to run
     * those plugin callbacks once here and again in the fallback, consuming
     * the per-frame draw budget and intermittently publishing a blank or
     * truncated replacement. The App host caches this preflight's Canvas
     * result across the immediate fallback walk. */
    {
        struct UITreeRoleOverlayGroup const* ignored_groups = NULL;
        int anchor_seen = 0;
        struct UITreeHostRequest anchor_req = {
            .kind = UITREE_HOST_GET_ROLE_OVERLAY_GROUPS,
            .u.get_role_overlay_groups = {
                .out_groups = &ignored_groups,
                .out_anchor_seen = &anchor_seen,
            },
        };
        (void)UITree_Host(host, &anchor_req);
        /* A draw callback may acquire or release a standing replacement even
         * without anchoring any art. That mutates native reachability, so the
         * old retained descriptor list is no longer publishable. */
        if( anchor_seen || tree->dirty_gen != dirty_before )
            return 0;
    }

    /* Reissue even sources that were empty on the full walk. Keep its host-call
     * order: ENTITY, then FRAME (resets plugin hit regions), then CANVAS (adds
     * its regions). Insert/remove commands from the returned data directly so
     * a zero crossing never dispatches plugin draw callbacks twice. */
    for( size_t oi = 0; oi < sizeof(overlay_order) / sizeof(overlay_order[0]); oi++ )
    {
        enum UITreeEmitOverlaySource const source = overlay_order[oi];
        uint8_t const bit = (uint8_t)(1u << source);
        struct UITreeEntityOverlay const* items = NULL;
        struct UITreeEmitClip clip = { 0 };
        enum UITreeHostRequestKind request_kind;
        int desc_index = -1;
        int count;
        int now_nonempty;

        if( !(out->volatile_overlay_seen & bit) )
            continue;
        /* A zero-count standing source has no descriptor in the published
         * list. Trust the metadata built by the full walk instead of scanning
         * thousands of unrelated commands three times on every retained frame. */
        if( out->volatile_overlay_nonempty & bit )
        {
            int const hint = out->volatile_overlay_insert_at[source];
            if( hint >= 0 && hint < out->count &&
                out->cmds[hint].entity_overlay_source == source )
                desc_index = hint;
            else
                for( int i = 0; i < out->count; i++ )
                    if( out->cmds[i].entity_overlay_source == source )
                    {
                        desc_index = i;
                        break;
                    }
            if( desc_index >= 0 )
            {
                out->volatile_overlay_template[source] = out->cmds[desc_index];
                out->volatile_overlay_insert_at[source] = desc_index;
            }
        }

        switch( source )
        {
        case UITREE_EMIT_OVERLAY_ENTITY:
            request_kind = UITREE_HOST_GET_ENTITY_OVERLAYS;
            break;
        case UITREE_EMIT_OVERLAY_CANVAS:
            request_kind = UITREE_HOST_GET_CANVAS_OVERLAYS;
            break;
        case UITREE_EMIT_OVERLAY_FRAME:
            request_kind = UITREE_HOST_GET_FRAME_OVERLAYS;
            break;
        default:
            assert(!"invalid volatile overlay source");
            continue;
        }
        {
            struct UITreeHostRequest req = {
                .kind = request_kind,
                .u.get_entity_overlays.out_items = &items,
                .u.get_entity_overlays.out_clip_x = &clip.x,
                .u.get_entity_overlays.out_clip_y = &clip.y,
                .u.get_entity_overlays.out_clip_w = &clip.w,
                .u.get_entity_overlays.out_clip_h = &clip.h,
            };
            count = UITree_Host(host, &req);
        }
        if( tree->dirty_gen != dirty_before )
            return 0;
        if( source == UITREE_EMIT_OVERLAY_ENTITY )
        {
            struct UITreeEmitClip const host_clip = clip;
            (void)clip_intersect(
                &clip,
                &out->volatile_overlay_enclosing_clip[source],
                host_clip.x,
                host_clip.y,
                host_clip.w,
                host_clip.h);
        }
        now_nonempty = count > 0 && items;
        if( !now_nonempty )
        {
            if( desc_index >= 0 )
                emit_buffer_remove_at(out, desc_index);
            out->volatile_overlay_nonempty &= (uint8_t)~bit;
            continue;
        }

        if( desc_index < 0 )
        {
            struct UITreeEmitDesc desc = out->volatile_overlay_template[source];
            int insert_at = emit_overlay_insert_index(tree, out, source);
            if( insert_at < 0 )
                insert_at = out->volatile_overlay_insert_at[source];
            if( insert_at < 0 )
                insert_at = out->count;
            if( insert_at > out->count )
                insert_at = out->count;
            desc.kind = UITREE_EMIT_ENTITY_OVERLAY;
            desc.entity_overlay_source = (uint8_t)source;
            desc.entity_overlays = items;
            desc.entity_overlay_count = count;
            desc.clip = clip;
            emit_buffer_insert_at(out, insert_at, &desc);
        }
        else
        {
            struct UITreeEmitDesc* d = &out->cmds[desc_index];
            d->entity_overlays = items;
            d->entity_overlay_count = count;
            d->clip = clip;
        }
        out->volatile_overlay_nonempty |= bit;
    }

    if( out->volatile_desc_refs )
        for( int i = 0; i < out->count; i++ )
        {
            struct UITreeEmitDesc* d = &out->cmds[i];

            if( d->minimap_dots )
            {
                struct UITreeHostRequest req = {
                    .kind = UITREE_HOST_GET_MINIMAP_DOTS,
                    .u.get_minimap_dots.out_dots = &d->minimap_dots,
                };
                d->minimap_dot_count = UITree_Host(host, &req);
            }
            if( d->debug_prims )
            {
                struct UITreeHostRequest req;
                req.kind = UITREE_HOST_GET_DEBUG_OVERLAY;
                req.u.get_debug_overlay.out_prims = &d->debug_prims;
                d->debug_prim_count = UITree_Host(host, &req);
            }
            if( tree->dirty_gen != dirty_before )
                return 0;
        }
    return 1;
}
