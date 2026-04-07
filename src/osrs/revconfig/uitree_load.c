#include "uitree_load.h"

#include "bmp.h"
#include "graphics/dash.h"
#include "graphics/dashmap.h"
#include "osrs/dash_utils.h"
#include "osrs/entity_scenebuild.h"
#include "osrs/game.h"
#include "osrs/obj_icon.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "osrs/rscache/tables_dat/pixfont.h"
#include "osrs/scene2.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SpriteEntry
{
    char name[64]; // Key must be first field and fixed size for DashMap
    struct DashSprite** sprites;
    int count;
    int id;
};

struct ComponentEntry
{
    char name[64]; // Key must be first field and fixed size for DashMap
    enum StaticUIComponentType type;
    int sprite_id;
    int sprite_index;
    int sprite_id_active;
    int sprite_index_active;
    int id;
    int width;
    int height;
    int anchor_x;
    int anchor_y;
    int tabno;
    int componentno;
    char inv[64];
};

enum SpriteLoad_AtlasMode
{
    SPRITELOAD_ATLAS_MODE_INDEX,
    SPRITELOAD_ATLAS_MODE_COUNT,
};

struct SpriteLoad
{
    char name[64];
    char table[64];
    char archive[64];
    char container[64];
    char index_filename[64];
    char data_filename[64];
    char format[16];

    enum SpriteLoad_AtlasMode atlas_mode;
    int atlas_index;
    int atlas_count;

    char transforms[5][32];
    int transform_count;

    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
};

struct ComponentLoad
{
    char name[64];
    char type[64];
    char sprite[64];
    char sprite_active[64];
    int width;
    int height;
    int anchor_x;
    int anchor_y;
    int tabno;
    int componentno;
    char inv[64];
};

#define MAX_LAYOUT_ENTRIES 64

struct LayoutItem
{
    char component[64];
    int x;
    int y;
    int flags;
    int top;
    int right;
    int bottom;
    int left;
    int width;
    int height;
    int anchor_x;
    int anchor_y;
};

struct LayoutLoad
{
    char name[64];
    struct LayoutItem entries[MAX_LAYOUT_ENTRIES];
    int entry_count;
};

enum LoadKind
{
    LOAD_KIND_NONE,
    LOAD_KIND_SPRITE,
    LOAD_KIND_COMPONENT,
    LOAD_KIND_LAYOUT,
    LOAD_KIND_INV
};

struct InvLoad
{
    char name[64];
    int item_ids[UI_INVENTORY_MAX_ITEMS];
    int item_count;
};

struct CurrentLoad
{
    enum LoadKind kind;
    union
    {
        struct SpriteLoad _sprite;
        struct ComponentLoad _component;
        struct LayoutLoad _layout;
        struct InvLoad _inv;
    };
};

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
    struct BuildCacheDat* buildcachedat)
{
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

