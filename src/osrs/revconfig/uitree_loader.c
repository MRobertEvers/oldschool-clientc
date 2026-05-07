#include "uitree_loader.h"

#include "graphics/dashmap.h"
#include "osrs/chat.h"
#include "osrs/revconfig/revconfig.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal (full) definition of UITreeLoader ─────────────────────────────── */

#define MAX_LAYOUT_ENTRIES 128

enum SpriteLoad_AtlasMode
{
    SPRITELOAD_ATLAS_MODE_INDEX,
    SPRITELOAD_ATLAS_MODE_COUNT,
};

enum LoadKind
{
    LOAD_KIND_NONE,
    LOAD_KIND_SPRITE,
    LOAD_KIND_COMPONENT,
    LOAD_KIND_LAYOUT,
    LOAD_KIND_INV
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
    int def_x;
    int def_y;
    int width;
    int height;
    int anchor_x;
    int anchor_y;
    int tabno;
    int componentno;
    char inv[64];
    char paint_levels[64];
    char font[64];
    struct ChatUILayout chat_geom;
    uint16_t chat_geom_mask;
    char minimenu_region_viewport[64];
    char minimenu_region_sidebar[64];
    char minimenu_region_chat[64];
    char minimenu_place_viewport_max[64];
    char minimenu_place_sidebar_max[64];
    char minimenu_place_chat_max[64];
    int crosshair_hotspot_offset;
    uint8_t crosshair_hotspot_offset_set;
};

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
    uint8_t always_dirty;
};

struct LayoutLoad
{
    char name[64];
    struct LayoutItem entries[MAX_LAYOUT_ENTRIES];
    int entry_count;
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
        struct SpriteLoad sprite;
        struct ComponentLoad component;
        struct LayoutLoad layout;
        struct InvLoad inv;
    } u;
};

uint32_t
uitree_load_parse_item_kind(const char* str);
void
uitree_load_bind_item_name(
    struct CurrentLoad* load,
    const char* value);

struct UITreeLoader
{
    enum UITreeLoaderStatus status;
    struct UITreeLoaderAssetRequest pending_assets[UITREE_LOADER_MAX_PENDING_ASSETS];
    int pending_asset_count;

    /* Borrowed references — not owned, must outlive the loader. */
    struct UITree* ui;
    struct RevConfigBuffer* revconfig_buffer;

    /* Iteration state. */
    uint32_t field_index;    /* next field to process */
    struct CurrentLoad load; /* accumulator for the current INI item */
};

/* ── Allocation ─────────────────────────────────────────────────────────────── */

struct UITreeLoader*
uitree_loader_new(
    struct UITree* ui,
    struct RevConfigBuffer* revconfig_buffer)
{
    struct UITreeLoader* loader = calloc(1, sizeof(*loader));
    if( !loader )
        return NULL;

    loader->status = UITREE_LOADER_RUNNING;
    loader->ui = ui;
    loader->revconfig_buffer = revconfig_buffer;
    loader->field_index = 0;

    /* Reset the UITree to empty before starting. */
    if( ui )
    {
        ui->component_count = 0;
        ui->root_index = -1;
    }

    return loader;
}

/* ── Step ───────────────────────────────────────────────────────────────────── */

static enum UITreeLoaderStatus
uitree_loader_on_itemdone(struct UITreeLoader* loader)
{
    if( !loader )
        return UITREE_LOADER_ERROR;

    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
    case LOAD_KIND_COMPONENT:
    case LOAD_KIND_LAYOUT:
    case LOAD_KIND_INV:
        return UITREE_LOADER_NEEDS_ASSET;
    default:
        return UITREE_LOADER_ERROR;
    }
}

/* ── RevConfig field handlers (uitree_loader_step) ─────────────────────────── */

static struct RevConfigField*
uitree_loader_current_field(struct UITreeLoader* loader)
{
    return &loader->revconfig_buffer->fields[loader->field_index];
}

static void
on_rcfield_itemtype(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    uint32_t k = uitree_load_parse_item_kind(field->value);

    loader->load.kind = (enum LoadKind)k;
    if( loader->load.kind == LOAD_KIND_INV )
        memset(&loader->load.u.inv, 0, sizeof(loader->load.u.inv));
    loader->field_index++;
}

