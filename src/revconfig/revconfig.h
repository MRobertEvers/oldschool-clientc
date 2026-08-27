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
    REVCONFIG_MINIMENU_OPPLAYER1 = 639,
    REVCONFIG_MINIMENU_OPPLAYER2 = 499,
    REVCONFIG_MINIMENU_OPPLAYER3 = 27,
    REVCONFIG_MINIMENU_OPPLAYER4 = 387,
    REVCONFIG_MINIMENU_OPPLAYER5 = 185,
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

    /*
     * A row the CLIENT invents, in the client's own action band.
     *
     * Every id above is the reference's, and every one of them is a message to
     * a server or a cache script. This one is neither: it opens the plugin
     * window, which is this client's own furniture, and there is nothing at
     * the other end of it to send.
     *
     * The band matters as much as the number. Ids below 1000 carry the
     * reference's +2000 priority bias and get it stripped again at dispatch,
     * so a client-invented id parked among them would arrive somewhere else;
     * UITREE_MINIMENU_ACTION_CLIENT_BASE is the range that is exempt from
     * both. Restated as a literal rather than derived because this header is
     * the leaf that ui/ and game/ both build on -- the static assertion in
     * game/rs_minimenu_build.h is what holds the two in step.
     */
    REVCONFIG_MINIMENU_PLUGIN_PANEL = 500005,
    /**
     * Set one chat filter to one mode. Pick: id = the button's component id,
     * secondary = the filter, tertiary = the mode.
     *
     * A CLIENT id and not a reference one, because the reference has no such
     * row: its privacy buttons only cycle, and the menu that names a mode
     * outright is this client's answer to a frame where the left click belongs
     * to something else -- the modern layouts give it to the chatbox switch.
     */
    REVCONFIG_MINIMENU_CHAT_FILTER = 500007,
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
    RCFIELD_CACHE_DEFAULTS_SLOT,
    RCFIELD_CACHE_GROUP,
    RCFIELD_CACHEREF_ID,
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
    RCFIELD_UICOMPONENT_HOTKEY,
    RCFIELD_HOTKEY_COMPONENT,
    RCFIELD_HOTKEY_EFFECT,
    RCFIELD_UICOMPONENT_COLOR,
    RCFIELD_UICOMPONENT_FILLED,
    RCFIELD_UICOMPONENT_TILED,
    RCFIELD_UICOMPONENT_MMB_ROTATE,
    RCFIELD_UICOMPONENT_WHEEL_ZOOM,
    RCFIELD_UICOMPONENT_FONT,
    RCFIELD_UICOMPONENT_CENTER,
    RCFIELD_UICOMPONENT_VALIGN,
    RCFIELD_UICOMPONENT_OVER_COLOR,
    RCFIELD_UICOMPONENT_SHADOWED,
    RCFIELD_UICOMPONENT_TEXT,
    RCFIELD_UICOMPONENT_TITLE_FIELD,
    RCFIELD_UICOMPONENT_TITLE_PREFIX,
    RCFIELD_UICOMPONENT_TITLE_CARET,
    RCFIELD_UICOMPONENT_TITLE_CARET_BLINK,
    RCFIELD_UICOMPONENT_TITLE_MASK,
    RCFIELD_UICOMPONENT_TITLE_MAXLEN,
    RCFIELD_UICOMPONENT_TITLE_CHARSET,
    RCFIELD_UICOMPONENT_TITLE_ACTION,
    RCFIELD_UICOMPONENT_TITLE_MESSAGE_INDEX,
    RCFIELD_UICOMPONENT_TITLE_PX_PER_PERCENT,
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
    RCFIELD_FEATURES_ERA,
    RCFIELD_FEATURES_GROUND_CLICK_NEAREST,
    RCFIELD_FEATURES_GROUND_CLICK_UNBOUNDED,
    RCFIELD_FEATURES_GROUND_CLICK_OFFMAP,
    RCFIELD_FEATURES_MOVER,
    RCFIELD_FEATURES_PAINTER_DRAW_DISTANCE,
    RCFIELD_CAMERA_ZOOM,
    RCFIELD_CAMERA_CONTROLS,
    RCFIELD_CAMERA_WHEEL_STEP,
    RCFIELD_CHROME_PLUGIN_IFACE,
    RCFIELD_CHROME_PLUGIN_BUTTON_PARENT,
    RCFIELD_CHROME_PLUGIN_PANEL_PARENT,
    RCFIELD_CHROME_PLUGIN_BUTTON_SLOT,
    RCFIELD_CHROME_PLUGIN_BUTTON_SIZE,
    RCFIELD_CHROME_PLUGIN_BUTTON_PITCH,
    RCFIELD_CHROME_PLUGIN_BUTTON_OP,
    RCFIELD_CHROME_PLUGIN_LAYOUT_SCRIPT,
    RCFIELD_ROLE_MATCH,
    RCFIELD_UICOMPONENT_ROLE,
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
    RCITEM_CACHE_REF,
    RCITEM_FEATURES,
    RCITEM_CAMERA,
    RCITEM_CHROME,
    RCITEM_ROLE,
};

