#ifndef REVCONFIG_CACHE_H
#define REVCONFIG_CACHE_H

#include <stdint.h>

#define REVCONFIG_MENU_OPTION_SLOTS 5
#define REVCONFIG_MENU_OPTION_LEN 32
#define REVCONFIG_CHAT_OP_TEMPLATE_LEN 64
/** Effects one [component:…] may advertise with repeated hotkey= lines. */
#define REVCONFIG_COMPONENT_HOTKEY_MAX 8

/* Local copies of RS button / minimenu constants so this module stays leaf. */
enum RevConfigButtonType
{
    REVCONFIG_BUTTON_TYPE_OK = 1,
    REVCONFIG_BUTTON_TYPE_TARGET = 2,
    REVCONFIG_BUTTON_TYPE_CLOSE = 3,
    REVCONFIG_BUTTON_TYPE_TOGGLE = 4,
    REVCONFIG_BUTTON_TYPE_SELECT = 5,
    REVCONFIG_BUTTON_TYPE_CONTINUE = 6,
};

enum RevConfigMiniMenuAction
{
    REVCONFIG_MINIMENU_CANCEL = 1106,
    REVCONFIG_MINIMENU_WALK = 718,
    REVCONFIG_MINIMENU_IF_BUTTON = 231,
    REVCONFIG_MINIMENU_IF_BUTTON_TOGGLE = 435,
    REVCONFIG_MINIMENU_IF_BUTTON_SELECT = 225,
    REVCONFIG_MINIMENU_RESUME_PAUSEBUTTON = 997,
    REVCONFIG_MINIMENU_CLOSE_MODAL = 737,
    REVCONFIG_MINIMENU_INV_BUTTON1 = 582,
    REVCONFIG_MINIMENU_INV_BUTTON2 = 113,
    REVCONFIG_MINIMENU_INV_BUTTON3 = 555,
    REVCONFIG_MINIMENU_INV_BUTTON4 = 331,
    REVCONFIG_MINIMENU_INV_BUTTON5 = 354,
    REVCONFIG_MINIMENU_FRIENDLIST_ADD = 605,
    REVCONFIG_MINIMENU_IGNORELIST_ADD = 47,
    REVCONFIG_MINIMENU_FRIENDLIST_DEL = 513,
    REVCONFIG_MINIMENU_IGNORELIST_DEL = 884,
    REVCONFIG_MINIMENU_MESSAGE_PRIVATE = 902,
    REVCONFIG_MINIMENU_REPORT_ABUSE = 524,
    REVCONFIG_MINIMENU_OPHELD1 = 694,
    REVCONFIG_MINIMENU_OPHELD2 = 962,
    REVCONFIG_MINIMENU_OPHELD3 = 795,
    REVCONFIG_MINIMENU_OPHELD4 = 681,
    REVCONFIG_MINIMENU_OPHELD5 = 100,
    REVCONFIG_MINIMENU_OPHELD6 = 1328,
    /** "Use <item>" — begin held-item targeting (rev-254 OPHELDT_START). */
    REVCONFIG_MINIMENU_OPHELDT_START = 102,
    REVCONFIG_MINIMENU_OPPLAYER_TRADEREQ = 507,
    REVCONFIG_MINIMENU_OPPLAYER_DUELREQ = 957,
    /* World entity ops (rev-254 scheme; mirrors v0 minimenu_action.h). */
    REVCONFIG_MINIMENU_OPNPC1 = 242,
    REVCONFIG_MINIMENU_OPNPC2 = 209,
    REVCONFIG_MINIMENU_OPNPC3 = 309,
    REVCONFIG_MINIMENU_OPNPC4 = 852,
    REVCONFIG_MINIMENU_OPNPC5 = 793,
    REVCONFIG_MINIMENU_OPNPC6 = 1714,
    REVCONFIG_MINIMENU_OPLOC1 = 625,
    REVCONFIG_MINIMENU_OPLOC2 = 721,
    REVCONFIG_MINIMENU_OPLOC3 = 743,
    REVCONFIG_MINIMENU_OPLOC4 = 357,
    REVCONFIG_MINIMENU_OPLOC5 = 1071,
    REVCONFIG_MINIMENU_OPLOC6 = 1381,
    REVCONFIG_MINIMENU_OPOBJ1 = 139,
    REVCONFIG_MINIMENU_OPOBJ2 = 778,
    REVCONFIG_MINIMENU_OPOBJ3 = 617,
    REVCONFIG_MINIMENU_OPOBJ4 = 224,
    REVCONFIG_MINIMENU_OPOBJ5 = 662,
    REVCONFIG_MINIMENU_OPOBJ6 = 1152,
    /* "Use <held item> with <target>" (reference USEHELD_ON*). */
    REVCONFIG_MINIMENU_USEHELD_ONLOC = 810,
    REVCONFIG_MINIMENU_USEHELD_ONNPC = 829,
    REVCONFIG_MINIMENU_USEHELD_ONOBJ = 111,
    REVCONFIG_MINIMENU_USEHELD_ONPLAYER = 275,
    REVCONFIG_MINIMENU_USEHELD_ONHELD = 398,
    /* "<spell verb> <target>" — cast a selected spell/prayer on a target
     * (reference TGT_*). TGT_BUTTON arms the target mode from a spell button. */
    REVCONFIG_MINIMENU_TGT_BUTTON = 274,
    REVCONFIG_MINIMENU_TGT_LOC = 899,
    REVCONFIG_MINIMENU_TGT_NPC = 240,
    REVCONFIG_MINIMENU_TGT_OBJ = 370,
    REVCONFIG_MINIMENU_TGT_PLAYER = 131,
    REVCONFIG_MINIMENU_TGT_HELD = 563,
};

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
    RCFIELD_CACHE_FONT_NAME,
    RCFIELD_CACHE_FONT_ID,
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
    RCFIELD_UICOMPONENT_MMB_ROTATE,
    RCFIELD_UICOMPONENT_WHEEL_ZOOM,
    RCFIELD_UICOMPONENT_HOTKEY,
    RCFIELD_HOTKEY_COMPONENT,
    RCFIELD_HOTKEY_EFFECT,
    RCFIELD_UICOMPONENT_COLOR,
    RCFIELD_UICOMPONENT_FILLED,
    RCFIELD_UICOMPONENT_FONT,
    RCFIELD_UICOMPONENT_CENTER,
    RCFIELD_UICOMPONENT_SHADOWED,
    RCFIELD_UICOMPONENT_TEXT,
    RCFIELD_UICOMPONENT_OPTION,
    RCFIELD_UICOMPONENT_OPTION_ACTION,
    RCFIELD_UICOMPONENT_OP0,
    RCFIELD_UICOMPONENT_OP1,
    RCFIELD_UICOMPONENT_OP2,
    RCFIELD_UICOMPONENT_OP3,
    RCFIELD_UICOMPONENT_OP4,
    RCFIELD_UICOMPONENT_OP0_ACTION,
    RCFIELD_UICOMPONENT_OP1_ACTION,
    RCFIELD_UICOMPONENT_OP2_ACTION,
    RCFIELD_UICOMPONENT_OP3_ACTION,
    RCFIELD_UICOMPONENT_OP4_ACTION,
    RCFIELD_UICOMPONENT_BUTTON_TYPE,
    RCFIELD_UICOMPONENT_CLIENT_CODE,
    RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE,
    RCFIELD_UICOMPONENT_CHAT_OP_REPORT_ABUSE_ACTION,
    RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE,
    RCFIELD_UICOMPONENT_CHAT_OP_ADD_IGNORE_ACTION,
    RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND,
    RCFIELD_UICOMPONENT_CHAT_OP_ADD_FRIEND_ACTION,
    RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE,
    RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_TRADE_ACTION,
    RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL,
    RCFIELD_UICOMPONENT_CHAT_OP_ACCEPT_DUEL_ACTION,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_FILTER,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_LABEL_Y,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE_Y,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE0_COLOR,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE1_COLOR,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE2_COLOR,
    RCFIELD_UICOMPONENT_CHAT_BUTTON_MODE3_COLOR,
    RCFIELD_UICOMPONENT_SELECTED,
    RCFIELD_UICOMPONENT_SLOT,
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
    RCITEM_CACHE_FONT,
    RCITEM_UICOMPONENT,
    RCITEM_UILAYOUT,
    RCITEM_INV,
    RCITEM_HOTKEY,
};