static void
on_rcfield_itemname(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);

    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(loader->load.u.sprite.name, field->value, sizeof(loader->load.u.sprite.name) - 1);
        break;
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.name, field->value, sizeof(loader->load.u.component.name) - 1);
        break;
    case LOAD_KIND_LAYOUT:
        strncpy(loader->load.u.layout.name, field->value, sizeof(loader->load.u.layout.name) - 1);
        break;
    case LOAD_KIND_INV:
        strncpy(loader->load.u.inv.name, field->value, sizeof(loader->load.u.inv.name) - 1);
        break;
    default:
        break;
    }

    loader->field_index++;
}

static enum UITreeLoaderStatus
on_rcfield_itemdone(struct UITreeLoader* loader)
{
    enum UITreeLoaderStatus status = uitree_loader_on_itemdone(loader);
    if( status != UITREE_LOADER_RUNNING )
        return status;

    loader->pending_asset_count = 0;
    loader->load.kind = LOAD_KIND_NONE;
    memset(&loader->load, 0, sizeof(loader->load));
    loader->field_index++;

    loader->status = UITREE_LOADER_RUNNING;
    return UITREE_LOADER_RUNNING;
}

static void
on_rcfield_cache_table(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(loader->load.u.sprite.table, field->value, sizeof(loader->load.u.sprite.table) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_archive(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(
            loader->load.u.sprite.archive, field->value, sizeof(loader->load.u.sprite.archive) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_container(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(
            loader->load.u.sprite.container,
            field->value,
            sizeof(loader->load.u.sprite.container) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_index_filename(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(
            loader->load.u.sprite.index_filename,
            field->value,
            sizeof(loader->load.u.sprite.index_filename) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_data_filename(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(
            loader->load.u.sprite.data_filename,
            field->value,
            sizeof(loader->load.u.sprite.data_filename) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_format(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        strncpy(
            loader->load.u.sprite.format, field->value, sizeof(loader->load.u.sprite.format) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_atlas_index(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.atlas_mode = SPRITELOAD_ATLAS_MODE_INDEX;
        loader->load.u.sprite.atlas_index = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_atlas_count(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.atlas_mode = SPRITELOAD_ATLAS_MODE_COUNT;
        loader->load.u.sprite.atlas_count = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_transform(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        if( loader->load.u.sprite.transform_count < 5 )
        {
            strncpy(
                loader->load.u.sprite.transforms[loader->load.u.sprite.transform_count],
                field->value,
                sizeof(loader->load.u.sprite.transforms[0]) - 1);
            loader->load.u.sprite.transform_count++;
        }
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_crop_x(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.crop_x = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_crop_y(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.crop_y = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_crop_width(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.crop_width = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_cache_crop_height(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_SPRITE:
        loader->load.u.sprite.crop_height = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_type(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.type, field->value, sizeof(loader->load.u.component.type) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_sprite(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.sprite,
            field->value,
            sizeof(loader->load.u.component.sprite) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_sprite_active(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.sprite_active,
            field->value,
            sizeof(loader->load.u.component.sprite_active) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_width(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.width = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_height(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.height = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_anchor_x(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.anchor_x = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_anchor_y(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.anchor_y = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_tabno(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.tabno = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_componentno(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.componentno = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_inv(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.inv, field->value, sizeof(loader->load.u.component.inv) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_paint_levels(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.paint_levels,
            field->value,
            sizeof(loader->load.u.component.paint_levels) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_font(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.font, field->value, sizeof(loader->load.u.component.font) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_chatback_screen_x(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.chatback_screen_x = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_CHATBACK_SCREEN_X;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_chatback_screen_y(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.chatback_screen_y = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_CHATBACK_SCREEN_Y;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_chat_clip_w(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.clip_w = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_CLIP_W;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_chat_clip_h(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.clip_h = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_CLIP_H;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_chat_text_x_local(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.text_x_local = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_TEXT_X_LOCAL;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_chat_scrollbar_x_local(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.scrollbar_x_local = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_SCROLLBAR_X_LOCAL;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_chat_separator_y_local(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.separator_y_local = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_SEPARATOR_Y_LOCAL;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_chat_separator_w(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.separator_w = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_SEPARATOR_W;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_chat_line_h(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.line_h = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_LINE_H;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_chat_input_line_y_local(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.chat_geom.input_line_y_local = atoi(field->value);
        loader->load.u.component.chat_geom_mask |= CHAT_LAYOUT_BIT_INPUT_LINE_Y_LOCAL;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_minimenu_region_viewport(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_region_viewport,
            field->value,
            sizeof(loader->load.u.component.minimenu_region_viewport) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_minimenu_region_sidebar(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_region_sidebar,
            field->value,
            sizeof(loader->load.u.component.minimenu_region_sidebar) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_minimenu_region_chat(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_region_chat,
            field->value,
            sizeof(loader->load.u.component.minimenu_region_chat) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_minimenu_place_viewport_max(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_place_viewport_max,
            field->value,
            sizeof(loader->load.u.component.minimenu_place_viewport_max) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_minimenu_place_sidebar_max(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_place_sidebar_max,
            field->value,
            sizeof(loader->load.u.component.minimenu_place_sidebar_max) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_minimenu_place_chat_max(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        strncpy(
            loader->load.u.component.minimenu_place_chat_max,
            field->value,
            sizeof(loader->load.u.component.minimenu_place_chat_max) - 1);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uicomponent_crosshair_hotspot_offset(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.crosshair_hotspot_offset = atoi(field->value);
        loader->load.u.component.crosshair_hotspot_offset_set = 1;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_inv_item(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_INV:
        if( loader->load.u.inv.item_count < UI_INVENTORY_MAX_ITEMS )
            loader->load.u.inv.item_ids[loader->load.u.inv.item_count++] = atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_component(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count >= MAX_LAYOUT_ENTRIES )
        {
            fprintf(
                stderr,
                "uitree_loader: layout exceeds MAX_LAYOUT_ENTRIES (%d);"
                " ignoring extra entries\n",
                MAX_LAYOUT_ENTRIES);
            break;
        }
        strncpy(
            loader->load.u.layout.entries[loader->load.u.layout.entry_count].component,
            field->value,
            sizeof(loader->load.u.layout.entries[0].component) - 1);
        loader->load.u.layout.entry_count++;
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_x(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.def_x = atoi(field->value);
        break;
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].x =
                atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_y(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_COMPONENT:
        loader->load.u.component.def_y = atoi(field->value);
        break;
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].y =
                atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_width(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].width =
                atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_height(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].height =
                atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_anchor_x(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].anchor_x =
                atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_anchor_y(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].anchor_y =
                atoi(field->value);
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_top(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
        {
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].flags =
                STATIC_UI_RELATIVE_FLAG_TOP;
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].top =
                atoi(field->value);
        }
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_left(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
        {
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].flags =
                STATIC_UI_RELATIVE_FLAG_LEFT;
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].left =
                atoi(field->value);
        }
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_bottom(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
        {
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].flags =
                STATIC_UI_RELATIVE_FLAG_BOTTOM;
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].bottom =
                atoi(field->value);
        }
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_right(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
        {
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].flags =
                STATIC_UI_RELATIVE_FLAG_RIGHT;
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].right =
                atoi(field->value);
        }
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_uilayout_dirty(struct UITreeLoader* loader)
{
    struct RevConfigField* field = uitree_loader_current_field(loader);
    switch( loader->load.kind )
    {
    case LOAD_KIND_LAYOUT:
        if( loader->load.u.layout.entry_count > 0 )
        {
            const char* v = field->value;
            int truthy = (strcmp(v, "true") == 0) || (strcmp(v, "1") == 0);
            loader->load.u.layout.entries[loader->load.u.layout.entry_count - 1].always_dirty =
                truthy ? 1u : 0u;
        }
        break;
    default:
        break;
    }
    loader->field_index++;
}

static void
on_rcfield_default(struct UITreeLoader* loader)
{
    loader->field_index++;
}

enum UITreeLoaderStatus
uitree_loader_step(struct UITreeLoader* loader)
{
    if( !loader )
        return UITREE_LOADER_ERROR;
    if( loader->status == UITREE_LOADER_DONE || loader->status == UITREE_LOADER_ERROR )
        return loader->status;

    struct RevConfigBuffer* buf = loader->revconfig_buffer;
    if( !buf )
        return UITREE_LOADER_ERROR;

    /* ── Host scaffolding (decouple from GGame): validate before the field loop. ──
     * Further decouple uitree_impl_* so host->game can go away; sys_dash reserved for inv icons.
     */

    while( loader->field_index < buf->field_count )
    {
        struct RevConfigField* field = &buf->fields[loader->field_index];

        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            on_rcfield_itemtype(loader);
            break;
        case RCFIELD_ITEMNAME:
            on_rcfield_itemname(loader);
            break;
        case RCFIELD_ITEMDONE:
        {
            enum UITreeLoaderStatus st = on_rcfield_itemdone(loader);
            if( st != UITREE_LOADER_RUNNING )
                return st;
        }
        break;

        case RCFIELD_CACHE_TABLE:
            on_rcfield_cache_table(loader);
            break;
        case RCFIELD_CACHE_ARCHIVE:
            on_rcfield_cache_archive(loader);
            break;
        case RCFIELD_CACHE_CONTAINER:
            on_rcfield_cache_container(loader);
            break;
        case RCFIELD_CACHE_INDEX_FILENAME:
            on_rcfield_cache_index_filename(loader);
            break;
        case RCFIELD_CACHE_DATA_FILENAME:
            on_rcfield_cache_data_filename(loader);
            break;
        case RCFIELD_CACHE_FORMAT:
            on_rcfield_cache_format(loader);
            break;
        case RCFIELD_CACHE_ATLAS_INDEX:
            on_rcfield_cache_atlas_index(loader);
            break;
        case RCFIELD_CACHE_ATLAS_COUNT:
            on_rcfield_cache_atlas_count(loader);
            break;
        case RCFIELD_CACHE_TRANSFORM:
            on_rcfield_cache_transform(loader);
            break;
        case RCFIELD_CACHE_CROP_X:
            on_rcfield_cache_crop_x(loader);
            break;
        case RCFIELD_CACHE_CROP_Y:
            on_rcfield_cache_crop_y(loader);
            break;
        case RCFIELD_CACHE_CROP_WIDTH:
            on_rcfield_cache_crop_width(loader);
            break;
        case RCFIELD_CACHE_CROP_HEIGHT:
            on_rcfield_cache_crop_height(loader);
            break;

        case RCFIELD_UICOMPONENT_TYPE:
            on_rcfield_uicomponent_type(loader);
            break;
        case RCFIELD_UICOMPONENT_SPRITE:
            on_rcfield_uicomponent_sprite(loader);
            break;
        case RCFIELD_UICOMPONENT_SPRITE_ACTIVE:
            on_rcfield_uicomponent_sprite_active(loader);
            break;
        case RCFIELD_UICOMPONENT_WIDTH:
            on_rcfield_uicomponent_width(loader);
            break;
        case RCFIELD_UICOMPONENT_HEIGHT:
            on_rcfield_uicomponent_height(loader);
            break;
        case RCFIELD_UICOMPONENT_ANCHOR_X:
            on_rcfield_uicomponent_anchor_x(loader);
            break;
        case RCFIELD_UICOMPONENT_ANCHOR_Y:
            on_rcfield_uicomponent_anchor_y(loader);
            break;
        case RCFIELD_UICOMPONENT_TABNO:
            on_rcfield_uicomponent_tabno(loader);
            break;
        case RCFIELD_UICOMPONENT_COMPONENTNO:
            on_rcfield_uicomponent_componentno(loader);
            break;
        case RCFIELD_UICOMPONENT_INV:
            on_rcfield_uicomponent_inv(loader);
            break;
        case RCFIELD_UICOMPONENT_PAINT_LEVELS:
            on_rcfield_uicomponent_paint_levels(loader);
            break;
        case RCFIELD_UICOMPONENT_FONT:
            on_rcfield_uicomponent_font(loader);
            break;
        case RCFIELD_UICOMPONENT_CHATBACK_SCREEN_X:
            on_rcfield_uicomponent_chatback_screen_x(loader);
            break;
        case RCFIELD_UICOMPONENT_CHATBACK_SCREEN_Y:
            on_rcfield_uicomponent_chatback_screen_y(loader);
            break;
        case RCFIELD_UICOMPONENT_CHAT_CLIP_W:
            on_rcfield_uicomponent_chat_clip_w(loader);
            break;
        case RCFIELD_UICOMPONENT_CHAT_CLIP_H:
            on_rcfield_uicomponent_chat_clip_h(loader);
            break;
        case RCFIELD_UICOMPONENT_CHAT_TEXT_X_LOCAL:
            on_rcfield_uicomponent_chat_text_x_local(loader);
            break;
        case RCFIELD_UICOMPONENT_CHAT_SCROLLBAR_X_LOCAL:
            on_rcfield_uicomponent_chat_scrollbar_x_local(loader);
            break;
        case RCFIELD_UICOMPONENT_CHAT_SEPARATOR_Y_LOCAL:
            on_rcfield_uicomponent_chat_separator_y_local(loader);
            break;
        case RCFIELD_UICOMPONENT_CHAT_SEPARATOR_W:
            on_rcfield_uicomponent_chat_separator_w(loader);
            break;
        case RCFIELD_UICOMPONENT_CHAT_LINE_H:
            on_rcfield_uicomponent_chat_line_h(loader);
            break;
        case RCFIELD_UICOMPONENT_CHAT_INPUT_LINE_Y_LOCAL:
            on_rcfield_uicomponent_chat_input_line_y_local(loader);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_VIEWPORT:
            on_rcfield_uicomponent_minimenu_region_viewport(loader);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_SIDEBAR:
            on_rcfield_uicomponent_minimenu_region_sidebar(loader);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_REGION_CHAT:
            on_rcfield_uicomponent_minimenu_region_chat(loader);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_VIEWPORT_MAX:
            on_rcfield_uicomponent_minimenu_place_viewport_max(loader);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_SIDEBAR_MAX:
            on_rcfield_uicomponent_minimenu_place_sidebar_max(loader);
            break;
        case RCFIELD_UICOMPONENT_MINIMENU_PLACE_CHAT_MAX:
            on_rcfield_uicomponent_minimenu_place_chat_max(loader);
            break;
        case RCFIELD_UICOMPONENT_CROSSHAIR_HOTSPOT_OFFSET:
            on_rcfield_uicomponent_crosshair_hotspot_offset(loader);
            break;

        case RCFIELD_INV_ITEM:
            on_rcfield_inv_item(loader);
            break;

        case RCFIELD_UILAYOUT_COMPONENT:
            on_rcfield_uilayout_component(loader);
            break;
        case RCFIELD_UILAYOUT_X:
            on_rcfield_uilayout_x(loader);
            break;
        case RCFIELD_UILAYOUT_Y:
            on_rcfield_uilayout_y(loader);
            break;
        case RCFIELD_UILAYOUT_WIDTH:
            on_rcfield_uilayout_width(loader);
            break;
        case RCFIELD_UILAYOUT_HEIGHT:
            on_rcfield_uilayout_height(loader);
            break;
        case RCFIELD_UILAYOUT_ANCHOR_X:
            on_rcfield_uilayout_anchor_x(loader);
            break;
        case RCFIELD_UILAYOUT_ANCHOR_Y:
            on_rcfield_uilayout_anchor_y(loader);
            break;
        case RCFIELD_UILAYOUT_TOP:
            on_rcfield_uilayout_top(loader);
            break;
        case RCFIELD_UILAYOUT_LEFT:
            on_rcfield_uilayout_left(loader);
            break;
        case RCFIELD_UILAYOUT_BOTTOM:
            on_rcfield_uilayout_bottom(loader);
            break;
        case RCFIELD_UILAYOUT_RIGHT:
            on_rcfield_uilayout_right(loader);
            break;
        case RCFIELD_UILAYOUT_DIRTY:
            on_rcfield_uilayout_dirty(loader);
            break;

        default:
            on_rcfield_default(loader);
            break;
        }
    }

    loader->status = UITREE_LOADER_DONE;
    return UITREE_LOADER_DONE;
}

/* ── Accessors ──────────────────────────────────────────────────────────────── */

int
uitree_loader_pending_asset_count(const struct UITreeLoader* loader)
{
    if( !loader )
        return 0;
    return loader->pending_asset_count;
}

const struct UITreeLoaderAssetRequest*
uitree_loader_pending_assets(const struct UITreeLoader* loader)
{
    if( !loader )
        return NULL;
    return loader->pending_assets;
}

/* ── Cleanup ────────────────────────────────────────────────────────────────── */

void
uitree_loader_free(struct UITreeLoader* loader)
{
    if( !loader )
        return;
    free(loader);
}