/** Section types that build an RCITEM_CACHE_REF, i.e. a bare name -> cache id. */
#define REVCONFIG_CACHEREF_KINDS "script", "iface", "varbit", "varp", "seq", "setting"

/*
 * One `[<kind>:<name>] id=<n>` binding — a cache id the CLIENT has to know by
 * number, given a name here instead of a literal in C.
 *
 * The client legitimately knows what a thing is FOR: that the settings panel
 * has an apply script, that the XP counter lives on some interface, that an
 * unrigged preview model needs the human ready animation. What it must not
 * know is WHICH id that is, because the answer moves every revision and a
 * literal in C is a silent wrong answer on every other cache.
 *
 * An undeclared name resolves to -1, and -1 means "this revision does not have
 * that thing" — not "use the built-in default". There is no built-in default;
 * a caller that gets -1 turns the feature off. That is why the rev-254 profile
 * can omit the tile-highlight scripts (which did not exist yet) without
 * pretending rev-239's ids apply to it.
 */
struct RevConfigCacheRefItem
{
    /* [<kind>:<name>] — the section type, one of REVCONFIG_CACHEREF_KINDS. */
    char kind[16];

    /* [<kind>:<name>] — the symbolic name C looks up. Unique within a kind. */
    char name[64];

    /* INI: id= — the cache id. -1 when the section opens, so a section that
     * declares no id= is indistinguishable from an absent one. */
    int id;
};

/** How one `match=` line in a [role:…] names the node it is looking for. */
enum RevConfigRoleMatchKind
{
    /* No form stated -- an empty matcher slot. */
    REVCONFIG_ROLE_MATCH_NONE,

    /* slot(<region>[, <member>]) — hand off to the frame-slot resolver, which
     * already answers the placeable regions on both a revconfig frame and a
     * cache one. The member is the role's OWN numbering (a chat button's
     * filter, a sidebar mount's tabno), never a position in a list. */
    REVCONFIG_ROLE_MATCH_SLOT,

    /* id(<expr>) — a uid, stated outright. Flat on dat1, `if(group, child)`
     * on dat2; the expression parser spells both. */
    REVCONFIG_ROLE_MATCH_ID,

    /* iface(<name>[, <child>]) — the [iface:<name>] group's `child`
     * (0 = the group root). A group this world has not mounted simply does
     * not match, which is what lets one chain carry a rung per toplevel. */
    REVCONFIG_ROLE_MATCH_IFACE,

    /* clientcode(<expr>) — the cache's own semantic tag. */
    REVCONFIG_ROLE_MATCH_CLIENTCODE,

    /* cc(<anchor>, <sub_id>) — a CS2-created child of `anchor`, by the sub id
     * the script named it with. The ONLY stable way to address a dynamic
     * node: its component_id is a rotating handle that a delete-all and
     * rebuild hands straight back out again. */
    REVCONFIG_ROLE_MATCH_CC,
};

/*
 * One component named inside a matcher: the whole of an `id()`/`iface()` line,
 * or the anchor half of a `cc()` one.
 */
struct RevConfigRoleRef
{
    /* REVCONFIG_ROLE_MATCH_ID, _IFACE, or _NONE for "no reference stated". */
    enum RevConfigRoleMatchKind kind;

