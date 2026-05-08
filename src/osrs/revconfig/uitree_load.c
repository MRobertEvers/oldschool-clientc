#include "uitree_load.h"

#include "bmp.h"
#include "graphics/dash.h"
#include "graphics/dashmap.h"
#include "osrs/_light_model_default.u.c"
#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/dash_utils.h"
#include "osrs/entity_scenebuild.h"
#include "osrs/game.h"
#include "osrs/gamecache/gamecache.h"
#include "osrs/interface_state.h"
#include "osrs/minimenu_regions.h"
#include "osrs/obj_icon.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "osrs/rscache/tables_dat/pixfont.h"
#include "uitree_load_bridge.h"
#include "uitree_load_private.h"
#include "uitree_loader.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UITREE_DEBUG_SUBTREE_COMPONENT_ID 1151

/** `TORI_UITREE_SUBTREE_STATS`: unset = off; "1"/"yes" = stats for component
 * `UITREE_DEBUG_SUBTREE_COMPONENT_ID`; else decimal = that root component id. */
static int
uitree_subtree_stats_env_root_id(void)
{
    static int cached = -999;
    if( cached != -999 )
        return cached;
    const char* e = getenv("TORI_UITREE_SUBTREE_STATS");
    if( !e || e[0] == '\0' )
    {
        cached = 0;
        return cached;
    }
    if( strcmp(e, "1") == 0 )
        cached = UITREE_DEBUG_SUBTREE_COMPONENT_ID;
    else
        cached = (int)strtol(e, NULL, 10);
    return cached;
}

struct uitree_push_rs_stats
{
    unsigned skip_hide_nonlayer;
    unsigned skip_layer_no_child_tables;
    unsigned skip_child_missing_gc;
    unsigned skip_graphic_no_sprite;
    unsigned skip_graphic_attach_fail;
    unsigned skip_model_no_game;
    unsigned skip_default_type;
    unsigned nodes_pushed;
};

static void
uitree_push_rs_stats_note_push(
    struct uitree_push_rs_stats* st,
    int32_t idx)
{
    if( st && idx >= 0 )
        st->nodes_pushed++;
}

static void
uitree_push_rs_stats_flush(
    struct uitree_push_rs_stats const* st,
    char const* where,
    int root_component_id)
{
    if( !st )
        return;
    fprintf(
        stderr,
        "[uitree_push_rs_stats] where=%s root_component_id=%d nodes_pushed=%u "
        "skip_hide_nonlayer=%u skip_layer_no_child_tables=%u skip_child_missing_gc=%u "
        "skip_graphic_no_sprite=%u skip_graphic_attach_fail=%u skip_model_no_game=%u "
        "skip_default_type=%u\n",
        where ? where : "?",
        root_component_id,
        st->nodes_pushed,
        st->skip_hide_nonlayer,
        st->skip_layer_no_child_tables,
        st->skip_child_missing_gc,
        st->skip_graphic_no_sprite,
        st->skip_graphic_attach_fail,
        st->skip_model_no_game,
        st->skip_default_type);
}

/** Log watch-id subtree only when that node exists (avoids spam per expand / load). */
static void
uitree_load_debug_log_subtree_watch_id(struct UITree* ui)
{
    if( !ui )
        return;
    if( uitree_find_by_component_id(ui, UITREE_DEBUG_SUBTREE_COMPONENT_ID) < 0 )
        return;
    uitree_debug_log_subtree_for_component_id(ui, UITREE_DEBUG_SUBTREE_COMPONENT_ID);
}

/* SpriteEntry, ComponentEntry, SpriteLoad_AtlasMode, SpriteLoad, ComponentLoad
 * are now defined in uitree_load_private.h */

/** Comma-separated level indices 0-7, optional inclusive ranges "lo-hi" -> bitmask; empty -> 0xF.
 */
static uint8_t
parse_paint_levels_mask(const char* str)
{
    if( !str || str[0] == '\0' )
        return 0xFu;
    unsigned m = 0u;
    const char* p = str;
    while( *p != '\0' )
    {
        while( *p == ' ' || *p == '\t' )
            p++;
        if( *p == '\0' )
            break;
        char* end = NULL;
        long lo = strtol(p, &end, 10);
        if( end == p )
        {
            while( *p && *p != ',' )
                p++;
            if( *p == ',' )
                p++;
            continue;
        }
        p = end;
        while( *p == ' ' || *p == '\t' )
            p++;
        if( *p == '-' )
        {
            p++;
            while( *p == ' ' || *p == '\t' )
                p++;
            long hi = strtol(p, &end, 10);
            if( end != p )
            {
                long a = lo;
                long b = hi;
                if( a > b )
                {
                    long t = a;
                    a = b;
                    b = t;
                }
                for( long k = a; k <= b && k < 8; k++ )
                {
                    if( k >= 0 )
                        m |= 1u << (unsigned)k;
                }
                p = end;
            }
            else if( lo >= 0 && lo < 8 )
                m |= 1u << (unsigned)lo;
        }
        else if( lo >= 0 && lo < 8 )
            m |= 1u << (unsigned)lo;
        while( *p == ' ' || *p == '\t' )
            p++;
        if( *p == ',' )
            p++;
    }
    if( m == 0u )
        return 0xFu;
    return (uint8_t)m;
}

/* MAX_LAYOUT_ENTRIES, LayoutItem, LayoutLoad, LoadKind, InvLoad, CurrentLoad
 * are now defined in uitree_load_private.h */

static struct DashSprite*
load_sprite_pix8(
    struct FileListDat* filelist,
    int data_file_idx,
    int index_file_idx,
    int sprite_idx)
{
    struct CacheDatPix8Palette* pix8_palette = NULL;
    struct DashSprite* sprite = NULL;

    pix8_palette = cache_dat_pix8_palette_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);

    sprite = dashsprite_new_from_cache_pix8_palette(pix8_palette);
    cache_dat_pix8_palette_free(pix8_palette);

    return sprite;
}

static struct DashSprite*
load_sprite_pix32(
    struct FileListDat* filelist,
    int data_file_idx,
    int index_file_idx,
    int sprite_idx)
{
    struct CacheDatPix32* pix32 = NULL;
    struct DashSprite* sprite = NULL;

    pix32 = cache_dat_pix32_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( !pix32 )
    {
        return NULL;
    }
    sprite = dashsprite_new_from_cache_pix32(pix32);
    cache_dat_pix32_free(pix32);
    return sprite;
}

static void
sprite_apply_ini_crop(
    struct DashSprite* sp,
    int crop_x,
    int crop_y,
    int crop_w,
    int crop_h)
{
    if( !sp || crop_w <= 0 || crop_h <= 0 )
        return;
    if( sp->width <= 0 || sp->height <= 0 )
        return;
    int ox = crop_x < 0 ? 0 : crop_x;
    int oy = crop_y < 0 ? 0 : crop_y;
    if( ox >= sp->width || oy >= sp->height )
        return;
    int w = crop_w;
    int h = crop_h;
    if( ox + w > sp->width )
        w = sp->width - ox;
    if( oy + h > sp->height )
        h = sp->height - oy;
    if( w <= 0 || h <= 0 )
        return;
    sp->crop_x = ox;
    sp->crop_y = oy;
    sp->crop_width = w;
    sp->crop_height = h;
}

static void
load_sprite(
    struct SpriteLoad* load,
    struct DashMap* sprite_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct BuildCacheDat* buildcachedat)
{
    assert(
        buildcachedat->cfg_media_jagfile &&
        "buildcachedat must have media_filelist to load sprites");

    struct FileListDat* filelist = buildcachedat->cfg_media_jagfile;
    int index_file_idx = filelist_dat_find_file_by_name(filelist, load->index_filename);
    int data_file_idx = filelist_dat_find_file_by_name(filelist, load->data_filename);
    if( index_file_idx == -1 || data_file_idx == -1 )
    {
        assert(0 && "Failed to find index or data file in filelist for sprite");
        return;
    }

    struct DashSprite** sprites = NULL;

    int count = 1;
    int start_atlas_index = 0;
    switch( load->atlas_mode )
    {
    case SPRITELOAD_ATLAS_MODE_INDEX:
        count = 1;
        start_atlas_index = load->atlas_index;
        break;
    case SPRITELOAD_ATLAS_MODE_COUNT:
        count = load->atlas_count;
        start_atlas_index = 0;
        break;
    }

    sprites = malloc(count * sizeof(struct DashSprite*));
    if( !sprites )
    {
        assert(0 && "Failed to allocate sprites array");
        return;
    }

    for( int i = 0; i < count; i++ )
    {
        int atlas_index = start_atlas_index + i;
        if( strcmp(load->format, "pix8") == 0 )
        {
            sprites[i] = load_sprite_pix8(filelist, data_file_idx, index_file_idx, atlas_index);
        }
        else if( strcmp(load->format, "pix32") == 0 )
        {
            sprites[i] = load_sprite_pix32(filelist, data_file_idx, index_file_idx, atlas_index);
        }
        else
        {
            assert(0 && "Unknown sprite format");
        }

        if( !sprites[i] )
        {
            // Ignore failed loads?
            continue;
        }

        for( int j = 0; j < 5; j++ )
        {
            if( load->transforms[j][0] != '\0' )
            {
                if( strcmp(load->transforms[j], "flip_h") == 0 )
                    dashsprite_flip_horizontal(sprites[i]);
                else if( strcmp(load->transforms[j], "flip_v") == 0 )
                    dashsprite_flip_vertical(sprites[i]);
                else
                    assert(0 && "Unknown transform");
            }
        }

        if( load->crop_width > 0 && load->crop_height > 0 )
        {
            sprite_apply_ini_crop(
                sprites[i], load->crop_x, load->crop_y, load->crop_width, load->crop_height);
        }
    }

    int element_id = uiscene_element_acquire(ui_scene, -1);
    if( element_id < 0 )
    {
        fprintf(
            stderr,
            "uitree_load load_sprite: UIScene full; cannot register sprite \"%s\"\n",
            load->name);
        for( int i = 0; i < count; i++ )
        {
            if( sprites[i] )
                dashsprite_free(sprites[i]);
        }
        free(sprites);
        return;
    }
    struct UISceneElement* element = uiscene_element_at(ui_scene, element_id);
    element->dash_sprites = sprites;
    element->dash_sprites_count = count;
    strncpy(element->name, load->name, sizeof(element->name) - 1);

    struct SpriteEntry* sprite_entry = dashmap_search(sprite_hmap, load->name, DASHMAP_INSERT);

    assert(sprite_entry && "Sprite must be inserted into hmap");
    sprite_entry->sprites = sprites;
    sprite_entry->id = element_id;
    sprite_entry->count = count;
    strncpy(sprite_entry->name, load->name, sizeof(sprite_entry->name) - 1);
};

static enum StaticUIComponentType
component_type_from_string(const char* str)
{
    if( strcmp(str, "compass") == 0 )
        return UIELEM_BUILTIN_COMPASS;
    else if( strcmp(str, "minimap") == 0 )
        return UIELEM_BUILTIN_MINIMAP;
    else if( strcmp(str, "world") == 0 )
        return UIELEM_BUILTIN_WORLD;
    else if( strcmp(str, "sidebar") == 0 )
        return UIELEM_BUILTIN_SIDEBAR;
    else if( strcmp(str, "chat") == 0 )
        return UIELEM_BUILTIN_CHAT;
    else if( strcmp(str, "sprite") == 0 )
        return UIELEM_BUILTIN_SPRITE;
    else if( strcmp(str, "builtin_tab_icons") == 0 )
        return UIELEM_BUILTIN_TAB_ICONS;
    else if( strcmp(str, "redstone_tab") == 0 )
        return UIELEM_BUILTIN_REDSTONE_TAB;
    else if( strcmp(str, "hover_tooltip") == 0 )
        return UIELEM_BUILTIN_HOVER_TOOLTIP;
    else if( strcmp(str, "minimenu") == 0 )
        return UIELEM_BUILTIN_MINIMENU;
    else if( strcmp(str, "crosshair") == 0 )
        return UIELEM_BUILTIN_CROSSHAIR;
    else if( strcmp(str, "chat_messages") == 0 )
        return UIELEM_BUILTIN_CHAT_MESSAGES;
    else if( strcmp(str, "chat_input") == 0 )
        return UIELEM_BUILTIN_CHAT_INPUT;
    else if( strcmp(str, "chat_privacy") == 0 )
        return UIELEM_BUILTIN_CHAT_PRIVACY;
    else if( strcmp(str, "collisionmap_overlay") == 0 )
        return UIELEM_BUILTIN_COLLISIONMAP_OVERLAY;
    else if( strcmp(str, "chat_dialog") == 0 )
        return UIELEM_BUILTIN_CHAT_DIALOG;
    else if( strcmp(str, "sidebar_overlay") == 0 )
        return UIELEM_BUILTIN_SIDEBAR_OVERLAY;
    else if( strcmp(str, "viewport_overlay") == 0 )
        return UIELEM_BUILTIN_VIEWPORT_OVERLAY;

    assert(0 && "Unknown component type");
    return 0;
}

