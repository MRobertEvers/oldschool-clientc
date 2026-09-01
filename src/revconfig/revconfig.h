#ifndef REVCONFIG_CACHE_H
#define REVCONFIG_CACHE_H

#include <stdint.h>

#define REVCONFIG_MENU_OPTION_SLOTS 5
#define REVCONFIG_MENU_OPTION_LEN 32
#define REVCONFIG_CHAT_OP_TEMPLATE_LEN 64
#define REVCONFIG_CHAT_PROMPT_LEN 64
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
    /* type=inkwell: which artwork, and which colour each outcome uses.
     * @see ui/torirs_chrome_inkwell.h. */
    RCFIELD_UICOMPONENT_INK_STYLE,
    RCFIELD_UICOMPONENT_INK_WALK_COLOR,
    RCFIELD_UICOMPONENT_INK_INTERACT_COLOR,
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
    RCFIELD_UICOMPONENT_FLAME_BIAS,
    RCFIELD_UICOMPONENT_FLAME_SWAY,
    RCFIELD_UICOMPONENT_FLAME_RUN,
    RCFIELD_UICOMPONENT_FLAME_ROW,
    RCFIELD_UICOMPONENT_FLAME_BLUR,
    RCFIELD_UICOMPONENT_TEXT_BASELINE,
    RCFIELD_STRING_TEXT,
    RCFIELD_PRELOAD_KIND,
    RCFIELD_PRELOAD_ARCHIVE,
    RCFIELD_PRELOAD_ID,
    RCFIELD_PRELOAD_PERCENT,
    RCFIELD_PRELOAD_SAY,
    RCFIELD_PRELOAD_WEIGHT,
    RCFIELD_PRELOAD_RENDER,
    RCFIELD_PRELOAD_ORDER,
    RCFIELD_LOGIN_REPLY_SCREEN,
    RCFIELD_LOGIN_REPLY_LINE1,
    RCFIELD_LOGIN_REPLY_LINE2,
    RCFIELD_LOGIN_REPLY_LINE3,
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
    RCFIELD_UICOMPONENT_CHAT_PROMPT,
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
    RCFIELD_UILAYOUT_XALIGN,
    RCFIELD_UILAYOUT_SAFE_AREA,
    RCFIELD_UILAYOUT_SAFE_AREA_MARGIN,
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
    RCFIELD_CHROME_PLUGIN_BUTTON_X,
    RCFIELD_CHROME_PLUGIN_BUTTON_Y,
    RCFIELD_CHROME_PLUGIN_BUTTON_W,
    RCFIELD_CHROME_PLUGIN_BUTTON_H,
    RCFIELD_CHROME_PLUGIN_BUTTON_OP,
    RCFIELD_CHROME_PLUGIN_BUTTON_ANCHOR,
    RCFIELD_CHROME_PLUGIN_BUTTON_ALIGN,
    RCFIELD_CHROME_PLUGIN_BUTTON_MARGIN,
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
    RCITEM_STRING,
    RCITEM_LOGIN_REPLY,
    RCITEM_PRELOAD,
};

/**
 * One `[string:<name>] text=` -- a line of UI prose, named here rather than
 * spelled in C.
 *
 * The client legitimately knows WHEN to say something ("we are connecting
 * now"); what it says, and in which revision's wording, is the profile's.
 */
/**
 * One `[preload:<name>]` -- a single step of the loading screen.
 *
 * WHAT a revision fetches before it can show a title screen is the
 * revision's business, and the two eras disagree about all of it. The 2004
 * client pulls nine jag archives over HTTP in a fixed order and unpacks
 * them one at a time; OldSchool 239 opens eight cache indices at once and
 * watches them complete, weighting each one's contribution to the
 * percentage (sound effects alone are 53% of its bar). Neither list is
 * derivable from the other, and neither belongs in C.
 *
 * `kind` says which machinery loads it, because that IS the client's part:
 * it knows how to pull a jagfile and how to open an index, and the profile
 * says which ones and in what order.
 *
 * `percent`/`say` are what the bar shows while the step runs -- `say` names
 * a [string:] entry, so the words stay the revision's too. `weight` is the
 * deob's model, where the bar is a weighted sum of several concurrent
 * loads rather than a position in a queue; a profile that states no weights
 * gets the 2004 model, where each step simply owns its percent.
 *
 * `render` is the opt-in: a step that sets it publishes a frame before it
 * runs, which is the only way a long fetch shows its own progress. A step
 * that does not sets nothing on screen and costs nothing.
 */