/*
 * Cache sprite binding from a [sprite:name] revconfig INI section (*_cache.ini).
 * Loaded by Task_InstanceOnRCCacheSprite; registered in ui_sprite_lookup by name.
 * Referenced from UI components via RevConfigUIComponentItem.sprite.
 *
 * Dat1 decode uses index_filename, data_filename, and format (media jagfile).
 * Dat2 decode requires archive_id (sprites table). table/archive/container are
 * documentary in src2; see ToriAuxLibCache_SpriteNewFromDat1RevConfigItem /
 * ToriAuxLibCache_SpriteNewFromDat2Archive and dat1_buildcache_sprite_decode /
 * dat2_buildcache_sprite_decode_from_archive.
 */
struct RevConfigCacheItem
{
    /* [sprite:<name>] — unique key; component sprite= and ui_sprite_lookup lookup. */
    char name[64];

    /*
     * INI: table=
     * Cache table name (e.g. configs for Dat1, sprites for Dat2). Parsed and stored;
     * not used by src2 decode (legacy IO loaders resolved table+archive to ids).
     */
    char table[64];

    /*
     * INI: archive=
     * Dat1 jagfile archive within configs (typically media). Metadata only in src2.
     */
    char archive[64];

    /*
     * INI: container=
     * Container type (typically jagfile); implies paired index + filename. Metadata only in src2.
     */
    char container[64];