/* Parses sprite ref (e.g. name or name[3]); returns atlas index from brackets, default 0. */
static struct SpriteEntry*
sprite_entry_resolve_ref(
    struct DashMap* sprite_hmap,
    const char* sprite_ref,
    int* atlas_index_out)
{
    *atlas_index_out = 0;
    if( !sprite_ref || sprite_ref[0] == '\0' )
        return NULL;

    char buf[64];
    strncpy(buf, sprite_ref, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* open_bracket = strchr(buf, '[');
    char* close_bracket = strchr(buf, ']');
    if( open_bracket != NULL && close_bracket != NULL )
    {
        *open_bracket = '\0';
        *close_bracket = '\0';
        *atlas_index_out = atoi(open_bracket + 1);
    }
    return dashmap_search(sprite_hmap, buf, DASHMAP_FIND);
}

/* 0 = bound; 1 = empty sprite string (caller breaks); 2 = missing in hmap (caller returns). */
static int
component_bind_sprite_from_load(
    struct ComponentLoad* load,
    struct DashMap* sprite_hmap,
    struct ComponentEntry* component_entry,
    char const* component_kind_for_log)
{
    struct SpriteEntry* sprite_entry = NULL;

    if( load->sprite[0] == '\0' )
        return 1;

    char* open_bracket = strchr(load->sprite, '[');
    char* close_bracket = strchr(load->sprite, ']');
    int sprite_index = 0;
    if( open_bracket != NULL && close_bracket != NULL )
    {
        *open_bracket = '\0';
        *close_bracket = '\0';
        sprite_index = atoi(open_bracket + 1);

        char search_name[64] = { 0 };
        strncpy(search_name, load->sprite, sizeof(search_name) - 1);

        sprite_entry = dashmap_search(sprite_hmap, search_name, DASHMAP_FIND);
        *open_bracket = '[';
        *close_bracket = ']';
    }
    else
    {
        sprite_entry = dashmap_search(sprite_hmap, load->sprite, DASHMAP_FIND);
    }
    if( !sprite_entry )
    {
        printf("Sprite for component not found in hmap: %s\n", load->sprite);
        return 2;
    }

    component_entry->sprite_id = sprite_entry->id;
    component_entry->sprite_index = sprite_index;
    return 0;
}

static void
load_component(
    struct ComponentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* gamecache_unused)
{
    (void)gamecache_unused;
    enum StaticUIComponentType type = component_type_from_string(load->type);
    struct ComponentEntry* component_entry = NULL;

    component_entry = dashmap_search(component_hmap, load->name, DASHMAP_INSERT);
    memset(component_entry, 0, sizeof(struct ComponentEntry));

    assert(component_entry && "Component must be inserted into hmap");
    component_entry->type = type;
    component_entry->sprite_id = -1;
    component_entry->sprite_index = 0;
    component_entry->sprite_id_active = -1;
    component_entry->sprite_index_active = 0;

    strncpy(component_entry->name, load->name, sizeof(component_entry->name) - 1);
    component_entry->def_x = load->def_x;
    component_entry->def_y = load->def_y;

    switch( type )
    {
    case UIELEM_BUILTIN_COMPASS:
    {
        int bind = component_bind_sprite_from_load(load, sprite_hmap, component_entry, "compass");
        if( bind == 2 )
            return;
        component_entry->width = load->width;
        component_entry->height = load->height;
        component_entry->anchor_x = load->anchor_x;
        component_entry->anchor_y = load->anchor_y;
    }
    break;
    case UIELEM_BUILTIN_MINIMAP:
    {
        component_entry->width = load->width;
        component_entry->height = load->height;
        component_entry->anchor_x = load->anchor_x;
        component_entry->anchor_y = load->anchor_y;
    }
    break;
    case UIELEM_BUILTIN_WORLD:
        component_entry->level_mask = parse_paint_levels_mask(load->paint_levels);
        component_entry->width = load->width;
        component_entry->height = load->height;
        break;
    case UIELEM_BUILTIN_SPRITE:
    {
        int bind = component_bind_sprite_from_load(load, sprite_hmap, component_entry, "sprite");
        if( bind == 2 )
            return;
        component_entry->width = load->width;
        component_entry->height = load->height;
        if( bind == 0 && (component_entry->width <= 0 || component_entry->height <= 0) )
        {
            int ai = 0;
            struct SpriteEntry* se = sprite_entry_resolve_ref(sprite_hmap, load->sprite, &ai);
            (void)ai;
            if( se && component_entry->sprite_index >= 0 &&
                component_entry->sprite_index < se->count )
            {
                struct DashSprite* sp = se->sprites[component_entry->sprite_index];
                if( sp )
                {
                    int sw = sp->crop_width > 0 ? sp->crop_width : sp->width;
                    int sh = sp->crop_height > 0 ? sp->crop_height : sp->height;
                    if( component_entry->width <= 0 )
                        component_entry->width = sw;
                    if( component_entry->height <= 0 )
                        component_entry->height = sh;
                }
            }
        }
    }
    break;
    case UIELEM_BUILTIN_REDSTONE_TAB:
    {
        component_entry->tabno = load->tabno;
        component_entry->width = load->width;
        component_entry->height = load->height;

        if( load->sprite[0] != '\0' )
        {
            int ai = 0;
            struct SpriteEntry* se = sprite_entry_resolve_ref(sprite_hmap, load->sprite, &ai);
            if( se )
            {
                component_entry->sprite_id = se->id;
                component_entry->sprite_index = ai;
            }
        }

        if( load->sprite_active[0] != '\0' )
        {
            int ai = 0;
            struct SpriteEntry* se =
                sprite_entry_resolve_ref(sprite_hmap, load->sprite_active, &ai);
            if( se )
            {
                component_entry->sprite_id_active = se->id;
                component_entry->sprite_index_active = ai;
                if( component_entry->width <= 0 || component_entry->height <= 0 )
                {
                    if( ai >= 0 && ai < se->count && se->sprites[ai] )
                    {
                        struct DashSprite* sp = se->sprites[ai];
                        int sw = sp->crop_width > 0 ? sp->crop_width : sp->width;
                        int sh = sp->crop_height > 0 ? sp->crop_height : sp->height;
                        if( component_entry->width <= 0 )
                            component_entry->width = sw;
                        if( component_entry->height <= 0 )
                            component_entry->height = sh;
                    }
                }
            }
        }
    }
    break;
    case UIELEM_BUILTIN_SIDEBAR:
    {
        component_entry->width = load->width > 0 ? load->width : 190;
        component_entry->height = load->height > 0 ? load->height : 261;
        component_entry->tabno = load->tabno;
        component_entry->componentno = load->componentno;
        strncpy(component_entry->inv, load->inv, sizeof(component_entry->inv) - 1);
        component_entry->inv[sizeof(component_entry->inv) - 1] = '\0';
    }
    break;
    case UIELEM_BUILTIN_HOVER_TOOLTIP:
    case UIELEM_BUILTIN_CHAT_PRIVACY:
    {
        strncpy(component_entry->font, load->font, sizeof(component_entry->font) - 1);
        component_entry->font[sizeof(component_entry->font) - 1] = '\0';
    }
    break;
    case UIELEM_BUILTIN_MINIMENU:
    {
        strncpy(component_entry->font, load->font, sizeof(component_entry->font) - 1);
        component_entry->font[sizeof(component_entry->font) - 1] = '\0';
        minimenu_regions_default(&component_entry->minimenu_regions);
        minimenu_regions_merge_ini(
            &component_entry->minimenu_regions,
            load->minimenu_region_viewport[0] ? load->minimenu_region_viewport : NULL,
            load->minimenu_region_sidebar[0] ? load->minimenu_region_sidebar : NULL,
            load->minimenu_region_chat[0] ? load->minimenu_region_chat : NULL,
            load->minimenu_place_viewport_max[0] ? load->minimenu_place_viewport_max : NULL,
            load->minimenu_place_sidebar_max[0] ? load->minimenu_place_sidebar_max : NULL,
            load->minimenu_place_chat_max[0] ? load->minimenu_place_chat_max : NULL);
    }
    break;
    case UIELEM_BUILTIN_CHAT_MESSAGES:
    {
        strncpy(component_entry->font, load->font, sizeof(component_entry->font) - 1);
        component_entry->font[sizeof(component_entry->font) - 1] = '\0';
        chat_layout_builtin(&component_entry->chat_layout);
        chat_layout_apply_mask(
            &component_entry->chat_layout, &load->chat_geom, load->chat_geom_mask);
        component_entry->chat_geom_mask = load->chat_geom_mask;
    }
    break;
    case UIELEM_BUILTIN_CHAT_INPUT:
    {
        strncpy(component_entry->font, load->font, sizeof(component_entry->font) - 1);
        component_entry->font[sizeof(component_entry->font) - 1] = '\0';
        memset(&component_entry->chat_layout, 0, sizeof(component_entry->chat_layout));
        chat_layout_apply_mask(
            &component_entry->chat_layout, &load->chat_geom, load->chat_geom_mask);
        component_entry->chat_geom_mask = load->chat_geom_mask;
    }
    break;
    case UIELEM_BUILTIN_CROSSHAIR:
    {
        int ho = 8;
        if( load->crosshair_hotspot_offset_set )
            ho = load->crosshair_hotspot_offset;
        component_entry->crosshair_hotspot_offset = ho;
    }
    break;
    case UIELEM_BUILTIN_CHAT_DIALOG:
        component_entry->width = load->width > 0 ? load->width : 409;
        component_entry->height = load->height > 0 ? load->height : 96;
        break;
    case UIELEM_BUILTIN_SIDEBAR_OVERLAY:
        component_entry->width = load->width > 0 ? load->width : 190;
        component_entry->height = load->height > 0 ? load->height : 261;
        break;
    case UIELEM_BUILTIN_VIEWPORT_OVERLAY:
        component_entry->width = load->width > 0 ? load->width : 512;
        component_entry->height = load->height > 0 ? load->height : 334;
        break;
    default:
        break;
    }
}

static void
load_inv(
    struct InvLoad* il,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct UIScene* ui_scene)
{
    if( !inv_pool || !il )
        return;

    struct UIInventory inv = { 0 };
    strncpy(inv.name, il->name, sizeof(inv.name) - 1);
    inv.name[sizeof(inv.name) - 1] = '\0';

    /* scene_id = 0 is a valid UIScene element ID, so explicitly sentinel all slots to -1
     * so that inv_sync_load_item_sprite can safely skip unacquired slots. */
    for( int j = 0; j < UI_INVENTORY_MAX_ITEMS; j++ )
        inv.items[j].scene_id = -1;

    for( int i = 0; i < il->item_count && i < UI_INVENTORY_MAX_ITEMS; i++ )
    {
        int obj_id = il->item_ids[i];
        inv.items[inv.item_count].obj_id = obj_id;
        inv.items[inv.item_count].scene_id = -1;
        inv.items[inv.item_count].atlas_index = 0;

        if( game && ui_scene && obj_id > 0 )
        {
            /* INI item= uses 1-based wire ids like invSlotObjId / Client.ts linkObjType;
             * obj_icon_get expects 0-based (see gameproto UPDATE_INV_FULL + interface_draw). */
            struct DashSprite* cached = obj_icon_get(game, obj_id - 1, 1);
            if( cached )
            {
                struct DashSprite* sp = dashsprite_clone(cached);
                if( sp )
                {
                    struct DashSprite** arr = malloc(sizeof(struct DashSprite*));
                    if( arr )
                    {
                        arr[0] = sp;
                        int eid = uiscene_element_acquire(ui_scene, -1);
                        if( eid >= 0 )
                        {
                            struct UISceneElement* el = uiscene_element_at(ui_scene, eid);
                            if( el )
                            {
                                el->dash_sprites = arr;
                                el->dash_sprites_count = 1;
                                inv.items[inv.item_count].scene_id = eid;
                            }
                            else
                            {
                                free(arr);
                                dashsprite_free(sp);
                            }
                        }
                        else
                        {
                            free(arr);
                            dashsprite_free(sp);
                        }
                    }
                    else
                        dashsprite_free(sp);
                }
            }
        }
        inv.item_count++;
    }

    uitree_inv_pool_append(inv_pool, &inv);
}

static int
ensure_font_id(
    struct UIScene* ui_scene,
    struct GameCache* bcd,
    int font_idx)
{
    static char const* const font_names[] = { "p11", "p12", "b12", "q8" };
    int fidx = font_idx;
    if( fidx < 0 || fidx > 3 )
        fidx = 1;
    char const* nm = font_names[fidx];
    int fid = uiscene_font_find_id(ui_scene, nm);
    if( fid >= 0 )
        return fid;
    int ref_id = gamecache_get_font_ref_id(bcd, nm);
    if( ref_id < 0 )
        return -1;
    struct DashPixFont* f = uiscene_font_get(ui_scene, ref_id);
    if( !f )
        return -1;
    return uiscene_font_add(ui_scene, nm, f);
}

/** Attach `count` sprites to a new UIScene element; UIScene owns `row` and each sprite pointer. */
static int
uiscene_attach_sprite_row(
    struct UIScene* ui_scene,
    struct DashSprite** row,
    int count)
{
    int eid = uiscene_element_acquire(ui_scene, -1);
    if( eid < 0 )
    {
        fprintf(stderr, "uiscene_attach_sprite_row: UIScene full; cannot register sprite row\n");
        return -1;
    }
    struct UISceneElement* el = uiscene_element_at(ui_scene, eid);
    if( !el )
        return -1;
    el->dash_sprites = row;
    el->dash_sprites_count = count;
    return eid;
}

/**
 * Copy revision-specific fields from a GameCacheComponent into the revision-agnostic
 * StaticUIComponent at tree->components[node_idx].  Called once per node at load time;
 * the UITree owns all allocated memory and frees it in uitree_free().
 */
static void
uitree_translate_agnostic_fields(
    struct UITree* tree,
    int32_t node_idx,
    struct GameCacheComponent* cc)
{
    if( node_idx < 0 || (uint32_t)node_idx >= tree->component_count || !cc )
        return;
    struct StaticUIComponent* c = &tree->components[node_idx];

    c->button_kind = (enum UIButtonKind)cc->buttonType;
    c->click_mask = cc->targetMask;
    c->interactable = cc->interactable ? 1 : 0;
    c->usable = cc->usable ? 1 : 0;
    c->swappable = cc->swappable ? 1 : 0;
    c->draggable = cc->draggable ? 1 : 0;
    c->alpha = cc->alpha;
    c->overlayer = cc->overlayer;
    c->client_code = cc->clientCode;
    c->anim_id = cc->anim;
    c->active_anim_id = cc->activeAnim;
    c->seq_frame = cc->seqFrame;
    c->seq_cycle = cc->seqCycle;

    /* Script bytecode. */
    int n = cc->scripts_count;
    c->scripts_count = n;
    if( n > 0 && cc->scripts && cc->scripts_lengths )
    {
        c->scripts = calloc((size_t)n, sizeof(struct UIScriptBytecode));
        c->script_comparator = calloc((size_t)n, sizeof(uint8_t));
        c->script_operand = calloc((size_t)n, sizeof(int));
        if( c->scripts && c->script_comparator && c->script_operand )
        {
            for( int i = 0; i < n; i++ )
            {
                int len = cc->scripts_lengths[i];
                c->scripts[i].len = len;
                c->scripts[i].code = NULL;
                if( len > 0 && cc->scripts[i] )
                {
                    c->scripts[i].code = malloc((size_t)len * sizeof(int));
                    if( c->scripts[i].code )
                        memcpy(c->scripts[i].code, cc->scripts[i], (size_t)len * sizeof(int));
                }
                c->script_comparator[i] =
                    cc->scriptComparator ? (uint8_t)cc->scriptComparator[i] : 0;
                c->script_operand[i] = cc->scriptOperand ? cc->scriptOperand[i] : 0;
            }
        }
        else
        {
            /* Allocation failure: release partial. */
            if( c->scripts )
            {
                for( int i = 0; i < n; i++ )
                    free(c->scripts[i].code);
                free(c->scripts);
            }
            free(c->script_comparator);
            free(c->script_operand);
            c->scripts = NULL;
            c->script_comparator = NULL;
            c->script_operand = NULL;
            c->scripts_count = 0;
        }
    }

    /* Inventory right-click option strings. */
    for( int k = 0; k < 5; k++ )
    {
        const char* src = (cc->iop && cc->iop[k]) ? cc->iop[k] : NULL;
        c->iop[k] = src ? strdup(src) : NULL;
    }
    c->option = cc->option ? strdup(cc->option) : NULL;
    c->target_verb = cc->targetVerb ? strdup(cc->targetVerb) : NULL;
    c->target_text = cc->targetText ? strdup(cc->targetText) : NULL;

    if( c->type == UIELEM_RS_MODEL )
    {
        c->u.rs_model.model_zoom = cc->zoom;
        c->u.rs_model.model_xan = cc->xan;
        c->u.rs_model.model_yan = cc->yan;
    }
}

/** Build owned DashModel for a MODEL GameCacheComponent (UITree registers on UIScene). */
static struct DashModel*
uitree_rs_dashmodel_for_model_component(
    struct GGame* game,
    struct GameCache* bcd,
    struct GameCacheComponent* comp)
{
    if( !game || !bcd || !comp )
        return NULL;

    struct DashModel* m = NULL;

    if( comp->modelType == 1 )
    {
        struct GameCacheModel* cache_model = gamecache_get_model(bcd, comp->model);
        if( !cache_model )
            return NULL;
        struct GameCacheModel* model_copy = gamecache_model_new_copy(cache_model);
        m = dashmodel_new_from_gamecache_model(model_copy);
        gamecache_model_free(model_copy);
        if( !m )
            return NULL;
        _light_model_default(m, 0, 0);
    }
    else if( comp->modelType == 2 || comp->modelType == 3 )
    {
        int* slots = NULL;
        int* colors = NULL;
        if( comp->modelType == 3 && game->world )
        {
            struct PlayerEntity* local = world_player(game->world, ACTIVE_PLAYER_SLOT);
            if( local && local->alive )
            {
                slots = local->appearance.slots;
                colors = local->appearance.colors;
            }
        }
        m = entity_scenebuild_head_model_for_component(
            game, comp->modelType, comp->model, slots, colors);
        if( !m )
            return NULL;
    }
    else if( comp->modelType == 4 )
    {
        m = obj_icon_new_dash_model_for_obj(game, comp->model);
        if( !m )
            return NULL;
    }
    else
        return NULL;

    return m;
}

void
uitree_rs_model_refresh_from_gamecache(
    struct GGame* game,
    int component_id)
{
    if( !game || !game->ui_root_buffer || !game->ui_scene || !game->gamecache )
        return;

    int32_t idx = uitree_find_by_component_id(game->ui_root_buffer, component_id);
    if( idx < 0 )
        return;
    struct StaticUIComponent* c = &game->ui_root_buffer->components[idx];
    if( c->type != UIELEM_RS_MODEL )
        return;

    struct GameCacheComponent* comp = gamecache_get_component(game->gamecache, component_id);
    if( !comp || comp->type != COMPONENT_TYPE_MODEL )
        return;

    int old_sid = c->u.rs_model.scene_id;
    if( old_sid >= 0 )
        uiscene_element_release(game->ui_scene, old_sid);
    c->u.rs_model.scene_id = -1;
    c->u.rs_model.rs_model_cached_sequence_id = -1;

    c->anim_id = comp->anim;
    c->active_anim_id = comp->activeAnim;
    c->u.rs_model.model_zoom = comp->zoom;
    c->u.rs_model.model_xan = comp->xan;
    c->u.rs_model.model_yan = comp->yan;

    if( comp->modelType == 0 )
    {
        c->is_dirty = 1;
        return;
    }

    struct DashModel* m = uitree_rs_dashmodel_for_model_component(game, game->gamecache, comp);
    if( !m )
    {
        c->is_dirty = 1;
        return;
    }

    int sid = uiscene_element_acquire_with_model(game->ui_scene, -1, m, NULL);
    if( sid < 0 )
    {
        dashmodel_free(m);
        c->is_dirty = 1;
        return;
    }

    c->u.rs_model.scene_id = sid;
    uitree_rs_model_ensure_sequence_precached(game, c);
    c->is_dirty = 1;
}

static void
uitree_rs_model_refresh_subtree_dfs(
    struct GGame* game,
    struct GameCache* gc,
    struct GameCacheComponent* comp,
    int depth_left)
{
    if( !game || !gc || !comp || depth_left <= 0 )
        return;
    if( comp->type == COMPONENT_TYPE_MODEL && comp->modelType != 0 )
        uitree_rs_model_refresh_from_gamecache(game, comp->id);
    if( !comp->children || comp->children_count <= 0 )
        return;
    for( int i = 0; i < comp->children_count; i++ )
    {
        struct GameCacheComponent* ch = gamecache_get_component(gc, comp->children[i]);
        if( ch )
            uitree_rs_model_refresh_subtree_dfs(game, gc, ch, depth_left - 1);
    }
}

void
uitree_rs_model_refresh_subtree_for_gamecache_root(
    struct GGame* game,
    int root_component_id)
{
    if( !game || !game->gamecache || root_component_id < 0 )
        return;
    struct GameCacheComponent* root = gamecache_get_component(game->gamecache, root_component_id);
    if( !root )
        return;
    uitree_rs_model_refresh_subtree_dfs(game, game->gamecache, root, 512);
}

/** TYPE_INV_TEXT often sits next to TYPE_INV; server UPDATE_INV_FULL targets the INV id only. */

/** DFS from interface node `node_id` (a layer child or nested layer). Ranks TYPE_INV peers:
 * rank 2 = exact width/height match, rank 1 = same slot count only. Skips the INV_TEXT node
 * `inv_text_id`. Only recurses into COMPONENT_TYPE_LAYER (only type with children in cache). */
static void
layer_subtree_rank_best_inv(
    struct GameCache* bcd,
    int node_id,
    int inv_text_id,
    int want_w,
    int want_h,
    int want_slots,
    int depth,
    int max_depth,
    struct GameCacheComponent** best,
    int* best_rank)
{
    if( depth > max_depth || node_id < 0 )
        return;
    struct GameCacheComponent* n = gamecache_get_component(bcd, node_id);
    if( !n || n->id == inv_text_id )
        return;

    if( n->type == COMPONENT_TYPE_INV )
    {
        int rank = 0;
        if( n->width == want_w && n->height == want_h )
            rank = 2;
        else if( want_slots > 0 && n->width * n->height == want_slots )
            rank = 1;
        if( rank > *best_rank )
        {
            *best_rank = rank;
            *best = n;
        }
        if( *best_rank >= 2 )
            return;
    }

    if( n->type == COMPONENT_TYPE_LAYER && n->children && n->children_count > 0 )
    {
        for( int i = 0; i < n->children_count; i++ )
        {
            layer_subtree_rank_best_inv(
                bcd,
                n->children[i],
                inv_text_id,
                want_w,
                want_h,
                want_slots,
                depth + 1,
                max_depth,
                best,
                best_rank);
            if( *best_rank >= 2 )
                return;
        }
    }
}

static struct GameCacheComponent*
layer_find_peer_type_inv(
    struct GameCache* bcd,
    struct GameCacheComponent const* layer,
    struct GameCacheComponent const* inv_text)
{
    if( !bcd || !layer || !inv_text || layer->type != COMPONENT_TYPE_LAYER || !layer->children )
        return NULL;
    if( inv_text->type != COMPONENT_TYPE_INV_TEXT )
        return NULL;

    int want_slots = inv_text->width * inv_text->height;

    /* Fast path: direct TYPE_INV sibling with exact grid (common case). */
    for( int i = 0; i < layer->children_count; i++ )
    {
        int chid = layer->children[i];
        struct GameCacheComponent* ch = gamecache_get_component(bcd, chid);
        if( !ch || ch->id == inv_text->id )
            continue;
        if( ch->type == COMPONENT_TYPE_INV && ch->width == inv_text->width &&
            ch->height == inv_text->height )
            return ch;
    }

    /* Nested layers (INV inside inner layer) or same slot count with different width/height. */
    struct GameCacheComponent* best = NULL;
    int best_rank = 0;
    for( int i = 0; i < layer->children_count; i++ )
    {
        int chid = layer->children[i];
        struct GameCacheComponent* ch = gamecache_get_component(bcd, chid);
        if( !ch || ch->id == inv_text->id )
            continue;
        layer_subtree_rank_best_inv(
            bcd,
            chid,
            inv_text->id,
            inv_text->width,
            inv_text->height,
            want_slots,
            0,
            32,
            &best,
            &best_rank);
        if( best_rank >= 2 )
            return best;
    }
    return best;
}

/** Local layer search first, then DFS from interface root so INV + INV_TEXT can sit in sibling
 * inner layers under the same tab root. */
static struct GameCacheComponent*
interface_find_peer_type_inv(
    struct GameCache* bcd,
    struct GameCacheComponent const* ifc_root,
    struct GameCacheComponent const* layer_parent,
    struct GameCacheComponent const* inv_text)
{
    if( !bcd || !inv_text || inv_text->type != COMPONENT_TYPE_INV_TEXT )
        return NULL;
    if( layer_parent && layer_parent->type == COMPONENT_TYPE_LAYER )
    {
        struct GameCacheComponent* p = layer_find_peer_type_inv(bcd, layer_parent, inv_text);
        if( p )
            return p;
    }
    if( !ifc_root )
        return NULL;
    int want_slots = inv_text->width * inv_text->height;
    struct GameCacheComponent* best = NULL;
    int best_rank = 0;
    layer_subtree_rank_best_inv(
        bcd,
        ifc_root->id,
        inv_text->id,
        inv_text->width,
        inv_text->height,
        want_slots,
        0,
        64,
        &best,
        &best_rank);
    return best;
}

/** Named component sprite -> UIScene element id. Lazy loads register on BuildCacheDat only;
 * gamecache is updated when Lua runs `gamecache_convert_reftables_from_buildcachedat`, so
 * standalone / harness paths fall back to buildcachedat after lazy load. */
static int
uitree_resolve_component_sprite_uiscene_element(
    struct GGame* game,
    struct GameCache* gc,
    const char* sprite_name)
{
    if( !sprite_name || sprite_name[0] == '\0' )
        return -1;
    int e = gamecache_get_component_sprite_element_id(gc, sprite_name);
    if( e >= 0 )
        return e;
    if( game && game->buildcachedat )
        return buildcachedat_get_component_sprite_element_id(game->buildcachedat, sprite_name);
    return -1;
}

static int
push_rs_from_cache_component(
    struct GGame* game,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* bcd,
    int32_t parent_uitree_idx,
    struct GameCacheComponent* comp,
    int abs_x,
    int abs_y,
    int sidebar_inv_index,
    struct GameCacheComponent* layer_parent_comp,
    struct GameCacheComponent* ifc_root_for_inv_peer,
    struct uitree_push_rs_stats* push_rs_stats,
    struct UITreeLoaderAssetRequest* out_req)
{
    if( !comp || !bcd || !ui || !ui_scene )
        return 0;

    if( comp->hide && comp->type != COMPONENT_TYPE_LAYER )
    {
        if( push_rs_stats )
            push_rs_stats->skip_hide_nonlayer++;
        return 0;
    }

    /* `uitree_load_single_component_tree_from_gamecache` calls with parent_uitree_idx==-1 and
     * abs_x=abs_y=0. Fold the root component's IF offset (comp->x/y). Expand_* callers pass
     * parent>=0 with abs already including root->x (e.g. bx=sx+root->x) — do not add twice. */
    int px = abs_x;
    int py = abs_y;
    if( parent_uitree_idx < 0 )
    {
        px = abs_x + comp->x;
        py = abs_y + comp->y;
    }

    switch( comp->type )
    {
    case COMPONENT_TYPE_LAYER:
    {
        int32_t lid = uitree_push_rs_layer(
            ui,
            parent_uitree_idx,
            comp->id,
            comp->scroll,
            comp->hide ? 1 : 0,
            px,
            py,
            comp->width,
            comp->height);
        uitree_translate_agnostic_fields(ui, lid, comp);
        uitree_push_rs_stats_note_push(push_rs_stats, lid);
        if( !comp->children || !comp->childX || !comp->childY )
        {
            if( push_rs_stats )
                push_rs_stats->skip_layer_no_child_tables++;
            return 0;
        }
        for( int i = 0; i < comp->children_count; i++ )
        {
            struct GameCacheComponent* ch = gamecache_get_component(bcd, comp->children[i]);
            if( !ch )
            {
                if( push_rs_stats )
                    push_rs_stats->skip_child_missing_gc++;
                continue;
            }
            int cx = px + comp->childX[i] + ch->x;
            int cy = py + comp->childY[i] + ch->y;
            int prc = push_rs_from_cache_component(
                game,
                ui,
                ui_scene,
                bcd,
                lid,
                ch,
                cx,
                cy,
                sidebar_inv_index,
                comp,
                ifc_root_for_inv_peer,
                push_rs_stats,
                out_req);
            if( prc != 0 )
                return prc;
        }
    }
    break;
    case COMPONENT_TYPE_RECT:
    {
        int32_t rid = uitree_push_rs_rect(
            ui,
            parent_uitree_idx,
            comp->id,
            comp->colour,
            comp->activeColour,
            comp->overColour,
            comp->activeOverColour,
            comp->alpha,
            comp->fill ? 1 : 0,
            px,
            py,
            comp->width,
            comp->height);
        uitree_translate_agnostic_fields(ui, rid, comp);
        uitree_push_rs_stats_note_push(push_rs_stats, rid);
    }
    break;
    case COMPONENT_TYPE_GRAPHIC:
    {
        struct DashSprite* g0 = NULL;
        struct DashSprite* g1 = NULL;
        if( comp->graphic && comp->graphic[0] != '\0' )
        {
            int e0 = uitree_resolve_component_sprite_uiscene_element(game, bcd, comp->graphic);
            if( e0 < 0 && game && !out_req )
            {
                buildcachedat_loader_load_component_sprite_lazy(
                    game->buildcachedat, ui_scene, game, comp->graphic);
                e0 = uitree_resolve_component_sprite_uiscene_element(game, bcd, comp->graphic);
            }
            if( e0 >= 0 )
            {
                struct UISceneElement* el0 = uiscene_element_at(ui_scene, e0);
                if( el0 && el0->dash_sprites_count > 0 && el0->dash_sprites )
                    g0 = el0->dash_sprites[0];
            }
        }
        if( comp->activeGraphic && comp->activeGraphic[0] != '\0' )
        {
            int e1 =
                uitree_resolve_component_sprite_uiscene_element(game, bcd, comp->activeGraphic);
            if( e1 < 0 && game && !out_req )
            {
                buildcachedat_loader_load_component_sprite_lazy(
                    game->buildcachedat, ui_scene, game, comp->activeGraphic);
                e1 =
                    uitree_resolve_component_sprite_uiscene_element(game, bcd, comp->activeGraphic);
            }
            if( e1 >= 0 )
            {
                struct UISceneElement* el1 = uiscene_element_at(ui_scene, e1);
                if( el1 && el1->dash_sprites_count > 0 && el1->dash_sprites )
                    g1 = el1->dash_sprites[0];
            }
        }
        int count = 0;
        if( g0 )
            count = 1;
        if( g1 && g1 != g0 )
            count = 2;
        /* Do not substitute activeGraphic for missing graphic — must have distinct sprites
         * for active/inactive state to work correctly (e.g. lit/unlit prayer icons).
         * If only activeGraphic exists but graphic is missing, skip drawing entirely. */
        if( count == 0 || (count == 1 && g1 && !g0) )
        {
            if( out_req )
            {
                const char* req_name = NULL;
                if( comp->graphic && comp->graphic[0] && !g0 )
                    req_name = comp->graphic;
                else if( comp->activeGraphic && comp->activeGraphic[0] && !g1 )
                    req_name = comp->activeGraphic;
                if( req_name )
                {
                    out_req->kind = UITREE_ASSET_SPRITE;
                    strncpy(out_req->u.sprite.name, req_name, sizeof(out_req->u.sprite.name) - 1);
                    out_req->u.sprite.name[sizeof(out_req->u.sprite.name) - 1] = '\0';
                    return -1;
                }
            }
            if( push_rs_stats )
                push_rs_stats->skip_graphic_no_sprite++;
            return 0;
        }
        struct DashSprite** row = malloc((size_t)count * sizeof(struct DashSprite*));
        if( !row )
        {
            if( push_rs_stats )
                push_rs_stats->skip_graphic_no_sprite++;
            return 0;
        }
        row[0] = dashsprite_clone(g0);
        if( !row[0] )
        {
            free(row);
            if( push_rs_stats )
                push_rs_stats->skip_graphic_no_sprite++;
            return 0;
        }
        if( count == 2 )
        {
            row[1] = dashsprite_clone(g1);
            if( !row[1] )
            {
                dashsprite_free(row[0]);
                free(row);
                if( push_rs_stats )
                    push_rs_stats->skip_graphic_no_sprite++;
                return 0;
            }
        }
        int sid = uiscene_attach_sprite_row(ui_scene, row, count);
        if( sid < 0 )
        {
            free(row);
            if( push_rs_stats )
                push_rs_stats->skip_graphic_attach_fail++;
            return 0;
        }
        int sid_a = -1;
        int atlas_a = 0;
        if( count >= 2 )
        {
            sid_a = sid;
            atlas_a = 1;
        }

        int32_t gid = uitree_push_rs_graphic(
            ui,
            parent_uitree_idx,
            comp->id,
            sid,
            0,
            sid_a,
            atlas_a,
            px,
            py,
            comp->width,
            comp->height);
        uitree_translate_agnostic_fields(ui, gid, comp);
        uitree_push_rs_stats_note_push(push_rs_stats, gid);
    }
    break;
    case COMPONENT_TYPE_TEXT:
    {
        int32_t tid = uitree_push_rs_text(
            ui,
            parent_uitree_idx,
            comp->id,
            comp->font,
            comp->colour,
            comp->activeColour,
            comp->overColour,
            comp->activeOverColour,
            comp->center ? 1 : 0,
            comp->shadowed ? 1 : 0,
            comp->text,
            comp->activeText,
            px,
            py,
            comp->width,
            comp->height);
        uitree_translate_agnostic_fields(ui, tid, comp);
        uitree_push_rs_stats_note_push(push_rs_stats, tid);
        /* Pre-resolve font_id at load time so render steps never touch buildcache. */
        if( tid >= 0 && (uint32_t)tid < ui->component_count )
        {
            int resolved = ensure_font_id(ui_scene, bcd, comp->font);
            ui->components[tid].u.rs_text.font_id = resolved;
        }
    }
    break;
    case COMPONENT_TYPE_INV_TEXT:
    case COMPONENT_TYPE_INV:
    {
        struct GameCacheComponent* inv_text_peer = NULL;
        struct GameCacheComponent* inv_pool_key_comp = comp;
        if( comp->type == COMPONENT_TYPE_INV_TEXT && sidebar_inv_index < 0 )
            inv_text_peer =
                interface_find_peer_type_inv(bcd, ifc_root_for_inv_peer, layer_parent_comp, comp);
        if( inv_text_peer )
            inv_pool_key_comp = inv_text_peer;

        int bg_sid[UI_INV_SLOT_OFFSET_MAX];
        int bg_ai[UI_INV_SLOT_OFFSET_MAX];
        int effective_inv_index = sidebar_inv_index;
        if( effective_inv_index < 0 && game && game->inv_pool )
        {
            effective_inv_index = uitree_inv_pool_find_or_append_by_component_id(
                game->inv_pool, inv_pool_key_comp->id);
        }
        for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
        {
            bg_sid[si] = -1;
            bg_ai[si] = 0;
        }
        if( comp->invSlotGraphic && ui_scene )
        {
            for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
            {
                char const* gname = comp->invSlotGraphic[si];
                if( !gname || gname[0] == '\0' )
                    continue;
                int ge = uitree_resolve_component_sprite_uiscene_element(game, bcd, gname);
                if( ge < 0 )
                {
                    if( out_req )
                    {
                        out_req->kind = UITREE_ASSET_SPRITE;
                        strncpy(out_req->u.sprite.name, gname, sizeof(out_req->u.sprite.name) - 1);
                        out_req->u.sprite.name[sizeof(out_req->u.sprite.name) - 1] = '\0';
                        return -1;
                    }
                    /* Sprite not yet loaded; try lazy-loading from media. */
                    buildcachedat_loader_load_component_sprite_lazy(
                        game->buildcachedat, ui_scene, game, gname);
                    ge = uitree_resolve_component_sprite_uiscene_element(game, bcd, gname);
                    if( ge < 0 )
                        continue;
                }
                struct UISceneElement* gel = uiscene_element_at(ui_scene, ge);
                if( !gel || gel->dash_sprites_count <= 0 || !gel->dash_sprites )
                    continue;
                struct DashSprite* sp = gel->dash_sprites[0];
                if( !sp )
                    continue;
                struct DashSprite* sp_copy = dashsprite_clone(sp);
                if( !sp_copy )
                    continue;
                struct DashSprite** row = malloc(sizeof(struct DashSprite*));
                if( !row )
                {
                    dashsprite_free(sp_copy);
                    continue;
                }
                row[0] = sp_copy;
                int sid = uiscene_attach_sprite_row(ui_scene, row, 1);
                if( sid < 0 )
                {
                    free(row);
                    continue;
                }
                bg_sid[si] = sid;
                bg_ai[si] = 0;
            }
        }
        int32_t iid = (comp->type == COMPONENT_TYPE_INV_TEXT) ? uitree_push_rs_inv_text(
                                                                    ui,
                                                                    parent_uitree_idx,
                                                                    comp->id,
                                                                    effective_inv_index,
                                                                    comp->width,
                                                                    comp->height,
                                                                    comp->marginX,
                                                                    comp->marginY,
                                                                    comp->invSlotOffsetX,
                                                                    comp->invSlotOffsetY,
                                                                    bg_sid,
                                                                    bg_ai,
                                                                    px,
                                                                    py,
                                                                    comp->width,
                                                                    comp->height)
                                                              : uitree_push_rs_inv(
                                                                    ui,
                                                                    parent_uitree_idx,
                                                                    comp->id,
                                                                    effective_inv_index,
                                                                    comp->width,
                                                                    comp->height,
                                                                    comp->marginX,
                                                                    comp->marginY,
                                                                    comp->invSlotOffsetX,
                                                                    comp->invSlotOffsetY,
                                                                    bg_sid,
                                                                    bg_ai,
                                                                    px,
                                                                    py,
                                                                    comp->width,
                                                                    comp->height);

        /* Pre-fill inventory items from cache component data (e.g., rune icons in magic-book
         * tooltip). Mirrors gamenet_rev245_2_exec_update_inv_full_v1 logic. */
        if( game && game->inv_pool && effective_inv_index >= 0 &&
            effective_inv_index < game->inv_pool->count )
        {
            struct GameCacheComponent* prefill_src = comp;
            if( comp->type == COMPONENT_TYPE_INV_TEXT && inv_text_peer )
                prefill_src = inv_text_peer;

            struct UIInventory* inv = &game->inv_pool->inventories[effective_inv_index];
            /* invSlotObjId / invSlotObjCount are sized width*height in config_component.c, not
             * UI_INV_SLOT_OFFSET_MAX (that limit applies to invSlotGraphic / offsets only). */
            int inv_obj_slots = comp->width * comp->height;
            if( inv_obj_slots < 0 )
                inv_obj_slots = 0;
            if( inv_obj_slots > UI_INVENTORY_MAX_ITEMS )
                inv_obj_slots = UI_INVENTORY_MAX_ITEMS;
            for( int si = 0; si < inv_obj_slots; si++ )
            {
                if( prefill_src->invSlotObjId && prefill_src->invSlotObjId[si] > 0 )
                {
                    int obj_id = prefill_src->invSlotObjId[si] - 1;
                    int obj_count =
                        prefill_src->invSlotObjCount ? prefill_src->invSlotObjCount[si] : 1;
                    struct UIInventoryItem* item = &inv->items[si];
                    item->obj_id = prefill_src->invSlotObjId[si];
                    item->obj_count = obj_count > 0 ? obj_count : 1;
                    /* Pre-cache the sprite so it's available during render. */
                    struct DashSprite* cached = obj_icon_get(game, obj_id, item->obj_count);
                    struct DashSprite* sp = cached ? dashsprite_clone(cached) : NULL;
                    if( sp )
                    {
                        struct DashSprite** row = malloc(sizeof(struct DashSprite*));
                        if( row )
                        {
                            row[0] = sp;
                            int sid = uiscene_attach_sprite_row(game->ui_scene, row, 1);
                            if( sid >= 0 )
                            {
                                item->scene_id = sid;
                                item->atlas_index = 0;
                            }
                            else
                            {
                                dashsprite_free(sp);
                                free(row);
                            }
                        }
                        else
                            dashsprite_free(sp);
                    }
                }
            }
        }

        uitree_translate_agnostic_fields(ui, iid, comp);
        uitree_push_rs_stats_note_push(push_rs_stats, iid);
        if( comp->type == COMPONENT_TYPE_INV_TEXT && iid >= 0 &&
            (uint32_t)iid < ui->component_count )
        {
            ui->components[iid].inv_text_peer_inv_component_id =
                inv_text_peer ? inv_text_peer->id : -1;
            if( ui_scene && bcd )
            {
                int resolved = ensure_font_id(ui_scene, bcd, comp->font);
                ui->components[iid].inv_text_font_id = resolved;
                ui->components[iid].inv_text_color = comp->colour;
                ui->components[iid].inv_text_center = comp->center ? 1 : 0;
                ui->components[iid].inv_text_shadowed = comp->shadowed ? 1 : 0;
            }
        }
    }
    break;
    case COMPONENT_TYPE_MODEL:
    {
        if( !game )
        {
            if( push_rs_stats )
                push_rs_stats->skip_model_no_game++;
            return 0;
        }
        if( comp->modelType == 1 && comp->model >= 0 && !gamecache_get_model(bcd, comp->model) )
        {
            if( out_req )
            {
                out_req->kind = UITREE_ASSET_MODEL;
                out_req->u.model.model_id = comp->model;
            }
            return -1;
        }
        int scene_id = -1;
        if( comp->modelType != 0 )
        {
            struct DashModel* m = uitree_rs_dashmodel_for_model_component(game, bcd, comp);
            if( m )
            {
                scene_id = uiscene_element_acquire_with_model(ui_scene, -1, m, NULL);
                if( scene_id < 0 )
                    dashmodel_free(m);
            }
        }
        int32_t mid = uitree_push_rs_model(
            ui, parent_uitree_idx, comp->id, scene_id, px, py, comp->width, comp->height);
        uitree_translate_agnostic_fields(ui, mid, comp);
        uitree_push_rs_stats_note_push(push_rs_stats, mid);
    }
    break;
    default:
        if( push_rs_stats )
            push_rs_stats->skip_default_type++;
        break;
    }
    return 0;
}

/** Non-NULL when `TORI_UITREE_SUBTREE_STATS` matches `root_id` (see
 * `uitree_subtree_stats_env_root_id`). */
static struct uitree_push_rs_stats*
uitree_push_rs_stats_buf_if_tracing(
    int root_id,
    struct uitree_push_rs_stats* out_buf)
{
    if( !out_buf || uitree_subtree_stats_env_root_id() != root_id || root_id < 0 )
        return NULL;
    memset(out_buf, 0, sizeof(*out_buf));
    return out_buf;
}

int
uitree_load_single_component_tree_from_gamecache(
    struct GGame* game,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* gamecache,
    int component_root_id)
{
    if( !game || !ui || !ui_scene || !gamecache || component_root_id < 0 )
        return -1;
    if( ui->component_count != 0 )
        return -2;
    struct GameCacheComponent* root = gamecache_get_component(gamecache, component_root_id);
    if( !root )
        return -1;
    struct uitree_push_rs_stats st;
    struct uitree_push_rs_stats* pst = uitree_push_rs_stats_buf_if_tracing(component_root_id, &st);
    push_rs_from_cache_component(
        game, ui, ui_scene, gamecache, -1, root, 0, 0, -1, NULL, root, pst, NULL);
    if( pst )
        uitree_push_rs_stats_flush(
            pst, "uitree_load_single_component_tree_from_gamecache", component_root_id);
    uitree_load_debug_log_subtree_watch_id(ui);
    return 0;
}

static int
expand_sidebar_rs_tree(
    struct GGame* game,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* bcd,
    int32_t sidebar_idx,
    int component_no,
    int inv_index,
    struct UITreeLoaderAssetRequest* out_req)
{
    if( !bcd || component_no < 0 || sidebar_idx < 0 ||
        (uint32_t)sidebar_idx >= ui->component_count )
        return 0;
    struct GameCacheComponent* root = gamecache_get_component(bcd, component_no);
    if( !root )
        return 0;
    int sx = ui->components[sidebar_idx].position.x;
    int sy = ui->components[sidebar_idx].position.y;
    int bx = sx + root->x;
    int by = sy + root->y;
    struct uitree_push_rs_stats st;
    struct uitree_push_rs_stats* pst = uitree_push_rs_stats_buf_if_tracing(component_no, &st);
    int rc = push_rs_from_cache_component(
        game, ui, ui_scene, bcd, sidebar_idx, root, bx, by, inv_index, NULL, root, pst, out_req);
    if( pst )
        uitree_push_rs_stats_flush(pst, "expand_sidebar_rs_tree", component_no);
    uitree_load_debug_log_subtree_watch_id(ui);
    return rc;
}

static void
expand_chat_dialog_rs_tree(
    struct GGame* game,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* bcd,
    int32_t chat_dialog_idx,
    int component_no,
    int inv_index)
{
    if( !bcd || component_no < 0 || chat_dialog_idx < 0 ||
        (uint32_t)chat_dialog_idx >= ui->component_count )
        return;
    struct GameCacheComponent* root = gamecache_get_component(bcd, component_no);
    if( !root )
        return;
    int sx = ui->components[chat_dialog_idx].position.x;
    int sy = ui->components[chat_dialog_idx].position.y;
    int bx = sx + root->x;
    int by = sy + root->y;
    struct uitree_push_rs_stats st;
    struct uitree_push_rs_stats* pst = uitree_push_rs_stats_buf_if_tracing(component_no, &st);
    push_rs_from_cache_component(
        game, ui, ui_scene, bcd, chat_dialog_idx, root, bx, by, inv_index, NULL, root, pst, NULL);
    if( pst )
        uitree_push_rs_stats_flush(pst, "expand_chat_dialog_rs_tree", component_no);
    uitree_load_debug_log_subtree_watch_id(ui);
}

static void
expand_sidebar_overlay_rs_tree(
    struct GGame* game,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* bcd,
    int32_t sidebar_overlay_idx,
    int component_no,
    int inv_index)
{
    if( !bcd || component_no < 0 || sidebar_overlay_idx < 0 ||
        (uint32_t)sidebar_overlay_idx >= ui->component_count )
        return;
    struct GameCacheComponent* root = gamecache_get_component(bcd, component_no);
    if( !root )
        return;
    int sx = ui->components[sidebar_overlay_idx].position.x;
    int sy = ui->components[sidebar_overlay_idx].position.y;
    int bx = sx + root->x;
    int by = sy + root->y;
    struct uitree_push_rs_stats st;
    struct uitree_push_rs_stats* pst = uitree_push_rs_stats_buf_if_tracing(component_no, &st);
    push_rs_from_cache_component(
        game,
        ui,
        ui_scene,
        bcd,
        sidebar_overlay_idx,
        root,
        bx,
        by,
        inv_index,
        NULL,
        root,
        pst,
        NULL);
    if( pst )
        uitree_push_rs_stats_flush(pst, "expand_sidebar_overlay_rs_tree", component_no);
    uitree_load_debug_log_subtree_watch_id(ui);
}

static void
expand_viewport_overlay_rs_tree(
    struct GGame* game,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* bcd,
    int32_t viewport_overlay_idx,
    int component_no,
    int inv_index)
{
    if( !bcd || component_no < 0 || viewport_overlay_idx < 0 ||
        (uint32_t)viewport_overlay_idx >= ui->component_count )
        return;
    struct GameCacheComponent* root = gamecache_get_component(bcd, component_no);
    if( !root )
        return;
    int sx = ui->components[viewport_overlay_idx].position.x;
    int sy = ui->components[viewport_overlay_idx].position.y;
    int bx = sx + root->x;
    int by = sy + root->y;
    struct uitree_push_rs_stats st;
    struct uitree_push_rs_stats* pst = uitree_push_rs_stats_buf_if_tracing(component_no, &st);
    push_rs_from_cache_component(
        game,
        ui,
        ui_scene,
        bcd,
        viewport_overlay_idx,
        root,
        bx,
        by,
        inv_index,
        NULL,
        root,
        pst,
        NULL);
    if( pst )
        uitree_push_rs_stats_flush(pst, "expand_viewport_overlay_rs_tree", component_no);
    uitree_load_debug_log_subtree_watch_id(ui);
}

static int
uitree_load_resolve_minimenu_font_id(
    struct UIScene* ui_scene,
    struct GameCache* gc,
    char const* ini_font_name)
{
    int id = -1;
    if( gc && ini_font_name && ini_font_name[0] )
    {
        id = gamecache_get_font_ref_id(gc, ini_font_name);
        if( id < 0 && ui_scene )
            id = uiscene_font_find_id(ui_scene, ini_font_name);
    }
    if( id >= 0 )
        return id;

    if( gc )
    {
        id = gamecache_get_font_ref_id(gc, "b12");
        if( id < 0 && ui_scene )
            id = uiscene_font_find_id(ui_scene, "b12");
    }
    else if( ui_scene )
        id = uiscene_font_find_id(ui_scene, "b12");
    if( id >= 0 )
        return id;

    if( gc )
    {
        id = gamecache_get_font_ref_id(gc, "p11");
        if( id < 0 && ui_scene )
            id = uiscene_font_find_id(ui_scene, "p11");
    }
    else if( ui_scene )
        id = uiscene_font_find_id(ui_scene, "p11");
    return id;
}

static int
uitree_load_resolve_chat_font_id(
    struct UIScene* ui_scene,
    struct GameCache* gc,
    char const* ini_font_name)
{
    int id = -1;
    if( gc && ini_font_name && ini_font_name[0] )
    {
        id = gamecache_get_font_ref_id(gc, ini_font_name);
        if( id < 0 && ui_scene )
            id = uiscene_font_find_id(ui_scene, ini_font_name);
    }
    if( id >= 0 )
        return id;

    static char const* const fallback[] = { "p11", "p12", "b12", "q8" };
    for( int i = 0; i < 4; i++ )
    {
        id = gc ? gamecache_get_font_ref_id(gc, fallback[i]) : -1;
        if( id < 0 && ui_scene )
            id = uiscene_font_find_id(ui_scene, fallback[i]);
        if( id >= 0 )
            return id;
    }
    return -1;
}

static int
load_layout(
    struct LayoutLoad* load,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* buildcachedat,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct UITreeLoaderAssetRequest* out_req)
{
    struct LayoutItem* layout_entry = NULL;
    struct ComponentEntry* component_entry = NULL;

    for( int i = 0; i < load->entry_count; i++ )
    {
        layout_entry = &load->entries[i];
        component_entry = dashmap_search(component_hmap, layout_entry->component, DASHMAP_FIND);
        assert(component_entry && "Component for layout entry not found in hmap");
        int lx = layout_entry->x;
        int ly = layout_entry->y;
        if( lx == 0 && ly == 0 )
        {
            lx = component_entry->def_x;
            ly = component_entry->def_y;
        }
        switch( component_entry->type )
        {
        case UIELEM_BUILTIN_COMPASS:
        {
            int32_t idx = uitree_push_compass(
                ui,
                -1,
                component_entry->sprite_id,
                component_entry->sprite_index,
                lx,
                ly,
                component_entry->width,
                component_entry->height,
                component_entry->anchor_x,
                component_entry->anchor_y);
            if( layout_entry->always_dirty && idx >= 0 )
                ui->components[idx].always_dirty = 1;
        }
        break;
        case UIELEM_BUILTIN_MINIMAP:
        {
            int32_t idx = uitree_push_minimap(
                ui,
                -1,
                lx,
                ly,
                component_entry->width,
                component_entry->height,
                component_entry->anchor_x,
                component_entry->anchor_y);
            if( layout_entry->always_dirty && idx >= 0 )
                ui->components[idx].always_dirty = 1;
        }
        break;
        case UIELEM_BUILTIN_WORLD: // "world"
        {
            int32_t idx = uitree_push_world(
                ui,
                -1,
                lx,
                ly,
                component_entry->width,
                component_entry->height,
                component_entry->level_mask);
            if( layout_entry->always_dirty && idx >= 0 )
                ui->components[idx].always_dirty = 1;
        }
        break;
        case UIELEM_BUILTIN_HOVER_TOOLTIP:
        {
            int font_id = -1;
            if( component_entry->font[0] != '\0' )
            {
                font_id = gamecache_get_font_ref_id(buildcachedat, component_entry->font);
                if( font_id < 0 && ui_scene )
                    font_id = uiscene_font_find_id(ui_scene, component_entry->font);
            }

            int32_t idx = uitree_push_hover_tooltip(
                ui, -1, lx, ly, component_entry->width, component_entry->height);
            if( idx >= 0 )
            {
                ui->components[idx].u.hover_tooltip.font_id = font_id;
                if( layout_entry->always_dirty )
                    ui->components[idx].always_dirty = 1;
            }
        }
        break;
        case UIELEM_BUILTIN_MINIMENU:
        {
            int font_id = uitree_load_resolve_minimenu_font_id(
                ui_scene,
                buildcachedat,
                component_entry->font[0] != '\0' ? component_entry->font : NULL);
            int32_t idx = uitree_push_minimenu(ui, -1);
            if( idx >= 0 )
            {
                ui->components[idx].u.minimenu.font_id = font_id;
                if( layout_entry->always_dirty )
                    ui->components[idx].always_dirty = 1;
            }
            if( game )
                game->minimenu_regions = component_entry->minimenu_regions;
        }
        break;
        case UIELEM_BUILTIN_CROSSHAIR:
        {
            int32_t idx = uitree_push_crosshair(ui, -1);
            if( idx >= 0 )
            {
                ui->components[idx].u.crosshair.scene_id =
                    uiscene_element_id_by_name(ui_scene, "cross");
                ui->components[idx].u.crosshair.hotspot_offset =
                    component_entry->crosshair_hotspot_offset;
                if( layout_entry->always_dirty )
                    ui->components[idx].always_dirty = 1;
            }
        }
        break;
        case UIELEM_BUILTIN_CHAT_DIALOG:
        {
            int32_t idx = uitree_push_builtin_chat_dialog(
                ui, -1, lx, ly, component_entry->width, component_entry->height);
            if( layout_entry->always_dirty && idx >= 0 )
                ui->components[idx].always_dirty = 1;
        }
        break;
        case UIELEM_BUILTIN_SIDEBAR_OVERLAY:
        {
            int32_t idx = uitree_push_builtin_sidebar_overlay(
                ui, -1, lx, ly, component_entry->width, component_entry->height);
            if( layout_entry->always_dirty && idx >= 0 )
                ui->components[idx].always_dirty = 1;
        }
        break;
        case UIELEM_BUILTIN_VIEWPORT_OVERLAY:
        {
            int32_t idx = uitree_push_builtin_viewport_overlay(
                ui, -1, lx, ly, component_entry->width, component_entry->height);
            if( layout_entry->always_dirty && idx >= 0 )
                ui->components[idx].always_dirty = 1;
        }
        break;
        case UIELEM_BUILTIN_CHAT_MESSAGES:
        {
            int font_id = uitree_load_resolve_chat_font_id(
                ui_scene,
                buildcachedat,
                component_entry->font[0] != '\0' ? component_entry->font : NULL);
            int32_t idx = uitree_push_chat_messages(
                ui, -1, lx, ly, component_entry->width, component_entry->height);
            if( idx >= 0 )
            {
                ui->components[idx].u.chat_messages.font_id = font_id;
                if( layout_entry->always_dirty )
                    ui->components[idx].always_dirty = 1;
            }
            if( game )
            {
                game->chat_layout = component_entry->chat_layout;
                game->chat_layout_valid = 1;
            }
        }
        break;
        case UIELEM_BUILTIN_CHAT_INPUT:
        {
            int font_id = uitree_load_resolve_chat_font_id(
                ui_scene,
                buildcachedat,
                component_entry->font[0] != '\0' ? component_entry->font : NULL);
            int32_t idx = uitree_push_chat_input(
                ui, -1, lx, ly, component_entry->width, component_entry->height);
            if( idx >= 0 )
            {
                ui->components[idx].u.chat_input.font_id = font_id;
                if( layout_entry->always_dirty )
                    ui->components[idx].always_dirty = 1;
            }
            if( game && (component_entry->chat_geom_mask & CHAT_LAYOUT_BIT_INPUT_LINE_Y_LOCAL) )
            {
                if( !game->chat_layout_valid )
                    chat_layout_builtin(&game->chat_layout);
                game->chat_layout.input_line_y_local =
                    component_entry->chat_layout.input_line_y_local;
                game->chat_layout_valid = 1;
            }
        }
        break;
        case UIELEM_BUILTIN_CHAT_PRIVACY:
        {
            int font_id = uitree_load_resolve_chat_font_id(
                ui_scene,
                buildcachedat,
                component_entry->font[0] != '\0' ? component_entry->font : NULL);
            int32_t idx = uitree_push_chat_privacy(
                ui, -1, lx, ly, component_entry->width, component_entry->height);
            if( idx >= 0 )
            {
                ui->components[idx].u.chat_privacy.font_id = font_id;
                if( layout_entry->always_dirty )
                    ui->components[idx].always_dirty = 1;
            }
        }
        break;
        case UIELEM_BUILTIN_COLLISIONMAP_OVERLAY:
        {
            uitree_push_collisionmap_overlay(ui, -1);
            if( game )
                game->debug_collisionmap_overlay = true;
        }
        break;
        case UIELEM_BUILTIN_REDSTONE_TAB:
        {
            int32_t idx = uitree_push_redstone_tab(
                ui,
                -1,
                component_entry->tabno,
                component_entry->sprite_id,
                component_entry->sprite_index,
                component_entry->sprite_id_active,
                component_entry->sprite_index_active,
                lx,
                ly,
                component_entry->width,
                component_entry->height);
            if( layout_entry->always_dirty && idx >= 0 )
                ui->components[idx].always_dirty = 1;
        }
        break;
        case UIELEM_BUILTIN_SIDEBAR: // "builtin_sidebar"
        {
            int inv_index = -1;
            if( inv_pool && component_entry->inv[0] != '\0' )
                inv_index = uitree_inv_pool_find_by_name(inv_pool, component_entry->inv);
            int32_t sidx = uitree_push_builtin_sidebar(
                ui,
                -1,
                component_entry->tabno,
                component_entry->componentno,
                inv_index,
                lx,
                ly,
                component_entry->width,
                component_entry->height);
            if( layout_entry->always_dirty && sidx >= 0 )
                ui->components[sidx].always_dirty = 1;
            if( expand_sidebar_rs_tree(
                    game,
                    ui,
                    ui_scene,
                    buildcachedat,
                    sidx,
                    component_entry->componentno,
                    inv_index,
                    out_req) != 0 )
                return -1;
        }
        case UIELEM_BUILTIN_CHAT:
        case UIELEM_BUILTIN_TAB_ICONS: // "builtin_tab_icons"

        case UIELEM_BUILTIN_SPRITE:
        {
            int32_t idx;
            if( layout_entry->flags != 0 )
            {
                idx = uitree_push_sprite_relative(
                    ui,
                    -1,
                    component_entry->sprite_id,
                    component_entry->sprite_index,
                    layout_entry->flags,
                    layout_entry->top,
                    layout_entry->right,
                    layout_entry->bottom,
                    layout_entry->left,
                    component_entry->width,
                    component_entry->height);
            }
            else
            {
                idx = uitree_push_sprite_xy(
                    ui,
                    -1,
                    component_entry->sprite_id,
                    component_entry->sprite_index,
                    lx,
                    ly,
                    component_entry->width,
                    component_entry->height);
            }
            if( layout_entry->always_dirty && idx >= 0 )
                ui->components[idx].always_dirty = 1;
        }
        break;
        default:
            assert(0 && "Unknown component type in layout");
            break;
        }
    }
    return 0;
}

static uint32_t
load_kind(const char* str)
{
    if( strcmp(str, "sprite") == 0 )
        return LOAD_KIND_SPRITE;
    else if( strcmp(str, "component") == 0 )
        return LOAD_KIND_COMPONENT;
    else if( strcmp(str, "layout") == 0 )
        return LOAD_KIND_LAYOUT;
    else if( strcmp(str, "inv") == 0 )
        return LOAD_KIND_INV;
    return LOAD_KIND_NONE;
}

static void
load_item(
    struct CurrentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct UIInventoryPool* inv_pool,
    struct GGame* game)
{
    switch( load->kind )
    {
    case LOAD_KIND_SPRITE:
        load_sprite(&load->_sprite, sprite_hmap, ui, ui_scene, game->buildcachedat);
        break;
    case LOAD_KIND_COMPONENT:
        load_component(
            &load->_component, sprite_hmap, component_hmap, ui, ui_scene, game->gamecache);
        break;
    case LOAD_KIND_LAYOUT:
        load_layout(
            &load->_layout, component_hmap, ui, ui_scene, game->gamecache, inv_pool, game, NULL);
        break;
    case LOAD_KIND_INV:
        load_inv(&load->_inv, inv_pool, game, ui_scene);
        break;

    default:
    {
        assert(0 && "Unknown load kind");
    }
    break;
    }
}

static void
on_itemname(
    struct CurrentLoad* load,
    const char* value)
{
    switch( load->kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(load->_sprite.name, value, sizeof(load->_sprite.name) - 1);
        break;
    case LOAD_KIND_COMPONENT:
        strncpy(load->_component.name, value, sizeof(load->_component.name) - 1);
        break;
    case LOAD_KIND_LAYOUT:
        strncpy(load->_layout.name, value, sizeof(load->_layout.name) - 1);
        break;
    case LOAD_KIND_INV:
        strncpy(load->_inv.name, value, sizeof(load->_inv.name) - 1);
        break;
    }
}

static void
uitree_resolve_game_uiscene_sprite_ids(
    struct GGame* game,
    struct UIScene* ui_scene)
{
    if( !game || !ui_scene || !game->ui_root_buffer )
        return;
    struct UITree* ui = game->ui_root_buffer;
    ui->ui_scrollbar0_element_id = uiscene_element_id_by_name(ui_scene, "scrollbar0");
    ui->ui_scrollbar1_element_id = uiscene_element_id_by_name(ui_scene, "scrollbar1");
    ui->ui_minimap_mapdots0_element_id = uiscene_element_id_by_name(ui_scene, "mapdots0");
    ui->ui_minimap_mapdots1_element_id = uiscene_element_id_by_name(ui_scene, "mapdots1");
    ui->ui_minimap_mapdots3_element_id = uiscene_element_id_by_name(ui_scene, "mapdots3");
    ui->ui_minimap_mapdots4_element_id = uiscene_element_id_by_name(ui_scene, "mapdots4");
    ui->ui_minimap_mapmarker2_element_id = uiscene_element_id_by_name(ui_scene, "mapmarker2");
    ui->ui_minimap_mapmarker_element_id = uiscene_element_id_by_name(ui_scene, "mapmarker");

    game->hitmarks_uiscene_element_id = uiscene_element_id_by_name(ui_scene, "hitmarks");

    {
        int fid = -1;
        if( game->gamecache )
            fid = gamecache_get_font_ref_id(game->gamecache, "p11");
        if( fid < 0 )
            fid = uiscene_font_find_id(ui_scene, "p11");
        game->hitsplat_damage_font_id = fid;
    }
}

void
uitree_from_revconfig_buildcachedat(
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct RevConfigBuffer* revconfig_buffer)
{
    /* Reimplemented as a simple synchronous loop over the incremental loader.
     * In this legacy call-path all assets are expected to be pre-loaded, so the
     * loader should never return UITREE_LOADER_NEEDS_ASSET.  If it does, we log
     * and stop early (tree will be incomplete). */
    struct UITreeLoader* loader = uitree_loader_new(ui, revconfig_buffer);
    if( !loader )
    {
        fprintf(stderr, "uitree_from_revconfig_buildcachedat: out of memory\n");
        return;
    }

    enum UITreeLoaderStatus status;
    while( (status = uitree_loader_step(loader)) == UITREE_LOADER_RUNNING )
        ; /* drain */

    if( status == UITREE_LOADER_NEEDS_ASSET )
    {
        int npc = uitree_loader_pending_asset_count(loader);
        const struct UITreeLoaderAssetRequest* reqs = uitree_loader_pending_assets(loader);
        for( int pi = 0; pi < npc; pi++ )
        {
            fprintf(
                stderr,
                "uitree_from_revconfig_buildcachedat: asset not pre-loaded (kind=%d), "
                "tree may be incomplete\n",
                (int)reqs[pi].kind);
        }
    }

    uitree_loader_free(loader);
}

int
uitree_revconfig_collect_inv_obj_ids(
    struct RevConfigBuffer* revconfig_buffer,
    int* out_ids,
    int max_ids)
{
    if( !revconfig_buffer || !out_ids || max_ids <= 0 )
        return 0;

    int n = 0;
    enum LoadKind current_kind = LOAD_KIND_NONE;

    for( uint32_t i = 0; i < revconfig_buffer->field_count; i++ )
    {
        struct RevConfigField* field = &revconfig_buffer->fields[i];
        if( field->kind == RCFIELD_ITEMTYPE )
        {
            current_kind = (enum LoadKind)load_kind(field->value);
        }
        else if( field->kind == RCFIELD_INV_ITEM && current_kind == LOAD_KIND_INV )
        {
            int id = atoi(field->value);
            int dup = 0;
            for( int j = 0; j < n; j++ )
            {
                if( out_ids[j] == id )
                {
                    dup = 1;
                    break;
                }
            }
            if( !dup && n < max_ids )
                out_ids[n++] = id;
        }
        else if( field->kind == RCFIELD_ITEMDONE )
        {
            current_kind = LOAD_KIND_NONE;
        }
    }
    return n;
}

void
uitree_load_inventories_from_revconfig(
    struct UIScene* ui_scene,
    struct GGame* game,
    struct UIInventoryPool* inv_pool,
    struct RevConfigBuffer* revconfig_buffer)
{
    if( !revconfig_buffer || !inv_pool )
        return;

    struct CurrentLoad load = { 0 };

    for( uint32_t i = 0; i < revconfig_buffer->field_count; i++ )
    {
        struct RevConfigField* field = &revconfig_buffer->fields[i];
        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
        {
            uint32_t k = load_kind(field->value);
            load.kind = (enum LoadKind)k;
            if( load.kind == LOAD_KIND_INV )
                memset(&load._inv, 0, sizeof(load._inv));
        }
        break;
        case RCFIELD_ITEMNAME:
            if( load.kind == LOAD_KIND_INV )
                strncpy(load._inv.name, field->value, sizeof(load._inv.name) - 1);
            break;
        case RCFIELD_ITEMDONE:
            if( load.kind == LOAD_KIND_INV )
                load_inv(&load._inv, inv_pool, game, ui_scene);
            load.kind = LOAD_KIND_NONE;
            memset(&load, 0, sizeof(load));
            break;
        case RCFIELD_INV_ITEM:
            if( load.kind == LOAD_KIND_INV && load._inv.item_count < UI_INVENTORY_MAX_ITEMS )
                load._inv.item_ids[load._inv.item_count++] = atoi(field->value);
            break;
        default:
            break;
        }
    }
}

