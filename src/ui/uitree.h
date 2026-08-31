#ifndef SRC_UITREE_H
#define SRC_UITREE_H

#include "ui/uitree_component_options.h"
#include "ui/uitree_debug_overlay.h"
#include "ui/uitree_hook.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define UI_INV_SLOT_OFFSET_MAX 20
/* UITreeComponent::child_key_max sentinels (below any real sub-id key). */
#define UITREE_CHILD_KEY_UNKNOWN INT32_MIN
/**
 * Component ids for controls a PROFILE authored, one group below the chrome's.
 *
 * A revconfig `[component:]` has no cache id -- there is no interface behind
 * it -- so the builder used to leave its component_id at -1, and that is the
 * whole reason such a control could be hovered but never clicked: the click
 * path carries a component ID, and `-1` reads as "the click landed on
 * nothing". The mouseover line worked, the right-click menu worked, and the
 * left click silently did nothing, which is the least debuggable shape a bug
 * can take.
 *
 * A synthetic id fixes that for every authored control at once, and a RANGE is
 * what makes it safe: it cannot collide with a cache uid (no interface is
 * numbered 0x7FFD) and it is one bounds test away from being recognised.
 *
 * A GROUP of its own rather than sharing the chrome's, because the chrome's
 * group is intercepted before the game's dispatch ever sees it
 * (add_component_rows returns 0 for it) -- these are the opposite: they are
 * ordinary components with ordinary menu rows, and the only thing they need
 * from the id is to have one.
 */
#define TORIRS_REVCONFIG_GROUP 0x7FFD
#define TORIRS_REVCONFIG_ID_BASE (TORIRS_REVCONFIG_GROUP << 16)

#define UITREE_CHILD_KEY_NONE (INT32_MIN + 1)
#define UITREE_MENU_OPTION_SLOTS 10
/* 64: option labels carry <col=...>name</col> tags well past 32 chars. */
#define UITREE_MENU_OPTION_LEN 64
#define UITREE_SUBMENU_OP_SLOTS 10
#define UITREE_SUBMENU_ENTRY_SLOTS 32
#define UITREE_CHAT_OP_TEMPLATE_LEN 64
#define UITREE_CHAT_PROMPT_LEN 64
#define UITREE_WALK_STACK_MAX 64
#define UITREE_INV_SOURCE_INVALID (-1)

#define UITREE_RELATIVE_FLAG_LEFT 1
#define UITREE_RELATIVE_FLAG_TOP 2
#define UITREE_RELATIVE_FLAG_RIGHT 4
#define UITREE_RELATIVE_FLAG_BOTTOM 8

/** Client.ts overMainComId / overSideComId / overChatComId. -1 = none. */
struct UITreeHoverIds
{
    int main_com_id;
    int side_com_id;
    int chat_com_id;
};

/**
 * "builtin" components are historically components that would've been
 * hardcoded into the client. RS_* map to cache IF1/IF3 widget types
 * (see ToriRS_ComponentType). CC_OBJ is a CS2-created dynamic child.
 */
enum UITreeComponentType
{
    /* Historically things that were hardcoded into the client. */
    UIELEM_BUILTIN_COMPASS = 1,
    UIELEM_BUILTIN_MINIMAP = 2,
    UIELEM_BUILTIN_SIDEBAR = 3,
    UIELEM_BUILTIN_CHAT = 4,
    UIELEM_BUILTIN_HOVERTEXT = 5,
    UIELEM_BUILTIN_WORLD = 6,
    UIELEM_BUILTIN_SPRITE = 7,
    UIELEM_BUILTIN_REDSTONE_TAB = 8,
    UIELEM_BUILTIN_TAB_ICONS = 9,
    UIELEM_BUILTIN_CROSS = 10,
    UIELEM_BUILTIN_MINIMENU = 11,
    UIELEM_BUILTIN_CHAT_BUTTON = 12,
    UIELEM_BUILTIN_PLAYERMODEL = 13,
    /** Screen-space pass over the world viewport: health bars + hitsplats
     *  (reference drawEntities). Positions come from the host. */
    UIELEM_BUILTIN_ENTITY_OVERLAY = 24,
    /** World map surface (clientCode 1400): the baked map regions, panned and
     *  zoomed by the CS2 world map state. */
    UIELEM_BUILTIN_WORLDMAP = 25,
    /** World map overview pane (clientCode 1401): scale-blit of the area's
     *  compositetexture PNG. The red viewport rects are CS2 on overview_overlay. */
    UIELEM_BUILTIN_WORLDMAP_OVERVIEW = 26,
    /** Developer debug overlay (src/ui/uitree_debug_overlay.h). Not content and
     *  not part of any gameframe: the app pushes it as the last root sibling and
     *  it gets its own emit pass, after the drag pass, so it draws over
     *  everything. Skipped entirely when the host has no overlay. */
    UIELEM_BUILTIN_DEBUG_OVERLAY = 27,
    /**
     * Multi-combat indicator (SET_MULTIWAY): one sprite frame, drawn only
     * while the server says the player is in a multi-combat zone. A builtin
     * because no interface owns it -- the reference plots it straight onto the
     * viewport -- but its sprite and its place are revconfig's, not C's.
     */
    UIELEM_BUILTIN_MULTIWAY = 29,
    /**
     * System-update countdown (UPDATE_REBOOT_TIMER): one line of text over the
     * viewport while an update is pending. Same deal as MULTIWAY -- font,
     * colour and position come from revconfig, the host supplies the string
     * and whether there is one.
     */
    UIELEM_BUILTIN_REBOOT_TIMER = 30,
    /*
     * Title-screen widgets, 31-35.
     *
     * Builtins for the same reason MULTIWAY and REBOOT_TIMER are: no revision
     * ships the login screen as interface data, so there is no cache pack to
     * mount and the client has to own the widgets. What it does NOT own is how
     * they look -- position, font, colour, the caret string, the mask, the
     * length caps and the charset all arrive from revconfig, which is what lets
     * one set of widgets draw both a 2004 login box and a modern one.
     */
    /** One credential line: prefix, value (masked if asked), blinking caret. */
    UIELEM_BUILTIN_LOGIN_INPUT = 31,
    /** A clickable sprite carrying a resolved RS_TitleAction. Its hit box is
     *  its own layout box, which is what keeps click and draw from drifting --
     *  the references compute the two from different origins and only agree by
     *  arithmetic coincidence. */
    UIELEM_BUILTIN_LOGIN_BUTTON = 32,
    /** One of the three server-supplied login message lines. */
    UIELEM_BUILTIN_LOGIN_MESSAGE = 33,
    /** The loading bar's filled part; the track and border are rs_rect. */
    UIELEM_BUILTIN_TITLE_PROGRESS = 34,
    /** The loading bar's status line. */
    UIELEM_BUILTIN_TITLE_PROGRESS_TEXT = 35,
    /** One of the two burning braziers. The fire is simulated host-side and
     *  arrives as a scene sprite reuploaded per frame; the widget says which
     *  side it is and where it goes. */
    UIELEM_BUILTIN_TITLE_FLAMES = 36,
    /** A login-form checkbox ("Remember username", "Hide username"). Draws one
     *  of two sprites depending on the host's answer for its toggle, and its
     *  click flips that answer -- the same TITLE_ACTION path a login_button
     *  takes. The LABEL beside it is a child rs_text, as with a button. */
    UIELEM_BUILTIN_LOGIN_TOGGLE = 37,
    /**
     * Touch marker -- the "inkwell". Shown for EVERY touch, which is the whole
     * difference from UIELEM_BUILTIN_CROSS: the cross marks a click that
     * resulted in something, and on a touchscreen a tap that draws nothing is
     * indistinguishable from a tap the device never received.
     *
     * A builtin for the same reason CROSS is: no interface owns it, the
     * reference plots it straight onto the canvas. Its artwork and its colour
     * rules are revconfig's (`style=`, `walk_color=`, `interact_color=`), and a
     * profile overrides them with a `[component:<name>@mobile]` section.
     *
     * @see ui/torirs_chrome_inkwell.h, ui/uitree_ink.h.
     */
    UIELEM_BUILTIN_INKWELL = 38,
    UIELEM_RS_TEXT = 14,     /* TYPE_TEXT */
    UIELEM_RS_GRAPHIC = 15,  /* TYPE_GRAPHIC */
    UIELEM_RS_MODEL = 16,    /* TYPE_MODEL */
    UIELEM_RS_INV = 17,      /* TYPE_INV — multi-slot inventory grid */
    UIELEM_RS_LAYER = 18,    /* TYPE_LAYER */
    UIELEM_RS_RECT = 19,     /* TYPE_RECT */
    UIELEM_RS_LINE = 20,     /* TYPE_LINE */
    UIELEM_RS_INV_TEXT = 21, /* TYPE_INV_TEXT */
    /** TYPE_ARC — a circular sector, shaped by CC/IF_SETARC's two angles.
     *  The only user in the cache is clientscript 5480's countdown pie, which
     *  stacks three of them: a full translucent disc, the swept wedge over it,
     *  and the wedge's 1px arc outline. */
    UIELEM_RS_ARC = 28,
    // Dynamic object created by CS2.
    UIELEM_CC_OBJ = 23, /* CS2 cc_create / if_setobject objbox */
};

/**
 * Content-slot clientCodes: cache components that mark where a builtin surface
 * belongs, because the pack format has no widget type for it. The gameframes
 * (16, 80, 161, 164, 548, 601) place them as empty layers/graphics.
 *
 *   1336 CONTENT_CHAT              layer     161, 164, 548, 601
 *   1337 CONTENT_WORLD (viewport)  layer     16, 80, 161, 164, 548, 601
 *   1338 CONTENT_MINIMAP           graphic   161, 164, 548, 601
 *   1339 CONTENT_COMPASS           graphic   161, 164, 548, 601, 898
 *   1354 CONTENT_XP_DROPS          layer     80, 161, 164, 548, 601
 *   1400 CONTENT_WORLDMAP          layer     595
 *   1401 CONTENT_WORLDMAP_OVERVIEW layer     595
 *
 * The four that have a builtin behind them are mapped in uitree_build.c; the
 * rest stay plain cache components until their builtin has somewhere to draw.
 */
enum
{
    /** Character-design preview (dat1 3559:3650). The reference poses the
     *  composite once at readyanim frame 0 and only spins modelYAn after —
     *  a still by design, and rebuilt by the design screen's own edits. */
    UITREE_CLIENT_CODE_DESIGN_PREVIEW = 327,
    /** The LIVE local player (dat2 84:4, the equipment-stats figure). Not the
     *  design preview's static composite: it is the player's real PLAYER_INFO
     *  appearance, worn equipment included, rebuilt whenever that changes, and
     *  posed from the entity's own movement track. Shares 327's viewing angles
     *  (xAn 150, yAn = sin(cycle/40)*256) — 84:4 ships (0,0,0) and the client
     *  overrides. Bound in app.c (app_player_model_poll) because only the app
     *  can reach the world entity; 327 gets its angles from RS_ClientCode_Tick,
     *  which is CS1-only and so cannot serve this one. */
    UITREE_CLIENT_CODE_LOCAL_PLAYER_MODEL = 328,
    UITREE_CLIENT_CODE_CONTENT_WORLD = 1337,
    UITREE_CLIENT_CODE_CONTENT_MINIMAP = 1338,
    UITREE_CLIENT_CODE_CONTENT_COMPASS = 1339,
    UITREE_CLIENT_CODE_CONTENT_WORLDMAP = 1400,
    UITREE_CLIENT_CODE_CONTENT_WORLDMAP_OVERVIEW = 1401,
};