    /* INI: index= — Dat1: sprite index file inside the jagfile (usually index.dat). */
    char index_filename[64];

    /* INI: filename= — Dat1: pixel data file inside the jagfile (e.g. invback.dat). */
    char data_filename[64];

    /* INI: format= — Dat1: pix8 (indexed) or pix32 (ARGB). Ignored by Dat2 decode. */
    char format[16];

    /*
     * INI: atlas_index=
     * Frame index when atlas_count is 0. Ignored when atlas_count > 0 (decode starts at 0).
     */
    int atlas_index;

    /*
     * INI: atlas_count=
     * When > 0, load this many consecutive frames (0 .. count-1) as a multi-frame scene
     * element (e.g. mapscene, sideicons). When 0, load one frame at atlas_index.
     */
    int atlas_count;

    /* INI: crop_x= / crop_y= / crop_width= / crop_height= — sub-rect after decode; both dims must
     * be > 0. */
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;

    /*
     * INI: transform1= … transform4=
     * Post-decode transforms applied in order (up to 4). Supported: flip_h, flip_v.
     */
    char transform[4][64];

    /* Number of entries in transform[]; set when transformN= keys are parsed. */
    int transform_count;

    /*
     * INI: archive_id=  (default -1 when section opens)
     * Dat2 sprites-table archive id; required for Dat2 load. Unused by Dat1 jagfile decode.
     */
    int archive_id;
};

/*
 * Cache font binding from a [font:name] revconfig INI section (*_cache.ini).
 *
 * Why cache_font_id exists:
 * RS interface TEXT widgets in the cache do not store font file names or runtime scene
 * IDs. Dat1 components store a single-byte font slot (0–3). Dat2 components store
 * textFont as a fonts-table archive_id. Revconfig loads fonts by symbolic name
 * ([font:b12]); cache_font_id pins the decoded font into ToriDraw_Scene at that
 * fixed slot so UI/render code can use scene.cache_fonts[id]. archive_id bridges
 * dat2 cache textFont values to the loaded font at bake time.
 *
 * Standard OSRS convention (configurable per revision via INI):
 *   0 = p11, 1 = p12, 2 = b12, 3 = q8
 *
 * Dat1: font_name= (e.g. b12) loads from title jagfile via dat1_buildcache_font_decode.
 * Dat2: archive_id= loads from Fonts table via TAPIDat2_FetchFont.
 */
struct RevConfigFontItem
{
    /* [font:<name>] — unique key; ui_font_lookup and component font= reference this. */
    char name[64];

    /*
     * INI: table=
     * Cache table name (e.g. fonts for Dat2). Metadata; Dat1 decode uses title jagfile.
     */
    char table[64];

    /* INI: archive= — Dat1 jagfile archive within configs; metadata only in src2. */
    char archive[64];

    /*
     * INI: font_name=
     * Dat1 jagfile font stem without .dat (e.g. b12, p11). Defaults to section name.
     */
    char font_name[64];

    /*
     * INI: archive_id=  (default -1 when section opens)
     * Dat2 fonts-table archive id; required for Dat2 load. Also used to resolve dat2
     * interface textFont values at RS subtree bake time.
     */
    int archive_id;

