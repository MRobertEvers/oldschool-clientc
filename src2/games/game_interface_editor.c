#include "buildcache/dat2_buildcache.h"
#include "ie_enum_lookup.h"
#include "ie_struct_lookup.h"
#include "interface_editor.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_clientscript_convert.h"
#include "toriauxlib/c/toriauxlibcache_font_convert.h"
#include "toriauxlib/c/toriauxlibcache_sprite_convert.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/td/toridraw_cachemodel.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_scene.h"
#include "ui/ui_if3_layout.h"
#include "ui/ui_inv_slot_view.h"
#include "ui/uitree_build.h"
#include "ui/uitree_layout.h"
#include "vm/cs1vm_host.h"
#include "vm/cs2_host_ui.h"
#include "vm/cs2_opcode.h"
#include "vm/cs2_opcode_meta.h"
#include "vm/cs2_script.h"
#include "vm/cs2_trigger_args.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Active game during CS2 script resolution callbacks. */
static struct GameInterfaceEditor* s_ie_cs2_game;

static void
ie_runtime_store_inv_hook(
    struct GameInterfaceEditor* game,
    int target_component_id,
    int script_id,
    int argc,
    int const* argv,
    int trigger_count,
    int const* triggers);

static void
ie_runtime_clear_inv_hook(
    struct GameInterfaceEditor* game,
    int target_component_id);

static void
ie_on_inv_container_change(
    void* ud,
    int container_id);

static void
ie_push_script_error(
    struct GameInterfaceEditor* game,
    char const* fmt,
    ...)
{
    if( !game || !fmt )
        return;

    char buf[IE_SCRIPT_ERROR_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if( game->script_error_count == 0 )
        strncpy(game->script_error, buf, sizeof(game->script_error) - 1);

    if( game->script_error_count < IE_SCRIPT_ERR_MAX )
    {
        strncpy(
            game->script_errors[game->script_error_count], buf, sizeof(game->script_errors[0]) - 1);
        game->script_error_count++;
    }
}

static void
ie_on_cc_create_notify(
    void* ud,
    int parent_component_id,
    int sub_id,
    int ok)
{
    (void)parent_component_id;
    (void)sub_id;
    struct GameInterfaceEditor* game = ud;
    if( !game )
        return;
    if( ok )
        game->diag_cc_create_ok++;
    else
        game->diag_cc_create_failed++;
}

static int
ie_resolve_obj_icon_sprite(
    struct GameInterfaceEditor* game,
    int obj_id);

static bool
ie_enumerate_sprite_ids(struct GameInterfaceEditor* game);

static struct RSCacheDat2A_ConfigObject*
ie_load_object_config(
    struct GameInterfaceEditor* game,
    int obj_id);

static void
ie_sync_preview_inventory(
    struct GameInterfaceEditor* game,
    struct InterfaceEditorWidget* w);

static int
ie_widget_parent_index(
    struct GameInterfaceEditor* game,
    int widget_index);

static int const k_addable_types[] = { 0, 2, 3, 4, 5, 6, 9 };
static int const k_addable_type_count = 7;

static char const*
widget_type_label(int type)
{
    switch( type )
    {
    case 0:
        return "Layer";
    case 2:
        return "Inventory";
    case 3:
        return "Rectangle";
    case 4:
        return "Font";
    case 5:
        return "Sprite";
    case 6:
        return "Model";
    case 9:
        return "Line";
    case 11:
        return "Layer11";
    default:
        return "Unknown";
    }
}

static bool
ie_is_container_type(int type)
{
    return type == 0 || type == 11;
}

static struct InterfaceEditorWidget*
ie_find_widget(
    struct GameInterfaceEditor* game,
    int uid)
{
    for( int i = 0; i < game->widget_count; i++ )
    {
        if( game->widgets[i].uid == uid )
            return &game->widgets[i];
    }
    return NULL;
}

static int
ie_widget_index(
    struct GameInterfaceEditor* game,
    int uid)
{
    for( int i = 0; i < game->widget_count; i++ )
    {
        if( game->widgets[i].uid == uid )
            return i;
    }
    return -1;
}

static bool
ie_is_expanded(
    struct GameInterfaceEditor* game,
    int uid)
{
    for( int i = 0; i < game->expanded_count; i++ )
    {
        if( game->expanded_uids[i] == uid )
            return true;
    }
    return false;
}

static void
ie_toggle_expanded(
    struct GameInterfaceEditor* game,
    int uid)
{
    for( int i = 0; i < game->expanded_count; i++ )
    {
        if( game->expanded_uids[i] == uid )
        {
            game->expanded_uids[i] = game->expanded_uids[game->expanded_count - 1];
            game->expanded_count--;
            return;
        }
    }
    if( game->expanded_count < IE_MAX_EXPANDED )
        game->expanded_uids[game->expanded_count++] = uid;
}

static void
ie_expand_uid(
    struct GameInterfaceEditor* game,
    int uid)
{
    if( ie_is_expanded(game, uid) )
        return;
    if( game->expanded_count < IE_MAX_EXPANDED )
        game->expanded_uids[game->expanded_count++] = uid;
}

static bool
ie_widget_has_static_children(
    struct GameInterfaceEditor* game,
    int parent_uid)
{
    for( int i = 0; i < game->widget_count; i++ )
    {
        struct InterfaceEditorWidget* node = &game->widgets[i];
        int const puid = node->is_editor_added ? node->parent_uid : node->data.layer;
        if( puid == parent_uid )
            return true;
    }
    return false;
}

static int
ie_widget_depth(
    struct GameInterfaceEditor* game,
    int widget_index)
{
    int depth = 0;
    int idx = widget_index;
    while( idx >= 0 && depth < 64 )
    {
        int const parent = ie_widget_parent_index(game, idx);
        if( parent < 0 )
            break;
        depth++;
        idx = parent;
    }
    return depth;
}

static void
ie_push_draw_fill(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int w,
    int h,
    int argb)
{
    if( game->draw_count >= IE_MAX_DRAW_ITEMS )
        return;
    struct InterfaceEditorDrawItem* item = &game->draw_list[game->draw_count++];
    memset(item, 0, sizeof(*item));
    item->kind = IEDRAW_FILL_RECT;
    item->x = x;
    item->y = y;
    item->w = w;
    item->h = h;
    item->argb = argb;
}

static void
ie_push_draw_font(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int w,
    int h,
    int font_id,
    char const* text,
    int color,
    int center,
    int shadowed,
    int line_height,
    int y_align)
{
    if( !text || !text[0] || game->draw_count >= IE_MAX_DRAW_ITEMS )
        return;
    if( font_id < 0 )
        return;
    struct InterfaceEditorDrawItem* item = &game->draw_list[game->draw_count++];
    memset(item, 0, sizeof(*item));
    item->kind = IEDRAW_FONT;
    item->x = x;
    item->y = y;
    item->w = w;
    item->h = h;
    item->font_id = font_id;
    item->color = color;
    item->center = center;
    item->shadowed = shadowed;
    item->line_height = line_height;
    item->y_align = y_align;
    snprintf(item->text, sizeof(item->text), "%s", text);
}

#define ie_push_draw_font_ui(game, x, y, w, h, font_id, text, color, center, shadowed) \
    ie_push_draw_font(                                                                 \
        (game), (x), (y), (w), (h), (font_id), (text), (color), (center), (shadowed), 0, 0)

static void
ie_push_draw_sprite(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int w,
    int h,
    int element_id,
    int atlas_index,
    int alpha,
    int tiled)
{
    if( element_id < 0 || game->draw_count >= IE_MAX_DRAW_ITEMS )
        return;
    struct InterfaceEditorDrawItem* item = &game->draw_list[game->draw_count++];
    memset(item, 0, sizeof(*item));
    item->kind = IEDRAW_SPRITE;
    item->x = x;
    item->y = y;
    item->w = w;
    item->h = h;
    item->sprite_element_id = element_id;
    item->sprite_atlas_index = atlas_index;
    item->sprite_alpha = alpha;
    item->tiled = tiled;
}

static void
ie_push_hit(
    struct GameInterfaceEditor* game,
    enum InterfaceEditorHitKind kind,
    int x,
    int y,
    int w,
    int h,
    int param0,
    int param1)
{
    if( game->hit_count >= IE_MAX_HIT_ITEMS )
        return;
    struct InterfaceEditorHitItem* item = &game->hit_list[game->hit_count++];
    item->kind = kind;
    item->x = x;
    item->y = y;
    item->w = w;
    item->h = h;
    item->param0 = param0;
    item->param1 = param1;
}

static int
ie_clamp_scroll(
    int scroll,
    int content_h,
    int visible_h)
{
    int max_scroll = content_h - visible_h;
    if( max_scroll < 0 )
        max_scroll = 0;
    if( scroll < 0 )
        return 0;
    if( scroll > max_scroll )
        return max_scroll;
    return scroll;
}

struct IEPanelScrollMetrics
{
    int viewport_x;
    int viewport_y;
    int viewport_w;
    int viewport_h;
    int content_h;
    int visible_h;
};

struct IEVerticalScrollbarGeom
{
    bool visible;
    int content_w;
    int track_x;
    int track_y;
    int track_w;
    int track_h;
    int grip_y;
    int grip_h;
};

static bool
ie_panel_scroll_metrics(
    struct GameInterfaceEditor* game,
    enum InterfaceEditorScrollTarget target,
    struct IEPanelScrollMetrics* out)
{
    if( !game || !out )
        return false;

    int const content_y = IE_TOOLBAR_H;
    int const content_h = IE_WINDOW_H - IE_TOOLBAR_H;
    int const center_w = IE_WINDOW_W - IE_LEFT_PANEL_W - IE_RIGHT_PANEL_W;
    int const right_x = IE_LEFT_PANEL_W + center_w;
    int const bottom_h = content_h - content_h / 2;

    memset(out, 0, sizeof(*out));
    switch( target )
    {
    case IE_SCROLL_LIST:
        out->viewport_x = 0;
        out->viewport_y = content_y + IE_ROW_H + 4;
        out->viewport_w = IE_LEFT_PANEL_W;
        out->viewport_h = content_h - IE_ROW_H - 4;
        out->content_h = game->list_content_h;
        out->visible_h = out->viewport_h;
        return true;
    case IE_SCROLL_TREE:
        out->viewport_x = right_x;
        out->viewport_y = content_y + IE_ROW_H + 8;
        out->viewport_w = IE_RIGHT_PANEL_W;
        out->viewport_h = content_h / 2 - IE_ROW_H - 8;
        out->content_h = game->tree_content_h;
        out->visible_h = out->viewport_h;
        return true;
    case IE_SCROLL_BOTTOM:
        out->viewport_x = right_x;
        out->viewport_y = content_y + content_h / 2 + IE_ROW_H + 2;
        out->viewport_w = IE_RIGHT_PANEL_W;
        out->viewport_h = bottom_h - IE_ROW_H - 2;
        if( game->right_tab == IE_TAB_CONTAINERS )
        {
            out->content_h = game->containers_content_h;
            out->visible_h = out->viewport_h - IE_ROW_H;
        }
        else
        {
            out->content_h = game->props_content_h;
            out->visible_h = out->viewport_h;
        }
        return true;
    default:
        return false;
    }
}

static struct IEVerticalScrollbarGeom
ie_vertical_scrollbar_geom(
    int x,
    int y,
    int w,
    int h,
    int scroll,
    int content_h)
{
    struct IEVerticalScrollbarGeom geom = { .content_w = w };
    if( content_h <= h )
        return geom;

    geom.visible = true;
    geom.content_w = w - IE_SCROLLBAR_W;
    geom.track_x = x + w - IE_SCROLLBAR_W;
    geom.track_y = y;
    geom.track_w = IE_SCROLLBAR_W;
    geom.track_h = h;

    int grip_h = h * h / content_h;
    if( grip_h < IE_SCROLLBAR_MIN_GRIP_H )
        grip_h = IE_SCROLLBAR_MIN_GRIP_H;
    if( grip_h > h )
        grip_h = h;
    geom.grip_h = grip_h;

    int const max_scroll = content_h - h;
    int const travel = h - grip_h;
    if( max_scroll > 0 && travel > 0 )
        geom.grip_y = y + scroll * travel / max_scroll;
    else
        geom.grip_y = y;

    return geom;
}

static void
ie_push_vertical_scrollbar(
    struct GameInterfaceEditor* game,
    enum InterfaceEditorScrollTarget target,
    struct IEVerticalScrollbarGeom const* geom)
{
    if( !game || !geom || !geom->visible )
        return;

    ie_push_draw_fill(
        game, geom->track_x, geom->track_y, geom->track_w, geom->track_h, IE_SCROLLBAR_TRACK_ARGB);
    ie_push_draw_fill(
        game, geom->track_x, geom->grip_y, geom->track_w, geom->grip_h, IE_SCROLLBAR_GRIP_ARGB);
    ie_push_hit(
        game,
        IEHIT_SCROLLBAR_GRIP,
        geom->track_x,
        geom->grip_y,
        geom->track_w,
        geom->grip_h,
        (int)target,
        0);
}

static int
ie_scroll_for_grip_y(
    int track_y,
    int track_h,
    int grip_h,
    int mouse_y,
    int content_h,
    int visible_h)
{
    int const max_scroll = content_h - visible_h;
    if( max_scroll <= 0 )
        return 0;

    int const travel = track_h - grip_h;
    if( travel <= 0 )
        return 0;

    int grip_y = mouse_y - grip_h / 2;
    if( grip_y < track_y )
        grip_y = track_y;
    if( grip_y > track_y + travel )
        grip_y = track_y + travel;
    return (grip_y - track_y) * max_scroll / travel;
}

static void
ie_apply_scroll_to_target(
    struct GameInterfaceEditor* game,
    enum InterfaceEditorScrollTarget target,
    int delta)
{
    struct IEPanelScrollMetrics metrics;
    if( !ie_panel_scroll_metrics(game, target, &metrics) )
        return;

    switch( target )
    {
    case IE_SCROLL_LIST:
        game->list_scroll =
            ie_clamp_scroll(game->list_scroll + delta, metrics.content_h, metrics.visible_h);
        break;
    case IE_SCROLL_TREE:
        game->tree_scroll =
            ie_clamp_scroll(game->tree_scroll + delta, metrics.content_h, metrics.visible_h);
        break;
    case IE_SCROLL_BOTTOM:
        if( game->right_tab == IE_TAB_CONTAINERS )
            game->containers_scroll = ie_clamp_scroll(
                game->containers_scroll + delta, metrics.content_h, metrics.visible_h);
        else
            game->props_scroll =
                ie_clamp_scroll(game->props_scroll + delta, metrics.content_h, metrics.visible_h);
        break;
    default:
        break;
    }
}

static void
ie_set_scroll_for_target(
    struct GameInterfaceEditor* game,
    enum InterfaceEditorScrollTarget target,
    int scroll)
{
    struct IEPanelScrollMetrics metrics;
    if( !ie_panel_scroll_metrics(game, target, &metrics) )
        return;

    scroll = ie_clamp_scroll(scroll, metrics.content_h, metrics.visible_h);
    switch( target )
    {
    case IE_SCROLL_LIST:
        game->list_scroll = scroll;
        break;
    case IE_SCROLL_TREE:
        game->tree_scroll = scroll;
        break;
    case IE_SCROLL_BOTTOM:
        if( game->right_tab == IE_TAB_CONTAINERS )
            game->containers_scroll = scroll;
        else
            game->props_scroll = scroll;
        break;
    default:
        break;
    }
}

/* Keep in sync with ie_build_draw_list panel geometry. */
static enum InterfaceEditorScrollTarget
ie_scroll_target_at(
    struct GameInterfaceEditor* game,
    int mx,
    int my)
{
    (void)game;
    int const content_y = IE_TOOLBAR_H;
    int const content_h = IE_WINDOW_H - IE_TOOLBAR_H;
    int const center_w = IE_WINDOW_W - IE_LEFT_PANEL_W - IE_RIGHT_PANEL_W;
    int const right_x = IE_LEFT_PANEL_W + center_w;
    int const right_split = content_y + content_h / 2;

    if( mx >= 0 && mx < IE_LEFT_PANEL_W && my >= content_y && my < content_y + content_h )
        return IE_SCROLL_LIST;
    if( mx >= right_x && mx < right_x + IE_RIGHT_PANEL_W && my >= content_y && my < right_split )
        return IE_SCROLL_TREE;
    if( mx >= right_x && mx < right_x + IE_RIGHT_PANEL_W && my >= right_split &&
        my < content_y + content_h )
        return IE_SCROLL_BOTTOM;
    return IE_SCROLL_NONE;
}

static void
ie_apply_scroll(
    struct GameInterfaceEditor* game,
    int mx,
    int my,
    int delta)
{
    enum InterfaceEditorScrollTarget const target = ie_scroll_target_at(game, mx, my);
    if( target == IE_SCROLL_NONE )
        return;
    ie_apply_scroll_to_target(game, target, delta);
}

static void
ie_scrollbar_drag_update(
    struct GameInterfaceEditor* game,
    struct LibToriRS_Input* input)
{
    if( !game || !input )
        return;

    if( !LibToriRS_Input_IsMouseHeld(input, TORIRSM_LEFT) )
    {
        game->scrollbar_drag_active = false;
        return;
    }

    if( !game->scrollbar_drag_active )
        return;

    struct IEPanelScrollMetrics metrics;
    if( !ie_panel_scroll_metrics(game, game->scrollbar_drag_target, &metrics) )
    {
        game->scrollbar_drag_active = false;
        return;
    }

    struct IEVerticalScrollbarGeom const geom = ie_vertical_scrollbar_geom(
        metrics.viewport_x,
        metrics.viewport_y,
        metrics.viewport_w,
        metrics.viewport_h,
        0,
        metrics.content_h);
    if( !geom.visible )
    {
        game->scrollbar_drag_active = false;
        return;
    }

    int const scroll = ie_scroll_for_grip_y(
        geom.track_y,
        geom.track_h,
        geom.grip_h,
        input->curr.mouse_y,
        metrics.content_h,
        metrics.visible_h);
    ie_set_scroll_for_target(game, game->scrollbar_drag_target, scroll);
}

static int
ie_default_ui_font_id(struct GameInterfaceEditor* game)
{
    if( game && game->font_cache_count > 0 )
        return game->font_cache[0].scene_font_id;
    return -1;
}

static int
ie_resolve_scene_font(
    struct GameInterfaceEditor* game,
    int rs_font_id)
{
    if( !game || rs_font_id < 0 )
        return ie_default_ui_font_id(game);

    for( int i = 0; i < game->font_cache_count; i++ )
    {
        if( game->font_cache[i].rs_font_id == rs_font_id )
            return game->font_cache[i].scene_font_id;
    }

    if( !game->dat2_cache || !game->cache || !game->td ||
        game->font_cache_count >= IE_FONT_CACHE_MAX )
        return ie_default_ui_font_id(game);

    struct RSCacheDat2Disk_Archive* font_archive =
        RSCacheDat2Disk_ArchiveNewLoad(game->dat2_cache, RSCacheDat2Disk_Table_Fonts, rs_font_id);
    struct RSCacheDat2Disk_Archive* sprite_archive =
        RSCacheDat2Disk_ArchiveNewLoad(game->dat2_cache, RSCacheDat2Disk_Table_Sprites, rs_font_id);
    if( !font_archive || !sprite_archive )
    {
        RSCacheDat2Disk_ArchiveFree(font_archive);
        RSCacheDat2Disk_ArchiveFree(sprite_archive);
        return ie_default_ui_font_id(game);
    }

    struct ToriAuxLibCore_Font* core_font =
        ToriAuxLibCache_FontNewFromDat2Archives(font_archive, sprite_archive, rs_font_id);
    if( !core_font )
        return ie_default_ui_font_id(game);

    int scene_font_id = game->next_element_id++;
    ToriAuxLibCache_SubmitFont(game->cache, scene_font_id, core_font);
    if( !ToriAuxLibTD_Font(game->td, scene_font_id) )
        return ie_default_ui_font_id(game);

    int idx = game->font_cache_count++;
    game->font_cache[idx].rs_font_id = rs_font_id;
    game->font_cache[idx].scene_font_id = scene_font_id;
    return scene_font_id;
}

static bool
ie_resolve_sprite(
    struct GameInterfaceEditor* game,
    int cache_sprite_id,
    int* out_element_id,
    int* out_frame_count)
{
    if( !game || cache_sprite_id < 0 || !out_element_id || !out_frame_count )
        return false;

    for( int i = 0; i < game->sprite_cache_count; i++ )
    {
        if( game->sprite_cache[i].cache_sprite_id == cache_sprite_id )
        {
            *out_element_id = game->sprite_cache[i].element_id;
            *out_frame_count = game->sprite_cache[i].frame_count;
            return true;
        }
    }

    if( !game->dat2_cache || game->sprite_cache_count >= IE_SPRITE_CACHE_MAX )
        return false;

    struct RSCacheDat2Disk_Archive* archive = RSCacheDat2Disk_ArchiveNewLoad(
        game->dat2_cache, RSCacheDat2Disk_Table_Sprites, cache_sprite_id);
    if( !archive )
        return false;

    struct ToriAuxLibCore_Sprite* sprite =
        ToriAuxLibCache_SpriteNewFromDat2ArchiveId(archive, cache_sprite_id);
    if( !sprite )
        return false;

    int element_id = game->next_element_id++;
    ToriAuxLibCache_SubmitSprite(game->cache, element_id, sprite);
    ToriAuxLibTD_Sprite(game->td, element_id);

    int idx = game->sprite_cache_count++;
    game->sprite_cache[idx].cache_sprite_id = cache_sprite_id;
    game->sprite_cache[idx].element_id = element_id;
    game->sprite_cache[idx].frame_count = sprite->frame_count > 0 ? sprite->frame_count : 1;

    *out_element_id = element_id;
    *out_frame_count = game->sprite_cache[idx].frame_count;
    return true;
}

static int
ie_decode_component(
    RSCacheDat2A_Component* out,
    char* data,
    int size,
    int group_id,
    int file_index)
{
    if( !data || size <= 0 )
        return -1;
    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, size);
    RSCacheDat2A_ComponentInit(out);
    out->id = (group_id << 16) | (file_index & 0xFFFF);
    if( (unsigned char)data[0] == (unsigned char)255 )
        RSCacheDat2A_ComponentDecodeIf3(out, &buf);
    else
        RSCacheDat2A_ComponentDecodeIf1(out, &buf);
    return 0;
}

static void
ie_layout_one(
    RSCacheDat2A_Component* comp,
    int px,
    int py,
    int pw,
    int ph,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    if( !comp->if3 )
    {
        *out_x = px + comp->baseX;
        *out_y = py + comp->baseY;
        *out_w = comp->baseWidth;
        *out_h = comp->baseHeight;
        return;
    }

    int rel_x = 0;
    int rel_y = 0;
    int w = 0;
    int h = 0;
    ui_if3_dat2_component_parent_relative_layout(comp, pw, ph, &rel_x, &rel_y, &w, &h);
    *out_w = w;
    *out_h = h;
    *out_x = px + rel_x;
    *out_y = py + rel_y;
}

static void
ie_cs2_relayout_preview(struct GameInterfaceEditor* game);

static void
ie_cs2_layout_root_size(
    struct GameInterfaceEditor* game,
    int* out_w,
    int* out_h);