/**
 * Interface-slot tag (INI slot=): marks a chrome node as the mount region for
 * a runtime-openable interface (reference mainModalId / sideModalId /
 * chatComId surfaces). The slot manager finds mount nodes by this tag, never
 * by coordinates, so the geometry stays entirely in RevConfig.
 */
enum UITreeSlotTag
{
    UITREE_SLOT_NONE = 0,
    UITREE_SLOT_MAIN_MODAL,
    UITREE_SLOT_MAIN_OVERLAY,
    UITREE_SLOT_SIDE_MODAL,
    UITREE_SLOT_CHAT,
    UITREE_SLOT_TUT,
};

enum UITreeElemPositionKind
{
    UIPOS_XY = 1,
    UIPOS_RELATIVE = 2,
};

struct UITreeElemPosition
{
    /* Hot front, 36 bytes: UITree_LayoutGetBounds reads exactly these nine
     * fields and nothing else, and it runs on every node of every per-frame
     * walk. They are first so that they share the containing component's first
     * cache line — see the hot block at the top of struct UITreeComponent for
     * why that matters. Ordering inside this struct is not observed anywhere
     * (no positional initializers, no memcpy/fread over it), but keep the
     * layout-resolved bounds together at the front. */
    uint8_t layout_resolved;
    int abs_x;
    int abs_y;
    int abs_w;
    int abs_h;
    int x;
    int y;
    int width;
    int height;

    enum UITreeElemPositionKind kind;

    int relative_flags;
    int anchor_x;
    int anchor_y;
    int left;
    int top;
    int right;
    int bottom;

    int8_t x_mode;
    int8_t y_mode;
    int8_t width_mode;
    int8_t height_mode;
    int aspect_w;
    int aspect_h;
};

/* Runtime script hooks live in their own file: the slot type, its accessors and
 * its lifetime (the argument tails are owned allocations now, so a slot cannot
 * be memset or struct-copied by hand). See ui/uitree_hook.h. */

/** %1..%5 are the placeholders the reference client substitutes in text. */
#define UITREE_CS1_VALUE_MAX 5
/** CS1 "infinity" (inv-contains sentinel); rendered as "*" in text. */
#define UITREE_CS1_VALUE_INFINITY 999999999

/** click_mask / events bits (OSRS WidgetFlags / IF_SETEVENTS). */
#define UITREE_FLAG_DRAG_DEPTH_SHIFT 17
#define UITREE_FLAG_DRAG_DEPTH_MASK 0x7
#define UITREE_FLAG_DRAG_ON (1 << 20)
/** Bit 21: cell may be targeted by "Use X with Y" (deob method2195). */
#define UITREE_FLAG_USEABLE_ON (1 << 21)

#define UITREE_INTERFACE_PARENT_MAX 32
/** parent_index for UITree_Push: allocate node without linking into the tree. */
#define UITREE_PARENT_UNLINKED (-2)
struct UITreeInterfaceParent
{
    int container_uid;
    int group_id;
    int type; /* 0 modal, 1 overlay, 3 tab/sidemodal */
};

struct UITreeBehavior
{
    /** First on purpose: this is the one field of this struct the per-frame
     *  walks read on every node, and putting it at offset 0 keeps it on the
     *  second cache line of the component rather than a fourth. */
    uint8_t hide;
    /** Set when `hide` was forced by the interface-mount bookkeeping (a group
     *  baked into the tree but not mounted anywhere, or the group a mount slot
     *  just replaced) rather than by the cache data or a script. Mounting the
     *  group clears it — a cache/script hide is left alone. */
    uint8_t hide_unmounted;
    uint8_t script_kind;
    /** CS1 (IF1) value scripts: scripts[i] is compared against script_operand[i]
     *  using script_comparator[i] to decide the component's active state. */
    int scripts_count;
    int** scripts;
    int* scripts_lengths;
    /** Sized independently of scripts_count in the cache format. */
    int comparator_count;
    int* script_comparator;
    int* script_operand;
    int button_type;
    int client_code;
    int32_t click_mask;
    /** `IfType.targetMask`, normalised across cache generations by the
     *  component decoders — what a spell/prayer button may be aimed at once its
     *  target verb is armed, and 0 for everything that is not targetable.
     *  See ToriRS_Component.target_mask and TORIRS_TARGET_MASK_*. */
    int32_t target_mask;
    int over_layer_id;
    int over_color;
    int active_color;
    int active_over_color;
};

struct UITreeMenuSubmenuOptions
{
    char ops[UITREE_SUBMENU_OP_SLOTS][UITREE_SUBMENU_ENTRY_SLOTS][UITREE_MENU_OPTION_LEN];
};

struct UITreeMenuOptions
{
    char option[UITREE_MENU_OPTION_LEN];
    char ops[UITREE_MENU_OPTION_SLOTS][UITREE_MENU_OPTION_LEN];
    /** BUTTON_TARGET spell strings (reference IfType.targetVerb/targetBase):
     * "Cast" + "Wind Strike" → the "Cast Wind Strike ..." target-mode verb.
     * Empty for non-spell components. */
    char target_verb[UITREE_MENU_OPTION_LEN];
    char target_base[UITREE_MENU_OPTION_LEN];
    int option_action;
    int op_actions[UITREE_MENU_OPTION_SLOTS];
    /** CC/IF_SETOPSUBMENU rows, owned by the component, NULL until one is set.
     *  10×32 64-byte labels is 20 KB — inline, that was two thirds of every
     *  widget node, memset on every push and reclaim and strided over by every
     *  linear walk, to carry a feature a handful of components use. Go through
     *  UITree_MenuSubmenu* rather than dereferencing it. */
    struct UITreeMenuSubmenuOptions* submenus;
};

/** Submenu label for op_index (1-based) / entry_index (1-based), "" when unset. */
char const*
UITree_MenuSubmenuEntry(
    struct UITreeMenuOptions const* opts,
    int op_index,
    int entry_index);

/** Set one submenu label, allocating the block on first use. False if the
 *  indices are out of range or the allocation failed. */
bool
UITree_MenuSubmenuSetEntry(
    struct UITreeMenuOptions* opts,
    int op_index,
    int entry_index,
    char const* text);

/** Clear the labels of one op (1-based), or of every op when op_index <= 0. */
void
UITree_MenuSubmenuClear(
    struct UITreeMenuOptions* opts,
    int op_index);

/** Release the block (component reclaim / tree free). */
void
UITree_MenuSubmenuFree(struct UITreeMenuOptions* opts);

/* Op slots 1..10; index 9 is the "typed key" slot the OPT opcode variants use. */
#define UITREE_OPKEY_SLOTS 10
#define UITREE_OPKEY_PAIR_MAX 5

/**
 * Keyboard shortcut bound to one of a component's ops, set by CC/IF_SETOPKEY.
 *
 * A binding matches when a key event carries either the bound character
 * (key_chars[i]) or the bound OSRS key code (key_codes[i]); the reference stores
 * up to five alternatives per slot so one op can answer to several keys.
 */
struct UITreeOpKeyBinding
{
    uint8_t bound;
    uint8_t pair_count;
    uint8_t ignore_held;
    uint8_t rate_enabled;
    int rate;
    int key_chars[UITREE_OPKEY_PAIR_MAX];
    int key_codes[UITREE_OPKEY_PAIR_MAX];
};

struct UITreeOpKeys
{
    struct UITreeOpKeyBinding slots[UITREE_OPKEY_SLOTS];
    /** Mirrors the reference hasKeyBindings: lets the match pass skip nodes. */
    uint8_t has_bindings;
};

struct UITreeChatMinimenuConfig
{
    char op_report_abuse[UITREE_CHAT_OP_TEMPLATE_LEN];
    int op_report_abuse_action;
    char op_add_ignore[UITREE_CHAT_OP_TEMPLATE_LEN];
    int op_add_ignore_action;
    char op_add_friend[UITREE_CHAT_OP_TEMPLATE_LEN];
    int op_add_friend_action;
    char op_accept_trade[UITREE_CHAT_OP_TEMPLATE_LEN];
    int op_accept_trade_action;
    char op_accept_duel[UITREE_CHAT_OP_TEMPLATE_LEN];
    int op_accept_duel_action;
};

enum UITreeChatButtonFilter
{
    UITREE_CHAT_BUTTON_PUBLIC = 0,
    UITREE_CHAT_BUTTON_PRIVATE,
    UITREE_CHAT_BUTTON_TRADE,
    UITREE_CHAT_BUTTON_REPORT,
};

struct UITreeChatButtonConfig
{
    enum UITreeChatButtonFilter filter;
    char label[64];
    int label_y;
    int mode_y;
    int font_id;
    int center;
    int shadowed;
    char mode_label[4][16];
    int mode_color[4];
};

/**
 * Everything a login_input needs to draw and to bound one credential field.
 *
 * Heap-allocated and reached through a pointer, like the chat and debug-overlay
 * configs beside it: a login screen has two of these, the component union is
 * shared by every one of ~7000 nodes, and a 220-byte inline member would cost
 * more than a megabyte to serve two widgets.
 *
 * The charset and maxlen live here rather than in the title model because they
 * are per-revision facts the INI states; the app copies them onto the model
 * once the tree is baked. @see RS_TitleFieldCfg.
 */
/*
 * What a login_button's click asks the host to do.
 *
 * A restatement of game/rs_title.h's RS_TitleAction, not an include of it: ui/
 * is a leaf of game/, the same reason revconfig.h restates the minimenu action
 * ids. game/rs_title.c carries the static assertions that hold the two in step,
 * so a value added to one and forgotten in the other fails to build.
 */
enum UITreeTitleAction
{
    UITREE_TITLE_ACTION_NONE = 0,
    UITREE_TITLE_ACTION_EXISTING_USER = 1,
    UITREE_TITLE_ACTION_NEW_USER = 2,
    UITREE_TITLE_ACTION_LOGIN = 3,
    UITREE_TITLE_ACTION_CANCEL = 4,
    UITREE_TITLE_ACTION_FOCUS_USERNAME = 5,
    UITREE_TITLE_ACTION_FOCUS_PASSWORD = 6,
    UITREE_TITLE_ACTION_TOGGLE_REMEMBER = 7,
    UITREE_TITLE_ACTION_TOGGLE_HIDE = 8,
};

struct UITreeLoginInputConfig
{
    /** Which credential: 0 username, 1 password. Mirrors RS_TitleField, kept
     *  as an int so ui/ stays a leaf of game/. */
    int field;
    int font_id;
    int color;
    int center;
    int shadowed;
    /** Blink period in client cycles; the caret shows for the first half.
     *  0 = never blink. */
    int caret_blink;
    /** Drawn before the value, on the same line and in the same string --
     *  which is how the references measure and centre it. */
    char prefix[32];
    /** Appended while the caret is visible, in the era's own font markup
     *  ("@yel@|" on dat1, "<col=ffff00>|" on dat2). */
    char caret[24];
    /** Shown instead of the value; empty shows the value. */
    char mask[8];
    int maxlen;
    char charset[160];
};

/**
 * One entry of a component's runtime param table (CS2 CC_SETCOMPONENTPARAM /
 * CC_GETCOMPONENTPARAM, OldSchool wire 1704/1703).
 *
 * Runtime-only by nature, not by omission: an OldSchool IF3 file has no param
 * section, so a component is born with an empty table and only a script can put
 * anything in it. The gameframe scripts use it to label the widgets they build —
 * "this row is kind 600, index 4" — and read the labels back after a cc_find to
 * decide what a click landed on.
 */
struct UITreeComponentParam
{
    int32_t id;
    int32_t value;
    /** Non-NULL for a string param (CC_SETCOMPONENTPARAM kind 2), in which case
     *  `value` is unset. Owned by the component. */
    char* str;
};

