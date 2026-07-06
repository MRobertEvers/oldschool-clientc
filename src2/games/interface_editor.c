#include "interface_editor.h"

#include "toriauxlib/c/toriauxlibcache_font_convert.h"
#include "toriauxlib/c/toriauxlibcache_sprite_convert.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_scene.h"
#include "ui/ui_if3_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    int shadowed)
{
    if( !text || !text[0] || game->draw_count >= IE_MAX_DRAW_ITEMS )
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
    snprintf(item->text, sizeof(item->text), "%s", text);
}

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
ie_default_ui_font_id(struct GameInterfaceEditor* game)
{
    if( game->font_cache_count > 0 )
        return game->font_cache[0].scene_font_id;
    return 495;
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

    if( !game->dat2_cache || game->font_cache_count >= IE_FONT_CACHE_MAX )
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
    ToriAuxLibTD_Font(game->td, scene_font_id);

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
    Component* out,
    char* data,
    int size,
    int group_id,
    int file_index)
{
    if( !data || size <= 0 )
        return -1;
    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (int8_t*)data, size);
    Component_init(out);
    out->id = (group_id << 16) | (file_index & 0xFFFF);
    if( (unsigned char)data[0] == (unsigned char)255 )
        Component_decodeIf3(out, &buf);
    else
        Component_decodeIf1(out, &buf);
    return 0;
}