static void
ie_relayout(struct GameInterfaceEditor* game)
{
    bool const rerun_scripts = game->scripts_ran;
    int const n = game->widget_count;
    if( n <= 0 )
        return;

    int lay_x[IE_MAX_WIDGETS];
    int lay_y[IE_MAX_WIDGETS];
    int lay_w[IE_MAX_WIDGETS];
    int lay_h[IE_MAX_WIDGETS];
    int parent_idx[IE_MAX_WIDGETS];
    int in_tree[IE_MAX_WIDGETS];
    int depth[IE_MAX_WIDGETS];
    int root_idx = 0;

    memset(parent_idx, -1, sizeof(parent_idx));
    memset(in_tree, 0, sizeof(in_tree));
    memset(depth, 0, sizeof(depth));

    for( int i = 0; i < n; i++ )
    {
        RSCacheDat2A_Component* c = &game->widgets[i].data;
        if( game->widgets[i].is_editor_added )
        {
            parent_idx[i] = ie_widget_index(game, game->widgets[i].parent_uid);
            continue;
        }
        if( c->layer < 0 )
            continue;
        for( int j = 0; j < n; j++ )
        {
            if( game->widgets[j].data.id == c->layer )
            {
                parent_idx[i] = j;
                break;
            }
        }
        if( parent_idx[i] < 0 && root_idx >= 0 )
            parent_idx[i] = root_idx;
    }

    if( root_idx >= 0 )
        in_tree[root_idx] = 1;
    int changed = 1;
    while( changed )
    {
        changed = 0;
        for( int i = 0; i < n; i++ )
        {
            if( in_tree[i] || game->widgets[i].data.layer < 0 )
                continue;
            int p = parent_idx[i];
            if( p >= 0 && in_tree[p] )
            {
                in_tree[i] = 1;
                changed = 1;
            }
        }
    }

    for( int i = 0; i < n; i++ )
    {
        if( !in_tree[i] )
            depth[i] = n + 1;
        else
        {
            int d = 0;
            int cur = i;
            while( parent_idx[cur] >= 0 )
            {
                cur = parent_idx[cur];
                d++;
                if( d > n )
                    break;
            }
            depth[i] = d;
        }
    }

    game->draw_order_count = 0;
    for( int i = 0; i < n; i++ )
    {
        if( !in_tree[i] )
            continue;
        game->draw_order[game->draw_order_count++] = i;
    }
    for( int a = 0; a < game->draw_order_count; a++ )
    {
        for( int b = a + 1; b < game->draw_order_count; b++ )
        {
            if( depth[game->draw_order[b]] < depth[game->draw_order[a]] )
            {
                int t = game->draw_order[a];
                game->draw_order[a] = game->draw_order[b];
                game->draw_order[b] = t;
            }
        }
    }
    for( int i = 0; i < n; i++ )
    {
        if( in_tree[i] || i == root_idx )
            continue;
        if( game->widgets[i].data.layer >= 0 )
            continue;
        game->draw_order[game->draw_order_count++] = i;
    }

    int const root_x = 0;
    int const root_y = 0;
    int root_w = IE_PREVIEW_ROOT_W;
    int root_h = IE_PREVIEW_ROOT_H;
    ie_cs2_layout_root_size(game, &root_w, &root_h);

    for( int k = 0; k < game->draw_order_count; k++ )
    {
        int i = game->draw_order[k];
        int px = root_x;
        int py = root_y;
        int pw = root_w;
        int ph = root_h;
        if( parent_idx[i] >= 0 )
        {
            int p = parent_idx[i];
            px = lay_x[p];
            py = lay_y[p];
            pw = lay_w[p];
            ph = lay_h[p];
        }
        ie_layout_one(
            &game->widgets[i].data, px, py, pw, ph, &lay_x[i], &lay_y[i], &lay_w[i], &lay_h[i]);
    }

    if( root_idx >= 0 )
    {
        for( int i = 0; i < n; i++ )
        {
            if( i == root_idx || in_tree[i] || game->widgets[i].data.layer >= 0 )
                continue;
            ie_layout_one(
                &game->widgets[i].data,
                lay_x[root_idx],
                lay_y[root_idx],
                lay_w[root_idx],
                lay_h[root_idx],
                &lay_x[i],
                &lay_y[i],
                &lay_w[i],
                &lay_h[i]);
        }
    }

    for( int i = 0; i < n; i++ )
    {
        game->widgets[i].lay_x = lay_x[i];
        game->widgets[i].lay_y = lay_y[i];
        game->widgets[i].lay_w = lay_w[i];
        game->widgets[i].lay_h = lay_h[i];
    }

    if( rerun_scripts && game->cs2_tree )
        ie_cs2_relayout_preview(game);
}

/* -------------------------------------------------------------------------- */
/* CS2 script runner (ported from tools/interface161_test/cs2_runner.c)        */
/* -------------------------------------------------------------------------- */

static void
ie_cs2_layout_root_size(
    struct GameInterfaceEditor* game,
    int* out_w,
    int* out_h)
{
    int root_w = IE_PREVIEW_ROOT_W;
    int root_h = IE_PREVIEW_ROOT_H;
    if( game )
    {
        root_w = game->preview_layout_w > 0 ? game->preview_layout_w : IE_PREVIEW_ROOT_W;
        root_h = game->preview_layout_h > 0 ? game->preview_layout_h : IE_PREVIEW_ROOT_H;
    }
    if( root_w < 320 )
        root_w = 320;
    if( root_h < 240 )
        root_h = 240;
    if( out_w )
        *out_w = root_w;
    if( out_h )
        *out_h = root_h;
}

static void
ie_cs2_relayout_preview(struct GameInterfaceEditor* game)
{
    if( !game || !game->scripts_ran || !game->cs2_tree )
        return;
    int root_w = IE_PREVIEW_ROOT_W;
    int root_h = IE_PREVIEW_ROOT_H;
    ie_cs2_layout_root_size(game, &root_w, &root_h);
    uitree_layout_resolve(game->cs2_tree, 0, 0, root_w, root_h);
}

static void
ie_fill_cs1_host(
    struct GameInterfaceEditor* game,
    struct CS1Host* out)
{
    if( !out )
        return;
    memset(out, 0, sizeof(*out));
    if( game && game->cs1vm_wrap )
        cs1vm_host_fill_varp_varbit(out, game->cs1vm_wrap);
}

static void
ie_runtime_hooks_reset(struct GameInterfaceEditor* game)
{
    if( !game )
        return;
    game->runtime_inv_hook_count = 0;
    game->runtime_hook_count = 0;
    memset(game->runtime_hooks, 0, sizeof(game->runtime_hooks));
}

static void
ie_runtime_store_hook(
    struct GameInterfaceEditor* game,
    int target_component_id,
    enum InterfaceEditorSetonEvent event,
    int script_id,
    int argc,
    int const* argv,
    int trigger_count,
    int const* triggers)
{
    if( !game || target_component_id < 0 || event < 0 || event >= IE_SETON_EVENT_COUNT )
        return;

    if( script_id < 0 )
    {
        for( int i = 0; i < game->runtime_hook_count; i++ )
        {
            struct InterfaceEditorRuntimeHook* hook = &game->runtime_hooks[i];
            if( !hook->used )
                continue;
            if( hook->target_component_id != target_component_id || hook->event != event )
                continue;
            for( int j = i + 1; j < game->runtime_hook_count; j++ )
                game->runtime_hooks[j - 1] = game->runtime_hooks[j];
            game->runtime_hook_count--;
            game->runtime_hooks[game->runtime_hook_count].used = false;
            return;
        }
        return;
    }

    for( int i = 0; i < game->runtime_hook_count; i++ )
    {
        struct InterfaceEditorRuntimeHook* hook = &game->runtime_hooks[i];
        if( !hook->used )
            continue;
        if( hook->target_component_id != target_component_id || hook->event != event )
            continue;
        hook->script_id = script_id;
        hook->argc = argc > 16 ? 16 : argc;
        memcpy(hook->argv, argv, (size_t)hook->argc * sizeof(int));
        hook->trigger_count = trigger_count > 8 ? 8 : trigger_count;
        memcpy(hook->triggers, triggers, (size_t)hook->trigger_count * sizeof(int));
        return;
    }

    if( game->runtime_hook_count >= IE_RUNTIME_HOOK_MAX )
        return;

    struct InterfaceEditorRuntimeHook* hook = &game->runtime_hooks[game->runtime_hook_count++];
    memset(hook, 0, sizeof(*hook));
    hook->used = true;
    hook->target_component_id = target_component_id;
    hook->event = event;
    hook->script_id = script_id;
    hook->argc = argc > 16 ? 16 : argc;
    memcpy(hook->argv, argv, (size_t)hook->argc * sizeof(int));
    hook->trigger_count = trigger_count > 8 ? 8 : trigger_count;
    memcpy(hook->triggers, triggers, (size_t)hook->trigger_count * sizeof(int));
}

static struct InterfaceEditorRuntimeHook*
ie_runtime_find_hook(
    struct GameInterfaceEditor* game,
    int target_component_id,
    enum InterfaceEditorSetonEvent event)
{
    if( !game )
        return NULL;
    for( int i = 0; i < game->runtime_hook_count; i++ )
    {
        struct InterfaceEditorRuntimeHook* hook = &game->runtime_hooks[i];
        if( hook->used && hook->target_component_id == target_component_id && hook->event == event )
            return hook;
    }
    return NULL;
}

static bool
ie_cs2_opcode_to_seton_event(
    int opcode,
    enum InterfaceEditorSetonEvent* out_event)
{
    int idx = -1;
    if( opcode >= CS2_OP_CC_SETONCLICK && opcode <= CS2_OP_CC_SETONCLANCHANNELTRANSMIT )
        idx = opcode - CS2_OP_CC_SETONCLICK;
    else if( opcode >= CS2_OP_IF_SETONCLICK && opcode <= CS2_OP_IF_SETONCLANCHANNELTRANSMIT )
        idx = opcode - CS2_OP_IF_SETONCLICK;
    else
        return false;

    if( idx < 0 || idx >= IE_SETON_EVENT_COUNT )
        return false;
    if( out_event )
        *out_event = (enum InterfaceEditorSetonEvent)idx;
    return true;
}

static void
ie_cs2_apply_seton_trigger(
    struct GameInterfaceEditor* game,
    struct CS2_InvokeCtx* ctx,
    int target_component_id,
    enum InterfaceEditorSetonEvent event)
{
    struct CS2TriggerArgs args;
    if( !cs2_trigger_args_parse(ctx, &args) || !args.ok )
        return;
    if( args.is_clear )
        ie_runtime_store_hook(game, target_component_id, event, -1, 0, NULL, 0, NULL);
    else
        ie_runtime_store_hook(
            game,
            target_component_id,
            event,
            args.script_id,
            args.argc,
            args.argv,
            args.trigger_count,
            args.triggers);

    if( event == IE_SETON_ON_INVTRANSMIT && !args.is_clear )
        ie_runtime_store_inv_hook(
            game,
            target_component_id,
            args.script_id,
            args.argc,
            args.argv,
            args.trigger_count,
            args.triggers);
    else if( event == IE_SETON_ON_INVTRANSMIT && args.is_clear )
        ie_runtime_clear_inv_hook(game, target_component_id);
}

static void
ie_runtime_inv_hooks_reset(struct GameInterfaceEditor* game)
{
    ie_runtime_hooks_reset(game);
}

static void
ie_runtime_store_inv_hook(
    struct GameInterfaceEditor* game,
    int target_component_id,
    int script_id,
    int argc,
    int const* argv,
    int trigger_count,
    int const* triggers)
{
    if( !game || target_component_id < 0 || script_id < 0 )
        return;

    for( int i = 0; i < game->runtime_inv_hook_count; i++ )
    {
        if( game->runtime_inv_hooks[i].target_component_id != target_component_id )
            continue;
        game->runtime_inv_hooks[i].script_id = script_id;
        game->runtime_inv_hooks[i].argc = argc > 16 ? 16 : argc;
        memcpy(
            game->runtime_inv_hooks[i].argv,
            argv,
            (size_t)game->runtime_inv_hooks[i].argc * sizeof(int));
        game->runtime_inv_hooks[i].trigger_count = trigger_count > 8 ? 8 : trigger_count;
        memcpy(
            game->runtime_inv_hooks[i].triggers,
            triggers,
            (size_t)game->runtime_inv_hooks[i].trigger_count * sizeof(int));
        return;
    }

    if( game->runtime_inv_hook_count >= IE_RUNTIME_INV_HOOK_MAX )
        return;

    struct InterfaceEditorRuntimeInvHook* hook =
        &game->runtime_inv_hooks[game->runtime_inv_hook_count++];
    hook->target_component_id = target_component_id;
    hook->script_id = script_id;
    hook->argc = argc > 16 ? 16 : argc;
    memcpy(hook->argv, argv, (size_t)hook->argc * sizeof(int));
    hook->trigger_count = trigger_count > 8 ? 8 : trigger_count;
    memcpy(hook->triggers, triggers, (size_t)hook->trigger_count * sizeof(int));
}

static void
ie_runtime_clear_inv_hook(
    struct GameInterfaceEditor* game,
    int target_component_id)
{
    if( !game )
        return;
    for( int i = 0; i < game->runtime_inv_hook_count; i++ )
    {
        if( game->runtime_inv_hooks[i].target_component_id != target_component_id )
            continue;
        for( int j = i + 1; j < game->runtime_inv_hook_count; j++ )
            game->runtime_inv_hooks[j - 1] = game->runtime_inv_hooks[j];
        game->runtime_inv_hook_count--;
        return;
    }
}

static void
ie_cs2_host_invoke(
    void* ud,
    struct CS2_InvokeCtx* ctx)
{
    struct GameInterfaceEditor* game = s_ie_cs2_game;
    enum InterfaceEditorSetonEvent event = IE_SETON_ON_CLICK;
    if( ie_cs2_opcode_to_seton_event(ctx->opcode, &event) )
    {
        int target_component_id = ctx->active_component;
        if( ctx->opcode >= CS2_OP_IF_SETONCLICK &&
            ctx->opcode <= CS2_OP_IF_SETONCLANCHANNELTRANSMIT )
        {
            if( !cs2vm_host_try_pop_int(ctx, &target_component_id) )
                return;
        }
        ie_cs2_apply_seton_trigger(game, ctx, target_component_id, event);
        return;
    }

    cs2_host_ui_invoke(ud, ctx);
}

static bool
ie_runtime_hook_watches_container(
    struct InterfaceEditorRuntimeInvHook const* hook,
    int container_id)
{
    if( !hook || container_id < 0 )
        return false;
    if( hook->trigger_count <= 0 )
        return true;
    for( int i = 0; i < hook->trigger_count; i++ )
    {
        if( hook->triggers[i] == container_id )
            return true;
    }
    return false;
}

static int
ie_cs2_inv_get_obj(
    void* ud,
    int inv_id,
    int slot)
{
    struct GameInterfaceEditor* game = ud;
    struct RSInvContainer const* c =
        game ? rs_inv_container_find(&game->inv_data.store, inv_id) : NULL;
    if( !c || slot < 0 || slot >= c->slot_count )
        return -1;
    int const obj_id = c->obj_id[slot];
    return obj_id > 0 ? obj_id : -1;
}

static int
ie_cs2_inv_get_num(
    void* ud,
    int inv_id,
    int slot)
{
    struct GameInterfaceEditor* game = ud;
    struct RSInvContainer const* c =
        game ? rs_inv_container_find(&game->inv_data.store, inv_id) : NULL;
    if( !c || slot < 0 || slot >= c->slot_count )
        return 0;
    return c->obj_count[slot];
}

static int
ie_cs2_inv_total(
    void* ud,
    int inv_id,
    int item_id)
{
    struct GameInterfaceEditor* game = ud;
    struct RSInvContainer const* c =
        game ? rs_inv_container_find(&game->inv_data.store, inv_id) : NULL;
    if( !c || item_id <= 0 )
        return 0;

    int total = 0;
    for( int slot = 0; slot < c->slot_count; slot++ )
    {
        if( c->obj_id[slot] == item_id )
            total += c->obj_count[slot] > 0 ? c->obj_count[slot] : 1;
    }
    return total;
}

static int
ie_cs2_inv_size(
    void* ud,
    int inv_id)
{
    struct GameInterfaceEditor* game = ud;
    struct RSInvContainer const* c =
        game ? rs_inv_container_find(&game->inv_data.store, inv_id) : NULL;
    if( c )
        return c->slot_count;

    switch( inv_id )
    {
    case 93:
        return 28;
    case 94:
        return 14;
    case 95:
        return 800;
    case 516:
        return 40;
    case 620:
        return 500;
    default:
        return 0;
    }
}

static bool
ie_cs2_resolve_obj_icon(
    void* ud,
    int obj_id,
    int* out_scene_id,
    int* out_atlas_index)
{
    struct GameInterfaceEditor* game = ud;
    if( out_scene_id )
        *out_scene_id = -1;
    if( out_atlas_index )
        *out_atlas_index = 0;
    if( !game || obj_id <= 0 )
        return false;

    int scene_id = ie_resolve_obj_icon_sprite(game, obj_id);
    if( scene_id < 0 )
        return false;

    if( out_scene_id )
        *out_scene_id = scene_id;
    if( out_atlas_index )
        *out_atlas_index = 0;
    return true;
}

static bool
ie_cs2_cache_script(
    struct GameInterfaceEditor* game,
    int script_id)
{
    if( !game || !game->dat2_cache || script_id < 0 )
        return false;

    for( int i = 0; i < game->script_cache_count; i++ )
    {
        if( game->script_cache[i].script_id == script_id )
            return true;
    }

    if( game->script_cache_count >= IE_SCRIPT_CACHE_MAX )
    {
        if( game->cs2_trace_enabled )
        {
            fprintf(
                stderr,
                "ie_cs2: script cache full, cannot load script %d\n",
                script_id);
        }
        return false;
    }

    struct RSCacheDat2Disk_Archive* archive = RSCacheDat2Disk_ArchiveNewLoad(
        game->dat2_cache, RSCacheDat2Disk_Table_Clientscript, script_id);
    if( !archive )
    {
        if( game->cs2_trace_enabled )
        {
            fprintf(stderr, "ie_cs2: script %d not found in cache\n", script_id);
        }
        return false;
    }

    struct ToriAuxLibCore_ClientScript* loaded =
        ToriAuxLibCache_ClientScriptNewFromDat2Archive2(
            archive, script_id, game->clientscript_decode_flags);
    if( !loaded || loaded->script.op_count <= 0 )
    {
        if( game->cs2_trace_enabled )
        {
            fprintf(
                stderr,
                "ie_cs2: failed to decode script %d from cache\n",
                script_id);
        }
        if( loaded )
        {
            cs2_script_free(&loaded->script);
            free(loaded);
        }
        return false;
    }

    struct InterfaceEditorScriptEntry* entry = &game->script_cache[game->script_cache_count++];
    entry->script_id = script_id;
    entry->loaded = loaded;
    return true;
}

static bool
ie_cs2_script_arg_counts(
    void* ud,
    int script_id,
    int* out_int_args,
    int* out_str_args)
{
    (void)ud;
    struct GameInterfaceEditor* game = s_ie_cs2_game;
    if( !out_int_args || !out_str_args || script_id < 0 )
        return false;

    if( game )
    {
        for( int i = 0; i < game->script_cache_count; i++ )
        {
            if( game->script_cache[i].script_id != script_id )
                continue;
            struct CS2_Script* script = &game->script_cache[i].loaded->script;
            *out_int_args = script->int_argument_count;
            *out_str_args = script->string_argument_count;
            return true;
        }
    }

    if( !game || !game->dat2_cache )
        return false;

    struct RSCacheDat2Disk_Archive* archive = RSCacheDat2Disk_ArchiveNewLoad(
        game->dat2_cache, RSCacheDat2Disk_Table_Clientscript, script_id);
    if( !archive )
        return false;

    struct ToriAuxLibCore_ClientScript* loaded =
        ToriAuxLibCache_ClientScriptNewFromDat2Archive2(
            archive, script_id, game->clientscript_decode_flags);
    if( !loaded || loaded->script.op_count <= 0 )
    {
        if( loaded )
        {
            cs2_script_free(&loaded->script);
            free(loaded);
        }
        return false;
    }

    *out_int_args = loaded->script.int_argument_count;
    *out_str_args = loaded->script.string_argument_count;
    cs2_script_free(&loaded->script);
    free(loaded);
    return true;
}

static struct CS2_Script*
ie_cs2_resolve_script(
    void* ud,
    int script_id)
{
    (void)ud;
    struct GameInterfaceEditor* game = s_ie_cs2_game;
    if( !game )
        return NULL;

    for( int i = 0; i < game->script_cache_count; i++ )
    {
        if( game->script_cache[i].script_id == script_id )
            return &game->script_cache[i].loaded->script;
    }

    (void)ie_cs2_cache_script(game, script_id);

    for( int i = 0; i < game->script_cache_count; i++ )
    {
        if( game->script_cache[i].script_id == script_id )
            return &game->script_cache[i].loaded->script;
    }

    game->diag_gosub_unresolved++;
    ie_push_script_error(game, "unresolved gosub script %d", script_id);
    return NULL;
}

static int
ie_cs2_enum_lookup_cb(
    void* ud,
    int input_type,
    int output_type,
    int enum_id,
    int key)
{
    (void)ud;
    struct GameInterfaceEditor* game = s_ie_cs2_game;
    if( !game || !game->cache )
        return -1;
    return ie_enum_lookup(dat2(game->cache), input_type, output_type, enum_id, key);
}

static char const*
ie_cs2_enum_lookup_string_cb(
    void* ud,
    int input_type,
    int output_type,
    int enum_id,
    int key)
{
    (void)ud;
    struct GameInterfaceEditor* game = s_ie_cs2_game;
    if( !game || !game->cache )
        return NULL;
    return ie_enum_lookup_string(dat2(game->cache), input_type, output_type, enum_id, key);
}

static int
ie_cs2_enum_output_count_cb(
    void* ud,
    int enum_id)
{
    struct GameInterfaceEditor* game = ud;
    if( !game || !game->cache )
        return 0;
    return ie_enum_output_count(dat2(game->cache), enum_id);
}

static bool
ie_cs2_struct_param_cb(
    void* ud,
    int struct_id,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str)
{
    struct GameInterfaceEditor* game = ud;
    if( !game || !game->cache )
        return false;
    return ie_struct_param_lookup(
        dat2(game->cache), struct_id, param_id, out_is_string, out_int, out_str);
}

static bool
ie_cs2_run_component_hook(
    struct GameInterfaceEditor* game,
    RSCacheDat2A_Component* comp,
    ComponentScriptVar* hook,
    int hook_len)
{
    if( !game || !comp || !hook || hook_len <= 0 || hook[0].type != SCRIPT_VAR_INT )
        return true;

    int script_id = hook[0].value.i;
    if( script_id < 0 )
        return true;

    ie_cs2_cache_script(game, script_id);
    struct CS2_Script* script = ie_cs2_resolve_script(game, script_id);
    if( !script )
        return true;

    int argv[16];
    int argc = hook_len - 1;
    if( argc > 16 )
        argc = 16;
    struct CS2_ScriptArgSubstitute const sub = { .component_id = comp->id };
    for( int i = 0; i < argc; i++ )
    {
        if( hook[i + 1].type == SCRIPT_VAR_INT )
            argv[i] = cs2_script_arg_substitute_int(hook[i + 1].value.i, &sub);
        else
            argv[i] = 0;
    }

    struct CS2_RunArgs args = {
        .int_argc = argc,
        .int_argv = argc > 0 ? argv : NULL,
        .string_argc = 0,
        .string_argv = NULL,
        .event_component_id = comp->id,
    };

    if( !game->cs2vm )
        return true;

    int const rc = cs2vm_run(game->cs2vm, script, &game->cs2host, &args);
    if( rc != CS2VM_OK )
    {
        int err_opcode = -1;
        int err_pc = -1;
        int err_script_id = -1;
        cs2vm_get_error_info(game->cs2vm, &err_opcode, &err_pc, &err_script_id);
        char const* op_name = "?";
        if( err_opcode >= 0 )
        {
            const struct CS2_OpcodeMeta* meta = cs2_opcode_meta_lookup((uint16_t)err_opcode);
            if( meta && meta->name )
                op_name = meta->name;
        }
        game->diag_onload_failed++;
        ie_push_script_error(
            game,
            "script %d comp 0x%x: %s at %s pc %d",
            script_id,
            comp->id,
            cs2vm_err_name(rc),
            op_name,
            err_pc);
        return false;
    }
    return true;
}

