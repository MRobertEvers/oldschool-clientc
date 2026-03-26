#include "static_ui_load.h"

#include "bmp.h"
#include "graphics/dash.h"
#include "osrs/dash_utils.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "osrs/rscache/tables_dat/pixfont.h"

#include <assert.h>
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
    int id;
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
};

struct ComponentLoad
{
    char name[64];
    char type[64];
    char sprite[64];
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
    LOAD_KIND_LAYOUT
};

struct CurrentLoad
{
    enum LoadKind kind;
    union
    {
        struct SpriteLoad _sprite;
        struct ComponentLoad _component;
        struct LayoutLoad _layout;
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
load_sprite(
    struct SpriteLoad* load,
    struct DashMap* sprite_hmap,
    struct StaticUIBuffer* ui,
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

    printf(
        "Loading sprite: name='%s', format='%s', data_file='%s', index_file='%s', atlas_index=%d, "
        "atlas_count=%d\n",
        load->name,
        load->format,
        load->data_filename,
        load->index_filename,
        load->atlas_index,
        load->atlas_count);

    for( int i = 0; i < count; i++ )
    {
        int atlas_index = start_atlas_index + i;
        if( strcmp(load->format, "pix8") == 0 )
        {
            sprites[atlas_index] =
                load_sprite_pix8(filelist, data_file_idx, index_file_idx, atlas_index);
        }
        else if( strcmp(load->format, "pix32") == 0 )
        {
            sprites[atlas_index] =
                load_sprite_pix32(filelist, data_file_idx, index_file_idx, atlas_index);
        }
        else
        {
            assert(0 && "Unknown sprite format");
        }

        if( !sprites[atlas_index] )
        {
            // Ignore failed loads?
            continue;
        }

        for( int i = 0; i < 5; i++ )
        {
            if( load->transforms[i][0] != '\0' )
            {
                if( strcmp(load->transforms[i], "flip_h") == 0 )
                    dashsprite_flip_horizontal(sprites[atlas_index]);
                else if( strcmp(load->transforms[i], "flip_v") == 0 )
                    dashsprite_flip_vertical(sprites[atlas_index]);
                else
                    assert(0 && "Unknown transform");
            }
        }

        char filename[128] = { 0 };
        snprintf(filename, sizeof(filename), "build/%s%d.bmp", load->name, atlas_index);
        bmp_write_file(
            filename,
            sprites[atlas_index]->pixels_argb,
            sprites[atlas_index]->width,
            sprites[atlas_index]->height);
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
    printf(
        "Loaded sprite '%s' with element id %d and atlas count %d\n",
        load->name,
        element_id,
        count);
};

static enum StaticUIComponentType
component_type_from_string(const char* str)
{
    if( strcmp(str, "compass") == 0 )
        return UIELEM_COMPASS;
    else if( strcmp(str, "minimap") == 0 )
        return UIELEM_MINIMAP;
    else if( strcmp(str, "world") == 0 )
        return UIELEM_WORLD;
    else if( strcmp(str, "builtin_sidebar") == 0 )
        return UIELEM_BUILTIN_SIDEBAR;
    else if( strcmp(str, "builtin_chat") == 0 )
        return UIELEM_BUILTIN_CHAT;
    else if( strcmp(str, "builtin_viewport") == 0 )
        return UIELEM_BUILTIN_VIEWPORT;
    else if( strcmp(str, "sprite") == 0 )
        return UIELEM_SPRITE;
    else if( strcmp(str, "tab_redstones") == 0 )
        return UIELEM_TAB_REDSTONES;
    else if( strcmp(str, "builtin_tab_icons") == 0 )
        return UIELEM_BUILTIN_TAB_ICONS;
    else if( strcmp(str, "chat_modes") == 0 )
        return UIELEM_CHAT_MODES;
    else if( strcmp(str, "chat_input") == 0 )
        return UIELEM_CHAT_INPUT;
    else if( strcmp(str, "chat_history") == 0 )
        return UIELEM_CHAT_HISTORY;
    else if( strcmp(str, "redstone") == 0 )
        return UIELEM_TAB_REDSTONES; // For testing unknown types

    assert(0 && "Unknown component type");
    return 0;
}

static void
load_component(
    struct ComponentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct StaticUIBuffer* ui,
    struct UIScene* ui_scene,
    struct BuildCacheDat* buildcachedat)
{
    enum StaticUIComponentType type = component_type_from_string(load->type);
    struct ComponentEntry* component_entry = NULL;

    component_entry = dashmap_search(component_hmap, load->name, DASHMAP_INSERT);
    printf("Loading component: name='%s', type='%s' (kind=%d)\n", load->name, load->type, type);
    memset(component_entry, 0, sizeof(struct ComponentEntry));
    struct SpriteEntry* sprite_entry = NULL;

    assert(component_entry && "Component must be inserted into hmap");
    component_entry->type = type;
    component_entry->sprite_id = -1;
    component_entry->sprite_index = 0;

    strncpy(component_entry->name, load->name, sizeof(component_entry->name) - 1);

    switch( type )
    {
    case UIELEM_COMPASS:
    {
    }
    break;
    case UIELEM_MINIMAP:
    {
    }
    break;
    case UIELEM_WORLD:
        break;
    case UIELEM_SPRITE:
    {
        if( load->sprite[0] == '\0' )
        {
            break;
        }
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
            return;
        }
        assert(sprite_entry && "Sprite for component not found in hmap");
        printf(
            "Loading sprite component '%s' with sprite '%s' and atlas index %d\n",
            load->name,
            load->sprite,
            sprite_index);

        component_entry->sprite_id = sprite_entry->id;
        component_entry->sprite_index = sprite_index;
    }
    break;
    case UIELEM_TAB_REDSTONES:
    }
}

static void
load_layout(
    struct LayoutLoad* load,
    struct DashMap* component_hmap,
    struct StaticUIBuffer* ui,
    struct UIScene* ui_scene,
    struct BuildCacheDat* buildcachedat)
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
        case UIELEM_COMPASS:
        {
            static_ui_buffer_push_compass(
                ui, component_entry->sprite_id, layout_entry->x, layout_entry->y);
        }
        break;
        case UIELEM_MINIMAP:
        {
            static_ui_buffer_push_minimap(ui, layout_entry->x, layout_entry->y);
        }
        break;
        case UIELEM_WORLD:
        {
            static_ui_buffer_push_world(ui, layout_entry->x, layout_entry->y);
        }
        break;
        case UIELEM_TAB_REDSTONES:
        case UIELEM_BUILTIN_SIDEBAR:
        case UIELEM_BUILTIN_CHAT:
        case UIELEM_BUILTIN_VIEWPORT:
        case UIELEM_BUILTIN_TAB_ICONS:
        case UIELEM_CHAT_MODES:
        case UIELEM_CHAT_INPUT:
        case UIELEM_CHAT_HISTORY:
            break;
        case UIELEM_SPRITE:
        {
            if( layout_entry->flags != 0 )
            {
                static_ui_buffer_push_sprite_relative(
                    ui,
                    component_entry->sprite_id,
                    component_entry->sprite_index,
                    layout_entry->flags,
                    layout_entry->top,
                    layout_entry->right,
                    layout_entry->bottom,
                    layout_entry->left);
            }
            else
            {
                static_ui_buffer_push_sprite_xy(
                    ui,
                    component_entry->sprite_id,
                    component_entry->sprite_index,
                    layout_entry->x,
                    layout_entry->y);
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
    return LOAD_KIND_NONE;
}

static void
load_item(
    struct CurrentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct StaticUIBuffer* ui,
    struct UIScene* ui_scene,
    struct BuildCacheDat* buildcachedat)
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
        load_layout(&load->_layout, component_hmap, ui, ui_scene, buildcachedat);
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
    }
}

void
static_ui_from_revconfig_buildcachedat(
    struct StaticUIBuffer* ui,
    struct UIScene* ui_scene,
    struct BuildCacheDat* buildcachedat,
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
        printf("Processing field: kind=%d, value='%s'\n", field->kind, field->value);
        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            load.kind = load_kind(field->value);
            printf("Loading item of kind: %s\n", field->value);
            break;
        case RCFIELD_ITEMNAME:
            on_itemname(&load, field->value);
            break;
        case RCFIELD_ITEMDONE:
            load_item(&load, sprite_hmap, component_hmap, ui, ui_scene, buildcachedat);
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