static int32_t
uitree_find_chat_dialog_builtin(struct UITree* ui)
{
    if( !ui )
        return -1;
    for( uint32_t i = 0; i < ui->component_count; i++ )
    {
        if( ui->components[i].type == UIELEM_BUILTIN_CHAT_DIALOG )
            return (int32_t)i;
    }
    return -1;
}

static int32_t
uitree_find_sidebar_overlay_builtin(struct UITree* ui)
{
    if( !ui )
        return -1;
    for( uint32_t i = 0; i < ui->component_count; i++ )
    {
        if( ui->components[i].type == UIELEM_BUILTIN_SIDEBAR_OVERLAY )
            return (int32_t)i;
    }
    return -1;
}

static int32_t
uitree_find_viewport_overlay_builtin(struct UITree* ui)
{
    if( !ui )
        return -1;
    for( uint32_t i = 0; i < ui->component_count; i++ )
    {
        if( ui->components[i].type == UIELEM_BUILTIN_VIEWPORT_OVERLAY )
            return (int32_t)i;
    }
    return -1;
}

void
uitree_expand_chat_dialog_for_interface(
    struct GGame* game,
    int component_id)
{
    if( !game || !game->ui_root_buffer || !game->ui_scene || !game->gamecache )
        return;

    struct UITree* ui = game->ui_root_buffer;
    int32_t cd_idx = uitree_find_chat_dialog_builtin(ui);
    if( cd_idx < 0 )
        return;

    uitree_clear_chat_dialog_children(ui, cd_idx);

    if( component_id >= 0 )
    {
        expand_chat_dialog_rs_tree(
            game, ui, game->ui_scene, game->gamecache, cd_idx, component_id, -1);
    }

    ui->components[cd_idx].is_dirty = 1;
    uitree_mark_all_dirty(ui);
}