static bool
ie_component_watches_container(
    RSCacheDat2A_Component* comp,
    int container_id)
{
    if( !comp || comp->onInvTransmitLen <= 0 )
        return false;
    if( comp->inventoryTriggersLen <= 0 )
        return true;
    for( int i = 0; i < comp->inventoryTriggersLen; i++ )
    {
        if( comp->inventoryTriggers[i] == container_id )
            return true;
    }
    return false;
}

static void
ie_cs2_run_runtime_inv_hook(
    struct GameInterfaceEditor* game,
    struct InterfaceEditorRuntimeInvHook const* hook)
{
    if( !game || !hook || hook->script_id < 0 )
        return;

    ComponentScriptVar hookv[17];
    memset(hookv, 0, sizeof(hookv));
    hookv[0].type = SCRIPT_VAR_INT;
    hookv[0].value.i = hook->script_id;
    int const hook_len = 1 + hook->argc;
    for( int i = 0; i < hook->argc; i++ )
    {
        hookv[i + 1].type = SCRIPT_VAR_INT;
        hookv[i + 1].value.i = hook->argv[i];
    }

    RSCacheDat2A_Component* comp = NULL;
    for( int i = 0; i < game->widget_count; i++ )
    {
        if( game->widgets[i].data.id == hook->target_component_id )
        {
            comp = &game->widgets[i].data;
            break;
        }
    }

    RSCacheDat2A_Component temp;
    if( !comp )
    {
        RSCacheDat2A_ComponentInit(&temp);
        temp.id = hook->target_component_id;
        comp = &temp;
    }

    ie_cs2_run_component_hook(game, comp, hookv, hook_len);
}

static RSCacheDat2A_Component*
ie_find_component_by_id(
    struct GameInterfaceEditor* game,
    int component_id)
{
    if( !game )
        return NULL;
    for( int i = 0; i < game->widget_count; i++ )
    {
        if( game->widgets[i].data.id == component_id )
            return &game->widgets[i].data;
    }
    return NULL;
}

static bool
ie_cs2_run_runtime_hook(
    struct GameInterfaceEditor* game,
    struct InterfaceEditorRuntimeHook const* hook)
{
    if( !game || !hook || !hook->used || hook->script_id < 0 )
        return true;

    ComponentScriptVar hookv[17];
    memset(hookv, 0, sizeof(hookv));
    hookv[0].type = SCRIPT_VAR_INT;
    hookv[0].value.i = hook->script_id;
    int const hook_len = 1 + hook->argc;
    for( int i = 0; i < hook->argc; i++ )
    {
        hookv[i + 1].type = SCRIPT_VAR_INT;
        hookv[i + 1].value.i = hook->argv[i];
    }

    RSCacheDat2A_Component temp;
    RSCacheDat2A_Component* comp = ie_find_component_by_id(game, hook->target_component_id);
    if( !comp )
    {
        RSCacheDat2A_ComponentInit(&temp);
        temp.id = hook->target_component_id;
        comp = &temp;
    }

    return ie_cs2_run_component_hook(game, comp, hookv, hook_len);
}

static int
ie_widget_parent_index(
    struct GameInterfaceEditor* game,
    int widget_index)
{
    if( !game || widget_index < 0 || widget_index >= game->widget_count )
        return -1;
    int const layer = game->widgets[widget_index].is_editor_added
                          ? game->widgets[widget_index].parent_uid
                          : game->widgets[widget_index].data.layer;
    if( layer < 0 )
        return -1;
    for( int j = 0; j < game->widget_count; j++ )
    {
        if( game->widgets[j].data.id == layer )
            return j;
    }
    return -1;
}

static int
ie_compare_widget_file_index(
    struct GameInterfaceEditor* game,
    int a,
    int b)
{
    int fa = game->widgets[a].file_index;
    int fb = game->widgets[b].file_index;
    if( fa < fb )
        return -1;
    if( fa > fb )
        return 1;
    return 0;
}

static void
ie_collect_widget_children(
    struct GameInterfaceEditor* game,
    int parent_index,
    int* out_indices,
    int* out_count,
    int out_max)
{
    int child_buf[IE_MAX_WIDGETS];
    int child_count = 0;

    for( int i = 0; i < game->widget_count; i++ )
    {
        if( game->widgets[i].data.type < 0 )
            continue;
        if( ie_widget_parent_index(game, i) != parent_index )
            continue;
        if( child_count < IE_MAX_WIDGETS )
            child_buf[child_count++] = i;
    }

    for( int a = 0; a < child_count; a++ )
    {
        for( int b = a + 1; b < child_count; b++ )
        {
            if( ie_compare_widget_file_index(game, child_buf[b], child_buf[a]) < 0 )
            {
                int t = child_buf[a];
                child_buf[a] = child_buf[b];
                child_buf[b] = t;
            }
        }
    }

    *out_count = 0;
    for( int i = child_count - 1; i >= 0 && *out_count < out_max; i-- )
        out_indices[(*out_count)++] = child_buf[i];
}

static void
ie_trigger_onload_dfs(struct GameInterfaceEditor* game)
{
    if( !game || game->widget_count <= 0 )
        return;

    int roots[IE_MAX_WIDGETS];
    int root_count = 0;
    for( int i = 0; i < game->widget_count; i++ )
    {
        if( game->widgets[i].data.type < 0 )
            continue;
        if( ie_widget_parent_index(game, i) < 0 )
            roots[root_count++] = i;
    }

    int stack[IE_MAX_WIDGETS];
    int sp = 0;
    for( int r = 0; r < root_count; r++ )
        stack[sp++] = roots[r];

    while( sp > 0 )
    {
        int const wi = stack[--sp];
        RSCacheDat2A_Component* comp = &game->widgets[wi].data;

        if( comp->onLoadLen > 0 )
        {
            game->diag_onload_run++;
            (void)ie_cs2_run_component_hook(game, comp, comp->onLoad, comp->onLoadLen);
        }

        int children[IE_MAX_WIDGETS];
        int child_count = 0;
        ie_collect_widget_children(game, wi, children, &child_count, IE_MAX_WIDGETS);
        for( int c = 0; c < child_count; c++ )
            stack[sp++] = children[c];
    }
}

static void
ie_trigger_inv_transmit_initial(struct GameInterfaceEditor* game)
{
    if( !game )
        return;

    for( int h = 0; h < game->runtime_inv_hook_count; h++ )
    {
        struct InterfaceEditorRuntimeInvHook const* hook = &game->runtime_inv_hooks[h];
        if( hook->trigger_count <= 0 )
            continue;
        ie_cs2_run_runtime_inv_hook(game, hook);
    }

    for( int i = 0; i < game->widget_count; i++ )
    {
        RSCacheDat2A_Component* comp = &game->widgets[i].data;
        if( comp->type < 0 )
            continue;

        struct InterfaceEditorRuntimeHook* runtime_hook =
            ie_runtime_find_hook(game, comp->id, IE_SETON_ON_INVTRANSMIT);
        if( runtime_hook )
        {
            if( runtime_hook->trigger_count > 0 )
                (void)ie_cs2_run_runtime_hook(game, runtime_hook);
            continue;
        }

        if( comp->onInvTransmitLen <= 0 )
            continue;
        if( comp->inventoryTriggersLen <= 0 )
            continue;
        (void)ie_cs2_run_component_hook(game, comp, comp->onInvTransmit, comp->onInvTransmitLen);
    }
}

static void
ie_cs2_run_script_pass(
    struct GameInterfaceEditor* game,
    bool fire_inv_transmit)
{
    if( !game || !game->cs2_tree )
        return;

    int root_w = IE_PREVIEW_ROOT_W;
    int root_h = IE_PREVIEW_ROOT_H;
    ie_cs2_layout_root_size(game, &root_w, &root_h);

    if( !fire_inv_transmit )
        uitree_layout_resolve(game->cs2_tree, 0, 0, root_w, root_h);

    ie_trigger_onload_dfs(game);
    uitree_layout_resolve(game->cs2_tree, 0, 0, root_w, root_h);
    if( fire_inv_transmit )
    {
        ie_trigger_inv_transmit_initial(game);
        uitree_layout_resolve(game->cs2_tree, 0, 0, root_w, root_h);
    }
}

static bool
ie_component_watches_any_changed_inv(
    RSCacheDat2A_Component* comp,
    struct InterfaceEditorTransmitCycles const* cycles)
{
    if( !comp || comp->onInvTransmitLen <= 0 || !cycles )
        return false;
    if( comp->inventoryTriggersLen <= 0 )
        return cycles->transmit_dirty;

    int const start = cycles->changed_inv_count > IE_TRANSMIT_RING_SIZE
                          ? cycles->changed_inv_count - IE_TRANSMIT_RING_SIZE
                          : 0;
    for( int i = cycles->changed_inv_count - 1; i >= start; i-- )
    {
        int const inv_id = cycles->changed_inv_buffer[i & (IE_TRANSMIT_RING_SIZE - 1)];
        if( ie_component_watches_container(comp, inv_id) )
            return true;
    }
    return false;
}

static void
ie_mark_var_transmit(
    struct GameInterfaceEditor* game,
    int varp_id)
{
    if( !game || varp_id < 0 )
        return;

    struct InterfaceEditorTransmitCycles* cycles = &game->transmit_cycles;
    cycles->transmit_dirty = true;
    cycles->changed_varp_buffer[cycles->changed_varp_count & (IE_TRANSMIT_RING_SIZE - 1)] = varp_id;
    cycles->changed_varp_count++;
}

static void
ie_on_varp_change(
    void* ud,
    int varp_id)
{
    ie_mark_var_transmit(ud, varp_id);
}

static void
ie_mark_inv_transmit(
    struct GameInterfaceEditor* game,
    int container_id)
{
    if( !game || container_id < 0 )
        return;

    struct InterfaceEditorTransmitCycles* cycles = &game->transmit_cycles;
    cycles->transmit_dirty = true;
    cycles->changed_inv_buffer[cycles->changed_inv_count & (IE_TRANSMIT_RING_SIZE - 1)] =
        container_id;
    cycles->changed_inv_count++;
}

static void
ie_on_inv_container_change(
    void* ud,
    int container_id)
{
    ie_mark_inv_transmit(ud, container_id);
}

static bool
ie_runtime_hook_inv_changed(
    struct InterfaceEditorRuntimeHook const* hook,
    struct InterfaceEditorTransmitCycles const* cycles)
{
    if( !hook || !cycles || hook->trigger_count <= 0 )
        return false;

    int const start = cycles->changed_inv_count > IE_TRANSMIT_RING_SIZE
                          ? cycles->changed_inv_count - IE_TRANSMIT_RING_SIZE
                          : 0;
    for( int i = cycles->changed_inv_count - 1; i >= start; i-- )
    {
        int const inv_id = cycles->changed_inv_buffer[i & (IE_TRANSMIT_RING_SIZE - 1)];
        for( int t = 0; t < hook->trigger_count; t++ )
        {
            if( hook->triggers[t] == inv_id )
                return true;
        }
    }
    return false;
}

static void
ie_process_widget_timers(struct GameInterfaceEditor* game)
{
    if( !game || !game->scripts_ran )
        return;

    for( int i = 0; i < game->widget_count; i++ )
    {
        RSCacheDat2A_Component* comp = &game->widgets[i].data;
        if( comp->type < 0 || comp->hidden )
            continue;

        struct InterfaceEditorRuntimeHook* hook =
            ie_runtime_find_hook(game, comp->id, IE_SETON_ON_TIMER);
        if( hook )
        {
            (void)ie_cs2_run_runtime_hook(game, hook);
            continue;
        }
        if( comp->onTimerLen > 0 )
            (void)ie_cs2_run_component_hook(game, comp, comp->onTimer, comp->onTimerLen);
    }
}

static void
ie_process_widget_transmits(struct GameInterfaceEditor* game)
{
    if( !game || !game->scripts_ran )
        return;
    if( !game->transmit_cycles.transmit_dirty && !game->transmit_cycles.widgets_loaded_dirty )
        return;

    char queued[IE_RUNTIME_HOOK_MAX][64];
    int queued_count = 0;

    for( int i = 0; i < game->runtime_hook_count; i++ )
    {
        struct InterfaceEditorRuntimeHook* hook = &game->runtime_hooks[i];
        if( !hook->used || hook->event != IE_SETON_ON_INVTRANSMIT )
            continue;
        if( hook->trigger_count > 0 && !ie_runtime_hook_inv_changed(hook, &game->transmit_cycles) )
            continue;

        char key[64];
        snprintf(key, sizeof(key), "%d:inv", hook->target_component_id);
        bool dup = false;
        for( int q = 0; q < queued_count; q++ )
        {
            if( strcmp(queued[q], key) == 0 )
            {
                dup = true;
                break;
            }
        }
        if( dup )
            continue;
        if( queued_count < IE_RUNTIME_HOOK_MAX )
            strncpy(queued[queued_count++], key, sizeof(queued[0]) - 1);

        (void)ie_cs2_run_runtime_hook(game, hook);
    }

    for( int i = 0; i < game->widget_count; i++ )
    {
        RSCacheDat2A_Component* comp = &game->widgets[i].data;
        if( comp->type < 0 || comp->onInvTransmitLen <= 0 )
            continue;
        if( !ie_component_watches_any_changed_inv(comp, &game->transmit_cycles) )
            continue;

        char key[64];
        snprintf(key, sizeof(key), "%d:cache_inv", comp->id);
        bool dup = false;
        for( int q = 0; q < queued_count; q++ )
        {
            if( strcmp(queued[q], key) == 0 )
            {
                dup = true;
                break;
            }
        }
        if( dup )
            continue;
        if( queued_count < IE_RUNTIME_HOOK_MAX )
            strncpy(queued[queued_count++], key, sizeof(queued[0]) - 1);

        (void)ie_cs2_run_component_hook(game, comp, comp->onInvTransmit, comp->onInvTransmitLen);
    }

    for( int i = 0; i < game->widget_count; i++ )
    {
        RSCacheDat2A_Component* comp = &game->widgets[i].data;
        if( comp->type < 0 || comp->onVarpTransmitLen <= 0 )
            continue;

        bool should_fire = false;
        if( comp->varpTriggersLen <= 0 )
            should_fire = game->transmit_cycles.transmit_dirty;
        else
        {
            int const start = game->transmit_cycles.changed_varp_count > IE_TRANSMIT_RING_SIZE
                                  ? game->transmit_cycles.changed_varp_count - IE_TRANSMIT_RING_SIZE
                                  : 0;
            for( int v = game->transmit_cycles.changed_varp_count - 1; v >= start; v-- )
            {
                int const varp_id =
                    game->transmit_cycles.changed_varp_buffer[v & (IE_TRANSMIT_RING_SIZE - 1)];
                for( int t = 0; t < comp->varpTriggersLen; t++ )
                {
                    if( comp->varpTriggers[t] == varp_id )
                    {
                        should_fire = true;
                        break;
                    }
                }
                if( should_fire )
                    break;
            }
        }
        if( !should_fire )
            continue;

        (void)ie_cs2_run_component_hook(game, comp, comp->onVarpTransmit, comp->onVarpTransmitLen);
    }

    game->transmit_cycles.last_transmit_process_cycle = game->transmit_cycles.cycle_cntr;
    game->transmit_cycles.transmit_dirty = false;
    game->transmit_cycles.widgets_loaded_dirty = false;
    ie_cs2_relayout_preview(game);
}

static void
ie_script_tick(struct GameInterfaceEditor* game)
{
    if( !game || !game->scripts_ran )
        return;
    game->transmit_cycles.cycle_cntr++;
    cs2_host_ui_tick(&game->cs2host);
    ie_process_widget_timers(game);
    ie_process_widget_transmits(game);
}

static void
ie_handle_canvas_click_scripts(
    struct GameInterfaceEditor* game,
    int component_uid,
    int click_x,
    int click_y)
{
    if( !game || !game->scripts_ran || component_uid < 0 )
        return;

    RSCacheDat2A_Component* comp = NULL;
    for( int i = 0; i < game->widget_count; i++ )
    {
        if( game->widgets[i].uid == component_uid )
        {
            comp = &game->widgets[i].data;
            break;
        }
    }
    if( !comp )
        return;

    (void)click_x;
    (void)click_y;

    struct InterfaceEditorRuntimeHook* on_click =
        ie_runtime_find_hook(game, comp->id, IE_SETON_ON_CLICK);
    if( on_click )
        (void)ie_cs2_run_runtime_hook(game, on_click);
    else if( comp->onClickLen > 0 )
        (void)ie_cs2_run_component_hook(game, comp, comp->onClick, comp->onClickLen);
    else
    {
        struct InterfaceEditorRuntimeHook* on_op =
            ie_runtime_find_hook(game, comp->id, IE_SETON_ON_OP);
        if( on_op )
            (void)ie_cs2_run_runtime_hook(game, on_op);
        else if( comp->onOpLen > 0 )
            (void)ie_cs2_run_component_hook(game, comp, comp->onOp, comp->onOpLen);
    }

    ie_cs2_relayout_preview(game);
}

static void
ie_get_drag_params_for_uid(
    struct GameInterfaceEditor* game,
    int component_uid,
    uint8_t* out_zone,
    uint8_t* out_time)
{
    uint8_t zone = 0;
    uint8_t time = 0;
    bool found = false;
    if( game && game->cs2_tree )
    {
        int32_t idx = uitree_find_by_component_id(game->cs2_tree, component_uid);
        if( idx >= 0 )
        {
            zone = game->cs2_tree->components[idx].drag_dead_zone;
            time = game->cs2_tree->components[idx].drag_dead_time;
            found = true;
        }
    }
    if( !found )
    {
        struct InterfaceEditorWidget* w = ie_find_widget(game, component_uid);
        if( w )
        {
            zone = w->data.dragDeadZone;
            time = w->data.dragDeadTime;
        }
    }
    if( out_zone )
        *out_zone = zone;
    if( out_time )
        *out_time = time;
}

static void
ie_restore_drag_thresholds(
    struct GameInterfaceEditor* game,
    struct LibToriRS_Input* input)
{
    if( !game || !input )
        return;
    LibToriRS_Input_SetDragThresholds(input, game->saved_drag_zone_px, game->saved_drag_time_ms);
    game->canvas_drag_uid = -1;
}

static void
ie_begin_canvas_drag_press(
    struct GameInterfaceEditor* game,
    struct LibToriRS_Input* input,
    int component_uid)
{
    if( !game || !input || component_uid < 0 )
        return;

    uint8_t zone = 0;
    uint8_t time = 0;
    ie_get_drag_params_for_uid(game, component_uid, &zone, &time);

    game->saved_drag_zone_px = input->drag_deadzone_pixels;
    game->saved_drag_time_ms = input->drag_dead_time_ms;
    game->canvas_drag_uid = component_uid;

    uint64_t const zone_px = zone > 0 ? (uint64_t)zone : LIBTORIRS_INPUT_DEFAULT_DRAG_DEADZONE_PX;
    uint64_t const time_ms = (uint64_t)time * 20u;
    LibToriRS_Input_SetDragThresholds(input, zone_px, time_ms);
}

static void
ie_handle_canvas_drag_scripts(
    struct GameInterfaceEditor* game,
    int component_uid,
    int mouse_x,
    int mouse_y,
    bool drag_complete)
{
    if( !game || !game->scripts_ran || component_uid < 0 )
        return;

    RSCacheDat2A_Component* comp = NULL;
    for( int i = 0; i < game->widget_count; i++ )
    {
        if( game->widgets[i].uid == component_uid )
        {
            comp = &game->widgets[i].data;
            break;
        }
    }
    if( !comp )
        return;

    (void)mouse_x;
    (void)mouse_y;

    enum InterfaceEditorSetonEvent const event =
        drag_complete ? IE_SETON_ON_DRAGCOMPLETE : IE_SETON_ON_DRAG;
    struct InterfaceEditorRuntimeHook* runtime_hook = ie_runtime_find_hook(game, comp->id, event);
    if( runtime_hook )
        (void)ie_cs2_run_runtime_hook(game, runtime_hook);
    else if( !drag_complete && comp->onDragLen > 0 )
        (void)ie_cs2_run_component_hook(game, comp, comp->onDrag, comp->onDragLen);
    else if( drag_complete && comp->onDragCompleteLen > 0 )
        (void)ie_cs2_run_component_hook(game, comp, comp->onDragComplete, comp->onDragCompleteLen);
}

struct IeCs2BuildContext
{
    struct GameInterfaceEditor* game;
    struct ToriAuxLibCore_Component** core_comps;
};

static struct ToriAuxLibCore_Component*
ie_cs2_build_get_component(
    void* ud,
    int index)
{
    struct IeCs2BuildContext* ctx = ud;
    if( !ctx || !ctx->game || index < 0 || index >= ctx->game->widget_count )
        return NULL;
    return ctx->core_comps[index];
}

static int
ie_cs2_build_get_parent_id(
    void* ud,
    int index)
{
    struct IeCs2BuildContext* ctx = ud;
    if( !ctx || !ctx->game || index < 0 || index >= ctx->game->widget_count )
        return -1;
    return ctx->game->widgets[index].is_editor_added ? ctx->game->widgets[index].parent_uid
                                                     : ctx->game->widgets[index].data.layer;
}

static int
ie_cs2_resolve_sprite_for_build(
    void* ud,
    int graphic_id)
{
    struct GameInterfaceEditor* game = ud;
    int element_id = -1;
    int frame_count = 0;
    if( !game || !ie_resolve_sprite(game, graphic_id, &element_id, &frame_count) )
        return -1;
    return element_id;
}

static int
ie_cs2_build_tree(struct GameInterfaceEditor* game)
{
    if( !game || !game->cs2_tree )
        return -1;

    int const n = game->widget_count;
    struct ToriAuxLibCore_Component** core_comps = calloc((size_t)n, sizeof(*core_comps));
    if( !core_comps )
        return -1;

    for( int i = 0; i < n; i++ )
    {
        RSCacheDat2A_Component const* src = &game->widgets[i].data;
        if( src->type < 0 )
            continue;
        core_comps[i] = ToriAuxLibCache_ComponentNewFromCacheDat2Component(src);
    }

    struct IeCs2BuildContext build_ctx = { .game = game, .core_comps = core_comps };
    struct UITreeBuildSource src = {
        .count = n,
        .get_component = ie_cs2_build_get_component,
        .get_parent_id = ie_cs2_build_get_parent_id,
        .resolve_sprite = ie_cs2_resolve_sprite_for_build,
        .ud = &build_ctx,
    };
    int const rc = uitree_build_from_source(game->cs2_tree, &src);

    for( int i = 0; i < n; i++ )
        ToriAuxLibCore_ComponentFree(core_comps[i]);
    free(core_comps);
    return rc;
}

static struct RSCacheDat2A_ConfigObject*
ie_load_object_config(
    struct GameInterfaceEditor* game,
    int obj_id)
{
    if( !game || obj_id < 0 || !game->cache )
        return NULL;

    struct Dat2BuildCache* bc = dat2(game->cache);
    if( !bc )
        return NULL;

    struct RSCacheDat2A_ConfigObject* cached = dat2_buildcache_object_get(bc, obj_id);
    if( cached )
        return cached;

    struct RSCacheDat2Disk* cache = game->dat2_cache;
    if( !cache )
        return NULL;

    struct RSCacheDat2Disk_Archive* arch = RSCacheDat2Disk_ArchiveNewLoad(
        cache, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Object);
    if( !arch )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(cache, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    struct RSCacheDat2Disk_ReferenceTable* table = cache->tables[RSCacheDat2Disk_Table_Configs];
    struct RSCacheDat2Disk_ArchiveReference* ref = NULL;
    if( table && table->archives )
        ref = &table->archives[RSCacheDat2A_ConfigKind_Object];

    struct RSCacheDat2A_ConfigObject* out = NULL;
    for( int i = 0; i < fl->file_count; i++ )
    {
        int id = (ref && i < ref->children.count) ? ref->children.files[i].id : i;
        if( id != obj_id )
            continue;

        out = calloc(1, sizeof(struct RSCacheDat2A_ConfigObject));
        if( !out )
            break;

        RSCacheDat2A_ConfigObjectDecodeInplace(out, fl->files[i], fl->file_sizes[i]);
        out->_id = obj_id;
        break;
    }

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);

    if( out )
        dat2_buildcache_object_add(bc, obj_id, out);

    return out;
}

