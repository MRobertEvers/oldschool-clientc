#include "revconfig.h"

#include "osrs/rscache/tables/string_utils.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char const*
revconfig_field_kind_str(enum RevConfigFieldKind kind)
{
    switch( kind )
    {
    case RCFIELD_NONE:
        return "RCFIELD_NONE";
    case RCFIELD_ITEMTYPE:
        return "RCFIELD_ITEMTYPE";
    case RCFIELD_ITEMNAME:
        return "RCFIELD_ITEMNAME";
    case RCFIELD_ITEMDONE:
        return "RCFIELD_ITEMDONE";
    case RCFIELD_CACHE_TABLE:
        return "RCFIELD_CACHE_TABLE";
    case RCFIELD_CACHE_ARCHIVE:
        return "RCFIELD_CACHE_ARCHIVE";
    case RCFIELD_CACHE_CONTAINER:
        return "RCFIELD_CACHE_CONTAINER";
    case RCFIELD_CACHE_INDEX_FILENAME:
        return "RCFIELD_CACHE_INDEX_FILENAME";
    case RCFIELD_CACHE_DATA_FILENAME:
        return "RCFIELD_CACHE_DATA_FILENAME";
    case RCFIELD_CACHE_FORMAT:
        return "RCFIELD_CACHE_FORMAT";
    case RCFIELD_CACHE_ATLAS_INDEX:
        return "RCFIELD_CACHE_ATLAS_INDEX";
    case RCFIELD_CACHE_ATLAS_COUNT:
        return "RCFIELD_CACHE_ATLAS_COUNT";
    case RCFIELD_CACHE_TRANSFORM:
        return "RCFIELD_CACHE_TRANSFORM";
    case RCFIELD_CACHE_CROP_X:
        return "RCFIELD_CACHE_CROP_X";
    case RCFIELD_CACHE_CROP_Y:
        return "RCFIELD_CACHE_CROP_Y";
    case RCFIELD_CACHE_CROP_WIDTH:
        return "RCFIELD_CACHE_CROP_WIDTH";
    case RCFIELD_CACHE_CROP_HEIGHT:
        return "RCFIELD_CACHE_CROP_HEIGHT";
    case RCFIELD_UICOMPONENT_TYPE:
        return "RCFIELD_UICOMPONENT_TYPE";
    case RCFIELD_UICOMPONENT_SPRITE:
        return "RCFIELD_UICOMPONENT_SPRITE";
    case RCFIELD_UICOMPONENT_WIDTH:
        return "RCFIELD_UICOMPONENT_WIDTH";
    case RCFIELD_UICOMPONENT_HEIGHT:
        return "RCFIELD_UICOMPONENT_HEIGHT";
    case RCFIELD_UICOMPONENT_ANCHOR_X:
        return "RCFIELD_UICOMPONENT_ANCHOR_X";
    case RCFIELD_UICOMPONENT_ANCHOR_Y:
        return "RCFIELD_UICOMPONENT_ANCHOR_Y";
    case RCFIELD_UICOMPONENT_TABNO:
        return "RCFIELD_UICOMPONENT_TABNO";
    case RCFIELD_UICOMPONENT_SPRITE_ACTIVE:
        return "RCFIELD_UICOMPONENT_SPRITE_ACTIVE";
    case RCFIELD_UICOMPONENT_COMPONENTNO:
        return "RCFIELD_UICOMPONENT_COMPONENTNO";
    case RCFIELD_UICOMPONENT_INV:
        return "RCFIELD_UICOMPONENT_INV";
    case RCFIELD_UICOMPONENT_PAINT_LEVELS:
        return "RCFIELD_UICOMPONENT_PAINT_LEVELS";
    case RCFIELD_INV_ITEM:
        return "RCFIELD_INV_ITEM";
    case RCFIELD_UILAYOUT_COMPONENT:
        return "RCFIELD_UILAYOUT_COMPONENT";
    case RCFIELD_UILAYOUT_X:
        return "RCFIELD_UILAYOUT_X";
    case RCFIELD_UILAYOUT_Y:
        return "RCFIELD_UILAYOUT_Y";
    case RCFIELD_UILAYOUT_WIDTH:
        return "RCFIELD_UILAYOUT_WIDTH";
    case RCFIELD_UILAYOUT_HEIGHT:
        return "RCFIELD_UILAYOUT_HEIGHT";
    case RCFIELD_UILAYOUT_ANCHOR_X:
        return "RCFIELD_UILAYOUT_ANCHOR_X";
    case RCFIELD_UILAYOUT_ANCHOR_Y:
        return "RCFIELD_UILAYOUT_ANCHOR_Y";
    case RCFIELD_UILAYOUT_TOP:
        return "RCFIELD_UILAYOUT_TOP";
    case RCFIELD_UILAYOUT_LEFT:
        return "RCFIELD_UILAYOUT_LEFT";
    case RCFIELD_UILAYOUT_BOTTOM:
        return "RCFIELD_UILAYOUT_BOTTOM";
    case RCFIELD_UILAYOUT_RIGHT:
        return "RCFIELD_UILAYOUT_RIGHT";
    case RCFIELD_UILAYOUT_DIRTY:
        return "RCFIELD_UILAYOUT_DIRTY";
    default:
        return "UNKNOWN";
    }
}