struct RevConfigPreloadItem
{
    char name[64];
    /** INI: kind= -- jagfile | index | ondemand | unpack */
    char kind[24];
    /** INI: archive= -- the jagfile stem or the cache index/table name. */
    char archive[64];
    /** INI: id= -- the numeric index/table, where the era addresses by
     *  number rather than by name. -1 when unstated. */
    int id;
    /** INI: percent= -- the bar position while this step runs. */
    int percent;
    /** INI: say= -- a [string:] name, drawn under the bar. */
    char say[64];
    /** INI: weight= -- share of the bar this step owns, deob-style. */
    int weight;
    /** INI: render= -- publish a frame before running this step. */
    int render;
    /** INI: order= -- ascending; ties keep file order. */
    int order;
};

struct RevConfigStringItem
{
    char name[64];
    char text[256];
};

/**
 * One `[login_reply:<code>]` -- what a login rejection means, in words.
 *
 * The CODE is protocol and belongs to net/; the SENTENCES are presentation and
 * differ per revision, which is exactly the split revconfig exists for: the
 * old lane answers codes 3..21 in two lines, the modern one -3..74 in three.
 *
 * `screen` carries the behavioural half -- some codes land on a dedicated
 * screen rather than the generic error page -- so a revision can say that
 * without new C.
 *
 * The section name is the code, or `default` for anything unlisted, or
 * `connect_failed` for a socket that never reached a server.
 */
#define REVCONFIG_LOGIN_REPLY_DEFAULT_NAME "default"
#define REVCONFIG_LOGIN_REPLY_CONNECT_FAILED_NAME "connect_failed"

/* Out of the protocol's byte range so they cannot collide with a real code.
 * Kept in step with TORIRS_NET_LOGIN_REPLY_CONNECT_FAILED by net/net.h. */
#define REVCONFIG_LOGIN_REPLY_CODE_DEFAULT (-1000)
#define REVCONFIG_LOGIN_REPLY_CODE_CONNECT_FAILED (-100)

struct RevConfigLoginReplyItem
{
    /** The reply byte, or one of the sentinels below. */
    int code;
    /** RS_TitleScreen to land on; -1 leaves the screen alone. */
    int screen;
    char line[3][256];
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

    /* ---- type=inkwell -------------------------------------------------
     *
     * INI: style= (splash|blot|ripple), walk_color= / interact_color=
     * (yellow|red).
     *
     * The colours are configurable rather than fixed because "yellow walks,
     * red interacts" is a REVISION's convention, not a law -- and a world that
     * wants one colour for every touch, or the two swapped, should be able to
     * say so where it already says everything else about its interface. A
     * profile overrides them with a `[component:<name>@mobile]` section.
     *
     * -1 means unstated, so a profile that names only `style=` keeps the
     * revision's colours.
     */
    int ink_style;
    int ink_walk_color;
    int ink_interact_color;
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

    /*
     * INI: baseline=
     * type=rs_text: the layout row's y is the text BASELINE rather than the top
     * of its box. Both references write every text coordinate that way
     * (font.drawString(s, x, y)), so without this a ported row has to carry a
     * box top computed from the font's ascent and the reference's own numbers
     * stop being usable as written.
     */
    int text_baseline;

    /* INI: text= — literal string for static type=rs_text owners (not cache-backed). */
    char text[256];

    /*
     * Title-screen widgets (type=login_input / login_button / login_message /
     * title_progress*). The title screen is not a cache interface -- no
     * revision ships one as widget data -- so it is built from client widgets
     * whose every appearance decision is stated here rather than in C.
     *
     * INI: field=
     * Which of a paired widget this one is: `username`/`password` for a
     * login_input, `left`/`right` for a title_flames brazier. One key rather
     * than two near-identical ones, because it answers the same question.
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

    /*
     * INI: flame_bias= / flame_sway= / flame_run= / flame_row=
     *
     * Where a brazier's fire sits inside the 128-wide column it burns in.
     * The column itself is blitted over the backdrop it was cut from, so
     * these move the FIRE within it -- which is how both references lean
     * the two flames outward without moving the strip of wall behind them.
     *
     * bias  destination column the run starts at, signed
     * sway  which way the per-row wobble pushes: +1 or -1
     * run   source columns drawn (the 2004 right brazier is 103 of 128)
     * row   destination row the fire starts on
     *
     * Client-TS: left bias -22 sway -1 run 128 row 9; right bias 24
     * sway +1 run 103 row 9. The deob leans both by 22 and starts a row
     * higher, which is why these are the profile's numbers and not C's.
     */
    int flame_bias;
    int flame_sway;
    int flame_run;
    int flame_row;
    /* INI: flame_blur= -- `neighbour4` (Client-TS) or `box` (the deob).
     * The single biggest difference in how the two eras' fire looks; see
     * enum TitleFlameBlur. Unstated is the older one. */
    char flame_blur[16];

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