/*
 * The per-slot overrides an RS_INV component may carry.
 *
 * Held off the component because inventories are a handful of nodes while the
 * union that carried these four arrays sized all of them: at 20 slots they are
 * 320 of the union's 352 bytes, and the union is the widest arm of a struct
 * that a loaded tree holds 8192 of. Every non-inv component paid for them.
 *
 * Absent is a real answer, so reads go through UITree_InvSlots, which hands
 * back uitree_inv_slots_none rather than NULL -- the callers assign .offset_x
 * straight into a layout and must not have to branch.
 */
struct UITreeInvSlots
{
    int offset_x[UI_INV_SLOT_OFFSET_MAX];
    int offset_y[UI_INV_SLOT_OFFSET_MAX];
    /** -1 = this slot draws no background, which is why "none" cannot be the
     *  zeroed block. */
    int bg_scene_id[UI_INV_SLOT_OFFSET_MAX];
    int bg_atlas_index[UI_INV_SLOT_OFFSET_MAX];
};

/*
 * The chrome arms held off the component.
 *
 * A tree carries one chat panel, one debug overlay and a handful of chat
 * buttons, and each of those arms was sizing the union for all 8192
 * components: chat at 344 bytes was the widest outright, then chat_button at
 * 168 and debug_overlay at 136 were the widest of what remained. Behind
 * pointers the union falls to rs_model's 60, so a component sheds 284 bytes
 * it only ever needed on a few nodes.
 *
 * Reads go through the accessors, which hand back a zeroed "none" block rather
 * than NULL. Absent is a real state -- a chat node whose spec has not run yet
 * -- and it has always read as all-zero, because a fresh component slot is
 * memset before its type is set.
 */
struct UITreeChatConfig
{
    struct UITreeChatMinimenuConfig minimenu;
    int font_id; /* INI font= (message + input line font, e.g. p12) */
    /** INI prompt= — the unfocused input line's invitation. Empty (the
     * "none" state included) means the renderer's own default wording. */
    char prompt[UITREE_CHAT_PROMPT_LEN];
};

struct UITreeDebugOverlayConfig
{
    /** Scene font ids the host registered the baked debug faces
     * under, indexed by enum ToriRSChromeFontSlot. ui/ stays leaf, so they
     * travel as plain ints the same way minimenu.font_id does. */
    int font_id_small;
    int font_id_menu;
    int font_id_body;
    /** Scene id the host uploaded the baked chrome skin under, and the
     * atlas index per enum ToriRSChromeSkinSlot. -1 = no skin. */
    int skin_scene_id;
    int skin_atlas[TORIRS_CHROME_SKIN_SLOT_COUNT];
};

struct UITreeComponent
{
    /* --- Hot block: the fields every per-frame walk reads on every node it
     * visits, packed into this struct's first cache line. Do not scatter them.
     *
     * The tree is ~7,100 components of ~744 bytes — 5.3 MB, several times the
     * L2 — and the emit walk chases first_child/next_sibling indices through
     * it in an order unrelated to the array's layout, so each visit is a
     * random stride and every extra 64-byte line it straddles is its own DRAM
     * miss. Measured: repeating the emit walk over the just-warmed tree costs
     * 41% of the cold walk, i.e. ~59% of the stage is the memory system, and
     * prefetching does not help because the footprint, not the latency, is
     * the problem. Before this block the per-visit reads were spread over
     * lines 0, 1, 2 and 5; they now sit on line 0, plus `behavior.hide` on
     * line 1.
     *
     * type + the three links + component_id + trans + four flag bytes is 28
     * bytes, and the hot front of `position` is the other 36 — exactly 64.
     * There is no slack: one more field here pushes `position.height` onto
     * the next line and gives the win back. --- */
    enum UITreeComponentType type;
    int32_t parent;
    int32_t first_child;
    int32_t next_sibling;
    int component_id;
    int trans;
    uint8_t if3;
    /** Visual-only override flag while dragging (does not mutate position.abs_*);
     *  the drag_visual_* values it selects are cold and live below. */
    uint8_t drag_active;
    uint8_t always_dirty;
    uint8_t is_dirty;
    struct UITreeElemPosition position;
    /** Immediately after `position` so that `behavior.hide` — read on every
     *  visit, and the only field of this struct that is — lands at offset 112,
     *  on the second cache line rather than a fourth one. */
    struct UITreeBehavior behavior;
    /* --- end hot block --- */

    /** Slot is on the tree free-list (CC_DELETEALL reclaim); skipped by array
     *  walks and reused by the next push. */
    uint8_t freed;
    /**
     * Identity of this particular occupant of the component-array slot.
     *
     * Array indices are storage, not identity: CC_DELETEALL puts an index on
     * the free list and the next CC_CREATE can hand it straight to an
     * unrelated node. Long-lived side tables (notably a plugin gameframe
     * declaration) pair an index with this value before touching it, so an
     * old declaration can never restore state through a recycled index.
     * Zero is reserved for an empty/reclaimed slot.
     */
    uint32_t incarnation;
    int32_t free_next;
    /** Hint at the tail of `first_child`'s sibling list, so appending a child is
     *  O(1) instead of walking the list (cc_create fills a container one child at
     *  a time, which is quadratic in the child count). Never trusted blindly —
     *  only used when it still looks like this node's last child, exactly like
     *  UITree::last_root_index; any mutation may leave it stale. */
    int32_t last_child_hint;
    /** Largest sub-id key among this node's children (see UITree_FindChildBySubid),
     *  UITREE_CHILD_KEY_NONE when none carry one, UITREE_CHILD_KEY_UNKNOWN when a
     *  mutation invalidated it and it has to be recomputed. Lets the by-sub-id
     *  lookup answer "no such child" without walking the sibling list, which is
     *  what made a container rebuild quadratic in row count. Only ever too high,
     *  never too low: a stale-high value costs a scan, it cannot miss a child. */
    int32_t child_key_max;
    /** Lazily built key->child map for this node's children, so a by-sub-id
     *  lookup that *hits* is O(1) too. The ceiling above only makes misses
     *  cheap; a container rebuild that re-finds each existing row still walked
     *  the sibling list once per row, which is quadratic in row count (the
     *  chatbox's 500 message rows made every chat line ~1300 steps x 500).
     *  Two halves of `child_key_index_cap` entries each: [0,cap) holds the
     *  dynamic child for a key, [cap,2*cap) the cache-baked one, matching the
     *  walk's "dynamic wins, static is the fallback" precedence. NULL when not
     *  built; `child_key_index_bad` marks a parent whose children carry
     *  duplicate keys, where only the walk can reproduce sibling order. */
    int32_t* child_key_index;
    int32_t child_key_index_cap;
    uint8_t child_key_index_bad;
    uint8_t dynamic;
    int dynamic_child_index;

    int item_id;
    int item_count;
    int item_scene_id;
    int item_atlas_index;
    /** Count-text rule from the SETOBJECT variant: 0 stackable-only (also the
     *  zero-init default), 1 always, 2 never. Read by emit_obj_stack_count. */
    uint8_t item_num_mode;

    /* Script-visible Component.colour / .fillColour / .text. Every Jagex iftype
     * carries these; layers use them as opaque data bags (loot info slots store
     * row counts and source names). TEXT/RECT rendering also mirrors colour into
     * the typed fields below. */
    int colour;
    int fill_colour;
    char* data_text;

    uint8_t no_click_through;
    uint8_t draggable;
    int drag_behavior;
    uint8_t drag_dead_zone;
    uint8_t drag_dead_time;
    uint8_t model_transparent;
    /** CC/IF_SETDRAGGABLE render-area parent uid (-1 = none). */
    int drag_render_area_uid;
    int drag_render_area_child_index;
    /** Visual-only overrides while dragging (do not mutate position.abs_*);
     *  selected by `drag_active` up in the hot block. */
    int drag_visual_x;
    int drag_visual_y;
    int drag_visual_trans; /* -1 = use component trans; else forced (e.g. 128) */