    /*
     * INI: cache_font_id=  (default -1 when section opens)
     * Fixed ToriDraw_Scene slot 0–3 for always-resident cache fonts. RS dat1 TEXT
     * components carry this slot on COMPONENT_TYPE_TEXT; dat2 uses archive_id to
     * resolve to the same slot at bake time.
     */
    int cache_font_id;
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
     * Builtin: compass, minimap, world, sidebar, chat, chat_button, sprite, redstone_tab, tab_icon.
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
     * Default draw pivot when the layout entry omits anchor_x=/anchor_y= (same
     * precedence model as w=/h=).
     */
    int anchor_x;
    int anchor_y;

    /*
     * INI: tabno=
     * Tab index for sidebar, redstone_tab, and tab_icon owners.
     */
    int tabno;

    /*
     * INI: selected= (true/1)
     * type=sidebar: this tab is the boot-time selection (reference sideTab
     * default 3; kept in the INI so C code never hardcodes a tab number).
     */
    int selected;

    /*
     * INI: slot=main_modal|main_overlay|side_modal|chat|tut
     * Marks this chrome node as a runtime interface mount region (reference
     * mainModalId/sideModalId/chatComId surfaces). Empty = not a slot.
     */
    char slot[24];

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

    /*
     * INI: mmb_rotate= (true/1 or false/0; default true when the section opens)
     * type=world: holding the MIDDLE mouse button inside the viewport and
     * dragging rotates the camera (yaw from horizontal travel, pitch from
     * vertical). Revisions that need the middle button for something else set
     * this false.
     */
    int mmb_rotate;

    /*
     * INI: wheel_zoom= (true/1 or false/0; default true when the section opens)
     * type=world: the mouse wheel over the viewport zooms the camera in/out
     * (orbit distance when following the player, dolly along the view axis for
     * the free camera).
     */
    int wheel_zoom;

    /*
     * INI: hotkey=  (repeatable, up to REVCONFIG_COMPONENT_HOTKEY_MAX)
     * Names a hard-coded chrome effect this component accepts from a bound key
     * (see enum UITreeHotkeyEffect; "select_tab" is currently the only one).
     * Listing an effect does not bind anything on its own — a [hotkey:<key>]
     * section supplies the key. This list is the allow-list those bindings are
     * checked against, so a component can never be driven by an effect it does
     * not opt into.
     */
    char hotkeys[REVCONFIG_COMPONENT_HOTKEY_MAX][64];
    int hotkey_count;

    /* INI: color= — RGB/text/line colour for rs_text, rs_rect, rs_line. */
    int color;

    /*
     * INI: filled= (true/1 or false/0)
     * type=rs_rect: filled rectangle. type=rs_line: treated as horizontal flag in
     * the layout builder (no dedicated INI key yet).
     */
    int filled;

    /* INI: font= — RS font id 0–3 for type=rs_text, or symbolic [font:…] name for minimenu. */
    int font;
    /** When font= is non-numeric, resolved via ui_font_lookup. */
    char font_ref[64];
    uint8_t has_font_ref;

    /* INI: center= — horizontally centred text for type=rs_text. */
    int center;

    /* INI: shadowed= — text shadow for type=rs_text. */
    int shadowed;

    /* INI: text= — literal string for static type=rs_text owners (not cache-backed). */
    char text[256];

    /* INI: option= / op0=..op4= — minimenu row labels for static/builtin owners. */
    char option[REVCONFIG_MENU_OPTION_LEN];
    char ops[REVCONFIG_MENU_OPTION_SLOTS][REVCONFIG_MENU_OPTION_LEN];

    /*
     * INI: option_action= / op0_action=..op4_action=
     * Symbolic MiniMenuAction name or numeric value. 0 = use default mapping at click time.
     */
    int option_action;
    int op_actions[REVCONFIG_MENU_OPTION_SLOTS];

    /* INI: button_type= / client_code= — static behavior when not RS-baked. */
    int button_type;
    int client_code;