    /* INI: prompt= — the unfocused input line's invitation on type=chat.
     * The place the wording lives, because the right words are a property of
     * the LANE: "Press Enter to chat..." on a desktop profile, "Tap here to
     * chat..." under a `@mobile` override. Empty means the renderer's own
     * default. */
    char chat_prompt[REVCONFIG_CHAT_PROMPT_LEN];

    /* INI: type=chat_button — privacy bar below chatback (filter, label, mode0..3). */
    int chat_button_filter;
    char chat_button_label[64];
    int chat_button_label_y;
    int chat_button_mode_y;
    char chat_button_mode_label[4][16];
    int chat_button_mode_color[4];
};

/* The area and edge named by a layout's `safe_area=<area>:<edge>`. Mirrors
 * UITREE_SAFE_AREA_SOURCE_* / UITREE_SAFE_AREA_FLAG_* one for one, translated
 * at bake -- revconfig describes an interface, it does not include the widget
 * tree that renders one.
 *
 * `os` is the canvas minus what the PLATFORM put over the window; the
 * game-chrome area (the canvas minus what the CLIENT put there) is the second
 * one coming, and is why the area is named at all rather than left implicit.
 * Only the bottom edge exists, because only the bottom edge is ever covered. */
#define REVCONFIG_SAFE_AREA_SOURCE_NONE 0
#define REVCONFIG_SAFE_AREA_SOURCE_OS 1

#define REVCONFIG_SAFE_AREA_FLAG_BOTTOM 1

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

    /*
     * INI: xalign=center
     * Centre this row horizontally in its parent and take `y` as the distance
     * from the parent's top.
     *
     * The modern title screen is a fixed 765-wide panel centred in whatever
     * window the client has -- the deob computes `titleX = (canvasW - 765)/2`
     * every frame -- and that is a layout RULE, not a coordinate, so it cannot
     * be written as an x. The edge insets cannot express it either: they
     * already centre an axis with no inset, but a row with all-zero insets is
     * indistinguishable from one that simply never set any.
     */
    int xalign_center;

    /*
     * INI: safe_area=<area>:<edge>, e.g. `safe_area=os:bottom`
     * Keep this row clear of one named safe area's edge. `os` is the part of
     * the window the OPERATING SYSTEM is covering -- today a soft keyboard, a
     * band off the bottom while it is up. When the band would overlap the row,
     * the row slides up by exactly the overlap; when it would not, the row
     * does not move at all.
     *
     * The area is named because there is going to be more than one: the client
     * also has a game-chrome area, the canvas minus the frame regions and the
     * edges plugins reserved, and a row dodging the chat box is not asking the
     * same question as a row dodging the keyboard. A bare edge would have to
     * pick one of them silently.
     *
     * This is the login box's rule, and it belongs here for the same reason
     * `xalign=center` does: which panel must stay reachable while someone is
     * typing into it is a fact about THIS revision's interface -- one profile's
     * stone box is another's chat entry -- and the client cannot know it by
     * looking. It used to be a role name (`title_box`) that a hardcoded block
     * in app.c went looking for every frame, which meant a new profile could
     * only get the behaviour by naming its panel what that block expected.
     *
     * A profile that says nothing here keeps its row exactly where it authored
     * it, on a phone as on a desktop.
     *
     * @see REVCONFIG_SAFE_AREA_SOURCE_*, and ToriRS_PluginApi::safe_os -- the
     * same band, offered to plugins that place their own chrome.
     */
    int safe_area_source;
    int safe_area_flags;

    /*
     * INI: safe_area_margin=
     * Extra canvas rows to leave between this row's bottom edge and the safe
     * area's, on top of the overlap. Unstated = 0, i.e. flush against the
     * keyboard. Ignored unless safe_area= names an area and an edge.
     */
    int safe_area_margin;
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

/*
 * enum for RevConfigCameraItem.zoom_mode -- is the WHEEL live?
 *
 * The player's switch, not the revision's. No `zoom=` sets it: a revision
 * states where its camera rests and whether it takes the later client's
 * viewport term (`viewport_zoom`), and both of those are facts about that
 * client. Whether a wheel moves the eye is a fact about THIS one, it is the
 * same answer everywhere, and the settings page owns it.
 *
 * Splitting them is what makes the feature behave the same on every lane.
 * While `zoom=fixed:` set this too, the one revision that states it -- the
 * shared 2004 profile, which wanted the projection fidelity -- silently lost
 * the wheel as well, and the settings row that appeared to give it back
 * instead turned on the later client's viewport zoom and halved the picture.
 */