struct RevConfigBuffer*
revconfig_buffer_new(uint32_t hint)
{
    struct RevConfigBuffer* buffer = malloc(sizeof(struct RevConfigBuffer));
    if( !buffer )
        return NULL;
    memset(buffer, 0, sizeof(struct RevConfigBuffer));

    if( hint > 0 )
    {
        buffer->fields = malloc(sizeof(struct RevConfigField) * hint);
        if( !buffer->fields )
        {
            free(buffer);
            return NULL;
        }
        buffer->field_capacity = hint;
    }

    return buffer;
}

void
revconfig_buffer_free(struct RevConfigBuffer* buffer)
{
    if( !buffer )
        return;
    free(buffer->fields);
    free(buffer);
}

int
revconfig_buffer_push_field(
    struct RevConfigBuffer* buffer,
    enum RevConfigFieldKind kind,
    const char* value)
{
    if( buffer->field_count >= buffer->field_capacity )
    {
        uint32_t new_capacity = buffer->field_capacity == 0 ? 16 : buffer->field_capacity * 2;
        struct RevConfigField* new_fields =
            realloc(buffer->fields, sizeof(struct RevConfigField) * new_capacity);
        if( !new_fields )
            return -1;
        buffer->fields = new_fields;
        buffer->field_capacity = new_capacity;
    }

    struct RevConfigField* field = &buffer->fields[buffer->field_count++];
    field->kind = kind;
    strncpy_trimmed(field->value, value, sizeof(field->value), TRIM_CHARS_WHITESPACE);
    return 0;
}

struct RevConfigItemBuffer*
revconfig_item_buffer_new(uint32_t hint)
{
    struct RevConfigItemBuffer* buffer = malloc(sizeof(struct RevConfigItemBuffer));
    if( !buffer )
        return NULL;
    memset(buffer, 0, sizeof(struct RevConfigItemBuffer));

    if( hint > 0 )
    {
        buffer->items = malloc(sizeof(struct RevConfigItem) * hint);
        if( !buffer->items )
        {
            free(buffer);
            return NULL;
        }
        buffer->item_capacity = hint;
    }

    return buffer;
}

void
revconfig_item_buffer_free(struct RevConfigItemBuffer* buffer)
{
    if( !buffer )
        return;
    free(buffer->items);
    free(buffer);
}

struct RevConfigItem*
revconfig_item_buffer_push(struct RevConfigItemBuffer* buffer)
{
    if( !buffer )
        return NULL;

    if( buffer->item_count >= buffer->item_capacity )
    {
        uint32_t new_capacity = buffer->item_capacity == 0 ? 16 : buffer->item_capacity * 2;
        struct RevConfigItem* new_items =
            realloc(buffer->items, sizeof(struct RevConfigItem) * new_capacity);
        if( !new_items )
            return NULL;
        buffer->items = new_items;
        buffer->item_capacity = new_capacity;
    }

    struct RevConfigItem* item = &buffer->items[buffer->item_count++];
    memset(item, 0, sizeof(*item));
    return item;
}