    /* _IFACE: the [iface:<name>] section id, verbatim -- this module is a leaf
     * and does not resolve it. Empty for _ID. */
    char name[48];

    /* _ID: the uid. _IFACE: the child within the group, 0 when unstated. */
    int value;
};

/*
 * One rung of a role's matcher chain.
 *
 * Rungs are tried in declaration order and the first that resolves against the
 * LIVE tree wins, so a profile states its alternates -- one per toplevel, or a
 * cache tag with a hardcoded uid behind it -- rather than having to know which
 * one this world booted.
 */
struct RevConfigRoleMatcher
{
    /* Which form the line used. */
    enum RevConfigRoleMatchKind kind;

    /* _SLOT: the region name and the member, verbatim ("chat_buttons",
     * "report"). An empty member means the region itself. */
    char slot[32];
    char member[24];

    /* _CLIENTCODE: the code. _CC: the dynamic sub id. -1 otherwise. */
    int value;

    /* _ID and _IFACE: the node itself. _CC: its parent. _NONE otherwise. */
    struct RevConfigRoleRef ref;
};

/** How many rungs one chain may carry. */
#define REVCONFIG_ROLE_MAX_MATCHERS 8

/*
 * One `[role:<name>]` section — a semantic name for an interface element,
 * bound to whatever this revision happens to have put it in.
 *
 * The same argument as [iface:…] one level further down. There, the client
 * knows the settings panel HAS an apply script and the profile knows its id.
 * Here, the client knows a world has a report button and a logout screen, and
 * the profile knows which node that is -- a chat-button member on a 2004
 * frame, a component of some toplevel on an OldSchool one, and on a CS2 lane
 * possibly a node no cache record describes at all because a script built it.
 *
 * A role no profile declares does not resolve, and that is an ANSWER: the
 * plugin asking offers no verb rather than pressing something at random.
 */
struct RevConfigRoleItem
{
    /* [role:<name>] — the symbolic name a plugin asks for. */
    char name[64];

    /* INI: match= — the chain, in declaration order. */
    struct RevConfigRoleMatcher matchers[REVCONFIG_ROLE_MAX_MATCHERS];
    int matcher_count;
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
     * INI: group=
     * Which [layout:<group>] builds need this sprite. Empty (the default, and
     * what every pre-existing section has) means every build loads it. A named
     * group means only a build selecting that group does -- the title screen's
     * art must not ride along in the gameframe's atlas, and vice versa.
     * @see UIBuilderManifestSources::layout_group.
     */
    char group[32];

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

    /*
     * INI: slot=  (default -1 when section opens)
     *
     * Position in the defaults record, for `table=defaults`. This is how the
     * client itself reaches these sprites: index 17 group 3 stores eleven ids
     * positionally and the engine loads each by id, so a slot is an address and
     * not a label. `table=sprites` with `archive=` is the other path — hash the
     * name, walk index 8 — and stays supported for caches with no defaults
     * table, dat1 among them.
     *
     * The two agree at rev239 (slot 0 and `archive=compass` both resolve to
     * sprite 169) and would part company the moment a revision repointed a
     * slot, because only the record says which sprite is the compass.
     */
    int defaults_slot;
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

    /* INI: group= — as RevConfigCacheItem::group; empty means every build. */
    char group[32];

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
     * Builtin: compass, cross, entity_overlay, hovertext, minimenu, minimap,
     * world, sidebar, chat, chat_button, sprite, redstone_tab, tab_icon.
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
     * INI: role=
     * A semantic name for this node, stamped onto the live component so a
     * plugin can ask for it by what it IS. The direct channel, for the nodes
     * this profile authored itself; a cache-owned or script-built node is
     * named the other way round, by a [role:<name>] matcher chain. Empty =
     * this node carries no role.
     */
    char role[64];

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

    /*
     * INI: tiled= (true/1 or false/0)
     * type=rs_graphic: repeat the sprite across the widget's box instead of
     * blitting it once. The interfaces' own wide buttons are authored this way
     * -- two fixed caps and a narrow tile stretched between them -- and a
     * client-owned control drawn with that art needs the same, or it gets one
     * tile and a gap.
     */
    int tiled;