enum RevConfigCameraZoomMode
{
    /**
     * The eye height is a live value the wheel moves, bounded by min..max.
     * This is the client's own gesture and no revision's behaviour; it is the
     * default on every revision, because it is what this tree already did
     * everywhere it was not switched off by accident.
     */
    REVCONFIG_CAMERA_ZOOM_CLAMPED = 0,
    /**
     * The eye is pinned at `zoom_height` and nothing moves it. What a player
     * picks when they want the revision's resting camera and no wheel at all
     * -- and, on a `zoom=fixed:` revision, what reproduces Client-TS exactly
     * (`camFollow(..., pitch * 3 + 600)`).
     *
     * Reached only from the settings page. A revision asking for the 2004
     * camera says so with `zoom=fixed:`, which states the rest height and
     * clears `viewport_zoom`; losing the wheel as well was a side-effect of
     * those two questions sharing one key, not anything a profile meant.
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

/** The reference eye height, and this client's zoom rest position. */
#define REVCONFIG_CAMERA_ZOOM_DEFAULT_HEIGHT 600

/*
 * The wheel band, as PERCENTAGES of whatever rest height a revision states.
 *
 * A ratio and not a pair of eye heights, because absolute is what made this
 * feature behave differently on every lane it ran on. [240,2160] is 40%..360%
 * of the 2004 client's 600 and something else entirely of a revision that
 * rests anywhere else, so one setting bought a different amount of travel
 * depending on which world you booted -- and on a revision that rests far from
 * 600 it could put the whole band on one side of the resting view. The ratio
 * is the part that is actually the same, so the ratio is what is stated.
 *
 * 40..360 of 600 IS [240,2160], the band this tree already shipped, so no lane
 * that was already right moves.
 */
#define REVCONFIG_CAMERA_ZOOM_DEFAULT_MIN_PCT 40
#define REVCONFIG_CAMERA_ZOOM_DEFAULT_MAX_PCT 360

/** `zoom=` when no `[camera]` section states one. Derived, so the two
 *  spellings of this tree's default band cannot drift apart. */
#define REVCONFIG_CAMERA_ZOOM_DEFAULT_MIN             (REVCONFIG_CAMERA_ZOOM_DEFAULT_HEIGHT *               REVCONFIG_CAMERA_ZOOM_DEFAULT_MIN_PCT / 100)
#define REVCONFIG_CAMERA_ZOOM_DEFAULT_MAX             (REVCONFIG_CAMERA_ZOOM_DEFAULT_HEIGHT *               REVCONFIG_CAMERA_ZOOM_DEFAULT_MAX_PCT / 100)

/** `wheel_step=` when no `[camera]` section states one: one notch moves the
 *  eye height by a tenth of the rest position. */
#define REVCONFIG_CAMERA_WHEEL_STEP_PCT 10
#define REVCONFIG_CAMERA_WHEEL_STEP_DEFAULT           (REVCONFIG_CAMERA_ZOOM_DEFAULT_HEIGHT *               REVCONFIG_CAMERA_WHEEL_STEP_PCT / 100)

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
    /*
     * Does this revision's follow camera have the LATER client's
     * viewport-derived zoom — the `* viewportZoom / 256` on the follow
     * distance (client.method2068) and the viewport-recomputed projection
     * scale (class159.method5357)?
     *
     * Derived from `zoom=` and never stated on its own: `fixed:` IS the 2004
     * camera, whose projection is the bare `<< 9` of Model.project and whose
     * distance is a flat `pitch * 3 + height`; `clamped:` is a later one.
     *
     * Separate from zoom_mode because the two answer different questions and
     * only one of them is the player's to change. zoom_mode is a live setting
     * — the settings page's "Zoom" row writes it — and reading it for THIS is
     * what made turning the wheel on halve the picture: the projection scale
     * dropped 512 -> 256 and the eye took the viewport term, so a 2004 lane
     * jumped to a view no band could bring back. At rest the frame is
     * 512/(pitch*3+600); at the OSRS band's closest notch it became
     * 256/(pitch*3+360), a third smaller than the zoom it was supposed to
     * start from. Enabling the wheel is not a claim about which client this
     * is.
     */
    int viewport_zoom;
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

/**
 * `plugin_button_align=` -- which edge of the mount an anchored plate hangs
 * off.
 *
 * An edge and a margin rather than a y, because the y is the thing that goes
 * wrong: the mount is a panel the cache sizes and a CS2 hook re-lays out, so a
 * number measured on one frame lands on top of the panel's own button on the
 * next. "Fifteen pixels in from the top" survives both.
 */
enum RevConfigChromeAlign
{
    REVCONFIG_CHROME_ALIGN_NONE = 0,
    REVCONFIG_CHROME_ALIGN_TOP,
    REVCONFIG_CHROME_ALIGN_BOTTOM,
};

/*
 * `[chrome]` -- where the CLIENT's own furniture mounts on this revision.
 *
 * One thing today: the plugin window's "Manage Plugins" launcher. That button
 * is not the cache's -- plugins are a client feature and no server knows about
 * them -- but WHERE it goes is entirely the cache's business: on rev-239 the
 * logout tab is interface 182 and its panel is 190x261, and neither number
 * means anything on another revision. Every one of them used to be a literal
 * in torirs_plugin_panel.u.c, which is the same silent-wrong-answer trap
 * `[iface:...]` exists to delete, one level down.
 *
 * A LANE WHOSE GAMEFRAME IS AUTHORED STATES NONE OF THIS. The 2004 profiles
 * write the same button out as `[component:manage_plugins_*]` records with a
 * layout entry inside the logout tab (revconfig/rs245_2lc), because a profile
 * that builds the whole frame can simply put it there. This section is for the
 * lanes whose frame comes out of a cache and cannot be authored -- and stating
 * it on a lane that already authors one is how you get two buttons.
 *
 * Nothing here is defaulted. An absent section, or one that leaves a key out,
 * means "the client builds no launcher on this revision".
 */
struct RevConfigChromeItem
{
    /* INI: plugin_button_iface= -- the `[iface:<name>]` section naming the
     * interface the button mounts inside. Empty when unstated. */
    char plugin_iface[REVCONFIG_CHROME_NAME_LEN];