    /** Last CS1 evaluation, written by the CS1 eval task and read by the emit
     *  host: emit stays synchronous while the VM's asset yields are serviced by
     *  the task layer. */
    uint8_t cs1_active;
    int cs1_values[UITREE_CS1_VALUE_MAX];
    /** CS2 event hooks, owned by the component, NULL until one is registered.
     *  17 slots each carrying argv[64] and strv[4][80] inline is ~10 KB — the
     *  whole of a widget node, memset on every push and reclaim and strided over
     *  by all four per-frame DFS walks, to carry hooks that only a few hundred of
     *  the tree's thousands of components ever have. Same rationale as
     *  UITreeMenuOptions::submenus. Go through UITree_Hooks / UITree_HooksMut
     *  rather than dereferencing it. */
    struct UITreeRuntimeHooks* runtime_hooks;
    int target_priority;
    /** CC/IF_SETOPFORCELEFTCLICK: left-click executes the op without opening the menu. */
    uint8_t force_left_click;
    /**
     * Suppressed by a plugin gameframe layout (ui/uitree_frame.h).
     *
     * A flag of its own rather than a use of `behavior.hide`, and the reason
     * is not tidiness. `hide` is the CACHE's and the SCRIPTS': the CS1 value
     * scripts rewrite it every tick on a dat1 frame and the CS2 hooks do the
     * same on a dat2 one, so a layout borrowing it is overwritten before the
     * frame it wrote for is drawn. Worse, `UITree_ComponentVisibleById` reads
     * a hidden node with NO component id as visible -- a rule that is right
     * for the hover-reveal it was written for and means every revconfig
     * builtin, which has no id, ignores `hide` completely. Setting it on the
     * 2004 gameframe's forty-six stones changed nothing at all.
     *
     * Nothing but the layout writes this, so it needs no re-asserting and
     * cannot be argued with.
     */
    uint8_t frame_hidden;
    /**
     * Suppressed because this title screen is not the one showing.
     *
     * Its own flag for the reason given above `frame_hidden`: `behavior.hide`
     * belongs to the cache and its scripts, and -- decisively here --
     * UITree_ComponentVisibleById reads a hidden node with NO component id as
     * visible, which is every revconfig builtin. The login screen's groups are
     * exactly that, so hiding them with `hide` would change nothing at all.
     *
     * Only the screen machine writes it, so it needs no re-asserting and no
     * script can argue with it.
     */
    uint8_t screen_hidden;
    /** Suppressed by an owner-scoped semantic role replacement. Separate from
     * frame_hidden so either declaration may release without revealing a
     * subtree the other still owns. Cache scripts never write this flag. */
    uint8_t replacement_hidden;
    /** Camera projection temporarily rejected this scripted entity-overlay
     * layer (for example, its subject crossed behind the near plane). Kept
     * separate from `behavior.hide`, which remains script-owned. */
    uint8_t projection_hidden;
    /** enum UITreeSlotTag — nonzero marks this node as a mount region. */
    uint8_t slot_tag;
    /**
     * Semantic role this node was AUTHORED with (ui/uitree_role.h), interned
     * against the tree's role table. 0 = none.
     *
     * The direct half of the role vocabulary, for the nodes a revconfig
     * profile built itself. The other half is a matcher chain, which finds a
     * node the profile did not author -- a cache component, or one a CS2
     * script created -- and needs no field here because there is nowhere to
     * put it: the node does not exist until the script runs.
     */
    uint16_t role_id;
    /** OR of enum UITreeHotkeyEffect: the effects this node accepts from a
     *  bound key (revconfig `hotkey=` lines). 0 = not a hotkey target. */
    uint32_t hotkey_effects;
    /** Runtime param table (see struct UITreeComponentParam). Grown on demand and
     *  usually empty, so it is a pointer rather than an inline array — the tree
     *  holds thousands of components and only the ones a script tags carry one. */
    struct UITreeComponentParam* params;
    int params_count;
    int params_capacity;
    int scroll_x;
    int scroll_y;
    /** CC/IF_SETTRANSBOT and CC_GETBLENDTRANS. Kept separately from `trans`,
     *  matching the reference widget's top/bottom transparency pair. */
    int trans_bot;
    /* Lazily allocated — see ui/uitree_component_options.h. Read through
     * UITree_MenuOptions / UITree_OpKeys (never NULL), write through the Mut
     * accessors, test with the Has ones. */
    struct UITreeMenuOptions* menu_options;
    struct UITreeOpKeys* op_keys;
    union
    {
        struct
        {
            int scene_id;
            int atlas_index;
            /* COMPASS only: the pack's placeholder graphic, kept as the
             * circular alpha mask (inverted; content shows where it is
             * transparent). 0 = no mask (dat1 RevConfig path). */
            int mask_scene_id;
            int mask_atlas_index;
        } sprite;
        /*
         * UIELEM_BUILTIN_INKWELL. No scene id here: the artwork is generated
         * and uploaded once into a single scene entry holding every style and
         * colour, so what a component carries is a CHOICE and not a binding.
         * -1 for any of these means "the profile said nothing", and the host
         * substitutes its default. @see ui/torirs_chrome_inkwell.h.
         */
        struct
        {
            int style;
            int walk_color;
            int interact_color;
        } inkwell;
        struct
        {
            int scene_id;
            int atlas_index;
            int scene_id_active;
            int atlas_index_active;
            int tabno;
        } redstone_tab;
        struct
        {
            int scene_id;
            /* The pack's placeholder graphic, kept as the inverted alpha mask
             * (map shows where it is transparent). 0 = draw unmasked. */
            int mask_scene_id;
            int mask_atlas_index;
        } minimap;
        struct
        {
            uint8_t level_mask;
        } world;
        struct
        {
            int tabno;
            int componentno;
            int inv_source_id;
            /** INI selected= — this tab is the boot-time selection (reference
             *  sideTab default 3, kept in RevConfig rather than C). */
            uint8_t selected;
        } sidebar;
        struct
        {
            int font_id;
            int color;
            int center;
            int y_align;
            /** revconfig `baseline=`: the layout row's y IS the text baseline,
             *  which is how both references write every text coordinate
             *  (font.drawString(s, x, y)). Without it a row has to carry the
             *  box top instead, and the reference's own numbers stop being
             *  usable as written. */
            int baseline;
            int line_height;
            int shadowed;
            char const* text;
            char const* text_active;
        } rs_text;
        struct
        {
            int scene_id;
            int atlas_index;
            int scene_id_active;
            int atlas_index_active;
            uint8_t graphic_hitbox_only;
            uint8_t tiled;
            int outline;
            int graphic_shadow;
            uint8_t flip_h;
            uint8_t flip_v;
            /** IF3 spriteAngle / CC_SET2DANGLE: 65536 = one full turn (not the
             *  2048-per-turn camera-yaw scale the compass/minimap use). */
            int sprite_angle_r2pi65536;
        } rs_graphic;
        struct
        {
            int gamecache_model_id;
            /* Model drawn instead of gamecache_model_id while the widget is CS1-
             * active (reference getTempModel: model2Type/model2Id). -1 = none,
             * and none means draw nothing at all — the 254 special-attack bar
             * is ten dark cover segments over a green bar, each cover dropping
             * out as spec energy passes its threshold. */
            int active_model_id;
            int zoom;
            int xan;
            int yan;
            int zan;
            int rotate_x_speed;
            int rotate_y_speed;
            int x_offset;
            int y_offset;
            uint8_t orthog;
            uint8_t fixed_zoom;
            /* Model animation sequence set via IF/CC_SETMODELANIM, or -1 = none.
             * anim_frame is advanced by the client tick driver; anim_frame_cycle
             * accumulates elapsed 50hz cycles toward the current frame's length. */
            int anim_seq_id;
            int anim_frame;
            int anim_frame_cycle;
            /* Hold anim_frame instead of advancing it. The player-design
             * preview poses the composite once (reference: model.animate(
             * SeqType.list[readyanim].frames[0]) inside the idkDesignRedraw
             * rebuild) and never ticks it; only modelYAn spins. */
            uint8_t anim_hold;
        } rs_model;
        struct
        {
            int color;
            int filled;
        } rs_rect;
        struct
        {
            int color;
            /** 1 = the whole disc, 0 = a `line_width`-pixel band along the arc
             *  (reference NXTPix2D::DrawCircularArc's inner radius). */
            int filled;
            int line_width;
            /** CC/IF_SETARC, 65536 to a full turn. 0 is straight up and the
             *  sweep runs clockwise, so a drain reads as a clock hand. */
            int arc_start;
            int arc_end;
        } rs_arc;
        struct
        {
            int inv_source_id;
            int cols;
            int rows;
            int margin_x;
            int margin_y;
            /** Item drag allowed (objSwap || objReplace); 1 = draggable. */
            int can_drag;
            /** Show ObjType-op rows (objOps); 0 hides Drop/wield/op1-5. */
            int obj_ops;
            /** Show the "Use" row (objUse); 0 hides it. */
            int obj_use;
            /** NULL until a spec supplies per-slot data. Read through
             *  UITree_InvSlots, never directly. */
            struct UITreeInvSlots* slots;
        } rs_inv;
        struct
        {
            int obj_id;
            int obj_count;
            int scene_id;
            int atlas_index;
        } cc_obj;
        struct
        {
            int scroll_height;
            int scroll_width;
        } rs_layer;
        struct
        {
            int scene_id;
            int atlas_index;
            int tabno;
        } tab_icon;
        struct
        {
            int font_id;
        } minimenu;
        struct
        {
            int font_id;
        } hovertext;
        struct
        {
            int font_id;
            /** revconfig color=; text colour of the countdown line. */
            int color;
        } reboot_timer;
        /** NULL until a spec configures the field. Read through
         *  UITree_LoginInput, never directly. */
        struct UITreeLoginInputConfig* login_input;
        struct
        {
            int scene_id;
            int atlas_index;
            /** Resolved RS_TitleAction; 0 = none, and a button with none is a
             *  typo in an INI announcing itself by doing nothing. */
            int action;
        } login_button;
        struct
        {
            /** Off and on art. `scene_id_on` may be -1: a revision whose
             *  checkbox is one sprite plus a drawn mark says so by declaring
             *  no sprite_active, and both states then draw the same plate. */
            int scene_id;
            int atlas_index;
            int scene_id_on;
            int atlas_index_on;
            /** Resolved RS_TitleAction (one of the toggle members). */
            int action;
            /** Resolved RS_TitleToggle: which checkbox to ask the host about. */
            int toggle;
        } login_toggle;
        struct
        {
            /** Which of the three login message lines, 0-2. */
            int index;
            int font_id;
            int color;
            int center;
            int shadowed;
        } login_message;
        struct
        {
            int color;
            /** Bar pixels per percent; 0 means fill the box at 100. */
            int px_per_percent;
        } title_progress;
        struct
        {
            /** enum TitleFlameSide; ui/ stays a leaf so it travels as an int. */
            int side;
            /* Where the fire sits inside the column it burns in. The
             * column is blitted over the wall it was cut from, so these
             * lean the FIRE outward rather than the wall with it. */
            int bias;
            int sway;
            int run;
            int row;
            /** enum TitleFlameBlur. */
            int blur;
        } title_flames;
        struct
        {
            int font_id;
            int color;
            int center;
            int shadowed;
        } title_progress_text;
        /** NULL until a spec configures the overlay. Read through
         *  UITree_DebugOverlay, never directly. */
        struct UITreeDebugOverlayConfig* debug_overlay;
        /** NULL until a spec configures the panel. Read through UITree_Chat. */
        struct UITreeChatConfig* chat;
        /** NULL until a spec configures the button. Read through
         *  UITree_ChatButton. */
        struct UITreeChatButtonConfig* chat_button;
        struct
        {
            int color;
            int line_width;
            int horizontal;
        } rs_line;
        struct
        {
            int inv_source_id;
            int cols;
            int rows;
            int margin_x;
            int margin_y;
            int font_id;
            int color;
            int center;
            int shadowed;
        } rs_inv_text;
    } u;
};

/** The all-zero block a component with no hooks reads as. Never written. */
extern struct UITreeRuntimeHooks const uitree_hooks_none;

/** Every slot at offset 0 with no background -- what an inv with no per-slot
 *  overrides behaves as. */
extern struct UITreeInvSlots const uitree_inv_slots_none;

/** The zeroed blocks the accessors hand back for an unconfigured component. */
extern struct UITreeChatConfig const uitree_chat_none;
extern struct UITreeDebugOverlayConfig const uitree_debug_overlay_none;
extern struct UITreeChatButtonConfig const uitree_chat_button_none;
extern struct UITreeLoginInputConfig const uitree_login_input_none;

/** The component's chrome block, or the "none" block. Never NULL. */
struct UITreeChatConfig const*
UITree_Chat(struct UITreeComponent const* c);
struct UITreeDebugOverlayConfig const*
UITree_DebugOverlay(struct UITreeComponent const* c);
struct UITreeChatButtonConfig const*
UITree_ChatButton(struct UITreeComponent const* c);
struct UITreeLoginInputConfig const*
UITree_LoginInput(struct UITreeComponent const* c);

/** The component's chrome block, allocating it on first use. */
struct UITreeChatConfig*
UITree_ChatMut(struct UITreeComponent* c);
struct UITreeDebugOverlayConfig*
UITree_DebugOverlayMut(struct UITreeComponent* c);
struct UITreeChatButtonConfig*
UITree_ChatButtonMut(struct UITreeComponent* c);
struct UITreeLoginInputConfig*
UITree_LoginInputMut(struct UITreeComponent* c);

/** The component's per-slot data, or the "none" block. Never NULL. */
struct UITreeInvSlots const*
UITree_InvSlots(struct UITreeComponent const* c);

/** The component's per-slot data, allocating it on first use. */
struct UITreeInvSlots*
UITree_InvSlotsMut(struct UITreeComponent* c);

/**
 * A component's hook block for reading. Never NULL, so a caller may take the
 * address of any slot: an unregistered hook reads as script_id 0, which is what
 * "no hook" means on every dispatch path already.
 */
static inline struct UITreeRuntimeHooks const*
UITree_Hooks(struct UITreeComponent const* c)
{
    assert(c);
    return c->runtime_hooks ? c->runtime_hooks : &uitree_hooks_none;
}

/**
 * A component's hook block for writing, allocated zeroed on first use.
 * NULL only if the allocation failed.
 */
struct UITreeRuntimeHooks*
UITree_HooksMut(struct UITreeComponent* c);

/** Release the block (component reclaim / tree free). */
void
UITree_HooksFree(struct UITreeComponent* c);

/**
 * Chrome actions a configured hotkey can trigger.
 *
 * The set is HARD-CODED: revconfig names an effect, it never defines one. A
 * component advertises the effects it accepts with `hotkey=<effect>` lines
 * (UITreeComponent.hotkey_effects), and a [hotkey:<key>] section binds a key to
 * one component + effect pair. The advertisement is an allow-list — a binding
 * naming an effect its component does not accept is dropped at bake.
 *
 * Values are bits so a component can advertise several in one mask.
 */
enum UITreeHotkeyEffect
{
    /** "select_tab" — make this node's sidebar tab the active one. Requires a
     *  node that carries a tabno (tab_icon, redstone_tab, sidebar). */
    UITREE_HOTKEY_EFFECT_SELECT_TAB = 1u << 0,
};