    /* INI: chat_op_* — dynamic chat minimenu templates on type=chat (%%s = sender). */
    char chat_op_report_abuse[REVCONFIG_CHAT_OP_TEMPLATE_LEN];
    int chat_op_report_abuse_action;
    char chat_op_add_ignore[REVCONFIG_CHAT_OP_TEMPLATE_LEN];
    int chat_op_add_ignore_action;
    char chat_op_add_friend[REVCONFIG_CHAT_OP_TEMPLATE_LEN];
    int chat_op_add_friend_action;
    char chat_op_accept_trade[REVCONFIG_CHAT_OP_TEMPLATE_LEN];
    int chat_op_accept_trade_action;
    char chat_op_accept_duel[REVCONFIG_CHAT_OP_TEMPLATE_LEN];
    int chat_op_accept_duel_action;

    /* INI: type=chat_button — privacy bar below chatback (filter, label, mode0..3). */
    int chat_button_filter;
    char chat_button_label[64];
    int chat_button_label_y;
    int chat_button_mode_y;
    char chat_button_mode_label[4][16];
    int chat_button_mode_color[4];
};

/*
 * UI placement from a [layout:group] revconfig INI section (*_ui.ini).
 * One section may contain multiple entries separated by bare '=' lines.
 * Consumed by instance_revconfig_build_tree → UINodeSpec.position on the UITree.
 * Widget behaviour comes from RevConfigUIComponentItem referenced by component (c=).
 */
struct RevConfigUILayoutItem
{
    /*
     * INI: n= / name=
     * Layout-entry id for parent linking (parent/p= references this, not component name).
     */
    char name[64];

    /*
     * INI: [layout:<group>] section header (and re-applied on each '=' separator).
     * Filtered at build time against InstanceRevConfigContext.layout_group (default "fixed").
     */
    char layout_group[32];

    /* INI: c= — required; name of a [component:…] section. */
    char component[64];

    /*
     * INI: p= / parent=
     * Parent layout entry name; empty = UITree root. Resolved via layout_node_index[].
     */
    char parent[64];

    /* INI: x= / y= — absolute offset from parent origin; used when no edge insets are set. */
    int x;
    int y;

    /*
     * INI: w= / h=
     * Size override; when <= 0, falls back to linked component width/height.
     */
    int width;
    int height;

    /*
     * INI: anchor_x= / anchor_y=
     * Draw pivot inside the layout rect (sprite dst anchor); not used for box placement.
     */
    int anchor_x;
    int anchor_y;

    /* Set when either anchor key is parsed; anchors copied to position only if true. */
    uint8_t has_anchor;

    /*
     * INI: top= / left= / bottom= / right=
     * Edge insets for UIPOS_RELATIVE. If any is non-zero, x/y are ignored for placement.
     */
    int top;
    int left;
    int bottom;
    int right;

    /* INI: dirty — presence flag; maps to UINodeSpec.always_dirty (redraw every frame). */
    int dirty;
};

#define REVCONFIG_INV_MAX_ITEMS 32

struct RevConfigInvItem
{
    char name[64];
    char items[REVCONFIG_INV_MAX_ITEMS][64];
    int item_count;
};

/*
 * One key binding from a [hotkey:<key>] revconfig INI section.
 *
 * The section name is the key ("f1", "3", "escape" — see
 * LibToriRS_OsrsKeyFromName), and the pair (component, effect) says what the
 * press does. The effect must also appear in that component's hotkey= list, or
 * the binding is dropped when the tree is baked.
 */
struct RevConfigHotkeyItem
{
    /* [hotkey:<name>] — the key this binds. Not unique: two keys may drive the
     * same component + effect, and one key may appear once per component. */
    char name[64];

    /* INI: c= — name of the [component:…] section the effect runs on. */
    char component[64];

    /* INI: e= — hard-coded effect name (enum UITreeHotkeyEffect spelling). */
    char effect[64];
};

struct RevConfigItem
{
    enum RevConfigItemKind kind;
    union
    {
        struct RevConfigCacheItem cache;
        struct RevConfigFontItem font;
        struct RevConfigUIComponentItem uicomponent;
        struct RevConfigUILayoutItem uilayout;
        struct RevConfigInvItem inv;
        struct RevConfigHotkeyItem hotkey;
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

/** Parse symbolic MiniMenuAction name or decimal string. Returns 0 if unknown/empty. */
int
revconfig_parse_minimenu_action(char const* str);

/** Parse button_type= string (ok/toggle/select/close/continue/target) or integer. */
int
revconfig_parse_button_type(char const* str);

#endif