void
uitree_expand_sidebar_overlay_for_interface(
    struct GGame* game,
    int component_id)
{
    if( !game || !game->ui_root_buffer || !game->ui_scene || !game->gamecache )
        return;

    struct UITree* ui = game->ui_root_buffer;
    int32_t so_idx = uitree_find_sidebar_overlay_builtin(ui);
    if( so_idx < 0 )
        return;

    uitree_clear_sidebar_overlay_children(ui, so_idx);

    if( component_id >= 0 )
    {
        expand_sidebar_overlay_rs_tree(
            game, ui, game->ui_scene, game->gamecache, so_idx, component_id, -1);
    }

    ui->components[so_idx].is_dirty = 1;
    uitree_mark_all_dirty(ui);
}

void
uitree_expand_viewport_overlay_for_interface(
    struct GGame* game,
    int component_id)
{
    if( !game || !game->ui_root_buffer || !game->ui_scene || !game->gamecache )
        return;

    struct UITree* ui = game->ui_root_buffer;
    int32_t vo_idx = uitree_find_viewport_overlay_builtin(ui);
    if( vo_idx < 0 )
        return;

    uitree_clear_viewport_overlay_children(ui, vo_idx);

    if( component_id >= 0 )
    {
        expand_viewport_overlay_rs_tree(
            game, ui, game->ui_scene, game->gamecache, vo_idx, component_id, -1);
    }

    ui->components[vo_idx].is_dirty = 1;
    uitree_mark_all_dirty(ui);
}

