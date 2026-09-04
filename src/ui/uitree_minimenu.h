#ifndef SRC_UITREE_MINIMENU_H
#define SRC_UITREE_MINIMENU_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Right-click minimenu model (reference Client "Choose Option" menu). Pure
 * data + layout math — population lives in the game layer (rs_minimenu_build)
 * and drawing in the emit walk (UIELEM_BUILTIN_MINIMENU expansion).
 *
 * Layout follows the reference: height = rows * 15 + 21 chrome at the default
 * bold-12 glyph line box (16), menu horizontally centered on the click, rows
 * drawn bottom-to-top so the highest-priority (last after sort) entry is on top.
 */

/* Reference cap: Client.ts addWorldOptions refuses to add past 400 rows
 * (Client.ts:9465, :9560) into a 500-slot array. Ours was 10, which is
 * fewer rows than a single ground tile with five items produces -- the
 * overflow was dropped silently by UIMinimenu_AddOption, so a stack of
 * objs listed only the first few and the rest were unreachable. */
#define UITREE_MINIMENU_MAX_OPTIONS 400
#define UITREE_MINIMENU_OPTION_LEN 128
/** Default bold-12 glyph line box: max(offset_y + glyph_height) == 16. */
#define UITREE_MINIMENU_DEFAULT_LINE_BOX 16
/** Reference OPTIONS_MENU background/title color. */
#define UITREE_MINIMENU_COLOR_BODY 0x5D5447

/**
 * How the popup is sized for the pointer that opened it.
 *
 * DESKTOP is the reference geometry, byte for byte. TOUCH is the same popup
 * with rows a finger can land on: every row band is about twice as tall and
 * the title bar and side insets grow with it, while the TEXT keeps the font's
 * own metrics and sits centred in its band. Both are in CANVAS pixels, so the
 * touch popup still follows the UI scale setting on top -- scaling is the
 * user's lever, this is the finger's.
 */
enum UIMinimenuStyle
{
    UI_MINIMENU_STYLE_DESKTOP = 0,
    UI_MINIMENU_STYLE_TOUCH,
};

/** A chosen row lingers for this long after the tap, fully drawn ... */
#define UITREE_MINIMENU_AFTERIMAGE_HOLD_MS 300
/** ... then fades out over this long. */
#define UITREE_MINIMENU_AFTERIMAGE_FADE_MS 400

/** What a menu row targets. World entity kinds (NPC/PLAYER/SCENERY/TERRAIN/OBJ)
 * come from the pickset; NONE/UI/INV_SLOT are UI/inv builders. */
enum UIMinimenuPickKind
{
    UI_MINIMENU_PICK_NONE = 0,
    UI_MINIMENU_PICK_UI,
    UI_MINIMENU_PICK_INV_SLOT,
    UI_MINIMENU_PICK_NPC,
    UI_MINIMENU_PICK_SCENERY,
    UI_MINIMENU_PICK_TERRAIN,
    UI_MINIMENU_PICK_OBJ,
    UI_MINIMENU_PICK_PLAYER,
    /** A sailing hull (world entity): id = view id, secondary = config op
     * index 0..4 (SAILING_PLAN C5.2). */
    UI_MINIMENU_PICK_WEV,
};

/*
 * Target reference carried by a row. The semantic fields use component ids;
 * app-retained UI rows additionally stamp the exact node incarnation because
 * CS2 CC ops can reclaim and reuse both ids and array slots while a popup is
 * open.
 *  UI:       id = component_id
 *  INV_SLOT: id = component_id of the inv node, secondary = slot,
 *            tertiary = obj_id
 */