/** Effect bit for a config spelling, or 0 when the name is not an effect. */
uint32_t
UITree_HotkeyEffectFromName(char const* name);

/** One resolved key binding: press `osrs_key` -> run `effect` on `node_index`.
 *  Node index rather than component_id because gameframe chrome (tab icons,
 *  the viewport) is built from revconfig and usually carries no component id. */
struct UITreeHotkey
{
    int osrs_key; /* OSRS internal code; see LibToriRS_OsrsKeyFromName */
    int32_t node_index;
    uint32_t effect;
};

#define UITREE_HOTKEY_MAX 64

/**
 * Dense live set of component slot indices with O(1) add/remove.
 * `pos[slot]` is the index into `slots[]`, or -1 when absent. `pos` is allocated
 * lazily on first insert and must be regrown whenever component_capacity grows.
 * Written only at the centralized mutation seams (Push, reclaim, SetBehavior,
 * ApplyRuntimeHook, ApplyOpKey, HooksFree) — see docs/UI_RENDERER_ARCHITECTURE.md §9.
 */
struct UITreeNodeSet
{
    int32_t* slots;
    int32_t* pos;
    int32_t count;
    int32_t cap;
    uint32_t pos_cap;
};

/** One open-addressed bucket: group id -> live nodes whose component_id>>16 matches. */
struct UITreeGroupBucket
{
    int group_id; /* -1 = empty */
    struct UITreeNodeSet nodes;
};

struct UITree
{
    struct UITreeComponent* components;
    uint32_t component_count;
    uint32_t component_capacity;
    int32_t root_index;
    /** Tail of root sibling list — O(1) append while baking large packs. */
    int32_t last_root_index;
    uint32_t generation;
    /** Monotonic source for UITreeComponent::incarnation. Zero is skipped. */
    uint32_t next_incarnation;
    /** Bumped every time `UITree_LayoutResolve` actually walks, i.e. every time
     *  a resolved box could have moved. `dirty_gen` does not cover this: layout
     *  re-resolves on `layout_stale`, `layout_force_full` and a changed root box,
     *  none of which raise a component's `is_dirty`. That gap is what made the
     *  emit retention gate call a frame quiet while a clip width moved 765 -> 807
     *  underneath it (measured once, at emit #5 of a 2,000-frame run). It is the
     *  third term of the gate, alongside `dirty_gen` and the hover id. */
    uint32_t layout_resolve_seq;
    /** Bumped only when a component_id is assigned or cleared (reclaim) — the id
     *  index depends on ids alone, so topology churn must not invalidate it. */
    uint32_t id_generation;
    /** Bumped wherever a component's `is_dirty` is raised, i.e. by every write
     *  that claims to change what a node draws — the tree half of the emit
     *  retention signal (Opt 11). `generation` is not a substitute: it tracks
     *  topology only, so a cc_settext that rewrites a label leaves it alone.
     *
     *  Deliberately conservative. It over-counts freely (a write of the same
     *  value still bumps), because over-counting only costs a missed skip,
     *  whereas under-counting reuses a stale list and freezes a panel. Anything
     *  that sets `is_dirty` must bump this in the same breath. */
    uint32_t dirty_gen;
    /** One byte per node, set by `emit_walk_node` for every node the last emit
     *  walk entered, cleared at the head of each walk. It is the reachability
     *  half of the retention signal: `UITree_MarkNodeDirty` skips the
     *  `dirty_gen` bump for a node this array says the walk never reached, so a
     *  closed interface ticking its 3D model stops defeating the gate.
     *
     *  "Reached" and "hidden" are not the same question, which is why this is a
     *  bitmap and not a `UITree_ComponentOrAncestorHidden` call: inactive
     *  sidebar tabs prune their whole mounted subtree with no hide bit set
     *  anywhere, and a dynamically-created child with no component_id cannot be
     *  looked up by that function at all. Both read as visible; neither is.
     *
     *  Sound because a node the walk did not enter can only become entered if
     *  some ancestor's traversal decision changed, and every write that changes
     *  one goes through a `dirty_gen` bump on that ancestor — which was itself
     *  entered, since it is the node that made the decision. That is why the
     *  bump sites inside uitree.c stay unfiltered: they are the ones that move
     *  hide bits and topology, and filtering them would break the argument.
     *
     *  Written through a `struct UITree const*` on purpose — the walk takes the
     *  tree read-only and this is scratch, not tree state. Allocated on first
     *  walk; `cap` trails `component_count` after a growth, and an index past
     *  it is treated as reached (conservative: a new node has never been
     *  walked, so it must not be skipped). */
    uint8_t* emit_visited;
    uint32_t emit_visited_cap;
    /** Head of the reclaimed-slot free-list (chained via component free_next). */
    int32_t free_head;
    /** Lazy id->index acceleration for UITree_FindByComponentId. Open-addressed,
     *  power-of-two, load factor <= 0.5. Preserves the dynamic-wins /
     *  lowest-index tie-break of the linear scan.
     *
     *  A full rebuild is O(component_count), so it must not run per mutation:
     *  UITree_Push inserts the new id incrementally (keeping `id_index_gen` in
     *  step with `id_generation`) and a reclaim tombstones just its own slot.
     *  Without either, cc_create's uid allocation rebuilt the whole map on every
     *  widget it made, which is quadratic in the tree size (~45% of frame time
     *  on rev230; the reclaim half of it stalled the QBD fight, where each chat
     *  line replaces 500 rows and so reclaims 500 ids).
     *
     *  A reclaim cannot name the replacement winner without rescanning, so it
     *  does not try: the slot keeps its key (probe chains must survive) and the
     *  value becomes a marker, resolved at most once by the next lookup for that
     *  id. See the `id_index_vals` encoding below. */
    int32_t* id_index_keys; /* component_id per slot, -1 = empty */
    /** >= 0 winning component index; -1 "winner reclaimed, rescan once";
     *  -2 "rescanned, no live component holds this id". */
    int32_t* id_index_vals;
    uint32_t id_index_cap;  /* slot count (power of two, 0 = unallocated) */
    uint32_t id_index_gen;  /* generation the map was last built for */
    /** Slots holding a key whose value is a marker. They never free a slot, so
     *  they count against the load factor or probing could never terminate. */
    uint32_t id_index_tombs;
    uint8_t id_index_valid; /* 0 until first successful build */
    /** Cached layout scratch (see UITree_LayoutResolve). `order`/`depth` depend
     *  only on tree topology and are recomputed only when `generation` changes;
     *  `changed` carries "this node's box moved in this pass" down to the
     *  children that read it. All three are reused across calls to avoid a
     *  per-frame calloc/free. */
    int* layout_order;
    int* layout_depth;
    uint8_t* layout_changed;
    uint32_t layout_cap;         /* allocated length of each layout buffer */
    uint32_t layout_order_count; /* live (non-freed) entries in layout_order */
    uint32_t layout_order_gen;   /* generation order/depth were computed for */
    uint8_t layout_order_valid;
    /** Nodes whose own layout inputs changed since the last resolve.
     *
     *  A node's box is a pure function of its own fields and its parent's
     *  box, so a resolve only has to visit the nodes that were invalidated
     *  and, transitively, the descendants of those whose box actually moved.
     *  Sweeping all of them to find the handful that moved cost 6,918 node
     *  visits a frame to do ~80 nodes of work.
     *
     *  Only invalidators that can NAME a node contribute here. The ones that
     *  cannot -- a root-box change, a frame-layer rebind, anything reaching
     *  UITree_LayoutInvalidate -- set `layout_dirty_overflow` instead, and
     *  the resolve falls back to the full sweep. So does a run that produces
     *  more seeds than the list holds: past that many distinct nodes the
     *  sweep is the cheaper answer anyway. The fallback is what makes this
     *  safe -- the worst case is exactly the old behaviour. */
    int32_t* layout_dirty;
    uint32_t layout_dirty_count;
    uint32_t layout_dirty_cap;
    uint8_t layout_dirty_overflow;
    /** Forces the next resolve to recompute every node instead of only the ones
     *  whose own box or parent's box changed. Set when a box moved outside the
     *  resolve's own bookkeeping (a JIT chain resolve that could not record into
     *  `layout_changed`), where the per-node flags no longer describe which
     *  descendants went stale. */
    uint8_t layout_force_full;
    /** Live nodes whose behavior carries CS1 scripts (see UITree_HasCS1Scripts).
     *  Maintained by UITree_SetBehavior and slot reclaim, the only two places a
     *  node's `behavior.scripts_count` can change. */
    uint32_t cs1_script_nodes;
    /** Live nodes with `drag_active` set (see UITree_HasActiveDrag). Write it
     *  through UITree_SetComponentDragActive rather than touching `drag_active`
     *  directly, so the count cannot drift. */
    uint32_t drag_active_nodes;
    uint16_t next_dynamic_uid;
    /** Mounted sub-interfaces (TS WidgetManager.interfaceParents). */
    struct UITreeInterfaceParent interface_parents[UITREE_INTERFACE_PARENT_MAX];
    int interface_parent_count;
    /** SETANTIDRAG — suppress new drag initiation while set. */
    uint8_t anti_drag;
    /** CC/IF_DRAGPICKUP staged for the input loop (reference dragTryPickup).
     *  The CS2 host cannot reach UIInteraction::input_state, so it writes
     *  these and InteractFrame consumes them into a live drag source. */
    uint8_t pending_drag_pickup;
    int pending_drag_pickup_id;
    int pending_drag_pickup_x;
    int pending_drag_pickup_y;
    /** Set when any node's layout is invalidated (position/size/topology
     *  mutation); cleared by UITree_LayoutResolve. Lets CS2 geometry getters
     *  lazily re-resolve mid-script (reference WidgetManager.ensureLayout —
     *  scripts read computed dims immediately after if_setsize/if_setposition,
     *  e.g. dropdown scrollbar dragger sizing).
     *
     *  It is also what lets UITree_LayoutResolve return without walking the
     *  tree, so EVERY write to a layout input must set it (position fields,
     *  parent links, and an RS_LAYER's scroll extent — see layout_parent_box).
     *  Use UITree_LayoutInvalidateBoxes rather than writing it directly. A
     *  writer that forgets it leaves the affected boxes a frame stale. */
    uint8_t layout_stale;
    /** State the last full resolve was computed against, so a resolve with the
     *  same topology, the same root box and no invalidation since can be
     *  skipped: idle frames run CS2 (the gameframe clock varc ticks every
     *  frame) without touching a single layout input. */
    uint32_t layout_resolved_gen;
    uint8_t layout_resolved_valid;
    /** Set once the root box below has been resolved against, and — unlike
     *  `layout_resolved_valid` — never cleared by an invalidation. An
     *  invalidation says some node's box changed, not that the canvas resized,
     *  so clearing it would make every root-level node recompute on any
     *  mutation anywhere in the tree. */
    uint8_t layout_resolved_root_valid;
    int layout_resolved_root_x;
    int layout_resolved_root_y;
    int layout_resolved_root_w;
    int layout_resolved_root_h;
    /** Minimap/compass mask polarity: 1 = the mask sprite's *opaque* pixels are
     *  the window, 0 = its transparent ones are (the default).
     *
     *  The widget field holds a different kind of art in the two eras. OldSchool
     *  ships a corner cover — opaque everywhere *outside* the round window, with
     *  a transparent hole (sprite 1178 on interface 161) — which the reference
     *  draws over the finished minimap and we apply as an inverted mask for the
     *  same result in one pass. Rev 634 ships a stencil, opaque *inside* the
     *  window (sprite 1185 on interface 548), and its client keeps the non-zero
     *  span per row (Class46.method425). Same field, opposite sense: reading a
     *  634 mask with the OldSchool rule draws the map everywhere except the
     *  circle and leaves the minimap a dark disc.
     *
     *  Set once from the cache profile, because it is a property of the era's
     *  art rather than of any one widget. */
    uint8_t mask_keep_opaque;
    /** Key bindings resolved at bake from the [hotkey:…] sections. Lives on the
     *  tree because the node indices it holds only mean anything for this tree;
     *  a rebuild re-resolves them from the manifest. */
    struct UITreeHotkey hotkeys[UITREE_HOTKEY_MAX];
    int hotkey_count;
    /**
     * Live node sets — maintained incrementally at Push / reclaim / predicate
     * writers. Consumers walk `set.slots[0..count)` (slot indices) instead of
     * scanning component_count. See UITreeNodeSet and §9 of the architecture doc.
     */
    struct UITreeNodeSet models;
    struct UITreeNodeSet timer_hooks;
    struct UITreeNodeSet key_hooks;
    struct UITreeNodeSet wheel_hooks;
    struct UITreeNodeSet opkeys;
    struct UITreeNodeSet client_code;
    struct UITreeNodeSet resize_hooks;
    struct UITreeNodeSet sub_change_hooks;
    struct UITreeNodeSet scroll_layers;
    /** Debug-overlay nodes. Not a singleton: a tree may carry more than one
     *  (see UITree_DebugOverlaySetFontIds), and the emit pass has to reach all
     *  of them. A set rather than a bare count because the count would only let
     *  the pass skip when there are none, and every lane's manifest declares one
     *  — switched off, drawing nothing, and still costing a whole-tree descent
     *  per frame to be found again. */
    struct UITreeNodeSet debug_overlays;
    /** Singleton builtins; -1 when absent. Maintained on Push / reclaim. */
    int32_t world_index;
    int32_t worldmap_index;
    /** The `entity_overlay` builtin, which is both the host-drawn health-bar /
     *  hitsplat layer and the PARENT of every scripted entity overlay
     *  (game/rs_entity_overlay.h). A singleton for the same reason the world is:
     *  the overlay ops reach for it on every create, and a whole-tree scan per
     *  create is what the fishing-spot scripts would pay sixteen times a tick. */
    int32_t entity_overlay_index;
    /** Open-addressed group_id -> nodes with that component_id high half. */
    struct UITreeGroupBucket* group_map;
    uint32_t group_map_cap;
    /** A plugin layout's hold on this frame (ui/uitree_frame.h), or NULL --
     *  which it is on every lane until a layout plugin claims one. */
    struct UITreeFrameLayout* frame_layout;
};