void
uitree_expand_sidebar_for_tab(
    struct GGame* game,
    int tabno,
    int component_id)
{
    if( !game || !game->ui_root_buffer || !game->ui_scene || !game->gamecache )
        return;

    struct UITree* ui = game->ui_root_buffer;
    int32_t sidebar_idx = -1;
    for( uint32_t i = 0; i < ui->component_count; i++ )
    {
        struct StaticUIComponent* c = &ui->components[i];
        if( c->type == UIELEM_BUILTIN_SIDEBAR && c->u.sidebar.tabno == tabno )
        {
            sidebar_idx = (int32_t)i;
            break;
        }
    }
    if( sidebar_idx < 0 )
        return;

    uitree_clear_sidebar_children(ui, sidebar_idx);

    struct StaticUIComponent* sb = &ui->components[sidebar_idx];
    sb->u.sidebar.componentno = component_id;
    sb->is_dirty = 1;

    if( component_id >= 0 )
    {
        expand_sidebar_rs_tree(
            game,
            ui,
            game->ui_scene,
            game->gamecache,
            sidebar_idx,
            component_id,
            sb->u.sidebar.inv_index,
            NULL);
    }

    if( game->iface && component_id == UITREE_DEBUG_SUBTREE_COMPONENT_ID &&
        (getenv("TORI_UITREE_SUBTREE_STATS") != NULL ||
         getenv("TORI_UITREE_TRAVERSE_STATS") != NULL) )
    {
        fprintf(
            stderr,
            "[uitree_expand_sidebar_for_tab] tabno=%d component_id=%d iface.selected_tab=%d "
            "iface.sidebar_interface_id=%d sidebar_inv_index=%d sidebar_builtin_first_child=%d\n",
            tabno,
            component_id,
            game->iface->selected_tab,
            game->iface->sidebar_interface_id,
            sb->u.sidebar.inv_index,
            (int)sb->first_child);
    }

    uitree_mark_all_dirty(ui);
}