    /* INI: font= — RS font id 0–3 for type=rs_text, or symbolic [font:…] name
     * for hovertext/minimenu. */
    int font;
    /** When font= is non-numeric, resolved via ui_font_lookup. */
    char font_ref[64];
    uint8_t has_font_ref;

    /* INI: center= — horizontally centred text for type=rs_text. */
    int center;

    /*
     * INI: valign= — 0 top, 1 centre, 2 bottom (the interfaces' own `valign`).
     * type=rs_text: where the line sits inside the widget's box.
     *
     * Centring is what a BUTTON caption wants, and it wants it against the
     * whole plate rather than against a line-height box the author positioned
     * by hand: ascent and descent differ per face, so a hand-placed 13px box
     * that looks centred in one font sits low in the next. The cache's own
     * button captions are authored the same way -- full height, `valign=1`.
     */
    int valign;

    /*
     * INI: over_color= — RGB the text/rect takes while the pointer is on it.
     * 0 (the default) means "no hover colour", matching the reference's own
     * test: a component with colourOver 0 keeps its ordinary colour.
     *
     * The cache authors this as a pair of mouseover/mouseleave scripts; a
     * client-owned control has no scripts, so it states the colour directly
     * and the emit does the same swap.
     */
    int over_color;

    /* INI: shadowed= — text shadow for type=rs_text. */
    int shadowed;

    /* INI: text= — literal string for static type=rs_text owners (not cache-backed). */
    char text[256];

    /*
     * Title-screen widgets (type=login_input / login_button / login_message /
     * title_progress*). The title screen is not a cache interface -- no
     * revision ships one as widget data -- so it is built from client widgets
     * whose every appearance decision is stated here rather than in C.
     *
     * INI: field= — which credential a login_input edits: username | password.
     */
    char title_field[16];

    /*
     * INI: prefix=
     * Drawn before the value on the same line, because the reference draws the
     * label and the value as ONE string ("Username: bob") and centring or
     * measuring them separately would not reproduce it.
     */
    char title_prefix[32];

    /*
     * INI: caret=
     * What a focused field appends while the caret is visible. The two lanes
     * spell the same idea differently -- "@yel@|" on dat1, "<col=ffff00>|" on
     * dat2 -- because it is the era's own font-markup dialect, so it is a
     * string here and not a colour plus a flag.
     */
    char title_caret[24];

    /*
     * INI: caret_blink=  (default 0 = never blink)
     * Blink period in client cycles; the caret shows for the first half. Both
     * references use 40, and both write it as a bare constant.
     */
    int title_caret_blink;

    /*
     * INI: mask=
     * Character a login_input shows instead of its value. Empty means show the
     * text. Only the first character is used.
     */
    char title_mask[8];

    /* INI: maxlen= — characters the field accepts. @see RS_TitleFieldCfg. */
    int title_maxlen;

    /*
     * INI: charset=
     * Characters the field accepts; empty accepts anything printable. The old
     * lane states the reference's 94-character set, because a glyph the
     * revision's font lacks is one the player cannot see.
     */
    char title_charset[160];

    /*
     * INI: action=
     * What a login_button does: existing_user | new_user | login | cancel |
     * focus_username | focus_password. A name rather than a screen number, so
     * the INI states an intent. @see RS_Title_ActionFromName.
     */
    char title_action[32];

    /* INI: index= — which of the three login message lines a login_message
     * draws (0-2). */
    int title_message_index;