static int
ie_resolve_obj_icon_sprite(
    struct GameInterfaceEditor* game,
    int obj_id)
{
    if( !game || obj_id <= 0 )
        return -1;

    for( int i = 0; i < game->obj_icon_cache_count; i++ )
    {
        if( game->obj_icon_cache[i].obj_id == obj_id )
            return game->obj_icon_cache[i].element_id;
    }

    if( !game->dat2_cache || !game->scene || !game->cache || !game->td )
        return -1;
    if( game->obj_icon_cache_count >= IE_OBJ_ICON_CACHE_MAX )
        return -1;

    struct RSCacheDat2A_ConfigObject* obj = ie_load_object_config(game, obj_id);
    if( !obj || obj->inventory_model_id <= 0 )
        return -1;

    int const inventory_model_id = obj->inventory_model_id;
    int const zoom2d = obj->zoom2d;
    int const xan2d = obj->xan2d;
    int const yan2d = obj->yan2d;
    int const contrast = obj->contrast;
    int const ambient = obj->ambient;
    int const recolor_count = obj->recolor_count;
    int* recolors_from = NULL;
    int* recolors_to = NULL;
    if( recolor_count > 0 )
    {
        recolors_from = calloc((size_t)recolor_count, sizeof(int));
        recolors_to = calloc((size_t)recolor_count, sizeof(int));
        if( recolors_from && recolors_to )
        {
            memcpy(recolors_from, obj->recolors_from, (size_t)recolor_count * sizeof(int));
            memcpy(recolors_to, obj->recolors_to, (size_t)recolor_count * sizeof(int));
        }
    }

    struct RSCacheDat2A_Model* raw =
        RSCacheDat2A_ModelNewFromCache(game->dat2_cache, inventory_model_id);
    if( !raw )
    {
        free(recolors_from);
        free(recolors_to);
        return -1;
    }

    struct RSCacheDat2A_Model* copy = RSCacheDat2A_ModelNewCopy(raw);
    RSCacheDat2A_ModelFree(raw);
    if( !copy )
    {
        free(recolors_from);
        free(recolors_to);
        return -1;
    }

    if( copy->face_colors && recolor_count > 0 && recolors_from && recolors_to )
    {
        for( int i = 0; i < recolor_count; i++ )
        {
            int color_src = recolors_from[i];
            int color_dst = recolors_to[i];
            for( int f = 0; f < copy->face_count; f++ )
            {
                if( copy->face_colors[f] == (uint16_t)color_src )
                    copy->face_colors[f] = (uint16_t)color_dst;
            }
        }
    }
    free(recolors_from);
    free(recolors_to);

    struct ToriDraw_Model* td_model = ToriDraw_ModelNewFromCacheModel(copy);
    RSCacheDat2A_ModelFree(copy);
    if( !td_model )
        return -1;

    ToriDraw_ModelSetBoundsCylinder(td_model);

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };
    ToriDraw_LightModelDefault(hnd, contrast, ambient);

    int zoom = zoom2d;
    if( zoom == 0 )
        zoom = 2000;

    struct ToriDraw_Sprite* td_sprite =
        ToriDraw_SpriteNewFromModelRaster(game->scene, hnd, zoom, xan2d, yan2d, 32, 32, true);
    if( !td_sprite )
        return -1;

    struct ToriAuxLibCore_Sprite* sprite = calloc(1, sizeof(struct ToriAuxLibCore_Sprite));
    if( !sprite )
    {
        ToriDraw_SpriteFree(td_sprite);
        return -1;
    }

    sprite->frame_count = 1;
    sprite->frames = ToriAuxLibCache_SpriteFrameNewFromToriDrawByMove(td_sprite);
    if( !sprite->frames )
    {
        free(sprite);
        return -1;
    }

    int element_id = game->next_element_id++;
    ToriAuxLibCache_SubmitSprite(game->cache, element_id, sprite);
    ToriAuxLibTD_Sprite(game->td, element_id);

    int idx = game->obj_icon_cache_count++;
    game->obj_icon_cache[idx].obj_id = obj_id;
    game->obj_icon_cache[idx].element_id = element_id;
    return element_id;
}

static void
ie_set_container_slot(
    struct GameInterfaceEditor* game,
    int source_id,
    int slot,
    int obj_id,
    int obj_count)
{
    if( !game || source_id < 0 || slot < 0 )
        return;

    int scene_id = -1;
    int atlas_index = 0;
    if( obj_id > 0 )
        scene_id = ie_resolve_obj_icon_sprite(game, obj_id);

    struct UIInvSlotData data = {
        .obj_id = obj_id > 0 ? obj_id : 0,
        .obj_count = obj_count > 0 ? obj_count : 1,
        .scene_id = scene_id,
        .atlas_index = atlas_index,
    };
    (void)ui_inv_data_service_set_slot(&game->inv_data, source_id, slot, &data);
}

void
GameInterfaceEditor_FreeScripts(struct GameInterfaceEditor* game)
{
    if( !game )
        return;

    if( game->cs2_tree )
    {
        uitree_free(game->cs2_tree);
        game->cs2_tree = NULL;
    }
    if( game->cs2vm )
    {
        cs2vm_free(game->cs2vm);
        game->cs2vm = NULL;
    }

    for( int i = 0; i < game->script_cache_count; i++ )
    {
        if( game->script_cache[i].loaded )
        {
            cs2_script_free(&game->script_cache[i].loaded->script);
            free(game->script_cache[i].loaded);
        }
    }
    game->script_cache_count = 0;
    ie_runtime_hooks_reset(game);
    game->scripts_ran = false;
    memset(&game->transmit_cycles, 0, sizeof(game->transmit_cycles));
    s_ie_cs2_game = NULL;
}

void
GameInterfaceEditor_RunScripts(struct GameInterfaceEditor* game)
{
    if( !game || !game->dat2_cache || game->widget_count <= 0 )
        return;

    GameInterfaceEditor_FreeScripts(game);
    game->script_error[0] = '\0';
    game->script_error_count = 0;
    game->diag_onload_run = 0;
    game->diag_onload_failed = 0;
    game->diag_gosub_unresolved = 0;
    game->diag_cc_create_ok = 0;
    game->diag_cc_create_failed = 0;
    ie_runtime_hooks_reset(game);
    memset(&game->transmit_cycles, 0, sizeof(game->transmit_cycles));
    game->transmit_cycles.widgets_loaded_dirty = true;

    game->cs2_tree = uitree_new(64);
    game->cs2vm = cs2vm_new();
    if( !game->cs2_tree || !game->cs2vm )
    {
        GameInterfaceEditor_FreeScripts(game);
        snprintf(game->script_error, sizeof(game->script_error), "failed to allocate CS2 tree/vm");
        return;
    }

    s_ie_cs2_game = game;
    if( ie_cs2_build_tree(game) != 0 )
    {
        GameInterfaceEditor_FreeScripts(game);
        snprintf(game->script_error, sizeof(game->script_error), "failed to build UITree");
        return;
    }

    s_ie_cs2_game = game;
    int root_w = IE_PREVIEW_ROOT_W;
    int root_h = IE_PREVIEW_ROOT_H;
    ie_cs2_layout_root_size(game, &root_w, &root_h);
    struct CS2HostUIInitArgs args = {
        .core = game->core,
        .cache = game->cache,
        .vm = game->cs1vm_wrap,
        .tree = game->cs2_tree,
        .resolve_obj_icon = ie_cs2_resolve_obj_icon,
        .resolve_obj_icon_ud = game,
        .inv_get_obj = ie_cs2_inv_get_obj,
        .inv_get_num = ie_cs2_inv_get_num,
        .inv_size = ie_cs2_inv_size,
        .inv_ud = game,
        .enum_output_count = ie_cs2_enum_output_count_cb,
        .enum_output_count_ud = game,
        .struct_param = ie_cs2_struct_param_cb,
        .struct_param_ud = game,
        .scene = game->scene,
        .on_varp_change = ie_on_varp_change,
        .on_varp_change_ud = game,
        .cc_create_notify = ie_on_cc_create_notify,
        .cc_create_notify_ud = game,
        .viewport_w = root_w,
        .viewport_h = root_h,
        .window_mode = 1,
    };
    cs2_host_ui_init(&game->cs2host, &args);
    game->cs2host.resolve_script = ie_cs2_resolve_script;
    game->cs2host.script_arg_counts = ie_cs2_script_arg_counts;
    game->cs2host.enum_lookup = ie_cs2_enum_lookup_cb;
    game->cs2host.enum_lookup_string = ie_cs2_enum_lookup_string_cb;
    game->cs2host.invoke = ie_cs2_host_invoke;

    cs2vm_set_trace(game->cs2_trace_enabled);

    ie_cs2_run_script_pass(game, false);
    ie_cs2_run_script_pass(game, true);

    game->scripts_ran = true;
    game->transmit_cycles.widgets_loaded_dirty = false;

    if( game->cs2_trace_enabled )
    {
        int const dynamic_count = GameInterfaceEditor_CountDynamicCs2Children(game);
        fprintf(
            stderr,
            "ie_cs2: iface %d onload_run=%d onload_failed=%d gosub_unresolved=%d "
            "cc_create_ok=%d cc_create_failed=%d dynamic_children=%d script_errors=%d\n",
            game->loaded_group_id,
            game->diag_onload_run,
            game->diag_onload_failed,
            game->diag_gosub_unresolved,
            game->diag_cc_create_ok,
            game->diag_cc_create_failed,
            dynamic_count,
            game->script_error_count);
        GameInterfaceEditor_PrintDynamicCs2Children(game);
        for( int i = 0; i < game->script_error_count; i++ )
            fprintf(stderr, "ie_cs2 error[%d]: %s\n", i, game->script_errors[i]);
    }
}

static int
ie_tree_node_obj_id(struct StaticUIComponent const* node)
{
    if( node->item_id > 0 )
        return node->item_id;
    if( node->type == UIELEM_CC_OBJ && node->u.cc_obj.obj_id > 0 )
        return node->u.cc_obj.obj_id;
    return 0;
}

static void
ie_free_widgets(struct GameInterfaceEditor* game)
{
    for( int i = 0; i < game->widget_count; i++ )
        RSCacheDat2A_ComponentFree(&game->widgets[i].data);
    game->widget_count = 0;
    game->draw_order_count = 0;
}

