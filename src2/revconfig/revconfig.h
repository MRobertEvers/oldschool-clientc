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
    RCFIELD_UILAYOUT_PARENT,
    RCFIELD_UILAYOUT_NAME,
    RCFIELD_UILAYOUT_GROUP,
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

/*
 * Static UI widget definition from a [component:name] revconfig INI section.
 * Referenced by layout entries via RevConfigUILayoutItem.component (c=).
 *
 * Not every field applies to every type= value; see instance_revconfig_build_layout_node
 * (owner UINodeSpec) and Task_InstanceOnRCUIComponent (RS subtree capture when
 * componentno >= 0).
 */
struct RevConfigUIComponentItem
{
    /* [component:<name>] — unique key; layout c= and rs_subtrees[] lookup by this string. */
    char name[64];

    /*
     * INI: type=
     * Widget kind; mapped by component_type_from_string() to StaticUIComponentType.
     * Builtin: compass, minimap, world, sidebar, chat, sprite, redstone_tab, tab_icon.
     * RS (static owner or RS-load trigger): rs_layer, rs_graphic, rs_text, rs_rect,
     * rs_model, rs_inv, rs_line.
     */
    char type[32];

    /*
     * INI: sprite=
     * Name of a [sprite:…] cache entry. Resolved through ui_sprite_lookup for
     * compass, minimap, sprite, redstone_tab, tab_icon, rs_graphic.
     */
    char sprite[64];

    /*
     * INI: sprite_active=
     * Alternate sprite for pressed/hover state (redstone_tab, rs_graphic).
     */
    char sprite_active[64];

    /*
     * INI: inv=
     * Name of an [inv:…] section. Resolves to uitree inv-pool index for sidebar
     * owners and static rs_inv; also passed into RS subtree bake so RS_COMPONENT_INV
     * children share the same inventory grid.
     */
    char inv[64];

    /*
     * INI: w=
     * Default width when the layout entry omits w=. For type=rs_inv: grid column count
     * (default 4). Also used by instance_revconfig_resolve_panel_roots (Dat1).
     */
    int width;

    /*
     * INI: h=
     * Default height when the layout entry omits h=. For type=rs_inv: grid row count
     * (default 7).
     */
    int height;

    /*
     * INI: anchor_x= / anchor_y= on the component section.
     * Parsed and stored; the src2 revconfig load path uses layout-entry anchors
     * (RevConfigUILayoutItem) for uitree position instead. Retained for legacy loaders.
     */
    int anchor_x;
    int anchor_y;

    /*
     * INI: tabno=
     * Tab index for sidebar, redstone_tab, and tab_icon owners.
     */
    int tabno;

    /*
     * INI: componentno=  (default -1 when section opens)
     * Interfaces-archive component id. When >= 0 and type is RS-backed, triggers
     * Task_RSComponentLoad during revconfig ingest; subtree is baked under the owner
     * in instance_revconfig_build_layout_node. Also copied to UINodeSpec.component_id
     * and sidebar.componentno.
     */
    int componentno;

    /*
     * INI: paint_levels=
     * Comma-separated scene level indices for type=world (e.g. "0,1,2,3").
     * Empty string means all levels (0xF mask).
     */
    char paint_levels[64];

    /* INI: color= — RGB/text/line colour for rs_text, rs_rect, rs_line. */
    int color;

    /*
     * INI: filled= (true/1 or false/0)
     * type=rs_rect: filled rectangle. type=rs_line: treated as horizontal flag in
     * the layout builder (no dedicated INI key yet).
     */
    int filled;

    /* INI: font= — RS font id 0–3 for type=rs_text (clamped to 1 if out of range). */
    int font;

    /* INI: center= — horizontally centred text for type=rs_text. */
    int center;

    /* INI: shadowed= — text shadow for type=rs_text. */
    int shadowed;

    /* INI: text= — literal string for static type=rs_text owners (not cache-backed). */
    char text[256];
};

struct RevConfigUILayoutItem
{
    char name[64];
    char layout_group[32];
    char component[64];
    char parent[64];
    int x;
    int y;
    int width;
    int height;
    int anchor_x;
    int anchor_y;
    uint8_t has_anchor;
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