    /*
     * INI: px_per_percent=  (default 0 = fill the declared width at 100)
     * Bar pixels per percent for title_progress. Both references write the fill
     * as `percent * 3` over a 300-wide track, and stating the scale keeps a
     * revision that sizes its bar differently from needing new C.
     */
    int title_px_per_percent;

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
     * Filtered at build time against UIBuilderManifestSources::layout_group and
     * ::layout_group_exclude -- a builder that selects no group takes every
     * layout, which is what every gameframe-only profile relies on.
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

/*
 * `[features]` — the per-era CLIENT BEHAVIOUR table, stated by the revision
 * profile instead of by the boot manifest.
 *
 * Every value here is a name or a number as written in the INI, never a
 * resolved enum: this module is a leaf and does not include
 * src/features/features.h, so the spellings ("lostcity", "box10_rect",
 * "frame") are handed on verbatim and resolved by the App, which already owns
 * ToriRS_Features_ByName and its siblings. A typo therefore names itself at
 * the one place that can say what the legal spellings are.
 *
 * Why the revconfig and not the manifest: which pathing model, which mover,
 * which unreachable-click fallback a client runs is a fact about the REVISION,
 * exactly like which id the settings script has — and a revision profile is
 * shared by every world that boots it (rs245_2lc's file serves 254, 289 and
 * 377), so stating it once here is what keeps three manifests from drifting.
 *
 * A manifest `[features:boot]` still wins, and has to: `era=server_routed` is
 * a property of the SERVER, not the cache, so manifest_osrs233xrsps.ini states
 * it over a rev-233 cache whose own profile would say `osrs`.
 *
 * Unstated is a real state, distinct from every value: the sentinels below are
 * what the App tests before touching its copy of the era table.
 */
struct RevConfigFeaturesItem
{
    /* INI: era= — "lostcity" | "osrs" | "server_routed". "" = not stated, i.e.
     * derive the era from the cache identity (ToriRS_Features_ForCache). */
    char era[32];

    /* INI: ground_click_nearest= — "ring3" | "box10_rect" | "none"
     * (enum ToriRS_NearestModel). "" = not stated. */
    char ground_click_nearest[32];

    /* INI: mover= — "cycle" | "frame" (enum ToriRS_MoverModel). "" = not
     * stated. */
    char mover[32];

    /* INI: ground_click_unbounded= / ground_click_offmap= — the two permissive
     * ground-click extensions. 0/1; -1 = not stated. */
    int ground_click_unbounded;
    int ground_click_offmap;

    /* INI: painter_draw_distance= — painter radius in tiles (the official
     * 25..90 band). 0 = not stated. */
    int painter_draw_distance;
};

/** enum for RevConfigCameraItem.zoom_mode. */
enum RevConfigCameraZoomMode
{
    /**
     * `zoom=clamped:[min,max]` — the eye height is a live value the wheel
     * moves, bounded by min..max. This is the client's own gesture and no
     * revision's behaviour; it is the default because it is what this tree
     * already did everywhere.
     */
    REVCONFIG_CAMERA_ZOOM_CLAMPED = 0,
    /**
     * `zoom=fixed:<height>` — the eye height is that number and nothing moves
     * it: no wheel, and no viewport-height interpolation either. `fixed:600`
     * is Client-TS exactly (`camFollow(..., pitch * 3 + 600)`), which is why
     * every pre-HD revision states it.
     */
    REVCONFIG_CAMERA_ZOOM_FIXED = 1,
};

/** Bits for RevConfigCameraItem.controls, from the `controls=` name list. */
enum
{
    /** `arrow_keys` — the reference's keyHeld[1..4] orbit (Client-TS
     *  updateOrbitCamera). Every revision has this; it is the default. */
    REVCONFIG_CAMERA_CONTROL_ARROW_KEYS = 1 << 0,
    /** `mmb` — middle-button drag rotates the camera. No revision has it;
     *  it is this client's own gesture. */
    REVCONFIG_CAMERA_CONTROL_MMB = 1 << 1,
};

/** `zoom=` when no `[camera]` section states one: the wheel band this tree
 *  shipped with, expressed as eye heights around the reference's 600. */
#define REVCONFIG_CAMERA_ZOOM_DEFAULT_MIN 240
#define REVCONFIG_CAMERA_ZOOM_DEFAULT_MAX 2160
/** The reference eye height, and this client's zoom rest position. */
#define REVCONFIG_CAMERA_ZOOM_DEFAULT_HEIGHT 600
/** `wheel_step=` when no `[camera]` section states one: one notch moves the eye
 *  height by a tenth of the reference 600, which is what this tree's old
 *  percentage step came to. */
#define REVCONFIG_CAMERA_WHEEL_STEP_DEFAULT 60

/*
 * `[camera]` — what the world camera lets the player do.
 *
 * The follow camera places the eye `pitch * 3 + height` behind the player
 * (Client-TS camFollow), and `height` is the whole of "zoom": the 2004 client
 * has no way to change it, so its camera is `fixed:600` and the wheel does
 * nothing. Later clients interpolate it over the viewport and this one adds a
 * wheel, which is what `clamped:[min,max]` describes.
 *
 * Every key replaces what it states outright rather than merging, so a profile
 * that says `controls=arrow_keys` has turned the middle button OFF — it has
 * not merely declined to mention it.
 */
struct RevConfigCameraItem
{
    /* INI: zoom= — enum RevConfigCameraZoomMode. */
    int zoom_mode;
    /* INI: zoom=fixed:<height> — the pinned eye height. */
    int zoom_height;
    /* INI: zoom=clamped:[min,max] — the band the wheel may reach. */
    int zoom_min;
    int zoom_max;
    /* INI: controls= — REVCONFIG_CAMERA_CONTROL_* bits. */
    int controls;
    /* INI: wheel_step= — eye-height units one wheel notch moves. Only the
     * `clamped:` camera reads it; a `fixed:` band has nowhere to move. */
    int wheel_step;