void
uitree_debug_log_sidebar_state(
    struct GGame* game,
    char const* where,
    int tab_id,
    int component_id)
{
    static int enabled = -1;
    if( enabled < 0 )
        enabled = getenv("TORI_SIDEBAR_DEBUG") != NULL ? 1 : 0;
    if( !enabled || !game || !game->iface || !game->ui_root_buffer )
        return;

    struct UITree* ui = game->ui_root_buffer;
    int32_t sidebar_idx = -1;
    int iface_tid = -999;
    int fc = -1;
    int cno = -999;

    if( tab_id >= 0 && tab_id < 14 )
        iface_tid = game->iface->tab_interface_id[tab_id];

    if( tab_id >= 0 && tab_id <= 13 )
    {
        for( uint32_t i = 0; i < ui->component_count; i++ )
        {
            struct StaticUIComponent* c = &ui->components[i];
            if( c->type == UIELEM_BUILTIN_SIDEBAR && c->u.sidebar.tabno == tab_id )
            {
                sidebar_idx = (int32_t)i;
                fc = c->first_child;
                cno = c->u.sidebar.componentno;
                break;
            }
        }
    }

    fprintf(
        stderr,
        "[TORI_SIDEBAR_DEBUG] %s tab_id=%d pkt_component_id=%d iface->tab_interface_id[tab]=%d "
        "uitree_sidebar_idx=%d sidebar.componentno=%d first_child=%d selected_tab=%d\n",
        where ? where : "?",
        tab_id,
        component_id,
        iface_tid,
        (int)sidebar_idx,
        cno,
        fc,
        game->iface->selected_tab);
}