    for( int i = 0; i < il->item_count && i < UI_INVENTORY_MAX_ITEMS; i++ )
    {
        int obj_id = il->item_ids[i];
        inv.items[inv.item_count].obj_id = obj_id;
        inv.items[inv.item_count].scene_id = -1;
        inv.items[inv.item_count].atlas_index = 0;

        if( game && ui_scene )
        {
            /* INI item= uses same 1-based ids as interface inv slots (see interface_draw). */
            int obj_lookup = obj_id;
            struct DashSprite* sp = obj_icon_get(game, obj_lookup, 1);
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
                            el->dash_sprites_borrowed = false;
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
                {
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
    struct BuildCacheDat* bcd,
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
    struct DashPixFont* f = buildcachedat_get_font(bcd, nm);
    if( !f )
        return -1;
    return uiscene_font_add(ui_scene, nm, f);
}

/** Attach `count` sprites to a new UIScene element; `borrowed` if owned by BuildCacheDat. */
static int
uiscene_attach_sprite_row(
    struct UIScene* ui_scene,
    struct DashSprite** row,
    int count,
    bool borrowed)
{
    int eid = uiscene_element_acquire(ui_scene, -1);
    struct UISceneElement* el = uiscene_element_at(ui_scene, eid);
    if( !el )
        return -1;
    el->dash_sprites = row;
    el->dash_sprites_count = count;
    el->dash_sprites_borrowed = borrowed;
    return eid;
}

static void
push_rs_from_cache_component(
    struct GGame* game,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct Scene2* scene2,
    struct BuildCacheDat* bcd,
    int32_t parent_uitree_idx,
    struct CacheDatConfigComponent* comp,
    int abs_x,
    int abs_y,
    int sidebar_inv_index)
{
    if( !comp || !bcd || !ui || !ui_scene )
        return;

    switch( comp->type )
    {
    case COMPONENT_TYPE_LAYER:
    {
        int32_t lid = uitree_push_rs_layer(
            ui, parent_uitree_idx, comp->id, abs_x, abs_y, comp->width, comp->height);
        if( !comp->children || !comp->childX || !comp->childY )
            return;
        for( int i = 0; i < comp->children_count; i++ )
        {
            struct CacheDatConfigComponent* ch =
                buildcachedat_get_component(bcd, comp->children[i]);
            if( !ch )
                continue;
            int cx = abs_x + comp->childX[i] + ch->x;
            int cy = abs_y + comp->childY[i] + ch->y;
            push_rs_from_cache_component(
                game, ui, ui_scene, scene2, bcd, lid, ch, cx, cy, sidebar_inv_index);
        }
    }
    break;
    case COMPONENT_TYPE_GRAPHIC:
    {
        struct DashSprite* g0 = (comp->graphic && comp->graphic[0] != '\0')
                                    ? buildcachedat_get_component_sprite(bcd, comp->graphic)
                                    : NULL;
        struct DashSprite* g1 = (comp->activeGraphic && comp->activeGraphic[0] != '\0')
                                    ? buildcachedat_get_component_sprite(bcd, comp->activeGraphic)
                                    : NULL;
        int count = 0;
        if( g0 )
            count = 1;
        if( g1 && g1 != g0 )
            count = 2;
        if( count == 0 )
            return;
        struct DashSprite** row = malloc((size_t)count * sizeof(struct DashSprite*));
        if( !row )
            return;
        row[0] = g0 ? g0 : g1;
        if( count == 2 )
            row[1] = g1;
        int sid = uiscene_attach_sprite_row(ui_scene, row, count, true);
        if( sid < 0 )
        {
            free(row);
            return;
        }
        int sid_a = -1;
        int atlas_a = 0;
        if( count >= 2 )
        {
            sid_a = sid;
            atlas_a = 1;
        }
        uitree_push_rs_graphic(
            ui,
            parent_uitree_idx,
            comp->id,
            sid,
            0,
            sid_a,
            atlas_a,
            abs_x,
            abs_y,
            comp->width,
            comp->height);
    }
    break;
    case COMPONENT_TYPE_TEXT:
    {
        int fid = ensure_font_id(ui_scene, bcd, comp->font);
        char const* tx = comp->text;
        uitree_push_rs_text(
            ui,
            parent_uitree_idx,
            comp->id,
            fid,
            comp->colour,
            comp->center ? 1 : 0,
            comp->shadowed ? 1 : 0,
            tx,
            abs_x,
            abs_y,
            comp->width,
            comp->height);
    }
    break;
    case COMPONENT_TYPE_INV:
    {
        uitree_push_rs_inv(
            ui,
            parent_uitree_idx,
            comp->id,
            sidebar_inv_index,
            comp->width,
            comp->height,
            comp->marginX,
            comp->marginY,
            comp->invSlotOffsetX,
            comp->invSlotOffsetY,
            abs_x,
            abs_y,
            comp->width,
            comp->height);
    }
    break;
    case COMPONENT_TYPE_MODEL:
    {
        if( !game || !scene2 || (comp->modelType != 2 && comp->modelType != 3) )
            return;
        int* slots = NULL;
        int* colors = NULL;
        if( comp->modelType == 3 && game->world )
        {
            struct PlayerEntity* local = &game->world->players[ACTIVE_PLAYER_SLOT];
            if( local->alive )
            {
                slots = local->appearance.slots;
                colors = local->appearance.colors;
            }
        }
        struct DashModel* m = entity_scenebuild_head_model_for_component(
            game, comp->modelType, comp->model, slots, colors);
        if( !m )
            return;
        int eid = scene2_element_acquire(scene2, -1);
        struct Scene2Element* se = scene2_element_at(scene2, eid);
        if( !se )
        {
            dashmodel_free(m);
            return;
        }
        se->dash_position = dashposition_new();
        if( !se->dash_position )
        {
            dashmodel_free(m);
            return;
        }
        memset(se->dash_position, 0, sizeof(struct DashPosition));
        scene2_element_set_dash_model(se, m);
        uitree_push_rs_model(
            ui, parent_uitree_idx, comp->id, eid, abs_x, abs_y, comp->width, comp->height);
    }
    break;
    default:
        break;
    }
}

static void
expand_sidebar_rs_tree(
    struct GGame* game,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct Scene2* scene2,
    struct BuildCacheDat* bcd,
    int32_t sidebar_idx,
    int component_no,
    int inv_index)
{
    if( !bcd || component_no < 0 || sidebar_idx < 0 ||
        (uint32_t)sidebar_idx >= ui->component_count )
        return;
    struct CacheDatConfigComponent* root = buildcachedat_get_component(bcd, component_no);
    if( !root )
        return;
    int sx = ui->components[sidebar_idx].position.x;
    int sy = ui->components[sidebar_idx].position.y;
    int bx = sx + root->x;
    int by = sy + root->y;
    push_rs_from_cache_component(
        game, ui, ui_scene, scene2, bcd, sidebar_idx, root, bx, by, inv_index);
}

static void
load_layout(
    struct LayoutLoad* load,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct Scene2* scene2,
    struct BuildCacheDat* buildcachedat,
    struct UIInventoryPool* inv_pool,
    struct GGame* game)
{
    struct LayoutItem* layout_entry = NULL;
    struct ComponentEntry* component_entry = NULL;

    for( int i = 0; i < load->entry_count; i++ )
    {
        layout_entry = &load->entries[i];
        component_entry = dashmap_search(component_hmap, layout_entry->component, DASHMAP_FIND);
        assert(component_entry && "Component for layout entry not found in hmap");
        switch( component_entry->type )
        {
        case UIELEM_BUILTIN_COMPASS:
        {
            uitree_push_compass(
                ui,
                -1,
                component_entry->sprite_id,
                component_entry->sprite_index,
                layout_entry->x,
                layout_entry->y,
                component_entry->width,
                component_entry->height,
                component_entry->anchor_x,
                component_entry->anchor_y);
        }
        break;
        case UIELEM_BUILTIN_MINIMAP:
        {
            uitree_push_minimap(
                ui,
                -1,
                layout_entry->x,
                layout_entry->y,
                component_entry->width,
                component_entry->height,
                component_entry->anchor_x,
                component_entry->anchor_y);
        }
        break;
        case UIELEM_BUILTIN_WORLD: // "world"
        {
            uitree_push_world(ui, -1, layout_entry->x, layout_entry->y);
        }
        break;
        case UIELEM_BUILTIN_REDSTONE_TAB:
        {
            uitree_push_redstone_tab(
                ui,
                -1,
                component_entry->tabno,
                component_entry->sprite_id,
                component_entry->sprite_index,
                component_entry->sprite_id_active,
                component_entry->sprite_index_active,
                layout_entry->x,
                layout_entry->y,
                component_entry->width,
                component_entry->height);
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
                layout_entry->x,
                layout_entry->y,
                component_entry->width,
                component_entry->height);
            expand_sidebar_rs_tree(
                game,
                ui,
                ui_scene,
                scene2,
                buildcachedat,
                sidx,
                component_entry->componentno,
                inv_index);
        }
        break;
        case UIELEM_BUILTIN_CHAT:
        case UIELEM_BUILTIN_TAB_ICONS: // "builtin_tab_icons"

        case UIELEM_BUILTIN_SPRITE:
        {
            if( layout_entry->flags != 0 )
            {
                uitree_push_sprite_relative(
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
                uitree_push_sprite_xy(
                    ui,
                    -1,
                    component_entry->sprite_id,
                    component_entry->sprite_index,
                    layout_entry->x,
                    layout_entry->y,
                    component_entry->width,
                    component_entry->height);
            }
        }
        break;
        default:
            assert(0 && "Unknown component type in layout");
            break;
        }
    }
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
    struct Scene2* scene2,
    struct BuildCacheDat* buildcachedat,
    struct UIInventoryPool* inv_pool,
    struct GGame* game)
{
    switch( load->kind )
    {
    case LOAD_KIND_SPRITE:
        load_sprite(&load->_sprite, sprite_hmap, ui, ui_scene, buildcachedat);
        break;
    case LOAD_KIND_COMPONENT:
        load_component(&load->_component, sprite_hmap, component_hmap, ui, ui_scene, buildcachedat);
        break;
    case LOAD_KIND_LAYOUT:
        load_layout(
            &load->_layout, component_hmap, ui, ui_scene, scene2, buildcachedat, inv_pool, game);
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

void
uitree_from_revconfig_buildcachedat(
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct Scene2* scene2,
    struct BuildCacheDat* buildcachedat,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct RevConfigBuffer* revconfig_buffer)
{
    struct CurrentLoad load = { 0 };

    if( ui )
    {
        ui->component_count = 0;
        ui->root_index = -1;
    }

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
            load_item(
                &load,
                sprite_hmap,
                component_hmap,
                ui,
                ui_scene,
                scene2,
                buildcachedat,
                inv_pool,
                game);
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
            strncpy(
                load._layout.entries[load._layout.entry_count].component,
                field->value,
                sizeof(load._layout.entries[load._layout.entry_count].component) - 1);
            load._layout.entry_count += 1;
        }
        break;
        case RCFIELD_UILAYOUT_X:
        {
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
        }
    }

    dashmap_free(sprite_hmap);
    dashmap_free(component_hmap);
    free(sprite_config.buffer);
    free(component_config.buffer);
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

void
uitree_load_ui_from_revconfig(
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct Scene2* scene2,
    struct BuildCacheDat* buildcachedat,
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
                load_item(
                    &load,
                    sprite_hmap,
                    component_hmap,
                    ui,
                    ui_scene,
                    scene2,
                    buildcachedat,
                    inv_pool,
                    game);
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
            strncpy(
                load._layout.entries[load._layout.entry_count].component,
                field->value,
                sizeof(load._layout.entries[load._layout.entry_count].component) - 1);
            load._layout.entry_count += 1;
        }
        break;
        case RCFIELD_UILAYOUT_X:
        {
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
        }
    }

    dashmap_free(sprite_hmap);
    dashmap_free(component_hmap);
    free(sprite_config.buffer);
    free(component_config.buffer);
}