    /* Which keys this section actually carried, so a later source can override
     * one of them without silently restoring the default for the other. */
    uint8_t has_zoom;
    uint8_t has_controls;
    uint8_t has_wheel_step;
};

/** Longest `[chrome]` name or op text. One field value is 64 bytes, so nothing
 *  longer than that can reach here anyway. */
#define REVCONFIG_CHROME_NAME_LEN 64

/*
 * `[chrome]` -- where the CLIENT's own furniture mounts on this revision.
 *
 * One thing today: the plugin window's launcher button and the strip it lives
 * in. That button is not the cache's -- plugins are a client feature and no
 * server knows about them -- but WHERE it goes is entirely the cache's
 * business: interface 728's button column is child 6 on rev-239 and does not
 * exist at all on a 2004 dat1 cache. Every one of those numbers used to be a
 * literal in torirs_plugin_panel.u.c, which is the same silent-wrong-answer
 * trap `[iface:…]` exists to delete, one level down: the interface id was
 * named by the profile and the children, the slot, the geometry and the layout
 * script were not.
 *
 * Nothing here is defaulted. An absent section, or one that leaves a key out,
 * means "this revision has no strip to mount in" -- the button is not built
 * and the window opens in the canvas instead, which is exactly what a lane
 * with no `[iface:plugin_popout]` already did.
 */
struct RevConfigChromeItem
{
    /* INI: plugin_button_iface= -- the `[iface:<name>]` section naming the
     * interface the strip belongs to. Empty when unstated. */
    char plugin_iface[REVCONFIG_CHROME_NAME_LEN];

    /* INI: plugin_button_parent= -- child component of that interface holding
     * the launcher column the button is appended to. -1 when unstated. */
    int plugin_button_parent;

    /* INI: plugin_panel_parent= -- child component panels mount into, i.e. the
     * one slot the strip shows at a time. -1 when unstated. */
    int plugin_panel_parent;

    /* INI: plugin_button_slot= -- index down the column. The shipped entries
     * take the ones before it. -1 when unstated. */
    int plugin_button_slot;

    /* INI: plugin_button_size= / plugin_button_pitch= -- the column's own icon
     * box and vertical pitch, in interface pixels. -1 when unstated. */
    int plugin_button_size;
    int plugin_button_pitch;

    /* INI: plugin_button_op= -- the hover option the button advertises, e.g.
     * "Show Plugin Settings". Empty means the button names nothing on hover. */
    char plugin_button_op[REVCONFIG_CHROME_NAME_LEN];

