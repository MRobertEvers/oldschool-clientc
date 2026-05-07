#ifndef UITREE_INI_LOAD_STATE_H
#define UITREE_INI_LOAD_STATE_H

/*
 * INI accumulator types and DashMap entry layouts shared by uitree_loader.c (field walk)
 * and uitree_load.c (commit).  Does not include uitree_load_private.h.
 */

#include "graphics/dash.h"
#include "graphics/dashmap.h"
#include "osrs/chat.h"
#include "osrs/minimenu_regions.h"
#include "osrs/revconfig/uitree.h"

#include <stdint.h>

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
        struct SpriteLoad _sprite;
        struct ComponentLoad _component;
        struct LayoutLoad _layout;
        struct InvLoad _inv;
    };
};

struct SpriteEntry
{
    char name[64]; /* key — must be first field */
    struct DashSprite** sprites;
    int count;
    int id;
};

struct ComponentEntry
{
    char name[64]; /* key — must be first field */
    enum StaticUIComponentType type;
    int sprite_id;
    int sprite_index;
    int sprite_id_active;
    int sprite_index_active;
    int id;
    int def_x;
    int def_y;
    int width;
    int height;
    int anchor_x;
    int anchor_y;
    int tabno;
    int componentno;
    char inv[64];
    char font[64];
    uint8_t level_mask;
    struct ChatUILayout chat_layout;
    uint16_t chat_geom_mask;
    struct MinimenuIniRegions minimenu_regions;
    int crosshair_hotspot_offset;
};

#endif /* UITREE_INI_LOAD_STATE_H */