static void
revconfig_item_set_name(struct RevConfigItem* item, const char* value)
{
    if( !item || !value )
        return;

    switch( item->kind )
    {
    case RCITEM_CACHE:
        strncpy(item->u.cache.name, value, sizeof(item->u.cache.name) - 1);
        break;
    case RCITEM_UICOMPONENT:
        strncpy(item->u.uicomponent.name, value, sizeof(item->u.uicomponent.name) - 1);
        break;
    case RCITEM_UILAYOUT:
        strncpy(item->u.uilayout.name, value, sizeof(item->u.uilayout.name) - 1);
        break;
    case RCITEM_INV:
        strncpy(item->u.inv.name, value, sizeof(item->u.inv.name) - 1);
        break;
    default:
        break;
    }
}

static void
revconfig_item_begin(struct RevConfigItem* item, const char* type_value)
{
    memset(item, 0, sizeof(*item));

    if( strcmp(type_value, "sprite") == 0 )
        item->kind = RCITEM_CACHE;
    else if( strcmp(type_value, "component") == 0 )
        item->kind = RCITEM_UICOMPONENT;
    else if( strcmp(type_value, "layout") == 0 )
        item->kind = RCITEM_UILAYOUT;
    else if( strcmp(type_value, "inv") == 0 )
        item->kind = RCITEM_INV;
    else
        item->kind = RCITEM_NONE;
}

static void
revconfig_item_apply_cache_field(
    struct RevConfigCacheItem* cache,
    enum RevConfigFieldKind kind,
    const char* value)
{
    switch( kind )
    {
    case RCFIELD_CACHE_TABLE:
        strncpy(cache->table, value, sizeof(cache->table) - 1);
        break;
    case RCFIELD_CACHE_ARCHIVE:
        strncpy(cache->archive, value, sizeof(cache->archive) - 1);
        break;
    case RCFIELD_CACHE_CONTAINER:
        strncpy(cache->container, value, sizeof(cache->container) - 1);
        break;
    case RCFIELD_CACHE_INDEX_FILENAME:
        strncpy(cache->index_filename, value, sizeof(cache->index_filename) - 1);
        break;
    case RCFIELD_CACHE_DATA_FILENAME:
        strncpy(cache->data_filename, value, sizeof(cache->data_filename) - 1);
        break;
    case RCFIELD_CACHE_FORMAT:
        strncpy(cache->format, value, sizeof(cache->format) - 1);
        break;
    case RCFIELD_CACHE_ATLAS_INDEX:
        cache->atlas_index = atoi(value);
        break;
    case RCFIELD_CACHE_ATLAS_COUNT:
        cache->atlas_count = atoi(value);
        break;
    case RCFIELD_CACHE_TRANSFORM:
        if( cache->transform_count < 4 )
        {
            strncpy(
                cache->transform[cache->transform_count],
                value,
                sizeof(cache->transform[cache->transform_count]) - 1);
            cache->transform_count++;
        }
        break;
    case RCFIELD_CACHE_CROP_X:
        cache->crop_x = atoi(value);
        break;
    case RCFIELD_CACHE_CROP_Y:
        cache->crop_y = atoi(value);
        break;
    case RCFIELD_CACHE_CROP_WIDTH:
        cache->crop_width = atoi(value);
        break;
    case RCFIELD_CACHE_CROP_HEIGHT:
        cache->crop_height = atoi(value);
        break;
    default:
        break;
    }
}

static void
revconfig_item_apply_uicomponent_field(
    struct RevConfigUIComponentItem* comp,
    enum RevConfigFieldKind kind,
    const char* value)
{
    switch( kind )
    {
    case RCFIELD_UICOMPONENT_TYPE:
        strncpy(comp->type, value, sizeof(comp->type) - 1);
        break;
    case RCFIELD_UICOMPONENT_SPRITE:
        strncpy(comp->sprite, value, sizeof(comp->sprite) - 1);
        break;
    case RCFIELD_UICOMPONENT_WIDTH:
        comp->width = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_HEIGHT:
        comp->height = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_ANCHOR_X:
        comp->anchor_x = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_ANCHOR_Y:
        comp->anchor_y = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_TABNO:
        comp->tabno = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_SPRITE_ACTIVE:
        strncpy(comp->sprite_active, value, sizeof(comp->sprite_active) - 1);
        break;
    case RCFIELD_UICOMPONENT_COMPONENTNO:
        comp->componentno = atoi(value);
        break;
    case RCFIELD_UICOMPONENT_INV:
        strncpy(comp->inv, value, sizeof(comp->inv) - 1);
        break;
    case RCFIELD_UICOMPONENT_PAINT_LEVELS:
        comp->paint_levels = 1;
        break;
    default:
        break;
    }
}