static bool
ie_load_group(
    struct GameInterfaceEditor* game,
    int group_id)
{
    if( !game || !game->dat2_cache )
        return false;

    ie_free_widgets(game);
    game->loaded_group_id = group_id;
    game->selected_uid = -1;
    game->next_dynamic_child = 0x8000;

    struct RSCacheDat2Disk_Archive* arch = RSCacheDat2Disk_ArchiveNewLoad(
        game->dat2_cache, RSCacheDat2Disk_Table_Interfaces, group_id);
    if( !arch )
        return false;

    RSCacheDat2Disk_ArchiveInitMetadata(game->dat2_cache, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    RSCacheDat2Disk_ArchiveFree(arch);
    if( !fl )
        return false;

    for( int fi = 0; fi < fl->file_count && game->widget_count < IE_MAX_WIDGETS; fi++ )
    {
        struct InterfaceEditorWidget* w = &game->widgets[game->widget_count];
        if( ie_decode_component(&w->data, fl->files[fi], fl->file_sizes[fi], group_id, fi) != 0 )
        {
            RSCacheDat2A_ComponentInit(&w->data);
            w->data.id = (group_id << 16) | (fi & 0xFFFF);
        }
        w->uid = w->data.id;
        w->parent_uid = w->data.layer;
        w->file_index = fi;
        w->is_editor_added = false;
        game->widget_count++;
    }

    RSCacheShared_FileListFree(fl);
    ie_relayout(game);
    if( game->widget_count > 0 )
    {
        game->expanded_count = 0;
        for( int i = 0; i < game->widget_count; i++ )
        {
            if( game->widgets[i].data.type < 0 )
                continue;
            if( ie_widget_depth(game, i) < 2 )
                ie_expand_uid(game, game->widgets[i].uid);
        }
    }
    GameInterfaceEditor_RunScripts(game);
    return game->widget_count > 0;
}

bool
GameInterfaceEditor_LoadGroup(
    struct GameInterfaceEditor* game,
    int group_id)
{
    return ie_load_group(game, group_id);
}

int
GameInterfaceEditor_CountDynamicCs2Children(struct GameInterfaceEditor* game)
{
    if( !game || !game->cs2_tree )
        return 0;

    int count = 0;
    for( uint32_t i = 0; i < game->cs2_tree->component_count; i++ )
    {
        if( game->cs2_tree->components[i].dynamic )
            count++;
    }
    return count;
}

void
GameInterfaceEditor_PrintDynamicCs2Children(struct GameInterfaceEditor* game)
{
    if( !game || !game->cs2_tree )
        return;

    for( uint32_t i = 0; i < game->cs2_tree->component_count; i++ )
    {
        struct StaticUIComponent const* node = &game->cs2_tree->components[i];
        if( !node->dynamic )
            continue;
        int const parent_id =
            node->parent >= 0 ? game->cs2_tree->components[node->parent].component_id : -1;
        fprintf(
            stderr,
            "  dynamic child sub_id=%d parent_id=%d type=%s\n",
            node->component_id & 0xFFFF,
            parent_id,
            uitree_component_type_str(node->type));
    }
}

static void
ie_apply_type_defaults(
    RSCacheDat2A_Component* c,
    int type)
{
    c->type = type;
    switch( type )
    {
    case 0:
        c->baseWidth = 200;
        c->baseHeight = 200;
        c->scrollWidth = 200;
        c->scrollHeight = 200;
        break;
    case 2:
        c->baseWidth = 4 * 36;
        c->baseHeight = 7 * 32;
        break;
    case 3:
        c->baseWidth = 100;
        c->baseHeight = 40;
        c->fill = true;
        c->color = 0xFFFFFF;
        break;
    case 4:
        c->baseWidth = 100;
        c->baseHeight = 14;
        c->text = strdup("Text");
        c->textFont = 495;
        c->color = 0xFFFF00;
        break;
    case 5:
        c->baseWidth = 32;
        c->baseHeight = 32;
        c->graphic = -1;
        break;
    case 6:
        c->baseWidth = 64;
        c->baseHeight = 64;
        c->modelZoom = 100;
        break;
    case 9:
        c->baseWidth = 100;
        c->baseHeight = 1;
        c->lineWidth = 1;
        c->color = 0xFFFFFF;
        break;
    default:
        break;
    }
}

static bool
ie_add_dynamic_widget(
    struct GameInterfaceEditor* game,
    int parent_uid,
    int type,
    int sprite_id)
{
    if( !game || game->widget_count >= IE_MAX_WIDGETS )
        return false;

    int child_id = game->next_dynamic_child++;
    if( child_id > 0xFFFF )
        return false;

    struct InterfaceEditorWidget* w = &game->widgets[game->widget_count];
    RSCacheDat2A_ComponentInit(&w->data);
    w->uid = (game->loaded_group_id << 16) | child_id;
    w->parent_uid = parent_uid;
    w->file_index = -1;
    w->is_editor_added = true;
    w->data.id = w->uid;
    w->data.layer = parent_uid;
    ie_apply_type_defaults(&w->data, type);
    if( type == 5 && sprite_id >= 0 )
        w->data.graphic = sprite_id;

    game->widget_count++;
    ie_relayout(game);
    game->selected_uid = w->uid;
    return true;
}

static int
ie_hit_test(
    struct GameInterfaceEditor* game,
    int px,
    int py)
{
    for( int i = game->hit_count - 1; i >= 0; i-- )
    {
        struct InterfaceEditorHitItem const* h = &game->hit_list[i];
        if( px >= h->x && px < h->x + h->w && py >= h->y && py < h->y + h->h )
            return i;
    }
    return -1;
}

static void
ie_text_field_begin(
    struct GameInterfaceEditor* game,
    enum InterfaceEditorTextFieldKind kind,
    int property_id,
    char const* initial)
{
    game->text_field.active = true;
    game->text_field.kind = kind;
    game->text_field.property_id = property_id;
    game->text_field.cursor = 0;
    game->text_field.buffer[0] = '\0';
    if( initial )
        snprintf(game->text_field.buffer, sizeof(game->text_field.buffer), "%s", initial);
    game->text_field.cursor = (int)strlen(game->text_field.buffer);
}

static void
ie_text_field_end(struct GameInterfaceEditor* game)
{
    game->text_field.active = false;
    game->text_field.kind = IE_FIELD_NONE;
    game->text_field.property_id = 0;
}

static void
ie_property_value_string(
    struct InterfaceEditorWidget* widget,
    int property_id,
    char* out,
    size_t out_size)
{
    if( !widget || !out || out_size == 0 )
        return;
    RSCacheDat2A_Component* c = &widget->data;
    switch( property_id )
    {
    case IE_PROP_RAW_X:
        snprintf(out, out_size, "%d", c->baseX);
        break;
    case IE_PROP_RAW_Y:
        snprintf(out, out_size, "%d", c->baseY);
        break;
    case IE_PROP_RAW_W:
        snprintf(out, out_size, "%d", c->baseWidth);
        break;
    case IE_PROP_RAW_H:
        snprintf(out, out_size, "%d", c->baseHeight);
        break;
    case IE_PROP_X_MODE:
        snprintf(out, out_size, "%d", (int)c->xMode);
        break;
    case IE_PROP_Y_MODE:
        snprintf(out, out_size, "%d", (int)c->yMode);
        break;
    case IE_PROP_W_MODE:
        snprintf(out, out_size, "%d", (int)c->widthMode);
        break;
    case IE_PROP_H_MODE:
        snprintf(out, out_size, "%d", (int)c->heightMode);
        break;
    case IE_PROP_OPACITY:
        snprintf(out, out_size, "%d", c->transparency);
        break;
    case IE_PROP_SCROLL_W:
        snprintf(out, out_size, "%d", c->scrollWidth);
        break;
    case IE_PROP_SCROLL_H:
        snprintf(out, out_size, "%d", c->scrollHeight);
        break;
    case IE_PROP_TEXT:
        snprintf(out, out_size, "%s", c->text ? c->text : "");
        break;
    case IE_PROP_FONT_ID:
        snprintf(out, out_size, "%d", c->textFont);
        break;
    case IE_PROP_LINE_HEIGHT:
        snprintf(out, out_size, "%d", c->textLineHeight);
        break;
    case IE_PROP_TEXT_ALIGN_H:
        snprintf(out, out_size, "%d", c->textHorizontalAlignment);
        break;
    case IE_PROP_TEXT_ALIGN_V:
        snprintf(out, out_size, "%d", c->textVerticalAlignment);
        break;
    case IE_PROP_COLOR:
    case IE_PROP_COLOR2:
        snprintf(out, out_size, "%06X", c->color & 0xFFFFFF);
        break;
    case IE_PROP_SPRITE_ID:
        snprintf(out, out_size, "%d", c->graphic);
        break;
    case IE_PROP_SPRITE_ID2:
        snprintf(out, out_size, "%d", c->activeGraphic);
        break;
    case IE_PROP_ANGLE:
        snprintf(out, out_size, "%d", c->angle);
        break;
    case IE_PROP_MODEL_ID:
        snprintf(out, out_size, "%d", c->modelId);
        break;
    case IE_PROP_ZOOM:
        snprintf(out, out_size, "%d", c->modelZoom);
        break;
    case IE_PROP_LINE_WIDTH:
        snprintf(out, out_size, "%d", c->lineWidth);
        break;
    case IE_PROP_GRID_COLS:
        snprintf(out, out_size, "%d", c->baseWidth);
        break;
    case IE_PROP_GRID_ROWS:
        snprintf(out, out_size, "%d", c->baseHeight);
        break;
    case IE_PROP_GRID_X_PITCH:
        snprintf(out, out_size, "%d", c->marginX);
        break;
    case IE_PROP_GRID_Y_PITCH:
        snprintf(out, out_size, "%d", c->marginY);
        break;
    default:
        out[0] = '\0';
        break;
    }
}

static int
ie_parse_int(
    char const* text,
    int fallback)
{
    if( !text || !text[0] )
        return fallback;
    return atoi(text);
}

static void
ie_apply_property(
    struct GameInterfaceEditor* game,
    struct InterfaceEditorWidget* w,
    int property_id,
    char const* value)
{
    RSCacheDat2A_Component* c = &w->data;
    switch( property_id )
    {
    case IE_PROP_RAW_X:
        c->baseX = ie_parse_int(value, c->baseX);
        break;
    case IE_PROP_RAW_Y:
        c->baseY = ie_parse_int(value, c->baseY);
        break;
    case IE_PROP_RAW_W:
        c->baseWidth = ie_parse_int(value, c->baseWidth);
        break;
    case IE_PROP_RAW_H:
        c->baseHeight = ie_parse_int(value, c->baseHeight);
        break;
    case IE_PROP_X_MODE:
        c->xMode = (int8_t)ie_parse_int(value, c->xMode);
        break;
    case IE_PROP_Y_MODE:
        c->yMode = (int8_t)ie_parse_int(value, c->yMode);
        break;
    case IE_PROP_W_MODE:
        c->widthMode = (int8_t)ie_parse_int(value, c->widthMode);
        break;
    case IE_PROP_H_MODE:
        c->heightMode = (int8_t)ie_parse_int(value, c->heightMode);
        break;
    case IE_PROP_OPACITY:
        c->transparency = ie_parse_int(value, c->transparency);
        break;
    case IE_PROP_SCROLL_W:
        c->scrollWidth = ie_parse_int(value, c->scrollWidth);
        break;
    case IE_PROP_SCROLL_H:
        c->scrollHeight = ie_parse_int(value, c->scrollHeight);
        break;
    case IE_PROP_TEXT:
        free(c->text);
        c->text = strdup(value ? value : "");
        break;
    case IE_PROP_FONT_ID:
        c->textFont = ie_parse_int(value, c->textFont);
        break;
    case IE_PROP_LINE_HEIGHT:
        c->textLineHeight = ie_parse_int(value, c->textLineHeight);
        break;
    case IE_PROP_TEXT_ALIGN_H:
        c->textHorizontalAlignment = ie_parse_int(value, c->textHorizontalAlignment);
        break;
    case IE_PROP_TEXT_ALIGN_V:
        c->textVerticalAlignment = ie_parse_int(value, c->textVerticalAlignment);
        break;
    case IE_PROP_COLOR:
    case IE_PROP_COLOR2:
    {
        unsigned v = 0;
        if( value && value[0] )
            sscanf(value, "%x", &v);
        if( property_id == IE_PROP_COLOR )
            c->color = (int)(v & 0xFFFFFF);
        break;
    }
    case IE_PROP_SPRITE_ID:
        c->graphic = ie_parse_int(value, c->graphic);
        break;
    case IE_PROP_SPRITE_ID2:
        c->activeGraphic = ie_parse_int(value, c->activeGraphic);
        break;
    case IE_PROP_ANGLE:
        c->angle = ie_parse_int(value, c->angle);
        break;
    case IE_PROP_MODEL_ID:
        c->modelId = ie_parse_int(value, c->modelId);
        break;
    case IE_PROP_ZOOM:
        c->modelZoom = ie_parse_int(value, c->modelZoom);
        break;
    case IE_PROP_LINE_WIDTH:
        c->lineWidth = ie_parse_int(value, c->lineWidth);
        break;
    case IE_PROP_GRID_COLS:
        c->baseWidth = ie_parse_int(value, c->baseWidth);
        break;
    case IE_PROP_GRID_ROWS:
        c->baseHeight = ie_parse_int(value, c->baseHeight);
        break;
    case IE_PROP_GRID_X_PITCH:
        c->marginX = ie_parse_int(value, c->marginX);
        break;
    case IE_PROP_GRID_Y_PITCH:
        c->marginY = ie_parse_int(value, c->marginY);
        break;
    case IE_PROP_OBJ_ID:
        c->objId = ie_parse_int(value, c->objId);
        break;
    case IE_PROP_OBJ_COUNT:
        c->objCount = ie_parse_int(value, c->objCount);
        break;
    case IE_PROP_PREVIEW_INV:
        w->preview_inv_id = ie_parse_int(value, w->preview_inv_id);
        ie_sync_preview_inventory(game, w);
        break;
    default:
        break;
    }
    ie_relayout(game);
}

static void
ie_sync_preview_inventory(
    struct GameInterfaceEditor* game,
    struct InterfaceEditorWidget* w)
{
    if( !game || !w || w->data.type != 2 || w->preview_inv_id < 0 )
        return;

    int source_id = -1;
    for( int i = 0; i < game->inv_data.source_count; i++ )
    {
        if( !game->inv_data.sources[i].used )
            continue;
        if( game->inv_data.sources[i].container_id == w->preview_inv_id )
        {
            source_id = i;
            break;
        }
    }
    if( source_id < 0 )
        return;

    int const slot_count = game->inv_data.sources[source_id].slot_count;
    if( slot_count <= 0 )
        return;

    if( w->data.objTypes )
        free(w->data.objTypes);
    if( w->data.objCounts )
        free(w->data.objCounts);
    w->data.objTypes = calloc((size_t)slot_count, sizeof(int32_t));
    w->data.objCounts = calloc((size_t)slot_count, sizeof(int32_t));
    if( !w->data.objTypes || !w->data.objCounts )
        return;

    for( int slot = 0; slot < slot_count; slot++ )
    {
        struct UIInvSlotData slot_data;
        memset(&slot_data, 0, sizeof(slot_data));
        if( ui_inv_data_service_get_slot(&game->inv_data, source_id, slot, &slot_data) )
        {
            w->data.objTypes[slot] = slot_data.obj_id;
            w->data.objCounts[slot] = slot_data.obj_count;
        }
        else
        {
            w->data.objTypes[slot] = -1;
            w->data.objCounts[slot] = 0;
        }
    }
}

static char const*
ie_obj_display_name(
    struct GameInterfaceEditor* game,
    int obj_id,
    char* buf,
    size_t buf_len)
{
    if( !buf || buf_len == 0 )
        return "";
    if( obj_id <= 0 )
    {
        snprintf(buf, buf_len, "(empty)");
        return buf;
    }

    struct RSCacheDat2A_ConfigObject* obj = ie_load_object_config(game, obj_id);
    if( obj && obj->name && obj->name[0] )
    {
        snprintf(buf, buf_len, "%s", obj->name);
        return buf;
    }
    snprintf(buf, buf_len, "obj %d", obj_id);
    return buf;
}

static void
ie_format_script_binding(
    char* out,
    size_t out_len,
    char const* label,
    ComponentScriptVar* hook,
    int hook_len)
{
    if( !out || out_len == 0 )
        return;
    if( !hook || hook_len <= 0 || hook[0].type != SCRIPT_VAR_INT )
    {
        snprintf(out, out_len, "%s: (none)", label ? label : "script");
        return;
    }
    snprintf(out, out_len, "%s: script %d (%d args)", label, hook[0].value.i, hook_len - 1);
}

static void
ie_commit_text_field(struct GameInterfaceEditor* game)
{
    if( !game->text_field.active )
        return;

    if( game->text_field.kind == IE_FIELD_PROPERTY )
    {
        struct InterfaceEditorWidget* w = ie_find_widget(game, game->selected_uid);
        if( w )
            ie_apply_property(game, w, game->text_field.property_id, game->text_field.buffer);
    }
    else if( game->text_field.kind == IE_FIELD_ADD_DIALOG_SPRITE )
    {
        game->add_dialog_sprite_id = ie_parse_int(game->text_field.buffer, -1);
    }
    else if( game->text_field.kind == IE_FIELD_CONTAINER_OBJ )
    {
        int obj_id = ie_parse_int(game->text_field.buffer, 0);
        struct UIInvSlotData slot_data;
        int count = 1;
        if( ui_inv_data_service_get_slot(
                &game->inv_data,
                game->container_edit_source_id,
                game->container_edit_slot,
                &slot_data) )
        {
            count = slot_data.obj_count > 0 ? slot_data.obj_count : 1;
        }
        ie_set_container_slot(
            game, game->container_edit_source_id, game->container_edit_slot, obj_id, count);
    }
    else if( game->text_field.kind == IE_FIELD_CONTAINER_COUNT )
    {
        int count = ie_parse_int(game->text_field.buffer, 1);
        struct UIInvSlotData slot_data;
        int obj_id = 0;
        if( ui_inv_data_service_get_slot(
                &game->inv_data,
                game->container_edit_source_id,
                game->container_edit_slot,
                &slot_data) )
        {
            obj_id = slot_data.obj_id;
        }
        ie_set_container_slot(
            game, game->container_edit_source_id, game->container_edit_slot, obj_id, count);
    }

    ie_text_field_end(game);
}

static void
ie_handle_text_input(
    struct GameInterfaceEditor* game,
    struct LibToriRS_Input* input)
{
    if( !game->text_field.active )
        return;

    for( int k = TORIRSK_SPACE; k < TORIRSK_COUNT; k++ )
    {
        if( !LibToriRS_Input_IsKeyDown(input, (enum LibToriRS_KeyCode)k) )
            continue;
        if( k == TORIRSK_RETURN || k == TORIRSK_ESCAPE )
        {
            if( k == TORIRSK_RETURN )
                ie_commit_text_field(game);
            else
                ie_text_field_end(game);
            return;
        }
        if( k == TORIRSK_BACKSPACE )
        {
            int len = (int)strlen(game->text_field.buffer);
            if( len > 0 )
                game->text_field.buffer[len - 1] = '\0';
            game->text_field.cursor = (int)strlen(game->text_field.buffer);
            return;
        }
        break;
    }

    for( int ch = 32; ch < 127; ch++ )
    {
        enum LibToriRS_KeyCode key = TORIRSK_UNKNOWN;
        if( ch >= 'a' && ch <= 'z' )
            key = (enum LibToriRS_KeyCode)(TORIRSK_A + (ch - 'a'));
        else if( ch >= 'A' && ch <= 'Z' )
            key = (enum LibToriRS_KeyCode)(TORIRSK_A + (ch - 'A'));
        else if( ch >= '0' && ch <= '9' )
            key = (enum LibToriRS_KeyCode)(TORIRSK_0 + (ch - '0'));
        else if( ch == ' ' )
            key = TORIRSK_SPACE;

        if( key == TORIRSK_UNKNOWN )
            continue;
        if( !LibToriRS_Input_IsKeyDown(input, key) )
            continue;

        int len = (int)strlen(game->text_field.buffer);
        if( len + 1 >= IE_TEXT_FIELD_LEN )
            return;
        game->text_field.buffer[len] = (char)ch;
        game->text_field.buffer[len + 1] = '\0';
        game->text_field.cursor = len + 1;
        return;
    }
}

static char const*
ie_position_mode_label(int mode)
{
    switch( mode )
    {
    case 0:
        return "Absolute";
    case 1:
        return "Center";
    case 2:
        return "Bottom/Right";
    case 3:
        return "Proportional X";
    case 4:
        return "Proportional Center";
    case 5:
        return "Proportional Bottom/Right";
    default:
        return "Unknown";
    }
}

static char const*
ie_size_mode_label(int mode)
{
    switch( mode )
    {
    case 0:
        return "Absolute";
    case 1:
        return "Minus";
    case 2:
        return "Proportional";
    case 4:
        return "Aspect ratio";
    default:
        return "Unknown";
    }
}

static void
ie_show_mode_dropdown(
    struct GameInterfaceEditor* game,
    int property_id,
    int click_x,
    int click_y)
{
    ui_minimenu_reset(&game->dropdown);
    game->dropdown_property_id = property_id;

    bool const is_size_mode = property_id == IE_PROP_W_MODE || property_id == IE_PROP_H_MODE;
    static int const k_size_modes[] = { 0, 1, 2, 4 };
    static int const k_pos_modes[] = { 0, 1, 2, 3, 4, 5 };
    int const mode_count = is_size_mode ? 4 : 6;
    int const* modes = is_size_mode ? k_size_modes : k_pos_modes;

    for( int i = 0; i < mode_count; i++ )
    {
        int const mode = modes[i];
        char label[48];
        char const* name = is_size_mode ? ie_size_mode_label(mode) : ie_position_mode_label(mode);
        snprintf(label, sizeof(label), "%d - %s", mode, name);
        ui_minimenu_add_option(&game->dropdown, label, (enum MinimenuAction)0, mode);
    }
    struct UIMinimenuLayout layout = ui_minimenu_layout_from_line_height(IE_ROW_H);
    int content_w = 120;
    ui_minimenu_show_at(
        &game->dropdown, layout, content_w, click_x, click_y, IE_WINDOW_W, IE_WINDOW_H);
}

static void
ie_show_type_dropdown(
    struct GameInterfaceEditor* game,
    int click_x,
    int click_y)
{
    ui_minimenu_reset(&game->dropdown);
    game->dropdown_property_id = -1;
    for( int i = 0; i < k_addable_type_count; i++ )
    {
        char label[64];
        snprintf(
            label,
            sizeof(label),
            "%d - %s",
            k_addable_types[i],
            widget_type_label(k_addable_types[i]));
        ui_minimenu_add_option(&game->dropdown, label, (enum MinimenuAction)0, k_addable_types[i]);
    }
    struct UIMinimenuLayout layout = ui_minimenu_layout_from_line_height(IE_ROW_H);
    ui_minimenu_show_at(&game->dropdown, layout, 180, click_x, click_y, IE_WINDOW_W, IE_WINDOW_H);
}

static void
ie_handle_hit(
    struct GameInterfaceEditor* game,
    int hit_index,
    int click_x,
    int click_y)
{
    if( hit_index < 0 )
        return;
    struct InterfaceEditorHitItem const* h = &game->hit_list[hit_index];

    if( game->dropdown.visible )
    {
        int opt = ui_minimenu_hit_option(&game->dropdown, click_x, click_y);
        if( opt >= 0 )
        {
            int const value = game->dropdown.options[opt].action_index;
            if( game->dropdown_property_id > 0 )
            {
                struct InterfaceEditorWidget* sel = ie_find_widget(game, game->selected_uid);
                if( sel )
                {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%d", value);
                    ie_apply_property(game, sel, game->dropdown_property_id, buf);
                }
            }
            else if( game->add_dialog_open )
            {
                game->add_dialog_type = value;
            }
            ui_minimenu_hide(&game->dropdown);
            return;
        }
        ui_minimenu_hide(&game->dropdown);
        return;
    }

    switch( h->kind )
    {
    case IEHIT_LIST_ROW:
        ie_load_group(game, h->param0);
        break;
    case IEHIT_TREE_ROW:
        game->selected_uid = h->param0;
        break;
    case IEHIT_TREE_EXPAND:
        ie_toggle_expanded(game, h->param0);
        break;
    case IEHIT_TREE_ADD:
        game->add_dialog_open = true;
        game->add_dialog_parent_uid = h->param0;
        game->add_dialog_type = 5;
        game->add_dialog_sprite_id = -1;
        game->sprite_picker_open = false;
        break;
    case IEHIT_CANVAS_WIDGET:
        game->selected_uid = h->param0;
        ie_handle_canvas_click_scripts(game, h->param0, click_x, click_y);
        break;
    case IEHIT_PROPERTY_FIELD:
    {
        struct InterfaceEditorWidget* sel = ie_find_widget(game, game->selected_uid);
        if( sel )
        {
            char buf[IE_TEXT_FIELD_LEN];
            ie_property_value_string(sel, h->param0, buf, sizeof(buf));
            ie_text_field_begin(game, IE_FIELD_PROPERTY, h->param0, buf);
        }
        break;
    }
    case IEHIT_PROPERTY_CHECKBOX:
    {
        struct InterfaceEditorWidget* w = ie_find_widget(game, game->selected_uid);
        if( !w )
            break;
        switch( h->param0 )
        {
        case 1:
            w->data.hidden = !w->data.hidden;
            break;
        case 2:
            w->data.fill = !w->data.fill;
            break;
        case 3:
            w->data.textShadow = !w->data.textShadow;
            break;
        case 4:
            w->data.tiled = !w->data.tiled;
            break;
        case 5:
            w->data.noClickThrough = !w->data.noClickThrough;
            break;
        case 6:
            w->data.horizontalFlip = !w->data.horizontalFlip;
            break;
        case 7:
            w->data.verticalFlip = !w->data.verticalFlip;
            break;
        case 8:
            w->data.lineDirection = !w->data.lineDirection;
            break;
        default:
            break;
        }
        ie_relayout(game);
        break;
    }
    case IEHIT_PROPERTY_DROPDOWN:
        ie_show_mode_dropdown(game, h->param0, click_x, click_y);
        break;
    case IEHIT_PROPERTY_BUTTON:
        if( h->param0 == 1 )
        {
            game->sprite_picker_open = true;
            game->sprite_picker_target = 0;
        }
        break;
    case IEHIT_ADD_DIALOG_BACKDROP:
        game->add_dialog_open = false;
        game->sprite_picker_open = false;
        break;
    case IEHIT_ADD_DIALOG_TYPE:
        ie_show_type_dropdown(game, click_x, click_y);
        break;
    case IEHIT_ADD_DIALOG_SPRITE_FIELD:
        ie_text_field_begin(
            game, IE_FIELD_ADD_DIALOG_SPRITE, 0, game->add_dialog_sprite_id >= 0 ? "" : "-1");
        break;
    case IEHIT_ADD_DIALOG_SPRITE_PICKER_BTN:
        game->sprite_picker_open = true;
        game->sprite_picker_target = 1;
        break;
    case IEHIT_ADD_DIALOG_CANCEL:
        game->add_dialog_open = false;
        game->sprite_picker_open = false;
        break;
    case IEHIT_ADD_DIALOG_ADD:
        ie_add_dynamic_widget(
            game, game->add_dialog_parent_uid, game->add_dialog_type, game->add_dialog_sprite_id);
        game->add_dialog_open = false;
        game->sprite_picker_open = false;
        break;
    case IEHIT_SPRITE_THUMB:
        if( h->param0 == -1 )
        {
            if( game->sprite_picker_page > 0 )
                game->sprite_picker_page--;
            break;
        }
        if( h->param0 == -2 )
        {
            game->sprite_picker_page++;
            break;
        }
        if( game->sprite_picker_target == 1 )
            game->add_dialog_sprite_id = h->param0;
        else
        {
            struct InterfaceEditorWidget* w = ie_find_widget(game, game->selected_uid);
            if( w && w->data.type == 5 )
            {
                w->data.graphic = h->param0;
                ie_relayout(game);
            }
        }
        game->sprite_picker_open = false;
        break;
    case IEHIT_TOOLBAR_RUN_SCRIPTS:
        GameInterfaceEditor_RunScripts(game);
        break;
    case IEHIT_TOOLBAR_TRACE:
        game->cs2_trace_enabled = !game->cs2_trace_enabled;
        cs2vm_set_trace(game->cs2_trace_enabled);
        fprintf(stderr, "ie_cs2: trace %s\n", game->cs2_trace_enabled ? "enabled" : "disabled");
        break;
    case IEHIT_TAB_PROPERTIES:
        game->right_tab = IE_TAB_PROPERTIES;
        break;
    case IEHIT_TAB_CONTAINERS:
        game->right_tab = IE_TAB_CONTAINERS;
        break;
    case IEHIT_CONTAINER_SOURCE_TAB:
        game->containers_source_id = h->param0;
        break;
    case IEHIT_CONTAINER_SLOT_OBJ:
    {
        game->container_edit_source_id = h->param0;
        game->container_edit_slot = h->param1;
        struct UIInvSlotData slot_data;
        char buf[32] = "0";
        if( ui_inv_data_service_get_slot(&game->inv_data, h->param0, h->param1, &slot_data) )
            snprintf(buf, sizeof(buf), "%d", slot_data.obj_id);
        ie_text_field_begin(game, IE_FIELD_CONTAINER_OBJ, 0, buf);
        break;
    }
    case IEHIT_CONTAINER_SLOT_COUNT:
    {
        game->container_edit_source_id = h->param0;
        game->container_edit_slot = h->param1;
        struct UIInvSlotData slot_data;
        char buf[32] = "1";
        if( ui_inv_data_service_get_slot(&game->inv_data, h->param0, h->param1, &slot_data) )
            snprintf(buf, sizeof(buf), "%d", slot_data.obj_count > 0 ? slot_data.obj_count : 1);
        ie_text_field_begin(game, IE_FIELD_CONTAINER_COUNT, 0, buf);
        break;
    }
    case IEHIT_SCROLLBAR_GRIP:
        game->scrollbar_drag_active = true;
        game->scrollbar_drag_target = (enum InterfaceEditorScrollTarget)h->param0;
        break;
    default:
        break;
    }
}

static void
ie_push_property_row(
    struct GameInterfaceEditor* game,
    int panel_x,
    int panel_y,
    int panel_w,
    int panel_h,
    int* y_cursor,
    char const* label,
    int property_id,
    char const* value,
    bool dropdown)
{
    int y = *y_cursor;
    int row_h = IE_ROW_H;
    if( y + row_h < panel_y || y >= panel_y + panel_h )
    {
        *y_cursor += row_h;
        return;
    }
    int field_x = panel_x + panel_w / 2;
    int field_w = panel_w / 2 - 8;

    ie_push_draw_font_ui(
        game,
        panel_x + 4,
        y,
        panel_w / 2 - 8,
        row_h,
        ie_default_ui_font_id(game),
        label,
        0xCCCCCC,
        0,
        0);
    ie_push_draw_fill(game, field_x, y + 2, field_w, row_h - 4, 0xFF2A2A2A);
    ie_push_draw_font_ui(
        game,
        field_x + 4,
        y,
        field_w - 8,
        row_h,
        ie_default_ui_font_id(game),
        value ? value : "",
        0xFFFFFF,
        0,
        0);

    if( dropdown )
        ie_push_hit(game, IEHIT_PROPERTY_DROPDOWN, field_x, y, field_w, row_h, property_id, 0);
    else
        ie_push_hit(game, IEHIT_PROPERTY_FIELD, field_x, y, field_w, row_h, property_id, 0);

    *y_cursor += row_h;
}

static void
ie_push_checkbox_row(
    struct GameInterfaceEditor* game,
    int panel_x,
    int panel_y,
    int panel_w,
    int panel_h,
    int* y_cursor,
    char const* label,
    bool checked,
    int checkbox_id)
{
    int y = *y_cursor;
    int row_h = IE_ROW_H;
    if( y + row_h < panel_y || y >= panel_y + panel_h )
    {
        *y_cursor += row_h;
        return;
    }
    int box = 14;
    int box_x = panel_x + 4;
    ie_push_draw_fill(game, box_x, y + 3, box, box, checked ? 0xFF4A9EFF : 0xFF3A3A3A);
    ie_push_hit(game, IEHIT_PROPERTY_CHECKBOX, box_x, y, box + 120, IE_ROW_H, checkbox_id, 0);
    ie_push_draw_font_ui(
        game,
        box_x + box + 6,
        y,
        panel_w - box - 12,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        label,
        0xCCCCCC,
        0,
        0);
    *y_cursor += IE_ROW_H;
}

static void
ie_push_property_section_header(
    struct GameInterfaceEditor* game,
    int panel_x,
    int panel_y,
    int panel_w,
    int panel_h,
    int* y_cursor,
    char const* title)
{
    int y = *y_cursor;
    if( y + IE_ROW_H >= panel_y && y < panel_y + panel_h )
    {
        ie_push_draw_font_ui(
            game,
            panel_x + 4,
            y,
            panel_w - 8,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            title ? title : "",
            0xFFFFFF,
            0,
            0);
    }
    *y_cursor += IE_ROW_H;
}

static void
ie_build_containers_panel(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int panel_w,
    int panel_h)
{
    ie_push_draw_fill(game, x, y, panel_w, panel_h, 0xFF1E1E1E);

    int const source_id =
        game->containers_source_id >= 0 ? game->containers_source_id : game->worn_source_id;
    int slot_count = RS_INV_CONTAINER_MAX_SLOTS;
    if( source_id >= 0 && source_id < game->inv_data.source_count )
        slot_count = game->inv_data.sources[source_id].slot_count;

    int cols = (source_id == game->worn_source_id) ? 4 : 4;
    int rows = (slot_count + cols - 1) / cols;
    struct UIInvGridLayout layout = {
        .cols = cols,
        .rows = rows,
        .margin_x = 36,
        .margin_y = 36,
        .offset_x = NULL,
        .offset_y = NULL,
    };

    game->containers_content_h = (IE_ROW_H + 12) + rows * (layout.margin_y + UI_INV_SLOT_ICON_SIZE);
    int const containers_visible_h = panel_h - IE_ROW_H;
    game->containers_scroll =
        ie_clamp_scroll(game->containers_scroll, game->containers_content_h, containers_visible_h);

    struct IEVerticalScrollbarGeom const containers_geom = ie_vertical_scrollbar_geom(
        x, y, panel_w, containers_visible_h, game->containers_scroll, game->containers_content_h);
    int const content_w = containers_geom.content_w;
    int tab_x = x + 2;
    int tab_y = y + 4 - game->containers_scroll;

    for( int si = 0; si < game->inv_data.source_count; si++ )
    {
        if( !game->inv_data.sources[si].used )
            continue;

        char tab_label[48];
        snprintf(
            tab_label,
            sizeof(tab_label),
            "%s (%d)",
            game->inv_data.sources[si].name,
            game->inv_data.sources[si].container_id);
        int const tab_w = 72;
        if( tab_y + IE_ROW_H < y || tab_y >= y + panel_h )
        {
            tab_x += tab_w + 2;
            continue;
        }

        bool const selected = game->containers_source_id == si;
        ie_push_draw_fill(game, tab_x, tab_y, tab_w, IE_ROW_H, selected ? 0xFF2A3A4A : 0xFF252525);
        ie_push_hit(game, IEHIT_CONTAINER_SOURCE_TAB, tab_x, tab_y, tab_w, IE_ROW_H, si, 0);
        ie_push_draw_font_ui(
            game,
            tab_x + 4,
            tab_y,
            tab_w - 8,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            tab_label,
            0xCCCCCC,
            0,
            0);
        tab_x += tab_w + 2;
    }

    int grid_x = x + 8;
    int grid_y = y + IE_ROW_H + 12 - game->containers_scroll;
    int slot_limit = ui_inv_slot_view_grid_slot_limit(&layout);
    if( slot_limit > slot_count )
        slot_limit = slot_count;

    for( int slot = 0; slot < slot_limit; slot++ )
    {
        int sx = 0;
        int sy = 0;
        int sw = 0;
        int sh = 0;
        ui_inv_slot_view_grid_rect(grid_x, grid_y, &layout, slot, &sx, &sy, &sw, &sh);
        if( sy + sh < y + IE_ROW_H || sy >= y + panel_h )
            continue;

        ie_push_draw_fill(game, sx, sy, sw, sh, 0xFF2A2A2A);

        struct UIInvSlotData slot_data;
        memset(&slot_data, 0, sizeof(slot_data));
        (void)ui_inv_data_service_get_slot(&game->inv_data, source_id, slot, &slot_data);

        if( slot_data.obj_id > 0 && slot_data.scene_id >= 0 )
        {
            int ix = 0;
            int iy = 0;
            int iw = 0;
            int ih = 0;
            ui_inv_slot_view_centered_rect(
                sx, sy, sw, sh, UI_INV_SLOT_ICON_SIZE, &ix, &iy, &iw, &ih);
            ie_push_draw_sprite(
                game, ix, iy, iw, ih, slot_data.scene_id, slot_data.atlas_index, 255, 0);
        }

        char label[48];
        char obj_name[48];
        ie_obj_display_name(game, slot_data.obj_id, obj_name, sizeof(obj_name));
        snprintf(label, sizeof(label), "%d %s", slot_data.obj_id, obj_name);
        ie_push_draw_font_ui(
            game, sx, sy + sh - 10, sw, 10, ie_default_ui_font_id(game), label, 0xAAAAAA, 1, 0);

        ie_push_hit(game, IEHIT_CONTAINER_SLOT_OBJ, sx, sy, sw, sh - 10, source_id, slot);
        ie_push_hit(game, IEHIT_CONTAINER_SLOT_COUNT, sx, sy + sh - 10, sw, 10, source_id, slot);
    }

    ie_push_draw_font_ui(
        game,
        x + 8,
        y + panel_h - IE_ROW_H - 4,
        panel_w - 16,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "Edit slots, then Rerun Scripts",
        0x888888,
        0,
        0);

    ie_push_vertical_scrollbar(game, IE_SCROLL_BOTTOM, &containers_geom);
}

static void
ie_build_right_panel_tabs(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int panel_w)
{
    int tab_w = panel_w / 2 - 2;
    ie_push_draw_fill(
        game,
        x + 2,
        y,
        tab_w,
        IE_ROW_H,
        game->right_tab == IE_TAB_PROPERTIES ? 0xFF2A3A4A : 0xFF252525);
    ie_push_hit(game, IEHIT_TAB_PROPERTIES, x + 2, y, tab_w, IE_ROW_H, 0, 0);
    ie_push_draw_font_ui(
        game,
        x + 8,
        y,
        tab_w - 8,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "Properties",
        0xCCCCCC,
        0,
        0);

    ie_push_draw_fill(
        game,
        x + tab_w + 4,
        y,
        tab_w,
        IE_ROW_H,
        game->right_tab == IE_TAB_CONTAINERS ? 0xFF2A3A4A : 0xFF252525);
    ie_push_hit(game, IEHIT_TAB_CONTAINERS, x + tab_w + 4, y, tab_w, IE_ROW_H, 0, 0);
    ie_push_draw_font_ui(
        game,
        x + tab_w + 12,
        y,
        tab_w - 8,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "Containers",
        0xCCCCCC,
        0,
        0);
}

static void
ie_build_properties_panel(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int panel_w,
    int panel_h)
{
    ie_push_draw_fill(game, x, y, panel_w, panel_h, 0xFF1E1E1E);

    game->props_scroll = ie_clamp_scroll(game->props_scroll, game->props_content_h, panel_h);

    struct IEVerticalScrollbarGeom props_geom = ie_vertical_scrollbar_geom(
        x, y, panel_w, panel_h, game->props_scroll, game->props_content_h);
    int content_w = props_geom.content_w;

    int cursor_y = y + 4 - game->props_scroll;
    struct InterfaceEditorWidget* sel = ie_find_widget(game, game->selected_uid);
    if( !sel )
    {
        ie_push_draw_font_ui(
            game,
            x + 8,
            cursor_y,
            content_w - 16,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            "Select a component",
            0x888888,
            0,
            0);
        game->props_content_h = IE_ROW_H;
        props_geom = ie_vertical_scrollbar_geom(
            x, y, panel_w, panel_h, game->props_scroll, game->props_content_h);
        ie_push_vertical_scrollbar(game, IE_SCROLL_BOTTOM, &props_geom);
        return;
    }

    RSCacheDat2A_Component* c = &sel->data;
    char buf[64];

    snprintf(buf, sizeof(buf), "%d", sel->uid);
    ie_push_property_section_header(game, x, y, content_w, panel_h, &cursor_y, "Identity");
    ie_push_property_row(game, x, y, content_w, panel_h, &cursor_y, "UID", 0, buf, false);
    snprintf(buf, sizeof(buf), "%s (%d)", widget_type_label(c->type), c->type);
    ie_push_property_row(game, x, y, content_w, panel_h, &cursor_y, "Type", 0, buf, false);
    ie_push_checkbox_row(game, x, y, content_w, panel_h, &cursor_y, "Active", !c->hidden, 1);
    snprintf(buf, sizeof(buf), "%d", sel->parent_uid);
    ie_push_property_row(game, x, y, content_w, panel_h, &cursor_y, "Parent", 0, buf, false);

    ie_push_property_section_header(game, x, y, content_w, panel_h, &cursor_y, "Layout");
    snprintf(buf, sizeof(buf), "%d", c->baseX);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "rawX", IE_PROP_RAW_X, buf, false);
    snprintf(buf, sizeof(buf), "%d", c->baseY);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "rawY", IE_PROP_RAW_Y, buf, false);
    snprintf(buf, sizeof(buf), "%d", c->baseWidth);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "rawWidth", IE_PROP_RAW_W, buf, false);
    snprintf(buf, sizeof(buf), "%d", c->baseHeight);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "rawHeight", IE_PROP_RAW_H, buf, false);
    snprintf(buf, sizeof(buf), "%d", (int)c->xMode);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "xMode", IE_PROP_X_MODE, buf, true);
    snprintf(buf, sizeof(buf), "%d", (int)c->yMode);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "yMode", IE_PROP_Y_MODE, buf, true);
    snprintf(buf, sizeof(buf), "%d", (int)c->widthMode);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "widthMode", IE_PROP_W_MODE, buf, true);
    snprintf(buf, sizeof(buf), "%d", (int)c->heightMode);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "heightMode", IE_PROP_H_MODE, buf, true);
    snprintf(buf, sizeof(buf), "%d", c->transparency);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "opacity", IE_PROP_OPACITY, buf, false);

    if( c->type == 0 )
    {
        snprintf(buf, sizeof(buf), "%d", c->scrollWidth);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "scrollWidth", IE_PROP_SCROLL_W, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->scrollHeight);
        ie_push_property_row(
            game,
            x,
            y,
            content_w,
            panel_h,
            &cursor_y,
            "scrollHeight",
            IE_PROP_SCROLL_H,
            buf,
            false);
        ie_push_checkbox_row(
            game, x, y, content_w, panel_h, &cursor_y, "noClickThrough", c->noClickThrough, 5);
    }
    else if( c->type == 3 )
    {
        ie_push_property_section_header(game, x, y, content_w, panel_h, &cursor_y, "Rectangle");
        ie_push_checkbox_row(game, x, y, content_w, panel_h, &cursor_y, "filled", c->fill, 2);
        snprintf(buf, sizeof(buf), "%06X", c->color & 0xFFFFFF);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "color", IE_PROP_COLOR, buf, false);
    }
    else if( c->type == 4 )
    {
        ie_push_property_section_header(game, x, y, content_w, panel_h, &cursor_y, "Text");
        ie_push_property_row(
            game,
            x,
            y,
            content_w,
            panel_h,
            &cursor_y,
            "text",
            IE_PROP_TEXT,
            c->text ? c->text : "",
            false);
        snprintf(buf, sizeof(buf), "%d", c->textFont);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "fontId", IE_PROP_FONT_ID, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->textLineHeight);
        ie_push_property_row(
            game,
            x,
            y,
            content_w,
            panel_h,
            &cursor_y,
            "lineHeight",
            IE_PROP_LINE_HEIGHT,
            buf,
            false);
        snprintf(buf, sizeof(buf), "%d", c->textHorizontalAlignment);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "alignH", IE_PROP_TEXT_ALIGN_H, buf, true);
        snprintf(buf, sizeof(buf), "%d", c->textVerticalAlignment);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "alignV", IE_PROP_TEXT_ALIGN_V, buf, true);
        ie_push_checkbox_row(game, x, y, content_w, panel_h, &cursor_y, "shaded", c->textShadow, 3);
        snprintf(buf, sizeof(buf), "%06X", c->color & 0xFFFFFF);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "color", IE_PROP_COLOR, buf, false);
    }
    else if( c->type == 5 )
    {
        ie_push_property_section_header(game, x, y, content_w, panel_h, &cursor_y, "Sprite");
        snprintf(buf, sizeof(buf), "%d", c->graphic);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "spriteId", IE_PROP_SPRITE_ID, buf, false);
        ie_push_draw_fill(
            game, x + content_w - 60, cursor_y - IE_ROW_H + 2, 50, IE_ROW_H - 4, 0xFF4A9EFF);
        ie_push_hit(
            game,
            IEHIT_PROPERTY_BUTTON,
            x + content_w - 60,
            cursor_y - IE_ROW_H,
            50,
            IE_ROW_H,
            1,
            0);
        ie_push_draw_font_ui(
            game,
            x + content_w - 58,
            cursor_y - IE_ROW_H,
            46,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            "Pick",
            0xFFFFFF,
            0,
            0);
        snprintf(buf, sizeof(buf), "%d", c->activeGraphic);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "spriteId2", IE_PROP_SPRITE_ID2, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->objId);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "objId", IE_PROP_OBJ_ID, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->objCount);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "objCount", IE_PROP_OBJ_COUNT, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->angle);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "angle", IE_PROP_ANGLE, buf, false);
        ie_push_checkbox_row(game, x, y, content_w, panel_h, &cursor_y, "tiled", c->tiled, 4);
        ie_push_checkbox_row(
            game, x, y, content_w, panel_h, &cursor_y, "hFlip", c->horizontalFlip, 6);
        ie_push_checkbox_row(
            game, x, y, content_w, panel_h, &cursor_y, "vFlip", c->verticalFlip, 7);
    }
    else if( c->type == 6 )
    {
        snprintf(buf, sizeof(buf), "%d", c->modelId);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "modelId", IE_PROP_MODEL_ID, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->modelZoom);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "zoom", IE_PROP_ZOOM, buf, false);
    }
    else if( c->type == 9 )
    {
        snprintf(buf, sizeof(buf), "%d", c->lineWidth);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "lineWidth", IE_PROP_LINE_WIDTH, buf, false);
        ie_push_checkbox_row(
            game, x, y, content_w, panel_h, &cursor_y, "horizontal", c->lineDirection, 8);
        snprintf(buf, sizeof(buf), "%06X", c->color & 0xFFFFFF);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "color", IE_PROP_COLOR, buf, false);
    }
    else if( c->type == 2 )
    {
        ie_push_property_section_header(
            game, x, y, content_w, panel_h, &cursor_y, "Inventory grid");
        snprintf(buf, sizeof(buf), "%d", c->baseWidth);
        ie_push_property_row(
            game,
            x,
            y,
            content_w,
            panel_h,
            &cursor_y,
            "gridColumns",
            IE_PROP_GRID_COLS,
            buf,
            false);
        snprintf(buf, sizeof(buf), "%d", c->baseHeight);
        ie_push_property_row(
            game, x, y, content_w, panel_h, &cursor_y, "gridRows", IE_PROP_GRID_ROWS, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->marginX);
        ie_push_property_row(
            game,
            x,
            y,
            content_w,
            panel_h,
            &cursor_y,
            "gridXPitch",
            IE_PROP_GRID_X_PITCH,
            buf,
            false);
        snprintf(buf, sizeof(buf), "%d", c->marginY);
        ie_push_property_row(
            game,
            x,
            y,
            content_w,
            panel_h,
            &cursor_y,
            "gridYPitch",
            IE_PROP_GRID_Y_PITCH,
            buf,
            false);
        snprintf(buf, sizeof(buf), "%d", sel->preview_inv_id);
        ie_push_property_row(
            game,
            x,
            y,
            content_w,
            panel_h,
            &cursor_y,
            "previewInvId",
            IE_PROP_PREVIEW_INV,
            buf,
            false);
    }

    ie_push_property_section_header(game, x, y, content_w, panel_h, &cursor_y, "Scripts");
    char script_buf[96];
    ie_format_script_binding(script_buf, sizeof(script_buf), "onLoad", c->onLoad, c->onLoadLen);
    ie_push_property_row(game, x, y, content_w, panel_h, &cursor_y, "onLoad", 0, script_buf, false);
    ie_format_script_binding(
        script_buf, sizeof(script_buf), "onInvTransmit", c->onInvTransmit, c->onInvTransmitLen);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "onInvTransmit", 0, script_buf, false);
    ie_format_script_binding(
        script_buf, sizeof(script_buf), "onVarTransmit", c->onVarpTransmit, c->onVarpTransmitLen);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "onVarTransmit", 0, script_buf, false);
    ie_format_script_binding(script_buf, sizeof(script_buf), "onTimer", c->onTimer, c->onTimerLen);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "onTimer", 0, script_buf, false);
    ie_format_script_binding(script_buf, sizeof(script_buf), "onClick", c->onClick, c->onClickLen);
    ie_push_property_row(
        game, x, y, content_w, panel_h, &cursor_y, "onClick", 0, script_buf, false);

    game->props_content_h = cursor_y - (y + 4);
    props_geom = ie_vertical_scrollbar_geom(
        x, y, panel_w, panel_h, game->props_scroll, game->props_content_h);
    ie_push_vertical_scrollbar(game, IE_SCROLL_BOTTOM, &props_geom);
}

