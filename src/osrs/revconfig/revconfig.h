#ifndef REVCONFIG_CACHE_H
#define REVCONFIG_CACHE_H

#include <stdint.h>

// Dat1 sprite cache section example:
// table=configs
// archive=media
// container=jagfile
// index=index.dat
// filename=invback.dat
// format=pix8
// atlas_index=0
//
// Dat2 sprite cache section example:
// table=sprites
// archive_id=297
// atlas_index=0

enum RevConfigFieldKind
{
    RCFIELD_NONE,
    RCFIELD_ITEMTYPE,
    RCFIELD_ITEMNAME,
    RCFIELD_ITEMDONE,
    RCFIELD_CACHE_TABLE,
    RCFIELD_CACHE_ARCHIVE,
    RCFIELD_CACHE_ARCHIVE_ID,
    RCFIELD_CACHE_CONTAINER,
    RCFIELD_CACHE_INDEX_FILENAME,
    RCFIELD_CACHE_DATA_FILENAME,
    RCFIELD_CACHE_FORMAT,
    RCFIELD_CACHE_ATLAS_INDEX,
    RCFIELD_CACHE_ATLAS_COUNT,
    RCFIELD_CACHE_TRANSFORM,
    RCFIELD_CACHE_CROP_X,
    RCFIELD_CACHE_CROP_Y,
    RCFIELD_CACHE_CROP_WIDTH,
    RCFIELD_CACHE_CROP_HEIGHT,
    RCFIELD_UICOMPONENT_TYPE,
    RCFIELD_UICOMPONENT_SPRITE,
    RCFIELD_UICOMPONENT_WIDTH,
    RCFIELD_UICOMPONENT_HEIGHT,
    RCFIELD_UICOMPONENT_ANCHOR_X,
    RCFIELD_UICOMPONENT_ANCHOR_Y,
    RCFIELD_UICOMPONENT_TABNO,
    RCFIELD_UICOMPONENT_SPRITE_ACTIVE,
    RCFIELD_UICOMPONENT_COMPONENTNO,
    RCFIELD_UICOMPONENT_INV,
    RCFIELD_UICOMPONENT_PAINT_LEVELS,
    RCFIELD_UICOMPONENT_COLOR,
    RCFIELD_UICOMPONENT_FILLED,
    RCFIELD_UICOMPONENT_FONT,
    RCFIELD_UICOMPONENT_CENTER,
    RCFIELD_UICOMPONENT_SHADOWED,
    RCFIELD_UICOMPONENT_TEXT,
    RCFIELD_INV_ITEM,
    RCFIELD_UILAYOUT_COMPONENT,
    RCFIELD_UILAYOUT_X,
    RCFIELD_UILAYOUT_Y,
    RCFIELD_UILAYOUT_WIDTH,
    RCFIELD_UILAYOUT_HEIGHT,
    RCFIELD_UILAYOUT_ANCHOR_X,
    RCFIELD_UILAYOUT_ANCHOR_Y,
    RCFIELD_UILAYOUT_TOP,
    RCFIELD_UILAYOUT_LEFT,
    RCFIELD_UILAYOUT_BOTTOM,
    RCFIELD_UILAYOUT_RIGHT,
    RCFIELD_UILAYOUT_DIRTY,
    RCFIELD_UILAYOUT_NULL,
};

struct RevConfigField
{
    uint8_t kind;
    char value[64];
};

struct RevConfigBuffer
{
    struct RevConfigField* fields;
    uint32_t field_count;
    uint32_t field_capacity;
};

enum RevConfigItemKind
{
    RCITEM_NONE,
    RCITEM_CACHE_SPRITE,
    RCITEM_UICOMPONENT,
    RCITEM_UILAYOUT,
    RCITEM_INV,
};

struct RevConfigCacheItem
{
    char name[64];
    char table[64];
    char archive[64];
    char container[64];
    char index_filename[64];
    char data_filename[64];
    char format[16];
    int atlas_index;
    int atlas_count;
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
    char transform[4][64];
    int transform_count;
    int archive_id;
};

struct RevConfigUIComponentItem
{
    char name[64];
    char type[32];
    char sprite[64];
    char sprite_active[64];
    char inv[64];
    int width;
    int height;
    int anchor_x;
    int anchor_y;
    int tabno;
    int componentno;
    char paint_levels[64];
    int color;
    int filled;
    int font;
    int center;
    int shadowed;
    char text[256];
};

struct RevConfigUILayoutItem
{
    char name[64];
    char component[64];
    int x;
    int y;
    int width;
    int height;
    int anchor_x;
    int anchor_y;
    int top;
    int left;
    int bottom;
    int right;
    int dirty;
};

#define REVRSCacheDat2A_ConfigKind_Inv_MAX_ITEMS 32

struct RevConfigInvItem
{
    char name[64];
    char items[REVRSCacheDat2A_ConfigKind_Inv_MAX_ITEMS][64];
    int item_count;
};

struct RevConfigItem
{
    enum RevConfigItemKind kind;
    union
    {
        struct RevConfigCacheItem cache;
        struct RevConfigUIComponentItem uicomponent;
        struct RevConfigUILayoutItem uilayout;
        struct RevConfigInvItem inv;
    } u;
};

struct RevConfigItemBuffer
{
    struct RevConfigItem* items;
    uint32_t item_count;
    uint32_t item_capacity;
};

char const*
revconfig_field_kind_str(enum RevConfigFieldKind kind);

struct RevConfigBuffer*
revconfig_buffer_new(uint32_t hint);

void
revconfig_buffer_free(struct RevConfigBuffer* buffer);

int
revconfig_buffer_push_field(
    struct RevConfigBuffer* buffer,
    enum RevConfigFieldKind kind,
    const char* value);

struct RevConfigItemBuffer*
revconfig_item_buffer_new(uint32_t hint);

void
revconfig_item_buffer_free(struct RevConfigItemBuffer* buffer);

struct RevConfigItem*
revconfig_item_buffer_push(struct RevConfigItemBuffer* buffer);

void
revconfig_items_build(
    const struct RevConfigBuffer* fields,
    struct RevConfigItemBuffer* out);

#endif