static void
revconfig_item_apply_uilayout_field(
    struct RevConfigUILayoutItem* layout,
    enum RevConfigFieldKind kind,
    const char* value)
{
    switch( kind )
    {
    case RCFIELD_UILAYOUT_COMPONENT:
        strncpy(layout->component, value, sizeof(layout->component) - 1);
        break;
    case RCFIELD_UILAYOUT_X:
        layout->x = atoi(value);
        break;
    case RCFIELD_UILAYOUT_Y:
        layout->y = atoi(value);
        break;
    case RCFIELD_UILAYOUT_WIDTH:
        layout->width = atoi(value);
        break;
    case RCFIELD_UILAYOUT_HEIGHT:
        layout->height = atoi(value);
        break;
    case RCFIELD_UILAYOUT_ANCHOR_X:
        layout->anchor_x = atoi(value);
        break;
    case RCFIELD_UILAYOUT_ANCHOR_Y:
        layout->anchor_y = atoi(value);
        break;
    case RCFIELD_UILAYOUT_TOP:
        layout->top = atoi(value);
        break;
    case RCFIELD_UILAYOUT_LEFT:
        layout->left = atoi(value);
        break;
    case RCFIELD_UILAYOUT_BOTTOM:
        layout->bottom = atoi(value);
        break;
    case RCFIELD_UILAYOUT_RIGHT:
        layout->right = atoi(value);
        break;
    case RCFIELD_UILAYOUT_DIRTY:
        layout->dirty = 1;
        break;
    default:
        break;
    }
}

static void
revconfig_item_apply_field(
    struct RevConfigItem* item,
    enum RevConfigFieldKind kind,
    const char* value)
{
    if( !item || item->kind == RCITEM_NONE )
        return;

    switch( item->kind )
    {
    case RCITEM_CACHE:
        revconfig_item_apply_cache_field(&item->u.cache, kind, value);
        break;
    case RCITEM_UICOMPONENT:
        revconfig_item_apply_uicomponent_field(&item->u.uicomponent, kind, value);
        break;
    case RCITEM_UILAYOUT:
        revconfig_item_apply_uilayout_field(&item->u.uilayout, kind, value);
        break;
    case RCITEM_INV:
        if( kind == RCFIELD_INV_ITEM && item->u.inv.item_count < REVCONFIG_INV_MAX_ITEMS )
        {
            strncpy(
                item->u.inv.items[item->u.inv.item_count],
                value,
                sizeof(item->u.inv.items[item->u.inv.item_count]) - 1);
            item->u.inv.item_count++;
        }
        break;
    default:
        break;
    }
}

static void
revconfig_item_finish(
    struct RevConfigItem* pending,
    struct RevConfigItemBuffer* out)
{
    if( !pending || !out || pending->kind == RCITEM_NONE )
        return;

    struct RevConfigItem* item = revconfig_item_buffer_push(out);
    if( !item )
        return;

    *item = *pending;
    pending->kind = RCITEM_NONE;
}

void
revconfig_items_build(
    const struct RevConfigBuffer* fields,
    struct RevConfigItemBuffer* out)
{
    if( !fields || !out )
        return;

    struct RevConfigItem pending = { 0 };

    for( uint32_t i = 0; i < fields->field_count; i++ )
    {
        const struct RevConfigField* field = &fields->fields[i];

        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            revconfig_item_begin(&pending, field->value);
            break;
        case RCFIELD_ITEMNAME:
            revconfig_item_set_name(&pending, field->value);
            break;
        case RCFIELD_ITEMDONE:
            revconfig_item_finish(&pending, out);
            break;
        default:
            revconfig_item_apply_field(&pending, field->kind, field->value);
            break;
        }
    }
}