struct UITreeNodeSpec
{
    enum UITreeComponentType type;
    int component_id;

    int x;
    int y;
    int width;
    int height;
    int anchor_x;
    int anchor_y;

    uint8_t always_dirty;
    uint8_t dynamic;
    int dynamic_child_index;
    uint8_t has_position;
    uint8_t slot_tag; /* enum UITreeSlotTag */
    /** Interned semantic role, or 0. @see UITreeComponent::role_id. */
    uint16_t role_id;
    /** dat2 noClickThrough on a LAYER; CS2 can also raise it later. */
    uint8_t no_click_through;
    struct UITreeElemPosition position;
    struct UITreeBehavior const* behavior;

    union
    {
        struct
        {
            int scene_id;
            int atlas_index;
            /* COMPASS only: inverted circular mask, see UITreeComponent. */
            int mask_scene_id;
            int mask_atlas_index;
        } sprite;
        /* UIELEM_BUILTIN_INKWELL: a CHOICE of artwork, not a binding to one.
         * @see the matching member on UITreeComponent. */
        struct
        {
            int style;
            int walk_color;
            int interact_color;
        } inkwell;
        struct
        {
            int scene_id;
            int atlas_index;
            int scene_id_active;
            int atlas_index_active;
            int tabno;
        } redstone_tab;
        struct
        {
            int scene_id;
            /* The pack's placeholder graphic, kept as the inverted alpha mask
             * (map shows where it is transparent). 0 = draw unmasked. */
            int mask_scene_id;
            int mask_atlas_index;
        } minimap;
        struct
        {
            uint8_t level_mask;
        } world;
        struct
        {
            int tabno;
            int componentno;
            int inv_source_id;
            uint8_t selected; /* INI selected= (see UITreeComponent) */
        } sidebar;
        struct
        {
            int font_id;
            int color;
            int center;
            int y_align;
            /** revconfig `baseline=`: the layout row's y IS the text baseline,
             *  which is how both references write every text coordinate
             *  (font.drawString(s, x, y)). Without it a row has to carry the
             *  box top instead, and the reference's own numbers stop being
             *  usable as written. */
            int baseline;
            int line_height;
            int shadowed;
            char const* text;
            char const* text_active;
        } rs_text;
        struct
        {
            int scene_id;
            int atlas_index;
            int scene_id_active;
            int atlas_index_active;
            uint8_t graphic_hitbox_only;
            uint8_t tiled;
            int outline;
            int graphic_shadow;
            uint8_t flip_h;
            uint8_t flip_v;
            /** IF3 spriteAngle / CC_SET2DANGLE: 65536 = one full turn (not the
             *  2048-per-turn camera-yaw scale the compass/minimap use). */
            int sprite_angle_r2pi65536;
        } rs_graphic;
        struct
        {
            int gamecache_model_id;
            /* Model drawn instead of gamecache_model_id while the widget is CS1-
             * active (reference getTempModel: model2Type/model2Id). -1 = none,
             * and none means draw nothing at all — the 254 special-attack bar
             * is ten dark cover segments over a green bar, each cover dropping
             * out as spec energy passes its threshold. */
            int active_model_id;
            int zoom;
            int xan;
            int yan;
            int zan;
            int rotate_x_speed;
            int rotate_y_speed;
            int x_offset;
            int y_offset;
            uint8_t orthog;
            uint8_t fixed_zoom;
            /* Model animation sequence set via IF/CC_SETMODELANIM, or -1 = none.
             * anim_frame is advanced by the client tick driver; anim_frame_cycle
             * accumulates elapsed 50hz cycles toward the current frame's length. */
            int anim_seq_id;
            int anim_frame;
            int anim_frame_cycle;
            /* Hold anim_frame instead of advancing it. The player-design
             * preview poses the composite once (reference: model.animate(
             * SeqType.list[readyanim].frames[0]) inside the idkDesignRedraw
             * rebuild) and never ticks it; only modelYAn spins. */
            uint8_t anim_hold;
        } rs_model;
        struct
        {
            int color;
            int filled;
        } rs_rect;
        struct
        {
            int color;
            /** 1 = the whole disc, 0 = a `line_width`-pixel band along the arc
             *  (reference NXTPix2D::DrawCircularArc's inner radius). */
            int filled;
            int line_width;
            /** CC/IF_SETARC, 65536 to a full turn. 0 is straight up and the
             *  sweep runs clockwise, so a drain reads as a clock hand. */
            int arc_start;
            int arc_end;
        } rs_arc;
        struct
        {
            int inv_source_id;
            int cols;
            int rows;
            int margin_x;
            int margin_y;
            /** Item drag allowed (objSwap || objReplace); 1 = draggable. */
            int can_drag;
            /** Show ObjType-op rows (objOps); 0 hides Drop/wield/op1-5. */
            int obj_ops;
            /** Show the "Use" row (objUse); 0 hides it. */
            int obj_use;
            int const* inv_slot_offset_x;
            int const* inv_slot_offset_y;
            int const* inv_slot_bg_scene_id;
            int const* inv_slot_bg_atlas_index;
        } rs_inv;
        struct
        {
            int obj_id;
            int obj_count;
            int scene_id;
            int atlas_index;
        } cc_obj;
        struct
        {
            int scroll_height;
            int scroll_width;
        } rs_layer;
        struct
        {
            int scene_id;
            int atlas_index;
            int tabno;
        } tab_icon;
        struct
        {
            int font_id;
        } minimenu;
        struct
        {
            int font_id;
        } hovertext;
        struct
        {
            int font_id;
            /** revconfig color=; text colour of the countdown line. */
            int color;
        } reboot_timer;
        /* By value in the spec, by pointer on the component: the spec is one
         * short-lived stack struct, the component is one of thousands. */
        struct UITreeLoginInputConfig login_input;
        struct
        {
            int scene_id;
            int atlas_index;
            int action;
        } login_button;
        struct
        {
            int scene_id;
            int atlas_index;
            int scene_id_on;
            int atlas_index_on;
            int action;
            int toggle;
        } login_toggle;
        struct
        {
            int index;
            int font_id;
            int color;
            int center;
            int shadowed;
        } login_message;
        struct
        {
            int color;
            int px_per_percent;
        } title_progress;
        struct
        {
            int font_id;
            int color;
            int center;
            int shadowed;
        } title_progress_text;
        struct
        {
            /** enum TitleFlameSide; ui/ stays a leaf so it travels as an int. */
            int side;
            /* Where the fire sits inside the column it burns in. The
             * column is blitted over the wall it was cut from, so these
             * lean the FIRE outward rather than the wall with it. */
            int bias;
            int sway;
            int run;
            int row;
            /** enum TitleFlameBlur. */
            int blur;
        } title_flames;
        struct UITreeDebugOverlayConfig debug_overlay;
        struct UITreeChatConfig chat;
        struct UITreeChatButtonConfig chat_button;
        struct
        {
            int color;
            int line_width;
            int horizontal;
        } rs_line;
        struct
        {
            int inv_source_id;
            int cols;
            int rows;
            int margin_x;
            int margin_y;
            int font_id;
            int color;
            int center;
            int shadowed;
        } rs_inv_text;
    } u;
    struct UITreeMenuOptions menu_options;
    /** OR of enum UITreeHotkeyEffect (see UITreeComponent.hotkey_effects). */
    uint32_t hotkey_effects;
};

char const*
UITree_ComponentTypeStr(enum UITreeComponentType type);

struct UITree*
UITree_New(uint32_t hint);

void
UITree_Free(struct UITree* tree);

/** Tear down every live node and mount record, keeping the tree object for a
 *  subsequent root open (IF_OPENTOP remount). Freed slots stay on the free-list. */
void
UITree_Clear(struct UITree* tree);

void
UITree_MarkAllDirty(struct UITree* tree);

/**
 * Make a detached LAYER under the `entity_overlay` builtin, for one scripted
 * entity overlay (game/rs_entity_overlay.h).
 *
 * `sub_id` is the overlay's index, which is what makes the node findable again
 * and what a rebuild replaces in place. The box is set absolute and at (0,0);
 * the App moves it to the projected anchor each frame, because where it belongs
 * is a fact about the camera and not about the tree.
 *
 * Returns the node index, or -1 when the tree has no `entity_overlay` builtin
 * -- a pack that declares none draws no overlays, which is a manifest decision
 * rather than an error.
 */
int32_t
UITree_EntityOverlayCreateLayer(struct UITree* tree, int sub_id, int width, int height);

/**
 * Move a layer made by `UITree_EntityOverlayCreateLayer` to its projected
 * parent-relative position. This invalidates both incremental layout and the
 * retained emit list; writing `position.x/y` directly does neither completely.
 *
 * Returns false when `idx` is not a live scripted-overlay layer.
 */
bool
UITree_EntityOverlaySetLayerPosition(struct UITree* tree, int32_t idx, int x, int y);

void
UITree_MarkNodeDirty(
    struct UITree* tree,
    int32_t idx);

/**
 * Mark a node whose visibility changed. Unlike UITree_MarkNodeDirty this
 * always advances retained-output identity: a previously pruned node was not
 * visited by the last emit, but revealing it necessarily changes the next one.
 */