struct UIMinimenuPick
{
    enum UIMinimenuPickKind kind;
    int id;
    int secondary_id;
    int tertiary_id;
    int quaternary_id;
    /** World-entity view a TERRAIN pick came out of; 0 = root. Deck tiles
     *  carry the view's own coordinates and resolve against its staging
     *  base at dispatch (the deob adds view.baseX at the menu-op layer). */
    int view_id;
    /** Optional exact UITree occupant retained with a popup row. Component
     * ids and array slots can both be reused while the menu is open. */
    int has_node_identity;
    int32_t node_index;
    uint32_t node_incarnation;
    /** Synthetic engine click delegated by a semantic replacement: its native
     * source remains addressable below replacement tombstones. Cache/script
     * hiding and other native visibility fences still invalidate it. */
    int allow_replacement_hidden;
    /** Synthetic engine click into a subtree a gameframe PLUGIN is not
     * showing -- a sidebar panel behind a shut drawer, a stone the plugin
     * replaced. The press names a component rather than a screen position, so
     * the arranger's presentation is not a reason to drop it; a hide the cache
     * or a script authored still is. @see UITree_NodeOrAncestorDisplayHiddenEx. */
    int allow_frame_hidden;
};

struct UIMinimenuLayout
{
    int line_height;
    int row_stride;
    int header_text_y;
    int header_bar_h;
    int separator_y;
    int option_base_y;
    int chrome_h;
    int hover_above;
    int hover_below;
    int width_pad;
    int click_y_bias;
    int border_inset;
    /** Top of the title's text box, from the popup's top edge. */
    int header_text_top;
    /** Horizontal inset of every text box (title and rows) from the edges. */
    int text_inset_x;
    /** A row's text box, relative to the top of its band (the hit band
     *  UIMinimenu_HitOption tests: option y - hover_above .. + hover_below). */
    int row_text_offset_y;
    int row_text_box_h;
};

/**
 * The row a tap chose, left on screen after the popup has gone.
 *
 * A popup that vanishes on the press edge gives a finger nothing to confirm
 * what it hit -- there is no hover state on a phone to have watched turn
 * yellow. So the chosen row stays where it was, drawn exactly as the popup
 * drew it and boxed in the popup's own border, lingers, and fades.
 */
struct UIMinimenuAfterimage
{
    bool active;
    /** The bordered box: the row's band plus a one-pixel border all round. */
    int x;
    int y;
    int w;
    int h;
    /** The row's text box, as the popup laid it out. */
    int text_x;
    int text_y;
    int text_w;
    int text_h;
    int font_id;
    /** The row's colour at the tap: hovered yellow, or white. */
    int color;
    char text[UITREE_MINIMENU_OPTION_LEN];
    /** Milliseconds since the tap. */
    int cycle_ms;
};

struct UIMinimenuOption
{
    char text[UITREE_MINIMENU_OPTION_LEN];
    /** RevConfigMiniMenuAction value (rev-254 action id scheme, +2000 =
     * deprioritized). Kept as int so ui/ stays leaf. */
    int action;
    /** Config op slot 0..4 this row came from, or -1. */
    int action_index;
    struct UIMinimenuPick pick;
};

struct UIMinimenu
{
    bool visible;
    int x;
    int y;
    int width;
    int height;
    int hovered_option; /* -1 = none */
    /** Cache font id used for measuring and drawing rows (bold-12). */
    int font_id;
    struct UIMinimenuLayout layout;
    struct UIMinimenuOption options[UITREE_MINIMENU_MAX_OPTIONS];
    int option_count;
    /** Outlives `visible`: Hide leaves it alone, only Reset and its own clock
     *  end it. */
    struct UIMinimenuAfterimage afterimage;
};

/** Width callback: pixel width of text in font_id, or <= 0 when unknown. */
typedef int (*UIMinimenuMeasureFn)(void* ud, int font_id, char const* text);

/** The reference (DESKTOP) geometry for a font whose glyph line box is
 *  line_box. */
struct UIMinimenuLayout
UIMinimenu_LayoutFromLineBox(int line_box);

struct UIMinimenuLayout
UIMinimenu_LayoutFromLineBoxStyled(int line_box, enum UIMinimenuStyle style);

void
UIMinimenu_Reset(struct UIMinimenu* menu);

void
UIMinimenu_Hide(struct UIMinimenu* menu);

bool
UIMinimenu_AddOption(
    struct UIMinimenu* menu,
    char const* text,
    int action,
    int action_index,
    struct UIMinimenuPick pick);

