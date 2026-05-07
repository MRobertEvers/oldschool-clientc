#ifndef UITREE_LOAD_PRIVATE_H
#define UITREE_LOAD_PRIVATE_H

/*
 * Internal types and non-static function declarations shared between
 * uitree_load.c (which defines them) and uitree_loader.c (which drives them).
 * This header is NOT part of the public API; do not include it from outside
 * src/osrs/revconfig/.
 */

#include "graphics/dash.h"
#include "graphics/dashmap.h"
#include "osrs/chat.h"
#include "osrs/minimenu_regions.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/revconfig/uitree_loader.h"

#include <stdint.h>

/* ── Internal constants ─────────────────────────────────────────────────────── */

#define MAX_LAYOUT_ENTRIES 128

/* ── Sprite atlas address mode ──────────────────────────────────────────────── */

enum SpriteLoad_AtlasMode
{
    SPRITELOAD_ATLAS_MODE_INDEX,
    SPRITELOAD_ATLAS_MODE_COUNT,
};

/* ── INI item kinds ─────────────────────────────────────────────────────────── */

enum LoadKind
{
    LOAD_KIND_NONE,
    LOAD_KIND_SPRITE,
    LOAD_KIND_COMPONENT,
    LOAD_KIND_LAYOUT,
    LOAD_KIND_INV
};

/* ── Per-item load accumulators ─────────────────────────────────────────────── */

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

/* ── hashmap entry types (kept in sync with the DashMap configs in uitree_load.c) ── */

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

/* ── Non-static functions exposed by uitree_load.c for use by uitree_loader.c ── */

struct BuildCacheDat;
struct GameCache;
struct UIScene;
struct UIInventoryPool;
struct Scene2;
struct GGame;

/**
 * Identify the load-kind from an INI type string.
 */
uint32_t
uitree_impl_load_kind(const char* str);

/**
 * Dispatch the current item name into the appropriate load accumulator.
 */
void
uitree_impl_on_itemname(
    struct CurrentLoad* load,
    const char* value);

/**
 * Attempt to load a sprite item from the 2D media jagfile.
 * Returns 0 on success, -1 when an asset is needed (fills *out_req).
 */
int
uitree_impl_load_sprite(
    struct SpriteLoad* load,
    struct DashMap* sprite_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct BuildCacheDat* buildcachedat,
    struct UITreeLoaderAssetRequest* out_req);

/**
 * Attempt to process a component definition item.
 * Returns 0 on success, -1 when an asset is needed (fills *out_req).
 */
int
uitree_impl_load_component(
    struct ComponentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* gamecache,
    struct UITreeLoaderAssetRequest* out_req);

/**
 * Attempt to process a layout item (may expand RS subtrees from gamecache).
 * Returns 0 on success, -1 when an asset is needed (fills *out_req).
 */
int
uitree_impl_load_layout(
    struct LayoutLoad* load,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct GameCache* gamecache,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct UITreeLoaderAssetRequest* out_req);

/**
 * Attempt to process an inventory item.
 * Returns 0 on success, -1 when an asset is needed (fills *out_req).
 */
int
uitree_impl_load_inv(
    struct InvLoad* il,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct UIScene* ui_scene,
    struct UITreeLoaderAssetRequest* out_req);

/**
 * Dispatch a completed CurrentLoad to the appropriate typed loader.
 * Returns 0 on success, -1 when an asset is needed (fills *out_req).
 */
int
uitree_impl_load_item(
    struct CurrentLoad* load,
    struct DashMap* sprite_hmap,
    struct DashMap* component_hmap,
    struct UITree* ui,
    struct UIScene* ui_scene,
    struct UIInventoryPool* inv_pool,
    struct GGame* game,
    struct UITreeLoaderAssetRequest* out_req);

/**
 * Wire named UIScene elements (scrollbars, minimap dots, etc.) to GGame fields.
 */
void
uitree_impl_resolve_game_uiscene_sprite_ids(
    struct GGame* game,
    struct UIScene* ui_scene);

#endif /* UITREE_LOAD_PRIVATE_H */