void
uitree_load_ui_from_revconfig(
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct RevConfigBuffer* revconfig_buffer)
{
    struct CurrentLoad load = { 0 };

    struct DashMapConfig sprite_config = {
        .buffer = malloc(1024 * sizeof(struct SpriteEntry)),
        .buffer_size = 1024 * sizeof(struct SpriteEntry),
        .key_size = 64, // Max sprite name length
        .entry_size = sizeof(struct SpriteEntry),
    };
    struct DashMap* sprite_hmap = dashmap_new(&sprite_config, 0);

    struct DashMapConfig component_config = {
        .buffer = malloc(1024 * sizeof(struct ComponentEntry)),
        .buffer_size = 1024 * sizeof(struct ComponentEntry),
        .key_size = 64, // Max component name length
        .entry_size = sizeof(struct ComponentEntry),
    };
    struct DashMap* component_hmap = dashmap_new(&component_config, 0);

    for( uint32_t i = 0; i < revconfig_buffer->field_count; i++ )
    {
        struct RevConfigField* field = &revconfig_buffer->fields[i];
        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
        {
            uint32_t k = load_kind(field->value);
            load.kind = (enum LoadKind)k;
            if( load.kind == LOAD_KIND_INV )
                memset(&load._inv, 0, sizeof(load._inv));
        }
        break;
        case RCFIELD_ITEMNAME:
            on_itemname(&load, field->value);
            break;
        case RCFIELD_ITEMDONE:
            if( load.kind != LOAD_KIND_INV )
                load_item(&load, sprite_hmap, component_hmap, ui, ui_scene, inv_pool, game);
            load.kind = LOAD_KIND_NONE;
            memset(&load, 0, sizeof(load));
            break;
        case RCFIELD_CACHE_TABLE:
            strncpy(load._sprite.table, field->value, sizeof(load._sprite.table) - 1);
            break;
        case RCFIELD_CACHE_ARCHIVE:
            strncpy(load._sprite.archive, field->value, sizeof(load._sprite.archive) - 1);
            break;
        case RCFIELD_CACHE_CONTAINER:
            strncpy(load._sprite.container, field->value, sizeof(load._sprite.container) - 1);
            break;
        case RCFIELD_CACHE_INDEX_FILENAME:
            strncpy(
                load._sprite.index_filename, field->value, sizeof(load._sprite.index_filename) - 1);
            break;
        case RCFIELD_CACHE_DATA_FILENAME:
            strncpy(
                load._sprite.data_filename, field->value, sizeof(load._sprite.data_filename) - 1);
            break;
        case RCFIELD_CACHE_FORMAT:
            strncpy(load._sprite.format, field->value, sizeof(load._sprite.format) - 1);
            break;
        case RCFIELD_CACHE_ATLAS_INDEX:
            load._sprite.atlas_mode = SPRITELOAD_ATLAS_MODE_INDEX;
            load._sprite.atlas_index = atoi(field->value);
            break;
        case RCFIELD_CACHE_ATLAS_COUNT:
            load._sprite.atlas_mode = SPRITELOAD_ATLAS_MODE_COUNT;
            load._sprite.atlas_count = atoi(field->value);
            break;
        case RCFIELD_CACHE_TRANSFORM:
            if( load._sprite.transform_count < 5 )
            {
                strncpy(
                    load._sprite.transforms[load._sprite.transform_count],
                    field->value,
                    sizeof(load._sprite.transforms[load._sprite.transform_count]) - 1);
                load._sprite.transform_count++;
            }
            else
            {
                assert(0 && "Too many transforms specified for sprite");
            }
            break;
        case RCFIELD_CACHE_CROP_X:
        {
            assert(
                load.kind == LOAD_KIND_SPRITE && "CACHE_CROP_X field must be within a sprite item");
            load._sprite.crop_x = atoi(field->value);
        }
        break;
        case RCFIELD_CACHE_CROP_Y:
        {
            assert(
                load.kind == LOAD_KIND_SPRITE && "CACHE_CROP_Y field must be within a sprite item");
            load._sprite.crop_y = atoi(field->value);
        }
        break;
        case RCFIELD_CACHE_CROP_WIDTH:
        {
            assert(
                load.kind == LOAD_KIND_SPRITE &&
                "CACHE_CROP_WIDTH field must be within a sprite item");
            load._sprite.crop_width = atoi(field->value);
        }
        break;
        case RCFIELD_CACHE_CROP_HEIGHT:
        {
            assert(
                load.kind == LOAD_KIND_SPRITE &&
                "CACHE_CROP_HEIGHT field must be within a sprite item");
            load._sprite.crop_height = atoi(field->value);
        }
        break;
        case RCFIELD_UICOMPONENT_TYPE:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_TYPE field must be within a component item");
            strncpy(load._component.type, field->value, sizeof(load._component.type) - 1);
        }
        break;
        case RCFIELD_UICOMPONENT_SPRITE:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_SPRITE field must be within a component item");
            strncpy(load._component.sprite, field->value, sizeof(load._component.sprite) - 1);
        }
        break;
        case RCFIELD_UICOMPONENT_WIDTH:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_WIDTH field must be within a component item");
            load._component.width = atoi(field->value);
        }
        break;
        case RCFIELD_UICOMPONENT_HEIGHT:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_HEIGHT field must be within a component item");
            load._component.height = atoi(field->value);
        }
        break;
        case RCFIELD_UICOMPONENT_ANCHOR_X:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_ANCHOR_X field must be within a component item");
            load._component.anchor_x = atoi(field->value);
        }
        break;
        case RCFIELD_UICOMPONENT_ANCHOR_Y:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_ANCHOR_Y field must be within a component item");
            load._component.anchor_y = atoi(field->value);
        }
        break;
        case RCFIELD_UICOMPONENT_TABNO:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_TABNO field must be within a component item");
            load._component.tabno = atoi(field->value);
        }
        break;
        case RCFIELD_UICOMPONENT_COMPONENTNO:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_COMPONENTNO field must be within a component item");
            load._component.componentno = atoi(field->value);
        }
        break;
        case RCFIELD_UICOMPONENT_INV:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_INV field must be within a component item");
            strncpy(load._component.inv, field->value, sizeof(load._component.inv) - 1);
            load._component.inv[sizeof(load._component.inv) - 1] = '\0';
        }
        break;
        case RCFIELD_UICOMPONENT_PAINT_LEVELS:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_PAINT_LEVELS field must be within a component item");
            strncpy(
                load._component.paint_levels,
                field->value,
                sizeof(load._component.paint_levels) - 1);
            load._component.paint_levels[sizeof(load._component.paint_levels) - 1] = '\0';
        }
        break;
        case RCFIELD_UICOMPONENT_FONT:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_FONT field must be within a component item");
            strncpy(load._component.font, field->value, sizeof(load._component.font) - 1);
            load._component.font[sizeof(load._component.font) - 1] = '\0';
        }
        break;
        case RCFIELD_UICOMPONENT_CHATBACK_SCREEN_X:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "CHATBACK_SCREEN_X field must be within a component item");
            load._component.chat_geom.chatback_screen_x = atoi(field->value);
            load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_CHATBACK_SCREEN_X;
        }
        break;
        case RCFIELD_UICOMPONENT_CHATBACK_SCREEN_Y:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "CHATBACK_SCREEN_Y field must be within a component item");
            load._component.chat_geom.chatback_screen_y = atoi(field->value);
            load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_CHATBACK_SCREEN_Y;
        }
        break;
        case RCFIELD_UICOMPONENT_CHAT_CLIP_W:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "CHAT_CLIP_W field must be within a component item");
            load._component.chat_geom.clip_w = atoi(field->value);
            load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_CLIP_W;
        }
        break;
        case RCFIELD_UICOMPONENT_CHAT_CLIP_H:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "CHAT_CLIP_H field must be within a component item");
            load._component.chat_geom.clip_h = atoi(field->value);
            load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_CLIP_H;
        }
        break;
        case RCFIELD_UICOMPONENT_CHAT_TEXT_X_LOCAL:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "CHAT_TEXT_X_LOCAL field must be within a component item");
            load._component.chat_geom.text_x_local = atoi(field->value);
            load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_TEXT_X_LOCAL;
        }
        break;
        case RCFIELD_UICOMPONENT_CHAT_SCROLLBAR_X_LOCAL:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "CHAT_SCROLLBAR_X_LOCAL field must be within a component item");
            load._component.chat_geom.scrollbar_x_local = atoi(field->value);
            load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_SCROLLBAR_X_LOCAL;
        }
        break;
        case RCFIELD_UICOMPONENT_CHAT_SEPARATOR_Y_LOCAL:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "CHAT_SEPARATOR_Y_LOCAL field must be within a component item");
            load._component.chat_geom.separator_y_local = atoi(field->value);
            load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_SEPARATOR_Y_LOCAL;
        }
        break;
        case RCFIELD_UICOMPONENT_CHAT_SEPARATOR_W:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "CHAT_SEPARATOR_W field must be within a component item");
            load._component.chat_geom.separator_w = atoi(field->value);
            load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_SEPARATOR_W;
        }
        break;
        case RCFIELD_UICOMPONENT_CHAT_LINE_H:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "CHAT_LINE_H field must be within a component item");
            load._component.chat_geom.line_h = atoi(field->value);
            load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_LINE_H;
        }
        break;
        case RCFIELD_UICOMPONENT_CHAT_INPUT_LINE_Y_LOCAL:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "CHAT_INPUT_LINE_Y_LOCAL field must be within a component item");
            load._component.chat_geom.input_line_y_local = atoi(field->value);
            load._component.chat_geom_mask |= CHAT_LAYOUT_BIT_INPUT_LINE_Y_LOCAL;
        }
        break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_VIEWPORT:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "MINIMENU_REGION_VIEWPORT field must be within a component item");
            strncpy(
                load._component.minimenu_region_viewport,
                field->value,
                sizeof(load._component.minimenu_region_viewport) - 1);
            load._component
                .minimenu_region_viewport[sizeof(load._component.minimenu_region_viewport) - 1] =
                '\0';
        }
        break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_SIDEBAR:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "MINIMENU_REGION_SIDEBAR field must be within a component item");
            strncpy(
                load._component.minimenu_region_sidebar,
                field->value,
                sizeof(load._component.minimenu_region_sidebar) - 1);
            load._component
                .minimenu_region_sidebar[sizeof(load._component.minimenu_region_sidebar) - 1] =
                '\0';
        }
        break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_CHAT:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "MINIMENU_REGION_CHAT field must be within a component item");
            strncpy(
                load._component.minimenu_region_chat,
                field->value,
                sizeof(load._component.minimenu_region_chat) - 1);
            load._component.minimenu_region_chat[sizeof(load._component.minimenu_region_chat) - 1] =
                '\0';
        }
        break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_VIEWPORT_MAX:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "MINIMENU_PLACE_VIEWPORT_MAX field must be within a component item");
            strncpy(
                load._component.minimenu_place_viewport_max,
                field->value,
                sizeof(load._component.minimenu_place_viewport_max) - 1);
            load._component.minimenu_place_viewport_max
                [sizeof(load._component.minimenu_place_viewport_max) - 1] = '\0';
        }
        break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_SIDEBAR_MAX:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "MINIMENU_PLACE_SIDEBAR_MAX field must be within a component item");
            strncpy(
                load._component.minimenu_place_sidebar_max,
                field->value,
                sizeof(load._component.minimenu_place_sidebar_max) - 1);
            load._component.minimenu_place_sidebar_max
                [sizeof(load._component.minimenu_place_sidebar_max) - 1] = '\0';
        }
        break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_CHAT_MAX:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "MINIMENU_PLACE_CHAT_MAX field must be within a component item");
            strncpy(
                load._component.minimenu_place_chat_max,
                field->value,
                sizeof(load._component.minimenu_place_chat_max) - 1);
            load._component
                .minimenu_place_chat_max[sizeof(load._component.minimenu_place_chat_max) - 1] =
                '\0';
        }
        break;
        case RCFIELD_UICOMPONENT_CROSSHAIR_HOTSPOT_OFFSET:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "CROSSHAIR_HOTSPOT_OFFSET field must be within a component item");
            load._component.crosshair_hotspot_offset = atoi(field->value);
            load._component.crosshair_hotspot_offset_set = 1;
        }
        break;
        case RCFIELD_INV_ITEM:
        {
            assert(load.kind == LOAD_KIND_INV && "INV_ITEM field must be within an inv item");
            if( load._inv.item_count < UI_INVENTORY_MAX_ITEMS )
                load._inv.item_ids[load._inv.item_count++] = atoi(field->value);
        }
        break;
        case RCFIELD_UICOMPONENT_SPRITE_ACTIVE:
        {
            assert(
                load.kind == LOAD_KIND_COMPONENT &&
                "UICOMPONENT_SPRITE_ACTIVE field must be within a component item");
            strncpy(
                load._component.sprite_active,
                field->value,
                sizeof(load._component.sprite_active) - 1);
        }
        break;
        case RCFIELD_UILAYOUT_COMPONENT:
        {
            assert(
                load.kind == LOAD_KIND_LAYOUT &&
                "UILAYOUT_COMPONENT field must be within a layout item");
            if( load._layout.entry_count >= MAX_LAYOUT_ENTRIES )
            {
                fprintf(
                    stderr,
                    "uitree_load: layout exceeds MAX_LAYOUT_ENTRIES (%d); ignoring extra entries\n",
                    MAX_LAYOUT_ENTRIES);
                break;
            }
            strncpy(
                load._layout.entries[load._layout.entry_count].component,
                field->value,
                sizeof(load._layout.entries[load._layout.entry_count].component) - 1);
            load._layout.entries[load._layout.entry_count]
                .component[sizeof(load._layout.entries[0].component) - 1] = '\0';
            load._layout.entry_count += 1;
        }
        break;
        case RCFIELD_UILAYOUT_X:
        {
            if( load.kind == LOAD_KIND_COMPONENT )
            {
                load._component.def_x = atoi(field->value);
                break;
            }
            assert(
                load._layout.entry_count > 0 &&
                "UILAYOUT_X field must come after a UILAYOUT_COMPONENT field");
            assert(
                load.kind == LOAD_KIND_LAYOUT && "UILAYOUT_X field must be within a layout item");
            load._layout.entries[load._layout.entry_count - 1].x = atoi(field->value);
        }
        break;
        case RCFIELD_UILAYOUT_Y:
        {
            if( load.kind == LOAD_KIND_COMPONENT )
            {
                load._component.def_y = atoi(field->value);
                break;
            }
            assert(
                load._layout.entry_count > 0 &&
                "UILAYOUT_X field must come after a UILAYOUT_COMPONENT field");
            assert(
                load.kind == LOAD_KIND_LAYOUT && "UILAYOUT_Y field must be within a layout item");
            load._layout.entries[load._layout.entry_count - 1].y = atoi(field->value);
        }
        break;
        case RCFIELD_UILAYOUT_WIDTH:
        {
            assert(
                load._layout.entry_count > 0 &&
                "UILAYOUT_WIDTH field must come after a UILAYOUT_COMPONENT field");
            assert(
                load.kind == LOAD_KIND_LAYOUT &&
                "UILAYOUT_WIDTH field must be within a layout item");
            load._layout.entries[load._layout.entry_count - 1].width = atoi(field->value);
        }
        break;
        case RCFIELD_UILAYOUT_HEIGHT:
        {
            assert(
                load._layout.entry_count > 0 &&
                "UILAYOUT_HEIGHT field must come after a UILAYOUT_COMPONENT field");
            assert(
                load.kind == LOAD_KIND_LAYOUT &&
                "UILAYOUT_HEIGHT field must be within a layout item");
            load._layout.entries[load._layout.entry_count - 1].height = atoi(field->value);
        }
        break;
        case RCFIELD_UILAYOUT_ANCHOR_X:
        {
            assert(
                load._layout.entry_count > 0 &&
                "UILAYOUT_ANCHOR_X field must come after a UILAYOUT_COMPONENT field");
            assert(
                load.kind == LOAD_KIND_LAYOUT &&
                "UILAYOUT_ANCHOR_X field must be within a layout item");
            load._layout.entries[load._layout.entry_count - 1].anchor_x = atoi(field->value);
        }
        break;
        case RCFIELD_UILAYOUT_ANCHOR_Y:
        {
            assert(
                load._layout.entry_count > 0 &&
                "UILAYOUT_ANCHOR_Y field must come after a UILAYOUT_COMPONENT field");
            assert(
                load.kind == LOAD_KIND_LAYOUT &&
                "UILAYOUT_ANCHOR_Y field must be within a layout item");
            load._layout.entries[load._layout.entry_count - 1].anchor_y = atoi(field->value);
        }
        break;
        case RCFIELD_UILAYOUT_TOP:
        {
            assert(
                load._layout.entry_count > 0 &&
                "UILAYOUT_X field must come after a UILAYOUT_COMPONENT field");
            assert(
                load.kind == LOAD_KIND_LAYOUT && "UILAYOUT_TOP field must be within a layout item");
            load._layout.entries[load._layout.entry_count - 1].flags = STATIC_UI_RELATIVE_FLAG_TOP;
            load._layout.entries[load._layout.entry_count - 1].top = atoi(field->value);
        }
        break;
        case RCFIELD_UILAYOUT_LEFT:
        {
            assert(
                load._layout.entry_count > 0 &&
                "UILAYOUT_X field must come after a UILAYOUT_COMPONENT field");
            assert(
                load.kind == LOAD_KIND_LAYOUT &&
                "UILAYOUT_LEFT field must be within a layout item");
            load._layout.entries[load._layout.entry_count - 1].flags = STATIC_UI_RELATIVE_FLAG_LEFT;
            load._layout.entries[load._layout.entry_count - 1].left = atoi(field->value);
        }
        break;
        case RCFIELD_UILAYOUT_BOTTOM:
        {
            assert(
                load._layout.entry_count > 0 &&
                "UILAYOUT_X field must come after a UILAYOUT_COMPONENT field");
            assert(
                load.kind == LOAD_KIND_LAYOUT &&
                "UILAYOUT_BOTTOM field must be within a layout item");
            load._layout.entries[load._layout.entry_count - 1].flags =
                STATIC_UI_RELATIVE_FLAG_BOTTOM;
            load._layout.entries[load._layout.entry_count - 1].bottom = atoi(field->value);
        }
        break;
        case RCFIELD_UILAYOUT_RIGHT:
        {
            assert(
                load._layout.entry_count > 0 &&
                "UILAYOUT_X field must come after a UILAYOUT_COMPONENT field");
            assert(
                load.kind == LOAD_KIND_LAYOUT &&
                "UILAYOUT_RIGHT field must be within a layout item");
            load._layout.entries[load._layout.entry_count - 1].flags =
                STATIC_UI_RELATIVE_FLAG_RIGHT;
            load._layout.entries[load._layout.entry_count - 1].right = atoi(field->value);
            break;
        }
        case RCFIELD_UILAYOUT_DIRTY:
        {
            assert(
                load._layout.entry_count > 0 &&
                "UILAYOUT_DIRTY must come after a UILAYOUT_COMPONENT field");
            assert(
                load.kind == LOAD_KIND_LAYOUT &&
                "UILAYOUT_DIRTY field must be within a layout item");
            const char* v = field->value;
            int truthy = (strcmp(v, "true") == 0) || (strcmp(v, "1") == 0);
            load._layout.entries[load._layout.entry_count - 1].always_dirty = truthy ? 1u : 0u;
        }
        break;
        }
    }

    uitree_resolve_game_uiscene_sprite_ids(game, ui_scene);

    if( ui && uitree_validate_sidebar_tab_layout(ui) != 0 )
        fprintf(stderr, "uitree_load_ui_from_revconfig: sidebar tab layout invalid\n");

    if( ui )
    {
        uitree_print_nodes(ui);
        uitree_load_debug_log_subtree_watch_id(ui);
    }

    dashmap_free(sprite_hmap);
    dashmap_free(component_hmap);
    free(sprite_config.buffer);
    free(component_config.buffer);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * uitree_impl_* — non-static wrappers used inside uitree_load.c; uitree_loader.c
 * calls the thin uitree_load_* bridge declared in uitree_load_bridge.h.
 * ───────────────────────────────────────────────────────────────────────────── */