static char const*
ie_uitree_type_label(enum StaticUIComponentType type)
{
    switch( type )
    {
    case UIELEM_RS_LAYER:
        return "Layer";
    case UIELEM_RS_RECT:
        return "Rectangle";
    case UIELEM_RS_TEXT:
        return "Font";
    case UIELEM_RS_GRAPHIC:
        return "Sprite";
    case UIELEM_CC_OBJ:
        return "Obj";
    default:
        return uitree_component_type_str(type);
    }
}

static int32_t
ie_cs2_tree_find_by_component_id(
    struct UITree const* tree,
    int component_id)
{
    if( !tree || component_id < 0 )
        return -1;

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].component_id == component_id )
            return (int32_t)i;
    }
    return -1;
}

static bool
ie_cs2_tree_has_dynamic_child(
    struct UITree const* tree,
    int32_t parent_index)
{
    if( !tree || parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return false;

    for( int32_t child = tree->components[parent_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        if( tree->components[child].dynamic )
            return true;
    }
    return false;
}

static void
ie_build_dynamic_tree_rows(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int panel_w,
    int panel_h,
    int parent_component_id,
    int depth,
    int* y_cursor)
{
    if( !game || !game->scripts_ran || !game->cs2_tree )
        return;

    int32_t const parent_index =
        ie_cs2_tree_find_by_component_id(game->cs2_tree, parent_component_id);
    if( parent_index < 0 )
        return;

    for( int32_t child = game->cs2_tree->components[parent_index].first_child; child >= 0;
         child = game->cs2_tree->components[child].next_sibling )
    {
        struct StaticUIComponent const* node = &game->cs2_tree->components[child];
        if( !node->dynamic )
            continue;

        int const uid = node->component_id;
        int row_y = *y_cursor - game->tree_scroll;
        bool const has_children = ie_cs2_tree_has_dynamic_child(game->cs2_tree, child);
        bool const expanded = ie_is_expanded(game, uid);

        if( row_y + IE_ROW_H >= y && row_y < y + panel_h )
        {
            int indent = x + 4 + depth * 14;
            char label[128];

            int const expand_indent = indent;
            if( has_children )
            {
                ie_push_draw_font_ui(
                    game,
                    indent,
                    row_y,
                    12,
                    IE_ROW_H,
                    ie_default_ui_font_id(game),
                    expanded ? "v" : ">",
                    0xCCCCCC,
                    0,
                    0);
                indent += 14;
            }

            snprintf(
                label,
                sizeof(label),
                "Dynamic %d (%s)",
                node->dynamic_child_index >= 0 ? node->dynamic_child_index : (uid & 0xFFFF),
                ie_uitree_type_label(node->type));

            int label_color = uid == game->selected_uid ? 0x4A9EFF : 0xCCCCCC;
            ie_push_draw_fill(
                game,
                x,
                row_y,
                panel_w,
                IE_ROW_H,
                uid == game->selected_uid ? 0xFF2A3A4A : 0x00000000);
            ie_push_hit(game, IEHIT_TREE_ROW, x, row_y, panel_w, IE_ROW_H, uid, 0);
            ie_push_draw_font_ui(
                game,
                indent,
                row_y,
                panel_w - (indent - x) - 24,
                IE_ROW_H,
                ie_default_ui_font_id(game),
                label,
                label_color,
                0,
                0);
            if( has_children )
                ie_push_hit(game, IEHIT_TREE_EXPAND, expand_indent, row_y, 12, IE_ROW_H, uid, 0);
        }

        *y_cursor += IE_ROW_H;
        if( has_children && expanded )
            ie_build_dynamic_tree_rows(game, x, y, panel_w, panel_h, uid, depth + 1, y_cursor);
    }
}

static bool
ie_component_has_tree_children(
    struct GameInterfaceEditor* game,
    int component_uid);

static void
ie_build_tree_rows(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int panel_w,
    int panel_h,
    int parent_uid,
    int depth,
    int* y_cursor);

static void
ie_build_dynamic_tree_rows(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int panel_w,
    int panel_h,
    int parent_component_id,
    int depth,
    int* y_cursor);

static bool
ie_component_has_tree_children(
    struct GameInterfaceEditor* game,
    int component_uid)
{
    if( ie_widget_has_static_children(game, component_uid) )
        return true;
    if( !game->scripts_ran || !game->cs2_tree )
        return false;

    int32_t const parent_index = ie_cs2_tree_find_by_component_id(game->cs2_tree, component_uid);
    if( parent_index < 0 )
        return false;
    return ie_cs2_tree_has_dynamic_child(game->cs2_tree, parent_index);
}

static void
ie_build_tree_root_row(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int panel_w,
    int panel_h,
    int content_w,
    int root_uid,
    int file_index,
    int* y_cursor)
{
    int row_y = *y_cursor - game->tree_scroll;
    if( row_y + IE_ROW_H >= y + IE_ROW_H && row_y < y + panel_h )
    {
        char root_label[64];
        snprintf(root_label, sizeof(root_label), "RSCacheDat2A_Component %d (Layer)", file_index);
        ie_push_draw_fill(
            game,
            x,
            row_y,
            content_w,
            IE_ROW_H,
            root_uid == game->selected_uid ? 0xFF2A3A4A : 0x00000000);
        ie_push_hit(game, IEHIT_TREE_ROW, x, row_y, content_w, IE_ROW_H, root_uid, 0);
        ie_push_draw_font_ui(
            game,
            x + 18,
            row_y,
            content_w - 40,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            root_label,
            root_uid == game->selected_uid ? 0x4A9EFF : 0xCCCCCC,
            0,
            0);
        ie_push_hit(game, IEHIT_TREE_EXPAND, x + 4, row_y, 12, IE_ROW_H, root_uid, 0);
        ie_push_draw_font_ui(
            game,
            x + 4,
            row_y,
            12,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            ie_is_expanded(game, root_uid) ? "v" : ">",
            0xCCCCCC,
            0,
            0);
        ie_push_draw_fill(game, x + content_w - 22, row_y + 2, 18, IE_ROW_H - 4, 0xFF3A3A3A);
        ie_push_hit(game, IEHIT_TREE_ADD, x + content_w - 22, row_y, 18, IE_ROW_H, root_uid, 0);
        ie_push_draw_font_ui(
            game,
            x + content_w - 18,
            row_y,
            14,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            "+",
            0xFFFFFF,
            0,
            0);
    }
    *y_cursor += IE_ROW_H;

    if( ie_is_expanded(game, root_uid) )
    {
        ie_build_tree_rows(game, x, y + IE_ROW_H, content_w, panel_h, root_uid, 1, y_cursor);
        ie_build_dynamic_tree_rows(
            game, x, y + IE_ROW_H, content_w, panel_h, root_uid, 1, y_cursor);
    }
}

static void
ie_build_tree_rows(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int panel_w,
    int panel_h,
    int parent_uid,
    int depth,
    int* y_cursor)
{
    for( int i = 0; i < game->widget_count; i++ )
    {
        struct InterfaceEditorWidget* node = &game->widgets[i];
        int const puid = node->is_editor_added ? node->parent_uid : node->data.layer;
        if( puid != parent_uid )
            continue;

        int row_y = *y_cursor - game->tree_scroll;
        if( row_y + IE_ROW_H >= y && row_y < y + panel_h )
        {
            int indent = x + 4 + depth * 14;
            bool expanded = ie_is_expanded(game, node->uid);
            bool const container = ie_is_container_type(node->data.type);
            bool const has_children = ie_component_has_tree_children(game, node->uid);
            char label[128];

            int const expand_indent = indent;
            if( has_children )
            {
                ie_push_draw_font_ui(
                    game,
                    indent,
                    row_y,
                    12,
                    IE_ROW_H,
                    ie_default_ui_font_id(game),
                    expanded ? "v" : ">",
                    0xCCCCCC,
                    0,
                    0);
                indent += 14;
            }

            if( node->is_editor_added )
                snprintf(
                    label,
                    sizeof(label),
                    "Dynamic %d (%s)",
                    node->file_index >= 0 ? node->file_index : 0,
                    widget_type_label(node->data.type));
            else
                snprintf(
                    label,
                    sizeof(label),
                    "RSCacheDat2A_Component %d (%s)",
                    node->file_index,
                    widget_type_label(node->data.type));

            int label_color = node->uid == game->selected_uid ? 0x4A9EFF : 0xCCCCCC;
            ie_push_draw_fill(
                game,
                x,
                row_y,
                panel_w,
                IE_ROW_H,
                node->uid == game->selected_uid ? 0xFF2A3A4A : 0x00000000);
            ie_push_hit(game, IEHIT_TREE_ROW, x, row_y, panel_w, IE_ROW_H, node->uid, 0);
            ie_push_draw_font_ui(
                game,
                indent,
                row_y,
                panel_w - (indent - x) - 24,
                IE_ROW_H,
                ie_default_ui_font_id(game),
                label,
                label_color,
                0,
                0);

            if( container )
            {
                int add_x = x + panel_w - 22;
                ie_push_draw_fill(game, add_x, row_y + 2, 18, IE_ROW_H - 4, 0xFF3A3A3A);
                ie_push_hit(game, IEHIT_TREE_ADD, add_x, row_y, 18, IE_ROW_H, node->uid, 0);
                ie_push_draw_font_ui(
                    game,
                    add_x + 4,
                    row_y,
                    14,
                    IE_ROW_H,
                    ie_default_ui_font_id(game),
                    "+",
                    0xFFFFFF,
                    0,
                    0);
            }
            if( has_children )
                ie_push_hit(
                    game, IEHIT_TREE_EXPAND, expand_indent, row_y, 12, IE_ROW_H, node->uid, 0);
        }

        *y_cursor += IE_ROW_H;
        if( ie_component_has_tree_children(game, node->uid) && ie_is_expanded(game, node->uid) )
        {
            ie_build_tree_rows(game, x, y, panel_w, panel_h, node->uid, depth + 1, y_cursor);
            ie_build_dynamic_tree_rows(
                game, x, y, panel_w, panel_h, node->uid, depth + 1, y_cursor);
        }
    }
}

static void
ie_build_tree_panel(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int panel_w,
    int panel_h)
{
    ie_push_draw_fill(game, x, y, panel_w, panel_h, 0xFF1E1E1E);
    ie_push_draw_font_ui(
        game,
        x + 4,
        y + 4,
        panel_w - 8,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "Components",
        0xFFFFFF,
        0,
        0);

    if( game->loaded_group_id < 0 || game->widget_count <= 0 )
    {
        ie_push_draw_font_ui(
            game,
            x + 8,
            y + IE_ROW_H + 12,
            panel_w - 16,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            "Load an interface to see components",
            0x888888,
            0,
            0);
        game->tree_content_h = 0;
        return;
    }

    int const tree_visible_h = panel_h - IE_ROW_H - 8;
    int const tree_viewport_y = y + IE_ROW_H + 8;
    game->tree_scroll = ie_clamp_scroll(game->tree_scroll, game->tree_content_h, tree_visible_h);

    struct IEVerticalScrollbarGeom tree_geom = ie_vertical_scrollbar_geom(
        x, tree_viewport_y, panel_w, tree_visible_h, game->tree_scroll, game->tree_content_h);
    int const content_w = tree_geom.content_w;

    int y_cursor = tree_viewport_y;
    for( int i = 0; i < game->widget_count; i++ )
    {
        if( game->widgets[i].data.type < 0 )
            continue;
        if( ie_widget_parent_index(game, i) >= 0 )
            continue;
        ie_build_tree_root_row(
            game,
            x,
            y,
            panel_w,
            panel_h,
            content_w,
            game->widgets[i].uid,
            game->widgets[i].file_index,
            &y_cursor);
    }

    game->tree_content_h = y_cursor - (y + IE_ROW_H);
    tree_geom = ie_vertical_scrollbar_geom(
        x, tree_viewport_y, panel_w, tree_visible_h, game->tree_scroll, game->tree_content_h);
    ie_push_vertical_scrollbar(game, IE_SCROLL_TREE, &tree_geom);
}

static void
ie_draw_inv_slot_grid(
    struct GameInterfaceEditor* game,
    int bx,
    int by,
    float scale,
    int cols,
    int rows,
    int margin_x,
    int margin_y,
    int const* offset_x,
    int const* offset_y,
    int const* bg_scene_id,
    int const* bg_atlas_index)
{
    if( !game || cols <= 0 || rows <= 0 )
        return;

    int const pitch = (int)(UI_INV_SLOT_ICON_SIZE * scale);
    if( pitch < 4 )
        return;
    int const margin_x_s = (int)(margin_x * scale);
    int const margin_y_s = (int)(margin_y * scale);

    int slot = 0;
    for( int row = 0; row < rows; row++ )
    {
        for( int col = 0; col < cols; col++ )
        {
            int slot_x = bx + col * (margin_x_s + pitch);
            int slot_y = by + row * (margin_y_s + pitch);
            if( offset_x && offset_y && slot < UI_INV_SLOT_OFFSET_MAX )
            {
                slot_x += (int)(offset_x[slot] * scale);
                slot_y += (int)(offset_y[slot] * scale);
            }

            int scene_id = -1;
            int atlas_index = 0;
            if( bg_scene_id && slot < UI_INV_SLOT_OFFSET_MAX && bg_scene_id[slot] >= 0 )
            {
                scene_id = bg_scene_id[slot];
                atlas_index = bg_atlas_index ? bg_atlas_index[slot] : 0;
            }

            if( scene_id >= 0 )
                ie_push_draw_sprite(
                    game, slot_x, slot_y, pitch, pitch, scene_id, atlas_index, 255, 0);
            slot++;
        }
    }
}

static void
ie_draw_widget_preview(
    struct GameInterfaceEditor* game,
    struct InterfaceEditorWidget* w,
    int off_x,
    int off_y,
    float scale)
{
    RSCacheDat2A_Component* c = &w->data;
    if( c->hidden )
        return;

    int bx = off_x + (int)(w->lay_x * scale);
    int by = off_y + (int)(w->lay_y * scale);
    int bw = (int)(w->lay_w * scale);
    int bh = (int)(w->lay_h * scale);
    if( bw <= 0 || bh <= 0 )
        return;

    int alpha = 255 - (c->transparency * 255 / 256);
    if( alpha < 0 )
        alpha = 0;

    if( c->type == 3 )
    {
        int const colour = 0xFF000000 | (c->color & 0xFFFFFF);
        if( c->fill )
            ie_push_draw_fill(game, bx, by, bw, bh, colour);
        else
        {
            int const lw = 1;
            ie_push_draw_fill(game, bx, by, bw, lw, colour);
            ie_push_draw_fill(game, bx, by + bh - lw, bw, lw, colour);
            ie_push_draw_fill(game, bx, by, lw, bh, colour);
            ie_push_draw_fill(game, bx + bw - lw, by, lw, bh, colour);
        }
    }
    else if( c->type == 4 && c->text && c->text[0] )
    {
        int font_id = ie_resolve_scene_font(game, c->textFont);
        if( font_id >= 0 )
            ie_push_draw_font(
                game,
                bx,
                by,
                bw,
                bh,
                font_id,
                c->text,
                c->color & 0xFFFFFF,
                c->textHorizontalAlignment,
                c->textShadow,
                c->textLineHeight,
                c->textVerticalAlignment);
    }
    else if( c->type == 5 && c->graphic >= 0 )
    {
        int element_id = -1;
        int frame_count = 0;
        if( ie_resolve_sprite(game, c->graphic, &element_id, &frame_count) )
            ie_push_draw_sprite(game, bx, by, bw, bh, element_id, 0, alpha, c->tiled ? 1 : 0);
    }
    else if( c->type == 2 )
    {
        int bg_scene[UI_INV_SLOT_OFFSET_MAX];
        int bg_atlas[UI_INV_SLOT_OFFSET_MAX];
        for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
        {
            bg_scene[si] = -1;
            bg_atlas[si] = 0;
            if( c->invSlotGraphicId && c->invSlotGraphicId[si] >= 0 )
            {
                int element_id = -1;
                int frame_count = 0;
                if( ie_resolve_sprite(game, c->invSlotGraphicId[si], &element_id, &frame_count) )
                    bg_scene[si] = element_id;
            }
        }
        ie_draw_inv_slot_grid(
            game,
            bx,
            by,
            scale,
            c->baseWidth > 0 ? c->baseWidth : 1,
            c->baseHeight > 0 ? c->baseHeight : 1,
            c->marginX,
            c->marginY,
            c->invSlotOffsetX,
            c->invSlotOffsetY,
            bg_scene,
            bg_atlas);
    }
    else if( c->type == 9 )
    {
        int const colour = 0xFF000000 | (c->color & 0xFFFFFF);
        int const lw = c->lineWidth > 0 ? c->lineWidth : 1;
        int x0 = bx;
        int y0 = by;
        int x1 = bx + bw - 1;
        int y1 = by + bh - 1;
        if( !c->lineDirection )
        {
            x1 = bx + bw - 1;
            y1 = by;
        }
        int dx = x1 - x0;
        int dy = y1 - y0;
        int sx = dx >= 0 ? 1 : -1;
        int sy = dy >= 0 ? 1 : -1;
        dx = dx >= 0 ? dx : -dx;
        dy = dy >= 0 ? dy : -dy;
        int err = dx - dy;
        for( ;; )
        {
            ie_push_draw_fill(game, x0, y0, lw, lw, colour);
            if( x0 == x1 && y0 == y1 )
                break;
            int e2 = 2 * err;
            if( e2 > -dy )
            {
                err -= dy;
                x0 += sx;
            }
            if( e2 < dx )
            {
                err += dx;
                y0 += sy;
            }
        }
    }

    if( w->uid == game->selected_uid )
    {
        ie_push_draw_fill(game, bx, by, bw, 2, 0xFF4A9EFF);
        ie_push_draw_fill(game, bx, by + bh - 2, bw, 2, 0xFF4A9EFF);
        ie_push_draw_fill(game, bx, by, 2, bh, 0xFF4A9EFF);
        ie_push_draw_fill(game, bx + bw - 2, by, 2, bh, 0xFF4A9EFF);
    }

    ie_push_hit(game, IEHIT_CANVAS_WIDGET, bx, by, bw, bh, w->uid, 0);
}

static bool
ie_is_orphan_graphic(RSCacheDat2A_Component const* comp);

static void
ie_draw_orphan_widget_graphics(
    struct GameInterfaceEditor* game,
    int off_x,
    int off_y,
    float scale);

static void
ie_draw_tree_node_preview(
    struct GameInterfaceEditor* game,
    struct StaticUIComponent const* node,
    int off_x,
    int off_y,
    float scale);

static void
ie_draw_cs2_tree_preview(
    struct GameInterfaceEditor* game,
    int canvas_x,
    int canvas_y,
    float scale)
{
    if( !game || !game->cs2_tree || game->cs2_tree->component_count == 0 )
        return;

    struct UITree* tree = game->cs2_tree;
    int32_t stack[UITREE_WALK_STACK_MAX];
    int stack_top = -1;
    int32_t current = tree->root_index;
    while( current >= 0 )
    {
        struct StaticUIComponent const* node = &tree->components[current];
        bool const visible = !node->behavior.hide;
        if( visible )
            ie_draw_tree_node_preview(game, node, canvas_x, canvas_y, scale);
        uitree_walk_advance(
            tree, &current, stack, &stack_top, UITREE_WALK_STACK_MAX, visible);
    }

    ie_draw_orphan_widget_graphics(game, canvas_x, canvas_y, scale);
}

static bool
ie_is_orphan_graphic(RSCacheDat2A_Component const* comp)
{
    return comp && comp->type == 5 && comp->layer < 0 && comp->graphic >= 0 && !comp->hidden;
}

static void
ie_draw_orphan_widget_graphics(
    struct GameInterfaceEditor* game,
    int off_x,
    int off_y,
    float scale)
{
    if( !game )
        return;

    for( int i = 0; i < game->widget_count; i++ )
    {
        struct InterfaceEditorWidget* w = &game->widgets[i];
        if( !ie_is_orphan_graphic(&w->data) )
            continue;

        int bx = off_x + (int)(w->lay_x * scale);
        int by = off_y + (int)(w->lay_y * scale);
        int bw = (int)(w->lay_w * scale);
        int bh = (int)(w->lay_h * scale);
        if( bw <= 0 || bh <= 0 )
            continue;

        int alpha = 255 - (w->data.transparency * 255 / 256);
        if( alpha < 0 )
            alpha = 0;

        int element_id = -1;
        int frame_count = 0;
        if( ie_resolve_sprite(game, w->data.graphic, &element_id, &frame_count) )
            ie_push_draw_sprite(game, bx, by, bw, bh, element_id, 0, alpha, w->data.tiled ? 1 : 0);
    }
}

static void
ie_draw_tree_node_preview(
    struct GameInterfaceEditor* game,
    struct StaticUIComponent const* node,
    int off_x,
    int off_y,
    float scale)
{
    if( !game || !node || node->behavior.hide )
        return;

    int bx = off_x + (int)(node->position.abs_x * scale);
    int by = off_y + (int)(node->position.abs_y * scale);
    int bw = (int)((node->position.abs_w > 0 ? node->position.abs_w : 1) * scale);
    int bh = (int)((node->position.abs_h > 0 ? node->position.abs_h : 1) * scale);
    if( bw <= 0 || bh <= 0 )
        return;

    if( node->type == UIELEM_CC_OBJ || (node->type == UIELEM_RS_GRAPHIC && node->item_id > 0) )
    {
        int obj_id = ie_tree_node_obj_id(node);
        if( obj_id > 0 )
        {
            int scene_id = node->item_scene_id;
            int atlas_index = node->item_atlas_index;
            if( scene_id < 0 )
                scene_id = ie_resolve_obj_icon_sprite(game, obj_id);
            if( scene_id >= 0 )
            {
                int icon = (int)(UI_INV_SLOT_ICON_SIZE * scale);
                if( icon < 8 )
                    icon = 8;
                int ix = bx + (bw - icon) / 2;
                int iy = by + (bh - icon) / 2;
                ie_push_draw_sprite(game, ix, iy, icon, icon, scene_id, atlas_index, 255, 0);
            }
        }
    }

    struct CS1Host cs1host;
    ie_fill_cs1_host(game, &cs1host);
    bool const active =
        ToriAuxLibVM_IsActive(game->cs1vm_wrap, &cs1host, (struct StaticUIComponent*)node);

    switch( node->type )
    {
    case UIELEM_RS_RECT:
        {
            int alpha = 255 - node->trans;
            if( alpha < 0 )
                alpha = 0;
            else if( alpha > 255 )
                alpha = 255;
            int const colour =
                (alpha << 24) | (node->u.rs_rect.color & 0xFFFFFF);
            if( node->u.rs_rect.filled )
                ie_push_draw_fill(game, bx, by, bw, bh, colour);
            else
            {
                int const lw = 1;
                ie_push_draw_fill(game, bx, by, bw, lw, colour);
                ie_push_draw_fill(game, bx, by + bh - lw, bw, lw, colour);
                ie_push_draw_fill(game, bx, by, lw, bh, colour);
                ie_push_draw_fill(game, bx + bw - lw, by, lw, bh, colour);
            }
        }
        break;
    case UIELEM_RS_TEXT:
        {
            char const* text = node->u.rs_text.text;
            if( active && node->u.rs_text.text_active && node->u.rs_text.text_active[0] )
                text = node->u.rs_text.text_active;
            if( !text || !text[0] )
                break;
            int color = node->u.rs_text.color;
            if( active && node->behavior.active_color != 0 )
                color = node->behavior.active_color;
            int font_id = ie_resolve_scene_font(game, node->u.rs_text.font_id);
            if( font_id >= 0 )
                ie_push_draw_font(
                    game,
                    bx,
                    by,
                    bw,
                    bh,
                    font_id,
                    text,
                    color & 0xFFFFFF,
                    node->u.rs_text.center,
                    node->u.rs_text.shadowed,
                    node->u.rs_text.line_height,
                    node->u.rs_text.y_align);
        }
        break;
    case UIELEM_RS_GRAPHIC:
        if( node->item_id > 0 )
            break;
        if( node->u.rs_graphic.graphic_hitbox_only )
            break;
        {
            int alpha = 255 - node->trans;
            if( alpha < 0 )
                alpha = 0;
            else if( alpha > 255 )
                alpha = 255;

            int scene_id = node->u.rs_graphic.scene_id;
            int atlas_index = node->u.rs_graphic.atlas_index;
            if( active && node->u.rs_graphic.scene_id_active >= 0 )
            {
                scene_id = node->u.rs_graphic.scene_id_active;
                atlas_index = node->u.rs_graphic.atlas_index_active;
            }
            if( scene_id < 0 )
                break;
            int element_id = -1;
            int frame_count = 0;
            if( ie_resolve_sprite(game, scene_id, &element_id, &frame_count) )
                ie_push_draw_sprite(
                    game, bx, by, bw, bh, element_id, atlas_index, alpha, node->u.rs_graphic.tiled);
            else
                ie_push_draw_sprite(
                    game, bx, by, bw, bh, scene_id, atlas_index, alpha, node->u.rs_graphic.tiled);
        }
        break;
    case UIELEM_RS_LINE:
        {
            int const colour = 0xFF000000 | (node->u.rs_line.color & 0xFFFFFF);
            int const lw = node->u.rs_line.line_width > 0 ? node->u.rs_line.line_width : 1;
            int x0 = bx;
            int y0 = by;
            int x1 = bx + bw - 1;
            int y1 = by + bh - 1;
            if( node->u.rs_line.horizontal )
            {
                x1 = bx + bw - 1;
                y1 = by;
            }
            int dx = x1 - x0;
            int dy = y1 - y0;
            int sx = dx >= 0 ? 1 : -1;
            int sy = dy >= 0 ? 1 : -1;
            dx = dx >= 0 ? dx : -dx;
            dy = dy >= 0 ? dy : -dy;
            int err = dx - dy;
            for( ;; )
            {
                ie_push_draw_fill(game, x0, y0, lw, lw, colour);
                if( x0 == x1 && y0 == y1 )
                    break;
                int e2 = 2 * err;
                if( e2 > -dy )
                {
                    err -= dy;
                    x0 += sx;
                }
                if( e2 < dx )
                {
                    err += dx;
                    y0 += sy;
                }
            }
        }
        break;
    case UIELEM_INV_GRID:
        ie_draw_inv_slot_grid(
            game,
            bx,
            by,
            scale,
            node->u.inv_grid.cols > 0 ? node->u.inv_grid.cols : 1,
            node->u.inv_grid.rows > 0 ? node->u.inv_grid.rows : 1,
            node->u.inv_grid.margin_x,
            node->u.inv_grid.margin_y,
            node->u.inv_grid.inv_slot_offset_x,
            node->u.inv_grid.inv_slot_offset_y,
            node->u.inv_grid.inv_slot_bg_scene_id,
            node->u.inv_grid.inv_slot_bg_atlas_index);
        break;
    default:
        break;
    }

    if( node->type == UIELEM_RS_LINE || node->type == UIELEM_RS_GRAPHIC )
    {
        ie_push_hit(game, IEHIT_CANVAS_WIDGET, bx, by, bw, bh, node->component_id, 0);
        if( game->selected_uid == node->component_id )
        {
            ie_push_draw_fill(game, bx, by, bw, 2, 0xFF4A9EFF);
            ie_push_draw_fill(game, bx, by + bh - 2, bw, 2, 0xFF4A9EFF);
            ie_push_draw_fill(game, bx, by, 2, bh, 0xFF4A9EFF);
            ie_push_draw_fill(game, bx + bw - 2, by, 2, bh, 0xFF4A9EFF);
        }
    }
}

static void
ie_build_preview_panel(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int w,
    int h)
{
    ie_push_draw_fill(game, x, y, w, h, 0xFF141414);

    int const padding = 8;
    int canvas_w = w - padding * 2;
    int canvas_h = h - IE_ROW_H - padding * 2;
    if( canvas_w < 320 )
        canvas_w = 320;
    if( canvas_h < 240 )
        canvas_h = 240;

    int canvas_x = x + (w - canvas_w) / 2;
    int canvas_y = y + IE_ROW_H + (h - IE_ROW_H - canvas_h) / 2;

    bool const layout_changed =
        game->preview_layout_w != canvas_w || game->preview_layout_h != canvas_h;

    game->preview_x = canvas_x;
    game->preview_y = canvas_y;
    game->preview_w = canvas_w;
    game->preview_h = canvas_h;
    game->preview_scale = 1.0f;
    game->preview_layout_w = canvas_w;
    game->preview_layout_h = canvas_h;

    if( layout_changed )
    {
        if( game->scripts_ran && game->cs2_tree )
            ie_cs2_relayout_preview(game);
        else if( game->widget_count > 0 )
            ie_relayout(game);
    }

    ie_push_draw_fill(game, canvas_x, canvas_y, canvas_w, canvas_h, 0xFF202020);

    if( game->loaded_group_id < 0 )
    {
        ie_push_draw_font_ui(
            game,
            x,
            y + (h - IE_ROW_H) / 2,
            w,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            "Select an interface to preview",
            0x888888,
            1,
            0);
        return;
    }

    if( game->scripts_ran && game->cs2_tree )
    {
        ie_draw_cs2_tree_preview(game, canvas_x, canvas_y, 1.0f);
    }
    else
    {
        for( int k = 0; k < game->draw_order_count; k++ )
        {
            int i = game->draw_order[k];
            ie_draw_widget_preview(game, &game->widgets[i], canvas_x, canvas_y, 1.0f);
        }
    }
}

static void
ie_build_list_panel(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int w,
    int h)
{
    ie_push_draw_fill(game, x, y, w, h, 0xFF1E1E1E);
    ie_push_draw_font_ui(
        game,
        x + 4,
        y + 4,
        w - 8,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "Interfaces",
        0xFFFFFF,
        0,
        0);

    int row_y = y + IE_ROW_H + 4;
    int const list_visible_h = h - IE_ROW_H - 4;
    int const list_viewport_y = row_y;
    game->list_content_h = game->group_count * IE_ROW_H;
    game->list_scroll = ie_clamp_scroll(game->list_scroll, game->list_content_h, list_visible_h);

    struct IEVerticalScrollbarGeom const list_geom = ie_vertical_scrollbar_geom(
        x, list_viewport_y, w, list_visible_h, game->list_scroll, game->list_content_h);
    int const row_w = list_geom.content_w - 4;

    for( int i = 0; i < game->group_count; i++ )
    {
        int ry = row_y + i * IE_ROW_H - game->list_scroll;
        if( ry + IE_ROW_H < y + IE_ROW_H || ry >= y + h )
            continue;

        int gid = game->group_ids[i];
        char label[64];
        snprintf(label, sizeof(label), "Interface %d", gid);
        bool selected = gid == game->loaded_group_id;
        ie_push_draw_fill(game, x + 2, ry, row_w, IE_ROW_H, selected ? 0xFF2A3A4A : 0xFF252525);
        ie_push_hit(game, IEHIT_LIST_ROW, x + 2, ry, row_w, IE_ROW_H, gid, 0);
        ie_push_draw_font_ui(
            game,
            x + 8,
            ry,
            list_geom.content_w - 16,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            label,
            selected ? 0x4A9EFF : 0xCCCCCC,
            0,
            0);
    }

    ie_push_vertical_scrollbar(game, IE_SCROLL_LIST, &list_geom);
}

static void
ie_build_sprite_picker(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int w,
    int h)
{
    int cols = 8;
    int thumb = 40;
    int pad = 4;
    int const page_start = game->sprite_picker_page * IE_SPRITE_PICKER_PAGE_SIZE;
    int const page_end = page_start + IE_SPRITE_PICKER_PAGE_SIZE;
    if( page_end > game->sprite_id_count )
    {
        if( page_start >= game->sprite_id_count )
            return;
    }

    ie_push_draw_fill(game, x, y, w, h, 0xE0000000);
    ie_push_draw_fill(game, x + 8, y + 8, w - 16, h - 16, 0xFF2A2A2A);

    char page_label[64];
    int const page_count =
        game->sprite_id_count > 0
            ? (game->sprite_id_count + IE_SPRITE_PICKER_PAGE_SIZE - 1) / IE_SPRITE_PICKER_PAGE_SIZE
            : 1;
    snprintf(
        page_label, sizeof(page_label), "Sprites %d/%d", game->sprite_picker_page + 1, page_count);
    ie_push_draw_font_ui(
        game,
        x + 12,
        y + 8,
        w - 24,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        page_label,
        0xFFFFFF,
        0,
        0);

    if( game->sprite_picker_page > 0 )
    {
        ie_push_draw_fill(game, x + 12, y + h - IE_ROW_H - 8, 48, IE_ROW_H, 0xFF3A3A3A);
        ie_push_hit(game, IEHIT_SPRITE_THUMB, x + 12, y + h - IE_ROW_H - 8, 48, IE_ROW_H, -1, 0);
        ie_push_draw_font_ui(
            game,
            x + 16,
            y + h - IE_ROW_H - 8,
            40,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            "Prev",
            0xFFFFFF,
            0,
            0);
    }
    if( page_end < game->sprite_id_count )
    {
        ie_push_draw_fill(game, x + w - 60, y + h - IE_ROW_H - 8, 48, IE_ROW_H, 0xFF3A3A3A);
        ie_push_hit(
            game, IEHIT_SPRITE_THUMB, x + w - 60, y + h - IE_ROW_H - 8, 48, IE_ROW_H, -2, 0);
        ie_push_draw_font_ui(
            game,
            x + w - 56,
            y + h - IE_ROW_H - 8,
            40,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            "Next",
            0xFFFFFF,
            0,
            0);
    }

    for( int i = page_start; i < page_end && i < game->sprite_id_count; i++ )
    {
        int const local = i - page_start;
        int sprite_id = game->sprite_ids[i];
        int col = local % cols;
        int row = local / cols;
        int tx = x + 16 + col * (thumb + pad);
        int ty = y + 24 + row * (thumb + pad);
        if( ty + thumb > y + h - 8 )
            break;

        ie_push_draw_fill(game, tx, ty, thumb, thumb, 0xFF1A1A1A);
        int element_id = -1;
        int frame_count = 0;
        if( ie_resolve_sprite(game, sprite_id, &element_id, &frame_count) )
            ie_push_draw_sprite(game, tx, ty, thumb, thumb, element_id, 0, 255, 0);
        ie_push_hit(game, IEHIT_SPRITE_THUMB, tx, ty, thumb, thumb, sprite_id, 0);
    }
}

static void
ie_build_add_dialog(struct GameInterfaceEditor* game)
{
    int dw = 320;
    int dh = 200;
    int dx = (IE_WINDOW_W - dw) / 2;
    int dy = (IE_WINDOW_H - dh) / 2;

    ie_push_hit(game, IEHIT_ADD_DIALOG_BACKDROP, 0, 0, IE_WINDOW_W, IE_WINDOW_H, 0, 0);
    ie_push_draw_fill(game, 0, 0, IE_WINDOW_W, IE_WINDOW_H, 0x80000000);
    ie_push_draw_fill(game, dx, dy, dw, dh, 0xFF2A2A2A);

    char parent_buf[64];
    snprintf(parent_buf, sizeof(parent_buf), "Parent: %d", game->add_dialog_parent_uid);
    ie_push_draw_font_ui(
        game,
        dx + 12,
        dy + 8,
        dw - 24,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "Add RSCacheDat2A_Component",
        0xFFFFFF,
        0,
        0);
    ie_push_draw_font_ui(
        game,
        dx + 12,
        dy + 24,
        dw - 24,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        parent_buf,
        0x888888,
        0,
        0);

    char type_label[64];
    snprintf(
        type_label,
        sizeof(type_label),
        "Type: %d - %s",
        game->add_dialog_type,
        widget_type_label(game->add_dialog_type));
    int row = dy + 48;
    ie_push_draw_fill(game, dx + 12, row, dw - 24, IE_ROW_H, 0xFF1E1E1E);
    ie_push_hit(game, IEHIT_ADD_DIALOG_TYPE, dx + 12, row, dw - 24, IE_ROW_H, 0, 0);
    ie_push_draw_font_ui(
        game,
        dx + 16,
        row,
        dw - 32,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        type_label,
        0xCCCCCC,
        0,
        0);

    if( game->add_dialog_type == 5 )
    {
        row += IE_ROW_H + 8;
        char sprite_buf[32];
        snprintf(sprite_buf, sizeof(sprite_buf), "spriteId: %d", game->add_dialog_sprite_id);
        ie_push_draw_fill(game, dx + 12, row, dw - 80, IE_ROW_H, 0xFF1E1E1E);
        ie_push_hit(game, IEHIT_ADD_DIALOG_SPRITE_FIELD, dx + 12, row, dw - 80, IE_ROW_H, 0, 0);
        ie_push_draw_font_ui(
            game,
            dx + 16,
            row,
            dw - 88,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            sprite_buf,
            0xCCCCCC,
            0,
            0);
        ie_push_draw_fill(game, dx + dw - 64, row, 52, IE_ROW_H, 0xFF4A9EFF);
        ie_push_hit(
            game, IEHIT_ADD_DIALOG_SPRITE_PICKER_BTN, dx + dw - 64, row, 52, IE_ROW_H, 0, 0);
        ie_push_draw_font_ui(
            game,
            dx + dw - 58,
            row,
            46,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            "Pick",
            0xFFFFFF,
            0,
            0);
    }

    int btn_y = dy + dh - 36;
    ie_push_draw_fill(game, dx + 12, btn_y, 80, 24, 0xFF3A3A3A);
    ie_push_hit(game, IEHIT_ADD_DIALOG_CANCEL, dx + 12, btn_y, 80, 24, 0, 0);
    ie_push_draw_font_ui(
        game,
        dx + 28,
        btn_y + 4,
        60,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "Cancel",
        0xFFFFFF,
        0,
        0);

    ie_push_draw_fill(game, dx + dw - 92, btn_y, 80, 24, 0xFF4A9EFF);
    ie_push_hit(game, IEHIT_ADD_DIALOG_ADD, dx + dw - 92, btn_y, 80, 24, 0, 0);
    ie_push_draw_font_ui(
        game,
        dx + dw - 72,
        btn_y + 4,
        60,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "Add",
        0xFFFFFF,
        0,
        0);
}

static void
ie_build_dropdown(struct GameInterfaceEditor* game)
{
    if( !game->dropdown.visible )
        return;

    ie_push_draw_fill(
        game,
        game->dropdown.x,
        game->dropdown.y,
        game->dropdown.width,
        game->dropdown.height,
        0xFF2A2A2A);
    for( int i = 0; i < game->dropdown.option_count; i++ )
    {
        int oy = ui_minimenu_option_y(&game->dropdown, i);
        int hovered = game->dropdown.hovered_option == i;
        ie_push_draw_font_ui(
            game,
            game->dropdown.x + 8,
            oy - 10,
            game->dropdown.width - 16,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            game->dropdown.options[i].text,
            hovered ? 0xFFFF00 : 0xFFFFFF,
            0,
            0);
    }
    ie_push_hit(
        game,
        IEHIT_MINIMENU,
        game->dropdown.x,
        game->dropdown.y,
        game->dropdown.width,
        game->dropdown.height,
        0,
        0);
}

static void
ie_build_draw_list(struct GameInterfaceEditor* game)
{
    game->draw_count = 0;
    game->hit_count = 0;

    ie_push_draw_fill(game, 0, 0, IE_WINDOW_W, IE_WINDOW_H, 0xFF121212);
    ie_push_draw_fill(game, 0, 0, IE_WINDOW_W, IE_TOOLBAR_H, 0xFF1A1A1A);
    ie_push_draw_font_ui(
        game, 8, 4, 200, IE_ROW_H, ie_default_ui_font_id(game), "Interface Editor", 0xFFFFFF, 0, 0);

    ie_push_draw_fill(game, 220, 2, 110, IE_TOOLBAR_H - 4, 0xFF4A9EFF);
    ie_push_hit(game, IEHIT_TOOLBAR_RUN_SCRIPTS, 220, 2, 110, IE_TOOLBAR_H - 4, 0, 0);
    ie_push_draw_font_ui(
        game, 228, 4, 100, IE_ROW_H, ie_default_ui_font_id(game), "Rerun Scripts", 0xFFFFFF, 0, 0);

    int const trace_color = game->cs2_trace_enabled ? 0xFF3A6A3A : 0xFF3A3A3A;
    ie_push_draw_fill(game, 336, 2, 64, IE_TOOLBAR_H - 4, trace_color);
    ie_push_hit(game, IEHIT_TOOLBAR_TRACE, 336, 2, 64, IE_TOOLBAR_H - 4, 0, 0);
    char trace_label[16];
    snprintf(trace_label, sizeof(trace_label), "Trace %s", game->cs2_trace_enabled ? "ON" : "OFF");
    ie_push_draw_font_ui(
        game, 340, 4, 56, IE_ROW_H, ie_default_ui_font_id(game), trace_label, 0xFFFFFF, 0, 0);

    char status[IE_SCRIPT_ERROR_LEN + 64];
    if( game->script_error_count > 0 )
    {
        if( game->script_error_count > 1 )
        {
            snprintf(
                status,
                sizeof(status),
                "CS2 error: %s (+%d more, see log)",
                game->script_error,
                game->script_error_count - 1);
        }
        else
            snprintf(status, sizeof(status), "CS2 error: %s", game->script_error);
    }
    else if( game->scripts_ran )
        snprintf(status, sizeof(status), "CS1/CS2 preview: on (scripts ran)");
    else
        snprintf(status, sizeof(status), "CS1/CS2 preview: waiting for load");
    ie_push_draw_font_ui(
        game,
        408,
        4,
        IE_WINDOW_W - 418,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        status,
        game->script_error_count > 0 ? 0xFF6666 : 0x66FF66,
        0,
        0);

    int const content_y = IE_TOOLBAR_H;
    int const content_h = IE_WINDOW_H - IE_TOOLBAR_H;
    int const center_w = IE_WINDOW_W - IE_LEFT_PANEL_W - IE_RIGHT_PANEL_W;
    int const right_x = IE_LEFT_PANEL_W + center_w;
    int const right_split = content_y + content_h / 2;

    ie_build_list_panel(game, 0, content_y, IE_LEFT_PANEL_W, content_h);
    ie_build_preview_panel(game, IE_LEFT_PANEL_W, content_y, center_w, content_h);
    ie_build_tree_panel(game, right_x, content_y, IE_RIGHT_PANEL_W, content_h / 2);

    int const bottom_y = right_split;
    int const bottom_h = content_h - content_h / 2;
    ie_build_right_panel_tabs(game, right_x, bottom_y, IE_RIGHT_PANEL_W);
    if( game->right_tab == IE_TAB_CONTAINERS )
        ie_build_containers_panel(
            game, right_x, bottom_y + IE_ROW_H + 2, IE_RIGHT_PANEL_W, bottom_h - IE_ROW_H - 2);
    else
        ie_build_properties_panel(
            game, right_x, bottom_y + IE_ROW_H + 2, IE_RIGHT_PANEL_W, bottom_h - IE_ROW_H - 2);

    if( game->sprite_picker_open )
        ie_build_sprite_picker(game, right_x, bottom_y, IE_RIGHT_PANEL_W, bottom_h);
    if( game->add_dialog_open )
        ie_build_add_dialog(game);
    ie_build_dropdown(game);
}

static void
ie_emit_font_draw_command(
    struct GameInterfaceEditor const* game,
    struct InterfaceEditorDrawItem const* item,
    struct LibToriRS_RenderCommand* command)
{
    command->kind = TORIRSRC_FONT;
    command->u.font.font_id = item->font_id;
    command->u.font.color = item->color;
    command->u.font.center = item->center;
    command->u.font.y_align = item->y_align;
    command->u.font.line_height = item->line_height;
    command->u.font.shadowed = item->shadowed;
    command->u.font.text = item->text;
    command->u.font.scissor_x = 0;
    command->u.font.scissor_y = 0;
    command->u.font.scissor_w = 0;
    command->u.font.scissor_h = 0;

    if( item->w > 0 && item->h > IE_ROW_H + 2 )
    {
        command->u.font.x = item->x;
        command->u.font.y = item->y;
        command->u.font.width = item->w;
        command->u.font.height = item->h;
        return;
    }

    int lh = 13;
    if( game && game->scene )
    {
        struct ToriDraw_Font* font = ToriDraw_SceneFontGet(game->scene, item->font_id);
        if( font && font->line_height > 0 )
            lh = font->line_height;
    }

    int draw_x = item->x;
    if( item->center && item->w > 0 )
        draw_x = item->x + item->w / 2;

    int draw_y;
    if( item->h > 0 && item->h <= IE_ROW_H + 2 )
        draw_y = item->y + (item->h + lh) / 2;
    else
        draw_y = item->y + lh;

    command->u.font.x = draw_x;
    command->u.font.y = draw_y;
    command->u.font.width = 0;
    command->u.font.height = 0;
}

static bool
ie_translate_gc_event(
    struct ToriDraw_Event const* ev,
    struct LibToriRS_RenderCommand* command)
{
    if( !ev || !command )
        return false;
    memset(command, 0, sizeof(*command));
    switch( ev->kind )
    {
    case TORIDRAW_EVENT_SPRITE_LOAD:
        command->kind = TORIRSRC_SPRITE_LOAD;
        command->u.sprite_load.element_id = ev->element_id;
        command->u.sprite_load.sprites = ev->sprites;
        command->u.sprite_load.count = ev->sprite_count;
        return true;
    case TORIDRAW_EVENT_FONT_LOAD:
        command->kind = TORIRSRC_FONT_LOAD;
        command->u.font_load.font_id = ev->texture_id;
        command->u.font_load.font = ev->font;
        return true;
    default:
        return false;
    }
}

struct GameInterfaceEditor*
GameInterfaceEditor_New(
    struct LibToriRS_ScriptQueue* script_queue,
    struct ToriDraw_Scene* scene)
{
    struct GameInterfaceEditor* game = calloc(1, sizeof(*game));
    if( !game )
        return NULL;
    game->script_queue = script_queue;
    game->scene = scene;
    game->loaded_group_id = -1;
    game->selected_uid = -1;
    game->next_element_id = 1;
    game->next_dynamic_child = 0x8000;
    game->add_dialog_type = 5;
    game->add_dialog_sprite_id = -1;
    game->right_tab = IE_TAB_PROPERTIES;
    game->containers_source_id = -1;
    ui_minimenu_reset(&game->dropdown);

    game->cs1vm_wrap = ToriAuxLibVM_New();
    ui_inv_data_service_init(&game->inv_data);
    ui_inv_data_service_set_change_callback(&game->inv_data, ie_on_inv_container_change, game);
    game->worn_source_id =
        ui_inv_data_service_ensure_container(&game->inv_data, RS_INV_CONTAINER_WORN, 14, "worn");
    game->inventory_source_id = ui_inv_data_service_ensure_container(
        &game->inv_data, RS_INV_CONTAINER_BACKPACK, 28, "inventory");
    (void)ui_inv_data_service_ensure_container(&game->inv_data, 95, 1410, "bank");
    (void)ui_inv_data_service_ensure_container(&game->inv_data, 516, 40, "shop");
    (void)ui_inv_data_service_ensure_container(&game->inv_data, 620, 2048, "collection_log");
    game->containers_source_id = game->worn_source_id;
    game->canvas_drag_uid = -1;
    game->preview_layout_w = IE_PREVIEW_ROOT_W;
    game->preview_layout_h = IE_PREVIEW_ROOT_H;
    game->preview_scale = 1.0f;

    {
        char const* env = getenv("IE_CS2_TRACE");
        game->cs2_trace_enabled = env && env[0] && env[0] != '0';
    }

    return game;
}

void
GameInterfaceEditor_Free(struct GameInterfaceEditor* game)
{
    if( !game )
        return;
    GameInterfaceEditor_FreeScripts(game);
    if( game->cs1vm_wrap )
        ToriAuxLibVM_Free(game->cs1vm_wrap);
    ie_free_widgets(game);
    free(game->group_ids);
    free(game->sprite_ids);
    free(game);
}

void
GameInterfaceEditor_SetCore(
    struct GameInterfaceEditor* game,
    struct ToriAuxLibCore* core)
{
    if( game )
        game->core = core;
}

void
GameInterfaceEditor_SetCache(
    struct GameInterfaceEditor* game,
    struct ToriAuxLibCache* cache)
{
    if( game )
        game->cache = cache;
}

void
GameInterfaceEditor_SetTD(
    struct GameInterfaceEditor* game,
    struct ToriAuxLibTD* td)
{
    if( game )
        game->td = td;
}

void
GameInterfaceEditor_SetDat2Cache(
    struct GameInterfaceEditor* game,
    struct RSCacheDat2Disk* dat2_cache)
{
    if( !game )
        return;
    game->dat2_cache = dat2_cache;
    if( dat2_cache && game->font_cache_count == 0 )
        (void)ie_resolve_scene_font(game, 495);
    if( dat2_cache )
        (void)ie_enumerate_sprite_ids(game);
}

void
GameInterfaceEditor_SetClientscriptDecodeFlags(
    struct GameInterfaceEditor* game,
    int flags)
{
    if( game )
        game->clientscript_decode_flags = flags;
}

bool
GameInterfaceEditor_EnumerateInterfaceGroups(struct GameInterfaceEditor* game)
{
    if( !game || !game->dat2_cache )
        return false;

    free(game->group_ids);
    game->group_ids = NULL;
    game->group_count = 0;

    struct RSCacheDat2Disk_Archive* table_archive = RSCacheDat2Disk_ArchiveNewReferenceTableLoad(
        game->dat2_cache, RSCacheDat2Disk_Table_Interfaces);
    if( !table_archive )
        return false;

    struct RSCacheDat2Disk_ReferenceTable* table =
        RSCacheDat2Disk_ReferenceTableNewDecode(table_archive->data, table_archive->data_size);
    RSCacheDat2Disk_ArchiveFree(table_archive);
    if( !table )
        return false;

    game->group_ids = calloc((size_t)table->id_count, sizeof(int));
    if( !game->group_ids )
    {
        RSCacheDat2Disk_ReferenceTableFree(table);
        return false;
    }

    for( int i = 0; i < table->id_count; i++ )
    {
        int id = table->ids[i];
        struct RSCacheDat2Disk_ArchiveReference* archive = &table->archives[id];
        if( archive->index < 0 )
            continue;
        game->group_ids[game->group_count++] = archive->index;
    }

    RSCacheDat2Disk_ReferenceTableFree(table);

    for( int a = 0; a < game->group_count; a++ )
    {
        for( int b = a + 1; b < game->group_count; b++ )
        {
            if( game->group_ids[b] < game->group_ids[a] )
            {
                int t = game->group_ids[a];
                game->group_ids[a] = game->group_ids[b];
                game->group_ids[b] = t;
            }
        }
    }
    return game->group_count > 0;
}

static bool
ie_enumerate_sprite_ids(struct GameInterfaceEditor* game)
{
    if( !game || !game->dat2_cache )
        return false;

    free(game->sprite_ids);
    game->sprite_ids = NULL;
    game->sprite_id_count = 0;

    struct RSCacheDat2Disk_Archive* table_archive = RSCacheDat2Disk_ArchiveNewReferenceTableLoad(
        game->dat2_cache, RSCacheDat2Disk_Table_Sprites);
    if( !table_archive )
        return false;

    struct RSCacheDat2Disk_ReferenceTable* table =
        RSCacheDat2Disk_ReferenceTableNewDecode(table_archive->data, table_archive->data_size);
    RSCacheDat2Disk_ArchiveFree(table_archive);
    if( !table )
        return false;

    game->sprite_ids = calloc((size_t)table->id_count, sizeof(int));
    if( !game->sprite_ids )
    {
        RSCacheDat2Disk_ReferenceTableFree(table);
        return false;
    }

    for( int i = 0; i < table->id_count; i++ )
    {
        int id = table->ids[i];
        struct RSCacheDat2Disk_ArchiveReference* archive = &table->archives[id];
        if( archive->index < 0 )
            continue;
        game->sprite_ids[game->sprite_id_count++] = archive->index;
    }

    RSCacheDat2Disk_ReferenceTableFree(table);

    for( int a = 0; a < game->sprite_id_count; a++ )
    {
        for( int b = a + 1; b < game->sprite_id_count; b++ )
        {
            if( game->sprite_ids[b] < game->sprite_ids[a] )
            {
                int t = game->sprite_ids[a];
                game->sprite_ids[a] = game->sprite_ids[b];
                game->sprite_ids[b] = t;
            }
        }
    }
    return game->sprite_id_count > 0;
}

void
GameInterfaceEditor_ProcessInput(
    struct GameInterfaceEditor* game,
    struct LibToriRS_Input* input)
{
    if( !game || !input )
        return;

    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_ESCAPE) )
    {
        if( game->dropdown.visible )
            ui_minimenu_hide(&game->dropdown);
        else if( game->sprite_picker_open )
            game->sprite_picker_open = false;
        else if( game->add_dialog_open )
            game->add_dialog_open = false;
        else if( game->text_field.active )
            ie_text_field_end(game);
        return;
    }

    ie_handle_text_input(game, input);

    if( game->dropdown.visible )
        ui_minimenu_update_hover(&game->dropdown, input->curr.mouse_x, input->curr.mouse_y);

    if( LibToriRS_Input_IsMouseDown(input, TORIRSM_LEFT) )
    {
        int px = input->curr.mouse_x;
        int py = input->curr.mouse_y;
        int hit = ie_hit_test(game, px, py);
        if( hit >= 0 && game->hit_list[hit].kind == IEHIT_CANVAS_WIDGET )
            ie_begin_canvas_drag_press(game, input, game->hit_list[hit].param0);
    }

    if( game->canvas_drag_uid >= 0 )
    {
        if( LibToriRS_Input_IsDragging(input, TORIRSM_LEFT) )
        {
            ie_handle_canvas_drag_scripts(
                game, game->canvas_drag_uid, input->curr.mouse_x, input->curr.mouse_y, false);
        }

        if( LibToriRS_Input_IsDragEnd(input, TORIRSM_LEFT) )
        {
            ie_handle_canvas_drag_scripts(
                game, game->canvas_drag_uid, input->curr.mouse_x, input->curr.mouse_y, true);
            ie_restore_drag_thresholds(game, input);
        }
        else if( input->curr.mouse_button_up[TORIRSM_LEFT] )
            ie_restore_drag_thresholds(game, input);
    }

    if( LibToriRS_Input_IsClick(input, TORIRSM_LEFT) )
    {
        int cx = input->last_click_x[TORIRSM_LEFT];
        int cy = input->last_click_y[TORIRSM_LEFT];
        int hit = ie_hit_test(game, cx, cy);
        ie_handle_hit(game, hit, cx, cy);
    }

    ie_scrollbar_drag_update(game, input);

    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_UP) ||
        LibToriRS_Input_IsKeyDown(input, TORIRSK_DOWN) )
    {
        int dir = LibToriRS_Input_IsKeyDown(input, TORIRSK_UP) ? -1 : 1;
        ie_apply_scroll(game, input->curr.mouse_x, input->curr.mouse_y, dir * IE_ROW_H);
    }

    if( input->curr.mouse_wheel_y != 0 )
    {
        ie_apply_scroll(
            game,
            input->curr.mouse_x,
            input->curr.mouse_y,
            -input->curr.mouse_wheel_y * IE_ROW_H * IE_WHEEL_SCROLL_ROWS);
    }
}