    /* INI: plugin_button_parent= -- child component of that interface the
     * button hangs off; 0 is the interface's own root. -1 when unstated. */
    int plugin_button_parent;

    /* INI: plugin_button_x= / plugin_button_y= -- where the plate sits inside
     * that parent, in interface pixels. -1 when unstated. */
    int plugin_button_x;
    int plugin_button_y;

    /* INI: plugin_button_w= / plugin_button_h= -- the plate's own box. The
     * three baked caps are 36px, so a width under 72 has nowhere to put the
     * tile between them. -1 when unstated. */
    int plugin_button_w;
    int plugin_button_h;

    /* INI: plugin_button_op= -- the hover option the button advertises, e.g.
     * "Manage Plugins". Empty means the button names nothing on hover; the
     * plate's own caption says it either way. */
    char plugin_button_op[REVCONFIG_CHROME_NAME_LEN];

    /*
     * INI: plugin_button_anchor= -- the `[role:<name>]` whose node the plate
     * is CUT FROM: its width, its height and the pictures it is made of.
     *
     * The alternative is what this replaced: a width, a height and a baked
     * skin written into the profile, all three of which are a guess about a
     * panel the cache lays out and a CS2 hook re-lays out per layout. Naming
     * the panel's own button instead makes the plate the same size and the
     * same material as the control above it on every frame the revision has,
     * and it is one name rather than four numbers.
     *
     * Empty when unstated, which is the absolute form: `plugin_button_x/y/w/h`
     * and the client's own baked plate.
     */
    char plugin_button_anchor[REVCONFIG_CHROME_NAME_LEN];

    /* INI: plugin_button_align= -- which edge of the parent the anchored plate
     * sits against, enum RevConfigChromeAlign. _NONE when unstated. */
    int plugin_button_align;

    /* INI: plugin_button_margin= -- how far in from that edge, in interface
     * pixels. -1 when unstated. */
    int plugin_button_margin;
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
        struct RevConfigStringItem string;
        struct RevConfigLoginReplyItem login_reply;
        struct RevConfigPreloadItem preload;
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
 * The wheel band this client offers around `height`, in eye heights.
 *
 * One place, so a revision's band and the settings page's presets are the same
 * arithmetic rather than two tables that agree until one is edited.
 * @see REVCONFIG_CAMERA_ZOOM_DEFAULT_MIN_PCT for why it is a ratio.
 */
void
revconfig_camera_default_band(int height, int* out_min, int* out_max);

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