void
UITree_MarkNodeVisibilityDirty(
    struct UITree* tree,
    int32_t idx);

/** Clear dirty after a successful emit (always_dirty nodes stay emit-eligible). */
void
UITree_ClearNodeDirty(
    struct UITree* tree,
    int32_t idx);

/** True if node should emit this frame: is_dirty || always_dirty. */
bool
UITree_NodeNeedsEmit(struct UITreeComponent const* component);

/** Force dirty on types that redraw every frame (world/minimap/compass/cross/minimenu). */
void
UITree_MarkFrameAlwaysDirtyTypes(struct UITree* tree);

int32_t
UITree_FindByComponentId(
    struct UITree const* tree,
    int component_id);

/**
 * Re-point every debug-overlay component at a different set of baked fonts.
 *
 * The chrome's scale can change after the tree is built -- the window moves to
 * a display of a different pixel density, or the editor's scale row is changed
 * -- and the font ids were resolved once, at bake time. Without this the
 * overlay lays out at the new size and draws at the old one, which reads as a
 * broken font rather than as a stale id.
 *
 * @param font_id_* scene font ids from UITreeSceneBridge_EnsureDebugFont.
 */
void
UITree_DebugOverlaySetFontIds(
    struct UITree* tree,
    int font_id_small,
    int font_id_menu,
    int font_id_body);

void
UITree_WalkAdvance(
    struct UITree const* tree,
    int32_t* io_current,
    int32_t* stack,
    int* io_stack_top,
    int stack_max,
    bool current_visible);

void
UITree_SetBehavior(
    struct UITree* tree,
    int32_t idx,
    struct UITreeBehavior const* src);

int32_t
UITree_Push(
    struct UITree* tree,
    int32_t parent_index,
    struct UITreeNodeSpec const* spec);

/** Link an existing unlinked component under parent (-1 = root). */
void
UITree_LinkUnderParent(
    struct UITree* tree,
    int32_t parent_index,
    int32_t child_index);

/** Move child_index under new_parent_index (-1 = root list). Preserves child's subtree. */
void
UITree_Reparent(
    struct UITree* tree,
    int32_t child_index,
    int32_t new_parent_index);

void
UITree_ClearSidebarChildren(
    struct UITree* tree,
    int32_t sidebar_idx);

/** Detach every child of `owner_idx` (slot mounts: clear before re-bake). */
void
UITree_ClearChildren(
    struct UITree* tree,
    int32_t owner_idx);

int32_t
UITree_FindChildBySubid(
    struct UITree const* tree,
    int32_t parent_index,
    int parent_component_id,
    int sub_id);

int32_t
UITree_CcCreate(
    struct UITree* tree,
    int32_t parent_index,
    int parent_component_id,
    int widget_type,
    int sub_id);

/** CC_COPY: clone the dynamic child at src_sub_id into slot dst_sub_id under the
 *  same parent (replace-in-slot, like UITree_CcCreate). Only the widget's own
 *  payload is cloned — the copy starts with no children. Returns the new node
 *  index, or -1 when the source slot is empty. */
int32_t
UITree_CcCopy(
    struct UITree* tree,
    int32_t parent_index,
    int parent_component_id,
    int src_sub_id,
    int dst_sub_id);

/** CC_DELETE: remove one dynamic child (and its subtree), leaving its siblings
 *  and their sub-ids in place. A static component is refused — see the
 *  implementation. */
void
UITree_CcDelete(
    struct UITree* tree,
    int32_t index);

void
UITree_CcDeleteAll(
    struct UITree* tree,
    int32_t parent_index);

int
UITree_CollectDynamicChildIndices(
    struct UITree const* tree,
    int parent_component_id,
    int start_index,
    int* out_indices,
    int out_cap);

/**
 * Typed runtime mutation API for callers that already hold a component-array
 * index.  These setters own all cache invalidation implied by their property:
 * callers must not write the corresponding UITreeComponent fields and then
 * try to reproduce the dirty/layout bookkeeping themselves.
 *
 * A repeated value is a successful no-op.  False means the index is not a live
 * component, or (where applicable) has the wrong component type.  The
 * corresponding component-id UITree_Apply* entry points below are lookup
 * wrappers around these functions.
 */
bool
UITree_SetHideAt(struct UITree* tree, int32_t idx, int hide);

/** Publish one cached CS1 active result as a visual mutation. */
bool
UITree_SetCS1ActiveAt(struct UITree* tree, int32_t idx, int active);

/** Publish one cached CS1 placeholder value as a visual mutation. */
bool
UITree_SetCS1ValueAt(struct UITree* tree, int32_t idx, int value_index, int value);

/** Set frame-layout suppression with unfiltered reachability invalidation. */
bool
UITree_SetFrameHiddenAt(struct UITree* tree, int32_t idx, int hidden);

/** As above for `screen_hidden`: hide the subtree of a title screen that is
 *  not the current one. */
bool
UITree_SetScreenHiddenAt(struct UITree* tree, int32_t idx, int hidden);

/** Set camera-owned visibility of a scripted entity-overlay layer. */
bool
UITree_SetProjectionHiddenAt(struct UITree* tree, int32_t idx, int hidden);

bool
UITree_SetTextAt(struct UITree* tree, int32_t idx, char const* text);

bool
UITree_SetGraphicAt(
    struct UITree* tree,
    int32_t idx,
    int scene_id,
    int atlas_index);

bool
UITree_SetColourAt(struct UITree* tree, int32_t idx, int colour);

bool
UITree_SetFillColourAt(struct UITree* tree, int32_t idx, int colour);

/** Set the component's 0..255 transparency value (0 is opaque). */
bool
UITree_SetTransparencyAt(struct UITree* tree, int32_t idx, int transparency);

/** Set the resolved scene-font id used by the builtin minimenu expansion. */
bool
UITree_SetMinimenuFontAt(struct UITree* tree, int32_t idx, int font_id);

/** Set a RS_ARC widget's 65536-per-turn start and end angles. */
bool
UITree_SetArcAnglesAt(struct UITree* tree, int32_t idx, int arc_start, int arc_end);

/** Replace the scene model drawn by a RS_MODEL widget. */
bool
UITree_SetModelAt(struct UITree* tree, int32_t idx, int model_id);

/**
 * Set all fields written by IF/CC_SETMODELANGLE in one paint mutation.  A
 * non-positive zoom preserves the current zoom, matching the opcode contract.
 */
bool
UITree_SetModelPoseAt(
    struct UITree* tree,
    int32_t idx,
    int x_offset,
    int y_offset,
    int x_angle,
    int y_angle,
    int z_angle,
    int zoom);

bool
UITree_SetPositionAt(struct UITree* tree, int32_t idx, int x, int y);

/** Replace a node's complete explicit XY box in one geometry mutation. */
bool
UITree_SetXYBoxAt(
    struct UITree* tree,
    int32_t idx,
    int x,
    int y,
    int width,
    int height);

bool
UITree_SetSizeAt(struct UITree* tree, int32_t idx, int width, int height);

bool
UITree_SetPositionModesAt(
    struct UITree* tree,
    int32_t idx,
    int x,
    int y,
    int x_mode,
    int y_mode);

bool
UITree_SetSizeModesAt(
    struct UITree* tree,
    int32_t idx,
    int width,
    int height,
    int width_mode,
    int height_mode);

bool
UITree_SetScrollSizeAt(
    struct UITree* tree,
    int32_t idx,
    int scroll_width,
    int scroll_height);

bool
UITree_SetScrollPosAt(struct UITree* tree, int32_t idx, int scroll_x, int scroll_y);

bool
UITree_ApplyHide(
    struct UITree* tree,
    int component_id,
    int hide);

/**
 * Write one entry of a component's runtime param table, replacing any entry
 * already under `param_id`. False when the component is not in the tree or the
 * table could not grow. `str` NULL stores `value` as an int param; otherwise the
 * string is copied and `value` is ignored.
 */
bool
UITree_ApplyComponentParam(
    struct UITree* tree,
    int component_id,
    int param_id,
    int value,
    char const* str);

/**
 * Read one int entry back. False when the component is not in the tree, has no
 * entry under `param_id`, or that entry holds a string — the caller answers that
 * with the ParamType default, which is what a script's `= -1` guard is testing
 * for.
 */
bool
UITree_ComponentParamGet(
    struct UITree const* tree,
    int component_id,
    int param_id,
    int* out_value);

/**
 * Read one string entry back, or NULL when there is no string entry under
 * `param_id`. The result is owned by the component and dies with it.
 */
char const*
UITree_ComponentParamGetStr(
    struct UITree const* tree,
    int component_id,
    int param_id);

bool
UITree_ApplyClickMask(
    struct UITree* tree,
    int component_id,
    int32_t click_mask);

bool
UITree_ApplyText(
    struct UITree* tree,
    int component_id,
    char const* text);

bool
UITree_ApplyGraphic(
    struct UITree* tree,
    int component_id,
    int scene_id,
    int atlas_index);

bool
UITree_ApplyColour(
    struct UITree* tree,
    int component_id,
    int colour);

bool
UITree_ApplyFillColour(
    struct UITree* tree,
    int component_id,
    int colour);

bool
UITree_ApplyPosition(
    struct UITree* tree,
    int component_id,
    int x,
    int y);

bool
UITree_ApplySize(
    struct UITree* tree,
    int component_id,
    int width,
    int height);

bool
UITree_ApplyPositionModes(
    struct UITree* tree,
    int component_id,
    int x,
    int y,
    int x_mode,
    int y_mode);

bool
UITree_ApplySizeModes(
    struct UITree* tree,
    int component_id,
    int width,
    int height,
    int width_mode,
    int height_mode);

bool
UITree_ApplyGraphicTiled(
    struct UITree* tree,
    int component_id,
    int tiled);

bool
UITree_ApplyGraphicOutline(
    struct UITree* tree,
    int component_id,
    int outline);

bool
UITree_ApplyGraphicShadow(
    struct UITree* tree,
    int component_id,
    int shadow_colour);

/** IF/CC_SET2DANGLE: rotate a graphic about its own centre, 65536 per turn. */
bool
UITree_ApplyGraphic2DAngle(
    struct UITree* tree,
    int component_id,
    int angle_r2pi65536);

bool
UITree_ApplyScrollSize(
    struct UITree* tree,
    int component_id,
    int scroll_width,
    int scroll_height);

bool
UITree_ApplyScrollPos(
    struct UITree* tree,
    int component_id,
    int scroll_x,
    int scroll_y);

/* num_mode is the SETOBJECT opcode variant's count-text rule: 0 = draw when
 * stackable (plain SETOBJECT, and the zero-init default for cells filled
 * outside CS2), 1 = always (_ALWAYS_NUM), 2 = never (_NONUM). */
bool
UITree_ApplyObject(
    struct UITree* tree,
    int component_id,
    int obj_id,
    int obj_count,
    int scene_id,
    int atlas_index,
    int num_mode);

bool
UITree_ApplyModel(
    struct UITree* tree,
    int component_id,
    int model_id);

bool
UITree_ApplyModelTransparent(
    struct UITree* tree,
    int component_id,
    int transparent);

bool
UITree_ApplyModelAngle(
    struct UITree* tree,
    int component_id,
    int xan,
    int yan,
    int zoom);

/** Set a MODEL widget's 2D offsets (reference modelOffsetX/Y). Part of an
 * objtype's own presentation rather than a widget property: an obj icon is
 * composed from zoom2d, the three angles AND xof2d/yof2d, and dropping the
 * offsets leaves off-centre models — `arrow_shaft` carries yof2d -29 and
 * projects clean out of its cell without it. */