void
GameInterfaceEditor_FrameBegin(
    struct GameInterfaceEditor* game,
    int cycles_elapsed)
{
    if( !game )
        return;
    game->script_tick_accum_ms += cycles_elapsed > 0 ? cycles_elapsed : 1;
    while( game->script_tick_accum_ms >= 20 )
    {
        game->script_tick_accum_ms -= 20;
        ie_script_tick(game);
    }
    game->frame.phase = IE_FRAME_GC_EVENTS;
    game->frame.event_index = 0;
    game->frame.draw_index = 0;
    ie_build_draw_list(game);
}

bool
GameInterfaceEditor_FrameNextCommand(
    struct GameInterfaceEditor* game,
    struct LibToriRS_RenderCommand* command)
{
    if( !game || !command )
        return false;
    memset(command, 0, sizeof(*command));

    for( ;; )
    {
        switch( game->frame.phase )
        {
        case IE_FRAME_GC_EVENTS:
        {
            struct ToriDraw_EventQueue* eq = game->scene ? ToriDraw_SceneEvents(game->scene) : NULL;
            if( eq )
            {
                while( game->frame.event_index < eq->count )
                {
                    struct ToriDraw_Event const* ev = &eq->events[game->frame.event_index++];
                    if( ie_translate_gc_event(ev, command) )
                        return true;
                }
            }
            game->frame.phase = IE_FRAME_BEGIN_2D;
            continue;
        }
        case IE_FRAME_BEGIN_2D:
            command->kind = TORIRSRC_BEGIN_2D;
            game->frame.phase = IE_FRAME_DRAW_LIST;
            return true;
        case IE_FRAME_DRAW_LIST:
        {
            if( game->frame.draw_index >= game->draw_count )
            {
                game->frame.phase = IE_FRAME_END_2D;
                continue;
            }
            struct InterfaceEditorDrawItem const* item = &game->draw_list[game->frame.draw_index++];
            switch( item->kind )
            {
            case IEDRAW_FILL_RECT:
                command->kind = TORIRSRC_FILL_RECT;
                command->u.fill_rect.x = item->x;
                command->u.fill_rect.y = item->y;
                command->u.fill_rect.w = item->w;
                command->u.fill_rect.h = item->h;
                command->u.fill_rect.argb = item->argb;
                return true;
            case IEDRAW_FONT:
                if( item->font_id < 0 )
                    continue;
                ie_emit_font_draw_command(game, item, command);
                return true;
            case IEDRAW_SPRITE:
                command->kind = TORIRSRC_SPRITE;
                command->u.sprite.element_id = item->sprite_element_id;
                command->u.sprite.atlas_index = item->sprite_atlas_index;
                command->u.sprite.x = item->x;
                command->u.sprite.y = item->y;
                command->u.sprite.w = item->w;
                command->u.sprite.h = item->h;
                command->u.sprite.alpha = item->sprite_alpha;
                command->u.sprite.tiled = item->tiled;
                return true;
            default:
                continue;
            }
        }
        case IE_FRAME_END_2D:
            command->kind = TORIRSRC_END_2D;
            game->frame.phase = IE_FRAME_DONE;
            return true;
        case IE_FRAME_DONE:
        default:
            return false;
        }
    }
}

void
GameInterfaceEditor_FrameEnd(struct GameInterfaceEditor* game)
{
    if( !game || !game->scene )
        return;
    ToriDraw_SceneFrameEnd(game->scene);
}