uint32_t
uitree_impl_load_kind(const char* str)
{
    return load_kind(str);
}

void
uitree_impl_on_itemname(
    struct CurrentLoad* load,
    const char* value)
{
    on_itemname(load, value);
}

void
uitree_impl_resolve_game_uiscene_sprite_ids(
    struct GGame* game,
    struct UIScene* ui_scene)
{
    uitree_resolve_game_uiscene_sprite_ids(game, ui_scene);
}

int
uitree_impl_load_sprite(
    struct SpriteLoad* load,
    struct DashMap* sprite_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct BuildCacheDat* buildcachedat,
    struct UITreeLoaderAssetRequest* out_req)
{
    if( !buildcachedat || !buildcachedat->cfg_media_jagfile )
    {
        if( out_req )
        {
            out_req->kind = UITREE_ASSET_SPRITE;
            strncpy(out_req->u.sprite.name, load->name, sizeof(out_req->u.sprite.name) - 1);
            out_req->u.sprite.name[sizeof(out_req->u.sprite.name) - 1] = '\0';
        }
        return -1;
    }
    load_sprite(load, sprite_hmap, ui, ui_scene, buildcachedat);
    return 0;
}

int
uitree_impl_load_component(
    struct ComponentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* gamecache,
    struct UITreeLoaderAssetRequest* out_req)
{
    (void)out_req;
    load_component(load, sprite_hmap, component_hmap, ui, ui_scene, gamecache);
    return 0;
}

int
uitree_impl_load_layout(
    struct LayoutLoad* load,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* gamecache,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct UITreeLoaderAssetRequest* out_req)
{
    /* Before expanding RS subtrees, verify that the required gamecache components
     * are available for any sidebar/overlay entries.  If any are missing, pause
     * so the caller can load the interface archive first. */
    if( gamecache )
    {
        int missing_component_ids[UITREE_MAX_INTERFACE_REQUESTS];
        int missing_count = 0;

        for( int i = 0; i < load->entry_count; i++ )
        {
            struct ComponentEntry* ce =
                dashmap_search(component_hmap, load->entries[i].component, DASHMAP_FIND);
            if( !ce )
                continue;
            if( (ce->type == UIELEM_BUILTIN_SIDEBAR || ce->type == UIELEM_BUILTIN_CHAT_DIALOG ||
                 ce->type == UIELEM_BUILTIN_SIDEBAR_OVERLAY ||
                 ce->type == UIELEM_BUILTIN_VIEWPORT_OVERLAY) &&
                ce->componentno > 0 )
            {
                struct GameCacheComponent* root =
                    gamecache_get_component(gamecache, ce->componentno);
                if( !root )
                {
                    int cid = ce->componentno;
                    int dup = 0;
                    for( int j = 0; j < missing_count; j++ )
                    {
                        if( missing_component_ids[j] == cid )
                        {
                            dup = 1;
                            break;
                        }
                    }
                    if( !dup && missing_count < UITREE_MAX_INTERFACE_REQUESTS )
                        missing_component_ids[missing_count++] = cid;
                }
            }
        }

        if( missing_count > 0 )
        {
            if( out_req )
            {
                out_req->kind = UITREE_ASSET_INTERFACE;
                memcpy(
                    out_req->u.interface_file.component_ids,
                    missing_component_ids,
                    (size_t)missing_count * sizeof(int));
                out_req->u.interface_file.count = missing_count;
            }
            return -1;
        }
    }
    return load_layout(load, component_hmap, ui, ui_scene, gamecache, inv_pool, game, out_req);
}

int
uitree_impl_load_inv(
    struct InvLoad* il,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct UIScene* ui_scene,
    struct UITreeLoaderAssetRequest* out_req)
{
    (void)out_req;
    load_inv(il, inv_pool, game, ui_scene);
    return 0;
}

int
uitree_impl_load_item(
    struct CurrentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct UIInventoryPool* inv_pool,
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct UITreeLoaderAssetRequest* out_req)
{
    switch( load->kind )
    {
    case LOAD_KIND_SPRITE:
        return uitree_impl_load_sprite(
            &load->_sprite, sprite_hmap, ui, ui_scene, buildcachedat, out_req);
    case LOAD_KIND_COMPONENT:
        return uitree_impl_load_component(
            &load->_component, sprite_hmap, component_hmap, ui, ui_scene, game->gamecache, out_req);
    case LOAD_KIND_LAYOUT:
        return uitree_impl_load_layout(
            &load->_layout, component_hmap, ui, ui_scene, game->gamecache, inv_pool, game, out_req);
    case LOAD_KIND_INV:
        return uitree_impl_load_inv(&load->_inv, inv_pool, game, ui_scene, out_req);
    default:
        return 0;
    }
}

/* ── uitree_load_bridge.h — implementations for uitree_loader.c ───────────── */

uint32_t
uitree_load_parse_item_kind(const char* str)
{
    return uitree_impl_load_kind(str);
}

void
uitree_load_bind_item_name(
    struct CurrentLoad* load,
    const char* value)
{
    uitree_impl_on_itemname(load, value);
}

void
uitree_load_finalize_uiscene_ids(
    struct GGame* game,
    struct UIScene* ui_scene)
{
    uitree_impl_resolve_game_uiscene_sprite_ids(game, ui_scene);
}

int
uitree_load_commit_revconfig_item(
    struct CurrentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct UIInventoryPool* inv_pool,
    struct BuildCacheDat* buildcachedat,
    struct GGame* game,
    struct UITreeLoaderAssetRequest* out_req)
{
    return uitree_impl_load_item(
        load, sprite_hmap, component_hmap, ui, ui_scene, inv_pool, buildcachedat, game, out_req);
}