/** Reference insertion-order bubble: normal-priority rows (< 1000) sink below
 * high ids (> 1000: Cancel/Examine); rows draw bottom-to-top afterwards. */
void
UIMinimenu_SortPriorityActions(struct UIMinimenu* menu);

/**
 * Where the client's own synthetic action ids start.
 *
 * The reference's action space is small (rev-254 tops out ~1714, ~3714 once
 * deprioritized) and every id in it may carry the +2000 priority bias. A row
 * the CLIENT invents — a dev tool's, never sent to a server — is not a
 * component operation and never carries that bias, so the two rules below must
 * leave it exactly as it was handed in. Without the guard, normalize turned the
 * loc editor's Select row from 500000 into 498000 and its dispatch, an
 * equality test against the constant, silently stopped matching: the row drew,
 * the menu closed, and nothing happened.
 */
#define UITREE_MINIMENU_ACTION_CLIENT_BASE 500000

/** Deprioritize bias (reference +2000): pushes a row below normal entries. */
static inline int
UIMinimenu_ActionDeprioritize(int action)
{
    if( action >= UITREE_MINIMENU_ACTION_CLIENT_BASE )
        return action;
    return action > 1000 ? action : action + 2000;
}

/** Undo the reference's +2000 priority bias before dispatching a row. */
static inline int
UIMinimenu_ActionNormalize(int action)
{
    if( action >= UITREE_MINIMENU_ACTION_CLIENT_BASE )
        return action;
    return action >= 2000 ? action - 2000 : action;
}

/** Compute layout + content width for the current rows. line_box <= 0 falls
 * back to the default glyph line box; measure == NULL falls back to strlen * 6. */
bool
UIMinimenu_PrepareShow(
    struct UIMinimenu const* menu,
    int line_box,
    UIMinimenuMeasureFn measure,
    void* measure_ud,
    struct UIMinimenuLayout* out_layout,
    int* out_content_width);

/** PrepareShow in a chosen style; PrepareShow is this in DESKTOP. */
bool
UIMinimenu_PrepareShowStyled(
    struct UIMinimenu const* menu,
    int line_box,
    enum UIMinimenuStyle style,
    UIMinimenuMeasureFn measure,
    void* measure_ud,
    struct UIMinimenuLayout* out_layout,
    int* out_content_width);

int
UIMinimenu_Height(struct UIMinimenuLayout const* layout, int option_count);

/** Text baseline-row y of option i (bottom-to-top: 0 = bottom row). */
int
UIMinimenu_OptionY(struct UIMinimenu const* menu, int option_index);

/** The text box a row is drawn in, canvas pixels. One formula for the emit
 *  and for the afterimage, so the row it leaves behind is the row it drew. */
void
UIMinimenu_RowTextBox(
    struct UIMinimenu const* menu,
    int option_index,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

/** Place at the click (centered horizontally, biased up), clamped inside the
 * viewport, and mark visible. */
void
UIMinimenu_ShowAt(
    struct UIMinimenu* menu,
    struct UIMinimenuLayout layout,
    int content_width,
    int click_x,
    int click_y,
    int viewport_w,
    int viewport_h);

/** >= 0 option row; -1 inside chrome / margin; -2 outside (close). */
int
UIMinimenu_HitOption(struct UIMinimenu const* menu, int click_x, int click_y);

/** Returns true when the hovered row changed. */
bool
UIMinimenu_UpdateHover(struct UIMinimenu* menu, int mouse_x, int mouse_y);

/**
 * Leave the chosen row on screen. Call while the popup is still VISIBLE --
 * the box is read off its geometry -- and before the app hides it.
 */
void
UIMinimenu_AfterimageShow(struct UIMinimenu* menu, int option_index);

void
UIMinimenu_AfterimageTick(struct UIMinimenu* menu, int delta_ms);

bool
UIMinimenu_AfterimageActive(struct UIMinimenu const* menu);

/** Transparency 0..255 of the afterimage right now: 0 while it lingers,
 *  climbing through the fade, 255 once it is gone. */
int
UIMinimenu_AfterimageTrans(struct UIMinimenu const* menu);

#endif