bool
UITree_ApplyModelOffset(
    struct UITree* tree,
    int component_id,
    int x_offset,
    int y_offset);

bool
UITree_ApplyModelRotateSpeed(
    struct UITree* tree,
    int component_id,
    int x_speed,
    int y_speed);

/** Set a MODEL widget's animation sequence (reference IF_SETANIM / modelAnim);
 * -1 clears it. Restarts playback only when the sequence actually changes —
 * re-applying the one already running must not reset the frame counters. */
bool
UITree_ApplyModelAnim(
    struct UITree* tree,
    int component_id,
    int anim_seq_id);

bool
UITree_ApplyTextFont(
    struct UITree* tree,
    int component_id,
    int font_id);

bool
UITree_ApplyTextAlign(
    struct UITree* tree,
    int component_id,
    int h_align,
    int v_align,
    int line_height);

bool
UITree_ApplyTextShadow(
    struct UITree* tree,
    int component_id,
    int shadowed);

bool
UITree_ApplyTargetPriority(
    struct UITree* tree,
    int component_id,
    int priority);

bool
UITree_ApplyForceLeftClick(
    struct UITree* tree,
    int component_id,
    int enabled);

bool
UITree_ClearOpSubmenu(
    struct UITree* tree,
    int component_id,
    int op_index);

/** str_mask bit i marks arg position i as a string; strs[0..str_argc) are the
 * string values in position order. Pass 0/NULL/0 for int-only hooks. */
bool
UITree_ApplyRuntimeHook(
    struct UITree* tree,
    int component_id,
    struct UITreeRuntimeScriptHook* slot,
    int script_id,
    int const* argv,
    int argc,
    uint64_t str_mask,
    char const* const* strs,
    int str_argc);

bool
UITree_ApplyOpBase(
    struct UITree* tree,
    int component_id,
    char const* text);

bool
UITree_ApplyTargetVerb(
    struct UITree* tree,
    int component_id,
    char const* text);

/**
 * CC/IF_SETOPKEY and the SETOPTKEY variants. op_index is 1..10, where 10 is the
 * typed-key slot. pair_count == 0, or a negative first key_char, clears the
 * slot. Returns false when the component or op_index is out of range.
 */
bool
UITree_ApplyOpKey(
    struct UITree* tree,
    int component_id,
    int op_index,
    int const* key_chars,
    int const* key_codes,
    int pair_count);

/** CC/IF_SETOPKEYRATE and the OPT variants. enabled mirrors tick_rate != 0. */
bool
UITree_ApplyOpKeyRate(
    struct UITree* tree,
    int component_id,
    int op_index,
    int rate,
    int enabled);

/** CC/IF_SETOPKEYIGNOREHELD and the OPT variants. */
bool
UITree_ApplyOpKeyIgnoreHeld(
    struct UITree* tree,
    int component_id,
    int op_index);

int
UITree_GetLayoutWidth(
    struct UITree const* tree,
    int component_id);

int
UITree_GetLayoutHeight(
    struct UITree const* tree,
    int component_id);

/** Parent-relative X for CC_GETX / IF_GETX (interfacex UITreeX_GetPosX). */
int
UITree_GetRelativeX(
    struct UITree const* tree,
    int component_id);

/** Parent-relative Y for CC_GETY / IF_GETY (interfacex UITreeX_GetPosY). */
int
UITree_GetRelativeY(
    struct UITree const* tree,
    int component_id);

/** Client.ts hide: only hide nodes are gated on hover id match. */
bool
UITree_ComponentVisibleById(
    struct UITreeComponent const* component,
    int hovered_component_id);

bool
UITree_ComponentHoveredByIds(
    int component_id,
    struct UITreeHoverIds const* hover_ids);

bool
UITree_ComponentVisibleByHoverIds(
    struct UITreeComponent const* component,
    struct UITreeHoverIds const* hover_ids);

bool
UITree_ComponentIsClickable(struct UITreeComponent const* component);

bool
UITree_ComponentHasMenuOptions(struct UITreeComponent const* component);

bool
UITree_TypeIsAlwaysDirtyFrame(enum UITreeComponentType type);

/** InterfaceParent mount table (TS WidgetManager.interfaceParents). */
int
UITree_InterfaceParentFind(
    struct UITree const* tree,
    int container_uid);

int
UITree_InterfaceParentSet(
    struct UITree* tree,
    int container_uid,
    int group_id,
    int type);

void
UITree_InterfaceParentClear(
    struct UITree* tree,
    int container_uid);

int
UITree_InterfaceParentIsMountedGroup(
    struct UITree const* tree,
    int group_id);

/**
 * Reclaim every root of interface `group_id` (and its subtree).
 *
 * Used when closing or replacing a mount in `chatbox:chatmodal`: dialogue
 * packs (chat_left / chat_right / chatmenu / …) must not linger as hidden
 * nodes, or the next IF_SETTEXT can update a shadowed copy while a remount
 * reuses the bake and draws a previous conversation's string. Inventory
 * panels keep the hide-and-reuse path; only chatmodal calls this.
 *
 * Same root selection as the hide-on-close loop: a node whose parent is also
 * in `group_id` is pack-internal and is reclaimed with its ancestor.
 */
void
UITree_ReclaimInterfaceGroup(
    struct UITree* tree,
    int group_id);

/**
 * Mount type of `child` when it is the root of a sub-interface mounted under
 * `container_uid` (0 modal, 1 overlay, 3 tab/sidemodal — IF_OPENSUB's own
 * argument), or -1 when it is an ordinary child.
 *
 * The distinction remains load-bearing for draw/layout behavior and world
 * input: a modal blocks the scene across its clipped mount-host rectangle,
 * while overlay/tab mounts remain transparent unless they declare
 * noClickThrough themselves.
 */
int
UITree_ChildMountType(
    struct UITree const* tree,
    int container_uid,
    struct UITreeComponent const* child);

/** 1 if anything is mounted on this container at all.
 *
 * Answers the per-child UITree_ChildMountType question once for the whole
 * child list: the emit walk asks it for every child of every node on both the
 * draw and the drag pass, and all but a handful of containers hold no mount. */
int
UITree_ContainerHasMounts(
    struct UITree const* tree,
    int container_uid);

/** Set or clear a component's `drag_active`, keeping `drag_active_nodes` in step.
 *
 * `idx` must name a live component. Idempotent: setting an already-set flag (or
 * clearing an already-clear one) does not move the count. */
void
UITree_SetComponentDragActive(
    struct UITree* tree,
    int32_t idx,
    int active);

/** 1 if any live node is mid-drag.
 *
 * The emit walk's second pass exists only to redraw deferred drag subtrees on
 * top of everything else, so with nothing being dragged it is a whole-tree
 * descent that emits nothing. */
static inline int
UITree_HasActiveDrag(struct UITree const* tree)
{
    assert(tree);
    return tree->drag_active_nodes > 0;
}

/** 1 if any live node carries CS1 (if1) behavior scripts.
 *
 * The CS1 evaluation pass runs on every 20ms tick and its first act is to scan
 * the whole tree for nodes to evaluate. An if3/CS2 tree has none at all, and at
 * a few thousand nodes of ~1.7 KB each that scan is pure memory traffic. */
static inline int
UITree_HasCS1Scripts(struct UITree const* tree)
{
    assert(tree);
    return tree->cs1_script_nodes > 0;
}

/** 1 if a top-level root should be shown/hovered/clicked; 0 for an unplaced
 *  orphan interface group (auto-mounted for CS2 property access, not displayed). */
int
UITree_RootIsDisplayable(
    struct UITree const* tree,
    int32_t root);

/** click_mask bits 17–19. */
static inline int
UITree_ClickMaskDragDepth(int32_t click_mask)
{
    return (click_mask >> UITREE_FLAG_DRAG_DEPTH_SHIFT) & UITREE_FLAG_DRAG_DEPTH_MASK;
}

/**
 * Node index of src's drag render area (clamp + script coordinate space), or
 * -1 when the widget drags freely. cc_setdraggable(parentUid, childIndex) is
 * resolved eagerly by the CS2 host to the child's component_id (child_index
 * left -1). Lazy form (uid=parent, child_index>=0) still resolves
 * parent.children[childIndex] and falls back to the parent if missing.
 */
int32_t
UITree_ResolveDragRenderArea(
    struct UITree const* tree,
    struct UITreeComponent const* src);

/** True if widget can start a drag (TS isWidgetDraggable). */
int
UITree_ComponentIsDraggable(struct UITreeComponent const* c);

/** True if node is a valid drop target (FLAG_DRAG_ON or drag/op handlers). */
int
UITree_ComponentIsDropTarget(struct UITreeComponent const* c);

/**
 * Hit-test for drop under (px,py), excluding exclude_component_id.
 * Visits InterfaceParent mounts after normal children (draw/hit order).
 * Returns component_id or -1.
 */
int
UITree_FindDropTarget(
    struct UITree const* tree,
    int px,
    int py,
    int exclude_component_id);

/** Exact-node form used by retained drag gestures. Returns the node index and
 * optionally its component id, or -1 when no drop target is painted there. */
int32_t
UITree_FindDropTargetNode(
    struct UITree const* tree,
    int px,
    int py,
    int exclude_component_id,
    int* out_component_id);

/** Cache/script hide check, including InterfaceParent mount containers. */
int
UITree_ComponentOrAncestorHidden(
    struct UITree const* tree,
    int component_id);

/** Display/input hide check, also including plugin-frame suppression. */
int
UITree_ComponentOrAncestorDisplayHidden(
    struct UITree const* tree,
    int component_id);

/**
 * The node-index form of ComponentOrAncestorDisplayHidden.
 *
 * Input gestures keep node indices while they are active, and a revconfig
 * builtin may have no component id at all.  Those callers must still observe
 * plugin-frame suppression as a true display:none subtree rather than
 * continuing a press/drag against an element which is no longer painted.
 */
int
UITree_NodeOrAncestorDisplayHidden(
    struct UITree const* tree,
    int32_t node_index);

/** The replacement-overlay visibility query: identical to the display-hidden
 * form except the target node's own replacement_hidden is the tombstone being
 * tested and is ignored. Native/frame hiding, an ancestor replacement,
 * InterfaceParent containers and orphaned roots still hide it. */
int
UITree_NodeOrAncestorDisplayHiddenExceptReplacement(
    struct UITree const* tree,
    int32_t node_index);

/** Set a replacement suppression only when `node_index` still holds the exact
 * incarnation the caller resolved. Returns 1 when the identity was live (also
 * for an idempotent write), 0 for a missing or recycled slot. */
int
UITree_SetReplacementHidden(
    struct UITree* tree,
    int32_t node_index,
    uint32_t incarnation,
    int hidden);

/** Resync timer/key/wheel/resize/sub_change set membership from current hooks.
 *  Call after writing hook slots outside UITree_ApplyRuntimeHook (tests, etc.). */
void
UITree_SyncHookMembership(
    struct UITree* tree,
    int32_t idx);

/** Free runtime_hooks at idx and drop the node from every hook live set. */
void
UITree_FreeHooksAt(
    struct UITree* tree,
    int32_t idx);

/** Live nodes whose component_id high half equals group_id, or NULL if none. */
struct UITreeNodeSet const*
UITree_GroupNodes(
    struct UITree const* tree,
    int group_id);

/** True when the tree holds at least one live node in group_id. */
int
UITree_GroupPresent(
    struct UITree const* tree,
    int group_id);

#ifdef UITREE_NODE_SET_VERIFY
/** Brute-force check that every live set matches a full-array scan. */
void UITree_VerifyLiveSets(struct UITree const* tree);
#endif

#endif