static void
ie_layout_one(
    Component* comp,
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
ie_relayout(struct GameInterfaceEditor* game)
{
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
        Component* c = &game->widgets[i].data;
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
    int const root_w = IE_PREVIEW_ROOT_W;
    int const root_h = IE_PREVIEW_ROOT_H;

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
}

static void
ie_free_widgets(struct GameInterfaceEditor* game)
{
    for( int i = 0; i < game->widget_count; i++ )
        Component_free(&game->widgets[i].data);
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
            Component_init(&w->data);
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
    return game->widget_count > 0;
}

static void
ie_apply_type_defaults(
    Component* c,
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
    Component_init(&w->data);
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
    Component* c = &widget->data;
    switch( property_id )
    {
    case IE_PROP_RAW_X:
        snprintf(out, out_size, "%d", c->x);
        break;
    case IE_PROP_RAW_Y:
        snprintf(out, out_size, "%d", c->y);
        break;
    case IE_PROP_RAW_W:
        snprintf(out, out_size, "%d", c->width);
        break;
    case IE_PROP_RAW_H:
        snprintf(out, out_size, "%d", c->height);
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
        snprintf(out, out_size, "%d", c->width);
        break;
    case IE_PROP_GRID_ROWS:
        snprintf(out, out_size, "%d", c->height);
        break;
    case IE_PROP_GRID_X_PITCH:
        snprintf(out, out_size, "%d", c->baseX);
        break;
    case IE_PROP_GRID_Y_PITCH:
        snprintf(out, out_size, "%d", c->baseY);
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
    Component* c = &w->data;
    switch( property_id )
    {
    case IE_PROP_RAW_X:
        c->x = ie_parse_int(value, c->x);
        break;
    case IE_PROP_RAW_Y:
        c->y = ie_parse_int(value, c->y);
        break;
    case IE_PROP_RAW_W:
        c->width = ie_parse_int(value, c->width);
        break;
    case IE_PROP_RAW_H:
        c->height = ie_parse_int(value, c->height);
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
        c->width = ie_parse_int(value, c->width);
        break;
    case IE_PROP_GRID_ROWS:
        c->height = ie_parse_int(value, c->height);
        break;
    case IE_PROP_GRID_X_PITCH:
        c->baseX = ie_parse_int(value, c->baseX);
        break;
    case IE_PROP_GRID_Y_PITCH:
        c->baseY = ie_parse_int(value, c->baseY);
        break;
    default:
        break;
    }
    ie_relayout(game);
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
        else if( ch == '-' )
            key = TORIRSK_MINUS;
        else if( ch == '=' )
            key = TORIRSK_EQUALS;
        else if( ch == '.' )
            key = TORIRSK_PERIOD;
        else if( ch == '#' )
            key = TORIRSK_HASH;

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

static void
ie_show_mode_dropdown(
    struct GameInterfaceEditor* game,
    int property_id,
    int click_x,
    int click_y)
{
    ui_minimenu_reset(&game->dropdown);
    game->dropdown_property_id = property_id;
    for( int i = 0; i <= 5; i++ )
    {
        char label[32];
        snprintf(label, sizeof(label), "Mode %d", i);
        ui_minimenu_add_option(&game->dropdown, label, (enum MinimenuAction)0, i);
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
            game->sprite_picker_open = true;
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
    int* y_cursor,
    char const* label,
    int property_id,
    char const* value,
    bool dropdown)
{
    int y = *y_cursor;
    int row_h = IE_ROW_H;
    int field_x = panel_x + panel_w / 2;
    int field_w = panel_w / 2 - 8;

    ie_push_draw_font(
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
    ie_push_draw_font(
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
    int* y_cursor,
    char const* label,
    bool checked,
    int checkbox_id)
{
    int y = *y_cursor;
    int box = 14;
    int box_x = panel_x + 4;
    ie_push_draw_fill(game, box_x, y + 3, box, box, checked ? 0xFF4A9EFF : 0xFF3A3A3A);
    ie_push_hit(game, IEHIT_PROPERTY_CHECKBOX, box_x, y, box + 120, IE_ROW_H, checkbox_id, 0);
    ie_push_draw_font(
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
ie_build_properties_panel(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int panel_w,
    int panel_h)
{
    (void)panel_h;
    ie_push_draw_fill(game, x, y, panel_w, panel_h, 0xFF1E1E1E);
    ie_push_draw_font(
        game,
        x + 4,
        y + 4 - game->props_scroll,
        panel_w - 8,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "Properties",
        0xFFFFFF,
        0,
        0);

    int cursor_y = y + IE_ROW_H + 4 - game->props_scroll;
    struct InterfaceEditorWidget* sel = ie_find_widget(game, game->selected_uid);
    if( !sel )
    {
        ie_push_draw_font(
            game,
            x + 8,
            cursor_y,
            panel_w - 16,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            "Select a component",
            0x888888,
            0,
            0);
        return;
    }

    Component* c = &sel->data;
    char buf[64];

    snprintf(buf, sizeof(buf), "%d", sel->uid);
    ie_push_property_row(game, x, y, panel_w, &cursor_y, "UID", 0, buf, false);
    snprintf(buf, sizeof(buf), "%d", c->type);
    ie_push_property_row(game, x, y, panel_w, &cursor_y, "Type", 0, buf, false);
    ie_push_checkbox_row(game, x, y, panel_w, &cursor_y, "Hidden", c->hidden, 1);

    snprintf(buf, sizeof(buf), "%d", c->x);
    ie_push_property_row(game, x, y, panel_w, &cursor_y, "rawX", IE_PROP_RAW_X, buf, false);
    snprintf(buf, sizeof(buf), "%d", c->y);
    ie_push_property_row(game, x, y, panel_w, &cursor_y, "rawY", IE_PROP_RAW_Y, buf, false);
    snprintf(buf, sizeof(buf), "%d", c->width);
    ie_push_property_row(game, x, y, panel_w, &cursor_y, "rawWidth", IE_PROP_RAW_W, buf, false);
    snprintf(buf, sizeof(buf), "%d", c->height);
    ie_push_property_row(game, x, y, panel_w, &cursor_y, "rawHeight", IE_PROP_RAW_H, buf, false);
    snprintf(buf, sizeof(buf), "%d", (int)c->xMode);
    ie_push_property_row(game, x, y, panel_w, &cursor_y, "xMode", IE_PROP_X_MODE, buf, true);
    snprintf(buf, sizeof(buf), "%d", (int)c->yMode);
    ie_push_property_row(game, x, y, panel_w, &cursor_y, "yMode", IE_PROP_Y_MODE, buf, true);
    snprintf(buf, sizeof(buf), "%d", (int)c->widthMode);
    ie_push_property_row(game, x, y, panel_w, &cursor_y, "widthMode", IE_PROP_W_MODE, buf, true);
    snprintf(buf, sizeof(buf), "%d", (int)c->heightMode);
    ie_push_property_row(game, x, y, panel_w, &cursor_y, "heightMode", IE_PROP_H_MODE, buf, true);
    snprintf(buf, sizeof(buf), "%d", c->transparency);
    ie_push_property_row(game, x, y, panel_w, &cursor_y, "opacity", IE_PROP_OPACITY, buf, false);

    if( c->type == 0 )
    {
        snprintf(buf, sizeof(buf), "%d", c->scrollWidth);
        ie_push_property_row(
            game, x, y, panel_w, &cursor_y, "scrollWidth", IE_PROP_SCROLL_W, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->scrollHeight);
        ie_push_property_row(
            game, x, y, panel_w, &cursor_y, "scrollHeight", IE_PROP_SCROLL_H, buf, false);
        ie_push_checkbox_row(
            game, x, y, panel_w, &cursor_y, "noClickThrough", c->noClickThrough, 5);
    }
    else if( c->type == 3 )
    {
        ie_push_checkbox_row(game, x, y, panel_w, &cursor_y, "filled", c->fill, 2);
        snprintf(buf, sizeof(buf), "%06X", c->color & 0xFFFFFF);
        ie_push_property_row(game, x, y, panel_w, &cursor_y, "color", IE_PROP_COLOR, buf, false);
    }
    else if( c->type == 4 )
    {
        ie_push_property_row(
            game, x, y, panel_w, &cursor_y, "text", IE_PROP_TEXT, c->text ? c->text : "", false);
        snprintf(buf, sizeof(buf), "%d", c->textFont);
        ie_push_property_row(game, x, y, panel_w, &cursor_y, "fontId", IE_PROP_FONT_ID, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->textLineHeight);
        ie_push_property_row(
            game, x, y, panel_w, &cursor_y, "lineHeight", IE_PROP_LINE_HEIGHT, buf, false);
        ie_push_checkbox_row(game, x, y, panel_w, &cursor_y, "shaded", c->textShadow, 3);
        snprintf(buf, sizeof(buf), "%06X", c->color & 0xFFFFFF);
        ie_push_property_row(game, x, y, panel_w, &cursor_y, "color", IE_PROP_COLOR, buf, false);
    }
    else if( c->type == 5 )
    {
        snprintf(buf, sizeof(buf), "%d", c->graphic);
        ie_push_property_row(
            game, x, y, panel_w, &cursor_y, "spriteId", IE_PROP_SPRITE_ID, buf, false);
        ie_push_draw_fill(
            game, x + panel_w - 60, cursor_y - IE_ROW_H + 2, 50, IE_ROW_H - 4, 0xFF4A9EFF);
        ie_push_hit(
            game, IEHIT_PROPERTY_BUTTON, x + panel_w - 60, cursor_y - IE_ROW_H, 50, IE_ROW_H, 1, 0);
        ie_push_draw_font(
            game,
            x + panel_w - 58,
            cursor_y - IE_ROW_H,
            46,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            "Pick",
            0xFFFFFF,
            0,
            0);
        snprintf(buf, sizeof(buf), "%d", c->angle);
        ie_push_property_row(game, x, y, panel_w, &cursor_y, "angle", IE_PROP_ANGLE, buf, false);
        ie_push_checkbox_row(game, x, y, panel_w, &cursor_y, "tiled", c->tiled, 4);
        ie_push_checkbox_row(game, x, y, panel_w, &cursor_y, "hFlip", c->horizontalFlip, 6);
        ie_push_checkbox_row(game, x, y, panel_w, &cursor_y, "vFlip", c->verticalFlip, 7);
    }
    else if( c->type == 6 )
    {
        snprintf(buf, sizeof(buf), "%d", c->modelId);
        ie_push_property_row(
            game, x, y, panel_w, &cursor_y, "modelId", IE_PROP_MODEL_ID, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->modelZoom);
        ie_push_property_row(game, x, y, panel_w, &cursor_y, "zoom", IE_PROP_ZOOM, buf, false);
    }
    else if( c->type == 9 )
    {
        snprintf(buf, sizeof(buf), "%d", c->lineWidth);
        ie_push_property_row(
            game, x, y, panel_w, &cursor_y, "lineWidth", IE_PROP_LINE_WIDTH, buf, false);
        ie_push_checkbox_row(game, x, y, panel_w, &cursor_y, "horizontal", c->lineDirection, 8);
        snprintf(buf, sizeof(buf), "%06X", c->color & 0xFFFFFF);
        ie_push_property_row(game, x, y, panel_w, &cursor_y, "color", IE_PROP_COLOR, buf, false);
    }
    else if( c->type == 2 )
    {
        snprintf(buf, sizeof(buf), "%d", c->width);
        ie_push_property_row(
            game, x, y, panel_w, &cursor_y, "gridColumns", IE_PROP_GRID_COLS, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->height);
        ie_push_property_row(
            game, x, y, panel_w, &cursor_y, "gridRows", IE_PROP_GRID_ROWS, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->baseX);
        ie_push_property_row(
            game, x, y, panel_w, &cursor_y, "gridXPitch", IE_PROP_GRID_X_PITCH, buf, false);
        snprintf(buf, sizeof(buf), "%d", c->baseY);
        ie_push_property_row(
            game, x, y, panel_w, &cursor_y, "gridYPitch", IE_PROP_GRID_Y_PITCH, buf, false);
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
            bool container = ie_is_container_type(node->data.type);
            char label[128];

            if( container )
            {
                ie_push_hit(game, IEHIT_TREE_EXPAND, indent, row_y, 12, IE_ROW_H, node->uid, 0);
                ie_push_draw_font(
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
                    "Component %d (%s)",
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
            ie_push_draw_font(
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
                ie_push_draw_font(
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
        }

        *y_cursor += IE_ROW_H;
        if( ie_is_container_type(node->data.type) && ie_is_expanded(game, node->uid) )
            ie_build_tree_rows(game, x, y, panel_w, panel_h, node->uid, depth + 1, y_cursor);
    }
}

static void
ie_build_tree_panel(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int w,
    int h)
{
    ie_push_draw_fill(game, x, y, w, h, 0xFF1E1E1E);
    ie_push_draw_font(
        game,
        x + 4,
        y + 4,
        w - 8,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "Components",
        0xFFFFFF,
        0,
        0);

    int y_cursor = y + IE_ROW_H + 8;
    if( game->loaded_group_id < 0 )
        return;

    int root_uid = game->widget_count > 0 ? game->widgets[0].uid : -1;
    ie_build_tree_rows(game, x, y + IE_ROW_H, w, h - IE_ROW_H, root_uid, 0, &y_cursor);
}

static void
ie_draw_widget_preview(
    struct GameInterfaceEditor* game,
    struct InterfaceEditorWidget* w,
    int off_x,
    int off_y,
    float scale)
{
    Component* c = &w->data;
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
        if( c->fill )
            ie_push_draw_fill(game, bx, by, bw, bh, 0xFF000000 | (c->color & 0xFFFFFF));
        else
            ie_push_draw_fill(game, bx, by, bw, 1, 0xFF000000 | (c->color & 0xFFFFFF));
    }
    else if( c->type == 4 && c->text && c->text[0] )
    {
        int font_id = ie_resolve_scene_font(game, c->textFont);
        ie_push_draw_font(
            game,
            bx,
            by,
            bw,
            bh,
            font_id,
            c->text,
            c->color & 0xFFFFFF,
            c->textHorizontalAlignment == 1,
            c->textShadow);
    }
    else if( c->type == 5 && c->graphic >= 0 )
    {
        int element_id = -1;
        int frame_count = 0;
        if( ie_resolve_sprite(game, c->graphic, &element_id, &frame_count) )
            ie_push_draw_sprite(game, bx, by, bw, bh, element_id, 0, alpha, c->tiled ? 1 : 0);
    }
    else if( c->type == 9 )
    {
        if( c->lineDirection )
            ie_push_draw_fill(
                game,
                bx,
                by + bh / 2,
                bw,
                c->lineWidth > 0 ? c->lineWidth : 1,
                0xFF000000 | (c->color & 0xFFFFFF));
        else
            ie_push_draw_fill(
                game,
                bx + bw / 2,
                by,
                c->lineWidth > 0 ? c->lineWidth : 1,
                bh,
                0xFF000000 | (c->color & 0xFFFFFF));
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

static void
ie_build_preview_panel(
    struct GameInterfaceEditor* game,
    int x,
    int y,
    int w,
    int h)
{
    ie_push_draw_fill(game, x, y, w, h, 0xFF141414);

    int pw = IE_PREVIEW_ROOT_W;
    int ph = IE_PREVIEW_ROOT_H;
    float scale_x = (float)(w - 16) / (float)pw;
    float scale_y = (float)(h - 32) / (float)ph;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    if( scale > 1.0f )
        scale = 1.0f;

    int canvas_w = (int)(pw * scale);
    int canvas_h = (int)(ph * scale);
    int canvas_x = x + (w - canvas_w) / 2;
    int canvas_y = y + IE_ROW_H + (h - IE_ROW_H - canvas_h) / 2;

    game->preview_x = canvas_x;
    game->preview_y = canvas_y;
    game->preview_w = canvas_w;
    game->preview_h = canvas_h;
    game->preview_scale = scale;

    ie_push_draw_fill(game, canvas_x, canvas_y, canvas_w, canvas_h, 0xFF202020);

    if( game->loaded_group_id < 0 )
    {
        ie_push_draw_font(
            game,
            x + 8,
            y + h / 2,
            w - 16,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            "Select an interface to preview",
            0x888888,
            1,
            0);
        return;
    }

    for( int k = 0; k < game->draw_order_count; k++ )
    {
        int i = game->draw_order[k];
        ie_draw_widget_preview(game, &game->widgets[i], canvas_x, canvas_y, scale);
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
    ie_push_draw_font(
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
    for( int i = 0; i < game->group_count; i++ )
    {
        int ry = row_y + i * IE_ROW_H - game->list_scroll;
        if( ry + IE_ROW_H < y + IE_ROW_H || ry >= y + h )
            continue;

        int gid = game->group_ids[i];
        char label[64];
        snprintf(label, sizeof(label), "Interface %d", gid);
        bool selected = gid == game->loaded_group_id;
        ie_push_draw_fill(game, x + 2, ry, w - 4, IE_ROW_H, selected ? 0xFF2A3A4A : 0xFF252525);
        ie_push_hit(game, IEHIT_LIST_ROW, x + 2, ry, w - 4, IE_ROW_H, gid, 0);
        ie_push_draw_font(
            game,
            x + 8,
            ry,
            w - 16,
            IE_ROW_H,
            ie_default_ui_font_id(game),
            label,
            selected ? 0x4A9EFF : 0xCCCCCC,
            0,
            0);
    }
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
    int start_id = game->sprite_picker_page * IE_SPRITE_PICKER_PAGE_SIZE;

    ie_push_draw_fill(game, x, y, w, h, 0xE0000000);
    ie_push_draw_fill(game, x + 8, y + 8, w - 16, h - 16, 0xFF2A2A2A);

    for( int i = 0; i < IE_SPRITE_PICKER_PAGE_SIZE; i++ )
    {
        int sprite_id = start_id + i;
        int col = i % cols;
        int row = i / cols;
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

    ie_push_draw_fill(game, 0, 0, IE_WINDOW_W, IE_WINDOW_H, 0x80000000);
    ie_push_draw_fill(game, dx, dy, dw, dh, 0xFF2A2A2A);

    ie_push_draw_font(
        game,
        dx + 12,
        dy + 8,
        dw - 24,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "Add Component",
        0xFFFFFF,
        0,
        0);

    char type_label[64];
    snprintf(
        type_label,
        sizeof(type_label),
        "Type: %d - %s",
        game->add_dialog_type,
        widget_type_label(game->add_dialog_type));
    int row = dy + 40;
    ie_push_draw_fill(game, dx + 12, row, dw - 24, IE_ROW_H, 0xFF1E1E1E);
    ie_push_hit(game, IEHIT_ADD_DIALOG_TYPE, dx + 12, row, dw - 24, IE_ROW_H, 0, 0);
    ie_push_draw_font(
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
        ie_push_draw_font(
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
        ie_push_draw_font(
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
    ie_push_draw_font(
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
    ie_push_draw_font(
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
        ie_push_draw_font(
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
    ie_push_draw_font(
        game, 8, 4, 400, IE_ROW_H, ie_default_ui_font_id(game), "Interface Editor", 0xFFFFFF, 0, 0);
    ie_push_draw_font(
        game,
        IE_WINDOW_W - 420,
        4,
        400,
        IE_ROW_H,
        ie_default_ui_font_id(game),
        "In-memory only (no CS1/CS2, no save)",
        0x66FF66,
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
    ie_build_properties_panel(
        game, right_x, right_split, IE_RIGHT_PANEL_W, content_h - content_h / 2);

    if( game->sprite_picker_open )
        ie_build_sprite_picker(
            game, right_x, right_split, IE_RIGHT_PANEL_W, content_h - content_h / 2);
    if( game->add_dialog_open )
        ie_build_add_dialog(game);
    ie_build_dropdown(game);
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
    ui_minimenu_reset(&game->dropdown);
    ie_resolve_scene_font(game, 495);
    return game;
}

void
GameInterfaceEditor_Free(struct GameInterfaceEditor* game)
{
    if( !game )
        return;
    ie_free_widgets(game);
    free(game->group_ids);
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
    if( game )
        game->dat2_cache = dat2_cache;
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

    if( LibToriRS_Input_IsClick(input, TORIRSM_LEFT) )
    {
        int cx = input->last_click_x[TORIRSM_LEFT];
        int cy = input->last_click_y[TORIRSM_LEFT];
        int hit = ie_hit_test(game, cx, cy);
        ie_handle_hit(game, hit, cx, cy);
    }

    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_UP) )
        game->list_scroll -= IE_ROW_H;
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_DOWN) )
        game->list_scroll += IE_ROW_H;
}

void
GameInterfaceEditor_FrameBegin(
    struct GameInterfaceEditor* game,
    int cycles_elapsed)
{
    (void)cycles_elapsed;
    if( !game )
        return;
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
                command->kind = TORIRSRC_FONT;
                command->u.font.font_id = item->font_id;
                command->u.font.x = item->x;
                command->u.font.y = item->y;
                command->u.font.color = item->color;
                command->u.font.center = item->center;
                command->u.font.shadowed = item->shadowed;
                command->u.font.text = item->text;
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