    /* INI: plugin_layout_script= -- the `[script:<name>]` binding for the
     * clientscript that lays the strip out, run after the panel takes the slot
     * or gives it back. Empty when the strip needs no such pass. */
    char plugin_layout_script[REVCONFIG_CHROME_NAME_LEN];
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
        struct RevConfigCacheRefItem cacheref;
        struct RevConfigFeaturesItem features;
        struct RevConfigCameraItem camera;
        struct RevConfigChromeItem chrome;
        struct RevConfigRoleItem role;
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

/**
 * Parse the value of a numeric key.
 *
 * The value is an integer expression, not just a decimal run, so a profile can
 * spell an id, a mask or a colour the way the reference does:
 *
 *   hex          0x1088     1088h     0FFh      #FF0000
 *   binary       0b1010_1010
 *   grouping     0x1000_0000          (underscores between digits)
 *   colours      rgb(255, 0, 0)       rgba(0, 0, 0, 128)
 *   palette      hsl16(0, 7, 64)      -- hue, saturation, lightness
 *   uids         if(1088, 255)        -- (interface << 16) | component
 *   arithmetic   (1088 << 16) | 0xFF
 *
 * Operators and their precedence are C's: | ^ & << >> + - * / % and unary
 * + - ~. `#` is a hex marker like `0x`, of no fixed width. `rgb()` packs RGB,
 * `rgba()` packs ARGB -- the word the client blits. `hsl16()` packs the
 * client's own palette index (hue 0..63, saturation 0..7, lightness 0..127),
 * which is the unit a face colour and a text tint are in and which no rgb()
 * can spell. `if()` takes 0..65535 per half, or -1 for the "no component"
 * 0xFFFF.
 *
 * Arithmetic is 64-bit and the result must land in [INT32_MIN, UINT32_MAX];
 * what comes back is its 32-bit pattern, so rgba(255,255,255,255) arrives as
 * -1 rather than as a value an `int` field could not hold.
 *
 * A value that does not parse is REPORTED on stderr and comes back 0. An empty
 * value is 0 with nothing said: an unstated key is not a malformed one.
 */
int
revconfig_parse_int(char const* str);

/**
 * The same grammar, for a value that has more than a number in it -- a zoom
 * band's `[<min>, <max>]`, say.
 *
 * Parses ONE expression off the front of `str`. Returns 1 and writes the value
 * and, when `out_end` is given, the first character not consumed; the caller
 * decides what may follow. Returns 0 without touching either when the text is
 * not an expression or the value does not fit 32 bits, and says nothing --
 * this is the form to probe with.
 */
int
revconfig_parse_int_expr(char const* str, char const** out_end, int* out_value);

/** Parse symbolic MiniMenuAction name or number (@see revconfig_parse_int). Returns 0 if unknown/empty. */
int
revconfig_parse_minimenu_action(char const* str);

/** Parse button_type= string (ok/toggle/select/close/continue/target) or number. */
int
revconfig_parse_button_type(char const* str);

/**
 * Parse a `[camera] zoom=` value into `out`: `fixed:<height>` or
 * `clamped:[<min>,<max>]`. Whitespace inside the brackets is allowed.
 *
 * Returns 1 on success. Returns 0 and leaves `out` untouched otherwise, so a
 * misspelling keeps the default camera rather than pinning the eye at 0 —
 * the caller reports it.
 */
int
revconfig_parse_camera_zoom(char const* str, struct RevConfigCameraItem* out);

/**
 * Parse one `[role:…] match=` line into `out`.
 *
 *   slot(<region>[, <member>])   slot(chat_buttons, report)
 *   id(<expr>)                   id(2449)   id(if(553, 0))
 *   iface(<name>[, <child>])     iface(logout)
 *   clientcode(<expr>)           clientcode(205)
 *   cc(<anchor>, <sub_id>)       cc(iface(xpdrop), 4)
 *
 * where `<anchor>` is an `id()` or an `iface()`, and every `<expr>` is the
 * full integer-expression grammar (@see revconfig_parse_int).
 *
 * Returns 1 on success. Returns 0 and leaves `out` untouched otherwise, having
 * REPORTED the line on stderr: a matcher that does not parse must be loud,
 * because the alternative is a role that silently never resolves and a plugin
 * that quietly offers no verb on every world.
 */
int
revconfig_parse_role_matcher(char const* str, struct RevConfigRoleMatcher* out);

/**
 * Parse a `[camera] controls=` comma-separated name list into a
 * REVCONFIG_CAMERA_CONTROL_* bitmask. `-1` when a name is not one of them.
 *
 * The list is the whole truth, not an addition: an empty value is a camera
 * with no player controls at all, which is a legal thing to want.
 */
int
revconfig_parse_camera_controls(char const* str);

#endif
